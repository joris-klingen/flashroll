#include "PluginEditor.h"

#include "Cards.h"
#include "NoteSpelling.h"

#include <bitset>

namespace flashroll
{

namespace
{
    constexpr int kWidth  = 1100;
    constexpr int kHeight = 640;
    constexpr int kTitleH = 44;
    constexpr int kLeftW  = 236;
    constexpr int kRightW = 210;
    constexpr int kKeysH  = 118;

    // Choice tables: combo index ⇄ setting value.
    constexpr std::array<int, 6> kTimeLimits { 0, 10000, 5000, 3000, 2000, 1000 };
    constexpr std::array<int, 5> kFlashTimes { 0, 2000, 1000, 500, 250 };

    template <std::size_t N>
    int indexOf (const std::array<int, N>& table, int value)
    {
        for (std::size_t i = 0; i < N; ++i)
            if (table[i] == value) return static_cast<int> (i);
        return 0;
    }

    const juce::Colour kPaneBg     { 0xff222222 };
    const juce::Colour kLabelGrey  { 0xff7a7a7a };
    const juce::Colour kTeal       { 0xff39c6c0 };
    const juce::Colour kAmber      { 0xfff2a93b };
    const juce::Colour kPink       { 0xfff48fb1 };   // soft flamingo pink (family title colour)

    constexpr double kFlashFadeMs = 380.0;

    juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    // Display face for the title and the streak number: Helvetica Neue on the
    // Mac (family look); the default sans elsewhere, where it isn't installed.
    juce::FontOptions displayFont (float size)
    {
       #if JUCE_MAC
        return juce::FontOptions ("Helvetica Neue", size, juce::Font::plain);
       #else
        return juce::FontOptions (size);
       #endif
    }
}

// ---- knob look ----------------------------------------------------------------------

void LevelKnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                             float sliderPos, float startAngle, float endAngle,
                                             juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto  centre = bounds.getCentre();
    const float arcR   = radius - 2.0f;
    const float angle  = startAngle + sliderPos * (endAngle - startAngle);
    const auto  accent = s.findColour (juce::Slider::rotarySliderFillColourId);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.0f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const float bodyR = arcR - 6.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff343434), centre.x, centre.y - bodyR,
                                             juce::Colour (0xff202020), centre.x, centre.y + bodyR, false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (accent.withAlpha (0.5f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -bodyR + 3.0f, 3.0f, bodyR * 0.55f, 1.5f);
    g.setColour (juce::Colours::white);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

// ---- editor -----------------------------------------------------------------------------

FlashRollAudioProcessorEditor::FlashRollAudioProcessorEditor (FlashRollAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      keyboard (p.getKeyboardState())
{
    setUpRow (clefRow,   "CLEF",        { "Treble", "Bass", "Grand staff" });
    setUpRow (cardRow,   "NOTES",       { "Single notes", "Intervals", "Triads", "Seventh chords" });
    setUpRow (trebleRow, "TREBLE RANGE", { "On the staff", "+1 ledger line", "+2 ledger lines",
                                           "+3 ledger lines", "+4 ledger lines" });
    setUpRow (bassRow,   "BASS RANGE",  { "On the staff", "+1 ledger line", "+2 ledger lines",
                                           "+3 ledger lines", "+4 ledger lines" });
    setUpRow (accRow,    "ACCIDENTALS", { "Naturals only", "Sharps", "Flats", "Sharps & flats" });
    setUpRow (octaveRow, "ANSWER",      { "Exact key", "Any octave" });
    setUpRow (timeRow,   "TIME LIMIT",  { "Untimed", "10 s", "5 s", "3 s", "2 s", "1 s" });
    setUpRow (flashRow,  "FLASH",       { "Note stays", "Hide after 2 s", "Hide after 1 s",
                                           "Hide after 0.5 s", "Hide after 0.25 s" });
    setUpRow (namesRow,  "NOTE NAMES",  { "Middle C = C4", "Middle C = C3 (Ableton)" });

    for (auto* t : { &retryToggle, &adaptToggle, &heatmapToggle })
    {
        t->setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        t->setColour (juce::ToggleButton::tickColourId, kTeal);
        t->onClick = [this] { pushSettings(); };
        addAndMakeVisible (*t);
    }

    const auto setUpKnob = [this] (juce::Slider& s, juce::Label& l, const juce::String& text, juce::Colour accent)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setLookAndFeel (&knobLnf);
        s.setColour (juce::Slider::rotarySliderFillColourId, accent);
        addAndMakeVisible (s);
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centred);
        l.setColour (juce::Label::textColourId, juce::Colours::white);
        l.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    setUpKnob (monitorKnob, monitorLabel, "MONITOR", kTeal);
    setUpKnob (cueKnob,     cueLabel,     "CUES",    kAmber);
    monitorAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.getParameters(), FlashRollAudioProcessor::kMonitorLevelId, monitorKnob);
    cueAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        p.getParameters(), FlashRollAudioProcessor::kCueLevelId, cueKnob);

    startButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a6ea5));
    startButton.onClick = [this]
    {
        if (processor.getDrill().hasCard()) processor.stopDrill();
        else                                processor.startDrill();
    };
    resetButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff333333));
    resetButton.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Reset stats",
            "Forget all per-note accuracy and timing history?", "Reset", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) processor.resetStats(); }));
    };
    addAndMakeVisible (startButton);
    addAndMakeVisible (resetButton);

    keyboard.onKeyClicked = [this] (int midi) { processor.submitKey (midi); };
    addAndMakeVisible (keyboard);

    pullSettings();
    setSize (kWidth, kHeight);
    startTimerHz (60);
}

