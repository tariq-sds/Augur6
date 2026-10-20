#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

// Varia engine: tempo-locked variation. Per-hit micro changes, slow filter and stereo drift,
// and phrase-end "flicks" (rolls, delay throws, reverse swells, gates, tape stops).
namespace va
{
constexpr float kPi = 3.14159265358979f;

struct Style
{
    const char* name;
    uint32_t c1, c2, c3;          // orb palette: deep, accent, highlight
    float groove, filter, roll, throwW, reverse, gate, tape, width;
    int phraseBars;
    float barFill;                // chance of small fills at the end of ordinary bars
    bool riser;                   // rolls climb in pitch
};

inline const std::array<Style, 12>& styles()
{
    static const std::array<Style, 12> s {{
        { "GROOVE",       0xff1d1208, 0xffe0a24a, 0xfff6e7c8, .9f, .25f, .15f, .10f, .00f, .00f, .00f, .3f, 4, .30f, false },
        { "CLUB ROLLS",   0xff06142a, 0xff1f8fc4, 0xffe9f6ff, .4f, .20f, 1.0f, .20f, .10f, .10f, .00f, .3f, 8, .50f, true  },
        { "DUB THROWS",   0xff04191a, 0xff1fb59a, 0xffd9fff4, .3f, .30f, .10f, 1.0f, .30f, .00f, .00f, .5f, 4, .60f, false },
        { "FILTER RIDE",  0xff1a0620, 0xffc23ad6, 0xfff8dcff, .3f, 1.0f, .20f, .20f, .10f, .00f, .00f, .3f, 8, .30f, false },
        { "GLITCH",       0xff0f0a1f, 0xff8a5cff, 0xffd4ff4a, .5f, .20f, .80f, .20f, .10f, 1.0f, .30f, .4f, 4, 1.0f, false },
        { "REVERSE TIDE", 0xff070d24, 0xff2f4fd0, 0xffffd27a, .3f, .30f, .10f, .40f, 1.0f, .00f, .00f, .5f, 4, .40f, false },
        { "TAPE STOP",    0xff1c0c06, 0xffc4562a, 0xfffde0c8, .3f, .20f, .20f, .20f, .20f, .10f, 1.0f, .3f, 8, .30f, false },
        { "BREATHE",      0xff0b160f, 0xff7fae84, 0xffeaf5e3, .6f, .50f, .00f, .15f, .15f, .00f, .00f, 1.0f, 8, .20f, false },
        { "PEAK TIME",    0xff200606, 0xffff4a2a, 0xffffe0b0, .6f, .40f, .70f, .60f, .40f, .40f, .20f, .5f, 8, .60f, true  },
        { "MINIMAL",      0xff111417, 0xff7d8b99, 0xffeef2f6, .8f, .15f, .20f, .20f, .00f, .10f, .00f, .3f, 4, .25f, false },
        { "WIDE DRIFT",   0xff061820, 0xff5fd3e8, 0xfff0fdff, .4f, .40f, .00f, .30f, .20f, .00f, .00f, 1.0f, 4, .20f, false },
        { "CHAOS",        0xff0d0d0d, 0xffb6ff1a, 0xffff3ec8, .7f, .70f, 1.0f, .80f, .70f, .90f, .60f, .8f, 2, 1.0f, true  },
    }};
    return s;
}

struct Rng
{
    uint32_t s = 0x2545F491u;
    void seed (uint32_t v) { s = v ? v : 0x2545F491u; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni() { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bi() { return uni() * 2.0f - 1.0f; }
};

struct Settings
{
    float texture = 0.5f, release = 0.5f, brightness = 0.6f, width = 0.5f, pitch = 0.0f;
    int style = 0;
    bool wetOnly = false, bypass = false;
};

struct Transport
{
    bool playing = false;
    double ppq = 0.0, bpm = 120.0;
    int num = 4, den = 4;
};

enum EventType { evNone = 0, evRoll, evTape, evGate, evThrow, evReverse, evFilter };

inline const char* eventName (int t)
{
    switch (t)
    {
        case evRoll: return "ROLL"; case evTape: return "TAPE STOP"; case evGate: return "GATE";
        case evThrow: return "THROW"; case evReverse: return "REVERSE"; case evFilter: return "FILTER SWEEP";
        default: return "";
    }
}

struct Svf
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    // returns lowpass; bp in v1
    float lp (float x, float g, float k, float* bp = nullptr, float* hp = nullptr)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        if (bp) *bp = v1;
        if (hp) *hp = x - k * v1 - v2;
        return v2;
    }
};

// Smooth random walk in [0,1]
struct Walk
{
    float a = 0.5f, b = 0.5f, ph = 0.0f;
    float tick (float dt, Rng& r)
    {
        ph += dt;
        if (ph >= 1.0f) { ph -= 1.0f; a = b; b = r.uni(); }
        const float t = ph * ph * (3.0f - 2.0f * ph);
        return a + (b - a) * t;
    }
};

class Engine
{
public:
    std::atomic<int> lastEvent { 0 }, eventCounter { 0 };
    std::atomic<float> motion { 0.0f };   // 0..1 overall movement, for the orb

