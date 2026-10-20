#pragma once
#include "DSP.h"
#include <array>
#include <vector>

namespace augur
{

// Snapshot of all sound parameters, refreshed once per audio block
struct Settings
{
    float osc1Freq = 0, osc1Shape = 0.5f, osc1PW = 0.5f, osc1Sub = 0; bool osc1Sync = false;
    float osc2Freq = 0, osc2Fine = 0, osc2Shape = 0.5f, osc2PW = 0.5f; bool osc2Low = false, osc2Key = true;
    float mixOsc1 = 0.8f, mixOsc2 = 0, mixNoise = 0, slop = 0.25f;

    float pmFiltEnv = 0, pmOsc2 = 0; bool pmFreq1 = false, pmShape1 = false, pmPW1 = false, pmLPF = false, pmHPF = false;

    float lfoDepth = 0;   // amount + mod wheel contribution, 0..1
    bool lfoFreq1 = false, lfoFreq2 = false, lfoPW = false, lfoLPF = false, lfoHPF = false, lfoAmp = false;

    float lpfCutoff = 8000, lpfRes = 0, lpfEnv = 0, lpfVel = 0, lpfKey = 0.5f, lpfAT = 0;
    float hpfCutoff = 20, hpfRes = 0, hpfEnv = 0, hpfVel = 0, hpfKey = 0;

    float fA = 0.005f, fD = 0.4f, fS = 0.5f, fR = 0.3f;
    float aA = 0.003f, aD = 0.3f, aS = 1.0f, aR = 0.25f, aVel = 0.3f;

    float glide = 0; bool unison = false; int uniVoices = 4; float uniDetune = 0.25f, panSpread = 0.3f;

    // performance controllers
    float bendSemis = 0, aftertouch = 0;
};

class Voice
{
public:
    int note = -1;
    bool gate = false;        // key physically held
    bool sustained = false;   // held by sustain pedal after key-up
    uint64_t age = 0;
    float detune = 0.0f;      // unison spread offset, semitones
    float pan = 0.0f;         // -1..1

    void prepare (float sampleRate, uint32_t seed)
    {
        sr = sampleRate;
        rng.seed (seed);
        // fixed per-voice component tolerances, scaled by the Slop control
        for (auto& t : tolerance) t = rng.next();
        o1.phase = (rng.next() + 1.0f) * 0.5f;
        o2.phase = (rng.next() + 1.0f) * 0.5f;
        reset();
    }

    void reset()
    {
        lp.reset(); hp.reset(); fenv.reset(); aenv.reset();
        note = -1; gate = sustained = false;
    }

    bool isActive() const { return aenv.active(); }
    bool isReleasing() const { return aenv.stage == Adsr::Release; }
    float level() const { return aenv.value; }

    void start (int midiNote, float vel, float fromPitch, uint64_t stamp)
    {
        note = midiNote;
        velocity = vel;
        gate = true;
        sustained = false;
        age = stamp;
        targetPitch = (float) midiNote;
        pitch = fromPitch;
        if (! aenv.active())
        {
            lp.reset(); hp.reset();
        }
        fenv.gateOn();
        aenv.gateOn();
    }

    // legato pitch change (unison note-stack) without retriggering envelopes
    void glideTo (int midiNote, uint64_t stamp)
    {
        note = midiNote;
        targetPitch = (float) midiNote;
        gate = true;
        age = stamp;
    }

    void release()
    {
        gate = false;
        sustained = false;
        fenv.gateOff();
        aenv.gateOff();
    }

    float currentPitch() const { return pitch; }

