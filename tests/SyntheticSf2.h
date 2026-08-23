#pragma once

// Minimal SF2 writer for offline tests.
//
// The private corpus has no bank that exercises cross-bank selection: the macOS
// system DLS only reaches bank 1, and the pinned SF3 fixture is bank 0 plus
// percussion. Rather than acquire and redistribute another bank, the bank-select
// tests synthesise one whose presets are audibly distinguishable — each preset
// plays a looped sine at its own frequency with scale tuning disabled, so the
// rendered pitch identifies which (bank, preset) FluidSynth actually selected.

#include "../JuceLibraryCode/JuceHeader.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace SyntheticSf2 {

struct PresetSpec {
    int bank{0};
    int preset{0};
    // Rendered pitch of this preset, in Hz. Must divide the fixture sample rate
    // exactly so the looped period is an integer number of frames.
    double frequency{440.0};
    juce::String name;
};

constexpr int sampleRate{44100};
// SF2 requires at least 46 zero frames after each sample's data.
constexpr int interSampleGap{46};

namespace detail {

inline void writeName(juce::MemoryOutputStream& out, const juce::String& name, int bytes)
{
    const auto utf8{name.toRawUTF8()};
    const int length{juce::jmin(static_cast<int>(std::strlen(utf8)), bytes - 1)};
    out.write(utf8, static_cast<size_t>(length));
    for (int i = length; i < bytes; ++i)
        out.writeByte(0);
}

// Writes a RIFF/LIST chunk header, the payload, and the pad byte an odd payload
// needs. Sizes are known up front because every payload is built in memory first.
inline void writeChunk(juce::MemoryOutputStream& out,
                       const char* fourcc,
                       const juce::MemoryBlock& payload)
{
    out.write(fourcc, 4);
    out.writeInt(static_cast<int>(payload.getSize()));
    out.write(payload.getData(), payload.getSize());
    if ((payload.getSize() & 1u) != 0)
        out.writeByte(0);
}

inline juce::MemoryBlock listChunk(const char* listType, const juce::MemoryBlock& body)
{
    juce::MemoryBlock block;
    juce::MemoryOutputStream out{block, false};
    out.write(listType, 4);
    out.write(body.getData(), body.getSize());
    out.flush();
    return block;
}

} // namespace detail

