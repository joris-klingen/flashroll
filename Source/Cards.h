#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "NoteSpelling.h"

namespace flashroll
{

// Flash cards: one note, an interval, or a chord stacked on one staff, plus the
// interval / chord qualities they're built from. Pure C++ (no JUCE), like
// NoteSpelling — staff placement still comes from NoteSpelling only; this file
// decides WHICH spellings make up a card.

enum class CardType    { Single = 0, Intervals = 1, Triads = 2, Sevenths = 3 };
enum class Accidentals { Naturals = 0, Sharps = 1, Flats = 2, Mixed = 3 };

inline constexpr int kMaxChordNotes = 4;

// A chord / interval quality: letter steps above the root and the semitones
// each note must land on. The spelling follows from the steps (so a minor
// third is always a THIRD — C–E♭, never C–D♯).
struct Quality
{
    const char* symbol;      // shown after the root letter ("m7", "°") or alone for intervals ("M3")
    const char* longName;    // "minor seventh", "major third" …
    int size;
    std::array<int, kMaxChordNotes> steps;
    std::array<int, kMaxChordNotes> semitones;
};

const std::vector<Quality>& qualitiesFor (CardType);

struct Card
{
    Clef clef = Clef::Treble;
    int  size = 1;                                     // 1 = single note
    std::array<Spelling, kMaxChordNotes> notes {};     // bottom → top
    std::array<int, kMaxChordNotes>      midis {};     // ascending
    const Quality* quality = nullptr;                  // nullptr for a single note

    bool isChord() const noexcept           { return size > 1; }
    const Spelling& bottom() const noexcept { return notes[0]; }
    int  bottomMidi() const noexcept        { return midis[0]; }
    bool contains (int midi) const noexcept;
    bool containsPitchClass (int midi) const noexcept;
    bool leansFlat() const noexcept;                  // any flat → spell stray keys as flats
    bool sameAs (const Card&) const noexcept;         // same clef + same spellings

    static Card single (Clef, const Spelling&);
};

namespace cards
{
    // Is this accidental allowed under the drill's Accidentals setting?
    bool allows (Accidentals, int accidental) noexcept;

    // Build `quality` on `root` (in `clef`). Returns false when the spelling
    // would need a double accidental, an awkward one (E♯, C♭ …), an
    // accidental `acc` doesn't allow, or a key outside MIDI.
    bool build (const Quality&, const Spelling& root, Clef, Accidentals acc, Card& out);

    // Every card of `type` on `clef` whose notes all sit within the drill
    // range for `ledgers`.
    void collect (CardType, Clef, int ledgers, Accidentals, std::vector<Card>& out);

    // Display text: "F♯4" (single), "M3 · C4 E4" (interval), "C♯m7 · C♯ E G♯ B" (chord).
    std::string label (const Card&, int octaveOffset = 0, bool unicode = true);
}

}  // namespace flashroll
