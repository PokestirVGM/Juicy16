// The 16-channel rack.

#include "ChannelListComponent.h"
#include "Theme.h"

using namespace std;

namespace {
// Accessible names and tooltips, built per row because cells are recycled.
String channelPrefix(int row) {
    return String{"MIDI channel "} + String(row + 1);
}
} // namespace

//==============================================================================
// PatchCell: instrument dropdown for one channel.
//==============================================================================
ChannelListComponent::PatchCell::PatchCell(ChannelListComponent& ownerRef)
: owner{ownerRef}
{
    addAndMakeVisible(combo);
    combo.onChange = [this] {
        owner.applyComboSelection(row, combo.getSelectedId());
    };
}

void ChannelListComponent::PatchCell::setRow(int newRow) {
    row = newRow;
    const String accessibleName{channelPrefix(row) + " instrument"};
    combo.setName(accessibleName);
    combo.setTitle(accessibleName);
    combo.setDescription(String{"Bank and preset selection for "} + accessibleName);
    combo.setHelpText(
        "Choose the starting instrument. Incoming Bank Select and Program Change may replace it.");

    // Repopulate only when the font's patch list changed.
    if (cellListVersion != owner.patchListVersion) {
        combo.clear(juce::dontSendNotification);
        for (size_t i = 0; i < owner.patches.size(); ++i)
            combo.addItem(patchLabel(owner.patches[i]), static_cast<int>(i) + 1);
        cellListVersion = owner.patchListVersion;
    }

    // Show the channel's current program without notifying (avoids a loop).
    ValueTree chNode{owner.valueTreeState.state.getChildWithName("channelPrograms")
        .getChildWithProperty("num", row)};
    int id{0};
    if (chNode.isValid()) {
        int idx{owner.patchIndexFor(chNode.getProperty("bank", 0),
                                    chNode.getProperty("preset", 0))};
        if (idx >= 0)
            id = idx + 1;
    }
    if (id != 0)
        combo.setSelectedId(id, juce::dontSendNotification);
    else if (chNode.isValid())
        // Saved patch is missing from the loaded font.
        combo.setText("Missing " + String(static_cast<int>(chNode.getProperty("bank", 0))) + ":"
                      + String(static_cast<int>(chNode.getProperty("preset", 0))),
                      juce::dontSendNotification);
    else
        combo.setText({}, juce::dontSendNotification);
}

void ChannelListComponent::PatchCell::resized() {
    // Half a group gap per side, so adjacent cells get a full gap between them.
    combo.setBounds(getLocalBounds().reduced(GuiConstants::groupGap / 2, 2));
}

//==============================================================================
// MuteSoloCell: bound to muteChN and soloChN.
//==============================================================================
ChannelListComponent::MuteSoloCell::MuteSoloCell(ChannelListComponent& ownerRef)
: owner{ownerRef}
{
    for (auto* button : {&mute, &solo}) {
        button->setClickingTogglesState(true);
        button->setWantsKeyboardFocus(true);
        button->setConnectedEdges(0);
        addAndMakeVisible(*button);
    }
}

void ChannelListComponent::MuteSoloCell::lookAndFeelChanged() {
    // Solo takes the accent, mute keeps its own red, so they differ by hue; both
    // use a dark label.
    auto& theme{getLookAndFeel()};
        if (!theme.isColourSpecified(Juicy16::textPrimaryColourId)) return;
    solo.setColour(juce::TextButton::buttonOnColourId,
                   theme.findColour(Juicy16::accentColourId));
    mute.setColour(juce::TextButton::buttonOnColourId,
                   theme.findColour(Juicy16::muteActiveColourId));
}

void ChannelListComponent::MuteSoloCell::setRow(int newRow) {
    if (row == newRow)
        return; // same channel: attachments still fit
    row = newRow;

    const String prefix{channelPrefix(row)};
    mute.setName(prefix + " mute");
    mute.setTitle(prefix + " mute");
    mute.setDescription(String{"Mute "} + prefix);
    mute.setHelpText(
        "Silences this channel's new notes. Not a MIDI controller: nothing in a MIDI file changes it.");
    mute.setTooltip(mute.getHelpText());
    solo.setName(prefix + " solo");
    solo.setTitle(prefix + " solo");
    solo.setDescription(String{"Solo "} + prefix);
    solo.setHelpText(
        "While any channel is soloed, every channel that is not soloed is silenced.");
    solo.setTooltip(solo.getHelpText());

    // Rebuild: an attachment binds one parameter for life.
    muteAttachment.reset();
    soloAttachment.reset();
    muteAttachment = make_unique<AudioProcessorValueTreeState::ButtonAttachment>(
        owner.valueTreeState, "muteCh" + String(row + 1), mute);
    soloAttachment = make_unique<AudioProcessorValueTreeState::ButtonAttachment>(
        owner.valueTreeState, "soloCh" + String(row + 1), solo);
}

