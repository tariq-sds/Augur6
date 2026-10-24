#pragma once
#include "../../Common/SpaceDSP.h"
#include <array>

// Relay: a twelve-machine stereo delay in the spirit of the classic studio delay pedal.
namespace relay
{
using namespace space;

enum Machine { mDigital, mTape, mBucket, mDual, mPattern, mReverse, mIce, mDuck, mSwell, mTrem, mFilter, mLofi, kNumMachines };

inline const char* machineName (int m)
{
    static const char* n[] { "DIGITAL", "dTAPE", "dBUCKET", "DUAL", "PATTERN", "REVERSE", "ICE", "DUCK", "SWELL", "TREM", "FILTER", "LO-FI" };
    return n[std::clamp (m, 0, kNumMachines - 1)];
}
inline const char* machineInfo (int m)
{
    static const char* t[] {
        "Clean, exact repeats. SMEAR blurs them into a wash.",
        "Tape echo: warm, saturating repeats with wow and flutter.",
        "Bucket-brigade analog: repeats get darker the longer the time.",
        "Two delays at once. RATIO sets the second one against the first.",
        "Rhythmic multi-tap patterns locked to the delay time.",
        "Every repeat plays backwards. OCTAVE adds an octave up.",
        "Repeats are pitch-shifted slices that climb with each echo.",
        "Repeats duck under your playing and bloom in the gaps.",
        "Swells each note in like a volume pedal, then repeats it.",
        "Tremolo on the repeats.",
        "A sweeping resonant filter on the repeats.",
        "Crushed bits, low sample rate and an old-radio band.",
    };
    return t[std::clamp (m, 0, kNumMachines - 1)];
}
// what the two machine-specific knobs mean
inline std::pair<const char*, const char*> paramNames (int m)
{
    switch (m)
    {
        case mDigital: return { "SMEAR", "SMEAR SIZE" };
        case mTape:    return { "TAPE AGE", "WOW" };
        case mBucket:  return { "DARKNESS", "CHORUS" };
        case mDual:    return { "RATIO", "DELAY 2" };
        case mPattern: return { "PATTERN", "TAP DECAY" };
        case mReverse: return { "FORWARD", "OCTAVE" };
        case mIce:     return { "INTERVAL", "BLEND" };
        case mDuck:    return { "SENSITIVITY", "RELEASE" };
        case mSwell:   return { "RISE", "SWELL DRY" };
        case mTrem:    return { "SHAPE", "STEREO" };
        case mFilter:  return { "RESONANCE", "LP > BP > HP" };
        case mLofi:    return { "SAMPLE RATE", "RADIO" };
        default:       return { "PARAM 1", "PARAM 2" };
    }
}

struct Settings
{
    int machine = mDigital;
    float timeSec = 0.5f, repeats = 0.4f, mix = 0.5f, filter = 0.75f, grit = 0, speed = 0.3f, depth = 0.2f;
    float p1 = 0.5f, p2 = 0.5f, width = 0.5f, lowCut = 120, outDb = 0;
    bool killDry = true, hold = false;
};

class Engine
{
public:
    void prepare (double rate)
    {
        sr = (float) rate;
        const int maxN = (int) (sr * 4.6f);
        for (auto* d : { &lineL, &lineR }) d->setMax (maxN * 3);   // reverse octave reads up to 3x
        for (auto* d : { &bL, &bR }) d->setMax (maxN);
        for (auto& ch : diff) for (auto& a : ch) a.setMax ((int) (sr * 0.12f));
        for (auto& p : shift) { p.setMax ((int) (sr * 0.2f)); p.window = sr * 0.05f; }
        reset();
    }

    void reset()
    {
        for (auto* d : { &lineL, &lineR, &bL, &bR }) d->clear();
        for (auto& ch : diff) for (auto& a : ch) a.clear();
        for (auto& p : shift) p.clear();
        for (int c = 0; c < 2; ++c) { loopLp[c].clear(); loopHp[c].clear(); inHp1[c].clear(); inHp2[c].clear(); band1[c].clear(); band2[c].clear(); sweep[c].clear(); patLp[c].clear(); bLp[c].clear(); held[c] = 0; }
        fbL = fbR = fbBL = fbBR = 0; timeS = -1; lfo = 0; wow = 0; wowT = 0; revPhase = 0; swellG = 0; holdCount = 0;
    }

