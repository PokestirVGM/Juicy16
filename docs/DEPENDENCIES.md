# Dependencies

Everything built into Juicy16. The exact versions and checksums live in
`tools/build_macos_dependencies.sh` and `tools/build_windows_dependencies.ps1`;
if this page disagrees with them, the scripts win. Licences are covered in
[LICENSING.md](LICENSING.md).

| Component | Version | What it's for |
| --- | --- | --- |
| JUCE | 8.0.14 | Plugin framework (with its bundled HarfBuzz, SheenBidi, zlib, libpng, JPEG) |
| FluidSynth | 2.5.7 + Juicy16 patches | The synth: SF2, SF3 and DLS loading and playback |
| GCEM | commit `012ae73c` | Maths headers FluidSynth needs |
| libsndfile | 1.2.2 + security patch | SF3 sample decoding |
| FLAC | 1.5.0 | Codec for libsndfile |
| libogg | 1.3.6 | Ogg container |
| libvorbis | 1.3.7 | Codec for libsndfile |
| Opus | 1.6.1 | Codec for libsndfile |

The FluidSynth 2.5.7 archive's SHA-256 is
`ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a`.

Everything is linked statically, so the plugin only depends on system frameworks
(macOS) or system DLLs (Windows). The AU and VST3 SDKs come with JUCE as headers.

## Patches

- **JUCE:** the pinned VST3 multichannel wrapper and an AU CFString lifetime fix.
  See [vendor/juce_patched](../vendor/juce_patched/README.md).
- **FluidSynth:** CC1 vibrato strength, full-range DLS pan, and timer-thread
  cleanup after a lazy SoundFont unload. See
  [vendor/fluidsynth_patched](../vendor/fluidsynth_patched/README.md).
- **libsndfile:** a backported fix for CVE-2025-52194, a buffer overflow a crafted
  `.sf3` file could reach. The build checks the patched file's hash.

## Windows validation

The 2026-09-27 Windows 1.0.0-beta.1 candidate passed local Debug and Release
checks, including the full-range DLS pan patch, static MSVC runtime/codec,
SF2/SF3/DLS loading, system-only DLL imports and installer lifecycle. The native
recipe uses CMake config packages and explicitly enables Opus's static runtime.
That evidence predates the 2026-09-28 timer-thread cleanup patch. The later
beta.2 Windows Debug/Release closures include that patch and pass 19/19 checks
each with generated SF2/DLS/SF3 and MIDI inputs. Windows leak instrumentation,
itemized DAW and clean Windows 10 checks remain open.
Beta.3 rebuilds both Windows closures with the expanded ×64 CC1 patch and passes
19/19 checks in each configuration, plus package and isolated installer checks.
See [Windows evidence](WINDOWS_RELEASE.md).

## Security notes

The riskiest code is the file parsing: FluidSynth's bank loaders and, for SF3,
libsndfile and its codecs. Juicy16 limits what reaches them. It never repairs a
file over 512 MB, and it rejects files whose header claims more data than they
contain.

There's no networking: JUCE's web and cURL support are compiled out.
`WebKit.framework` is still linked on macOS because `juce_gui_extra` declares
it, but nothing uses it.
