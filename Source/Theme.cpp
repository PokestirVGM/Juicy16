#include "Theme.h"
#include "GuiConstants.h"

namespace Juicy16 {

namespace {
// The only place colours are named. Text colours clear WCAG AA (4.5:1) on every
// background they appear on; shape-only colours clear 3:1.
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
const juce::Colour kTextValue       {0xffc8c8c8};
const juce::Colour kTextLabel       {0xffb4b4b4};
const juce::Colour kTextFaint       {0xff909090};
const juce::Colour kTextError       {0xffef8f77};
const juce::Colour kKnobTrack       {0xff3d3d3d};
const juce::Colour kKeyboard        {0xff151515};
const juce::Colour kMuteActive      {0xffd0705e};
const juce::Colour kFocusRing       {0xff9a9a9a};
// A scrim over a silenced row, not a colour.
const juce::Colour kRowSilenced     {0xa61c1c1c};

// Accents share one mid-luminance band so each clears 3:1 on every background.
const juce::Colour kAccentSage      {0xff8fa47a};
const juce::Colour kAccentOlive     {0xffa8a05c};
const juce::Colour kAccentAmber     {0xffd8a24a};
const juce::Colour kAccentTerracotta{0xffc07a5e};
const juce::Colour kAccentRose      {0xffcf8096};
const juce::Colour kAccentMagenta   {0xffc281b6};
const juce::Colour kAccentViolet    {0xffa684c6};
const juce::Colour kAccentIndigo    {0xff8a8ed2};
const juce::Colour kAccentSteel     {0xff7f9bb5};
const juce::Colour kAccentIce       {0xff6fa6bd};
const juce::Colour kAccentTeal      {0xff5ba69d};
const juce::Colour kAccentNeutral   {0xff9d9d9d};
} // namespace

const std::vector<Accent>& allAccents() {
    static const std::vector<Accent> accents{
        Accent::sage, Accent::olive, Accent::amber, Accent::terracotta,
        Accent::rose, Accent::magenta, Accent::violet, Accent::indigo,
        Accent::steel, Accent::ice, Accent::teal, Accent::neutral};
    return accents;
}

juce::Colour accentColour(Accent accent) {
    switch (accent) {
        case Accent::olive:      return kAccentOlive;
        case Accent::amber:      return kAccentAmber;
        case Accent::terracotta: return kAccentTerracotta;
        case Accent::rose:       return kAccentRose;
        case Accent::magenta:    return kAccentMagenta;
        case Accent::violet:     return kAccentViolet;
        case Accent::indigo:     return kAccentIndigo;
        case Accent::steel:      return kAccentSteel;
        case Accent::ice:        return kAccentIce;
        case Accent::teal:       return kAccentTeal;
        case Accent::neutral:    return kAccentNeutral;
        case Accent::sage:       break;
    }
    return kAccentSage;
}

juce::String accentName(Accent accent) {
    switch (accent) {
        case Accent::olive:      return "olive";
        case Accent::amber:      return "amber";
        case Accent::terracotta: return "terracotta";
        case Accent::rose:       return "rose";
        case Accent::magenta:    return "magenta";
        case Accent::violet:     return "violet";
        case Accent::indigo:     return "indigo";
        case Accent::steel:      return "steel";
        case Accent::ice:        return "ice";
        case Accent::teal:       return "teal";
        case Accent::neutral:    return "neutral";
        case Accent::sage:       break;
    }
    return "sage";
}

// Unknown names fall back to the default so newer projects still open.
Accent accentFromName(const juce::String& name) {
    for (const auto accent : allAccents())
        if (name == accentName(accent))
            return accent;
    return Accent::sage;
}

namespace {
bool focusRingsAreVisible{false};
} // namespace

bool focusRingsVisible() noexcept { return focusRingsAreVisible; }
void setFocusRingsVisible(bool visible) noexcept { focusRingsAreVisible = visible; }

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
    setColour(muteActiveColourId,         kMuteActive);
    setColour(focusRingColourId,          kFocusRing);
    setColour(rowSilencedColourId,        kRowSilenced);
    setColour(keyboardBackgroundColourId, kKeyboard);

    // Stock JUCE ids, so unstyled controls still use the palette.
    setColour(juce::ResizableWindow::backgroundColourId, kWindow);
    setColour(juce::DocumentWindow::textColourId,        kTextPrimary);

