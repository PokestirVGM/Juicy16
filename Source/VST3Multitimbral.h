//
// Per-channel VST3 multitimbral support (the HALion-style "units" mechanism).
//
// Stock JUCE's VST3 wrapper exposes IUnitInfo hardwired to a single root unit and
// a single global program list, so hosts have no way to associate MIDI channels
// with per-channel programs. This extension shadows the wrapper's IUnitInfo via
// JUCE's official VST3ClientExtensions::queryIEditController hook (user-provided
// interfaces are consulted BEFORE the wrapper's own — see extractResult() in
// juce_audio_plugin_client_VST3.cpp) with:
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
// The extension is only ever queried by the VST3 wrapper — it is inert (never
// called) in the AU/Standalone/VST2 builds even though it's compiled in.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

class JuicyVST3Extensions : public juce::VST3ClientExtensions
{
public:
    JuicyVST3Extensions();
    ~JuicyVST3Extensions() override;

    int32_t queryIEditController (const Steinberg::TUID, void** obj) override;
    void setIComponentHandler (Steinberg::FUnknown*) override;

    // Message thread: replace the shared program list's names (index = GM program
    // number 0..127) and notify the host (IUnitHandler) so it re-reads the list.
    // Called whenever a soundfont/DLS (re)load changes the available presets.
    void setProgramNames (const juce::StringArray& names);

private:
    class UnitInfoImpl;            // the COM object; defined in the .cpp (SDK types)
    UnitInfoImpl* unitInfo;        // we hold one COM reference for our lifetime
    Steinberg::FUnknown* unitHandler{nullptr}; // host's IUnitHandler (held with one ref)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicyVST3Extensions)
};