// Builds a complete SF2 image. One instrument and one sample per preset, each
// instrument a single zone that loops its sample continuously with scaleTuning 0,
// so every key sounds the sample's own frequency.
inline juce::MemoryBlock build(const std::vector<PresetSpec>& presets)
{
    jassert(!presets.empty());

    // ---- sample data -----------------------------------------------------
    struct SampleRegion { int start{0}; int end{0}; int loopStart{0}; int loopEnd{0}; };
    std::vector<SampleRegion> regions;
    std::vector<int16_t> pcm;

    for (const auto& spec : presets) {
        const int period{static_cast<int>(std::lround(sampleRate / spec.frequency))};
        jassert(period > 1);
        int cycles{1};
        while (period * cycles < 1024)
            ++cycles;
        // Eight frames of head- and tailroom keep the loop points clear of the
        // sample bounds, which FluidSynth's sample validation insists on.
        const int frames{period * cycles + 16};
        const int start{static_cast<int>(pcm.size())};

        for (int i = 0; i < frames; ++i) {
            const double phase{2.0 * juce::MathConstants<double>::pi * i / period};
            pcm.push_back(static_cast<int16_t>(std::lround(24000.0 * std::sin(phase))));
        }
        regions.push_back({start, start + frames, start + 8, start + 8 + period * cycles});
        pcm.insert(pcm.end(), static_cast<size_t>(interSampleGap), 0);
    }

    // ---- INFO ------------------------------------------------------------
    juce::MemoryBlock infoBody;
    {
        juce::MemoryOutputStream info{infoBody, false};
        juce::MemoryBlock ifil;
        {
            juce::MemoryOutputStream v{ifil, false};
            v.writeShort(2);
            v.writeShort(1);
            v.flush();
        }
        detail::writeChunk(info, "ifil", ifil);

        const auto zstr{[](const char* text) {
            juce::MemoryBlock block;
            juce::MemoryOutputStream out{block, false};
            out.write(text, std::strlen(text) + 1);
            out.flush();
            return block;
        }};
        detail::writeChunk(info, "isng", zstr("EMU8000"));
        detail::writeChunk(info, "INAM", zstr("Juicy16 bank-select fixture"));
        info.flush();
    }

    // ---- sdta ------------------------------------------------------------
    juce::MemoryBlock sdtaBody;
    {
        juce::MemoryOutputStream sdta{sdtaBody, false};
        juce::MemoryBlock smpl;
        {
            juce::MemoryOutputStream out{smpl, false};
            for (const auto frame : pcm)
                out.writeShort(frame);
            out.flush();
        }
        detail::writeChunk(sdta, "smpl", smpl);
        sdta.flush();
    }

    // ---- pdta ------------------------------------------------------------
    const int count{static_cast<int>(presets.size())};
    juce::MemoryBlock pdtaBody;
    {
        juce::MemoryOutputStream pdta{pdtaBody, false};

        juce::MemoryBlock phdr;
        {
            juce::MemoryOutputStream out{phdr, false};
            for (int i = 0; i < count; ++i) {
                detail::writeName(out, presets[static_cast<size_t>(i)].name, 20);
                out.writeShort(static_cast<int16_t>(presets[static_cast<size_t>(i)].preset));
                out.writeShort(static_cast<int16_t>(presets[static_cast<size_t>(i)].bank));
                out.writeShort(static_cast<int16_t>(i)); // one zone each
                out.writeInt(0);
                out.writeInt(0);
                out.writeInt(0);
            }
            detail::writeName(out, "EOP", 20);
            out.writeShort(0);
            out.writeShort(0);
            out.writeShort(static_cast<int16_t>(count));
            out.writeInt(0);
            out.writeInt(0);
            out.writeInt(0);
            out.flush();
        }
        detail::writeChunk(pdta, "phdr", phdr);

        juce::MemoryBlock pbag;
        {
            juce::MemoryOutputStream out{pbag, false};
            for (int i = 0; i <= count; ++i) {
                out.writeShort(static_cast<int16_t>(i)); // one generator per zone
                out.writeShort(0);
            }
            out.flush();
        }
        detail::writeChunk(pdta, "pbag", pbag);

        juce::MemoryBlock pmod;
        {
            juce::MemoryOutputStream out{pmod, false};
            for (int i = 0; i < 10; ++i)
                out.writeByte(0);
            out.flush();
        }
        detail::writeChunk(pdta, "pmod", pmod);

        juce::MemoryBlock pgen;
        {
            juce::MemoryOutputStream out{pgen, false};
            for (int i = 0; i < count; ++i) {
                out.writeShort(41); // instrument
                out.writeShort(static_cast<int16_t>(i));
            }
            out.writeShort(0);
            out.writeShort(0);
            out.flush();
        }
        detail::writeChunk(pdta, "pgen", pgen);

        juce::MemoryBlock inst;
        {
            juce::MemoryOutputStream out{inst, false};
            for (int i = 0; i < count; ++i) {
                detail::writeName(out, presets[static_cast<size_t>(i)].name + " inst", 20);
                out.writeShort(static_cast<int16_t>(i));
            }
            detail::writeName(out, "EOI", 20);
            out.writeShort(static_cast<int16_t>(count));
            out.flush();
        }
        detail::writeChunk(pdta, "inst", inst);

        constexpr int gensPerInstrumentZone{3};
        juce::MemoryBlock ibag;
        {
            juce::MemoryOutputStream out{ibag, false};
            for (int i = 0; i <= count; ++i) {
                out.writeShort(static_cast<int16_t>(i * gensPerInstrumentZone));
                out.writeShort(0);
            }
            out.flush();
        }
        detail::writeChunk(pdta, "ibag", ibag);

        juce::MemoryBlock imod;
        {
            juce::MemoryOutputStream out{imod, false};
            for (int i = 0; i < 10; ++i)
                out.writeByte(0);
            out.flush();
        }
        detail::writeChunk(pdta, "imod", imod);

        juce::MemoryBlock igen;
        {
            juce::MemoryOutputStream out{igen, false};
            for (int i = 0; i < count; ++i) {
                out.writeShort(54); // sampleModes
                out.writeShort(1);  // loop continuously
                out.writeShort(56); // scaleTuning
                out.writeShort(0);  // every key sounds the sample's own pitch
                out.writeShort(53); // sampleID, required last
                out.writeShort(static_cast<int16_t>(i));
            }
            out.writeShort(0);
            out.writeShort(0);
            out.flush();
        }
        detail::writeChunk(pdta, "igen", igen);

        juce::MemoryBlock shdr;
        {
            juce::MemoryOutputStream out{shdr, false};
            for (int i = 0; i < count; ++i) {
                const auto& region{regions[static_cast<size_t>(i)]};
                detail::writeName(out, presets[static_cast<size_t>(i)].name + " smpl", 20);
                out.writeInt(region.start);
                out.writeInt(region.end);
                out.writeInt(region.loopStart);
                out.writeInt(region.loopEnd);
                out.writeInt(sampleRate);
                out.writeByte(60);          // original pitch
                out.writeByte(0);           // pitch correction
                out.writeShort(0);          // sample link
                out.writeShort(1);          // monoSample
            }
            detail::writeName(out, "EOS", 20);
            for (int i = 0; i < 5; ++i)
                out.writeInt(0);
            out.writeByte(0);
            out.writeByte(0);
            out.writeShort(0);
            out.writeShort(0);
            out.flush();
        }
        detail::writeChunk(pdta, "shdr", shdr);
        pdta.flush();
    }

    juce::MemoryBlock body;
    {
        juce::MemoryOutputStream out{body, false};
        out.write("sfbk", 4);
        detail::writeChunk(out, "LIST", detail::listChunk("INFO", infoBody));
        detail::writeChunk(out, "LIST", detail::listChunk("sdta", sdtaBody));
        detail::writeChunk(out, "LIST", detail::listChunk("pdta", pdtaBody));
        out.flush();
    }

    juce::MemoryBlock file;
    {
        juce::MemoryOutputStream out{file, false};
        detail::writeChunk(out, "RIFF", body);
        out.flush();
    }
    return file;
}

inline bool write(const juce::File& destination, const std::vector<PresetSpec>& presets)
{
    const auto image{build(presets)};
    destination.deleteFile();
    return destination.replaceWithData(image.getData(), image.getSize());
}

} // namespace SyntheticSf2
