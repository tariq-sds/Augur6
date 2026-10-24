#pragma once
#include "../../Common/SpaceDSP.h"
#include <array>

// Vault: a twelve-machine stereo reverb in the spirit of the classic studio reverb pedal. Most machines are
// built on one 8-line feedback delay network (exact decay times, separate low-end decay, modulated lines),
// with machine-specific stages around it: early reflections, springs, bloom diffusers, formants, shimmer,
// a tape echo and nonlinear envelopes.
namespace vault
{
using namespace space;

enum Machine { mRoom, mHall, mPlate, mSpring, mSwell, mBloom, mCloud, mChorale, mShimmer, mMagneto, mNonlinear, mReflections, kNumMachines };

inline const char* machineName (int m)
{
    static const char* n[] { "ROOM", "HALL", "PLATE", "SPRING", "SWELL", "BLOOM", "CLOUD", "CHORALE", "SHIMMER", "MAGNETO", "NONLINEAR", "REFLECTIONS" };
    return n[std::clamp (m, 0, kNumMachines - 1)];
}
inline const char* machineInfo (int m)
{
    static const char* t[] {
        "A real room: early reflections and a short, natural tail.",
        "Big concert hall. LOW END sets how long the bass rings.",
        "Bright, dense studio plate.",
        "Vintage spring tank: drip, boing and twang.",
        "Each note swells in, then blooms into the reverb.",
        "The reverb builds and blooms slowly after every note.",
        "A huge, soft, endlessly diffused ambient wash.",
        "Singing vowel formants in the tail, like a distant choir.",
        "Octaves climb out of the tail.",
        "Multi-head tape echo feeding a reverb.",
        "Gated, reversed and swooshing reverb shapes.",
        "Early reflections of a real space: shape and position.",
    };
    return t[std::clamp (m, 0, kNumMachines - 1)];
}
inline std::pair<const char*, const char*> paramNames (int m)
{
    switch (m)
    {
        case mRoom:        return { "SIZE", "EARLY" };
        case mHall:        return { "SIZE", "LOW END" };
        case mPlate:       return { "LOW END", "DIFFUSION" };
        case mSpring:      return { "DWELL", "TENSION" };
        case mSwell:       return { "RISE", "SWELL DRY" };
        case mBloom:       return { "LENGTH", "FEEDBACK" };
        case mCloud:       return { "DIFFUSION", "SIZE" };
        case mChorale:     return { "VOWEL", "RESONANCE" };
        case mShimmer:     return { "INTERVAL", "AMOUNT" };
        case mMagneto:     return { "HEAD TIME", "REPEATS" };
        case mNonlinear:   return { "SHAPE", "DIFFUSION" };
        case mReflections: return { "SHAPE", "POSITION" };
        default:           return { "PARAM 1", "PARAM 2" };
    }
}

struct Settings
{
    int machine = mHall;
    float decay = 2.5f, predelay = 0.02f, mix = 0.5f, tone = 0, mod = 0.3f, p1 = 0.5f, p2 = 0.5f;
    float lowCut = 150, highCut = 12000, width = 0.5f, outDb = 0;
    bool killDry = true, freeze = false;
};

class Engine
{
public:
    static constexpr int N = 8;

    void prepare (double rate)
    {
        sr = (float) rate;
        pre[0].setMax ((int) (sr * 0.6f)); pre[1].setMax ((int) (sr * 0.6f));
        for (auto& ch : inDiff) for (auto& a : ch) a.setMax ((int) (sr * 0.25f));
        for (auto& d : lines) d.setMax ((int) (sr * 0.09f * 2.7f + sr * 0.01f));
        for (auto& ch : bloom) for (auto& a : ch) a.setMax ((int) (sr * 0.5f));
        er[0].setMax ((int) (sr * 0.3f)); er[1].setMax ((int) (sr * 0.3f));
        tape.setMax ((int) (sr * 1.8f));
        for (auto& p : shim) { p.setMax ((int) (sr * 0.25f)); p.window = sr * 0.07f; }
        for (auto& ch : springs) for (auto& sp : ch) sp.line.setMax ((int) (sr * 0.08f));
        // early reflection patterns: 4 shapes x 16 taps (time in ms, gain)
        Rng r; r.s = 0xBEEF;
        for (int shape = 0; shape < 4; ++shape)
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 16; ++k)
                {
                    const float spread = shape == 0 ? 0.6f : shape == 1 ? 1.0f : shape == 2 ? 1.6f : 2.3f;   // small / studio / chamber / hall
                    const float t = (3.0f + k * 5.5f * spread + (r.next() + 1.0f) * 4.0f * spread) * (c ? 1.07f : 1.0f);
                    erTaps[(size_t) shape][(size_t) c][(size_t) k] = { t, (r.next() > 0 ? 1.0f : -1.0f) * std::exp (-k * 0.16f) };
                }
        reset();
    }

