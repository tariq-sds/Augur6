// Tests for Interlock's Pro-Q style EQ: filter accuracy, slopes, engine (zero latency and linear phase),
// stereo placement, dynamics with external sidechain, solo, auto gain, EQ match, collision registry, state, CPU.
#pragma once
#include <cstdio>

namespace eqtest
{
using namespace ileq;
static int fails = 0;
static void check (bool ok, const juce::String& what) { std::printf ("  %-78s %s\n", what.toRawUTF8(), ok ? "ok" : "FAIL"); if (! ok) ++fails; }
static const double fs = 48000.0;

static double analogBellDb (double f0, double gDb, double q, double hz)
{
    const double A = std::pow (10.0, gDb / 40.0), w = 2 * kPi * f0;
    return 10 * std::log10 (analogMag2 ({ 1, A * w / q, w * w, 1, w / (A * q), w * w }, 2 * kPi * hz));
}
static double rbjBellDb (double f0, double gDb, double q, double hz)   // ordinary bilinear (for comparison)
{
    const double A = std::pow (10.0, gDb / 40.0), w0 = 2 * kPi * f0 / fs, al = std::sin (w0) / (2 * q);
    Section s; const double a0 = 1 + al / A;
    s.c = { (1 + al * A) / a0, -2 * std::cos (w0) / a0, (1 - al * A) / a0, -2 * std::cos (w0) / a0, (1 - al / A) / a0 };
    const auto ph = phiOf (hz, fs);
    return 10 * std::log10 (s.mag2 (ph.p0, ph.p1, ph.p2));
}
static double specDb (const BandParams& p, double hz) { return Engine::specDb (p, 1.0f, fs, hz); }   // floor -300 dB
static BandParams band (int shape, double f, double g, double q, int slopeIdx = -1)
{
    BandParams p; p.used = true; p.shape = shape; p.freq = (float) f; p.gainDb = (float) g; p.q = (float) q;
    p.slopeIndex = slopeIdx < 0 ? defaultSlopeIndex (shape) : slopeIdx;
    return p;
}

// level of a steady sine through the engine (after it settles), dB relative to the input
static double sineGain (Engine& e, double hz, bool stereoSide = false, int lat = 0)
{
    const int n = (int) fs * 1;
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int i = 0; i < n; ++i) { const float v = 0.25f * (float) std::sin (2 * kPi * hz * i / fs); l[(size_t) i] = v; r[(size_t) i] = stereoSide ? -v : v; }
    for (int i = 0; i < n; i += 512) e.process (l.data() + i, r.data() + i, std::min (512, n - i), nullptr, true);
    double se = 0;
    for (int i = n / 2; i < n; ++i) se += (double) l[(size_t) i] * l[(size_t) i];
    juce::ignoreUnused (lat);
    return 10 * std::log10 (se / (n / 2) / (0.25 * 0.25 * 0.5));
}