void ChannelListComponent::MuteSoloCell::resized() {
    Rectangle<int> r{getLocalBounds().reduced(GuiConstants::groupGap / 2, 4)};
    const int gap{4};
    const int width{(r.getWidth() - gap) / 2};
    mute.setBounds(r.removeFromLeft(width));
    solo.setBounds(r.removeFromRight(width));
}

//==============================================================================
// MixerCell: volume or pan knob, bound to volChN / panChN.
//==============================================================================
ChannelListComponent::MixerCell::MixerCell(ChannelListComponent& ownerRef, int column)
: owner{ownerRef}
, columnId{column}
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    // Editable readout, so values can be typed.
    knob.setTextBoxStyle(juce::Slider::TextBoxRight, false,
                         GuiConstants::rowValueWidth, GuiConstants::rowKnobSize);
    knob.setRange(MidiConstants::midiMinValue, MidiConstants::midiMaxValue, 1);
    // Pan is bipolar: the arc grows from centre.
    knob.getProperties().set("bipolar", columnId == panColumn);
    // Sliders refuse focus by default; this enables arrow keys.
    knob.setWantsKeyboardFocus(true);
    addAndMakeVisible(knob);
}

void ChannelListComponent::MixerCell::setRow(int newRow) {
    if (row == newRow)
        return;
    row = newRow;

    const bool isVolume{columnId == volumeColumn};
    const bool isTrim{columnId == trimColumn};
    const String prefix{channelPrefix(row)};
    const String name{prefix + (isTrim ? " independent trim" : isVolume ? " volume" : " pan")};
    knob.setName(name);
    knob.setTitle(name);
    knob.setDescription(name);
    knob.getProperties().set("bipolar", isTrim || columnId == panColumn);
    if (isTrim) knob.setDoubleClickReturnValue(true, 0.0);
    knob.setHelpText(isTrim
        ? "Independent audio trim in dB, including this channel's effects. Incoming MIDI "
          "does not change it. Double-click for 0 dB."
        : isVolume
        ? "Volume (CC7) for this channel. Default 100. Incoming CC7 on this "
          "channel replaces this value."
        : "Pan (CC10) for this channel. 64 is centre, 0 is hard left, 127 is "
          "hard right. Incoming CC10 on this channel replaces this value.");
    knob.setTooltip(knob.getHelpText());

    attachment.reset();
    attachment = make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        owner.valueTreeState,
        (isTrim ? "trimCh" : isVolume ? "volCh" : "panCh") + String(row + 1),
        knob);
}

void ChannelListComponent::MixerCell::resized() {
    knob.setBounds(getLocalBounds().reduced(GuiConstants::groupGap / 2, 3));
}

