#!/usr/bin/env bash

# Build a deterministic, self-validating macOS Beta candidate archive from an
# already-built strict Release artifact directory. Dirty/ad-hoc inputs are
# unmistakably labelled as local validation packages and cannot be confused
# with a publishable candidate.
set -euo pipefail

if [[ $(uname -s) != Darwin || $(uname -m) != arm64 ]]; then
  echo "This packaging workflow currently supports only Apple Silicon macOS." >&2
  exit 2
fi

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd -- "$script_dir/.." && pwd)
artifacts_dir=${1:-}
candidate=${2:-}

if [[ -z $artifacts_dir || ! $candidate =~ ^BC[1-9][0-9]*$ ]]; then
  echo "usage: $0 <JuicySFPlugin_artefacts/Release> <BCnumber>" >&2
  exit 2
fi
artifacts_dir=$(cd -- "$artifacts_dir" && pwd)

project_version=$(sed -nE 's/^project\(JUICY16 VERSION ([0-9]+\.[0-9]+\.[0-9]+)\)$/\1/p' "$repo_dir/CMakeLists.txt")
# The label stopped being a CACHE entry in 0.5.1-alpha.4. A parse that silently
# yields nothing would name the package after the bare project version, so an
# absent or unreadable line is a hard failure rather than an empty label.
if [[ $(grep -c '^set(JUICYSF_PRERELEASE_LABEL_DEFAULT ' "$repo_dir/CMakeLists.txt") -ne 1 ]]; then
  echo "Cannot read JUICYSF_PRERELEASE_LABEL_DEFAULT from CMakeLists.txt" >&2
  exit 2
fi
prerelease=$(sed -nE 's/^set\(JUICYSF_PRERELEASE_LABEL_DEFAULT "([^"]*)"\)$/\1/p' "$repo_dir/CMakeLists.txt")
display_version=$project_version
if [[ -n $prerelease ]]; then
  display_version="$display_version-$prerelease"
fi

au_source="$artifacts_dir/AU/Juicy16.component"
vst3_source="$artifacts_dir/VST3/Juicy16.vst3"
test -f "$au_source/Contents/MacOS/Juicy16"
test -f "$vst3_source/Contents/MacOS/Juicy16"

cmake \
  -DPROJECT_SOURCE_DIR="$repo_dir" \
  -DARTIFACTS_DIR="$artifacts_dir" \
  -DPROJECT_VERSION="$project_version" \
  -DDISPLAY_VERSION="$display_version" \
  -P "$repo_dir/tests/MetadataTests.cmake"
cmake \
  -DARTIFACTS_DIR="$artifacts_dir" \
  -DEXPECTED_DEPLOYMENT_TARGET=11.0 \
  -DINCLUDE_STANDALONE=OFF \
  -P "$repo_dir/tests/MacArtifactTests.cmake"

commit=$(git -C "$repo_dir" rev-parse HEAD)
dirty=no
label_suffix=
if [[ -n $(git -C "$repo_dir" status --porcelain --untracked-files=normal) ]]; then
  dirty=yes
  if [[ ${JUICY16_ALLOW_DIRTY_PACKAGE:-0} != 1 ]]; then
    echo "Refusing to package a dirty worktree. Commit the candidate or set JUICY16_ALLOW_DIRTY_PACKAGE=1 for a clearly labelled local validation archive." >&2
    exit 2
  fi
  label_suffix="-LOCAL-DIRTY"
fi

signature=distribution
signature_details=$(/usr/bin/codesign -dv --verbose=4 "$au_source" 2>&1)
if grep -q '^Signature=adhoc$' <<< "$signature_details"; then
  signature=adhoc
  label_suffix="$label_suffix-ADHOC"
fi
if [[ ${JUICY16_REQUIRE_DISTRIBUTION_SIGNATURE:-0} == 1 && $signature != distribution ]]; then
  echo "A distribution signature was required, but the AU is ad-hoc signed." >&2
  exit 2
fi

package_name="Juicy16-$display_version-$candidate-macos-arm64$label_suffix"
output_dir="$script_dir/out"
staging_dir="$output_dir/$package_name"
archive="$output_dir/$package_name.zip"

case "$staging_dir" in
  "$script_dir"/out/Juicy16-*) ;;
  *) echo "Refusing unsafe staging path: $staging_dir" >&2; exit 2 ;;
esac

