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
TOOLCHAIN=x86_64

  echo "arch: $ARCH"
  echo "repo: $REPO"
  echo "toolchain: $TOOLCHAIN"
  TOOLCHAIN_FILE="/${TOOLCHAIN}_toolchain.cmake"
  echo "toolchain file: $TOOLCHAIN_FILE"

  BUILD="build_$ARCH"

  # OpenMP doesn't support static libraries on Windows:
  # https://github.com/llvm/llvm-project/blob/main/openmp/README.rst#options-for-libomp
  # Yet, our priority is "statically-link everything" (in order to build a single-file binary,
  # which can be installed by drag-and-drop).
  PKG_CONFIG_PATH="/$REPO/lib/pkgconfig" cmake -B"$BUILD" -DCMAKE_INSTALL_PREFIX="/$REPO" \
-DBUILD_SHARED_LIBS=off \
-Dosal=cpp11 \
-Denable-portaudio=off \
-Denable-dbus=off \
-Denable-aufile=off \
-Denable-ipv6=off \
-Denable-jack=off \
-Denable-ladspa=off \
-Denable-libinstpatch=off \
-Denable-native-dls=on \
-Denable-libsndfile=on \
-Denable-midishare=off \
-Denable-opensles=off \
-Denable-oboe=off \
-Denable-network=off \
-Denable-oss=off \
-Denable-dsound=off \
-Denable-wasapi=off \
-Denable-waveout=off \
-Denable-winmidi=off \
-Denable-sdl2=off \
-Denable-pkgconfig=on \
-Denable-pulseaudio=off \
-Denable-readline=off \
-Denable-threads=on \
-Denable-openmp=off \
-Denable-coreaudio=off \
-Denable-coremidi=off \
-Denable-framework=off \
-Denable-lash=off \
-Denable-alsa=off \
-Denable-systemd=off \
-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
-DCMAKE_BUILD_TYPE=Release
