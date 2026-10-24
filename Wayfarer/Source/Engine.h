#pragma once
#include "DSP.h"
#include "Params.h"
#include <vector>
#include <array>

// Wayfarer: a monophonic three-oscillator synth laid out like the classic American flagship mono:
// three continuously variable oscillators (osc 3 doubles as an LFO), 1-2 sync and 3-1 FM, a five-input mixer
// with overload, two ladder filters (stereo LP/LP with spacing, or HP/LP band-pass), two ADSRs, an LFO,
// two modulation busses (source x shaping -> destination) and an X/Y touch pad.
namespace wf
{
struct Settings
{
    int o1Oct = 2, o2Oct = 2, o3Oct = 2;
    float o1Wave = 0.333f, o2Wave = 0.333f, o3Wave = 0.333f, o2Freq = 0, o3Freq = 0;
    bool sync = false, o3KbOff = false, o3Lo = false;
    float fm = 0;
    float mix1 = 1, mix2 = 0, mix3 = 0, noise = 0, overload = 0;
    float cutoff = 4000, spacing = 0, res = 0, kb = 0.5f, envAmt = 0.4f, fVel = 0;
    int fMode = 0;
    float fA = 0.005f, fD = 0.4f, fS = 0.4f, fR = 0.3f;
    float aA = 0.003f, aD = 0.3f, aS = 1.0f, aR = 0.2f, aVel = 0;
    bool release = true;
    int gate = 0;
    float lfoHz = 4;
    int lfoSync = 0;
    double lfoPhaseAt = -1;     // tempo sync: LFO phase at the start of this block
    struct Bus { int src = 0, shape = 0, dest = 0; float amt = 0; } bus[2];
    float padX = 0, padY = 0, padXAmt = 0, padYAmt = 0;
    int padXDest = choices::dPitch, padYDest = choices::dFilter;
    bool padGate = false, padTouch = false;
    bool glideOn = false, legato = true;
    float glide = 0.1f;
    int priority = 2;
    float bendSemis = 0, tune = 0, drift = 0.2f, pressure = 0;
};

class Engine
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto* o : { &o1, &o2, &o3 }) o->phase = 0;
        o2.phase = 0.31f; o3.phase = 0.67f;
        lpL.reset(); lpR.reset(); hp1.reset(); hp2.reset();
        fenv.reset(); aenv.reset();
        held.clear();
        keyGate = sustainHeld = false;
        pitch = target = 48;
        lfoPhase = 0; sh = 0;
        pk0 = pk1 = pk2 = 0;
        mod.fill (0);
        smoothInit = false;
        lastOut = 0;
    }

    // ---- notes (monophonic, with low / high / last priority)
    void noteOn (int note, float vel, const Settings& s)
    {
        const bool wasHeld = ! held.empty();
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        held.push_back (note);
        const int chosen = pick (s.priority);
        if (wasHeld && chosen == target && keyGate) return;      // a lower / higher note than the one playing
        target = (float) chosen;
        if (! s.glideOn || ! everPlayed) pitch = target;
        everPlayed = true;
        if (! wasHeld || ! s.legato || ! keyGate)
        {
            velocity = vel;
            keyGate = true;
            if (s.gate == 0) { fenv.gateOn(); aenv.gateOn(); }
            if (s.lfoSync == 1) lfoPhase = 0;
        }
    }

    void noteOff (int note, const Settings& s)
    {
        held.erase (std::remove (held.begin(), held.end(), note), held.end());
        if (! held.empty()) { target = (float) pick (s.priority); if (! s.glideOn) pitch = target; return; }
        if (! sustainHeld) releaseGate();
    }

    void setSustain (bool down)
    {
        sustainHeld = down;
        if (! down && held.empty()) releaseGate();
    }

    void allNotesOff() { held.clear(); sustainHeld = false; releaseGate(); }
    bool active() const { return aenv.active(); }
    float currentPitch() const { return pitch; }
    float modValue (int dest) const { return mod[(size_t) dest]; }

    void render (float* L, float* R, int n, const Settings& s)
    {
        using namespace choices;
        const float invSr = 1.0f / sr;
        const float nyq = 0.45f * sr;
        fenv.set (s.fA, s.fD, s.fS, s.release ? s.fR : 0.012f, sr);
        aenv.set (s.aA, s.aD, s.aS, s.release ? s.aR : 0.012f, sr);
        const float glideCoef = s.glideOn && s.glide > 0.0005f ? 1.0f - std::exp (-1.0f / (s.glide * 0.35f * sr)) : 1.0f;
        const float smCoef = 1.0f - std::exp (-1.0f / (0.006f * sr));
        const float driftCoef = 1.0f - std::exp (-6.2831853f * 0.7f / sr);

        const float cutT = std::log2 (clampf (s.cutoff, 16.0f, 22000.0f));
        if (! smoothInit) { cutS = cutT; spaceS = s.spacing; padXS = s.padX; padYS = s.padY; smoothInit = true; }
        if (s.lfoPhaseAt >= 0) lfoPhase = (float) s.lfoPhaseAt;

        const float k = clampf (s.res, 0.0f, 1.0f) * 4.1f;
        const float hk1 = 0.765f * (1.0f - 0.8f * clampf (s.res, 0.0f, 1.0f)), hk2 = 1.848f;
        const float envDepth = s.envAmt * (1.0f - s.fVel + s.fVel * velocity) * 7.0f;
        const float ampVel = 1.0f - s.aVel + s.aVel * velocity;
        const float drive = 1.0f + s.overload * 9.0f;
        const float driveMakeup = 1.0f / (1.0f + s.overload * 1.6f);
        const float oct1 = (float) (s.o1Oct - 2) * 12.0f, oct2 = (float) (s.o2Oct - 2) * 12.0f, oct3 = (float) (s.o3Oct - 2) * 12.0f;
        const bool padGateOn = s.padGate && s.padTouch;

        for (int i = 0; i < n; ++i)
        {
            pitch += (target - pitch) * glideCoef;
            cutS += (cutT - cutS) * smCoef;
            spaceS += (s.spacing - spaceS) * smCoef;
            padXS += (s.padX - padXS) * smCoef;
            padYS += (s.padY - padYS) * smCoef;

            // analog drift: slow random wander of each oscillator
            if (--driftTimer <= 0)
            {
                for (auto& d : driftTarget) d = rng.next();
                driftTimer = (int) (sr * (0.3f + 0.3f * (rng.next() + 1.0f)));
            }
            for (int d = 0; d < 3; ++d) driftNow[(size_t) d] += (driftTarget[(size_t) d] - driftNow[(size_t) d]) * driftCoef;

            // ---- LFO (triangle, square, sample & hold)
            const float rateMul = std::exp2 (clampf (mod[dLfoRate], -1.0f, 1.0f) * 4.0f);
            lfoPhase += s.lfoHz * rateMul * invSr;
            bool lfoWrap = false;
            if (lfoPhase >= 1.0f) { lfoPhase -= std::floor (lfoPhase); lfoWrap = true; sh = rng.next(); }
            const float tri = 1.0f - 4.0f * std::fabs (lfoPhase - 0.5f);
            const float sqr = lfoPhase < 0.5f ? 1.0f : -1.0f;
            shS += (sh - shS) * 0.02f;    // a touch of slew so S&H steps don't click

            // ---- gate
            const bool gateNow = s.gate == 2 ? true : s.gate == 1 ? sqr > 0.0f : (keyGate || padGateOn);
            if (gateNow && ! gateWas) { fenv.gateOn(); aenv.gateOn(); }
            if (! gateNow && gateWas) { fenv.gateOff(); aenv.gateOff(); }
            if (s.gate == 1 && lfoWrap && gateNow) { fenv.gateOn(); aenv.gateOn(); }
            gateWas = gateNow;
            const float fe = fenv.process();
            const float ae = aenv.process();

            // ---- noise (pinkish)
            const float w = rng.next();
            pk0 = 0.99765f * pk0 + w * 0.0990460f;
            pk1 = 0.96300f * pk1 + w * 0.2965164f;
            pk2 = 0.57000f * pk2 + w * 1.0526913f;
            const float pink = (pk0 + pk1 + pk2 + w * 0.1848f) * 0.22f;

            // ---- modulation busses + touch pad
            const float src[8] { tri, sqr, shS, lastOsc3, w, fe, ae, s.pressure };
            mod.fill (0.0f);
            for (auto& b : s.bus)
            {
                if (b.amt <= 0.0f) continue;
                const float shp = b.shape == shOn ? 1.0f : b.shape == shFEnv ? fe : b.shape == shVel ? velocity : s.pressure;
                mod[(size_t) juce::jlimit (0, (int) kNumDests - 1, b.dest)] += src[juce::jlimit (0, 7, b.src)] * shp * b.amt;
            }
            mod[(size_t) juce::jlimit (0, (int) kNumDests - 1, s.padXDest)] += padXS * s.padXAmt;
            mod[(size_t) juce::jlimit (0, (int) kNumDests - 1, s.padYDest)] += padYS * s.padYAmt;

            const float waveMod = mod[dWave] * 0.5f;
            const float basePitch = pitch + s.bendSemis + s.tune;
            const float modPitch = mod[dPitch] * 24.0f;
            const float dr = s.drift * 0.12f;

            // ---- oscillator 3 (also a modulation source)
            float n3 = (s.o3KbOff ? 48.0f + s.tune : basePitch) + oct3 + s.o3Freq + modPitch + mod[dOsc3] * 24.0f + driftNow[2] * dr;
            if (s.o3Lo) n3 -= 84.0f;
            const float dt3 = clampf (midiToHz (n3) * invSr, 0.0f, 0.45f);
            const float w3 = render (o3, dt3, clampf (s.o3Wave + waveMod, 0.0f, 1.0f));
            float f3; o3.advance (dt3, f3);
            lastOsc3 = w3;

            // ---- oscillator 1 (sync master, FM from osc 3)
            const float n1 = basePitch + oct1 + modPitch + driftNow[0] * dr;
            float dt1 = midiToHz (n1) * invSr * (1.0f + s.fm * 1.6f * w3);
            dt1 = clampf (dt1, 0.0f, 0.45f);
            const float w1 = render (o1, dt1, clampf (s.o1Wave + waveMod, 0.0f, 1.0f));
            float f1 = 0; const bool wrapped1 = o1.advance (dt1, f1);

            // ---- oscillator 2 (synced to osc 1)
            const float n2 = basePitch + oct2 + s.o2Freq + modPitch + mod[dOsc2] * 24.0f + driftNow[1] * dr;
            const float dt2 = clampf (midiToHz (n2) * invSr, 0.0f, 0.45f);
            const float w2 = render (o2, dt2, clampf (s.o2Wave + waveMod, 0.0f, 1.0f));
            float f2; o2.advance (dt2, f2);
            if (s.sync && wrapped1) o2.phase = juce::jmin (0.999f, f1 * dt2);

            // ---- mixer with overload
            float mx = (w1 * s.mix1 + w2 * s.mix2 + w3 * s.mix3 + pink * s.noise) * 0.5f + w * 1.0e-5f;   // circuit hiss lets the filter self-oscillate
            if (s.overload > 0.001f) mx = fastTanh (mx * drive) * driveMakeup * (1.0f + s.overload * 0.6f);

            // ---- filters
            const float key = (pitch - 60.0f) * (1.0f / 12.0f);
            const float cut = cutS + fe * envDepth + s.kb * key + mod[dFilter] * 5.0f;
            const float space = clampf (spaceS + mod[dSpacing] * 3.0f, -4.0f, 4.0f);
            float yl, yr;
            if (s.fMode == 0)
            {
                // dual low-pass: left at the cutoff, right offset by spacing (stereo when spacing isn't zero)
                const float fl = clampf (std::exp2 (cut), 10.0f, nyq);
                const float fr = clampf (std::exp2 (cut + space), 10.0f, nyq);
                yl = lpL.process (mx, std::tan (kPi * fl * invSr), k);
                yr = std::fabs (space) < 0.001f && std::fabs (spaceS) < 0.001f ? yl : lpR.process (mx, std::tan (kPi * fr * invSr), k);
            }
            else
            {
                // high-pass into low-pass: spacing sets how far above the high-pass the low-pass sits (band-pass)
                const float fh = clampf (std::exp2 (cut), 10.0f, nyq);
                const float fl = clampf (std::exp2 (cut + space), 10.0f, nyq);
                const float gh = std::tan (kPi * fh * invSr);
                float h = hp2.process (hp1.process (mx, gh, hk1), gh, hk2);
                yl = yr = lpL.process (h, std::tan (kPi * fl * invSr), k * 0.6f);
            }

            float amp = ae * ampVel;
            if (mod[dAmp] != 0.0f) amp *= clampf (1.0f + mod[dAmp], 0.0f, 1.6f);
            L[i] = yl * amp * 0.8f;
            R[i] = yr * amp * 0.8f;
        }
    }

