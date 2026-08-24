# Dependency inventory and security review

Everything statically linked into or embedded in a Juicy16 macOS release artifact, with the version actually built. Licensing obligations are in [LICENSING.md](LICENSING.md) and [NOTICE.md](../NOTICE.md); this document tracks **what is present and whether it is current**.

The pinned versions and their checksums live in `tools/build_macos_dependencies.sh`, which is the authority. Anything below that disagrees with that script is stale.

## macOS arm64 release closure

| Component | Version built | Source | Role |
| --- | --- | --- | --- |
| JUCE | 8.0.14 (exact) | juce-framework/JUCE | Framework, plugin wrappers, embedded HarfBuzz/SheenBidi/zlib/libpng/IJG JPEG |
| FluidSynth | 2.5.5 (exact) | FluidSynth/fluidsynth | Synthesis engine, SF2/SF3/DLS loading |
| GCEM | commit `012ae73c` | kthohr/gcem | Header-only constexpr math required by FluidSynth |
| libsndfile | 1.2.2 + IRCAM hardening patch | libsndfile/libsndfile | SF3 sample decoding; see `vendor/libsndfile_patched/` |
| FLAC | 1.5.0 | xiph/flac | libsndfile codec |
| libogg | 1.3.6 | xiph/ogg | Container for Vorbis/Opus |
| libvorbis | 1.3.7 | xiph/vorbis | libsndfile codec |
| Opus | 1.6.1 | downloads.xiph.org | libsndfile codec |

The Audio Unit and VST3 SDK interface sources ship with JUCE and are compiled as headers; no separate SDK binary is linked.

Windows has no validated closure yet. It is deliberately absent from this table rather than assumed to match macOS — see Phase 4.3 of [../ROADMAP.md](../ROADMAP.md).

## Security and currency review

Reviewed 2026-08-24 against a built and installed release artifact, not against
the recipe that produces it. Every static version in the artifact matches the
table above, and the installed AU's dynamic dependencies are **system frameworks
only** — no Homebrew path and no bundled dylib.

All pinned versions are current upstream releases at time of review, with one
backport:

- **CVE-2025-52194, libsndfile 1.2.2.** A buffer overflow in `ircam_read_header`,
  reachable because FluidSynth passes a SoundFont's embedded sample bytes to
  libsndfile's format detection and only *warns* when the result is not OGG — so
  a crafted `.sf3`, the plugin's primary untrusted input, could reach the
  vulnerable reader. Upstream has no release carrying the fix, so
  `vendor/libsndfile_patched/` backports it and both dependency recipes bracket
  the edit with pre- and post-edit `src/ircam.c` hashes, failing the build on any
  mismatch. The patched closure is built and linked, not merely specified. The
  related MPEG advisories are unreachable because `ENABLE_MPEG=OFF` keeps that
  code out of the binary.

**Finding: `WebKit.framework` is linked but unused.** The build sets
`JUCE_WEB_BROWSER=0` and no code path reaches it, but `juce_gui_extra` declares
the framework at module level so it is linked regardless. Low severity — a system
framework, not dlopen'd — but avoidable attack surface in an audio plugin. Worth
removing before 1.0 by dropping `juce_gui_extra` if nothing else needs it.

The Windows closure is intended but not yet built, so it is not reviewed here.

## Attack surface notes

The parsing surface reachable from a user-selected file is FluidSynth's SF2/SF3/DLS loaders and, for SF3, libsndfile with its FLAC/Ogg/Vorbis/Opus codecs. That is the highest-risk area in the closure, since the input is arbitrary and user-supplied.

Juicy16 bounds its own handling ahead of those parsers: the DLS repair path never reads a file larger than 512 MB into memory, and a RIFF container declaring more data than the file holds is rejected before FluidSynth sees it. See [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

No runtime networking exists. JUCE's cURL and web-browser support are compiled out, so nothing in the closure opens a socket.
