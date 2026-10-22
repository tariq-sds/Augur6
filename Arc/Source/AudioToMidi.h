#pragma once
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Model.h"

// Offline audio -> MIDI for loops: drums (onsets + classification), mono (pitch tracking) and
// poly (harmonic salience with iterative note picking), all quantised to the host tempo grid.
namespace arc
{
struct AudioLoop { std::vector<float> mono; double sr = 48000; bool ok = false; juce::String name; };

inline AudioLoop loadAudio (const juce::File& f)
{
    AudioLoop a;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples < 1000) return a;
    const int n = (int) juce::jmin (r->lengthInSamples, (juce::int64) (r->sampleRate * 120));
    juce::AudioBuffer<float> b ((int) juce::jmax (1u, r->numChannels), n);
    r->read (&b, 0, n, 0, true, true);
    a.mono.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float s = 0;
        for (int c = 0; c < b.getNumChannels(); ++c) s += b.getSample (c, i);
        a.mono[(size_t) i] = s / (float) b.getNumChannels();
    }
    a.sr = r->sampleRate;
    a.name = f.getFileNameWithoutExtension();
    a.ok = true;
    return a;
}

// How many beats the audio spans, and the tempo it was played at.
struct LoopTiming { double beats = 16, tempo = 120; int bars = 4; };

inline LoopTiming timingFor (double seconds, double hostBpm)
{
    LoopTiming t;
    const double beatsAtHost = seconds * hostBpm / 60.0;
    for (int bars : { 1, 2, 4, 8, 16, 32 })
        if (std::abs (beatsAtHost - 4.0 * bars) / (4.0 * bars) < 0.04)
        {
            t.bars = bars; t.beats = 4.0 * bars; t.tempo = 4.0 * bars * 60.0 / seconds;
            return t;
        }
    // not warped to the project: find the bar count whose tempo is nearest the host's
    double best = 1e9;
    for (int bars : { 1, 2, 4, 8, 16, 32 })
    {
        const double tempo = 4.0 * bars * 60.0 / seconds;
        if (tempo < 70 || tempo > 180) continue;
        if (std::abs (tempo - hostBpm) < best) { best = std::abs (tempo - hostBpm); t.bars = bars; t.tempo = tempo; t.beats = 4.0 * bars; }
    }
    if (best > 1e8) { t.beats = std::max (1.0, std::round (beatsAtHost)); t.tempo = hostBpm; t.bars = (int) std::max (1.0, t.beats / 4); }
    return t;
}

inline double quantise (double beats, double grid) { return std::round (beats / grid) * grid; }

// ---------------------------------------------------------------- STFT helper
struct Stft
{
    int N, hop;
    std::vector<std::vector<float>> mag;   // [frame][bin]
    double sr;
    Stft (const std::vector<float>& x, double sampleRate, int order, int hopSize) : N (1 << order), hop (hopSize), sr (sampleRate)
    {
        juce::dsp::FFT fft (order);
        std::vector<float> win ((size_t) N), work ((size_t) N * 2);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2 * juce::MathConstants<float>::pi * i / (N - 1));
        for (int start = -N / 2; start + N / 2 < (int) x.size(); start += hop)
        {
            std::fill (work.begin(), work.end(), 0.0f);
            for (int i = 0; i < N; ++i)
            {
                const int k = start + i;
                work[(size_t) i] = (k >= 0 && k < (int) x.size()) ? x[(size_t) k] * win[(size_t) i] : 0.0f;
            }
            fft.performFrequencyOnlyForwardTransform (work.data(), true);
            std::vector<float> m ((size_t) N / 2 + 1);
            for (int b = 0; b <= N / 2; ++b) m[(size_t) b] = work[(size_t) b] * (2.0f / N);
            mag.push_back (std::move (m));
        }
    }
    double frameTime (int f) const { return (double) f * hop / sr; }     // centre of frame f
    float hzPerBin() const { return (float) (sr / N); }
};

