// Per-channel VST3 program routing (HALion-style units).
//
// The pinned wrapper patch owns IUnitInfo on the component and controller: a
// root unit plus units "Ch 1".."Ch 16" (IDs hashed from the "chUnitN" parameter
// groups, so each progChN lives in its channel's unit), one shared 128-entry
// program list, and getUnitByBus mapping MIDI channel N to unit N, which Cubase
// uses to route Program Change. This extension supplies the program names and
// forwards list-change notifications. SDK-free header; inert outside VST3.

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

class JuicyVST3Extensions : public juce::VST3ClientExtensions
{
public:
    JuicyVST3Extensions();
    ~JuicyVST3Extensions() override;

    void setIComponentHandler (Steinberg::FUnknown*) override;

    // Message thread. Replaces the program names (index = GM program) and tells
    // the host to re-read the list. Called on every bank load.
    void setProgramNames (const juce::StringArray& names);

private:
    Steinberg::FUnknown* unitHandler{nullptr};  // host's IUnitHandler, one ref held

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicyVST3Extensions)
};
