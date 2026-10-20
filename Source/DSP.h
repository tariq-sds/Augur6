#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

// Core building blocks: band-limited morphing oscillator, nonlinear 4-pole ladder,
// 2-pole state-variable high-pass, analog-style ADSR and a small PRNG.
namespace augur
{
constexpr float kPi = 3.14159265358979f;

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float midiToHz (float n) { return 440.0f * std::exp2 ((n - 69.0f) * (1.0f / 12.0f)); }

// Fast tanh approximation (Padé), accurate enough for saturation
inline float fastTanh (float x)
{
    x = clampf (x, -4.5f, 4.5f);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    void seed (uint32_t v) { s = v ? v : 0x9E3779B9u; }
    // uniform in [-1, 1)
    float next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) (s >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }
};

inline float polyBlep (float t, float dt)
{
    if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
    return 0.0f;
}

// Continuously variable waveshape: 0 = triangle, 0.5 = sawtooth, 1 = pulse (width set by pw)
struct MorphOsc
{
    float phase = 0.0f;

    float render (float dt, float shape, float pw) const
    {
        const float p = phase;
        const float saw = 2.0f * p - 1.0f - polyBlep (p, dt);

        if (shape <= 0.5f)
        {
            const float tri = 4.0f * std::fabs (p - 0.5f) - 1.0f;
            const float t = shape * 2.0f;
            return tri + (saw - tri) * t;
        }

        float p2 = p + 1.0f - pw;
        if (p2 >= 1.0f) p2 -= 1.0f;
        float pulse = (p < pw ? 1.0f : -1.0f) + polyBlep (p, dt) - polyBlep (p2, dt);
        pulse -= (2.0f * pw - 1.0f); // remove DC like an AC-coupled analog output
        const float t = (shape - 0.5f) * 2.0f;
        return saw + (pulse - saw) * t;
    }

    // returns true if the phase wrapped; frac = how far past the wrap we are, in samples
    bool advance (float dt, float& frac)
    {
        phase += dt;
        if (phase >= 1.0f)
        {
            phase -= 1.0f;
            if (phase >= 1.0f) phase = 0.0f;
            frac = dt > 0.0f ? phase / dt : 0.0f;
            return true;
        }
        return false;
    }
};

// Square sub-oscillator one octave below oscillator 1
struct SubOsc
{
    float phase = 0.0f;
    float tick (float dt)
    {
        const float p = phase;
        float p2 = p + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f;
        float v = (p < 0.5f ? 1.0f : -1.0f) + polyBlep (p, dt) - polyBlep (p2, dt);
        phase += dt; if (phase >= 1.0f) phase -= 1.0f;
        return v;
    }
};

// Zero-delay-feedback 4-pole ladder with saturating input stage
struct Ladder
{
    float s[4] {};
    void reset() { s[0] = s[1] = s[2] = s[3] = 0.0f; }

    float process (float x, float g, float k)
    {
        const float G = g / (1.0f + g);
        const float beta = 1.0f / (1.0f + g);
        const float G2 = G * G, G3 = G2 * G, G4 = G2 * G2;
        const float S = G3 * beta * s[0] + G2 * beta * s[1] + G * beta * s[2] + beta * s[3];

        x *= (1.0f + 0.45f * k);                   // resonance bass-loss compensation
        float u = (x - k * S) / (1.0f + k * G4);
        u = fastTanh (u);

        for (int i = 0; i < 4; ++i)
        {
            const float v = (u - s[i]) * G;
            const float y = v + s[i];
            s[i] = y + v;
            u = y;
        }
        return u;
    }
};

// 2-pole TPT state-variable filter, high-pass output
struct SvfHP
{
    float ic1 = 0.0f, ic2 = 0.0f;
    void reset() { ic1 = ic2 = 0.0f; }

    float process (float x, float g, float k)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return x - k * v1 - v2;
    }
};

// Analog-style exponential ADSR (RC-curve attack that overshoots its target)
struct Adsr
{
    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float value = 0.0f;

    void set (float a, float d, float s, float r, float sr)
    {
        sus = clampf (s, 0.0f, 1.0f);
        coef (a * sr, 0.3f,    aCoef);
        aBase = (1.0f + 0.3f) * (1.0f - aCoef);
        coef (d * sr, kRatioDR, dCoef);
        dBase = (sus - kRatioDR) * (1.0f - dCoef);
        coef (r * sr, kRatioDR, rCoef);
        rBase = -kRatioDR * (1.0f - rCoef);
    }

    void gateOn()  { stage = Attack; }
    void gateOff() { if (stage != Idle) stage = Release; }
    void reset()   { stage = Idle; value = 0.0f; }
    bool active() const { return stage != Idle; }

    float process()
    {
        switch (stage)
        {
            case Attack:
                value = aBase + value * aCoef;
                if (value >= 1.0f) { value = 1.0f; stage = Decay; }
                break;
            case Decay:
                value = dBase + value * dCoef;
                if (value <= sus) { value = sus; stage = Sustain; }
                break;
            case Sustain:
                value += (sus - value) * 0.002f;
                break;
            case Release:
                value = rBase + value * rCoef;
                if (value <= 0.0f) { value = 0.0f; stage = Idle; }
                break;
            case Idle: default:
                break;
        }
        return value;
    }

private:
    static constexpr float kRatioDR = 0.0005f;
    float aCoef = 0, aBase = 0, dCoef = 0, dBase = 0, rCoef = 0, rBase = 0, sus = 1.0f;

    static void coef (float samples, float ratio, float& c)
    {
        samples = std::max (samples, 4.0f);
        c = std::exp (-std::log ((1.0f + ratio) / ratio) / samples);
    }
};

} // namespace augur
