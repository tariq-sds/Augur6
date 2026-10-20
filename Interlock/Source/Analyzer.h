#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// Low-frequency spectrum analyser for the kick (sidechain) and bass (main input).
// Runs on a ~12 kHz decimated copy so a 4096-point FFT resolves ~3 Hz, which is what's needed
// to pin down a kick fundamental. Feeds both the display and the Learn routine.
namespace il
{
struct LpState { float ic1 = 0, ic2 = 0; };

inline float svfLow (LpState& s, float x, float g, float k)
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - s.ic2;
    const float v1 = a1 * s.ic1 + a2 * v3;
    const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
    s.ic1 = 2.0f * v1 - s.ic1;
    s.ic2 = 2.0f * v2 - s.ic2;
    return v2;
}

struct LearnResult
{
    float kickHz = 55, dipQ = 1.5f, depthDb = 6, duckDb = 2, boostHz = 110, boostDb = 2, releaseMs = 120;
    float bassRmsDb = -18, overlap = 0.5f;
};

class Analyzer
{
public:
    static constexpr int kOrder = 12;
    static constexpr int kSize = 1 << kOrder;
    static constexpr int kBins = kSize / 2;
    static constexpr int kHop = 1024;
    static constexpr int kLearnFrames = 60;   // ~5 s of material where both kick and bass are playing

    Analyzer() : fft (kOrder)
    {
        for (auto& v : kickDisplay) v.store (-120.0f);
        for (auto& v : bassDisplay) v.store (-120.0f);
    }

    void prepare (double sampleRate)
    {
        decim = juce::jmax (1, (int) std::round (sampleRate / 12000.0));
        fsD = (float) (sampleRate / decim);
        g = std::tan (juce::MathConstants<float>::pi * juce::jmin (4500.0f, fsD * 0.38f) / (float) sampleRate);
        ringK.assign ((size_t) kSize, 0.0f);
        ringB.assign ((size_t) kSize, 0.0f);
        work.assign ((size_t) kSize * 2, 0.0f);
        window.resize ((size_t) kSize);
        for (int i = 0; i < kSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (kSize - 1));
        lpK1 = lpK2 = lpB1 = lpB2 = {};
        w = hop = dcount = 0;
        sumK.assign ((size_t) kBins + 1, 0.0);
        sumB.assign ((size_t) kBins + 1, 0.0);
        dispK.assign ((size_t) kBins + 1, -120.0f);
        dispB.assign ((size_t) kBins + 1, -120.0f);
    }

    float binHz() const { return fsD / (float) kSize; }

    void startLearning()
    {
        learnRequest.store (true);
    }

    void push (const float* bass, const float* kick, int n)
    {
        constexpr float k = 1.41421356f;
        for (int i = 0; i < n; ++i)
        {
            const float kk = svfLow (lpK2, svfLow (lpK1, kick != nullptr ? kick[i] : 0.0f, g, k), g, k);
            const float bb = svfLow (lpB2, svfLow (lpB1, bass[i], g, k), g, k);
            if (++dcount >= decim)
            {
                dcount = 0;
                ringK[(size_t) w] = kk;
                ringB[(size_t) w] = bb;
                w = (w + 1) % kSize;
                if (++hop >= kHop)
                {
                    hop = 0;
                    frame();
                }
            }
        }
    }

    // Display data (read by the editor; occasional tearing is harmless)
    std::array<std::atomic<float>, kBins + 1> kickDisplay, bassDisplay;
    std::atomic<float> displayBinHz { 2.9f };

    // Learn state: 0 idle, 1 learning, 2 result ready, 3 applied
    std::atomic<int> learnState { 0 };
    std::atomic<float> learnProgress { 0.0f };
    std::atomic<float> hostBpm { 120.0f };
    LearnResult result;   // written by the audio thread before learnState = 2

private:
    void frame()
    {
        if (learnRequest.exchange (false))
        {
            std::fill (sumK.begin(), sumK.end(), 0.0);
            std::fill (sumB.begin(), sumB.end(), 0.0);
            bassMs = 0.0;
            frames = 0;
            learnProgress = 0.0f;
            learnState = 1;
        }

        const auto pw = [this] (const std::vector<float>& ring, std::vector<float>& out, double& meanSquare)
        {
            std::fill (work.begin(), work.end(), 0.0f);
            double ms = 0.0;
            for (int i = 0; i < kSize; ++i)
            {
                const float v = ring[(size_t) ((w + i) % kSize)];
                ms += (double) v * v;
                work[(size_t) i] = v * window[(size_t) i];
            }
            meanSquare = ms / kSize;
            fft.performFrequencyOnlyForwardTransform (work.data(), true);
            out.resize ((size_t) kBins + 1);
            const float norm = 4.0f / (float) kSize;
            for (int b = 0; b <= kBins; ++b)
            {
                const float m = work[(size_t) b] * norm;
                out[(size_t) b] = m * m;
            }
        };

        double msK = 0, msB = 0;
        pw (ringK, powK, msK);
        pw (ringB, powB, msB);

        displayBinHz = binHz();
        for (int b = 0; b <= kBins; ++b)
        {
            const float dk = 10.0f * std::log10 (powK[(size_t) b] + 1e-14f);
            const float db = 10.0f * std::log10 (powB[(size_t) b] + 1e-14f);
            float& vk = dispK[(size_t) b];
            float& vb = dispB[(size_t) b];
            vk = juce::jmax (dk, vk - 2.0f);
            vb = vb * 0.6f + db * 0.4f;
            kickDisplay[(size_t) b].store (vk, std::memory_order_relaxed);
            bassDisplay[(size_t) b].store (vb, std::memory_order_relaxed);
        }

        if (learnState.load() == 1 && msK > 1e-7 && msB > 1e-7)
        {
            for (int b = 0; b <= kBins; ++b)
            {
                sumK[(size_t) b] += powK[(size_t) b];
                sumB[(size_t) b] += powB[(size_t) b];
            }
            bassMs += msB;
            ++frames;
            learnProgress = (float) frames / (float) kLearnFrames;
            if (frames >= kLearnFrames)
            {
                computeResult();
                learnState = 2;
            }
        }
    }

