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

Windows has no validated closure yet. It is deliberately absent from this table rather than assumed to match macOS — see Phase 4.3 of [../MILESTONE_PLAN.md](../MILESTONE_PLAN.md).

## Version currency review — 2026-08-19

Each upstream project's latest published release was compared against the pinned version:

| Component | Pinned | Upstream latest | Assessment |
| --- | --- | --- | --- |
| libogg | 1.3.6 | 1.3.6 | Current |
| libvorbis | 1.3.7 | 1.3.7 | Current |
| FLAC | 1.5.0 | 1.5.0 | Current |
| libsndfile | 1.2.2 | 1.2.2 | Current, and patched: 1.2.2 is upstream's newest release but does not contain the CVE-2025-52194 fix that `master` does. See the advisory review below. |
| Opus | 1.6.1 | 1.5.2 tagged on GitHub | Current; xiph publishes 1.6.1 on downloads.xiph.org ahead of the GitHub release listing |
| GCEM | commit `012ae73c` | 1.18.0 | Pinned commit; header-only constexpr math with no I/O or parsing surface |
| FluidSynth | 2.5.5 | 2.6.0 | **Deliberately behind.** Strict release validation requires 2.5.5 exactly for a reviewed ABI and feature set. Moving to 2.6.0 is a scoped upgrade, not a security patch. |
| JUCE | 8.0.14 | 9.0.1 | **Deliberately behind.** The vendored VST3 wrapper patch is byte-pinned to 8.0.14 and CMake refuses any drift. A JUCE 9 move requires rebasing and revalidating that patch in real hosts. |

Four of the six codec dependencies are exactly upstream's latest release; Opus is ahead of the GitHub tag. The two components that are behind are pinned by explicit project decision, each recorded above and enforced at configure time.

### What this review did and did not check

It verified **version currency against upstream releases**. It did **not** query a CVE database, and no such check can be inferred from this table. A dependency carrying an unpatched advisory in its latest release would look "current" here.

Before the candidate freeze, run an actual advisory lookup against every version in the first table and record the result underneath this section. That check is a Phase 8.7 release gate, not a one-time task, because the answer changes over time while the pinned versions do not.

## Windows closure — intended, not yet built

The Windows dependency policy is now written down as a recipe rather than left
to whatever a package manager supplies.
`tools/build_windows_dependencies.ps1` builds the **same components, versions,
and SHA-256 checksums** as the macOS recipe, so this inventory covers both
platforms as one list. FluidSynth is configured identically where it matters:
`osal=cpp11`, native C++17 DLS loader on, libinstpatch off.

Two things differ from macOS by necessity:

- **Static C runtime.** The closure and the plugin are both built with MSVC's
  `/MT` runtime, so the VST3 needs no Visual C++ redistributable on a tester's
  machine. `CMakeLists.txt` derives the plugin's setting from
  `FLUIDSYNTH_LINK_STATIC` under MSVC, because a mismatch is a link failure.
- **No `-ffile-prefix-map` equivalent.** MSVC embeds `__FILE__` paths and offers
  no supported way to remap them, so the recipe builds in a temporary directory
  and installs to a short, non-personal prefix. The macOS artifact's automated
  developer-path scan has no Windows counterpart yet.

Superseded: the CI job previously ran a bare `vcpkg install
fluidsynth:x64-windows`. That was never an approved dependency source, its
version was whatever vcpkg happened to carry, and it was not configured for the
native DLS loader — so the Windows plugin it produced may not have loaded DLS
banks at all. It has been replaced.

**Unproven.** The recipe has not been executed and no Windows artifact exists.
Every claim in this section is the intended state, pending the first green
`windows-vst3` CI run. The macOS closure above is measured; this one is not.

## Advisory review — 2026-08-20

This is the CVE lookup the version-currency table above explicitly did **not**
perform. It must be repeated at candidate freeze; a dependency can acquire an
advisory without its version changing.

### REMEDIATED: CVE-2025-52194 — libsndfile 1.2.2, reachable from a malicious SF3

Buffer overflow in `ircam_read_header` (`src/ircam.c:164`) when parsing malformed
IRCAM audio files. Affects libsndfile through 1.2.2, which is our pinned version.
Upstream issue: <https://github.com/libsndfile/libsndfile/issues/1082>, still
open and unlabelled. There is no libsndfile release above 1.2.2. Re-checked on
2026-08-20: the fix exists only on `master`, and Debian's current libsndfile
patch series carries nothing for this CVE either, so there is no distribution
backport to take.

**The vulnerable code ships.** `strings` on the built AU finds `ircam`, so the
handler is linked into the artifact. The MPEG-related libsndfile advisories are
*not* reachable: `mpeg_l3` does not appear in the binary, confirming
`ENABLE_MPEG=OFF` removed that code.

