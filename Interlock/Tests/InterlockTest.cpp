// Offline test for Interlock: synthesises a kick (sidechain) and an overlapping bass, lets the
// plugin auto-learn, then checks the detected kick frequency and that the bass ducks on each hit.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/Processor.h"
#include "../Source/Editor.h"
#include <cstdio>

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (b.getNumChannels()).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (os, opts))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct Band  // simple band-pass for measurement
{
    float ic1 = 0, ic2 = 0, g, k;
    Band (float f, float sr) : g (std::tan (3.14159265f * f / sr)), k (1.0f / 2.0f) {}
    float operator() (float x)
    {
        const float a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2, v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        return v1 * k;
    }
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0;
    const int block = 512;
    const double bpm = 124.0;
    const int total = (int) (sr * 20.0);
    const int beat = (int) (sr * 60.0 / bpm);
    const float kickHz = 52.0f;

    // ---- Source material
    juce::AudioBuffer<float> kick (1, total), bass (1, total);
    double ph = 0, bph = 0;
    float lp = 0;
    for (int i = 0; i < total; ++i)
    {
        const int t = i % beat;
        const float ts = (float) t / (float) sr;
        if (t == 0) ph = 0;
        const float f = kickHz + 110.0f * std::exp (-ts / 0.025f);
        ph += f / sr;
        kick.setSample (0, i, 0.8f * std::exp (-ts / 0.28f) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * ph));

        // rolling bass: A1 (55 Hz) then E1 per bar, saw through a one-pole low-pass
        const int bar = (i / (beat * 4)) % 2;
        const double bf = bar == 0 ? 55.0 : 41.2;
        bph += bf / sr; if (bph >= 1) bph -= 1;
        const float saw = (float) (2 * bph - 1);
        lp += (saw - lp) * 0.04f;
        bass.setSample (0, i, 0.45f * lp);
    }

    InterlockProcessor proc;
    proc.setRateAndBufferSizeDetails (sr, block);
    proc.prepareToPlay (sr, block);
    const int nCh = proc.getTotalNumInputChannels();
    std::printf ("channels in=%d out=%d buses in=%d\n", nCh, proc.getTotalNumOutputChannels(), proc.getBusCount (true));

    juce::AudioBuffer<float> out (2, total);
    juce::AudioBuffer<float> buf (juce::jmax (nCh, 2), block);
    juce::MidiBuffer midi;
    bool bad = false;
    float peak = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        buf.setSize (buf.getNumChannels(), n, false, false, true);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, bass, 0, pos, n);
        for (int c = 2; c < buf.getNumChannels(); ++c) buf.copyFrom (c, 0, kick, 0, pos, n);
        proc.processBlock (buf, midi);
        proc.pollLearn();
        if (std::getenv ("TRACE") && pos > sr * 12 && pos < sr * 12 + beat) std::printf ("t=%5.0fms dip=%.1f duck=%.1f comp=%.1f\n", 1000.0 * ((pos - (int)(sr*12)) ) / sr, proc.dipNowDb.load(), proc.duckNowDb.load(), proc.compGrDb.load());
        for (int c = 0; c < 2; ++c)
        {
            out.copyFrom (c, pos, buf, c, 0, n);
            for (int i = 0; i < n; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) bad = true;
                peak = juce::jmax (peak, std::abs (v));
            }
        }
    }

    int failures = 0;
    const float learned = proc.learnedKickHz.load();
    std::printf ("learned: %s  kick=%.1f Hz (true %.1f)  boost=%.1f Hz  overlap=%.2f\n",
                 proc.hasLearned.load() ? "yes" : "NO", learned, kickHz, proc.learnedBoostHz.load(), proc.learnedOverlap.load());
    auto P = [&] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    std::printf ("params: dip %.1f Hz depth %.1f dB Q %.2f duck %.1f rel %.0f ms boost %.0f Hz %+.1f dB  bassRms %.1f dB\n",
                 P (ilp::dipFreq), P (ilp::dipDepth), P (ilp::dipQ), P (ilp::duck), P (ilp::release), P (ilp::boostFreq), P (ilp::boostGain), proc.bassRmsDb.load());
    if (! proc.hasLearned.load() || std::abs (learned - kickHz) > 6.0f) { ++failures; std::printf ("FAIL: kick detection\n"); }
    if (bad) { ++failures; std::printf ("FAIL: NaN\n"); }
    std::printf ("peak out %.3f\n", peak);

    // ---- Ducking measurement after learning (from 10 s on), in a band around the kick fundamental
    Band bi (kickHz, (float) sr), bo (kickHz, (float) sr);
    double eInHit = 0, eOutHit = 0, eInGap = 0, eOutGap = 0;
    for (int i = 0; i < total; ++i)
    {
        const float a = bi (bass.getSample (0, i)), b = bo (0.5f * (out.getSample (0, i) + out.getSample (1, i)));
        if (i < (int) (sr * 10)) continue;
        const int t = i % beat;
        if (t > (int) (sr * 0.02) && t < (int) (sr * 0.18)) { eInHit += a * a; eOutHit += b * b; }
        else if (t > (int) (sr * 0.33) && t < (int) (sr * 0.47)) { eInGap += a * a; eOutGap += b * b; }
    }
    const double hitDb = 10 * std::log10 (eOutHit / eInHit), gapDb = 10 * std::log10 (eOutGap / eInGap);
    std::printf ("bass level change at kick fundamental: on hits %.1f dB, between hits %.1f dB -> breathing %.1f dB\n", hitDb, gapDb, gapDb - hitDb);
    if (gapDb - hitDb < 3.0) { ++failures; std::printf ("FAIL: not enough ducking\n"); }

    // ---- State round trip
    {
        juce::MemoryBlock mb;
        proc.getStateInformation (mb);
        InterlockProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = b.hasLearned.load() && std::abs (b.apvts.getRawParameterValue (ilp::dipFreq)->load() - P (ilp::dipFreq)) < 0.01f;
        std::printf ("state round-trip: %s\n", same ? "ok" : "FAILED");
        if (! same) ++failures;
    }

    // ---- Renders for listening: before / after (kick + bass)
    if (argc > 1)
    {
        juce::File dir (argv[1]);
        dir.createDirectory();
        juce::AudioBuffer<float> before (2, total), after (2, total);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
            {
                before.setSample (c, i, 0.7f * (kick.getSample (0, i) + bass.getSample (0, i)));
                after.setSample (c, i, 0.7f * (kick.getSample (0, i) + out.getSample (c, i)));
            }
        writeWav (dir.getChildFile ("interlock_before.wav"), before, sr);
        writeWav (dir.getChildFile ("interlock_after.wav"), after, sr);
    }

    if (auto* snap = std::getenv ("SNAPSHOT"))
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        // feed a little more audio so the analyser display is live
        for (int pos = 0; pos < (int) sr; pos += block)
        {
            buf.setSize (buf.getNumChannels(), block, false, false, true);
            for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, bass, 0, pos + (int) (sr * 5), block);
            for (int c = 2; c < buf.getNumChannels(); ++c) buf.copyFrom (c, 0, kick, 0, pos + (int) (sr * 5) - 600, block);
            proc.processBlock (buf, midi);
        }
        ed->setSize (940, 540);
        if (auto* e = dynamic_cast<InterlockEditor*> (ed.get())) e->refreshNow();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        juce::File f (snap);
        f.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    }

    std::printf ("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
