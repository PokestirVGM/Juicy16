#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GuiConstants.h"
#include "Theme.h"
#include <BinaryData.h>

namespace {

// One spelling of accent names for the dropdown and its list.
String accentDisplayName(Juicy16::Accent accent) {
    const String name{Juicy16::accentName(accent)};
    return name.substring(0, 1).toUpperCase() + name.substring(1);
}

// Accent dropdown only: each list row shows a swatch of its colour.
class AccentListLookAndFeel final : public Juicy16::PluginLookAndFeel {
public:
    void drawPopupMenuItem(Graphics& g, const Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const String& text,
                           const String& shortcutKeyText, const juce::Drawable* icon,
                           const Colour* textColour) override {
        Juicy16::PluginLookAndFeel::drawPopupMenuItem(
            g, area, isSeparator, isActive, isHighlighted, isTicked, hasSubMenu,
            text, shortcutKeyText, icon, textColour);
        if (isSeparator)
            return;

        // Swatch at the trailing edge, clear of the tick and text.
        for (const auto choice : Juicy16::allAccents()) {
            if (accentDisplayName(choice) != text)
                continue;
            const float size{static_cast<float>(
                juce::jmin(10, juce::jmax(4, area.getHeight() - 10)))};
            const Rectangle<float> swatch{
                static_cast<float>(area.getRight()) - size - GuiConstants::innerPadding,
                static_cast<float>(area.getCentreY()) - size * 0.5f,
                size, size};
            g.setColour(Juicy16::accentColour(choice));
            g.fillRoundedRectangle(swatch, 2.0f);
            break;
        }
    }
};

// Settings popover: accent, sound and MIDI settings, and build facts as
// key/value rows.
class SettingsPanel final : public Component, private juce::Timer, private ValueTree::Listener {
public:
    struct Fact { String key; String value; };

    // accentName() is the stored identifier; this is the label.
    static String displayName(Juicy16::Accent accent) { return accentDisplayName(accent); }

    static int indexOfAccent(Juicy16::Accent accent) {
        const auto& accents{Juicy16::allAccents()};
        for (std::size_t i = 0; i < accents.size(); ++i)
            if (accents[i] == accent)
                return static_cast<int>(i);
        return 0;
    }

    SettingsPanel(AudioProcessorValueTreeState& state,
                  FluidSynthModel& model,
                  Juicy16::Accent current,
                  std::vector<Fact> factsToShow,
                  std::function<void(Juicy16::Accent)> onAccentChosen)
    : valueTreeState{state}, fluidSynthModel{model}, facts{std::move(factsToShow)}
    , chooseAccent{std::move(onAccentChosen)}
    {
        setName("Settings");
        setTitle("Settings");
        setDescription("Juicy16 settings: accent colour, sample interpolation, MIDI bend and vibrato compensation, and build information");

        soundHeading.setText("SOUND", dontSendNotification);
        soundHeading.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
        soundHeading.setAccessible(false);
        addAndMakeVisible(soundHeading);
        interpolationLabel.setText("Interpolation", dontSendNotification);
        interpolationLabel.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
        interpolationLabel.setAccessible(false);
        addAndMakeVisible(interpolationLabel);
        interpolationBox.setName("Sample interpolation");
        interpolationBox.setTitle("Sample interpolation");
        interpolationBox.setDescription(
            "How samples are resampled to play at other pitches: 7th-order, linear, "
            "or none");
        interpolationBox.setTooltip(
            "7th-order is the cleanest. Linear keeps more of the bright grit of "
            "low-rate samples. None is the rawest.");
        interpolationBox.setWantsKeyboardFocus(true);
        interpolationBox.addItemList({"7th-order", "Linear", "None"}, 1);
        addAndMakeVisible(interpolationBox);
        interpolationAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
            state, "interpolation", interpolationBox);

        midiHeading.setText("MIDI", dontSendNotification);
        midiHeading.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
        midiHeading.setAccessible(false);
        addAndMakeVisible(midiHeading);

