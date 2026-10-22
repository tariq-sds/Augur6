#pragma once
#include "AudioToMidi.h"
#include "../../Mirror/Source/PhaseVocoder.h"
#include "LiveSet.h"
#include <memory>

// Arc 2: audio stems as lanes. Each stem is decoded, its tempo and bar 1 found, cropped, time-stretched
// to the host tempo, optionally transposed into key, and kept as a seamless loop buffer.
namespace arc2
{
enum Role { rKick = 0, rBass, rHats, rClap, rPerc, rTops, rDrums, rChords, rLead, rVocal, rFx, kRoles };

inline const char* roleName (int r)
{
    static const char* n[] { "KICK", "BASS", "HATS", "CLAP", "PERC", "TOPS", "DRUMS", "CHORDS", "LEAD", "VOCAL", "FX" };
    return n[juce::jlimit (0, kRoles - 1, r)];
}
// how much a lane changes by default: a kick stays dead straight, percussion and leads move most
inline int defaultVary (int r) { return r == rKick ? 0 : (r == rPerc || r == rTops || r == rLead) ? 2 : 1; }
inline bool isDrumRole (int r) { return r == rKick || r == rHats || r == rClap || r == rPerc || r == rTops || r == rDrums; }
inline bool isTonalRole (int r) { return r == rBass || r == rChords || r == rLead || r == rVocal; }
inline juce::Colour roleColour (int r)
{
    static const juce::uint32 c[] { 0xffff5a4e, 0xff4fb6ff, 0xffffd166, 0xffff8fab, 0xfff4a259, 0xffc5e063, 0xffff7043, 0xffb388ff, 0xff5ce1a6, 0xff64e0e0, 0xff9aa5b1 };
    return juce::Colour (c[juce::jlimit (0, kRoles - 1, r)]);
}

// Role from the file name (like most loop tools), else -1
inline int roleFromName (const juce::String& fileName)
{
    const auto n = " " + fileName.toLowerCase().replaceCharacters ("_-.()[]", "       ") + " ";
    auto has = [&n] (std::initializer_list<const char*> w) { for (auto* x : w) if (n.contains (x)) return true; return false; };
    if (has ({ "vox", "vocal", "voice", "acapella", "acap", "chant", "spoken" })) return rVocal;
    if (has ({ " fx", "riser", "sweep", "impact", "noise", "atmos", "transition", "uplifter", "downlifter" })) return rFx;
    if (has ({ "kick", " bd ", "bassdrum", "bass drum", "bombo" })) return rKick;
    if (has ({ "bass", " sub", "808", "reese" })) return rBass;
    if (has ({ "hat", " hh", "hihat", "shaker", "ride", "cymbal" })) return rHats;
    if (has ({ "clap", "snare", " snr", " sd ", " cp ", "rimshot" })) return rClap;
    if (has ({ "perc", "conga", "bongo", " tom", "rim ", "tamb", "cowbell", "clave", "metal", " hit", "block", "triangle", "agogo", "timbale" })) return rPerc;
    if (has ({ "top", "tops", "groove" })) return rTops;
    if (has ({ "drum", "beat", "break", "kit" })) return rDrums;
    if (has ({ "chord", " pad", "keys", "piano", "stab", "organ", "string", "rhodes", "synth" })) return rChords;
    if (has ({ "lead", " arp", "pluck", "hook", "melody", "riff", "bell", "guitar" })) return rLead;
    return -1;
}

// ---------------------------------------------------------------- DSP helpers (offline)
inline juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double ratio)   // ratio = out/in length
{
    if (std::abs (ratio - 1.0) < 1e-6) return in;
    const int outN = juce::jmax (1, (int) std::round (in.getNumSamples() * ratio));
    juce::AudioBuffer<float> out (in.getNumChannels(), outN);
    for (int c = 0; c < in.getNumChannels(); ++c)
    {
        juce::LagrangeInterpolator li;
        li.process (1.0 / ratio, in.getReadPointer (c), out.getWritePointer (c), outN, in.getNumSamples(), 0);
    }
    return out;
}

