# Local font and MIDI test corpus

The files currently under `testfiles/` are local validation inputs. They must not be copied into a public package or committed/distributed until their provenance and redistribution rights are documented.

Configure the optional corpus gate with:

```bash
cmake -S . -B build \
  -DJUICYSF_FONT_CORPUS="$(pwd)/testfiles" \
  <the remaining normal build options>
ctest --test-dir build -R font_load_configured_corpus --output-on-failure
```

## Current local inputs

| File | Type | SHA-256 | Current result | Provenance/redistribution |
| --- | --- | --- | --- | --- |
| `Abyss Bank Revision 3 (awave style).dls` | malformed/Awave-style DLS | `5030ae51db0fd2af497eec4f837059dd60e7c7c54a6b1b88aaa54aef180fd6cb` | 85 presets; loads after safe temporary repair | Unknown; local/private only |
| `SEQ_BGM_C_03.dls` | conventional DLS | `f6cea05b89549558d6cf4fd7d87c69ab0850d1c9bd0968233391dafeedaeac8b` | 7 presets; loads without repair | Unknown; local/private only |
| `SEQ_BGM_C_03.sf2` | SF2 | `86d583036e5eb05fa5e1f673ee1f5ec7ef6f1b460bcea6085ce78735bb4c00f7` | 7 presets; loads without repair | Unknown; local/private only |
| `SEQ_BGM_C_03.mid` | format-1 MIDI, 11 tracks | `d08d70b8228540961dfe8ea10ae2ee207e3c7dd729a16be6b8e671258cb5495f` | Fixture analysis not yet registered | Unknown; local/private only |

The opt-in `font_load_configured_corpus` CTest passed all three banks with FluidSynth 2.5.5 in Debug and static Release on arm64 macOS on 2026-08-19. This is useful implementation evidence but does not establish their redistribution rights, Windows compatibility, or final-candidate compatibility.

## Pinned upstream SF3 input

The checksum-pinned FluidSynth 2.5.5 source includes `sf2/VintageDreamsWaves-v2.sf3` for its own tests. The file is not copied into this repository or package. Its SHA-256 is `bbb921fa98a3705d304f05904f06952b75e1cfe1ada086590d36cbd6efec1a40`; the registered strict-Release `font_load_release_sf3` test loaded 136 presets on arm64 macOS on 2026-08-19. Strict configuration without a real `.sf3` fixture fails.

FluidSynth's adjacent `sf2/COPYRIGHT.txt` records Ian Wilson's permission to convert the bank to SF3 and permits redistribution subject to preserving that copyright notice. Any future copy retained or distributed by Juicy16 must remain unmodified and include the notice. Current evidence uses the file in place from the pinned dependency source only.

## Still required

- Run the required compressed-SF3 gate on Windows and every final release candidate.
- Provenance and license/redistribution status for every retained fixture.
- Sparse/non-zero banks, percussion banks, large banks, and unusual/Unicode preset names where the current files do not cover them.
- The same complete corpus run against macOS and Windows release artifacts.
