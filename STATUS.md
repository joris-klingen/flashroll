# FlashRoll — status & development log

A living reference for the current state, plus a changelog. Update it along
with non-trivial commits. The open backlog is in [TODO.md](TODO.md).

## What's built and working

| Layer | File(s) | State |
|-------|---------|-------|
| VST plumbing | `PluginProcessor.{h,cpp}`, `CMakeLists.txt` | **Instrument shape** like hitnotedmx (`IS_SYNTH=TRUE`, VST3 category `Instrument`): stereo output, no input, MIDI in. MIDI is consumed, not echoed. Two automatable params: **Monitor Level** and **Cue Level** (0..1, shown as %). |
| Note spelling | `NoteSpelling.{h,cpp}` | Scientific pitch (C4 = 60). Diatonic index, staff step per clef (treble bottom line E4, bass G2), natural at a step, MIDI → spelling (sharp or flat preference), ledger count, and drill range per ledger setting. A range of 0 ledgers still includes the space just outside the staff (D4–G5 on treble), and each extra ledger adds 2 steps on each side. E♯/B♯/F♭/C♭ count as "awkward" and are never dealt. |
| Drill engine | `Drill.{h,cpp}` | Phases: `Idle` → `Prompt` → `Solved` (260 ms green) / `Revealed` (1.4 s amber, only when retry is off). Only the first answer is scored. In retry mode, the first wrong key shows a ghost, the second reveals the answer, and a timeout reveals it too. In flash mode the note hides after `flashMs` and comes back once the card is missed. Dealing is weighted: `base × (0.3 + 3·missRate + 0.6·slowness)`, where missRate is Laplace-smoothed (an unseen note = 0.5) and slowness is the note's average time ÷ the session average − 1, clamped to 0..2. Accidentals get half weight in Mixed so each staff position comes up equally often. The same key is never dealt twice in a row. Stats are kept per (clef, key) and serialised as `c:m:attempts:correct:ms;…`. Corrupt records are dropped. |
| Threading | `PluginProcessor.cpp`, `NoteInbox.h` | The audio thread pushes host note-ons into a 256-slot `AbstractFifo` SPSC ring. The processor's 120 Hz message-thread timer drains it into the Drill, ticks timeouts and requests cues. On-screen clicks call `submitKey` directly. `setStateInformation` swaps the tree and raises `stateDirty`, which the timer applies. |
| Sound | `ToneEngine.{h,cpp}` | 16-voice monitor (sine plus 2nd and 3rd partials, velocity-scaled, 3.5 s decay held / 0.25 s release) and a 4-voice cue pool: **Correct** is a short bright tick two octaves up, **Wrong** a soft 98 Hz thud, and **Reveal** plays the target pitch for 1.2 s. Rendering is sample-accurate between MIDI events, with no allocations. Cue requests go through one atomic slot. |
| Staff | `StaffRenderer.{h,cpp}` | Bravura is embedded through `juce_add_binary_data`. Glyph outlines are calibrated from `noteheadBlack`, which is one staff space tall. Clefs are placed at their SMuFL origins (G line / F line). It draws the whole note, accidental and ledger lines. The red ghost note sits 3.4 spaces to the right, or becomes a label with an arrow if it's more than 2 ledgers past the range. The answer label is a pill, and there's a background flash for right/wrong. Vertical layout fits the active ledger range. The grand-staff gap grows with the ledgers so the two staves never collide. If the font fails to load, it falls back to plain glyphs, and the `staff-font` CTest fails. |
| Editor | `PluginEditor.{h,cpp}`, `TrainerKeyboard.{h,cpp}` | 1100×640. Left pane: 8 settings combos, 3 toggles, and the Monitor/Cues knobs (the hitnotedmx knob look). Centre: the title strip (flamingo pink), the staff, and a time-limit bar. Right pane: streak, accuracy, average time, best streak, the three weakest notes, and Start/Stop + Reset stats (with a confirm dialog). Bottom: a keyboard that auto-fits the pool's range (whole octaves, at least 3) with Target/Wrong/Correct tints and the heatmap strip. Repaints at 60 Hz. |
| Tests / tools | `tools/DrillCheck.cpp`, `tools/StaffRender.cpp` | `drill-logic`: staff landmarks, round trips over all 128 keys, pool ranges and accidental modes, the scoring flow (retry and no-retry), timeouts and flash, adaptive weighting, stats round trip and rejection of corrupt input, and coverage over 2000 deals. `staff-font`: renders the contact sheet and fails if Bravura doesn't load. |

## Verified

- Linux build of all targets: no warnings under JUCE's recommended warning
  flags, and `ctest` passes 2/2.
- The Standalone was run under Xvfb and driven with synthetic clicks. Start,
  a wrong key (red ghost + heat), a second wrong key (amber reveal + target
  key tint) and a correct key (green flash, streak and accuracy update, next
  card dealt) all behave as designed.
- **Not yet verified:** macOS / Xcode build, and loading in Live with a real
  controller.

## Changelog

- **v0.1.0**: first version. Drill engine, Bravura staff, editor, monitor and
  cue tones, persisted stats, and both CTests.
