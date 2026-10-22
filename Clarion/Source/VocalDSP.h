#pragma once
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// Building blocks for Clarion: pitch tracking (YIN), key finding, pitch-synchronous
// overlap-add pitch shifting (formant-preserving), chroma analysis and filters.
namespace cl
{
constexpr float kPi = 3.14159265358979f;

inline const char* noteName (int pc)
{
    static const char* n[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return n[((pc % 12) + 12) % 12];
}

// ---------------------------------------------------------------- filters
struct Svf
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    inline void tick (float x, float g, float k, float& bp, float& lp)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        bp = a1 * ic1 + a2 * v3;
        lp = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * bp - ic1;
        ic2 = 2.0f * lp - ic2;
    }
    float bell (float x, float g, float q, float db)
    {
        const float A = std::exp (db * 0.0575646273f);
        const float k = 1.0f / (q * A);
        float bp, lp; tick (x, g, k, bp, lp);
        return x + k * (A * A - 1.0f) * bp;
    }
    float highShelf (float x, float gBase, float q, float db)
    {
        const float A = std::exp (db * 0.0575646273f);
        const float k = 1.0f / q;
        float bp, lp; tick (x, gBase * std::sqrt (A), k, bp, lp);
        const float hp = x - k * bp - lp;
        return A * A * hp + A * k * bp + lp;
    }
    float highPass (float x, float g, float q)
    {
        const float k = 1.0f / q;
        float bp, lp; tick (x, g, k, bp, lp);
        return x - k * bp - lp;
    }
    float bandPass (float x, float g, float q)
    {
        const float k = 1.0f / q;
        float bp, lp; tick (x, g, k, bp, lp);
        return bp * k;
    }
};

inline float gOf (float f, float sr) { return std::tan (kPi * juce::jlimit (5.0f, sr * 0.45f, f) / sr); }

// ---------------------------------------------------------------- key finding
struct KeyResult { int tonic = -1; bool minor = false; float confidence = 0; };

inline KeyResult findKey (const std::array<float, 12>& h)
{
    static const float maj[12] { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float mnr[12] { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };
    float sum = 0; for (float v : h) sum += v;
    KeyResult r;
    if (sum <= 1e-9f) return r;

    auto corr = [&] (const float* prof, int tonic)
    {
        float mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += h[(size_t) ((i + tonic) % 12)]; my += prof[i]; }
        mx /= 12; my /= 12;
        float sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const float dx = h[(size_t) ((i + tonic) % 12)] - mx, dy = prof[i] - my;
            sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
        }
        return sxy / std::sqrt (sxx * syy + 1e-12f);
    };

    float best = -2, second = -2;
    for (int t = 0; t < 12; ++t)
        for (int m = 0; m < 2; ++m)
        {
            const float c = corr (m ? mnr : maj, t);
            if (c > best) { second = best; best = c; r.tonic = t; r.minor = m == 1; }
            else if (c > second) second = c;
        }
    r.confidence = juce::jlimit (0.0f, 1.0f, (best - second) * 6.0f + juce::jmax (0.0f, best) * 0.5f);
    return r;
}

// Semitone shift (in -6..+5) that moves the vocal's key onto the reference key.
// Relative major/minor pairs are treated as the same key.
inline int keyShift (KeyResult vocal, KeyResult ref)
{
    if (vocal.tonic < 0 || ref.tonic < 0) return 0;
    const int v = vocal.minor ? (vocal.tonic + 3) % 12 : vocal.tonic;
    const int r = ref.minor ? (ref.tonic + 3) % 12 : ref.tonic;
    int s = ((r - v) % 12 + 12) % 12;
    if (s > 5) s -= 12;
    return s;
}

inline int rootShift (int vocalTonic, int refRoot)
{
    if (vocalTonic < 0 || refRoot < 0) return 0;
    int s = ((refRoot - vocalTonic) % 12 + 12) % 12;
    if (s > 5) s -= 12;
    return s;
}

