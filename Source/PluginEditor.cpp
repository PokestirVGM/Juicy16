/*
  ==============================================================================

    The Juicy16 editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "GuiConstants.h"
#include "Theme.h"
#include <BinaryData.h>

namespace {

// A gear, drawn rather than shipped as a second asset. One closed outline with
// no self-overlap - alternating root and tooth radius with a flat top on each
// tooth - so the even-odd rule punches only the bore.
// Accent display names live here so both the dropdown and the list that drops
// out of it spell them the same way.
String accentDisplayName(Juicy16::Accent accent) {
    const String name{Juicy16::accentName(accent)};
    return name.substring(0, 1).toUpperCase() + name.substring(1);
}

// Scoped to the accent dropdown alone: every row in the open list carries a
// swatch of the colour it selects, so the accent can be seen before it is
// chosen rather than only after. Derives from the plugin's LookAndFeel so
// scoping this one control does not opt it out of the palette.
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

        // Drawn at the trailing edge, where nothing else in the row lands: the
        // tick and the text both start from the left.
        for (const auto accent : Juicy16::allAccents()) {
            if (accentDisplayName(accent) != text)
                continue;
            const float size{static_cast<float>(
                juce::jmin(10, juce::jmax(4, area.getHeight() - 10)))};
            const Rectangle<float> swatch{
                static_cast<float>(area.getRight()) - size - GuiConstants::innerPadding,
                static_cast<float>(area.getCentreY()) - size * 0.5f,
                size, size};
            g.setColour(Juicy16::accentColour(accent));
            g.fillRoundedRectangle(swatch, 2.0f);
            break;
        }
    }
};

// The settings popover: the accent choice, plus the engine facts worth quoting
// in a bug report. It exists so later settings have somewhere to land instead of
// being bolted onto the header one at a time.
//
// The facts are KEY/VALUE rows, not a block of text. As a bare four-line label
// they read as debug output: "Standalone" and "48000 Hz" with nothing saying
// what either one is.
class SettingsPanel final : public Component {
public:
    struct Fact { String key; String value; };

    // accentName() is an identifier, stored in state; the popover shows a label.
    static String displayName(Juicy16::Accent accent) { return accentDisplayName(accent); }

    static int indexOfAccent(Juicy16::Accent accent) {
        const auto& accents{Juicy16::allAccents()};
        for (std::size_t i = 0; i < accents.size(); ++i)
            if (accents[i] == accent)
                return static_cast<int>(i);
        return 0;
    }

    SettingsPanel(AudioProcessorValueTreeState& state,
                  Juicy16::Accent current,
                  std::vector<Fact> factsToShow,
                  std::function<void(Juicy16::Accent)> onAccentChosen)
    : facts{std::move(factsToShow)}
    , chooseAccent{std::move(onAccentChosen)}
    {
        setName("Settings");
        setTitle("Settings");
        setDescription("Juicy16 settings: accent colour, MIDI bend compensation, and build information");

        midiHeading.setText("MIDI", dontSendNotification);
        midiHeading.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
        midiHeading.setAccessible(false);
        addAndMakeVisible(midiHeading);

        // Host bend compensation. Both are real parameters, so a project keeps
        // them; the popover is just where they live.
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

        // Twelve accents are too many to swatch across a 252px popover, so the
        // list carries the names and each row is drawn in the colour it selects.
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
            // The popover is not a child of the editor, so the editor's
            // sendLookAndFeelChange() does not reach it. It shares the same
            // LookAndFeel object, so it has to be told separately.
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
            // The value carries the key as its accessible name, so a screen
            // reader announces "Engine: FluidSynth 2.5.5" rather than a bare
            // version string.
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
                    + kHeadingHeight + kHeadingGap
                    + 2 * kControlRowHeight + kControlRowGap
                    + GuiConstants::groupGap + 1 + GuiConstants::groupGap
                    + kHeadingHeight + kHeadingGap
                    + static_cast<int>(facts.size()) * kFactRowHeight);
    }

    ~SettingsPanel() override {
        // The scoped LookAndFeel is a member, so it must be off the ComboBox
        // before either goes away.
        accentBox.setLookAndFeel(nullptr);
    }

    void paint(Graphics& g) override {
        g.fillAll(findColour(Juicy16::panelBackgroundColourId));
        g.setColour(findColour(Juicy16::subtleBorderColourId));
        for (const int y : {midiDividerY, dividerY})
            g.fillRect(GuiConstants::padding, y, getWidth() - GuiConstants::padding * 2, 1);
    }

    void lookAndFeelChanged() override {
        auto& lookAndFeel{getLookAndFeel()};
        const Colour label{lookAndFeel.findColour(Juicy16::textLabelColourId)};
        for (Label* heading : {&accentHeading, &midiHeading, &buildHeading,
                               &bendRangeLabel, &bendScaleLabel})
            heading->setColour(Label::textColourId, label);
        // The closed dropdown draws its text in the accent it currently selects,
        // so the chosen hue is visible without opening the list.
        accentBox.setColour(juce::ComboBox::textColourId,
                            lookAndFeel.findColour(Juicy16::accentColourId));
        for (Label* key : factKeys)
            key->setColour(Label::textColourId, label);
        for (Label* value : factValues)
            value->setColour(Label::textColourId,
                             lookAndFeel.findColour(Juicy16::textPrimaryColourId));
    }

    void resized() override {
        Rectangle<int> r{getLocalBounds().reduced(GuiConstants::padding)};

        accentHeading.setBounds(r.removeFromTop(kHeadingHeight));
        r.removeFromTop(kHeadingGap);
        accentBox.setBounds(r.removeFromTop(kSwatchHeight));

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

    std::vector<Fact> facts;
    Label accentHeading, midiHeading, buildHeading;
    Label bendRangeLabel, bendScaleLabel;
    AccentListLookAndFeel accentListLookAndFeel;
    juce::ComboBox accentBox;
    juce::ComboBox bendRangeBox, bendScaleBox;
    // Declared after the boxes they attach to, so they are destroyed first.
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> bendRangeAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> bendScaleAttachment;
    juce::OwnedArray<Label> factKeys, factValues;
    int dividerY{0};
    int midiDividerY{0};
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
, mixerPanel{state}
{
    // Install the palette before any child is constructed below reads a colour.
    // Set on the editor rather than globally: a host runs several plugins in one
    // process, and LookAndFeel::setDefaultLookAndFeel would reach all of them.
    setLookAndFeel(&lookAndFeel);
    applyAccentFromState();

    logo = juce::ImageCache::getFromMemory(BinaryData::juicy16logo_png,
                                     BinaryData::juicy16logo_pngSize);
    // Handed over BEFORE the first setSize below. resized() sizes the header from
    // logoButton.logoWidth(), so a logo installed after that first layout left the
    // wordmark in an 8px box - clipped until the user resized the window and the
    // layout ran again.
    logoButton.setLogo(logo);

    // Cap the width at the on-screen keyboard's own natural size (its full MIDI
    // range at its fixed key width): resizing wider than that would just add blank
    // space past the last key, so there's no reason to allow it.
    const int keyboardMaxWidth{juce::jmax(
        GuiConstants::minWidth,
        midiKeyboard.getTotalKeyboardWidth() + 2 * GuiConstants::padding)};

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

    // true = also report clicks on nested children, so pressing a knob counts as
    // mouse use and puts the focus rings away.
    addMouseListener(this, true);

    // status bar: build version and a visible bank-load result
    statusLabel.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
    statusLabel.setName("Version and bank load status");
    statusLabel.setTitle("Version and bank load status");
    statusLabel.setDescription("Juicy16 version and latest sound-bank load result");
    statusLabel.setMinimumHorizontalScale(0.7f);
    addAndMakeVisible(statusLabel);

    // Every child is added by now, so tell the whole tree the LookAndFeel is in
    // place. setLookAndFeel() above fired sendLookAndFeelChange() when NONE of
    // these were children yet - JUCE does not re-send it when a child is added
    // later - so any component that resolves its colours in lookAndFeelChanged()
    // never heard about the palette. That is what left "No bank loaded" drawing
    // in the black that a missing ColourId falls back to.
    sendLookAndFeelChange();

    // keyboard: light up for MIDI on any channel, but send notes on the channel
    // selected in the list, so clicking a row lets you audition its instrument.
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
    // Named facts, in the order a bug report wants them.
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
        lookAndFeel.getAccent(),
        std::move(facts),
        [this](Juicy16::Accent accent) {
            valueTreeState.state.getChildWithName("uiState")
                .setProperty("accent", Juicy16::accentName(accent), nullptr);
            lookAndFeel.setAccent(accent);
            // Not just a repaint. Controls resolve and CACHE their colours in
            // lookAndFeelChanged() - the mixer panel says so in as many words -
            // so a bare repaint redrew them in the accent they had cached, and
            // the new one only appeared where a colour happened to be looked up
            // live. sendLookAndFeelChange() walks the tree telling every child to
            // re-resolve, and repaints as it goes.
            sendLookAndFeelChange();
            if (auto* top{getTopLevelComponent()}; top != nullptr && top != this)
                top->repaint();
        })};
    panel->setLookAndFeel(&lookAndFeel);
    juce::CallOutBox::launchAsynchronously(
        std::move(panel),
        getLocalArea(&logoButton, logoButton.getLocalBounds()),
        this);
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
    // Both states are palette tokens; both clear WCAG AA on the status bar's own
    // background, which is what makes the error state readable rather than merely
    // red.
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

// called when the stored window size changes
void JuicySFAudioProcessorEditor::valueChanged(Value&) {
    setSize(lastUIWidth.getValue(), lastUIHeight.getValue());
}

JuicySFAudioProcessorEditor::~JuicySFAudioProcessorEditor()
{
    removeMouseListener(this);
    valueTreeState.state.removeListener(this);
    lastUIWidth.removeListener(this);
    lastUIHeight.removeListener(this);
    setLookAndFeel(nullptr);
}

//==============================================================================
void JuicySFAudioProcessorEditor::paint (Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
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
    // Every metric is a GuiConstants token, and GuiConstants::defaultHeight is
    // derived from the same ones, so the layout here cannot drift out of sync
    // with the default window size.
    Rectangle<int> r{getLocalBounds()};

    Rectangle<int> header{r.removeFromTop(GuiConstants::headerHeight)};
    header.reduce(GuiConstants::padding, 0);
    // The wordmark is the settings button, so it needs a clickable box rather
    // than just its own width: a little breathing room either side, over the
    // full header height.
    logoButton.setBounds(
        header.removeFromLeft(logoButton.logoWidth() + GuiConstants::innerPadding));
    // The wordmark is a different KIND of thing from the field beside it, so it
    // needs more than the window's own margin between them or the two read as
    // one run-on group.
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
    // Any key press means the user is working by keyboard, so focus rings become
    // visible from here until the next mouse click. Unhandled keys - Tab above
    // all - bubble up to the editor, which is what makes this catch the moment
    // keyboard traversal starts.
    setFocusRingsVisible(true);
    // patch selection now lives in per-row dropdowns; all key input drives the
    // on-screen MIDI keyboard.
    return midiKeyboard.keyPressed(key);
}

void JuicySFAudioProcessorEditor::setFocusRingsVisible(bool visible) {
    if (Juicy16::focusRingsVisible() == visible)
        return;
    Juicy16::setFocusRingsVisible(visible);
    // The rings are drawn by the LookAndFeel across the whole tree, so the whole
    // tree has to be asked to redraw.
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
