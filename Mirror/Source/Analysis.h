#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../../Clarion/Source/VocalDSP.h"   // Chroma + key finding
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace mr
{
constexpr int kBands = 24;

inline float bandEdge (int i) { return 25.0f * std::pow (18000.0f / 25.0f, (float) i / (float) kBands); }
inline float bandCentre (int i) { return std::sqrt (bandEdge (i) * bandEdge (i + 1)); }

// Weight of each band in the match: the extremes matter less, the body of the mix most.
inline float bandWeight (int i)
{
    const float f = bandCentre (i);
    if (f < 35) return 0.4f;
    if (f > 12000) return 0.5f;
    return 1.0f;
}

using Bands = std::array<float, kBands>;

inline float toDb (float p) { return 10.0f * std::log10 (p + 1e-14f); }

// Long-term band power analyser (8192-point FFT, hop 4096), stereo-averaged.
class BandAnalyzer
{
public:
    static constexpr int kOrder = 13, kN = 1 << kOrder, kHop = kN / 2;
    BandAnalyzer() : fft (kOrder) {}

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        ringL.assign ((size_t) kN, 0.0f);
        ringR.assign ((size_t) kN, 0.0f);
        work.assign ((size_t) kN * 2, 0.0f);
        win.resize ((size_t) kN);
        for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (kN - 1));
        binBand.assign ((size_t) kN / 2 + 1, -1);
        for (int b = 1; b <= kN / 2; ++b)
        {
            const float f = (float) b * sr / kN;
            for (int k = 0; k < kBands; ++k)
                if (f >= bandEdge (k) && f < bandEdge (k + 1)) { binBand[(size_t) b] = k; break; }
        }
        w = hop = 0;
        reset();
        display.fill (-120.0f);
    }

    void reset() { sum.fill (0.0); frames = 0; frameEnergies.clear(); framePowers.clear(); flatSum = 0; flatCount = 0; }

    // keepFrames: store every frame (offline reference analysis)
    void push (const float* l, const float* r, int n, bool accumulate, bool keepFrames = false)
    {
        for (int i = 0; i < n; ++i)
        {
            ringL[(size_t) w] = l[i];
            ringR[(size_t) w] = r != nullptr ? r[i] : l[i];
            w = (w + 1) & (kN - 1);
            if (++hop >= kHop) { hop = 0; frame (accumulate, keepFrames); }
        }
    }

    Bands mean() const
    {
        Bands m {};
        for (int k = 0; k < kBands; ++k) m[(size_t) k] = frames > 0 ? (float) (sum[(size_t) k] / frames) : 0.0f;
        return m;
    }

    // Mean over the loudest fraction of frames (the "drop")
    Bands loudMean (float fraction) const
    {
        Bands m {};
        if (framePowers.empty()) return m;
        // frames within 6 dB of the loud end (90th percentile) = the full-energy sections
        std::vector<float> e (frameEnergies);
        std::sort (e.begin(), e.end());
        const float p90 = e[(size_t) juce::jlimit (0, (int) e.size() - 1, (int) (0.9f * e.size()))];
        const float thr = juce::jmin (p90 * 0.25f, e[(size_t) juce::jlimit (0, (int) e.size() - 1, (int) ((1.0f - fraction) * e.size()))]);
        int count = 0;
        for (size_t f = 0; f < framePowers.size(); ++f)
            if (frameEnergies[f] >= thr)
            {
                for (int k = 0; k < kBands; ++k) m[(size_t) k] += framePowers[f][(size_t) k];
                ++count;
            }
        for (auto& v : m) v /= (float) juce::jmax (1, count);
        return m;
    }

    int frameCount() const { return frames; }
    float tonality() const { return flatCount > 0 ? (float) (flatSum / flatCount) : 0.0f; }   // 0 = noise, 1 = pure tones
    std::array<float, kBands> display {};     // smoothed dB, for the UI