        // Host bend compensation; real parameters, saved with the project.
        bendRangeLabel.setText("Bend range", dontSendNotification);
        bendRangeLabel.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
        bendRangeLabel.setAccessible(false);
        addAndMakeVisible(bendRangeLabel);
        bendRangeBox.setName("Bend range override");
        bendRangeBox.setTitle("Bend range override");
        bendRangeBox.setDescription(
            "Force one pitch-bend range on every channel, for a host that does not "
            "pass the file's RPN bend range to the plugin");
        bendRangeBox.setTooltip(
            "Follow the MIDI file, or force one bend range on all 16 channels. Use "
            "it when a host drops the file's RPN bend range: game rips usually ask "
            "for 12 semitones.");
        bendRangeBox.setWantsKeyboardFocus(true);
        bendRangeBox.addItem("Follow the MIDI file", 1);
        for (int semitones = 1; semitones <= 24; ++semitones)
            bendRangeBox.addItem(String(semitones) + (semitones == 1 ? " semitone" : " semitones"),
                                 semitones + 1);
        addAndMakeVisible(bendRangeBox);

        bendScaleLabel.setText("Bend scale", dontSendNotification);
        bendScaleLabel.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
        bendScaleLabel.setAccessible(false);
        addAndMakeVisible(bendScaleLabel);
        bendScaleBox.setName("Bend scale");
        bendScaleBox.setTitle("Bend scale");
        bendScaleBox.setDescription(
            "Multiply every incoming pitch bend, for a host that shrank the bends "
            "when it imported the MIDI file");
        bendScaleBox.setTooltip(
            "Multiplies incoming pitch bend. FL Studio imports every MIDI bend as "
            "plus or minus two semitones whatever the file asked for; for a rip "
            "written for 12 semitones, choose x6.");
        bendScaleBox.setWantsKeyboardFocus(true);
        for (int factor = 1; factor <= 24; ++factor)
            bendScaleBox.addItem(String::fromUTF8("\xc3\x97") + String(factor)
                                     + (factor == 1 ? " (off)" : ""),
                                 factor);
        addAndMakeVisible(bendScaleBox);
        for (auto* label : {&vibratoChannelLabel, &vibratoScaleLabel, &cc1Label}) {
            label->setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
            label->setAccessible(false);
            addAndMakeVisible(*label);
        }
        vibratoChannelLabel.setText("CC1 channel", dontSendNotification);
        vibratoScaleLabel.setText("CC1 scale", dontSendNotification);
        cc1Label.setText("CC1 received", dontSendNotification);
        vibratoChannelBox.setName("CC1 MIDI channel");
        vibratoChannelBox.setTooltip("Choose the channel whose CC1 vibrato strength is edited. Also selects that channel in the rack.");
        vibratoScaleBox.setName("Selected channel CC1 vibrato strength");
        vibratoScaleBox.setTooltip("Scales CC1-driven pitch vibrato on this channel. x1 follows the bank. "
            "CC1 must be delivered by the host: at zero this does not create vibrato. Bank rate and delay are preserved.");
        for (int ch = 1; ch <= 16; ++ch) vibratoChannelBox.addItem("Channel " + String(ch), ch);
        for (int factor = 1; factor <= 24; ++factor)
            vibratoScaleBox.addItem(String::fromUTF8("\xc3\x97") + String(factor)
                + (factor == 1 ? " (off)" : ""), factor);
        for (auto* box : {&vibratoChannelBox, &vibratoScaleBox}) {
            box->setWantsKeyboardFocus(true);
            addAndMakeVisible(*box);
        }
        vibratoChannelBox.onChange = [this] {
            fluidSynthModel.selectChannelForEditing(vibratoChannelBox.getSelectedId() - 1);
            syncVibratoChannel();
        };
        cc1Value.setName("Received CC1 value");
        cc1Value.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
        cc1Value.setJustificationType(Justification::centredRight);
        cc1Value.setTooltip("Current modulation controller received by Juicy16 on the selected channel. "
            "If this stays zero while the MIDI file contains modulation, check its controller routing in the host.");
        addAndMakeVisible(cc1Value);
        valueTreeState.state.addListener(this);
        syncVibratoChannel();
        startTimerHz(20);
        resetPolicyLabel.setText("Reset policy", dontSendNotification);
        resetPolicyLabel.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
        resetPolicyLabel.setAccessible(false);
        addAndMakeVisible(resetPolicyLabel);
        resetPolicyBox.addItemList({"DAW recovery", "Standard MIDI"}, 1);
        resetPolicyBox.setName("MIDI reset policy");
        resetPolicyBox.setTooltip("DAW recovery preserves setup across resets and reconstructs "
            "same-timestamp controller order. Standard MIDI honors incoming order and resets. "
            "Choose before starting playback; it does not reset the current sound.");
        addAndMakeVisible(resetPolicyBox);
        resetPolicyAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
            state, "resetPolicy", resetPolicyBox);
        bendRangeAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
            state, "bendRange", bendRangeBox);
        bendScaleAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
            state, "bendScale", bendScaleBox);

        accentHeading.setText("ACCENT", dontSendNotification);
        accentHeading.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
        accentHeading.setAccessible(false);
        addAndMakeVisible(accentHeading);

        buildHeading.setText("BUILD", dontSendNotification);
        buildHeading.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
        buildHeading.setAccessible(false);
        addAndMakeVisible(buildHeading);

        // Too many accents for swatches, so each list row is drawn in its colour.
        accentBox.setName("Accent colour");
        accentBox.setTitle("Accent colour");
        accentBox.setDescription("Choose the accent colour used for knobs, the selected row, and held keys");
        accentBox.setTooltip(accentBox.getDescription());
        accentBox.setWantsKeyboardFocus(true);
        accentBox.setLookAndFeel(&accentListLookAndFeel);
        int itemId{1};
        for (const auto accent : Juicy16::allAccents())
            accentBox.addItem(displayName(accent), itemId++);
        accentBox.setSelectedId(indexOfAccent(current) + 1, dontSendNotification);
        accentBox.onChange = [this] {
            const auto& accents{Juicy16::allAccents()};
            const int index{accentBox.getSelectedId() - 1};
            if (index < 0 || index >= static_cast<int>(accents.size()))
                return;
            if (chooseAccent != nullptr)
                chooseAccent(accents[static_cast<std::size_t>(index)]);
            // The popover is not an editor child, so it needs its own LookAndFeel change.
            sendLookAndFeelChange();
        };
        addAndMakeVisible(accentBox);

        for (const auto& fact : facts) {
            auto key{std::make_unique<Label>()};
            key->setText(fact.key, dontSendNotification);
            key->setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
            key->setAccessible(false);
            addAndMakeVisible(*key);
            factKeys.add(std::move(key));

            auto value{std::make_unique<Label>()};
            value->setText(fact.value, dontSendNotification);
            value->setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
            value->setJustificationType(Justification::centredRight);
            value->setMinimumHorizontalScale(0.8f);
            // Key doubles as the accessible name, e.g. "Engine: FluidSynth 2.5.7".
            value->setName(fact.key);
            value->setTitle(fact.key);
            value->setDescription(fact.key + ": " + fact.value);
            addAndMakeVisible(*value);
            factValues.add(std::move(value));
        }

        setSize(kWidth,
                GuiConstants::padding * 2
                    + kHeadingHeight + kHeadingGap + kSwatchHeight
                    + GuiConstants::groupGap + 1 + GuiConstants::groupGap
                    + kHeadingHeight + kHeadingGap + kControlRowHeight
                    + GuiConstants::groupGap + 1 + GuiConstants::groupGap
                    + kHeadingHeight + kHeadingGap
                    + 6 * kControlRowHeight + 5 * kControlRowGap
                    + GuiConstants::groupGap + 1 + GuiConstants::groupGap
                    + kHeadingHeight + kHeadingGap
                    + static_cast<int>(facts.size()) * kFactRowHeight);
    }

    ~SettingsPanel() override {
        stopTimer();
        valueTreeState.state.removeListener(this);
        // Detach the member LookAndFeel before either is destroyed.
        accentBox.setLookAndFeel(nullptr);
    }

    void paint(Graphics& g) override {
        g.fillAll(findColour(Juicy16::panelBackgroundColourId));
        g.setColour(findColour(Juicy16::subtleBorderColourId));
        for (const int y : {soundDividerY, midiDividerY, dividerY})
            g.fillRect(GuiConstants::padding, y, getWidth() - GuiConstants::padding * 2, 1);
    }

    void lookAndFeelChanged() override {
        auto& theme{getLookAndFeel()};
        if (!theme.isColourSpecified(Juicy16::textPrimaryColourId)) return;
        const Colour label{theme.findColour(Juicy16::textLabelColourId)};
        for (Label* heading : {&accentHeading, &soundHeading, &midiHeading, &buildHeading, &interpolationLabel,
                               &bendRangeLabel, &bendScaleLabel, &resetPolicyLabel,
                               &vibratoChannelLabel, &vibratoScaleLabel, &cc1Label})
            heading->setColour(Label::textColourId, label);
        cc1Value.setColour(Label::textColourId, theme.findColour(Juicy16::textPrimaryColourId));
        // The closed dropdown shows its text in the selected accent.
        accentBox.setColour(juce::ComboBox::textColourId,
                            theme.findColour(Juicy16::accentColourId));
        for (Label* key : factKeys)
            key->setColour(Label::textColourId, label);
        for (Label* value : factValues)
            value->setColour(Label::textColourId,
                             theme.findColour(Juicy16::textPrimaryColourId));
    }

    void resized() override {
        Rectangle<int> r{getLocalBounds().reduced(GuiConstants::padding)};

        accentHeading.setBounds(r.removeFromTop(kHeadingHeight));
        r.removeFromTop(kHeadingGap);
        accentBox.setBounds(r.removeFromTop(kSwatchHeight));

        r.removeFromTop(GuiConstants::groupGap);
        soundDividerY = r.removeFromTop(1).getY();
        r.removeFromTop(GuiConstants::groupGap);

        soundHeading.setBounds(r.removeFromTop(kHeadingHeight));
        r.removeFromTop(kHeadingGap);
        {
            Rectangle<int> row{r.removeFromTop(kControlRowHeight)};
            interpolationBox.setBounds(row.removeFromRight(row.getWidth() * 3 / 5));
            interpolationLabel.setBounds(row);
        }

        r.removeFromTop(GuiConstants::groupGap);
        midiDividerY = r.removeFromTop(1).getY();
        r.removeFromTop(GuiConstants::groupGap);

        midiHeading.setBounds(r.removeFromTop(kHeadingHeight));
        r.removeFromTop(kHeadingGap);
        {
            Rectangle<int> row{r.removeFromTop(kControlRowHeight)};
            bendRangeBox.setBounds(row.removeFromRight(row.getWidth() * 3 / 5));
            bendRangeLabel.setBounds(row);
            r.removeFromTop(kControlRowGap);
            row = r.removeFromTop(kControlRowHeight);
            bendScaleBox.setBounds(row.removeFromRight(row.getWidth() * 3 / 5));
            bendScaleLabel.setBounds(row);
            r.removeFromTop(kControlRowGap);
            row = r.removeFromTop(kControlRowHeight);
            vibratoChannelBox.setBounds(row.removeFromRight(row.getWidth() * 3 / 5));
            vibratoChannelLabel.setBounds(row);
            r.removeFromTop(kControlRowGap);
            row = r.removeFromTop(kControlRowHeight);
            vibratoScaleBox.setBounds(row.removeFromRight(row.getWidth() * 3 / 5));
            vibratoScaleLabel.setBounds(row);
            r.removeFromTop(kControlRowGap);
            row = r.removeFromTop(kControlRowHeight);
            cc1Value.setBounds(row.removeFromRight(row.getWidth() * 3 / 5));
            cc1Label.setBounds(row);
            r.removeFromTop(kControlRowGap);
            auto policyRow = r.removeFromTop(kControlRowHeight);
            resetPolicyBox.setBounds(policyRow.removeFromRight(policyRow.getWidth() * 3 / 5));
            resetPolicyLabel.setBounds(policyRow);
        }

        r.removeFromTop(GuiConstants::groupGap);
        dividerY = r.removeFromTop(1).getY();
        r.removeFromTop(GuiConstants::groupGap);

        buildHeading.setBounds(r.removeFromTop(kHeadingHeight));
        r.removeFromTop(kHeadingGap);
        for (int i = 0; i < factKeys.size(); ++i) {
            Rectangle<int> row{r.removeFromTop(kFactRowHeight)};
            factValues[i]->setBounds(row.removeFromRight(row.getWidth() * 3 / 5));
            factKeys[i]->setBounds(row);
        }
    }

