#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "NoteSpelling.h"

namespace flashroll
{

// The flash-card engine: what to show next, whether a played key answers it,
// and the running score. Pure C++ (no JUCE, no clock of its own — every call
// takes `nowMs` from the caller), so it is deterministic under the drill-check
// CTest and owned by the processor on the MESSAGE thread (the audio thread
// only forwards note-ons through NoteInbox; it never touches a Drill).

enum class ClefMode    { Treble = 0, Bass = 1, Grand = 2 };
enum class Accidentals { Naturals = 0, Sharps = 1, Flats = 2, Mixed = 3 };
enum class OctaveRule  { Exact = 0, AnyOctave = 1 };

struct DrillSettings
{
    ClefMode    clefMode    = ClefMode::Treble;
    std::array<int, kNumClefs> ledgers { 1, 1 };   // per clef, 0..kMaxLedgers
    Accidentals accidentals = Accidentals::Naturals;
    OctaveRule  octaveRule  = OctaveRule::Exact;
    int  timeLimitMs = 0;      // 0 = untimed; else the card times out (a miss)
    int  flashMs     = 0;      // 0 = note stays up; else it hides after this long
    bool retryOnMiss = true;   // stay on a missed card until it's played right
    bool adaptive    = true;   // weight missed / slow notes to come up more

    bool operator== (const DrillSettings& o) const noexcept
    {
        return clefMode == o.clefMode && ledgers == o.ledgers && accidentals == o.accidentals
            && octaveRule == o.octaveRule && timeLimitMs == o.timeLimitMs
            && flashMs == o.flashMs && retryOnMiss == o.retryOnMiss && adaptive == o.adaptive;
    }
    bool operator!= (const DrillSettings& o) const noexcept { return ! (*this == o); }
};

inline constexpr int kMaxLedgers = 4;

// One flash card: the note as drawn (clef + spelling) and the key that answers it.
struct Card
{
    Clef     clef = Clef::Treble;
    Spelling spelling;
    int      midi = 60;
};

// Lifetime stats for one (clef, key) pair. Only a card's FIRST answer counts —
// retries after a miss teach, they don't score.
struct NoteStats
{
    std::uint32_t attempts  = 0;
    std::uint32_t correct   = 0;
    double        correctMs = 0.0;   // summed response time of the correct firsts

    double accuracy() const noexcept  { return attempts ? double (correct) / attempts : 0.0; }
    double avgMs() const noexcept     { return correct ? correctMs / correct : 0.0; }
};

class Drill
{
public:
    enum class Phase
    {
        Idle,       // not started — any key starts
        Prompt,     // card up, waiting for the key
        Solved,     // played right; brief green flash, then the next card
        Revealed,   // missed and moving on (retry off): answer shown, then next
    };

    enum class Outcome { Ignored, Started, Correct, Wrong, Timeout };

    // Feedback timings (ms).
    static constexpr double kSolvedFlashMs = 260.0;
    static constexpr double kRevealMs      = 1400.0;

    explicit Drill (std::uint32_t seed = std::random_device{}());

    // Settings: changing them rebuilds the candidate pool and, if a card is up
    // that the new settings can't produce, deals a fresh one.
    void setSettings (const DrillSettings&, double nowMs);
    const DrillSettings& getSettings() const noexcept { return settings; }

    void start (double nowMs);   // (re)start: deal the first card
    void stop() noexcept;        // back to Idle (stats are kept)

    // A played key (note-on). Returns what it meant.
    Outcome submit (int midi, double nowMs);

    // Advance time: timeouts and end-of-feedback transitions. Returns Timeout
    // when the current card just ran out of time, else Ignored.
    Outcome tick (double nowMs);

    // --- view ---------------------------------------------------------------
    Phase phase() const noexcept               { return phase_; }
    bool  hasCard() const noexcept             { return phase_ != Phase::Idle; }
    const Card& card() const noexcept          { return card_; }
    bool  missedThisCard() const noexcept      { return missed; }
    bool  answerRevealed() const noexcept      { return revealed; }
    int   lastWrongMidi() const noexcept       { return wrongMidi; }    // -1 = none
    double cardAgeMs (double nowMs) const noexcept   { return nowMs - cardShownAt; }
    double phaseAgeMs (double nowMs) const noexcept  { return nowMs - phaseStartedAt; }

    // Is the notehead drawn right now? (Flash mode hides it after flashMs; it
    // comes back once missed so the player sees what it was.)
    bool noteVisible (double nowMs) const noexcept;

    // 1 → 0 over the time limit; 1 when untimed.
    float timeLeft (double nowMs) const noexcept;

    // Session score (since start/resetSession).
    int    cardsAnswered() const noexcept      { return sessionCards; }
    int    cardsCorrect() const noexcept       { return sessionCorrect; }
    int    streak() const noexcept             { return streak_; }
    int    bestStreak() const noexcept         { return bestStreak_; }
    double sessionAvgMs() const noexcept       { return sessionCorrect ? sessionMs / sessionCorrect : 0.0; }
    void   resetSession() noexcept;

    // Lifetime per-note stats (persisted with the plugin state).
    const NoteStats& stats (Clef c, int midi) const noexcept;
    NoteStats statsForKey (int midi) const noexcept;   // both clefs merged
    void   resetStats() noexcept;
    void   setBestStreak (int b) noexcept      { bestStreak_ = b; }

    // Compact text form of the lifetime stats: "clef:midi:attempts:correct:ms;…"
    std::string serialiseStats() const;
    void        parseStats (const std::string&);

    // The candidate pool for the current settings (for tests / the UI).
    const std::vector<Card>& candidates() const noexcept { return pool; }
    double weightOf (const Card&) const noexcept;

    // Does `midi` answer `card` under `rule`?
    static bool answers (const Card&, int midi, OctaveRule) noexcept;

private:
    void rebuildPool();
    void deal (double nowMs);
    void enter (Phase, double nowMs) noexcept;
    void scoreFirstAnswer (bool correct, double nowMs) noexcept;

    DrillSettings settings;
    std::vector<Card> pool;
    std::mt19937 rng;

    Phase  phase_ = Phase::Idle;
    Card   card_;
    bool   missed   = false;   // this card's first answer was wrong / timed out
    bool   revealed = false;   // answer shown (after a miss)
    int    wrongMidi = -1;
    double cardShownAt    = 0.0;
    double phaseStartedAt = 0.0;
    int    lastMidi = -1;      // avoid dealing the same key twice in a row

    int    sessionCards = 0, sessionCorrect = 0, streak_ = 0, bestStreak_ = 0;
    double sessionMs = 0.0;

    std::array<std::array<NoteStats, 128>, kNumClefs> noteStats {};
};

}  // namespace flashroll