    void process (float* L, float* R, int n, const Settings& s)
    {
        const int m = std::clamp (s.machine, 0, (int) kNumMachines - 1);
        const float target = clampf (s.timeSec, 0.01f, m == mReverse ? 1.5f : 4.0f) * sr;
        if (timeS < 0) timeS = target;
        const float glide = 1.0f - std::exp (-1.0f / ((m == mTape || m == mBucket ? 0.25f : 0.04f) * sr));
        const float fb = s.hold ? 1.0f : clampf (s.repeats, 0.0f, 1.1f);
        const float loopHz = 800.0f * std::exp2 (s.filter * 4.6f);
        const float lfoHz = 0.05f * std::exp2 (s.speed * 7.6f);           // 0.05 .. ~10 Hz
        const float modDepth = s.depth * s.depth * 0.004f * sr;            // up to 4 ms
        const float drive = 1.0f + s.grit * 5.0f;
        const float pp = clampf (s.width * 2.0f - 1.0f, 0.0f, 1.0f), st = clampf (s.width * 2.0f, 0.0f, 1.0f);
        const float wetG = std::min (1.0f, 2.0f * s.mix), dryG = s.killDry ? 0.0f : std::min (1.0f, 2.0f * (1.0f - s.mix));
        const float outG = dbToGain (s.outDb);
        for (int c = 0; c < 2; ++c)
        {
            float hz = loopHz;
            if (m == mTape) hz *= 1.0f - 0.65f * s.p1;
            if (m == mBucket) hz = std::min (hz, clampf (4200.0f * std::sqrt (0.3f / clampf (s.timeSec, 0.02f, 4.0f)), 1500.0f, 12000.0f) * (1.0f - 0.55f * s.p1));
            loopLp[c].setHz (hz, sr);
            loopHp[c].setHz (m == mTape ? 90.0f : 40.0f, sr);
            patLp[c].setHz (loopHz, sr); bLp[c].setHz (loopHz, sr);
            inHp1[c].setHz (s.lowCut, sr); inHp2[c].setHz (s.lowCut, sr);
            band1[c].setHz (8000.0f - 5200.0f * s.p2, sr);
            band2[c].setHz (60.0f + 600.0f * s.p2, sr);
        }
        duckEnv.set (0.004f, 0.05f + s.p2 * 1.5f, sr);
        swellEnv.set (0.002f, 0.08f, sr);
        const float wowCoef = 1.0f - std::exp (-kTwoPi * 1.2f / sr);
        // pattern taps (fractions of the delay time)
        static const std::array<std::array<float, 5>, 8> pat { {
            { 0.25f, 0.5f, 0.75f, 1, 0 }, { 0.375f, 0.75f, 1, 0, 0 }, { 0.3333f, 0.6667f, 1, 0, 0 }, { 0.5f, 0.75f, 1, 0, 0 },
            { 0.25f, 0.375f, 0.75f, 1, 0 }, { 0.1875f, 0.375f, 0.5625f, 0.75f, 1 }, { 0.5f, 1, 0, 0, 0 }, { 0.125f, 0.25f, 0.5f, 1, 0 } } };
        const auto& taps = pat[(size_t) std::clamp ((int) (s.p1 * 7.99f), 0, 7)];
        static const float ratios[] { 0.25f, 0.3333f, 0.5f, 0.6667f, 0.75f, 1.5f };
        const float ratioB = ratios[std::clamp ((int) (s.p1 * 5.99f), 0, 5)];
        static const float intervals[] { -12, -7, -5, 5, 7, 12, 19, 24 };
        const float iceRatio = std::exp2 (intervals[std::clamp ((int) (s.p1 * 7.99f), 0, 7)] / 12.0f);
        const float duckThr = dbToGain (-12.0f - 40.0f * s.p1);
        const float swellRate = 1.0f - std::exp (-1.0f / ((0.05f + 2.5f * s.p1 * s.p1) * sr));
        const float holdSr = 1.0f + s.p1 * s.p1 * 30.0f;
        const float bits = std::exp2 (16.0f - s.grit * 12.0f);

        for (int i = 0; i < n; ++i)
        {
            timeS += (target - timeS) * glide;
            const float dryL = L[i], dryR = R[i];
            // wet input: low cut so returns stay clean
            float xl = inHp2[0].hp (inHp1[0].hp (dryL)), xr = inHp2[1].hp (inHp1[1].hp (dryR));

            // modulation
            lfo += lfoHz / sr; if (lfo >= 1.0f) lfo -= 1.0f;
            const float sine = std::sin (kTwoPi * lfo);
            if (--wowT <= 0) { wowTarget = rng.next(); wowT = (int) (sr * 0.3f); }
            wow += (wowTarget - wow) * wowCoef;
            float mod = 0;
            if (m == mTape) mod = sine * modDepth * 0.35f + wow * s.p2 * 0.006f * sr;
            else if (m == mBucket) mod = sine * modDepth + std::sin (kTwoPi * lfo * 0.37f) * s.p2 * 0.003f * sr;
            else if (m != mReverse && m != mPattern) mod = sine * modDepth;

            // swell / duck envelopes
            const float inLevel = 0.5f * (std::fabs (dryL) + std::fabs (dryR));
            if (m == mSwell)
            {
                const float e = swellEnv.process (inLevel);
                if (e > 0.004f) swellG += (1.0f - swellG) * swellRate; else swellG *= 0.995f;
                xl *= swellG * swellG; xr *= swellG * swellG;
            }
            if (s.hold) { xl = xr = 0; }

            // read
            float yl, yr;
            if (m == mReverse)
            {
                const float T = timeS, oct = s.p2 > 0.5f ? 3.0f : 2.0f;
                revPhase += 1.0f / T; if (revPhase >= 1.0f) revPhase -= 1.0f;
                const float ph2 = revPhase + 0.5f - std::floor (revPhase + 0.5f);
                const float w1 = std::sin (kPi * revPhase), w2 = std::sin (kPi * ph2);
                auto rev = [&] (const Delay& d) { return d.read (1.0f + oct * T * revPhase) * w1 * w1 + d.read (1.0f + oct * T * ph2) * w2 * w2; };
                yl = rev (lineL); yr = rev (lineR);
                const float fwd = s.p1;
                yl = yl * (1 - fwd) + lineL.read (T) * fwd; yr = yr * (1 - fwd) + lineR.read (T) * fwd;
            }
            else if (m == mPattern)
            {
                yl = yr = 0; int nt = 0; float last = 0, lastR = 0;
                for (auto f : taps) if (f > 0) ++nt;
                for (int k = 0, j = 0; k < 5; ++k)
                {
                    if (taps[(size_t) k] <= 0) continue;
                    const float g = 1.0f - s.p2 * 0.8f * (float) (nt - 1 - j) / std::max (1, nt - 1);
                    const float a = lineL.read (timeS * taps[(size_t) k]), b = lineR.read (timeS * taps[(size_t) k]);
                    yl += a * g; yr += b * g; last = a; lastR = b; ++j;
                }
                yl *= 0.7f; yr *= 0.7f;
                fbTapL = last; fbTapR = lastR;
            }
            else
            {
                yl = lineL.read (timeS + mod);
                yr = lineR.read (timeS + mod);
            }

            // colour each repeat (this is also what goes round the loop)
            float cl = colour (0, yl, m, s, drive, bits, holdSr);
            float cr = colour (1, yr, m, s, drive, bits, holdSr);
            if (m == mIce)
            {
                const float il = shift[0].process (cl, iceRatio), ir = shift[1].process (cr, iceRatio);
                cl = cl * (1 - s.p2) + il * s.p2; cr = cr * (1 - s.p2) + ir * s.p2;
            }

            // feedback (pattern feeds back from its last tap)
            float fl = m == mPattern ? patLp[0].lp (fbTapL) : cl;
            float fr = m == mPattern ? patLp[1].lp (fbTapR) : cr;
            if (s.hold) { fl = m == mPattern ? fbTapL : yl; fr = m == mPattern ? fbTapR : yr; }
            const float monoIn = 0.5f * (xl + xr);
            const float inL = (xl * st + monoIn * (1 - st)) * (1 - pp) + monoIn * pp;
            const float inR = (xr * st + monoIn * (1 - st)) * (1 - pp);
            float wl = inL + fb * (fl * (1 - pp) + fr * pp);
            float wr = inR + fb * (fr * (1 - pp) + fl * pp);
            if (! s.hold) { wl = softClip (wl * 0.5f) * 2.0f; wr = softClip (wr * 0.5f) * 2.0f; }
            lineL.push (wl); lineR.push (wr);

            float outL = cl, outR = cr;
            if (m == mDual)
            {
                const float bl = bL.read (timeS * ratioB + mod), br = bR.read (timeS * ratioB + mod);
                const float cbl = bLp[0].lp (bl), cbr = bLp[1].lp (br);
                bL.push (softClip ((inL + fb * 0.85f * cbl) * 0.5f) * 2.0f); bR.push (softClip ((inR + fb * 0.85f * cbr) * 0.5f) * 2.0f);
                outL = cl + s.p2 * cbl; outR = cr + s.p2 * cbr;
            }
            // effects on the repeats only
            if (m == mDuck)
            {
                const float e = duckEnv.process (inLevel);
                const float g = 1.0f / (1.0f + std::pow (e / duckThr, 2.0f) * 4.0f);
                outL *= g; outR *= g;
            }
            else if (m == mTrem)
            {
                auto shape = [&] (float ph) { const float sn = std::sin (kTwoPi * ph); const float sq = sn >= 0 ? 1.0f : -1.0f; return sn * (1 - s.p1) + sq * s.p1; };
                const float tl = 1.0f - s.depth * (0.5f + 0.5f * shape (lfo)), tr = 1.0f - s.depth * (0.5f + 0.5f * shape (lfo + s.p2 * 0.5f));
                outL *= tl; outR *= tr;
            }
            else if (m == mFilter)
            {
                const float hz = 700.0f * std::exp2 (sine * s.depth * 3.0f);
                const float q = 0.7f + s.p1 * 7.0f;
                float o[2] { outL, outR };
                for (int c = 0; c < 2; ++c)
                {
                    sweep[c].process (o[c], hz, q, sr);
                    const float t = s.p2 * 2.0f;
                    o[c] = t < 1 ? sweep[c].lpo * (1 - t) + sweep[c].bpo * t : sweep[c].bpo * (2 - t) + sweep[c].hpo * (t - 1);
                }
                outL = o[0]; outR = o[1];
            }

            float dl = dryL, dr = dryR;
            if (m == mSwell && s.p2 > 0.5f) { dl *= swellG * swellG; dr *= swellG * swellG; }
            L[i] = (dl * dryG + outL * wetG) * outG;
            R[i] = (dr * dryG + outR * wetG) * outG;
        }
        if (s.hold) ++holdCount; else holdCount = 0;
    }

private:
    float colour (int c, float y, int m, const Settings& s, float drive, float bits, float holdSr)
    {
        y = loopHp[c].hp (loopLp[c].lp (y));
        switch (m)
        {
            case mDigital:
            {
                if (s.p1 > 0.01f)
                {
                    static const float lens[] { 0.0113f, 0.0171f, 0.0234f, 0.0297f };
                    float v = y;
                    for (int k = 0; k < 4; ++k) { diff[c][(size_t) k].len = lens[k] * sr * (0.3f + 1.4f * s.p2); v = diff[c][(size_t) k].process (v, 0.62f); }
                    y = y * (1 - s.p1) + v * s.p1;
                }
                if (s.grit > 0.01f) y = softClip (y * drive) / drive;
                return y;
            }
            case mTape:   return softClip (y * drive * 1.3f) / (drive * 1.3f);
            case mBucket: return softClip (y * (1.0f + s.grit * 2.0f)) / (1.0f + s.grit * 2.0f) + rng.next() * s.grit * 0.0015f;
            case mLofi:
            {
                if (++holdN[c] >= (int) holdSr) { holdN[c] = 0; held[c] = std::round (y * bits) / bits; }
                float v = band2[c].hp (band1[c].lp (held[c]));
                return v * (1.0f + s.p2 * 0.6f) + rng.next() * s.p2 * 0.002f;
            }
            default:
                return s.grit > 0.01f ? softClip (y * drive) / drive : y;
        }
    }

    float sr = 48000;
    Delay lineL, lineR, bL, bR;
    std::array<std::array<Allpass, 4>, 2> diff;
    std::array<PitchShifter, 2> shift;
    OnePole loopLp[2], loopHp[2], patLp[2], bLp[2], inHp1[2], inHp2[2], band1[2], band2[2];
    Svf sweep[2];
    Follower duckEnv, swellEnv;
    Rng rng;
    float timeS = -1, lfo = 0, wow = 0, wowTarget = 0, revPhase = 0, swellG = 0;
    float fbL = 0, fbR = 0, fbBL = 0, fbBR = 0, fbTapL = 0, fbTapR = 0;
    float held[2] {};
    int holdN[2] {}, wowT = 0, holdCount = 0;
};
} // namespace relay
