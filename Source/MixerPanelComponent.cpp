#include "MixerPanelComponent.h"
#include "FluidSynthModel.h"
#include "GuiConstants.h"
#include "Theme.h"

namespace {
constexpr int kMasterKnobSize{40};
constexpr int kHeadingHeight{14};
constexpr int kReverbKnobSize{36};
void showPercent(Slider& slider) {
    slider.textFromValueFunction = [](double value) { return String(juce::roundToInt(value * 100.0)) + "%"; };
    slider.valueFromTextFunction = [](const String& text) { return text.getDoubleValue() / 100.0; };
    slider.updateText();
}

// The panel's section headings: small, letterspaced, quiet.
void styleHeading(Label& label, const String& text) {
    label.setText(text.toUpperCase(), NotificationType::dontSendNotification);
    label.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
    label.setJustificationType(Justification::centredLeft);
    label.setInterceptsMouseClicks(false, false);
}
} // namespace

MixerPanelComponent::MixerPanelComponent(AudioProcessorValueTreeState& state, FluidSynthModel& model)
: fluidSynthModel{model}, valueTreeState{state}
{
    setName("Master and bank panel");
    setTitle("Master and bank panel");
    setDescription("Plugin-wide controls: master output trim and the loaded sound bank");

    channelInfo.setName("Selected channel diagnostics");
    styleHeading(channelInfo, "Channel 01");
    addAndMakeVisible(channelInfo);
    channelState.setJustificationType(Justification::centredRight);
    channelState.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
    addAndMakeVisible(channelState);
    channelPatch.setFont(Font{juce::FontOptions{GuiConstants::bodyFontHeight}});
    channelPatch.setMinimumHorizontalScale(1.0f);
    channelPatch.setJustificationType(Justification::centredLeft);
    addAndMakeVisible(channelPatch);
    channelPatchDetail.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
    channelPatchDetail.setMinimumHorizontalScale(1.0f);
    channelPatchDetail.setJustificationType(Justification::centredLeft);
    addAndMakeVisible(channelPatchDetail);
    for (const char* text : {"Expression", "Sustain", "Bend range", "Pitch bend", "Chorus send (CC93)"}) {
        auto label = make_unique<Label>();
        label->setText(text, dontSendNotification);
        label->setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
        label->setJustificationType(Justification::centredLeft);
        label->setInterceptsMouseClicks(false, false);
        label->setAccessible(false);
        addAndMakeVisible(*label);
        diagnosticLabels.add(std::move(label));
        auto value = make_unique<Label>();
        value->setName("Selected channel " + String(text).toLowerCase());
        value->setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
        value->setJustificationType(Justification::centredRight);
        addAndMakeVisible(*value);
        diagnosticValues.add(std::move(value));
    }
    peakReadout.setName("Master peak; click to clear overload");
    peakReadout.setTooltip("Peak level after master trim. OVER means output exceeded 0 dBFS; "
        "no limiter is applied. Click to clear the overload indicator.");
    peakReadout.onClick = [this] { fluidSynthModel.clearOutputOverload(); };
    addAndMakeVisible(peakReadout);
    startTimerHz(20);
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
    outputLevelValue.setJustificationType(Justification::centredLeft);
    outputLevelValue.setInterceptsMouseClicks(false, false);
    // The slider is the accessible control; this is its visible readout, and a
    // screen reader announcing it twice would be noise.
    outputLevelValue.setAccessible(false);
    addAndMakeVisible(outputLevelValue);

    outputLevelUnit.setText("dB trim", NotificationType::dontSendNotification);
    outputLevelUnit.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
    outputLevelUnit.setJustificationType(Justification::centredLeft);
    outputLevelUnit.setInterceptsMouseClicks(false, false);
    outputLevelUnit.setAccessible(false);
    addAndMakeVisible(outputLevelUnit);

    outputLevelSliderAttachment =
        make_unique<SliderAttachment>(state, "outputLevel", outputLevelSlider);

    // ---- Reverb -----------------------------------------------------------
    reverbTab.setName("Show reverb controls");
    chorusTab.setName("Show chorus controls");
    reverbTab.setConnectedEdges(Button::ConnectedOnRight);
    chorusTab.setConnectedEdges(Button::ConnectedOnLeft);
    reverbTab.onClick = [this] { selectEffect(false); };
    chorusTab.onClick = [this] { selectEffect(true); };
    addAndMakeVisible(reverbTab);
    addAndMakeVisible(chorusTab);

    reverbEnable.setButtonText("Enable");
    reverbEnable.setName("Reverb enabled");
    reverbEnable.setTitle("Reverb enabled");
    reverbEnable.setDescription("Enable or bypass the reverb");
    reverbEnable.setHelpText(
        "Bypassing the reverb removes it entirely rather than turning it down.");
    reverbEnable.setTooltip(reverbEnable.getHelpText());
    reverbEnable.setWantsKeyboardFocus(true);
    addAndMakeVisible(reverbEnable);
    reverbEnableAttachment =
        make_unique<AudioProcessorValueTreeState::ButtonAttachment>(
            state, "reverbOn", reverbEnable);

    reverbProfile.setName("Reverb profile");
    reverbProfile.setTitle("Reverb profile");
    reverbProfile.setDescription("Named set of reverb settings");
    reverbProfile.setHelpText(
        "Selecting a profile moves the four controls below; editing any of them "
        "selects Custom.");
    reverbProfile.setTooltip(reverbProfile.getHelpText());
    reverbProfile.setWantsKeyboardFocus(true);
    reverbProfile.addItemList(FluidSynthModel::reverbProfileNames(), 1);
    addAndMakeVisible(reverbProfile);
    reverbProfileAttachment =
        make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
            state, "reverbProfile", reverbProfile);

    {
        const char* captions[]{"Size", "Damp", "Width", "Level"};
        const char* help[]{
            "How large the reverberant space is.",
            "How quickly the tail loses its high frequencies.",
            "How wide the reverb sits across the stereo field.",
            "How much reverb the channel sends produce.",
        };
        for (int i = 0; i < FluidSynthModel::numReverbParams; ++i) {
            auto knob{make_unique<Slider>()};
            knob->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
            knob->setTextBoxStyle(Slider::TextBoxBelow, false, 58, 20);
            knob->setRange(0.0, 1.0, 0.001);
            const String name{String{"Reverb "} + String(captions[i]).toLowerCase()};
            knob->setName(name);
            knob->setTitle(name);
            knob->setDescription(name);
            knob->setHelpText(help[i]);
            knob->setTooltip(help[i]);
            knob->setWantsKeyboardFocus(true);
            addAndMakeVisible(*knob);
            reverbAttachments.add(new SliderAttachment(
                state, FluidSynthModel::reverbParamId(i), *knob));
            showPercent(*knob);
            reverbKnobs.add(std::move(knob));

            auto caption{make_unique<Label>()};
            caption->setText(captions[i], NotificationType::dontSendNotification);
            caption->setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
            caption->setJustificationType(Justification::centred);
            caption->setInterceptsMouseClicks(false, false);
            // The knob is the accessible control; its caption would only be
            // announced twice.
            caption->setAccessible(false);
            addAndMakeVisible(*caption);
            reverbLabels.add(std::move(caption));
        }
    }

    chorusEnable.setButtonText("Enable");
    chorusEnable.setName("Chorus enabled");
    chorusEnable.setTitle("Chorus enabled");
    chorusEnable.setDescription("Enable or bypass the global chorus");
    chorusEnable.setTooltip("Enable chorus. Its input follows the bank and MIDI CC93 (default 0). "
        "Send CC93 above 0 to hear chorus on an ordinary SoundFont.");
    chorusEnable.setWantsKeyboardFocus(true);
    addChildComponent(chorusEnable);
    chorusEnableAttachment = make_unique<AudioProcessorValueTreeState::ButtonAttachment>(
        state, "chorusOn", chorusEnable);
    chorusWaveform.setName("Chorus waveform");
    chorusWaveform.setTitle("Chorus waveform");
    chorusWaveform.setDescription("Shape of the chorus modulation");
    chorusWaveform.setTooltip("Chorus modulation waveform. Routing follows the bank and MIDI CC93.");
    chorusWaveform.setWantsKeyboardFocus(true);
    chorusWaveform.addItemList({"Sine", "Triangle"}, 1);
    addChildComponent(chorusWaveform);
    chorusWaveformAttachment = make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
        state, "chorusWaveform", chorusWaveform);
    const char* captions[]{"Voices", "Level", "Rate", "Depth"};
    const char* help[]{"Number of chorus voices (1-8).", "Level of the chorus return (0-1).",
        "Chorus modulation rate in Hz (0.1-5).", "Chorus modulation depth in milliseconds (0-21)."};
    for (int i = 0; i < 4; ++i) {
        auto knob = make_unique<Slider>();
        knob->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(Slider::TextBoxBelow, false, 58, 20);
        knob->setName("Chorus " + String(captions[i]).toLowerCase());
        knob->setTitle(knob->getName());
        knob->setDescription(help[i]);
        knob->setTooltip(help[i]);
        knob->setWantsKeyboardFocus(true);
        addChildComponent(*knob);
        chorusAttachments.add(new SliderAttachment(state,
            FluidSynthModel::chorusParamIds[FluidSynthModel::chorusVoices + i], *knob));
        if (i == 1) showPercent(*knob);
        else if (i >= 2) {
            const String unit = i == 2 ? " Hz" : " ms";
            knob->textFromValueFunction = [unit](double value) { return String(value, 2) + unit; };
            knob->valueFromTextFunction = [](const String& text) { return text.getDoubleValue(); };
            knob->updateText();
        }
        chorusKnobs.add(std::move(knob));
        auto caption = make_unique<Label>();
        caption->setText(captions[i], dontSendNotification);
        caption->setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
        caption->setJustificationType(Justification::centred);
        caption->setInterceptsMouseClicks(false, false);
        caption->setAccessible(false);
        addChildComponent(*caption);
        chorusLabels.add(std::move(caption));
    }
    selectEffect(false);

    styleHeading(bankHeading, "Bank");
    addAndMakeVisible(bankHeading);

    bankName.setFont(Font{juce::FontOptions{GuiConstants::bodyFontHeight}});
    bankName.setJustificationType(Justification::centredLeft);
    bankName.setMinimumHorizontalScale(1.0f);
    bankName.setName("Loaded bank");
    bankName.setTitle("Loaded bank");
    bankName.setDescription("The sound bank currently loaded");
    addAndMakeVisible(bankName);

    bankDetail.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
    bankDetail.setJustificationType(Justification::centredLeft);
    bankDetail.setAccessible(false);
    addAndMakeVisible(bankDetail);

    valueTreeState.state.addListener(this);
    syncOutputLevelReadout();
    syncBankSummary();
    timerCallback();
}

