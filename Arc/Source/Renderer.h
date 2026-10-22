#pragma once
#include "Plan.h"

// Plays one lane of the plan from its prepared loop. Same code runs live and for export, so what you
// hear in Arc is exactly what lands in Live.
namespace arc2
{
struct LaneState
{
    cl::Svf lp[2], hp[2];
    float gain = 0, lpHz = 20000, hpHz = 20;
    void reset() { for (auto& f : lp) f.reset(); for (auto& f : hp) f.reset(); gain = 0; lpHz = 20000; hpHz = 20; }
};

inline bool continues (const SliceOp& prev, const SliceOp& cur, int loopSteps)
{
    if (prev.rep != 1 || cur.rep != 1 || prev.rev != cur.rev) return false;
    if (cur.span > 1 || prev.span > 1)
        return cur.src == prev.src && cur.span == prev.span && cur.phase == prev.phase + 1;
    return cur.src == (prev.src + 1) % juce::jmax (1, loopSteps);
}

// Adds the lane into L/R for n samples starting at song position `beat` (quarter notes).
inline void renderLane (const LaneAudio& la, const LanePlan& plan, LaneState& st, float* L, float* R, int n,
                        double beat, double hostBpm, double sr, float laneGain)
{
    const int loopLen = la.loop.getNumSamples();
    if (loopLen < 16 || plan.bars.empty()) return;
    const int loopSteps = la.bars * 16;
    const double beatsPerSample = hostBpm / 60.0 / sr;
    const double srcStep = (double) loopLen / loopSteps;          // samples per 16th in the prepared loop
    const double hostStep = sr * 60.0 / hostBpm / 4.0;             // samples per 16th in the song
    const float fade = (float) (0.0025 * sr);
    const float gDown = 1.0f - std::exp (-1.0f / (float) (0.004 * sr));      // fades out over ~4 ms
    const float gUp = 1.0f - std::exp (-1.0f / (float) (0.0003 * sr));      // comes in almost instantly, so a hit on the 1 keeps its attack
    const float* s0 = la.loop.getReadPointer (0);
    const float* s1 = la.loop.getReadPointer (1);
    const int totalBars = (int) plan.bars.size();
    float gLp = cl::gOf (st.lpHz, (float) sr), gHp = cl::gOf (st.hpHz, (float) sr);

    for (int i = 0; i < n; ++i)
    {
        const double b = beat + i * beatsPerSample;
        float target = 0, l = 0, r = 0, lpT = 20000, hpT = 20;
        if (b >= 0)
        {
            const int bar = (int) (b / 4.0);
            if (bar < totalBars)
            {
                const auto& B = plan.bars[(size_t) bar];
                const double inBar = b - bar * 4.0;
                const int step = juce::jlimit (0, 15, (int) (inBar * 4.0));
                const double frac = inBar * 4.0 - step;
                const auto& op = B.steps[(size_t) step];
                const float bf = (float) (inBar / 4.0);
                lpT = lerpExp (B.lp0, B.lp1, bf);
                hpT = lerpExp (B.hp0, B.hp1, bf);
                if (B.on && ! op.mute)
                {
                    target = (B.g0 + (B.g1 - B.g0) * bf) * op.gain * laneGain;
                    double u;
                    float env = 1;
                    if (op.rep > 1)
                    {
                        const double sub = frac * op.rep, sf = sub - std::floor (sub);
                        u = sf / op.rep;
                        const float segLen = (float) (hostStep / op.rep);
                        env = juce::jmin (1.0f, (float) (sf * segLen) / fade, (float) ((1.0 - sf) * segLen) / fade);
                    }
                    else
                    {
                        u = op.phase + frac;
                        if (op.rev) u = op.span - u;
                        // de-click where the slice doesn't simply continue from / into its neighbours
                        const SliceOp* prev = step > 0 ? &B.steps[(size_t) step - 1] : (bar > 0 ? &plan.bars[(size_t) bar - 1].steps[15] : nullptr);
                        const SliceOp* next = step < 15 ? &B.steps[(size_t) step + 1] : (bar + 1 < totalBars ? &plan.bars[(size_t) bar + 1].steps[0] : nullptr);
                        if (prev == nullptr || ! continues (*prev, op, loopSteps)) env = juce::jmin (env, (float) (frac * hostStep) / fade);
                        if (next == nullptr || ! continues (op, *next, loopSteps)) env = juce::jmin (env, (float) ((1.0 - frac) * hostStep) / fade);
                    }
                    double pos = (op.src + u) * srcStep;
                    pos = std::fmod (pos, (double) loopLen); if (pos < 0) pos += loopLen;
                    const int i0 = (int) pos, i1 = (i0 + 1) % loopLen;
                    const float t = (float) (pos - i0);
                    l = (s0[i0] + (s0[i1] - s0[i0]) * t) * env;
                    r = (s1[i0] + (s1[i1] - s1[i0]) * t) * env;
                }
            }
        }
        st.gain += (target - st.gain) * (target > st.gain ? gUp : gDown);
        if ((i & 31) == 0)
        {
            st.lpHz = st.lpHz * std::pow (lpT / st.lpHz, 0.15f);
            st.hpHz = st.hpHz * std::pow (hpT / st.hpHz, 0.15f);
            gLp = cl::gOf (st.lpHz, (float) sr);
            gHp = cl::gOf (st.hpHz, (float) sr);
        }
        float ch[2] { l * st.gain, r * st.gain };
        for (int c = 0; c < 2; ++c)
        {
            float x = ch[c], bp, lpo;
            st.lp[c].tick (x, gLp, 1.2f, bp, lpo); x = lpo;     // k 1.2: gentle resonance on sweeps
            x = st.hp[c].highPass (x, gHp, 0.8f);
            ch[c] = x;
        }
        L[i] += ch[0];
        R[i] += ch[1];
    }
}
} // namespace arc2
