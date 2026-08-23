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

// A gear, drawn rather than shipped as a second asset.
//
// Two attempts failed before this one, both instructive. A star polygon - eight
// points joined straight to eight valleys - reads as an asterisk, because a gear
// has flat-topped teeth, not spikes. Overlapping rounded-rectangle teeth on a
// hub disc looked right until the even-odd rule needed for the bore cancelled
// every overlap, detaching the teeth into petals.
//
// So the silhouette is ONE closed outline with no self-overlap: around the
// circle, alternating between the root radius and the tooth radius with a short
// flat top on each tooth. Even-odd then punches only the bore, because the bore
// is the only subpath that overlaps anything.
std::unique_ptr<juce::Drawable> makeGearDrawable(juce::Colour colour) {
    // Six teeth, not eight: a STROKED gear has two outlines per tooth, and at
    // header size seven of them left barely a pixel of gap between the flanks,
    // which reads as fuzz rather than as teeth. Fewer, larger teeth survive
    // the scale.
    constexpr int teeth{6};
    constexpr float outerRadius{10.4f};
    constexpr float rootRadius{6.4f};
    constexpr float boreRadius{3.1f};

    const float step{juce::MathConstants<float>::twoPi / static_cast<float>(teeth)};
    // Angular half-widths: the tooth is narrower than the gap it stands in, so
    // the flanks slope outward the way a real tooth's do.
    const float toothHalf{step * 0.21f};
    const float rootHalf{step * 0.33f};

    const auto pointAt{[](float radius, float angle) {
        return juce::Point<float>{std::sin(angle) * radius, -std::cos(angle) * radius};
    }};

    juce::Path gear;
    for (int i = 0; i < teeth; ++i) {
        const float centre{step * static_cast<float>(i)};
        const juce::Point<float> flankIn{pointAt(rootRadius, centre - rootHalf)};
        if (i == 0)
            gear.startNewSubPath(flankIn);
        else
            gear.lineTo(flankIn);
        gear.lineTo(pointAt(outerRadius, centre - toothHalf));
        gear.lineTo(pointAt(outerRadius, centre + toothHalf));
        gear.lineTo(pointAt(rootRadius, centre + rootHalf));
        // Round the root between this tooth and the next, so the valleys are
        // curved rather than a straight chord across the hub.
        const float nextCentre{step * static_cast<float>(i + 1)};
        gear.quadraticTo(pointAt(rootRadius * 1.04f, (centre + nextCentre) * 0.5f),
                         pointAt(rootRadius, nextCentre - rootHalf));
    }
    gear.closeSubPath();

    gear.addEllipse(-boreRadius, -boreRadius, boreRadius * 2.0f, boreRadius * 2.0f);
    gear.applyTransform(juce::AffineTransform::translation(12.0f, 12.0f));

    auto drawable{std::make_unique<juce::DrawablePath>()};
    drawable->setPath(gear);
    // STROKED, not filled. The folder beside it in the same header is line art,
    // and a solid gear next to a hollow folder is two icon families in one strip
    // - which is what made the pair look wrong rather than either one alone.
    // No even-odd rule is needed once it is stroked: the bore is simply a second
    // circle, drawn rather than punched.
    drawable->setFill(juce::Colours::transparentBlack);
    drawable->setStrokeFill(colour);
    // 1.8 units on the same 24-unit grid the folder is drawn on, so the two
    // strokes match once both are scaled into the header.
    drawable->setStrokeType(juce::PathStrokeType{1.6f,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded});
    return drawable;
}

