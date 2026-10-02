#include "MidiPlayerComponent.h"
#include "Theme.h"
#include <cmath>

namespace {
constexpr double speedFactors[] = {0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 3.0, 4.0};
}

MidiPlayerComponent::TimeField::TimeField(MidiPlayerComponent& component, bool isStart)
    : owner(component), start(isStart) {
    setName(start ? "Set loop start" : "Set loop end");
    setTitle(start ? "Loop section start time" : "Loop section end time");
    setDescription("Editable loop boundary. Enter minutes:seconds or seconds.");
    setTooltip(juce::String(start ? "Start time. " : "End time. ")
        + "Click or press Enter to edit mm:ss or seconds. Right-click to use the playhead.");
    setFont(juce::Font{juce::FontOptions{GuiConstants::valueFontHeight}});
    setJustificationType(juce::Justification::centred);
    setBorderSize(juce::BorderSize<int>{0});
    setMinimumHorizontalScale(0.85f);
    setEditable(true, true, false);
    setWantsKeyboardFocus(true);
    onTextChange = [this] { owner.commitTimeInput(start); };
    onEditorShow = [this] {
        if (auto* editor = getCurrentTextEditor()) {
            editor->setInputRestrictions(32);
            editor->setJustification(juce::Justification::centred);
        }
    };
}

bool MidiPlayerComponent::TimeField::keyPressed(const juce::KeyPress& key) {
    if (key.getKeyCode() == juce::KeyPress::returnKey && isEnabled()) {
        showEditor();
        return true;
    }
    return juce::Label::keyPressed(key);
}

void MidiPlayerComponent::TimeField::paint(juce::Graphics& g) {
    juce::Label::paint(g);
    if (!isBeingEdited() && hasKeyboardFocus(false) && Juicy16::focusRingsVisible()) {
        g.setColour(findColour(Juicy16::focusRingColourId));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), GuiConstants::cornerRadius, 1.0f);
    }
}

std::unique_ptr<juce::AccessibilityHandler> MidiPlayerComponent::TimeField::createAccessibilityHandler() {
    class ValueInterface final : public juce::AccessibilityTextValueInterface {
    public:
        explicit ValueInterface(TimeField& component) : field(component) {}
        bool isReadOnly() const override { return !field.isEnabled(); }
        juce::String getCurrentValueAsString() const override { return field.getText(); }
        void setValueAsString(const juce::String& value) override {
            if (field.isEnabled())
                field.setText(value, juce::sendNotificationSync);
        }
    private:
        TimeField& field;
    };
    class Handler final : public juce::AccessibilityHandler {
    public:
        explicit Handler(TimeField& component)
            : juce::AccessibilityHandler(component, juce::AccessibilityRole::editableText,
                juce::AccessibilityActions{}.addAction(juce::AccessibilityActionType::press,
                    [&component] { component.showEditor(); }),
                {std::make_unique<ValueInterface>(component)}), field(component) {}
        juce::String getTitle() const override { return field.getTitle(); }
        juce::String getHelp() const override { return field.getTooltip(); }
        juce::AccessibleState getCurrentState() const override {
            return field.isBeingEdited() ? juce::AccessibleState{} : juce::AccessibilityHandler::getCurrentState();
        }
    private:
        TimeField& field;
    };
    return std::make_unique<Handler>(*this);
}

void MidiPlayerComponent::TimeField::mouseDown(const juce::MouseEvent& event) {
    if (!event.mods.isPopupMenu()) {
        juce::Label::mouseDown(event);
        return;
    }
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    menu.addItem(1, start ? "Set Start to playhead" : "Set End to playhead");
    const juce::Component::SafePointer<TimeField> safeThis{this};
    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(this), [safeThis](int result) {
        if (safeThis != nullptr && result == 1) {
            const auto status = safeThis->owner.processor.getMidiFilePlayer().getStatus();
            safeThis->owner.commitLoopMarker(safeThis->start, status.positionSeconds);
        }
    });
}

MidiPlayerComponent::LoopHandle::LoopHandle(MidiPlayerComponent& component, bool isStart)
    : owner(component), start(isStart) {
    setName(start ? "Loop section start" : "Loop section end");
    setTitle(getName());
    setDescription(start ? "Start of the loop section" : "End of the loop section");
    setTooltip("Drag to adjust this section boundary. Arrow keys move 0.1 seconds; Shift moves 1 second.");
    setSliderStyle(juce::Slider::LinearHorizontal);
    setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    textFromValueFunction = [](double seconds) { return formatTimeInput(seconds); };
    onValueChange = [this] { owner.commitLoopMarker(start, getValue()); };
}

