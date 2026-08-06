/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginProcessor.h"
#include "TablesComponent.h"
#include "SurjectiveMidiKeyboardComponent.h"
#include "FilePicker.h"
#include "SlidersComponent.h"

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

    AudioProcessorValueTreeState& valueTreeState;

    // these are used to persist the UI's size - the values are stored along with the
    // filter's other parameters, and the UI component will update them when it gets
    // resized.
    Value lastUIWidth, lastUIHeight;

    SurjectiveMidiKeyboardComponent midiKeyboard;
    TablesComponent tablesComponent;
    FilePicker filePicker;
    SlidersComponent slidersComponent;

    // status bar: build version plus the latest bank-load result
    Label statusLabel;

    bool focusInitialized{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicySFAudioProcessorEditor)
};
