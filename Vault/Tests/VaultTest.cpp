// Vault offline tests: decay time accuracy, freeze, pre-delay, kill dry, shimmer octave, gated shape,
// every machine at extreme settings, every preset at three rates, CPU and a snapshot.
#define VAULT_NO_ENTRY 1
#include "../Source/Vault.cpp"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static int failures = 0;
static void check (bool ok, const juce::String& w) { std::printf ("%-78s %s\n", w.toRawUTF8(), ok ? "ok" : "FAIL"); if (! ok) ++failures; }
static void setP (VaultProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }

static juce::AudioBuffer<float> run (VaultProcessor& p, const juce::AudioBuffer<float>& in, double sr = 48000, int block = 256, std::function<void (int)> at = nullptr)
{
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    juce::AudioBuffer<float> out (2, in.getNumSamples()), buf (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        if (at) at (pos);
        const int n = std::min (block, in.getNumSamples() - pos);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, pos, n);
        p.processBlock (buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, buf, c, 0, n);
    }
    return out;
}
static juce::AudioBuffer<float> burst (double secs, double len = 0.05, double sr = 48000, float amp = 0.5f)
{
    juce::AudioBuffer<float> b (2, (int) (secs * sr)); b.clear(); juce::Random r (3);
    for (int i = 0; i < (int) (len * sr); ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, (r.nextFloat() * 2 - 1) * amp);
    return b;
}
static double rmsIn (const juce::AudioBuffer<float>& b, double from, double to, double sr = 48000, int ch = 0)
{
    const int s = (int) (from * sr), e = std::min (b.getNumSamples(), (int) (to * sr));
    return e > s ? b.getRMSLevel (ch, s, e - s) : 0.0;
}
// RT60 from the Schroeder energy decay curve, fitted between -5 and -25 dB
static double rt60 (const juce::AudioBuffer<float>& b, double sr = 48000)
{
    const int n = b.getNumSamples();
    std::vector<double> edc ((size_t) n + 1, 0.0);
    for (int i = n - 1; i >= 0; --i) { const double v = 0.5 * (b.getSample (0, i) + b.getSample (1, i)); edc[(size_t) i] = edc[(size_t) i + 1] + v * v; }
    const double top = edc[0];
    int i5 = -1, i25 = -1;
    for (int i = 0; i < n; ++i)
    {
        const double db = 10 * std::log10 (edc[(size_t) i] / top + 1e-30);
        if (i5 < 0 && db <= -5) i5 = i;
        if (i25 < 0 && db <= -25) { i25 = i; break; }
    }
    if (i5 < 0 || i25 < 0) return -1;
    return (i25 - i5) / sr * 3.0;    // 20 dB took this long: 60 dB takes three times as long
}
static double goertzel (const juce::AudioBuffer<float>& b, double hz, double from, double len, double sr = 48000)
{
    const int s = (int) (from * sr), n = (int) (len * sr);
    const double w = 2 * juce::MathConstants<double>::pi * hz / sr, c = 2 * std::cos (w);
    double q1 = 0, q2 = 0;
    for (int i = 0; i < n; ++i) { const double q0 = c * q1 - q2 + b.getSample (0, s + i); q2 = q1; q1 = q0; }
    return std::sqrt (q1 * q1 + q2 * q2 - c * q1 * q2) / n;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // ---- decay accuracy (hall, flat low end, plate)
    for (auto [m, d] : { std::pair<int, float> { vault::mHall, 2.0f }, { vault::mHall, 6.0f }, { vault::mPlate, 3.0f }, { vault::mRoom, 0.8f } })
    {
        VaultProcessor p; p.loadPreset (-1);
        setP (p, vid::machine, (float) m); setP (p, vid::decay, d); setP (p, vid::predelay, 0); setP (p, vid::mod, 0.1f);
        setP (p, vid::highCut, 20000); setP (p, vid::lowCut, 20);
        if (m == vault::mHall) setP (p, vid::p2, 1.0f / 3.0f);     // low end x1
        if (m == vault::mPlate) setP (p, vid::p1, 1.0f / 3.0f);
        if (m == vault::mRoom) setP (p, vid::p2, 0.0f);
        auto out = run (p, burst (d * 2.5 + 1.0, 0.02));
        const double t = rt60 (out);
        check (t > d * 0.75 && t < d * 1.3, juce::String (vault::machineName (m)) + " DECAY " + juce::String (d, 1) + " s: measured " + juce::String (t, 2) + " s");
    }
    // ---- low end decays longer when asked
    {
        VaultProcessor p; p.loadPreset (-1);
        setP (p, vid::machine, vault::mHall); setP (p, vid::decay, 2.0f); setP (p, vid::p2, 1.0f); setP (p, vid::lowCut, 20); setP (p, vid::predelay, 0);
        auto out = run (p, burst (5.0, 0.02));
        const double lo = goertzel (out, 120, 2.0, 0.5), hi = goertzel (out, 2000, 2.0, 0.5);
        const double lo0 = goertzel (out, 120, 0.2, 0.5), hi0 = goertzel (out, 2000, 0.2, 0.5);
        check ((lo / lo0) > 3.0 * (hi / hi0), "HALL LOW END 100%: bass outlasts the mids");
    }
    // ---- pre-delay
    {
        VaultProcessor p; p.loadPreset (-1);
        setP (p, vid::predelay, 200); setP (p, vid::machine, vault::mHall);
        juce::AudioBuffer<float> in (2, 48000); in.clear(); in.setSample (0, 0, 1); in.setSample (1, 0, 1);
        auto out = run (p, in);
        int first = -1; for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > 1e-4f) { first = i; break; }
        check (first >= 0.2 * 48000 && first < 0.24 * 48000, "PRE-DELAY 200 ms: nothing until " + juce::String (first / 48.0, 1) + " ms");
    }
    // ---- kill dry / mix
    {
        VaultProcessor p; p.loadPreset (-1); setP (p, vid::predelay, 100);
        juce::AudioBuffer<float> in (2, 4800); in.clear(); in.setSample (0, 100, 1); in.setSample (1, 100, 1);
        auto out = run (p, in);
        check (std::abs (out.getSample (0, 100)) < 1e-6f, "KILL DRY on (return default): no dry");
        setP (p, vid::killDry, 0);
        auto out2 = run (p, in);
        check (std::abs (out2.getSample (0, 100) - 1.0f) < 1e-3f, "KILL DRY off, MIX 50%: dry at unity");
    }
    // ---- freeze holds
    {
        VaultProcessor p; p.loadPreset (-1); setP (p, vid::machine, vault::mHall); setP (p, vid::decay, 2.0f);
        auto in = burst (8.0, 1.0, 48000, 0.3f);
        auto out = run (p, in, 48000, 256, [&p] (int pos) { if (pos >= 48000 * 1.1) setP (p, vid::freeze, 1); });
        const double a = rmsIn (out, 1.4, 2.0), b = rmsIn (out, 7.2, 7.9);
        check (a > 0.005 && std::abs (juce::Decibels::gainToDecibels (b / a)) < 3.0, "FREEZE holds the tail (" + juce::String (juce::Decibels::gainToDecibels (b / a), 1) + " dB after 6 s)");
    }
    // ---- shimmer: an octave grows out of the tail
    {
        VaultProcessor p; p.loadPreset (-1); setP (p, vid::machine, vault::mShimmer); setP (p, vid::p1, 0.0f); setP (p, vid::p2, 0.8f); setP (p, vid::decay, 6.0f);
        setP (p, vid::lowCut, 20);
        juce::AudioBuffer<float> in (2, 48000 * 4); in.clear();
        for (int i = 0; i < 24000; ++i) for (int c = 0; c < 2; ++c) in.setSample (c, i, std::sin (juce::MathConstants<float>::twoPi * 220.0f * i / 48000.0f) * 0.3f);
        auto out = run (p, in);
        VaultProcessor q; q.loadPreset (-1); setP (q, vid::machine, vault::mHall); setP (q, vid::decay, 6.0f); setP (q, vid::lowCut, 20);
        auto plain = run (q, in);
        const double rs = goertzel (out, 440, 2.5, 1.0) / goertzel (out, 220, 2.5, 1.0), rp = goertzel (plain, 440, 2.5, 1.0) / goertzel (plain, 220, 2.5, 1.0);
        check (rs > 5 * rp, "SHIMMER +12: octave above builds in the tail (" + juce::String (rs, 2) + " vs " + juce::String (rp, 3) + " in HALL)");
    }
    // ---- nonlinear gate closes
    {
        VaultProcessor p; p.loadPreset (-1); setP (p, vid::machine, vault::mNonlinear); setP (p, vid::p1, 0.1f); setP (p, vid::decay, 1.6f); setP (p, vid::predelay, 0);
        auto out = run (p, burst (2.0, 0.03));
        const double in = rmsIn (out, 0.1, 0.35), after = rmsIn (out, 0.55, 1.0);
        check (in > 0.01 && after < in * 0.02, "NONLINEAR gated: full tail, then shut (" + juce::String (juce::Decibels::gainToDecibels (after / in), 0) + " dB after the gate)");
    }
    // ---- every machine: tail present, bounded at extreme settings
    {
        juce::String bad;
        for (int m = 0; m < vault::kNumMachines; ++m)
        {
            VaultProcessor p; p.loadPreset (-1); setP (p, vid::machine, (float) m); setP (p, vid::p1, 0.0f);
            auto out = run (p, burst (2.0, 0.1));
            const double tail = rmsIn (out, 0.4, 0.8);
            if (tail < 0.002 && m != vault::mNonlinear) bad << vault::machineName (m) << " no tail (" << tail << ") ";
            VaultProcessor x; x.loadPreset (-1); setP (x, vid::machine, (float) m);
            for (auto* id : { vid::p1, vid::p2, vid::mod, vid::tone, vid::width }) setP (x, id, 1.0f);
            setP (x, vid::decay, 60); setP (x, vid::killDry, 0);
            juce::AudioBuffer<float> in (2, 48000 * 10); in.clear(); juce::Random r (9);
            for (int i = 0; i < 48000 * 4; ++i) for (int c = 0; c < 2; ++c) in.setSample (c, i, (r.nextFloat() * 2 - 1) * 0.9f);
            auto o2 = run (x, in);
            float pk = 0; bool nan = false;
            for (int c = 0; c < 2; ++c) { const float* d = o2.getReadPointer (c); for (int i = 0; i < o2.getNumSamples(); ++i) { if (! std::isfinite (d[i])) nan = true; pk = std::max (pk, std::abs (d[i])); } }
            if (nan || pk > 6.0f) bad << vault::machineName (m) << " peak " << juce::String (pk, 1) << (nan ? " NaN " : " ");
        }
        check (bad.isEmpty(), "every machine makes a tail and stays bounded at 60 s decay, all knobs up " + bad);
    }
    // ---- presets
    {
        juce::String bad;
        for (int i = 0; i < (int) vaultPresets().size(); ++i)
            for (auto [sr, blk] : { std::pair<double, int> { 44100, 437 }, { 48000, 512 }, { 96000, 64 } })
            {
                VaultProcessor p; p.loadPreset (i);
                juce::AudioBuffer<float> in (2, (int) (sr * 3)); in.clear();
                for (int k = 0; k < (int) (sr * 0.3); ++k) for (int c = 0; c < 2; ++c) in.setSample (c, k, std::sin (k * 0.02f) * 0.5f * (k % 9000 < 3000 ? 1.0f : 0.0f));
                auto out = run (p, in, sr, blk);
                const float pk = out.getMagnitude (0, 0, out.getNumSamples());
                const double tail = out.getRMSLevel (0, (int) (sr * 0.35), (int) (sr * 0.5));
                if (! std::isfinite (pk) || pk > 1.2f || tail < 0.002) bad << vaultPresets()[(size_t) i].name << " @" << sr << " (" << pk << ", " << tail << ") ";
            }
        check (bad.isEmpty(), "16 presets at 44.1 / 48 / 96 kHz: tails, no clipping, no NaN " + bad);
    }
    // ---- state, CPU
    {
        VaultProcessor a, b; a.loadPreset (8); setP (a, vid::decay, 12);
        juce::MemoryBlock mb; a.getStateInformation (mb); b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (b.v (vid::decay) - 12) < 1e-3f && b.v (vid::machine) == 6.0f, "state round-trip (" + juce::String (b.v (vid::decay)) + ", " + juce::String (b.v (vid::machine)) + ")");
        double worst = 0; juce::String wn;
        for (int m = 0; m < vault::kNumMachines; ++m)
        {
            VaultProcessor p; p.loadPreset (-1); setP (p, vid::machine, (float) m);
            auto in = burst (5.0, 5.0, 48000, 0.2f);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            run (p, in);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            if (ms > worst) { worst = ms; wn = vault::machineName (m); }
        }
        check (worst < 500, "CPU: heaviest machine (" + wn + ") " + juce::String (worst / 50.0, 2) + "% of a core");
    }
    {
        VaultProcessor p; p.loadPreset (10);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        ed->setSize (toota::PedalPanel::W, toota::PedalPanel::H);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile ("vault_ui.png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        check (img.isValid(), "editor snapshot");
    }
    std::printf ("\nfailures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