rm -rf -- "$staging_dir"
rm -f -- "$archive" "$archive.sha256"
mkdir -p "$staging_dir/AU" "$staging_dir/VST3" \
             "$staging_dir/docs" "$staging_dir/licenses_of_dependencies"

COPYFILE_DISABLE=1 cp -R "$au_source" "$staging_dir/AU/"
COPYFILE_DISABLE=1 cp -R "$vst3_source" "$staging_dir/VST3/"
cp "$repo_dir/LICENSE.txt" "$repo_dir/NOTICE.md" "$repo_dir/README.md" \
   "$repo_dir/CHANGELOG.md" "$repo_dir/PRIVACY.txt" "$repo_dir/ROADMAP.md" \
   "$repo_dir/building.macos.md" "$staging_dir/"
# The installer, which must stay executable so a tester can double-click it.
# SHA256SUMS is generated below and therefore covers it, and the installer
# checks SHA256SUMS before copying anything.
cp "$script_dir/install_macos.command" "$staging_dir/"
chmod +x "$staging_dir/install_macos.command"
# Every published doc, so that no link inside the package dangles. The list
# duplicated two entries and copied README.md a second time into docs/; it also
# copied PERFORMANCE.md, which is no longer tracked, so a clean clone could not
# package at all.
for document in ARCHITECTURE.md BETA_TESTER_GUIDE.md COMPATIBILITY.md \
                CONTROLLER_SUPPORT.md DEPENDENCIES.md KNOWN_ISSUES.md \
                LICENSING.md TROUBLESHOOTING.md; do
  cp "$repo_dir/docs/$document" "$staging_dir/docs/"
done
for notice in JUCE-framework_AGPL3.txt JUCE-AudioUnitSDK.txt JUCE-HarfBuzz.txt \
              JUCE-libpng.txt JUCE-SheenBidi.txt JUCE-VST3_SDK.txt JUCE-zlib.txt \
              gcem_Apache_2.0.txt libflac_New_BSD.txt libfluidsynth_LGPL_2.1.txt \
              libjpeg_IJG.txt libogg_New_BSD.txt libopus_BSD.txt \
              libsndfile_LGPL_2.1.txt libvorbis_New_BSD.txt \
              libvorbisenc_New_BSD.txt; do
  cp "$repo_dir/licenses_of_dependencies/$notice" \
     "$staging_dir/licenses_of_dependencies/"
done

au_hash=$(shasum -a 256 "$au_source/Contents/MacOS/Juicy16" | cut -d ' ' -f 1)
vst3_hash=$(shasum -a 256 "$vst3_source/Contents/MacOS/Juicy16" | cut -d ' ' -f 1)
{
  printf 'Product: Juicy16\n'
  printf 'Version: %s\n' "$display_version"
  printf 'Candidate: %s\n' "$candidate"
  printf 'Source commit: %s\n' "$commit"
  printf 'Source: https://github.com/PokestirVGM/Juicy16\n'
  printf 'Dirty worktree: %s\n' "$dirty"
  printf 'Signature: %s\n' "$signature"
  printf 'AU executable SHA-256: %s\n' "$au_hash"
  printf 'VST3 executable SHA-256: %s\n' "$vst3_hash"
} > "$staging_dir/BUILD_INFO.txt"

(
  cd "$staging_dir"
  find . -type f ! -name SHA256SUMS -print | LC_ALL=C sort | \
    while IFS= read -r file; do shasum -a 256 "$file"; done > SHA256SUMS
)

# Every Markdown link inside the package must resolve within the package. A doc
# that links to a file left out of the archive is a dead end for the tester who
# is told to read it.
cmake -DSOURCE_ROOT="$staging_dir" -P "$repo_dir/tests/DocumentationLinkTests.cmake"

if grep -IRnE --exclude=SHA256SUMS \
     '/Users/[A-Za-z0-9._-]+/|/private/tmp|BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY' \
     "$staging_dir"; then
  echo "Package text contains a prohibited path or credential marker." >&2
  exit 1
fi
if find "$staging_dir" -iname '*standalone*' -o -iname '*vst2*' -o -iname 'testfiles' | grep -q .; then
  echo "Package contains an unsupported format or private test corpus." >&2
  exit 1
fi

