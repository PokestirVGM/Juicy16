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
    ~ChannelListComponent();

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

    void resized() override;

    virtual void valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged,
                                          const Identifier& property) override;
    inline virtual void valueTreeChildAdded(ValueTree& parentTree,
                                            ValueTree& childWhichHasBeenAdded) override {};
    inline virtual void valueTreeChildRemoved(ValueTree& parentTree,
                                              ValueTree& childWhichHasBeenRemoved,
                                              int indexFromWhichChildWasRemoved) override {};
    inline virtual void valueTreeChildOrderChanged(ValueTree& parentTreeWhoseChildrenHaveMoved,
                                                   int oldIndex, int newIndex) override {};
    inline virtual void valueTreeParentChanged(ValueTree& treeWhoseParentHasChanged) override {};
    inline virtual void valueTreeRedirected(ValueTree& treeWhichHasBeenChanged) override {};

private:
    // one cell's patch dropdown, bound to a MIDI channel (row)
    class PatchCell : public Component {
    public:
        explicit PatchCell(ChannelListComponent& owner);
        void setRow(int newRow);
        void resized() override;
    private:
        ChannelListComponent& owner;
        juce::ComboBox combo;
        int row{-1};
        int cellListVersion{-1};
    };

    static constexpr int numChannels{16};

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
