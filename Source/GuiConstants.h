#pragma once

struct GuiConstants {
    inline static const int minWidth = 500;

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

    // Small floor: the user can shrink well below defaultHeight below — the
    // channel list just becomes scrollable (TableListBox provides that for free).
    inline static const int minHeight = 300;
    inline static const int maxHeight = 1000;

    // Shared layout metrics — referenced by both PluginEditor::resized() and
    // defaultHeight below, so the two can't drift apart.
    inline static const int padding = 8;
    inline static const int filePickerHeight = 25;
    inline static const int pianoHeight = 70;
    inline static const int statusBarHeight = 16;

    // The channel list's own metrics: TableListBox's built-in default header
    // height (see JUCE's TableListBox::setHeader), the row height
    // ChannelListComponent sets (must match — see its constructor), and the fixed
    // MIDI channel count.
    inline static const int channelHeaderHeight = 28;
    inline static const int channelRowHeight = 24;
    inline static const int numMidiChannels = 16;

    // Default window height: reserves space for the file picker, status bar,
    // piano and padding, then sizes the channel list to its natural height
    // (header + all 16 rows) — so a fresh instance opens with the whole channel
    // list visible and nothing more.
    inline static const int defaultHeight =
        filePickerHeight + padding // top strip, incl. the padding trimmed off it
        + statusBarHeight
        + pianoHeight
        + 2 * padding // TablesComponent's vertical reduce
        + channelHeaderHeight + numMidiChannels * channelRowHeight;
};
