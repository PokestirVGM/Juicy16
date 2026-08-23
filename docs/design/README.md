# Beta 1 interface design

The approved Beta 1 interface direction, as reviewed and chosen by the owner on
2026-08-23. Phase 9 of [MILESTONE_PLAN.md](../../MILESTONE_PLAN.md) implements
what is here; nothing in this directory ships in the plugin.

Live canvas: <https://claude.ai/code/artifact/c86db4a3-8c21-487d-93ec-d02f86605bc3>

## Files

| File | What it is |
| --- | --- |
| `Main.dc.html` | **The approved layout.** Mixer rack: per-channel volume and pan knobs, mute/solo, right-hand panel for master and reverb. |
| `Settings.dc.html` | The same window with the settings popover open. |
| `DirectionA.dc.html` | Not chosen — minimal-change rack row. Kept for reference. |
| `DirectionC.dc.html` | Not chosen — list plus detail panel. Its read-only per-channel reverb send is worth revisiting when per-channel sends land. |
| `DirectionD.dc.html` | Not chosen — console density with per-row activity meters. The activity meter is worth revisiting; it needs engine work. |
| `canvas.json` | Canvas layout: which artboard sits where, and the notes beside them. |

Each `.dc.html` is a self-contained HTML file. Open one directly in a browser to
read it, or edit and re-publish the whole set to the canvas above.

## What the approved layout specifies

- **Per-channel volume and pan on every row**, for all 16 channels, so nothing
  requires selecting a row first. This is the point of the redesign: today both
  controls edit only the selected channel.
- **Row anatomy**, left to right, with a 12px gap between every group: channel
  number, mute/solo, program number, instrument name with its dropdown, volume
  knob and value, pan knob and value.
- **Right-hand panel, 236px**: master trim, then the reverb section (enable,
  profile, and the four engine parameters from Phase 10), then the loaded bank.
- **Channels the file never touches recede** to 42% opacity. This is a proposal
  about behavior, not current behavior — it needs a definition of "untouched"
  before it can be implemented.
- **Keyboard**: unchanged from `MidiKeyboardComponent` — white keys tiled, black
  keys half height straddling the seam, octave labels C-2 to C7, and JUCE's
  translucent yellow key-down overlay.
- **Settings popover** behind the header gear: accent choice, engine facts worth
  quoting in a bug report, version, and the links the tester guide already points
  people at. It exists so later settings have somewhere to land instead of being
  bolted onto the header one at a time.
- **Palette**: neutral greys, no blue-green cast. JUCE's stock `LookAndFeel_V4`
  dark scheme (`#323e44` window, `#42a2c8` fill) is replaced entirely. The accent
  is a single hue used for knob arcs, the selected-row marker, the reverb enable,
  and held keys; sage `#8fa47a` is the default, with amber `#d8a24a`, terracotta
  `#c07a5e`, and neutral `#9d9d9d` offered in settings.

## Known gaps in this spec

- **The logo is the real asset**, supplied by the owner and committed at
  `resources/juicy16-logo.png` (1382x342). `docs/design/juicy16-logo.png` is the
  480px copy the canvas embeds. It still has to be bound into the build as a
  binary resource before the header can be implemented.
- Metrics here match the current source (24px rows became 26px, 28px header,
  32px channel column). Anything that changes them changes
  `GuiConstants::defaultHeight`, which is derived rather than typed in.
