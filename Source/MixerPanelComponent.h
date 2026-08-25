//
// The right-hand panel: the plugin's global controls, kept apart from the
// channel rack so they read as global rather than as a seventeenth channel.
//
// Master output trim lives here with room for its label and its value, which is
// the defect Phase 9 records against the old 50px "Master" group box. The reverb
// section (Phase 10) lands between master and the bank summary.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

using namespace std;
using SliderAttachment = AudioProcessorValueTreeState::SliderAttachment;

class MixerPanelComponent : public Component,
                            private ValueTree::Listener
{
public:
    explicit MixerPanelComponent(AudioProcessorValueTreeState& state);
    ~MixerPanelComponent() override;

    void paint(Graphics&) override;
    void resized() override;
    // Every token colour is resolved here rather than in the constructor. The
    // panel is built as an editor member BEFORE the editor installs its
    // LookAndFeel, so a constructor findColour asks the default one, which has
    // never heard of Juicy16's ColourIds: it asserts and returns black.
    void lookAndFeelChanged() override;

private:
    void valueTreePropertyChanged(ValueTree&, const Identifier&) override;
    void valueTreeChildAdded(ValueTree&, ValueTree&) override {}
    void valueTreeChildRemoved(ValueTree&, ValueTree&, int) override {}
    void valueTreeChildOrderChanged(ValueTree&, int, int) override {}
    void valueTreeParentChanged(ValueTree&) override {}
    void valueTreeRedirected(ValueTree&) override {}

    void syncOutputLevelReadout();
    void syncBankSummary();

    AudioProcessorValueTreeState& valueTreeState;

    Label masterHeading;
    Slider outputLevelSlider;
    Label outputLevelValue;
    Label outputLevelUnit;
    unique_ptr<SliderAttachment> outputLevelSliderAttachment;

    Label reverbHeading;
    juce::ToggleButton reverbEnable;
    unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> reverbEnableAttachment;
    juce::ComboBox reverbProfile;
    unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> reverbProfileAttachment;
    // size, damping, width, level - one knob and one caption each
    juce::OwnedArray<Slider> reverbKnobs;
    juce::OwnedArray<Label> reverbLabels;
    juce::OwnedArray<SliderAttachment> reverbAttachments;

    Label bankHeading;
    Label bankName;
    Label bankDetail;

    // y of the divider under the master block, set in resized() and drawn in
    // paint() so both agree without a second layout pass.
    int masterDividerY{0};
    int bankDividerY{0};
    int reverbDividerY{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanelComponent)
};