    void reset()
    {
        for (auto& d : pre) d.clear();
        for (auto& ch : inDiff) for (auto& a : ch) a.clear();
        for (auto& d : lines) d.clear();
        for (auto& ch : bloom) for (auto& a : ch) a.clear();
        for (auto& d : er) d.clear();
        tape.clear();
        for (auto& p : shim) p.clear();
        for (auto& ch : springs) for (auto& sp : ch) { sp.line.clear(); sp.ap.fill (0); sp.lp.clear(); }
        for (int i = 0; i < N; ++i) { lowSplit[i].clear(); damp[i].clear(); lfoPh[i] = i * 0.137f; }
        for (int c = 0; c < 2; ++c) { inHp[c].clear(); outHp[c].clear(); outLp[c].clear(); for (auto& f : formant[c]) f.clear(); }
        sizeS = -1; swellG = 0; bloomFb[0] = bloomFb[1] = 0; nlT = 1e9f; nlArmed = true; tapeFb = 0; shimFb = 0;
        fdnOut.fill (0);
    }

    void process (float* L, float* R, int n, const Settings& s)
    {
        const int m = std::clamp (s.machine, 0, (int) kNumMachines - 1);
        // ---- per-machine shape of the network
        float size = 1.0f, diffG = 0.7f, erLevel = 0, bassMult = 1.0f, fdnLevel = 1.0f;
        switch (m)
        {
            case mRoom:        size = 0.25f + 0.35f * s.p1; diffG = 0.62f; erLevel = 0.9f * s.p2; break;
            case mHall:        size = 0.7f + 0.9f * s.p1; diffG = 0.7f; erLevel = 0.25f; bassMult = 0.5f + 1.5f * s.p2; break;
            case mPlate:       size = 0.55f; diffG = 0.55f + 0.3f * s.p2; bassMult = 0.5f + 1.5f * s.p1; break;
            case mCloud:       size = 1.4f + 1.2f * s.p2; diffG = 0.6f + 0.25f * s.p1; break;
            case mShimmer:     size = 1.2f; break;
            case mBloom:       size = 1.1f; break;
            case mMagneto:     size = 0.8f; break;
            case mNonlinear:   size = 0.6f; diffG = 0.4f + 0.45f * s.p2; break;
            case mReflections: size = 0.5f; erLevel = 1.0f; fdnLevel = 0.25f; break;
            default: break;
        }
        if (sizeS < 0) sizeS = size;
        const float sizeCoef = 1.0f - std::exp (-1.0f / (0.25f * sr));
        float decay = s.decay;
        if (m == mNonlinear) decay = std::max (2.0f, s.decay);
        const float dampHz = clampf (s.highCut * std::exp2 (s.tone * 1.5f), 800.0f, 0.45f * sr);
        for (int i = 0; i < N; ++i) { damp[i].setHz (dampHz, sr); lowSplit[i].setHz (300.0f, sr); }
        for (int c = 0; c < 2; ++c) { inHp[c].setHz (s.lowCut, sr); outHp[c].setHz (s.lowCut, sr); outLp[c].setHz (clampf (s.highCut * std::exp2 (s.tone), 1000.0f, 0.45f * sr), sr); }
        static const float baseMs[N] { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.3f, 79.1f };
        const float modDepth = s.mod * s.mod * 0.0025f * sr;
        const float wetG = std::min (1.0f, 2.0f * s.mix), dryG = s.killDry ? 0.0f : std::min (1.0f, 2.0f * (1.0f - s.mix));
        const float outG = dbToGain (s.outDb);
        const float side = s.width * 2.0f;
        const float preS = clampf (s.predelay, 0.0f, 0.5f) * sr;
        static const float inDiffMs[4] { 4.7f, 3.6f, 12.7f, 9.3f };
        static const float cloudMs[4] { 37.0f, 53.0f, 71.0f, 97.0f };
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < 4; ++k) inDiff[c][(size_t) k].len = (m == mCloud ? cloudMs[k] : inDiffMs[k]) * 0.001f * sr * (c ? 1.09f : 1.0f);
        const float bloomLen = 0.06f + 0.3f * s.p1;
        static const float bloomMul[6] { 1.0f, 0.77f, 0.61f, 0.53f, 0.43f, 0.37f };
        for (int c = 0; c < 2; ++c) for (int k = 0; k < 6; ++k) bloom[c][(size_t) k].len = bloomLen * bloomMul[k] * sr * (c ? 1.05f : 1.0f);
        const float swellRate = 1.0f - std::exp (-1.0f / ((0.08f + 3.0f * s.p1 * s.p1) * sr));
        swellEnv.set (0.002f, 0.12f, sr);
        nlEnv.set (0.001f, 0.08f, sr);
        static const float shimSemis[] { 12, 7, 5, 19, -12, 24 };
        const float shimRatio = std::exp2 (shimSemis[std::clamp ((int) (s.p1 * 5.99f), 0, 5)] / 12.0f);
        const float tapeT = (0.06f + 0.24f * s.p1) * sr;
        // chorale formants (F1, F2, F3) for a / e / i / o / u, morphed by P1
        static const float vow[5][3] { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 270, 2140, 2950 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        const float vp = s.p1 * 3.999f; const int v0 = (int) vp; const float vf = vp - v0;
        float fHz[3]; for (int k = 0; k < 3; ++k) fHz[k] = vow[v0][k] * (1 - vf) + vow[std::min (4, v0 + 1)][k] * vf;
        const int erShape = std::clamp ((int) (s.p1 * 3.99f), 0, 3);
        const float erScale = m == mReflections ? 0.6f + 1.2f * s.p2 : 1.0f;
        const float nlLen = clampf (s.decay * 0.25f, 0.08f, 1.5f) * sr;
        const float springG = std::pow (10.0f, -3.0f * 0.04f / std::max (0.1f, s.decay));
        const float springA = 0.45f + 0.35f * s.p2;
        for (auto& ch : springs) for (int k = 0; k < 3; ++k) ch[(size_t) k].lp.setHz (4200.0f * std::exp2 (s.tone), sr);

        for (int i = 0; i < n; ++i)
        {
            sizeS += (size - sizeS) * sizeCoef;
            const float dryL = L[i], dryR = R[i];
            float xl = inHp[0].hp (dryL), xr = inHp[1].hp (dryR);
            const float inLevel = 0.5f * (std::fabs (dryL) + std::fabs (dryR));

            if (m == mSwell)
            {
                const float e = swellEnv.process (inLevel);
                if (e > 0.004f) swellG += (1.0f - swellG) * swellRate; else swellG *= 0.9995f;
                xl *= swellG * swellG; xr *= swellG * swellG;
            }
            if (s.freeze) xl = xr = 0;

            // pre-delay
            pre[0].push (xl); pre[1].push (xr);
            xl = preS >= 1 ? pre[0].read (preS) : xl; xr = preS >= 1 ? pre[1].read (preS) : xr;
            const float erInL = xl, erInR = xr;

            float wetL = 0, wetR = 0;
            if (m == mSpring)
            {
                float o[2] { 0, 0 };
                const float in[2] { softClip (xl * (1.0f + 3.0f * s.p1)) , softClip (xr * (1.0f + 3.0f * s.p1)) };
                static const float springMs[3] { 33.0f, 41.0f, 47.0f };
                for (int c = 0; c < 2; ++c)
                    for (int k = 0; k < 3; ++k)
                    {
                        auto& sp = springs[(size_t) c][(size_t) k];
                        float v = sp.line.read (springMs[k] * 0.001f * sr * (c ? 1.03f : 1.0f));
                        o[c] += v;
                        float u = in[c] * 0.5f + (s.freeze ? 1.0f : springG) * v;
                        for (auto& z : sp.ap) { const float y = springA * u + z; z = u - springA * y; u = y; }
                        sp.line.push (sp.lp.lp (u));
                    }
                wetL = o[0] * 0.5f; wetR = o[1] * 0.5f;
            }
            else
            {
                // machine stages before the network
                if (m == mMagneto)
                {
                    const float mono = 0.5f * (xl + xr);
                    float echo = 0;
                    for (int h = 1; h <= 4; ++h) echo += tape.read (tapeT * h) * (h == 4 ? 1.0f : 0.6f);
                    tape.push (softClip (mono + echo * 0.25f * s.p2 * 0.9f));
                    tapeOut = echo * 0.35f;
                    xl = xl * 0.4f + tapeOut; xr = xr * 0.4f + tapeOut;
                }
                if (m == mBloom)
                {
                    float b[2] { xl + bloomFb[0] * s.p2 * 0.75f, xr + bloomFb[1] * s.p2 * 0.75f };
                    for (int c = 0; c < 2; ++c) { for (auto& a : bloom[c]) b[c] = a.process (b[c], 0.65f); bloomFb[c] = softClip (b[c]); }
                    xl = b[0]; xr = b[1];
                }
                if (m == mShimmer)
                {
                    const float sh = shim[0].process (shimFb, shimRatio);
                    const float add = softClip (sh) * s.p2 * 0.55f;
                    xl += add; xr += add;
                }
                // input diffusion
                const float dg = s.freeze ? 0.0f : diffG;
                for (auto& a : inDiff[0]) xl = a.process (xl, dg);
                for (auto& a : inDiff[1]) xr = a.process (xr, dg);

                // the network
                std::array<float, N> y;
                for (int k = 0; k < N; ++k)
                {
                    lfoPh[k] += (0.3f + 0.13f * k) / sr; if (lfoPh[k] >= 1.0f) lfoPh[k] -= 1.0f;
                    const float len = baseMs[k] * 0.001f * sr * sizeS;
                    const float v = lines[(size_t) k].read (len + modDepth * std::sin (kTwoPi * lfoPh[k]) * (0.6f + 0.4f * sizeS));
                    if (s.freeze) { y[(size_t) k] = v; continue; }
                    const float lenSec = len / sr;
                    const float g = std::pow (10.0f, -3.0f * lenSec / std::max (0.05f, decay));
                    const float gl = std::pow (10.0f, -3.0f * lenSec / std::max (0.05f, decay * bassMult));
                    const float lo = lowSplit[k].lp (v);
                    y[(size_t) k] = lo * gl + damp[k].lp (v - lo) * g;
                }
                // Hadamard mix
                std::array<float, N> h = y;
                for (int len = 1; len < N; len <<= 1)
                    for (int a = 0; a < N; a += len << 1)
                        for (int b = a; b < a + len; ++b) { const float u = h[(size_t) b], w = h[(size_t) (b + len)]; h[(size_t) b] = u + w; h[(size_t) (b + len)] = u - w; }
                const float norm = 0.35355339f;
                for (int k = 0; k < N; ++k)
                {
                    const float inj = (k & 1) ? xr : xl;
                    const float sgn = (k & 2) ? -1.0f : 1.0f;
                    float wv = h[(size_t) k] * norm + inj * sgn * 0.5f;
                    if (! s.freeze) wv = softClip (wv * 0.5f) * 2.0f;
                    lines[(size_t) k].push (wv);
                }
                static const float sl[N] { 1, -1, 1, 1, -1, 1, -1, -1 }, sr8[N] { 1, 1, -1, 1, 1, -1, -1, 1 };
                for (int k = 0; k < N; ++k) { wetL += y[(size_t) k] * sl[k]; wetR += y[(size_t) k] * sr8[k]; }
                wetL *= 0.3f * fdnLevel; wetR *= 0.3f * fdnLevel;
                shimFb = 0.5f * (wetL + wetR);
                fdnOut = y;
            }

            // early reflections
            er[0].push (erInL); er[1].push (erInR);
            if (erLevel > 0)
            {
                float el = 0, erR = 0;
                const auto& tl = erTaps[(size_t) (m == mReflections ? erShape : m == mRoom ? 0 : 3)];
                for (int k = 0; k < 16; ++k)
                {
                    el  += er[0].readInt ((int) (tl[0][(size_t) k].first * erScale * 0.001f * sr) + 1) * tl[0][(size_t) k].second;
                    erR += er[1].readInt ((int) (tl[1][(size_t) k].first * erScale * 0.001f * sr) + 1) * tl[1][(size_t) k].second;
                }
                const float dist = m == mReflections ? 1.0f - 0.6f * s.p2 : 1.0f;
                wetL += el * 0.22f * erLevel * dist + (m == mReflections ? erInL * 0.3f * (1 - s.p2) : 0.0f);
                wetR += erR * 0.22f * erLevel * dist + (m == mReflections ? erInR * 0.3f * (1 - s.p2) : 0.0f);
            }

            // machine stages after the network
            if (m == mChorale)
            {
                float o[2] { wetL, wetR };
                for (int c = 0; c < 2; ++c)
                {
                    float f = 0;
                    for (int k = 0; k < 3; ++k) { formant[c][(size_t) k].process (o[c], fHz[k] * (c ? 1.02f : 1.0f), 4.0f + 8.0f * s.p2, sr); f += formant[c][(size_t) k].bpo / (4.0f + 8.0f * s.p2) * (k == 0 ? 1.0f : 0.7f); }
                    o[c] = o[c] * (1.0f - 0.75f * s.p2 - 0.15f) + f * (1.6f + 2.0f * s.p2);
                }
                wetL = o[0]; wetR = o[1];
            }
            if (m == mMagneto) { wetL += tapeOut * 0.5f; wetR += tapeOut * 0.5f; }
            if (m == mNonlinear)
            {
                const float e = nlEnv.process (inLevel);
                if (nlArmed && e > 0.02f) { nlT = 0; nlArmed = false; }
                if (e < 0.005f) nlArmed = true;
                const float t = nlT / nlLen;
                nlT += 1.0f;
                float g;
                if (s.p1 < 0.34f) g = t < 1.0f ? 1.0f : std::max (0.0f, 1.0f - (t - 1.0f) * 20.0f);              // gated
                else if (s.p1 < 0.67f) g = t < 1.0f ? t * t : std::max (0.0f, 1.0f - (t - 1.0f) * 20.0f);        // reverse
                else g = t < 1.0f ? std::sin (kPi * t) : 0.0f;                                                  // swoosh
                wetL *= g * 1.4f; wetR *= g * 1.4f;
            }

            // output: tone, low cut, width
            wetL = outHp[0].hp (outLp[0].lp (wetL));
            wetR = outHp[1].hp (outLp[1].lp (wetR));
            const float mid = 0.5f * (wetL + wetR), sd = 0.5f * (wetL - wetR) * side;
            wetL = mid + sd; wetR = mid - sd;

            float dl = dryL, dr = dryR;
            if (m == mSwell && s.p2 > 0.5f) { dl *= swellG * swellG; dr *= swellG * swellG; }
            L[i] = (dl * dryG + wetL * wetG) * outG;
            R[i] = (dr * dryG + wetR * wetG) * outG;
        }
    }

private:
    struct Spring { Delay line; std::array<float, 24> ap {}; OnePole lp; };

    float sr = 48000;
    Delay pre[2], er[2], tape;
    std::array<Delay, N> lines;
    std::array<std::array<Allpass, 4>, 2> inDiff;
    std::array<std::array<Allpass, 6>, 2> bloom;
    std::array<PitchShifter, 1> shim;
    std::array<std::array<Spring, 3>, 2> springs;
    std::array<std::array<std::array<std::pair<float, float>, 16>, 2>, 4> erTaps {};
    OnePole lowSplit[N], damp[N], inHp[2], outHp[2], outLp[2];
    std::array<std::array<Svf, 3>, 2> formant;
    Follower swellEnv, nlEnv;
    std::array<float, N> fdnOut {};
    float lfoPh[N] {};
    float sizeS = -1, swellG = 0, bloomFb[2] {}, nlT = 1e9f, tapeFb = 0, tapeOut = 0, shimFb = 0;
    bool nlArmed = true;
};
} // namespace vault
