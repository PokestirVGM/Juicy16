#!/usr/bin/env bash
set -eo pipefail
shopt -s nullglob

cd fluidsynth

ARCH="${1:-x64}"
if [[ "$ARCH" != "x64" ]]; then
  echo "Only Windows x64 is in the Juicy16 Beta 1 scope; requested: $ARCH" >&2
  exit 2
fi
REPO=clang64

  echo "arch: $ARCH"
  echo "repo: $REPO"

  BUILD="build_$ARCH"

  cmake --build "$BUILD" --target libfluidsynth
  # manual installation; not sure how to ask it to "only install libfluidsynth".
  cp "$BUILD"/fluidsynth.pc "/$REPO/lib/pkgconfig/"
  cp "$BUILD"/src/libfluidsynth*.a "/$REPO/lib/libfluidsynth.a"
  mkdir -p /$REPO/include/fluidsynth
  cp include/fluidsynth/*.h "/$REPO/include/fluidsynth/"
  cp "$BUILD"/include/fluidsynth.h "/$REPO/include/fluidsynth.h"
  cp "$BUILD"/include/fluidsynth/*.h "/$REPO/include/fluidsynth/"