void MidiPlayerComponent::LoopHandle::paint(juce::Graphics& g) {
    const float x = start ? static_cast<float>(getWidth() - 8) : 0.0f;
    const float y = static_cast<float>(getHeight()) * 0.5f - 8.0f;
    g.setColour(findColour(Juicy16::accentColourId).withAlpha(isMouseOverOrDragging() ? 1.0f : 0.8f));
    g.fillRoundedRectangle(x, y, 8.0f, 16.0f, GuiConstants::cornerRadius);
    g.setColour(findColour(Juicy16::inputBackgroundColourId).withAlpha(0.6f));
    g.fillRect(x + 3.0f, y + 4.0f, 1.0f, 8.0f);
    if (hasKeyboardFocus(false) && Juicy16::focusRingsVisible()) {
        g.setColour(findColour(Juicy16::focusRingColourId));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), GuiConstants::cornerRadius, 1.0f);
    }
}

void MidiPlayerComponent::LoopHandle::mouseDown(const juce::MouseEvent& event) {
    grabKeyboardFocus();
    dragScreenX = event.getScreenX();
    dragSeconds = getValue();
    owner.draggingLoopMarker = true;
}

void MidiPlayerComponent::LoopHandle::mouseDrag(const juce::MouseEvent& event) {
    if (owner.timelineTrackBounds.getWidth() > 0)
        owner.previewLoopMarker(start, dragSeconds + static_cast<double>(event.getScreenX() - dragScreenX)
            / static_cast<double>(owner.timelineTrackBounds.getWidth()) * owner.durationSeconds);
}

void MidiPlayerComponent::LoopHandle::mouseUp(const juce::MouseEvent&) {
    owner.draggingLoopMarker = false;
    owner.commitLoopMarker(start, start ? owner.loopStartSeconds : owner.loopEndSeconds);
}

bool MidiPlayerComponent::LoopHandle::keyPressed(const juce::KeyPress& key) {
    const int code = key.getKeyCode();
    const bool backwards = code == juce::KeyPress::leftKey || code == juce::KeyPress::downKey;
    const bool forwards = code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey;
    if (backwards || forwards) {
        const double step = key.getModifiers().isShiftDown() ? 1.0 : 0.1;
        owner.commitLoopMarker(start, getValue() + (backwards ? -step : step));
        return true;
    }
    return juce::Slider::keyPressed(key);
}

