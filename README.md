# JuicySF Rack

JuicySF Rack is a **16-channel multitimbral General MIDI sound module**, built as a native Apple Silicon audio plugin. It's a fork of [Birch-san's juicysfplugin](https://github.com/Birch-san/juicysfplugin) — the original is a single-instrument soundfont player; this fork rebuilds it into a 16-channel rack modeled on hardware multitimbral modules (in the spirit of tools like Fruity LSD).

[JUCE](https://github.com/juce-framework/JUCE) is the framework. [FluidSynth](https://www.fluidsynth.org/) is the synthesis engine.

## The core idea

You don't put notes on the track that holds this plugin. Instead:

1. Load JuicySF Rack **once**, as an instrument in your DAW.
2. Route up to **16 separate MIDI sources** into it — one per MIDI channel (1–16). In FL Studio, for example, that's 16 "MIDI Out" tracks pointed at this plugin's port, each set to a different channel.
3. Load a SoundFont or DLS bank. Each of the 16 channels gets its own instrument.
4. **Incoming MIDI Program Change on a channel is authoritative** — it instantly selects that channel's instrument and updates the UI to match, exactly like a hardware GM module responding to a MIDI file. You can also assign instruments by hand per channel; manual picks are the fallback until a Program Change overrides them.

All 16 channels mix down to a single stereo output.

## Interface

- **Channel list (1–16)**: one row per MIDI channel, each with its own **Patch Selection** dropdown — populated live from whatever SoundFont/DLS is loaded, flat and sorted by bank then preset. Pick a row to select it for editing; the row highlights and its ADSR/filter sliders load on the right.
- **ADSR/filter sliders**: attack, decay, sustain, release, filter cutoff, and resonance for the selected channel (mapped to SoundFont-spec MIDI CCs 71–75/79). Centered at 64 = no change from the SoundFont's own settings, matching the standard MIDI/GS convention — turn up or down from there.
- **On-screen keyboard**: follows whichever channel is selected, so you can audition an instrument directly. Lights up for MIDI arriving on any channel. Resizing is capped so the window can never stretch past the keyboard's own natural width.
- **Status bar**: shows the build version, so it's always obvious whether a rebuilt plugin actually reloaded in your DAW.

## Formats

- **AU** (Audio Unit) — the primary supported format on macOS.
- **Standalone** app — for testing without a DAW.
- **VST2** — builds only if you supply a VST2 SDK (see [Building from source](#building-from-source-macos)); Steinberg no longer distributes it, so it's not bundled here.
- **VST3 is intentionally not supported.** The VST3 format has no per-channel MIDI Program Change — a Program Change can only reach a VST3 plugin as a single *global* program parameter, never scoped to a channel. That makes the core "MIDI channel selects instrument" workflow impossible under VST3, no matter how the plugin is written. AU and VST2 both deliver MIDI Program Change per channel correctly, so those are the supported targets.

## SoundFont / DLS support

Loads `.sf2`, `.sf3`, and `.dls`. DLS files exported by some tools (notably Awave Studio) carry malformed RIFF chunk sizes that strict parsers — including FluidSynth's — reject outright with an "early EOF" error, even though lenient players load them fine. JuicySF Rack detects this and **repairs the file automatically on load** (a corrected copy is used internally; your original file is never modified), so those DLS files just work.

## Playback fidelity

- 7th-order ("highest") sample interpolation.
- 512-voice polyphony, so dense 16-channel material doesn't steal voices mid-note.
- The synth's sample rate always tracks the host's actual sample rate.
- SoundFont-spec CC71–79 modulators (filter/envelope) use the correct bipolar MIDI convention — a channel that's never touched those CCs behaves identically to plain FluidSynth defaults; there's no hidden coloration from CCs sitting at their spec-neutral value.

## Building from source (macOS)

Requires JUCE (7.x/8.x) installed via CMake, and FluidSynth ≥ 2 available via pkg-config (Homebrew's `fluid-synth` works):

```bash
brew install fluid-synth pkg-config

git clone git@github.com:juce-framework/JUCE.git
cd JUCE && git checkout 8.0.14
cmake -B cmake-build-install -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/juicydeps"
cmake --build cmake-build-install --target install

cd /path/to/JuicySF-Rack
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH="$HOME/juicydeps;/opt/homebrew"
cmake --build build --target JuicySFPlugin_Standalone JuicySFPlugin_AU
```

Built artifacts land in `build/JuicySFPlugin_artefacts/Debug/` (Standalone `.app`, AU `.component`) and auto-install to your user plugin folders (`COPY_PLUGIN_AFTER_BUILD` in `CMakeLists.txt`).

To also build the VST2 `.vst`, drop a VST2 SDK's `pluginterfaces/vst2.x/` headers into `VST2_SDK/` before configuring — CMake detects it automatically and adds the `JuicySFPlugin_VST` target.

For Windows cross-compilation, see `building.win32.md` and `win32.Dockerfile`.

## Known limitations

- Single stereo output — all 16 channels mix down together; there's no per-channel audio-out routing.
- No VST3 (see above).
- VST2 requires supplying your own SDK.

## Licenses

Overall, JuicySF Rack is GPLv3, inheriting from the original juicysfplugin. See [licenses for all libraries and frameworks](licenses_of_dependencies/).
