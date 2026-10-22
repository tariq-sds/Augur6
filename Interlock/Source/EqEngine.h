#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "EqDesign.h"

// Interlock EQ engine: up to 24 bands, zero-latency (matched biquads) or linear-phase (STFT, zero-phase
// gains), dynamic bands with internal or external sidechain, per-band stereo placement, solo, gain scale,
// auto gain and output pan.
namespace ileq
{
constexpr int kBands = 24;

struct BandParams
{
    bool used = false, on = true;
    int shape = shBell, slopeIndex = 1, place = plStereo;
    float freq = 1000, gainDb = 0, q = 1;
    bool dynamic = false, autoThr = true, external = false;
    float range = -6, thresholdDb = -30;
    bool operator== (const BandParams& o) const
    {
        return used == o.used && on == o.on && shape == o.shape && slopeIndex == o.slopeIndex && place == o.place && freq == o.freq
            && gainDb == o.gainDb && q == o.q && dynamic == o.dynamic && autoThr == o.autoThr && external == o.external
            && range == o.range && thresholdDb == o.thresholdDb;
    }
};

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set (const Coefs& c) { b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2; }
    inline double tick (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() { z1 = z2 = 0; }
};

// the gain this band applies right now (static gain, scaled, plus whatever the dynamics add)
inline float effectiveGain (const BandParams& p, float scale, float dyn) { return (hasGain (p.shape) ? p.gainDb * scale : 0.0f) + dyn; }

class Engine
{
public:
    enum Phase { zeroLatency = 0, linearPhase = 1 };
    static constexpr int kLpOrder = 12, kLpSize = 1 << kLpOrder, kLpHop = kLpSize / 4;

    Engine() : fft (kLpOrder) {}

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        fs = sampleRate;
        for (auto& b : bands) { for (auto& ch : b.st) for (auto& q : ch) q.reset(); b.det.reset(); b.env = 0; b.avgDb = -60; b.avgInit = false; b.dyn = 0; b.amt = 0; b.dirty = true; }
        soloBp[0].reset(); soloBp[1].reset();
        for (int c = 0; c < 2; ++c) { inFifo[c].assign ((size_t) kLpSize, 0.0f); outAcc[c].assign ((size_t) kLpSize * 2, 0.0f); outFifo[c].assign ((size_t) kLpSize, 0.0f); }
        win.resize ((size_t) kLpSize);
        for (int i = 0; i < kLpSize; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * (float) i / (float) kLpSize);
        for (auto* v : { &specL, &specR, &tmp }) v->assign ((size_t) kLpSize, {});
        binPhi.resize ((size_t) kLpSize / 2 + 1);
        for (int k = 0; k <= kLpSize / 2; ++k) binPhi[(size_t) k] = phiOf (std::max (1.0, k * fs / kLpSize), fs);
        for (auto& h : resp) h.assign ((size_t) kLpSize / 2 + 1, 1.0f);
        lpPos = lpFilled = 0;
        gainSmooth = 1; panSmooth = 0;
        updateCounter = 0;
    }

    // ---- control (audio thread, once per block)
    std::array<BandParams, kBands> params;
    int phase = zeroLatency;
    float gainScale = 1.0f, outPan = 0.0f, autoGainDb = 0.0f;
    bool autoGain = false;
    std::atomic<int> soloBand { -1 };
    std::array<std::atomic<float>, kBands> dynNow {};        // current dynamic gain per band (dB), for the display
    int latency() const { return phase == linearPhase ? kLpSize : 0; }

