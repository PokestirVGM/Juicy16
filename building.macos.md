# Building Juicy16 on macOS

These commands describe the locally verified developer build. They do not by themselves create an approved public release: minimum/current-OS testing, host validation, signing identity, notarization, and final licensing review remain Beta 1 gates.

## Requirements

- CMake 3.15 or newer
- Xcode Command Line Tools or Xcode with a C++17 compiler
- JUCE exactly 8.0.14, installed as a CMake package
- FluidSynth 2.x and pkg-config

Example dependencies using Homebrew:

```bash
brew install cmake pkg-config fluid-synth
```

Install the pinned JUCE version to a prefix of your choice:

```bash
git clone https://github.com/juce-framework/JUCE.git
cd JUCE
git checkout 8.0.14
cmake -S . -B cmake-build-install \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$HOME/juicydeps"
cmake --build cmake-build-install --target install -j 8
```

## Debug build

From the Juicy16 repository:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH="$HOME/juicydeps;/opt/homebrew" \
  -DFLUIDSYNTH_LINK_STATIC=OFF \
  -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --config Debug -j 8
ctest --test-dir build -C Debug --output-on-failure
```

Artifacts are written below `build/JuicySFPlugin_artefacts/Debug/`. They are not copied into plugin folders unless `JUICYSF_COPY_PLUGIN_AFTER_BUILD=ON` is explicitly selected.

## Portable Release build

The release path must be built from a directory whose full path contains no spaces. pkg-config emits unquoted `-L`/`-I` flags, so whitespace splits the flag, the pinned dependency prefix is discarded, and FluidSynth silently resolves from Homebrew instead — producing a non-portable artifact. Strict configuration, the dependency recipe, and `tools/ci_gates.sh release` all refuse such a path with an explicit message. Clone or copy the repository somewhere space-free before building a candidate.

Beta artifacts must not depend on a developer's Homebrew paths. Build the pinned macOS 11 arm64 dependency closure from source first:

```bash
tools/build_macos_dependencies.sh build/macos11-deps
```

The script downloads checksum-pinned FluidSynth 2.5.5, GCEM, libogg 1.3.6, libvorbis 1.3.7, FLAC 1.5.0, Opus 1.6.1, and libsndfile 1.2.2 sources. It builds static arm64 archives with a macOS 11 deployment target. FluidSynth uses C++ threading, native DLS, and libsndfile-backed SF3; unused drivers, networking, shell editing, and its default-bank path are disabled.

Configure Juicy16 so pkg-config can see only that dependency prefix:

```bash
env \
  PKG_CONFIG_PATH="$PWD/build/macos11-deps/lib/pkgconfig" \
  PKG_CONFIG_LIBDIR="$PWD/build/macos11-deps/lib/pkgconfig" \
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH="$HOME/juicydeps;$PWD/build/macos11-deps" \
  -DFLUIDSYNTH_LINK_STATIC=ON \
  -DJUICYSF_SF3_FIXTURE="$PWD/build/macos11-deps/share/juicy16-test-fixtures/VintageDreamsWaves-v2.sf3" \
  -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF \
  -DJUICYSF_RELEASE_VALIDATION=ON \
  -DJUICYSF_CODE_SIGN_IDENTITY="-"