**Reachability, established by reading the call path rather than assumed.**
FluidSynth 2.5.5 `fluid_sffile_read_vorbis()` passes a SoundFont's *embedded
sample bytes* to `sf_open_virtual()` and relies on libsndfile's automatic format
detection. It does not pre-validate the format, and when the detected format is
not OGG it emits a warning and **continues**:

```c
sndfile = sf_open_virtual(&sfvio, SFM_READ, &sfinfo, &sfdata);   /* line 2202 */
...
if ((sfinfo.format & SF_FORMAT_OGG) == 0)
    FLUID_LOG(FLUID_WARN, "OGG sample is not OGG compressed, ...");  /* warning only */
```

So an `.sf3` whose compressed-sample region begins with the IRCAM magic number is
routed into the vulnerable reader. A bank file is Juicy16's primary untrusted
input, opened by the user on purpose.

**What is NOT established:** no proof-of-concept was built and no crash was
observed. This is reachability by code path, not a demonstrated exploit. The
distinction matters for severity and should not be lost when this is triaged.

Juicy16's own bounds work does not help here: the DLS repair cap and RIFF-overrun
rejection guard the DLS path, and this is inside FluidSynth's SF3 path.

**Resolution — 2026-08-20: option 1, patch the pinned source.** Waiting for a
release was ruled out: libsndfile `master` carries the fix but 1.2.2 is still the
newest release, and no libsndfile release contains it. Both recipes already build
from checksum-pinned source, so a patch step keeps that pinning intact.

`vendor/libsndfile_patched/libsndfile-1.2.2-ircam-hardening.patch` backports two
changes from libsndfile `master`:

- `psf->sf.samplerate = psf_lrintf (samplerate)` in place of `(int) samplerate`.
  This is the exact line the CVE names and the exact change `master` made.
- Rejecting a channel count below 1. 1.2.2 checks only `> SF_MAX_CHANNELS`, in
  both the little-endian read and the big-endian retry, so a zero or negative
  count from the file reaches `psf->sf.channels * psf->bytewidth` — signed
  overflow for a large negative value, a later divide by zero for zero.

`tools/build_macos_dependencies.sh` applies the diff;
`tools/build_windows_dependencies.ps1` makes the same two substitutions directly,
because Windows has no guaranteed `patch.exe`. Both bracket the edit with the pre-
and post-edit `src/ircam.c` hashes and fail the build on any mismatch, so a
closure cannot be produced without the patch and upstream source drift is caught
rather than patched around. The `dependency_patch_contract` CTest keeps the patch,
the two recipes, and `vendor/libsndfile_patched/README.md` from drifting apart; it
was confirmed to fail against a tampered patch.

**Built and linked — 2026-08-23.** The closure was rebuilt from a space-free copy
with network access restored. `tools/build_macos_dependencies.sh` fetched every
pinned tarball, verified each checksum, and reported the pre- and post-edit
`src/ircam.c` hashes as matching, so the patched reader is what FluidSynth linked
against. The strict portable Release gate then passed end to end on that closure:
15/15 CTests including `macos_artifact_portability`, `font_load_release_sf3` — the
SF3 path that reaches libsndfile — and `dependency_patch_contract`. The AU and
VST3 packaged from that build are `0681063528b4d8b89c1c903412235eed29fafaaf8845895e6e3cec0d0290ca5b`
and `6c88fb43d5c1cc91234eda5dee3ca966c86b87c96ca59871b0960ed229ca5c5b`.

**What is still not established.** There is still no proof-of-concept, before or after, so this closes a reachable
code path rather than disproving a demonstrated exploit. On arm64, out-of-range
float-to-int conversion saturates in practice, which makes the reporter's original
SIGILL an instrumented-build symptom; the channel-count gap is the half with
defined-behavior consequences on an ordinary build.

### Checked and not applicable

| Component | Result |
| --- | --- |
| libsndfile 1.2.2 — MPEG memory leak, MPEG reachable assertion | Not reachable; `ENABLE_MPEG=OFF`, no `mpeg_l3` symbols in the artifact |

## Attack surface notes

The parsing surface reachable from a user-selected file is FluidSynth's SF2/SF3/DLS loaders and, for SF3, libsndfile with its FLAC/Ogg/Vorbis/Opus codecs. That is the highest-risk area in the closure, since the input is arbitrary and user-supplied.

Juicy16 bounds its own handling ahead of those parsers: the DLS repair path never reads a file larger than 512 MB into memory, and a RIFF container declaring more data than the file holds is rejected before FluidSynth sees it. See [DLS_REPAIR.md](DLS_REPAIR.md).

No runtime networking exists. JUCE's cURL and web-browser support are compiled out, so nothing in the closure opens a socket.
