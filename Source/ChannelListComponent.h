//
// 16-channel instrument list (Fruity LSD-style). Each row is a MIDI channel;
// each row carries its own "Patch Selection" dropdown that assigns an instrument
// to that channel. Incoming MIDI program changes update the dropdown in place.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "FluidSynthModel.h"
#include "PatchList.h"
#include "GuiConstants.h"
#include <vector>

using namespace std;

class ChannelListComponent : public Component,
                             public TableListBoxModel,
                             public ValueTree::Listener {
public:
    ChannelListComponent(
        AudioProcessorValueTreeState& valueTreeState,
        FluidSynthModel& fluidSynthModel
    );
    ~ChannelListComponent() override;

    int getNumRows() override;

    void paintRowBackground(
        Graphics& g,
        int rowNumber,
        int width,
        int height,
        bool rowIsSelected
    ) override;
    void paintCell(
        Graphics& g,
        int rowNumber,
        int columnId,
        int width,
        int height,
        bool rowIsSelected
    ) override;

    // the Instrument column hosts a live ComboBox per row
    Component* refreshComponentForCell(
        int rowNumber,
        int columnId,
        bool isRowSelected,
        Component* existingComponentToUpdate
    ) override;

    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;

    // Keyboard channel selection. TableListBox reports arrow-key movement here,
    // so this is what makes the channel list reachable without a mouse.
    void selectedRowsChanged(int lastRowSelected) override;

    // Keyboard patch selection: Return on the focused table opens the selected
    // row's instrument dropdown, which is then a normal keyboard-driven menu.
    void returnKeyPressed(int lastRowSelected) override;

    // The row's instrument dropdown, scrolled into view and created if the row
    // was offscreen; nullptr for an out-of-range row. Split out of
    // returnKeyPressed so the routing can be tested without opening a popup.
    juce::ComboBox* patchComboForRow(int row);

    void resized() override;

    void valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged,
                                  const Identifier& property) override;
    void valueTreeChildAdded(ValueTree&, ValueTree&) override {}
    void valueTreeChildRemoved(ValueTree&, ValueTree&, int) override {}
    void valueTreeChildOrderChanged(ValueTree&, int, int) override {}
    void valueTreeParentChanged(ValueTree&) override {}
    void valueTreeRedirected(ValueTree&) override {}

private:
    // one cell's patch dropdown, bound to a MIDI channel (row)
    class PatchCell : public Component {
    public:
        explicit PatchCell(ChannelListComponent& owner);
        void setRow(int newRow);
        void resized() override;
        juce::ComboBox& getCombo() { return combo; }
    private:
        ChannelListComponent& owner;
        juce::ComboBox combo;
        int row{-1};
        int cellListVersion{-1};
    };

    static constexpr int numChannels{16};

    // uiState.selectedChannel remains the single source of truth for which
    // channel is being edited; the table's own selection mirrors it. Pushing a
    // change in either direction notifies the other, so this breaks the loop.
    bool syncingSelection{false};
    void syncTableSelectionFromState();

    int getSelectedChannelIndex() const; // 0-indexed
    String getInstrumentName(int bankNum, int presetNum) const;

    void rebuildPatchList();
    int patchIndexFor(int bank, int preset) const; // -1 if absent from font
    void applyComboSelection(int row, int selectedId);

    AudioProcessorValueTreeState& valueTreeState;
    FluidSynthModel& fluidSynthModel;

    // flat, sorted patch list shared by every row's dropdown; version bumps on
    // each rebuild so cells know to repopulate their items.
    std::vector<Patch> patches;
    int patchListVersion{0};

    TableListBox table;
    Font font;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelListComponent)
};