//==============================================================================
ChannelListComponent::ChannelListComponent(
    AudioProcessorValueTreeState& state,
    FluidSynthModel& model
)
: valueTreeState{state}
, fluidSynthModel{model}
, font{juce::FontOptions{GuiConstants::bodyFontHeight}}
{
    rebuildPatchList();

    setName("MIDI channel rack");
    setTitle("MIDI channel rack");
    setDescription(
        "Sixteen MIDI channels, each with its own mute, solo, instrument, volume and pan");

    addAndMakeVisible(table);
    table.setModel(this);
    table.setName("MIDI channel assignments");
    table.setTitle("MIDI channel assignments");
    table.setDescription("Select a row to audition that MIDI channel on the keyboard");
    table.setHelpText(
        "Up and down arrows select a MIDI channel; Return opens that channel's instrument list.");

    table.setOutlineThickness(0);
    // GuiConstants::defaultHeight derives from these.
    table.setRowHeight(GuiConstants::channelRowHeight);
    table.setHeaderHeight(GuiConstants::channelHeaderHeight);

    const auto addColumn = [this](const String& name, int id, int width, bool fixed,
                                  Justification justification) {
        // Fixed columns are neither resizable nor draggable; only Instrument stretches.
        table.getHeader().addColumn(
            name, id, width,
            fixed ? width : GuiConstants::minInstrumentWidth,
            fixed ? width : -1,
            fixed ? TableHeaderComponent::visible
                  : (TableHeaderComponent::visible | TableHeaderComponent::resizable));
        // The rack states each header's alignment; the LookAndFeel draws it.
        if (name.isNotEmpty())
            table.getHeader().getProperties().set(
                "headerJustification" + name, justification.getFlags());
    };
    addColumn("Ch",         channelColumn,    GuiConstants::channelNumberWidth, true,
              Justification::centredRight);
    addColumn({},           muteSoloColumn,   GuiConstants::muteSoloWidth,      true,
              Justification::centredLeft);
    addColumn("Instrument", instrumentColumn, GuiConstants::minInstrumentWidth, false,
              Justification::centredLeft);
    addColumn("Volume",        volumeColumn,     GuiConstants::mixerCellWidth,     true,
              Justification::centred);
    addColumn("Pan",        panColumn,        GuiConstants::mixerCellWidth,     true,
              Justification::centred);

    addColumn("Trim dB", trimColumn, GuiConstants::mixerCellWidth, true, Justification::centred);
    addColumn("Signal", activityColumn, GuiConstants::activityWidth, true, Justification::centred);
    startTimerHz(20);
    // Arrow keys move the selection; Return opens the instrument list.
    table.setWantsKeyboardFocus(true);
    table.setMultipleSelectionEnabled(false);

    valueTreeState.state.addListener(this);
    // Start on the restored state's selected channel.
    syncTableSelectionFromState();
}

ChannelListComponent::~ChannelListComponent() {
    stopTimer();
    valueTreeState.state.removeListener(this);
}

void ChannelListComponent::rebuildPatchList() {
    patches = buildPatchList(valueTreeState.state.getChildWithName("banks"));
    ++patchListVersion;
}

int ChannelListComponent::patchIndexFor(int bank, int preset) const {
    for (size_t i = 0; i < patches.size(); ++i)
        if (patches[i].bank == bank && patches[i].preset == preset)
            return static_cast<int>(i);
    return -1;
}

void ChannelListComponent::applyComboSelection(int row, int selectedId) {
    if (row < 0 || row >= numChannels)
        return;
    if (selectedId < 1 || selectedId > static_cast<int>(patches.size()))
        return;
    const Patch& p{patches[static_cast<size_t>(selectedId) - 1]};
    fluidSynthModel.setChannelProgram(row, p.bank, p.preset);
}

int ChannelListComponent::getNumRows() {
    return numChannels;
}

int ChannelListComponent::getSelectedChannelIndex() const {
    return static_cast<int>(valueTreeState.state.getChildWithName("uiState")
        .getProperty("selectedChannel", 1)) - 1;
}

void ChannelListComponent::paintRowBackground(
    Graphics& g,
    int rowNumber,
    int width,
    int height,
    bool /*rowIsSelected*/
) {
    auto& theme{getLookAndFeel()};
    const bool selected{rowNumber == getSelectedChannelIndex()};
    if (selected)
        g.fillAll(theme.findColour(Juicy16::rowSelectedColourId));
    else if (rowNumber % 2)
        g.fillAll(theme.findColour(Juicy16::rowAlternateColourId));
    if (selected) {
        // A 2px accent marker keeps row text contrast.
        g.setColour(theme.findColour(Juicy16::accentColourId));
        g.fillRect(0, 0, 2, height);
    }
    if (isRowSilenced(rowNumber)) {
        // Muted or solo-silenced rows read as not sounding.
        g.setColour(theme.findColour(Juicy16::rowSilencedColourId));
        g.fillRect(0, 0, width, height);
    }
}

bool ChannelListComponent::isRowSilenced(int row) const {
    return fluidSynthModel.isChannelSilenced(row);
}

