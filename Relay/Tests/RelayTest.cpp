// Relay offline tests: tempo-synced timing, repeats, hold, kill dry, reverse, ice, pattern taps,
// stability of every machine at runaway settings, every preset at three rates, CPU and a snapshot.
#define RELAY_NO_ENTRY 1
#include "../Source/Relay.cpp"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static int failures = 0;
static void check (bool ok, const juce::String& w) { std::printf ("%-76s %s\n", w.toRawUTF8(), ok ? "ok" : "FAIL"); if (! ok) ++failures; }
static void setP (RelayProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }

static juce::AudioBuffer<float> run (RelayProcessor& p, const juce::AudioBuffer<float>& in, double sr = 48000, int block = 256,
                                     std::function<void (int)> atBlock = nullptr)
{
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    juce::AudioBuffer<float> out (2, in.getNumSamples()), buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0, k = 0; pos < in.getNumSamples(); pos += block, ++k)
    {
        if (atBlock) atBlock (pos);
        const int n = std::min (block, in.getNumSamples() - pos);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, pos, n);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
    }
    return out;
}
static juce::AudioBuffer<float> impulse (double secs, double at = 0.1, double sr = 48000)
{
    juce::AudioBuffer<float> b (2, (int) (secs * sr)); b.clear();
    b.setSample (0, (int) (at * sr), 1.0f); b.setSample (1, (int) (at * sr), 1.0f);
    return b;
}
static juce::AudioBuffer<float> noise (double secs, double len, double sr = 48000, float amp = 0.3f)
{
    juce::AudioBuffer<float> b (2, (int) (secs * sr)); b.clear(); juce::Random r (7);
    for (int i = 0; i < (int) (len * sr); ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, (r.nextFloat() * 2 - 1) * amp);
    return b;
}
// time of the biggest absolute sample within [from, to)
static double peakTime (const juce::AudioBuffer<float>& b, double from, double to, double sr = 48000)
{
    int best = (int) (from * sr); float bv = 0;
    for (int i = (int) (from * sr); i < std::min (b.getNumSamples(), (int) (to * sr)); ++i) { const float v = std::abs (b.getSample (0, i)); if (v > bv) { bv = v; best = i; } }
    return best / sr;
}
static float peakIn (const juce::AudioBuffer<float>& b, double from, double to, double sr = 48000, int ch = 0)
{
    const int s = (int) (from * sr), e = std::min (b.getNumSamples(), (int) (to * sr));
    return e > s ? b.getMagnitude (ch, s, e - s) : 0.0f;
}
static double rmsIn (const juce::AudioBuffer<float>& b, double from, double to, double sr = 48000)
{
    const int s = (int) (from * sr), e = std::min (b.getNumSamples(), (int) (to * sr));
    return b.getRMSLevel (0, s, e - s);
}
static double pitchHz (const juce::AudioBuffer<float>& b, double from, double len, double sr = 48000)
{
    const int s0 = (int) (from * sr), n = (int) (len * sr);
    const float* x = b.getReadPointer (0) + s0;
    double best = 0; int bl = 0;
    for (int lag = (int) (sr / 2000); lag < (int) (sr / 60); ++lag)
    {
        double s = 0, e1 = 0, e2 = 0;
        for (int i = 0; i + lag < n; ++i) { s += x[i] * x[i + lag]; e1 += x[i] * x[i]; e2 += x[i + lag] * x[i + lag]; }
        const double r = s / std::sqrt (e1 * e2 + 1e-20);
        if (r > best + 0.02) { best = r; bl = lag; }
    }
    return bl ? sr / bl : 0;
}
static void fresh (RelayProcessor& p, int machine)
{
    p.loadPreset (-1);
    setP (p, rid::machine, (float) machine); setP (p, rid::depth, 0); setP (p, rid::p2, 0); setP (p, rid::lowCut, 20);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // ---- timing: 1/4 at 120 BPM = 500 ms
    for (int m : { relay::mDigital, relay::mTape, relay::mBucket, relay::mDuck, relay::mTrem, relay::mSwell })
    {
        RelayProcessor p; fresh (p, m);
        if (m == relay::mSwell) setP (p, rid::p1, 0.0f);
        auto in = impulse (1.5);
        if (m == relay::mSwell) { in = noise (1.5, 0.0); for (int i = 4800; i < 4800 + 480; ++i) for (int c = 0; c < 2; ++c) in.setSample (c, i, std::sin (i * 0.3f) * 0.8f); }
        auto out = run (p, in);
        const double t = peakTime (out, 0.3, 0.9);
        check (std::abs (t - 0.6) < 0.0025 || (m == relay::mSwell && std::abs (t - 0.6) < 0.012),
               juce::String (relay::machineName (m)) + ": first repeat 500 ms after the hit (" + juce::String ((t - 0.1) * 1000, 1) + " ms)");
    }
    // ---- free time + repeats fall away
    {
        RelayProcessor p; fresh (p, relay::mDigital);
        setP (p, rid::tempoSync, 0); setP (p, rid::delayTime, 300); setP (p, rid::repeats, 0.5f); setP (p, rid::filter, 1.0f); setP (p, rid::p1, 0.0f);
        auto out = run (p, impulse (1.5));
        const double t = peakTime (out, 0.2, 0.55);
        const float a = peakIn (out, 0.35, 0.45), b = peakIn (out, 0.65, 0.75);
        check (std::abs (t - 0.4) < 0.002, "free time 300 ms (" + juce::String ((t - 0.1) * 1000, 1) + " ms)");
        check (b / a > 0.4f && b / a < 0.55f, "REPEATS 50%: second echo about half the first (" + juce::String (b / a, 2) + ", " + juce::String (a, 3) + " / " + juce::String (b, 3) + ")");
    }
    // ---- kill dry and mix
    {
        RelayProcessor p; fresh (p, relay::mDigital);
        auto out = run (p, impulse (0.3));
        check (peakIn (out, 0.09, 0.11) < 1e-4f, "KILL DRY (default on a return): no dry signal");
        setP (p, rid::killDry, 0);
        auto out2 = run (p, impulse (0.3));
        check (std::abs (peakIn (out2, 0.09, 0.11) - 1.0f) < 0.02f, "KILL DRY off, MIX 50%: dry passes at unity");
    }
    // ---- hold
    {
        RelayProcessor p; fresh (p, relay::mDigital);
        setP (p, rid::repeats, 0.6f);
        auto in = noise (6.0, 1.0);
        auto out = run (p, in, 48000, 256, [&p] (int pos) { if (pos >= 48000 * 1.2) setP (p, rid::hold, 1); });
        const double a = rmsIn (out, 1.3, 1.8), b = rmsIn (out, 5.2, 5.8);
        check (a > 0.01 && std::abs (juce::Decibels::gainToDecibels (b / a)) < 3.0, "HOLD keeps the loop going at the same level (" + juce::String (juce::Decibels::gainToDecibels (b / a), 1) + " dB after 4 s)");
    }
    // ---- reverse: a sharp, decaying note comes back swelling
    {
        RelayProcessor p; fresh (p, relay::mReverse);
        setP (p, rid::repeats, 0.0f); setP (p, rid::p1, 0.0f);
        juce::AudioBuffer<float> in (2, 48000 * 3); in.clear();
        for (int i = 0; i < 24000; ++i) for (int c = 0; c < 2; ++c) in.setSample (c, 4800 + i, std::sin (i * 0.05f) * std::exp (-i / 4000.0f) * 0.8f);
        auto out = run (p, in);
        // the reversed copy: find its loudest point and check the energy before it is a slow rise
        const double t = peakTime (out, 0.35, 2.5);
        const double before = rmsIn (out, t - 0.06, t - 0.03), atPk = rmsIn (out, t - 0.01, t + 0.005);
        check (before < atPk * 0.8, "REVERSE: the note swells up to its attack (" + juce::String (before / atPk, 2) + " level 50 ms before the peak, peak at " + juce::String (t, 3) + ")");
    }
    // ---- ice: repeats an octave up
    {
        RelayProcessor p; fresh (p, relay::mIce);
        setP (p, rid::repeats, 0.0f); setP (p, rid::p1, 0.7f); setP (p, rid::p2, 1.0f); setP (p, rid::filter, 1.0f);
        juce::AudioBuffer<float> in (2, 48000 * 2); in.clear();
        for (int i = 0; i < 19200; ++i) for (int c = 0; c < 2; ++c) in.setSample (c, i, std::sin (juce::MathConstants<float>::twoPi * 220.0f * i / 48000.0f) * 0.5f);
        auto out = run (p, in);
        const double hz = pitchHz (out, 0.65, 0.12);
        check (std::abs (hz - 440) < 20, "ICE +12: repeats come back an octave up (" + juce::String (hz, 0) + " Hz from 220)");
    }
    // ---- pattern: number of taps
    {
        RelayProcessor p; fresh (p, relay::mPattern);
        setP (p, rid::repeats, 0.0f); setP (p, rid::p1, 0.0f); setP (p, rid::filter, 1.0f);
        auto out = run (p, impulse (1.0));
        int taps = 0; for (double t = 0.1 + 0.125; t < 0.62; t += 0.125) if (peakIn (out, t - 0.004, t + 0.004) > 0.15f) ++taps;
        check (taps == 4, "PATTERN 1: four taps across the quarter note (" + juce::String (taps) + ")");
    }
    // ---- every machine at runaway settings stays bounded
    {
        bool ok = true; juce::String worst;
        for (int m = 0; m < relay::kNumMachines; ++m)
        {
            RelayProcessor p; p.loadPreset (-1);
            setP (p, rid::machine, (float) m); setP (p, rid::repeats, 1.1f); setP (p, rid::grit, 1); setP (p, rid::p1, 1); setP (p, rid::p2, 1);
            setP (p, rid::depth, 1); setP (p, rid::speed, 1); setP (p, rid::width, 1); setP (p, rid::tempoSync, 0); setP (p, rid::delayTime, 40);
            auto out = run (p, noise (8.0, 3.0, 48000, 0.9f));
            float pk = 0; bool bad = false;
            for (int c = 0; c < 2; ++c) { const float* d = out.getReadPointer (c); for (int i = 0; i < out.getNumSamples(); ++i) { if (! std::isfinite (d[i])) bad = true; pk = std::max (pk, std::abs (d[i])); } }
            if (bad || pk > 6.0f) { ok = false; worst << relay::machineName (m) << " peak " << juce::String (pk, 1) << (bad ? " NaN " : " "); }
        }
        check (ok, "every machine at REPEATS 110%, full grit / depth: bounded, no NaN " + worst);
    }
    // ---- presets at three rates
    {
        bool ok = true; juce::String bad;
        for (int i = 0; i < (int) relayPresets().size(); ++i)
            for (auto [sr, blk] : { std::pair<double, int> { 44100, 437 }, { 48000, 512 }, { 96000, 64 } })
            {
                RelayProcessor p; p.loadPreset (i);
                juce::AudioBuffer<float> in (2, (int) (sr * 3)); in.clear();
                for (int k = 0; k < (int) (sr * 0.2); ++k) for (int c = 0; c < 2; ++c) in.setSample (c, k, std::sin (k * 0.03f) * 0.5f);
                auto out = run (p, in, sr, blk);
                const float pk = out.getMagnitude (0, 0, out.getNumSamples());
                const double tail = out.getRMSLevel (0, (int) (sr * 0.4), (int) (sr * 1.5));
                if (! std::isfinite (pk) || pk > 1.5f || (tail < 1e-4 && i != 10)) { ok = false; bad << relayPresets()[(size_t) i].name << " @" << sr << " (" << pk << ", " << tail << ") "; }
            }
        check (ok, "15 presets at 44.1 / 48 / 96 kHz: echoes, no clipping, no NaN " + bad);
    }
    // ---- state, CPU
    {
        RelayProcessor a, b; a.loadPreset (4); setP (a, rid::grit, 0.9f);
        juce::MemoryBlock mb; a.getStateInformation (mb); b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b.v (rid::grit) - 0.9f) < 1e-4f && b.v (rid::machine) == 1.0f, "state round-trip");
        RelayProcessor p; p.loadPreset (2);
        auto in = noise (10.0, 10.0);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (p, in);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (ms < 1000, "CPU: 10 s in " + juce::String (ms, 0) + " ms (" + juce::String (ms / 100, 2) + "% of a core)");
    }
    // ---- snapshot
    {
        RelayProcessor p; p.loadPreset (3);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (toota::PedalPanel::W, toota::PedalPanel::H);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile ("relay_ui.png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        check (img.isValid(), "editor snapshot");
    }
    std::printf ("\nfailures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