// The settings popover: the accent choice, plus the engine facts worth quoting
// in a bug report. It exists so later settings have somewhere to land instead of
// being bolted onto the header one at a time.
class SettingsPanel final : public Component {
public:
    SettingsPanel(Juicy16::Accent current,
                  const String& engineFacts,
                  std::function<void(Juicy16::Accent)> onAccentChosen)
    : chooseAccent{std::move(onAccentChosen)}
    {
        setName("Settings");
        setTitle("Settings");
        setDescription("Juicy16 settings: accent colour and build information");

        accentHeading.setText("ACCENT", dontSendNotification);
        accentHeading.setFont(Font{juce::FontOptions{GuiConstants::labelFontHeight}});
        accentHeading.setAccessible(false);
        addAndMakeVisible(accentHeading);

        for (const auto accent : {Juicy16::Accent::sage, Juicy16::Accent::amber,
                                  Juicy16::Accent::terracotta, Juicy16::Accent::neutral}) {
            const String label{Juicy16::accentName(accent)};
            auto button{std::make_unique<juce::TextButton>(
                label.substring(0, 1).toUpperCase() + label.substring(1))};
            const String name{Juicy16::accentName(accent) + " accent"};
            button->setName(name);
            button->setTitle(name);
            button->setDescription(String{"Use the "} + name);
            button->setTooltip(button->getDescription());
            button->setClickingTogglesState(true);
            button->setRadioGroupId(1);
            button->setWantsKeyboardFocus(true);
            button->setToggleState(accent == current, dontSendNotification);
            // The swatch IS the button: its own accent fills it when chosen and
            // colours its label when not, so the choice is visible rather than
            // described.
            button->setColour(juce::TextButton::buttonOnColourId,
                              Juicy16::accentColour(accent));
            button->setColour(juce::TextButton::textColourOffId,
                              Juicy16::accentColour(accent));
            button->onClick = [this, accent] {
                if (chooseAccent != nullptr)
                    chooseAccent(accent);
            };
            addAndMakeVisible(*button);
            accentButtons.add(std::move(button));
        }

        facts.setText(engineFacts, dontSendNotification);
        facts.setFont(Font{juce::FontOptions{GuiConstants::valueFontHeight}});
        facts.setJustificationType(Justification::topLeft);
        facts.setName("Build information");
        facts.setTitle("Build information");
        facts.setDescription("Juicy16 version and engine details");
        addAndMakeVisible(facts);

        setSize(232, 182);
    }

    void paint(Graphics& g) override {
        g.fillAll(findColour(Juicy16::panelBackgroundColourId));
    }

    // Resolved here, not in the constructor: the panel is built before it is
    // given the plugin's LookAndFeel, and the default one has never heard of
    // Juicy16's ColourIds - it asserts and returns black.
    void lookAndFeelChanged() override {
        auto& lookAndFeel{getLookAndFeel()};
        accentHeading.setColour(Label::textColourId,
                                lookAndFeel.findColour(Juicy16::textLabelColourId));
        facts.setColour(Label::textColourId,
                        lookAndFeel.findColour(Juicy16::textValueColourId));
    }

    void resized() override {
        Rectangle<int> r{getLocalBounds().reduced(GuiConstants::padding)};
        accentHeading.setBounds(r.removeFromTop(14));
        r.removeFromTop(6);
        // Two rows of two: four accent names do not fit side by side at the
        // popover's width, and a truncated colour name is worse than a wrap.
        const int width{(r.getWidth() - 6) / 2};
        for (int pair = 0; pair < 2; ++pair) {
            Rectangle<int> row{r.removeFromTop(24)};
            for (int column = 0; column < 2; ++column) {
                accentButtons[pair * 2 + column]->setBounds(row.removeFromLeft(width));
                row.removeFromLeft(6);
            }
            r.removeFromTop(6);
        }
        r.removeFromTop(GuiConstants::innerPadding);
        facts.setBounds(r);
    }

private:
    Label accentHeading;
    juce::OwnedArray<juce::TextButton> accentButtons;
    Label facts;
    std::function<void(Juicy16::Accent)> chooseAccent;
};

} // namespace

//==============================================================================
JuicySFAudioProcessorEditor::JuicySFAudioProcessorEditor(
    JuicySFAudioProcessor& p,
    AudioProcessorValueTreeState& state)
