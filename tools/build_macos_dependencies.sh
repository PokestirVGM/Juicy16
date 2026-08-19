#!/usr/bin/env bash

# Build the minimal static dependency closure used by the Juicy16 macOS Beta.
# Sources and build trees live in a temporary directory; only installed build
# products are written to the requested prefix.
set -euo pipefail

if [[ $(uname -s) != Darwin || $(uname -m) != arm64 ]]; then
  echo "This dependency recipe currently supports only Apple Silicon macOS." >&2
  exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/.." && pwd)
install_prefix=${1:-"$repo_dir/build/macos11-deps"}
build_jobs=${JUICY16_BUILD_JOBS:-8}
work_dir=$(mktemp -d /private/tmp/juicy16-deps.XXXXXX)
trap 'rm -rf -- "$work_dir"' EXIT

case "$install_prefix" in
  /|/Users|/private|/private/tmp)
    echo "Refusing unsafe dependency install prefix: $install_prefix" >&2
    exit 2
    ;;
esac

# pkg-config emits unquoted -L/-I flags, so a space anywhere in the prefix is
# split into two arguments. The linker then silently resolves FluidSynth from
# another prefix (typically Homebrew) instead of this closure.
case "$install_prefix" in
  *[[:space:]]*)
    echo "Dependency install prefix must not contain whitespace: $install_prefix" >&2
    echo "pkg-config cannot quote such a path; build from a space-free location." >&2
    exit 2
    ;;
esac

fetch_source() {
  local name=$1
  local url=$2
  local expected_sha=$3
  local archive="$work_dir/$name.tar.gz"
  local source_dir="$work_dir/$name"

  curl -fsSL --retry 3 --max-time 120 -o "$archive" "$url"
  if ! printf '%s  %s\n' "$expected_sha" "$archive" | shasum -a 256 -c -; then
    echo "Checksum verification failed for $name" >&2
    exit 1
  fi
  mkdir "$source_dir"
  tar -xzf "$archive" -C "$source_dir" --strip-components=1
}

build_and_install() {
  local source_dir=$1
  local build_dir=$2
  shift 2
  # Static libraries may retain __FILE__ strings even in Release builds. Map
  # temporary source/build roots to stable relative paths so the final plugin
  # cannot disclose a developer or CI filesystem layout.
  local c_path_maps="-ffile-prefix-map=$source_dir=. -ffile-prefix-map=$build_dir=."
  cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_C_FLAGS_RELEASE="-O3 -DNDEBUG $c_path_maps" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG $c_path_maps" \
    "$@"
  cmake --build "$build_dir" --target install --parallel "$build_jobs"
}

fetch_source fluidsynth \
  https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v2.5.5.tar.gz \
  0827eefc06f66157c332d7bd0d65ee81be5d4c795f214db7ba0e1c70ee394430
fetch_source gcem \
  https://github.com/kthohr/gcem/archive/012ae73c6d0a2cb09ffe86475f5c6fba3926e200.tar.gz \
  34ab0ee87a9eb26d3087fa9b49c2572ea8ee03db0c9705b83648301a3a3fc172
fetch_source ogg \
  https://github.com/xiph/ogg/archive/refs/tags/v1.3.6.tar.gz \
  95b643da661155d79db9de2fca55daed3a8d491039829def246aacb3d9201c81
fetch_source vorbis \
  https://github.com/xiph/vorbis/archive/refs/tags/v1.3.7.tar.gz \
  270c76933d0934e42c5ee0a54a36280e2d87af1de3cc3e584806357e237afd13
fetch_source flac \
  https://github.com/xiph/flac/archive/refs/tags/1.5.0.tar.gz \
  aea54ed186ad07a34750399cb27fc216a2b62d0ffcd6dc2e3064a3518c3146f8
fetch_source opus \
  https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz \
  6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1
fetch_source sndfile \
  https://github.com/libsndfile/libsndfile/archive/refs/tags/1.2.2.tar.gz \
  ffe12ef8add3eaca876f04087734e6e8e029350082f3251f565fa9da55b52121

mkdir -p "$work_dir/fluidsynth/gcem"
cmake -E copy_directory "$work_dir/gcem" "$work_dir/fluidsynth/gcem"
mkdir -p "$install_prefix"

build_and_install "$work_dir/ogg" "$work_dir/build-ogg" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_FRAMEWORK=OFF \
  -DINSTALL_DOCS=OFF

build_and_install "$work_dir/vorbis" "$work_dir/build-vorbis" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_PREFIX_PATH="$install_prefix" \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_FRAMEWORK=OFF

build_and_install "$work_dir/flac" "$work_dir/build-flac" \
  -DCMAKE_PREFIX_PATH="$install_prefix" \
  -DCMAKE_DISABLE_FIND_PACKAGE_Intl=TRUE \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_CXXLIBS=OFF \
  -DBUILD_PROGRAMS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_TESTING=OFF \
  -DBUILD_DOCS=OFF \
  -DINSTALL_MANPAGES=OFF \
  -DWITH_OGG=ON

build_and_install "$work_dir/opus" "$work_dir/build-opus" \
  -DOPUS_BUILD_SHARED_LIBRARY=OFF \
  -DOPUS_BUILD_TESTING=OFF \
  -DOPUS_BUILD_PROGRAMS=OFF \
  -DOPUS_BUILD_FRAMEWORK=OFF

build_and_install "$work_dir/sndfile" "$work_dir/build-sndfile" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_PREFIX_PATH="$install_prefix" \
  -DCMAKE_DISABLE_FIND_PACKAGE_mp3lame=TRUE \
  -DCMAKE_DISABLE_FIND_PACKAGE_mpg123=TRUE \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_PROGRAMS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_TESTING=OFF \
  -DENABLE_CPACK=OFF \
  -DENABLE_EXTERNAL_LIBS=ON \
  -DENABLE_MPEG=OFF

build_and_install "$work_dir/fluidsynth" "$work_dir/build-fluidsynth" \
  -DCMAKE_PREFIX_PATH="$install_prefix" \
  -DDEFAULT_SOUNDFONT= \
  -DBUILD_SHARED_LIBS=OFF \
  -Dosal=cpp11 \
  -Denable-native-dls=ON \
  -Denable-libinstpatch=OFF \
  -Denable-libsndfile=ON \
  -Denable-aufile=OFF \
  -Denable-network=OFF \
  -Denable-readline=OFF \
  -Denable-dbus=OFF \
  -Denable-coreaudio=OFF \
  -Denable-coremidi=OFF \
  -Denable-framework=OFF \
  -Denable-portaudio=OFF \
  -Denable-openmp=OFF

# Keep FluidSynth's explicitly licensed SF3 regression input and its required
# notice in the local dependency prefix. Juicy16 strict validation loads it, but
# the plugin/package workflow never stages test fixtures from this directory.
sf3_fixture_dir="$install_prefix/share/juicy16-test-fixtures"
mkdir -p "$sf3_fixture_dir"
cp "$work_dir/fluidsynth/sf2/VintageDreamsWaves-v2.sf3" "$sf3_fixture_dir/"
cp "$work_dir/fluidsynth/sf2/COPYRIGHT.txt" "$sf3_fixture_dir/VintageDreamsWaves-COPYRIGHT.txt"

echo "Built macOS 11 arm64 dependencies at: $install_prefix"
echo "Use PKG_CONFIG_PATH=$install_prefix/lib/pkgconfig when configuring Juicy16."
echo "Use JUICYSF_SF3_FIXTURE=$sf3_fixture_dir/VintageDreamsWaves-v2.sf3 for strict validation."