// ---------------------------------------------------------------- YIN pitch tracker (runs on a 4x decimated copy)
class PitchTracker
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        decim = juce::jmax (1, (int) std::round (sr / 12000.0f));
        fsd = sr / (float) decim;
        g = gOf (juce::jmin (2500.0f, fsd * 0.4f), sr);
        win = (int) (fsd * 0.034f);
        maxLag = (int) (fsd / 65.0f);
        minLag = juce::jmax (2, (int) (fsd / 1100.0f));
        ring.assign ((size_t) kRing, 0.0f);
        full.assign ((size_t) kFull, 0.0f);
        fw = 0;
        d.assign ((size_t) maxLag + 2, 0.0f);
        lp1.reset(); lp2.reset();
        w = cnt = hop = 0;
        hz = 0; voiced = false; aperiodicity = 1; rms = 0;
    }

    // returns true when a new estimate is available
    bool push (float x)
    {
        full[(size_t) fw] = x;
        fw = (fw + 1) & (kFull - 1);
        float bp, l1, l2;
        lp1.tick (x, g, 1.41421356f, bp, l1);
        lp2.tick (l1, g, 1.41421356f, bp, l2);
        if (++cnt < decim) return false;
        cnt = 0;
        ring[(size_t) w] = l2;
        w = (w + 1) & (kRing - 1);
        if (++hop < 64) return false;
        hop = 0;
        analyse();
        return true;
    }

    float hz = 0, aperiodicity = 1, rms = 0;
    bool voiced = false;
    float periodSamples() const { return hz > 0 ? sr / hz : 0.0f; }

private:
    static constexpr int kRing = 2048;
    static constexpr int kFull = 8192;
    std::vector<float> full;
    int fw = 0;

    void analyse()
    {
        const int N = win + maxLag;
        auto at = [this, N] (int i) { return ring[(size_t) ((w - N + i + kRing) & (kRing - 1))]; };
        double e = 0;
        for (int i = 0; i < win; ++i) e += (double) at (i) * at (i);
        rms = (float) std::sqrt (e / win);

        d[0] = 1.0f;
        float running = 0;
        for (int tau = 1; tau <= maxLag; ++tau)
        {
            float s = 0;
            for (int j = 0; j < win; ++j) { const float df = at (j) - at (j + tau); s += df * df; }
            running += s;
            d[(size_t) tau] = running > 0 ? s * (float) tau / running : 1.0f;
        }

        int tau = -1;
        for (int t = minLag; t <= maxLag; ++t)
            if (d[(size_t) t] < 0.13f)
            {
                while (t + 1 <= maxLag && d[(size_t) t + 1] < d[(size_t) t]) ++t;
                tau = t;
                break;
            }
        if (tau < 0)
        {
            int best = minLag;
            for (int t = minLag; t <= maxLag; ++t) if (d[(size_t) t] < d[(size_t) best]) best = t;
            if (d[(size_t) best] < 0.3f) tau = best;
        }

        if (tau < 0 || rms < 0.0025f)
        {
            voiced = false;
            aperiodicity = 1.0f;
            return;
        }

        float t = (float) tau;
        if (tau > 1 && tau < maxLag)
        {
            const float a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1];
            const float den = a - 2 * b + c;
            if (std::abs (den) > 1e-9f) t += juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den);
        }
        aperiodicity = d[(size_t) tau];
        // refine at full rate: search +-1.5 decimated samples around the coarse lag
        {
            const float coarse = t * (float) decim;
            const int lo = juce::jmax (8, (int) std::floor (coarse - 1.5f * decim)), hi = (int) std::ceil (coarse + 1.5f * decim);
            const int len = juce::jmin (kFull - hi - 2, juce::jmax (512, 2 * hi));
            auto fa = [this] (int back) { return full[(size_t) ((fw - 1 - back + kFull * 4) & (kFull - 1))]; };
            float best = 1e30f; int bi = (int) std::round (coarse);
            std::array<float, 64> dv {};
            for (int L = lo; L <= hi && L - lo < 64; ++L)
            {
                float sdiff = 0;
                for (int j = 0; j < len; ++j) { const float df = fa (j) - fa (j + L); sdiff += df * df; }
                dv[(size_t) (L - lo)] = sdiff;
                if (sdiff < best) { best = sdiff; bi = L; }
            }
            float fine = (float) bi;
            if (bi > lo && bi < hi && bi - lo < 63)
            {
                const float a = dv[(size_t) (bi - lo - 1)], b = dv[(size_t) (bi - lo)], c = dv[(size_t) (bi - lo + 1)];
                const float den = a - 2 * b + c;
                if (std::abs (den) > 1e-12f) fine += juce::jlimit (-0.5f, 0.5f, 0.5f * (a - c) / den);
            }
            hz = sr / fine;
        }
        voiced = hz > 60.0f && hz < 1100.0f;
    }

    float sr = 48000, fsd = 12000, g = 0.5f;
    int decim = 4, win = 400, maxLag = 184, minLag = 11;
    std::vector<float> ring, d;
    Svf lp1, lp2;
    int w = 0, cnt = 0, hop = 0;
};

