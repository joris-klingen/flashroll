// drill-check — the `drill-logic` CTest. Drives the real NoteSpelling + Drill
// code (no JUCE, no clock) and fails on any wrong staff placement, a candidate
// outside the requested range, a mis-scored answer or a broken stats round
// trip. Fast enough to run on every build.

#include "Drill.h"
#include "NoteSpelling.h"

#include <cstdio>
#include <cstdlib>
#include <set>

using namespace flashroll;

namespace
{
    int failures = 0;

    void expect (bool ok, const char* what, int line)
    {
        if (! ok)
        {
            std::printf ("FAIL (line %d): %s\n", line, what);
            ++failures;
        }
    }
    #define EXPECT(cond) expect ((cond), #cond, __LINE__)

    Spelling sp (int letter, int acc, int oct) { return { letter, acc, oct }; }

    void spellingTests()
    {
        // Landmarks: treble bottom line E4, top line F5; bass bottom G2, top A3.
        EXPECT (spelling::midiFor (sp (0, 0, 4)) == 60);              // middle C
        EXPECT (spelling::staffStep (sp (2, 0, 4), Clef::Treble) == 0);
        EXPECT (spelling::staffStep (sp (3, 0, 5), Clef::Treble) == 8);
        EXPECT (spelling::staffStep (sp (4, 0, 2), Clef::Bass) == 0);
        EXPECT (spelling::staffStep (sp (5, 0, 3), Clef::Bass) == 8);
        EXPECT (spelling::staffStep (sp (4, 0, 4), Clef::Treble) == 2); // G line of the G clef
        EXPECT (spelling::staffStep (sp (3, 0, 3), Clef::Bass) == 6);   // F line of the F clef

        // Middle C: one ledger below treble, one above bass.
        EXPECT (spelling::staffStep (sp (0, 0, 4), Clef::Treble) == -2);
        EXPECT (spelling::staffStep (sp (0, 0, 4), Clef::Bass) == 10);
        EXPECT (spelling::ledgerLines (-2) == -1);
        EXPECT (spelling::ledgerLines (10) == 1);
        EXPECT (spelling::ledgerLines (-1) == 0);
        EXPECT (spelling::ledgerLines (9) == 0);
        EXPECT (spelling::ledgerLines (-5) == -2);

        // Accidentals change the key, not the staff position.
        EXPECT (spelling::midiFor (sp (3, +1, 4)) == 66);               // F#4
        EXPECT (spelling::midiFor (sp (6, -1, 3)) == 58);               // Bb3
        EXPECT (spelling::staffStep (sp (3, +1, 4), Clef::Treble)
                == spelling::staffStep (sp (3, 0, 4), Clef::Treble));

        // fromMidi / naturalAtStep round trips across the whole keyboard.
        for (int m = 0; m < 128; ++m)
        {
            EXPECT (spelling::midiFor (spelling::fromMidi (m, false)) == m);
            EXPECT (spelling::midiFor (spelling::fromMidi (m, true)) == m);
            EXPECT (! spelling::isAwkward (spelling::fromMidi (m, false)));
            EXPECT (! spelling::isAwkward (spelling::fromMidi (m, true)));
        }
        for (int c = 0; c < kNumClefs; ++c)
            for (int step = -12; step <= 20; ++step)
            {
                const auto clef = static_cast<Clef> (c);
                const auto n = spelling::naturalAtStep (step, clef);
                EXPECT (n.accidental == 0);
                EXPECT (spelling::staffStep (n, clef) == step);
            }

        // Names.
        EXPECT (spelling::name (sp (0, 0, 4), 0, false) == "C4");
        EXPECT (spelling::name (sp (0, 0, 4), -1, false) == "C3");      // Ableton octave
        EXPECT (spelling::name (sp (3, +1, 4), 0, false) == "F#4");
        EXPECT (spelling::name (sp (6, -1, 3), 0, false) == "Bb3");
        EXPECT (spelling::midiName (61) == "C#4");
        EXPECT (spelling::midiName (0) == "C-1");

        // Ranges: 0 ledgers = the staff plus the space either side.
        EXPECT (spelling::lowestStep (0) == -1 && spelling::highestStep (0) == 9);
        EXPECT (spelling::lowestStep (1) == -3 && spelling::highestStep (1) == 11);
    }

