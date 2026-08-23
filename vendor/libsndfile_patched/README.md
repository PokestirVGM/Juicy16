# libsndfile 1.2.2 IRCAM hardening patch

`libsndfile-1.2.2-ircam-hardening.patch` is applied to the pinned libsndfile
1.2.2 source before it is configured. `tools/build_macos_dependencies.sh` applies
the diff; `tools/build_windows_dependencies.ps1` makes the same two substitutions
directly, because Windows has no guaranteed `patch.exe`. Both bracket the edit
with the pre- and post-edit `src/ircam.c` hashes below and fail the build on any
mismatch, so a release closure cannot be produced without the patch, and upstream
source drift is caught rather than silently patched around.

`dependency_patch_contract` (`tests/DependencyPatchTests.cmake`) keeps this file,
the patch, and both recipes agreeing on the same three hashes. It cannot rebuild
the closure — that needs network access — so it guards drift, not application.

## Why

libsndfile is linked statically into every Juicy16 artifact because FluidSynth
uses it to decode SF3 compressed samples. `docs/DEPENDENCIES.md` records the
review that found **CVE-2025-52194** in `ircam_read_header`, and that FluidSynth
routes a SoundFont's embedded sample bytes into `sf_open_virtual()` without
pre-validating the format — so a crafted `.sf3` reaches the IRCAM reader. A bank
file is the product's primary untrusted input.

1.2.2 is upstream's latest release and no newer one carries the fix, so the two
changes are backported from libsndfile `master`.

## What it changes

- `psf->sf.samplerate = psf_lrintf (samplerate)` instead of `(int) samplerate`.
  This is the exact line the CVE names (`src/ircam.c:164`) and the exact change
  upstream `master` made. Converting an out-of-range or NaN `float` with a C cast
  is undefined behavior; `psf_lrintf` is a defined conversion. `psf_lrintf` is
  already present in 1.2.2's `src/common.h`, so nothing else is needed.
- Reject a channel count below 1. 1.2.2 checks only `> SF_MAX_CHANNELS`, in both
  the little-endian read and the big-endian retry, so a zero or negative count
  from the file passes both. It then reaches
  `psf->blockwidth = psf->sf.channels * psf->bytewidth`, which is signed-overflow
  UB for a large negative value and a later divide-by-zero for zero. Extending
  the existing condition preserves the endian-detection retry — an invalid
  little-endian count still triggers the big-endian re-read — and reuses the
  existing `SFE_IRCAM_BAD_CHANNELS`.

Upstream `master` also widens the five `blockwidth` products with an
`(sf_count_t)` cast. That is **not** backported: `blockwidth` is `int` in 1.2.2
and `sf_count_t` only in `master`, so the cast would be truncated straight back
on assignment. With the channel count constrained to 1..`SF_MAX_CHANNELS` (1024)
and `bytewidth` at most 4, the product cannot overflow `int` anyway.

## Reproducing and verifying

The patch was generated against the pinned upstream tarball, whose checksum both
recipes verify before extracting. Base, patched, and diff hashes:

```text
52fab7073b1c7716902ee217769a48117577c1f33e84fb038232e2fe41088470  src/ircam.c (upstream 1.2.2)
27c25a5938d0c2571f9aaf0910ecedee57c440e62be66cf55f7708fa5ba3a1ab  src/ircam.c (patched)
9ab039a1261c8705f7238876d7ec634d375ddb78ac72a3676b9216e10f88995b  libsndfile-1.2.2-ircam-hardening.patch
```

To reproduce, extract the pinned tarball and run `patch -p1` with this file from
its root; `src/ircam.c` must then match the patched hash.

Re-pinning libsndfile means re-checking whether upstream has released the fix. If
it has, drop this patch rather than rebasing it.