private:
    static constexpr int kWidth{252};
    static constexpr int kHeadingHeight{14};
    static constexpr int kHeadingGap{8};
    static constexpr int kSwatchHeight{26};
    static constexpr int kFactRowHeight{18};
    static constexpr int kControlRowHeight{24};
    static constexpr int kControlRowGap{6};

    void timerCallback() override {
        const int value = fluidSynthModel.getChannelDiagnostics(attachedVibratoChannel).modulation;
        cc1Value.setText(String(value) + (value == 0 ? " (inactive)" : ""), dontSendNotification);
    }
    void syncVibratoChannel() {
        const int ch = juce::jlimit(0, 15, static_cast<int>(valueTreeState.state
            .getChildWithName("uiState").getProperty("selectedChannel", 1)) - 1);
        if (ch != attachedVibratoChannel) {
            vibratoScaleAttachment.reset();
            attachedVibratoChannel = ch;
            vibratoChannelBox.setSelectedId(ch + 1, dontSendNotification);
            vibratoScaleAttachment = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
                valueTreeState, "vibratoScaleCh" + String(ch + 1), vibratoScaleBox);
        }
        timerCallback();
    }
    void valueTreePropertyChanged(ValueTree& tree, const Identifier& property) override {
        if (tree.getType() == StringRef("uiState") && property == StringRef("selectedChannel"))
            syncVibratoChannel();
    }
    AudioProcessorValueTreeState& valueTreeState;
    FluidSynthModel& fluidSynthModel;
    Label vibratoChannelLabel, vibratoScaleLabel, cc1Label, cc1Value;
    juce::ComboBox vibratoChannelBox, vibratoScaleBox;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> vibratoScaleAttachment;
    int attachedVibratoChannel{-1};
    std::vector<Fact> facts;
    Label accentHeading, soundHeading, midiHeading, buildHeading;
    Label bendRangeLabel, bendScaleLabel, resetPolicyLabel, interpolationLabel;
    AccentListLookAndFeel accentListLookAndFeel;
    juce::ComboBox accentBox;
    juce::ComboBox bendRangeBox, bendScaleBox, resetPolicyBox, interpolationBox;
    // Declared after their boxes, so destroyed first.
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> resetPolicyAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> bendRangeAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> bendScaleAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> interpolationAttachment;
    juce::OwnedArray<Label> factKeys, factValues;
    int dividerY{0};
    int midiDividerY{0};
    int soundDividerY{0};
    std::function<void(Juicy16::Accent)> chooseAccent;
};

} // namespace

