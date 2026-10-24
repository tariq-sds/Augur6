// Wayfarer offline tests: tuning, octaves, the mono note handling, glide, the dual filter, modulation busses,
// the touch pad, gate modes, every factory preset at three sample rates, state round-trip and an editor snapshot.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"
#include "../Source/PluginEditor.h"
#include <cstdio>

static int failures = 0;
static void check (bool ok, const juce::String& what) { std::printf ("%-74s %s\n", what.toRawUTF8(), ok ? "ok" : "FAIL"); if (! ok) ++failures; }

static void setP (WayfarerProcessor& p, const char* id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

struct Ev { double t; int note; bool on; int cc = -1; int val = 0; };

// renders `secs` seconds with the given events; returns stereo audio
static juce::AudioBuffer<float> run (WayfarerProcessor& p, double secs, std::vector<Ev> ev, double sr = 48000, int block = 256)
{
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    const int total = (int) (secs * sr);
    juce::AudioBuffer<float> out (2, total), buf (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        for (auto& e : ev)
        {
            const int at = (int) (e.t * sr);
            if (at >= pos && at < pos + n)
            {
                if (e.cc >= 0) midi.addEvent (juce::MidiMessage::controllerEvent (1, e.cc, e.val), at - pos);
                else midi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, e.note), at - pos);
            }
        }
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
    }
    return out;
}

// fundamental by autocorrelation over a window
static double pitchHz (const juce::AudioBuffer<float>& b, double from, double len, double sr = 48000, int ch = 0)
{
    const int s0 = (int) (from * sr), n = (int) (len * sr);
    const float* x = b.getReadPointer (ch) + s0;
    const int minLag = (int) (sr / 2000), maxLag = (int) (sr / 25);
    std::vector<double> r ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (int i = 0; i + lag < n; ++i) { s += x[i] * x[i + lag]; e1 += x[i] * x[i]; e2 += x[i + lag] * x[i + lag]; }
        r[(size_t) lag] = s / std::sqrt (e1 * e2 + 1e-20);
    }
    int best = minLag; double bv = -1;
    const double top = *std::max_element (r.begin() + minLag, r.begin() + maxLag);
    for (int lag = minLag; lag <= maxLag; ++lag)   // first peak close to the best one (avoids octave errors)
        if (r[(size_t) lag] > 0.9 * top && r[(size_t) lag] >= r[(size_t) lag - 1] && r[(size_t) lag] >= r[(size_t) lag + 1]) { best = lag; bv = r[(size_t) lag]; break; }
    if (bv < 0) return 0;
    const double a = r[(size_t) best - 1], b0 = r[(size_t) best], c = r[(size_t) best + 1];
    const double d = (a - c) / (2 * (a - 2 * b0 + c));
    return sr / (best + (std::isfinite (d) ? d : 0));
}

static double rms (const juce::AudioBuffer<float>& b, double from, double len, int ch = 0, double sr = 48000)
{
    return b.getRMSLevel (ch, (int) (from * sr), (int) (len * sr));
}

static double cents (double a, double b) { return 1200.0 * std::log2 (a / b); }

