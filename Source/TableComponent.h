//
//  Model.hpp
//  Lazarus
//
//  Created by Alex Birch on 01/09/2017.
//
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <memory>
#include <string>
#include <vector>

using namespace std;

class TableRow {
public:
    TableRow(
             int bank,
             int preset,
             String name
             );
private:
    /** 1-indexed */
    String getStringContents(int columnId);

    int bank;
    int preset;
    String name;

    friend class TableComponent;
};


class TableComponent    : public Component,
                          public TableListBoxModel,
                          public ValueTree::Listener,
                          public AudioProcessorValueTreeState::Listener {
public:
    TableComponent(
            AudioProcessorValueTreeState& valueTreeState
    );
    ~TableComponent();

    int getNumRows() override;

    void paintRowBackground (
            Graphics& g,
            int rowNumber,
            int width,
            int height,
            bool rowIsSelected
    ) override;
    void paintCell (
            Graphics& g,
            int rowNumber,
            int columnId,
            int width,
            int height,
            bool rowIsSelected
    ) override;

    int getColumnAutoSizeWidth (int columnId) override;

    void selectedRowsChanged (int row) override;

    void resized() override;

    bool keyPressed(const KeyPress &key) override;

    virtual void parameterChanged (const String& parameterID, float newValue) override;

    virtual void valueTreePropertyChanged (ValueTree& treeWhosePropertyHasChanged,
                                           const Identifier& property) override;
    inline virtual void valueTreeChildAdded (ValueTree& parentTree,
                                             ValueTree& childWhichHasBeenAdded) override {};
    inline virtual void valueTreeChildRemoved (ValueTree& parentTree,
                                               ValueTree& childWhichHasBeenRemoved,
                                               int indexFromWhichChildWasRemoved) override {};
    inline virtual void valueTreeChildOrderChanged (ValueTree& parentTreeWhoseChildrenHaveMoved,
                                                    int oldIndex, int newIndex) override {};
    inline virtual void valueTreeParentChanged (ValueTree& treeWhoseParentHasChanged) override {};
    inline virtual void valueTreeRedirected (ValueTree& treeWhichHasBeenChanged) override {};
private:
    void loadModelFrom(ValueTree& banks);
    void selectCurrentPreset();

    AudioProcessorValueTreeState& valueTreeState;

    TableListBox table;     // the table component itself
    Font font;

    // every preset from every bank in the loaded soundfont, sorted by bank,preset
    vector<TableRow> rows;

    bool selecting{false}; // guards against re-entrant selection callbacks

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TableComponent)
};
