#pragma once
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <vector>
#include <cmath>

// Polyphonic pitch shifter for tonal stems (pads, keys, bass, leads).
// Phase vocoder (4096 / 4x overlap) with stereo phase locking so the image stays put.
namespace mr
{
class PhaseVocoder
{
public:
    static constexpr int kOrder = 12, kN = 1 << kOrder, kOsamp = 4, kHop = kN / kOsamp, kHalf = kN / 2;

    PhaseVocoder() : fft (kOrder) {}

    int latency() const { return kN; }

    void prepare()
    {
        for (int c = 0; c < 2; ++c)
        {
            inFifo[c].assign ((size_t) kN, 0.0f);
            outAcc[c].assign ((size_t) kN * 2, 0.0f);
            outFifo[c].assign ((size_t) kN, 0.0f);
        }
        lastPhase.assign ((size_t) kHalf + 1, 0.0f);
        sumPhase.assign ((size_t) kHalf + 1, 0.0f);
        win.resize ((size_t) kN);
        for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / kN);
        for (auto* v : { &bufL, &bufR, &bufM, &outL, &outR, &spec }) v->assign ((size_t) kN, {});
        anaMag.assign ((size_t) kHalf + 1, 0.0f); anaFreq.assign ((size_t) kHalf + 1, 0.0f);
        synMag[0].assign ((size_t) kHalf + 1, 0.0f); synMag[1].assign ((size_t) kHalf + 1, 0.0f);
        synFreq.assign ((size_t) kHalf + 1, 0.0f); synOff[0].assign ((size_t) kHalf + 1, 0.0f); synOff[1].assign ((size_t) kHalf + 1, 0.0f);
        synBest.assign ((size_t) kHalf + 1, 0.0f);
        pos = 0; filled = 0;
    }

    // ratio = 2^(semitones/12)
    void process (float& l, float& r, float ratio)
    {
        inFifo[0][(size_t) pos] = l;
        inFifo[1][(size_t) pos] = r;
        l = outFifo[0][(size_t) pos];
        r = outFifo[1][(size_t) pos];
        pos = (pos + 1) % kN;
        if (++filled >= kHop) { filled = 0; frame (ratio); }
    }

private:
    void frame (float ratio)
    {
        const float pi2 = 2.0f * juce::MathConstants<float>::pi;
        const float expct = pi2 * (float) kHop / (float) kN;

        // ---- analysis (oldest sample first)
        for (int i = 0; i < kN; ++i)
        {
            const int idx = (pos + i) % kN;
            const float w = win[(size_t) i];
            const float a = inFifo[0][(size_t) idx], b = inFifo[1][(size_t) idx];
            bufL[(size_t) i] = { a * w, 0 };
            bufR[(size_t) i] = { b * w, 0 };
            bufM[(size_t) i] = { 0.5f * (a + b) * w, 0 };
        }
        fft.perform (bufL.data(), spec.data(), false); std::copy (spec.begin(), spec.end(), bufL.begin());
        fft.perform (bufR.data(), spec.data(), false); std::copy (spec.begin(), spec.end(), bufR.begin());
        fft.perform (bufM.data(), spec.data(), false); std::copy (spec.begin(), spec.end(), bufM.begin());

        std::fill (synMag[0].begin(), synMag[0].end(), 0.0f);
        std::fill (synMag[1].begin(), synMag[1].end(), 0.0f);
        std::fill (synFreq.begin(), synFreq.end(), 0.0f);
        std::fill (synBest.begin(), synBest.end(), 0.0f);

        for (int k = 0; k <= kHalf; ++k)
        {
            const auto m = bufM[(size_t) k];
            const float ph = std::arg (m);
            float d = ph - lastPhase[(size_t) k];
            lastPhase[(size_t) k] = ph;
            d -= (float) k * expct;
            d -= pi2 * std::round (d / pi2);
            const float trueBin = (float) k + d * (float) kOsamp / pi2;

            const int j = (int) std::lround ((float) k * ratio);
            if (j < 0 || j > kHalf) continue;
            const float mL = std::abs (bufL[(size_t) k]), mR = std::abs (bufR[(size_t) k]);
            synMag[0][(size_t) j] += mL;
            synMag[1][(size_t) j] += mR;
            const float strength = std::abs (m);
            if (strength >= synBest[(size_t) j])
            {
                synBest[(size_t) j] = strength;
                synFreq[(size_t) j] = trueBin * ratio;
                synOff[0][(size_t) j] = std::arg (bufL[(size_t) k]) - ph;
                synOff[1][(size_t) j] = std::arg (bufR[(size_t) k]) - ph;
            }
        }

        // ---- synthesis
        for (int j = 0; j <= kHalf; ++j)
        {
            const float dev = synFreq[(size_t) j] - (float) j;
            sumPhase[(size_t) j] += (float) j * expct + dev * pi2 / (float) kOsamp;
            const float base = sumPhase[(size_t) j];
            outL[(size_t) j] = std::polar (synMag[0][(size_t) j], base + synOff[0][(size_t) j]);
            outR[(size_t) j] = std::polar (synMag[1][(size_t) j], base + synOff[1][(size_t) j]);
        }
        for (int j = 1; j < kHalf; ++j)
        {
            outL[(size_t) (kN - j)] = std::conj (outL[(size_t) j]);
            outR[(size_t) (kN - j)] = std::conj (outR[(size_t) j]);
        }
        fft.perform (outL.data(), spec.data(), true); std::copy (spec.begin(), spec.end(), outL.begin());
        fft.perform (outR.data(), spec.data(), true); std::copy (spec.begin(), spec.end(), outR.begin());

        // ---- overlap-add (Hann analysis + synthesis at 4x overlap sums to 1.5)
        const float g = 1.0f / 1.5f;
        for (int i = 0; i < kN; ++i)
        {
            outAcc[0][(size_t) i] += outL[(size_t) i].real() * win[(size_t) i] * g;
            outAcc[1][(size_t) i] += outR[(size_t) i].real() * win[(size_t) i] * g;
        }
        // the next kHop output samples are complete: hand them to the output FIFO
        for (int i = 0; i < kHop; ++i)
        {
            const int idx = (pos + i) % kN;
            outFifo[0][(size_t) idx] = outAcc[0][(size_t) i];
            outFifo[1][(size_t) idx] = outAcc[1][(size_t) i];
        }
        for (int c = 0; c < 2; ++c)
        {
            std::copy (outAcc[c].begin() + kHop, outAcc[c].begin() + kN + kHop, outAcc[c].begin());
            std::fill (outAcc[c].begin() + kN, outAcc[c].end(), 0.0f);
        }
    }

    juce::dsp::FFT fft;
    std::vector<float> inFifo[2], outAcc[2], outFifo[2], win, lastPhase, sumPhase;
    std::vector<std::complex<float>> bufL, bufR, bufM, outL, outR, spec;
    std::vector<float> anaMag, anaFreq, synMag[2], synFreq, synOff[2], synBest;
    int pos = 0, filled = 0;
};
} // namespace mr
