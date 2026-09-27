# Licensing

## What applies

- Juicy16's own code, inherited from JuicySF, is **GPLv3** ([LICENSE.txt](../LICENSE.txt)).
  The original copyright notices stay in place.
- **JUCE 8** is used under its open-source **AGPLv3** option. I don't claim a
  commercial JUCE licence.
- GPLv3 and AGPLv3 allow the two to be combined; AGPLv3 section 13 covers the
  combined work.
- **FluidSynth** and **libsndfile** are LGPL-2.1. FLAC, Ogg, Vorbis and Opus are
  BSD-style, and GCEM is Apache-2.0. JUCE also embeds HarfBuzz, SheenBidi, zlib,
  libpng and IJG JPEG code. All their licence texts are in
  `licenses_of_dependencies/`.

## What every release includes

- `LICENSE.txt`, `NOTICE.md` and this file
- every applicable licence in `licenses_of_dependencies/`
- the FluidSynth patches in `vendor/fluidsynth_patched/`
- `BUILD_INFO.txt` (macOS) or `BUILD_INFO.json` (Windows), identifying the source

## Source and relinking

The full source for every release is public at
[github.com/PokestirVGM/Juicy16](https://github.com/PokestirVGM/Juicy16), tagged
to match the release. FluidSynth and libsndfile are linked statically, so LGPL
requires that you can rebuild Juicy16 with your own modified versions of them.
The source makes that possible:

- the dependency scripts (`tools/build_macos_dependencies.sh`,
  `tools/build_windows_dependencies.ps1`) pin and build every library from
  upstream source
- `vendor/` holds every patch Juicy16 applies to JUCE, FluidSynth and libsndfile
- `CMakeLists.txt` and the build guides give the exact build commands

Windows packages also include a matching corresponding-source ZIP with the exact
project snapshot, original dependency archives and pinned JUCE sources.
`SOURCE_INFO.json` records the base commit and local modifications. Ship that
matching source ZIP with the portable ZIP and installer; see the
[offline rebuild instructions](../building.win32.md). Publish the corresponding
source when distributing the binaries and keep it available while offering them.
The private `testfiles/` corpus is excluded from packages.

## Copyright

New Juicy16 work is `Copyright (c) 2026 Pokestir`. That doesn't claim ownership of
earlier Birchlabs or contributor work. Open-source code is still copyrighted: the
licences grant permission to use, change and share it under their terms.

I reviewed the licensing myself; it hasn't had a formal legal review.
