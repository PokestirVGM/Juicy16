/*
  ==============================================================================

    The Juicy16 editor: a header strip, the 16-channel rack, the global panel,
    the audition keyboard, and a status bar.

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginProcessor.h"
#include "ChannelListComponent.h"
#include "SurjectiveMidiKeyboardComponent.h"
#include "FilePicker.h"
#include "MixerPanelComponent.h"
#include "Theme.h"

using juce::SurjectiveMidiKeyboardComponent;

//==============================================================================
/**
*/
class JuicySFAudioProcessorEditor
: public AudioProcessorEditor
, private Value::Listener
, private ValueTree::Listener
{
public:
    JuicySFAudioProcessorEditor(
      JuicySFAudioProcessor&,
      AudioProcessorValueTreeState& valueTreeState
      );
    ~JuicySFAudioProcessorEditor() override;

    //==============================================================================
    void paint (Graphics&) override;
    void resized() override;

    bool keyPressed(const KeyPress &key) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void valueChanged (Value&) override;

    // keyboard follows the selected channel so you can audition the row you clicked
    void valueTreePropertyChanged (ValueTree&, const Identifier&) override;
    inline void valueTreeChildAdded (ValueTree&, ValueTree&) override {}
    inline void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override {}
    inline void valueTreeChildOrderChanged (ValueTree&, int, int) override {}
    inline void valueTreeParentChanged (ValueTree&) override {}
    inline void valueTreeRedirected (ValueTree&) override {}
    void syncKeyboardChannel();
    void syncStatusLabel();
    void showSettings();
    void applyAccentFromState();

    JuicySFAudioProcessor& processor;
    AudioProcessorValueTreeState& valueTreeState;

    // Owned by the editor and installed as the default LookAndFeel for its whole
    // component tree, so a control added later inherits the theme by default.
    Juicy16::PluginLookAndFeel lookAndFeel;

    // these are used to persist the UI's size - the values are stored along with the
    // filter's other parameters, and the UI component will update them when it gets
    // resized.
    Value lastUIWidth, lastUIHeight;

    // The owner's wordmark, decoded from the compiled-in binary resource.
    juce::Image logo;

    SurjectiveMidiKeyboardComponent midiKeyboard;
    ChannelListComponent channelRack;
    FilePicker filePicker;
    MixerPanelComponent mixerPanel;
    juce::DrawableButton settingsButton;

    // status bar: build version plus the latest bank-load result
    juce::Label statusLabel;

    bool focusInitialized{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicySFAudioProcessorEditor)
};
