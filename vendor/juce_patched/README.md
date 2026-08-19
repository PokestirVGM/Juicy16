# JUCE 8.0.14 VST3 wrapper patch

Juicy16 replaces only the VST3 format target's JUCE wrapper. AU and Standalone continue to compile the installed JUCE source. The patch preserves two generic Program Change routes required by FL Studio and Cubase: `IMidiMapping` and per-channel `IUnitInfo` program parameters. It converts every point in those program-parameter queues back into a timestamped, channelized MIDI Program Change so JUCE cannot collapse mid-block changes to the final value at block start. The patched wrapper is the sole `IUnitInfo` owner on both the VST3 component and controller; the application extension only supplies program names and change notifications, avoiding JUCE's duplicate-interface assertion.

The base is the unmodified JUCE 8.0.14 wrapper:

```text
ae1186c98c011c8ccd3b17d1cc4e6c5ea67d5cebb443f08c56ae957fcd2e10d8  juce_audio_plugin_client_VST3.cpp
44cfaf16c5f843acd0c7efdac5ceb318b07e0bc6da65cc9829252af61517f0ac  juce_audio_plugin_client_VST3.mm
```

The reviewed vendored result and normalized unified diff are:

```text
60cd751e32be8487e7e3d5903ce9706b015892dfddf76e4b0407ceb2d5e0543e  juce_audio_plugin_client_VST3.cpp
f15ba7b2eaee6dab3cc96c1e242ec782ee8503a72d4d9d0bda0c7d3261f3d7d3  juce_audio_plugin_client_VST3.mm
6b17dc0975b0f640a79545725fabc218c484a316b2dc1eac9663b7785c0de388  juce-8.0.14-vst3-multitimbral.patch
```

JUCE installs the base files with CRLF line endings. The committed diff is normalized to LF so it remains reviewable and portable. To reproduce it, copy both stock files to an empty directory, normalize their line endings, and run:

```bash
patch -p1 < juce-8.0.14-vst3-multitimbral.patch
```

The resulting LF files must match the normalized committed vendored `.cpp` and `.mm`. CMake verifies the two base hashes, two vendored hashes, and patch hash before it swaps the target source. Any JUCE update or intentional wrapper edit must regenerate the diff, update the reviewed hashes, and pass `vst3_multitimbral_smoke` and the real-host matrix before release.
