//
// Juicy16's single visual system: one palette, expressed as named tokens, and
// one LookAndFeel that every control takes its appearance from.
//
// Rule this file exists to enforce (MILESTONE_PLAN.md, phase 9.3): no component
// may name a colour. `Colours::` literals belong here and nowhere else, and a
// control added later inherits the theme without being styled individually.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"

namespace Juicy16 {

// Custom ColourIds, so components resolve tokens through the normal
// findColour() path rather than reaching for a global. The base is an arbitrary
// value outside JUCE's own ranges.
enum ColourIds {
    windowBackgroundColourId = 0x4a16000,
    headerBackgroundColourId,
    panelBackgroundColourId,
    inputBackgroundColourId,
    controlBackgroundColourId,
    borderColourId,
    subtleBorderColourId,
    controlBorderColourId,
    rowAlternateColourId,
    rowSelectedColourId,
    textPrimaryColourId,
    textValueColourId,
    textLabelColourId,
    textFaintColourId,
    textErrorColourId,
    knobTrackColourId,
    accentColourId,
    keyboardBackgroundColourId,
};

// The accents offered to the user. Sage is the default; all four are a single
// hue used for knob arcs, the selected-row marker, and held keys.
enum class Accent { sage, amber, terracotta, neutral };

juce::Colour accentColour(Accent accent);
juce::String accentName(Accent accent);
Accent accentFromName(const juce::String& name);

class PluginLookAndFeel : public juce::LookAndFeel_V4 {
public:
    PluginLookAndFeel();

    // Repaints nothing by itself; the caller repaints the tree it owns.
    void setAccent(Accent accent);
    Accent getAccent() const { return accent; }

    // Rotary knob: a track arc, an accent arc from the start (or from centre for
    // a bipolar control such as pan), and a pointer. Bipolar is signalled by the
    // slider's own "bipolar" component property so no subclass is needed.
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;

    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    void drawButtonBackground(juce::Graphics&, juce::Button&,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    // A pill switch rather than JUCE's tick box: an unlabelled tick box beside a
    // section heading reads as an empty square, which is exactly what the
    // approved layout avoided by drawing a switch.
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    void drawTableHeaderBackground(juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn(juce::Graphics&, juce::TableHeaderComponent&,
                               const juce::String& columnName, int columnId,
                               int width, int height, bool isMouseOver,
                               bool isMouseDown, int columnFlags) override;

private:
    void applyTokens();

    Accent accent{Accent::sage};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginLookAndFeel)
};

} // namespace Juicy16
