#!/bin/bash

# Juicy16 installer. Double-click this file in Finder.
#
# It exists because the manual procedure has a step people skip: macOS
# quarantines every downloaded file and silently refuses to load an ad-hoc
# signed plugin that still carries the tag, so the plugin never appears in the
# host and nothing says why. This does that step for you, and backs up anything
# it replaces.

set -uo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")" || exit 1
package_dir=$(pwd)

au_source="$package_dir/AU/Juicy16.component"
vst3_source="$package_dir/VST3/Juicy16.vst3"
au_target="$HOME/Library/Audio/Plug-Ins/Components"
vst3_target="$HOME/Library/Audio/Plug-Ins/VST3"

bold=$(tput bold 2>/dev/null || true)
plain=$(tput sgr0 2>/dev/null || true)

say()  { printf '%s\n' "$*"; }
fail() { printf '\n%sInstall failed:%s %s\n\n' "$bold" "$plain" "$*"; printf 'Press Return to close.'; read -r _; exit 1; }

printf '\n%sJuicy16 installer%s\n\n' "$bold" "$plain"

[[ $(uname -s) == Darwin ]] || fail "This installer is for macOS."
if [[ $(uname -m) != arm64 ]]; then
  fail "Juicy16 Beta 1 is Apple Silicon only, but this Mac reports $(uname -m).
Check  Apple menu > About This Mac  for the chip."
fi
[[ -d $au_source || -d $vst3_source ]] || fail "No Juicy16 bundles found next to this installer.
Unpack the whole .zip first, then double-click the installer inside it."

# 1. Integrity. SHA256SUMS covers every packaged file and ships inside the
#    archive, so this catches a truncated or tampered download before anything
#    is copied into a plug-in folder.
if [[ -f $package_dir/SHA256SUMS ]]; then
  printf 'Checking the download... '
  if shasum -a 256 -c "$package_dir/SHA256SUMS" >/dev/null 2>&1; then
    say "intact."
  else
    fail "this copy does not match its checksums.
Download it again rather than installing this one."
  fi
fi

# 2. What to install. Both is the common answer, so it is the default.
say ""
say "Which formats?  ${bold}1${plain}) Both (recommended)   ${bold}2${plain}) AU only   ${bold}3${plain}) VST3 only"
printf 'Choice [1]: '
read -r choice
case "${choice:-1}" in
  1|"") want_au=1; want_vst3=1 ;;
  2)    want_au=1; want_vst3=0 ;;
  3)    want_au=0; want_vst3=1 ;;
  *)    fail "'$choice' is not one of the options." ;;
esac
[[ -d $au_source   ]] || want_au=0
[[ -d $vst3_source ]] || want_vst3=0
(( want_au || want_vst3 )) || fail "Nothing selected to install."

backup_root="$HOME/Library/Audio/Plug-Ins/.juicy16-backup-$(date +%Y%m%d-%H%M%S)"

install_bundle() {
  local source=$1 target_dir=$2 name
  name=$(basename -- "$source")
  mkdir -p "$target_dir" || fail "Could not create $target_dir"

  # Anything already there is moved aside, never overwritten: a tester who needs
  # to go back to a previous build must be able to.
  if [[ -e $target_dir/$name ]]; then
    mkdir -p "$backup_root"
    ditto "$target_dir/$name" "$backup_root/$name" || fail "Could not back up the existing $name"
    rm -rf -- "${target_dir:?}/$name"
    say "  backed up the previous $name"
  fi

  ditto "$source" "$target_dir/$name" || fail "Could not copy $name into $target_dir"
  # The step this installer exists for.
  xattr -dr com.apple.quarantine "$target_dir/$name" 2>/dev/null || true
  if ! codesign --verify --strict "$target_dir/$name" >/dev/null 2>&1; then
    fail "$name failed signature verification after copying. Do not use this copy."
  fi
  say "  installed $name"
}

say ""
say "Installing..."
(( want_au ))   && install_bundle "$au_source"   "$au_target"
(( want_vst3 )) && install_bundle "$vst3_source" "$vst3_target"

printf '\n%sDone.%s\n\n' "$bold" "$plain"
say "Quit your DAW completely, reopen it, and rescan plug-ins."
say "Juicy16 appears as an instrument."
if [[ -d $backup_root ]]; then
  say ""
  say "Your previous version was saved to:"
  say "  $backup_root"
fi
say ""
say "If it still does not appear, see docs/BETA_TESTER_GUIDE.md."
printf '\nPress Return to close.'
read -r _