private:
    static float render (const MorphOsc& o, float dt, float shape)
    {
        // 0 triangle -> 1/3 saw -> 2/3 square -> 1 narrow pulse
        if (shape < 0.3333f) return o.render (dt, shape * 1.5f, 0.5f);
        if (shape < 0.6667f) return o.render (dt, 0.5f + (shape - 0.3333f) * 1.5f, 0.5f);
        return o.render (dt, 1.0f, 0.5f - (shape - 0.6667f) * 3.0f * 0.44f);
    }

    int pick (int priority) const
    {
        if (held.empty()) return (int) target;
        if (priority == 0) return *std::min_element (held.begin(), held.end());
        if (priority == 1) return *std::max_element (held.begin(), held.end());
        return held.back();
    }

    void releaseGate() { keyGate = false; }

    float sr = 96000;
    MorphOsc o1, o2, o3;
    Ladder lpL, lpR;
    SvfHP hp1, hp2;
    Adsr fenv, aenv;
    Rng rng;
    std::vector<int> held;
    bool keyGate = false, sustainHeld = false, gateWas = false, everPlayed = false, smoothInit = false;
    float pitch = 48, target = 48, velocity = 1;
    float lfoPhase = 0, sh = 0, shS = 0, lastOsc3 = 0, lastOut = 0;
    float pk0 = 0, pk1 = 0, pk2 = 0;
    float cutS = 12, spaceS = 0, padXS = 0, padYS = 0;
    std::array<float, 3> driftNow {}, driftTarget {};
    int driftTimer = 0;
    std::array<float, choices::kNumDests> mod {};
};
} // namespace wf