cmake --build build-release --config Release -j 8
ctest --test-dir build-release -C Release --output-on-failure
```

`-` produces an ad-hoc signature suitable for local testing. An approved Developer ID identity and notarization workflow are still required if the Beta distribution policy calls for them.

The approved Beta 1 target is macOS 11.0 or later on Apple Silicon. Strict release validation deliberately fails unless the deployment target is `11.0`, the architecture is exactly `arm64`, and FluidSynth is linked statically. This proves the artifact's declared target; runtime testing on macOS 11 and the current macOS release is still required before making a support claim.

Strict validation also inspects every absolute static archive reported by FluidSynth's pkg-config closure. It rejects an archive whose objects target a newer macOS release or lack `arm64`. Homebrew bottles installed on a newer macOS may therefore be suitable for local development but unsuitable for the Beta package. The dependency recipe remaps temporary source/build roots so assertion metadata cannot disclose developer or CI paths. It also retains FluidSynth's licensed SF3 test bank and required notice in the dependency prefix; strict CTest must load that bank, and the packager never includes it.

In strict mode, CTest also verifies the AU, VST3, and Standalone executables are arm64-only, declare macOS 11.0, contain no prohibited dynamic or embedded paths, and pass strict code-signature verification.

## Artifact checks

```bash
AU="build-release/JuicySFPlugin_artefacts/Release/AU/Juicy16.component"
VST3="build-release/JuicySFPlugin_artefacts/Release/VST3/Juicy16.vst3"

codesign --verify --deep --strict --verbose=2 "$AU"
codesign --verify --deep --strict --verbose=2 "$VST3"
file "$AU/Contents/MacOS/Juicy16"
file "$VST3/Contents/MacOS/Juicy16"
otool -L "$AU/Contents/MacOS/Juicy16"
otool -L "$VST3/Contents/MacOS/Juicy16"
```

Reject a release artifact if `otool -L` contains `/opt/homebrew`, `/usr/local`, a user home directory, the repository, or a build directory. Verify `LC_BUILD_VERSION`/deployment targets with `otool -l` on the exact packaged candidate.

## AU validation and local installation

`auval` discovers installed Audio Units, so installing a candidate can replace a plugin used by existing projects. Back up or move any existing copy first, then copy the exact candidate to `~/Library/Audio/Plug-Ins/Components/` and run:

```bash
auval -v aumu Jc16 Pkst
```

Do not mark the milestone task complete until the exact packaged candidate passes. VST3 may be installed under `~/Library/Audio/Plug-Ins/VST3/` for host validation.

## Candidate archive

After the strict Release suite passes, build and revalidate a candidate archive with:

```bash
JUICY16_REQUIRE_DISTRIBUTION_SIGNATURE=1 \
distribute/bundle_macos.sh \
  build-release/JuicySFPlugin_artefacts/Release \
  BC1
```

The packager rechecks metadata, signatures, architecture, deployment targets, dynamic dependencies, and embedded paths before staging. It includes only AU/VST3 plus the selected documentation/notices, writes internal and external SHA-256 manifests, fixes archive timestamps and ordering, extracts the ZIP to a clean temporary directory, verifies every file hash, and reruns artifact validation there.

It refuses a dirty worktree and, when `JUICY16_REQUIRE_DISTRIBUTION_SIGNATURE=1` is set, refuses ad-hoc signatures. For local workflow testing only, `JUICY16_ALLOW_DIRTY_PACKAGE=1` permits a dirty input and labels the filename `LOCAL-DIRTY`; ad-hoc input is always labelled `ADHOC`. Never publish a package carrying either label.

## Architectures

Beta 1 is Apple Silicon `arm64` only. Intel macOS is deferred until compatible JUCE and static FluidSynth dependencies exist and a physical Intel test system or tester is available.

## Optional targets

Standalone is a QA target. VST2 is not configurable or built by the Beta 1 CMake project.

## Quality gates

`tools/ci_gates.sh` runs the same gates as CI, one at a time or all together:

```bash
tools/ci_gates.sh docs      # internal Markdown links
tools/ci_gates.sh debug     # Debug build, first-party warnings as errors, CTest
tools/ci_gates.sh asan      # sanitized offline engine/DLS harnesses
tools/ci_gates.sh release   # strict portable Release build and CTest
tools/ci_gates.sh all
```

`.github/workflows/ci.yml` calls this script, so a local failure is a CI failure.  Run `tools/ci_gates.sh` with no argument list to see the gate names; a passing run
proves the automated gates only, not DAW or clean-machine behaviour.
