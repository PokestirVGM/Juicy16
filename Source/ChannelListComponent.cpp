//
// 16-channel instrument list (Fruity LSD-style), with a per-row patch dropdown.
//

#include "ChannelListComponent.h"
#include "MyColours.h"

using namespace std;

//==============================================================================
// PatchCell: a ComboBox bound to one MIDI channel (table row).
//==============================================================================
ChannelListComponent::PatchCell::PatchCell(ChannelListComponent& ownerRef)
: owner{ownerRef}
{
    combo.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible(combo);
    // user picked a patch for this channel
    combo.onChange = [this] {
        owner.applyComboSelection(row, combo.getSelectedId());
    };
}

void ChannelListComponent::PatchCell::setRow(int newRow) {
    row = newRow;
    const String accessibleName{
        String{"MIDI channel "} + String(row + 1) + " instrument"};
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
    combo.setBounds(getLocalBounds().reduced(1));
}

//==============================================================================
ChannelListComponent::ChannelListComponent(
    AudioProcessorValueTreeState& state,
    FluidSynthModel& model
)
: valueTreeState{state}
, fluidSynthModel{model}
, font{juce::FontOptions{14.0f}}
{
    rebuildPatchList();

    setName("MIDI channel instrument list");
    setTitle("MIDI channel instrument list");
    setDescription("Sixteen MIDI channels with independent bank and preset assignments");

    addAndMakeVisible(table);
    table.setModel(this);
    table.setName("MIDI channel assignments");
    table.setTitle("MIDI channel assignments");
    table.setDescription("Select a row to edit that MIDI channel's sound controls");
    table.setHelpText(
        "Up and down arrows select a MIDI channel; Return opens that channel's instrument list.");

    table.setColour(ListBox::outlineColourId, juce::Colours::grey);
    table.setOutlineThickness(1);
    // GuiConstants::defaultHeight assumes this row height + the header's default
    // height (28px, JUCE's TableListBox built-in default, not set here) — keep in
    // sync if this changes.
    table.setRowHeight(GuiConstants::channelRowHeight);

    int columnIx = 1;
    table.getHeader().addColumn(
        String("Ch"),
        columnIx++,
        32,  // width
        32,  // min
        32,  // max
        TableHeaderComponent::notSortable);
    table.getHeader().addColumn(
        String("Instrument"),
        columnIx++,
        200, // width
        80,  // min
        600, // max
        TableHeaderComponent::notSortable);

    // Keyboard-reachable. This was previously false, justified as stopping arrow
    // keys from fighting row selection the plugin drives from MIDI - but nothing
    // does: selectChannelForEditing has one caller, a mouse click, and incoming
    // MIDI changes a channel's program rather than which row is selected. So the
    // table can take focus, and channel selection works without a mouse.
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

String ChannelListComponent::getInstrumentName(int bankNum, int presetNum) const {
    ValueTree banks{valueTreeState.state.getChildWithName("banks")};
    ValueTree bank{banks.getChildWithProperty("num", bankNum)};
    if (bank.isValid()) {
        ValueTree preset{bank.getChildWithProperty("num", presetNum)};
        if (preset.isValid())
            return preset.getProperty("name").toString();
    }
    // fallback when the saved program isn't present in the loaded font
    return String(bankNum) + "/" + String(presetNum);
}

void ChannelListComponent::paintRowBackground(
    Graphics& g,
    int rowNumber,
    int /*width*/,
    int /*height*/,
    bool /*rowIsSelected*/
) {
    const Colour alternateColour(getLookAndFeel().findColour(ListBox::backgroundColourId)
        .interpolatedWith(getLookAndFeel().findColour(ListBox::textColourId), 0.03f));
    if (rowNumber == getSelectedChannelIndex())
        // The scheme's selection fill is designed to pair with its highlighted
        // text; a fixed pale blue left near-white row text at 1.5:1.
        g.fillAll(MyColours::getUIColourIfAvailable(
            LookAndFeel_V4::ColourScheme::UIColour::highlightedFill,
            juce::Colours::steelblue));
    else if (rowNumber % 2)
        g.fillAll(alternateColour);
}

void ChannelListComponent::paintCell(
    Graphics& g,
    int rowNumber,
    int columnId,
    int width,
    int height,
    bool /*rowIsSelected*/
) {
    if (rowNumber < 0 || rowNumber >= numChannels)
        return;

    // column 2 (Instrument) is drawn by its PatchCell ComboBox
    if (columnId == 1) {
        g.setColour(rowNumber == getSelectedChannelIndex()
            ? MyColours::getUIColourIfAvailable(
                LookAndFeel_V4::ColourScheme::UIColour::highlightedText,
                juce::Colours::white)
            : getLookAndFeel().findColour(ListBox::textColourId));
        g.setFont(font);
        // channel number, displayed 1-indexed
        g.drawText(String(rowNumber + 1), 2, 0, width - 4, height,
                   Justification::centredRight, true);
        g.setColour(getLookAndFeel().findColour(ListBox::backgroundColourId));
        g.fillRect(width - 1, 0, 1, height);
    }
}

Component* ChannelListComponent::refreshComponentForCell(
    int rowNumber,
    int columnId,
    bool /*isRowSelected*/,
    Component* existingComponentToUpdate
) {
    if (columnId != 2) {
        // only the Instrument column owns a custom component
        jassert(existingComponentToUpdate == nullptr);
        return nullptr;
    }
    if (rowNumber < 0 || rowNumber >= numChannels) {
        delete existingComponentToUpdate;
        return nullptr;
    }
    auto* cell{static_cast<PatchCell*>(existingComponentToUpdate)};
    if (cell == nullptr)
        cell = new PatchCell(*this);
    cell->setRow(rowNumber);
    return cell;
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
    auto* cell{dynamic_cast<PatchCell*>(table.getCellComponent(2, row))};
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
    const Identifier& /*property*/) {
    const Identifier type{treeWhosePropertyHasChanged.getType()};
    if (type == StringRef("banks")) {
        // a font (re)loaded: rebuild the shared patch list, then refresh every
        // row's dropdown items + selection.
        rebuildPatchList();
        table.updateContent();
    } else if (type == StringRef("ch") || type == StringRef("uiState")) {
        // a channel's program changed (manual or via MIDI), or the selection
        // moved: refresh dropdown selections + the selected-row highlight.
        table.updateContent();
        if (type == StringRef("uiState"))
            syncTableSelectionFromState();
    }
}

void ChannelListComponent::resized() {
    table.setBoundsInset(BorderSize<int>(0));
    // give the instrument column whatever width is left after the fixed Ch column
    table.getHeader().setColumnWidth(2, jmax(120, getWidth() - 32 - 4));
}