FlashRollAudioProcessorEditor::~FlashRollAudioProcessorEditor()
{
    stopTimer();
    monitorKnob.setLookAndFeel (nullptr);
    cueKnob.setLookAndFeel (nullptr);
}

void FlashRollAudioProcessorEditor::setUpRow (Row& r, const juce::String& title, const juce::StringArray& items)
{
    r.label.setText (title, juce::dontSendNotification);
    r.label.setColour (juce::Label::textColourId, kLabelGrey);
    r.label.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    addAndMakeVisible (r.label);

    r.box.addItemList (items, 1);
    r.box.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2e2e2e));
    r.box.setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3a3a));
    r.box.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    r.box.setColour (juce::ComboBox::arrowColourId, kTeal);
    r.box.onChange = [this] { pushSettings(); };
    addAndMakeVisible (r.box);
}

// ---- settings ------------------------------------------------------------------------------

void FlashRollAudioProcessorEditor::pullSettings()
{
    const juce::ScopedValueSetter<bool> guard (updatingWidgets, true);
    const auto& s = processor.getDrill().getSettings();

    clefRow.box.setSelectedItemIndex   (static_cast<int> (s.clefMode), juce::dontSendNotification);
    cardRow.box.setSelectedItemIndex   (static_cast<int> (s.cardType), juce::dontSendNotification);
    trebleRow.box.setSelectedItemIndex (s.ledgers[0], juce::dontSendNotification);
    bassRow.box.setSelectedItemIndex   (s.ledgers[1], juce::dontSendNotification);
    accRow.box.setSelectedItemIndex    (static_cast<int> (s.accidentals), juce::dontSendNotification);
    octaveRow.box.setSelectedItemIndex (static_cast<int> (s.octaveRule), juce::dontSendNotification);
    timeRow.box.setSelectedItemIndex   (indexOf (kTimeLimits, s.timeLimitMs), juce::dontSendNotification);
    flashRow.box.setSelectedItemIndex  (indexOf (kFlashTimes, s.flashMs), juce::dontSendNotification);
    namesRow.box.setSelectedItemIndex  (processor.getOctaveOffset() < 0 ? 1 : 0, juce::dontSendNotification);
    retryToggle.setToggleState   (s.retryOnMiss, juce::dontSendNotification);
    adaptToggle.setToggleState   (s.adaptive, juce::dontSendNotification);
    heatmapToggle.setToggleState (processor.getShowHeatmap(), juce::dontSendNotification);

    trebleRow.box.setEnabled (s.clefMode != ClefMode::Bass);
    bassRow.box.setEnabled   (s.clefMode != ClefMode::Treble);
    keyboard.setOctaveForMiddleC (4 + processor.getOctaveOffset());
    keyboard.setHeatmapVisible (processor.getShowHeatmap());
}

