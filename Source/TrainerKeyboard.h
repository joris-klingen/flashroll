#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <functional>

namespace flashroll
{

// The on-screen keyboard: plays like JUCE's MidiKeyboardComponent (clicks go
// through the processor's MidiKeyboardState into the MIDI stream, so they
// answer cards exactly like a real key) and adds the trainer's key tints —
// the revealed answer (amber), the wrong key (red), a correct flash (green),
// and an optional per-key accuracy heatmap underneath.
class TrainerKeyboard : public juce::MidiKeyboardComponent
{
public:
    explicit TrainerKeyboard (juce::MidiKeyboardState& keyState);

    enum class Tint { None, Target, Wrong, Correct };

    // Per-key accuracy, 0..1, or < 0 for "no data". Shown when heatmap is on.
    void setHeat (int midi, float accuracy) noexcept;
    void setHeatmapVisible (bool) noexcept;

    void setTint (int midi, Tint) noexcept;   // -1 / None to clear
    void clearTints() noexcept;

    // Show `lo`..`hi` (widened to whole octaves, min 3) and fit keys to width.
    void showRange (int lo, int hi);

    // Called (message thread) when a key is clicked. Clicks answer the drill
    // directly rather than via the audio thread, so the on-screen keyboard works
    // even when no audio device is running (e.g. a Standalone with no output).
    std::function<void (int midi)> onKeyClicked;

    void resized() override;

private:
    void drawWhiteNote (int midi, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int midi, juce::Graphics&, juce::Rectangle<float> area,
                        bool isDown, bool isOver, juce::Colour noteFillColour) override;

    bool mouseDownOnKey (int midi, const juce::MouseEvent&) override;
    bool mouseDraggedToKey (int midi, const juce::MouseEvent&) override;

    juce::Colour tintColour (int midi) const noexcept;
    juce::Colour heatColour (int midi) const noexcept;
    void fitKeys();

    std::array<float, 128> heat {};
    std::array<Tint, 128>  tints {};
    bool heatmapVisible = true;
    int  rangeLo = 48, rangeHi = 83;
};

}  // namespace flashroll
