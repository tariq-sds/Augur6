// Arc test: render audio loops from known notes, convert them back to MIDI and score the
// transcription, then build an arrangement and check its structure and exports.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/Processor.h"
#include "../Source/Editor.h"
#include <cstdio>

static constexpr double SR = 44100.0, BPM = 124.0;
static int failures = 0;
static void check (bool ok, const juce::String& w) { std::printf ("%-66s %s\n", w.toRawUTF8(), ok ? "ok" : "FAIL"); if (! ok) ++failures; }

struct Head : juce::AudioPlayHead
{
    double ppq = 0; bool play = true;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying (play); p.setBpm (BPM); p.setPpqPosition (ppq); return p; }
};

static void writeWav (const juce::File& f, const std::vector<float>& x)
{
    f.deleteFile();
    juce::AudioBuffer<float> b (2, (int) x.size());
    for (int i = 0; i < (int) x.size(); ++i) { b.setSample (0, i, x[(size_t) i]); b.setSample (1, i, x[(size_t) i]); }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (SR).withNumChannels (2).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static double spb() { return 60.0 / BPM; }

// ---- renderers (deliberately different from Arc's preview sounds)
static std::vector<float> renderDrums (const arc::Clip& c)
{
    std::vector<float> x ((size_t) (c.lengthBeats * spb() * SR) + 1, 0.0f);
    juce::Random r (3);
    for (auto& n : c.notes)
    {
        const int s0 = (int) (n.start * spb() * SR);
        for (int i = 0; i < (int) (0.5 * SR) && s0 + i < (int) x.size(); ++i)
        {
            const double t = i / SR; float v = 0;
            if (arc::isKick (n.pitch)) v = (float) (std::sin (2 * juce::MathConstants<double>::pi * (55 * t + 2.0 * (1 - std::exp (-t / 0.03)))) * std::exp (-t / 0.2));
            else if (arc::isSnare (n.pitch)) v = (float) ((r.nextFloat() * 2 - 1) * 0.6 * std::exp (-t / 0.09) + std::sin (2 * 3.14159 * 200 * t) * 0.3 * std::exp (-t / 0.04));
            else { static float hp = 0; const float w = r.nextFloat() * 2 - 1; const float h = w - hp; hp = w; v = h * 0.35f * (float) std::exp (-t / (n.pitch == arc::gm::openHat ? 0.2 : 0.03)); }
            x[(size_t) (s0 + i)] += v * n.velocity;
        }
    }
    return x;
}

static std::vector<float> renderTonal (const arc::Clip& c, bool saw)
{
    std::vector<float> x ((size_t) (c.lengthBeats * spb() * SR) + 1, 0.0f);
    for (auto& n : c.notes)
    {
        const int s0 = (int) (n.start * spb() * SR), len = (int) (n.length * spb() * SR);
        const double f = 440.0 * std::exp2 ((n.pitch - 69) / 12.0);
        double ph = 0; float lp = 0;
        for (int i = 0; i < len + (int) (0.03 * SR) && s0 + i < (int) x.size(); ++i)
        {
            ph += f / SR; if (ph >= 1) ph -= 1;
            const float raw = saw ? (float) (2 * ph - 1) : (float) std::sin (2 * juce::MathConstants<double>::pi * ph) + 0.3f * (float) std::sin (4 * juce::MathConstants<double>::pi * ph);
            lp += (raw - lp) * 0.25f;
            float env = juce::jmin (1.0f, i / (0.004f * (float) SR));
            if (i > len) env *= (float) std::exp (-(i - len) / (0.008 * SR));
            x[(size_t) (s0 + i)] += lp * env * 0.3f * n.velocity;
        }
    }
    return x;
}

static arc::Note N (int p, double s, double l, float v = 0.9f) { arc::Note n; n.pitch = p; n.start = s; n.length = l; n.velocity = v; return n; }

// precision / recall of a transcription (pitch equal, start within a 16th)
static void score (const char* what, const arc::Clip& truth, const arc::Clip& got, float minF)
{
    int hit = 0;
    std::vector<bool> used (got.notes.size(), false);
    for (auto& t : truth.notes)
        for (size_t i = 0; i < got.notes.size(); ++i)
            if (! used[i] && got.notes[i].pitch == t.pitch && std::abs (got.notes[i].start - t.start) < 0.13) { used[i] = true; ++hit; break; }
    const float rec = truth.notes.empty() ? 1 : (float) hit / truth.notes.size();
    const float pre = got.notes.empty() ? 0 : (float) hit / got.notes.size();
    const float f = rec + pre > 0 ? 2 * rec * pre / (rec + pre) : 0;
    if (std::getenv ("ARC_DEBUG")) { std::printf ("    got:"); for (auto& n : got.notes) std::printf (" %d@%.2f/%.2f", n.pitch, n.start, n.length); std::printf ("\n"); }
    std::printf ("  %-8s truth %3d  found %3d  matched %3d  precision %.2f  recall %.2f  F %.2f\n", what, (int) truth.notes.size(), (int) got.notes.size(), hit, pre, rec, f);
    check (f >= minF, juce::String (what) + " audio->MIDI accuracy F >= " + juce::String (minF, 2));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("arc-test");
    dir.deleteRecursively(); dir.createDirectory();

    // ---- ground truth loops (A minor)
    arc::Clip drums; drums.lengthBeats = 8;
    for (int b = 0; b < 8; ++b)
    {
        drums.notes.push_back (N (arc::gm::kick, b, 0.25));
        drums.notes.push_back (N (b == 7 ? arc::gm::openHat : arc::gm::closedHat, b + 0.5, 0.25, 0.7f));
        if (b % 2 == 1) drums.notes.push_back (N (arc::gm::snare, b, 0.25));
    }
    drums.notes.push_back (N (arc::gm::kick, 6.75, 0.25, 0.7f));
    drums.sort();

    arc::Clip bass; bass.lengthBeats = 8;
    const int bn[] { 45, 45, 48, 45, 43, 43, 47, 50 };
    for (int k = 0; k < 8; ++k) bass.notes.push_back (N (bn[k], k + 0.5, 0.375));

    arc::Clip chords; chords.lengthBeats = 16;
    const int ch[4][3] { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };
    for (int k = 0; k < 4; ++k) for (int v = 0; v < 3; ++v) chords.notes.push_back (N (ch[k][v], k * 4.0, 3.5, 0.8f));
    chords.sort();

    arc::Clip lead; lead.lengthBeats = 8;
    const int ln[] { 76, 74, 72, 71, 72, 74, 76, 79 };
    const double ls[] { 0, 0.75, 1.5, 2.5, 4, 4.75, 5.5, 6.5 };
    for (int k = 0; k < 8; ++k) lead.notes.push_back (N (ln[k], ls[k], 0.5));

    writeWav (dir.getChildFile ("drums.wav"), renderDrums (drums));
    writeWav (dir.getChildFile ("bass.wav"), renderTonal (bass, true));
    writeWav (dir.getChildFile ("chords.wav"), renderTonal (chords, true));
    writeWav (dir.getChildFile ("lead.wav"), renderTonal (lead, false));

    ArcProcessor p;
    Head head;
    p.setPlayHead (&head);
    p.prepareToPlay (SR, 512);
    { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; head.play = false; p.processBlock (b, m); }   // pick up host tempo

    // ---- audio -> MIDI: lane given
    p.loadFileSync (arc::laneDrums, dir.getChildFile ("drums.wav"));
    p.loadFileSync (arc::laneBass, dir.getChildFile ("bass.wav"));
    p.loadFileSync (arc::laneChords, dir.getChildFile ("chords.wav"));
    p.loadFileSync (arc::laneLead, dir.getChildFile ("lead.wav"));
    for (int l = 0; l < arc::kNumLanes; ++l) std::printf ("  %s: %s\n", arc::laneName (l), p.lane (l).report.toRawUTF8());
    score ("Drums", drums, p.lane (arc::laneDrums).loop, 0.9f);
    score ("Bass", bass, p.lane (arc::laneBass).loop, 0.9f);
    score ("Chords", chords, p.lane (arc::laneChords).loop, 0.8f);
    score ("Lead", lead, p.lane (arc::laneLead).loop, 0.9f);
    check (std::abs (p.lane (arc::laneBass).loop.lengthBeats - 8.0) < 1e-6 && std::abs (p.lane (arc::laneChords).loop.lengthBeats - 16.0) < 1e-6, "loop lengths read as 2 and 4 bars");

    // ---- automatic lane guess from audio
    {
        ArcProcessor q; q.setPlayHead (&head); q.prepareToPlay (SR, 512);
        { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; q.processBlock (b, m); }
        q.loadFileSync (-1, dir.getChildFile ("drums.wav"));
        q.loadFileSync (-1, dir.getChildFile ("bass.wav"));
        q.loadFileSync (-1, dir.getChildFile ("lead.wav"));
        for (int l = 0; l < arc::kNumLanes; ++l) std::printf ("    guess lane %s <- %s\n", arc::laneName (l), q.lane (l).source.toRawUTF8());
        check (! q.lane (arc::laneDrums).loop.empty() && ! q.lane (arc::laneBass).loop.empty() && ! q.lane (arc::laneLead).loop.empty(), "unlabelled audio goes to the right lanes");
    }

    // ---- MIDI import
    {
        arc::writeMidi (dir.getChildFile ("bass.mid"), bass, BPM, false, "bass");
        auto m = arc::readMidi (dir.getChildFile ("bass.mid"));
        check (m.ok && m.suggestedLane == arc::laneBass && m.clip.notes.size() == bass.notes.size(), "MIDI clip import: bass recognised, all notes kept");
        arc::writeMidi (dir.getChildFile ("drums.mid"), drums, BPM, true, "drums");
        check (arc::readMidi (dir.getChildFile ("drums.mid")).suggestedLane == arc::laneDrums, "MIDI drum clip recognised");
    }

    // ---- arrangement
    p.regenerate (false);
    auto a = p.arrangement();
    std::printf ("  key %s %s, %d bars, sections:", cl::noteName (a->scale.tonic), a->scale.minor ? "minor" : "major", a->totalBars);
    for (auto& s : a->sections) std::printf (" %s(%d)", arc::sectionName (s.type), s.bars);
    std::printf ("\n");
    check (a->totalBars == 128 && a->sections.size() == 7, "club template: 7 sections, 128 bars");
    check ((a->scale.tonic == 9 && a->scale.minor) || (a->scale.tonic == 0 && ! a->scale.minor), "key detected: A minor / C major");

    auto notesIn = [&] (int lane, int bar0, int bars, auto pred) { int c = 0; for (auto& n : a->lanes[(size_t) lane].notes) if (n.start >= bar0 * 4.0 && n.start < (bar0 + bars) * 4.0 && pred (n)) ++c; return c; };
    auto any = [] (const arc::Note&) { return true; };
    const auto& S = a->sections;   // 0 intro 1 build 2 drop 3 break 4 build 5 drop 6 outro
    check (notesIn (arc::laneDrums, S[3].startBar, S[3].bars, [] (const arc::Note& n) { return arc::isKick (n.pitch); }) == 0, "break has no kick");
    check (notesIn (arc::laneDrums, S[1].startBar + S[1].bars - 1, 1, [] (const arc::Note& n) { return n.tag == "roll"; }) >= 12
           && notesIn (arc::laneDrums, S[1].startBar + S[1].bars - 1, 1, [] (const arc::Note& n) { return arc::isKick (n.pitch); }) == 0, "build ends with a snare roll and the kick out");
    int fills = 0; for (int b = 7; b < S[2].bars; b += 8) fills += notesIn (arc::laneDrums, S[2].startBar + b, 1, [] (const arc::Note& n) { return n.tag == "fill"; }) > 0;
    check (fills == S[2].bars / 8, "drop has a fill at the end of every 8 bars");
    check (notesIn (arc::laneBass, S[0].startBar, 8, any) == 0 && notesIn (arc::laneLead, S[0].startBar, S[0].bars, any) == 0, "bass and lead held back in the intro");
    check (notesIn (arc::laneBass, S[3].startBar + S[3].bars / 2, S[3].bars / 2, [] (const arc::Note& n) { return n.tag == "sustain"; }) > 0, "break bass becomes held roots");
    check (notesIn (arc::laneLead, S[2].startBar, S[2].bars, [] (const arc::Note& n) { return n.tag == "response"; }) > 0, "lead has response phrases in the drop");
    {
        // the response differs from the call but stays in key
        int out = 0, resp = 0;
        for (auto& n : a->lanes[arc::laneLead].notes) if (n.tag == "response") { ++resp; if (! a->scale.contains (n.pitch)) ++out; }
        check (resp > 0 && out == 0, "response notes are all in key");
    }
    check (notesIn (arc::laneChords, S[1].startBar, S[1].bars, [] (const arc::Note& n) { return n.tag == "stab"; }) > 0, "build turns chords into rising stabs");
    check (notesIn (arc::laneDrums, S[6].startBar + S[6].bars - 4, 4, [] (const arc::Note& n) { return ! arc::isKick (n.pitch); }) == 0, "outro ends on kick only");
    {
        int ghost = 0, resp = 0;
        for (auto& n : a->lanes[arc::laneDrums].notes) ghost += n.tag == "ghost";
        for (auto& t : a->tags[arc::laneBass]) resp += t.text.contains ("answer") || t.text.contains ("passing") || t.text.contains ("walk") || t.text.contains ("syncop");
        std::printf ("  drum ghost notes %d, bass variations %d, tags: drums %d bass %d chords %d lead %d\n", ghost, resp,
                     (int) a->tags[0].size(), (int) a->tags[1].size(), (int) a->tags[2].size(), (int) a->tags[3].size());
        check (resp >= 4, "bass gets call-and-response variations");
    }
    // determinism and re-rolls
    {
        const auto before = a->lanes[arc::laneBass].notes.size();
        p.regenerate (false);
        check (p.arrangement()->lanes[arc::laneBass].notes.size() == before, "same seed rebuilds the same arrangement");
        p.regenerate (true);
        auto b2 = p.arrangement();
        bool differs = false;
        for (int l = 0; l < arc::kNumLanes && ! differs; ++l)
        {
            const auto& x = a->lanes[(size_t) l].notes; const auto& y = b2->lanes[(size_t) l].notes;
            if (x.size() != y.size()) differs = true;
            else for (size_t i = 0; i < x.size(); ++i) if (x[i].pitch != y[i].pitch || std::abs (x[i].start - y[i].start) > 1e-9) { differs = true; break; }
        }
        check (differs, "NEW VARIATIONS changes the fills and phrases");
        a = b2;
    }
    // every template builds
    for (int t = 0; t < (int) arc::templates().size(); ++t)
    {
        if (auto* prm = p.apvts.getParameter (ap::style)) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) t));
        p.regenerate (false);
        int total = 0; for (auto& l : p.arrangement()->lanes) total += (int) l.notes.size();
        std::printf ("  template %-28s %3d bars %5d notes\n", arc::templates()[(size_t) t].name, p.arrangement()->totalBars, total);
        check (total > 100, juce::String ("template builds: ") + arc::templates()[(size_t) t].name);
    }
    if (auto* prm = p.apvts.getParameter (ap::style)) prm->setValueNotifyingHost (0.0f);
    p.regenerate (false);
    a = p.arrangement();

    // ---- export
    auto files = p.exportAll();
    check (files.size() == 4, "exports 4 MIDI files");
    for (int i = 0; i < files.size(); ++i)
    {
        auto m = arc::readMidi (files[i]);
        std::printf ("  %s: %d notes\n", files[i].getFileName().toRawUTF8(), (int) m.clip.notes.size());
        check (m.ok && std::abs ((int) m.clip.notes.size() - (int) a->lanes[(size_t) i].notes.size()) == 0, "export round-trips: " + files[i].getFileName());
    }

    // ---- preview renders in time with the transport
    {
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        head.play = true;
        double e = 0; bool bad = false;
        for (int k = 0; k < 400; ++k)
        {
            head.ppq = S[2].startBar * 4.0 + k * 512 / SR * BPM / 60.0;
            b.clear(); p.processBlock (b, m);
            for (int i = 0; i < 512; ++i) { const float v = b.getSample (0, i); e += v * v; if (! std::isfinite (v)) bad = true; }
        }
        check (e > 1.0 && ! bad, "preview plays the drop in sync with the host");
    }

    // ---- state round-trip
    {
        juce::MemoryBlock mb; p.getStateInformation (mb);
        ArcProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.lane (arc::laneChords).loop.notes.size() == p.lane (arc::laneChords).loop.notes.size()
               && q.arrangement()->lanes[arc::laneLead].notes.size() == a->lanes[arc::laneLead].notes.size(), "state restores loops and arrangement");
    }

    if (auto* snap = std::getenv ("SNAPSHOT"))
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        head.ppq = 70 * 4.0; { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; p.processBlock (b, m); }
        if (auto* e = dynamic_cast<ArcEditor*> (ed.get())) e->refreshNow();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.4f);
        juce::File f (snap); f.deleteFile(); juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    }
    if (argc > 1)
    {
        // render the whole arrangement through the preview for a listen
        juce::File out (argv[1]);
        const int total = (int) (a->totalBars * 4 * 60.0 / BPM * SR);
        std::vector<float> L ((size_t) total), R ((size_t) total);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        head.play = true;
        for (int pos = 0; pos + 512 <= total; pos += 512)
        {
            head.ppq = pos / SR * BPM / 60.0;
            b.clear(); p.processBlock (b, m);
            for (int i = 0; i < 512; ++i) { L[(size_t) (pos + i)] = b.getSample (0, i); R[(size_t) (pos + i)] = b.getSample (1, i); }
        }
        juce::AudioBuffer<float> ob (2, total);
        for (int i = 0; i < total; ++i) { ob.setSample (0, i, L[(size_t) i]); ob.setSample (1, i, R[(size_t) i]); }
        out.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (out.createOutputStream().release());
        auto opts = juce::AudioFormatWriterOptions().withSampleRate (SR).withNumChannels (2).withBitsPerSample (16);
        if (auto w = wav.createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (ob, 0, total);
    }
    std::printf ("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