    void computeResult()
    {
        const float hz = binHz();
        auto binOf = [hz] (float f) { return juce::jlimit (1, kBins - 1, (int) std::round (f / hz)); };

        // ---- Kick fundamental: strongest bin 35-130 Hz with parabolic interpolation
        int kb = binOf (35.0f);
        for (int b = binOf (35.0f); b <= binOf (130.0f); ++b)
            if (sumK[(size_t) b] > sumK[(size_t) kb]) kb = b;
        float kickHz = (float) kb * hz;
        {
            const double a = std::log (sumK[(size_t) kb - 1] + 1e-20), bb = std::log (sumK[(size_t) kb] + 1e-20),
                         c = std::log (sumK[(size_t) kb + 1] + 1e-20);
            const double den = a - 2.0 * bb + c;
            if (std::abs (den) > 1e-12)
                kickHz = (float) (((double) kb + juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / den)) * hz);
        }

        // -6 dB width of the kick peak -> dip Q
        const double half = sumK[(size_t) kb] * 0.25;
        int lo = kb, hi = kb;
        while (lo > 1 && sumK[(size_t) lo] > half) --lo;
        while (hi < kBins - 1 && sumK[(size_t) hi] > half) ++hi;
        const float bw = juce::jmax (hz, (float) (hi - lo) * hz);
        const float dipQ = juce::jlimit (0.8f, 2.5f, kickHz / bw);

        // ---- Normalised spectra over the low end
        double kMax = 1e-20, bMax = 1e-20;
        for (int b = binOf (25.0f); b <= binOf (500.0f); ++b)
        {
            kMax = juce::jmax (kMax, sumK[(size_t) b]);
            bMax = juce::jmax (bMax, sumB[(size_t) b]);
        }
        auto kN = [&] (int b) { return (float) (sumK[(size_t) b] / kMax); };
        auto bN = [&] (int b) { return (float) (sumB[(size_t) b] / bMax); };

        // Overlap: how much of the bass's low energy sits where the kick lives
        double inter = 0, bassSum = 1e-20;
        for (int b = binOf (30.0f); b <= binOf (160.0f); ++b)
        {
            inter += juce::jmin (kN (b), bN (b));
            bassSum += bN (b);
        }
        const float overlap = juce::jlimit (0.0f, 1.0f, (float) (inter / bassSum));

        // ---- Boost: where the bass has energy but the kick doesn't, away from the kick fundamental
        int best = binOf (110.0f);
        float bestScore = -1.0f;
        for (int b = binOf (55.0f); b <= binOf (400.0f); ++b)
        {
            const float f = (float) b * hz;
            if (std::abs (std::log2 (f / kickHz)) < 0.45f) continue;
            const float free = 1.0f - juce::jmin (1.0f, kN (b));
            const float score = bN (b) * free * free / (1.0f + f / 400.0f);
            if (score > bestScore) { bestScore = score; best = b; }
        }

        LearnResult r;
        r.kickHz = juce::jlimit (30.0f, 200.0f, kickHz);
        r.dipQ = dipQ;
        r.overlap = overlap;
        r.depthDb = juce::jlimit (4.0f, 14.0f, 4.0f + 10.0f * overlap);
        r.duckDb = juce::jlimit (1.0f, 6.0f, 1.0f + 5.0f * overlap);
        r.boostHz = juce::jlimit (50.0f, 600.0f, (float) best * hz);
        r.boostDb = juce::jlimit (1.5f, 4.5f, 1.5f + 3.0f * overlap);
        const float beatMs = 60000.0f / juce::jlimit (60.0f, 200.0f, hostBpm.load());
        r.releaseMs = juce::jlimit (60.0f, 250.0f, beatMs * 0.3f);
        r.bassRmsDb = (float) (10.0 * std::log10 (bassMs / juce::jmax (1, frames) + 1e-12));
        result = r;
    }

    juce::dsp::FFT fft;
    int decim = 4;
    float fsD = 12000.0f, g = 0.5f;
    LpState lpK1, lpK2, lpB1, lpB2;
    std::vector<float> ringK, ringB, work, window, powK, powB, dispK, dispB;
    std::vector<double> sumK, sumB;
    double bassMs = 0;
    int w = 0, hop = 0, dcount = 0, frames = 0;
    std::atomic<bool> learnRequest { false };
};
} // namespace il
