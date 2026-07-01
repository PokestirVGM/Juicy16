//
//  Model.cpp
//  Lazarus
//
//  Created by Alex Birch on 01/09/2017.
//
//

#include "TableComponent.h"
#include "Util.h"
#include <algorithm>

using namespace std;
using namespace Util;

//==============================================================================
/**
    Shows every preset in the loaded soundfont/DLS as a flat, selectable list.
    Clicking a row sets the bank+preset for the currently-selected MIDI channel.
    The highlight follows the selected channel's current program (which may be
    changed either here or by incoming MIDI program-change messages).
*/
TableComponent::TableComponent(
    AudioProcessorValueTreeState& valueTreeState
)
: valueTreeState{valueTreeState}
, font{14.0f}
{
    addAndMakeVisible (table);
    table.setModel (this);

    table.setColour (ListBox::outlineColourId, juce::Colours::grey);
    table.setOutlineThickness (1);

    int columnIx = 1;
    table.getHeader().addColumn (
            String("Bank"),
            columnIx++,
            40, 40, 60,
            TableHeaderComponent::notSortable);
    table.getHeader().addColumn (
            String("#"),
            columnIx++,
            34, 34, 60,
            TableHeaderComponent::notSortable);
    table.getHeader().addColumn (
            String("Name"),
            columnIx++,
            200, 80, 600,
            TableHeaderComponent::notSortable);

    table.setWantsKeyboardFocus(false);

    ValueTree banks{valueTreeState.state.getChildWithName("banks")};
    loadModelFrom(banks);

    valueTreeState.state.addListener(this);
    valueTreeState.addParameterListener("bank", this);
    valueTreeState.addParameterListener("preset", this);
}

TableComponent::~TableComponent() {
    valueTreeState.removeParameterListener("bank", this);
    valueTreeState.removeParameterListener("preset", this);
    valueTreeState.state.removeListener(this);
}

void TableComponent::loadModelFrom(ValueTree& banks) {
    rows.clear();
    int banksChildren{banks.getNumChildren()};
    for(int bankIx{0}; bankIx<banksChildren; bankIx++) {
        ValueTree bank{banks.getChild(bankIx)};
        int bankNum{bank.getProperty("num")};
        int bankChildren{bank.getNumChildren()};
        for(int presetIx{0}; presetIx<bankChildren; presetIx++) {
            ValueTree preset{bank.getChild(presetIx)};
            int presetNum{preset.getProperty("num")};
            String presetName = preset.getProperty("name");
            rows.emplace_back(bankNum, presetNum, move(presetName));
        }
    }
    sort(rows.begin(), rows.end(), [](const TableRow& a, const TableRow& b) {
        if (a.bank != b.bank) return a.bank < b.bank;
        return a.preset < b.preset;
    });
    table.updateContent();
    selectCurrentPreset();
    table.repaint();
}

void TableComponent::parameterChanged(const String& parameterID, float /*newValue*/) {
    if (parameterID == "bank" || parameterID == "preset") {
        selectCurrentPreset();
    }
}

void TableComponent::valueTreePropertyChanged(
    ValueTree& treeWhosePropertyHasChanged,
    const Identifier& property) {
    if (treeWhosePropertyHasChanged.getType() == StringRef("banks")) {
        if (property == StringRef("synthetic")) {
            loadModelFrom(treeWhosePropertyHasChanged);
        }
    }
}

int TableComponent::getNumRows()
{
    return static_cast<int>(rows.size());
}

void TableComponent::paintRowBackground (
        Graphics& g,
        int rowNumber,
        int /*width*/,
        int /*height*/,
        bool rowIsSelected
) {
    const Colour alternateColour (getLookAndFeel().findColour (ListBox::backgroundColourId)
            .interpolatedWith (getLookAndFeel().findColour (ListBox::textColourId), 0.03f));
    if (rowIsSelected)
        g.fillAll (juce::Colours::lightblue);
    else if (rowNumber % 2)
        g.fillAll (alternateColour);
}

String TableRow::getStringContents(int columnId) {
    if (columnId == 1) {
        return String(bank);
    } else if (columnId == 2) {
        return String(preset);
    }
    return name;
}

void TableComponent::paintCell (
        Graphics& g,
        int rowNumber,
        int columnId,
        int width,
        int height,
        bool /*rowIsSelected*/
) {
    g.setColour (getLookAndFeel().findColour (ListBox::textColourId));
    g.setFont (font);

	if (rowNumber < (int)rows.size()) {
		TableRow& row{ rows[rowNumber] };
		String text{ row.getStringContents(columnId) };
		Justification just{ columnId == 3 ? Justification::centredLeft : Justification::centredRight };
		g.drawText(text, 2, 0, width - 4, height, just, true);
	}

    g.setColour (getLookAndFeel().findColour (ListBox::backgroundColourId));
    g.fillRect (width - 1, 0, 1, height);
}

void TableComponent::selectCurrentPreset() {
    int bank, preset;
    {
        AudioParameterInt* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("bank"))};
        jassert(p != nullptr);
        bank = p->get();
    }
    {
        AudioParameterInt* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("preset"))};
        jassert(p != nullptr);
        preset = p->get();
    }

    for (auto it{rows.begin()}; it != rows.end(); ++it) {
        if (it->bank == bank && it->preset == preset) {
            int index{static_cast<int>(distance(rows.begin(), it))};
            juce::ScopedValueSetter<bool> guard{selecting, true};
            table.selectRow(index);
            return;
        }
    }
    juce::ScopedValueSetter<bool> guard{selecting, true};
    table.deselectAllRows();
}

int TableComponent::getColumnAutoSizeWidth (int columnId) {
    if (columnId != 3)
        return 40;

    int widest = 32;
    for (int i{getNumRows()}; --i >= 0;) {
        TableRow& row{rows[i]};
        String text{row.getStringContents(columnId)};
        widest = jmax (widest, juce::GlyphArrangement::getStringWidthInt (font, text));
    }
    return widest + 8;
}

//==============================================================================
void TableComponent::resized() {
    table.setBoundsInset (BorderSize<int> (7));
}

void TableComponent::selectedRowsChanged (int row) {
    if (selecting) // change originated from selectCurrentPreset, not a user click
        return;
    if (row < 0 || row >= (int)rows.size()) {
        return;
    }
    int newBank{rows[row].bank};
    int newPreset{rows[row].preset};
    {
        AudioParameterInt* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("bank"))};
        jassert(p != nullptr);
        *p = newBank;
    }
    {
        AudioParameterInt* p{dynamic_cast<AudioParameterInt*>(valueTreeState.getParameter("preset"))};
        jassert(p != nullptr);
        *p = newPreset;
    }
}

bool TableComponent::keyPressed(const KeyPress &key) {
    return table.keyPressed(key);
}

TableRow::TableRow(
    int bank,
    int preset,
    String name
)
: bank{bank}
, preset{preset}
, name{name}
{}
