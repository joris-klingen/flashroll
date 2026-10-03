#include "TrainerKeyboard.h"

#include "StaffRenderer.h"

namespace flashroll
{

namespace
{
    bool isBlack (int midi) noexcept
    {
        const int pc = midi % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }
}

TrainerKeyboard::TrainerKeyboard (juce::MidiKeyboardState& keyState)
    : juce::MidiKeyboardComponent (keyState, juce::KeyboardComponentBase::horizontalKeyboard)
{
    heat.fill (-1.0f);
    tints.fill (Tint::None);

    setScrollButtonsVisible (false);
    setWantsKeyboardFocus (false);   // no QWERTY-as-piano: the drill is about reading, not typing
    setColour (whiteNoteColourId, juce::Colour (0xfff4f1ea));
    setColour (blackNoteColourId, juce::Colour (0xff1e1e1e));
    setColour (keySeparatorLineColourId, juce::Colour (0xff9a9a9a));
    setColour (keyDownOverlayColourId, juce::Colour (0xff39c6c0).withAlpha (0.75f));
    setColour (mouseOverKeyOverlayColourId, juce::Colour (0xff39c6c0).withAlpha (0.18f));
    setColour (textLabelColourId, juce::Colour (0xff6a6a6a));
    setColour (shadowColourId, juce::Colours::transparentBlack);
}

void TrainerKeyboard::setHeat (int midi, float accuracy) noexcept
{
    if (midi < 0 || midi > 127 || juce::exactlyEqual (heat[static_cast<std::size_t> (midi)], accuracy))
        return;
    heat[static_cast<std::size_t> (midi)] = accuracy;
    if (heatmapVisible)
        repaint();
}

void TrainerKeyboard::setHeatmapVisible (bool b) noexcept
{
    if (heatmapVisible != b)
    {
        heatmapVisible = b;
        repaint();
    }
}

void TrainerKeyboard::setTint (int midi, Tint t) noexcept
{
    if (midi < 0 || midi > 127 || tints[static_cast<std::size_t> (midi)] == t)
        return;
    tints[static_cast<std::size_t> (midi)] = t;
    repaint();
}

void TrainerKeyboard::clearTints() noexcept
{
    bool any = false;
    for (auto& t : tints)
    {
        any |= t != Tint::None;
        t = Tint::None;
    }
    if (any)
        repaint();
}

void TrainerKeyboard::showRange (int lo, int hi)
{
    lo = juce::jlimit (0, 127, lo - (lo % 12));                 // down to a C
    hi = juce::jlimit (0, 127, hi + (11 - (hi % 12)));          // up to a B
    while (hi - lo + 1 < 36)                                    // at least three octaves
    {
        if (lo >= 12)       lo -= 12;
        if (hi - lo + 1 < 36 && hi <= 115) hi += 12;
        if (lo < 12 && hi > 115) break;
    }
    if (lo == rangeLo && hi == rangeHi)
        return;
    rangeLo = lo;
    rangeHi = hi;
    fitKeys();
}

void TrainerKeyboard::resized()
{
    fitKeys();
    juce::MidiKeyboardComponent::resized();
}

void TrainerKeyboard::fitKeys()
{
    int whites = 0;
    for (int n = rangeLo; n <= rangeHi; ++n)
        whites += isBlack (n) ? 0 : 1;

    setAvailableRange (rangeLo, rangeHi);
    if (whites > 0 && getWidth() > 0)
        setKeyWidth (static_cast<float> (getWidth()) / static_cast<float> (whites));
    setLowestVisibleKey (rangeLo);
}

bool TrainerKeyboard::mouseDownOnKey (int midi, const juce::MouseEvent&)
{
    if (onKeyClicked)
        onKeyClicked (midi);
    return true;   // still sound it (monitor) through the keyboard state
}

bool TrainerKeyboard::mouseDraggedToKey (int, const juce::MouseEvent&)
{
    return false;  // no glissando answers: one click = one key
}

juce::Colour TrainerKeyboard::tintColour (int midi) const noexcept
{
    switch (tints[static_cast<std::size_t> (midi)])
    {
        case Tint::Target:  return StaffRenderer::reveal();
        case Tint::Wrong:   return StaffRenderer::wrong();
        case Tint::Correct: return StaffRenderer::correct();
        case Tint::None:    break;
    }
    return juce::Colours::transparentBlack;
}

juce::Colour TrainerKeyboard::heatColour (int midi) const noexcept
{
    const float a = heat[static_cast<std::size_t> (midi)];
    if (! heatmapVisible || a < 0.0f)
        return juce::Colours::transparentBlack;
    // Red (weak) → amber → green (solid), as a soft wash.
    const auto c = a < 0.5f ? StaffRenderer::wrong().interpolatedWith (StaffRenderer::reveal(), a * 2.0f)
                            : StaffRenderer::reveal().interpolatedWith (StaffRenderer::correct(), (a - 0.5f) * 2.0f);
    return c.withAlpha (0.38f);
}

void TrainerKeyboard::drawWhiteNote (int midi, juce::Graphics& g, juce::Rectangle<float> area,
                                     bool isDown, bool isOver, juce::Colour lineColour, juce::Colour textColour)
{
    // Heat as a strip along the bottom of the key; tints fill the whole key.
    if (const auto h = heatColour (midi); ! h.isTransparent())
    {
        g.setColour (h);
        g.fillRect (area.withTrimmedTop (area.getHeight() * 0.62f));
    }
    if (const auto t = tintColour (midi); ! t.isTransparent())
    {
        g.setColour (t.withAlpha (0.85f));
        g.fillRect (area.reduced (1.0f, 0.0f));
    }
    juce::MidiKeyboardComponent::drawWhiteNote (midi, g, area, isDown, isOver, lineColour, textColour);
}

void TrainerKeyboard::drawBlackNote (int midi, juce::Graphics& g, juce::Rectangle<float> area,
                                     bool isDown, bool isOver, juce::Colour noteFillColour)
{
    juce::MidiKeyboardComponent::drawBlackNote (midi, g, area, isDown, isOver, noteFillColour);

    if (const auto h = heatColour (midi); ! h.isTransparent())
    {
        g.setColour (h.withAlpha (0.7f));
        g.fillRect (area.reduced (area.getWidth() * 0.18f, 0.0f)
                        .withTrimmedTop (area.getHeight() * 0.7f).withTrimmedBottom (area.getHeight() * 0.06f));
    }
    if (const auto t = tintColour (midi); ! t.isTransparent())
    {
        g.setColour (t);
        g.fillRect (area.reduced (area.getWidth() * 0.12f, 0.0f).withTrimmedBottom (area.getHeight() * 0.04f));
    }
}

}  // namespace flashroll
