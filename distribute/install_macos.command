#!/bin/bash

# Juicy16 installer. Double-click this file in Finder.
#
# It exists because the manual procedure has a step people skip: macOS
# quarantines every downloaded file and silently refuses to load an ad-hoc
# signed bundle that still carries the tag. This does that step for you, and
# backs up anything it replaces.

set -uo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")" || exit 1
package_dir=$(pwd)

au_source="$package_dir/AU/Juicy16.component"
vst3_source="$package_dir/VST3/Juicy16.vst3"
app_source="$package_dir/Standalone/Juicy16.app"
au_target="$HOME/Library/Audio/Plug-Ins/Components"
vst3_target="$HOME/Library/Audio/Plug-Ins/VST3"
app_target="$HOME/Applications"
install_stage=
trap 'if [[ -n $install_stage ]]; then rm -rf -- "$install_stage"; fi' EXIT

bold=$(tput bold 2>/dev/null || true)
plain=$(tput sgr0 2>/dev/null || true)

say()  { printf '%s\n' "$*"; }
fail() { printf '\n%sInstall failed:%s %s\n\n' "$bold" "$plain" "$*"; printf 'Press Return to close.'; read -r _; exit 1; }

printf '\n%sJuicy16 installer%s\n\n' "$bold" "$plain"

[[ $(uname -s) == Darwin ]] || fail "This installer is for macOS."
if [[ $(uname -m) != arm64 ]]; then
  fail "Juicy16 is Apple Silicon only, but this Mac reports $(uname -m).
Check  Apple menu > About This Mac  for the chip."
fi
[[ -d $au_source || -d $vst3_source || -d $app_source ]] || fail "No Juicy16 bundles found next to this installer.
Unpack the whole .zip first, then double-click the installer inside it."

# 1. Integrity. SHA256SUMS covers every packaged file and ships inside the
#    archive, so this catches a truncated or tampered download before anything
#    is copied into a plug-in folder.
[[ -f $package_dir/SHA256SUMS ]] || fail "The package checksum manifest is missing.
Unpack the whole .zip first, or download it again."
if [[ -f $package_dir/SHA256SUMS ]]; then
  printf 'Checking the download... '
  if shasum -a 256 -c "$package_dir/SHA256SUMS" >/dev/null 2>&1; then
    say "intact."
  else
    fail "this copy does not match its checksums.
Download it again rather than installing this one."
  fi
fi

# 2. What to install. The app is installed into the current user's Applications
#    folder; no administrator permissions or automatic launch are needed.
say ""
say "Which formats?"
say "  ${bold}1${plain}) All formats: AU, VST3 and Standalone app (recommended)"
say "  ${bold}2${plain}) Plugins: AU and VST3"
say "  ${bold}3${plain}) Standalone app only"
say "  ${bold}4${plain}) AU only"
say "  ${bold}5${plain}) VST3 only"
printf 'Choice [1]: '
read -r choice
case "${choice:-1}" in
  1|"") want_au=1; want_vst3=1; want_app=1 ;;
  2)    want_au=1; want_vst3=1; want_app=0 ;;
  3)    want_au=0; want_vst3=0; want_app=1 ;;
  4)    want_au=1; want_vst3=0; want_app=0 ;;
  5)    want_au=0; want_vst3=1; want_app=0 ;;
  *)    fail "'$choice' is not one of the options." ;;
esac
verify_source() {
  local source=$1 name binary
  name=$(basename -- "$source")
  binary="$source/Contents/MacOS/Juicy16"
  [[ -d $source && -x $binary ]] || fail "$name is missing or incomplete.
Unpack the whole .zip first, or download it again."
  [[ $(lipo -archs "$binary" 2>/dev/null) == arm64 ]] || fail "$name is not an Apple Silicon-only bundle."
  codesign --verify --deep --strict "$source" >/dev/null 2>&1 || fail "$name failed signature verification. Do not install this copy."
}

# Validate every selected source before changing an installed format.
if (( want_au )); then verify_source "$au_source"; fi
if (( want_vst3 )); then verify_source "$vst3_source"; fi
if (( want_app )); then verify_source "$app_source"; fi

backup_root="$HOME/Library/Audio/Plug-Ins/.juicy16-backup-$(date +%Y%m%d-%H%M%S)"

install_bundle() {
  local source=$1 target_dir=$2 name had_previous=0
  name=$(basename -- "$source")
  mkdir -p "$target_dir" || fail "Could not create $target_dir"

  # Copy and verify first. A failed copy or signature check leaves the currently
  # installed bundle intact, including when replacing the Standalone app.
  install_stage=$(mktemp -d "$target_dir/.juicy16-install.XXXXXX") || fail "Could not prepare $name"
  ditto "$source" "$install_stage/$name" || fail "Could not prepare $name in $target_dir"
  xattr -dr com.apple.quarantine "$install_stage/$name" 2>/dev/null || true
  if ! codesign --verify --deep --strict "$install_stage/$name" >/dev/null 2>&1; then
    fail "$name failed signature verification after copying. Your existing copy was retained."
  fi

  # Anything already there is moved aside, never overwritten: a tester who needs
  # to go back to a previous build must be able to.
  if [[ -e $target_dir/$name ]]; then
    had_previous=1
    mkdir -p "$backup_root" || fail "Could not create the backup folder"
    ditto "$target_dir/$name" "$backup_root/$name" || fail "Could not back up the existing $name"
    rm -rf -- "${target_dir:?}/$name" || fail "Could not replace the existing $name"
    say "  backed up the previous $name"
  fi

  mv -- "$install_stage/$name" "$target_dir/$name" || fail "Could not place $name into $target_dir. The previous version is in $backup_root."
  rmdir -- "$install_stage"
  install_stage=
  if ! codesign --verify --deep --strict "$target_dir/$name" >/dev/null 2>&1; then
    rm -rf -- "${target_dir:?}/$name" || fail "$name failed verification and could not be removed. Do not use this copy."
    if (( had_previous )); then
      ditto "$backup_root/$name" "$target_dir/$name" || fail "$name failed verification. Restore the previous version from $backup_root."
      fail "$name failed verification in its final location. The previous version was restored."
    fi
    fail "$name failed verification in its final location and was removed."
  fi
  say "  installed $name"
}

say ""
say "Installing..."
if (( want_au )); then install_bundle "$au_source" "$au_target"; fi
if (( want_vst3 )); then install_bundle "$vst3_source" "$vst3_target"; fi
if (( want_app )); then install_bundle "$app_source" "$app_target"; fi

printf '\n%sDone.%s\n\n' "$bold" "$plain"
if (( want_au || want_vst3 )); then
  say "Quit your DAW completely, reopen it, and rescan plug-ins."
  say "Juicy16 appears as an instrument."
fi
if (( want_app )); then
  say "Open the Standalone player at: $app_target/Juicy16.app"
  say "For MIDI playback, see docs/STANDALONE.md."
fi
if [[ -d $backup_root ]]; then
  say ""
  say "Your previous version was saved to:"
  say "  $backup_root"
fi
say ""
say "For help, see docs/BETA_TESTER_GUIDE.md."
printf '\nPress Return to close.'
read -r _ || true
