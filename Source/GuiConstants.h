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
    // Default trim. See PluginProcessor's parameter layout for the measurement
    // this number comes from.
    inline static const float outputLevelDefaultDb = 1.5f;

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
    inline static const float bodyFontHeight = 14.0f;
    inline static const float valueFontHeight = 13.0f;
    inline static const float labelFontHeight = 12.0f;
    inline static const float masterValueFontHeight = 26.0f;

    // Knob arc thickness as a fraction of the knob's diameter, so a row knob and
    // a panel knob keep the same weight at different sizes.
    inline static const float knobArcThickness = 0.11f;

    // Header strip: the wordmark (which opens settings) and the bank picker.
    inline static const int headerHeight = 48;
    inline static const int filePickerHeight = 30;
    inline static const int logoHeight = 14;
    // The folder is the header's only icon now that the settings cog is gone, so
    // it carries the corner on its own and is drawn a little larger than the 14px
    // it used when it had to match a gear beside it.
    inline static const int folderIconSize = 16;

    inline static const int pianoHeight = 66;
    inline static const int statusBarHeight = 24;

    // Right-hand panel: shared section rhythm, including the selected channel.
    inline static const int panelInset = 16;
    inline static const int panelSectionGap = 12;
    inline static const int masterSectionHeight = 118;
    inline static const int effectsSectionHeight = 188;
    inline static const int bankSectionHeight = 72;
    // Right-hand panel: master trim, effects, bank and channel state.
    inline static const int panelWidth = 288;

    // Channel rack metrics. The column widths are the row's anatomy, left to
    // right, and minInstrumentWidth is what an instrument name needs before the
    // window stops shrinking.
    inline static const int channelHeaderHeight = 30;
    inline static const int channelRowHeight = 34;
    inline static const int numMidiChannels = 16;
    inline static const int channelNumberWidth = 34;
    inline static const int muteSoloWidth = 60;
    inline static const int mixerCellWidth = 80;
    inline static const int minInstrumentWidth = 180;
    inline static const int activityWidth = 80;
    // The knob inside a mixer cell, and the value readout beside it.
    inline static const int rowKnobSize = 24;
    inline static const int rowValueWidth = 30;

    // Derived, not guessed. Minimum width is the narrowest row that keeps every
    // control usable, plus the fixed right-hand panel and its divider.
    inline static const int minRackWidth =
        channelNumberWidth + muteSoloWidth + minInstrumentWidth
        + 3 * mixerCellWidth + activityWidth + 2 * padding;
    inline static const int minWidth = minRackWidth + panelWidth + 1;

    // Default window height: the header, the column header, all 16 rows, the
    // keyboard and the status bar - so a fresh instance opens with the whole
    // channel rack visible and nothing more.
    inline static const int defaultHeight =
        headerHeight
        + channelHeaderHeight + numMidiChannels * channelRowHeight
        + pianoHeight
        + statusBarHeight;

    // The master, bank and channel diagnostics must fit above the keyboard.
    inline static const int minHeight = defaultHeight;
};
