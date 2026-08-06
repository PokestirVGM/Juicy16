# Building JuicySF Rack on macOS

These commands describe the locally verified developer build. They do not by themselves create an approved public release: minimum macOS, released architectures, signing identity, notarization, product identity, and licensing remain Beta 1 gates.

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

From the JuicySF Rack repository:

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

Beta artifacts must not depend on a developer's Homebrew paths. With a FluidSynth pkg-config installation that exposes its static dependency closure:

```bash
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$HOME/juicydeps;/opt/homebrew" \
  -DFLUIDSYNTH_LINK_STATIC=ON \
  -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF \
  -DJUICYSF_CODE_SIGN_IDENTITY="-"
cmake --build build-release --config Release -j 8
ctest --test-dir build-release -C Release --output-on-failure
```

`-` produces an ad-hoc signature suitable for local testing. An approved Developer ID identity and notarization workflow are still required if the Beta distribution policy calls for them.

The local test artifact built without `CMAKE_OSX_DEPLOYMENT_TARGET` inherited macOS 26.0 and must not be distributed as a compatible Beta. After Phase 0 approves the minimum OS and architecture list, add explicit `-DCMAKE_OSX_DEPLOYMENT_TARGET=<approved>` and `-DCMAKE_OSX_ARCHITECTURES=<approved>`, plus `-DJUICYSF_RELEASE_VALIDATION=ON`. Strict release validation deliberately fails when those values or static FluidSynth linkage are absent.

## Artifact checks

```bash
AU="build-release/JuicySFPlugin_artefacts/Release/AU/JuicySF Rack.component"
VST3="build-release/JuicySFPlugin_artefacts/Release/VST3/JuicySF Rack.vst3"

codesign --verify --deep --strict --verbose=2 "$AU"
codesign --verify --deep --strict --verbose=2 "$VST3"
file "$AU/Contents/MacOS/JuicySF Rack"
file "$VST3/Contents/MacOS/JuicySF Rack"
otool -L "$AU/Contents/MacOS/JuicySF Rack"
otool -L "$VST3/Contents/MacOS/JuicySF Rack"
```

Reject a release artifact if `otool -L` contains `/opt/homebrew`, `/usr/local`, a user home directory, the repository, or a build directory. Verify `LC_BUILD_VERSION`/deployment targets with `otool -l` after the minimum macOS decision is approved.

## AU validation and local installation

`auval` discovers installed Audio Units, so installing a candidate can replace a plugin used by existing projects. Back up or move any existing copy first, then copy the exact candidate to `~/Library/Audio/Plug-Ins/Components/` and run:

```bash
auval -v aumu Jsfr Blbs
```

Do not mark the milestone task complete until the exact packaged candidate passes. VST3 may be installed under `~/Library/Audio/Plug-Ins/VST3/` for host validation.

## Architectures

The locally verified 2026-08-05 build was `arm64`. A universal build can be requested with `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` only when compatible JUCE and static FluidSynth dependencies exist for both architectures. Universal support is not considered proven until `lipo -archs`, dependency inspection, tests, and host validation pass on both machines.

## Optional targets

Standalone is a QA target. VST2 is disabled by default and is not a Beta 1 format; enabling `JUICYSF_ENABLE_LEGACY_VST2` does not make it supported.