// WSOLA time stretch of a seamless loop to exactly outLen samples (pitch unchanged)
inline juce::AudioBuffer<float> stretchLoop (const juce::AudioBuffer<float>& in, int outLen, double sr)
{
    const int inLen = in.getNumSamples(), ch = in.getNumChannels();
    if (inLen <= 0 || outLen <= 0) return {};
    if (std::abs (outLen - inLen) <= 1)
    {
        juce::AudioBuffer<float> o (ch, outLen); o.clear();
        for (int c = 0; c < ch; ++c) o.copyFrom (c, 0, in, c, 0, juce::jmin (inLen, outLen));
        return o;
    }
    const int frame = juce::jmax (256, (int) (0.040 * sr)) & ~1, hop = frame / 2, tol = (int) (0.010 * sr);
    const double alpha = (double) outLen / inLen;
    juce::AudioBuffer<float> out (ch, outLen + frame);
    out.clear();
    std::vector<float> win ((size_t) frame);
    for (int i = 0; i < frame; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / frame);
    auto mono = [&] (long long idx) { idx %= inLen; if (idx < 0) idx += inLen; float s = 0; for (int c = 0; c < ch; ++c) s += in.getSample (c, (int) idx); return s; };
    // transients: grains that touch one are placed exactly on the grid (no search), so hits stay on time
    std::vector<int> hits;
    {
        const int blk = juce::jmax (16, (int) (sr * 0.001)), nb = inLen / blk;
        std::vector<float> e ((size_t) juce::jmax (1, nb)), on ((size_t) juce::jmax (1, nb));
        float mx = 0;
        for (int b = 0; b < nb; ++b) { double sum = 0; for (int i = b * blk; i < (b + 1) * blk; ++i) { const float v = mono (i); sum += v * v; } e[(size_t) b] = (float) sum; }
        for (int b = 0; b < nb; ++b)
        {
            // energy over the next 8 ms vs the 16 ms before: a real attack jumps by 5 dB or more
            // (windows longer than a low note's period, so a steady tone never looks like a hit)
            auto sum = [&] (int from, int len) { float s = 0; for (int k = 0; k < len; ++k) s += e[(size_t) (((from + k) % nb + nb) % nb)]; return s; };
            const float now = sum (b, 8) / 8, before = sum (b - 16, 16) / 16;
            const float jump = std::log10 ((now + 1e-9f) / (before + 1e-9f));
            on[(size_t) b] = jump > 0.5f && now > 1e-6f * blk ? jump * std::sqrt (now) : 0.0f;
            mx = juce::jmax (mx, on[(size_t) b]);
        }
        int last = -1000000;
        for (int b = 0; b < nb; ++b)
            if (on[(size_t) b] > 0.1f * mx && on[(size_t) b] >= on[(size_t) ((b + nb - 1) % nb)] && on[(size_t) b] >= on[(size_t) ((b + 1) % nb)] && (b - last) * blk > (int) (0.05 * sr))
            { hits.push_back (b * blk); last = b; }
    }
    auto nearHit = [&] (long long from, long long to)
    {
        for (int h : hits) for (long long w = -inLen; w <= inLen; w += inLen) if (h + w >= from && h + w < to) return true;
        return false;
    };
    long long prevIn = 0;
    for (int k = 0; (long long) k * hop < outLen; ++k)
    {
        const long long nominal = (long long) std::llround (k * hop / alpha);
        long long best = nominal;
        if (k > 0 && ! nearHit (nominal - tol, nominal + frame + tol))
        {
            // pick the input position whose start best continues the previous grain
            const long long natural = prevIn + hop;
            double bestC = -1e30;
            for (int d = -tol; d <= tol; d += 2)
            {
                double c = 0;
                for (int i = 0; i < hop; i += 4) c += (double) mono (natural + i) * mono (nominal + d + i);
                if (c > bestC) { bestC = c; best = nominal + d; }
            }
        }
        prevIn = best;
        const int o0 = k * hop;
        for (int c = 0; c < ch; ++c)
        {
            float* dst = out.getWritePointer (c);
            for (int i = 0; i < frame; ++i)
            {
                long long idx = (best + i) % inLen; if (idx < 0) idx += inLen;
                dst[o0 + i] += in.getSample (c, (int) idx) * win[(size_t) i];
            }
        }
    }
    // wrap the tail into the head so the loop is seamless
    juce::AudioBuffer<float> res (ch, outLen);
    for (int c = 0; c < ch; ++c)
    {
        res.copyFrom (c, 0, out, c, 0, outLen);
        res.addFrom (c, 0, out, c, outLen, frame);
    }
    return res;
}