private:
    void frame (bool accumulate, bool keepFrames)
    {
        Bands p {};
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& ring = ch == 0 ? ringL : ringR;
            for (int i = 0; i < kN; ++i) work[(size_t) i] = ring[(size_t) ((w + i) & (kN - 1))] * win[(size_t) i];
            std::fill (work.begin() + kN, work.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (work.data(), true);
            const float norm = 2.0f / (float) kN;
            for (int b = 1; b <= kN / 2; ++b)
            {
                const int k = binBand[(size_t) b];
                if (k < 0) continue;
                const float m = work[(size_t) b] * norm;
                p[(size_t) k] += 0.5f * m * m;
            }
        }
        if (accumulate)
        {
            // spectral flatness 80 Hz - 6 kHz on the last channel's spectrum (geometric / arithmetic mean)
            const int b0 = juce::jmax (2, (int) (80.0f * kN / sr)), b1 = juce::jmin (kN / 2 - 1, (int) (6000.0f * kN / sr));
            double lsum = 0, asum = 0; int cnt = 0;
            for (int b = b0; b <= b1; ++b) { const double m = (double) work[(size_t) b] * work[(size_t) b] + 1e-20; lsum += std::log (m); asum += m; ++cnt; }
            if (cnt > 0 && asum / cnt > 1e-12)
            {
                const double flat = std::exp (lsum / cnt) / (asum / cnt);
                const double wgt = std::sqrt (asum / cnt);     // loud moments count more than tails
                flatSum += wgt * (1.0 - std::pow (flat, 0.25));
                flatCount += wgt;
            }
        }
        for (int k = 0; k < kBands; ++k)
        {
            const float d = toDb (p[(size_t) k]);
            display[(size_t) k] = display[(size_t) k] < -100 ? d : display[(size_t) k] * 0.8f + d * 0.2f;
        }
        if (accumulate)
        {
            for (int k = 0; k < kBands; ++k) sum[(size_t) k] += p[(size_t) k];
            ++frames;
        }
        if (keepFrames)
        {
            float e = 0; for (float v : p) e += v;
            frameEnergies.push_back (e);
            framePowers.push_back (p);
        }
    }

    juce::dsp::FFT fft;
    float sr = 48000;
    std::vector<float> ringL, ringR, work, win;
    std::vector<int> binBand;
    int w = 0, hop = 0, frames = 0;
    double flatSum = 0, flatCount = 0;
    std::array<double, kBands> sum {};
    std::vector<float> frameEnergies;
    std::vector<Bands> framePowers;
};

// Key fit of a pitch-class profile against a key; best transposition into that key.
struct KeyFit { float fit = 0, bestFit = 0; int shift = 0; };

inline KeyFit fitToKey (const std::array<float, 12>& h, cl::KeyResult key)
{
    static const float maj[12] { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float mnr[12] { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    KeyFit r;
    if (key.tonic < 0) return r;
    const float* prof = key.minor ? mnr : maj;
    auto corr = [&] (int s)
    {
        float mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += h[(size_t) i]; my += prof[i]; }
        mx /= 12; my /= 12;
        float sxy = 0, sxx = 0, syy = 0;
        for (int pc = 0; pc < 12; ++pc)
        {
            // stem shifted by s semitones: pitch class pc moves to pc+s; compare with key profile at that degree
            const float x = h[(size_t) pc] - mx;
            const float y = prof[((pc + s - key.tonic) % 12 + 12) % 12] - my;
            sxy += x * y; sxx += x * x; syy += y * y;
        }
        return sxy / std::sqrt (sxx * syy + 1e-12f);
    };
    r.fit = corr (0);
    r.bestFit = r.fit;
    for (int s = -6; s <= 5; ++s)
    {
        const float c = corr (s);
        if (c > r.bestFit + 0.02f) { r.bestFit = c; r.shift = s; }
    }
    return r;
}

// ---------------------------------------------------------------- reference track
struct ReferenceInfo
{
    bool valid = false;
    juce::String name;
    Bands bands {};          // loud-section mean band power
    float loudness = -100;   // dB (K-ish, loud sections)
    float peakDb = -100;
    cl::KeyResult key;
    std::array<float, 12> chroma {};
    double lengthSec = 0;
};

inline ReferenceInfo analyseReference (const juce::File& file, std::atomic<float>* progress = nullptr)
{
    ReferenceInfo info;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr) return info;

    const double sr = reader->sampleRate;
    BandAnalyzer ba;
    ba.prepare (sr);
    cl::Chroma chroma;
    chroma.prepare ((float) sr);
    const int block = 16384;
    juce::AudioBuffer<float> buf ((int) juce::jmax (2u, reader->numChannels), block);
    float peak = 0;
    const juce::int64 total = reader->lengthInSamples;
    std::array<double, 12> chromaSum {};
    for (juce::int64 pos = 0; pos < total; pos += block)
    {
        const int n = (int) juce::jmin ((juce::int64) block, total - pos);
        buf.clear();
        reader->read (&buf, 0, n, pos, true, true);
        const float* l = buf.getReadPointer (0);
        const float* r = buf.getReadPointer (reader->numChannels > 1 ? 1 : 0);
        ba.push (l, r, n, true, true);
        for (int i = 0; i < n; ++i)
        {
            chroma.push (0.5f * (l[i] + r[i]), false);
            peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
        }
        if ((pos / block) % 8 == 0)
            for (int k = 0; k < 12; ++k) chromaSum[(size_t) k] += chroma.hist[(size_t) k];
        if (progress) progress->store ((float) pos / (float) juce::jmax ((juce::int64) 1, total));
    }
    info.bands = ba.loudMean (0.4f);
    float e = 0;
    for (int k = 0; k < kBands; ++k)
    {
        const float f = bandCentre (k);
        const float wgt = f < 60 ? 0.3f : (f > 1500 ? 1.6f : 1.0f);   // rough loudness weighting
        e += info.bands[(size_t) k] * wgt;
    }
    info.loudness = toDb (e);
    info.peakDb = juce::Decibels::gainToDecibels (peak, -100.0f);
    for (int k = 0; k < 12; ++k) info.chroma[(size_t) k] = (float) chromaSum[(size_t) k];
    info.key = cl::findKey (info.chroma);
    info.lengthSec = (double) total / sr;
    info.name = file.getFileNameWithoutExtension();
    info.valid = ba.frameCount() > 4;
    if (progress) progress->store (1.0f);
    return info;
}

