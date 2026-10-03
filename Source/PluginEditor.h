#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "StaffRenderer.h"
#include "TrainerKeyboard.h"

namespace flashroll
{

// Rotary look for the level knobs (same family as hitnotedmx's dim knobs): a
// dark body, coloured value arc read from rotarySliderFillColourId, white pointer.
class LevelKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
};

// The editor: settings pane (left), the flashing staff (centre), score pane
// (right), and the trainer keyboard along the bottom. A 60 Hz timer repaints
// the staff from the processor's Drill — all on the message thread.
class FlashRollAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit FlashRollAudioProcessorEditor (FlashRollAudioProcessor&);
    ~FlashRollAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Settings widgets → processor (and back on open).
    void pushSettings();
    void pullSettings();

    StaffScene buildScene (double now) const;
    void updateKeyboard (double now);
    void paintStats (juce::Graphics&, juce::Rectangle<int>) const;
    void paintTimerBar (juce::Graphics&, juce::Rectangle<int>, double now) const;

    // A labelled combo row in the settings pane.
    struct Row
    {
        juce::Label    label;
        juce::ComboBox box;
    };
    void setUpRow (Row&, const juce::String& title, const juce::StringArray& items);

    FlashRollAudioProcessor& processor;
    StaffRenderer staff;

    Row clefRow, cardRow, trebleRow, bassRow, accRow, octaveRow, timeRow, flashRow, namesRow;
    juce::ToggleButton retryToggle   { "Retry until right" };
    juce::ToggleButton adaptToggle   { "Adaptive (drill weak notes)" };
    juce::ToggleButton heatmapToggle { "Accuracy heatmap" };

    LevelKnobLookAndFeel knobLnf;
    juce::Slider monitorKnob, cueKnob;
    juce::Label  monitorLabel, cueLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> monitorAttach, cueAttach;

    juce::TextButton startButton { "Start" };
    juce::TextButton resetButton { "Reset stats" };

    TrainerKeyboard keyboard;

    juce::Rectangle<int> staffArea, statsArea, timerArea;
    bool updatingWidgets = false;   // guard: pullSettings shouldn't echo back

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FlashRollAudioProcessorEditor)
};

}  // namespace flashroll
