#pragma once
#include <cmath>
#include <array>
#include <vector>
#include <algorithm>

// Interlock EQ: filter design.
// Every section is "matched": poles placed where the analogue filter's poles land (impulse invariance)
// and the numerator solved so the digital magnitude equals the analogue one at DC, at the band frequency
// and at Nyquist. Result: zero latency and no high-frequency "cramping" (a bell at 16 kHz keeps its shape).
namespace ileq
{
constexpr double kPi = 3.14159265358979323846;

enum Shape { shBell = 0, shLowShelf, shLowCut, shHighShelf, shHighCut, shNotch, shBandPass, shTiltShelf, shFlatTilt, kNumShapes };
enum Place { plStereo = 0, plLeft, plRight, plMid, plSide, kNumPlaces };

inline const char* shapeName (int s)
{
    static const char* n[] { "Bell", "Low Shelf", "Low Cut", "High Shelf", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" };
    return n[std::clamp (s, 0, kNumShapes - 1)];
}
inline const char* placeName (int p) { static const char* n[] { "Stereo", "Left", "Right", "Mid", "Side" }; return n[std::clamp (p, 0, kNumPlaces - 1)]; }
inline const std::vector<int>& slopes() { static const std::vector<int> s { 6, 12, 18, 24, 30, 36, 48, 72, 96, 0 }; return s; }   // 0 = brickwall
inline bool isCut (int s) { return s == shLowCut || s == shHighCut; }
inline bool hasGain (int s) { return s == shBell || s == shLowShelf || s == shHighShelf || s == shTiltShelf || s == shFlatTilt; }
inline bool hasSlope (int s) { return s != shBell; }
inline int defaultSlopeIndex (int s) { return isCut (s) ? 3 : (s == shTiltShelf ? 0 : 1); }

struct Coefs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

struct Section
{
    Coefs c;
    // magnitude squared at a digital frequency via the phi form (cheap, exact for this biquad)
    double mag2 (double phi0, double phi1, double phi2) const
    {
        const double B0 = (c.b0 + c.b1 + c.b2) * (c.b0 + c.b1 + c.b2), B1 = (c.b0 - c.b1 + c.b2) * (c.b0 - c.b1 + c.b2), B2 = -4.0 * c.b0 * c.b2;
        const double A0 = (1 + c.a1 + c.a2) * (1 + c.a1 + c.a2), A1 = (1 - c.a1 + c.a2) * (1 - c.a1 + c.a2), A2 = -4.0 * c.a2;
        const double num = B0 * phi0 + B1 * phi1 + B2 * phi2, den = A0 * phi0 + A1 * phi1 + A2 * phi2;
        return den > 1e-300 ? std::max (0.0, num / den) : 1.0;
    }
};

struct Phi { double p0, p1, p2; };
inline Phi phiOf (double hz, double fs)
{
    const double s = std::sin (kPi * hz / fs);
    const double p1 = s * s, p0 = 1.0 - p1;
    return { p0, p1, 4.0 * p0 * p1 };
}

// analogue section: (n2 s^2 + n1 s + n0) / (d2 s^2 + d1 s + d0), s in rad/s
struct Analog { double n2, n1, n0, d2, d1, d0; };

inline double analogMag2 (const Analog& a, double w)
{
    const double nr = a.n0 - a.n2 * w * w, ni = a.n1 * w;
    const double dr = a.d0 - a.d2 * w * w, di = a.d1 * w;
    const double den = dr * dr + di * di;
    return den > 1e-300 ? (nr * nr + ni * ni) / den : 1e12;
}

inline Section matched (const Analog& a, double fs, double fMatch)
{
    Section out;
    const double T = 1.0 / fs;
    const double wNyq = kPi * fs;
    if (a.d2 == 0.0)
    {
        // first order: pole by impulse invariance, numerator matched at DC and Nyquist
        const double p = -a.d0 / a.d1;
        const double a1 = -std::exp (p * T);
        const double B0 = analogMag2 (a, 0.0) * (1 + a1) * (1 + a1);
        const double B1 = analogMag2 (a, wNyq) * (1 - a1) * (1 - a1);
        const double s0 = std::sqrt (std::max (0.0, B0)), s1 = std::sqrt (std::max (0.0, B1));
        // numerator sign: keep the DC and Nyquist signs of the analogue section
        const double dcSign = (a.n0 / a.d0) >= 0 ? 1.0 : -1.0;
        const double nySign = (a.n1 / a.d1) >= 0 ? 1.0 : -1.0;
        out.c.b0 = 0.5 * (dcSign * s0 + nySign * s1);
        out.c.b1 = 0.5 * (dcSign * s0 - nySign * s1);
        out.c.b2 = 0; out.c.a1 = a1; out.c.a2 = 0;
        return out;
    }
    // poles
    const double p = a.d1 / a.d2, q = a.d0 / a.d2;
    const double disc = p * p * 0.25 - q;
    double a1, a2;
    if (disc < 0)
    {
        const double re = -p * 0.5, im = std::sqrt (-disc);
        const double r = std::exp (re * T);
        a1 = -2.0 * r * std::cos (im * T);
        a2 = r * r;
    }
    else
    {
        const double sq = std::sqrt (disc);
        const double s1 = -p * 0.5 + sq, s2 = -p * 0.5 - sq;
        a1 = -(std::exp (s1 * T) + std::exp (s2 * T));
        a2 = std::exp ((s1 + s2) * T);
    }
    const double A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4.0 * a2;
    const double B0 = analogMag2 (a, 0.0) * A0;
    const double B1 = analogMag2 (a, wNyq) * A1;
    const double fm = std::clamp (fMatch, 1.0, fs * 0.499);
    const auto ph = phiOf (fm, fs);
    const double Am = A0 * ph.p0 + A1 * ph.p1 + A2 * ph.p2;
    double B2 = (analogMag2 (a, 2.0 * kPi * fm) * Am - B0 * ph.p0 - B1 * ph.p1) / std::max (1e-30, ph.p2);
    const double s0 = std::sqrt (std::max (0.0, B0)), s1 = std::sqrt (std::max (0.0, B1));
    const double W = 0.5 * (s0 + s1);
    if (W * W + B2 < 0) B2 = -W * W;
    const double b0 = 0.5 * (W + std::sqrt (W * W + B2));
    out.c.b0 = b0;
    out.c.b1 = 0.5 * (s0 - s1);
    out.c.b2 = b0 > 1e-30 ? -B2 / (4.0 * b0) : 0.0;
    out.c.a1 = a1; out.c.a2 = a2;
    return out;
}

// ---------------------------------------------------------------- band -> sections
struct BandSpec
{
    int shape = shBell;
    double freq = 1000, gainDb = 0, q = 1;
    int slopeIndex = 1;
};

constexpr int kMaxSections = 9;

struct Design
{
    std::array<Section, kMaxSections> s {};
    int n = 0;
    bool brick = false;          // brickwall cut (ideal in linear phase, 96 dB/oct here)
    double scale = 1.0;          // overall linear gain (flat tilt pivot)
    void add (const Section& x) { if (n < kMaxSections) s[(size_t) n++] = x; }
};

inline int slopeDb (int idx) { const auto& v = slopes(); return v[(size_t) std::clamp (idx, 0, (int) v.size() - 1)]; }

inline Design design (const BandSpec& b, double fs)
{
    Design d;
    const double f = std::clamp (b.freq, 5.0, fs * 0.49);
    const double w = 2.0 * kPi * f;
    const double G = std::pow (10.0, b.gainDb / 20.0);
    const double q = std::clamp (b.q, 0.025, 40.0);
    int slope = slopeDb (b.slopeIndex);
    if (slope == 0) { d.brick = true; slope = 96; }
    const int order = std::max (1, slope / 6);

    auto butterQs = [] (int N, std::vector<double>& qs)
    {
        qs.clear();
        for (int k = 1; k <= N / 2; ++k) qs.push_back (1.0 / (2.0 * std::cos (kPi * (2.0 * k - 1.0) / (2.0 * N))));
    };
    std::vector<double> qs;

    switch (b.shape)
    {
        case shBell:
        {
            const double A = std::sqrt (G);
            d.add (matched ({ 1, A * w / q, w * w, 1, w / (A * q), w * w }, fs, f));
            break;
        }
        case shLowShelf:
        case shHighShelf:
        {
            const bool low = b.shape == shLowShelf;
            butterQs (order, qs);
            const double gSec2 = std::pow (G, 2.0 / order), gSec1 = std::pow (G, 1.0 / order);
            for (size_t k = 0; k < qs.size(); ++k)
            {
                const double qk = qs[k] * (k == 0 ? q / 0.70710678 : 1.0);
                const double A = std::sqrt (gSec2), sA = std::sqrt (A);
                if (low) d.add (matched ({ A, A * sA * w / qk, A * A * w * w, A, sA * w / qk, w * w }, fs, f));
                else     d.add (matched ({ A * A, A * sA * w / qk, A * w * w, 1, sA * w / qk, A * w * w }, fs, f));
            }
            if (order % 2 == 1)
            {
                const double sg = std::sqrt (gSec1);
                if (low) d.add (matched ({ 0, 1, w * sg, 0, 1, w / sg }, fs, f));
                else     d.add (matched ({ 0, gSec1, sg * w, 0, 1, sg * w }, fs, f));
            }
            break;
        }
        case shLowCut:
        case shHighCut:
        {
            const bool low = b.shape == shLowCut;
            butterQs (order, qs);
            for (size_t k = 0; k < qs.size(); ++k)
            {
                const double qk = qs[k] * (k == 0 ? q / 0.70710678 : 1.0);
                if (low) d.add (matched ({ 1, 0, 0, 1, w / qk, w * w }, fs, f));
                else     d.add (matched ({ 0, 0, w * w, 1, w / qk, w * w }, fs, f));
            }
            if (order % 2 == 1)
            {
                if (low) d.add (matched ({ 0, 1, 0, 0, 1, w }, fs, f));
                else     d.add (matched ({ 0, 0, w, 0, 1, w }, fs, f));
            }
            break;
        }
        case shNotch:
        case shBandPass:
        {
            const int k = std::clamp (slope / 12, 1, 8);
            for (int i = 0; i < k; ++i)
            {
                if (b.shape == shNotch) d.add (matched ({ 1, 0, w * w, 1, w / q, w * w }, fs, f * 1.0001));
                else                    d.add (matched ({ 0, w / q, 0, 1, w / q, w * w }, fs, f));
            }
            break;
        }
        case shTiltShelf:
        {
            // a tilt around the band frequency: lows go one way, highs the other, 0 dB at f
            const int k = std::clamp (order, 1, 8);
            const double g = std::pow (G, 1.0 / k), sg = std::sqrt (g);
            for (int i = 0; i < k; ++i)
                d.add (matched ({ 0, g / sg, sg * w / sg, 0, 1, sg * w }, fs, f));   // high shelf, scaled by 1/sqrt(g)
            break;
        }
        case shFlatTilt:
        {
            // a straight dB-per-octave tilt over the whole audible range, pivoting at the band frequency.
            // gainDb is the total tilt from 20 Hz to 20 kHz.
            const double corners[] { 28, 70, 175, 440, 1100, 2750, 6900, 17000 };
            const int k = 8;
            const double g = std::pow (G, 1.0 / k), sg = std::sqrt (g);
            for (int i = 0; i < k; ++i)
            {
                const double wc = 2.0 * kPi * corners[i];
                d.add (matched ({ 0, g, sg * wc, 0, 1, sg * wc }, fs, corners[i]));
            }
            // pivot: 0 dB at the band frequency
            const auto ph = phiOf (f, fs);
            double m = 1.0;
            for (int i = 0; i < d.n; ++i) m *= d.s[(size_t) i].mag2 (ph.p0, ph.p1, ph.p2);
            d.scale = m > 0 ? 1.0 / std::sqrt (m) : 1.0;
            break;
        }
        default: break;
    }
    return d;
}

inline double designMag2 (const Design& d, const Phi& ph)
{
    double m = d.scale * d.scale;
    for (int i = 0; i < d.n; ++i) m *= d.s[(size_t) i].mag2 (ph.p0, ph.p1, ph.p2);
    return m;
}

// ideal brickwall (linear phase): a 1/12-octave raised-cosine edge at the cut frequency
inline double brickMag2 (int shape, double freq, double hz)
{
    const double oct = std::log2 (std::max (1e-3, hz) / freq);
    const double t = std::clamp ((oct + 1.0 / 24.0) * 12.0, 0.0, 1.0);    // 0 below, 1 above the edge
    const double pass = shape == shLowCut ? 0.5 - 0.5 * std::cos (kPi * t) : 0.5 + 0.5 * std::cos (kPi * t);
    return pass * pass;
}
} // namespace ileq
