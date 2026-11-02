#pragma once
// Sanctum: turns one vocal into a choir. Every singer is its own formant-preserving PSOLA voice with its own
// pitch drift, timing, vowel size and place on the stage, so the ensemble sounds like people, not a chorus pedal.
// Harmony parts follow the key / scale (or MIDI chords) but keep the lead's natural inflections: each part is
// shifted by a whole number of semitones from the note the lead is singing, never pulled onto a grid.
#include "../../Clarion/Source/VocalDSP.h"
#include "../../Common/SpaceDSP.h"
#include <array>
#include <atomic>
#include <vector>
#include <cmath>
#include <algorithm>

namespace sc
{
constexpr int kSections = 4;            // soprano, alto, tenor, bass
constexpr int kPerSection = 8;
constexpr int kMaxSingers = kSections * kPerSection;

inline const char* const kSectionNames[kSections] { "SOPRANO", "ALTO", "TENOR", "BASS" };
inline const char* const kHarmonyNames[] { "Unison", "Octaves", "Thirds", "Triad Above", "Triad Below", "Power", "Full 4-Part", "Gospel Stack" };
constexpr int kNumHarmonies = 8;
// scale-degree offsets from the lead for S, A, T, B (7 = an octave)
inline const int kHarmonyDegrees[kNumHarmonies][kSections] {
    { 0, 0, 0, 0 },     // unison
    { 7, 0, 0, -7 },    // octaves
    { 2, 0, -2, -7 },   // thirds above and below
    { 4, 2, 0, -7 },    // triad above
    { 0, -2, -4, -7 },  // triad below (lead on top)
    { 7, 4, 0, -7 },    // power: root, fifth, octave
    { 7, 4, 2, -7 },    // full 4-part
    { 5, 2, 0, -3 },    // gospel stack: 6th and 3rd above, 5th below
};
inline const char* const kScaleNames[] { "Major", "Minor", "Dorian", "Mixolydian", "Harmonic Minor" };
constexpr int kNumScales = 5;
inline const int kScales[kNumScales][7] {
    { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 3, 5, 7, 8, 11 } };

inline int floorDiv (int a, int b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

// note -> note + n scale degrees; chromatic notes keep their offset from the scale note below them
inline int harmonise (int note, int degrees, int key, int scale)
{
    const int* s = kScales[std::clamp (scale, 0, kNumScales - 1)];
    const int rel = note - key, oct = floorDiv (rel, 12), pc = rel - 12 * oct;
    int idx = 0; for (int i = 0; i < 7; ++i) if (s[i] <= pc) idx = i;
    const int residual = pc - s[idx];
    const int d = idx + degrees, o2 = floorDiv (d, 7), i2 = d - 7 * o2;
    return key + 12 * (oct + o2) + s[i2] + residual;
}

// ---------------------------------------------------------------- shared input history for every voice
struct InputHistory
{
    static constexpr int kSize = 1 << 15, kMask = kSize - 1;
    std::vector<float> in, ep;
    int64_t n = 0;
    float epLp = 0;
    void prepare() { in.assign (kSize, 0.0f); ep.assign (kSize, 0.0f); n = 0; epLp = 0; }
    void push (float x) { const size_t i = (size_t) (n & kMask); in[i] = x; epLp += (x - epLp) * 0.12f; ep[i] = epLp; ++n; }
    float at (double pos) const
    {
        const double fl = std::floor (pos); const int64_t i = (int64_t) fl; const float f = (float) (pos - fl);
        const float a = in[(size_t) (i & kMask)], b = in[(size_t) ((i + 1) & kMask)];
        return a + (b - a) * f;
    }
    int64_t findEpoch (int64_t around, int T, int64_t limit) const
    {
        const int span = std::max (2, T / 4);
        int64_t best = around; float bv = -1e9f;
        for (int64_t i = around - span, hi = std::min (around + span, limit); i <= hi; ++i)
        {
            const float v = ep[(size_t) (i & kMask)];
            if (v > bv) { bv = v; best = i; }
        }
        return std::min (best, limit);
    }
};

// ---------------------------------------------------------------- one PSOLA voice with pitch and formant control
struct Voice
{
    static constexpr int kSize = InputHistory::kSize, kMask = kSize - 1;
    std::vector<float> out;
    double nextMark = 0;
    int64_t lastEpoch = -1000000;
    void prepare() { out.assign (kSize, 0.0f); nextMark = 0; lastEpoch = -1000000; }
    void reset() { std::fill (out.begin(), out.end(), 0.0f); lastEpoch = -1000000; }

    // call after history.push(); returns the sample `latency` behind the newest input
    float render (const InputHistory& h, int latency, int unvoicedT, float ratio, float formant, float period)
    {
        const int64_t cur = h.n - 1, outIdx = cur - latency;
        const bool voiced = period > 20.0f && period < (float) latency * 0.42f;
        const float T = voiced ? period : (float) unvoicedT;
        const float reach = T / std::min (1.0f, formant) + 1.0f;   // half-length of the longest grain
        if (nextMark < (double) outIdx - 4.0 * T) nextMark = (double) outIdx;   // after a reset or a long silence
        while (nextMark - reach <= (double) outIdx + 1)
        {
            const int64_t mark = (int64_t) std::llround (nextMark);
            if (voiced)
            {
                const int Ti = (int) std::round (T);
                const int64_t limit = cur - (int64_t) std::ceil (Ti * 1.35f) - 2;
                int64_t e;
                if (lastEpoch < mark - 3 * Ti || lastEpoch > mark + Ti) e = h.findEpoch (mark, Ti, limit);
                else { e = lastEpoch; while (e + Ti / 2 < mark) e = h.findEpoch (e + Ti, Ti, limit); }
                lastEpoch = e;
                addGrain (h, mark, e, (float) Ti, std::sqrt (formant / ratio), formant, outIdx);
                nextMark += (double) T / ratio;
            }
            else
            {
                const float L = (float) unvoicedT / formant;
                addGrain (h, mark, mark, (float) unvoicedT, 1.0f, formant, outIdx);
                nextMark = (double) mark + std::max (8.0f, L);
                lastEpoch = -1000000;
            }
        }
        const size_t o = (size_t) (outIdx & kMask);
        const float y = outIdx >= 0 ? out[o] : 0.0f;
        out[o] = 0.0f;
        return y;
    }

private:
    // grain centred on `at`, read from around `from`; formant > 1 squeezes the grain (smaller vocal tract)
    void addGrain (const InputHistory& h, int64_t at, int64_t from, float T, float gain, float formant, int64_t outIdx)
    {
        const int L = std::max (4, (int) std::round (T / formant));
        const float inv = space::kPi / (float) L;
        for (int k = (int) std::max<int64_t> (-L + 1, outIdx - at); k < L; ++k)   // never write behind the read point
        {
            const float wv = (0.5f + 0.5f * std::cos ((float) k * inv)) * gain;
            out[(size_t) ((at + k) & kMask)] += h.at ((double) from + (double) k * formant) * wv;
        }
    }
};

// slow random walk in -1..1 (new target every `period` seconds, smoothed)
struct Wander
{
    space::Rng rng;
    float v = 0, target = 0, coef = 0.999f;
    int count = 0, every = 24000;
    void setup (uint32_t seed, float periodSec, float sr)
    {
        rng.s = seed * 2654435761u + 12345u;
        every = std::max (64, (int) (periodSec * sr));
        coef = std::exp (-1.0f / (periodSec * 0.5f * sr));
        target = rng.next(); v = rng.next() * 0.5f; count = (int) ((rng.next() * 0.5f + 0.5f) * every);
    }
    float next()
    {
        if (++count >= every) { count = 0; target = rng.next(); }
        v = target + (v - target) * coef;
        return v;
    }
};

struct Singer
{
    Voice voice;
    space::Delay timing;
    space::OnePole air;           // distance roll-off
    Wander pitch, time;
    float baseDelay = 0.005f, scatter = 0, gainVar = 1, formantVar = 1;
    float fade = 0, gL = 0, gR = 0, send = 0, lastPitchCents = 0;
};

struct Settings
{
    int harmony = 6, key = 0, scale = 0, singers = 12;
    bool midiChord = true;
    float humanise = 0.5f, spread = 0.7f, character = 0.6f, formant = 0.0f, breath = 0.2f, tone = 0.0f, energy = 0.3f;
    float space = 0.5f, reverb = 0.35f, sub = 0.0f, response = 0.3f;
    float leadGain = 1.0f, choirGain = 0.7f, outGain = 1.0f;
    std::array<float, kSections> level { 1, 1, 1, 1 }, pan { -0.72f, -0.24f, 0.24f, 0.72f }, depth { 0.5f, 0.4f, 0.4f, 0.5f };
    int soloSection = -1;         // tests: only this section, dry, no reverb or breath
};

// ---------------------------------------------------------------- hall: 8-line feedback delay network
class Hall
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        for (auto& d : lines) d.setMax ((int) (0.25f * sr));
        for (auto& a : diff) a.setMax ((int) (0.03f * sr));
        pre.setMax ((int) (0.1f * sr));
        for (auto& f : damp) f.clear();
        configured = -1.0f;
    }
    void clear() { for (auto& d : lines) d.clear(); for (auto& a : diff) a.clear(); pre.clear(); for (auto& f : damp) f.clear(); }
    void process (float inL, float inR, float& outL, float& outR, float spaceAmt)
    {
        if (std::abs (spaceAmt - configured) > 0.002f) configure (spaceAmt);
        pre.push (0.5f * (inL + inR));
        float x = pre.read (preDelay);
        for (int i = 0; i < 4; ++i) x = diff[i].process (x, 0.62f);
        ++modPhase; const float mod = std::sin ((float) modPhase * modInc) * 6.0f;
        std::array<float, 8> v;
        for (int i = 0; i < 8; ++i) v[(size_t) i] = damp[i].lp (lines[i].read (len[i] + (i < 2 ? mod * (i ? -1.0f : 1.0f) : 0.0f)));
        // Hadamard mix
        for (int s = 1; s < 8; s <<= 1)
            for (int i = 0; i < 8; i += 2 * s)
                for (int j = i; j < i + s; ++j) { const float a = v[(size_t) j], b = v[(size_t) (j + s)]; v[(size_t) j] = a + b; v[(size_t) (j + s)] = a - b; }
        for (int i = 0; i < 8; ++i) lines[i].push (x * (i & 1 ? 1.0f : -1.0f) * 0.35f + v[(size_t) i] * 0.35355339f * fb[i]);
        outL = outR = 0;
        for (int i = 0; i < 8; ++i) { const float y = lines[i].read (1.0f + (float) (i * 37)); (i & 1 ? outR : outL) += y; }
        outL *= 0.5f; outR *= 0.5f;
    }

private:
    void configure (float s)
    {
        configured = s;
        static const float ms[8] { 37.1f, 41.3f, 47.9f, 53.7f, 59.3f, 67.1f, 73.9f, 79.7f };
        const float scale = 0.55f + s * 1.5f, t60 = 0.9f + s * 4.6f;
        for (int i = 0; i < 8; ++i)
        {
            len[i] = ms[i] * 0.001f * sr * scale;
            fb[i] = std::pow (10.0f, -3.0f * len[i] / (t60 * sr));
            damp[i].setHz (9500.0f - s * 3500.0f, sr);
        }
        static const float dms[4] { 4.7f, 3.6f, 12.7f, 9.3f };
        for (int i = 0; i < 4; ++i) diff[i].len = dms[i] * 0.001f * sr * (0.7f + s * 0.6f);
        preDelay = (0.012f + s * 0.03f) * sr;
        modInc = space::kTwoPi * 0.31f / sr;
    }
    float sr = 48000, configured = -1, preDelay = 600, modInc = 0;
    int64_t modPhase = 0;
    space::Delay lines[8], pre;
    space::Allpass diff[4];
    space::OnePole damp[8];
    float len[8] {}, fb[8] {};
};

