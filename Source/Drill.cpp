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
            return c.clef == card_.clef && c.spelling == card_.spelling;
        });
        if (! stillValid)
            deal (nowMs);
    }
}

void Drill::rebuildPool()
{
    pool.clear();

    const auto addClef = [this] (Clef clef)
    {
        const int ledgers = settings.ledgers[static_cast<std::size_t> (clef)];
        for (int step = spelling::lowestStep (ledgers); step <= spelling::highestStep (ledgers); ++step)
        {
            const auto natural = spelling::naturalAtStep (step, clef);

            const auto add = [&] (int accidental)
            {
                Spelling sp = natural;
                sp.accidental = accidental;
                if (spelling::isAwkward (sp))
                    return;
                const int midi = spelling::midiFor (sp);
                if (midi >= 0 && midi <= 127)
                    pool.push_back ({ clef, sp, midi });
            };

            add (0);
            if (settings.accidentals == Accidentals::Sharps || settings.accidentals == Accidentals::Mixed)
                add (+1);
            if (settings.accidentals == Accidentals::Flats || settings.accidentals == Accidentals::Mixed)
                add (-1);
        }
    };

    if (settings.clefMode != ClefMode::Bass)   addClef (Clef::Treble);
    if (settings.clefMode != ClefMode::Treble) addClef (Clef::Bass);
}

double Drill::weightOf (const Card& c) const noexcept
{
    // In Mixed mode a step has three spellings; halve the accidentals so each
    // staff position still comes up about as often as in the other modes.
    double base = 1.0;
    if (settings.accidentals == Accidentals::Mixed && c.spelling.accidental != 0)
        base = 0.5;

    if (! settings.adaptive)
        return base;

    const auto& s = stats (c.clef, c.midi);

    // Laplace-smoothed miss rate: an unseen note sits at 0.5, so new notes get
    // dealt early; a mastered note decays toward 0 but never vanishes.
    const double missRate = (double (s.attempts - s.correct) + 1.0) / (double (s.attempts) + 2.0);

    // Slowness relative to the player's session average (only once both exist).
    double slow = 0.0;
    if (s.correct > 0 && sessionCorrect > 0)
        slow = std::clamp (s.avgMs() / sessionAvgMs() - 1.0, 0.0, 2.0);

    return base * (0.3 + 3.0 * missRate + 0.6 * slow);
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
                                             [this] (const Card& c) { return c.midi != lastMidi; });
    std::vector<double> w (pool.size());
    for (std::size_t i = 0; i < pool.size(); ++i)
        w[i] = (canAvoidRepeat && pool[i].midi == lastMidi) ? 0.0 : weightOf (pool[i]);

    std::discrete_distribution<std::size_t> pick (w.begin(), w.end());
    card_     = pool[pick (rng)];
    lastMidi  = card_.midi;
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
    return rule == OctaveRule::Exact ? midi == c.midi
                                     : ((midi % 12) + 12) % 12 == ((c.midi % 12) + 12) % 12;
}

void Drill::scoreFirstAnswer (bool correct, double nowMs) noexcept
{
    auto& s = noteStats[static_cast<std::size_t> (card_.clef)][static_cast<std::size_t> (card_.midi)];
    ++s.attempts;
    ++sessionCards;

    if (correct)
    {
        const double ms = std::max (0.0, nowMs - cardShownAt);
        ++s.correct;
        s.correctMs += ms;
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

    if (answers (card_, midi, settings.octaveRule))
    {
        if (! missed)
            scoreFirstAnswer (true, nowMs);
        wrongMidi = -1;
        enter (Phase::Solved, nowMs);
        return Outcome::Correct;
    }

    // Wrong key. The first one scores the miss; on retry the SECOND wrong key
    // reveals the answer (one free guess to self-correct from the red ghost).
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