// ---------------------------------------------------------------- drums
inline Clip drumsToMidi (const AudioLoop& a, const LoopTiming& tm, juce::String* report = nullptr)
{
    Clip c; c.lengthBeats = tm.beats;
    Stft s (a.mono, a.sr, 10, 256);
    const int F = (int) s.mag.size();
    if (F < 8) return c;
    const float hz = s.hzPerBin();
    auto band = [&] (int f, float lo, float hi)
    {
        double e = 0;
        for (int b = juce::jmax (1, (int) (lo / hz)); b <= juce::jmin ((int) s.mag[0].size() - 1, (int) (hi / hz)); ++b) e += (double) s.mag[(size_t) f][(size_t) b] * s.mag[(size_t) f][(size_t) b];
        return (float) e;
    };
    struct BandDef { float lo, hi; int note; float rel; };
    const BandDef defs[3] { { 30, 150, gm::kick, 0.30f }, { 1000, 4500, gm::snare, 0.38f }, { 7000, 16000, gm::closedHat, 0.25f } };
    const double secPerBeat = 60.0 / tm.tempo;
    const double grid = 0.25;
    std::vector<std::vector<float>> energy (3, std::vector<float> ((size_t) F));
    for (int f = 0; f < F; ++f) for (int k = 0; k < 3; ++k) energy[(size_t) k][(size_t) f] = band (f, defs[k].lo, defs[k].hi);

    std::vector<std::pair<double, int>> lowHits;   // to suppress a kick's click being read as a snare
    int counts[3] {};
    for (int k = 0; k < 3; ++k)
    {
        auto& e = energy[(size_t) k];
        std::vector<float> flux ((size_t) F, 0.0f);
        for (int f = 1; f < F; ++f)
            flux[(size_t) f] = juce::jmax (0.0f, std::log10 (e[(size_t) f] + 1e-10f) - std::log10 (e[(size_t) f - 1] + 1e-10f)) * std::sqrt (e[(size_t) f]);
        float mx = 0; for (float v : flux) mx = juce::jmax (mx, v);
        if (mx <= 0) continue;
        const int minGap = (int) (0.06 * a.sr / s.hop);
        int last = -1000;
        for (int f = 1; f < F - 2; ++f)
        {
            const float v = flux[(size_t) f];
            if (v < defs[k].rel * mx || v < flux[(size_t) f - 1] || v < flux[(size_t) f + 1] || f - last < minGap) continue;
            last = f;
            const double t = s.frameTime (f) - 0.25 * (double) s.N / a.sr;   // frame centre leads the attack
            double beat = quantise (juce::jmax (0.0, t) / secPerBeat, grid);
            if (beat >= tm.beats - 1e-6) beat -= tm.beats;
            if (beat < 0) beat = 0;
            if (k == 0) lowHits.push_back ({ beat, f });
            if (k == 1)
            {
                // a kick's click: coincides with a kick and is weak compared to the loudest snare
                bool kickHere = false;
                for (auto& h : lowHits) if (std::abs (h.first - beat) < 1e-6) kickHere = true;
                if (kickHere && v < 0.7f * mx) continue;
            }
            Note n;
            n.pitch = defs[k].note;
            n.start = beat;
            n.length = grid;
            n.velocity = juce::jlimit (0.35f, 1.0f, 0.35f + 0.65f * v / mx);
            if (k == 2)
            {
                // a snare's noise also lights up the hat band: skip weaker hat onsets that coincide with a snare
                bool snareHere = false;
                for (auto& o : c.notes) if (isSnare (o.pitch) && std::abs (o.start - beat) < 1e-6) snareHere = true;
                // real hats are much brighter than they are mid-heavy; a snare's noise is broadband
                const float bright = energy[2][(size_t) f] / (energy[1][(size_t) f] + 1e-12f);
                if (snareHere && (v < 0.6f * mx || bright < 1.5f)) continue;
                // open hat if the high band is still ringing 80 ms after the hit's peak
                float pk = 0; int fp = f;
                for (int q = f; q <= juce::jmin (F - 1, f + 3); ++q) if (e[(size_t) q] > pk) { pk = e[(size_t) q]; fp = q; }
                const int f2 = juce::jmin (F - 1, fp + (int) (0.08 * a.sr / s.hop));
                if (e[(size_t) f2] > 0.3f * pk) { n.pitch = gm::openHat; n.length = 0.5; }
            }
            bool dup = false;
            for (auto& o : c.notes) if (o.pitch == n.pitch && std::abs (o.start - n.start) < 1e-6) dup = true;
            if (! dup) { c.notes.push_back (n); ++counts[k]; }
        }
    }
    c.sort();
    if (report) *report = juce::String (counts[0]) + " kicks, " + juce::String (counts[1]) + " snares/claps, " + juce::String (counts[2]) + " hats";
    return c;
}