void ChannelListComponent::refreshSilencedRows() {
    const unsigned int mask{fluidSynthModel.getSilencedMask()};
    if (mask == lastSilencedMask)
        return;
    lastSilencedMask = mask;
    for (int row = 0; row < numChannels; ++row) {
        const float alpha{(mask & (1u << row)) != 0 ? 0.45f : 1.0f};
        // Mute and solo stay full strength: they bring the channel back.
        for (const int column : {instrumentColumn, volumeColumn, panColumn, trimColumn})
            if (auto* cell{table.getCellComponent(column, row)})
                cell->setAlpha(alpha);
    }
    table.repaint();
}

void ChannelListComponent::paintCell(
    Graphics& g,
    int rowNumber,
    int columnId,
    int width,
    int height,
    bool /*rowIsSelected*/
) {
    if (rowNumber < 0 || rowNumber >= numChannels) return;
    if (columnId == activityColumn) {
        const auto d = fluidSynthModel.getChannelDiagnostics(rowNumber);
        const auto accent = getLookAndFeel().findColour(Juicy16::accentColourId);
        g.setColour(midiLampTicks[static_cast<size_t>(rowNumber)] > 0 ? accent : accent.withAlpha(0.15f));
        g.fillEllipse(4.0f, static_cast<float>(height / 2 - 3), 6.0f, 6.0f);
        const float level = juce::jlimit(0.0f, 1.0f,
            (juce::Decibels::gainToDecibels(d.peak, -60.0f) + 60.0f) / 60.0f);
        g.setColour(accent.withAlpha(0.15f));
        g.fillRect(15, height / 2 - 3, width - 20, 6);
        g.setColour(d.peak > 1.0f ? getLookAndFeel().findColour(Juicy16::textErrorColourId) : accent);
        g.fillRect(15, height / 2 - 3, juce::roundToInt(static_cast<float>(width - 20) * level), 6);
        return;
    }
    if (columnId != channelColumn) return; // other columns draw themselves

    auto& theme{getLookAndFeel()};
    g.setColour(theme.findColour(rowNumber == getSelectedChannelIndex()
        ? Juicy16::textPrimaryColourId
        : Juicy16::textValueColourId)
        .withMultipliedAlpha(isRowSilenced(rowNumber) ? 0.45f : 1.0f));
    g.setFont(font);
    // 1-based channel number.
    g.drawText(String(rowNumber + 1),
               0, 0, width - GuiConstants::innerPadding, height,
               Justification::centredRight, true);
}

Component* ChannelListComponent::refreshComponentForCell(
    int rowNumber,
    int columnId,
    bool /*isRowSelected*/,
    Component* existingComponentToUpdate
) {
    if (columnId == channelColumn || columnId == activityColumn) {
        // Painted, not a control.
        jassert(existingComponentToUpdate == nullptr);
        return nullptr;
    }
    if (rowNumber < 0 || rowNumber >= numChannels) {
        delete existingComponentToUpdate;
        return nullptr;
    }
    switch (columnId) {
        case muteSoloColumn: {
            auto* cell{static_cast<MuteSoloCell*>(existingComponentToUpdate)};
            if (cell == nullptr)
                cell = new MuteSoloCell(*this);
            cell->setRow(rowNumber);
            return cell;
        }
        case instrumentColumn: {
            auto* cell{static_cast<PatchCell*>(existingComponentToUpdate)};
            if (cell == nullptr)
                cell = new PatchCell(*this);
            cell->setRow(rowNumber);
            cell->setAlpha(isRowSilenced(rowNumber) ? 0.45f : 1.0f);
            return cell;
        }
        case volumeColumn:
        case panColumn:
        case trimColumn: {
            auto* cell{static_cast<MixerCell*>(existingComponentToUpdate)};
            if (cell == nullptr)
                cell = new MixerCell(*this, columnId);
            cell->setRow(rowNumber);
            cell->setAlpha(isRowSilenced(rowNumber) ? 0.45f : 1.0f);
            return cell;
        }
        default:
            break;
    }
    delete existingComponentToUpdate;
    return nullptr;
}

void ChannelListComponent::cellClicked(int rowNumber, int /*columnId*/, const juce::MouseEvent&) {
    if (rowNumber < 0 || rowNumber >= numChannels)
        return;
    fluidSynthModel.selectChannelForEditing(rowNumber);
}

