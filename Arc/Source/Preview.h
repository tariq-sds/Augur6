#pragma once
#include "Arranger.h"
#include <memory>

// A small built-in sound set so you can hear the arrangement in time with Live before exporting.
namespace arc
{
class Preview
{
public:
    void prepare (double sampleRate) { sr = (float) sampleRate; for (auto& v : voices) v.active = false; rng.setSeed (5); }
    void stopAll() { for (auto& v : voices) v.active = false; }

    void render (float* L, float* R, int n, const Arrangement& a, double ppq, double bpm, bool playing, float gain)
    {
        if (! playing) { lastPpq = -1; return; }
        const double bps = bpm / 60.0;
        const double end = ppq + n * bps / sr;
        if (lastPpq < 0 || std::abs (ppq - lastPpq) > 0.05) stopAll();
        lastPpq = end;
        for (int l = 0; l < kNumLanes; ++l)
        {
            const auto& notes = a.lanes[(size_t) l].notes;
            auto it = std::lower_bound (notes.begin(), notes.end(), ppq, [] (const Note& x, double t) { return x.start < t; });
            for (; it != notes.end() && it->start < end; ++it)
            {
                const int off = juce::jlimit (0, n - 1, (int) ((it->start - ppq) / bps * sr));
                trigger (l, *it, off, (float) (it->length / bps));
            }
        }
        for (auto& v : voices) if (v.active) renderVoice (v, L, R, n, gain);
    }

private:
    struct Voice
    {
        bool active = false;
        int lane = 0, pitch = 60, delay = 0;
        float vel = 1, t = 0, dur = 0.2f, phase = 0, phase2 = 0, lp = 0, hp = 0, env = 0;
        float pan = 0;
    };

    void trigger (int lane, const Note& nt, int offset, float durSec)
    {
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) { v = &voices[0]; for (auto& c : voices) if (c.t > v->t) v = &c; }
        *v = Voice();
        v->active = true; v->lane = lane; v->pitch = nt.pitch; v->vel = nt.velocity; v->delay = offset; v->dur = durSec;
        v->pan = lane == laneDrums ? (isHat (nt.pitch) ? 0.25f : 0.0f) : (lane == laneChords ? (rng.nextFloat() - 0.5f) * 0.6f : 0.0f);
    }

    float noise() { return rng.nextFloat() * 2 - 1; }

    void renderVoice (Voice& v, float* L, float* R, int n, float gain)
    {
        const float dt = 1.0f / sr;
        for (int i = 0; i < n; ++i)
        {
            if (v.delay > 0) { --v.delay; continue; }
            float s = 0; bool done = false;
            if (v.lane == laneDrums)
            {
                const int p = v.pitch;
                if (isKick (p)) { const float f = 48 + 120 * std::exp (-v.t / 0.03f); v.phase += f * dt; s = std::sin (6.2831853f * v.phase) * std::exp (-v.t / 0.25f); done = v.t > 0.9f; }
                else if (p == gm::clap) { const float burst = std::fmod (v.t, 0.011f) < 0.006f && v.t < 0.035f ? 1.0f : 0.0f; v.hp += (noise() - v.hp) * 0.35f; s = (noise() - v.hp) * (std::exp (-v.t / 0.12f) * 0.6f + burst * 0.5f) * 0.8f; done = v.t > 0.5f; }
                else if (isSnare (p)) { v.phase += 190 * dt; s = (std::sin (6.2831853f * v.phase) * 0.5f * std::exp (-v.t / 0.05f) + noise() * 0.6f * std::exp (-v.t / 0.12f)); done = v.t > 0.5f; }
                else if (p == gm::crash || p == gm::ride) { v.hp += (noise() - v.hp) * 0.6f; s = (noise() - v.hp) * 0.4f * std::exp (-v.t / 1.2f); done = v.t > 3.0f; }
                else if (isHat (p)) { v.hp += (noise() - v.hp) * 0.75f; const float dec = p == gm::openHat ? 0.22f : 0.035f; s = (noise() - v.hp) * 0.5f * std::exp (-v.t / dec); done = v.t > dec * 6; }
                else { const float f = 100 + (p - 41) * 18 + 60 * std::exp (-v.t / 0.04f); v.phase += f * dt; s = std::sin (6.2831853f * v.phase) * std::exp (-v.t / 0.18f) * 0.8f; done = v.t > 0.8f; }
                s *= v.vel;
            }
            else
            {
                const float f = 440.0f * std::exp2 ((v.pitch - 69) / 12.0f);
                v.phase += f * dt; if (v.phase >= 1) v.phase -= 1;
                v.phase2 += f * 1.006f * dt; if (v.phase2 >= 1) v.phase2 -= 1;
                const float atk = v.lane == laneChords ? 0.01f : 0.003f;
                const float rel = v.lane == laneChords ? 0.25f : 0.06f;
                float env = v.t < atk ? v.t / atk : 1.0f;
                if (v.t > v.dur) env *= std::exp (-(v.t - v.dur) / rel);
                done = v.t > v.dur + rel * 6;
                float osc;
                float cutoff;
                if (v.lane == laneBass) { osc = (2 * v.phase - 1); cutoff = 0.06f; env *= v.t < v.dur ? 1.0f : 1.0f; }
                else if (v.lane == laneChords) { osc = (2 * v.phase - 1) * 0.5f + (2 * v.phase2 - 1) * 0.5f; cutoff = 0.08f; }
                else { osc = v.phase < 0.5f ? 0.7f : -0.7f; cutoff = 0.2f; }
                v.lp += (osc - v.lp) * cutoff;
                s = v.lp * env * v.vel * (v.lane == laneChords ? 0.22f : 0.45f);
            }
            const float g = gain * 0.6f;
            L[i] += s * g * (1.0f - v.pan);
            R[i] += s * g * (1.0f + v.pan);
            v.t += dt;
            if (done) { v.active = false; return; }
        }
    }

    float sr = 48000;
    std::array<Voice, 40> voices;
    juce::Random rng;
    double lastPpq = -1;
};
} // namespace arc