void FlashRollAudioProcessorEditor::pushSettings()
{
    if (updatingWidgets)
        return;

    DrillSettings s;
    s.clefMode    = static_cast<ClefMode> (std::max (0, clefRow.box.getSelectedItemIndex()));
    s.cardType    = static_cast<CardType> (std::max (0, cardRow.box.getSelectedItemIndex()));
    s.ledgers     = { std::max (0, trebleRow.box.getSelectedItemIndex()),
                      std::max (0, bassRow.box.getSelectedItemIndex()) };
    s.accidentals = static_cast<Accidentals> (std::max (0, accRow.box.getSelectedItemIndex()));
    s.octaveRule  = static_cast<OctaveRule> (std::max (0, octaveRow.box.getSelectedItemIndex()));
    s.timeLimitMs = kTimeLimits[static_cast<std::size_t> (juce::jlimit (0, 5, timeRow.box.getSelectedItemIndex()))];
    s.flashMs     = kFlashTimes[static_cast<std::size_t> (juce::jlimit (0, 4, flashRow.box.getSelectedItemIndex()))];
    s.retryOnMiss = retryToggle.getToggleState();
    s.adaptive    = adaptToggle.getToggleState();

    processor.setDrillSettings (s);
    processor.setOctaveOffset (namesRow.box.getSelectedItemIndex() == 1 ? -1 : 0);
    processor.setShowHeatmap (heatmapToggle.getToggleState());
    pullSettings();   // refresh enable states / keyboard naming
}

// ---- per-frame -----------------------------------------------------------------------------------

void FlashRollAudioProcessorEditor::timerCallback()
{
    // State restored by the host after the editor opened → reflect it.
    const auto& s = processor.getDrill().getSettings();
    if (static_cast<int> (s.clefMode) != clefRow.box.getSelectedItemIndex()
        || static_cast<int> (s.cardType) != cardRow.box.getSelectedItemIndex()
        || s.ledgers[0] != trebleRow.box.getSelectedItemIndex()
        || s.ledgers[1] != bassRow.box.getSelectedItemIndex()
        || static_cast<int> (s.accidentals) != accRow.box.getSelectedItemIndex()
        || static_cast<int> (s.octaveRule) != octaveRow.box.getSelectedItemIndex()
        || s.timeLimitMs != kTimeLimits[static_cast<std::size_t> (juce::jmax (0, timeRow.box.getSelectedItemIndex()))]
        || s.flashMs != kFlashTimes[static_cast<std::size_t> (juce::jmax (0, flashRow.box.getSelectedItemIndex()))]
        || s.retryOnMiss != retryToggle.getToggleState() || s.adaptive != adaptToggle.getToggleState())
        pullSettings();

    const double now = FlashRollAudioProcessor::nowMs();
    updateKeyboard (now);
    startButton.setButtonText (processor.getDrill().hasCard() ? "Stop" : "Start");
    repaint (staffArea.getUnion (timerArea).getUnion (statsArea));
}

StaffScene FlashRollAudioProcessorEditor::buildScene (double now) const
{
    const auto& d = processor.getDrill();
    const auto& s = d.getSettings();
    const int   octaveOffset = processor.getOctaveOffset();

    StaffScene scene;
    scene.layout  = s.clefMode;
    scene.ledgers = s.ledgers;

    if (! d.hasCard())
    {
        scene.prompt = "Play any key to start";
        return scene;
    }

    scene.hasNote     = true;
    scene.card        = d.card();
    scene.noteVisible = d.noteVisible (now);

    if (d.phase() == Drill::Phase::Solved)  scene.mark = StaffScene::Mark::Correct;
    else if (d.answerRevealed())            scene.mark = StaffScene::Mark::Revealed;
    else if (d.missedThisCard())            scene.mark = StaffScene::Mark::Wrong;

    const auto outcome = processor.lastOutcome();
    if (outcome == Drill::Outcome::Correct || outcome == Drill::Outcome::Wrong || outcome == Drill::Outcome::Timeout)
        scene.flash = static_cast<float> (juce::jlimit (0.0, 1.0, 1.0 - (now - processor.lastOutcomeAtMs()) / kFlashFadeMs));

    if (d.answerRevealed())
        scene.answerLabel = utf8 (cards::label (scene.card, octaveOffset));

    for (int i = 0; i < scene.card.size; ++i)
        scene.found[static_cast<std::size_t> (i)] = scene.card.isChord() && d.toneFound (i);

    if (d.phase() != Drill::Phase::Solved && d.lastWrongMidi() >= 0)
    {
        int ghost = d.lastWrongMidi();
        // In any-octave mode, draw the wrong key in the target's octave so it
        // lands next to the note instead of a dozen ledger lines away.
        if (s.octaveRule == OctaveRule::AnyOctave)
            while (std::abs (ghost - scene.card.bottomMidi()) > 6)
                ghost += ghost < scene.card.bottomMidi() ? 12 : -12;
        scene.ghostMidi  = ghost;
        scene.ghostLabel = utf8 (spelling::name (spelling::fromMidi (ghost, scene.card.leansFlat()), octaveOffset));
    }
    return scene;
}

