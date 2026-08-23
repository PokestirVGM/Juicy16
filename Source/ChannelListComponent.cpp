//
// The 16-channel rack: one row per MIDI channel, each owning that channel's
// mute, solo, instrument, volume and pan.
//

#include "ChannelListComponent.h"
#include "Theme.h"

using namespace std;

namespace {
// Names read by screen readers and shown as tooltips. Built per row rather than
// stored, because a cell component is recycled across rows as the table scrolls.
String channelPrefix(int row) {
    return String{"MIDI channel "} + String(row + 1);
}
} // namespace

//==============================================================================
// PatchCell: a ComboBox bound to one MIDI channel (table row).
//==============================================================================
ChannelListComponent::PatchCell::PatchCell(ChannelListComponent& ownerRef)
: owner{ownerRef}
{
    addAndMakeVisible(combo);
    // user picked a patch for this channel
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

    // (re)populate items only when the loaded font's patch list changed
    if (cellListVersion != owner.patchListVersion) {
        combo.clear(juce::dontSendNotification);
        for (size_t i = 0; i < owner.patches.size(); ++i)
            combo.addItem(patchLabel(owner.patches[i]), static_cast<int>(i) + 1);
        cellListVersion = owner.patchListVersion;
    }

    // reflect this channel's current program (driven either by a manual pick or
    // by an incoming MIDI program change). dontSendNotification so we don't loop.
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
        // saved patch isn't present in the loaded font: show a bank/preset placeholder
        combo.setText(String(static_cast<int>(chNode.getProperty("bank", 0))) + "/"
                      + String(static_cast<int>(chNode.getProperty("preset", 0))),
                      juce::dontSendNotification);
    else
        combo.setText({}, juce::dontSendNotification);
}

void ChannelListComponent::PatchCell::resized() {
    // Half the group gap on each side. Every row cell insets itself the same
    // way, so two adjacent cells produce a full groupGap between their contents
    // without any cell needing to know what sits beside it. Without it the
    // instrument dropdown butted straight against the solo button - 0px on one
    // side of the mute/solo pair and 8px on the other.
    combo.setBounds(getLocalBounds().reduced(GuiConstants::groupGap / 2, 2));
}

//==============================================================================
// MuteSoloCell: this channel's mute and solo, bound to muteChN and soloChN.
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
    // A lit mute and a lit solo must never be told apart by position alone, so
    // they get different hues: solo takes the accent, mute keeps its own warm
    // red in every accent. Both carry a dark label, which is legible on either
    // fill - a near-white fill with a dark letter read as a blank white box.
    auto& lookAndFeel{getLookAndFeel()};
    solo.setColour(juce::TextButton::buttonOnColourId,
                   lookAndFeel.findColour(Juicy16::accentColourId));
    mute.setColour(juce::TextButton::buttonOnColourId,
                   lookAndFeel.findColour(Juicy16::muteActiveColourId));
}

