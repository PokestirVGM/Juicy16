#pragma once

// Every editor metric. Spacing comes from here, and derived sizes stay derived.
struct GuiConstants {
    // Master trim range in dB. The floor reads as -inf so hosts can automate to silence.
    inline static const float outputLevelMinDb = -24.0f;
    inline static const float outputLevelMaxDb = 12.0f;
    inline static const float outputLevelDefaultDb = 1.5f;

    // No maxWidth: PluginEditor caps it at the keyboard's full-range width.
    inline static const int maxHeight = 1000;

    // Outer margin, inner control margin, and the gap between control groups.
    inline static const int padding = 10;
    inline static const int innerPadding = 8;
    inline static const int groupGap = 12;
    inline static const float cornerRadius = 2.0f;

    // Type scale.
    inline static const float bodyFontHeight = 14.0f;
    inline static const float valueFontHeight = 13.0f;
    inline static const float labelFontHeight = 12.0f;
    inline static const float masterValueFontHeight = 26.0f;

    // Arc thickness as a fraction of knob diameter, so sizes keep the same weight.
    inline static const float knobArcThickness = 0.11f;

    // Header: the wordmark (opens settings) and the bank picker.
    inline static const int headerHeight = 48;
    inline static const int filePickerHeight = 30;
    inline static const int logoHeight = 14;
    inline static const int folderIconSize = 16;

    inline static const int pianoHeight = 66;
    inline static const int statusBarHeight = 24;

    // Right-hand panel.
    inline static const int panelInset = 16;
    inline static const int panelSectionGap = 12;
    inline static const int masterSectionHeight = 118;
    inline static const int effectsSectionHeight = 188;
    inline static const int bankSectionHeight = 72;
    inline static const int panelWidth = 288;

    // Channel rack columns, left to right. minInstrumentWidth stops shrinking.
    inline static const int channelHeaderHeight = 30;
    inline static const int channelRowHeight = 34;
    inline static const int numMidiChannels = 16;
    inline static const int channelNumberWidth = 34;
    inline static const int muteSoloWidth = 60;
    inline static const int mixerCellWidth = 80;
    inline static const int minInstrumentWidth = 180;
    inline static const int activityWidth = 80;
    // Row knob and its value readout.
    inline static const int rowKnobSize = 24;
    inline static const int rowValueWidth = 30;

    // Narrowest usable rack plus the right-hand panel and its divider.
    inline static const int minRackWidth =
        channelNumberWidth + muteSoloWidth + minInstrumentWidth
        + 3 * mixerCellWidth + activityWidth + 2 * padding;
    inline static const int minWidth = minRackWidth + panelWidth + 1;

    // Header, column header, 16 rows, keyboard and status bar.
    inline static const int defaultHeight =
        headerHeight
        + channelHeaderHeight + numMidiChannels * channelRowHeight
        + pianoHeight
        + statusBarHeight;

    inline static const int minHeight = defaultHeight;
};
