#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace flashroll
{

namespace
{
    // State-tree property names (persisted, not automatable).
    namespace prop
    {
        const juce::Identifier clefMode     { "clefMode" };
        const juce::Identifier ledgersT     { "ledgersTreble" };
        const juce::Identifier ledgersB     { "ledgersBass" };
        const juce::Identifier accidentals  { "accidentals" };
        const juce::Identifier octaveRule   { "octaveRule" };
        const juce::Identifier timeLimitMs  { "timeLimitMs" };
        const juce::Identifier flashMs      { "flashMs" };
        const juce::Identifier retry        { "retryOnMiss" };
        const juce::Identifier adaptive     { "adaptive" };
        const juce::Identifier octaveOffset { "octaveOffset" };
        const juce::Identifier heatmap      { "heatmap" };
        const juce::Identifier stats        { "stats" };
        const juce::Identifier bestStreak   { "bestStreak" };
    }

    constexpr int kTimerHz = 120;   // drill clock: answer latency ≤ ~8 ms + block size
}

juce::AudioProcessorValueTreeState::ParameterLayout
FlashRollAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto pctAttributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) {
            return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
        .withValueFromStringFunction ([] (const juce::String& t) {
            return t.removeCharacters ("% ").getFloatValue() / 100.0f; });

    // Monitor = the tone for the keys you play (useful in the Standalone with
    // a silent controller; turn it down if your piano already sounds).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (kMonitorLevelId, 1), "Monitor Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pctAttributes));
    // Cues = the right / wrong ticks and the reveal tone.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (kCueLevelId, 1), "Cue Level",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, pctAttributes));

    return layout;
}

FlashRollAudioProcessor::FlashRollAudioProcessor()
    // Instrument shape (as hitnotedmx): a stereo OUTPUT bus, no input. It
    // loads on a MIDI track as the instrument and receives the keyboard
    // directly; the audio carries the optional monitor / cue tones.
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "FlashRoll", createParameterLayout())
{
    monitorLevelParam = parameters.getRawParameterValue (kMonitorLevelId);
    cueLevelParam     = parameters.getRawParameterValue (kCueLevelId);

    startTimerHz (kTimerHz);
}

FlashRollAudioProcessor::~FlashRollAudioProcessor()
{
    stopTimer();
}

// ---- audio ------------------------------------------------------------------------

void FlashRollAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    tone.prepare (sampleRate);
    keyboardState.reset();
}

void FlashRollAudioProcessor::releaseResources() {}

bool FlashRollAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono()
        || mainOut == juce::AudioChannelSet::stereo();
}

void FlashRollAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Host / controller note-ons answer the drill. Collected BEFORE the
    // on-screen keyboard's clicks are merged in below: those reach the drill
    // directly from the editor (submitKey), so they must not count twice.
    for (const auto meta : midi)
        if (const auto msg = meta.getMessage(); msg.isNoteOn())
            inbox.push (msg.getNoteNumber(), msg.getVelocity());

    // Merge on-screen clicks into this block's MIDI so the monitor sounds them,
    // and mark host notes as down on the on-screen keyboard.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    const float monitorGain = monitorLevelParam->load();
    const float cueGain     = cueLevelParam->load();

    // Render sample-accurately between events so a struck key sounds on time.
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (pos, buffer.getNumSamples(), meta.samplePosition);
        tone.render (buffer, pos, at - pos, monitorGain, cueGain);
        pos = at;

        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            tone.noteOn (msg.getNoteNumber(), msg.getVelocity());
        }
        else if (msg.isNoteOff())
        {
            tone.noteOff (msg.getNoteNumber());
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (int n = 0; n < 128; ++n)
                tone.noteOff (n);
        }
    }
    tone.render (buffer, pos, buffer.getNumSamples() - pos, monitorGain, cueGain);

    // We're an instrument: consume the MIDI rather than echo it back.
    midi.clear();
}

// ---- drill (message thread) ---------------------------------------------------------

void FlashRollAudioProcessor::timerCallback()
{
    const double now = nowMs();

    if (stateDirty.exchange (false))
    {
        const auto& st = parameters.state;
        drill.setSettings (readSettings(), now);
        drill.parseStats (st.getProperty (prop::stats).toString().toStdString());
        drill.setBestStreak (static_cast<int> (st.getProperty (prop::bestStreak, 0)));
        storeStats();
    }

    inbox.drain ([this, now] (int midi, int /*velocity*/) {
        handleOutcome (drill.submit (midi, now), now);
    });
    handleOutcome (drill.tick (now), now);
}