// Transpose a loop (pitch only). Bass and other single-line parts: resample then WSOLA back to length,
// which stays tight and clean on low notes. Chords and pads: phase vocoder.
inline juce::AudioBuffer<float> transposeLoop (const juce::AudioBuffer<float>& in, int semis, double sr = 48000, bool monophonic = false)
{
    if (semis == 0) return in;
    if (monophonic)
    {
        const double r = std::exp2 (semis / 12.0);
        auto rs = resample (in, 1.0 / r);
        return stretchLoop (rs, in.getNumSamples(), sr);
    }
    mr::PhaseVocoder pv;
    pv.prepare();
    const int n = in.getNumSamples(), lat = pv.latency();
    const float ratio = std::exp2 ((float) semis / 12.0f);
    juce::AudioBuffer<float> out (2, n);
    // run the loop through twice so the output is steady and seamless; keep the second pass
    double eIn = 0, eOut = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < n + (pass == 1 ? lat : 0); ++i)
        {
            const int idx = i % n;
            float l = in.getSample (0, idx), r = in.getSample (in.getNumChannels() > 1 ? 1 : 0, idx);
            if (pass == 1) eIn += 0.5 * (l * l + r * r);
            pv.process (l, r, ratio);
            if (pass == 1 && i >= lat) { const int o = (i - lat) % n; out.setSample (0, o, l); out.setSample (1, o, r); eOut += 0.5 * (l * l + r * r); }
            else if (pass == 1 && i < lat) { const int o = (n - lat + i) % n; if (o >= 0) { out.setSample (0, o, l); out.setSample (1, o, r); } }
        }
    if (eOut > 1e-12) out.applyGain ((float) std::sqrt (eIn / eOut));
    return out;
}

inline std::array<float, 12> chromaOf (const juce::AudioBuffer<float>& b, double sr)
{
    cl::Chroma c;
    c.prepare ((float) sr);
    std::array<double, 12> sum {};
    const int passes = b.getNumSamples() < (int) (sr * 12) ? 2 : 1;   // short loops: go round twice so there are enough frames
    for (int pass = 0; pass < passes; ++pass)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float m = 0; for (int k = 0; k < b.getNumChannels(); ++k) m += b.getSample (k, i);
            c.push (m, false);
            if ((i & 4095) == 0) for (int k = 0; k < 12; ++k) sum[(size_t) k] += c.hist[(size_t) k];
        }
    std::array<float, 12> h {};
    for (int k = 0; k < 12; ++k) h[(size_t) k] = (float) sum[(size_t) k];
    return h;
}

// ---------------------------------------------------------------- a lane
struct LaneAudio   // what the audio thread plays: immutable once published
{
    juce::AudioBuffer<float> loop;   // host sample rate, host tempo, exactly `bars` bars
    int bars = 1;
    double bpm = 120, sr = 48000;
    double samplesPerBeat() const { return sr * 60.0 / bpm; }
};