    void render (float* outL, float* outR, int numSamples, const Settings& p, const float* lfo, float gain)
    {
        if (! aenv.active())
            return;

        fenv.set (p.fA, p.fD, p.fS, p.fR, sr);
        aenv.set (p.aA, p.aD, p.aS, p.aR, sr);

        const float glideCoef = p.glide > 0.0005f ? 1.0f - std::exp (-1.0f / (p.glide * 0.35f * sr)) : 1.0f;
        const float invSr = 1.0f / sr;
        const float nyq = 0.42f * sr;

        const float slopAmt = p.slop;
        const float cutLog = std::log2 (p.lpfCutoff) + tolerance[2] * slopAmt * 0.12f;
        const float hpLog  = std::log2 (p.hpfCutoff) + tolerance[3] * slopAmt * 0.12f;
        const float lpEnvDepth = p.lpfEnv * (1.0f - p.lpfVel + p.lpfVel * velocity) * 9.0f;
        const float hpEnvDepth = p.hpfEnv * (1.0f - p.hpfVel + p.hpfVel * velocity) * 9.0f;
        const float k = clampf (p.lpfRes, 0.0f, 1.0f) * 4.15f;
        const float kh = 2.0f - 1.88f * clampf (p.hpfRes, 0.0f, 1.0f);
        const float ampVel = 1.0f - p.aVel + p.aVel * velocity;
        const float lfoPitch = p.lfoDepth * p.lfoDepth * 12.0f;       // squared so low settings give gentle vibrato
        const float lfoFilt = p.lfoDepth * 4.0f;                       // octaves
        const float atOct = p.aftertouch * p.lpfAT * 3.0f;
        const float tol0 = tolerance[0] * slopAmt * 0.08f;              // static detune, semitones
        const float tol1 = tolerance[1] * slopAmt * 0.08f;
        const float panL = std::sqrt (0.5f * (1.0f - pan));
        const float panR = std::sqrt (0.5f * (1.0f + pan));
        const float driftCoef = 1.0f - std::exp (-kTwoPiF * 1.5f / sr);

        for (int i = 0; i < numSamples; ++i)
        {
            pitch += (targetPitch - pitch) * glideCoef;

            // slow random pitch drift ("slop")
            if (--driftTimer <= 0)
            {
                driftTarget1 = rng.next();
                driftTarget2 = rng.next();
                driftTimer = (int) (sr * (0.25f + 0.25f * (rng.next() + 1.0f)));
            }
            drift1 += (driftTarget1 - drift1) * driftCoef;
            drift2 += (driftTarget2 - drift2) * driftCoef;

            const float fe = fenv.process();
            const float ae = aenv.process();
            const float l = lfo[i] * p.lfoDepth;
            const float lfoP = lfo[i] * lfoPitch;

            // ---- Oscillator 2 (poly-mod source and sync master)
            float n2 = p.osc2Key ? pitch + detune : 48.0f;
            n2 += p.osc2Freq + p.osc2Fine * 0.01f + p.bendSemis + tol1 + drift2 * slopAmt * 0.1f;
            if (p.lfoFreq2) n2 += lfoP;
            if (p.osc2Low) n2 -= 84.0f;
            const float dt2 = clampf (midiToHz (n2) * invSr, 0.0f, 0.45f);
            const float pw2 = clampf (p.osc2PW + (p.lfoPW ? l * 0.4f : 0.0f), 0.03f, 0.97f);
            const float s2 = o2.render (dt2, p.osc2Shape, pw2);
            float frac = 0.0f;
            const bool wrapped = o2.advance (dt2, frac);

            // ---- Poly mod
            const float pm = fe * p.pmFiltEnv + s2 * p.pmOsc2;

            // ---- Oscillator 1
            float n1 = pitch + detune + p.osc1Freq + p.bendSemis + tol0 + drift1 * slopAmt * 0.1f;
            if (p.lfoFreq1) n1 += lfoP;
            if (p.pmFreq1)  n1 += pm * 36.0f;
            const float dt1 = clampf (midiToHz (n1) * invSr, 0.0f, 0.45f);
            const float shape1 = p.pmShape1 ? clampf (p.osc1Shape + pm * 0.5f, 0.0f, 1.0f) : p.osc1Shape;
            float pw1 = p.osc1PW;
            if (p.lfoPW)  pw1 += l * 0.4f;
            if (p.pmPW1)  pw1 += pm * 0.45f;
            pw1 = clampf (pw1, 0.03f, 0.97f);
            const float s1 = o1.render (dt1, shape1, pw1);
            float frac1 = 0.0f;
            o1.advance (dt1, frac1);
            if (p.osc1Sync && wrapped)
                o1.phase = frac * dt1;

            const float sub = p.osc1Sub > 0.0f ? sub1.tick (dt1 * 0.5f) : 0.0f;

            float mix = s1 * p.mixOsc1 + sub * p.osc1Sub + s2 * p.mixOsc2 + rng.next() * p.mixNoise;
            mix *= 0.55f;

            // ---- Low-pass ladder
            const float key = pitch - 60.0f;
            float cut = cutLog + fe * lpEnvDepth + p.lpfKey * key * (1.0f / 12.0f) + atOct;
            if (p.lfoLPF) cut += lfo[i] * lfoFilt;
            if (p.pmLPF)  cut += pm * 5.0f;
            const float fc = clampf (std::exp2 (cut), 12.0f, nyq);
            const float g = std::tan (kPi * fc * invSr);
            float y = lp.process (mix, g, k);

            // ---- High-pass SVF
            float hcut = hpLog + fe * hpEnvDepth + p.hpfKey * key * (1.0f / 12.0f);
            if (p.lfoHPF) hcut += lfo[i] * lfoFilt;
            if (p.pmHPF)  hcut += pm * 5.0f;
            const float fh = clampf (std::exp2 (hcut), 12.0f, nyq);
            if (fh > 14.0f || p.hpfRes > 0.0f)
                y = hp.process (y, std::tan (kPi * fh * invSr), kh);

            float amp = ae * ampVel;
            if (p.lfoAmp) amp *= 1.0f - p.lfoDepth * 0.5f * (1.0f + lfo[i]);

            const float out = y * amp * gain;
            outL[i] += out * panL;
            outR[i] += out * panR;
        }

        if (! aenv.active())
        {
            note = -1;
            gate = sustained = false;
        }
    }

private:
    static constexpr float kTwoPiF = 6.28318530718f;
    float sr = 96000.0f;
    float velocity = 1.0f;
    float pitch = 60.0f, targetPitch = 60.0f;
    float drift1 = 0, drift2 = 0, driftTarget1 = 0, driftTarget2 = 0;
    int driftTimer = 0;
    float tolerance[4] {};
    MorphOsc o1, o2;
    SubOsc sub1;
    Ladder lp;
    SvfHP hp;
    Adsr fenv, aenv;
    Rng rng;
};

class Engine
{
public:
    static constexpr int kNumVoices = 6;

