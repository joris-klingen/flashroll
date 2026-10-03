#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace flashroll
{

// The plugin's (deliberately small) sound: an optional MONITOR voice for the
// keys you play — so the Standalone makes a sound with a silent MIDI
// controller — and short feedback CUES from the drill. Runs entirely on the
// audio thread: fixed voice pools, no allocations, no locks. The message
// thread requests cues through one atomic slot.

class ToneEngine
{
public:
    enum class Cue
    {
        Correct,   // short bright tick
        Wrong,     // soft low thud
        Reveal,    // the TARGET note(s) itself, so the ear learns what was missed
    };

    static constexpr int kMaxCueNotes = 4;

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // Message thread → audio thread. The latest request wins if two land in
    // the same block (they never usefully overlap). `midis` are the card's
    // notes (up to kMaxCueNotes): Reveal sounds them all, so a missed chord is
    // heard as a chord; the other cues key off the first.
    void requestCue (Cue, const int* midis, int count) noexcept;

    // Audio thread.
    void noteOn (int midi, int velocity) noexcept;
    void noteOff (int midi) noexcept;
    void render (juce::AudioBuffer<float>&, int startSample, int numSamples,
                 float monitorGain, float cueGain) noexcept;

private:
    struct Voice
    {
        int    midi = -1;            // -1 = free
        double phase = 0.0, inc = 0.0;
        float  level = 0.0f;         // peak amplitude
        float  env = 0.0f;           // current envelope
        float  decay = 0.0f;         // per-sample multiplier while held
        float  release = 0.0f;       // per-sample multiplier once released
        float  attack = 0.0f;        // per-sample attack increment (0..1 ramp)
        float  attackPos = 0.0f;
        float  bright = 0.0f;        // 2nd/3rd partial mix
        bool   held = false;
        bool   active() const noexcept { return midi >= 0; }
    };

    void startVoice (Voice&, double freq, float level, float decaySec,
                     float releaseSec, float bright, int midiTag) noexcept;
    static float renderVoice (Voice&) noexcept;
    void pollCue() noexcept;

    static double freqOf (int midi) noexcept;
    float perSample (float seconds) const noexcept;   // -60 dB over `seconds`

    double sampleRate = 48000.0;
    std::array<Voice, 16> monitor {};
    std::array<Voice, 6>  cues {};
    std::size_t nextCue = 0;

    // Packed request, one byte each: [cue+1][count][note0..note3]; 0 = nothing
    // pending. One 64-bit atomic, so a request can never tear.
    std::atomic<std::uint64_t> pendingCue { 0 };
};

}  // namespace flashroll