struct Stem
{
    // source
    juce::String name, path;
    std::shared_ptr<const juce::AudioBuffer<float>> original;
    int uid = 0;
    double fileSr = 48000;
    arc::LoopTiming timing;
    std::array<float, 12> chroma {};
    cl::KeyResult key;
    // user settings
    int role = rDrums;
    int cropStart = 0, cropBars = 0;   // in bars of the source; cropBars 0 = whole loop (max 8)
    int nudge = 0;                     // 16ths
    int transpose = 0;                 // semitones applied (worked out by the processor)
    int manualTranspose = kAuto;       // kAuto = follow the KEY switch, else fixed semitones
    int suggested = 0;                 // semitones that would put it in key
    static constexpr int kAuto = -99;
    float volumeDb = 0;
    bool mute = false, solo = false, hold = false;
    uint32_t seed = 1;
    // prepared audio + display
    std::shared_ptr<const LaneAudio> audio;
    std::vector<float> peaks;          // |max| per pixel bucket of the prepared loop, for the waveform
    juce::String report;
    bool fromMidi = false;
    std::vector<double> tempoChoices;  // other likely source tempos (bar counts), best first
    juce::String clipTrack;            // the Live track this came from, when read from a Live set
    int vary = 1;                      // 0 straight, 1 some, 2 lots
};

inline juce::String timingReport (const arc::LoopTiming& t)
{
    const double tr = std::round (t.tempo * 100) / 100;
    return juce::String (t.bars) + (t.bars == 1 ? " bar, " : " bars, ") + juce::String (tr, std::abs (tr - std::round (tr)) < 0.005 ? 0 : 2) + " BPM"
         + (t.fromSet ? ", synced from your Live set" : t.shifted ? ", bar 1 found " + juce::String (juce::roundToInt (t.offsetSec * 1000)) + " ms in" : juce::String());
}

inline int sourceBars (const Stem& s) { return juce::jmax (1, s.timing.bars); }


// ---------------------------------------------------------------- timing
// Loops and stems are almost always bounced from bar 1 and cut to whole bars, so that is the starting
// assumption: bar 1 is the first sample and the tempo is exactly (bars x 4 beats) / length. The hits
// only decide HOW MANY bars the file is (and so its tempo); bar 1 is only moved for a drum loop whose
// hits clearly don't line up with the file start.
struct TimingResult { arc::LoopTiming t; std::vector<double> choices; };