    void prepare (double sampleRate, uint32_t seed)
    {
        sr = (float) sampleRate;
        rng.seed (seed);
        capSize = (int) (sr * 8.0f);
        for (auto& c : cap) c.assign ((size_t) capSize, 0.0f);
        dlySize = (int) (sr * 2.5f);
        for (auto& d : dly) d.assign ((size_t) dlySize, 0.0f);
        haasSize = (int) (sr * 0.05f) + 4;
        haas.assign ((size_t) haasSize, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& c : cap) std::fill (c.begin(), c.end(), 0.0f);
        for (auto& d : dly) std::fill (d.begin(), d.end(), 0.0f);
        std::fill (haas.begin(), haas.end(), 0.0f);
        wAbs = 0; dW = 0; hW = 0;
        lastStep = INT64_MIN;
        internalPpq = 0.0;
        numPending = 0;
        rep = {};
        rev = {};
        sendLeft = 0; sendEnv = 0;
        fdActive = false; fdLen = fdPos = 0;
        for (auto& f : motionLp) f.reset();
        for (auto& f : evLp) f.reset();
        for (auto& f : riserHp) f.reset();
        fbL = fbR = 0; fbLpL = fbLpR = fbHpL = fbHpR = 0;
        fast = slow = 0; holdoff = 0;
        gGain = gTilt = gPan = tGain = tTilt = tPan = 0;
        tiltLp[0] = tiltLp[1] = 0;
        mSmooth = 0; bypassMix = 0; quietSamples = 0;
        echoTime = echoTimeTarget = sr * 0.375f;
    }

