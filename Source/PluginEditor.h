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
, private juce::Timer // TEMPORARY: drives the diagnostic MIDI monitor in the status bar
{
public:
    JuicySFAudioProcessorEditor(
      JuicySFAudioProcessor&,
      AudioProcessorValueTreeState& valueTreeState
      );
    ~JuicySFAudioProcessorEditor();

    //==============================================================================
    void paint (Graphics&) override;
    void resized() override;

    bool keyPressed(const KeyPress &key) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    void valueChanged (Value&) override;
    void timerCallback() override; // TEMPORARY: refresh diagnostic MIDI monitor

    // keyboard follows the selected channel so you can audition the row you clicked
    void valueTreePropertyChanged (ValueTree&, const Identifier&) override;
    inline void valueTreeChildAdded (ValueTree&, ValueTree&) override {}
    inline void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override {}
    inline void valueTreeChildOrderChanged (ValueTree&, int, int) override {}
    inline void valueTreeParentChanged (ValueTree&) override {}
    inline void valueTreeRedirected (ValueTree&) override {}
    void syncKeyboardChannel();

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    JuicySFAudioProcessor& processor;

    AudioProcessorValueTreeState& valueTreeState;

    // these are used to persist the UI's size - the values are stored along with the
    // filter's other parameters, and the UI component will update them when it gets
    // resized.
    Value lastUIWidth, lastUIHeight;

    SurjectiveMidiKeyboardComponent midiKeyboard;
    TablesComponent tablesComponent;
    FilePicker filePicker;
    SlidersComponent slidersComponent;

    // status bar: shows the build version, so it's obvious when a new build has loaded
    Label statusLabel;

    bool focusInitialized;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicySFAudioProcessorEditor)
};