# ZIP stores timestamps with two-second granularity. A fixed timestamp plus a
# sorted input list makes repeated packaging of identical inputs byte-stable.
find "$staging_dir" -exec touch -h -t 202601010000 {} +
(
  cd "$output_dir"
  find "$package_name" -print | LC_ALL=C sort | \
    zip -X -9 -y "$archive" -@ >/dev/null
)
# Relative to the archive, so `shasum -a 256 -c <file>.sha256` works wherever a
# tester downloads it — and so the sidecar cannot publish a developer path.
(cd "$output_dir" && shasum -a 256 "$package_name.zip" > "$package_name.zip.sha256")
if grep -qE '/Users/|/private/tmp|/home/' "$archive.sha256"; then
  echo "Checksum sidecar contains an absolute path: $archive.sha256" >&2
  exit 1
fi

# Extraction safety: every entry must land inside one top-level directory. An
# absolute path or a parent traversal would let extraction write outside the
# chosen destination.
unsafe_entries=$(unzip -Z1 "$archive" | grep -E '^/|(^|/)\.\./' || true)
if [[ -n $unsafe_entries ]]; then
  echo "Archive contains unsafe extraction paths:" >&2
  echo "$unsafe_entries" >&2
  exit 1
fi
top_level_count=$(unzip -Z1 "$archive" | cut -d / -f 1 | sort -u | wc -l | tr -d ' ')
if [[ $top_level_count != 1 ]]; then
  echo "Archive must contain exactly one top-level directory; found $top_level_count." >&2
  exit 1
fi

verification_dir=$(mktemp -d /private/tmp/juicy16-package-verify.XXXXXX)
trap 'rm -rf -- "$verification_dir"' EXIT
unzip -q "$archive" -d "$verification_dir"
verified_root="$verification_dir/$package_name"
(
  cd "$verified_root"
  shasum -a 256 -c SHA256SUMS >/dev/null
)

# A bundle whose binary lost its executable bit in transit is not loadable, and
# the failure would only show up in a host.
for extracted in "$verified_root/AU/Juicy16.component/Contents/MacOS/Juicy16" \
                 "$verified_root/VST3/Juicy16.vst3/Contents/MacOS/Juicy16"; do
  if [[ ! -x $extracted ]]; then
    echo "Extracted binary is not executable: $extracted" >&2
    exit 1
  fi
done

# The earlier text scan uses grep -I, which skips binaries entirely. Compiled
# code is where a build path is most likely to survive, so scan it explicitly.
# JUCE's shared-folder special-location literal is not a developer identity; it
# appears in the string table as /Users/Share with the final byte held elsewhere.
for extracted in "$verified_root/AU/Juicy16.component/Contents/MacOS/Juicy16" \
                 "$verified_root/VST3/Juicy16.vst3/Contents/MacOS/Juicy16"; do
  leaked=$(strings -a "$extracted" \
    | grep -aoE '/Users/[A-Za-z0-9._-]+|/private/tmp/[A-Za-z0-9._-]+|/opt/homebrew|/usr/local/(opt|Cellar)' \
    | grep -vE '^/Users/Shared?$' | sort -u || true)
  if [[ -n $leaked ]]; then
    echo "Binary embeds a developer or build path: $extracted" >&2
    echo "$leaked" >&2
    exit 1
  fi
done
cmake \
  -DPROJECT_SOURCE_DIR="$repo_dir" \
  -DARTIFACTS_DIR="$verified_root" \
  -DPROJECT_VERSION="$project_version" \
  -DDISPLAY_VERSION="$display_version" \
  -P "$repo_dir/tests/MetadataTests.cmake"
cmake \
  -DARTIFACTS_DIR="$verified_root" \
  -DEXPECTED_DEPLOYMENT_TARGET=11.0 \
  -DINCLUDE_STANDALONE=OFF \
  -P "$repo_dir/tests/MacArtifactTests.cmake"

echo "Created and revalidated: $archive"
echo "Archive checksum: $archive.sha256"
# Ad-hoc signing is the APPROVED Beta 1 signature, so it must not be reported the
# same way as an untraceable dirty build: saying "do not ship this" about the
# thing being shipped is how a real disqualifier gets ignored.
if [[ $dirty == yes ]]; then
  echo "LOCAL VALIDATION ONLY: built from a dirty worktree and traceable to no commit. Do not distribute."
elif [[ $signature != distribution ]]; then
  echo "Ad-hoc signed, which is expected for Beta 1. Testers must clear quarantine; see docs/BETA_TESTER_GUIDE.md."
fi
