#pragma once

#include <juce_graphics/juce_graphics.h>

#include "Drill.h"

namespace flashroll
{

// Everything the staff needs to draw one moment of the drill. Built by the
// editor from the processor's Drill each frame (and by the staff-render tool
// offline), so the renderer itself holds no drill state.
struct StaffScene
{
    enum class Mark { Neutral, Correct, Wrong, Revealed };

    ClefMode layout = ClefMode::Treble;        // Treble / Bass = one staff; Grand = both
    std::array<int, kNumClefs> ledgers { 1, 1 }; // range in use — sizes the staff to fit

    bool  hasNote = false;
    bool  noteVisible = true;                  // false while flash-hidden
    Card  card;
    Mark  mark = Mark::Neutral;
    std::array<bool, kMaxChordNotes> found {}; // chord tones already pressed (drawn green)
    int   ghostMidi = -1;                      // the wrong key, drawn as a red ghost
    float flash = 0.0f;                        // 0..1 background pulse (green/red)
    juce::String answerLabel;                  // drawn by the card when revealed (e.g. "F♯4", "Am · A C E")
    juce::String ghostLabel;                   // drawn by the ghost
    juce::String prompt;                       // centred message when there's no card
};

// Paints a single or grand staff with the Bravura (SMuFL) music font embedded
// as binary data: clefs, whole-note heads, accidentals and ledger lines all
// come from real engraving glyphs. A JUCE-only class (no editor, no
// processor) so the staff-render tool draws the identical picture headless.
class StaffRenderer
{
public:
    StaffRenderer();

    void paint (juce::Graphics&, juce::Rectangle<float> area, const StaffScene&) const;

    bool hasMusicFont() const noexcept { return fontOk; }

    // Palette shared with the editor.
    static juce::Colour background()  { return juce::Colour (0xff1c1c1c); }
    static juce::Colour ink()         { return juce::Colour (0xffe8e8e8); }
    static juce::Colour lineInk()     { return juce::Colour (0xff8a8a8a); }
    static juce::Colour correct()     { return juce::Colour (0xff5bd27a); }
    static juce::Colour wrong()       { return juce::Colour (0xffff5a5a); }
    static juce::Colour reveal()      { return juce::Colour (0xfff2a93b); }

private:
    // SMuFL code points.
    enum Glyph : juce::juce_wchar
    {
        gClef = 0xE050, fClef = 0xE062,
        noteheadWhole = 0xE0A2, noteheadBlack = 0xE0A4,
        accFlat = 0xE260, accNatural = 0xE261, accSharp = 0xE262,
    };

    // Glyph outline in STAFF SPACES, origin at the SMuFL origin (baseline).
    juce::Path glyph (juce::juce_wchar) const;

    struct StaffGeom
    {
        Clef  clef;
        float bottomLineY;   // y of step 0
        float space;         // px per staff space (2 steps)
        float stepY (int step) const noexcept { return bottomLineY - step * space * 0.5f; }
    };

    void drawStaff (juce::Graphics&, const StaffGeom&, float x0, float x1) const;
    // A card's noteheads (single, interval or chord) centred on `x`, with
    // ledger lines, accidentals and an optional label pill.
    void drawCard (juce::Graphics&, const StaffGeom&, float x, const Card&,
                   const std::array<juce::Colour, kMaxChordNotes>& colours,
                   const juce::String& label, juce::Colour labelColour) const;
    void drawPill (juce::Graphics&, float centreX, float topY, float space,
                   const juce::String& text, juce::Colour) const;
    static float pillHeight (float space) noexcept;
    void fillGlyph (juce::Graphics&, juce::juce_wchar, float x, float y, float space) const;

    juce::Typeface::Ptr music;
    bool fontOk = false;
    float unitsPerSpace = 1.0f;   // glyph-path units in one staff space (calibrated)
};

}  // namespace flashroll
