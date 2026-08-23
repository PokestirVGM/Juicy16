#include "MixerPanelComponent.h"
#include "GuiConstants.h"
#include "Theme.h"

namespace {
constexpr int kMasterKnobSize{40};
constexpr int kHeadingHeight{14};
constexpr int kBankNameHeight{16};
constexpr int kBankDetailHeight{18};

// The panel's section headings: small, letterspaced, quiet.
void styleHeading(Label& label, const String& text) {
    label.setText(text.toUpperCase(), NotificationType::dontSendNotification);
    label.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
    label.setColour(Label::textColourId,
                    label.getLookAndFeel().findColour(Juicy16::textLabelColourId));
    label.setJustificationType(Justification::centredLeft);
    label.setInterceptsMouseClicks(false, false);
}
} // namespace

MixerPanelComponent::MixerPanelComponent(
    AudioProcessorValueTreeState& state,
    FluidSynthModel& model)
: valueTreeState{state}
, fluidSynthModel{model}
{
    setName("Master and bank panel");
    setTitle("Master and bank panel");
    setDescription("Plugin-wide controls: master output trim and the loaded sound bank");

    styleHeading(masterHeading, "Master");
    addAndMakeVisible(masterHeading);

    outputLevelSlider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    outputLevelSlider.setRange(GuiConstants::outputLevelMinDb,
                               GuiConstants::outputLevelMaxDb, 0.1);
    // The readout is the large number beside the knob, not a text box under it.
    outputLevelSlider.setTextBoxStyle(Slider::NoTextBox, true, 0, 0);
    outputLevelSlider.getProperties().set("bipolar", true); // 0 dB is the origin
    outputLevelSlider.setName("Output level");
    outputLevelSlider.setTitle("Output level");
    outputLevelSlider.setDescription("Output level");
    outputLevelSlider.setHelpText(
        "Master output trim for the whole plugin, in decibels. Not a MIDI "
        "controller: nothing in a MIDI file changes it.");
    outputLevelSlider.setTooltip(outputLevelSlider.getHelpText());
    // JUCE sliders decline keyboard focus by default, which would leave this
    // mouse-only. A focused slider handles arrow keys.
    outputLevelSlider.setWantsKeyboardFocus(true);
    outputLevelSlider.onValueChange = [this] { syncOutputLevelReadout(); };
    addAndMakeVisible(outputLevelSlider);

    outputLevelValue.setFont(Font{juce::FontOptions{GuiConstants::masterValueFontHeight}});
    outputLevelValue.setColour(Label::textColourId,
                               findColour(Juicy16::textPrimaryColourId));
    outputLevelValue.setJustificationType(Justification::centredLeft);
    outputLevelValue.setInterceptsMouseClicks(false, false);
    // The slider is the accessible control; this is its visible readout, and a
    // screen reader announcing it twice would be noise.
    outputLevelValue.setAccessible(false);
    addAndMakeVisible(outputLevelValue);

    outputLevelUnit.setText("dB trim", NotificationType::dontSendNotification);
    outputLevelUnit.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
    outputLevelUnit.setColour(Label::textColourId,
                              findColour(Juicy16::textLabelColourId));
    outputLevelUnit.setJustificationType(Justification::centredLeft);
    outputLevelUnit.setInterceptsMouseClicks(false, false);
    outputLevelUnit.setAccessible(false);
    addAndMakeVisible(outputLevelUnit);

    outputLevelSliderAttachment =
        make_unique<SliderAttachment>(state, "outputLevel", outputLevelSlider);

    styleHeading(bankHeading, "Bank");
    addAndMakeVisible(bankHeading);

    bankName.setFont(Font{juce::FontOptions{GuiConstants::bodyFontHeight}});
    bankName.setColour(Label::textColourId, findColour(Juicy16::textPrimaryColourId));
    bankName.setJustificationType(Justification::centredLeft);
    bankName.setMinimumHorizontalScale(0.75f);
    bankName.setName("Loaded bank");
    bankName.setTitle("Loaded bank");
    bankName.setDescription("The sound bank currently loaded");
    addAndMakeVisible(bankName);

    bankDetail.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
    bankDetail.setColour(Label::textColourId, findColour(Juicy16::textLabelColourId));
    bankDetail.setJustificationType(Justification::centredLeft);
    bankDetail.setAccessible(false);
    addAndMakeVisible(bankDetail);

    valueTreeState.state.addListener(this);
    syncOutputLevelReadout();
    syncBankSummary();
}

MixerPanelComponent::~MixerPanelComponent() {
    valueTreeState.state.removeListener(this);
}

