// Offline tests for Clarion + Clarion Room using a synthetic sung vocal (G minor melody)
// over a synthetic track in A minor.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/Processor.h"
#include "../Source/Editor.h"
#include "../Source/Room.h"
#include <cstdio>

static constexpr double SR = 48000.0;
static constexpr int BLOCK = 512;

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (SR).withNumChannels (2).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct Reson   // two-pole resonator for formants
{
    float b1 = 0, b2 = 0, a0 = 0, y1 = 0, y2 = 0;
    Reson (float f, float bw)
    {
        const float r = std::exp (-3.14159f * bw / (float) SR);
        b1 = 2 * r * std::cos (2 * 3.14159f * f / (float) SR);
        b2 = -r * r;
        a0 = 1 - r;
    }
    float operator() (float x) { const float y = a0 * x + b1 * y1 + b2 * y2; y2 = y1; y1 = y; return y; }
};

// Synthetic "ah" vocal singing a G minor phrase, with vibrato, breaths and an "s"
static juce::AudioBuffer<float> makeVocal (int total, float level)
{
    juce::AudioBuffer<float> b (2, total);
    // G4 Bb4 D5 C5 Bb4 A4 G4 D4 | F4 G4 Bb4 A4 G4 ...
    const int notes[] { 67, 70, 74, 72, 70, 69, 67, 62, 65, 67, 70, 69, 67, 67, 74, 70 };
    const float noteLen = 0.5f;
    Reson f1 (750, 90), f2 (1150, 110), f3 (2600, 160), f4 (3400, 250);
    juce::Random rnd (3);
    double ph = 0;
    for (int i = 0; i < total; ++i)
    {
        const float t = (float) i / (float) SR;
        const int idx = (int) (t / noteLen);
        const float within = t - idx * noteLen;
        const int note = notes[idx % 16];
        const bool rest = (idx % 8 == 7);
        const float vib = 0.2f * std::sin (2 * 3.14159f * 5.2f * t) * juce::jmin (1.0f, within * 4);
        const float hz = 440.0f * std::exp2 ((note + vib - 69) / 12.0f);
        ph += hz / SR;
        if (ph >= 1) ph -= 1;
        float src = (float) (ph < 0.08 ? 1.0 - ph / 0.08 : 0.0) - 0.08f;   // glottal-ish pulse
        float env = juce::jmin (1.0f, within * 20) * juce::jmin (1.0f, (noteLen - within) * 20);
        if (rest) env = 0;
        float v = (f1 (src) * 1.0f + f2 (src) * 0.6f + f3 (src) * 0.25f + f4 (src) * 0.12f) * env * 6.0f;
        if (idx % 8 == 3 && within < 0.08f) v += (rnd.nextFloat() * 2 - 1) * 0.15f * (1 - within / 0.08f);   // sibilant
        b.setSample (0, i, v * level);
        b.setSample (1, i, v * level);
    }
    return b;
}