void FlashRollAudioProcessorEditor::updateKeyboard (double /*now*/)
{
    const auto& d = processor.getDrill();

    int lo = 127, hi = 0;
    for (const auto& c : d.candidates())
    {
        lo = std::min (lo, c.midis[0]);
        hi = std::max (hi, c.midis[static_cast<std::size_t> (c.size - 1)]);
    }
    if (lo <= hi)
        keyboard.showRange (lo, hi);

    for (int m = 0; m < 128; ++m)
    {
        const auto st = d.statsForKey (m);
        keyboard.setHeat (m, st.attempts > 0 ? static_cast<float> (st.accuracy()) : -1.0f);
    }

    keyboard.clearTints();
    if (! d.hasCard())
        return;
    const auto& card = d.card();
    for (int i = 0; i < card.size; ++i)
    {
        const int m = card.midis[static_cast<std::size_t> (i)];
        if (d.phase() == Drill::Phase::Solved)
            keyboard.setTint (m, TrainerKeyboard::Tint::Correct);
        else if (d.answerRevealed())
            keyboard.setTint (m, TrainerKeyboard::Tint::Target);
    }
    // Chord tones already held light up green while the rest are still owed
    // (in any-octave mode that's whatever key was actually pressed).
    if (card.isChord() && d.phase() == Drill::Phase::Prompt)
        for (int m = 0; m < 128; ++m)
            if (d.pressedOnCard (m))
                keyboard.setTint (m, TrainerKeyboard::Tint::Correct);
    if (d.phase() != Drill::Phase::Solved && d.lastWrongMidi() >= 0)
        keyboard.setTint (d.lastWrongMidi(), TrainerKeyboard::Tint::Wrong);
}

// ---- painting --------------------------------------------------------------------------------------

void FlashRollAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181818));
    const double now = FlashRollAudioProcessor::nowMs();

    // Title strip.
    auto title = getLocalBounds().removeFromTop (kTitleH).withTrimmedLeft (kLeftW + 8);
    g.setColour (kPink);
    g.setFont (displayFont (24.0f));
    g.drawText ("FlashRoll", title.removeFromLeft (130), juce::Justification::centredLeft);
    g.setColour (kLabelGrey);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("read the note, play the key", title, juce::Justification::centredLeft);

    // Side panes.
    g.setColour (kPaneBg);
    g.fillRect (getLocalBounds().removeFromLeft (kLeftW));
    g.setColour (juce::Colour (0xff2c2c2c));
    g.fillRect (kLeftW - 1, 0, 1, getHeight());

    staff.paint (g, staffArea.toFloat(), buildScene (now));
    paintTimerBar (g, timerArea, now);
    paintStats (g, statsArea);
}

void FlashRollAudioProcessorEditor::paintTimerBar (juce::Graphics& g, juce::Rectangle<int> r, double now) const
{
    const auto& d = processor.getDrill();
    if (d.getSettings().timeLimitMs <= 0 || ! d.hasCard())
        return;

    const float left = d.timeLeft (now);
    const auto rf = r.toFloat();
    g.setColour (juce::Colour (0xff2a2a2a));
    g.fillRoundedRectangle (rf, rf.getHeight() * 0.5f);
    g.setColour (left > 0.3f ? kTeal : StaffRenderer::wrong());
    g.fillRoundedRectangle (rf.withWidth (rf.getWidth() * left), rf.getHeight() * 0.5f);
}

