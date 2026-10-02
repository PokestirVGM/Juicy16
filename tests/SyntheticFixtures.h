#pragma once

// Generated tones only: no system banks, downloaded samples, or private files.
#include "SyntheticSf2.h"
#include "SyntheticDls.h"

namespace SyntheticFixtures {
inline std::size_t findChunk(const juce::MemoryBlock& bytes, const char* name, std::size_t offset) {
    const auto* data = static_cast<const juce::uint8*>(bytes.getData());
    while (offset + 8 <= bytes.getSize()) {
        const auto size = static_cast<std::size_t>(juce::ByteOrder::littleEndianInt(data + offset + 4));
        if (size > bytes.getSize() - offset - 8) break;
        if (std::memcmp(data + offset, name, 4) == 0) return offset;
        offset += 8 + size + (size & 1);
    }
    return bytes.getSize();
}

inline bool write(const juce::File& directory) {
    if (directory.createDirectory().failed()) return false;
    std::vector<SyntheticSf2::PresetSpec> presets;
    for (int program = 0; program < 128; ++program)
        presets.push_back({0, program, 441.0, "Synthetic " + juce::String(program)});
    presets.push_back({128, 0, 220.5, "Synthetic drums"});
    if (!SyntheticSf2::write(directory.getChildFile("general-midi.sf2"), presets)) return false;
    const auto dls = SyntheticDls::buildGeneralMidi();
    if (!directory.getChildFile("general-midi.dls").replaceWithData(dls.getData(), dls.getSize())) return false;

    // SF3 stores individual Ogg streams with byte offsets in the sample header;
    // loop points still refer to decoded frames. Use the exact SF2 sine first.
    const auto sf2 = SyntheticSf2::build({{0, 0, 441.0, "Compressed sine"}});
    const auto* data = static_cast<const juce::uint8*>(sf2.getData());
    const auto infoSize = static_cast<std::size_t>(juce::ByteOrder::littleEndianInt(data + 16));
    const auto sdtaOffset = 20 + infoSize + (infoSize & 1);
    const auto sdtaSize = static_cast<std::size_t>(juce::ByteOrder::littleEndianInt(data + sdtaOffset + 4));
    const auto pdtaOffset = sdtaOffset + 8 + sdtaSize + (sdtaSize & 1);
    juce::MemoryBlock info{data + 20, infoSize};
    juce::MemoryBlock pdta{data + pdtaOffset + 8, sf2.getSize() - pdtaOffset - 8};
    const auto ifil = findChunk(info, "ifil", 4);
    const auto shdr = findChunk(pdta, "shdr", 4);
    if (ifil == info.getSize() || shdr == pdta.getSize()) return false;
    auto* infoData = static_cast<juce::uint8*>(info.getData());
    auto* header = static_cast<juce::uint8*>(pdta.getData()) + shdr + 8;
    infoData[ifil + 8] = 3;
    infoData[ifil + 10] = 0;
    const int frames = static_cast<int>(juce::ByteOrder::littleEndianInt(header + 24));
    juce::AudioBuffer<float> samples{1, frames};
    const auto* pcm = data + sdtaOffset + 20;
    for (int i = 0; i < frames; ++i)
        samples.setSample(0, i, static_cast<float>(static_cast<juce::int16>(
            juce::ByteOrder::littleEndianShort(pcm + 2 * i))) / 32768.0f);
    juce::MemoryBlock ogg;
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream>(ogg, false);
    juce::OggVorbisAudioFormat format;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions{}
        .withSampleRate(SyntheticSf2::sampleRate).withNumChannels(1).withBitsPerSample(16).withQualityOptionIndex(5));
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(samples, 0, frames)) return false;
    writer.reset();
    const auto put32 = [header](int offset, std::size_t value) {
        for (int byte = 0; byte < 4; ++byte) header[offset + byte] = static_cast<juce::uint8>(value >> (8 * byte));
    };
    put32(20, 0);
    put32(24, ogg.getSize());
    header[44] = 0x11; // Mono Ogg Vorbis sample.
    juce::MemoryOutputStream sdta;
    SyntheticSf2::detail::writeChunk(sdta, "smpl", ogg);
    juce::MemoryOutputStream body;
    body.write("sfbk", 4);
    SyntheticSf2::detail::writeChunk(body, "LIST", info);
    SyntheticSf2::detail::writeChunk(body, "LIST", SyntheticSf2::detail::listChunk("sdta", sdta.getMemoryBlock()));
    SyntheticSf2::detail::writeChunk(body, "LIST", pdta);
    juce::MemoryOutputStream output;
    SyntheticSf2::detail::writeChunk(output, "RIFF", body.getMemoryBlock());
    return directory.getChildFile("compressed.sf3").replaceWithData(output.getData(), output.getDataSize());
}
} // namespace SyntheticFixtures
