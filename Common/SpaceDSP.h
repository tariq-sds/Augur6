#pragma once
// Shared building blocks for Relay (delay) and Vault (reverb): interpolated delay lines, allpasses,
// filters, envelope followers, a grain pitch shifter and tempo divisions.
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace space
{
constexpr float kPi = 3.14159265358979f, kTwoPi = 6.28318530718f;
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float softClip (float x) { x = clampf (x, -3.0f, 3.0f); return x * (27.0f + x * x) / (27.0f + 9.0f * x * x); }
inline float dbToGain (float db) { return std::pow (10.0f, db * 0.05f); }

struct Rng
{
    uint32_t s = 0x1234567u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (2.0f / 16777216.0f) - 1.0f; }
};

// power-of-two circular buffer, read with 4-point Hermite interpolation
struct Delay
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void setMax (int samples)
    {
        int n = 1; while (n < samples + 8) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    // delay in samples (>= 1) measured from the most recent write
    float read (float d) const
    {
        d = clampf (d, 1.0f, (float) (mask - 4));
        const float rp = (float) w - d;
        int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float y0 = buf[(size_t) ((i - 1) & mask)], y1 = buf[(size_t) (i & mask)], y2 = buf[(size_t) ((i + 1) & mask)], y3 = buf[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }
    float readInt (int d) const { return buf[(size_t) ((w - d) & mask)]; }
};

// Schroeder allpass with an (optionally modulated) interpolated delay
struct Allpass
{
    Delay d;
    float len = 100;
    void setMax (int n) { d.setMax (n); }
    void clear() { d.clear(); }
    float process (float x, float g, float mod = 0.0f)
    {
        const float v = d.read (len + mod);
        const float u = x + g * v;
        d.push (u);
        return v - g * u;
    }
};

struct OnePole   // low-pass; hp() = x - lp
{
    float z = 0, a = 0;
    void setHz (float hz, float sr) { a = std::exp (-kTwoPi * clampf (hz, 1.0f, 0.49f * sr) / sr); }
    float lp (float x) { z = x * (1.0f - a) + z * a; return z; }
    float hp (float x) { return x - lp (x); }
    void clear() { z = 0; }
};

// TPT state-variable filter: low / band / high outputs
struct Svf
{
    float ic1 = 0, ic2 = 0, lpo = 0, bpo = 0, hpo = 0;
    void clear() { ic1 = ic2 = 0; }
    void process (float x, float hz, float q, float sr)
    {
        const float g = std::tan (kPi * clampf (hz, 10.0f, 0.45f * sr) / sr), k = 1.0f / std::max (0.3f, q);
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        lpo = v2; bpo = v1; hpo = x - k * v1 - v2;
    }
};

struct Follower
{
    float env = 0, att = 0, rel = 0;
    void set (float attSec, float relSec, float sr) { att = std::exp (-1.0f / (std::max (1e-4f, attSec) * sr)); rel = std::exp (-1.0f / (std::max (1e-4f, relSec) * sr)); }
    float process (float x) { x = std::fabs (x); env = x > env ? x + (env - x) * att : x + (env - x) * rel; return env; }
};

// Two crossfading grains read from a delay line: shifts pitch by `ratio` with `window` samples of latency
struct PitchShifter
{
    Delay d;
    float phase = 0, window = 2048;
    void setMax (int n) { d.setMax (n); }
    void clear() { d.clear(); phase = 0; }
    float process (float x, float ratio)
    {
        d.push (x);
        phase += (1.0f - ratio) / window;
        phase -= std::floor (phase);
        const float p2 = phase + 0.5f - std::floor (phase + 0.5f);
        const float w1 = std::sin (kPi * phase), w2 = std::sin (kPi * p2);
        return d.read (1.0f + phase * window) * w1 * w1 + d.read (1.0f + p2 * window) * w2 * w2;
    }
};

// note divisions for tempo sync, in quarter-note beats
inline const char* const kDivNames[] { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2T", "1/2", "1/2.", "1 bar" };
constexpr int kNumDivs = 14;
inline double divBeats (int i)
{
    static const double b[] { 0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0, 3.0, 4.0 };
    return b[std::clamp (i, 0, kNumDivs - 1)];
}
} // namespace space
