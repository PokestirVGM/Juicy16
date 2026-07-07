//
// Shared VST3 multitimbral unit structure — single source of truth used by BOTH
// the plugin's own IUnitInfo implementation (VST3Multitimbral.cpp) and the
// vendored JUCE VST3 wrapper's controller-side IUnitInfo methods.
//
// Why the wrapper needs it: hosts (Cubase) interrogate IUnitInfo immediately
// after creating the edit controller, BEFORE the component connection that gives
// JUCE's controller its AudioProcessor pointer — which is also the moment the
// VST3ClientExtensions shadow becomes reachable. Stock JUCE answers those early
// queries with "1 unit, no program lists" (with a jassertfalse acknowledging the
// situation), the host caches that forever, and discards all MIDI Program
// Change. The unit structure is static, so it can and must be served without a
// processor, identically, from the first query onward.
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

// MUST match the JUCE VST3 wrapper's unit-ID derivation for parameter groups
// (group->getID().hashCode() & 0x7fffffff) applied to our group IDs
// "chUnit1".."chUnit16" from createParameterLayout() — that is what places each
// progChN parameter inside its channel's unit.
inline int unitIdForChannel (int chZeroBased)
{
    return ("chUnit" + juce::String (chZeroBased + 1)).hashCode() & 0x7fffffff;
}

// program-name store (names from the loaded font's bank 0; message thread writes,
// host UI thread reads). Implemented in VST3Multitimbral.cpp.
void setProgramNames (const juce::StringArray& names);
juce::String programNameForIndex (int index); // falls back to "Program N"

} // namespace juicysf::vst3units