    void process (float* L, float* R, int n, const float* sc, bool stereo)
    {
        // pick up parameter changes
        if (gainScale != lastScale) { lastScale = gainScale; for (auto& b : bands) b.dirty = true; }
        for (int i = 0; i < kBands; ++i)
            if (! (params[(size_t) i] == bands[(size_t) i].p)) { bands[(size_t) i].p = params[(size_t) i]; bands[(size_t) i].dirty = true; }
        const int solo = soloBand.load();
        const float targetGain = autoGain ? std::pow (10.0f, autoGainDb / 20.0f) : 1.0f;
        const float gs = 1.0f - std::exp (-1.0f / (0.05f * (float) fs));

        for (int i = 0; i < n; ++i)
        {
            const double inL = L[i], inR = stereo ? R[i] : L[i];
            const double inM = 0.5 * (inL + inR), inS = 0.5 * (inL - inR);
            const double scx = sc != nullptr ? sc[i] : 0.0;

            // ---- dynamics: detector per dynamic band (band-limited, pre-EQ input or the sidechain)
            for (auto& b : bands)
            {
                if (! b.p.used || ! b.p.on || ! b.p.dynamic) continue;
                double src;
                if (b.p.external) src = scx;
                else switch (b.p.place) { case plLeft: src = inL; break; case plRight: src = inR; break; case plSide: src = inS; break; default: src = inM; break; }
                const double v = std::abs (b.det.tick (src));
                b.env = v > b.env ? v + (b.env - v) * b.atk : v + (b.env - v) * b.rel;   // fast, ripple-free level
            }

            if ((updateCounter++ & 31) == 0) updateGains();

            double l = inL, r = inR;
            if (phase == zeroLatency)
            {
                for (auto& b : bands)
                {
                    if (! b.p.used || ! b.p.on || b.design.n == 0) continue;
                    switch (b.p.place)
                    {
                        case plStereo: l = run (b, 0, l); if (stereo) r = run (b, 1, r); break;
                        case plLeft:   l = run (b, 0, l); break;
                        case plRight:  if (stereo) r = run (b, 1, r); break;
                        case plMid:    { double m = 0.5 * (l + r), s = 0.5 * (l - r); m = run (b, 0, m); l = m + s; r = m - s; break; }
                        case plSide:   { if (! stereo) break; double m = 0.5 * (l + r), s = 0.5 * (l - r); s = run (b, 1, s); l = m + s; r = m - s; break; }
                        default: break;
                    }
                }
                if (solo >= 0 && solo < kBands && bands[(size_t) solo].p.used)
                {
                    l = soloBp[0].tick (inL);
                    r = soloBp[1].tick (inR);
                }
            }
            else
            {
                // linear phase: STFT with zero-phase gains, kLpSize samples of latency
                inFifo[0][(size_t) lpPos] = (float) inL;
                inFifo[1][(size_t) lpPos] = (float) inR;
                l = outFifo[0][(size_t) lpPos];
                r = outFifo[1][(size_t) lpPos];
                lpPos = (lpPos + 1) % kLpSize;
                if (++lpFilled >= kLpHop) { lpFilled = 0; lpFrame (solo); }
            }

            gainSmooth += (targetGain - gainSmooth) * gs;
            panSmooth += (outPan - panSmooth) * gs;
            const double gl = gainSmooth * (panSmooth > 0 ? 1.0 - panSmooth : 1.0);
            const double gr = gainSmooth * (panSmooth < 0 ? 1.0 + panSmooth : 1.0);
            L[i] = (float) (l * gl);
            if (stereo) R[i] = (float) (r * gr);
        }
    }

    // the band's response as the display draws it (dB at hz), including its current dynamics
    double bandDb (int i, double hz, bool live) const
    {
        const auto& b = bands[(size_t) i];
        if (! b.p.used) return 0;
        const auto& d = live ? b.design : b.staticDesign;
        if (d.brick && phase == linearPhase) return 10.0 * std::log10 (std::max (1e-12, brickMag2 (b.p.shape, b.p.freq, hz)));
        return 10.0 * std::log10 (std::max (1e-12, designMag2 (d, phiOf (hz, fs))));
    }
    // static response of a parameter set (message thread: display, auto gain, match)
    static double specDb (const BandParams& p, float scale, double fsr, double hz)
    {
        if (! p.used || ! p.on) return 0;
        BandSpec s { p.shape, p.freq, effectiveGain (p, scale, 0), p.q, p.slopeIndex };
        return 10.0 * std::log10 (std::max (1e-30, designMag2 (design (s, fsr), phiOf (hz, fsr))));
    }
    double sampleRate() const { return fs; }

private:
    struct Band
    {
        BandParams p;
        Design design, staticDesign;
        std::array<std::array<Biquad, kMaxSections>, 2> st {};
        Biquad det;
        double env = 0, atk = 0.99, rel = 0.999;
        bool avgInit = false;
        float avgDb = -60, dyn = 0, amt = 0, gAtk = 0.5f, gRel = 0.9f, lastGain = 1e9f;
        bool dirty = true;
    };

    inline double run (Band& b, int ch, double x)
    {
        auto& s = b.st[(size_t) ch];
        for (int k = 0; k < b.design.n; ++k) x = s[(size_t) k].tick (x);
        return x * b.design.scale;
    }

