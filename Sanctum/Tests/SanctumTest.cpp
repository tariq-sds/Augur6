// Sanctum offline tests: harmony parts land on the right notes, MIDI chords, the lead stays sample-aligned,
// every preset renders cleanly, CPU, state and MIDI Learn round-trips, and an editor snapshot.
#include "../Source/Sanctum.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("%-72s %s\n", what.toRawUTF8(), ok ? "ok" : "FAILED");
    if (! ok) ++failures;
}

// a sung "ah": glottal pulses through three vowel formants, with a little vibrato
static juce::AudioBuffer<float> voice (double sr, float hz, double seconds, float vibrato = 0.004f)
{
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (2, n);
    space::Svf f1, f2, f3;
    double ph = 0;
    for (int i = 0; i < n; ++i)
    {
        const float f = hz * (1.0f + vibrato * std::sin (2.0f * space::kPi * 5.2f * (float) i / (float) sr));
        ph += f / sr;
        const float frac = (float) (ph - std::floor (ph));
        const float pulse = frac < 0.4f ? std::sin (space::kPi * frac / 0.4f) : 0.0f;          // open phase
        const float src = pulse - 0.32f;
        f1.process (src, 750.0f, 6.0f, (float) sr); f2.process (src, 1200.0f, 8.0f, (float) sr); f3.process (src, 2700.0f, 10.0f, (float) sr);
        const float env = juce::jmin (1.0f, (float) i / (float) (0.02 * sr), (float) (n - i) / (float) (0.02 * sr));
        const float y = (f1.bpo * 1.0f + f2.bpo * 0.6f + f3.bpo * 0.35f) * 0.35f * env;
        b.setSample (0, i, y); b.setSample (1, i, y);
    }
    return b;
}

static juce::AudioBuffer<float> run (SanctumProcessor& p, const juce::AudioBuffer<float>& in, double sr, juce::MidiBuffer notes = {})
{
    p.prepareToPlay (sr, 512);
    juce::AudioBuffer<float> out (in);
    for (int pos = 0; pos < out.getNumSamples(); pos += 512)
    {
        const int len = juce::jmin (512, out.getNumSamples() - pos);
        juce::AudioBuffer<float> blk (out.getArrayOfWritePointers(), 2, pos, len);
        juce::MidiBuffer mb;
        if (pos == 0) mb = notes;
        p.processBlock (blk, mb);
    }
    return out;
}

// median pitch of the second half of a render
static float medianHz (const juce::AudioBuffer<float>& b, double sr)
{
    cl::PitchTracker t; t.prepare ((float) sr);
    std::vector<float> est;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (t.push (0.5f * (b.getSample (0, i) + b.getSample (1, i))) && i > b.getNumSamples() / 2 && t.voiced) est.push_back (t.hz);
    if (est.empty()) return 0;
    std::sort (est.begin(), est.end());
    return est[est.size() / 2];
}

