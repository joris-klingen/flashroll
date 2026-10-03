#pragma once

#include <string>

namespace flashroll
{

// Note spelling + staff geometry — the single source of truth for "where on
// the staff does this note sit" and "what is it called". Pure C++ (no JUCE)
// so the drill-check CTest can exercise it directly.
//
// Vocabulary:
//   * A *spelling* is letter + accidental + octave (scientific pitch: middle C
//     = C4 = MIDI 60). It is what's DRAWN. Several spellings share one MIDI
//     key (C#4 / Db4) — the player answers with the KEY, so enharmonics are
//     both correct by construction.
//   * A *diatonic index* counts letter steps: octave * 7 + letter (C = 0 …
//     B = 6). Middle C = 28.
//   * A *staff step* is a diatonic index relative to the clef's BOTTOM line:
//     lines sit on even steps 0, 2, 4, 6, 8; spaces on the odd steps between;
//     negative steps / steps > 8 need ledger lines.

enum class Clef { Treble = 0, Bass = 1 };

inline constexpr int kNumClefs = 2;

struct Spelling
{
    int letter     = 0;   // 0..6 = C D E F G A B
    int accidental = 0;   // -1 flat, 0 natural, +1 sharp
    int octave     = 4;   // scientific (C4 = middle C)

    bool operator== (const Spelling& o) const noexcept
    {
        return letter == o.letter && accidental == o.accidental && octave == o.octave;
    }
    bool operator!= (const Spelling& o) const noexcept { return ! (*this == o); }
};

namespace spelling
{
    // Diatonic index of the clef's bottom staff line: E4 (treble), G2 (bass).
    int bottomLineDiatonic (Clef) noexcept;

    int diatonicIndex (const Spelling&) noexcept;
    int midiFor (const Spelling&) noexcept;

    // Staff step of a spelling on the given clef (0 = bottom line, 8 = top).
    int staffStep (const Spelling&, Clef) noexcept;

    // The NATURAL spelling sitting on `step` of `clef`.
    Spelling naturalAtStep (int step, Clef) noexcept;

    // Spell a MIDI key with naturals where possible, else sharp (preferFlat =
    // false) or flat (preferFlat = true).
    Spelling fromMidi (int midi, bool preferFlat = false) noexcept;

    // Ledger lines needed to draw `step`: negative = below the staff, positive
    // = above, 0 = on the staff. (Middle C on treble = step -2 → -1 ledger.)
    int ledgerLines (int step) noexcept;

    // Staff-step range a drill draws from, given how many ledger lines the
    // player wants to practise past the staff. 0 ledgers still includes the
    // space just outside the staff (D4 / G5 on treble) — that's standard
    // "on the staff" reading; each extra ledger adds two steps either side.
    int lowestStep (int ledgers) noexcept;
    int highestStep (int ledgers) noexcept;

    // An accidental that would land on a white key (E#, B#, Fb, Cb) — legal
    // notation, but confusing for sight-reading drills, so never generated.
    bool isAwkward (const Spelling&) noexcept;

    // Display names. `octaveOffset` lets the UI show Ableton's convention
    // (middle C = C3) as -1. Uses ♯ / ♭ when `unicode` is set, else # / b.
    std::string name (const Spelling&, int octaveOffset = 0, bool unicode = true);
    std::string letterName (const Spelling&, bool unicode = true);   // no octave
    std::string midiName (int midi, int octaveOffset = 0);           // e.g. "C#4"
}

}  // namespace flashroll