    void poolTests()
    {
        Drill d (1);
        DrillSettings s;

        // Treble, 0 ledgers, naturals: D4..G5 = 11 notes.
        s.clefMode = ClefMode::Treble;
        s.ledgers = { 0, 0 };
        s.accidentals = Accidentals::Naturals;
        d.setSettings (s, 0);
        EXPECT (d.candidates().size() == 11);
        EXPECT (d.candidates().front().bottomMidi() == 62);   // D4
        EXPECT (d.candidates().back().bottomMidi() == 79);    // G5
        for (const auto& c : d.candidates())
            EXPECT (c.clef == Clef::Treble && c.bottom().accidental == 0);

        // Bass, 2 ledgers: steps -5..13 = 19 naturals, all on bass clef.
        s.clefMode = ClefMode::Bass;
        s.ledgers = { 0, 2 };
        d.setSettings (s, 0);
        EXPECT (d.candidates().size() == 19);
        for (const auto& c : d.candidates())
        {
            const int step = spelling::staffStep (c.bottom(), Clef::Bass);
            EXPECT (c.clef == Clef::Bass && step >= -5 && step <= 13);
        }

        // Grand staff covers both clefs; Mixed accidentals never deals E#/Cb etc.
        s.clefMode = ClefMode::Grand;
        s.ledgers = { 1, 1 };
        s.accidentals = Accidentals::Mixed;
        d.setSettings (s, 0);
        std::set<int> clefs;
        int sharps = 0, flats = 0;
        for (const auto& c : d.candidates())
        {
            clefs.insert (static_cast<int> (c.clef));
            EXPECT (! spelling::isAwkward (c.bottom()));
            EXPECT (spelling::midiFor (c.bottom()) == c.bottomMidi());
            sharps += c.bottom().accidental > 0;
            flats  += c.bottom().accidental < 0;
        }
        EXPECT (clefs.size() == 2);
        EXPECT (sharps > 0 && flats > 0);

        // Sharps-only mode deals no flats.
        s.accidentals = Accidentals::Sharps;
        d.setSettings (s, 0);
        for (const auto& c : d.candidates())
            EXPECT (c.bottom().accidental >= 0);
    }

    void flowTests()
    {
        Drill d (7);
        DrillSettings s;
        s.clefMode = ClefMode::Treble;
        s.ledgers = { 1, 1 };
        s.retryOnMiss = true;
        d.setSettings (s, 0);

        // Any key starts; that key is not an answer.
        EXPECT (d.phase() == Drill::Phase::Idle);
        EXPECT (d.submit (60, 0) == Drill::Outcome::Started);
        EXPECT (d.phase() == Drill::Phase::Prompt);
        EXPECT (d.cardsAnswered() == 0);

        // Correct first answer scores, flashes, then deals a different key.
        const int first = d.card().bottomMidi();
        EXPECT (d.submit (first, 500) == Drill::Outcome::Correct);
        EXPECT (d.phase() == Drill::Phase::Solved);
        EXPECT (d.cardsAnswered() == 1 && d.cardsCorrect() == 1 && d.streak() == 1);
        EXPECT (d.submit (first, 510) == Drill::Outcome::Ignored);   // during the flash
        d.tick (500 + Drill::kSolvedFlashMs);
        EXPECT (d.phase() == Drill::Phase::Prompt);
        EXPECT (d.card().bottomMidi() != first);
        EXPECT (d.stats (Clef::Treble, first).correct == 1);
        EXPECT (d.stats (Clef::Treble, first).avgMs() == 500.0);

        // Retry mode: wrong → miss scored once, ghost shown, not revealed yet;
        // second wrong reveals; the right key then solves without re-scoring.
        const int target = d.card().bottomMidi();
        const double t0 = 1000;
        d.tick (t0);
        EXPECT (d.submit (target + 1, t0 + 100) == Drill::Outcome::Wrong);
        EXPECT (d.phase() == Drill::Phase::Prompt);
        EXPECT (d.missedThisCard() && ! d.answerRevealed());
        EXPECT (d.lastWrongMidi() == target + 1);
        EXPECT (d.streak() == 0 && d.cardsAnswered() == 2 && d.cardsCorrect() == 1);
        EXPECT (d.submit (target + 2, t0 + 200) == Drill::Outcome::Wrong);
        EXPECT (d.answerRevealed());
        EXPECT (d.cardsAnswered() == 2);
        EXPECT (d.submit (target, t0 + 300) == Drill::Outcome::Correct);
        EXPECT (d.cardsCorrect() == 1);
        EXPECT (d.stats (Clef::Treble, target).attempts >= 1);

        // No-retry mode: a wrong key reveals and moves on by itself.
        s.retryOnMiss = false;
        d.setSettings (s, 2000);
        d.tick (2000 + Drill::kSolvedFlashMs);
        EXPECT (d.phase() == Drill::Phase::Prompt);
        const int t2 = d.card().bottomMidi();
        EXPECT (d.submit (t2 + 12 + 1, 3000) == Drill::Outcome::Wrong);
        EXPECT (d.phase() == Drill::Phase::Revealed && d.answerRevealed());
        d.tick (3000 + Drill::kRevealMs);
        EXPECT (d.phase() == Drill::Phase::Prompt);

        // Any-octave rule accepts the pitch class anywhere.
        Card c = Card::single (Clef::Treble, { 0, 0, 4 });
        EXPECT (Drill::answers (c, 72, OctaveRule::AnyOctave));
        EXPECT (Drill::answers (c, 36, OctaveRule::AnyOctave));
        EXPECT (! Drill::answers (c, 72, OctaveRule::Exact));
        EXPECT (! Drill::answers (c, 61, OctaveRule::AnyOctave));
    }

