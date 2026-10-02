#!/usr/bin/env bash

# Run the CI quality gates locally. `.github/workflows/ci.yml` runs the same
# configure/build/test commands, so a failure here is a failure there.
#
#   tools/ci_gates.sh docs        internal Markdown links only
#   tools/ci_gates.sh debug       Debug build, warnings-as-errors, CTest
#   tools/ci_gates.sh asan        sanitized offline harnesses
#   tools/ci_gates.sh leaks       macOS `leaks` run over every offline harness
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
deps_prefix=${JUICY16_DEPS_PREFIX:-"$repo_dir/build/macos11-deps"}
debug_ready=0

cd "$repo_dir"

# Refresh FluidSynth CMake cache entries at configure time too: FindPkgConfig
# otherwise retains the former Homebrew prefix when PKG_CONFIG_PATH changes.
prepare_dependencies() {
  if [[ ! -f "$deps_prefix/lib/pkgconfig/fluidsynth.pc" ]] || \
     ! grep -q '^#define FLUIDSYNTH_JUICY16_VIBRATO_SCALE 1$' \
       "$deps_prefix/include/fluidsynth/synth.h" || \
     ! grep -q '^#define FLUIDSYNTH_JUICY16_DLS_FULL_PAN 1$' \
       "$deps_prefix/include/fluidsynth/synth.h"; then
    JUICY16_BUILD_JOBS="$build_jobs" tools/build_macos_dependencies.sh "$deps_prefix"
  fi
  export PKG_CONFIG_PATH="$deps_prefix/lib/pkgconfig"
  export PKG_CONFIG_LIBDIR="$deps_prefix/lib/pkgconfig"
}

run_docs() {
  echo "== docs: internal Markdown links =="
  cmake -DSOURCE_ROOT="$repo_dir" -P tests/DocumentationLinkTests.cmake
}

run_debug() {
  echo "== debug: build with first-party warnings as errors =="
  prepare_dependencies
  cmake -U '*FLUIDSYNTH*' -S . -B build-ci-debug \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DCMAKE_PREFIX_PATH="$juce_prefix;$deps_prefix" \
    -DFLUIDSYNTH_LINK_STATIC=ON \
    -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF \
    -DJUICYSF_WARNINGS_AS_ERRORS=ON
  cmake --build build-ci-debug --config Debug --parallel "$build_jobs"
  ctest --test-dir build-ci-debug -C Debug --output-on-failure --no-tests=error
  debug_ready=1
}

run_asan() {
  echo "== asan: sanitized offline harnesses =="
  prepare_dependencies
  # Only the offline harnesses are sanitized; an unsanitized host cannot load a
  # sanitized plugin bundle.
  cmake -U '*FLUIDSYNTH*' -S . -B build-ci-asan \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DCMAKE_PREFIX_PATH="$juce_prefix;$deps_prefix" \
    -DFLUIDSYNTH_LINK_STATIC=ON \
    -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF \
    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
  cmake --build build-ci-asan \
    --target JuicySFFontQA JuicySFEngineMidiTests JuicySFPlaybackReliabilityTests \
      JuicySFRenderEquivalenceTests --parallel "$build_jobs"
  ASAN_OPTIONS=detect_leaks=0:abort_on_error=1 \
  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
    ctest --test-dir build-ci-asan -C Debug --output-on-failure \
      --no-tests=error \
      -R '^(font_repair_unit|engine_midi_system_dls|playback_reliability|render_equivalence)$'
}

run_leak_harness() {
  local name=$1 completion=$2
  shift 2
  local log="build-ci-debug/Testing/leaks-$name.log"
  local status=0 summary
  mkdir -p "$(dirname "$log")"
  echo "-- $name"
  MallocStackLogging=1 leaks -atExit -- "$@" > "$log" 2>&1 || status=$?
  summary=$(grep -E "leaks? for [0-9]+ total leaked bytes" "$log" | tail -1 || true)
  if [[ $status -ne 0 ]]; then
    echo "   leaks exited with status $status; see $log" >&2
    failed=1
  fi
  if [[ -z $summary ]]; then
    echo "   leaks produced no summary; see $log" >&2
    failed=1
  else
    echo "   $summary"
    if [[ ! $summary =~ (^|[[:space:]])0\ leaks\ for\ 0\ total\ leaked\ bytes\.?$ ]]; then
      echo "   leaked allocations remain; see $log" >&2
      failed=1
    fi
  fi
  # `leaks` reports its own result, not reliably the target's exit status. Debug
  # CTest checks the harness exit codes first; require completion here too, so
  # an early exit or an instrumented harness failure cannot look like success.
  if ! grep -qE "$completion" "$log" || grep -qE '^[[:space:]]*FAIL([[:space:]]|$)' "$log"; then
    echo "   the harness did not complete successfully; see $log" >&2
    failed=1
  fi
}

run_leaks() {
  echo "== leaks: Core Foundation and heap leaks in the offline harnesses =="
  # LeakSanitizer is unavailable on Darwin arm64, so the ASan gate runs with leak
  # detection off. macOS `leaks` covers that gap, and covers Core Foundation
  # objects the sanitizer would not attribute anyway.
  # An executable left by an earlier checkout is not evidence for this one.
  # `all` has already built/tested Debug in this invocation, so reuse it there.
  if [[ $debug_ready -ne 1 ]]; then
    run_debug
  fi

  local dls="/System/Library/Components/CoreAudio.component/Contents/Resources/gs_instruments.dls"
  local artefacts="build-ci-debug/JuicySFPlugin_artefacts/Debug"
  local failed=0
  run_leak_harness JuicySFFontQA \
    '^== summary: [1-9][0-9]* ok \([0-9]+ auto-repaired\), 0 failed, 0 unit-test failures ==$' \
    build-ci-debug/JuicySFFontQA "$dls"
  run_leak_harness JuicySFEngineMidiTests '^== engine_midi_tests: 0 failures ==$' \
    build-ci-debug/JuicySFEngineMidiTests "$dls" tests/fixtures/controller_conformance.csv
  run_leak_harness JuicySFVST3Smoke '^== vst3_smoke: 0 failures ==$' \
    build-ci-debug/JuicySFVST3Smoke "$artefacts/VST3/Juicy16.vst3" "$dls" \
    tests/fixtures/vst3_multichannel_programs.csv
  run_leak_harness JuicySFAUSmoke '^== au_smoke: 0 failures ==$' \
    build-ci-debug/JuicySFAUSmoke "$artefacts/AU/Juicy16.component" "$dls"

  if [[ $failed -ne 0 ]]; then
    exit 1
  fi
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

  prepare_dependencies

  env \
    PKG_CONFIG_PATH="$deps_prefix/lib/pkgconfig" \
    PKG_CONFIG_LIBDIR="$deps_prefix/lib/pkgconfig" \
  cmake -U '*FLUIDSYNTH*' -S . -B build-release \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
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
  ctest --test-dir build-release -C Release --output-on-failure --no-tests=error
}

case "$gate" in
  docs) run_docs ;;
  debug) run_debug ;;
  asan) run_asan ;;
  leaks) run_leaks ;;
  release) run_release ;;
  all) run_docs; run_debug; run_asan; run_leaks; run_release ;;
  *)
    echo "Unknown gate: $gate (expected docs, debug, asan, leaks, release, or all)" >&2
    exit 2
    ;;
esac

echo "Gate '$gate' passed."
