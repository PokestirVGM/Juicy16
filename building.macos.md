# Building Juicy16 on macOS

You don't need this to use Juicy16; the release has everything built in. This is
for building from source.

## Requirements

- An Apple Silicon Mac with Xcode or the Command Line Tools
- CMake 3.15+ and pkg-config (`brew install cmake pkg-config`)
- JUCE 8.0.14 installed as a CMake package:

```bash
git clone https://github.com/juce-framework/JUCE.git
cd JUCE && git checkout 8.0.14
cmake -S . -B build-install -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/juicydeps"
cmake --build build-install --target install -j 8
```

Clone Juicy16 to a path **without spaces**. pkg-config doesn't quote paths, so a
space silently links the wrong FluidSynth. The release scripts refuse such paths.

## The easy way

`tools/ci_gates.sh` does everything, including building the patched dependencies
the first time:

```bash
tools/ci_gates.sh debug     # Debug build and tests
tools/ci_gates.sh release   # strict arm64 / macOS 11 Release build and tests
tools/ci_gates.sh all       # docs, debug, sanitizers, leaks, release
```

CI runs the same script.

## By hand

Build the pinned, patched dependencies (FluidSynth 2.5.7 and its codecs, all
static, targeting macOS 11 arm64). Stock Homebrew FluidSynth won't work because
it lacks Juicy16's patches.

```bash
tools/build_macos_dependencies.sh "$PWD/build/macos11-deps"
```

Then configure and build a release:

```bash
env PKG_CONFIG_PATH="$PWD/build/macos11-deps/lib/pkgconfig" \
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

The plugins end up in `build-release/JuicySFPlugin_artefacts/Release/`. They're
ad-hoc signed (`-`). Release validation fails unless they're arm64-only, target
macOS 11, link FluidSynth statically and contain no developer paths.

## Packaging

From a clean, committed tree:

```bash
distribute/bundle_macos.sh build-release/JuicySFPlugin_artefacts/Release BC1
```

This rechecks the plugins, builds `distribute/out/Juicy16-<version>-BC1-macos-arm64-ADHOC.zip`
with checksums, then unzips it and verifies everything again.
`JUICY16_ALLOW_DIRTY_PACKAGE=1` allows an uncommitted tree for local testing, and
the zip is labelled `LOCAL-DIRTY`. Don't publish those.

## Checking the AU

Install the build to `~/Library/Audio/Plug-Ins/Components/` (back up any
existing copy first) and run `auval -v aumu Jc16 Pkst`.