// Track in A minor: Am - F - C - G pads, bass on roots, four-to-the-floor kick
static juce::AudioBuffer<float> makeMusic (int total, float level)
{
    juce::AudioBuffer<float> b (2, total);
    const int chords[4][3] { { 57, 60, 64 }, { 53, 57, 60 }, { 60, 64, 67 }, { 55, 59, 62 } };
    const int bass[4] { 45, 41, 48, 43 };
    const double beat = 60.0 / 124.0;
    double ph[4] {};
    for (int i = 0; i < total; ++i)
    {
        const double t = i / SR;
        const int bar = (int) (t / (beat * 4)) % 4;
        float pad = 0;
        for (int k = 0; k < 3; ++k)
        {
            ph[k] += 440.0 * std::exp2 ((chords[bar][k] - 69) / 12.0) / SR;
            pad += (float) (std::fmod (ph[k], 1.0) * 2 - 1);
        }
        ph[3] += 440.0 * std::exp2 ((bass[bar] - 69) / 12.0) / SR;
        const double bp = std::fmod (ph[3], 1.0);
        const float bs = (float) (std::sin (2 * juce::MathConstants<double>::pi * bp) * 0.5 + (bp * 2 - 1) * 0.15);
        const double tb = std::fmod (t, beat);
        const float kick = (float) (0.8 * std::exp (-tb / 0.2) * std::sin (2 * juce::MathConstants<double>::pi * (50 * tb + 2.5 * (1 - std::exp (-tb / 0.025))))) ;
        const float v = (pad * 0.12f + bs + kick) * level;
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

// Median pitch over voiced frames using the plugin's own tracker
static float medianPitch (const juce::AudioBuffer<float>& b, int start, int len)
{
    cl::PitchTracker tr;
    tr.prepare ((float) SR);
    std::vector<float> v;
    for (int i = start; i < start + len; ++i)
        if (tr.push (b.getSample (0, i)) && tr.voiced) v.push_back (tr.hz);
    if (v.empty()) return 0;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

static void setP (juce::AudioProcessorValueTreeState& s, const char* id, float v)
{
    auto* p = s.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (v));
}

static juce::AudioBuffer<float> runClarion (ClarionProcessor& p, const juce::AudioBuffer<float>& vox, const juce::AudioBuffer<float>& music, bool& bad)
{
    const int total = vox.getNumSamples();
    juce::AudioBuffer<float> out (2, total), buf (4, BLOCK);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += BLOCK)
    {
        const int n = juce::jmin (BLOCK, total - pos);
        buf.setSize (4, n, false, false, true);
        for (int c = 0; c < 2; ++c) { buf.copyFrom (c, 0, vox, c, pos, n); buf.copyFrom (c + 2, 0, music, c, pos, n); }
        p.processBlock (buf, midi);
        p.pollLearn();
        for (int c = 0; c < 2; ++c)
        {
            out.copyFrom (c, pos, buf, c, 0, n);
            for (int i = 0; i < n; ++i) if (! std::isfinite (buf.getSample (c, i))) bad = true;
        }
    }
    return out;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (std::getenv ("PSEXP"))
    {
        const float sr = 48000;
        for (float f0 : { 110.f, 220.f, 440.f, 600.f }) for (int st : { 2, -3, 5 })
        {
            cl::PitchTracker tr; tr.prepare (sr); cl::PitchShifter ps; ps.prepare (sr); cl::PitchTracker t2; t2.prepare (sr);
            double ph = 0; float period = 0; std::vector<float> outs;
            const float ratio = std::exp2 (st / 12.f);
            for (int i = 0; i < (int) (sr * 3); ++i)
            {
                ph += f0 / sr; if (ph >= 1) ph -= 1;
                float x = ((float) (ph < 0.08 ? 1 - ph / 0.08 : 0) - 0.08f) * 0.3f;
                if (tr.push (x)) period = tr.voiced ? tr.periodSamples() : 0;
                float l = x, r = x; ps.process (l, r, ratio, period);
                if (t2.push (l) && t2.voiced && i > sr) outs.push_back (t2.hz);
            }
            std::sort (outs.begin(), outs.end());
            const float m = outs.empty() ? 0 : outs[outs.size() / 2];
            std::printf ("f0 %5.0f st %+d -> %.2f Hz expected %.2f err %.1f c (period est %.2f true %.2f)\n", f0, st, m, f0 * ratio, 1200 * std::log2 (m / (f0 * ratio)), period, sr / f0);
        }
        return 0;
    }
    const int total = (int) (SR * 16.0);
    const auto vox = makeVocal (total, 0.25f);
    const auto music = makeMusic (total, 0.5f);
    int failures = 0;
    bool bad = false;
    auto check = [&] (bool ok, const char* what) { std::printf ("%-46s %s\n", what, ok ? "ok" : "FAIL"); if (! ok) ++failures; };

    // ---------------- 1) analysis + auto EQ + ride on the full chain
    ClarionProcessor p;
    p.setRateAndBufferSizeDetails (SR, BLOCK);
    p.prepareToPlay (SR, BLOCK);
    auto processed = runClarion (p, vox, music, bad);
    const auto vk = p.vocalKey(), rk = p.refKey();
    std::printf ("vocal key: %s %s (conf %.2f)   reference: %s %s   root %s   suggested shift %+d\n",
                 cl::noteName (vk.tonic), vk.minor ? "minor" : "major", vk.confidence,
                 cl::noteName (rk.tonic), rk.minor ? "minor" : "major", cl::noteName (p.refRoot()), p.suggestedShift());
    auto P = [&] (const char* id) { return p.apvts.getRawParameterValue (id)->load(); };
    std::printf ("auto EQ: hpf %.0f Hz  mud -%.1f dB  presence %+.1f dB  air %+.1f dB   rider %+.1f dB\n",
                 P (cp::hpf), P (cp::mud), P (cp::presence), P (cp::air), p.riderDb.load());
    check ((vk.tonic == 7 && vk.minor) || (vk.tonic == 10 && ! vk.minor), "vocal key = G minor (or relative Bb major)");
    check ((rk.tonic == 9 && rk.minor) || (rk.tonic == 0 && ! rk.minor), "reference key = A minor (or relative C major)");
    check (p.refRoot() == 9 || p.refRoot() == 0, "reference root = A or its relative C");
    check (p.suggestedShift() == 2, "suggested key-match shift = +2 st");
    check (p.hasAutoEq.load(), "auto EQ learned");
    check (! bad, "no NaN/inf");

    // ---------------- 2) pitch shift accuracy (+2 st) and transparency (0 st)
    for (int shift : { 2, -3 })
    {
        ClarionProcessor q;
        q.prepareToPlay (SR, BLOCK);
        setP (q.apvts, cp::shift, (float) shift);
        setP (q.apvts, cp::ride, 0);
        auto o = runClarion (q, vox, juce::AudioBuffer<float> (2, total), bad);
        const int lat = q.getLatencySamples();
        const float pin = medianPitch (vox, (int) SR, (int) (SR * 8));
        const float pout = medianPitch (o, (int) SR + lat, (int) (SR * 8));
        const float err = 1200.0f * std::log2 (pout / pin) - 100.0f * shift;
        std::printf ("shift %+d: median in %.1f Hz, out %.1f Hz -> error %.1f cents\n", shift, pin, pout, err);
        check (std::abs (err) < 15.0f, shift > 0 ? "pitch shift +2 st accurate (<15 cents)" : "pitch shift -3 st accurate (<15 cents)");
    }
    {
        ClarionProcessor q;
        q.prepareToPlay (SR, BLOCK);
        setP (q.apvts, cp::hpf, 30); setP (q.apvts, cp::mud, 0); setP (q.apvts, cp::presence, 0); setP (q.apvts, cp::air, 0);
        setP (q.apvts, cp::deess, 0); setP (q.apvts, cp::clarity, 0); setP (q.apvts, cp::parMix, 0); setP (q.apvts, cp::ride, 0);
        q.hasAutoEq = true; q.autoEqState = 3;
        auto o = runClarion (q, vox, juce::AudioBuffer<float> (2, total), bad);
        const int lat = q.getLatencySamples();
        double e = 0, s = 0;
        for (int i = (int) SR; i < total - lat; ++i)
        {
            const float ref = vox.getSample (0, i), d = o.getSample (0, i + lat) - ref;
            e += d * d; s += ref * ref;
        }
        const float db = (float) (10 * std::log10 (e / s + 1e-20));
        double so = 0;
        for (int i = (int) SR; i < total - lat; ++i) so += (double) o.getSample (0, i + lat) * o.getSample (0, i + lat);
        const float lvl = (float) (10 * std::log10 (so / s));
        std::printf ("shift 0, flat settings: level change %+.2f dB (latency %d samples = %.1f ms)\n", lvl, lat, 1000.0 * lat / SR);
        check (std::abs (lvl) < 0.5f, "flat settings keep the vocal level");
        juce::ignoreUnused (db);
        // the pitch engine alone at 0 st must be a pure delay
        cl::PitchShifter ps; ps.prepare ((float) SR);
        double err = 0, sig = 0;
        for (int i = 0; i < total; ++i)
        {
            float l = vox.getSample (0, i), r = l;
            ps.process (l, r, 1.0f, 200.0f);
            if (i >= ps.getLatency()) { const float ref = vox.getSample (0, i - ps.getLatency()); err += (l - ref) * (l - ref); sig += ref * ref; }
        }
        const float pdb = (float) (10 * std::log10 (err / sig + 1e-30));
        std::printf ("pitch engine at 0 st: residual %.1f dB\n", pdb);
        check (pdb < -100.0f, "pitch engine is a pure delay at 0 st");
    }

    // ---------------- 3) rider: quiet vocal against loud music gets lifted
    {
        ClarionProcessor q;
        q.prepareToPlay (SR, BLOCK);
        setP (q.apvts, cp::range, 12); setP (q.apvts, cp::target, 3);
        juce::AudioBuffer<float> quiet (vox);
        quiet.applyGain (0.25f);
        runClarion (q, quiet, music, bad);
        std::printf ("rider on a -12 dB vocal: %+.1f dB\n", q.riderDb.load());
        check (q.riderDb.load() > 4.0f, "rider lifts a buried vocal");
    }

    // ---------------- 4) Room: carves the music only while the vocal sings
    juce::AudioBuffer<float> roomOut (2, total);
    {
        RoomProcessor r;
        r.setRateAndBufferSizeDetails (SR, BLOCK);
        r.prepareToPlay (SR, BLOCK);
        juce::AudioBuffer<float> buf (4, BLOCK);
        juce::MidiBuffer midi;
        float carveSinging = 0, carveRest = 0;
        for (int pos = 0; pos < total; pos += BLOCK)
        {
            const int n = juce::jmin (BLOCK, total - pos);
            buf.setSize (4, n, false, false, true);
            for (int c = 0; c < 2; ++c) { buf.copyFrom (c, 0, music, c, pos, n); buf.copyFrom (c + 2, 0, processed, c, pos, n); }
            r.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) roomOut.copyFrom (c, pos, buf, c, 0, n);
            const float t = (float) pos / (float) SR;
            const int idx = (int) (t / 0.5f);
            const float within = t - idx * 0.5f;
            const float c3 = r.carveDb[3].load();
            if (t > 2 && within > 0.15f && within < 0.3f && idx % 8 != 7) carveSinging = juce::jmax (carveSinging, c3);
            if (t > 2 && idx % 8 == 7 && within > 0.4f) carveRest = juce::jmax (carveRest, c3);
        }
        std::printf ("room carve at 3 kHz: while singing %.1f dB, in rests %.1f dB\n", carveSinging, carveRest);
        check (carveSinging > 2.0f && carveRest < carveSinging * 0.6f, "room carves while singing, releases in rests");
    }

    // ---------------- renders for listening
    if (argc > 1)
    {
        juce::File dir (argv[1]);
        dir.createDirectory();
        const int lat = p.getLatencySamples();
        juce::AudioBuffer<float> before (2, total), after (2, total), shifted (2, total);
        ClarionProcessor q;
        q.prepareToPlay (SR, BLOCK);
        setP (q.apvts, cp::shift, 2);
        auto o2 = runClarion (q, vox, music, bad);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
            {
                const float m = music.getSample (c, i), v = vox.getSample (c, i);
                before.setSample (c, i, 0.6f * (m + v));
                const float pv = i + lat < total ? processed.getSample (c, i + lat) : 0.0f;
                const float rv = i + lat < total ? o2.getSample (c, i + lat) : 0.0f;
                after.setSample (c, i, 0.6f * (roomOut.getSample (c, i) + pv));
                shifted.setSample (c, i, 0.6f * (m + rv));
            }
        writeWav (dir.getChildFile ("1_before.wav"), before);
        writeWav (dir.getChildFile ("2_clarion_and_room.wav"), after);
        writeWav (dir.getChildFile ("3_key_matched_plus2.wav"), shifted);
    }

    if (auto* snap = std::getenv ("SNAPSHOT"))
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        if (auto* e = dynamic_cast<ClarionEditor*> (ed.get())) e->refreshNow();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        juce::File f (snap); f.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    }
    if (auto* snap = std::getenv ("SNAPSHOT_ROOM"))
    {
        RoomProcessor r;
        r.prepareToPlay (SR, BLOCK);
        juce::AudioBuffer<float> buf (4, BLOCK); juce::MidiBuffer midi;
        for (int pos = 0; pos < (int) (SR * 3.15); pos += BLOCK)
        {
            for (int c = 0; c < 2; ++c) { buf.copyFrom (c, 0, music, c, pos, BLOCK); buf.copyFrom (c + 2, 0, processed, c, pos, BLOCK); }
            r.processBlock (buf, midi);
        }
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.createEditor());
        if (auto* e = dynamic_cast<RoomEditor*> (ed.get())) e->refreshNow();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        juce::File f (snap); f.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
    }

    std::printf ("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
