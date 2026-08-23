# pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "FluidSynthModel.h"

using namespace std;
using SliderAttachment = AudioProcessorValueTreeState::SliderAttachment;

// Volume and pan for the selected MIDI channel, plus the plugin's master output
// trim.
//
// Volume and pan are ordinary MIDI controllers (CC7/CC10), so these two sliders
// behave exactly like the per-row instrument dropdowns: what you set here is a
// starting point, and the next CC7/CC10 the channel receives replaces it and
// moves the slider. Output level is not a MIDI controller and is not per
// channel — it is the plugin's own gain staging.
class SlidersComponent : public Component
{
public:
    SlidersComponent(
        AudioProcessorValueTreeState& state,
        FluidSynthModel& model);
    ~SlidersComponent() override;

    void resized() override;

    const int getDesiredWidth();

    // Called when a mapped CC arrives for the selected channel, so the slider
    // follows incoming MIDI without echoing it back to the engine.
    void acceptMidiControlEvent(int controller, int value);

private:
    std::function<void()> makeSliderListener(Slider& slider, int controller);

    FluidSynthModel& fluidSynthModel;

    GroupComponent channelGroup;

    Slider volumeSlider;
    Label volumeLabel;
    unique_ptr<SliderAttachment> volumeSliderAttachment;

    Slider panSlider;
    Label panLabel;
    unique_ptr<SliderAttachment> panSliderAttachment;

    GroupComponent masterGroup;

    Slider outputLevelSlider;
    Label outputLevelLabel;
    unique_ptr<SliderAttachment> outputLevelSliderAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlidersComponent)
};
