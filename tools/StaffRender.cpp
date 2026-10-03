// staff-render — draws a contact sheet of staff scenes through the REAL
// StaffRenderer into a PNG, so engraving / layout changes can be eyeballed
// without launching the plugin in a host.
//
//   staff-render [out.png]     (default: previews/staff-sheet.png)

#include <juce_gui_basics/juce_gui_basics.h>

#include "Cards.h"
#include "NoteSpelling.h"
#include "StaffRenderer.h"

using namespace flashroll;

namespace
{
    Card cardFor (Clef clef, int letter, int acc, int octave)
    {
        return Card::single (clef, { letter, acc, octave });
    }

    Card chordFor (CardType type, const char* longName, Clef clef, int letter, int acc, int octave)
    {
        for (const auto& q : qualitiesFor (type))
            if (std::string (q.longName) == longName)
            {
                Card c;
                if (cards::build (q, { letter, acc, octave }, clef, Accidentals::Mixed, c))
                    return c;
            }
        std::printf ("warning: couldn't build %s\n", longName);
        return cardFor (clef, letter, acc, octave);
    }

    juce::String label (const Card& c)
    {
        return juce::String::fromUTF8 (cards::label (c).c_str());
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::File out = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                    : juce::File::getCurrentWorkingDirectory().getChildFile ("previews/staff-sheet.png");

    StaffRenderer renderer;
    if (! renderer.hasMusicFont())
        std::printf ("warning: music font failed to load — drawing fallback glyphs\n");

    struct Cell { const char* title; StaffScene scene; };
    std::vector<Cell> cells;

    const auto add = [&] (const char* title, StaffScene s) { cells.push_back ({ title, std::move (s) }); };

    StaffScene s;
    s.hasNote = true;

    s.layout = ClefMode::Treble; s.card = cardFor (Clef::Treble, 0, 0, 4);  // middle C
    add ("treble: middle C", s);

    s.card = cardFor (Clef::Treble, 3, +1, 5);                                // F#5 top line
    add ("treble: F#5", s);

    s.card = cardFor (Clef::Treble, 0, 0, 6); s.ledgers = { 4, 4 };           // C6, 2 ledgers above
    add ("treble +4 ledgers: C6", s);

    s.ledgers = { 1, 1 };
    s.layout = ClefMode::Bass; s.card = cardFor (Clef::Bass, 6, -1, 2);      // Bb2
    add ("bass: Bb2", s);

    s.card = cardFor (Clef::Bass, 4, 0, 2); s.mark = StaffScene::Mark::Correct; s.flash = 1.0f;
    add ("bass: G2 correct", s);

    s.mark = StaffScene::Mark::Wrong; s.flash = 1.0f; s.ghostMidi = 45; s.ghostLabel = "A2";
    s.card = cardFor (Clef::Bass, 3, 0, 3);
    add ("bass: F3, played A2", s);

    s.layout = ClefMode::Treble; s.card = cardFor (Clef::Treble, 4, 0, 4);
    s.ghostMidi = 24; s.ghostLabel = "C1";
    add ("treble: G4, played C1 (far)", s);

    s.layout = ClefMode::Grand; s.mark = StaffScene::Mark::Revealed; s.flash = 0.5f;
    s.ghostMidi = -1; s.ghostLabel = {};
    s.card = cardFor (Clef::Treble, 1, 0, 4); s.answerLabel = label (s.card);
    add ("grand: D4 revealed", s);

    s.mark = StaffScene::Mark::Neutral; s.flash = 0.0f; s.answerLabel = {};
    s.card = cardFor (Clef::Bass, 0, 0, 4); s.ledgers = { 2, 2 };
    add ("grand +2: middle C (bass)", s);

    // Chords: stacked thirds, a second (shifted head), accidental columns,
    // tones already found in green, a revealed seventh with its label.
    s.layout = ClefMode::Treble; s.ledgers = { 1, 1 }; s.mark = StaffScene::Mark::Neutral; s.flash = 0.0f;
    s.card = chordFor (CardType::Triads, "major", Clef::Treble, 0, 0, 4);          // C E G
    s.found = { true, false, true, false };
    add ("triad: C major, C + G found", s);

    s.found = {};
    s.card = chordFor (CardType::Intervals, "major second", Clef::Treble, 3, 0, 4);  // F G
    add ("interval: M2 (shifted head)", s);

    s.card = chordFor (CardType::Sevenths, "dominant seventh", Clef::Treble, 1, 0, 4);  // D F# A C
    s.mark = StaffScene::Mark::Revealed; s.answerLabel = label (s.card);
    add ("D7 revealed", s);

    s.layout = ClefMode::Bass; s.mark = StaffScene::Mark::Neutral; s.answerLabel = {};
    s.card = chordFor (CardType::Sevenths, "diminished seventh", Clef::Bass, 0, +1, 3);  // C# E G Bb
    add ("bass: C#dim7 (accidental columns)", s);

    s.card = chordFor (CardType::Triads, "minor", Clef::Bass, 6, -1, 2);           // Bb Db F
    s.mark = StaffScene::Mark::Wrong; s.flash = 1.0f; s.ghostMidi = 50; s.ghostLabel = "D3";
    add ("bass: Bbm, played D3", s);

    s.ghostMidi = -1; s.ghostLabel = {}; s.mark = StaffScene::Mark::Neutral; s.flash = 0.0f;
    s.hasNote = false; s.prompt = "Play any key to start";
    add ("idle", s);

    const int cw = 520, ch = 300, cols = 3;
    const int rows = (static_cast<int> (cells.size()) + cols - 1) / cols;
    juce::Image img (juce::Image::ARGB, cw * cols, ch * rows, true);
    juce::Graphics g (img);
    g.fillAll (juce::Colour (0xff181818));

    for (int i = 0; i < static_cast<int> (cells.size()); ++i)
    {
        const auto r = juce::Rectangle<float> (float ((i % cols) * cw), float ((i / cols) * ch),
                                               float (cw), float (ch)).reduced (8.0f);
        renderer.paint (g, r.withTrimmedTop (18.0f), cells[static_cast<std::size_t> (i)].scene);
        g.setColour (juce::Colour (0xff7a7a7a));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (cells[static_cast<std::size_t> (i)].title, r.withHeight (16.0f), juce::Justification::centredLeft);
    }

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (img, stream))
    {
        std::printf ("error: couldn't write %s\n", out.getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf ("wrote %s\n", out.getFullPathName().toRawUTF8());
    return renderer.hasMusicFont() ? 0 : 2;
}
