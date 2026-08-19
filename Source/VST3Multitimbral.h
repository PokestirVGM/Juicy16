//
// Per-channel VST3 multitimbral support (the HALion-style "units" mechanism).
//
// Juicy16's pinned wrapper patch owns IUnitInfo on both the VST3 component and
// controller. This extension supplies the runtime program names and forwards
// program-list refresh notifications without claiming IUnitInfo itself. The
// wrapper exposes:
//   - a root unit plus 16 child units "Ch 1".."Ch 16", whose unit IDs use the
//     wrapper's own group-hash formula over our parameter-group IDs
//     ("chUnit1".."chUnit16"), so each channel's progChN parameter lives inside
//     its channel's unit;
//   - one shared program list (128 GM slots, names from the loaded font) attached
//     to every channel unit;
//   - getUnitByBus mapping MIDI input channel N -> unit N, which is exactly what
//     hosts like Cubase use to route per-channel MIDI Program Change to the
//     corresponding unit's program.
//
// This header stays free of VST3 SDK includes; all SDK types live in the .cpp.
// The extension is inert in the AU/Standalone builds even though it is compiled
// into the shared target.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

class JuicyVST3Extensions : public juce::VST3ClientExtensions
{
public:
    JuicyVST3Extensions();
    ~JuicyVST3Extensions() override;

    void setIComponentHandler (Steinberg::FUnknown*) override;

    // Message thread: replace the shared program list's names (index = GM program
    // number 0..127) and notify the host (IUnitHandler) so it re-reads the list.
    // Called whenever a DLS/SoundFont (re)load changes the available presets.
    void setProgramNames (const juce::StringArray& names);

private:
    Steinberg::FUnknown* unitHandler{nullptr}; // host's IUnitHandler (held with one ref)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicyVST3Extensions)
};