    void process (float* L, float* R, int n, const Settings& s, const Transport& tp)
    {
        const Style& st = styles()[(size_t) juce::jlimit (0, (int) styles().size() - 1, s.style)];
        cur = s;
        curStyle = &st;
        bpm = (float) juce::jlimit (40.0, 300.0, tp.bpm);
        sec16 = 60.0f / bpm / 4.0f;
        stepsPerBar = juce::jmax (1, tp.num * 16 / juce::jmax (1, tp.den));

        double ppq0 = tp.playing ? tp.ppq : internalPpq;
        const double dppq = (double) bpm / 60.0 / sr;
        internalPpq = ppq0 + dppq * n;

        const float tex = s.texture;
        const float rel = s.release;
        const float fEv = 900.0f * std::pow (2.0f, s.brightness * 4.4f);
        const float gEv = std::tan (kPi * juce::jmin (fEv, sr * 0.45f) / sr);
        const float fb = 0.25f + 0.6f * rel;
        const float fbLpC = 1.0f - std::exp (-2.0f * kPi * fEv / sr);
        const float fbHpC = 1.0f - std::exp (-2.0f * kPi * 180.0f / sr);
        const float tiltC = 1.0f - std::exp (-2.0f * kPi * 1200.0f / sr);
        const float grooveSm = 1.0f - std::exp (-1.0f / ((0.003f + 0.03f * rel) * sr));
        const float pitchRate = std::exp2 (s.pitch / 12.0f);
        const float fLow = 180.0f * std::exp2 (s.brightness * 5.0f);
        const float wid = s.width * st.width * tex;
        const float rampStep = 1.0f / (0.004f * sr);
        const float bypStep = 1.0f / (0.015f * sr);
        float motionPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            // ---------------- Clock
            const double ppq = ppq0 + dppq * i;
            const int64_t step = (int64_t) std::floor (ppq * 4.0 + 1e-9);
            if (step != lastStep)
            {
                if (lastStep == INT64_MIN || step < lastStep || step > lastStep + 4)
                    resync (step);
                else
                    for (int64_t k = lastStep + 1; k <= step; ++k) onStep (k);
                lastStep = step;
            }

            const float inL = L[i], inR = R[i];
            cap[0][(size_t) (wAbs % capSize)] = inL;
            cap[1][(size_t) (wAbs % capSize)] = inR;

            const float a = std::abs (inL) + std::abs (inR);
            quietSamples = a > 0.002f ? 0 : quietSamples + 1;

            // ---------------- Per-hit variation (groove)
            fast += (a - fast) * (a > fast ? 0.2f : 0.0016f);
            slow += (a - slow) * (a > slow ? 0.0015f : 0.0002f);
            if (holdoff > 0) --holdoff;
            if (fast > slow * 1.9f + 0.002f && holdoff == 0)
            {
                holdoff = (int) (0.06f * sr);
                const float g = st.groove * tex;
                tGain = (0.25f - rng.uni() * 1.25f) * 2.4f * g;   // mostly softer hits, the odd accent
                tTilt = rng.bi() * 5.0f * g;
                tPan = rng.bi() * 0.35f * g * s.width;
            }
            gGain += (tGain - gGain) * grooveSm;
            gTilt += (tTilt - gTilt) * grooveSm;
            gPan += (tPan - gPan) * grooveSm;

            float x[2] { inL, inR };
            {
                const float gl = std::exp (gGain * 0.11512925f);
                const float tl = std::exp (gTilt * 0.5f * 0.11512925f);
                for (int c = 0; c < 2; ++c)
                {
                    tiltLp[c] += (x[c] - tiltLp[c]) * tiltC;
                    const float lo = tiltLp[c], hi = x[c] - lo;
                    x[c] = (lo / tl + hi * tl) * gl;
                }
                x[0] *= 1.0f - juce::jmax (0.0f, gPan);
                x[1] *= 1.0f + juce::jmin (0.0f, gPan);
            }

            // ---------------- Replace events: roll / tape stop / gate
            float y[2] { x[0], x[1] };
            float wet[2] { 0, 0 };
            if (rep.type != evNone)
            {
                rep.mix = rep.pos < rep.len ? juce::jmin (1.0f, rep.mix + rampStep) : juce::jmax (0.0f, rep.mix - rampStep);
                const float prog = juce::jlimit (0.0f, 1.0f, (float) rep.pos / (float) juce::jmax (1, rep.len));
                float ev[2] { 0, 0 };

                if (rep.type == evRoll)
                {
                    float slice = rep.slice;
                    if (rep.accelerate && prog >= 0.5f) slice *= 0.5f;
                    float rate = pitchRate * (rep.riser ? std::exp2 (rep.intensity * 12.0f * prog * prog / 12.0f) : 1.0f);
                    rep.readOff += rate;
                    if (rep.readOff >= slice) rep.readOff -= slice * std::floor (rep.readOff / slice);
                    const double pos = (double) rep.startAbs - rep.slice + rep.readOff;
                    const float fade = juce::jlimit (0.0f, 1.0f, juce::jmin (rep.readOff, slice - rep.readOff) / (0.0015f * sr));
                    const float amp = (0.75f + 0.25f * prog) * fade;
                    for (int c = 0; c < 2; ++c) ev[c] = readCap (c, pos) * amp;
                    if (rep.riser)
                    {
                        const float fh = 30.0f * std::exp2 (prog * 6.0f * rep.intensity);
                        const float gh = std::tan (kPi * juce::jmin (fh, sr * 0.45f) / sr);
                        for (int c = 0; c < 2; ++c) { float hp; riserHp[c].lp (ev[c], gh, 1.2f, nullptr, &hp); ev[c] = hp; }
                    }
                }
                else if (rep.type == evTape)
                {
                    const float r = juce::jmin (1.0f, pitchRate) * std::pow (1.0f - prog, 1.6f);
                    rep.readPos += r;
                    const float amp = juce::jmin (1.0f, (1.0f - prog) * 4.0f);
                    for (int c = 0; c < 2; ++c) ev[c] = readCap (c, rep.readPos) * amp;
                }
                else if (rep.type == evGate)
                {
                    const int bitLen = juce::jmax (1, (int) (sec16 * 0.5f * sr));
                    const int bit = juce::jlimit (0, 31, rep.pos / bitLen);
                    const bool on = rep.pos >= rep.len || ((rep.pattern >> bit) & 1u);
                    const float tgt = on ? 1.0f : 0.0f;
                    const float c = 1.0f - std::exp (-1.0f / ((0.0015f + 0.01f * rel) * sr));
                    rep.gateEnv += (tgt - rep.gateEnv) * c;
                    for (int c2 = 0; c2 < 2; ++c2) ev[c2] = x[c2] * rep.gateEnv;
                }

                for (int c = 0; c < 2; ++c)
                {
                    y[c] = x[c] * (1.0f - rep.mix) + ev[c] * rep.mix;
                    wet[c] += ev[c] * rep.mix;
                }
                ++rep.pos;
                if (rep.pos >= rep.len && rep.mix <= 0.0f) rep.type = evNone;
                motionPeak = juce::jmax (motionPeak, rep.mix);
            }

            // ---------------- Filter motion (slow drift + sweeps into phrase ends)
            {
                const float walk = fWalk.tick (1.0f / (sec16 * 16.0f * 2.0f * sr), rng);
                float m = walk * 0.55f * st.filter * tex;
                if (fdActive)
                {
                    const float p = (float) fdPos / (float) juce::jmax (1, fdLen);
                    m += fdIntensity * p * std::sqrt (p);
                    if (++fdPos >= fdLen) fdActive = false;
                }
                m = juce::jlimit (0.0f, 1.0f, m);
                mSmooth += (m - mSmooth) * (m < mSmooth ? 0.004f : 0.0008f);
                if (mSmooth > 0.002f)
                {
                    if ((i & 7) == 0)
                    {
                        const float fc = 20000.0f * std::pow (fLow / 20000.0f, mSmooth);
                        gMotion = std::tan (kPi * juce::jmin (fc, sr * 0.45f) / sr);
                    }
                    const float wmix = juce::jmin (1.0f, mSmooth * 12.0f);
                    for (int c = 0; c < 2; ++c)
                    {
                        const float f = motionLp[c].lp (y[c], gMotion, 1.0f / 1.1f);
                        y[c] += (f - y[c]) * wmix;
                    }
                }
                else
                {
                    motionLp[0].reset(); motionLp[1].reset();
                }
                motionPeak = juce::jmax (motionPeak, mSmooth);
            }

            // ---------------- Reverse swell (additive)
            if (rev.active)
            {
                const float prog = (float) rev.pos / (float) juce::jmax (1, rev.len);
                const double pos = (double) rev.startAbs - 1.0 - (double) rev.pos * juce::jlimit (0.5f, 2.0f, pitchRate);
                const float tail = juce::jmin (1.0f, (float) (rev.len - rev.pos) / (0.004f * sr));
                const float amp = rev.intensity * prog * prog * tail * 0.9f;
                for (int c = 0; c < 2; ++c)
                {
                    const float v = evLp[c].lp (readCap (c, pos), gEv, 1.3f) * amp;
                    y[c] += v;
                    wet[c] += v;
                }
                if (++rev.pos >= rev.len) rev.active = false;
                motionPeak = juce::jmax (motionPeak, prog);
            }

            // ---------------- Delay throws (send gate into tempo echo)
            {
                const float tgt = sendLeft > 0 ? 1.0f : 0.0f;
                sendEnv += (tgt - sendEnv) * 0.004f;
                if (sendLeft > 0) --sendLeft;
                echoTime += (echoTimeTarget - echoTime) * 0.0005f;
                const float inMono = 0.5f * (y[0] + y[1]) * sendEnv * sendGain;
                const float dl = readDly (0, echoTime), dr = readDly (1, echoTime);
                fbLpL += (dl - fbLpL) * fbLpC; fbLpR += (dr - fbLpR) * fbLpC;
                fbHpL += (fbLpL - fbHpL) * fbHpC; fbHpR += (fbLpR - fbHpR) * fbHpC;
                const float oL = fbLpL - fbHpL, oR = fbLpR - fbHpR;
                dly[0][(size_t) dW] = inMono + oR * fb;
                dly[1][(size_t) dW] = oL * fb;
                if (++dW >= dlySize) dW = 0;
                const float spread = 0.5f + 0.5f * s.width;
                const float eL = oL * (0.5f + 0.5f * spread) + oR * (0.5f - 0.5f * spread);
                const float eR = oR * (0.5f + 0.5f * spread) + oL * (0.5f - 0.5f * spread);
                y[0] += eL * 0.8f; y[1] += eR * 0.8f;
                wet[0] += eL * 0.8f; wet[1] += eR * 0.8f;
                motionPeak = juce::jmax (motionPeak, sendEnv);
            }

            // ---------------- Stereo movement (mono-compatible: only touches the side signal)
            if (wid > 0.0005f)
            {
                const float mid = 0.5f * (y[0] + y[1]);
                float side = 0.5f * (y[0] - y[1]);
                haas[(size_t) hW] = mid;
                const float lfo = wWalk.tick (1.0f / (sec16 * 16.0f * sr), rng) * 2.0f - 1.0f;
                const float d = (0.009f + 0.005f * lfo) * sr;
                float rp = (float) hW - d; while (rp < 0) rp += (float) haasSize;
                const int i0 = (int) rp; const float fr = rp - (float) i0;
                const float dm = haas[(size_t) (i0 % haasSize)] * (1 - fr) + haas[(size_t) ((i0 + 1) % haasSize)] * fr;
                if (++hW >= haasSize) hW = 0;
                side = side * (1.0f + 0.35f * wid * lfo) + dm * 0.4f * wid;
                y[0] = mid + side; y[1] = mid - side;
            }

            // ---------------- Output
            bypassMix += ((s.bypass ? 1.0f : 0.0f) - bypassMix) > 0 ? bypStep : -bypStep;
            bypassMix = juce::jlimit (0.0f, 1.0f, bypassMix);
            float o[2];
            for (int c = 0; c < 2; ++c)
            {
                const float v = s.wetOnly ? wet[c] : y[c];
                o[c] = juce::jlimit (-4.0f, 4.0f, v);
            }
            L[i] = o[0] * (1.0f - bypassMix) + inL * bypassMix;
            R[i] = o[1] * (1.0f - bypassMix) + inR * bypassMix;
            ++wAbs;
        }