    setColour(juce::Label::textColourId,                 kTextPrimary);
    setColour(juce::Label::backgroundColourId,           juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId,              juce::Colours::transparentBlack);
    setColour(juce::Label::textWhenEditingColourId,      kTextPrimary);
    setColour(juce::Label::backgroundWhenEditingColourId, kInput);
    setColour(juce::Label::outlineWhenEditingColourId,   kFocusRing);

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
    setColour(juce::ComboBox::focusedOutlineColourId,    kFocusRing);

    setColour(juce::PopupMenu::backgroundColourId,          kPanel);
    setColour(juce::PopupMenu::textColourId,                kTextPrimary);
    setColour(juce::PopupMenu::headerTextColourId,          kTextLabel);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, kRowSelected);
    setColour(juce::PopupMenu::highlightedTextColourId,     kTextPrimary);

    setColour(juce::TextButton::buttonColourId,          kControl);
    setColour(juce::TextButton::buttonOnColourId,        kAccent);
    setColour(juce::TextButton::textColourOffId,         kTextLabel);
    // Accent fills are light; the on-state label darkens.
    setColour(juce::TextButton::textColourOnId,          kWindow);

    setColour(juce::Slider::backgroundColourId,          kKnobTrack);
    setColour(juce::Slider::thumbColourId,               kAccent);
    setColour(juce::Slider::trackColourId,               kAccent);
    setColour(juce::Slider::rotarySliderFillColourId,    kAccent);
    setColour(juce::Slider::rotarySliderOutlineColourId, kKnobTrack);
    setColour(juce::Slider::textBoxTextColourId,         kTextValue);
    setColour(juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId,      juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId,    kControlBorder);

    setColour(juce::TextEditor::backgroundColourId,      kInput);
    setColour(juce::TextEditor::textColourId,            kTextPrimary);
    setColour(juce::TextEditor::outlineColourId,         kBorder);
    setColour(juce::TextEditor::focusedOutlineColourId,  kFocusRing);
    setColour(juce::TextEditor::highlightColourId,       kControlBorder);

    setColour(juce::ScrollBar::backgroundColourId,       juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId,            kControlBorder);
    setColour(juce::ScrollBar::trackColourId,            juce::Colours::transparentBlack);

    setColour(juce::TooltipWindow::backgroundColourId,   kPanel);
    setColour(juce::TooltipWindow::textColourId,         kTextPrimary);
    setColour(juce::TooltipWindow::outlineColourId,      kBorder);

    // Ids LookAndFeel_V4 leaves unset; findColour would assert and return black.
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
    setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId,
              kFocusRing.withAlpha(0.35f));
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
    // Bipolar controls (pan) fill from twelve o'clock.
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

    // Pointer, kept inside the arc for small knobs.
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

    if ((slider.hasKeyboardFocus(false) && focusRingsVisible())) {
        g.setColour(findColour(focusRingColourId).withAlpha(0.55f));
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
    if ((slider.hasKeyboardFocus(false) && focusRingsVisible())) {
        g.setColour(findColour(focusRingColourId).withAlpha(0.55f));
        g.drawRect(juce::Rectangle<int>{x, y, width, height}.toFloat(), 1.0f);
    }
}

juce::Label* PluginLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label{LookAndFeel_V4::createSliderTextBox(slider)};
    label->setFont(juce::Font{juce::FontOptions{GuiConstants::valueFontHeight}});
    label->setJustificationType(juce::Justification::centred);
    // Readouts are plain text: clear the border and fill V2 gives the label.
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
    g.setColour((box.hasKeyboardFocus(false) && focusRingsVisible())
        ? box.findColour(juce::ComboBox::focusedOutlineColourId)
        : box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(0.5f), GuiConstants::cornerRadius, 1.0f);

    // Stroked chevron.
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
    g.setColour((button.hasKeyboardFocus(false) && focusRingsVisible())
        ? findColour(focusRingColourId)
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
    // Fixed 26x14 switch proportions; label text sits to the left.
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
    if ((button.hasKeyboardFocus(false) && focusRingsVisible())) {
        g.setColour(findColour(focusRingColourId).withAlpha(0.55f));
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

void PluginLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&,
                                                 juce::Graphics& g,
                                                 const juce::Path& path,
                                                 juce::Image&) {
    g.setColour(findColour(panelBackgroundColourId));
    g.fillPath(path);
    g.setColour(findColour(borderColourId));
    g.strokePath(path, juce::PathStrokeType{1.0f});
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
    // Alignment comes from the rack's "headerJustification<column>" property.
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
