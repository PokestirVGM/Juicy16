# JuicySF Rack

A 16-channel multitimbral General MIDI sound module. Fork of Birch-san's juicysfplugin:
https://github.com/Birch-san/juicysfplugin

Targets Windows 7 i686
Commonly this means a 32-bit Intel or AMD processor.

## Installation

If the folder "C:\Program Files (x86)\Common Files\VST2" does not yet exist: make the folder yourself.

VST2\libJuicySF Rack.dll -> "C:\Program Files (x86)\Common Files\VST2\libJuicySF Rack.dll" (only present if this build was made with a VST2 SDK supplied)
Standalone\JuicySF Rack.exe -> "C:\Program Files (x86)\Birchlabs\JuicySF Rack.exe"

Note: VST3 is not supported. VST3 has no per-channel MIDI Program Change — a
Program Change can only reach a VST3 plugin as a single global program
parameter, never scoped to a channel, which breaks JuicySF Rack's core
"MIDI channel selects instrument" workflow. Use the VST2 or Standalone build.

## Usage

Pick a soundfont from the file-picker (drag-and-drop works too).

Route up to 16 external MIDI sources into the plugin, one per MIDI channel
(1-16). Each channel gets its own instrument, either by manually picking a
patch from that channel's dropdown, or automatically from incoming MIDI
Program Change on that channel (which takes priority over a manual pick).

### Soundfonts

Here's some soundfonts to get you started:

- Fatboy (no specific license stated, but described as "free")
  - https://fatboy.site/
- MuseScore's recommended soundfonts (includes MIT, GPL, other licenses)
  - https://musescore.org/en/handbook/soundfonts-and-sfz-files#list
- FlameStudios' guitar soundfonts (GPL-licensed)
  - http://www.flamestudios.org/free/Soundfonts
