// Toota Strip + Master offline tests
#define STRIP_NO_ENTRY 1
#define MASTER_NO_ENTRY 1
#include "../Source/Strip.cpp"
#include "../Source/Master.cpp"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

static int failures = 0;
static void check (bool ok, const juce::String& w) { std::printf ("%-80s %s\n", w.toRawUTF8(), ok ? "ok" : "FAIL"); if (! ok) ++failures; }
template <class P> static void setP (P& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }

static juce::AudioBuffer<float> pink (double secs, float rmsDb, double sr = 48000, bool stereoDiff = false)
{
    juce::AudioBuffer<float> b (2, (int) (secs * sr)); juce::Random r (5);
    float p0 = 0, p1 = 0, p2 = 0, q0 = 0, q1 = 0, q2 = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float w = r.nextFloat() * 2 - 1, w2 = r.nextFloat() * 2 - 1;
        p0 = 0.99765f * p0 + w * 0.0990460f; p1 = 0.96300f * p1 + w * 0.2965164f; p2 = 0.57000f * p2 + w * 1.0526913f;
        q0 = 0.99765f * q0 + w2 * 0.0990460f; q1 = 0.96300f * q1 + w2 * 0.2965164f; q2 = 0.57000f * q2 + w2 * 1.0526913f;
        const float a = p0 + p1 + p2 + w * 0.1848f, c = q0 + q1 + q2 + w2 * 0.1848f;
        b.setSample (0, i, a); b.setSample (1, i, stereoDiff ? c : a);
    }
    const float rms = b.getRMSLevel (0, 0, b.getNumSamples());
    b.applyGain (mix::dbToGain (rmsDb) / rms);
    return b;
}
template <class P> static juce::AudioBuffer<float> run (P& p, juce::AudioBuffer<float> in, double sr = 48000, int block = 512)
{
    p.setRateAndBufferSizeDetails (sr, block); p.prepareToPlay (sr, block);
    juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer m;
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = std::min (block, in.getNumSamples() - pos);
        buf.setSize (2, n, false, false, true);
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, pos, n);
        p.processBlock (buf, m);
        for (int c = 0; c < 2; ++c) in.copyFrom (c, pos, buf, c, 0, n);
    }
    return in;
}
static double rmsDb (const juce::AudioBuffer<float>& b, double from, double to, int ch = 0, double sr = 48000)
{ return mix::gainToDb (b.getRMSLevel (ch, (int) (from * sr), (int) ((to - from) * sr))); }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    // ---------------- STRIP
    {
        StripProcessor p; p.setRateAndBufferSizeDetails (48000, 512); p.prepareToPlay (48000, 512);
        p.startLearn();
        auto in = pink (8.0, -30.0f);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer m;
        for (int pos = 0; pos + 512 <= in.getNumSamples(); pos += 512) { for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, in, c, pos, 512); p.processBlock (buf, m); }
        p.pollForTests();
        check (std::abs (p.v (sid::trim) - 12.0f) < 0.6f, "LEVEL: -30 dBFS RMS channel gets +12 dB trim to sit at -18 (" + juce::String (p.v (sid::trim), 2) + ")");
    }
    {
        juce::String worst; float maxDev = 0;
        for (int t = 0; t < strip::kNumTypes; ++t)
        {
            StripProcessor p; setP (p, sid::type, (float) t);
            auto out = run (p, pink (3.0, -18.0f));
            const float dev = (float) rmsDb (out, 1.0, 3.0) + 18.0f;
            if (std::abs (dev) > std::abs (maxDev)) { maxDev = dev; worst = strip::types()[t].name; }
        }
        check (std::abs (maxDev) < 6.0f, "every source type at default keeps the level within 6 dB (largest " + juce::String (maxDev, 1) + " dB on " + worst + ")");
    }
    {
        StripProcessor p; setP (p, sid::type, 0);   // KICK
        auto out = run (p, pink (3.0, -18.0f, 48000, true));
        double side = 0, mid = 0;
        for (int i = 48000; i < 144000; ++i) { const float l = out.getSample (0, i), r = out.getSample (1, i); side += (l - r) * (l - r); mid += (l + r) * (l + r); }
        check (side < mid * 1e-4, "KICK: output is mono");
        StripProcessor q; setP (q, sid::type, 5);   // CHORDS: mono below 150, width above
        auto sideTone = [] (float hz) { juce::AudioBuffer<float> b (2, 96000); for (int i = 0; i < 96000; ++i) { const float v = 0.1f * std::sin (2 * juce::MathConstants<float>::pi * hz * i / 48000.0f); b.setSample (0, i, v); b.setSample (1, i, -v); } return b; };
        auto lo = run (q, sideTone (50)); auto hi = run (q, sideTone (2000));
        const double loDb = rmsDb (lo, 1, 2) - mix::gainToDb (0.0707f), hiDb = rmsDb (hi, 1, 2) - mix::gainToDb (0.0707f);
        check (loDb < -25 && hiDb > -4, "CHORDS: stereo content at 50 Hz folded to mono (" + juce::String (loDb, 0) + " dB), 2 kHz stays wide (" + juce::String (hiDb, 1) + " dB)");
    }
    {
        StripProcessor p; setP (p, sid::type, 7); setP (p, sid::clean, 1.0f);   // VOCAL, more clean-up
        juce::AudioBuffer<float> in (2, 96000);
        for (int i = 0; i < 96000; ++i) for (int c = 0; c < 2; ++c) in.setSample (c, i, 0.1f * std::sin (2 * juce::MathConstants<float>::pi * 50.0f * i / 48000.0f));
        auto out = run (p, in);
        check (rmsDb (out, 1.0, 2.0) < rmsDb (in, 1.0, 2.0) - 18, "VOCAL with CLEAN up: 50 Hz rumble down " + juce::String (rmsDb (in, 1.0, 2.0) - rmsDb (out, 1.0, 2.0), 0) + " dB");
    }
    {
        bool ok = true;
        for (int t = 0; t < strip::kNumTypes; ++t)
            for (float m : { 0.0f, 1.0f })
            {
                StripProcessor p; setP (p, sid::type, (float) t);
                for (auto* id : sid::macros) setP (p, id, m);
                auto out = run (p, pink (2.0, -6.0f));
                for (int c = 0; c < 2; ++c) for (int i = 0; i < out.getNumSamples(); ++i) if (! std::isfinite (out.getSample (c, i)) || std::abs (out.getSample (c, i)) > 8) ok = false;
            }
        check (ok, "every type with all macros at 0 and at 100%: stable, no NaN");
    }
    // ---------------- MASTER
    {
        MasterProcessor p; p.loadPreset (7);   // Demo Check: no target, no glue/warmth
        juce::AudioBuffer<float> in (2, 48000 * 5);
        for (int i = 0; i < in.getNumSamples(); ++i) for (int c = 0; c < 2; ++c) in.setSample (c, i, mix::dbToGain (-20) * std::sin (2 * juce::MathConstants<float>::pi * 1000.0f * i / 48000.0f));
        run (p, in);
        check (std::abs (p.engine.loud.shortTerm + 20.0) < 0.3, "LUFS meter: 1 kHz at -20 dBFS on both channels reads " + juce::String (p.engine.loud.shortTerm, 2) + " LUFS (spec: -20.0)");
    }
    for (int preset : { 0, 1, 5 })
    {
        MasterProcessor p; p.loadPreset (preset);
        auto out = run (p, pink (40.0, -26.0f, 48000, true));
        const float tgt = targetLufs ((int) p.v (mid::target));
        juce::dsp::Oversampling<float> os (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
        os.initProcessing ((size_t) out.getNumSamples());
        juce::AudioBuffer<float> copy (out);
        juce::dsp::AudioBlock<float> blk (copy);
        auto up = os.processSamplesUp (blk);
        float tp = 0; for (int c = 0; c < 2; ++c) for (size_t i = 0; i < up.getNumSamples(); ++i) tp = std::max (tp, std::abs (up.getSample (c, (int) i)));
        const float ceil = p.v (mid::ceiling);
        check (std::abs (p.engine.loud.shortTerm - tgt) < 1.0,
               juce::String (masterPresets()[(size_t) preset].name) + ": auto gain lands at " + juce::String (p.engine.loud.shortTerm, 1) + " LUFS (target " + juce::String (tgt, 0) + ")");
        check (mix::gainToDb (tp) <= ceil + 0.3f, juce::String (masterPresets()[(size_t) preset].name) + ": true peak " + juce::String (mix::gainToDb (tp), 2) + " dBTP, ceiling " + juce::String (ceil, 1));
    }
    {
        MasterProcessor p; p.loadPreset (7);
        p.setRateAndBufferSizeDetails (48000, 512); p.prepareToPlay (48000, 512);
        juce::AudioBuffer<float> in (2, 48000); in.clear();
        for (int i = 2000; i < 2600; ++i) for (int c = 0; c < 2; ++c) in.setSample (c, i, 0.3f * std::sin (2 * juce::MathConstants<float>::pi * 1000.0f * (i - 2000) / 48000.0f) * std::sin (juce::MathConstants<float>::pi * (i - 2000) / 600.0f));
        auto out = run (p, in);
        int pi = 0, po = 0; float bi = 0, bo = 0;
        for (int i = 0; i < 48000; ++i) { if (std::abs (in.getSample (0, i)) > bi) { bi = std::abs (in.getSample (0, i)); pi = i; } if (std::abs (out.getSample (0, i)) > bo) { bo = std::abs (out.getSample (0, i)); po = i; } }
        check (std::abs ((po - pi) - p.getLatencySamples()) <= 3, "reported latency matches (" + juce::String (p.getLatencySamples()) + " vs measured " + juce::String (po - pi) + ")");
    }
    {
        bool ok = true;
        for (int i = 0; i < (int) masterPresets().size(); ++i)
            for (double sr : { 44100.0, 96000.0 })
            {
                MasterProcessor p; p.loadPreset (i);
                auto out = run (p, pink (6.0, -14.0f, sr, true), sr, 256);
                for (int c = 0; c < 2; ++c) for (int k = 0; k < out.getNumSamples(); ++k) if (! std::isfinite (out.getSample (c, k)) || std::abs (out.getSample (c, k)) > 1.0f) ok = false;
            }
        check (ok, "every master preset at 44.1 and 96 kHz: no NaN, never over 0 dBFS");
    }
    {
        MasterProcessor p; const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (p, pink (10.0, -14.0f, 48000, true));
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (ms < 1500, "Master CPU: " + juce::String (ms / 100.0, 2) + "% of a core");
        StripProcessor s; const auto t1 = juce::Time::getMillisecondCounterHiRes();
        run (s, pink (10.0, -14.0f, 48000, true));
        check (true, "Strip CPU: " + juce::String ((juce::Time::getMillisecondCounterHiRes() - t1) / 100.0, 2) + "% of a core");
    }
    // snapshots
    auto snap = [] (juce::AudioProcessor& p, const char* file)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (file); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        return img.isValid();
    };
    { StripProcessor s; setP (s, sid::type, 7); check (snap (s, "strip_ui.png"), "Strip snapshot"); }
    { MasterProcessor m; run (m, pink (5.0, -20.0f, 48000, true)); check (snap (m, "master_ui.png"), "Master snapshot"); }
    std::printf ("\nfailures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