static int run()
{
    std::printf ("EQ (Pro-Q style)\n");
    // ---- 1. matched bells: no cramping near Nyquist
    for (double f0 : { 100.0, 1000.0, 8000.0, 15000.0, 19000.0 })
    {
        double worst = 0, worstRbj = 0;
        auto d = design ({ shBell, f0, 12.0, 2.0, 1 }, fs);
        for (double o = -2; o <= 2.001; o += 0.125)
        {
            const double hz = f0 * std::pow (2.0, o);
            if (hz > fs * 0.49 || hz < 10) continue;
            const double ref = analogBellDb (f0, 12, 2, hz);
            const double dig = 10 * std::log10 (designMag2 (d, phiOf (hz, fs)));
            worst = std::max (worst, std::abs (dig - ref));
            worstRbj = std::max (worstRbj, std::abs (rbjBellDb (f0, 12, 2, hz) - ref));
        }
        const double peak = 10 * std::log10 (designMag2 (d, phiOf (f0, fs)));
        check (worst < (f0 > 16000 ? 1.5 : f0 >= 12000 ? 1.0 : 0.6) && std::abs (peak - 12) < 0.1,
               "bell +12 dB Q2 at " + juce::String (f0, 0) + " Hz: worst " + juce::String (worst, 2) + " dB off analogue (plain bilinear: " + juce::String (worstRbj, 2) + " dB)");
    }
    // ---- 2. cut slopes
    for (int si : { 1, 3, 6, 8 })
    {
        const int slope = slopes()[(size_t) si];
        const auto p = band (shLowCut, 200, 0, 0.7071, si);
        const double at200 = specDb (p, 200), at50 = specDb (p, 50), at25 = specDb (p, 25);
        const double perOct = (at50 - at25);   // an octave well down the slope
        check (std::abs (at200 + 3.01) < 0.3 && std::abs (perOct - slope) < slope * 0.08 + 0.6,
               "low cut 200 Hz " + juce::String (slope) + " dB/oct: -3 dB point " + juce::String (at200, 2) + ", slope " + juce::String (perOct, 1) + " dB/oct");
    }
    {
        const auto p = band (shHighCut, 2000, 0, 0.7071, 3);
        check (std::abs (specDb (p, 2000) + 3.01) < 0.3 && specDb (p, 8000) < -46 && std::abs (specDb (p, 200)) < 0.05, "high cut 2 kHz 24 dB/oct");
        const auto hc = band (shHighCut, 16000, 0, 0.7071, 3);
        check (std::abs (specDb (hc, 16000) + 3.01) < 0.5, "high cut 16 kHz keeps its -3 dB point near Nyquist (" + juce::String (specDb (hc, 16000), 2) + ")");
    }
    // ---- 3. shelves, notch, band pass, tilts
    {
        const auto ls = band (shLowShelf, 200, 6, 0.7071);
        check (std::abs (specDb (ls, 15) - 6) < 0.15 && std::abs (specDb (ls, 10000)) < 0.1, "low shelf +6 dB: lows +" + juce::String (specDb (ls, 15), 2) + ", highs " + juce::String (specDb (ls, 10000), 2));
        const auto ls48 = band (shLowShelf, 200, 6, 0.7071, 6);
        const double tr12 = specDb (ls, 100) - specDb (ls, 400), tr48 = specDb (ls48, 100) - specDb (ls48, 400);
        check (tr48 > tr12 + 0.5, "steeper shelf slope gives a sharper shelf (" + juce::String (tr12, 1) + " vs " + juce::String (tr48, 1) + " dB over 2 octaves)");
        const auto hs = band (shHighShelf, 4000, -9, 0.7071);
        check (std::abs (specDb (hs, 20000) + 9) < 0.4 && std::abs (specDb (hs, 100)) < 0.1, "high shelf -9 dB");
        const auto nt = band (shNotch, 1000, 0, 4);
        check (specDb (nt, 1000) < -30 && std::abs (specDb (nt, 4000)) < 0.3, "notch: " + juce::String (specDb (nt, 1000), 0) + " dB at the centre");
        const auto bp = band (shBandPass, 1000, 0, 2);
        check (std::abs (specDb (bp, 1000)) < 0.1 && specDb (bp, 250) < -15, "band pass");
        const auto tl = band (shTiltShelf, 1000, 6, 0.7071, 0);
        check (std::abs (specDb (tl, 1000)) < 0.05 && specDb (tl, 30) < -2.5 && specDb (tl, 20000) > 2.5, "tilt shelf pivots at 0 dB");
        const auto ft = band (shFlatTilt, 1000, 12, 1);
        const double a = specDb (ft, 100), b = specDb (ft, 10000);
        check (std::abs (specDb (ft, 1000)) < 0.05 && a < -2.5 && b > 2.5 && std::abs ((b - a) / std::log2 (100.0) - 12.0 / std::log2 (1000.0)) < 0.4,
               "flat tilt +12 dB: " + juce::String (a, 1) + " dB at 100 Hz, " + juce::String (b, 1) + " dB at 10 kHz");
    }
    // ---- 4. engine: zero latency vs linear phase
    {
        Engine e; e.prepare (fs, 512);
        e.params[0] = band (shBell, 1000, 6, 1);
        const double gz = sineGain (e, 1000);
        Engine e2; e2.prepare (fs, 512);
        e2.params[0] = band (shBell, 1000, 6, 1); e2.phase = Engine::linearPhase;
        const double gl = sineGain (e2, 1000);
        check (std::abs (gz - 6) < 0.15 && std::abs (gl - 6) < 0.3, "bell +6 dB at 1 kHz: zero latency " + juce::String (gz, 2) + " dB, linear phase " + juce::String (gl, 2) + " dB");
        // linear phase impulse response is symmetric about the latency
        Engine e3; e3.prepare (fs, 512);
        e3.params[0] = band (shLowCut, 300, 0, 0.7071, 3); e3.params[1] = band (shBell, 3000, 8, 2); e3.phase = Engine::linearPhase;
        const int N = Engine::kLpSize * 3;
        std::vector<float> l ((size_t) N, 0.0f), r ((size_t) N, 0.0f);
        // warm up with silence, then an impulse
        for (int i = 0; i < N; i += 512) e3.process (l.data() + i, r.data() + i, 512, nullptr, true);
        std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
        l[0] = r[0] = 1.0f;
        for (int i = 0; i < N; i += 512) e3.process (l.data() + i, r.data() + i, 512, nullptr, true);
        const int c = Engine::kLpSize;
        int pk = 0; for (int i = 0; i < N; ++i) if (std::abs (l[(size_t) i]) > std::abs (l[(size_t) pk])) pk = i;
        double asym = 0, energy = 0;
        for (int k = 1; k < 1200; ++k) { asym += std::pow (l[(size_t) (c + k)] - l[(size_t) (c - k)], 2); energy += std::pow (l[(size_t) (c + k)], 2) + std::pow (l[(size_t) (c - k)], 2); }
        check (pk == c && asym < 0.01 * energy, "linear phase: impulse peak at " + juce::String (pk) + " (latency " + juce::String (c) + "), symmetric to " + juce::String (100 * asym / std::max (1e-12, energy), 3) + "%");
    }
    // ---- 5. stereo placement: a Mid band leaves pure side content alone
    {
        Engine e; e.prepare (fs, 512);
        auto p = band (shBell, 1000, 9, 1); p.place = plMid; e.params[0] = p;
        const double mid = sineGain (e, 1000, false);
        Engine e2; e2.prepare (fs, 512); e2.params[0] = p;
        const double side = sineGain (e2, 1000, true);
        Engine e3; e3.prepare (fs, 512); p.place = plSide; e3.params[0] = p;
        const double sideBand = sineGain (e3, 1000, true);
        check (std::abs (mid - 9) < 0.2 && std::abs (side) < 0.1 && std::abs (sideBand - 9) < 0.2,
               "placement: Mid band on mid " + juce::String (mid, 1) + " dB, on side " + juce::String (side, 1) + " dB; Side band on side " + juce::String (sideBand, 1) + " dB");
    }
    // ---- 6. dynamics: a band triggered by the external sidechain
    {
        Engine e; e.prepare (fs, 512);
        auto p = band (shBell, 100, 0, 1.5); p.dynamic = true; p.external = true; p.autoThr = false; p.thresholdDb = -40; p.range = -9;
        e.params[0] = p;
        const int n = (int) fs * 4;
        std::vector<float> l ((size_t) n), r ((size_t) n), sc ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.25f * (float) std::sin (2 * kPi * 100 * i / fs);
            l[(size_t) i] = r[(size_t) i] = v;
            const bool on = (i / (int) (fs * 0.5)) % 2 == 1;       // sidechain on for half a second, off for half
            sc[(size_t) i] = on ? 0.8f * (float) std::sin (2 * kPi * 100 * i / fs) : 0.0f;
        }
        for (int i = 0; i < n; i += 512) e.process (l.data() + i, r.data() + i, std::min (512, n - i), sc.data() + i, true);
        auto lvl = [&] (int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += (double) l[(size_t) i] * l[(size_t) i]; return 10 * std::log10 (s / (b - a) / (0.25 * 0.25 * 0.5)); };
        const int h = (int) (fs * 0.5);
        const double off = lvl (2 * h + h / 2, 3 * h), on = lvl (3 * h + h / 2, 4 * h);
        check (std::abs (off) < 0.5 && std::abs (on + 9) < 1.0, "dynamic band, external sidechain: " + juce::String (off, 1) + " dB without the kick, " + juce::String (on, 1) + " dB with it");
        // auto threshold, internal: loud passages get pulled down, quiet ones left alone
        Engine e2; e2.prepare (fs, 512);
        auto p2 = band (shBell, 3000, 0, 1); p2.dynamic = true; p2.range = -6; e2.params[0] = p2;
        std::vector<float> x ((size_t) n), y ((size_t) n);
        for (int i = 0; i < n; ++i) { const float amp = (i / (int) (fs * 0.25)) % 4 == 3 ? 0.5f : 0.05f; x[(size_t) i] = y[(size_t) i] = amp * (float) std::sin (2 * kPi * 3000 * i / fs); }
        auto inCopy = x;
        for (int i = 0; i < n; i += 512) e2.process (x.data() + i, y.data() + i, std::min (512, n - i), nullptr, true);
        auto g = [&] (int a, int b) { double so = 0, si = 0; for (int i = a; i < b; ++i) { so += (double) x[(size_t) i] * x[(size_t) i]; si += (double) inCopy[(size_t) i] * inCopy[(size_t) i]; } return 10 * std::log10 (so / si); };
        const int q4 = (int) (fs * 0.25);
        const double quiet = g (12 * q4 + q4 / 2, 13 * q4), loud = g (15 * q4 + q4 / 2, 16 * q4);
        check (std::abs (quiet) < 1.0 && loud < -3.5, "dynamic band, auto threshold: quiet part " + juce::String (quiet, 1) + " dB, loud part " + juce::String (loud, 1) + " dB");
    }
    // ---- 7. solo
    {
        Engine e; e.prepare (fs, 512);
        e.params[0] = band (shBell, 1000, 0, 2); e.soloBand = 0;
        const double at1k = sineGain (e, 1000);
        Engine e2; e2.prepare (fs, 512); e2.params[0] = band (shBell, 1000, 0, 2); e2.soloBand = 0;
        const double at8k = sineGain (e2, 8000);
        check (std::abs (at1k) < 0.5 && at8k < -15, "band solo: 1 kHz passes (" + juce::String (at1k, 1) + " dB), 8 kHz removed (" + juce::String (at8k, 1) + " dB)");
    }
    return fails;
}