        motion = juce::jmax (motionPeak, std::abs (gGain) / 3.0f);
    }

private:
    struct Replace
    {
        int type = evNone;
        int64_t startAbs = 0;
        int len = 0, pos = 0;
        float mix = 0, slice = 1, readOff = 0, intensity = 1, gateEnv = 1;
        double readPos = 0;
        bool accelerate = false, riser = false;
        uint32_t pattern = 0;
    };
    struct Reverse { bool active = false; int64_t startAbs = 0; int len = 0, pos = 0; float intensity = 1; };
    struct Pending { int type; int64_t step; int len; int variant; };

    float readCap (int c, double absPos) const
    {
        double p = std::fmod (absPos, (double) capSize);
        if (p < 0) p += capSize;
        const int i0 = (int) p;
        const float f = (float) (p - i0);
        const auto& b = cap[(size_t) c];
        return b[(size_t) i0] * (1.0f - f) + b[(size_t) ((i0 + 1) % capSize)] * f;
    }

    float readDly (int c, float delay) const
    {
        float p = (float) dW - delay;
        while (p < 0) p += (float) dlySize;
        const int i0 = (int) p;
        const float f = p - (float) i0;
        const auto& b = dly[(size_t) c];
        return b[(size_t) (i0 % dlySize)] * (1.0f - f) + b[(size_t) ((i0 + 1) % dlySize)] * f;
    }

