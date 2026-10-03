# FlashRoll

A sight-reading trainer, built as a VST3 + Standalone (JUCE 8, macOS first;
it also builds on Linux). It flashes a note on a treble, bass or grand staff,
and the player answers by playing the matching MIDI key. It follows the
hitdmx family conventions (see `../hitnotedmx`).

- Architecture, per-layer state, and the development log: [STATUS.md](STATUS.md)
- Open backlog: [TODO.md](TODO.md)

## Finding things

- **Where a note sits on the staff and what it's called**:
  `Source/NoteSpelling.{h,cpp}`. It is the single source of truth for staff
  steps, ledger lines, drill ranges and names. Never redo that maths
  elsewhere.
- **What a card is (single / interval / chord), chord qualities and
  spelling, labels**: `Source/Cards.{h,cpp}`.
- **What gets dealt, how answers score, timing, and stats**:
  `Source/Drill.{h,cpp}`.
- **How the staff is drawn**: `Source/StaffRenderer.{h,cpp}`. The editor and
  the `staff-render` tool share it.

## Invariants

- `NoteSpelling`, `Cards` and `Drill` stay **plain C++ with no JUCE and no clock of
  their own**. Every call takes `nowMs`, which keeps `drill-check`
  deterministic.
- **The audio thread never touches the `Drill`.** `processBlock` only pushes
  host note-ons and note-offs (velocity 0) into `NoteInbox` (lock-free SPSC) and renders `ToneEngine`.
  It must not allocate or lock. The Drill runs on the message thread, either
  from the processor's timer or from editor callbacks.
- On-screen key clicks go **directly** to `submitKey` (message thread), not
  through the audio thread. That way they work with no audio device. The
  host-MIDI loop reads note-ons *before* `processNextMidiBuffer` merges the
  clicks in, so a click is never counted twice. Clicks are never released,
  so for chords they stay latched until the card changes.
- Only the two level knobs are automatable. Settings and stats are persisted
  as state-tree **properties**. `getStateInformation` reads a stats snapshot
  under a lock, never the Drill, because hosts can call it off the message
  thread.
- Always build both plugin targets (plain `cmake --build build` does) so the
  VST3 and the Standalone never diverge.

## Build & verify

```sh
cmake -S . -B build
cmake --build build -j8
ctest --test-dir build          # drill-logic + staff-font
./build/staff-render_artefacts/Release/staff-render previews/staff-sheet.png   # eyeball engraving
```

## Releasing

`installer/install.command` follows the hitnotedmx installer. Its Dropbox
fallback folder (`…/9_tech/flashroll_installer`) is an **assumed** location:
adjust it before the first release. Refresh that folder only on request.