    // dynamics -> gains -> coefficients (every 32 samples)
    void updateGains()
    {
        const double upd = 32.0 / fs;
        const int solo = soloBand.load();
        bool soloDirty = solo != lastSolo;
        for (int i = 0; i < kBands; ++i)
        {
            auto& b = bands[(size_t) i];
            if (! b.p.used) { dynNow[(size_t) i] = 0; b.dirty = false; continue; }
            if (b.dirty && i == solo) soloDirty = true;
            if (b.dirty)
            {
                // detector follows the band: band-pass at its frequency (low-pass / high-pass for shelves and cuts)
                Analog a { 0, 0, 0, 1, 0, 0 };
                const double w = 2.0 * kPi * std::clamp ((double) b.p.freq, 10.0, fs * 0.45);
                const double q = std::clamp ((double) b.p.q, 0.3, 12.0);
                switch (b.p.shape)
                {
                    case shLowShelf: case shLowCut:   a = { 0, 0, w * w, 1, w / 0.7071, w * w }; break;
                    case shHighShelf: case shHighCut: a = { 1, 0, 0, 1, w / 0.7071, w * w }; break;
                    default:                          a = { 0, w / q, 0, 1, w / q, w * w }; break;
                }
                b.det.set (matched (a, fs, b.p.freq).c);
                // program-dependent timing: the level follower holds about two cycles (no ripple), the gain reacts
                // within a cycle or two and lets go over roughly eight
                const double hold = std::clamp (2.0 / b.p.freq, 0.0005, 0.04);
                b.atk = std::exp (-1.0 / (0.0002 * fs)); b.rel = std::exp (-1.0 / (hold * fs));
                const double atkS = std::clamp (1.5 / b.p.freq, 0.0008, 0.02), relS = std::clamp (8.0 / b.p.freq, 0.03, 0.25);
                b.gAtk = (float) std::exp (-32.0 / (atkS * fs)); b.gRel = (float) std::exp (-32.0 / (relS * fs));
                BandSpec spec { b.p.shape, b.p.freq, effectiveGain (b.p, gainScale, 0), b.p.q, b.p.slopeIndex };
                b.staticDesign = design (spec, fs);
                b.lastGain = 1e9f;
            }
            float dyn = 0;
            if (b.p.dynamic && b.p.on)
            {
                const float lvl = (float) (20.0 * std::log10 (std::max (1e-9, b.env)));
                if (lvl > -80) { if (! b.avgInit) { b.avgDb = lvl; b.avgInit = true; } else b.avgDb += (lvl - b.avgDb) * (float) (1.0 - std::exp (-upd / 3.0)); }
                const float thr = b.p.autoThr ? b.avgDb + 4.0f : b.p.thresholdDb;
                const float target = std::clamp ((lvl - thr) / 10.0f, 0.0f, 1.0f);    // 10 dB over the threshold = full range
                b.amt = target > b.amt ? target + (b.amt - target) * b.gAtk : target + (b.amt - target) * b.gRel;
                dyn = b.p.range * gainScale * b.amt;
            }
            else b.amt = 0;
            b.dyn = dyn;
            dynNow[(size_t) i] = b.dyn;
            const float g = effectiveGain (b.p, gainScale, b.dyn);
            if (b.dirty || std::abs (g - b.lastGain) > 0.02f)
            {
                BandSpec spec { b.p.shape, b.p.freq, g, b.p.q, b.p.slopeIndex };
                const int oldN = b.design.n;
                b.design = design (spec, fs);
                for (int c = 0; c < 2; ++c)
                    for (int k = 0; k < b.design.n; ++k)
                    {
                        auto& bq = b.st[(size_t) c][(size_t) k];
                        if (oldN != b.design.n) bq.reset();     // shape / slope changed: start the sections clean
                        bq.set (b.design.s[(size_t) k].c);
                    }
                b.lastGain = g;
            }
            b.dirty = false;
        }
        // keep the solo filter current
        if (soloDirty && solo >= 0 && solo < kBands)
        {
            const auto& p = bands[(size_t) solo].p;
            const double w = 2.0 * kPi * std::clamp ((double) p.freq, 10.0, fs * 0.45);
            const double q = std::max (0.4, (double) p.q);
            const auto c = matched ({ 0, w / q, 0, 1, w / q, w * w }, fs, p.freq).c;
            soloBp[0].set (c); soloBp[1].set (c);
        }
        lastSolo = solo;
    }

