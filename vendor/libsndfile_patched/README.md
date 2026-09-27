# libsndfile 1.2.2 security patch

FluidSynth uses libsndfile to decode SF3 samples, so a crafted `.sf3` bank can
reach libsndfile's IRCAM reader. That reader has a known bug, **CVE-2025-52194**.
No libsndfile release fixes it yet, so Juicy16 backports two fixes from upstream
`master`:

- the sample rate is converted with `psf_lrintf` instead of an unsafe `(int)`
  cast (the exact line the CVE names)
- a channel count below 1 is rejected, not just one above the maximum

The macOS script applies `libsndfile-1.2.2-ircam-hardening.patch`; the Windows
script makes the same two edits directly. Both check `src/ircam.c` before and
after, and fail if anything differs:

```text
52fab7073b1c7716902ee217769a48117577c1f33e84fb038232e2fe41088470  src/ircam.c (upstream 1.2.2)
27c25a5938d0c2571f9aaf0910ecedee57c440e62be66cf55f7708fa5ba3a1ab  src/ircam.c (patched)
9ab039a1261c8705f7238876d7ec634d375ddb78ac72a3676b9216e10f88995b  libsndfile-1.2.2-ircam-hardening.patch
```

The `dependency_patch_contract` test keeps this file, the patch and both scripts
in agreement. When libsndfile ships a release with the fix, drop this patch.