void ChannelListComponent::selectedRowsChanged(int lastRowSelected) {
    if (syncingSelection || lastRowSelected < 0 || lastRowSelected >= numChannels)
        return;
    fluidSynthModel.selectChannelForEditing(lastRowSelected);
}

juce::ComboBox* ChannelListComponent::patchComboForRow(int row) {
    if (row < 0 || row >= numChannels)
        return nullptr;
    // Off-screen rows have no cell component yet.
    table.scrollToEnsureRowIsOnscreen(row);
    auto* cell{dynamic_cast<PatchCell*>(table.getCellComponent(instrumentColumn, row))};
    return cell == nullptr ? nullptr : &cell->getCombo();
}

void ChannelListComponent::returnKeyPressed(int lastRowSelected) {
    if (auto* combo{patchComboForRow(lastRowSelected)})
        combo->showPopup();
}

void ChannelListComponent::syncTableSelectionFromState() {
    const int selected{getSelectedChannelIndex()};
    if (selected < 0 || selected >= numChannels
        || table.getSelectedRow() == selected)
        return;
    const juce::ScopedValueSetter<bool> guard{syncingSelection, true};
    // Scroll so arrow keys never select an off-screen row.
    table.selectRow(selected);
}

void ChannelListComponent::valueTreePropertyChanged(
    ValueTree& treeWhosePropertyHasChanged,
    const Identifier& property) {
    const Identifier type{treeWhosePropertyHasChanged.getType()};
    if (type == StringRef("banks")) {
        // Font reloaded: rebuild the patch list and every dropdown.
        rebuildPatchList();
        table.updateContent();
    } else if (type == StringRef("ch")) {
        // Only program changes rebuild dropdowns; volume, pan, mute and solo update
        // through attachments, and rips stream CC7/CC10 continuously.
        if (property == StringRef("bank") || property == StringRef("preset"))
            table.updateContent();
        // Solo changes other rows' appearance too.
        else if (property == StringRef("mute") || property == StringRef("solo"))
            refreshSilencedRows();
    } else if (type == StringRef("uiState")
               && property == StringRef("selectedChannel")) {
        // Selection is paint-only.
        table.repaint();
        syncTableSelectionFromState();
    }
}

int ChannelListComponent::instrumentColumnWidth() const {
    // Whatever the fixed columns leave.
    return juce::jmax(
        GuiConstants::minInstrumentWidth,
        getWidth()
            - GuiConstants::channelNumberWidth
            - GuiConstants::muteSoloWidth
            - 3 * GuiConstants::mixerCellWidth - GuiConstants::activityWidth);
}

void ChannelListComponent::resized() {
    table.setBoundsInset(BorderSize<int>(0));
    table.getHeader().setColumnWidth(instrumentColumn, instrumentColumnWidth());
    // Header changes notify asynchronously; lay out cells now to avoid a stale frame.
    table.updateContent();
}

void ChannelListComponent::timerCallback() {
    for (int ch = 0; ch < 16; ++ch) {
        const auto d = fluidSynthModel.getChannelDiagnostics(ch);
        const auto i = static_cast<size_t>(ch);
        if (d.midiEvents != lastMidiEvents[i]) midiLampTicks[i] = 4;
        else midiLampTicks[i] = juce::jmax(0, midiLampTicks[i] - 1);
        lastMidiEvents[i] = d.midiEvents;
        if (auto* cell = dynamic_cast<PatchCell*>(table.getCellComponent(instrumentColumn, ch))) {
            const auto saved = valueTreeState.state.getChildWithName("channelPrograms").getChildWithProperty("num", ch);
            const int bank = saved.getProperty("bank", 0), program = saved.getProperty("preset", 0);
            String description = "Requested " + String(bank) + ":" + String(program);
            if (d.soundingBank < 0) description += " - no playable instrument";
            else if (d.soundingBank != bank || d.soundingPreset != program) {
                const int idx = patchIndexFor(d.soundingBank, d.soundingPreset);
                description += " - FALLBACK: " + (idx >= 0 ? patchLabel(patches[static_cast<size_t>(idx)])
                    : String(d.soundingBank) + ":" + String(d.soundingPreset));
            }
            cell->getCombo().setTooltip(description);
        }
        // Only the activity column animates.
        table.repaint(table.getCellPosition(activityColumn, ch, true));
    }
}
