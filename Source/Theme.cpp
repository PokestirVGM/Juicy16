#include "Theme.h"
#include "GuiConstants.h"

namespace Juicy16 {

namespace {
// The only place in the plugin allowed to name a colour. Neutral greys, no
// blue-green cast; every value that carries text was chosen to clear WCAG AA
// (4.5:1) against every background it can be drawn on, and every value that only
// draws a shape to clear 3:1.
const juce::Colour kWindow          {0xff1c1c1c};
const juce::Colour kHeader          {0xff262626};
const juce::Colour kPanel           {0xff232323};
const juce::Colour kInput           {0xff171717};
const juce::Colour kControl         {0xff232323};
const juce::Colour kBorder          {0xff333333};
const juce::Colour kSubtleBorder    {0xff303030};
const juce::Colour kControlBorder   {0xff363636};
const juce::Colour kRowAlternate    {0xff212121};
const juce::Colour kRowSelected     {0xff2e2e2e};
const juce::Colour kTextPrimary     {0xffe4e4e4};
const juce::Colour kTextValue       {0xffa8a8a8};
const juce::Colour kTextLabel       {0xff949494};
const juce::Colour kTextFaint       {0xff7a7a7a};
const juce::Colour kTextError       {0xffef8f77};
const juce::Colour kKnobTrack       {0xff3d3d3d};
const juce::Colour kKeyboard        {0xff151515};

const juce::Colour kAccentSage      {0xff8fa47a};
const juce::Colour kAccentAmber     {0xffd8a24a};
const juce::Colour kAccentTerracotta{0xffc07a5e};
const juce::Colour kAccentNeutral   {0xff9d9d9d};
} // namespace

juce::Colour accentColour(Accent accent) {
    switch (accent) {
        case Accent::amber:      return kAccentAmber;
        case Accent::terracotta: return kAccentTerracotta;
        case Accent::neutral:    return kAccentNeutral;
        case Accent::sage:       break;
    }
    return kAccentSage;
}

juce::String accentName(Accent accent) {
    switch (accent) {
        case Accent::amber:      return "amber";
        case Accent::terracotta: return "terracotta";
        case Accent::neutral:    return "neutral";
        case Accent::sage:       break;
    }
    return "sage";
}

Accent accentFromName(const juce::String& name) {
    if (name == "amber")      return Accent::amber;
    if (name == "terracotta") return Accent::terracotta;
    if (name == "neutral")    return Accent::neutral;
    return Accent::sage;
}

PluginLookAndFeel::PluginLookAndFeel() {
    applyTokens();
}

void PluginLookAndFeel::setAccent(Accent newAccent) {
    accent = newAccent;
    applyTokens();
}

void PluginLookAndFeel::applyTokens() {
    const juce::Colour kAccent{accentColour(accent)};

    setColour(windowBackgroundColourId,   kWindow);
    setColour(headerBackgroundColourId,   kHeader);
    setColour(panelBackgroundColourId,    kPanel);
    setColour(inputBackgroundColourId,    kInput);
    setColour(controlBackgroundColourId,  kControl);
    setColour(borderColourId,             kBorder);
    setColour(subtleBorderColourId,       kSubtleBorder);
    setColour(controlBorderColourId,      kControlBorder);
    setColour(rowAlternateColourId,       kRowAlternate);
    setColour(rowSelectedColourId,        kRowSelected);
    setColour(textPrimaryColourId,        kTextPrimary);
    setColour(textValueColourId,          kTextValue);
    setColour(textLabelColourId,          kTextLabel);
    setColour(textFaintColourId,          kTextFaint);
    setColour(textErrorColourId,          kTextError);
    setColour(knobTrackColourId,          kKnobTrack);
    setColour(accentColourId,             kAccent);
    setColour(keyboardBackgroundColourId, kKeyboard);

    // Stock JUCE ids, so any control the plugin has not styled by hand still
    // lands in the palette rather than in LookAndFeel_V4's blue-green scheme.
    setColour(juce::ResizableWindow::backgroundColourId, kWindow);
    setColour(juce::DocumentWindow::textColourId,        kTextPrimary);

    setColour(juce::Label::textColourId,                 kTextPrimary);
    setColour(juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId,              juce::Colours::transparentBlack);
    setColour(juce::Label::textWhenEditingColourId,      kTextPrimary);
    setColour(juce::Label::backgroundWhenEditingColourId, kInput);
    setColour(juce::Label::outlineWhenEditingColourId,   kAccent);

    setColour(juce::ListBox::backgroundColourId,         kWindow);
    setColour(juce::ListBox::textColourId,               kTextPrimary);
    setColour(juce::ListBox::outlineColourId,            kBorder);

    setColour(juce::TableHeaderComponent::backgroundColourId, kWindow);
    setColour(juce::TableHeaderComponent::textColourId,       kTextLabel);
    setColour(juce::TableHeaderComponent::outlineColourId,    kBorder);
    setColour(juce::TableHeaderComponent::highlightColourId,  kRowSelected);

    setColour(juce::ComboBox::backgroundColourId,        kInput);
    setColour(juce::ComboBox::textColourId,              kTextPrimary);
    setColour(juce::ComboBox::outlineColourId,           kBorder);
    setColour(juce::ComboBox::buttonColourId,            kTextFaint);
    setColour(juce::ComboBox::arrowColourId,             kTextFaint);
    setColour(juce::ComboBox::focusedOutlineColourId,    kAccent);

    setColour(juce::PopupMenu::backgroundColourId,          kPanel);
    setColour(juce::PopupMenu::textColourId,                kTextPrimary);
    setColour(juce::PopupMenu::headerTextColourId,          kTextLabel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.28f));
    setColour(juce::PopupMenu::highlightedTextColourId,     kTextPrimary);

