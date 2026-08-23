//
// The 16-channel rack. Each row is a MIDI channel and owns everything that
// belongs to that channel: mute and solo, its instrument dropdown, and its
// volume and pan knobs. Nothing here requires selecting a row first - that was
// the defect Phase 9 exists to fix.
//
// Every control is bound to a real plugin parameter (muteChN, soloChN, progChN
// via the dropdown, volChN, panChN), so host automation, incoming MIDI, and the
// user's mouse all move the same thing, and a host's right-click automation menu
// works on the knobs.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include "FluidSynthModel.h"
#include "PatchList.h"
#include "GuiConstants.h"
#include <memory>
#include <vector>

using namespace std;

class ChannelListComponent : public Component,
                             public TableListBoxModel,
                             public ValueTree::Listener {
public:
    // Column ids, in the row's left-to-right order.
    enum ColumnId {
        channelColumn = 1,
        muteSoloColumn,
        instrumentColumn,
        volumeColumn,
        panColumn,
    };

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

    // the instrument, mute/solo, volume and pan columns each host live controls
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
    // TableListBox creates a cell component before handing it to the table, so a
    // control built in a cell's constructor resolves the DEFAULT LookAndFeel and
    // caches its colours from it - the theme is only reachable once the cell is
    // parented. Re-sending the change on reparent is what makes a cell inherit
    // the palette, and it is inherited by every cell type below rather than
    // remembered per control.
    class ThemedCell : public Component {
    public:
        void parentHierarchyChanged() override { sendLookAndFeelChange(); }
    };

    // one cell's patch dropdown, bound to a MIDI channel (row)
    class PatchCell : public ThemedCell {
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

    // Mute and solo for one channel, each attached to its own bool parameter.
    class MuteSoloCell : public ThemedCell {
    public:
        explicit MuteSoloCell(ChannelListComponent& owner);
        void setRow(int newRow);
        void resized() override;
        // Colours must be resolved HERE, not in the constructor: a cell is built
        // before it is parented, so a constructor findColour asks the default
        // LookAndFeel, which has never heard of Juicy16's ColourIds - it asserts
        // and hands back black. That is what made a lit mute a blank box.
        void lookAndFeelChanged() override;
    private:
        ChannelListComponent& owner;
        juce::TextButton mute{"M"};
        juce::TextButton solo{"S"};
        unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
        unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;
        int row{-1};
    };

    // A volume or pan knob for one channel, attached to volChN / panChN.
    class MixerCell : public ThemedCell {
    public:
        MixerCell(ChannelListComponent& owner, int columnId);
        void setRow(int newRow);
        void resized() override;
    private:
        ChannelListComponent& owner;
        int columnId;
        juce::Slider knob;
        unique_ptr<AudioProcessorValueTreeState::SliderAttachment> attachment;
        int row{-1};
    };

    static constexpr int numChannels{16};

    // uiState.selectedChannel remains the single source of truth for which
    // channel is being edited; the table's own selection mirrors it. Pushing a
    // change in either direction notifies the other, so this breaks the loop.
    bool syncingSelection{false};
    void syncTableSelectionFromState();

    int getSelectedChannelIndex() const; // 0-indexed
    // Width the instrument column should take: everything the fixed columns
    // leave behind. No visible region belongs to no control.
    int instrumentColumnWidth() const;

    // A channel is silenced by its own mute, or by another channel's solo. The
    // row shows it either way: the controls recede and a scrim goes over the
    // background, so "this is not sounding" is visible without reading buttons.
    bool isRowSilenced(int row) const;
    void refreshSilencedRows();
    unsigned int lastSilencedMask{0};

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