MidiPlayerComponent::MidiPlayerComponent(JuicySFAudioProcessor& owner)
    : processor(owner), loopStartTime(*this, true), loopEndTime(*this, false),
      startHandle(*this, true), endHandle(*this, false) {
    setName("MIDI file player");
    setTitle("MIDI file player");
    setDescription("Standalone MIDI file transport. Load a MIDI file and a sound bank, then play all channels through the rack.");
    setTooltip("Click or drag the seek track. In Section mode, drag the Start and End handles to set the loop range.");
    setOpaque(true);

    const auto configureButton = [this](juce::Button& button, const juce::String& name,
                                       const juce::String& tooltip) {
        button.setName(name);
        button.setTitle(name);
        button.setTooltip(tooltip);
        button.setWantsKeyboardFocus(true);
        addAndMakeVisible(button);
    };
    configureButton(loadButton, "Load MIDI file", "Open a .mid or .midi file. You can also drop a MIDI file onto Juicy16.");
    configureButton(playButton, "Play or pause MIDI file", "Play or pause the MIDI file. Space toggles playback.");
    configureButton(stopButton, "Stop MIDI file", "Stop playback and return to the beginning.");
    configureButton(loopButton, "Loop MIDI file", "Enable repeating playback. Choose Whole song or Section for the range.");
    configureButton(resetLoopButton, "Reset MIDI loop range", "Return to whole-song looping.");
    loadButton.onClick = [this] { chooseFile(); };
    playButton.onClick = [this] { togglePlayback(); };
    stopButton.onClick = [this] { processor.getMidiFilePlayer().stop(); refreshStatus(); };
    loopButton.onClick = [this] { processor.getMidiFilePlayer().setLooping(loopButton.getToggleState()); refreshStatus(); };
    resetLoopButton.onClick = [this] {
        const auto status = processor.getMidiFilePlayer().getStatus();
        processor.getMidiFilePlayer().setLoopRange(0.0, status.durationSeconds);
        sectionMode = false;
        errorMessage.clear();
        refreshStatus();
        resized();
        repaint(timelineBounds);
    };

    speedBox.setName("MIDI playback speed");
    speedBox.setTitle("MIDI playback speed");
    speedBox.setTooltip("Playback speed. Pitch stays unchanged.");
    speedBox.addItemList({"0.25x", "0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x", "3x", "4x"}, 1);
    speedBox.setSelectedId(4, juce::dontSendNotification);
    speedBox.onChange = [this] {
        const int index = speedBox.getSelectedId() - 1;
        if (index >= 0 && index < 9)
            processor.getMidiFilePlayer().setSpeed(speedFactors[index]);
        refreshStatus();
    };
    addAndMakeVisible(speedBox);

    loopModeBox.setName("MIDI loop mode");
    loopModeBox.setTitle("MIDI loop mode");
    loopModeBox.setTooltip("Whole song repeats from beginning to end. Section lets you drag Start and End on the timeline.");
    loopModeBox.addItem("Whole song", 1);
    loopModeBox.addItem("Section", 2);
    loopModeBox.onChange = [this] {
        sectionMode = loopModeBox.getSelectedId() == 2;
        if (!sectionMode) {
            const auto status = processor.getMidiFilePlayer().getStatus();
            processor.getMidiFilePlayer().setLoopRange(0.0, status.durationSeconds);
            errorMessage.clear();
        }
        refreshStatus();
        resized();
        repaint(timelineBounds);
    };
    addAndMakeVisible(loopModeBox);

    positionSlider.setName("MIDI playback position");
    positionSlider.setTitle("MIDI playback position");
    positionSlider.setTooltip("Click or drag to seek. Dragging previews the time; playback moves when you release.");
    positionSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    positionSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    positionSlider.setRange(0.0, 1.0);
    positionSlider.textFromValueFunction = [this](double seconds) { return formatTime(seconds, durationSeconds < 10.0); };
    positionSlider.onDragStart = [this] { scrubbingSlider = true; };
    positionSlider.onDragEnd = [this] {
        scrubbingSlider = false;
        processor.getMidiFilePlayer().seek(positionSlider.getValue());
        refreshStatus();
    };
    positionSlider.onValueChange = [this] {
        if (!scrubbingSlider)
            processor.getMidiFilePlayer().seek(positionSlider.getValue());
        refreshStatus();
    };
    addAndMakeVisible(positionSlider);
    addAndMakeVisible(startHandle);
    addAndMakeVisible(endHandle);
    addAndMakeVisible(loopStartTime);
    addAndMakeVisible(loopEndTime);

    const auto configureLabel = [this](juce::Label& label, const juce::String& name, float height) {
        label.setName(name);
        label.setFont(juce::Font{juce::FontOptions{height}});
        label.setMinimumHorizontalScale(0.85f);
        label.setBorderSize(juce::BorderSize<int>{0});
        addAndMakeVisible(label);
    };
    configureLabel(fileLabel, "MIDI file name", GuiConstants::bodyFontHeight);
    configureLabel(detailsLabel, "MIDI file details", GuiConstants::valueFontHeight);
    configureLabel(guidanceLabel, "MIDI player guidance", GuiConstants::labelFontHeight);
    configureLabel(timeLabel, "MIDI playback time", GuiConstants::valueFontHeight);
    configureLabel(loopRangeLabel, "MIDI loop range", GuiConstants::labelFontHeight);
    configureLabel(speedLabel, "MIDI speed label", GuiConstants::labelFontHeight);
    configureLabel(bpmLabel, "MIDI tempo", GuiConstants::valueFontHeight);
    configureLabel(loopStartCaption, "Loop start label", GuiConstants::labelFontHeight);
    configureLabel(loopEndCaption, "Loop end label", GuiConstants::labelFontHeight);
    loopStartCaption.setText("Start", juce::dontSendNotification);
    loopEndCaption.setText("End", juce::dontSendNotification);
    loopStartCaption.setJustificationType(juce::Justification::centredRight);
    loopEndCaption.setJustificationType(juce::Justification::centredRight);
    loopStartCaption.setAccessible(false);
    loopEndCaption.setAccessible(false);
    bpmLabel.setJustificationType(juce::Justification::centredRight);
    timeLabel.setJustificationType(juce::Justification::centredRight);
    speedLabel.setText("Speed", juce::dontSendNotification);
    loopRangeLabel.setText("Loop range", juce::dontSendNotification);
    loopRangeLabel.setVisible(false);
    speedLabel.setAccessible(false);
    refreshStatus();
    startTimerHz(20);
}

