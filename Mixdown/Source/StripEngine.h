#pragma once
#include "../../Common/MixDSP.h"

// Toota Strip: a pre-balanced channel chain. Pick what's on the channel; the type sets a sensible chain
// (clean-up filter, tone, compression, colour, stereo) and seven macros move it from there.
namespace strip
{
struct Type
{
    const char* name;
    float hpf;                         // clean-up high-pass (Hz)
    float bodyHz, bodyQ;               // BODY bell
    float cutHz, cutDb, cutQ;          // fixed corrective dip (mud / box)
    float presHz, presDb;              // fixed presence bell
    float airHz;                       // AIR high shelf
    float thr, ratio, attMs, relMs;    // compressor at GLUE 50 %
    float drive;                       // saturation at DRIVE 50 %
    float monoBelow;                   // Hz, 0 = off
    float width;                       // stereo width at WIDTH 50 %
    const char* info;
};

inline const Type* types()
{
    static const Type t[] {
        { "KICK",       25,  60, 0.9f, 330, -3.0f, 1.3f, 3500, 1.5f, 9000, -14, 3.0f, 12,  80, 0.20f, 300, 0.0f,  "Weight at 60 Hz, box cut at 330, click at 3.5 k. Mono." },
        { "BASS",       28,  90, 0.8f, 250, -2.0f, 1.1f, 800,  1.5f, 5000, -16, 4.0f, 10, 120, 0.30f, 150, 0.8f,  "Solid low end, definition at 800 Hz, mono below 150." },
        { "DRUMS",      45,  180, 0.9f, 450, -1.5f, 1.0f, 5000, 1.0f, 9000, -18, 3.0f, 20, 120, 0.15f, 0,  1.0f,  "Drum loops and percussion: body, a little snap and glue." },
        { "HATS",      300, 3000, 1.0f, 600, -2.0f, 1.0f, 7000, 0.0f, 11000, -20, 2.0f, 5,   60, 0.10f, 0,  1.1f,  "Tops: low end cleared, air and a gentle squash." },
        { "SNARE/CLAP", 90, 200, 1.0f, 500, -2.0f, 1.2f, 4000, 1.5f, 10000, -16, 3.5f, 15, 100, 0.25f, 0,  1.0f,  "Body at 200 Hz, crack at 4 k." },
        { "CHORDS",    150, 300, 0.8f, 450, -1.5f, 1.0f, 2500, 0.0f, 9000, -20, 2.5f, 30, 150, 0.15f, 150, 1.15f, "Pads, chords, keys: room for the bass and vocal." },
        { "LEAD",      180, 900, 0.9f, 400, -1.5f, 1.2f, 3000, 1.0f, 8000, -18, 3.0f, 15, 100, 0.20f, 0,  1.0f,  "Synth leads and riffs: forward and controlled." },
        { "VOCAL",     100, 200, 1.0f, 350, -2.5f, 1.4f, 3000, 2.0f, 12000, -20, 4.0f, 5,  90, 0.10f, 0,  1.0f,  "Lead vocal: mud cut, presence, air and steady level." },
        { "BACKING",   200, 400, 1.0f, 400, -3.0f, 1.2f, 3500, 1.0f, 12000, -24, 4.0f, 8, 120, 0.10f, 0,  1.3f,  "Backing vocals: thinner, airier and wider." },
        { "FX",        250, 1000, 0.7f, 500, -1.0f, 1.0f, 6000, 0.0f, 10000, -24, 2.0f, 40, 250, 0.10f, 0,  1.3f,  "Risers, atmos and effects: out of the way, wide." },
        { "BUS",        20, 100, 0.7f, 300, 0.0f, 1.0f, 3000, 0.0f, 12000, -16, 2.0f, 30, 200, 0.10f, 0,  1.0f,  "Group buses: glue without changing the balance." },
    };
    return t;
}
constexpr int kNumTypes = 11;

struct Settings
{
    int type = 0;
    float punch = 0.5f, body = 0.5f, air = 0.5f, glue = 0.5f, drive = 0.5f, width = 0.5f, clean = 0.5f;   // macros 0..1
    float trimDb = 0, outDb = 0;
};

class Engine
{
public:
    void prepare (double rate)
    {
        sr = rate;
        for (auto* b : { &hp1, &hp2, &bodyEq, &cutEq, &presEq, &airEq, &monoLp }) b->reset();
        comp.prepare ((float) rate);
        tr.prepare ((float) rate);
        lastType = -1;
    }