//==============================================================================
JuicySFAudioProcessorEditor::JuicySFAudioProcessorEditor(
    JuicySFAudioProcessor& p,
    AudioProcessorValueTreeState& state)
: AudioProcessorEditor{&p}
, audioProcessor{p}
, valueTreeState{state}
, midiKeyboard{p.keyboardState, SurjectiveMidiKeyboardComponent::horizontalKeyboard}
, channelRack{state, p.getFluidSynthModel()}
, filePicker{state}
, mixerPanel{state, p.getFluidSynthModel()}
{
    // Install the palette before children read colours. Per editor, not global:
    // a host runs several plugins in one process.
    setLookAndFeel(&lookAndFeel);
    applyAccentFromState();

    logo = juce::ImageCache::getFromMemory(BinaryData::juicy16logo_png,
                                     BinaryData::juicy16logo_pngSize);
    // Before the first setSize: resized() sizes the header from the logo width.
    logoButton.setLogo(logo);

    // No wider than the keyboard's full range.
    const int keyboardMaxWidth{juce::jmax(
        GuiConstants::minWidth,
        midiKeyboard.getTotalKeyboardWidth() + 2 * GuiConstants::padding)};

    setResizeLimits(
        GuiConstants::minWidth,
        GuiConstants::minHeight,
        keyboardMaxWidth,
        GuiConstants::maxHeight);
    // Some hosts (FL Studio's AU view) need the plugin's own resize corner.
    setResizable(true, true);

    lastUIWidth.referTo(state.state.getChildWithName("uiState").getPropertyAsValue("width",  nullptr));
    lastUIHeight.referTo(state.state.getChildWithName("uiState").getPropertyAsValue("height", nullptr));

    // Restore the saved size.
    setBoundsConstrained({getX(), getY(), static_cast<int>(lastUIWidth.getValue()),
                          static_cast<int>(lastUIHeight.getValue())});

    lastUIWidth.addListener(this);
    lastUIHeight.addListener(this);

    midiKeyboard.setName ("MIDI Keyboard");
    midiKeyboard.setTitle("MIDI Keyboard");
    midiKeyboard.setDescription(
        "Audition keyboard for the currently selected MIDI channel");
    midiKeyboard.setHelpText(
        "Select a channel row, then use this keyboard to audition its instrument.");

    midiKeyboard.setWantsKeyboardFocus(false);
    channelRack.setWantsKeyboardFocus(false);

    setWantsKeyboardFocus(true);
    addAndMakeVisible(midiKeyboard);

    addAndMakeVisible(mixerPanel);
    addAndMakeVisible(channelRack);
    addAndMakeVisible(filePicker);

    logoButton.onClick = [this] { showSettings(); };
    addAndMakeVisible(logoButton);

    // Include children, so pressing a knob counts as mouse use.
    addMouseListener(this, true);

    // Status bar: build version and bank-load result.
    statusLabel.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
    statusLabel.setName("Version and bank load status");
    statusLabel.setTitle("Version and bank load status");
    statusLabel.setDescription("Juicy16 version and latest sound-bank load result");
    statusLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(statusLabel);

    // setLookAndFeel() above ran before these children existed, and JUCE does not
    // resend on addChild, so tell the whole tree now.
    sendLookAndFeelChange();

    // Show MIDI on every channel; play notes on the selected channel.
    midiKeyboard.setMidiChannelsToDisplay(0xffff);
    valueTreeState.state.addListener(this);
    syncKeyboardChannel();
    syncStatusLabel();
}