void FlashRollAudioProcessor::handleOutcome (Drill::Outcome o, double now)
{
    switch (o)
    {
        case Drill::Outcome::Ignored:
            return;
        case Drill::Outcome::Started:
            break;
        case Drill::Outcome::Correct:
            tone.requestCue (ToneEngine::Cue::Correct, drill.card().midi);
            break;
        case Drill::Outcome::Wrong:
            // Once the answer is on screen, let the ear hear it too.
            tone.requestCue (drill.answerRevealed() ? ToneEngine::Cue::Reveal : ToneEngine::Cue::Wrong,
                             drill.card().midi);
            break;
        case Drill::Outcome::Timeout:
            tone.requestCue (ToneEngine::Cue::Reveal, drill.card().midi);
            break;
    }

    lastOutcome_  = o;
    lastOutcomeAt = now;
    if (o != Drill::Outcome::Started)
        storeStats();
}

void FlashRollAudioProcessor::submitKey (int midi)
{
    const double now = nowMs();
    handleOutcome (drill.submit (midi, now), now);
}

void FlashRollAudioProcessor::setDrillSettings (const DrillSettings& s)
{
    writeSettings (s);
    drill.setSettings (s, nowMs());
}

void FlashRollAudioProcessor::startDrill()
{
    drill.resetSession();
    drill.start (nowMs());
    lastOutcome_ = Drill::Outcome::Started;
    lastOutcomeAt = nowMs();
}

void FlashRollAudioProcessor::stopDrill()
{
    drill.stop();
}

void FlashRollAudioProcessor::resetStats()
{
    drill.resetStats();
    storeStats();
}

// ---- settings / persistence ---------------------------------------------------------

DrillSettings FlashRollAudioProcessor::readSettings() const
{
    const auto& st = parameters.state;
    DrillSettings s;
    s.clefMode    = static_cast<ClefMode> (juce::jlimit (0, 2, static_cast<int> (st.getProperty (prop::clefMode, 0))));
    s.ledgers     = { static_cast<int> (st.getProperty (prop::ledgersT, 1)),
                      static_cast<int> (st.getProperty (prop::ledgersB, 1)) };
    s.accidentals = static_cast<Accidentals> (juce::jlimit (0, 3, static_cast<int> (st.getProperty (prop::accidentals, 0))));
    s.octaveRule  = static_cast<OctaveRule> (juce::jlimit (0, 1, static_cast<int> (st.getProperty (prop::octaveRule, 0))));
    s.timeLimitMs = static_cast<int> (st.getProperty (prop::timeLimitMs, 0));
    s.flashMs     = static_cast<int> (st.getProperty (prop::flashMs, 0));
    s.retryOnMiss = static_cast<bool> (st.getProperty (prop::retry, true));
    s.adaptive    = static_cast<bool> (st.getProperty (prop::adaptive, true));
    return s;
}

void FlashRollAudioProcessor::writeSettings (const DrillSettings& s)
{
    auto& st = parameters.state;
    st.setProperty (prop::clefMode,    static_cast<int> (s.clefMode), nullptr);
    st.setProperty (prop::ledgersT,    s.ledgers[0], nullptr);
    st.setProperty (prop::ledgersB,    s.ledgers[1], nullptr);
    st.setProperty (prop::accidentals, static_cast<int> (s.accidentals), nullptr);
    st.setProperty (prop::octaveRule,  static_cast<int> (s.octaveRule), nullptr);
    st.setProperty (prop::timeLimitMs, s.timeLimitMs, nullptr);
    st.setProperty (prop::flashMs,     s.flashMs, nullptr);
    st.setProperty (prop::retry,       s.retryOnMiss, nullptr);
    st.setProperty (prop::adaptive,    s.adaptive, nullptr);
}

int FlashRollAudioProcessor::getOctaveOffset() const
{
    return static_cast<int> (parameters.state.getProperty (prop::octaveOffset, 0));
}

void FlashRollAudioProcessor::setOctaveOffset (int o)
{
    parameters.state.setProperty (prop::octaveOffset, juce::jlimit (-2, 0, o), nullptr);
}

bool FlashRollAudioProcessor::getShowHeatmap() const
{
    return static_cast<bool> (parameters.state.getProperty (prop::heatmap, true));
}

void FlashRollAudioProcessor::setShowHeatmap (bool b)
{
    parameters.state.setProperty (prop::heatmap, b, nullptr);
}

void FlashRollAudioProcessor::storeStats()
{
    const juce::ScopedLock sl (statsLock);
    statsText = juce::String (drill.serialiseStats());
    bestStreakCached = drill.bestStreak();
}

void FlashRollAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    {
        const juce::ScopedLock sl (statsLock);
        state.setProperty (prop::stats, statsText, nullptr);
        state.setProperty (prop::bestStreak, bestStreakCached, nullptr);
    }
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FlashRollAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
        {
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
            stateDirty = true;   // applied to the Drill by the timer (message thread)
        }
}

juce::AudioProcessorEditor* FlashRollAudioProcessor::createEditor()
{
    return new FlashRollAudioProcessorEditor (*this);
}

}  // namespace flashroll

// JUCE's plugin entry point lives at file scope and forwards into the namespace.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new flashroll::FlashRollAudioProcessor();
}