    static int64_t floorMod (int64_t a, int64_t m) { const int64_t r = a % m; return r < 0 ? r + m : r; }

    int phraseSteps() const { return stepsPerBar * curStyle->phraseBars; }

    void schedule (int type, int64_t step, int len, int variant = 0)
    {
        if (numPending >= (int) pending.size()) return;
        pending[(size_t) numPending++] = { type, step, len, variant };
    }

    bool quiet() const { return quietSamples > (int64_t) (sr * 2.0f); }

    int pickType (float wRoll, float wThrow, float wRev, float wGate, float wTape)
    {
        const float w[] { wRoll, wThrow, wRev, wGate, wTape };
        const int types[] { evRoll, evThrow, evReverse, evGate, evTape };
        float sum = 0; for (float v : w) sum += v;
        if (sum <= 0) return evNone;
        float r = rng.uni() * sum;
        for (int i = 0; i < 5; ++i) { if (r < w[i]) return types[i]; r -= w[i]; }
        return types[4];
    }

    void planPhrase (int64_t ps, int64_t fromStep)
    {
        const Style& st = *curStyle;
        const float tex = cur.texture;
        if (tex < 0.001f || quiet()) return;
        const int P = phraseSteps();
        const int64_t end = ps + P;

        if (st.filter > 0 && rng.uni() < tex * st.filter * 0.8f)
        {
            const int len = (st.phraseBars >= 8 && rng.uni() < 0.5f) ? stepsPerBar * 2 : stepsPerBar;
            if (end - len >= fromStep) schedule (evFilter, end - len, len);
        }

        if (rng.uni() < juce::jmin (1.0f, tex * 1.6f))
        {
            const int t = pickType (st.roll, st.throwW, st.reverse, st.gate, st.tape);
            int len = 4; int64_t start = end - 4; int variant = 0;
            switch (t)
            {
                case evRoll:    len = (tex > 0.6f && rng.uni() < 0.5f) ? 8 : 4; start = end - len; variant = rng.uni() < 0.5f ? 1 : 0; break;
                case evThrow:   len = 2; start = end - (rng.uni() < 0.5f ? 2 : 4); break;
                case evReverse: len = cur.release > 0.6f ? 8 : 4; start = end - len; break;
                case evGate:    len = 4; start = end - 4; break;
                case evTape:    len = (tex > 0.5f && rng.uni() < 0.5f) ? 8 : 4; start = end - len; break;
                default: return;
            }
            if (start >= fromStep) schedule (t, start, len, variant);

            // extra: throw the tail of a roll/gate/tape into the echo
            if (t != evThrow && t != evReverse && rng.uni() < tex * 0.45f * st.throwW && end - 2 >= fromStep)
                schedule (evThrow, end - 2, 2);
        }
    }

