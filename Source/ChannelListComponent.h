// The 16-channel rack. Each row owns its channel's mute, solo, instrument,
// volume and pan. Every control is bound to a real parameter, so automation,
// MIDI and the mouse move the same value.

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
                             public ValueTree::Listener,
                             private juce::Timer {
public:
    // Column ids, in the row's left-to-right order.
    enum ColumnId {
        channelColumn = 1,
        muteSoloColumn,
        instrumentColumn,
        volumeColumn,
        panColumn,
        trimColumn,
        activityColumn,
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

    Component* refreshComponentForCell(
        int rowNumber,
        int columnId,
        bool isRowSelected,
        Component* existingComponentToUpdate
    ) override;

    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;

    // Arrow-key channel selection.
    void selectedRowsChanged(int lastRowSelected) override;

    // Return opens the selected row's instrument dropdown.
    void returnKeyPressed(int lastRowSelected) override;

    // The row's dropdown, scrolled into view; nullptr if out of range. Testable
    // without opening a popup.
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
    void timerCallback() override;
    std::array<unsigned int, 16> lastMidiEvents{};
    std::array<int, 16> midiLampTicks{};
    // A cell's controls are built before it is parented and cache the default
    // LookAndFeel's colours; resending on reparent applies the theme.
    class ThemedCell : public Component {
    public:
        void parentHierarchyChanged() override { sendLookAndFeelChange(); }
    };

    // Patch dropdown for one channel.
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

    class MuteSoloCell : public ThemedCell {
    public:
        explicit MuteSoloCell(ChannelListComponent& owner);
        void setRow(int newRow);
        void resized() override;
        // Resolve colours here, not in the constructor: an unparented cell sees the
        // default LookAndFeel, which lacks Juicy16's ColourIds.
        void lookAndFeelChanged() override;
    private:
        ChannelListComponent& owner;
        juce::TextButton mute{"M"};
        juce::TextButton solo{"S"};
        unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> muteAttachment;
        unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> soloAttachment;
        int row{-1};
    };

    // Volume or pan knob (volChN / panChN).
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

    // uiState.selectedChannel is the source of truth; the table mirrors it. Guards
    // the two-way sync loop.
    bool syncingSelection{false};
    void syncTableSelectionFromState();

    int getSelectedChannelIndex() const; // 0-based
    // Instrument column takes whatever the fixed columns leave.
    int instrumentColumnWidth() const;

    // Muted, or not soloed while another channel is. Silenced rows are dimmed.
    bool isRowSilenced(int row) const;
    void refreshSilencedRows();
    unsigned int lastSilencedMask{0};

    void rebuildPatchList();
    int patchIndexFor(int bank, int preset) const; // -1 if absent
    void applyComboSelection(int row, int selectedId);

    AudioProcessorValueTreeState& valueTreeState;
    FluidSynthModel& fluidSynthModel;

    // Shared by every dropdown; the version tells cells to repopulate.
    std::vector<Patch> patches;
    int patchListVersion{0};

    TableListBox table;
    Font font;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelListComponent)
};