    void lpFrame (int solo)
    {
        // per-bin zero-phase gains for each placement group
        const int nb = kLpSize / 2 + 1;
        for (auto& h : resp) std::fill (h.begin(), h.end(), 1.0f);
        for (auto& b : bands)
        {
            if (! b.p.used || ! b.p.on || b.design.n == 0) continue;
            auto& h = resp[(size_t) b.p.place];
            for (int k = 0; k < nb; ++k)
            {
                const double hz = k * fs / kLpSize;
                const double m2 = b.design.brick ? brickMag2 (b.p.shape, b.p.freq, std::max (1.0, hz)) : designMag2 (b.design, binPhi[(size_t) k]);
                h[(size_t) k] *= (float) std::sqrt (m2);
            }
        }
        if (solo >= 0 && solo < kBands && bands[(size_t) solo].p.used)
        {
            for (auto& h : resp) std::fill (h.begin(), h.end(), 1.0f);
            for (int k = 0; k < nb; ++k)
            {
                Section s; s.c = { soloBp[0].b0, soloBp[0].b1, soloBp[0].b2, soloBp[0].a1, soloBp[0].a2 };
                resp[plStereo][(size_t) k] = (float) std::sqrt (s.mag2 (binPhi[(size_t) k].p0, binPhi[(size_t) k].p1, binPhi[(size_t) k].p2));
            }
        }
        // analysis
        for (int c = 0; c < 2; ++c)
        {
            auto& spec = c == 0 ? specL : specR;
            for (int i = 0; i < kLpSize; ++i) tmp[(size_t) i] = { inFifo[(size_t) c][(size_t) ((lpPos + i) % kLpSize)] * win[(size_t) i], 0.0f };
            fft.perform (tmp.data(), spec.data(), false);
        }
        const bool needMs = ! isFlat (resp[plMid]) || ! isFlat (resp[plSide]);
        for (int k = 0; k < nb; ++k)
        {
            const float hs = resp[plStereo][(size_t) k];
            auto l = specL[(size_t) k] * (hs * resp[plLeft][(size_t) k]);
            auto r = specR[(size_t) k] * (hs * resp[plRight][(size_t) k]);
            if (needMs)
            {
                auto m = (l + r) * 0.5f * resp[plMid][(size_t) k];
                auto s = (l - r) * 0.5f * resp[plSide][(size_t) k];
                l = m + s; r = m - s;
            }
            specL[(size_t) k] = l; specR[(size_t) k] = r;
            if (k > 0 && k < kLpSize / 2) { specL[(size_t) (kLpSize - k)] = std::conj (l); specR[(size_t) (kLpSize - k)] = std::conj (r); }
        }
        const float g = 1.0f / 1.5f;   // Hann^2 at 75% overlap sums to 1.5
        for (int c = 0; c < 2; ++c)
        {
            auto& spec = c == 0 ? specL : specR;
            fft.perform (spec.data(), tmp.data(), true);
            auto& acc = outAcc[(size_t) c];
            for (int i = 0; i < kLpSize; ++i) acc[(size_t) i] += tmp[(size_t) i].real() * win[(size_t) i] * g;
            for (int i = 0; i < kLpHop; ++i) outFifo[(size_t) c][(size_t) ((lpPos + i) % kLpSize)] = acc[(size_t) i];
            std::copy (acc.begin() + kLpHop, acc.begin() + kLpSize + kLpHop, acc.begin());
            std::fill (acc.begin() + kLpSize, acc.end(), 0.0f);
        }
    }
    static bool isFlat (const std::vector<float>& h) { for (float v : h) if (std::abs (v - 1.0f) > 1e-5f) return false; return true; }

    double fs = 48000;
    std::array<Band, kBands> bands;
    Biquad soloBp[2];
    int lastSolo = -1;
    float lastScale = 1.0f;
    uint32_t updateCounter = 0;
    float gainSmooth = 1, panSmooth = 0;
    // linear phase
    juce::dsp::FFT fft;
    std::array<std::vector<float>, 2> inFifo, outAcc, outFifo;
    std::vector<float> win;
    std::vector<std::complex<float>> specL, specR, tmp;
    std::vector<Phi> binPhi;
    std::array<std::vector<float>, kNumPlaces> resp;
    int lpPos = 0, lpFilled = 0;
};
} // namespace ileq
