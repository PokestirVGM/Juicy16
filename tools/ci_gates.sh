#!/usr/bin/env bash

# Run the CI quality gates locally. `.github/workflows/ci.yml` runs the same
# configure/build/test commands, so a failure here is a failure there.
#
#   tools/ci_gates.sh docs        internal Markdown links only
#   tools/ci_gates.sh debug       Debug build, warnings-as-errors, CTest
#   tools/ci_gates.sh asan        sanitized offline harnesses
#   tools/ci_gates.sh release     strict portable Release build and CTest
#   tools/ci_gates.sh all         every gate above, in order
#
# JUCE is expected at $JUICE_PREFIX (default ~/juicydeps).

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/.." && pwd)
juce_prefix=${JUICE_PREFIX:-"$HOME/juicydeps"}
build_jobs=${JUICY16_BUILD_JOBS:-8}
gate=${1:-all}

cd "$repo_dir"

run_docs() {
  echo "== docs: internal Markdown links =="
  cmake -DSOURCE_ROOT="$repo_dir" -P tests/DocumentationLinkTests.cmake
}

run_debug() {
  echo "== debug: build with first-party warnings as errors =="
  cmake -S . -B build-ci-debug \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$juce_prefix;/opt/homebrew" \
    -DFLUIDSYNTH_LINK_STATIC=OFF \
    -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF \
    -DJUICYSF_WARNINGS_AS_ERRORS=ON
  cmake --build build-ci-debug --config Debug --parallel "$build_jobs"
  ctest --test-dir build-ci-debug -C Debug --output-on-failure
}

run_asan() {
  echo "== asan: sanitized offline harnesses =="
  # Only the offline harnesses are sanitized; an unsanitized host cannot load a
  # sanitized plugin bundle.
  cmake -S . -B build-ci-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_PREFIX_PATH="$juce_prefix;/opt/homebrew" \
    -DFLUIDSYNTH_LINK_STATIC=OFF \
    -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
  cmake --build build-ci-asan \
    --target JuicySFFontQA JuicySFEngineMidiTests --parallel "$build_jobs"
  ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    ctest --test-dir build-ci-asan -C Debug --output-on-failure \
      -R 'font_repair_unit|engine_midi_system_dls'
}

run_release() {
  echo "== release: strict portable macOS candidate =="
  case "$repo_dir" in
    *[[:space:]]*)
      echo "The strict Release gate cannot run from a path containing whitespace:" >&2
      echo "  $repo_dir" >&2
      echo "pkg-config emits unquoted -L flags, so the pinned dependency prefix" >&2
      echo "would be discarded and FluidSynth silently resolved elsewhere." >&2
      echo "Copy or clone the repository to a space-free path and rerun." >&2
      exit 2
      ;;
  esac

  local deps_prefix="$repo_dir/build/macos11-deps"
  if [[ ! -f "$deps_prefix/lib/pkgconfig/fluidsynth.pc" ]]; then
    JUICY16_BUILD_JOBS="$build_jobs" tools/build_macos_dependencies.sh "$deps_prefix"
  fi

  env \
    PKG_CONFIG_PATH="$deps_prefix/lib/pkgconfig" \
    PKG_CONFIG_LIBDIR="$deps_prefix/lib/pkgconfig" \
  cmake -S . -B build-release \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_PREFIX_PATH="$juce_prefix;$deps_prefix" \
    -DFLUIDSYNTH_LINK_STATIC=ON \
    -DJUICYSF_SF3_FIXTURE="$deps_prefix/share/juicy16-test-fixtures/VintageDreamsWaves-v2.sf3" \
    -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF \
    -DJUICYSF_RELEASE_VALIDATION=ON \
    -DJUICYSF_WARNINGS_AS_ERRORS=ON \
    -DJUICYSF_CODE_SIGN_IDENTITY="-"
  cmake --build build-release --config Release --parallel "$build_jobs"
  ctest --test-dir build-release -C Release --output-on-failure
}

case "$gate" in
  docs) run_docs ;;
  debug) run_debug ;;
  asan) run_asan ;;
  release) run_release ;;
  all) run_docs; run_debug; run_asan; run_release ;;
  *)
    echo "Unknown gate: $gate (expected docs, debug, asan, release, or all)" >&2
    exit 2
    ;;
esac

echo "Gate '$gate' passed."