inline TimingResult stemTiming (const arc::AudioLoop& a, double hostBpm, int role)
{
    TimingResult r;
    const double dur = (double) a.mono.size() / a.sr;
    hostBpm = juce::jlimit (40.0, 250.0, hostBpm);
    arc::Stft s (a.mono, a.sr, 10, 256);
    const int F = (int) s.mag.size();
    const float hz = s.hzPerBin();
    std::vector<float> all ((size_t) F, 0), low ((size_t) F, 0);
    for (int f = 0; f < F; ++f)
        for (int b = 1; b < (int) s.mag[(size_t) f].size(); ++b)
        {
            const float m = s.mag[(size_t) f][(size_t) b], p = m * m;
            all[(size_t) f] += p;
            if (b * hz < 150) low[(size_t) f] += p;
        }
    for (auto* v : { &all, &low })
    {
        std::vector<float> d ((size_t) F);
        for (int f = 0; f < F; ++f)
        {
            const float cur = (*v)[(size_t) f], prev = (*v)[(size_t) (f > 0 ? f - 1 : F - 1)];
            d[(size_t) f] = juce::jmax (0.0f, std::log10 (cur + 1e-10f) - std::log10 (prev + 1e-10f)) * std::sqrt (cur);
        }
        *v = d;
    }
    const double fps = a.sr / s.hop, lead = 0.25 * (double) s.N / a.sr;
    auto at = [&] (const std::vector<float>& v, double fr)
    {
        if (F == 0) return 0.0f;
        const int i = (int) std::round (fr); float m = 0;
        for (int k = i - 1; k <= i + 1; ++k) m = juce::jmax (m, v[(size_t) (((k % F) + F) % F)]);
        return m;
    };
    // how well the hits sit on a beat grid of this tempo starting `phaseSec` into the file
    auto fit = [&] (double tempo, double phaseSec)
    {
        const double P = 60.0 / tempo * fps;
        double sc = 0; int n = 0;
        for (double fr = (phaseSec + lead) * fps; fr < F; fr += P) { sc += at (all, fr) + 2.0 * at (low, fr); ++n; }
        return n ? sc / n : 0.0;
    };

    // tempo evidence shouldn't depend on where the beat starts: best fit over all phases
    auto fitAny = [&] (double tempo)
    {
        const double beatSec = 60.0 / tempo;
        double best = 0;
        for (double ph = 0; ph < beatSec; ph += 0.004) best = std::max (best, fit (tempo, ph));
        return best;
    };
    // 1) how many bars: every whole-bar count with a plausible tempo is a candidate
    struct Cand { int bars; double tempo, score; };
    std::vector<Cand> cands;
    for (int bars = 1; bars <= 512; ++bars)
    {
        const double tempo = bars * 240.0 / dur;
        if (tempo < 60) continue;
        if (tempo > 200) break;
        const bool pow2 = (bars & (bars - 1)) == 0;
        const double prior = std::exp (-std::abs (std::log (tempo / hostBpm)) * 8.0)          // near Live's tempo
                           * (pow2 ? 1.15 : bars % 4 == 0 ? 1.05 : 1.0)                         // 1, 2, 4, 8... bars
                           * (std::abs (tempo - hostBpm) < 0.02 ? 1.3 : 1.0);                   // exactly Live's tempo
        cands.push_back ({ bars, tempo, (fitAny (tempo) + 1e-6) * prior });
    }
    {
        // recordings and bounces with a tail aren't whole bars long: always offer Live's own tempo too
        bool hasHost = false;
        for (auto& c : cands) hasHost |= std::abs (c.tempo - hostBpm) < 0.005 * hostBpm;
        const int hb = (int) std::floor (dur * hostBpm / 240.0 + 0.02);
        if (! hasHost && hb >= 1)
            cands.push_back ({ hb, hostBpm, (fitAny (hostBpm) + 1e-6) * ((hb & (hb - 1)) == 0 ? 1.15 : hb % 4 == 0 ? 1.05 : 1.0) });
    }
    if (cands.empty())
    {
        // not a whole number of bars at any sensible tempo: use Live's tempo and round the length
        r.t.tempo = hostBpm; r.t.bars = juce::jmax (1, (int) std::round (dur * hostBpm / 240.0)); r.t.beats = r.t.bars * 4.0;
    }
    else
    {
        std::sort (cands.begin(), cands.end(), [] (const Cand& x, const Cand& y) { return x.score > y.score; });
        r.t.tempo = cands[0].tempo; r.t.bars = cands[0].bars; r.t.beats = cands[0].bars * 4.0;
        for (size_t i = 0; i < cands.size() && r.choices.size() < 6; ++i) r.choices.push_back (cands[i].tempo);
    }
    r.t.offsetSec = 0;

    // 2) bar 1: only for a kick or full drum loop whose hits clearly sit somewhere other than the file start
    //    (hats, percussion, basslines are often on the offbeat on purpose)
    if ((role == rDrums || role == rKick) && F > 16)
    {
        const double beatSec = 60.0 / r.t.tempo;
        const double f0 = fit (r.t.tempo, 0);
        double bestPh = 0, bestF = f0;
        for (double ph = 0.002; ph < beatSec; ph += 0.002) { const double v = fit (r.t.tempo, ph); if (v > bestF) { bestF = v; bestPh = ph; } }
        if (bestF > 1.6 * f0 && bestPh > 0.015 && bestPh < beatSec - 0.015)
        {
            // which beat of the bar is the 1: kick on it, backbeat on 2 and 4
            std::vector<float> mid ((size_t) F, 0);
            for (int f = 0; f < F; ++f)
            {
                float e = 0, prevE = 0;
                for (int b = 1; b < (int) s.mag[(size_t) f].size(); ++b) { const float fr = b * hz; if (fr > 1000 && fr < 5000) { e += s.mag[(size_t) f][(size_t) b] * s.mag[(size_t) f][(size_t) b]; prevE += s.mag[(size_t) (f > 0 ? f - 1 : F - 1)][(size_t) b] * s.mag[(size_t) (f > 0 ? f - 1 : F - 1)][(size_t) b]; } }
                mid[(size_t) f] = juce::jmax (0.0f, std::log10 (e + 1e-10f) - std::log10 (prevE + 1e-10f)) * std::sqrt (e);
            }
            const double P = beatSec * fps;
            int bestJ = 0; double sc0 = 0, bestD = -1;
            for (int j = 0; j < 4; ++j)
            {
                double sc = 0;
                for (int m = 0; m < r.t.bars; ++m)
                {
                    const double fr = (bestPh + lead) * fps + (j + 4.0 * m) * P;
                    sc += at (low, fr) + 0.6 * at (mid, fr + P) + 0.6 * at (mid, fr + 3 * P) - 0.3 * at (mid, fr);
                }
                if (j == 0) { sc0 = sc; bestD = sc; }
                else if (sc > bestD * 1.1 && sc > sc0 * 1.3) { bestD = sc; bestJ = j; }   // ties go to the earliest beat
            }
            // refine the phase to about a millisecond from the raw signal (STFT frames smear onsets)
            {
                const int blk = std::max (16, (int) (a.sr * 0.001)), nb = (int) a.mono.size() / blk;
                std::vector<float> e ((size_t) nb), on ((size_t) nb);
                for (int b = 0; b < nb; ++b) { double sum = 0; for (int i = b * blk; i < (b + 1) * blk; ++i) sum += (double) a.mono[(size_t) i] * a.mono[(size_t) i]; e[(size_t) b] = (float) sum; }
                float mx = 0;
                for (int b = 0; b < nb; ++b) { on[(size_t) b] = juce::jmax (0.0f, std::log10 (e[(size_t) b] + 1e-9f) - std::log10 (e[(size_t) ((b + nb - 1) % nb)] + 1e-9f)) * std::sqrt (e[(size_t) b]); mx = juce::jmax (mx, on[(size_t) b]); }
                std::vector<double> devs;
                const double blkSec = (double) blk / a.sr;
                for (double pred = bestPh; pred < dur && nb > 0; pred += beatSec)
                {
                    const int c = (int) std::round (pred / blkSec), w = (int) std::ceil (0.03 / blkSec);
                    int bi = -1; float bv = 0;
                    for (int b = c - w; b <= c + w; ++b) { const int bb = ((b % nb) + nb) % nb; if (on[(size_t) bb] > bv) { bv = on[(size_t) bb]; bi = b; } }
                    if (bi >= 0 && bv > 0.25f * mx) devs.push_back (bi * blkSec - pred);
                }
                if (devs.size() >= 2) { std::sort (devs.begin(), devs.end()); bestPh += devs[devs.size() / 2]; }
            }
            double first = bestPh + bestJ * beatSec;
            const double barSec = 4 * beatSec;
            first = std::fmod (first, barSec);
            r.t.offsetSec = first;
            r.t.shifted = true;
        }
    }
    return r;
}

