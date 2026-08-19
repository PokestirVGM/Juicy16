# Dependency inventory and security review

Everything statically linked into or embedded in a Juicy16 macOS release artifact, with the version actually built. Licensing obligations are in [LICENSING.md](LICENSING.md) and [NOTICE.md](../NOTICE.md); this document tracks **what is present and whether it is current**.

The pinned versions and their checksums live in `tools/build_macos_dependencies.sh`, which is the authority. Anything below that disagrees with that script is stale.

## macOS arm64 release closure

| Component | Version built | Source | Role |
| --- | --- | --- | --- |
| JUCE | 8.0.14 (exact) | juce-framework/JUCE | Framework, plugin wrappers, embedded HarfBuzz/SheenBidi/zlib/libpng/IJG JPEG |
| FluidSynth | 2.5.5 (exact) | FluidSynth/fluidsynth | Synthesis engine, SF2/SF3/DLS loading |
| GCEM | commit `012ae73c` | kthohr/gcem | Header-only constexpr math required by FluidSynth |
| libsndfile | 1.2.2 | libsndfile/libsndfile | SF3 sample decoding |
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
| libsndfile | 1.2.2 | 1.2.2 | Current |
| Opus | 1.6.1 | 1.5.2 tagged on GitHub | Current; xiph publishes 1.6.1 on downloads.xiph.org ahead of the GitHub release listing |
| GCEM | commit `012ae73c` | 1.18.0 | Pinned commit; header-only constexpr math with no I/O or parsing surface |
| FluidSynth | 2.5.5 | 2.6.0 | **Deliberately behind.** Strict release validation requires 2.5.5 exactly for a reviewed ABI and feature set. Moving to 2.6.0 is a scoped upgrade, not a security patch. |
| JUCE | 8.0.14 | 9.0.1 | **Deliberately behind.** The vendored VST3 wrapper patch is byte-pinned to 8.0.14 and CMake refuses any drift. A JUCE 9 move requires rebasing and revalidating that patch in real hosts. |

Four of the six codec dependencies are exactly upstream's latest release; Opus is ahead of the GitHub tag. The two components that are behind are pinned by explicit project decision, each recorded above and enforced at configure time.

### What this review did and did not check

It verified **version currency against upstream releases**. It did **not** query a CVE database, and no such check can be inferred from this table. A dependency carrying an unpatched advisory in its latest release would look "current" here.

Before the candidate freeze, run an actual advisory lookup against every version in the first table and record the result underneath this section. That check is a Phase 8.7 release gate, not a one-time task, because the answer changes over time while the pinned versions do not.

## Attack surface notes

The parsing surface reachable from a user-selected file is FluidSynth's SF2/SF3/DLS loaders and, for SF3, libsndfile with its FLAC/Ogg/Vorbis/Opus codecs. That is the highest-risk area in the closure, since the input is arbitrary and user-supplied.

Juicy16 bounds its own handling ahead of those parsers: the DLS repair path never reads a file larger than 512 MB into memory, and a RIFF container declaring more data than the file holds is rejected before FluidSynth sees it. See [DLS_REPAIR.md](DLS_REPAIR.md).

No runtime networking exists. JUCE's cURL and web-browser support are compiled out, so nothing in the closure opens a socket.