// ---------------------------------------------------------------- PSOLA pitch shifter (fixed latency)
class PitchShifter
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        latency = (int) std::ceil (0.04f * sr);
        unvoicedT = (int) std::round (0.008f * sr);
        for (auto& b : in) b.assign ((size_t) kSize, 0.0f);
        for (auto& b : out) b.assign ((size_t) kSize, 0.0f);
        epochBuf.assign ((size_t) kSize, 0.0f);
        n = 0;
        nextMark = 0.0;
        lastEpoch = -1000000;
        epochLp = 0;
    }

    int getLatency() const { return latency; }

    // ratio: output pitch / input pitch. periodHint: detected period in samples (0 = unvoiced)
    void process (float& l, float& r, float ratio, float periodHint)
    {
        const int64_t idx = n & (kSize - 1);
        in[0][(size_t) idx] = l;
        in[1][(size_t) idx] = r;
        epochLp += (0.5f * (l + r) - epochLp) * 0.12f;
        epochBuf[(size_t) idx] = epochLp;

        const int64_t outIdx = n - latency;
        const bool shift = std::abs (ratio - 1.0f) > 1e-4f;
        const bool voiced = shift && periodHint > 20.0f && periodHint < latency * 0.42f;
        const float T = voiced ? periodHint : (float) unvoicedT;

        while (nextMark - T <= (double) outIdx + 1)
        {
            const int64_t mark = (int64_t) std::llround (nextMark);
            if (voiced)
            {
                const int Ti = (int) std::round (T);
                int64_t e;
                if (lastEpoch < mark - 3 * Ti || lastEpoch > mark + Ti)
                    e = findEpoch (mark, Ti);
                else
                {
                    e = lastEpoch;
                    while (e + Ti / 2 < mark) e = findEpoch (e + Ti, Ti);
                }
                lastEpoch = e;
                const float gain = 1.0f / std::sqrt (ratio);
                addGrain (mark, e, Ti, gain);
                nextMark += (double) T / ratio;
            }
            else
            {
                addGrain (mark, mark, unvoicedT, 1.0f);
                nextMark = (double) mark + unvoicedT;
                lastEpoch = -1000000;
            }
        }

        if (outIdx >= 0)
        {
            const size_t o = (size_t) (outIdx & (kSize - 1));
            l = out[0][o]; r = out[1][o];
            out[0][o] = out[1][o] = 0.0f;
        }
        else
            l = r = 0.0f;
        ++n;
    }

private:
    static constexpr int kSize = 1 << 15;

    int64_t findEpoch (int64_t around, int T) const
    {
        const int span = juce::jmax (2, T / 4);
        int64_t best = around;
        float bv = -1e9f;
        const int64_t hi = juce::jmin (around + span, n - T - 1);
        for (int64_t i = around - span; i <= hi; ++i)
        {
            const float v = epochBuf[(size_t) (i & (kSize - 1))];
            if (v > bv) { bv = v; best = i; }
        }
        return juce::jmin (best, n - T - 1);
    }

    void addGrain (int64_t at, int64_t from, int T, float gain)
    {
        const float inv = kPi / (float) T;
        for (int k = -T + 1; k < T; ++k)
        {
            const float wv = (0.5f + 0.5f * std::cos ((float) k * inv)) * gain;
            const size_t si = (size_t) ((from + k) & (kSize - 1));
            const size_t di = (size_t) ((at + k) & (kSize - 1));
            out[0][di] += in[0][si] * wv;
            out[1][di] += in[1][si] * wv;
        }
    }

    float sr = 48000;
    int latency = 1920, unvoicedT = 384;
    std::array<std::vector<float>, 2> in, out;
    std::vector<float> epochBuf;
    int64_t n = 0, lastEpoch = -1000000;
    double nextMark = 0;
    float epochLp = 0;
};

