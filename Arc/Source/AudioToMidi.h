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
struct LoopTiming { double beats = 16, tempo = 120; int bars = 4; double offsetSec = 0; float confidence = 0; bool shifted = false, fromSet = false; };

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
inline double wrapBeat (double b, double len) { b = std::fmod (b, len); if (b < 0) b += len; if (b >= len - 1e-6) b = 0; return b; }

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

// ---------------------------------------------------------------- tempo + downbeat
// Finds the tempo and where bar 1 really starts, from where the hits land (kick weighted), instead of
// assuming the file begins exactly on the downbeat.
inline LoopTiming analyseTiming (const AudioLoop& a, double hostBpm, bool findDownbeat = true)
{
    const double dur = (double) a.mono.size() / a.sr;
    LoopTiming t = timingFor (dur, hostBpm);
    Stft s (a.mono, a.sr, 10, 256);
    const int F = (int) s.mag.size();
    if (F < 16) return t;
    const float hz = s.hzPerBin();
    std::vector<float> all ((size_t) F, 0), low ((size_t) F, 0), mid ((size_t) F, 0);
    std::vector<float> prevA ((size_t) 3, 0);
    for (int f = 0; f < F; ++f)
    {
        float e[3] {};
        for (int b = 1; b < (int) s.mag[(size_t) f].size(); ++b)
        {
            const float m = s.mag[(size_t) f][(size_t) b], fr = b * hz, p = m * m;
            e[0] += p;
            if (fr < 150) e[1] += p; else if (fr > 1000 && fr < 5000) e[2] += p;
        }
        for (int k = 0; k < 3; ++k)
            (k == 0 ? all : k == 1 ? low : mid)[(size_t) f] = e[k];
    }
    // onset strength; the loop wraps, so frame 0 is compared with the last frame (a hit on the 1 counts)
    for (auto* v : { &all, &low, &mid })
    {
        std::vector<float> d ((size_t) F);
        for (int f = 0; f < F; ++f)
        {
            const float cur = (*v)[(size_t) f], prev = (*v)[(size_t) (f > 0 ? f - 1 : F - 1)];
            d[(size_t) f] = juce::jmax (0.0f, std::log10 (cur + 1e-10f) - std::log10 (prev + 1e-10f)) * std::sqrt (cur);
        }
        *v = d;
    }
    juce::ignoreUnused (prevA);
    const double fps = a.sr / s.hop;
    const double lead = 0.25 * (double) s.N / a.sr;     // same attack correction as the transcribers
    auto at = [&] (const std::vector<float>& v, double fr) { const int i = (int) std::round (fr); float m = 0; for (int k = i - 1; k <= i + 1; ++k) if (k >= 0 && k < F) m = juce::jmax (m, v[(size_t) k]); return m; };

    // tempo: keep the host tempo if the hits line up with it, otherwise search 80-180 BPM
    auto tempoScore = [&] (double bpm)
    {
        const double P = 60.0 / bpm * fps;
        double best = 0;
        for (double ph = 0; ph < P; ph += 0.5)
        {
            double sc = 0; int n = 0;
            for (double fr = ph; fr < F; fr += P) { sc += at (all, fr) + 2.0 * at (low, fr); ++n; }
            best = std::max (best, n ? sc / n : 0.0);
        }
        return best;
    };
    const double hostScore = tempoScore (t.tempo);
    double bestBpm = t.tempo, bestScore = hostScore;
    // a file that is a whole number of bars long already tells us its tempo
    const bool pow2Bars = t.bars <= 32 && (t.bars & (t.bars - 1)) == 0 && std::abs (t.beats - 4.0 * t.bars) < 1e-6;
    const bool wholeBars = pow2Bars && std::abs (dur * t.tempo / 60.0 - t.beats) < 0.02 * t.beats;
    for (double bpm = 80; bpm <= 180 && ! wholeBars; bpm += 0.5)
    {
        const double sc = tempoScore (bpm);
        if (sc > bestScore * 1.25) { bestScore = sc; bestBpm = bpm; }   // only move off the expected tempo for a clear win
    }
    if (bestBpm != t.tempo)
    {
        t.tempo = bestBpm;
        const double beats = dur * bestBpm / 60.0;
        int bars = std::max (1, (int) std::round (beats / 4.0));
        t.bars = bars; t.beats = bars * 4.0;
    }
    // phase: where the beats fall
    const double P = 60.0 / t.tempo * fps;
    auto phaseScore = [&] (double ph) { double sc = 0; for (double fr = ph; fr < F; fr += P) sc += at (all, fr) + 2.0 * at (low, fr); return sc; };
    double bestPh = 0, bestSc = -1;
    for (double ph = 0; ph < P; ph += 0.25)
    {
        const double sc = phaseScore (ph);
        if (sc > bestSc) { bestSc = sc; bestPh = ph; }
    }
    // Most stems start on the beat: only move off "the file starts on a beat" for a clear win
    // (an offbeat bassline would otherwise look like it starts half a beat late).
    const double ph0 = std::fmod (lead * fps, P);
    if (phaseScore (ph0) >= 0.92 * bestSc || ! findDownbeat) bestPh = ph0;
    // downbeat: kick on the 1, backbeat (snare/clap) on 2 and 4 (indices wrap round the loop)
    const double loopFrames = t.beats * P;
    auto atW = [&] (const std::vector<float>& v, double fr) { fr = std::fmod (fr, loopFrames); if (fr < 0) fr += loopFrames; return at (v, fr); };
    int bestJ = 0; double score0 = 0, bestD = -1;
    for (int j = 0; j < 4; ++j)
    {
        double sc = 0;
        for (int m = 0; m < t.bars; ++m)
        {
            const double f0 = bestPh + (j + 4.0 * m) * P;
            sc += atW (low, f0) + 0.6 * atW (mid, f0 + P) + 0.6 * atW (mid, f0 + 3 * P) - 0.3 * atW (mid, f0);
        }
        if (j == 0) { score0 = sc; bestD = sc; }
        else if (sc > bestD && sc > score0 * 1.2) { bestD = sc; bestJ = j; }   // the file's first beat wins ties
    }
    if (! findDownbeat) { bestPh = ph0; bestJ = 0; }
    double first = (bestPh + bestJ * P) / fps - lead;
    const double barSec = 4.0 * 60.0 / t.tempo;
    first = std::fmod (first, barSec); if (first < 0) first += barSec;
    if (first > barSec - 0.02) first = 0;
    // refine to about a millisecond with a sample-domain onset envelope: STFT frames smear onsets by several ms
    {
        const int blk = std::max (16, (int) (a.sr * 0.001)), nb = (int) a.mono.size() / blk;
        if (nb > 8)
        {
            std::vector<float> e ((size_t) nb), on ((size_t) nb);
            for (int b = 0; b < nb; ++b) { double sum = 0; for (int i = b * blk; i < (b + 1) * blk; ++i) sum += (double) a.mono[(size_t) i] * a.mono[(size_t) i]; e[(size_t) b] = (float) sum; }
            float mx = 0;
            for (int b = 0; b < nb; ++b)
            {
                const float prev = e[(size_t) (b > 0 ? b - 1 : nb - 1)];
                on[(size_t) b] = juce::jmax (0.0f, std::log10 (e[(size_t) b] + 1e-9f) - std::log10 (prev + 1e-9f)) * std::sqrt (e[(size_t) b]);
                mx = juce::jmax (mx, on[(size_t) b]);
            }
            const double beatSec = 60.0 / t.tempo, blkSec = (double) blk / a.sr;
            std::vector<double> devs;
            for (double pred = first; pred < dur; pred += beatSec)
            {
                const int c = (int) std::round (pred / blkSec), w = (int) std::ceil (0.025 / blkSec);
                int bi = -1; float bv = 0;
                for (int b = c - w; b <= c + w; ++b) { const int bb = ((b % nb) + nb) % nb; if (on[(size_t) bb] > bv) { bv = on[(size_t) bb]; bi = b; } }
                if (bi >= 0 && bv > 0.25f * mx) devs.push_back (bi * blkSec - pred);
            }
            if (devs.size() >= 2)
            {
                std::sort (devs.begin(), devs.end());
                first += devs[devs.size() / 2];
                first = std::fmod (first, barSec); if (first < 0) first += barSec;
                if (first > barSec - 0.002) first = 0;
            }
        }
    }
    t.offsetSec = first;
    t.confidence = bestSc > 0 ? (float) juce::jlimit (0.0, 1.0, bestScore / (bestScore + 1e-9)) : 0.0f;
    return t;
}

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
        for (int f = 0; f < F; ++f)   // loops wrap: frame 0 compares with the last frame
            flux[(size_t) f] = juce::jmax (0.0f, std::log10 (e[(size_t) f] + 1e-10f) - std::log10 (e[(size_t) (f > 0 ? f - 1 : F - 1)] + 1e-10f)) * std::sqrt (e[(size_t) f]);
        float mx = 0; for (float v : flux) mx = juce::jmax (mx, v);
        if (mx <= 0) continue;
        // how loud a kick's own click is in the snare band (low percentile of the band at kick hits)
        float kickClick = 0;
        if (k == 1 && ! lowHits.empty())
        {
            std::vector<float> clicks;
            for (auto& h : lowHits) { float m = 0; for (int q = h.second - 1; q <= h.second + 1; ++q) if (q >= 0 && q < F) m = juce::jmax (m, flux[(size_t) q]); clicks.push_back (m); }
            std::sort (clicks.begin(), clicks.end());
            kickClick = clicks[(size_t) (clicks.size() * 3 / 10)];
        }
        const int minGap = (int) (0.06 * a.sr / s.hop);
        int last = -1000;
        for (int f = 0; f < F - 1; ++f)
        {
            const float v = flux[(size_t) f];
            const float prev = flux[(size_t) (f > 0 ? f - 1 : F - 1)];
            if (v < defs[k].rel * mx || v < prev || v < flux[(size_t) f + 1] || f - last < minGap) continue;
            last = f;
            const double t = s.frameTime (f) - 0.25 * (double) s.N / a.sr;   // frame centre leads the attack
            const double beat = wrapBeat (quantise ((t - tm.offsetSec) / secPerBeat, grid), tm.beats);
            if (k == 0) lowHits.push_back ({ beat, f });
            if (k == 1)
            {
                // a kick's click: coincides with a kick and is weak compared to the loudest snare
                bool kickHere = false;
                for (auto& h : lowHits) if (std::abs (h.first - beat) < 1e-6) kickHere = true;
                if (kickHere && v < juce::jmax (2.2f * kickClick, 0.3f * mx)) continue;
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
        const double rawSt = quantise ((fr[(size_t) startIdx].t - tm.offsetSec) / spb, 0.25);
        double len = quantise ((fr[(size_t) endIdx].t - tm.offsetSec) / spb, 0.25) - rawSt;
        if (len < 0.25) len = 0.25;
        const double st = wrapBeat (rawSt, tm.beats);
        n.start = st; n.length = juce::jmin (len, tm.beats);
        n.velocity = juce::jlimit (0.4f, 1.0f, 0.4f + 0.6f * notePeak / peak);
        // merge with a previous identical note that this one only continues
        if (! c.notes.empty() && c.notes.back().pitch == n.pitch && c.notes.back().start < n.start && c.notes.back().start + c.notes.back().length > n.start + 1e-6)
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
        const double t0 = st * 0.25 * spb, t1 = (st + 1) * 0.25 * spb, loopSec = tm.beats * spb;
        int cnt = 0;
        for (int f = 0; f < F; ++f)
        {
            double t = std::fmod (s.frameTime (f) - tm.offsetSec, loopSec);
            if (t < 0) t += loopSec;
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
