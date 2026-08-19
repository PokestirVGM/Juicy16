# DLS repair boundary

Juicy16 does not edit a selected DLS file. When a file has a `RIFF`/`DLS ` header, the plugin may create a private operating-system temporary copy, apply the narrowly defined size corrections below, and ask FluidSynth to validate that copy. The original path and bytes remain untouched.

## Corrections Juicy16 may make

The repair routine changes only 32-bit little-endian RIFF size fields:

1. If the outer RIFF size claims bytes beyond the physical end of the file, it is clamped to the available byte count.
2. While walking complete top-level chunks, if the next claimed chunk runs past the physical end and there is a preceding complete chunk, that preceding chunk may be grown to absorb the remaining bytes. This targets the observed Awave-style undersized-chunk/phantom-chunk pattern.

RIFF word alignment is respected. A well-formed DLS is not changed, a second repair pass is a no-op, and any non-DLS RIFF—including SF2—is byte-identical.

## What it does not repair

Juicy16 does not reconstruct or guess:

- missing/truncated sample or instrument data;
- invalid nested chunk layouts, pool tables, wave links, regions, or articulation data;
- arbitrary corrupt chunk identifiers or sizes;
- a first corrupt chunk when no preceding complete chunk provides a safe repair target;
- unsupported encodings or files that merely use a `.dls` extension;
- an empty bank with no playable preset.

A repaired copy is not considered successful merely because its size fields changed. FluidSynth must load it and enumerate at least one preset. If validation still fails, Juicy16 deletes the candidate copy, reports the failure, and preserves the previously active bank and saved successful path.

## Size boundary

Repair rewrites an in-memory image of the whole file, and the file is chosen by the user, so the size is bounded before any of it is read. Banks larger than 512 MB are never repaired; they are handed to FluidSynth unchanged, which streams rather than buffers them.

That leaves a gap the repair pass would otherwise have absorbed, so an unrepaired bank is also checked for a RIFF header whose declared payload extends past the end of the file. Such a container cannot be valid and is rejected immediately with a specific message. Without that check, FluidSynth's parser can spend over a minute scanning a large malformed image while the message thread waits — measured at roughly 72 seconds for an 805 MB file declaring a 4 GB payload. A well-formed bank passes this check at any size, so legitimate large SoundFonts are unaffected.

## Temporary-file lifecycle

The repair candidate uses JUCE's unique temporary-file facility in the operating-system temporary directory. A successful repaired copy remains private to the plugin instance while it is the active bank and is removed when replaced or when the model is destroyed. Failed candidates are removed immediately. These files are not included in packages, diagnostics, or telemetry.

## Test evidence and reporting

`font_repair_unit` covers empty/truncated buffers, outer and inner size inconsistencies, odd-byte padding, unsafe first-chunk refusal, non-DLS identity, idempotence, and 6,000 deterministic malformed/non-DLS property cases. The engine test also proves failed corrupt, unsupported, unreadable, and zero-instrument replacements do not replace audible or serialized state, and separately covers zero-byte, truncated, oversized, read-only, and concurrently removed banks. A private Awave-style DLS currently passes after temporary repair on arm64 macOS; that file is not redistributable evidence and Windows validation remains open.

If a DLS still fails, keep the original file, report whether Juicy16 said repair was attempted, and include only a file you have permission to share. Do not rewrite the source file in place.