void ChannelListComponent::MuteSoloCell::setRow(int newRow) {
    if (row == newRow)
        return; // recycled onto the same channel: the attachments already fit
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

    // Rebuild rather than retarget: an attachment binds one parameter for life.
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
// MixerCell: one channel's volume or pan knob, bound to volChN / panChN.
//==============================================================================
ChannelListComponent::MixerCell::MixerCell(ChannelListComponent& ownerRef, int column)
: owner{ownerRef}
, columnId{column}
{
    knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    // The value box is the readout in the approved layout; editable, so a value
    // can be typed rather than only dragged.
    knob.setTextBoxStyle(juce::Slider::TextBoxRight, false,
                         GuiConstants::rowValueWidth, GuiConstants::rowKnobSize);
    knob.setRange(MidiConstants::midiMinValue, MidiConstants::midiMaxValue, 1);
    // Pan is bipolar: the accent arc grows outward from centre, so "centred"
    // reads as no fill. See Juicy16::LookAndFeel::drawRotarySlider.
    knob.getProperties().set("bipolar", columnId == panColumn);
    // JUCE sliders decline keyboard focus by default, which would leave the
    // rack mouse-only. A focused slider handles arrow keys.
    knob.setWantsKeyboardFocus(true);
    addAndMakeVisible(knob);
}

void ChannelListComponent::MixerCell::setRow(int newRow) {
    if (row == newRow)
        return;
    row = newRow;

    const bool isVolume{columnId == volumeColumn};
    const String prefix{channelPrefix(row)};
    const String name{prefix + (isVolume ? " volume" : " pan")};
    knob.setName(name);
    knob.setTitle(name);
    knob.setDescription(name);
    knob.setHelpText(isVolume
        ? "Volume (CC7) for this channel. Default 100. Incoming CC7 on this "
          "channel replaces this value."
        : "Pan (CC10) for this channel. 64 is centre, 0 is hard left, 127 is "
          "hard right. Incoming CC10 on this channel replaces this value.");
    knob.setTooltip(knob.getHelpText());

    attachment.reset();
    attachment = make_unique<AudioProcessorValueTreeState::SliderAttachment>(
        owner.valueTreeState,
        (isVolume ? "volCh" : "panCh") + String(row + 1),
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
    // GuiConstants::defaultHeight is derived from these two, so a change here
    // moves the default window height with it.
    table.setRowHeight(GuiConstants::channelRowHeight);
    table.setHeaderHeight(GuiConstants::channelHeaderHeight);

    const auto addColumn = [this](const String& name, int id, int width, bool fixed,
                                  Justification justification) {
        table.getHeader().addColumn(
            name, id, width,
            fixed ? width : GuiConstants::minInstrumentWidth,
            fixed ? width : -1,
            TableHeaderComponent::notSortable);
        // Each header aligns over its own column's content; the LookAndFeel draws
        // it, so the rack states the alignment rather than the theme guessing.
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
    addColumn("Vol",        volumeColumn,     GuiConstants::mixerCellWidth,     true,
              Justification::centred);
    addColumn("Pan",        panColumn,        GuiConstants::mixerCellWidth,     true,
              Justification::centred);

    // Keyboard-reachable: arrow keys move the selection, Return opens the
    // selected row's instrument list. Nothing drives row selection from MIDI, so
    // there is no selection for the keyboard to fight.
    table.setWantsKeyboardFocus(true);
    table.setMultipleSelectionEnabled(false);

    valueTreeState.state.addListener(this);
    // Open on whichever channel the restored state was editing, so the first
    // arrow key moves from there rather than from row 0.
    syncTableSelectionFromState();
}

ChannelListComponent::~ChannelListComponent() {
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
    auto& lookAndFeel{getLookAndFeel()};
    const bool selected{rowNumber == getSelectedChannelIndex()};
    if (selected)
        g.fillAll(lookAndFeel.findColour(Juicy16::rowSelectedColourId));
    else if (rowNumber % 2)
        g.fillAll(lookAndFeel.findColour(Juicy16::rowAlternateColourId));
    if (selected) {
        // A 2px accent marker rather than a wash of colour, so the row text keeps
        // its contrast and the selection reads at a glance.
        g.setColour(lookAndFeel.findColour(Juicy16::accentColourId));
        g.fillRect(0, 0, 2, height);
    }
    if (isRowSilenced(rowNumber)) {
        // Whether this channel muted itself or another channel soloed, the row
        // reads as not sounding. Soloing one channel visibly quiets fifteen.
        g.setColour(lookAndFeel.findColour(Juicy16::rowSilencedColourId));
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
        // Mute and solo stay at full strength: they are how the user gets the
        // channel back, so they must not recede with the rest of the row.
        for (const int column : {instrumentColumn, volumeColumn, panColumn})
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
    if (rowNumber < 0 || rowNumber >= numChannels || columnId != channelColumn)
        return; // every other column is drawn by its own control

    auto& lookAndFeel{getLookAndFeel()};
    g.setColour(lookAndFeel.findColour(rowNumber == getSelectedChannelIndex()
        ? Juicy16::textPrimaryColourId
        : Juicy16::textValueColourId)
        .withMultipliedAlpha(isRowSilenced(rowNumber) ? 0.45f : 1.0f));
    g.setFont(font);
    // channel number, displayed 1-indexed
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
    if (columnId == channelColumn) {
        // painted, not a control
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
        case panColumn: {
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
    // A row that is scrolled out of view has no cell component yet.
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
    // Scroll it into view: at the minimum window height only part of the list is
    // visible, so arrow-keying off-screen would otherwise lose the selection.
    table.selectRow(selected);
}

void ChannelListComponent::valueTreePropertyChanged(
    ValueTree& treeWhosePropertyHasChanged,
    const Identifier& property) {
    const Identifier type{treeWhosePropertyHasChanged.getType()};
    if (type == StringRef("banks")) {
        // a font (re)loaded: rebuild the shared patch list, then refresh every
        // row's dropdown items + selection.
        rebuildPatchList();
        table.updateContent();
    } else if (type == StringRef("ch")) {
        // Only a program change needs the dropdowns rebuilt. Volume, pan, mute
        // and solo reach their controls through parameter attachments, and a
        // game rip streams CC7/CC10 continuously - rebuilding the table on every
        // one of those would rebuild every visible cell for nothing.
        if (property == StringRef("bank") || property == StringRef("preset"))
            table.updateContent();
        // Solo changes what fifteen OTHER rows look like, so this cannot be a
        // per-row repaint driven by the row that changed.
        else if (property == StringRef("mute") || property == StringRef("solo"))
            refreshSilencedRows();
    } else if (type == StringRef("uiState")
               && property == StringRef("selectedChannel")) {
        // the selection marker is a paint concern, not a content one
        table.repaint();
        syncTableSelectionFromState();
    }
}

int ChannelListComponent::instrumentColumnWidth() const {
    // Everything the fixed columns leave behind. Nothing is clamped away, so no
    // visible region belongs to no control.
    return juce::jmax(
        GuiConstants::minInstrumentWidth,
        getWidth()
            - GuiConstants::channelNumberWidth
            - GuiConstants::muteSoloWidth
            - 2 * GuiConstants::mixerCellWidth);
}

void ChannelListComponent::resized() {
    table.setBoundsInset(BorderSize<int>(0));
    table.getHeader().setColumnWidth(instrumentColumn, instrumentColumnWidth());
}