    void timingTests()
    {
        Drill d (3);
        DrillSettings s;
        s.timeLimitMs = 2000;
        s.flashMs = 500;
        s.retryOnMiss = false;
        d.setSettings (s, 0);
        d.start (0);

        EXPECT (d.noteVisible (100));
        EXPECT (! d.noteVisible (600));                     // flashed away
        EXPECT (d.timeLeft (1000) > 0.49f && d.timeLeft (1000) < 0.51f);
        EXPECT (d.tick (1999) == Drill::Outcome::Ignored);
        EXPECT (d.tick (2000) == Drill::Outcome::Timeout);
        EXPECT (d.noteVisible (2001));                      // shown again once missed
        EXPECT (d.phase() == Drill::Phase::Revealed);
        EXPECT (d.cardsAnswered() == 1 && d.cardsCorrect() == 0);
        EXPECT (d.tick (2100) == Drill::Outcome::Ignored);  // a timeout scores once

        // Retry + timeout: revealed but still waiting for the key.
        s.retryOnMiss = true;
        d.setSettings (s, 5000);
        d.start (5000);
        EXPECT (d.tick (7000) == Drill::Outcome::Timeout);
        EXPECT (d.phase() == Drill::Phase::Prompt && d.answerRevealed());
        EXPECT (d.timeLeft (7500) == 0.0f);
    }

    void adaptiveTests()
    {
        Drill d (11);
        DrillSettings s;
        s.ledgers = { 0, 0 };
        d.setSettings (s, 0);

        // A note missed a lot outweighs one always played right.
        const Card& weak  = d.candidates()[2];
        const Card& solid = d.candidates()[5];
        std::string text = "0:" + std::to_string (weak.bottomMidi()) + ":10:1:9000;"
                         + "0:" + std::to_string (solid.bottomMidi()) + ":10:10:6000;";
        d.parseStats (text);
        EXPECT (d.weightOf (weak) > 3.0 * d.weightOf (solid));

        // Round trip.
        EXPECT (d.serialiseStats() == text);
        d.parseStats ("garbage;1:999:1:1:1;0:60:2:5:1;");    // all rejected
        EXPECT (d.serialiseStats().empty());

        // Over many deals every candidate shows up, and no key repeats back-to-back.
        d.resetStats();
        std::set<int> seen;
        int last = -1;
        d.start (0);
        for (int i = 0; i < 2000; ++i)
        {
            const int m = d.card().bottomMidi();
            EXPECT (m != last);
            last = m;
            seen.insert (m);
            d.submit (m, i * 1000.0 + 10);
            d.tick (i * 1000.0 + 10 + Drill::kSolvedFlashMs);
        }
        EXPECT (seen.size() == d.candidates().size());

        // Non-adaptive weights are flat.
        s.adaptive = false;
        d.setSettings (s, 0);
        EXPECT (d.weightOf (d.candidates()[0]) == d.weightOf (d.candidates()[3]));
    }

