#include "Cards.h"

#include <algorithm>

namespace flashroll
{

namespace
{
    // Steps / semitones above the root. Interval symbols use the usual
    // quality letters (m / M / P); chord symbols are what follows the root
    // letter in a lead sheet ("" = major triad).
    const std::vector<Quality> kIntervals {
        { "m2", "minor second",   2, { 0, 1 }, { 0, 1 } },
        { "M2", "major second",   2, { 0, 1 }, { 0, 2 } },
        { "m3", "minor third",    2, { 0, 2 }, { 0, 3 } },
        { "M3", "major third",    2, { 0, 2 }, { 0, 4 } },
        { "P4", "perfect fourth", 2, { 0, 3 }, { 0, 5 } },
        { "P5", "perfect fifth",  2, { 0, 4 }, { 0, 7 } },
        { "m6", "minor sixth",    2, { 0, 5 }, { 0, 8 } },
        { "M6", "major sixth",    2, { 0, 5 }, { 0, 9 } },
        { "m7", "minor seventh",  2, { 0, 6 }, { 0, 10 } },
        { "M7", "major seventh",  2, { 0, 6 }, { 0, 11 } },
        { "P8", "octave",         2, { 0, 7 }, { 0, 12 } },
    };

    const std::vector<Quality> kTriads {
        { "",            "major",      3, { 0, 2, 4 }, { 0, 4, 7 } },
        { "m",           "minor",      3, { 0, 2, 4 }, { 0, 3, 7 } },
        { "\xC2\xB0",    "diminished", 3, { 0, 2, 4 }, { 0, 3, 6 } },   // °
        { "+",           "augmented",  3, { 0, 2, 4 }, { 0, 4, 8 } },
    };

    const std::vector<Quality> kSevenths {
        { "maj7",         "major seventh",        4, { 0, 2, 4, 6 }, { 0, 4, 7, 11 } },
        { "7",            "dominant seventh",     4, { 0, 2, 4, 6 }, { 0, 4, 7, 10 } },
        { "m7",           "minor seventh",        4, { 0, 2, 4, 6 }, { 0, 3, 7, 10 } },
        { "\xC3\xB8" "7", "half-diminished",      4, { 0, 2, 4, 6 }, { 0, 3, 6, 10 } },   // ø7
        { "\xC2\xB0" "7", "diminished seventh",   4, { 0, 2, 4, 6 }, { 0, 3, 6, 9 } },    // °7
    };

    const std::vector<Quality> kNone;

    int pitchClass (int midi) noexcept { return ((midi % 12) + 12) % 12; }
}

const std::vector<Quality>& qualitiesFor (CardType t)
{
    switch (t)
    {
        case CardType::Intervals: return kIntervals;
        case CardType::Triads:    return kTriads;
        case CardType::Sevenths:  return kSevenths;
        case CardType::Single:    break;
    }
    return kNone;
}

// ---- Card ----------------------------------------------------------------------------

bool Card::contains (int midi) const noexcept
{
    return std::find (midis.begin(), midis.begin() + size, midi) != midis.begin() + size;
}

bool Card::containsPitchClass (int midi) const noexcept
{
    return std::any_of (midis.begin(), midis.begin() + size,
                        [pc = pitchClass (midi)] (int m) { return pitchClass (m) == pc; });
}

bool Card::leansFlat() const noexcept
{
    return std::any_of (notes.begin(), notes.begin() + size, [] (const Spelling& s) { return s.accidental < 0; });
}

bool Card::sameAs (const Card& o) const noexcept
{
    if (clef != o.clef || size != o.size)
        return false;
    for (int i = 0; i < size; ++i)
        if (notes[static_cast<std::size_t> (i)] != o.notes[static_cast<std::size_t> (i)])
            return false;
    return true;
}

Card Card::single (Clef clef, const Spelling& s)
{
    Card c;
    c.clef     = clef;
    c.size     = 1;
    c.notes[0] = s;
    c.midis[0] = spelling::midiFor (s);
    return c;
}

// ---- building ----------------------------------------------------------------------------

namespace cards
{

bool allows (Accidentals a, int accidental) noexcept
{
    switch (a)
    {
        case Accidentals::Naturals: return accidental == 0;
        case Accidentals::Sharps:   return accidental >= 0;
        case Accidentals::Flats:    return accidental <= 0;
        case Accidentals::Mixed:    return true;
    }
    return false;
}

bool build (const Quality& q, const Spelling& root, Clef clef, Accidentals acc, Card& out)
{
    out = Card();
    out.clef    = clef;
    out.size    = q.size;
    out.quality = &q;

    const int rootMidi = spelling::midiFor (root);
    const int rootDia  = spelling::diatonicIndex (root);

    for (int i = 0; i < q.size; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        const int  d = rootDia + q.steps[k];
        Spelling sp { ((d % 7) + 7) % 7, 0, d >= 0 ? d / 7 : -((-d + 6) / 7) };
        const int target = rootMidi + q.semitones[k];
        sp.accidental = target - spelling::midiFor (sp);

        if (sp.accidental < -1 || sp.accidental > 1 || spelling::isAwkward (sp)
            || ! allows (acc, sp.accidental) || target < 0 || target > 127)
            return false;

        out.notes[k] = sp;
        out.midis[k] = target;
    }
    return true;
}

void collect (CardType type, Clef clef, int ledgers, Accidentals acc, std::vector<Card>& out)
{
    const int lo = spelling::lowestStep (ledgers);
    const int hi = spelling::highestStep (ledgers);

    for (int step = lo; step <= hi; ++step)
    {
        const auto natural = spelling::naturalAtStep (step, clef);

        for (int a = -1; a <= 1; ++a)
        {
            Spelling root = natural;
            root.accidental = a;
            if (! allows (acc, a) || spelling::isAwkward (root))
                continue;

            if (type == CardType::Single)
            {
                if (const int m = spelling::midiFor (root); m >= 0 && m <= 127)
                    out.push_back (Card::single (clef, root));
                continue;
            }

            for (const auto& q : qualitiesFor (type))
            {
                Card c;
                if (! build (q, root, clef, acc, c))
                    continue;
                const int top = spelling::staffStep (c.notes[static_cast<std::size_t> (c.size - 1)], clef);
                if (top <= hi)
                    out.push_back (c);
            }
        }
    }
}

std::string label (const Card& c, int octaveOffset, bool unicode)
{
    if (! c.isChord())
        return spelling::name (c.bottom(), octaveOffset, unicode);

    std::string out;
    const bool interval = c.size == 2;
    if (interval)
        out = c.quality != nullptr ? c.quality->symbol : "";
    else
        out = spelling::letterName (c.bottom(), unicode) + (c.quality != nullptr ? c.quality->symbol : "");

    out += " \xC2\xB7";   // " ·"
    for (int i = 0; i < c.size; ++i)
    {
        const auto& n = c.notes[static_cast<std::size_t> (i)];
        out += ' ';
        out += interval ? spelling::name (n, octaveOffset, unicode) : spelling::letterName (n, unicode);
    }
    return out;
}

}  // namespace cards
}  // namespace flashroll
