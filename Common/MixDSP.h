#pragma once
// Mixing / mastering building blocks: RBJ biquads, a feed-forward compressor, saturation, a transient
// shaper, an ITU-R BS.1770 loudness meter and a look-ahead peak limiter.
#include <cmath>
#include <vector>
#include <algorithm>
#include <array>

namespace mix
{
constexpr double kPi = 3.14159265358979323846;
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) { return 20.0f * std::log10 (std::max (1e-9f, g)); }
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] {}, z2[2] {};
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    float process (float x, int ch)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    { b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0; }

    enum Type { Bell, LowShelf, HighShelf, HighPass, LowPass };
    void design (Type t, double hz, double q, double db, double sr)
    {
        hz = std::min (hz, sr * 0.45);
        const double A = std::pow (10.0, db / 40.0), w = 2 * kPi * hz / sr, cw = std::cos (w), sw = std::sin (w), al = sw / (2 * q);
        switch (t)
        {
            case Bell:      set (1 + al * A, -2 * cw, 1 - al * A, 1 + al / A, -2 * cw, 1 - al / A); break;
            case LowShelf:  { const double s = 2 * std::sqrt (A) * al;
                              set (A * ((A + 1) - (A - 1) * cw + s), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - s),
                                   (A + 1) + (A - 1) * cw + s, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - s); break; }
            case HighShelf: { const double s = 2 * std::sqrt (A) * al;
                              set (A * ((A + 1) + (A - 1) * cw + s), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - s),
                                   (A + 1) - (A - 1) * cw + s, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - s); break; }
            case HighPass:  set ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + al, -2 * cw, 1 - al); break;
            case LowPass:   set ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + al, -2 * cw, 1 - al); break;
        }
    }
    // magnitude in dB at hz (for drawing)
    double magDb (double hz, double sr) const
    {
        const double w = 2 * kPi * hz / sr;
        const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        return 10 * std::log10 ((nr * nr + ni * ni) / std::max (1e-30, dr * dr + di * di));
    }
};

// stereo-linked feed-forward compressor with soft knee, RMS-ish detector and auto makeup
struct Compressor
{
    float env = 0, gr = 0;   // gr in dB (positive = reduction)
    float att = 0, rel = 0, sr = 48000;
    void prepare (float rate) { sr = rate; env = 0; gr = 0; }
    void setTimes (float attMs, float relMs) { att = std::exp (-1.0f / (std::max (0.05f, attMs) * 0.001f * sr)); rel = std::exp (-1.0f / (std::max (1.0f, relMs) * 0.001f * sr)); }
    // returns linear gain to apply
    float gain (float l, float r, float thrDb, float ratio, float kneeDb = 6.0f)
    {
        const float x = std::max (l * l, r * r);
        env = x > env ? x + (env - x) * att : x + (env - x) * rel;
        const float lv = 10.0f * std::log10 (env + 1e-12f);
        const float over = lv - thrDb;
        float red = 0;
        if (over > kneeDb * 0.5f) red = over * (1 - 1 / ratio);
        else if (over > -kneeDb * 0.5f) { const float k = over + kneeDb * 0.5f; red = (1 - 1 / ratio) * k * k / (2 * kneeDb); }
        gr = red;
        return dbToGain (-red);
    }
};

inline float saturate (float x, float drive)   // drive 0..1: gentle tape-ish curve with a touch of even harmonics
{
    if (drive <= 0.001f) return x;
    const float k = 1.0f + drive * 6.0f;
    const float y = std::tanh (k * x + 0.08f * drive * x * x) / std::tanh (k);
    return x * (1.0f - drive) + y * drive * (1.0f / (1.0f + drive * 0.35f));
}

// transient shaper: difference between a fast and a slow envelope boosts / softens attacks
struct Transient
{
    float fast = 0, slow = 0, a1 = 0, a2 = 0, r = 0;
    void prepare (float sr) { a1 = std::exp (-1.0f / (0.0007f * sr)); a2 = std::exp (-1.0f / (0.020f * sr)); r = std::exp (-1.0f / (0.12f * sr)); fast = slow = 0; }
    float gain (float l, float rr, float amountDb)   // amountDb: + punchier, - softer
    {
        const float x = std::max (std::fabs (l), std::fabs (rr));
        fast = x > fast ? x + (fast - x) * a1 : x + (fast - x) * r;
        slow = x > slow ? x + (slow - x) * a2 : x + (slow - x) * r;
        const float t = clampf ((fast - slow) / (slow + 1e-5f), 0.0f, 3.0f) / 3.0f;
        return dbToGain (amountDb * t);
    }
};

