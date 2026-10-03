#include "StaffRenderer.h"

#include "BinaryData.h"

namespace flashroll
{

namespace
{
    // Engraving proportions, in staff spaces (Bravura's own defaults are close).
    constexpr float kStaffLineThickness  = 0.13f;
    constexpr float kLedgerThickness     = 0.16f;
    constexpr float kLedgerExtension     = 0.4f;
    constexpr float kAccidentalGap       = 0.25f;
    constexpr float kGhostOffset         = 3.8f;   // ghost note sits this far right of the target
    constexpr float kAccidentalColumn    = 1.15f;  // width of one accidental column (stacked chords)
    constexpr float kStaffWidthSpaces    = 20.0f;
    constexpr int   kGhostSlackSteps     = 4;      // ghost drawn up to 2 ledgers past the drill range
}

StaffRenderer::StaffRenderer()
{
    music = juce::Typeface::createSystemTypefaceFor (BinaryData::Bravura_otf,
                                                     static_cast<size_t> (BinaryData::Bravura_otfSize));
    if (music == nullptr)
        return;

    // Calibrate: SMuFL's black notehead is exactly one staff space tall, so its
    // outline height at any font size gives "path units per staff space".
    unitsPerSpace = 1.0f;
    const auto head = glyph (noteheadBlack);
    if (! head.isEmpty() && head.getBounds().getHeight() > 0.0f)
    {
        unitsPerSpace = head.getBounds().getHeight();
        fontOk = true;
    }
}

juce::Path StaffRenderer::glyph (juce::juce_wchar cp) const
{
    juce::Path p;
    if (music == nullptr)
        return p;

    juce::Font f { juce::FontOptions (music).withHeight (100.0f) };
    juce::GlyphArrangement ga;
    ga.addLineOfText (f, juce::String::charToString (cp), 0.0f, 0.0f);   // y = baseline = SMuFL origin
    ga.createPath (p);
    p.applyTransform (juce::AffineTransform::scale (1.0f / unitsPerSpace));
    return p;
}

void StaffRenderer::fillGlyph (juce::Graphics& g, juce::juce_wchar cp, float x, float y, float space) const
{
    auto p = glyph (cp);
    p.applyTransform (juce::AffineTransform::scale (space).translated (x, y));
    g.fillPath (p);
}

void StaffRenderer::drawStaff (juce::Graphics& g, const StaffGeom& s, float x0, float x1) const
{
    g.setColour (lineInk());
    const float t = std::max (1.0f, kStaffLineThickness * s.space);
    for (int step = 0; step <= 8; step += 2)
        g.fillRect (x0, s.stepY (step) - t * 0.5f, x1 - x0, t);

    // Clef: SMuFL origins sit on the line the clef names — G line (step 2) for
    // the treble clef, F line (step 6) for the bass clef.
    g.setColour (ink());
    const float clefX = x0 + 0.7f * s.space;
    if (fontOk)
    {
        if (s.clef == Clef::Treble) fillGlyph (g, gClef, clefX, s.stepY (2), s.space);
        else                        fillGlyph (g, fClef, clefX, s.stepY (6), s.space);
    }
    else
    {
        g.setFont (juce::FontOptions (s.space * 3.0f, juce::Font::bold));
        g.drawText (s.clef == Clef::Treble ? "G" : "F",
                    juce::Rectangle<float> (clefX, s.stepY (8), s.space * 2.5f, s.space * 4.0f),
                    juce::Justification::centred);
    }
}

void StaffRenderer::drawPill (juce::Graphics& g, float centreX, float topY, float space,
                              const juce::String& text, juce::Colour colour) const
{
    const float fh = juce::jlimit (11.0f, 28.0f, space * 1.25f);
    juce::Font f { juce::FontOptions (fh, juce::Font::bold) };
    const float tw = juce::GlyphArrangement::getStringWidth (f, text) + fh * 0.8f;
    const juce::Rectangle<float> pill (centreX - tw * 0.5f, topY, tw, fh * 1.3f);
    g.setColour (background().withAlpha (0.92f));
    g.fillRoundedRectangle (pill, fh * 0.3f);
    g.setColour (colour);
    g.drawRoundedRectangle (pill, fh * 0.3f, 1.0f);
    g.setFont (f);
    g.drawText (text, pill, juce::Justification::centred);
}

float StaffRenderer::pillHeight (float space) noexcept
{
    return juce::jlimit (11.0f, 28.0f, space * 1.25f) * 1.3f;
}

void StaffRenderer::drawCard (juce::Graphics& g, const StaffGeom& s, float x, const Card& card,
                              const std::array<juce::Colour, kMaxChordNotes>& colours,
                              const juce::String& label, juce::Colour labelColour) const
{
    const int n = card.size;
    std::array<int, kMaxChordNotes> steps {};
    for (int i = 0; i < n; ++i)
        steps[static_cast<std::size_t> (i)] = spelling::staffStep (card.notes[static_cast<std::size_t> (i)], s.clef);
    const int lowStep  = steps[0];
    const int highStep = steps[static_cast<std::size_t> (n - 1)];

    auto head = fontOk ? glyph (noteheadWhole) : juce::Path();
    if (head.isEmpty())
        head.addEllipse (0.0f, -0.5f, 1.6f, 1.0f);
    const auto hb = head.getBounds();
    const float headW = hb.getWidth() * s.space;
    const float left  = x - headW * 0.5f;

    // Seconds can't share a column: walking up, the upper note of a second
    // moves to the right of the stem side (standard engraving), unless the
    // note below it already moved.
    std::array<bool, kMaxChordNotes> shifted {};
    bool anyShifted = false;
    for (int i = 1; i < n; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        shifted[k] = steps[k] - steps[k - 1] == 1 && ! shifted[k - 1];
        anyShifted |= shifted[k];
    }

    // Ledger lines: shared by the whole stack, long enough for a shifted column.
    g.setColour (lineInk());
    const float lt = std::max (1.0f, kLedgerThickness * s.space);
    const float lx = left - kLedgerExtension * s.space;
    const float lw = headW * (anyShifted ? 2.0f : 1.0f) + 2.0f * kLedgerExtension * s.space;
    for (int k = -2; k >= lowStep; k -= 2)   g.fillRect (lx, s.stepY (k) - lt * 0.5f, lw, lt);
    for (int k = 10; k <= highStep; k += 2)  g.fillRect (lx, s.stepY (k) - lt * 0.5f, lw, lt);

    for (int i = 0; i < n; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        auto h = head;
        const float hx = left + (shifted[k] ? headW : 0.0f);
        h.applyTransform (juce::AffineTransform::scale (s.space).translated (hx - hb.getX() * s.space, s.stepY (steps[k])));
        g.setColour (colours[k]);
        g.fillPath (h);
    }

    // Accidentals, top note first, each in the nearest column (leftward) where
    // it clears every accidental already there by at least a sixth (6 steps).
    std::array<std::vector<int>, kMaxChordNotes> columns;
    for (int i = n - 1; i >= 0; --i)
    {
        const auto k  = static_cast<std::size_t> (i);
        const auto& sp = card.notes[k];
        if (sp.accidental == 0)
            continue;

        std::size_t col = 0;
        while (col + 1 < columns.size()
               && std::any_of (columns[col].begin(), columns[col].end(),
                               [&] (int other) { return std::abs (other - steps[k]) < 6; }))
            ++col;
        columns[col].push_back (steps[k]);

        const float y = s.stepY (steps[k]);
        const float colRight = left - kAccidentalGap * s.space - static_cast<float> (col) * kAccidentalColumn * s.space;
        g.setColour (colours[k]);
        if (fontOk)
        {
            auto acc = glyph (sp.accidental > 0 ? accSharp : accFlat);
            const auto ab = acc.getBounds();
            acc.applyTransform (juce::AffineTransform::scale (s.space).translated (colRight - ab.getRight() * s.space, y));
            g.fillPath (acc);
        }
        else
        {
            g.setFont (juce::FontOptions (s.space * 2.0f));
            g.drawText (sp.accidental > 0 ? "#" : "b",
                        juce::Rectangle<float> (colRight - 1.4f * s.space, y - s.space, s.space * 1.4f, s.space * 2.0f),
                        juce::Justification::centredRight);
        }
    }

    // Label: under the stack when it sits high, over it when it sits low.
    if (label.isNotEmpty())
    {
        const float cx = x + (anyShifted ? headW * 0.5f : 0.0f);
        if (lowStep >= 4)
            drawPill (g, cx, s.stepY (lowStep) + 1.6f * s.space, s.space, label, labelColour);
        else
            drawPill (g, cx, s.stepY (highStep) - 1.6f * s.space - pillHeight (s.space), s.space, label, labelColour);
    }
}

void StaffRenderer::paint (juce::Graphics& g, juce::Rectangle<float> area, const StaffScene& scene) const
{
    g.setColour (background());
    g.fillRoundedRectangle (area, 6.0f);

    if (scene.flash > 0.0f && (scene.mark == StaffScene::Mark::Correct || scene.mark == StaffScene::Mark::Wrong
                               || scene.mark == StaffScene::Mark::Revealed))
    {
        const auto c = scene.mark == StaffScene::Mark::Correct ? correct() : wrong();
        g.setColour (c.withAlpha (0.16f * juce::jlimit (0.0f, 1.0f, scene.flash)));
        g.fillRoundedRectangle (area, 6.0f);
    }

    // ---- vertical layout, in "global steps" (bottom staff's bottom line = 0) ----
    const bool grand  = scene.layout == ClefMode::Grand;
    const int  lT     = scene.ledgers[static_cast<std::size_t> (Clef::Treble)];
    const int  lB     = scene.ledgers[static_cast<std::size_t> (Clef::Bass)];
    const Clef single = scene.layout == ClefMode::Bass ? Clef::Bass : Clef::Treble;

    int gapSteps = 0;   // grand: bass top line → treble bottom line
    int lowG = 0, highG = 0;
    if (grand)
    {
        // Wide enough that the treble's lower ledgers and the bass's upper
        // ledgers never reach the other staff.
        const int reach = std::max (-spelling::lowestStep (lT), spelling::highestStep (lB) - 8);
        gapSteps = std::max (8, reach + 3);
        lowG  = spelling::lowestStep (lB);
        highG = 8 + gapSteps + spelling::highestStep (lT);
    }
    else
    {
        const int l = scene.ledgers[static_cast<std::size_t> (single)];
        lowG  = spelling::lowestStep (l);
        highG = spelling::highestStep (l);
    }

    // Pad for labels; never let a tiny range blow the staff up to cartoon size.
    const float spanSpaces = std::max (12.0f, (highG - lowG) * 0.5f + 4.5f);
    const float space = std::min (area.getHeight() / spanSpaces, area.getWidth() / (kStaffWidthSpaces + 2.0f));
    const float midStep = (lowG + highG) * 0.5f;
    const float baseY   = area.getCentreY() + midStep * space * 0.5f;   // y of global step 0

    const float staffW = std::min (area.getWidth() - 2.0f * space, kStaffWidthSpaces * space);
    const float x0 = area.getCentreX() - staffW * 0.5f;
    const float x1 = x0 + staffW;

    StaffGeom bottom { grand ? Clef::Bass : single, baseY, space };
    StaffGeom top    { Clef::Treble, baseY - (8 + gapSteps) * space * 0.5f, space };

    if (grand)
    {
        drawStaff (g, top, x0, x1);
        drawStaff (g, bottom, x0, x1);
        // System barline + a bracket-ish bar binding the two staves.
        g.setColour (lineInk());
        const float yTop = top.stepY (8), yBot = bottom.stepY (0);
        g.fillRect (x0 - 0.5f, yTop, std::max (1.0f, kStaffLineThickness * space), yBot - yTop);
        g.fillRect (x0 - 0.55f * space, yTop, 0.3f * space, yBot - yTop);
    }
    else
    {
        drawStaff (g, bottom, x0, x1);
    }

    const auto geomFor = [&] (Clef c) -> const StaffGeom& { return (grand && c == Clef::Treble) ? top : bottom; };

    if (scene.hasNote)
    {
        const auto& card = scene.card;
        const auto& geom = geomFor (card.clef);
        const float noteX = x0 + staffW * 0.52f;

        if (scene.noteVisible)
        {
            const auto base = scene.mark == StaffScene::Mark::Correct  ? correct()
                            : scene.mark == StaffScene::Mark::Revealed ? reveal()
                                                                        : ink();
            // Chord tones already found glow green while the rest are still owed.
            std::array<juce::Colour, kMaxChordNotes> colours;
            for (std::size_t i = 0; i < colours.size(); ++i)
                colours[i] = (scene.mark != StaffScene::Mark::Correct && scene.found[i]) ? correct() : base;
            drawCard (g, geom, noteX, card, colours, scene.answerLabel, base);
        }
        else
        {
            // Flash-hidden: a faint marker so the eye knows where to keep looking.
            g.setColour (lineInk().withAlpha (0.35f));
            g.setFont (juce::FontOptions (space * 1.6f, juce::Font::bold));
            g.drawText ("?", juce::Rectangle<float> (noteX - space, geom.stepY (4) - space, space * 2.0f, space * 2.0f),
                        juce::Justification::centred);
        }

        if (scene.ghostMidi >= 0)
        {
            // Spell the wrong key the way the card leans (flats stay flats).
            const auto ghost = Card::single (card.clef, spelling::fromMidi (scene.ghostMidi, card.leansFlat()));
            const int  gStep = spelling::staffStep (ghost.bottom(), card.clef);
            const int  l     = scene.ledgers[static_cast<std::size_t> (card.clef)];
            const float gx   = noteX + kGhostOffset * space;

            if (gStep >= spelling::lowestStep (l) - kGhostSlackSteps
                && gStep <= spelling::highestStep (l) + kGhostSlackSteps)
            {
                const auto red = wrong().withAlpha (0.75f);
                drawCard (g, geom, gx, ghost, { red, red, red, red }, scene.ghostLabel, red);
            }
            else
            {
                // Far off this staff (wrong octave by a mile): name it with an
                // arrow instead of drawing a ladder of ledger lines off-screen.
                const auto text = scene.ghostLabel + (gStep < 0 ? juce::String::fromUTF8 (" \xe2\x86\x93")
                                                                : juce::String::fromUTF8 (" \xe2\x86\x91"));
                drawPill (g, gx, geom.stepY (4) - pillHeight (space) * 0.5f, space, text, wrong());
            }
        }
    }

    if (scene.prompt.isNotEmpty())
    {
        const float fh = juce::jlimit (14.0f, 30.0f, space * 1.6f);
        juce::Font f { juce::FontOptions (fh) };
        const float tw = juce::GlyphArrangement::getStringWidth (f, scene.prompt) + fh * 1.6f;
        const juce::Rectangle<float> pill (area.getCentreX() - tw * 0.5f, area.getCentreY() - fh,
                                           tw, fh * 2.0f);
        g.setColour (background().withAlpha (0.9f));
        g.fillRoundedRectangle (pill, fh * 0.5f);
        g.setColour (juce::Colour (0xfff48fb1));
        g.drawRoundedRectangle (pill, fh * 0.5f, 1.2f);
        g.setFont (f);
        g.drawText (scene.prompt, pill, juce::Justification::centred);
    }
}

}  // namespace flashroll
