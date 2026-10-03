#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>

#include "Drill.h"
#include "NoteInbox.h"
#include "ToneEngine.h"

namespace flashroll
{

// Sight-reading trainer: flashes a note on a treble / bass / grand staff and
// waits for the matching MIDI key.
//
// Threading:
//   * Audio thread (processBlock) — forwards every host / controller note-on
//     and note-off into `inbox` (lock-free; offs matter for held chords) and renders the monitor / cue tones. It never
//     touches the Drill. (On-screen key clicks skip the audio thread and call
//     submitKey directly, so they work with no audio device running.)
//   * Message thread (this object's Timer) — drains the inbox into the Drill,
//     advances its clock, and asks ToneEngine for feedback cues. The editor
//     reads the Drill on the same thread, so the game state needs no locks.
//
// Only the two LEVEL controls are host-automatable. Drill settings, octave
// naming and the lifetime per-note stats are persisted as properties on the
// parameter tree (like hitnotedmx's grid shape): they're session state, not
// something to ride with automation.

class FlashRollAudioProcessor  : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    FlashRollAudioProcessor();
    ~FlashRollAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                                   { return true; }

    const juce::String getName() const override                       { return JucePlugin_Name; }
    bool acceptsMidi()  const override                                { return true; }
    bool producesMidi() const override                                { return false; }
    bool isMidiEffect() const override                                { return false; }
    double getTailLengthSeconds() const override                      { return 0.0; }

    int getNumPrograms() override                                     { return 1; }
    int getCurrentProgram() override                                  { return 0; }
    void setCurrentProgram (int) override                             {}
    const juce::String getProgramName (int) override                  { return {}; }
    void changeProgramName (int, const juce::String&) override        {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept      { return parameters; }
    juce::MidiKeyboardState& getKeyboardState() noexcept              { return keyboardState; }

    // ---- message-thread API (editor) ---------------------------------------
    const Drill& getDrill() const noexcept                            { return drill; }
    void setDrillSettings (const DrillSettings&);
    void startDrill();
    void stopDrill();
    void resetStats();
    void submitKey (int midi);   // an on-screen key click (latched: never released, see Drill::release)

    // Last scored event, for the editor's flash animation.
    Drill::Outcome lastOutcome() const noexcept                       { return lastOutcome_; }
    double lastOutcomeAtMs() const noexcept                           { return lastOutcomeAt; }

    // Display preferences (persisted, not automatable).
    int  getOctaveOffset() const;            // 0 = scientific (C4 = middle C), -1 = Ableton (C3)
    void setOctaveOffset (int);
    bool getShowHeatmap() const;
    void setShowHeatmap (bool);

    static double nowMs() noexcept           { return juce::Time::getMillisecondCounterHiRes(); }

    // Automatable level parameter IDs (0..1).
    static constexpr const char* kMonitorLevelId = "monitorLevel";
    static constexpr const char* kCueLevelId     = "cueLevel";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void timerCallback() override;
    void handleOutcome (Drill::Outcome, double now);

    // Settings ⇄ state-tree properties.
    DrillSettings readSettings() const;
    void writeSettings (const DrillSettings&);
    void storeStats();   // drill stats → cached string (message thread)

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* monitorLevelParam { nullptr };
    std::atomic<float>* cueLevelParam     { nullptr };

    juce::MidiKeyboardState keyboardState;
    NoteInbox  inbox;
    ToneEngine tone;

    Drill drill;
    Drill::Outcome lastOutcome_ = Drill::Outcome::Ignored;
    double lastOutcomeAt = 0.0;

    // Stats snapshot for getStateInformation, which hosts may call off the
    // message thread — so it never reads the Drill, only this cached copy.
    juce::CriticalSection statsLock;
    juce::String statsText;
    int bestStreakCached = 0;

    // setStateInformation may also arrive off the message thread: it swaps the
    // tree and raises this flag; the timer applies it to the Drill.
    std::atomic<bool> stateDirty { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlashRollAudioProcessor)
};

}  // namespace flashroll