void JuicySFAudioProcessorEditor::applyAccentFromState() {
    const String stored{valueTreeState.state.getChildWithName("uiState")
        .getProperty("accent", "sage").toString()};
    lookAndFeel.setAccent(Juicy16::accentFromName(stored));
}

void JuicySFAudioProcessorEditor::showSettings() {
    settingsCallout.reset();
    settingsContent.reset();
    // Facts in bug-report order.
    std::vector<SettingsPanel::Fact> facts{
        SettingsPanel::Fact{"Version", JUICY16_VERSION},
        SettingsPanel::Fact{"Engine", "FluidSynth " + String(FLUIDSYNTH_VERSION)},
        SettingsPanel::Fact{"Format",
                            audioProcessor.getWrapperTypeDescription(audioProcessor.wrapperType)},
        SettingsPanel::Fact{"Sample rate",
                            String(audioProcessor.getSampleRate(), 0) + " Hz"},
    };

    auto panel{std::make_unique<SettingsPanel>(
        valueTreeState,
        audioProcessor.getFluidSynthModel(),
        lookAndFeel.getAccent(),
        std::move(facts),
        [this](Juicy16::Accent accent) {
            valueTreeState.state.getChildWithName("uiState")
                .setProperty("accent", Juicy16::accentName(accent), nullptr);
            lookAndFeel.setAccent(accent);
            // Controls cache colours in lookAndFeelChanged(), so a repaint is not enough.
            sendLookAndFeelChange();
            if (auto* top{getTopLevelComponent()}; top != nullptr && top != this)
                top->repaint();
        })};
    panel->setLookAndFeel(&lookAndFeel);
    settingsContent = std::move(panel);
    settingsCallout = std::make_unique<juce::CallOutBox>(
        *settingsContent, getLocalArea(&logoButton, logoButton.getLocalBounds()), this);
    settingsCallout->enterModalState(isShowing());
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
    const String text{String::fromUTF8(
        "Juicy16 v" JUICY16_VERSION " \xe2\x80\x94 ") + message};
    statusLabel.setText(text, dontSendNotification);
    statusLabel.setTooltip(message);
    // Both are palette tokens that clear WCAG AA on the status bar.
    statusLabel.setColour(
        Label::textColourId,
        lookAndFeel.findColour(status == "error" ? Juicy16::textErrorColourId
                                                 : Juicy16::textLabelColourId));
}

