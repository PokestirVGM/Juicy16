# JUCE 8.0.14 wrapper patches

Juicy16 replaces JUCE's VST3 wrapper with a patched copy. The VST3 patch lets
hosts change instruments per MIDI channel:

- It answers Cubase's early unit and program-list queries with the full
  16-channel structure, so all channels work, not just channel 1.
- It maps Program Change on each MIDI channel to that channel's `progChN`
  parameter, as FL Studio expects.
- It turns every point of a `progChN` automation queue into a Program Change at
  its exact sample, instead of keeping only the block's last value.

Stock JUCE 8.0.14 wrapper hashes:

```text
ae1186c98c011c8ccd3b17d1cc4e6c5ea67d5cebb443f08c56ae957fcd2e10d8  juce_audio_plugin_client_VST3.cpp
44cfaf16c5f843acd0c7efdac5ceb318b07e0bc6da65cc9829252af61517f0ac  juce_audio_plugin_client_VST3.mm
```

Patched files and the diff:

```text
60cd751e32be8487e7e3d5903ce9706b015892dfddf76e4b0407ceb2d5e0543e  juce_audio_plugin_client_VST3.cpp
f15ba7b2eaee6dab3cc96c1e242ec782ee8503a72d4d9d0bda0c7d3261f3d7d3  juce_audio_plugin_client_VST3.mm
6b17dc0975b0f640a79545725fabc218c484a316b2dc1eac9663b7785c0de388  juce-8.0.14-vst3-multitimbral.patch
```

CMake checks all five hashes before building. JUCE ships the files with CRLF
line endings; the diff uses LF. To reproduce it, copy the stock files somewhere,
convert them to LF, and run `patch -p1 < juce-8.0.14-vst3-multitimbral.patch`.

The AU target compiles a one-line patched copy of `juce_audio_plugin_client_AU_1.mm`.
JUCE 8.0.14 copies the temporary result of `toCFString()` without releasing it;
the patched `OwnedArray` takes ownership of that result directly. This preserves
the parameter text while releasing it on AU destruction. The installed JUCE and
Standalone source stay untouched. CMake checks the stock, generated, and diff
hashes before building:

```text
d6b4d9016335df4d92b829bba4d7ca99f60588a0a00b145033befeb66956d095  stock juce_audio_plugin_client_AU_1.mm (CRLF)
161911f3e5d6a2aa9e498cf700324943fc16626751c0d84098a778d99b1e7291  patched juce_audio_plugin_client_AU_1.mm (LF)
ed03eede36380a7db4cc2b0334ac78f553a4604275b1ceb853364d14b03d2a11  juce-8.0.14-au-cfstring-lifetime.patch
```

Updating JUCE means regenerating both patches and their hashes, then re-running
the AU/VST3 smoke tests and testing the wrappers in real hosts.
