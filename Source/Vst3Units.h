//
// Shared VST3 multitimbral unit structure used by the vendored JUCE VST3
// wrapper's component-side and controller-side IUnitInfo methods.
//
// Why the wrapper needs it: hosts (Cubase) interrogate IUnitInfo immediately
// after creating the edit controller, BEFORE the component connection that gives
// JUCE's controller its AudioProcessor pointer. Stock JUCE answers those early
// queries with "1 unit, no program lists" (with a jassertfalse acknowledging the
// situation), the host caches that forever, and discards all MIDI Program
// Change. The unit structure is static, so the patched wrapper owns IUnitInfo on
// both VST3 objects and serves it identically from the first query onward.
//
// This header is SDK-free (plain ints + juce types) so it can be included from
// both the shared code and the wrapper TU.
//

#pragma once

// NOTE: deliberately does NOT include JuceHeader.h — this header is also included
// from inside the vendored VST3 wrapper TU, where JuceHeader's project-level
// using-declarations clash with the wrapper's own scope. Every includer (the
// wrapper, VST3Multitimbral.cpp) already has the juce_core types in scope; if you
// include this from a fresh TU, include a JUCE header first.

namespace juicysf::vst3units {

constexpr int kNumMidiChannels = 16;
constexpr int kNumPrograms = 128;      // GM program numbers; progChN params are 0..127
constexpr int kProgramListId = 0x50524F47; // 'PROG'

// Frozen Beta 1 host-session identifiers. These are the JUCE 8.0.14 group-ID
// hashes for "chUnit1".."chUnit16", recorded as constants so a future JUCE
// hashing change cannot silently alter the IUnitInfo side of the contract.
constexpr int kChannelUnitIds[kNumMidiChannels]{
    0x2B6251C8, 0x2B6251C9, 0x2B6251CA, 0x2B6251CB,
    0x2B6251CC, 0x2B6251CD, 0x2B6251CE, 0x2B6251CF,
    0x2B6251D0, 0x40E7E768, 0x40E7E769, 0x40E7E76A,
    0x40E7E76B, 0x40E7E76C, 0x40E7E76D, 0x40E7E76E
};

// Frozen JUCE VST3 ParamIDs for progCh1..progCh16. Besides protecting session
// compatibility, the vendored wrapper uses these to retain every automation
// point's sample offset: a program-parameter queue is converted back into
// timestamped MIDI Program Change events instead of being collapsed to the
// queue's final value at block start.
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

// MUST match the JUCE VST3 wrapper's unit-ID derivation for parameter groups
// (group->getID().hashCode() & 0x7fffffff) applied to our group IDs
// "chUnit1".."chUnit16" from createParameterLayout() — that is what places each
// progChN parameter inside its channel's unit.
inline int unitIdForChannel (int chZeroBased)
{
    jassert (chZeroBased >= 0 && chZeroBased < kNumMidiChannels);
    return kChannelUnitIds[chZeroBased];
}

// program-name store (names from the loaded font's bank 0; message thread writes,
// host UI thread reads). Implemented in VST3Multitimbral.cpp.
void setProgramNames (const juce::StringArray& names);
juce::String programNameForIndex (int index); // falls back to "Program N"

} // namespace juicysf::vst3units