    void planBar (int64_t bs, int64_t fromStep)
    {
        const Style& st = *curStyle;
        const float tex = cur.texture;
        if (tex < 0.001f || quiet()) return;
        if (rng.uni() >= tex * st.barFill * 0.6f) return;
        const int64_t end = bs + stepsPerBar;
        const int t = pickType (st.roll, st.throwW, st.reverse * 0.5f, st.gate, st.tape * 0.5f);
        int len = 2; int64_t start = end - 2;
        if (t == evGate) { len = 4; start = end - 4; }
        if (t == evNone || start < fromStep) return;
        schedule (t, start, len, 2);
    }

    void resync (int64_t step)
    {
        numPending = 0;
        rep.pos = rep.len;   // let any active replace event fade out
        rev.active = false;
        sendLeft = 0;
        fdActive = false;
        const int P = phraseSteps();
        const int64_t ps = step - floorMod (step, P);
        const int64_t bs = step - floorMod (step, stepsPerBar);
        planPhrase (ps, step);
        if (bs + stepsPerBar < ps + P) planBar (bs, step);
        onStep (step, true);
    }

    void onStep (int64_t step, bool skipPlanning = false)
    {
        const int P = phraseSteps();
        if (! skipPlanning)
        {
            if (floorMod (step, P) == 0) planPhrase (step, step);
            if (floorMod (step, stepsPerBar) == 0 && floorMod (step, P) < P - stepsPerBar) planBar (step, step);
        }
        for (int k = 0; k < numPending;)
        {
            auto& p = pending[(size_t) k];
            if (p.step == step) { start (p); pending[(size_t) k] = pending[(size_t) --numPending]; }
            else if (p.step < step) { pending[(size_t) k] = pending[(size_t) --numPending]; }
            else ++k;
        }
    }