// decode a file into a stem (no analysis)
inline bool decodeStem (Stem& s, const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples < 2000) return false;
    const int n = (int) juce::jmin (r->lengthInSamples, (juce::int64) (r->sampleRate * 600));
    auto orig = std::make_shared<juce::AudioBuffer<float>> (2, n);
    juce::AudioBuffer<float> tmp ((int) juce::jmax (1u, r->numChannels), n);
    r->read (&tmp, 0, n, 0, true, true);
    for (int c = 0; c < 2; ++c) orig->copyFrom (c, 0, tmp, juce::jmin (c, tmp.getNumChannels() - 1), 0, n);
    s.fileSr = r->sampleRate;
    s.name = f.getFileNameWithoutExtension();
    s.path = f.getFullPathName();
    s.original = orig;
    return true;
}

// A stem exactly as a Live set uses it: bar 1, tempo and region straight from the clip's warp markers
inline bool stemFromClip (Stem& s, const live::ClipUse& u)
{
    if (! decodeStem (s, u.file)) return false;
    // name: the track's name says what it is far better than "4-Audio 0001"
    if (u.track.isNotEmpty()) s.name = u.track;
    s.clipTrack = u.track;
    const double tempo = 60.0 / u.secPerBeat;
    int bars = (int) std::floor (u.beats / 4.0 + 0.02);
    s.timing.tempo = tempo;
    s.timing.bars = juce::jmax (1, bars);
    s.timing.beats = s.timing.bars * 4.0;
    s.timing.offsetSec = u.startSec;
    s.timing.fromSet = true;
    s.timing.shifted = false;
    return true;
}

