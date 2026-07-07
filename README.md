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

- **AU** (Audio Unit) — the primary format on macOS (FL Studio, Logic, etc.). Delivers per-channel MIDI Program Change natively.
- **VST3** — for Cubase (which supports neither AU nor, since Cubase 13, VST2) and Windows. VST3 has no per-channel Program Change *event*, so a host has to route it one of two ways, and JuicySF Rack supports both:
  - Sixteen `Ch N Prog` parameters (0–127), automatable in any host, driven by MIDI Program Change via `IMidiMapping`.
  - Sixteen VST3 **units** (one per MIDI channel) sharing one program list — the mechanism HALion-style multitimbral instruments use — implemented via JUCE's `VST3ClientExtensions` (`Source/VST3Multitimbral.cpp`), no JUCE source changes required for this part.

  Getting Cubase working end-to-end also required fixing two real bugs in **stock JUCE's VST3 wrapper** (not this plugin's code): an out-of-bounds array read that returns a garbage parameter ID when a host asks where Program Change should go, and a timing hole where the wrapper reports "no program lists" to any host that queries unit structure before the plugin's internal component/controller connection completes — which some hosts (Cubase) do immediately and then cache forever. Both are fixed in a small vendored patch (`vendor/juce_patched/`, ~40 changed lines, diffable against the stock file) that CMake swaps in automatically when building the VST3 target — no manual step needed. AU/AUv3/Standalone are unaffected (they don't compile that file).
- **Standalone** app — for testing without a DAW.
- **VST2** — builds only if you supply a VST2 SDK (see [Building from source](#building-from-source-macos)); Steinberg no longer distributes it, so it's not bundled here.

## SoundFont / DLS support

Loads `.sf2`, `.sf3`, and `.dls`. DLS files exported by some tools (notably Awave Studio) carry malformed RIFF chunk sizes that strict parsers — including FluidSynth's — reject outright with an "early EOF" error, even though lenient players load them fine. JuicySF Rack detects this and **repairs the file automatically on load** (a corrected copy is used internally; your original file is never modified), so those DLS files just work.

## Playback fidelity

- 7th-order ("highest") sample interpolation.
- 512-voice polyphony, so dense 16-channel material doesn't steal voices mid-note.
- The synth's sample rate always tracks the host's actual sample rate.
- SoundFont-spec CC71–79 modulators (filter/envelope) use the correct bipolar MIDI convention — a channel that's never touched those CCs behaves identically to plain FluidSynth defaults; there's no hidden coloration from CCs sitting at their spec-neutral value.
- Self-heals from GM/GS/XG system-reset SysEx: FluidSynth resets every channel's program internally when it passes one through (game-rip MIDI files commonly carry one at tick 0), invisibly to the plugin's own state and the host's parameter cache. JuicySF Rack detects the reset and immediately re-asserts every channel's saved program, so it can't silently strip your channel assignments on replay.

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
cmake --build build --target JuicySFPlugin_Standalone JuicySFPlugin_AU JuicySFPlugin_VST3
```

Built artifacts land in `build/JuicySFPlugin_artefacts/Debug/` (Standalone `.app`, AU `.component`) and auto-install to your user plugin folders (`COPY_PLUGIN_AFTER_BUILD` in `CMakeLists.txt`).

To also build the VST2 `.vst`, drop a VST2 SDK's `pluginterfaces/vst2.x/` headers into `VST2_SDK/` before configuring — CMake detects it automatically and adds the `JuicySFPlugin_VST` target.

For Windows cross-compilation, see `building.win32.md` and `win32.Dockerfile`.

## Known limitations

- Single stereo output — all 16 channels mix down together; there's no per-channel audio-out routing.
- VST2 requires supplying your own SDK.

## Licenses

Overall, JuicySF Rack is GPLv3, inheriting from the original juicysfplugin. See [licenses for all libraries and frameworks](licenses_of_dependencies/).