: AudioProcessorEditor{&p}
, processor{p}
, valueTreeState{state}
, midiKeyboard{p.keyboardState, SurjectiveMidiKeyboardComponent::horizontalKeyboard}
, channelRack{state, p.getFluidSynthModel()}
, filePicker{state}
, mixerPanel{state, p.getFluidSynthModel()}
, settingsButton{"Settings", juce::DrawableButton::ImageFitted}
{
    // Install the palette before any child is constructed below reads a colour.
    // Set on the editor rather than globally: a host runs several plugins in one
    // process, and LookAndFeel::setDefaultLookAndFeel would reach all of them.
    setLookAndFeel(&lookAndFeel);
    applyAccentFromState();

    logo = juce::ImageCache::getFromMemory(BinaryData::juicy16logo_png,
                                     BinaryData::juicy16logo_pngSize);

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

    settingsButton.setImages(
        makeGearDrawable(lookAndFeel.findColour(Juicy16::textLabelColourId)).get());
    // DrawableButton::ImageFitted scales the path to FILL the button, so a gear
    // whose outline spans 18.8 of its 24-unit grid rendered at the full 24px
    // with a 2.3px stroke - visibly bigger and heavier than the 1.17px folder
    // beside it. The indent brings it to a 12px icon, which puts both strokes at
    // about 1.2px and both icons at the same visual weight.
    settingsButton.setEdgeIndent(4);
    settingsButton.setName("Settings");
    settingsButton.setTitle("Settings");
    settingsButton.setDescription("Open Juicy16 settings");
    settingsButton.setHelpText("Accent colour and build information.");
    settingsButton.setTooltip(settingsButton.getHelpText());
    settingsButton.setWantsKeyboardFocus(true);
    settingsButton.onClick = [this] { showSettings(); };
    addAndMakeVisible(settingsButton);

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
    const String facts{
        String::fromUTF8("Juicy16 " JUICY16_VERSION "\n")
        + "FluidSynth " + String(FLUIDSYNTH_VERSION) + "\n"
        + processor.getWrapperTypeDescription(processor.wrapperType) + "\n"
        + String(processor.getSampleRate(), 0) + " Hz"};

    auto panel{std::make_unique<SettingsPanel>(
        lookAndFeel.getAccent(),
        facts,
        [this](Juicy16::Accent accent) {
            valueTreeState.state.getChildWithName("uiState")
                .setProperty("accent", Juicy16::accentName(accent), nullptr);
            lookAndFeel.setAccent(accent);
            // The palette changed under the whole tree, including the popover.
            if (auto* top{getTopLevelComponent()})
                top->repaint();
            repaint();
        })};
    panel->setLookAndFeel(&lookAndFeel);
    juce::CallOutBox::launchAsynchronously(
        std::move(panel),
        getLocalArea(&settingsButton, settingsButton.getLocalBounds()),
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

    if (logo.isValid()) {
        // Drawn at the header's own scale, from the source asset's aspect ratio,
        // so the wordmark is never stretched.
        const int logoWidth{juce::roundToInt(
            static_cast<float>(GuiConstants::logoHeight)
            * static_cast<float>(logo.getWidth())
            / static_cast<float>(logo.getHeight()))};
        g.drawImage(
            logo,
            Rectangle<int>{GuiConstants::padding,
                           (GuiConstants::headerHeight - GuiConstants::logoHeight) / 2,
                           logoWidth,
                           GuiConstants::logoHeight}.toFloat(),
            juce::RectanglePlacement::centred);
    }

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
    const int logoWidth{logo.isValid()
        ? juce::roundToInt(static_cast<float>(GuiConstants::logoHeight)
                           * static_cast<float>(logo.getWidth())
                           / static_cast<float>(logo.getHeight()))
        : 0};
    // The wordmark is a different KIND of thing from the field beside it, so it
    // needs more than the window's own margin between them or the two read as
    // one run-on group.
    header.removeFromLeft(logoWidth + GuiConstants::padding + GuiConstants::innerPadding);
    Rectangle<int> headerRow{header.withSizeKeepingCentre(
        header.getWidth(), GuiConstants::filePickerHeight)};
    settingsButton.setBounds(
        headerRow.removeFromRight(GuiConstants::settingsButtonWidth));
    // Small, because both buttons already carry their own icon inset: the gap
    // the eye sees is this plus roughly five pixels from each side.
    headerRow.removeFromRight(2);
    filePicker.setBounds(headerRow);

    statusLabel.setBounds(r.removeFromBottom(GuiConstants::statusBarHeight)
                              .reduced(GuiConstants::padding, 0));
    midiKeyboard.setBounds(r.removeFromBottom(GuiConstants::pianoHeight));

    mixerPanel.setBounds(r.removeFromRight(GuiConstants::panelWidth + 1));
    channelRack.setBounds(r);

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
