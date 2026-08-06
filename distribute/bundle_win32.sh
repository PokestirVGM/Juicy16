#!/usr/bin/env bash

# Historical Docker artifact bundler. This is deterministic, VST3-only staging,
# but the resulting Windows build remains unsupported until building.win32.md's
# clean-machine and host/font matrix is complete.
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
VERSION=${1:-}

if [[ ! $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]]; then
  echo "usage: $0 <semver, for example 0.5.0-beta.1>" >&2
  exit 2
fi

PROJECT_VERSION=$(sed -nE 's/^project\(JUICY_SF_RACK VERSION ([0-9]+\.[0-9]+\.[0-9]+)\)$/\1/p' "$REPO_DIR/CMakeLists.txt")
PRERELEASE=$(sed -nE 's/^set\(JUICYSF_PRERELEASE_LABEL "([^"]+)" CACHE STRING$/\1/p' "$REPO_DIR/CMakeLists.txt")
CANONICAL_VERSION=$PROJECT_VERSION
if [[ -n $PRERELEASE ]]; then
  CANONICAL_VERSION="$CANONICAL_VERSION-$PRERELEASE"
fi
if [[ $VERSION != "$CANONICAL_VERSION" ]]; then
  echo "version '$VERSION' does not match canonical '$CANONICAL_VERSION'" >&2
  exit 2
fi

OUTPUT_DIR="$SCRIPT_DIR/out"
STAGING_DIR="$OUTPUT_DIR/JuicySF-Rack-$VERSION-windows-x64"
ARCHIVE="$OUTPUT_DIR/JuicySF-Rack-$VERSION-windows-x64.zip"
rm -rf -- "$STAGING_DIR"
rm -f -- "$ARCHIVE"
mkdir -p "$STAGING_DIR/VST3"

CONTAINER_ID=$(docker create llvm-mingw)
trap 'docker rm "$CONTAINER_ID" >/dev/null' EXIT
docker cp \
  "$CONTAINER_ID:/x64/Release/VST3/JuicySF Rack.vst3" \
  "$STAGING_DIR/VST3/"

test -f "$STAGING_DIR/VST3/JuicySF Rack.vst3/Contents/x86_64-win/JuicySF Rack.vst3"
cp -R "$REPO_DIR/licenses_of_dependencies" "$STAGING_DIR/"
cp "$REPO_DIR/LICENSE.txt" "$REPO_DIR/README.md" "$REPO_DIR/CHANGELOG.md" \
   "$REPO_DIR/PRIVACY.txt" "$REPO_DIR/building.win32.md" "$STAGING_DIR/"

(
  cd "$OUTPUT_DIR"
  zip -9 -r "$(basename "$ARCHIVE")" "$(basename "$STAGING_DIR")"
)
shasum -a 256 "$ARCHIVE" > "$ARCHIVE.sha256"

echo "Created unsupported validation package: $ARCHIVE"
echo "Do not publish until the Windows Beta 1 gates pass."
