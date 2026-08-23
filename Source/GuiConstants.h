#pragma once

// Every metric the editor lays out with. Phase 9.3's rule: spacing and padding
// come from here, not from local literals, and the derived sizes below stay
// derived so a metric change moves the window with it.
struct GuiConstants {
    // Master output trim, in dB. The floor is treated as -inf so a host can
    // automate the plugin to actual silence; the ceiling is deliberately modest
    // because FluidSynth's own gain already sits at its documented default and
    // the point of the control is trim, not a second volume war.
    inline static const float outputLevelMinDb = -24.0f;
    inline static const float outputLevelMaxDb = 12.0f;

    // maxWidth is NOT here: PluginEditor caps it dynamically at the on-screen
    // keyboard's natural full-range width (see its constructor), so the window can
    // never be resized wider than needed to show the keyboard without trailing
    // blank space.
    inline static const int maxHeight = 1000;

    // Spacing scale. `padding` is the window's outer margin, `innerPadding` the
    // margin inside a control, `groupGap` the space between two groups of
    // controls in a row (the gap that stops Solo colliding with the instrument
    // name, per the owner's correction to the mockup).
    inline static const int padding = 10;
    inline static const int innerPadding = 8;
    inline static const int groupGap = 12;
    inline static const float cornerRadius = 2.0f;

    // Type scale.
    inline static const float bodyFontHeight = 13.0f;
    inline static const float valueFontHeight = 11.0f;
    inline static const float labelFontHeight = 11.0f;
    inline static const float masterValueFontHeight = 19.0f;

    // Knob arc thickness as a fraction of the knob's diameter, so a row knob and
    // a panel knob keep the same weight at different sizes.
    inline static const float knobArcThickness = 0.16f;

    // Header strip: the logo, the bank picker, and the settings button.
    inline static const int headerHeight = 38;
    inline static const int filePickerHeight = 24;
    inline static const int settingsButtonWidth = 24;
    inline static const int logoHeight = 14;

    inline static const int pianoHeight = 66;
    inline static const int statusBarHeight = 22;

    // Right-hand panel: master trim, reverb, and the loaded bank.
    inline static const int panelWidth = 236;

    // Channel rack metrics. The column widths are the row's anatomy, left to
    // right, and minInstrumentWidth is what an instrument name needs before the
    // window stops shrinking.
    inline static const int channelHeaderHeight = 22;
    inline static const int channelRowHeight = 26;
    inline static const int numMidiChannels = 16;
    inline static const int channelNumberWidth = 34;
    inline static const int muteSoloWidth = 46;
    inline static const int mixerCellWidth = 70;
    inline static const int minInstrumentWidth = 140;
    // The knob inside a mixer cell, and the value readout beside it.
    inline static const int rowKnobSize = 20;
    inline static const int rowValueWidth = 30;

    // Derived, not guessed. Minimum width is the narrowest row that keeps every
    // control usable, plus the fixed right-hand panel and its divider.
    inline static const int minRackWidth =
        channelNumberWidth + muteSoloWidth + minInstrumentWidth
        + 2 * mixerCellWidth + 2 * padding;
    inline static const int minWidth = minRackWidth + panelWidth + 1;

    // Default window height: the header, the column header, all 16 rows, the
    // keyboard and the status bar - so a fresh instance opens with the whole
    // channel rack visible and nothing more.
    inline static const int defaultHeight =
        headerHeight
        + channelHeaderHeight + numMidiChannels * channelRowHeight
        + pianoHeight
        + statusBarHeight;

    // Small floor: the user can shrink well below defaultHeight - the channel
    // rack just becomes scrollable (TableListBox provides that for free). The
    // floor is what the fixed strips need plus a few rows.
    inline static const int minHeight =
        headerHeight + channelHeaderHeight + 4 * channelRowHeight
        + pianoHeight + statusBarHeight;
};
