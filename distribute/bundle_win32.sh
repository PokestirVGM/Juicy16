#!/usr/bin/env bash

# Historical Docker artifact bundler. This is deterministic, VST3-only staging,
# but the resulting Windows build remains unsupported until building.win32.md's
# clean-machine and host/font matrix is complete.
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd -- "$SCRIPT_DIR/.." && pwd)
VERSION=${1:-}

if [[ ! $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.-]+)?$ ]]; then
  echo "usage: $0 <semver, for example 0.5.1-alpha.1>" >&2
  exit 2
fi

PROJECT_VERSION=$(sed -nE 's/^project\(JUICY16 VERSION ([0-9]+\.[0-9]+\.[0-9]+)\)$/\1/p' "$REPO_DIR/CMakeLists.txt")
# See the note in bundle_macos.sh: an unreadable label must fail, not default
# to an empty string that renames the candidate after the bare version.
if [[ $(grep -c '^set(JUICYSF_PRERELEASE_LABEL_DEFAULT ' "$REPO_DIR/CMakeLists.txt") -ne 1 ]]; then
  echo "Cannot read JUICYSF_PRERELEASE_LABEL_DEFAULT from CMakeLists.txt" >&2
  exit 2
fi
PRERELEASE=$(sed -nE 's/^set\(JUICYSF_PRERELEASE_LABEL_DEFAULT "([^"]*)"\)$/\1/p' "$REPO_DIR/CMakeLists.txt")
CANONICAL_VERSION=$PROJECT_VERSION
if [[ -n $PRERELEASE ]]; then
  CANONICAL_VERSION="$CANONICAL_VERSION-$PRERELEASE"
fi
if [[ $VERSION != "$CANONICAL_VERSION" ]]; then
  echo "version '$VERSION' does not match canonical '$CANONICAL_VERSION'" >&2
  exit 2
fi

OUTPUT_DIR="$SCRIPT_DIR/out"
STAGING_DIR="$OUTPUT_DIR/Juicy16-$VERSION-windows-x64"
ARCHIVE="$OUTPUT_DIR/Juicy16-$VERSION-windows-x64.zip"
rm -rf -- "$STAGING_DIR"
rm -f -- "$ARCHIVE"
mkdir -p "$STAGING_DIR/VST3" "$STAGING_DIR/docs"

CONTAINER_ID=$(docker create llvm-mingw)
trap 'docker rm "$CONTAINER_ID" >/dev/null' EXIT
docker cp \
  "$CONTAINER_ID:/x64/Release/VST3/Juicy16.vst3" \
  "$STAGING_DIR/VST3/"

test -f "$STAGING_DIR/VST3/Juicy16.vst3/Contents/x86_64-win/Juicy16.vst3"
cp -R "$REPO_DIR/licenses_of_dependencies" "$STAGING_DIR/"
cp "$REPO_DIR/LICENSE.txt" "$REPO_DIR/NOTICE.md" "$REPO_DIR/README.md" \
   "$REPO_DIR/CHANGELOG.md" "$REPO_DIR/PRIVACY.txt" \
   "$REPO_DIR/building.win32.md" "$STAGING_DIR/"
for document in BETA_TESTER_GUIDE.md COMPATIBILITY.md KNOWN_ISSUES.md \
                TROUBLESHOOTING.md LICENSING.md COMPATIBILITY.md ../README.md \
                TROUBLESHOOTING.md; do
  cp "$REPO_DIR/docs/$document" "$STAGING_DIR/docs/"
done

(
  cd "$OUTPUT_DIR"
  zip -9 -r "$(basename "$ARCHIVE")" "$(basename "$STAGING_DIR")"
)
# Relative, so `shasum -a 256 -c` works on a tester's machine and the sidecar
# cannot publish a developer path.
(cd "$(dirname "$ARCHIVE")" && shasum -a 256 "$(basename "$ARCHIVE")" \
  > "$(basename "$ARCHIVE").sha256")

echo "Created unsupported validation package: $ARCHIVE"
echo "Do not publish until the Windows Beta 1 gates pass."