// ---------------------------------------------------------------- mono (bass / lead)
inline Clip monoToMidi (const AudioLoop& a, const LoopTiming& tm, juce::String* report = nullptr)
{
    Clip c; c.lengthBeats = tm.beats;
    cl::PitchTracker tr;
    tr.prepare ((float) a.sr);
    struct Fr { double t; float midi; bool voiced; float rms; };
    std::vector<Fr> fr;
    for (size_t i = 0; i < a.mono.size(); ++i)
        if (tr.push (a.mono[i]))
            fr.push_back ({ (double) i / a.sr - 0.017, tr.voiced ? 69.0f + 12.0f * std::log2 (tr.hz / 440.0f) : 0.0f, tr.voiced, tr.rms });
    // add a little silence at the end so the last note closes
    for (int k = 0; k < 8; ++k) fr.push_back ({ fr.empty() ? 0.0 : fr.back().t + 0.005, 0.0f, false, 0.0f });
    float peak = 0; for (auto& f : fr) peak = juce::jmax (peak, f.rms);
    if (peak <= 0) return c;
    const double spb = 60.0 / tm.tempo;

    int startIdx = -1; std::vector<float> pitches; float notePeak = 0;
    auto close = [&] (int endIdx)
    {
        if (startIdx < 0 || pitches.size() < 6) { startIdx = -1; pitches.clear(); notePeak = 0; return; }
        std::vector<float> p (pitches); std::sort (p.begin(), p.end());
        Note n;
        n.pitch = (int) std::lround (p[p.size() / 2]);
        double st = quantise (juce::jmax (0.0, fr[(size_t) startIdx].t) / spb, 0.25);
        double en = quantise (fr[(size_t) endIdx].t / spb, 0.25);
        if (en <= st) en = st + 0.25;
        if (st >= tm.beats - 1e-6) { startIdx = -1; pitches.clear(); return; }
        n.start = st; n.length = juce::jmin (en, tm.beats) - st;
        n.velocity = juce::jlimit (0.4f, 1.0f, 0.4f + 0.6f * notePeak / peak);
        // merge with a previous identical note that this one only continues
        if (! c.notes.empty() && c.notes.back().pitch == n.pitch && c.notes.back().start + c.notes.back().length > n.start + 1e-6)
            c.notes.back().length = juce::jmax (c.notes.back().length, n.start + n.length - c.notes.back().start);
        else if (n.length > 0) c.notes.push_back (n);
        startIdx = -1; pitches.clear(); notePeak = 0;
    };
    float prevRms = 0;
    for (int i = 0; i < (int) fr.size(); ++i)
    {
        const auto& f = fr[(size_t) i];
        const bool on = f.voiced && f.rms > 0.15f * peak;
        if (startIdx >= 0)
        {
            const float cur = pitches.empty() ? f.midi : pitches.back();
            const bool pitchJump = on && std::abs (f.midi - cur) > 0.7f && i + 2 < (int) fr.size()
                                   && std::abs (fr[(size_t) i + 1].midi - f.midi) < 0.5f && std::abs (fr[(size_t) i + 2].midi - f.midi) < 0.5f;
            const bool reattack = on && f.rms > prevRms * 1.8f && prevRms < 0.5f * notePeak;
            if (! on || pitchJump || reattack) close (i);
        }
        if (on)
        {
            if (startIdx < 0) startIdx = i;
            pitches.push_back (f.midi);
            notePeak = juce::jmax (notePeak, f.rms);
        }
        prevRms = f.rms;
    }
    c.sort();
    if (report) *report = juce::String ((int) c.notes.size()) + " notes";
    return c;
}

