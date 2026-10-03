// drill-check — the `drill-logic` CTest. Drives the real NoteSpelling + Drill
// code (no JUCE, no clock) and fails on any wrong staff placement, a candidate
// outside the requested range, a mis-scored answer or a broken stats round
// trip. Fast enough to run on every build.

#include "Drill.h"
#include "NoteSpelling.h"

#include <cstdio>
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
        EXPECT (d.candidates().front().midi == 62);   // D4
        EXPECT (d.candidates().back().midi == 79);    // G5
        for (const auto& c : d.candidates())
            EXPECT (c.clef == Clef::Treble && c.spelling.accidental == 0);

        // Bass, 2 ledgers: steps -5..13 = 19 naturals, all on bass clef.
        s.clefMode = ClefMode::Bass;
        s.ledgers = { 0, 2 };
        d.setSettings (s, 0);
        EXPECT (d.candidates().size() == 19);
        for (const auto& c : d.candidates())
        {
            const int step = spelling::staffStep (c.spelling, Clef::Bass);
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
            EXPECT (! spelling::isAwkward (c.spelling));
            EXPECT (spelling::midiFor (c.spelling) == c.midi);
            sharps += c.spelling.accidental > 0;
            flats  += c.spelling.accidental < 0;
        }
        EXPECT (clefs.size() == 2);
        EXPECT (sharps > 0 && flats > 0);

        // Sharps-only mode deals no flats.
        s.accidentals = Accidentals::Sharps;
        d.setSettings (s, 0);
        for (const auto& c : d.candidates())
            EXPECT (c.spelling.accidental >= 0);
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
        const int first = d.card().midi;
        EXPECT (d.submit (first, 500) == Drill::Outcome::Correct);
        EXPECT (d.phase() == Drill::Phase::Solved);
        EXPECT (d.cardsAnswered() == 1 && d.cardsCorrect() == 1 && d.streak() == 1);
        EXPECT (d.submit (first, 510) == Drill::Outcome::Ignored);   // during the flash
        d.tick (500 + Drill::kSolvedFlashMs);
        EXPECT (d.phase() == Drill::Phase::Prompt);
        EXPECT (d.card().midi != first);
        EXPECT (d.stats (Clef::Treble, first).correct == 1);
        EXPECT (d.stats (Clef::Treble, first).avgMs() == 500.0);

        // Retry mode: wrong → miss scored once, ghost shown, not revealed yet;
        // second wrong reveals; the right key then solves without re-scoring.
        const int target = d.card().midi;
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
        const int t2 = d.card().midi;
        EXPECT (d.submit (t2 + 12 + 1, 3000) == Drill::Outcome::Wrong);
        EXPECT (d.phase() == Drill::Phase::Revealed && d.answerRevealed());
        d.tick (3000 + Drill::kRevealMs);
        EXPECT (d.phase() == Drill::Phase::Prompt);

        // Any-octave rule accepts the pitch class anywhere.
        Card c { Clef::Treble, { 0, 0, 4 }, 60 };
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
        std::string text = "0:" + std::to_string (weak.midi) + ":10:1:9000;"
                         + "0:" + std::to_string (solid.midi) + ":10:10:6000;";
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
            const int m = d.card().midi;
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
}

int main()
{
    spellingTests();
    poolTests();
    flowTests();
    timingTests();
    adaptiveTests();

    if (failures)
    {
        std::printf ("drill-check: %d failure(s)\n", failures);
        return 1;
    }
    std::printf ("drill-check: all checks passed\n");
    return 0;
}