inline void analyseStem (Stem& s, double hostBpm, int forcedRole);

// decode a file and work out its role, timing and key. If the file is used in a Live set (a stem taken
// from inside a project), the set says exactly where bar 1 is and how fast it runs.
inline bool loadStem (Stem& s, const juce::File& f, double hostBpm)
{
    live::ClipUse u;
    if (live::useOf (f, u) && stemFromClip (s, u)) { analyseStem (s, hostBpm, -1); return true; }
    if (! decodeStem (s, f)) return false;
    analyseStem (s, hostBpm, -1);
    return true;
}

// tempo, bar 1, role (if not given) and key of a decoded stem
inline void analyseStem (Stem& s, double hostBpm, int forcedRole)
{
    const auto& src = *s.original;
    const int n = src.getNumSamples();

    arc::AudioLoop a;
    a.sr = s.fileSr; a.ok = true;
    a.mono.resize ((size_t) n);
    for (int i = 0; i < n; ++i) a.mono[(size_t) i] = 0.5f * (src.getSample (0, i) + src.getSample (1, i));

    // role: file name first, then the sound
    int role = forcedRole >= 0 ? forcedRole : roleFromName (s.name);
    if (role < 0 && s.clipTrack.isNotEmpty()) role = roleFromName (juce::File (s.path).getFileNameWithoutExtension());
    if (role < 0)
    {
        cl::PitchTracker tr; tr.prepare ((float) a.sr);
        int frames = 0, voiced = 0; double sumMidi = 0; float clean = 0;
        for (auto v : a.mono) if (tr.push (v) && tr.rms > 0.005f) { ++frames; if (tr.voiced) { ++voiced; clean += 1 - tr.aperiodicity; sumMidi += 69 + 12 * std::log2 (tr.hz / 440.0f); } }
        const float vf = frames ? (float) voiced / frames : 0, periodic = voiced ? clean / voiced : 0;
        const double meanMidi = voiced ? sumMidi / voiced : 0;
        // spectral balance for drums
        arc::Stft st (a.mono, a.sr, 10, 1024);
        double lo = 0, hi = 0, tot = 0;
        for (auto& fr : st.mag) for (int b = 1; b < (int) fr.size(); ++b) { const double p = (double) fr[(size_t) b] * fr[(size_t) b], hz = b * st.hzPerBin(); tot += p; if (hz < 150) lo += p; if (hz > 6000) hi += p; }
        if (vf < 0.35f || (vf < 0.6f && meanMidi < 40))
            role = tot > 0 && lo / tot > 0.6 ? rKick : tot > 0 && hi / tot > 0.4 ? rHats : rPerc;
        else if (meanMidi < 52) role = rBass;
        else if (vf > 0.6f && periodic > 0.8f) role = rLead;
        else role = rChords;
    }
    s.role = role;
    s.vary = defaultVary (role);
    if (! s.timing.fromSet)
    {
        auto tr = stemTiming (a, hostBpm, role);
        s.timing = tr.t;
        s.tempoChoices = tr.choices;
    }
    // long stems (a whole song part): start on the busiest 8 bars instead of a quiet intro
    s.cropStart = 0; s.cropBars = 0;
    if (s.timing.bars > 8)
    {
        const double barSec = 4.0 * 60.0 / s.timing.tempo;
        double best = -1;
        for (int b0 = 0; b0 + 8 <= s.timing.bars; b0 += 4)
        {
            const int i0 = (int) ((s.timing.offsetSec + b0 * barSec) * s.fileSr), i1 = juce::jmin (n, (int) ((s.timing.offsetSec + (b0 + 8) * barSec) * s.fileSr));
            double e = 0;
            for (int i = i0; i < i1; i += 4) e += (double) a.mono[(size_t) i] * a.mono[(size_t) i];
            if (e > best * 1.05) { best = e; s.cropStart = b0; }
        }
        s.cropBars = 8;
    }
    s.chroma = chromaOf (src, s.fileSr);
    s.key = cl::findKey (s.chroma);
    s.report = timingReport (s.timing);
}

