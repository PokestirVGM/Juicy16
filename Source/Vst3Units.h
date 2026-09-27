// Static VST3 unit structure served by the vendored wrapper's IUnitInfo.
//
// Cubase queries IUnitInfo right after creating the controller, before the
// component connection. Stock JUCE answers "1 unit, no program lists" there, the
// host caches it, and Program Change is discarded. The structure is static, so
// the patched wrapper serves it identically from the first query.

#pragma once

// Deliberately excludes JuceHeader.h: the wrapper TU includes this and clashes
// with its using-declarations. Include a JUCE header first in a new TU.

namespace juicysf::vst3units {

constexpr int kNumMidiChannels = 16;
constexpr int kNumPrograms = 128;  // GM programs; progChN spans 0..127
constexpr int kProgramListId = 0x50524F47; // 'PROG'

// Frozen host-session IDs: JUCE 8.0.14 group hashes of "chUnit1".."chUnit16",
// pinned so a JUCE hashing change cannot alter them.
constexpr int kChannelUnitIds[kNumMidiChannels]{
    0x2B6251C8, 0x2B6251C9, 0x2B6251CA, 0x2B6251CB,
    0x2B6251CC, 0x2B6251CD, 0x2B6251CE, 0x2B6251CF,
    0x2B6251D0, 0x40E7E768, 0x40E7E769, 0x40E7E76A,
    0x40E7E76B, 0x40E7E76C, 0x40E7E76D, 0x40E7E76E
};

// Frozen ParamIDs of progCh1..progCh16. The wrapper also uses them to turn
// program-parameter queues back into timestamped Program Change events.
constexpr unsigned int kProgramParamIds[kNumMidiChannels]{
    0x6D8E6EB2u, 0x6D8E6EB3u, 0x6D8E6EB4u, 0x6D8E6EB5u,
    0x6D8E6EB6u, 0x6D8E6EB7u, 0x6D8E6EB8u, 0x6D8E6EB9u,
    0x6D8E6EBAu, 0x443F67BEu, 0x443F67BFu, 0x443F67C0u,
    0x443F67C1u, 0x443F67C2u, 0x443F67C3u, 0x443F67C4u
};

inline int programChannelForParamId (unsigned int paramId)
{
    for (int channel = 0; channel < kNumMidiChannels; ++channel)
        if (kProgramParamIds[channel] == paramId)
            return channel;
    return -1;
}

// Must match the wrapper's unit-ID derivation for parameter groups
// (hashCode & 0x7fffffff of "chUnitN").
inline int unitIdForChannel (int chZeroBased)
{
    jassert (chZeroBased >= 0 && chZeroBased < kNumMidiChannels);
    return kChannelUnitIds[chZeroBased];
}

// Program names from bank 0. Message thread writes, host UI thread reads.
void setProgramNames (const juce::StringArray& names);
juce::String programNameForIndex (int index); // falls back to "Program N"

} // namespace juicysf::vst3units