MidiPlayerComponent::~MidiPlayerComponent() {
    stopTimer();
    fileChooser.reset();
}

void MidiPlayerComponent::paint(juce::Graphics& g) {
    g.fillAll(findColour(Juicy16::panelBackgroundColourId));
    g.setColour(findColour(Juicy16::borderColourId));
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
    g.setColour(findColour(Juicy16::inputBackgroundColourId));
    g.fillRoundedRectangle(timelineBounds.toFloat(), GuiConstants::cornerRadius);
    g.setColour(findColour(Juicy16::controlBorderColourId));
    g.drawRoundedRectangle(timelineBounds.toFloat().reduced(0.5f), GuiConstants::cornerRadius, 1.0f);
    if (durationSeconds > 0.0 && !timelineTrackBounds.isEmpty()) {
        const auto track = timelineTrackBounds.toFloat();
        const auto xForTime = [&track, this](double seconds) {
            return track.getX() + static_cast<float>(seconds / durationSeconds) * track.getWidth();
        };
        const int labelTop = timelineBounds.getBottom() - 16;
        g.setFont(juce::Font{juce::FontOptions{GuiConstants::labelFontHeight}});
        for (int tick = 0; tick <= 4; ++tick) {
            const double seconds = durationSeconds * static_cast<double>(tick) / 4.0;
            const int x = juce::roundToInt(xForTime(seconds));
            g.setColour(findColour(Juicy16::subtleBorderColourId));
            g.drawVerticalLine(x, static_cast<float>(labelTop - 3), static_cast<float>(labelTop));
            g.setColour(findColour(Juicy16::textLabelColourId));
            const int labelWidth = 70;
            const int labelX = tick == 0 ? x : tick == 4 ? x - labelWidth : x - labelWidth / 2;
            const auto alignment = tick == 0 ? juce::Justification::centredLeft
                : tick == 4 ? juce::Justification::centredRight : juce::Justification::centred;
            g.drawText(formatTime(seconds, durationSeconds < 10.0), labelX, labelTop, labelWidth, 12, alignment);
        }
        if (sectionMode) {
            const float startX = xForTime(loopStartSeconds);
            const float endX = xForTime(loopEndSeconds);
            g.setColour(findColour(Juicy16::accentColourId).withAlpha(looping ? 0.16f : 0.07f));
            g.fillRoundedRectangle(startX, track.getCentreY() - 4.0f,
                                   juce::jmax(1.0f, endX - startX), 8.0f, 1.0f);
        }
    }
    g.setColour(findColour(Juicy16::subtleBorderColourId));
    const float controlsTop = static_cast<float>(playButton.getY() + 4);
    const float controlsBottom = static_cast<float>(playButton.getBottom() - 4);
    g.drawVerticalLine(transportDividerX, controlsTop, controlsBottom);
    g.drawVerticalLine(rangeDividerX, controlsTop, controlsBottom);
    if (draggingFile) {
        g.setColour(findColour(Juicy16::accentColourId));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), GuiConstants::cornerRadius, 1.0f);
    }
}

