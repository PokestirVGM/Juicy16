// Juicy16's visual system: one tokenised palette and one LookAndFeel. No
// component names a colour; Colours:: literals live only here.

#pragma once

#include <vector>

#include "../JuceLibraryCode/JuceHeader.h"

#ifndef JUICYSF_UI_WORK_COUNTERS
 #define JUICYSF_UI_WORK_COUNTERS 0
#endif

#if JUICYSF_UI_WORK_COUNTERS
class ChannelListComponent;
class MixerPanelComponent;
#endif

namespace Juicy16 {

#if JUICYSF_UI_WORK_COUNTERS
// Opt-in harness measurements; absent from ordinary product builds.
struct UIWorkCounters {
    juce::uint64 rackPatchLookups{}, rackTooltipFormats{}, rackActivityRepaints{};
    juce::uint64 mixerPatchFormats{}, mixerControllerFormats{}, mixerPeakFormats{};
    juce::uint64 settingsCC1Formats{}, accentTreeRefreshes{}, keyboardKeyDraws{};
};
inline UIWorkCounters uiWorkCounters;
struct UIWorkBenchmark {
    static void tickRack(ChannelListComponent&);
    static void tickMixer(MixerPanelComponent&);
    static void tickSettings(juce::Component&);
};
#define JUICY16_COUNT_UI_WORK(field) (++Juicy16::uiWorkCounters.field)
#else
#define JUICY16_COUNT_UI_WORK(field) ((void) 0)
#endif

// Custom ColourIds resolved through findColour(). The base is outside JUCE's
// ranges.
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
    // Mute keeps its own hue in every accent.
    muteActiveColourId,
    // Focus and hover are neutral; the accent marks values only.
    focusRingColourId,
    // Overlay for a muted or solo-silenced channel.
    rowSilencedColourId,
    keyboardBackgroundColourId,
};

// User accents, ordered around the hue wheel. Sage is the default.
enum class Accent {
    sage, olive, amber, terracotta, rose, magenta,
    violet, indigo, steel, ice, teal, neutral
};

const std::vector<Accent>& allAccents();

juce::Colour accentColour(Accent accent);
juce::String accentName(Accent accent);
Accent accentFromName(const juce::String& name);

// Focus rings show only during keyboard use (like :focus-visible): on after a
// key press, off after a click.
bool focusRingsVisible() noexcept;
void setFocusRingsVisible(bool visible) noexcept;

class PluginLookAndFeel : public juce::LookAndFeel_V4 {
public:
    PluginLookAndFeel();

    // Does not repaint; the caller does.
    void setAccent(Accent accent);
    Accent getAccent() const { return accent; }

    // Track arc, accent arc (from centre when the slider's "bipolar" property is
    // set) and a pointer.
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

    // A pill switch; a bare tick box beside a heading reads as an empty square.
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    // Dark popover chrome; JUCE's default is a light box.
    void drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics&,
                                  const juce::Path&, juce::Image&) override;

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