// ITU-R BS.1770-4 loudness: K-weighting, 400 ms momentary, 3 s short-term, gated integrated
class Loudness
{
public:
    void prepare (double sr)
    {
        rate = sr;
        // pre-filter (high shelf) and RLB high-pass from the analogue prototypes, valid at any rate
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (kPi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            shelf.set ((Vh + Vb * K / Q + K * K), 2.0 * (K * K - Vh), (Vh - Vb * K / Q + K * K), a0, 2.0 * (K * K - 1.0), (1.0 - K / Q + K * K));
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (kPi * f0 / sr);
            hp.set (1.0, -2.0, 1.0, 1.0 + K / Q + K * K, 2.0 * (K * K - 1.0), 1.0 - K / Q + K * K);
        }
        block = (int) (sr * 0.1);
        reset();
    }
    void reset()
    {
        shelf.reset(); hp.reset();
        acc = 0; n = 0; blocks.clear(); gated.clear();
        momentary = shortTerm = integrated = -70; peakHold = 0;
    }
    void process (const float* L, const float* R, int num)
    {
        for (int i = 0; i < num; ++i)
        {
            const float l = hp.process (shelf.process (L[i], 0), 0), r = hp.process (shelf.process (R[i], 1), 1);
            acc += (double) l * l + (double) r * r;
            if (++n >= block)
            {
                blocks.push_back (acc / n);
                acc = 0; n = 0;
                if (blocks.size() > 30) blocks.erase (blocks.begin());
                momentary = lufs (meanLast (4));
                shortTerm = lufs (meanLast (30));
                // integrated: 400 ms blocks with 75 % overlap, absolute gate -70, relative gate -10
                if (blocks.size() >= 4)
                {
                    const double m = meanLast (4);
                    if (lufs (m) > -70) gated.push_back (m);
                    if (gated.size() > 36000) gated.erase (gated.begin());
                    double s = 0; for (auto v : gated) s += v;
                    const double rel = lufs (s / std::max<size_t> (1, gated.size())) - 10;
                    double s2 = 0; int c = 0;
                    for (auto v : gated) if (lufs (v) > rel) { s2 += v; ++c; }
                    integrated = c ? lufs (s2 / c) : -70;
                }
            }
        }
    }
    double momentary = -70, shortTerm = -70, integrated = -70;
    float peakHold = 0;

private:
    static double lufs (double ms) { return -0.691 + 10.0 * std::log10 (std::max (1e-12, ms)); }
    double meanLast (size_t k) const { const size_t m = std::min (k, blocks.size()); double s = 0; for (size_t i = blocks.size() - m; i < blocks.size(); ++i) s += blocks[i]; return m ? s / m : 0; }
    Biquad shelf, hp;
    double rate = 48000, acc = 0;
    int n = 0, block = 4800;
    std::vector<double> blocks, gated;
};

// stereo-linked look-ahead brick-wall limiter (run it oversampled for true-peak safety).
// Gain = box-average of (release-smoothed (min-hold of the needed gain)), with the audio delayed by the
// look-ahead: this guarantees the gain on every sample is at or below what that sample needs, so nothing
// ever has to be clipped.
class Limiter
{
public:
    void prepare (double sr, double lookMs = 1.5)
    {
        rate = sr;
        look = std::max (2, (int) (sr * lookMs * 0.001));
        for (auto& b : buf) b.assign ((size_t) look + 1, 0.0f);
        need.assign ((size_t) look + 1, 1.0f);
        box.assign ((size_t) look, 1.0f);
        boxSum = (double) look; w = 0; bw = 0; r = 1; gr = 0;
    }
    int latency() const { return look; }
    // peaks: optional per-sample detection level (e.g. the true peak from an oversampled copy)
    void process (float* L, float* R, int n, float ceiling, float releaseMs, const float* peaks = nullptr)
    {
        const float rel = std::exp (-1.0f / (std::max (5.0f, releaseMs) * 0.001f * (float) rate));
        const int size = look + 1;
        for (int i = 0; i < n; ++i)
        {
            const float pk = peaks != nullptr ? std::max (peaks[i], std::max (std::fabs (L[i]), std::fabs (R[i]))) : std::max (std::fabs (L[i]), std::fabs (R[i]));
            buf[0][(size_t) w] = L[i]; buf[1][(size_t) w] = R[i];
            need[(size_t) w] = pk > ceiling ? ceiling / pk : 1.0f;
            float h = 1.0f;
            for (int k = 0; k < size; ++k) h = std::min (h, need[(size_t) k]);    // min over the last look+1 samples
            r = h < r ? h : h + (r - h) * rel;                                     // instant down, smooth release
            boxSum += (double) r - box[(size_t) bw]; box[(size_t) bw] = r; bw = (bw + 1) % look;
            const float g = (float) (boxSum / look);
            const int rd = (w + 1) % size;                                         // the sample from look samples ago
            L[i] = buf[0][(size_t) rd] * g; R[i] = buf[1][(size_t) rd] * g;
            w = (w + 1) % size;
            gr = std::max (gr * 0.9995f, -gainToDb (g));
        }
    }
    float gr = 0;

private:
    double rate = 48000, boxSum = 0;
    int look = 64, w = 0, bw = 0;
    float r = 1;
    std::array<std::vector<float>, 2> buf;
    std::vector<float> need, box;
};
} // namespace mix