void MidiPlayerComponent::resized() {
    auto area = getLocalBounds().reduced(GuiConstants::padding);
    auto identity = area.removeFromTop(32);
    loadButton.setBounds(identity.removeFromRight(94).withSizeKeepingCentre(94, 28));
    identity.removeFromRight(GuiConstants::groupGap);
    fileLabel.setBounds(identity.removeFromTop(18));
    bpmLabel.setBounds(identity.removeFromRight(210));
    identity.removeFromRight(GuiConstants::innerPadding);
    detailsLabel.setBounds(identity);
    guidanceLabel.setBounds(identity);
    area.removeFromTop(GuiConstants::innerPadding);
    timelineBounds = area.removeFromTop(48);
    auto timeline = timelineBounds.reduced(GuiConstants::innerPadding, 4);
    timeLabel.setBounds(timeline.removeFromRight(132));
    timeline.removeFromRight(GuiConstants::innerPadding);
    positionSlider.setBounds(timeline.withHeight(26));
    // Ticks and custom loop markers share the native seek track's endpoints.
    const int thumbInset = positionSlider.getLookAndFeel().getSliderThumbRadius(positionSlider);
    timelineTrackBounds = positionSlider.getBounds().withTrimmedLeft(thumbInset).withTrimmedRight(thumbInset);
    positionLoopHandles();
    area.removeFromTop(GuiConstants::innerPadding);
    auto controls = area.removeFromTop(28);
    const auto takeControl = [&controls](int width) {
        const auto bounds = controls.removeFromLeft(width);
        controls.removeFromLeft(GuiConstants::innerPadding);
        return bounds;
    };
    playButton.setBounds(takeControl(76));
    stopButton.setBounds(takeControl(60));
    loopButton.setBounds(takeControl(74));
    transportDividerX = controls.getX() + GuiConstants::innerPadding;
    auto range = controls.removeFromRight(400);
    const auto modeBounds = range.removeFromLeft(104);
    loopModeBox.setBounds(sectionMode ? modeBounds : modeBounds.withX(getWidth() - GuiConstants::padding - 104));
    loopRangeLabel.setBounds(loopModeBox.getBounds());
    rangeDividerX = loopModeBox.getX() - GuiConstants::groupGap;
    range.removeFromLeft(GuiConstants::groupGap);
    loopStartCaption.setBounds(range.removeFromLeft(28));
    range.removeFromLeft(4);
    loopStartTime.setBounds(range.removeFromLeft(70).reduced(0, 2));
    range.removeFromLeft(GuiConstants::groupGap);
    loopEndCaption.setBounds(range.removeFromLeft(24));
    range.removeFromLeft(4);
    loopEndTime.setBounds(range.removeFromLeft(70).reduced(0, 2));
    range.removeFromLeft(GuiConstants::groupGap);
    resetLoopButton.setBounds(range);
    auto speed = controls.withSizeKeepingCentre(122, controls.getHeight());
    speedLabel.setBounds(speed.removeFromLeft(38));
    speed.removeFromLeft(GuiConstants::innerPadding);
    speedBox.setBounds(speed);
}

void MidiPlayerComponent::lookAndFeelChanged() {
    if (!getLookAndFeel().isColourSpecified(Juicy16::textPrimaryColourId))
        return;
    fileLabel.setColour(juce::Label::textColourId, findColour(Juicy16::textPrimaryColourId));
    for (auto* label : {&detailsLabel, &loopRangeLabel, &speedLabel, &loopStartCaption, &loopEndCaption})
        label->setColour(juce::Label::textColourId, findColour(Juicy16::textLabelColourId));
    timeLabel.setColour(juce::Label::textColourId, findColour(Juicy16::textValueColourId));
    bpmLabel.setColour(juce::Label::textColourId, findColour(Juicy16::textValueColourId));
    for (auto* field : {&loopStartTime, &loopEndTime}) {
        field->setColour(juce::Label::textColourId, findColour(Juicy16::textValueColourId));
        field->setColour(juce::Label::backgroundColourId, findColour(Juicy16::inputBackgroundColourId));
        field->setColour(juce::Label::outlineColourId, findColour(Juicy16::subtleBorderColourId));
    }
    refreshStatus();
    repaint();
}

juce::String MidiPlayerComponent::formatTime(double seconds, bool showTenths) {
    const double nonnegative = juce::jmax(0.0, seconds);
    const auto whole = static_cast<int>(std::floor(nonnegative));
    const auto time = juce::String(whole / 60) + ":" + juce::String(whole % 60).paddedLeft('0', 2);
    return showTenths ? time + "." + juce::String(static_cast<int>(nonnegative * 10.0) % 10) : time;
}

juce::String MidiPlayerComponent::formatTimeInput(double seconds) {
    const int hundredths = juce::roundToInt(juce::jmax(0.0, seconds) * 100.0);
    const int fraction = hundredths % 100;
    const auto time = juce::String(hundredths / 6000) + ":"
        + juce::String((hundredths / 100) % 60).paddedLeft('0', 2);
    return time + "." + (fraction % 10 == 0 ? juce::String(fraction / 10)
        : juce::String(fraction).paddedLeft('0', 2));
}