// ---------------------------------------------------------------- polyphonic chroma (reference element)
class Chroma
{
public:
    static constexpr int kOrder = 12, kN = 1 << kOrder;
    Chroma() : fft (kOrder) {}

    void prepare (float sampleRate)
    {
        decim = juce::jmax (1, (int) std::round (sampleRate / 12000.0f));
        fsd = sampleRate / (float) decim;
        g = gOf (juce::jmin (2500.0f, fsd * 0.4f), sampleRate);
        ring.assign ((size_t) kN, 0.0f);
        work.assign ((size_t) kN * 2, 0.0f);
        win.resize ((size_t) kN);
        for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2 * kPi * i / (kN - 1));
        w = cnt = hop = 0;
        lp1.reset(); lp2.reset();
        frames = 0;
        decay = std::exp (-(1024.0f / fsd) / 25.0f);   // ~25 s memory
    }

    void push (float x, bool freeze)
    {
        float bp, l1, l2;
        lp1.tick (x, g, 1.41421356f, bp, l1);
        lp2.tick (l1, g, 1.41421356f, bp, l2);
        if (++cnt < decim) return;
        cnt = 0;
        ring[(size_t) w] = l2;
        w = (w + 1) & (kN - 1);
        if (++hop < 1024) return;
        hop = 0;
        if (! freeze) frame();
    }

    std::array<float, 12> hist {};    // read by the UI and key finder
    std::array<float, 12> bassHist {};
    int frames = 0;
    double peakinessSum = 0, peakinessWeight = 0;
    float peakiness() const { return peakinessWeight > 0 ? (float) (peakinessSum / peakinessWeight) : 0.0f; }
    void resetPeakiness() { peakinessSum = peakinessWeight = 0; }

private:
    void frame()
    {
        double e = 0;
        for (int i = 0; i < kN; ++i)
        {
            const float v = ring[(size_t) ((w + i) & (kN - 1))];
            e += (double) v * v;
            work[(size_t) i] = v * win[(size_t) i];
        }
        if (e / kN < 1e-7) return;   // silent
        std::fill (work.begin() + kN, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data(), true);

        std::array<float, 12> c {}, cb {};
        const float hz = fsd / (float) kN;
        const int b0 = (int) (40.0f / hz), b1 = (int) (2000.0f / hz);
        const float norm = 4.0f / (float) kN;
        for (int b = juce::jmax (2, b0); b <= b1; ++b)
        {
            const float m = work[(size_t) b];
            if (m < work[(size_t) b - 1] || m < work[(size_t) b + 1]) continue;    // peaks only
            // parabolic peak interpolation for an accurate frequency
            const float a = work[(size_t) b - 1], cc = work[(size_t) b + 1];
            const float den = a - 2 * m + cc;
            const float off = std::abs (den) > 1e-9f ? juce::jlimit (-0.5f, 0.5f, 0.5f * (a - cc) / den) : 0.0f;
            const float f = ((float) b + off) * hz;
            const float midi = 69.0f + 12.0f * std::log2 (f / 440.0f);
            const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
            const float wgt = std::sqrt (m * norm);                 // gentle compression
            if (f >= 60.0f) c[(size_t) pc] += wgt;                  // key: skip sub-bass (kick fundamentals)
            if (f >= 38.0f && f < 300.0f) cb[(size_t) pc] += wgt;   // root: bass register
        }
        {
            // per-frame pitch concentration (how 'note-like' this moment is), energy weighted
            float cm = 0, cs = 0;
            for (float v : c) { cm = juce::jmax (cm, v); cs += v; }
            if (cs > 0)
            {
                const float wgt = (float) std::sqrt (e / kN);
                peakinessSum += wgt * (cm / (cs / 12.0f));
                peakinessWeight += wgt;
            }
        }
        for (int i = 0; i < 12; ++i)
        {
            hist[(size_t) i] = hist[(size_t) i] * decay + c[(size_t) i];
            bassHist[(size_t) i] = bassHist[(size_t) i] * decay + cb[(size_t) i];
        }
        ++frames;
    }

    juce::dsp::FFT fft;
    int decim = 4, w = 0, cnt = 0, hop = 0;
    float fsd = 12000, g = 0.5f, decay = 0.99f;
    std::vector<float> ring, work, win;
    Svf lp1, lp2;
};
} // namespace cl