MixerPanelComponent::~MixerPanelComponent() {
    stopTimer();
    valueTreeState.state.removeListener(this);
}

void MixerPanelComponent::selectEffect(bool chorus) {
    reverbTab.setToggleState(!chorus, dontSendNotification);
    chorusTab.setToggleState(chorus, dontSendNotification);
    reverbEnable.setVisible(!chorus);
    reverbProfile.setVisible(!chorus);
    for (auto* knob : reverbKnobs) knob->setVisible(!chorus);
    for (auto* label : reverbLabels) label->setVisible(!chorus);
    chorusEnable.setVisible(chorus);
    chorusWaveform.setVisible(chorus);
    for (auto* knob : chorusKnobs) knob->setVisible(chorus);
    for (auto* label : chorusLabels) label->setVisible(chorus);
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
    auto& theme = getLookAndFeel();
    const int colour = loadedPath.isEmpty() ? Juicy16::textLabelColourId : Juicy16::textPrimaryColourId;
    // This also runs before the editor attaches its theme. Text can be prepared
    // immediately; custom colours are resolved when that theme is available.
    if (theme.isColourSpecified(colour)) bankName.setColour(Label::textColourId, theme.findColour(colour));
    if (loadedPath.isEmpty()) {
        bankName.setText("No bank loaded", NotificationType::dontSendNotification);
        bankDetail.setText({}, NotificationType::dontSendNotification);
        bankName.setTooltip({});
        return;
    }
    const File file{loadedPath};
    bankName.setText(file.getFileName(), NotificationType::dontSendNotification);
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

void MixerPanelComponent::lookAndFeelChanged() {
    auto& lookAndFeel{getLookAndFeel()};
        if (!lookAndFeel.isColourSpecified(Juicy16::textPrimaryColourId)) return;
    const Colour label{lookAndFeel.findColour(Juicy16::textLabelColourId)};
    const Colour primary{lookAndFeel.findColour(Juicy16::textPrimaryColourId)};
    for (Label* heading : {&masterHeading, &bankHeading})
        heading->setColour(Label::textColourId, label);
    outputLevelValue.setColour(Label::textColourId, primary);
    channelInfo.setColour(Label::textColourId, label);
    channelState.setColour(Label::textColourId, lookAndFeel.findColour(Juicy16::textErrorColourId));
    channelPatch.setColour(Label::textColourId, primary);
    channelPatchDetail.setColour(Label::textColourId, label);
    for (auto* text : diagnosticLabels) text->setColour(Label::textColourId, label);
    for (auto* value : diagnosticValues) value->setColour(Label::textColourId, primary);
    outputLevelUnit.setColour(Label::textColourId, label);
    bankDetail.setColour(Label::textColourId, label);
    for (Label* caption : reverbLabels)
        caption->setColour(Label::textColourId, label);
    for (Label* caption : chorusLabels)
        caption->setColour(Label::textColourId, label);
    // The bank name switches between primary and label depending on whether a
    // bank is loaded, so let that logic own it.
    syncBankSummary();
}

void MixerPanelComponent::paint(Graphics& g) {
    auto& lookAndFeel{getLookAndFeel()};
    g.fillAll(lookAndFeel.findColour(Juicy16::panelBackgroundColourId));
    g.setColour(lookAndFeel.findColour(Juicy16::borderColourId));
    g.fillRect(0, 0, 1, getHeight()); // divider from the rack
    g.setColour(lookAndFeel.findColour(Juicy16::subtleBorderColourId));
    g.fillRect(1, masterDividerY, getWidth() - 1, 1);
    g.fillRect(1, reverbDividerY, getWidth() - 1, 1);
    g.fillRect(1, bankDividerY, getWidth() - 1, 1);
}

void MixerPanelComponent::resized() {
    auto area = getLocalBounds().withTrimmedLeft(1);
    const int inset = GuiConstants::panelInset;
    auto master = area.removeFromTop(GuiConstants::masterSectionHeight);
    masterDividerY = master.getBottom();
    master.reduce(inset, GuiConstants::panelSectionGap);
    masterHeading.setBounds(master.removeFromTop(kHeadingHeight));
    master.removeFromTop(4);
    auto output = master.removeFromTop(kMasterKnobSize);
    outputLevelSlider.setBounds(output.removeFromLeft(kMasterKnobSize));
    output.removeFromLeft(GuiConstants::groupGap);
    outputLevelValue.setBounds(output.removeFromTop(27));
    outputLevelUnit.setBounds(output);
    master.removeFromTop(8);
    peakReadout.setBounds(master.removeFromTop(28));

    auto effects = area.removeFromTop(GuiConstants::effectsSectionHeight);
    reverbDividerY = effects.getBottom();
    effects.reduce(inset, GuiConstants::panelSectionGap);
    auto tabs = effects.removeFromTop(28);
    reverbTab.setBounds(tabs.removeFromLeft(tabs.getWidth() / 2));
    chorusTab.setBounds(tabs);
    effects.removeFromTop(12);
    auto settings = effects.removeFromTop(28);
    reverbEnable.setBounds(settings.removeFromLeft(84));
    chorusEnable.setBounds(reverbEnable.getBounds());
    settings.removeFromLeft(12);
    reverbProfile.setBounds(settings);
    chorusWaveform.setBounds(settings);
    effects.removeFromTop(12);
    const int cell = effects.getWidth() / 4;
    for (int i = 0; i < 4; ++i) {
        auto column = effects.removeFromLeft(cell);
        reverbLabels[i]->setBounds(column.removeFromTop(18));
        chorusLabels[i]->setBounds(reverbLabels[i]->getBounds());
        reverbKnobs[i]->setBounds(column.removeFromTop(kReverbKnobSize + 20));
        chorusKnobs[i]->setBounds(reverbKnobs[i]->getBounds());
    }

    auto bank = area.removeFromTop(GuiConstants::bankSectionHeight);
    bankDividerY = bank.getBottom();
    bank.reduce(inset, GuiConstants::panelSectionGap);
    bankHeading.setBounds(bank.removeFromTop(kHeadingHeight));
    bankName.setBounds(bank.removeFromTop(20));
    bankDetail.setBounds(bank.removeFromTop(16));

    auto channel = area.reduced(inset, GuiConstants::panelSectionGap);
    auto heading = channel.removeFromTop(18);
    channelState.setBounds(heading.removeFromRight(84));
    channelInfo.setBounds(heading);
    channelPatch.setBounds(channel.removeFromTop(22));
    channelPatchDetail.setBounds(channel.removeFromTop(18));
    channel.removeFromTop(8);
    for (int i = 0; i < diagnosticLabels.size(); ++i) {
        auto row = channel.removeFromTop(22);
        diagnosticValues[i]->setBounds(row.removeFromRight(80));
        diagnosticLabels[i]->setBounds(row);
    }
}

void MixerPanelComponent::PeakReadout::setPeak(float value, bool over) {
    peak = value;
    overload = over;
    setButtonText((over ? "Overload: " : "Output peak: ")
        + (value < 0.00001f ? String("-inf") : String(juce::Decibels::gainToDecibels(value), 1)) + " dBFS");
    repaint();
}

void MixerPanelComponent::PeakReadout::paintButton(Graphics& g, bool highlighted, bool) {
    auto& theme = getLookAndFeel();
    auto bounds = getLocalBounds();
    auto labels = bounds.removeFromTop(18);
    g.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
    g.setColour(theme.findColour(Juicy16::textLabelColourId));
    g.drawText("OUTPUT PEAK", labels.removeFromLeft(100), Justification::centredLeft);
    g.setColour(theme.findColour(overload ? Juicy16::textErrorColourId : Juicy16::textPrimaryColourId));
    const String level = peak < 0.00001f ? String::fromUTF8("−∞")
        : String(juce::Decibels::gainToDecibels(peak), 1);
    g.drawText((overload ? "OVER  " : "") + level + " dBFS", labels, Justification::centredRight);
    const auto track = bounds.withHeight(5).toFloat();
    g.setColour(theme.findColour(Juicy16::knobTrackColourId));
    g.fillRoundedRectangle(track, 2.0f);
    const float amount = juce::jlimit(0.0f, 1.0f,
        (juce::Decibels::gainToDecibels(peak, -60.0f) + 60.0f) / 66.0f);
    g.setColour(theme.findColour(overload ? Juicy16::textErrorColourId : Juicy16::accentColourId));
    g.fillRoundedRectangle(track.withWidth(track.getWidth() * amount), 2.0f);
    g.setColour(theme.findColour(Juicy16::textLabelColourId));
    const float unity = track.getX() + track.getWidth() * (60.0f / 66.0f);
    g.drawVerticalLine(juce::roundToInt(unity), track.getY(), track.getBottom());
    if (highlighted || (hasKeyboardFocus(false) && Juicy16::focusRingsVisible())) {
        g.setColour(theme.findColour(Juicy16::focusRingColourId));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 2.0f, 1.0f);
    }
}