void JuicySFAudioProcessorEditor::valueTreePropertyChanged(ValueTree& tree, const Identifier& property) {
    if (tree.getType() == StringRef("uiState") && property == StringRef("selectedChannel"))
        syncKeyboardChannel();
    if (tree.getType() == StringRef("soundFont")
        && (property == StringRef("loadStatus") || property == StringRef("loadMessage")))
        syncStatusLabel();
}

// Stored window size changed.
void JuicySFAudioProcessorEditor::valueChanged(Value&) {
    setBoundsConstrained({getX(), getY(), static_cast<int>(lastUIWidth.getValue()),
                          static_cast<int>(lastUIHeight.getValue())});
}

JuicySFAudioProcessorEditor::~JuicySFAudioProcessorEditor()
{
    // Settings listeners and attachments must die before the processor.
    settingsCallout.reset();
    settingsContent.reset();
    removeMouseListener(this);
    valueTreeState.state.removeListener(this);
    lastUIWidth.removeListener(this);
    lastUIHeight.removeListener(this);
    setLookAndFeel(nullptr);
}

//==============================================================================
void JuicySFAudioProcessorEditor::paint (Graphics& g)
{
    // Opaque component: fill completely.
    g.fillAll(findColour(Juicy16::windowBackgroundColourId));

    const int width{getWidth()};
    Rectangle<int> header{0, 0, width, GuiConstants::headerHeight};
    g.setColour(findColour(Juicy16::headerBackgroundColourId));
    g.fillRect(header);
    g.setColour(findColour(Juicy16::borderColourId));
    g.fillRect(0, header.getBottom() - 1, width, 1);

    Rectangle<int> statusBar{0, getHeight() - GuiConstants::statusBarHeight,
                             width, GuiConstants::statusBarHeight};
    g.setColour(findColour(Juicy16::panelBackgroundColourId));
    g.fillRect(statusBar);
    g.setColour(findColour(Juicy16::borderColourId));
    g.fillRect(statusBar.getX(), statusBar.getY(), width, 1);

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
    // All metrics come from GuiConstants, like defaultHeight, so they stay in sync.
    Rectangle<int> r{getLocalBounds()};

    Rectangle<int> header{r.removeFromTop(GuiConstants::headerHeight)};
    header.reduce(GuiConstants::padding, 0);
    // A clickable box around the wordmark over the full header height.
    logoButton.setBounds(
        header.removeFromLeft(logoButton.logoWidth() + GuiConstants::innerPadding));
    // Extra space so the wordmark and bank field read as separate groups.
    header.removeFromLeft(GuiConstants::innerPadding);
    filePicker.setBounds(header.withSizeKeepingCentre(
        header.getWidth(), GuiConstants::filePickerHeight));

    statusLabel.setBounds(r.removeFromBottom(GuiConstants::statusBarHeight)
                              .reduced(GuiConstants::padding, 0));
    midiKeyboard.setBounds(r.removeFromBottom(GuiConstants::pianoHeight));

    mixerPanel.setBounds(r.removeFromRight(GuiConstants::panelWidth + 1));
    channelRack.setBounds(r);

    lastUIWidth = getWidth();
    lastUIHeight = getHeight();
}

bool JuicySFAudioProcessorEditor::keyPressed(const KeyPress &key) {
    // Any key press means keyboard use: show focus rings until the next click.
    setFocusRingsVisible(true);
    // All other keys play the on-screen keyboard.
    return midiKeyboard.keyPressed(key);
}

void JuicySFAudioProcessorEditor::setFocusRingsVisible(bool visible) {
    if (Juicy16::focusRingsVisible() == visible)
        return;
    Juicy16::setFocusRingsVisible(visible);
    // Rings are drawn across the tree, so repaint all of it.
    repaint();
    if (auto* top{getTopLevelComponent()}; top != nullptr && top != this)
        top->repaint();
}

void JuicySFAudioProcessorEditor::mouseDown(const juce::MouseEvent&) {
    setFocusRingsVisible(false);
}

bool JuicySFAudioProcessorEditor::keyStateChanged (bool isKeyDown) {
    return midiKeyboard.keyStateChanged(isKeyDown);
}
