#include "NoteSpelling.h"

#include <array>
#include <cstdlib>

namespace flashroll
{
namespace spelling
{

namespace
{
    constexpr std::array<int, 7>  kLetterSemitone { 0, 2, 4, 5, 7, 9, 11 };
    constexpr std::array<char, 7> kLetterChar     { 'C', 'D', 'E', 'F', 'G', 'A', 'B' };

    // Natural letter per pitch class, or -1 for a black key.
    constexpr std::array<int, 12> kNaturalLetter { 0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6 };

    // Floor division — octaves below 0 (MIDI < 12) must still round down.
    constexpr int floorDiv (int a, int b) noexcept { return a >= 0 ? a / b : -((-a + b - 1) / b); }
    constexpr int floorMod (int a, int b) noexcept { return a - floorDiv (a, b) * b; }
}

int bottomLineDiatonic (Clef c) noexcept
{
    return c == Clef::Treble ? 4 * 7 + 2    // E4
                             : 2 * 7 + 4;   // G2
}

int diatonicIndex (const Spelling& s) noexcept
{
    return s.octave * 7 + s.letter;
}

int midiFor (const Spelling& s) noexcept
{
    return (s.octave + 1) * 12 + kLetterSemitone[static_cast<std::size_t> (s.letter)] + s.accidental;
}

int staffStep (const Spelling& s, Clef c) noexcept
{
    return diatonicIndex (s) - bottomLineDiatonic (c);
}

Spelling naturalAtStep (int step, Clef c) noexcept
{
    const int d = bottomLineDiatonic (c) + step;
    return { floorMod (d, 7), 0, floorDiv (d, 7) };
}

Spelling fromMidi (int midi, bool preferFlat) noexcept
{
    const int pc  = floorMod (midi, 12);
    const int oct = floorDiv (midi, 12) - 1;

    if (const int nat = kNaturalLetter[static_cast<std::size_t> (pc)]; nat >= 0)
        return { nat, 0, oct };

    if (preferFlat)
        return { kNaturalLetter[static_cast<std::size_t> (pc + 1)], -1, oct };   // pc ≤ 10 for black keys
    return { kNaturalLetter[static_cast<std::size_t> (pc - 1)], +1, oct };
}

int ledgerLines (int step) noexcept
{
    if (step < 0)  return -((-step) / 2);        // -1 → 0 (space under the staff), -2 → 1 …
    if (step > 8)  return (step - 8) / 2;         //  9 → 0, 10 → 1 …
    return 0;
}

int lowestStep  (int ledgers) noexcept { return -2 * ledgers - 1; }
int highestStep (int ledgers) noexcept { return 8 + 2 * ledgers + 1; }

bool isAwkward (const Spelling& s) noexcept
{
    // E# / B# (sharp on a letter with no black key above) and Fb / Cb.
    if (s.accidental > 0) return s.letter == 2 || s.letter == 6;
    if (s.accidental < 0) return s.letter == 3 || s.letter == 0;
    return false;
}

std::string letterName (const Spelling& s, bool unicode)
{
    std::string out (1, kLetterChar[static_cast<std::size_t> (s.letter)]);
    if (s.accidental > 0) out += unicode ? "\xE2\x99\xAF" : "#";   // ♯
    if (s.accidental < 0) out += unicode ? "\xE2\x99\xAD" : "b";   // ♭
    return out;
}

std::string name (const Spelling& s, int octaveOffset, bool unicode)
{
    return letterName (s, unicode) + std::to_string (s.octave + octaveOffset);
}

std::string midiName (int midi, int octaveOffset)
{
    return name (fromMidi (midi), octaveOffset, false);
}

}  // namespace spelling
}  // namespace flashroll
