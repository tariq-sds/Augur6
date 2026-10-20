// Offline test for Varia: plays a synthetic 124 BPM groove through every style with a running
// transport and checks for events, stability, level and transparency at Texture 0.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/Processor.h"
#include "../Source/Editor.h"
#include <cstdio>

struct TestHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 124;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static juce::AudioBuffer<float> makeGroove (double sr, double bpm, int bars)
{
    const int total = (int) (sr * 60.0 / bpm * 4 * bars);
    juce::AudioBuffer<float> b (2, total);
    b.clear();
    const double s16 = sr * 60.0 / bpm / 4.0;
    juce::Random rnd (7);
    const double chord[] { 220.0, 261.63, 329.63 };
    for (int step = 0; step * s16 < total; ++step)
    {
        const int start = (int) (step * s16);
        const int pos = step % 16;
        for (int i = 0; i < (int) (s16 * 4) && start + i < total; ++i)
        {
            const float t = (float) i / (float) sr;
            float l = 0, r = 0;
            if (pos % 4 == 0) { const float k = 0.8f * std::exp (-t / 0.18f) * std::sin (2 * 3.14159f * (50 * t + 2.5f * (1 - std::exp (-t / 0.025f)))); l += k; r += k; }
            if (pos % 2 == 1 && i < s16) { const float h = 0.18f * std::exp (-t / 0.02f) * rnd.nextFloat() * 2 - 0.18f * std::exp (-t / 0.02f); l += h * 0.8f; r += h; }
            if ((pos == 4 || pos == 12) && i < s16 * 2) { const float c = 0.3f * std::exp (-t / 0.06f) * (rnd.nextFloat() * 2 - 1); l += c; r += c * 0.9f; }
            if ((pos == 2 || pos == 10 || pos == 14) && i < s16 * 1.5)
            {
                float v = 0;
                for (double f : chord) v += (float) (std::fmod (f * t, 1.0) * 2 - 1);
                v *= 0.06f * std::exp (-t / 0.12f);
                l += v; r += v;
            }
            b.addSample (0, start + i, l);
            b.addSample (1, start + i, r);
        }
    }
    return b;
}

struct Result { float peak = 0, diffDb = -200; int events = 0; bool bad = false; juce::AudioBuffer<float> out; };

static Result run (int style, float texture, const juce::AudioBuffer<float>& in, double sr, int block, bool keep)
{
    VariaProcessor p;
    TestHead head;
    p.testPlayHead = &head;
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    auto set = [&] (const char* id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
    set (vap::style, (float) style);
    set (vap::texture, texture);

    Result r;
    const int total = in.getNumSamples();
    if (keep) r.out.setSize (2, total);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    double diff = 0, ref = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, pos, n);
        head.ppq = (double) pos / sr * head.bpm / 60.0;
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample (c, i), d = v - in.getSample (c, pos + i);
                if (! std::isfinite (v)) r.bad = true;
                r.peak = juce::jmax (r.peak, std::abs (v));
                diff += (double) d * d;
                ref += (double) in.getSample (c, pos + i) * in.getSample (c, pos + i);
            }
        if (keep) for (int c = 0; c < 2; ++c) r.out.copyFrom (c, pos, buf, c, 0, n);
    }
    r.diffDb = (float) (10 * std::log10 (diff / ref + 1e-30));
    r.events = p.engine.eventCounter.load();
    return r;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const auto groove = makeGroove (sr, 124.0, 16);
    int failures = 0;
    std::printf ("input peak %.3f\n", groove.getMagnitude (0, groove.getNumSamples()));

    std::printf ("%-14s %7s %8s %7s  %s\n", "style", "events", "diff dB", "peak", "status");
    const int nStyles = (int) va::styles().size();
    for (int s = 0; s < nStyles; ++s)
    {
        const bool keep = argc > 1;
        auto r = run (s, 0.8f, groove, sr, 512, keep);
        auto r2 = run (s, 1.0f, groove, 44100.0 == 0 ? sr : sr, 333, false);   // odd block size, max texture
        juce::String status = "ok";
        if (r.bad || r2.bad) status = "NaN!";
        else if (r.peak > 1.4f || r2.peak > 1.6f) status = "TOO LOUD";
        else if (r.events < 2 || r.diffDb < -30.0f) status = "NOT ENOUGH MOVEMENT";
        if (status != "ok") ++failures;
        std::printf ("%-14s %7d %8.1f %7.3f  %s\n", va::styles()[(size_t) s].name, r.events, r.diffDb, juce::jmax (r.peak, r2.peak), status.toRawUTF8());
        if (keep)
        {
            juce::File dir (argv[1]);
            dir.createDirectory();
            writeWav (dir.getChildFile (juce::String (s).paddedLeft ('0', 2) + "_" + juce::String (va::styles()[(size_t) s].name).replaceCharacter (' ', '_') + ".wav"), r.out, sr);
        }
    }
    if (argc > 1) writeWav (juce::File (argv[1]).getChildFile ("dry.wav"), groove, sr);

    // Transparency at Texture 0
    {
        auto r = run (2, 0.0f, groove, sr, 512, false);
        std::printf ("texture 0: diff %.1f dB, events %d -> %s\n", r.diffDb, r.events, r.diffDb < -90 && r.events == 0 ? "transparent" : "NOT TRANSPARENT");
        if (! (r.diffDb < -90 && r.events == 0)) ++failures;
    }

    if (auto* snap = std::getenv ("SNAPSHOT"))
    {
        VariaProcessor p;
        TestHead head; p.testPlayHead = &head;
        p.prepareToPlay (sr, 512);
        if (auto* e = std::getenv ("SNAP_STYLE")) { auto* prm = p.apvts.getParameter (vap::style); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) std::atoi (e))); }
        auto* prm = p.apvts.getParameter (vap::texture); prm->setValueNotifyingHost (0.68f);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        for (int pos = 0; pos < (int) sr * 6; pos += 512) { for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, groove, c, pos, 512); head.ppq = pos / sr * 124 / 60; p.processBlock (buf, midi); }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        if (auto* ve = dynamic_cast<VariaEditor*> (ed.get())) for (int i = 0; i < 20; ++i) ve->refreshNow();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::File f (snap); f.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    }

    std::printf ("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