bool MidiPlayerComponent::parseTimeInput(const juce::String& input, double& seconds) {
    auto text = input.trim();
    if (text.isEmpty() || text.length() > 32)
        return false;
    if (text.endsWithIgnoreCase("s"))
        text = text.dropLastCharacters(1).trim();
    const auto parseNumber = [](const juce::String& value, double& result) {
        auto digits = value.trim();
        if (digits.startsWithChar('+') || digits.startsWithChar('-'))
            digits = digits.substring(1);
        if (digits.isEmpty() || !digits.containsOnly("0123456789.") || !digits.containsAnyOf("0123456789"))
            return false;
        int dots = 0;
        for (int i = 0; i < digits.length(); ++i)
            if (digits[i] == '.')
                ++dots;
        if (dots > 1)
            return false;
        result = value.trim().getDoubleValue();
        return std::isfinite(result);
    };
    const int colon = text.indexOfChar(':');
    if (colon < 0)
        return parseNumber(text, seconds);
    const auto minutesText = text.substring(0, colon).trim();
    if (minutesText.isEmpty() || !minutesText.containsOnly("0123456789"))
        return false;
    double remainder = 0.0;
    if (!parseNumber(text.substring(colon + 1), remainder) || remainder < 0.0 || remainder >= 60.0)
        return false;
    seconds = minutesText.getDoubleValue() * 60.0 + remainder;
    return std::isfinite(seconds);
}

void MidiPlayerComponent::commitTimeInput(bool isStart) {
    const auto& field = isStart ? loopStartTime : loopEndTime;
    double seconds = 0.0;
    if (parseTimeInput(field.getText(), seconds))
        commitLoopMarker(isStart, seconds);
    else {
        errorMessage = "Use mm:ss or seconds, for example 0:06.5 or 6.5.";
        refreshStatus();
    }
}

void MidiPlayerComponent::timerCallback() { refreshStatus(); }

