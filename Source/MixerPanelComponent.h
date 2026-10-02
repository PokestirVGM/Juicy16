// Right-hand panel: global controls (master trim, effects, bank summary,
// selected-channel diagnostics), kept apart from the channel rack.

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "FluidSynthModel.h"
#include "Theme.h"
#include <array>

using namespace std;
using SliderAttachment = AudioProcessorValueTreeState::SliderAttachment;

class MixerPanelComponent : public Component,
                            public juce::AsyncUpdater,
                            private ValueTree::Listener, private juce::Timer
{
public:
    explicit MixerPanelComponent(AudioProcessorValueTreeState& state, FluidSynthModel& model);
    ~MixerPanelComponent() override;

    void paint(Graphics&) override;
    void resized() override;
    // Colours resolve here: the panel is built before the editor installs its
    // LookAndFeel.
    void lookAndFeelChanged() override;

private:
#if JUICYSF_UI_WORK_COUNTERS
    friend struct Juicy16::UIWorkBenchmark;
#endif
    void handleAsyncUpdate() override;
    void valueTreePropertyChanged(ValueTree&, const Identifier&) override;
    void valueTreeChildAdded(ValueTree&, ValueTree&) override {}
    void valueTreeChildRemoved(ValueTree&, ValueTree&, int) override {}
    void valueTreeChildOrderChanged(ValueTree&, int, int) override {}
    void valueTreeParentChanged(ValueTree&) override {}
    void valueTreeRedirected(ValueTree&) override {}

    void timerCallback() override;
    void syncOutputLevelReadout();
    FluidSynthModel& fluidSynthModel;
    Label channelInfo, channelState, channelPatch, channelPatchDetail;
    int displayedChannel{-1}, displayedSilenced{-1};
    std::array<int, 5> displayedPatchInputs{{-1, -1, -1, -1, -1}};
    std::array<int, 5> displayedDiagnosticInputs{{-1, -1, -1, -1, -1}};
    bool patchDisplayInvalid{true};
    juce::OwnedArray<Label> diagnosticLabels, diagnosticValues;
    class PeakReadout : public juce::TextButton {
    public:
        void setPeak(float value, bool over);
        void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    private:
        float peak{0.0f};
        bool overload{false};
        bool initialised{false};
    };
    PeakReadout peakReadout;
    float displayPeak{0.0f};
    void syncBankSummary();

    AudioProcessorValueTreeState& valueTreeState;

    Label masterHeading;
    Slider outputLevelSlider;
    Label outputLevelValue;
    Label outputLevelUnit;
    unique_ptr<SliderAttachment> outputLevelSliderAttachment;

    void selectEffect(bool chorus);
    juce::TextButton reverbTab{"Reverb"}, chorusTab{"Chorus"};
    juce::ToggleButton chorusEnable;
    unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> chorusEnableAttachment;
    juce::ComboBox chorusWaveform;
    unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> chorusWaveformAttachment;
    juce::OwnedArray<Slider> chorusKnobs;
    juce::OwnedArray<Label> chorusLabels;
    juce::OwnedArray<SliderAttachment> chorusAttachments;
    juce::ToggleButton reverbEnable;
    unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> reverbEnableAttachment;
    juce::ComboBox reverbProfile;
    unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> reverbProfileAttachment;
    // Size, damping, width, level.
    juce::OwnedArray<Slider> reverbKnobs;
    juce::OwnedArray<Label> reverbLabels;
    juce::OwnedArray<SliderAttachment> reverbAttachments;

    Label bankHeading;
    Label bankName;
    Label bankDetail;

    // Divider positions, set in resized() and drawn in paint().
    int masterDividerY{0};
    int bankDividerY{0};
    int reverbDividerY{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanelComponent)
};