    void prepare (float sampleRate)
    {
        sr = sampleRate;
        for (int i = 0; i < kNumVoices; ++i)
            voices[(size_t) i].prepare (sr, 1234567u + 7919u * (uint32_t) i);
        noteStack.clear();
        noteStack.reserve (128);
        lastPitch = 60.0f;
        sustainDown = false;
    }

    void allNotesOff()
    {
        for (auto& v : voices) v.release();
        noteStack.clear();
    }

    void killAll()
    {
        for (auto& v : voices) v.reset();
        noteStack.clear();
    }

    void setUnison (bool on, int numVoices)
    {
        numVoices = std::max (1, std::min (kNumVoices, numVoices));
        if (on != unison || numVoices != uniCount)
        {
            allNotesOff();
            unison = on;
            uniCount = numVoices;
        }
    }

    void setSustain (bool down)
    {
        sustainDown = down;
        if (! down)
            for (auto& v : voices)
                if (v.sustained) v.release();
    }

    void noteOn (int note, float vel, const Settings& p)
    {
        ++clock;
        const float from = p.glide > 0.0005f ? lastPitch : (float) note;

        if (unison)
        {
            noteStack.erase (std::remove (noteStack.begin(), noteStack.end(), note), noteStack.end());
            noteStack.push_back (note);
            for (int i = 0; i < uniCount; ++i)
            {
                auto& v = voices[(size_t) i];
                v.detune = uniCount > 1 ? ((float) i / (float) (uniCount - 1) - 0.5f) * 2.0f * p.uniDetune * 0.5f : 0.0f;
                v.pan = uniCount > 1 ? ((float) i / (float) (uniCount - 1) - 0.5f) * 2.0f * p.panSpread : 0.0f;
                v.start (note, vel, v.isActive() ? v.currentPitch() : from, clock);
            }
        }
        else
        {
            Voice* target = nullptr;
            for (auto& v : voices)                          // same note re-struck: reuse its voice
                if (v.note == note && v.isActive()) { target = &v; break; }

            if (target == nullptr)                          // free voice, rotating so voices share the work
                for (int i = 0; i < kNumVoices && target == nullptr; ++i)
                {
                    auto& v = voices[(size_t) ((nextVoice + i) % kNumVoices)];
                    if (! v.isActive()) { target = &v; nextVoice = (nextVoice + i + 1) % kNumVoices; }
                }

            if (target == nullptr)                          // steal the quietest released voice
            {
                float best = 2.0f;
                for (auto& v : voices)
                    if (v.isReleasing() && v.level() < best) { best = v.level(); target = &v; }
            }

            if (target == nullptr)                          // steal the oldest held voice
            {
                uint64_t oldest = UINT64_MAX;
                for (auto& v : voices)
                    if (v.age < oldest) { oldest = v.age; target = &v; }
            }

            const int idx = (int) (target - voices.data());
            static const float panPos[kNumVoices] { -1.0f, 1.0f, -0.6f, 0.6f, -0.2f, 0.2f };
            target->detune = 0.0f;
            target->pan = panPos[idx] * p.panSpread;
            target->start (note, vel, from, clock);
        }
        lastPitch = (float) note;
    }

