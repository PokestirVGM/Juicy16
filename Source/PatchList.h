//
// Flat, sorted list of every preset in the loaded soundfont/DLS.
// Shared by the per-channel patch dropdowns.
//

#pragma once

#include "../JuceLibraryCode/JuceHeader.h"
#include <vector>
#include <algorithm>

struct Patch {
    int bank;
    int preset;
    juce::String name;
};

// Build a flat list of all presets in the given `banks` ValueTree, sorted by
// bank then preset. (Mirrors the iteration+sort TableComponent used to do.)
inline std::vector<Patch> buildPatchList(const juce::ValueTree& banks) {
    std::vector<Patch> patches;
    for (int b = 0; b < banks.getNumChildren(); ++b) {
        juce::ValueTree bank{banks.getChild(b)};
        int bankNum{bank.getProperty("num")};
        for (int p = 0; p < bank.getNumChildren(); ++p) {
            juce::ValueTree preset{bank.getChild(p)};
            patches.push_back({bankNum,
                               static_cast<int>(preset.getProperty("num")),
                               preset.getProperty("name").toString()});
        }
    }
    std::sort(patches.begin(), patches.end(), [](const Patch& a, const Patch& b) {
        if (a.bank != b.bank) return a.bank < b.bank;
        return a.preset < b.preset;
    });
    return patches;
}

// e.g. "0:000 Acoustic Grand Piano"
inline juce::String patchLabel(const Patch& p) {
    return juce::String(p.bank) + ":" + juce::String(p.preset).paddedLeft('0', 3)
        + " " + p.name;
}
