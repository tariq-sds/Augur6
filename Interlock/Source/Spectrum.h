#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <mutex>
#include <map>
#include "EqEngine.h"

// Full-range analyser for the EQ display (pre, post and sidechain), EQ Match capture, and a registry so
// every Interlock in the session can show the others' spectra and where they collide.
namespace ileq
{
constexpr int kPoints = 400;                     // log-spaced display points
constexpr double kLoHz = 10.0, kHiHz = 22000.0;
inline double pointHz (int i) { return kLoHz * std::pow (kHiHz / kLoHz, (double) i / (kPoints - 1)); }
inline double pointOf (double hz) { return (kPoints - 1) * std::log (hz / kLoHz) / std::log (kHiHz / kLoHz); }

using Curve = std::array<float, kPoints>;

// lock-free sample tap: the audio thread writes, the analyser (message thread) reads the latest window
struct Tap
{
    static constexpr int kSize = 1 << 15;
    std::vector<float> buf = std::vector<float> ((size_t) kSize, 0.0f);
    std::atomic<int> w { 0 };
    void push (const float* x, int n, float gain = 1.0f)
    {
        int p = w.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { buf[(size_t) p] = x != nullptr ? x[i] * gain : 0.0f; p = (p + 1) & (kSize - 1); }
        w.store (p, std::memory_order_release);
    }
};

class Analyser
{
public:
    static constexpr int kOrder = 13, kN = 1 << kOrder;
    enum { pre = 0, post = 1, side = 2 };

    Analyser() : fft (kOrder)
    {
        win.resize ((size_t) kN);
        for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * i / (float) (kN - 1));
        work.assign ((size_t) kN * 2, 0.0f);
        for (auto& c : curves) c.fill (-140.0f);
        for (auto& c : capture) c.fill (0.0);
    }

    Tap taps[3];
    std::array<Curve, 3> curves;                 // smoothed display spectra (dB, 4.5 dB/oct tilt)
    double fs = 48000;

    // EQ Match capture: long-term average of pre and sidechain
    bool capturing = false;
    int captureFrames = 0;
    std::array<std::array<double, kPoints>, 2> capture;

    void startCapture() { for (auto& c : capture) c.fill (0.0); captureFrames = 0; capturing = true; }

    // message thread, ~30 times a second
    void analyse()
    {
        for (int t = 0; t < 3; ++t)
        {
            auto& tap = taps[t];
            const int w = tap.w.load (std::memory_order_acquire);
            for (int i = 0; i < kN; ++i) work[(size_t) i] = tap.buf[(size_t) ((w - kN + i + Tap::kSize) & (Tap::kSize - 1))] * win[(size_t) i];
            std::fill (work.begin() + kN, work.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (work.data(), true);
            const double binHz = fs / kN;
            Curve raw;
            for (int p = 0; p < kPoints; ++p)
            {
                const double f0 = pointHz (std::max (0, p - 1)) * 0.5 + pointHz (p) * 0.5, f1 = pointHz (std::min (kPoints - 1, p + 1)) * 0.5 + pointHz (p) * 0.5;
                int b0 = (int) std::floor (f0 / binHz), b1 = (int) std::ceil (f1 / binHz);
                b0 = std::clamp (b0, 1, kN / 2 - 1); b1 = std::clamp (b1, b0, kN / 2 - 1);
                float m = 0;
                if (b1 - b0 <= 1)
                {
                    // fewer bins than points down low: interpolate
                    const double bf = pointHz (p) / binHz;
                    const int i0 = std::clamp ((int) bf, 1, kN / 2 - 2);
                    const float fr = (float) (bf - i0);
                    m = work[(size_t) i0] * (1 - fr) + work[(size_t) i0 + 1] * fr;
                }
                else for (int b = b0; b <= b1; ++b) m = std::max (m, work[(size_t) b]);
                const float db = 20.0f * std::log10 (std::max (1e-9f, m * 2.0f / (kN * 0.5f)));
                raw[(size_t) p] = db + 4.5f * (float) std::log2 (pointHz (p) / 1000.0);   // like Pro-Q: tilt so pink noise reads flat
                if (capturing && t != post) capture[(size_t) (t == pre ? 0 : 1)][(size_t) p] += (double) m * m;
            }
            auto& c = curves[(size_t) t];
            for (int p = 0; p < kPoints; ++p) c[(size_t) p] = raw[(size_t) p] > c[(size_t) p] ? raw[(size_t) p] : c[(size_t) p] + (raw[(size_t) p] - c[(size_t) p]) * 0.25f;
        }
        if (capturing) ++captureFrames;
    }

    // smoothed capture difference (reference minus input), dB, 1/3 octave
    std::array<double, kPoints> matchDiff() const
    {
        std::array<double, kPoints> d {};
        std::array<double, kPoints> a {}, b {};
        for (int p = 0; p < kPoints; ++p)
        {
            a[(size_t) p] = 10.0 * std::log10 (capture[0][(size_t) p] + 1e-20);
            b[(size_t) p] = 10.0 * std::log10 (capture[1][(size_t) p] + 1e-20);
        }
        const double perOct = (kPoints - 1) / std::log2 (kHiHz / kLoHz);
        const int half = std::max (1, (int) (perOct / 6.0));
        for (int p = 0; p < kPoints; ++p)
        {
            double s = 0; int n = 0;
            for (int k = p - half; k <= p + half; ++k) if (k >= 0 && k < kPoints) { s += b[(size_t) k] - a[(size_t) k]; ++n; }
            d[(size_t) p] = n ? s / n : 0;
        }
        // remove the overall level difference (match tone, not volume): median over 100 Hz - 8 kHz
        std::vector<double> mid;
        for (int p = 0; p < kPoints; ++p) { const double hz = pointHz (p); if (hz > 100 && hz < 8000) mid.push_back (d[(size_t) p]); }
        double m = 0;
        if (! mid.empty()) { std::nth_element (mid.begin(), mid.begin() + (long) mid.size() / 2, mid.end()); m = mid[mid.size() / 2]; }
        for (auto& v : d) v -= m;
        return d;
    }

private:
    juce::dsp::FFT fft;
    std::vector<float> win, work;
};

// ---------------------------------------------------------------- instances in this session
struct Registry
{
    struct Entry { juce::String name; Curve post; double stamp = 0; };
    static Registry& get() { static Registry r; return r; }
    std::mutex m;
    std::map<int, Entry> entries;
    int nextId = 1;

    int add() { std::lock_guard<std::mutex> l (m); const int id = nextId++; entries[id] = {}; return id; }
    void remove (int id) { std::lock_guard<std::mutex> l (m); entries.erase (id); }
    void publish (int id, const juce::String& name, const Curve& c)
    {
        std::lock_guard<std::mutex> l (m);
        auto& e = entries[id];
        e.name = name; e.post = c; e.stamp = juce::Time::getMillisecondCounterHiRes();
    }
    std::vector<std::pair<int, juce::String>> others (int self)
    {
        std::lock_guard<std::mutex> l (m);
        std::vector<std::pair<int, juce::String>> v;
        for (auto& [id, e] : entries) if (id != self) v.push_back ({ id, e.name.isNotEmpty() ? e.name : "Interlock " + juce::String (id) });
        return v;
    }
    bool curveOf (int id, Curve& out)
    {
        std::lock_guard<std::mutex> l (m);
        auto it = entries.find (id);
        if (it == entries.end()) return false;
        out = it->second.post;
        return true;
    }
};
} // namespace ileq
