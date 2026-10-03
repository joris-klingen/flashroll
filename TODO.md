# FlashRoll — open tasks

The open backlog. Item numbers are stable: a removed item leaves a gap, and
the remaining items are not renumbered.

## Show-stoppers to check first

1. **macOS build + Live smoke test.** Development so far has been on Linux.
   Build with Xcode, load the VST3 on a MIDI track in Live 12, and check that
   controller notes answer cards and that state (settings + stats) survives
   saving and reopening a set.

## Drill features

2. **Key signatures.** Deal notes within a chosen key, drawing the key
   signature after the clef and leaving accidentals off notes in the key.
   `NoteSpelling` would need a key-aware spelling.
4. **Rounds.** Add a fixed-length set (20 / 50 cards) with an end-of-round
   summary (accuracy, average time, slowest notes) alongside the current
   endless mode.
5. **Note-name mode.** Show a letter name and ask for the key, as the inverse
   drill.
6. **Note-reading over time.** Keep a per-day history of accuracy and average
   time and show a small trend chart.

9. **Chords across the grand staff.** Today every interval and chord sits on
   one staff. Splitting a chord between the hands (for example a bass root
   with the upper tones in the treble) and adding inversions would make it
   closer to real piano reading.

## Polish

7. **Brace glyph** for the grand staff. It currently uses a plain bracket bar.
   SMuFL `brace` (U+E000) needs vertical stretching.
8. **Ableton note-name rack** (like hitnotedmx's *Init. names*). It probably
   isn't needed here, because the player reads the staff, not the piano roll.