void MixerPanelComponent::timerCallback() {
    displayPeak = juce::jmax(fluidSynthModel.consumeMasterPeak(), displayPeak * 0.89f);
    peakReadout.setPeak(displayPeak, fluidSynthModel.hasOutputOverload());
    const int ch = fluidSynthModel.getSelectedChannel();
    const auto d = fluidSynthModel.getChannelDiagnostics(ch);
    int bank{0}, program{0};
    fluidSynthModel.getAppliedChannelProgram(ch, bank, program);
    const auto bankTree = valueTreeState.state.getChildWithName("banks")
        .getChildWithProperty("num", d.soundingBank);
    const auto presetTree = bankTree.getChildWithProperty("num", d.soundingPreset);
    const String name = presetTree.getProperty("name", "Unnamed instrument").toString();
    const bool fallback = d.soundingBank >= 0 && (bank != d.soundingBank || program != d.soundingPreset);
    channelInfo.setText("CHANNEL " + String(ch + 1).paddedLeft('0', 2), dontSendNotification);
    channelState.setText(fluidSynthModel.isChannelSilenced(ch) ? "Silenced" : "", dontSendNotification);
    channelPatch.setText(d.soundingBank < 0 ? "No instrument loaded" : name, dontSendNotification);
    channelPatch.setTooltip(name);
    channelPatchDetail.setText(d.soundingBank < 0 ? "Load a bank to begin"
        : fallback ? "Fallback " + String(d.soundingBank) + ":" + String(d.soundingPreset)
            + String::fromUTF8(" · requested ") + String(bank) + ":" + String(program)
        : "Bank " + String(bank) + String::fromUTF8(" · Program ") + String(program), dontSendNotification);
    const double range = static_cast<double>(d.bendRange >> 7) + static_cast<double>(d.bendRange & 127) / 100.0;
    const String values[]{String(d.expression), d.sustain >= 64 ? "On" : "Off",
        String(range, 2) + " st", String(d.pitchBend), String(d.chorusSend)};
    for (int i = 0; i < diagnosticValues.size(); ++i)
        diagnosticValues[i]->setText(values[i], dontSendNotification);
}