    void start (const Pending& p)
    {
        const float tex = cur.texture;
        const int lenS = juce::jmax (1, (int) ((float) p.len * sec16 * sr));
        switch (p.type)
        {
            case evRoll:
            case evTape:
            case evGate:
            {
                if (rep.type != evNone && rep.mix > 0.0f && rep.pos < rep.len) return;   // one at a time
                rep = {};
                rep.type = p.type;
                rep.startAbs = wAbs;
                rep.len = lenS;
                rep.intensity = juce::jlimit (0.3f, 1.0f, tex * 1.2f);
                if (p.type == evRoll)
                {
                    const bool small = p.variant == 2;
                    const float frac = small ? 0.5f : (p.len >= 8 ? 1.0f : (p.variant == 1 ? 0.5f : 1.0f));
                    rep.slice = juce::jmax (64.0f, frac * sec16 * sr);
                    rep.accelerate = ! small && p.len >= 8;
                    rep.riser = curStyle->riser && ! small;
                }
                else if (p.type == evTape)
                {
                    rep.readPos = (double) wAbs;
                }
                else
                {
                    uint32_t pat = 1u;
                    for (int b = 1; b < p.len * 2; ++b)
                        if (rng.uni() < 0.55f) pat |= (1u << b);
                    rep.pattern = pat;
                    rep.gateEnv = 1.0f;
                }
                break;
            }
            case evThrow:
                sendLeft = lenS;
                sendGain = juce::jlimit (0.4f, 1.0f, tex * 1.3f);
                {
                    const float r = rng.uni();
                    const float steps = r < 0.45f ? 3.0f : (r < 0.8f ? 2.0f : 4.0f);
                    echoTimeTarget = juce::jmin ((float) dlySize - 4.0f, steps * sec16 * sr);
                    if (echoTime < 1.0f) echoTime = echoTimeTarget;
                }
                break;
            case evReverse:
                rev.active = true;
                rev.startAbs = wAbs;
                rev.len = lenS;
                rev.pos = 0;
                rev.intensity = juce::jlimit (0.4f, 1.0f, tex * 1.3f);
                evLp[0].reset(); evLp[1].reset();
                break;
            case evFilter:
                fdActive = true;
                fdLen = lenS;
                fdPos = 0;
                fdIntensity = juce::jlimit (0.3f, 1.0f, tex * curStyle->filter * 1.4f);
                break;
            default: break;
        }
        lastEvent = p.type;
        ++eventCounter;
    }

    float sr = 48000.0f, bpm = 120.0f, sec16 = 0.125f;
    int stepsPerBar = 16;
    Settings cur;
    const Style* curStyle = &styles()[0];
    Rng rng;

    std::array<std::vector<float>, 2> cap, dly;
    std::vector<float> haas;
    int capSize = 1, dlySize = 1, haasSize = 1, dW = 0, hW = 0;
    int64_t wAbs = 0, lastStep = INT64_MIN, quietSamples = 0;
    double internalPpq = 0.0;

    std::array<Pending, 8> pending {};
    int numPending = 0;

    Replace rep;
    Reverse rev;
    int sendLeft = 0;
    float sendEnv = 0, sendGain = 1, echoTime = 1, echoTimeTarget = 1;
    float fbL = 0, fbR = 0, fbLpL = 0, fbLpR = 0, fbHpL = 0, fbHpR = 0;
    bool fdActive = false;
    int fdLen = 0, fdPos = 0;
    float fdIntensity = 1.0f;
    Svf motionLp[2], evLp[2], riserHp[2];
    float gMotion = 1.0f, mSmooth = 0.0f;
    Walk fWalk, wWalk;
    float fast = 0, slow = 0;
    int holdoff = 0;
    float gGain = 0, gTilt = 0, gPan = 0, tGain = 0, tTilt = 0, tPan = 0;
    float tiltLp[2] { 0, 0 };
    float bypassMix = 0;
};
} // namespace va