void MidiPlayerComponent::refreshStatus() {
    const auto status = processor.getMidiFilePlayer().getStatus();
    const bool loaded = status.fileName.isNotEmpty() && status.durationSeconds > 0.0;
    const bool bankLoaded = processor.getFluidSynthModel().getLoadedFontPath().isNotEmpty();
    const bool customRange = status.loopStartSeconds > 0.001 || status.loopEndSeconds < status.durationSeconds - 0.001;
    const bool wasSectionMode = sectionMode;
    if (status.revision != fileRevision) {
        fileRevision = status.revision;
        sectionMode = customRange;
    } else if (customRange)
        sectionMode = true;
    const bool rangeChanged = !juce::exactlyEqual(durationSeconds, status.durationSeconds)
        || (!draggingLoopMarker && (!juce::exactlyEqual(loopStartSeconds, status.loopStartSeconds)
            || !juce::exactlyEqual(loopEndSeconds, status.loopEndSeconds))) || looping != status.looping;
    durationSeconds = status.durationSeconds;
    if (!draggingLoopMarker) {
        loopStartSeconds = status.loopStartSeconds;
        loopEndSeconds = status.loopEndSeconds;
    }
    looping = status.looping;
    fileLabel.setText(loaded ? status.fileName : "No MIDI file loaded", juce::dontSendNotification);
    fileLabel.setTooltip(status.fileName);
    detailsLabel.setText(loaded ? juce::String(status.trackCount) + (status.trackCount == 1 ? " track" : " tracks")
        + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(status.channelCount)
        + (status.channelCount == 1 ? " channel" : " channels")
        + (sectionMode ? juce::String::fromUTF8(" \xc2\xb7 Edit section times or drag handles") : juce::String{})
        : juce::String{}, juce::dontSendNotification);
    guidanceLabel.setText(errorMessage.isNotEmpty() ? errorMessage
        : !bankLoaded ? "Choose a sound bank in the header to enable playback."
        : !loaded ? "Load or drop a .mid or .midi file to play all 16 channels."
        : juce::String{}, juce::dontSendNotification);
    guidanceLabel.setVisible(guidanceLabel.getText().isNotEmpty());
    detailsLabel.setVisible(guidanceLabel.getText().isEmpty());
    bpmLabel.setVisible(loaded);
    const auto bpmText = [](double bpm) {
        const double rounded = std::round(bpm);
        return juce::String(bpm, std::abs(bpm - rounded) < 0.05 ? 0 : 1);
    };
    bpmLabel.setText(loaded ? bpmText(status.playbackTempoBpm) + " BPM"
        + (std::abs(status.speed - 1.0) > 0.001 ? "  (source " + bpmText(status.tempoBpm) + ")" : juce::String{})
        : juce::String{}, juce::dontSendNotification);
    bpmLabel.setTooltip("Current MIDI tempo: " + bpmText(status.tempoBpm) + " BPM. Effective playback tempo: "
                        + bpmText(status.playbackTempoBpm) + " BPM.");
    guidanceLabel.setTooltip(guidanceLabel.getText());
    if (getLookAndFeel().isColourSpecified(Juicy16::textPrimaryColourId))
        guidanceLabel.setColour(juce::Label::textColourId,
            findColour(errorMessage.isNotEmpty() ? Juicy16::textErrorColourId : Juicy16::textLabelColourId));
    playButton.setEnabled(loaded && bankLoaded);
    playButton.setButtonText(status.playing ? "Pause" : "Play");
    playButton.setToggleState(status.playing, juce::dontSendNotification);
    stopButton.setEnabled(loaded);
    loopButton.setEnabled(loaded && status.durationSeconds >= 0.05);
    loopButton.setToggleState(status.looping, juce::dontSendNotification);
    loopModeBox.setEnabled(loaded && status.durationSeconds >= 0.05);
    loopModeBox.setSelectedId(sectionMode ? 2 : 1, juce::dontSendNotification);
    speedBox.setEnabled(loaded);
    for (int i = 0; i < 9; ++i)
        if (std::abs(status.speed - speedFactors[i]) < 0.001)
            speedBox.setSelectedId(i + 1, juce::dontSendNotification);
    loopStartTime.setEnabled(loaded && status.durationSeconds >= 0.05);
    loopEndTime.setEnabled(loaded && status.durationSeconds >= 0.05);
    loopStartTime.setVisible(sectionMode);
    loopEndTime.setVisible(sectionMode);
    loopStartCaption.setVisible(sectionMode);
    loopEndCaption.setVisible(sectionMode);
    resetLoopButton.setVisible(sectionMode);
    resetLoopButton.setEnabled(loaded && status.durationSeconds >= 0.05);
    positionSlider.setEnabled(loaded);
    if (!scrubbingSlider) {
        const double maximum = juce::jmax(0.001, status.durationSeconds);
        if (!juce::exactlyEqual(positionSlider.getMaximum(), maximum))
            positionSlider.setRange(0.0, maximum);
        positionSlider.setValue(status.positionSeconds, juce::dontSendNotification);
    }
    const double shownPosition = scrubbingSlider ? positionSlider.getValue() : status.positionSeconds;
    const bool showTenths = status.durationSeconds < 10.0;
    timeLabel.setText(formatTime(shownPosition, showTenths) + " / " + formatTime(status.durationSeconds, showTenths),
                      juce::dontSendNotification);
    if (!loopStartTime.isBeingEdited())
        loopStartTime.setText(formatTimeInput(loopStartSeconds), juce::dontSendNotification);
    if (!loopEndTime.isBeingEdited())
        loopEndTime.setText(formatTimeInput(loopEndSeconds), juce::dontSendNotification);
    loopRangeLabel.setTooltip("Loop section: Start " + formatTime(loopStartSeconds, showTenths) + " to End "
                             + formatTime(loopEndSeconds, showTenths) + ". Drag the timeline handles, or set a boundary to the playhead.");
    loopRangeLabel.setDescription(loopRangeLabel.getTooltip());
    positionLoopHandles();
    if (wasSectionMode != sectionMode)
        resized();
    if (rangeChanged)
        repaint(timelineBounds);
}

void MidiPlayerComponent::positionLoopHandles() {
    const bool visible = sectionMode && durationSeconds >= 0.05 && !timelineTrackBounds.isEmpty();
    startHandle.setVisible(visible);
    endHandle.setVisible(visible);
    const double maximum = juce::jmax(0.001, durationSeconds);
    for (auto* handle : {&startHandle, &endHandle})
        if (!juce::exactlyEqual(handle->getMaximum(), maximum))
            handle->setRange(0.0, maximum);
    startHandle.setValue(loopStartSeconds, juce::dontSendNotification);
    endHandle.setValue(loopEndSeconds, juce::dontSendNotification);
    if (!visible)
        return;
    const auto track = timelineTrackBounds;
    const int startX = track.getX() + juce::roundToInt(loopStartSeconds / durationSeconds * static_cast<double>(track.getWidth()));
    const int endX = track.getX() + juce::roundToInt(loopEndSeconds / durationSeconds * static_cast<double>(track.getWidth()));
    // Opposing hit areas stay distinct even for a very short section.
    startHandle.setBounds(startX - 20, track.getY(), 20, track.getHeight());
    endHandle.setBounds(endX, track.getY(), 20, track.getHeight());
}