// ---------------------------------------------------------------- the choir
class Engine
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        latency = (int) std::ceil (0.04f * sr);
        unvoicedT = (int) std::round (0.008f * sr);
        hintDelay = (int) (0.015f * sr);
        tracker.prepare (sr);
        history.prepare();
        for (int i = 0; i < kMaxSingers; ++i)
        {
            auto& s = singers[(size_t) i];
            s.voice.prepare();
            s.timing.setMax ((int) (0.09f * sr));
            s.air.setHz (14000.0f, sr);
            s.pitch.setup ((uint32_t) (i * 7 + 1), 0.9f + 0.05f * (float) (i % 7), sr);
            s.time.setup ((uint32_t) (i * 13 + 5), 2.5f + 0.2f * (float) (i % 5), sr);
            space::Rng r; r.s = (uint32_t) (0x9E3779B9u * (uint32_t) (i + 1));
            s.baseDelay = 0.5f + 0.5f * r.next();          // 0..1, scaled by humanise
            s.scatter = r.next();
            s.gainVar = std::pow (10.0f, r.next() * 0.1f);  // +-2 dB
            s.formantVar = r.next();                         // -1..1
            s.fade = 0;
        }
        sub.prepare();
        dry.setMax (latency + 64);
        hintRing.assign ((size_t) kHint, 0.0f);
        hall.prepare (sr);
        for (auto& b : breathBp) b.clear();
        breathHp.setHz (3000.0f, sr); breathEnv.set (0.002f, 0.06f, sr);
        tiltLp[0].setHz (800.0f, sr); tiltLp[1].setHz (800.0f, sr);
        presHp[0].setHz (2500.0f, sr); presHp[1].setHz (2500.0f, sr);
        levelEnv.set (0.005f, 0.25f, sr);
        for (auto& e : secEnv) e = 0;
        for (auto& s : semis) s = 0;
        period = 0; snapped = -1; voiced = false; heldCount = 0;
        n = 0;
    }

    int getLatency() const { return latency; }

    // MIDI chord mode: the held notes become the parts (highest = soprano, lowest = bass)
    void noteOn (int note) { if (heldCount < 16) { for (int i = 0; i < heldCount; ++i) if (held[i] == note) return; held[heldCount++] = note; } }
    void noteOff (int note) { for (int i = 0; i < heldCount; ++i) if (held[i] == note) { held[i] = held[--heldCount]; return; } }
    void allNotesOff() { heldCount = 0; }

    void process (const float* inL, const float* inR, float* outL, float* outR, int num, const Settings& st)
    {
        // ---- singer allocation and per-block gains
        std::array<int, kSections> active {};
        std::array<int, kSections> order {}; int nAct = 0;
        for (int s = 0; s < kSections; ++s) if (st.level[(size_t) s] > 0.01f && (st.soloSection < 0 || st.soloSection == s)) order[(size_t) nAct++] = s;
        const int total = nAct == 0 ? 0 : std::clamp (st.singers, 1, nAct * kPerSection);
        for (int i = 0; i < total; ++i) ++active[(size_t) order[(size_t) (i % nAct)]];
        const float norm = total > 0 ? 1.5f / std::sqrt ((float) total) : 0.0f;
        const float character = st.character, fShift = std::pow (2.0f, st.formant * 0.32f);
        static const float secFormant[kSections] { 1.08f, 1.03f, 0.95f, 0.86f };
        const float hum = st.humanise;
        for (int s = 0; s < kSections; ++s)
            for (int k = 0; k < kPerSection; ++k)
            {
                auto& sg = singers[(size_t) (s * kPerSection + k)];
                const bool on = k < active[(size_t) s];
                sg.targetOn = on ? 1.0f : 0.0f;
                const float slot = active[(size_t) s] > 1 ? ((float) k / (float) (active[(size_t) s] - 1)) * 2.0f - 1.0f : 0.0f;
                const float p = std::clamp (st.pan[(size_t) s] + st.spread * (0.22f * slot + 0.08f * sg.scatter), -1.0f, 1.0f);
                const float d = std::clamp (st.depth[(size_t) s], 0.0f, 1.0f);
                const float g = st.level[(size_t) s] * norm * sg.gainVar * (1.0f - 0.45f * d);
                const float a = (p + 1.0f) * 0.25f * space::kPi;
                sg.tgtL = g * std::cos (a); sg.tgtR = g * std::sin (a);
                sg.send = 0.35f + 1.2f * d;
                sg.formant = (1.0f + (secFormant[s] - 1.0f) * character) * fShift * (1.0f + 0.035f * hum * sg.formantVar);
                sg.delaySamples = ((0.004f + 0.032f * hum * sg.baseDelay) + 0.02f * d) * sr;
                sg.air.setHz (15000.0f * (1.0f - 0.65f * d), sr);
                sg.section = s;
                if (sg.fade <= 0.0f) sg.curDelay = sg.delaySamples;
            }
        const float glideCoef = std::exp (-1.0f / ((0.008f + 0.15f * st.response) * sr));
        const float driftCents = 14.0f * hum, timeWander = 0.006f * hum * sr;
        const float breathAmt = st.soloSection >= 0 ? 0.0f : st.breath * 2.5f;
        const float revAmt = st.soloSection >= 0 ? 0.0f : st.reverb;
        const float lead = st.soloSection >= 0 ? 0.0f : st.leadGain;
        const float subLvl = st.soloSection >= 0 ? 0.0f : st.sub * 0.8f;
        const float rampInc = 1.0f / (float) std::max (1, num);

        // targets in semitones per section
        computeTargets (st);

        for (int i = 0; i < num; ++i)
        {
            const float x = 0.5f * (inL[i] + inR[i]);
            if (tracker.push (x)) onPitch();
            history.push (x);
            dry.push (x);
            // pitch hint delayed so it lines up with the grains being placed ~40 ms back
            hintRing[(size_t) (n & (kHint - 1))] = voiced ? period : 0.0f;
            const float hint = hintRing[(size_t) ((n - hintDelay) & (kHint - 1))];
            ++n;
            for (int s = 0; s < kSections; ++s) semis[(size_t) s] = targetSemis[(size_t) s] + (semis[(size_t) s] - targetSemis[(size_t) s]) * glideCoef;

            float cl = 0, cr = 0, sendL = 0, sendR = 0;
            std::array<float, kSections> secPk {};
            for (auto& sg : singers)
            {
                if (sg.fade <= 0.0f && sg.targetOn <= 0.0f) continue;
                sg.fade += (sg.targetOn - sg.fade) * 0.0015f;
                if (sg.fade < 1e-4f && sg.targetOn <= 0.0f) { sg.fade = 0; sg.voice.reset(); sg.timing.clear(); continue; }
                sg.gL += (sg.tgtL - sg.gL) * rampInc * 4.0f; sg.gR += (sg.tgtR - sg.gR) * rampInc * 4.0f;
                const float cents = driftCents * sg.pitch.next();
                const float ratio = std::exp2 ((semis[(size_t) sg.section] + cents * 0.01f) / 12.0f);
                float y = sg.voice.render (history, latency, unvoicedT, ratio, sg.formant, hint);
                sg.timing.push (y);
                sg.curDelay += (sg.delaySamples - sg.curDelay) * 0.0004f;
                y = sg.air.lp (sg.timing.read (std::max (1.0f, sg.curDelay + timeWander * sg.time.next())));
                y *= sg.fade;
                const float l = y * sg.gL, r = y * sg.gR;
                cl += l; cr += r; sendL += l * sg.send; sendR += r * sg.send;
                secPk[(size_t) sg.section] = std::max (secPk[(size_t) sg.section], std::abs (y) * (sg.gL + sg.gR));
            }
            for (int s = 0; s < kSections; ++s) secEnv[(size_t) s] = std::max (secPk[(size_t) s], secEnv[(size_t) s] * 0.9997f);

            // sub-octave layer
            if (subLvl > 0.0f || subFade > 0.0f)
            {
                subFade += ((subLvl > 0.0f ? 1.0f : 0.0f) - subFade) * 0.002f;
                const float y = sub.render (history, latency, unvoicedT, 0.5f, 0.93f, hint) * subLvl * subFade;
                cl += y; cr += y; sendL += y * 0.3f; sendR += y * 0.3f;
            }

            // breath: air shaped by the lead's own breathiness and consonants
            if (breathAmt > 0.0f)
            {
                const float e = breathEnv.process (breathHp.hp (x));
                const float nl = breathBp[0].bp (noise.next(), sr) * e * breathAmt, nr = breathBp[1].bp (noise.next(), sr) * e * breathAmt;
                cl += nl; cr += nr; sendL += nl; sendR += nr;
            }

            // tone tilt and energy
            for (int c = 0; c < 2; ++c)
            {
                float& v = c ? cr : cl;
                const float lo = tiltLp[c].lp (v), hi = v - lo;
                v = lo * (1.0f - 0.4f * st.tone) + hi * (1.0f + 0.7f * st.tone);
                if (st.energy > 0.001f)
                {
                    const float k = 1.0f + 2.5f * st.energy;
                    v = space::softClip (v * k) / k * (1.0f + 0.5f * st.energy);
                    v += st.energy * 0.6f * presHp[c].hp (v);
                }
            }

            float rl = 0, rr = 0;
            if (revAmt > 0.0f) hall.process (sendL, sendR, rl, rr, st.space);
            const float d = dry.readInt (latency + 1) * lead;
            outL[i] = (d + (cl + rl * revAmt * 1.4f) * st.choirGain) * st.outGain;
            outR[i] = (d + (cr + rr * revAmt * 1.4f) * st.choirGain) * st.outGain;
        }
        for (int s = 0; s < kSections; ++s) { meter[(size_t) s] = secEnv[(size_t) s]; note[(size_t) s] = voiced && active[(size_t) s] > 0 ? snapped + (int) std::lround (targetSemis[(size_t) s]) : -1; }
        leadNote = voiced ? snapped : -1;
    }

    // UI
    std::array<std::atomic<float>, kSections> meter {};
    std::array<std::atomic<int>, kSections> note {};
    std::atomic<int> leadNote { -1 };
    float leadHz() const { return voiced ? sr / std::max (1.0f, period) : 0.0f; }

