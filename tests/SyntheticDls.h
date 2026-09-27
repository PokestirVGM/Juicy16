#pragma once

// Minimal DLS writer for offline tests.
//
// Builds the shape of an SF2-converted stereo bank such as AbyssBank: one
// instrument whose two regions play the same looped sine, one hard left and one
// hard right, each with its own narrow CC10->pan connection. The static region
// pans and the bank's CC10 depth are the inputs the DLS pan extension acts on.

#include "../JuceLibraryCode/JuceHeader.h"

#include <cmath>
#include <cstdint>

namespace SyntheticDls {

namespace detail {

inline void u16(juce::MemoryOutputStream& out, int value) { out.writeShort(static_cast<short>(value)); }
inline void u32(juce::MemoryOutputStream& out, std::int64_t value) { out.writeInt(static_cast<int>(value)); }

inline juce::MemoryBlock chunk(const char* id, const juce::MemoryBlock& body)
{
    juce::MemoryOutputStream out;
    out.write(id, 4);
    u32(out, static_cast<std::int64_t>(body.getSize()));
    out.write(body.getData(), body.getSize());
    if (body.getSize() % 2 != 0)
        out.writeByte(0);
    return out.getMemoryBlock();
}

inline juce::MemoryBlock list(const char* type, const juce::MemoryBlock& body, const char* id = "LIST")
{
    juce::MemoryOutputStream inner;
    inner.write(type, 4);
    inner.write(body.getData(), body.getSize());
    return chunk(id, inner.getMemoryBlock());
}

inline juce::MemoryBlock join(std::initializer_list<juce::MemoryBlock> parts)
{
    juce::MemoryOutputStream out;
    for (const auto& part : parts)
        out.write(part.getData(), part.getSize());
    return out.getMemoryBlock();
}

// Unity note 69, forward loop over the whole sample.
inline juce::MemoryBlock wsmp(int frames)
{
    juce::MemoryOutputStream out;
    u32(out, 20); u16(out, 69); u16(out, 0); u32(out, 0); u32(out, 0); u32(out, 1);
    u32(out, 16); u32(out, 0); u32(out, 0); u32(out, frames);
    return chunk("wsmp", out.getMemoryBlock());
}

// Static pan (tenths of a percent) plus the bank's own CC10 -> pan depth.
inline juce::MemoryBlock region(int staticPan, int cc10Depth, int frames)
{
    juce::MemoryOutputStream rgnh;
    u16(rgnh, 0); u16(rgnh, 127); u16(rgnh, 0); u16(rgnh, 127); u16(rgnh, 0); u16(rgnh, 0);
    juce::MemoryOutputStream wlnk;
    u16(wlnk, 0); u16(wlnk, 0); u32(wlnk, 1); u32(wlnk, 0);
    juce::MemoryOutputStream art;
    u32(art, 8); u32(art, 2);
    u16(art, 0x0000); u16(art, 0x0000); u16(art, 0x0004); u16(art, 0x0000);
    u32(art, static_cast<std::int64_t>(staticPan) * 65536);
    u16(art, 0x008a); u16(art, 0x0000); u16(art, 0x0004); u16(art, 0x4000);
    u32(art, static_cast<std::int64_t>(cc10Depth) * 65536);
    return list("rgn ", join({chunk("rgnh", rgnh.getMemoryBlock()), wsmp(frames),
                              chunk("wlnk", wlnk.getMemoryBlock()),
                              list("lart", chunk("art2", art.getMemoryBlock()))}));
}

} // namespace detail

constexpr int sampleRate{44100};

// Stereo-pair instrument at bank 0, program 0: a 441 Hz sine in both regions.
inline juce::MemoryBlock buildStereoPair(int cc10Depth = 254)
{
    using namespace detail;
    constexpr int frames{100};
    juce::MemoryOutputStream pcm;
    for (int i = 0; i < frames; ++i)
        pcm.writeShort(static_cast<short>(
            std::lround(16000.0 * std::sin(2.0 * juce::MathConstants<double>::pi * i / frames))));
    juce::MemoryOutputStream fmt;
    u16(fmt, 1); u16(fmt, 1); u32(fmt, sampleRate); u32(fmt, sampleRate * 2); u16(fmt, 2); u16(fmt, 16);
    const auto wave{list("wave", join({chunk("fmt ", fmt.getMemoryBlock()), wsmp(frames),
                                       chunk("data", pcm.getMemoryBlock())}))};

    juce::MemoryOutputStream insh;
    u32(insh, 2); u32(insh, 0); u32(insh, 0);
    const auto instrument{list("ins ", join({chunk("insh", insh.getMemoryBlock()),
        list("lrgn", join({region(-500, cc10Depth, frames), region(500, cc10Depth, frames)}))}))};

    juce::MemoryOutputStream colh, ptbl;
    u32(colh, 1);
    u32(ptbl, 8); u32(ptbl, 1); u32(ptbl, 0);
    return list("DLS ", join({chunk("colh", colh.getMemoryBlock()), list("lins", instrument),
                              chunk("ptbl", ptbl.getMemoryBlock()), list("wvpl", wave)}),
                "RIFF");
}

} // namespace SyntheticDls