    void process (float* L, float* R, int n, const Settings& s)
    {
        const auto& t = types()[std::clamp (s.type, 0, kNumTypes - 1)];
        // ---- macros -> chain
        const float hz = s.clean <= 0.02f ? 0.0f : t.hpf * std::exp2 ((s.clean - 0.5f) * 2.0f);
        const float bodyDb = (s.body - 0.5f) * 12.0f;
        const float airDb = (s.air - 0.5f) * 12.0f + 1.0f;
        const float punchDb = (s.punch - 0.5f) * 18.0f;
        const float thr = t.thr - (s.glue - 0.5f) * 16.0f;
        const float ratio = s.glue <= 0.02f ? 1.0f : 1.0f + (t.ratio - 1.0f) * std::min (1.5f, s.glue * 2.0f);
        const float drive = std::min (1.0f, t.drive * s.drive * 2.0f);
        const float width = t.width * s.width * 2.0f;
        if (s.type != lastType || hz != lastHz || bodyDb != lastBody || airDb != lastAir)
        {
            if (hz > 0) { hp1.design (mix::Biquad::HighPass, hz, 0.54, 0, sr); hp2.design (mix::Biquad::HighPass, hz, 1.31, 0, sr); }
            bodyEq.design (mix::Biquad::Bell, t.bodyHz, t.bodyQ, bodyDb, sr);
            cutEq.design (mix::Biquad::Bell, t.cutHz, t.cutQ, t.cutDb, sr);
            presEq.design (mix::Biquad::Bell, t.presHz, 1.0, t.presDb, sr);
            airEq.design (mix::Biquad::HighShelf, t.airHz, 0.7, airDb, sr);
            if (t.monoBelow > 0) { monoLp.design (mix::Biquad::HighPass, t.monoBelow, 0.707, 0, sr); monoHp2.design (mix::Biquad::HighPass, t.monoBelow, 0.707, 0, sr); }
            if (s.type != lastType) { hp1.reset(); hp2.reset(); monoLp.reset(); monoHp2.reset(); }
            lastType = s.type; lastHz = hz; lastBody = bodyDb; lastAir = airDb;
        }
        comp.setTimes (t.attMs * (1.0f + std::max (0.0f, s.punch - 0.5f)), t.relMs);
        const float makeup = mix::dbToGain (std::max (0.0f, -thr - 6.0f) * (1.0f - 1.0f / ratio) * 0.35f);
        const float trim = mix::dbToGain (s.trimDb), out = mix::dbToGain (s.outDb);
        float grMax = 0;

        for (int i = 0; i < n; ++i)
        {
            float l = L[i] * trim, r = R[i] * trim;
            if (hz > 0) { l = hp2.process (hp1.process (l, 0), 0); r = hp2.process (hp1.process (r, 1), 1); }
            l = cutEq.process (l, 0); r = cutEq.process (r, 1);
            l = bodyEq.process (l, 0); r = bodyEq.process (r, 1);
            l = presEq.process (l, 0); r = presEq.process (r, 1);
            l = airEq.process (l, 0); r = airEq.process (r, 1);
            if (std::fabs (punchDb) > 0.05f) { const float g = tr.gain (l, r, punchDb); l *= g; r *= g; }
            if (ratio > 1.01f) { const float g = comp.gain (l, r, thr, ratio) * makeup; l *= g; r *= g; grMax = std::max (grMax, comp.gr); }
            l = mix::saturate (l, drive); r = mix::saturate (r, drive);
            // stereo: width, with everything below monoBelow made mono
            float m = 0.5f * (l + r), sd = 0.5f * (l - r) * width;
            if (t.monoBelow > 0) sd = monoHp2.process (monoLp.process (sd, 0), 0);
            L[i] = (m + sd) * out; R[i] = (m - sd) * out;
        }
        gr = std::max (grMax, gr * 0.9f);
    }

    float gr = 0;
    double sr = 48000;

private:
    mix::Biquad hp1, hp2, bodyEq, cutEq, presEq, airEq, monoLp, monoHp2;
    mix::Compressor comp;
    mix::Transient tr;
    int lastType = -1;
    float lastHz = -1, lastBody = 99, lastAir = 99;
};
} // namespace strip