private:
    void onPitch()
    {
        voiced = tracker.voiced;
        if (! voiced) return;
        period = tracker.periodSamples();
        const float m = 69.0f + 12.0f * std::log2 (tracker.hz / 440.0f);
        if (snapped < 0 || std::abs (m - (float) snapped) > 0.62f) snapped = (int) std::lround (m);
    }

    void computeTargets (const Settings& st)
    {
        if (snapped < 0) return;
        if (st.midiChord && heldCount > 0)
        {
            std::array<int, 16> notes {}; for (int i = 0; i < heldCount; ++i) notes[(size_t) i] = held[i];
            std::sort (notes.begin(), notes.begin() + heldCount, std::greater<int>());
            const int pick[kSections] { 0, std::min (1, heldCount - 1), std::min (2, heldCount - 1), heldCount - 1 };
            for (int s = 0; s < kSections; ++s) targetSemis[(size_t) s] = (float) (notes[(size_t) pick[s]] - snapped);
            return;
        }
        const int h = std::clamp (st.harmony, 0, kNumHarmonies - 1);
        for (int s = 0; s < kSections; ++s)
            targetSemis[(size_t) s] = (float) (harmonise (snapped, kHarmonyDegrees[h][s], st.key, st.scale) - snapped);
    }

    struct Bp
    {
        space::Svf f;
        void clear() { f.clear(); }
        float bp (float x, float sr) { f.process (x, 6500.0f, 0.6f, sr); return f.bpo; }
    };
    struct SingerState : Singer { float targetOn = 0, tgtL = 0, tgtR = 0, formant = 1, delaySamples = 200, curDelay = 200; int section = 0; };

    static constexpr int kHint = 4096;
    float sr = 48000;
    int latency = 1920, unvoicedT = 384, hintDelay = 720;
    cl::PitchTracker tracker;
    InputHistory history;
    std::array<SingerState, kMaxSingers> singers;
    Voice sub; float subFade = 0;
    space::Delay dry;
    std::vector<float> hintRing;
    Hall hall;
    space::Rng noise;
    Bp breathBp[2];
    space::OnePole breathHp, tiltLp[2], presHp[2];
    space::Follower breathEnv, levelEnv;
    std::array<float, kSections> secEnv {}, semis {}, targetSemis {};
    float period = 0;
    int snapped = -1;
    bool voiced = false;
    int held[16] {}; int heldCount = 0;
    int64_t n = 0;
};
} // namespace sc
