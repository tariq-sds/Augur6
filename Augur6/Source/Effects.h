#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "DSP.h"
#include <vector>

namespace augur
{

struct FxSettings
{
    float drive = 0;
    float choMix = 0, choRate = 0.6f, choDepth = 0.5f;
    float dlyTimeSec = 0.375f, dlyFb = 0.35f, dlyMix = 0; bool dlyPing = true;
    float revSize = 0.6f, revDamp = 0.4f, revMix = 0;
};

// Analog-flavoured output stage: drive -> stereo chorus -> delay -> reverb
class Effects
{
public:
    void prepare (double sampleRate, int)
    {
        sr = (float) sampleRate;
        choL.assign ((size_t) (sr * 0.05f) + 4, 0.0f);
        choR.assign (choL.size(), 0.0f);
        dlyL.assign ((size_t) (sr * 2.1f) + 4, 0.0f);
        dlyR.assign (dlyL.size(), 0.0f);
        choW = dlyW = 0;
        choPhase = 0.0f;
        dlyTimeSm = -1.0f;
        lpL = lpR = 0.0f;
        reverb.setSampleRate (sampleRate);
        reverb.reset();
        lastRev = {};
        lastRev.roomSize = -1.0f;
    }

    void reset()
    {
        std::fill (choL.begin(), choL.end(), 0.0f);
        std::fill (choR.begin(), choR.end(), 0.0f);
        std::fill (dlyL.begin(), dlyL.end(), 0.0f);
        std::fill (dlyR.begin(), dlyR.end(), 0.0f);
        reverb.reset();
    }

    void process (float* L, float* R, int n, const FxSettings& s)
    {
        // ---- Drive
        if (s.drive > 0.001f)
        {
            const float g = 1.0f + s.drive * 14.0f;
            const float makeup = 1.0f / (1.0f + s.drive * 2.2f);
            for (int i = 0; i < n; ++i)
            {
                L[i] = (L[i] * (1.0f - s.drive) + fastTanh (L[i] * g) * s.drive * 1.6f) * makeup * (1.0f + s.drive);
                R[i] = (R[i] * (1.0f - s.drive) + fastTanh (R[i] * g) * s.drive * 1.6f) * makeup * (1.0f + s.drive);
            }
        }

        // ---- Chorus (two modulated taps in anti-phase)
        if (s.choMix > 0.001f)
        {
            const int size = (int) choL.size();
            const float base = 0.0068f * sr;
            const float depth = (0.0005f + s.choDepth * 0.0042f) * sr;
            const float inc = s.choRate / sr;
            for (int i = 0; i < n; ++i)
            {
                choL[(size_t) choW] = L[i];
                choR[(size_t) choW] = R[i];
                const float m = std::sin (choPhase * 6.2831853f);
                const float wl = readFrac (choL, choW, base + depth * m, size);
                const float wr = readFrac (choR, choW, base - depth * m, size);
                L[i] = L[i] * (1.0f - 0.5f * s.choMix) + wl * s.choMix * 0.85f;
                R[i] = R[i] * (1.0f - 0.5f * s.choMix) + wr * s.choMix * 0.85f;
                if (++choW >= size) choW = 0;
                choPhase += inc; if (choPhase >= 1.0f) choPhase -= 1.0f;
            }
        }

        // ---- Delay
        if (s.dlyMix > 0.001f || dlyTail > 0)
        {
            const int size = (int) dlyL.size();
            const float target = juce::jlimit (0.001f * sr, 2.0f * sr, s.dlyTimeSec * sr);
            if (dlyTimeSm < 0.0f) dlyTimeSm = target;
            const float tCoef = 1.0f - std::exp (-1.0f / (0.08f * sr));
            const float dCoef = 1.0f - std::exp (-6.2831853f * 4200.0f / sr);
            const float fb = juce::jlimit (0.0f, 0.95f, s.dlyFb);
            for (int i = 0; i < n; ++i)
            {
                dlyTimeSm += (target - dlyTimeSm) * tCoef;
                const float dl = readFrac (dlyL, dlyW, dlyTimeSm, size);
                const float dr = readFrac (dlyR, dlyW, dlyTimeSm, size);
                lpL += (dl - lpL) * dCoef;
                lpR += (dr - lpR) * dCoef;
                if (s.dlyPing)
                {
                    dlyL[(size_t) dlyW] = 0.5f * (L[i] + R[i]) + lpR * fb;
                    dlyR[(size_t) dlyW] = lpL * fb;
                }
                else
                {
                    dlyL[(size_t) dlyW] = L[i] + lpL * fb;
                    dlyR[(size_t) dlyW] = R[i] + lpR * fb;
                }
                L[i] += dl * s.dlyMix;
                R[i] += dr * s.dlyMix;
                if (++dlyW >= size) dlyW = 0;
            }
            dlyTail = s.dlyMix > 0.001f ? (int) (sr * 8.0f) : std::max (0, dlyTail - n);
        }

        // ---- Reverb
        if (s.revMix > 0.001f)
        {
            juce::Reverb::Parameters rp;
            rp.roomSize = s.revSize;
            rp.damping = s.revDamp;
            rp.wetLevel = s.revMix * 0.6f;
            rp.dryLevel = 1.0f - s.revMix * 0.35f;
            rp.width = 1.0f;
            rp.freezeMode = 0.0f;
            if (rp.roomSize != lastRev.roomSize || rp.damping != lastRev.damping
                || rp.wetLevel != lastRev.wetLevel || rp.dryLevel != lastRev.dryLevel)
            {
                reverb.setParameters (rp);
                lastRev = rp;
            }
            reverb.processStereo (L, R, n);
        }
    }

private:
    static float readFrac (const std::vector<float>& buf, int writePos, float delay, int size)
    {
        float rp = (float) writePos - delay;
        while (rp < 0.0f) rp += (float) size;
        const int i0 = (int) rp;
        const int i1 = (i0 + 1) % size;
        const float f = rp - (float) i0;
        return buf[(size_t) (i0 % size)] * (1.0f - f) + buf[(size_t) i1] * f;
    }

    float sr = 48000.0f;
    std::vector<float> choL, choR, dlyL, dlyR;
    int choW = 0, dlyW = 0, dlyTail = 0;
    float choPhase = 0.0f, dlyTimeSm = -1.0f, lpL = 0.0f, lpR = 0.0f;
    juce::Reverb reverb;
    juce::Reverb::Parameters lastRev;
};

} // namespace augur