static float midiHz (int m) { return 440.0f * std::exp2 ((float) (m - 69) / 12.0f); }

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const double sr = 48000.0;

    // ---- harmony: C major, full 4-part, lead on A3 -> S A4, A E4, T C4, B A2
    {
        const auto in = voice (sr, midiHz (57), 2.0);
        const int expect[] { 69, 64, 60, 45 };
        for (int s = 0; s < sc::kSections; ++s)
        {
            SanctumProcessor p; p.loadPreset (0);
            p.setParam (sp::humanise, 0.15f); p.setParam (sp::singers, 4);
            p.soloSectionForTests = s;
            const float hz = medianHz (run (p, in, sr), sr), want = midiHz (expect[s]);
            check (std::abs (hz / want - 1.0f) < 0.015f, juce::String ("harmony ") + sc::kSectionNames[s] + ": " + juce::String (hz, 1) + " Hz, want " + juce::String (want, 1));
        }
    }
    // ---- other keys and scales (A minor, triad below, lead E4): A C4 (-2 deg), T A3, B E3
    {
        const auto in = voice (sr, midiHz (64), 2.0);
        const int expect[] { 64, 60, 57, 52 };
        for (int s = 0; s < sc::kSections; ++s)
        {
            SanctumProcessor p; p.loadPreset (0);
            p.setParam (sp::key, 9); p.setParam (sp::scale, 1); p.setParam (sp::harmony, 4); p.setParam (sp::humanise, 0.15f);
            p.soloSectionForTests = s;
            const float hz = medianHz (run (p, in, sr), sr), want = midiHz (expect[s]);
            check (std::abs (hz / want - 1.0f) < 0.015f, juce::String ("A minor triad below ") + sc::kSectionNames[s] + ": " + juce::String (hz, 1) + " Hz, want " + juce::String (want, 1));
        }
    }
    // ---- MIDI chords: hold C5 G4 E4 C3 while singing A3
    {
        const auto in = voice (sr, midiHz (57), 2.0);
        juce::MidiBuffer chord;
        for (int nn : { 72, 67, 64, 48 }) chord.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) 100), 0);
        const int expect[] { 72, 67, 64, 48 };
        for (int s = 0; s < sc::kSections; ++s)
        {
            SanctumProcessor p; p.loadPreset (0); p.setParam (sp::humanise, 0.15f);
            p.soloSectionForTests = s;
            const float hz = medianHz (run (p, in, sr, chord), sr), want = midiHz (expect[s]);
            check (std::abs (hz / want - 1.0f) < 0.015f, juce::String ("MIDI chord ") + sc::kSectionNames[s] + ": " + juce::String (hz, 1) + " Hz, want " + juce::String (want, 1));
        }
    }
    // ---- the lead passes through untouched, delayed by exactly the reported latency
    {
        SanctumProcessor p; p.loadPreset (0); p.setParam (sp::choir, -36.0f);
        const auto in = voice (sr, 200.0f, 1.0);
        const auto out = run (p, in, sr);
        const int lat = p.getLatencySamples();
        float err = 0;
        for (int i = lat + 10; i < in.getNumSamples(); ++i) err = juce::jmax (err, std::abs (out.getSample (0, i) - in.getSample (0, i - lat)));
        check (lat > 0 && err < 1e-5f, "lead is sample-aligned with " + juce::String (lat) + " samples latency");
    }
    // ---- every preset, two lead notes, two sample rates
    std::printf ("\n%-3s %-18s %8s %8s\n", "#", "preset", "peak", "rms");
    for (int i = 0; i < (int) choirPresets().size(); ++i)
    {
        float pk = 0, rms = 0; bool bad = false;
        for (double rate : { 48000.0, 44100.0 })
            for (float hz : { 196.0f, 311.0f })
            {
                SanctumProcessor p; p.loadPreset (i);
                const auto out = run (p, voice (rate, hz, 2.5), rate);
                for (int c = 0; c < 2; ++c)
                    for (int k = 0; k < out.getNumSamples(); ++k) { const float v = out.getSample (c, k); if (! std::isfinite (v)) bad = true; pk = juce::jmax (pk, std::abs (v)); }
                rms = juce::jmax (rms, out.getRMSLevel (0, out.getNumSamples() / 3, out.getNumSamples() / 3));
            }
        const bool ok = ! bad && pk < 1.2f && rms > 0.02f;
        if (! ok) ++failures;
        std::printf ("%-3d %-18s %8.3f %8.4f  %s\n", i, choirPresets()[(size_t) i].name.toRawUTF8(), pk, rms, ok ? "ok" : (bad ? "NaN!" : "LEVEL"));
    }
    std::printf ("\n");
    // ---- CPU: 32 singers, sub, breath and hall
    {
        SanctumProcessor p; p.loadPreset (1); p.setParam (sp::singers, 32); p.setParam (sp::sub, 0.5f); p.setParam (sp::breath, 0.5f);
        const auto in = voice (sr, 220.0f, 10.0);
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        run (p, in, sr);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (ms < 10000 * 0.25, "CPU: 32 singers, 10 s of audio in " + juce::String (ms, 0) + " ms (" + juce::String (ms / 100.0, 1) + "% of one core)");
    }
    // ---- state and MIDI Learn
    {
        SanctumProcessor a, b;
        a.loadPreset (6); a.setParam (sp::pan[2], -0.9f);
        a.midiLearn.learn (sp::sub);
        a.prepareToPlay (sr, 256);
        juce::AudioBuffer<float> buf (2, 256); buf.clear();
        juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::controllerEvent (1, 21, 10), 0);
        mb.addEvent (juce::MidiMessage::controllerEvent (1, 74, 127), 1);
        a.processBlock (buf, mb);
        juce::MemoryBlock st; a.getStateInformation (st);
        b.setStateInformation (st.getData(), (int) st.getSize());
        bool same = b.getCurrentProgram() == 6;
        for (auto* prm : a.getParameters())
        {
            auto* pa = dynamic_cast<juce::RangedAudioParameter*> (prm);
            auto* pb = dynamic_cast<juce::RangedAudioParameter*> (b.getParameters()[prm->getParameterIndex()]);
            if (pa && pb && std::abs (pa->getValue() - pb->getValue()) > 1e-5f) same = false;
        }
        check (same, "state round-trip");
        check (a.apvts.getParameter (sp::room)->getValue() > 0.99f && b.midiLearn.ccFor (sp::sub) == 21 && b.midiLearn.ccFor (sp::room) == 74,
               "MIDI Learn: CC74 moves Space, learned CC21 kept with the set");
    }
    // ---- editor snapshot, singing so the stage shows notes
    if (auto* snap = std::getenv ("SANCTUM_SNAPSHOT"))
    {
        SanctumProcessor p; p.loadPreset (std::getenv ("SANCTUM_PRESET") ? std::atoi (std::getenv ("SANCTUM_PRESET")) : 0);
        run (p, voice (sr, midiHz (57), 0.8), sr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (snap); f.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    }
    // ---- a choir render to listen to
    if (auto* wavPath = std::getenv ("SANCTUM_WAV"))
    {
        SanctumProcessor p; p.loadPreset (std::getenv ("SANCTUM_PRESET") ? std::atoi (std::getenv ("SANCTUM_PRESET")) : 0);
        juce::AudioBuffer<float> in (2, (int) (sr * 6));
        in.clear();
        const int melody[] { 57, 60, 64, 62, 60, 57 };
        for (int k = 0; k < 6; ++k)
        {
            const auto v = voice (sr, midiHz (melody[k]), 1.0);
            for (int c = 0; c < 2; ++c) in.addFrom (c, (int) (sr * k), v, c, 0, v.getNumSamples());
        }
        const auto out = run (p, in, sr);
        juce::File f (wavPath); f.deleteFile();
        juce::WavAudioFormat wav;
        if (auto os = f.createOutputStream())
            if (auto* w = wav.createWriterFor (os.get(), sr, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr (w); wr->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()); }
    }
    std::printf ("\nfailures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