    const Quality& quality (CardType t, const char* longName)
    {
        for (const auto& q : qualitiesFor (t))
            if (std::string (q.longName) == longName)
                return q;
        std::printf ("no quality %s\n", longName);
        std::abort();
    }

    void chordBuildTests()
    {
        Card c;
        // C major on middle C: C4 E4 G4.
        EXPECT (cards::build (quality (CardType::Triads, "major"), sp (0, 0, 4), Clef::Treble, Accidentals::Naturals, c));
        EXPECT (c.size == 3 && c.midis[0] == 60 && c.midis[1] == 64 && c.midis[2] == 67);
        EXPECT (cards::label (c, 0, false) == "C \xC2\xB7 C E G");

        // D minor seventh: D F A C, all naturals.
        EXPECT (cards::build (quality (CardType::Sevenths, "minor seventh"), sp (1, 0, 4), Clef::Treble, Accidentals::Naturals, c));
        EXPECT (c.midis[0] == 62 && c.midis[1] == 65 && c.midis[2] == 69 && c.midis[3] == 72);
        EXPECT (c.notes[3] == sp (0, 0, 5));

        // B half-diminished: B D F A.
        EXPECT (cards::build (quality (CardType::Sevenths, "half-diminished"), sp (6, 0, 3), Clef::Treble, Accidentals::Naturals, c));
        EXPECT (c.notes[2] == sp (3, 0, 4) && c.notes[3] == sp (5, 0, 4));

        // Augmented needs a sharp: refused under Naturals, C E G# under Sharps.
        EXPECT (! cards::build (quality (CardType::Triads, "augmented"), sp (0, 0, 4), Clef::Treble, Accidentals::Naturals, c));
        EXPECT (cards::build (quality (CardType::Triads, "augmented"), sp (0, 0, 4), Clef::Treble, Accidentals::Sharps, c));
        EXPECT (c.notes[2] == sp (4, +1, 4));

        // Spelling follows the steps: a minor third on C is Eb (a third), not D#.
        EXPECT (cards::build (quality (CardType::Intervals, "minor third"), sp (0, 0, 4), Clef::Treble, Accidentals::Mixed, c));
        EXPECT (c.notes[1] == sp (2, -1, 4));
        EXPECT (cards::label (c, 0, false) == "m3 \xC2\xB7 C4 Eb4");
        EXPECT (! cards::build (quality (CardType::Intervals, "minor third"), sp (0, 0, 4), Clef::Treble, Accidentals::Sharps, c));

        // Double / awkward accidentals refused: Cb-rooted or Fb results never appear.
        EXPECT (! cards::build (quality (CardType::Triads, "minor"), sp (1, -1, 4), Clef::Treble, Accidentals::Mixed, c));   // Db Fb Ab

        // Pools: every card stacked in range, ascending, spelled per its quality.
        for (int t = 1; t <= 3; ++t)
            for (int a = 0; a < 4; ++a)
                for (int clef = 0; clef < kNumClefs; ++clef)
                {
                    std::vector<Card> pool;
                    const auto type = static_cast<CardType> (t);
                    const auto acc  = static_cast<Accidentals> (a);
                    cards::collect (type, static_cast<Clef> (clef), 1, acc, pool);
                    EXPECT (! pool.empty());
                    for (const auto& k : pool)
                    {
                        EXPECT (k.size == k.quality->size && k.size == (t == 1 ? 2 : t + 1));
                        for (int i = 0; i < k.size; ++i)
                        {
                            const auto& n = k.notes[static_cast<std::size_t> (i)];
                            const int step = spelling::staffStep (n, static_cast<Clef> (clef));
                            EXPECT (step >= spelling::lowestStep (1) && step <= spelling::highestStep (1));
                            EXPECT (spelling::midiFor (n) == k.midis[static_cast<std::size_t> (i)]);
                            EXPECT (cards::allows (acc, n.accidental) && ! spelling::isAwkward (n));
                            EXPECT (k.midis[static_cast<std::size_t> (i)] - k.midis[0] == k.quality->semitones[static_cast<std::size_t> (i)]);
                            if (i > 0)
                                EXPECT (k.midis[static_cast<std::size_t> (i)] > k.midis[static_cast<std::size_t> (i - 1)]);
                        }
                    }
                }
    }

