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
#include "GuiConstants.h"

using juce::SurjectiveMidiKeyboardComponent;

// The wordmark is the settings button. A separate cog beside the bank field gave
// the header two icons competing for the same corner, and the logo was inert
// decoration taking the best position in the window. Making it the control
// removes an icon and gives the wordmark a job.
class LogoButton final : public juce::Button {
public:
    LogoButton() : juce::Button{"Settings"} {
        setTitle("Settings");
        setDescription("Open Juicy16 settings");
        setHelpText("Accent colour and build information.");
        setTooltip(getHelpText());
        setWantsKeyboardFocus(true);
    }

    void setLogo(juce::Image image) {
        logo = std::move(image);
        repaint();
    }

    // The wordmark's natural width at the header's logo height, so the header can
    // lay out around it without knowing the asset's proportions.
    int logoWidth() const {
        if (!logo.isValid() || logo.getHeight() <= 0)
            return 0;
        return juce::roundToInt(static_cast<float>(GuiConstants::logoHeight)
                                * static_cast<float>(logo.getWidth())
                                / static_cast<float>(logo.getHeight()));
    }

private:
    void paintButton(Graphics& g, bool isMouseOver, bool isDown) override {
        if (logo.isValid()) {
            const int width{logoWidth()};
            const auto area{Rectangle<int>{
                (getWidth() - width) / 2,
                (getHeight() - GuiConstants::logoHeight) / 2,
                width, GuiConstants::logoHeight}.toFloat()};
            // The wordmark is a fixed asset, so hover and press are carried by
            // its opacity rather than by a background plate that would box in
            // the one element in the header that is not in a box.
            g.setOpacity(isDown ? 0.6f : (isMouseOver ? 0.85f : 1.0f));
            g.drawImage(logo, area, juce::RectanglePlacement::centred);
            g.setOpacity(1.0f);
        }

        if (hasKeyboardFocus(false) && Juicy16::focusRingsVisible()) {
            g.setColour(findColour(Juicy16::focusRingColourId));
            g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f),
                                   GuiConstants::cornerRadius, 1.0f);
        }
    }

    juce::Image logo;
};

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
    // Focus rings follow keyboard use, not focus alone. A mouse press anywhere in
    // the editor hides them; any key press brings them back. Registered on every
    // child, so a click on a knob counts as mouse use rather than only a click on
    // bare background.
    void mouseDown(const juce::MouseEvent&) override;
    void setFocusRingsVisible(bool visible);

    void syncKeyboardChannel();
    void syncStatusLabel();
    void showSettings();
    void applyAccentFromState();

    JuicySFAudioProcessor& audioProcessor;
    AudioProcessorValueTreeState& valueTreeState;

    // Owned by the editor and installed as the default LookAndFeel for its whole
    // component tree, so a control added later inherits the theme by default.
    Juicy16::PluginLookAndFeel lookAndFeel;

    // these are used to persist the UI's size - the values are stored along with the
    // filter's other parameters, and the UI component will update them when it gets
    // resized.
    Value lastUIWidth, lastUIHeight;

    // The owner's wordmark, decoded from the compiled-in binary resource. It is
    // handed to logoButton, which both draws it and opens settings.
    juce::Image logo;

    SurjectiveMidiKeyboardComponent midiKeyboard;
    ChannelListComponent channelRack;
    FilePicker filePicker;
    MixerPanelComponent mixerPanel;
    LogoButton logoButton;

    // status bar: build version plus the latest bank-load result
    juce::Label statusLabel;

    bool focusInitialized{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicySFAudioProcessorEditor)
};
