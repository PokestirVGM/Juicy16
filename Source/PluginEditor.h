// The editor: header, 16-channel rack, global panel, keyboard and status bar.

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginProcessor.h"
#include "ChannelListComponent.h"
#include "SurjectiveMidiKeyboardComponent.h"
#include "FilePicker.h"
#include "MixerPanelComponent.h"
#include "MidiPlayerComponent.h"
#include "Theme.h"
#include "GuiConstants.h"

using juce::SurjectiveMidiKeyboardComponent;

// The wordmark doubles as the settings button.
class LogoButton final : public juce::Button {
public:
    LogoButton() : juce::Button{"Settings"} {
        setTitle("Settings");
        setDescription("Open Juicy16 settings");
        setHelpText("Accent colour, MIDI bend and vibrato settings, and build information.");
        setTooltip(getHelpText());
        setWantsKeyboardFocus(true);
    }

    void setLogo(juce::Image image) {
        logo = std::move(image);
        repaint();
    }

    // Wordmark width at the header's logo height.
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
            // Hover and press dim the wordmark rather than drawing a plate.
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
class JuicySFAudioProcessorEditor
: public AudioProcessorEditor
, private Value::Listener
, private ValueTree::Listener
, public juce::AsyncUpdater
, public juce::FileDragAndDropTarget
, private juce::KeyListener
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
    void focusLost(FocusChangeType) override;
    void focusOfChildComponentChanged(FocusChangeType) override;
    bool isInterestedInFileDrag(const StringArray&) override;
    void filesDropped(const StringArray&, int, int) override;

private:
    void valueChanged (Value&) override;
    void handleAsyncUpdate() override;

    // Keyboard follows the selected channel.
    void valueTreePropertyChanged (ValueTree&, const Identifier&) override;
    inline void valueTreeChildAdded (ValueTree&, ValueTree&) override {}
    inline void valueTreeChildRemoved (ValueTree&, ValueTree&, int) override {}
    inline void valueTreeChildOrderChanged (ValueTree&, int, int) override {}
    inline void valueTreeParentChanged (ValueTree&) override {}
    inline void valueTreeRedirected (ValueTree&) override {}
    // Focus rings show during keyboard use only: any mouse press hides them,
    // any key press restores them. Registered on every child.
    void mouseDown(const juce::MouseEvent&) override;
    void setFocusRingsVisible(bool visible);

    void syncKeyboardChannel();
    void syncStatusLabel();
    void showSettings();
    void applyAccentFromState();
    bool handleTransportKey(const KeyPress&, juce::Component* origin);
    bool keyPressed(const KeyPress&, juce::Component* origin) override;
    bool keyStateChanged(bool, juce::Component*) override;
    void registerEditorKeys(juce::Component&, bool add);

    JuicySFAudioProcessor& audioProcessor;
    AudioProcessorValueTreeState& valueTreeState;

    // Default LookAndFeel for the whole editor tree, so new controls inherit it.
    Juicy16::PluginLookAndFeel lookAndFeel;

    // Persisted UI size.
    Value lastUIWidth, lastUIHeight;

    // Decoded wordmark, drawn by logoButton.
    juce::Image logo;

    SurjectiveMidiKeyboardComponent midiKeyboard;
    ChannelListComponent channelRack;
    FilePicker filePicker;
    MixerPanelComponent mixerPanel;
    std::unique_ptr<MidiPlayerComponent> midiPlayer;
    LogoButton logoButton;

    // Build version plus the latest bank-load result.
    juce::Label statusLabel;

    // Owned here so async dismissal cannot outlive the processor or theme.
    std::unique_ptr<juce::Component> settingsContent;
    std::unique_ptr<juce::CallOutBox> settingsCallout;

    bool focusInitialized{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuicySFAudioProcessorEditor)
};