// ---------------------------------------------------------------- the balance solver
// Finds per-stem gains (dB) so the summed band spectrum matches the reference's shape.
struct SolveInput
{
    std::vector<Bands> stems;      // mean band power of each stem (pre-gain)
    std::vector<bool> locked;
    std::vector<float> start;      // current gains dB
    Bands target {};               // reference band power
    float maxCut = 15.0f, maxBoost = 9.0f, regular = 0.0015f;
};

struct SolveResult { std::vector<float> gains; Bands before {}, after {}, target {}; float errBefore = 0, errAfter = 0; };

inline Bands mixOf (const std::vector<Bands>& stems, const std::vector<float>& g)
{
    Bands m {};
    for (size_t i = 0; i < stems.size(); ++i)
    {
        const float lin = std::pow (10.0f, g[i] / 10.0f);
        for (int k = 0; k < kBands; ++k) m[(size_t) k] += stems[i][(size_t) k] * lin;
    }
    return m;
}

// Error with the best overall level offset removed (only the shape matters)
inline float shapeError (const Bands& mix, const Bands& target, float* offsetOut = nullptr)
{
    float sw = 0, sd = 0;
    for (int k = 0; k < kBands; ++k) { const float w = bandWeight (k); sw += w; sd += w * (toDb (mix[(size_t) k]) - toDb (target[(size_t) k])); }
    const float c = sd / sw;
    float e = 0;
    for (int k = 0; k < kBands; ++k)
    {
        const float d = toDb (mix[(size_t) k]) - toDb (target[(size_t) k]) - c;
        e += bandWeight (k) * d * d;
    }
    if (offsetOut) *offsetOut = c;
    return e / sw;
}

inline SolveResult solveBalance (const SolveInput& in)
{
    const size_t n = in.stems.size();
    std::vector<float> g = in.start;
    g.resize (n, 0.0f);
    SolveResult r;
    r.target = in.target;
    r.before = mixOf (in.stems, g);
    r.errBefore = shapeError (r.before, in.target);

    auto objective = [&] (const std::vector<float>& x)
    {
        float reg = 0;
        for (size_t i = 0; i < n; ++i) reg += x[i] * x[i];
        return shapeError (mixOf (in.stems, x), in.target) + in.regular * reg;
    };

    float step = 1.0f;
    float f = objective (g);
    for (int it = 0; it < 3000 && step > 1e-4f; ++it)
    {
        // analytic gradient
        const Bands mix = mixOf (in.stems, g);
        float c = 0;
        shapeError (mix, in.target, &c);
        float sw = 0; for (int k = 0; k < kBands; ++k) sw += bandWeight (k);
        std::vector<float> grad (n, 0.0f);
        for (size_t i = 0; i < n; ++i)
        {
            if (in.locked.size() > i && in.locked[i]) continue;
            const float lin = std::pow (10.0f, g[i] / 10.0f);
            float s = 0;
            for (int k = 0; k < kBands; ++k)
            {
                const float frac = in.stems[i][(size_t) k] * lin / (mix[(size_t) k] + 1e-14f);
                const float d = toDb (mix[(size_t) k]) - toDb (in.target[(size_t) k]) - c;
                s += bandWeight (k) * d * frac;
            }
            grad[i] = 2.0f * s / sw + 2.0f * in.regular * g[i];
        }
        float gn = 0; for (float v : grad) gn += v * v;
        if (gn < 1e-10f) break;
        gn = std::sqrt (gn);

        std::vector<float> trial (g);
        bool improved = false;
        while (step > 1e-4f)
        {
            for (size_t i = 0; i < n; ++i)
                trial[i] = juce::jlimit (-in.maxCut, in.maxBoost, g[i] - step * grad[i] / gn);
            const float ft = objective (trial);
            if (ft < f) { g = trial; f = ft; improved = true; step *= 1.5f; break; }
            step *= 0.5f;
        }
        if (! improved) break;
    }

    // centre the result so the loudest stem's gain change is ~0 (keeps the mix level sensible)
    float mean = 0; int cnt = 0;
    for (size_t i = 0; i < n; ++i) if (! (in.locked.size() > i && in.locked[i])) { mean += g[i]; ++cnt; }
    if (cnt > 0)
    {
        mean /= (float) cnt;
        for (size_t i = 0; i < n; ++i) if (! (in.locked.size() > i && in.locked[i])) g[i] = juce::jlimit (-in.maxCut, in.maxBoost, g[i] - mean);
    }
    r.gains = g;
    r.after = mixOf (in.stems, g);
    r.errAfter = shapeError (r.after, in.target);
    return r;
}
} // namespace mr