// ---------------------------------------------------------------- poly (chords / keys / pads)
inline Clip polyToMidi (const AudioLoop& a, const LoopTiming& tm, juce::String* report = nullptr)
{
    Clip c; c.lengthBeats = tm.beats;
    Stft s (a.mono, a.sr, 13, 1024);
    const int F = (int) s.mag.size(), B = (int) s.mag[0].size();
    const float hz = s.hzPerBin();
    const int lo = 40, hi = 96, M = hi - lo + 1;
    const double spb = 60.0 / tm.tempo;

    std::vector<std::vector<float>> act ((size_t) F, std::vector<float> ((size_t) M, 0.0f));
    std::vector<float> frameE ((size_t) F, 0.0f);
    float globalMax = 0;
    for (int f = 0; f < F; ++f)
    {
        std::vector<float> m (s.mag[(size_t) f]);
        double e = 0; for (float v : m) e += v * v;
        frameE[(size_t) f] = (float) e;
        auto peakNear = [&] (float freq)
        {
            const int b0 = juce::jmax (1, (int) (freq * 0.977f / hz)), b1 = juce::jmin (B - 1, (int) std::ceil (freq * 1.023f / hz));
            float best = 0; int bi = -1;
            for (int b = b0; b <= b1; ++b) if (m[(size_t) b] > best) { best = m[(size_t) b]; bi = b; }
            return std::pair<float, int> (best, bi);
        };
        float first = 0;
        for (int pick = 0; pick < 6; ++pick)
        {
            float bestS = 0; int bestM = -1;
            for (int k = 0; k < M; ++k)
            {
                const float f0 = 440.0f * std::exp2 ((lo + k - 69) / 12.0f);
                float sal = 0;
                for (int h = 1; h <= 6; ++h) sal += std::pow (0.8f, (float) (h - 1)) * peakNear (f0 * h).first;
                // a note needs its own fundamental, otherwise it's someone else's harmonic
                if (peakNear (f0).first < 0.25f * sal / 2.5f) sal *= 0.3f;
                if (sal > bestS) { bestS = sal; bestM = k; }
            }
            if (bestM < 0 || bestS <= 0) break;
            if (pick == 0) first = bestS;
            else if (bestS < 0.33f * first) break;
            act[(size_t) f][(size_t) bestM] = bestS;
            globalMax = juce::jmax (globalMax, bestS);
            // remove its harmonics from the spectrum before looking for the next note
            const float f0 = 440.0f * std::exp2 ((lo + bestM - 69) / 12.0f);
            for (int h = 1; h <= 8; ++h)
            {
                auto pk = peakNear (f0 * h);
                if (pk.second < 0) continue;
                for (int b = juce::jmax (1, pk.second - 2); b <= juce::jmin (B - 1, pk.second + 2); ++b) m[(size_t) b] *= (h == 1 ? 0.0f : 0.15f);
            }
        }
    }
    if (globalMax <= 0) return c;

    // onsets (energy rises) for re-struck chords
    std::vector<bool> onset ((size_t) F, false);
    for (int f = 2; f < F; ++f) onset[(size_t) f] = frameE[(size_t) f] > 2.5f * frameE[(size_t) f - 2] && frameE[(size_t) f] > 1e-6f;

    // grid: 1/16 steps; a note is on in a step if active in most of that step's frames
    const int steps = (int) std::round (tm.beats / 0.25);
    std::vector<std::vector<float>> on ((size_t) steps, std::vector<float> ((size_t) M, 0.0f));
    std::vector<bool> stepOnset ((size_t) steps, false);
    for (int st = 0; st < steps; ++st)
    {
        const double t0 = st * 0.25 * spb, t1 = (st + 1) * 0.25 * spb;
        int cnt = 0;
        for (int f = 0; f < F; ++f)
        {
            const double t = s.frameTime (f);
            if (t < t0 || t >= t1) continue;
            ++cnt;
            if (onset[(size_t) f]) stepOnset[(size_t) st] = true;
            for (int k = 0; k < M; ++k) if (act[(size_t) f][(size_t) k] > 0.12f * globalMax) on[(size_t) st][(size_t) k] += 1.0f;
        }
        for (int k = 0; k < M; ++k) on[(size_t) st][(size_t) k] = cnt > 0 ? on[(size_t) st][(size_t) k] / cnt : 0.0f;
    }
    for (int k = 0; k < M; ++k)
    {
        int startStep = -1;
        for (int st = 0; st <= steps; ++st)
        {
            const bool isOn = st < steps && on[(size_t) st][(size_t) k] >= 0.5f;
            const bool restrike = st < steps && startStep >= 0 && stepOnset[(size_t) st] && isOn;
            if (startStep >= 0 && (! isOn || restrike))
            {
                Note n; n.pitch = lo + k; n.start = startStep * 0.25; n.length = (st - startStep) * 0.25; n.velocity = 0.8f;
                if (n.length >= 0.25) c.notes.push_back (n);
                startStep = -1;
            }
            if (isOn && startStep < 0) startStep = st;
        }
    }
    c.sort();
    if (report) *report = juce::String ((int) c.notes.size()) + " notes";
    return c;
}
} // namespace arc