    setColour(juce::TextButton::buttonColourId,          kControl);
    setColour(juce::TextButton::buttonOnColourId,        kAccent);
    setColour(juce::TextButton::textColourOffId,         kTextLabel);
    // Accent fills are light; the on-state label has to darken to stay legible.
    setColour(juce::TextButton::textColourOnId,          kWindow);

    setColour(juce::Slider::backgroundColourId,          kKnobTrack);
    setColour(juce::Slider::thumbColourId,               kAccent);
    setColour(juce::Slider::trackColourId,               kAccent);
    setColour(juce::Slider::rotarySliderFillColourId,    kAccent);
    setColour(juce::Slider::rotarySliderOutlineColourId, kKnobTrack);
    setColour(juce::Slider::textBoxTextColourId,         kTextValue);
    setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId,    kAccent.withAlpha(0.3f));

    setColour(juce::TextEditor::backgroundColourId,      kInput);
    setColour(juce::TextEditor::textColourId,            kTextPrimary);
    setColour(juce::TextEditor::outlineColourId,         kBorder);
    setColour(juce::TextEditor::focusedOutlineColourId,  kAccent);
    setColour(juce::TextEditor::highlightColourId,       kAccent.withAlpha(0.3f));

    setColour(juce::ScrollBar::backgroundColourId,       juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId,            kControlBorder);
    setColour(juce::ScrollBar::trackColourId,            juce::Colours::transparentBlack);

    setColour(juce::TooltipWindow::backgroundColourId,   kPanel);
    setColour(juce::TooltipWindow::textColourId,         kTextPrimary);
    setColour(juce::TooltipWindow::outlineColourId,      kBorder);

    // LookAndFeel_V4::initialiseColours does NOT cover every ColourId a JUCE
    // control can ask for, and findColour asserts and returns black for one it
    // has never been given. These are the ids this editor's controls reach for
    // that the V4 scheme leaves unset.
    setColour(juce::DrawableButton::backgroundColourId,   juce::Colours::transparentBlack);
    setColour(juce::DrawableButton::backgroundOnColourId, kAccent.withAlpha(0.25f));
    setColour(juce::DrawableButton::textColourId,         kTextLabel);
    setColour(juce::DrawableButton::textColourOnId,       kTextPrimary);

    setColour(juce::ToggleButton::textColourId,          kTextPrimary);
    setColour(juce::ToggleButton::tickColourId,          kAccent);
    setColour(juce::ToggleButton::tickDisabledColourId,  kControlBorder);

    setColour(juce::GroupComponent::outlineColourId,     kSubtleBorder);
    setColour(juce::GroupComponent::textColourId,        kTextLabel);

    setColour(juce::MidiKeyboardComponent::whiteNoteColourId,     juce::Colour{0xffcfcfcf});
    setColour(juce::MidiKeyboardComponent::blackNoteColourId,     juce::Colour{0xff1a1a1a});
    setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour{0xff4a4a4a});
    setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, kAccent.withAlpha(0.4f));
    setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, kAccent.withAlpha(0.75f));
    setColour(juce::MidiKeyboardComponent::textLabelColourId,     kTextFaint);
    setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, kControl);
    setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, kTextLabel);
    setColour(juce::MidiKeyboardComponent::shadowColourId,        juce::Colours::transparentBlack);
}

void PluginLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width,
                                   int height, float sliderPosProportional,
                                   float rotaryStartAngle, float rotaryEndAngle,
                                   juce::Slider& slider) {
    const auto bounds{juce::Rectangle<int>{x, y, width, height}.toFloat().reduced(1.0f)};
    const float diameter{juce::jmin(bounds.getWidth(), bounds.getHeight())};
    const auto centre{bounds.getCentre()};
    const float thickness{juce::jmax(2.0f, diameter * GuiConstants::knobArcThickness)};
    const float radius{(diameter - thickness) * 0.5f};
    const float angle{rotaryStartAngle
        + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle)};
    // Bipolar controls (pan) fill outward from twelve o'clock rather than from
    // the left stop, so "centred" reads as no fill at all.
    const bool bipolar{static_cast<bool>(slider.getProperties().getWithDefault("bipolar", false))};
    const float originAngle{bipolar
        ? (rotaryStartAngle + rotaryEndAngle) * 0.5f
        : rotaryStartAngle};

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                        rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
    g.strokePath(track, juce::PathStrokeType{thickness, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded});

    if (std::abs(angle - originAngle) > 1.0e-4f) {
        juce::Path fill;
        fill.addCentredArc(centre.x, centre.y, radius, radius, 0.0f,
                           juce::jmin(originAngle, angle),
                           juce::jmax(originAngle, angle), true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId)
                        .withMultipliedAlpha(slider.isEnabled() ? 1.0f : 0.4f));
        g.strokePath(fill, juce::PathStrokeType{thickness, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded});
    }

    // Pointer. Kept inside the arc so a small knob stays readable at row height.
    juce::Path pointer;
    const float pointerLength{radius * 0.72f};
    const float pointerThickness{juce::jmax(1.5f, thickness * 0.62f)};
    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -pointerLength,
                                pointerThickness, pointerLength,
                                pointerThickness * 0.5f);
    pointer.applyTransform(
        juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    g.setColour(slider.findColour(juce::Slider::textBoxTextColourId).brighter(0.35f));
    g.fillPath(pointer);

    if (slider.hasKeyboardFocus(false)) {
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.drawEllipse(bounds.withSizeKeepingCentre(diameter, diameter), 1.0f);
    }
}

void PluginLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width,
                                   int height, float sliderPos,
                                   float minSliderPos, float maxSliderPos,
                                   juce::Slider::SliderStyle style,
                                   juce::Slider& slider) {
    LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos,
                                     minSliderPos, maxSliderPos, style, slider);
    if (slider.hasKeyboardFocus(false)) {
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId));
        g.drawRect(juce::Rectangle<int>{x, y, width, height}.toFloat(), 1.0f);
    }
}

juce::Label* PluginLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label{LookAndFeel_V4::createSliderTextBox(slider)};
    label->setFont(juce::Font{juce::FontOptions{GuiConstants::valueFontHeight}});
    label->setJustificationType(juce::Justification::centred);
    // A value readout is text beside a knob, not a boxed field. LookAndFeel_V2
    // gives the label a border and fill from the slider's textBox colours; the
    // palette has no chrome for it, so clear them here rather than leaving a
    // stray outline that came from no token.
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::textColourId,
                     slider.findColour(juce::Slider::textBoxTextColourId));
    return label;
}

void PluginLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height,
                               bool /*isButtonDown*/, int /*buttonX*/,
                               int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                               juce::ComboBox& box) {
    const auto bounds{juce::Rectangle<int>{0, 0, width, height}.toFloat()};
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, GuiConstants::cornerRadius);
    g.setColour(box.hasKeyboardFocus(false)
        ? box.findColour(juce::ComboBox::focusedOutlineColourId)
        : box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), GuiConstants::cornerRadius, 1.0f);

    // Chevron, matching the approved mockup: three-quarter-width strokes rather
    // than JUCE's filled triangle.
    const float size{6.0f};
    const float cx{static_cast<float>(width) - GuiConstants::innerPadding - size * 0.5f};
    const float cy{static_cast<float>(height) * 0.5f};
    juce::Path chevron;
    chevron.startNewSubPath(cx - size * 0.5f, cy - size * 0.25f);
    chevron.lineTo(cx, cy + size * 0.4f);
    chevron.lineTo(cx + size * 0.5f, cy - size * 0.25f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(chevron, juce::PathStrokeType{1.6f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded});
}

void PluginLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(GuiConstants::innerPadding, 0,
                    box.getWidth() - GuiConstants::innerPadding * 3,
                    box.getHeight());
    label.setFont(getComboBoxFont(box));
}

juce::Font PluginLookAndFeel::getComboBoxFont(juce::ComboBox&) {
    return juce::Font{juce::FontOptions{GuiConstants::bodyFontHeight}};
}

juce::Font PluginLookAndFeel::getPopupMenuFont() {
    return juce::Font{juce::FontOptions{GuiConstants::bodyFontHeight}};
}

void PluginLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                       const juce::Colour& backgroundColour,
                                       bool shouldDrawButtonAsHighlighted,
                                       bool shouldDrawButtonAsDown) {
    const auto bounds{button.getLocalBounds().toFloat().reduced(0.5f)};
    juce::Colour fill{backgroundColour};
    if (shouldDrawButtonAsDown)
        fill = fill.brighter(0.18f);
    else if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.09f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, GuiConstants::cornerRadius);
    g.setColour(button.hasKeyboardFocus(false)
        ? findColour(accentColourId)
        : findColour(controlBorderColourId));
    g.drawRoundedRectangle(bounds, GuiConstants::cornerRadius, 1.0f);
}

juce::Font PluginLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return juce::Font{juce::FontOptions{
        juce::jmin(GuiConstants::bodyFontHeight,
                   static_cast<float>(buttonHeight) * 0.68f)}};
}

void PluginLookAndFeel::drawToggleButton(juce::Graphics& g,
                                         juce::ToggleButton& button,
                                         bool shouldDrawButtonAsHighlighted,
                                         bool /*shouldDrawButtonAsDown*/) {
    const auto area{button.getLocalBounds().toFloat()};
    // The switch keeps the mockup's 26x14 proportions whatever room it is given,
    // and any label text sits to its left.
    const float height{juce::jmin(area.getHeight(), 16.0f)};
    const float width{height * 26.0f / 14.0f};
    const auto track{juce::Rectangle<float>{width, height}
        .withCentre({area.getRight() - width * 0.5f, area.getCentreY()})};

    const bool on{button.getToggleState()};
    juce::Colour fill{on ? button.findColour(juce::ToggleButton::tickColourId)
                         : findColour(controlBackgroundColourId)};
    if (shouldDrawButtonAsHighlighted)
        fill = fill.brighter(0.12f);
    g.setColour(fill);
    g.fillRoundedRectangle(track, height * 0.5f);
    if (!on) {
        g.setColour(findColour(controlBorderColourId));
        g.drawRoundedRectangle(track.reduced(0.5f), height * 0.5f, 1.0f);
    }
    if (button.hasKeyboardFocus(false)) {
        g.setColour(findColour(accentColourId));
        g.drawRoundedRectangle(track.expanded(2.0f), (height + 4.0f) * 0.5f, 1.0f);
    }

    const float inset{height * 0.15f};
    const float knobSize{height - inset * 2.0f};
    g.setColour(on ? findColour(windowBackgroundColourId)
                   : findColour(textLabelColourId));
    g.fillEllipse(on ? track.getRight() - inset - knobSize : track.getX() + inset,
                  track.getY() + inset, knobSize, knobSize);

    if (button.getButtonText().isNotEmpty()) {
        g.setColour(button.findColour(juce::ToggleButton::textColourId));
        g.setFont(juce::Font{juce::FontOptions{GuiConstants::bodyFontHeight}});
        g.drawText(button.getButtonText(),
                   area.withTrimmedRight(width + GuiConstants::innerPadding).toNearestInt(),
                   juce::Justification::centredLeft, true);
    }
}

void PluginLookAndFeel::drawTableHeaderBackground(juce::Graphics& g,
                                            juce::TableHeaderComponent& header) {
    g.fillAll(header.findColour(juce::TableHeaderComponent::backgroundColourId));
    g.setColour(header.findColour(juce::TableHeaderComponent::outlineColourId));
    g.fillRect(0, header.getHeight() - 1, header.getWidth(), 1);
}

void PluginLookAndFeel::drawTableHeaderColumn(juce::Graphics& g,
                                        juce::TableHeaderComponent& header,
                                        const juce::String& columnName,
                                        int /*columnId*/, int width, int height,
                                        bool /*isMouseOver*/, bool /*isMouseDown*/,
                                        int /*columnFlags*/) {
    if (columnName.isEmpty())
        return;
    g.setColour(header.findColour(juce::TableHeaderComponent::textColourId));
    g.setFont(juce::Font{juce::FontOptions{GuiConstants::labelFontHeight}});
    // A header sits over its column's own content: the channel number is right
    // aligned, the knob columns are centred, everything else reads left to
    // right. The column carries the alignment as a component property so this
    // stays a drawing decision and the rack stays the one that knows its shape.
    const auto stored{header.getProperties().getWithDefault(
        "headerJustification" + columnName, {})};
    const auto justification{stored.isVoid()
        ? juce::Justification::centredLeft
        : juce::Justification{static_cast<int>(stored)}};
    g.drawText(columnName.toUpperCase(),
               juce::Rectangle<int>{width, height}.reduced(GuiConstants::innerPadding, 0),
               justification, false);
}

} // namespace Juicy16