// Builds the playable loop for the current host tempo / sample rate and the stem's settings
inline std::shared_ptr<const LaneAudio> prepareStem (const Stem& s, double hostBpm, double hostSr)
{
    auto la = std::make_shared<LaneAudio>();
    la->bpm = hostBpm; la->sr = hostSr;
    const int srcBars = sourceBars (s);
    const double srcBarSec = 4.0 * 60.0 / s.timing.tempo;
    const auto& orig = *s.original;
    const int n = orig.getNumSamples();
    // 1) rotate so bar 1 is at the start, then crop
    const int bars = s.cropBars > 0 ? juce::jmin (s.cropBars, srcBars) : juce::jmin (srcBars, 8);
    const int startBar = juce::jlimit (0, srcBars - 1, s.cropStart);
    const int segLen = (int) std::round (bars * srcBarSec * s.fileSr);
    const double startSec = s.timing.offsetSec + startBar * srcBarSec;
    juce::AudioBuffer<float> seg (2, juce::jmax (1, segLen));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < segLen; ++i)
        {
            long long idx = (long long) std::llround (startSec * s.fileSr) + i;
            idx %= n; if (idx < 0) idx += n;
            seg.setSample (c, i, orig.getSample (c, (int) idx));
        }
    // 2) sample rate, then tempo
    auto rs = resample (seg, hostSr / s.fileSr);
    const int outLen = (int) std::round (bars * 4.0 * 60.0 / hostBpm * hostSr);
    auto st = stretchLoop (rs, outLen, hostSr);
    // 3) key
    auto tp = transposeLoop (st, s.transpose, hostSr, s.role == rBass || s.role == rLead || s.role == rVocal);
    // 4) the user's nudge (which 16th is the 1)
    if (s.nudge != 0)
    {
        const double step = 60.0 / hostBpm / 4.0 * hostSr;
        long long sh = (long long) std::llround (s.nudge * step) % outLen; if (sh < 0) sh += outLen;
        juce::AudioBuffer<float> r (2, outLen);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < outLen; ++i) r.setSample (c, i, tp.getSample (c, (int) ((i + sh) % outLen)));
        tp = r;
    }
    la->loop = tp;
    la->bars = bars;
    return la;
}

inline std::vector<float> peaksOf (const LaneAudio& a, int buckets)
{
    std::vector<float> p ((size_t) buckets, 0.0f);
    const int n = a.loop.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const int b = (int) ((long long) i * buckets / n);
        p[(size_t) b] = juce::jmax (p[(size_t) b], std::abs (a.loop.getSample (0, i)), std::abs (a.loop.getSample (1, i)));
    }
    return p;
}
} // namespace arc2
