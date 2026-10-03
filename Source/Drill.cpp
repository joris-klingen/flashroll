#include "Drill.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace flashroll
{

Drill::Drill (std::uint32_t seed)
    : rng (seed)
{
    rebuildPool();
}

// ---- settings / pool ---------------------------------------------------------

void Drill::setSettings (const DrillSettings& s, double nowMs)
{
    settings = s;
    for (auto& l : settings.ledgers)
        l = std::clamp (l, 0, kMaxLedgers);
    settings.timeLimitMs = std::max (0, settings.timeLimitMs);
    settings.flashMs     = std::max (0, settings.flashMs);

    rebuildPool();

    // A card the new settings can no longer produce (clef switched, range
    // narrowed, accidentals off) gets replaced; anything else stays up.
    if (hasCard())
    {
        const bool stillValid = std::any_of (pool.begin(), pool.end(), [this] (const Card& c) {
            return c.sameAs (card_);
        });
        if (! stillValid)
            deal (nowMs);
    }
}

void Drill::rebuildPool()
{
    pool.clear();

    const auto addClef = [this] (Clef clef) {
        cards::collect (settings.cardType, clef, settings.ledgers[static_cast<std::size_t> (clef)],
                        settings.accidentals, pool);
    };

    if (settings.clefMode != ClefMode::Bass)   addClef (Clef::Treble);
    if (settings.clefMode != ClefMode::Treble) addClef (Clef::Bass);
}

double Drill::weightOf (const Card& c) const noexcept
{
    // In Mixed mode a step has three spellings; halve the accidentals so each
    // staff position still comes up about as often as in the other modes.
    double base = 1.0;
    if (! c.isChord() && settings.accidentals == Accidentals::Mixed && c.bottom().accidental != 0)
        base = 0.5;

    if (! settings.adaptive)
        return base;

    // A chord is as hard as its notes on average.
    double sum = 0.0;
    for (int i = 0; i < c.size; ++i)
    {
        const auto& s = stats (c.clef, c.midis[static_cast<std::size_t> (i)]);

        // Laplace-smoothed miss rate: an unseen note sits at 0.5, so new notes
        // get dealt early; a mastered note decays toward 0 but never vanishes.
        const double missRate = (double (s.attempts - s.correct) + 1.0) / (double (s.attempts) + 2.0);

        // Slowness relative to the player's session average (once both exist).
        double slow = 0.0;
        if (s.correct > 0 && sessionCorrect > 0)
            slow = std::clamp (s.avgMs() / sessionAvgMs() - 1.0, 0.0, 2.0);

        sum += 0.3 + 3.0 * missRate + 0.6 * slow;
    }
    return base * sum / c.size;
}

// ---- flow ----------------------------------------------------------------------

void Drill::start (double nowMs)
{
    deal (nowMs);
}

void Drill::stop() noexcept
{
    phase_ = Phase::Idle;
}

void Drill::deal (double nowMs)
{
    if (pool.empty())
    {
        phase_ = Phase::Idle;
        return;
    }

    // Weighted pick, never the same key twice running (when there's a choice).
    const bool canAvoidRepeat = std::any_of (pool.begin(), pool.end(),
                                             [this] (const Card& c) { return c.bottomMidi() != lastMidi; });
    std::vector<double> w (pool.size());
    for (std::size_t i = 0; i < pool.size(); ++i)
        w[i] = (canAvoidRepeat && pool[i].bottomMidi() == lastMidi) ? 0.0 : weightOf (pool[i]);

    std::discrete_distribution<std::size_t> pick (w.begin(), w.end());
    card_     = pool[pick (rng)];
    lastMidi  = card_.bottomMidi();
    pressed.reset();
    missed    = false;
    revealed  = false;
    wrongMidi = -1;
    cardShownAt = nowMs;
    enter (Phase::Prompt, nowMs);
}

void Drill::enter (Phase p, double nowMs) noexcept
{
    phase_ = p;
    phaseStartedAt = nowMs;
}

bool Drill::answers (const Card& c, int midi, OctaveRule rule) noexcept
{
    return rule == OctaveRule::Exact ? c.contains (midi) : c.containsPitchClass (midi);
}

bool Drill::chordComplete (const Card& c, const std::bitset<128>& keys, OctaveRule rule) noexcept
{
    for (int i = 0; i < c.size; ++i)
    {
        const int tone = c.midis[static_cast<std::size_t> (i)];
        bool found = false;
        if (rule == OctaveRule::Exact)
            found = tone >= 0 && tone < 128 && keys[static_cast<std::size_t> (tone)];
        else
            for (int k = tone % 12; k < 128 && ! found; k += 12)
                found = keys[static_cast<std::size_t> (k)];
        if (! found)
            return false;
    }
    return true;
}

bool Drill::toneFound (int index) const noexcept
{
    Card one = Card::single (card_.clef, card_.notes[static_cast<std::size_t> (index)]);
    return chordComplete (one, pressed, settings.octaveRule);
}

void Drill::scoreFirstAnswer (bool correct, double nowMs) noexcept
{
    const double ms = std::max (0.0, nowMs - cardShownAt);

    // Per-note lifetime stats. A missed chord still credits the tones that
    // were found, so the stats point at the notes that actually failed.
    for (int i = 0; i < card_.size; ++i)
    {
        auto& s = noteStats[static_cast<std::size_t> (card_.clef)]
                           [static_cast<std::size_t> (card_.midis[static_cast<std::size_t> (i)])];
        ++s.attempts;
        if (correct || (card_.isChord() && toneFound (i)))
        {
            ++s.correct;
            s.correctMs += ms;
        }
    }

    ++sessionCards;
    if (correct)
    {
        ++sessionCorrect;
        sessionMs += ms;
        bestStreak_ = std::max (bestStreak_, ++streak_);
    }
    else
    {
        streak_ = 0;
    }
}

Drill::Outcome Drill::submit (int midi, double nowMs)
{
    switch (phase_)
    {
        case Phase::Idle:
            start (nowMs);
            return Outcome::Started;

        case Phase::Solved:
        case Phase::Revealed:
            return Outcome::Ignored;

        case Phase::Prompt:
            break;
    }

    if (! answers (card_, midi, settings.octaveRule))
        return wrongKey (midi, nowMs);

    if (card_.isChord())
    {
        pressed.set (static_cast<std::size_t> (midi));
        if (! chordComplete (card_, pressed, settings.octaveRule))
            return Outcome::Partial;
    }

    if (! missed)
        scoreFirstAnswer (true, nowMs);
    wrongMidi = -1;
    enter (Phase::Solved, nowMs);
    return Outcome::Correct;
}

Drill::Outcome Drill::wrongKey (int midi, double nowMs)
{
    // The first wrong key scores the miss; on retry the SECOND one reveals the
    // answer (one free guess to self-correct from the red ghost).
    if (missed)
        revealed = true;
    else
        scoreFirstAnswer (false, nowMs);

    missed    = true;
    wrongMidi = midi;

    if (! settings.retryOnMiss)
    {
        revealed = true;
        enter (Phase::Revealed, nowMs);
    }
    return Outcome::Wrong;
}

void Drill::release (int midi) noexcept
{
    if (midi >= 0 && midi < 128)
        pressed.reset (static_cast<std::size_t> (midi));
}

Drill::Outcome Drill::tick (double nowMs)
{
    switch (phase_)
    {
        case Phase::Idle:
            return Outcome::Ignored;

        case Phase::Solved:
            if (phaseAgeMs (nowMs) >= kSolvedFlashMs)
                deal (nowMs);
            return Outcome::Ignored;

        case Phase::Revealed:
            if (phaseAgeMs (nowMs) >= kRevealMs)
                deal (nowMs);
            return Outcome::Ignored;

        case Phase::Prompt:
            break;
    }

    if (settings.timeLimitMs > 0 && ! revealed && cardAgeMs (nowMs) >= settings.timeLimitMs)
    {
        if (! missed)
            scoreFirstAnswer (false, nowMs);
        missed   = true;
        revealed = true;
        if (! settings.retryOnMiss)
            enter (Phase::Revealed, nowMs);
        return Outcome::Timeout;
    }
    return Outcome::Ignored;
}

bool Drill::noteVisible (double nowMs) const noexcept
{
    if (phase_ == Phase::Idle)                     return false;
    if (settings.flashMs <= 0 || missed || revealed) return true;
    if (phase_ == Phase::Solved)                   return true;
    return cardAgeMs (nowMs) < settings.flashMs;
}

float Drill::timeLeft (double nowMs) const noexcept
{
    if (settings.timeLimitMs <= 0 || phase_ == Phase::Idle)
        return 1.0f;
    // Freeze the bar once the card is answered.
    const double at = phase_ == Phase::Prompt ? nowMs : phaseStartedAt;
    return static_cast<float> (std::clamp (1.0 - (at - cardShownAt) / settings.timeLimitMs, 0.0, 1.0));
}

// ---- stats -----------------------------------------------------------------------

void Drill::resetSession() noexcept
{
    sessionCards = sessionCorrect = streak_ = 0;
    sessionMs = 0.0;
}

const NoteStats& Drill::stats (Clef c, int midi) const noexcept
{
    return noteStats[static_cast<std::size_t> (c)][static_cast<std::size_t> (std::clamp (midi, 0, 127))];
}

NoteStats Drill::statsForKey (int midi) const noexcept
{
    NoteStats out;
    for (int c = 0; c < kNumClefs; ++c)
    {
        const auto& s = stats (static_cast<Clef> (c), midi);
        out.attempts  += s.attempts;
        out.correct   += s.correct;
        out.correctMs += s.correctMs;
    }
    return out;
}

void Drill::resetStats() noexcept
{
    for (auto& clef : noteStats)
        clef.fill ({});
    bestStreak_ = 0;
    resetSession();
}

std::string Drill::serialiseStats() const
{
    std::ostringstream out;
    for (int c = 0; c < kNumClefs; ++c)
        for (int m = 0; m < 128; ++m)
            if (const auto& s = noteStats[static_cast<std::size_t> (c)][static_cast<std::size_t> (m)]; s.attempts > 0)
                out << c << ':' << m << ':' << s.attempts << ':' << s.correct << ':'
                    << static_cast<long long> (std::llround (s.correctMs)) << ';';
    return out.str();
}

void Drill::parseStats (const std::string& text)
{
    for (auto& clef : noteStats)
        clef.fill ({});

    std::istringstream in (text);
    std::string rec;
    while (std::getline (in, rec, ';'))
    {
        long long c = -1, m = -1, a = 0, k = 0, ms = 0;
        char s1 = 0, s2 = 0, s3 = 0, s4 = 0;
        std::istringstream r (rec);
        if (! (r >> c >> s1 >> m >> s2 >> a >> s3 >> k >> s4 >> ms))
            continue;
        if (c < 0 || c >= kNumClefs || m < 0 || m > 127 || a < 0 || k < 0 || k > a || ms < 0)
            continue;   // skip corrupt records rather than trusting them
        auto& s = noteStats[static_cast<std::size_t> (c)][static_cast<std::size_t> (m)];
        s.attempts  = static_cast<std::uint32_t> (a);
        s.correct   = static_cast<std::uint32_t> (k);
        s.correctMs = static_cast<double> (ms);
    }
}

}  // namespace flashroll