// processor-level: auto gain, EQ match, registry, state, CPU
static int runProcessor()
{
    std::printf ("EQ in the plugin\n");
    auto make = [] { auto p = std::make_unique<InterlockProcessor>(); p->setRateAndBufferSizeDetails (fs, 512); p->prepareToPlay (fs, 512); p->hasLearned = true; return p; };
    juce::Random rnd (3);
    auto pink = [&rnd] (std::vector<float>& x)
    {
        double b0 = 0, b1 = 0, b2 = 0;
        for (auto& v : x) { const double w = rnd.nextFloat() * 2 - 1; b0 = 0.99765 * b0 + w * 0.099046; b1 = 0.963 * b1 + w * 0.2965164; b2 = 0.57 * b2 + w * 1.0526913; v = (float) ((b0 + b1 + b2 + w * 0.1848) * 0.08); }
    };
    auto runBlock = [] (InterlockProcessor& p, std::vector<float>& l, std::vector<float>& r, const std::vector<float>* sc)
    {
        const int n = (int) l.size();
        juce::AudioBuffer<float> buf (4, 512); juce::MidiBuffer m;
        for (int i = 0; i < n; i += 512)
        {
            const int k = std::min (512, n - i);
            buf.setSize (4, k, false, false, true);
            buf.copyFrom (0, 0, l.data() + i, k); buf.copyFrom (1, 0, r.data() + i, k);
            if (sc) { buf.copyFrom (2, 0, sc->data() + i, k); buf.copyFrom (3, 0, sc->data() + i, k); } else { buf.clear (2, 0, k); buf.clear (3, 0, k); }
            p.processBlock (buf, m);
            std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + k, l.data() + i);
            std::copy (buf.getReadPointer (1), buf.getReadPointer (1) + k, r.data() + i);
            if ((i / 512) % 4 == 0) p.runTimerForTests();
        }
    };
    auto setP = [] (InterlockProcessor& p, const juce::String& id, float v) { auto* prm = p.apvts.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
    auto neutral = [&] (InterlockProcessor& p) { setP (p, ilp::dipDepth, 0); setP (p, ilp::duck, 0); setP (p, ilp::boostGain, 0); setP (p, ilp::parMix, 0); setP (p, ilp::monoLows, 0); };

    // auto gain
    {
        auto p = make(); neutral (*p);
        const int b = p->addBand (1000, 8.0f); auto bp = p->bandParams (b); bp.q = 0.4f; p->setBandParams (b, bp);
        std::vector<float> l ((size_t) fs * 3), r; pink (l); r = l; auto in = l;
        runBlock (*p, l, r, nullptr);
        auto rms = [] (const std::vector<float>& v, int a) { double s = 0; for (size_t i = (size_t) a; i < v.size(); ++i) s += (double) v[i] * v[i]; return 10 * std::log10 (s / (v.size() - (size_t) a)); };
        const double without = rms (l, (int) fs) - rms (in, (int) fs);
        auto p2 = make(); neutral (*p2);
        p2->setBandParams (p2->addBand (1000, 8.0f), bp); setP (*p2, ilp::autoGain, 1);
        std::vector<float> l2 = in, r2 = in;
        runBlock (*p2, l2, r2, nullptr);
        const double with = rms (l2, (int) fs) - rms (in, (int) fs);
        check (without > 2 && std::abs (with) < 1.0, "auto gain: +" + juce::String (without, 1) + " dB louder without it, " + juce::String (with, 1) + " dB with it (" + juce::String (p2->autoGainNowDb(), 1) + " dB applied)");
    }
    // EQ match: the input is the reference with its lows cut by 6 dB
    {
        auto p = make(); neutral (*p);
        std::vector<float> ref ((size_t) fs * 6); pink (ref);
        Engine pre; pre.prepare (fs, 512); pre.params[0] = band (shLowShelf, 300, -6, 0.7071);
        std::vector<float> l = ref, r = ref;
        for (size_t i = 0; i < l.size(); i += 512) pre.process (l.data() + i, r.data() + i, (int) std::min<size_t> (512, l.size() - i), nullptr, true);
        p->startMatch();
        runBlock (*p, l, r, &ref);
        const int made = p->applyMatch (8, 1.0f);
        double at80 = 0, at3k = 0;
        for (int i = 0; i < kBands; ++i) { const auto bp = p->bandParams (i); at80 += Engine::specDb (bp, 1, fs, 80); at3k += Engine::specDb (bp, 1, fs, 3000); }
        check (made >= 1 && std::abs (at80 - 6) < 2.0 && std::abs (at3k) < 1.5, "EQ match: " + juce::String (made) + " bands, +" + juce::String (at80, 1) + " dB at 80 Hz (want +6), " + juce::String (at3k, 1) + " dB at 3 kHz");
    }
    // registry: two instances see each other
    {
        auto a = make(), b = make();
        a->trackName = "Bass"; b->trackName = "Kick";
        a->editorOpen = true; b->editorOpen = true;
        a->runTimerForTests(); b->runTimerForTests();
        auto others = Registry::get().others (a->registryId);
        bool seesKick = false; for (auto& [id, name] : others) seesKick |= name == "Kick";
        check (seesKick, "collision: one Interlock can see the others in the session");
    }
    // state
    {
        auto p = make();
        const int b = p->addBand (440, -4.5f, shHighShelf); auto bp = p->bandParams (b); bp.place = plSide; bp.dynamic = true; bp.range = 5; p->setBandParams (b, bp);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto p2 = make(); p2->setStateInformation (mb.getData(), (int) mb.getSize());
        check (p2->bandParams (b) == p->bandParams (b), "band settings survive save / reload");
    }
    // CPU: all 24 bands dynamic, both modes
    for (int mode : { 0, 1 })
    {
        auto p = make(); neutral (*p);
        for (int i = 0; i < kBands; ++i)
        {
            const int s = p->addBand (40.0 * std::pow (1.3, i), (i % 2 ? 3.0f : -3.0f), i == 0 ? shLowCut : i == 23 ? shHighCut : shBell);
            auto bp = p->bandParams (s); bp.dynamic = i % 3 == 0; bp.slopeIndex = i == 0 || i == 23 ? 8 : bp.slopeIndex; p->setBandParams (s, bp);
        }
        setP (*p, ilp::eqPhase, (float) mode);
        std::vector<float> l ((size_t) fs * 5), r; pink (l); r = l;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        runBlock (*p, l, r, nullptr);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (ms < 5000 * 0.35, juce::String (mode ? "linear phase" : "zero latency") + ", 24 bands: " + juce::String (ms / 50.0, 1) + "% of one core");
    }
    return fails;
}
} // namespace eqtest