    void chordFlowTests()
    {
        Drill d (5);
        DrillSettings s;
        s.cardType = CardType::Triads;
        s.ledgers = { 1, 1 };
        d.setSettings (s, 0);
        d.start (0);

        const Card c = d.card();
        EXPECT (c.size == 3);

        // One tone at a time, out of order: partial, partial, done.
        EXPECT (d.submit (c.midis[2], 100) == Drill::Outcome::Partial);
        EXPECT (d.pressedOnCard (c.midis[2]));
        EXPECT (d.submit (c.midis[0], 150) == Drill::Outcome::Partial);
        EXPECT (d.submit (c.midis[1], 200) == Drill::Outcome::Correct);
        EXPECT (d.cardsCorrect() == 1);
        for (int i = 0; i < 3; ++i)
            EXPECT (d.stats (c.clef, c.midis[static_cast<std::size_t> (i)]).correct == 1);

        // Keys held over from the last card don't count on the next one.
        d.tick (200 + Drill::kSolvedFlashMs);
        EXPECT (d.phase() == Drill::Phase::Prompt);
        EXPECT (! d.pressedOnCard (c.midis[0]) && ! d.pressedOnCard (c.midis[1]));

        // Releasing a tone means it must be pressed again.
        const Card c2 = d.card();
        EXPECT (d.submit (c2.midis[0], 1000) == Drill::Outcome::Partial);
        d.release (c2.midis[0]);
        EXPECT (d.submit (c2.midis[1], 1010) == Drill::Outcome::Partial);
        EXPECT (d.submit (c2.midis[2], 1020) == Drill::Outcome::Partial);
        EXPECT (d.submit (c2.midis[0], 1030) == Drill::Outcome::Correct);

        // A non-chord key is wrong; the tones already found are credited, the
        // rest scored as missed.
        d.tick (1030 + Drill::kSolvedFlashMs);
        const Card c3 = d.card();
        const auto before0 = d.stats (c3.clef, c3.midis[0]);
        const auto before2 = d.stats (c3.clef, c3.midis[2]);
        EXPECT (d.submit (c3.midis[0], 2000) == Drill::Outcome::Partial);
        int stray = c3.midis[0] + 1;
        while (c3.contains (stray)) ++stray;
        EXPECT (d.submit (stray, 2100) == Drill::Outcome::Wrong);
        EXPECT (d.missedThisCard() && d.lastWrongMidi() == stray);
        EXPECT (d.stats (c3.clef, c3.midis[0]).correct == before0.correct + 1);
        EXPECT (d.stats (c3.clef, c3.midis[2]).correct == before2.correct);
        EXPECT (d.stats (c3.clef, c3.midis[2]).attempts == before2.attempts + 1);
        // Finishing it after the miss solves the card without re-scoring.
        d.submit (c3.midis[1], 2200);
        EXPECT (d.submit (c3.midis[2], 2300) == Drill::Outcome::Correct);
        EXPECT (d.cardsAnswered() == 3 && d.cardsCorrect() == 2);

        // Any-octave: chord tones count in any octave.
        s.octaveRule = OctaveRule::AnyOctave;
        d.setSettings (s, 3000);
        d.tick (3000 + Drill::kSolvedFlashMs);
        const Card c4 = d.card();
        EXPECT (d.submit (c4.midis[0] - 12, 4000) == Drill::Outcome::Partial);
        EXPECT (d.submit (c4.midis[1] + 12, 4010) == Drill::Outcome::Partial);
        EXPECT (d.submit (c4.midis[2] + 24, 4020) == Drill::Outcome::Correct);

        // Interval and seventh pools deal 2- and 4-note cards.
        s.cardType = CardType::Intervals;
        d.setSettings (s, 5000);
        d.start (5000);
        EXPECT (d.card().size == 2);
        s.cardType = CardType::Sevenths;
        d.setSettings (s, 5000);
        EXPECT (d.card().size == 4);   // the interval card was replaced
    }
}

int main()
{
    spellingTests();
    poolTests();
    flowTests();
    timingTests();
    adaptiveTests();
    chordBuildTests();
    chordFlowTests();

    if (failures)
    {
        std::printf ("drill-check: %d failure(s)\n", failures);
        return 1;
    }
    std::printf ("drill-check: all checks passed\n");
    return 0;
}