void MixerPanelComponent::syncOutputLevelReadout() {
    const double decibels{outputLevelSlider.getValue()};
    outputLevelValue.setText(
        decibels <= GuiConstants::outputLevelMinDb
            ? String::fromUTF8("-\xe2\x88\x9e")
            : String(decibels, 1),
        NotificationType::dontSendNotification);
}

void MixerPanelComponent::syncBankSummary() {
    const ValueTree fontState{valueTreeState.state.getChildWithName("soundFont")};
    const String loadedPath{fontState.getProperty("loadedPath", "").toString()};
    if (loadedPath.isEmpty()) {
        bankName.setText("No bank loaded", NotificationType::dontSendNotification);
        bankName.setColour(Label::textColourId, findColour(Juicy16::textLabelColourId));
        bankDetail.setText({}, NotificationType::dontSendNotification);
        bankName.setTooltip({});
        return;
    }
    const File file{loadedPath};
    bankName.setText(file.getFileName(), NotificationType::dontSendNotification);
    bankName.setColour(Label::textColourId, findColour(Juicy16::textPrimaryColourId));
    bankName.setTooltip(loadedPath);

    // Preset count, not "channels active": how many channels a file touches is
    // not something the plugin can know without a definition of "touched", and
    // the count of presets in the bank is a fact it does have.
    int presets{0};
    const ValueTree banks{valueTreeState.state.getChildWithName("banks")};
    for (int b = 0; b < banks.getNumChildren(); ++b)
        presets += banks.getChild(b).getNumChildren();
    bankDetail.setText(
        file.getFileExtension().toUpperCase().trimCharactersAtStart(".")
            + String::fromUTF8(" \xc2\xb7 ") + String(presets)
            + (presets == 1 ? " preset" : " presets"),
        NotificationType::dontSendNotification);
}

void MixerPanelComponent::valueTreePropertyChanged(ValueTree& tree,
                                                   const Identifier& property) {
    if (tree.getType() == StringRef("soundFont")
        && property == StringRef("loadedPath"))
        syncBankSummary();
    else if (tree.getType() == StringRef("banks"))
        syncBankSummary();
}

void MixerPanelComponent::paint(Graphics& g) {
    auto& lookAndFeel{getLookAndFeel()};
    g.fillAll(lookAndFeel.findColour(Juicy16::panelBackgroundColourId));
    g.setColour(lookAndFeel.findColour(Juicy16::borderColourId));
    g.fillRect(0, 0, 1, getHeight()); // divider from the rack
    g.setColour(lookAndFeel.findColour(Juicy16::subtleBorderColourId));
    g.fillRect(1, masterDividerY, getWidth() - 1, 1);
    g.fillRect(1, bankDividerY, getWidth() - 1, 1);
}

void MixerPanelComponent::resized() {
    Rectangle<int> r{getLocalBounds().withTrimmedLeft(1)};

    Rectangle<int> master{r.removeFromTop(
        GuiConstants::padding * 2 + kHeadingHeight + kMasterKnobSize
        + GuiConstants::innerPadding)};
    masterDividerY = master.getBottom();
    master.reduce(GuiConstants::padding + 4, GuiConstants::padding);
    masterHeading.setBounds(master.removeFromTop(kHeadingHeight));
    master.removeFromTop(GuiConstants::innerPadding);
    Rectangle<int> knobRow{master.removeFromTop(kMasterKnobSize)};
    outputLevelSlider.setBounds(knobRow.removeFromLeft(kMasterKnobSize));
    knobRow.removeFromLeft(GuiConstants::groupGap);
    outputLevelValue.setBounds(knobRow.removeFromTop(
        static_cast<int>(GuiConstants::masterValueFontHeight) + 4));
    outputLevelUnit.setBounds(knobRow.removeFromTop(kHeadingHeight));

    // The bank summary sits at the foot of the panel; whatever is left between
    // the two is where the reverb section lands in Phase 10.
    Rectangle<int> bank{r.removeFromBottom(
        GuiConstants::padding * 2 + kHeadingHeight + kBankNameHeight
        + kBankDetailHeight + 8)};
    bankDividerY = bank.getY();
    bank.reduce(GuiConstants::padding + 4, GuiConstants::padding);
    bankHeading.setBounds(bank.removeFromTop(kHeadingHeight));
    bank.removeFromTop(8);
    bankName.setBounds(bank.removeFromTop(kBankNameHeight));
    bankDetail.setBounds(bank.removeFromTop(kBankDetailHeight));
}