void FlashRollAudioProcessorEditor::paintStats (juce::Graphics& g, juce::Rectangle<int> r) const
{
    const auto& d = processor.getDrill();

    g.setColour (kPaneBg);
    g.fillRoundedRectangle (r.toFloat(), 6.0f);
    r.reduce (14, 10);

    // Big streak number.
    g.setColour (d.streak() > 0 ? StaffRenderer::correct() : juce::Colours::white);
    g.setFont (displayFont (52.0f));
    g.drawText (juce::String (d.streak()), r.removeFromTop (58), juce::Justification::centred);
    g.setColour (kLabelGrey);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("STREAK", r.removeFromTop (16), juce::Justification::centred);
    r.removeFromTop (12);

    const auto row = [&] (const juce::String& k, const juce::String& v)
    {
        auto line = r.removeFromTop (22);
        g.setColour (kLabelGrey);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (k, line, juce::Justification::centredLeft);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (v, line, juce::Justification::centredRight);
    };

    const int n = d.cardsAnswered(), k = d.cardsCorrect();
    row ("ACCURACY", n ? juce::String (juce::roundToInt (100.0 * k / n)) + "%  (" + juce::String (k) + "/" + juce::String (n) + ")"
                       : juce::String ("-"));
    row ("AVG TIME", k ? juce::String (d.sessionAvgMs() / 1000.0, 2) + " s" : juce::String ("-"));
    row ("BEST STREAK", juce::String (d.bestStreak()));

    // Lifetime weak spots: the three least accurate notes in the current pool
    // with enough attempts to mean something.
    r.removeFromTop (14);
    g.setColour (kLabelGrey);
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.drawText ("NEEDS WORK", r.removeFromTop (18), juce::Justification::centredLeft);

    struct Weak { Clef clef; Spelling spelling; double acc; };
    std::vector<Weak> weak;
    std::array<std::bitset<128>, kNumClefs> listed;   // one row per (clef, key)
    for (const auto& c : d.candidates())
        for (int i = 0; i < c.size; ++i)
        {
            const int m = c.midis[static_cast<std::size_t> (i)];
            auto& seen = listed[static_cast<std::size_t> (c.clef)];
            if (seen[static_cast<std::size_t> (m)])
                continue;
            if (const auto& st = d.stats (c.clef, m); st.attempts >= 3 && st.accuracy() < 0.9)
            {
                seen.set (static_cast<std::size_t> (m));
                weak.push_back ({ c.clef, c.notes[static_cast<std::size_t> (i)], st.accuracy() });
            }
        }
    std::sort (weak.begin(), weak.end(), [] (const Weak& a, const Weak& b) { return a.acc < b.acc; });

    if (weak.empty())
    {
        g.setColour (juce::Colour (0xff5a5a5a));
        g.setFont (juce::FontOptions (11.5f));
        g.drawText ("nothing yet", r.removeFromTop (20), juce::Justification::centredLeft);
    }
    for (std::size_t i = 0; i < std::min<std::size_t> (3, weak.size()); ++i)
    {
        const auto& w = weak[i];
        const auto name = utf8 (spelling::name (w.spelling, processor.getOctaveOffset()))
                        + (w.clef == Clef::Treble ? "  treble" : "  bass");
        row (name, juce::String (juce::roundToInt (w.acc * 100.0)) + "%");
    }
}

void FlashRollAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Left pane: settings rows, toggles, level knobs.
    auto left = area.removeFromLeft (kLeftW).reduced (14, 12);
    {
        auto brand = left.removeFromTop (26);
        juce::ignoreUnused (brand);   // aligns the first row under the title strip
    }
    for (auto* row : { &clefRow, &cardRow, &trebleRow, &bassRow, &accRow, &octaveRow, &timeRow, &flashRow, &namesRow })
    {
        row->label.setBounds (left.removeFromTop (16));
        row->box.setBounds (left.removeFromTop (24));
        left.removeFromTop (7);
    }
    left.removeFromTop (4);
    for (auto* t : { &retryToggle, &adaptToggle, &heatmapToggle })
        t->setBounds (left.removeFromTop (22));

    left.removeFromTop (6);
    auto knobs = left.removeFromTop (84);
    auto k1 = knobs.removeFromLeft (knobs.getWidth() / 2);
    monitorLabel.setBounds (k1.removeFromBottom (16));
    monitorKnob.setBounds (k1.withSizeKeepingCentre (64, 64));
    cueLabel.setBounds (knobs.removeFromBottom (16));
    cueKnob.setBounds (knobs.withSizeKeepingCentre (64, 64));

    // Keyboard along the bottom of the main area.
    area.removeFromTop (kTitleH);
    keyboard.setBounds (area.removeFromBottom (kKeysH).reduced (12, 10));

    // Right pane: stats + buttons.
    auto right = area.removeFromRight (kRightW).reduced (0, 4).withTrimmedRight (12);
    auto buttons = right.removeFromBottom (30);
    startButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
    buttons.removeFromLeft (8);
    resetButton.setBounds (buttons);
    right.removeFromBottom (8);
    statsArea = right;

    // Centre: staff + timer bar.
    auto centre = area.reduced (12, 4);
    timerArea = centre.removeFromBottom (8);
    centre.removeFromBottom (6);
    staffArea = centre;
}

}  // namespace flashroll
