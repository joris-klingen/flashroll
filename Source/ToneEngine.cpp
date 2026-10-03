#include "ToneEngine.h"

#include <cmath>

namespace flashroll
{

void ToneEngine::prepare (double sr) noexcept
{
    sampleRate = sr > 0.0 ? sr : 48000.0;
    reset();
}

void ToneEngine::reset() noexcept
{
    for (auto& v : monitor) v.midi = -1;
    for (auto& v : cues)    v.midi = -1;
    pendingCue.store (0);
}

double ToneEngine::freqOf (int midi) noexcept
{
    return 440.0 * std::pow (2.0, (midi - 69) / 12.0);
}

float ToneEngine::perSample (float seconds) const noexcept
{
    // Multiplier that decays by 60 dB (×0.001) over `seconds`.
    return static_cast<float> (std::pow (0.001, 1.0 / (std::max (0.005f, seconds) * sampleRate)));
}

void ToneEngine::requestCue (Cue c, const int* midis, int count) noexcept
{
    count = juce::jlimit (1, kMaxCueNotes, count);
    std::uint64_t packed = (static_cast<std::uint64_t> (c) + 1) << 56
                         | static_cast<std::uint64_t> (count) << 48;
    for (int i = 0; i < count; ++i)
        packed |= static_cast<std::uint64_t> (juce::jlimit (0, 127, midis[i])) << (8 * i);
    pendingCue.store (packed);
}

void ToneEngine::startVoice (Voice& v, double freq, float level, float decaySec,
                             float releaseSec, float bright, int midiTag) noexcept
{
    v.midi      = midiTag;
    v.phase     = 0.0;
    v.inc       = juce::MathConstants<double>::twoPi * freq / sampleRate;
    v.level     = level;
    v.env       = 1.0f;
    v.decay     = perSample (decaySec);
    v.release   = perSample (releaseSec);
    v.attack    = static_cast<float> (1.0 / (0.004 * sampleRate));   // 4 ms click-free attack
    v.attackPos = 0.0f;
    v.bright    = bright;
    v.held      = true;
}

void ToneEngine::noteOn (int midi, int velocity) noexcept
{
    // Re-strike the same key's voice if it's still ringing, else the quietest.
    Voice* target = nullptr;
    for (auto& v : monitor)
        if (v.midi == midi) { target = &v; break; }
    if (target == nullptr)
    {
        target = &monitor[0];
        for (auto& v : monitor)
        {
            if (! v.active()) { target = &v; break; }
            if (v.env < target->env) target = &v;
        }
    }

    const float vel = juce::jlimit (1, 127, velocity) / 127.0f;
    // A soft electric-piano-ish tone: long decay while held, quick release.
    startVoice (*target, freqOf (midi), 0.22f * vel, 3.5f, 0.25f, 0.35f * vel, midi);
}

void ToneEngine::noteOff (int midi) noexcept
{
    for (auto& v : monitor)
        if (v.midi == midi)
            v.held = false;
}

void ToneEngine::pollCue() noexcept
{
    const auto req = pendingCue.exchange (0);
    if (req == 0)
        return;

    const auto cue   = static_cast<Cue> ((req >> 56) - 1);
    const int  count = static_cast<int> ((req >> 48) & 0xff);
    const auto note  = [req] (int i) { return static_cast<int> ((req >> (8 * i)) & 0xff); };

    const auto next = [this] () -> Voice& {
        auto& v = cues[nextCue];
        nextCue = (nextCue + 1) % cues.size();
        return v;
    };

    // Cues are "released" from the start (held = false) so they ring out.
    const auto ringOut = [] (Voice& v) { v.held = false; v.release = v.decay; };

    switch (cue)
    {
        case Cue::Correct:
        {
            auto& v = next();
            startVoice (v, freqOf (juce::jlimit (84, 108, note (0) + 24)), 0.20f, 0.18f, 0.18f, 0.15f, 128);
            ringOut (v);
            break;
        }
        case Cue::Wrong:
        {
            auto& v = next();
            startVoice (v, 98.0, 0.30f, 0.22f, 0.22f, 0.6f, 128);
            ringOut (v);
            break;
        }
        case Cue::Reveal:
            // Quieter per note as the chord grows, so a seventh isn't louder than a single note.
            for (int i = 0; i < count; ++i)
            {
                auto& v = next();
                startVoice (v, freqOf (note (i)), 0.26f / std::sqrt (static_cast<float> (count)), 1.2f, 1.2f, 0.3f, 128);
                ringOut (v);
            }
            break;
    }
}

float ToneEngine::renderVoice (Voice& v) noexcept
{
    const auto s = static_cast<float> (std::sin (v.phase)
                                       + v.bright * 0.5 * std::sin (2.0 * v.phase)
                                       + v.bright * 0.2 * std::sin (3.0 * v.phase));
    v.phase += v.inc;
    if (v.phase > juce::MathConstants<double>::twoPi * 64.0)
        v.phase -= juce::MathConstants<double>::twoPi * 64.0;

    float a = 1.0f;
    if (v.attackPos < 1.0f)
    {
        a = v.attackPos;
        v.attackPos += v.attack;
    }

    const float out = s * v.level * v.env * a;
    v.env *= v.held ? v.decay : v.release;
    if (v.env < 1.0e-4f)
        v.midi = -1;
    return out;
}

void ToneEngine::render (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                         float monitorGain, float cueGain) noexcept
{
    pollCue();

    const int numCh = buffer.getNumChannels();
    if (numCh == 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        float mon = 0.0f, cue = 0.0f;
        for (auto& v : monitor)
            if (v.active()) mon += renderVoice (v);
        for (auto& v : cues)
            if (v.active()) cue += renderVoice (v);

        const float s = mon * monitorGain + cue * cueGain;
        for (int ch = 0; ch < numCh; ++ch)
            buffer.addSample (ch, startSample + i, s);
    }
}

}  // namespace flashroll