    void noteOff (int note)
    {
        if (unison)
        {
            noteStack.erase (std::remove (noteStack.begin(), noteStack.end(), note), noteStack.end());
            if (! noteStack.empty())
            {
                const int prev = noteStack.back();
                ++clock;
                for (int i = 0; i < uniCount; ++i)
                    voices[(size_t) i].glideTo (prev, clock);
                lastPitch = (float) prev;
                return;
            }
            for (int i = 0; i < uniCount; ++i)
            {
                auto& v = voices[(size_t) i];
                if (sustainDown) { v.gate = false; v.sustained = true; }
                else v.release();
            }
            return;
        }

        for (auto& v : voices)
            if (v.note == note && v.gate)
            {
                if (sustainDown) { v.gate = false; v.sustained = true; }
                else v.release();
            }
    }

    void render (float* L, float* R, int n, const Settings& p, const float* lfo)
    {
        const float gain = unison ? 0.62f / std::sqrt ((float) uniCount) : 0.42f;
        for (auto& v : voices)
            v.render (L, R, n, p, lfo, gain);
    }

    int activeVoices() const
    {
        int c = 0;
        for (auto& v : voices) c += v.isActive() ? 1 : 0;
        return c;
    }

private:
    std::array<Voice, kNumVoices> voices;
    std::vector<int> noteStack;
    float sr = 96000.0f;
    float lastPitch = 60.0f;
    bool unison = false, sustainDown = false;
    int uniCount = 4, nextVoice = 0;
    uint64_t clock = 0;
};

// Global LFO
struct Lfo
{
    float phase = 0.0f, held = 0.0f, smooth = 0.0f;
    Rng rng;

    void render (float* out, int n, float hz, int shape, float sr)
    {
        const float dt = hz / sr;
        const float sCoef = 1.0f - std::exp (-6.2831853f * 120.0f / sr);
        for (int i = 0; i < n; ++i)
        {
            float v = 0.0f;
            switch (shape)
            {
                case 0:  v = 1.0f - 4.0f * std::fabs (phase - 0.5f); v = -v; break;     // triangle
                case 1:  v = 2.0f * phase - 1.0f; break;                               // saw up
                case 2:  v = 1.0f - 2.0f * phase; break;                               // saw down
                case 3:  v = phase < 0.5f ? 1.0f : -1.0f; break;                       // square
                default: v = held; break;                                              // sample & hold
            }
            smooth += (v - smooth) * sCoef;   // de-click edges
            out[i] = smooth;
            phase += dt;
            if (phase >= 1.0f)
            {
                phase -= std::floor (phase);
                held = rng.next();
            }
        }
    }
};

} // namespace augur