// a plain test voice: one triangle-ish saw, filter open, no drift
static void plain (WayfarerProcessor& p)
{
    p.loadPreset (-1);
    setP (p, pid::drift, 0); setP (p, pid::cutoff, 20000); setP (p, pid::envAmt, 0); setP (p, pid::m1Amt, 0);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    // ---- tuning and octaves
    {
        WayfarerProcessor p; plain (p);
        auto a = run (p, 0.6, { { 0.0, 69, true } });
        const double hz = pitchHz (a, 0.2, 0.3);
        check (std::abs (cents (hz, 440.0)) < 3, "A4 at 8' is 440 Hz (" + juce::String (hz, 2) + ")");
        setP (p, pid::o1Oct, 1);
        auto b = run (p, 0.6, { { 0.0, 69, true } });
        const double hz2 = pitchHz (b, 0.2, 0.3);
        check (std::abs (cents (hz2, 220.0)) < 3, "16' is an octave down (" + juce::String (hz2, 2) + ")");
        setP (p, pid::o1Oct, 2); setP (p, pid::tune, 50);
        auto c = run (p, 0.6, { { 0.0, 69, true } });
        check (std::abs (cents (pitchHz (c, 0.2, 0.3), 440.0) - 50) < 4, "fine tune +50 cents");
    }
    // ---- osc 2 frequency: a fifth up
    {
        WayfarerProcessor p; plain (p);
        setP (p, pid::mix1, 0); setP (p, pid::mix2, 1); setP (p, pid::o2Freq, 7);
        auto a = run (p, 0.6, { { 0.0, 57, true } });
        const double hz = pitchHz (a, 0.2, 0.3);
        check (std::abs (cents (hz, 220.0 * std::pow (2.0, 7 / 12.0))) < 4, "osc 2 FREQUENCY +7 = a fifth (" + juce::String (hz, 1) + ")");
    }
    // ---- mono note priority
    {
        WayfarerProcessor p; plain (p);
        setP (p, pid::priority, 0);   // low
        auto a = run (p, 0.8, { { 0.0, 60, true }, { 0.2, 48, true }, { 0.3, 72, true } });
        check (std::abs (cents (pitchHz (a, 0.45, 0.3), 130.81)) < 5, "LOW priority keeps the lowest held note");
        setP (p, pid::priority, 2);   // last
        auto b = run (p, 1.0, { { 0.0, 60, true }, { 0.2, 72, true }, { 0.5, 72, false } });
        check (std::abs (cents (pitchHz (b, 0.3, 0.15), 523.25)) < 5 && std::abs (cents (pitchHz (b, 0.7, 0.25), 261.63)) < 5,
               "LAST priority plays the newest note, falls back to the held one on release");
    }
    // ---- glide
    {
        WayfarerProcessor p; plain (p);
        setP (p, pid::glideOn, 1); setP (p, pid::glide, 0.5f);
        auto a = run (p, 1.6, { { 0.0, 48, true }, { 0.4, 60, true } });
        const double mid = pitchHz (a, 0.5, 0.06), end = pitchHz (a, 1.4, 0.15);
        check (mid > 140 && mid < 250 && std::abs (cents (end, 261.63)) < 8, "GLIDE slides between notes (" + juce::String (mid, 0) + " Hz on the way, " + juce::String (end, 1) + " at the end)");
    }
    // ---- dual filter: spacing makes stereo, mode HP/LP removes lows
    {
        WayfarerProcessor p; plain (p);
        setP (p, pid::cutoff, 600);
        auto a = run (p, 0.5, { { 0.0, 48, true } });
        double diff = 0; for (int i = 4800; i < 24000; ++i) diff += std::abs (a.getSample (0, i) - a.getSample (1, i));
        check (diff < 1e-3, "LP/LP with no spacing: identical left and right");
        setP (p, pid::spacing, 2.0f);
        auto b = run (p, 0.5, { { 0.0, 48, true } });
        auto bright = [&b] (int ch) { double e = 0; for (int i = 9600; i < 24000; ++i) { const double d = b.getSample (ch, i) - b.getSample (ch, i - 1); e += d * d; } return std::sqrt (e); };
        check (bright (1) > 2.0 * bright (0), "SPACING +2 oct: right filter opens further than left (stereo), " + juce::String (bright (1) / bright (0), 1) + "x brighter");
        WayfarerProcessor q; plain (q);
        setP (q, pid::cutoff, 2000); setP (q, pid::fMode, 1); setP (q, pid::spacing, 1.0f);
        setP (q, pid::o1Oct, 0); setP (q, pid::o1Wave, 0.0f);
        auto hp = run (q, 0.5, { { 0.0, 36, true } });
        setP (q, pid::fMode, 0);
        auto lp = run (q, 0.5, { { 0.0, 36, true } });
        check (rms (hp, 0.2, 0.25) < 0.25 * rms (lp, 0.2, 0.25), "HP/LP mode cuts a low note that LP/LP passes");
    }
    // ---- resonance self-oscillates
    {
        WayfarerProcessor p; plain (p);
        setP (p, pid::mix1, 0); setP (p, pid::res, 1.0f); setP (p, pid::cutoff, 880); setP (p, pid::kb, 0);
        auto a = run (p, 1.0, { { 0.0, 60, true } });
        const double hz = pitchHz (a, 0.6, 0.3);
        check (rms (a, 0.6, 0.3) > 0.02 && std::abs (cents (hz, 880)) < 150, "full resonance whistles at the cutoff with no oscillators (" + juce::String (hz, 0) + " Hz)");
    }
    // ---- mod wheel bus 1: vibrato only when the wheel is up
    {
        WayfarerProcessor p; plain (p);
        setP (p, pid::m1Amt, 0.5f); setP (p, pid::lfoRate, 3.0f);
        auto still = run (p, 1.2, { { 0.0, 69, true } });
        double lo = 1e9, hi = 0;
        for (double t = 0.3; t < 1.1; t += 0.05) { const double h = pitchHz (still, t, 0.04); lo = std::min (lo, h); hi = std::max (hi, h); }
        check (cents (hi, lo) < 8, "wheel down: no vibrato from MOD 1");
        auto moving = run (p, 1.2, { { 0.0, 1, false, 1, 127 }, { 0.0, 69, true } });
        lo = 1e9; hi = 0;
        for (double t = 0.3; t < 1.1; t += 0.05) { const double h = pitchHz (moving, t, 0.04); lo = std::min (lo, h); hi = std::max (hi, h); }
        check (cents (hi, lo) > 300, "wheel up: MOD 1 triangle -> pitch vibrato (" + juce::String (cents (hi, lo), 0) + " cents swing)");
    }
    // ---- touch pad X -> pitch
    {
        WayfarerProcessor p; plain (p);
        setP (p, pid::padXDest, choices::dPitch); setP (p, pid::padXAmt, 1.0f); setP (p, pid::padX, 0.5f);
        auto a = run (p, 0.6, { { 0.0, 57, true } });
        check (std::abs (cents (pitchHz (a, 0.3, 0.2), 440.0)) < 10, "touch pad X halfway, amount 100% -> pitch up an octave");
    }
    // ---- gates: drone plays with no keys, LFO gate pulses
    {
        WayfarerProcessor p; plain (p);
        setP (p, pid::gate, 2);
        auto a = run (p, 0.5, {});
        check (rms (a, 0.2, 0.25) > 0.02, "GATE = Drone: sound with no keys held");
        setP (p, pid::gate, 1); setP (p, pid::lfoRate, 4.0f); setP (p, pid::aD, 0.05f); setP (p, pid::aS, 0.0f); setP (p, pid::aR, 0.02f);
        auto b = run (p, 1.0, {});
        int bursts = 0; bool on = false;
        for (int i = 0; i < b.getNumSamples(); i += 240) { const float r = b.getRMSLevel (0, i, 240); if (! on && r > 0.05f) { ++bursts; on = true; } if (on && r < 0.01f) on = false; }
        check (bursts >= 3 && bursts <= 5, "GATE = LFO at 4 Hz: " + juce::String (bursts) + " envelope bursts in a second");
    }
    // ---- 1-2 sync and 3-1 FM change the tone
    {
        WayfarerProcessor p; plain (p);
        setP (p, pid::mix1, 0); setP (p, pid::mix2, 1); setP (p, pid::o2Freq, 5.3f);
        auto a = run (p, 0.5, { { 0.0, 57, true } });
        setP (p, pid::hardSync, 1);
        auto b = run (p, 0.5, { { 0.0, 57, true } });
        check (std::abs (cents (pitchHz (b, 0.2, 0.25), 220.0)) < 6 && std::abs (cents (pitchHz (a, 0.2, 0.25), 220.0)) > 300,
               "1-2 SYNC locks osc 2 to osc 1's pitch");
    }

    // ---- every preset at three rates
    const auto& bank = factoryPresets();
    std::printf ("\n%-3s %-20s %-7s %8s %8s  %s\n", "#", "preset", "cat", "peak", "rms", "status");
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        float peakAll = 0; double rmsMain = 0, rms44 = 0; bool bad = false;
        for (auto [sr, blk] : { std::pair<double, int> { 48000, 512 }, { 44100, 437 }, { 96000, 64 } })
        {
            WayfarerProcessor p;
            p.loadPreset (i);
            const bool lead = bank[(size_t) i].category == "Leads";
            const int note = bank[(size_t) i].category == "Bass" ? 36 : 60;
            std::vector<Ev> ev { { 0.0, note, true }, { 1.6, note, false } };
            if (lead) { ev.push_back ({ 0.8, note + 7, true }); ev.push_back ({ 1.6, note + 7, false }); }
            auto a = run (p, 3.5, ev, sr, blk);
            for (int c = 0; c < 2; ++c)
            {
                peakAll = std::max (peakAll, a.getMagnitude (c, 0, a.getNumSamples()));
                const float* d = a.getReadPointer (c);
                for (int k = 0; k < a.getNumSamples(); ++k) if (! std::isfinite (d[k])) bad = true;
            }
            const double r = rms (a, 0.2, 1.3, 0, sr);
            if (sr == 48000) rmsMain = r; else if (sr == 44100) rms44 = r;
            if (sr == 48000 && argc > 1)
            {
                juce::File dir (argv[1]); dir.createDirectory();
                auto f = dir.getChildFile (juce::String (i).paddedLeft ('0', 2) + "_" + bank[(size_t) i].name.replaceCharacters (" '", "__") + ".wav");
                f.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
                auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
                if (auto w = wav.createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (a, 0, a.getNumSamples());
            }
        }
        juce::String st = "ok";
        if (bad) st = "NaN!";
        else if (peakAll < 0.2f) st = "TOO QUIET";
        else if (peakAll > 1.0f) st = "TOO LOUD";
        else if (std::abs (rmsMain - rms44) > 0.5 * rmsMain) st = "SR MISMATCH";
        if (st != "ok") ++failures;
        std::printf ("%-3d %-20s %-7s %8.3f %8.4f  %s\n", i, bank[(size_t) i].name.toRawUTF8(), bank[(size_t) i].category.toRawUTF8(), peakAll, rmsMain, st.toRawUTF8());
    }

    // ---- state round trip
    {
        WayfarerProcessor a, b;
        a.loadPreset (4); setP (a, pid::padX, 0.3f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = b.getCurrentProgram() == 4;
        for (auto* prm : a.getParameters())
        {
            auto* pa = dynamic_cast<juce::RangedAudioParameter*> (prm);
            auto* pb = dynamic_cast<juce::RangedAudioParameter*> (b.getParameters()[prm->getParameterIndex()]);
            if (pa && pb && std::abs (pa->getValue() - pb->getValue()) > 1e-5f) same = false;
        }
        check (same, "state round-trip");
    }
    // ---- CPU
    {
        WayfarerProcessor p; p.loadPreset (0);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (p, 10.0, { { 0.0, 48, true } });
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (ms < 10000 * 0.15, "CPU: 10 s of audio in " + juce::String (ms, 0) + " ms (" + juce::String (ms / 100.0, 2) + "% of one core)");
    }
    // ---- editor snapshot
    if (auto* snap = std::getenv ("WF_SNAPSHOT"))
    {
        WayfarerProcessor proc;
        proc.loadPreset (std::getenv ("WF_SNAP_PRESET") ? std::atoi (std::getenv ("WF_SNAP_PRESET")) : 0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (WayfarerPanel::kWidth, WayfarerPanel::kHeight);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f (snap); f.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        std::printf ("snapshot written: %s\n", snap);
    }
    std::printf ("\npresets: %d, failures: %d\n", (int) bank.size(), failures);
    return failures == 0 ? 0 : 1;
}