void MidiPlayerComponent::previewLoopMarker(bool isStart, double seconds) {
    if (durationSeconds < 0.05)
        return;
    const double minimum = juce::jmin(0.050001, durationSeconds);
    if (isStart)
        loopStartSeconds = juce::jlimit(0.0, juce::jmax(0.0, loopEndSeconds - minimum), seconds);
    else
        loopEndSeconds = juce::jlimit(juce::jmin(durationSeconds, loopStartSeconds + minimum), durationSeconds, seconds);
    positionLoopHandles();
    if (!loopStartTime.isBeingEdited())
        loopStartTime.setText(formatTimeInput(loopStartSeconds), juce::dontSendNotification);
    if (!loopEndTime.isBeingEdited())
        loopEndTime.setText(formatTimeInput(loopEndSeconds), juce::dontSendNotification);
    repaint(timelineBounds);
}

void MidiPlayerComponent::commitLoopMarker(bool isStart, double seconds) {
    const auto status = processor.getMidiFilePlayer().getStatus();
    if (status.durationSeconds < 0.05)
        return;
    const double minimum = juce::jmin(0.050001, status.durationSeconds);
    const double start = isStart
        ? juce::jlimit(0.0, juce::jmax(0.0, status.loopEndSeconds - minimum), seconds) : status.loopStartSeconds;
    const double end = isStart ? status.loopEndSeconds
        : juce::jlimit(juce::jmin(status.durationSeconds, status.loopStartSeconds + minimum), status.durationSeconds, seconds);
    if (processor.getMidiFilePlayer().setLoopRange(start, end)) {
        sectionMode = true;
        errorMessage.clear();
    } else
        errorMessage = "This section could not be restored. Choose an earlier start point.";
    refreshStatus();
    resized();
    repaint(timelineBounds);
}

void MidiPlayerComponent::showLoadResult(const juce::String& error) {
    errorMessage = error;
    refreshStatus();
}

void MidiPlayerComponent::togglePlayback() {
    const auto status = processor.getMidiFilePlayer().getStatus();
    if (status.playing)
        processor.getMidiFilePlayer().pause();
    else if (status.fileName.isNotEmpty() && processor.getFluidSynthModel().getLoadedFontPath().isNotEmpty())
        processor.getMidiFilePlayer().play();
    refreshStatus();
}

void MidiPlayerComponent::chooseFile() {
    if (fileChooser != nullptr)
        return;
    fileChooser = std::make_unique<juce::FileChooser>("Load MIDI file", juce::File{}, "*.mid;*.midi");
    const juce::Component::SafePointer<MidiPlayerComponent> safeThis{this};
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& chooser) {
            if (safeThis == nullptr)
                return;
            const auto file = chooser.getResult();
            if (file.existsAsFile())
                safeThis->loadFile(file);
            safeThis->fileChooser.reset();
        });
}

void MidiPlayerComponent::loadFile(const juce::File& file) {
    juce::String error;
    if (processor.getMidiFilePlayer().loadFile(file, error))
        errorMessage.clear();
    else
        errorMessage = error.isNotEmpty() ? error : "Could not load this MIDI file.";
    refreshStatus();
}

bool MidiPlayerComponent::isInterestedInFileDrag(const juce::StringArray& files) {
    return files.size() == 1 && juce::File{files[0]}.hasFileExtension("mid;midi");
}

void MidiPlayerComponent::filesDropped(const juce::StringArray& files, int, int) {
    draggingFile = false;
    if (isInterestedInFileDrag(files))
        loadFile(juce::File{files[0]});
    repaint();
}

void MidiPlayerComponent::fileDragEnter(const juce::StringArray& files, int, int) {
    draggingFile = isInterestedInFileDrag(files);
    repaint();
}

void MidiPlayerComponent::fileDragExit(const juce::StringArray&) {
    draggingFile = false;
    repaint();
}
