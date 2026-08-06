/*
  ==============================================================================

    This file was auto-generated!

    It contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GuiConstants.h"

//==============================================================================
JuicySFAudioProcessorEditor::JuicySFAudioProcessorEditor(
    JuicySFAudioProcessor& p,
    AudioProcessorValueTreeState& state)
: AudioProcessorEditor{&p}
, valueTreeState{state}
, midiKeyboard{p.keyboardState, SurjectiveMidiKeyboardComponent::horizontalKeyboard}
, tablesComponent{state, p.getFluidSynthModel()}
, filePicker{state}
, slidersComponent{state, p.getFluidSynthModel()}
{
    // Cap the width at the on-screen keyboard's own natural size (its full MIDI
    // range at its fixed key width): resizing wider than that would just add blank
    // space past the last key, so there's no reason to allow it.
    const int keyboardMaxWidth{midiKeyboard.getTotalKeyboardWidth() + 2 * GuiConstants::padding};

    // set resize limits for this plug-in
    setResizeLimits(
        GuiConstants::minWidth,
        GuiConstants::minHeight,
        keyboardMaxWidth,
        GuiConstants::maxHeight);
    // setResizeLimits() alone marks the editor resizable to the HOST, but some
    // hosts' generic AU views don't supply their own resize chrome and rely on the
    // plugin drawing one; without this, those hosts (e.g. FL Studio's AU wrapper)
    // show a fixed-size window despite the limits above.
    setResizable(true, true);

    lastUIWidth.referTo(state.state.getChildWithName("uiState").getPropertyAsValue("width",  nullptr));
    lastUIHeight.referTo(state.state.getChildWithName("uiState").getPropertyAsValue("height", nullptr));

    // set our component's initial size to be the last one that was stored in the filter's settings
    setSize(lastUIWidth.getValue(), lastUIHeight.getValue());

    lastUIWidth.addListener(this);
    lastUIHeight.addListener(this);

    midiKeyboard.setName ("MIDI Keyboard");

    midiKeyboard.setWantsKeyboardFocus(false);
    tablesComponent.setWantsKeyboardFocus(false);

    setWantsKeyboardFocus(true);
    addAndMakeVisible(midiKeyboard);

    addAndMakeVisible(slidersComponent);
    addAndMakeVisible(tablesComponent);
    addAndMakeVisible(filePicker);

    // status bar: build version and a visible bank-load result
    statusLabel.setFont(Font{juce::FontOptions{12.0f}});
    statusLabel.setName("Version and bank load status");
    statusLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(statusLabel);

    // keyboard: light up for MIDI on any channel, but send notes on the channel
    // selected in the list, so clicking a row lets you audition its instrument.
    midiKeyboard.setMidiChannelsToDisplay(0xffff);
    valueTreeState.state.addListener(this);
    syncKeyboardChannel();
    syncStatusLabel();
}

void JuicySFAudioProcessorEditor::syncKeyboardChannel() {
    const int sel{valueTreeState.state.getChildWithName("uiState")
        .getProperty("selectedChannel", 1)};
    midiKeyboard.setMidiChannel(juce::jlimit(1, 16, sel));
}

void JuicySFAudioProcessorEditor::syncStatusLabel() {
    const ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
    const String status{fontState.getProperty("loadStatus", "idle").toString()};
    const String message{fontState.getProperty("loadMessage", "No bank loaded.").toString()};
    const String text{"JuicySF Rack v" JUICYSF_RACK_VERSION " — " + message};
    statusLabel.setText(text, dontSendNotification);
    statusLabel.setTooltip(message);
    statusLabel.setColour(
        Label::textColourId,
        status == "error" ? juce::Colours::salmon : juce::Colours::lightgrey);
}

void JuicySFAudioProcessorEditor::valueTreePropertyChanged(ValueTree& tree, const Identifier& property) {
    if (tree.getType() == StringRef("uiState") && property == StringRef("selectedChannel"))
        syncKeyboardChannel();
    if (tree.getType() == StringRef("soundFont")
        && (property == StringRef("loadStatus") || property == StringRef("loadMessage")))
        syncStatusLabel();
}

// called when the stored window size changes
void JuicySFAudioProcessorEditor::valueChanged(Value&) {
    setSize(lastUIWidth.getValue(), lastUIHeight.getValue());
}

JuicySFAudioProcessorEditor::~JuicySFAudioProcessorEditor()
{
    valueTreeState.state.removeListener(this);
    lastUIWidth.removeListener(this);
    lastUIHeight.removeListener(this);
}

//==============================================================================
void JuicySFAudioProcessorEditor::paint (Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (ResizableWindow::backgroundColourId));

    if (!focusInitialized) {
        if (!hasKeyboardFocus(false) && isVisible()) {
            grabKeyboardFocus();
        }
        if (getCurrentlyFocusedComponent() == this) {
            focusInitialized = true;
        }
    }
}

void JuicySFAudioProcessorEditor::resized()
{
    // shared with GuiConstants::defaultHeight and the keyboard-width cap above, so
    // the layout here can't drift out of sync with those computations.
    const int padding{GuiConstants::padding};
    Rectangle<int> r{getLocalBounds()};
    filePicker.setBounds(r.removeFromTop(GuiConstants::filePickerHeight + padding).reduced(padding, 0).withTrimmedTop(padding));

    statusLabel.setBounds(r.removeFromBottom(GuiConstants::statusBarHeight).reduced(padding, 0));

    midiKeyboard.setBounds (r.removeFromBottom (GuiConstants::pianoHeight).reduced(padding, 0));

    Rectangle<int> rContent{r.reduced(0, padding)};
    slidersComponent.setBounds(rContent.removeFromRight(slidersComponent.getDesiredWidth() + padding).withTrimmedRight(padding));

    tablesComponent.setBounds(rContent);

    lastUIWidth = getWidth();
    lastUIHeight = getHeight();
}

bool JuicySFAudioProcessorEditor::keyPressed(const KeyPress &key) {
    // patch selection now lives in per-row dropdowns; all key input drives the
    // on-screen MIDI keyboard.
    return midiKeyboard.keyPressed(key);
}

bool JuicySFAudioProcessorEditor::keyStateChanged (bool isKeyDown) {
    return midiKeyboard.keyStateChanged(isKeyDown);
}
