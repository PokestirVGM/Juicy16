# Licensing and source-distribution policy

Juicy16 uses JUCE 8 under its open-source AGPLv3 option. No commercial JUCE license is being claimed.

## Licenses that apply

- The inherited JuicySF application code remains under GNU GPL version 3 (`LICENSE.txt`). Existing copyright and author notices remain intact.
- JUCE 8 modules are used under GNU AGPL version 3 (`licenses_of_dependencies/JUCE-framework_AGPL3.txt`). JUCE documents its modules as dual-licensed under AGPLv3 or its commercial license.
- GPLv3 section 13 and AGPLv3 section 13 permit GPLv3 and AGPLv3-covered parts to be combined. The GPLv3 terms continue to apply to the GPL-covered parts, and AGPLv3 section 13 applies to the combined work.
- FluidSynth and the other bundled or statically linked dependencies retain their respective notices under `licenses_of_dependencies/`.

The current macOS static recipe pins FluidSynth 2.5.5, libsndfile 1.2.2, FLAC 1.5.0, libogg 1.3.6, libvorbis 1.3.7, Opus 1.6.1, and GCEM revision `012ae73c6d0a2cb09ffe86475f5c6fba3926e200`. Their LGPL, BSD-style, and Apache-2.0 texts/notices are staged from `licenses_of_dependencies/`.

The JUCE 8.0.14 modules compiled into the macOS artifact also embed HarfBuzz, SheenBidi, zlib, libpng, and Independent JPEG Group code. The package includes the available upstream license texts for those components and the IJG acknowledgement required for executable distribution. JUCE's copied Audio Unit SDK and VST3 SDK interface notices are included as well. The exact Windows closure still requires candidate-specific review.

This is a compatibility/distribution policy, not an attempt to relicense code owned by earlier contributors.

## Binary distribution requirements

Every distributed Beta binary/package must be accompanied by, or provide the required durable access to, the complete corresponding source for that exact candidate, including Juicy16 modifications, the vendored JUCE wrapper changes, build scripts, and other material required by the applicable licenses. The package must include:

- `LICENSE.txt`;
- `NOTICE.md`;
- `docs/LICENSING.md`;
- `licenses_of_dependencies/JUCE-framework_AGPL3.txt`; and
- all applicable third-party notices for the libraries actually present in the artifact.

Release notes must identify the exact source commit and source location. The project must not describe an AGPL build as closed source or impose terms that prevent recipients from exercising their GPL/AGPL rights. Juicy16 has no runtime networking, so it does not currently provide a network source-offer UI; normal binary-conveyance source obligations still apply.

The local `testfiles/` corpus is private, ignored by Git, and excluded from source archives and binary packages.

## Relinking and the source offer

FluidSynth and libsndfile are LGPL-2.1 and are linked **statically** into the macOS artifact. LGPL-2.1 section 6 requires that a recipient be able to relink the work against a modified version of those libraries.

Juicy16 satisfies that through complete corresponding source rather than by shipping object files, which is available to it because the whole work is already GPLv3/AGPLv3 and therefore distributed with its source. The materials that make relinking possible are:

- the Juicy16 source for the exact candidate commit named in `BUILD_INFO.txt`;
- `tools/build_macos_dependencies.sh`, which pins the version and SHA-256 of every statically linked dependency and builds the identical closure from upstream source;
- `vendor/juce_patched/`, containing the vendored JUCE wrapper sources, the reproducible patch, and the recorded input/output hashes;
- `CMakeLists.txt` and `building.macos.md`, which give the exact configure, build, and validation commands.

With those, a recipient can substitute a modified FluidSynth or libsndfile, rebuild, and produce a working plugin — which is the outcome section 6 exists to guarantee. `docs/DEPENDENCIES.md` lists what is actually present in the artifact, so the obligation can be checked against the binary rather than inferred.

The corresponding source must be published for the exact candidate at, or before, the moment the binary is distributed, and must remain available for as long as the binary is offered. A candidate whose source is not yet published is not distributable.

## Copyright notices

Open-source software remains copyrighted. The license grants recipients permission to use, study, modify, and redistribute the work under stated conditions; it does not erase copyright.

New Juicy16 work uses `Copyright (c) 2026 Pokestir`. This notice applies only to the new work and does not claim ownership of earlier Birchlabs or individual-contributor contributions. Historical notices remain in place.

Before public distribution, a qualified reviewer should confirm that the source offer, dependency inventory, notices, and ownership statements match the exact candidate artifact.
