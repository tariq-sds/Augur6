#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../../Common/MixDSP.h"

// Toota Master: tone, glue, warmth, width (bass kept mono) and a true-peak limiter, with an automatic
// loudness target measured to ITU-R BS.1770.
namespace master
{
struct Settings
{
    float inputDb = 0, tilt = 0, low = 0, high = 0, glue = 0.3f, warmth = 0.2f, width = 0.5f;
    float ceilingDb = -1.0f, release = 0.5f;
    float targetLufs = -9.0f;    // 0 = off
    bool autoGain = true;
    float manualGainDb = 0;
};

class Engine
{
public:
    void prepare (double rate, int block)
    {
        sr = rate;
        os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        os->initProcessing ((size_t) block);
        det = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        det->initProcessing ((size_t) block);
        detDelay = (int) std::lround (det->getLatencyInSamples());
        for (auto& d : align) d.assign ((size_t) detDelay + 1, 0.0f);
        alignW = 0;
        detBuf.setSize (2, block * 2 + 16);
        peaks.assign ((size_t) block * 2 + 16, 0.0f);
        limiter.prepare (rate, 1.5);
        comp.prepare ((float) rate);
        comp.setTimes (30, 200);
        loud.prepare (rate);
        for (auto* b : { &tiltLo, &tiltHi, &lowSh, &highSh, &monoLp, &monoHp2, &scHp }) b->reset();
        autoDb = 0;
        last = {};
        last.tilt = 99;
    }
    int latency() const { return (int) std::lround (os ? os->getLatencyInSamples() : 0) + detDelay + limiter.latency(); }

    void process (juce::AudioBuffer<float>& b, const Settings& s)
    {
        const int n = b.getNumSamples();
        float* L = b.getWritePointer (0);
        float* R = b.getWritePointer (1);
        if (s.tilt != last.tilt || s.low != last.low || s.high != last.high)
        {
            tiltLo.design (mix::Biquad::LowShelf, 1000, 0.5, -s.tilt * 3.0, sr);
            tiltHi.design (mix::Biquad::HighShelf, 1000, 0.5, s.tilt * 3.0, sr);
            lowSh.design (mix::Biquad::LowShelf, 80, 0.7, s.low, sr);
            highSh.design (mix::Biquad::HighShelf, 12000, 0.7, s.high, sr);
            monoLp.design (mix::Biquad::HighPass, 120, 0.707, 0, sr); monoHp2.design (mix::Biquad::HighPass, 120, 0.707, 0, sr);
            scHp.design (mix::Biquad::HighPass, 90, 0.707, 0, sr);
            last = s;
        }
        const float thr = -8.0f - s.glue * 16.0f, ratio = 1.5f + s.glue * 1.0f;
        const float makeup = mix::dbToGain (s.glue * 3.0f);
        const float width = s.width * 2.0f;
        const float drive = s.warmth * 0.6f;
        const float gain = mix::dbToGain (s.inputDb + (s.autoGain && s.targetLufs < 0 ? autoDb : s.manualGainDb));
        float grMax = 0;
        for (int i = 0; i < n; ++i)
        {
            float l = L[i] * gain, r = R[i] * gain;
            l = tiltHi.process (tiltLo.process (l, 0), 0); r = tiltHi.process (tiltLo.process (r, 1), 1);
            l = highSh.process (lowSh.process (l, 0), 0); r = highSh.process (lowSh.process (r, 1), 1);
            if (s.glue > 0.01f)
            {
                // glue compressor keyed from a high-passed copy so the kick doesn't pump the whole mix
                const float kl = scHp.process (l, 0), kr = scHp.process (r, 1);
                const float g = comp.gain (kl, kr, thr, ratio, 8.0f) * makeup;
                l *= g; r *= g; grMax = std::max (grMax, comp.gr);
            }
            float m = 0.5f * (l + r), sd = 0.5f * (l - r) * width;
            sd = monoHp2.process (monoLp.process (sd, 0), 0);          // bass stays mono (side high-passed, LR4)
            L[i] = m + sd; R[i] = m - sd;
        }
        glueGr = std::max (grMax, glueGr * 0.9f);

        // warmth at 4x
        juce::dsp::AudioBlock<float> blk (b);
        if (drive > 0.001f)
        {
            auto up = os->processSamplesUp (blk);
            float* uL = up.getChannelPointer (0);
            float* uR = up.getChannelPointer (1);
            for (size_t i = 0; i < up.getNumSamples(); ++i) { uL[i] = mix::saturate (uL[i], drive); uR[i] = mix::saturate (uR[i], drive); }
            os->processSamplesDown (blk);
        }
        else
        {
            // keep the latency constant when warmth is off
            auto up = os->processSamplesUp (blk);
            os->processSamplesDown (blk);
        }

        // true-peak detection from a 4x copy; the audio is delayed to line up with it
        if (detBuf.getNumSamples() < n) { detBuf.setSize (2, n); peaks.resize ((size_t) n); }
        for (int c = 0; c < 2; ++c) detBuf.copyFrom (c, 0, b, c, 0, n);
        juce::dsp::AudioBlock<float> dblk (detBuf.getArrayOfWritePointers(), 2, (size_t) n);
        auto dup = det->processSamplesUp (dblk);
        for (int i = 0; i < n; ++i)
        {
            float pk = 0;
            for (int j = 0; j < 4; ++j) pk = std::max ({ pk, std::fabs (dup.getSample (0, i * 4 + j)), std::fabs (dup.getSample (1, i * 4 + j)) });
            peaks[(size_t) i] = pk;
            // align the audio with the (delayed) detection
            const int sz = detDelay + 1;
            align[0][(size_t) alignW] = L[i]; align[1][(size_t) alignW] = R[i];
            const int rd = (alignW + 1) % sz;
            L[i] = align[0][(size_t) rd]; R[i] = align[1][(size_t) rd];
            alignW = (alignW + 1) % sz;
        }
        limiter.process (L, R, n, mix::dbToGain (s.ceilingDb - 0.2f), 20.0f + s.release * 280.0f, peaks.data());

        // loudness of what leaves the plug-in, and the slow automatic gain towards the target
        if (resetLoud.exchange (false)) loud.reset();
        loud.process (L, R, n);
        float pk = 0; for (int i = 0; i < n; ++i) pk = std::max ({ pk, std::fabs (L[i]), std::fabs (R[i]) });
        truePeak = std::max (pk, truePeak * 0.9995f);
        if (s.autoGain && s.targetLufs < 0 && loud.shortTerm > -45.0)
        {
            const double err = s.targetLufs - loud.shortTerm;
            const double step = std::clamp (err, -1.0, 1.0) * 1.5 * n / sr;   // up to 1.5 dB per second
            autoDb = (float) std::clamp (autoDb + step, -12.0, 18.0);
        }
    }

    float autoDb = 0, glueGr = 0, truePeak = 0;
    std::atomic<bool> resetLoud { false };
    float limiterGr() const { return limiter.gr; }
    mix::Loudness loud;

private:
    double sr = 48000;
    std::unique_ptr<juce::dsp::Oversampling<float>> os, det;
    juce::AudioBuffer<float> detBuf;
    std::vector<float> peaks;
    std::array<std::vector<float>, 2> align;
    int detDelay = 0, alignW = 0;
    mix::Limiter limiter;
    mix::Compressor comp;
    mix::Biquad tiltLo, tiltHi, lowSh, highSh, monoLp, monoHp2, scHp;
    Settings last;
};
} // namespace master
