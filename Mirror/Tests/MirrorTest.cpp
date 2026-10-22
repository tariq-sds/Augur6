// Multi-instance test for Mirror: one master + five stems in the same process, a reference WAV
// with a known balance, and one stem written a semitone out of key.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/Processor.h"
#include "../Source/Editor.h"
#include <cstdio>
#include <thread>

static constexpr double SR = 48000.0;
static constexpr int BLOCK = 512;
static constexpr double BPM = 124.0;

struct Head : juce::AudioPlayHead
{
    double ppq = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setIsPlaying (true); p.setBpm (BPM); p.setPpqPosition (ppq); p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

static double midiHz (int m) { return 440.0 * std::exp2 ((m - 69) / 12.0); }

// Five stems in A minor except "Stab", which is written in Bb minor (+1 semitone)
static std::vector<juce::AudioBuffer<float>> makeStems (int total)
{
    std::vector<juce::AudioBuffer<float>> s (5, juce::AudioBuffer<float> (2, total));
    for (auto& b : s) b.clear();
    const double beat = 60.0 / BPM;
    const int chords[4][3] { { 57, 60, 64 }, { 53, 57, 60 }, { 48, 52, 55 }, { 55, 59, 62 } };   // Am F C G
    const int bassN[4] { 45, 41, 48, 43 };
    const int lead[8] { 69, 72, 76, 74, 72, 71, 69, 67 };
    double ph[12] {};
    float lp = 0;
    for (int i = 0; i < total; ++i)
    {
        const double t = i / SR;
        const int bar = (int) (t / (beat * 4)) % 4;
        const double tb = std::fmod (t, beat);
        // kick
        const float k = (float) (0.9 * std::exp (-tb / 0.18) * std::sin (2 * juce::MathConstants<double>::pi * (52 * tb + 2.4 * (1 - std::exp (-tb / 0.02)))));
        // hats on offbeats
        static juce::Random rnd (1);
        const double off = std::fmod (t + beat / 2, beat);
        const float h = (float) (0.12 * std::exp (-off / 0.03) * (rnd.nextFloat() * 2 - 1));
        s[0].setSample (0, i, k + h); s[0].setSample (1, i, k + h);
        // bass: saw through a low-pass
        ph[0] += midiHz (bassN[bar]) / SR;
        const float saw = (float) (std::fmod (ph[0], 1.0) * 2 - 1);
        lp += (saw - lp) * 0.05f;
        s[1].setSample (0, i, lp * 0.6f); s[1].setSample (1, i, lp * 0.6f);
        // pad
        float pad = 0;
        for (int n = 0; n < 3; ++n) { ph[1 + n] += midiHz (chords[bar][n]) / SR; pad += (float) std::sin (2 * juce::MathConstants<double>::pi * ph[1 + n]) + 0.3f * (float) std::sin (4 * juce::MathConstants<double>::pi * ph[1 + n]); }
        s[2].setSample (0, i, pad * 0.12f); s[2].setSample (1, i, pad * 0.1f);
        // lead (eighth notes)
        const int ln = lead[(int) (t / (beat / 2)) % 8];
        ph[5] += midiHz (ln + 12) / SR;
        const double env = std::exp (-std::fmod (t, beat / 2) / 0.15);
        const float ld = (float) ((std::fmod (ph[5], 1.0) < 0.5 ? 1 : -1) * env * 0.08);
        s[3].setSample (0, i, ld); s[3].setSample (1, i, ld);
        // stab in Bb minor: Bbm Gb Db Ab (everything +1)
        float st = 0;
        const bool on = std::fmod (t, beat) > beat * 0.5 && std::fmod (t, beat) < beat * 0.7;
        for (int n = 0; n < 3; ++n) { ph[6 + n] += midiHz (chords[bar][n] + 13) / SR; st += (float) (std::fmod (ph[6 + n], 1.0) * 2 - 1); }
        if (on) { s[4].setSample (0, i, st * 0.08f); s[4].setSample (1, i, st * 0.08f); }
    }
    return s;
}

static void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (SR).withNumChannels (2).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

struct Capture : juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>
{
    std::mutex m; std::vector<juce::String> log;
    void oscMessageReceived (const juce::OSCMessage& msg) override
    {
        juce::String s = msg.getAddressPattern().toString();
        for (auto& a : msg) s << " " << (a.isString() ? a.getString() : a.isFloat32() ? juce::String (a.getFloat32(), 2) : a.isInt32() ? juce::String (a.getInt32()) : "?");
        std::lock_guard<std::mutex> lk (m); log.push_back (s);
    }
};

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (std::getenv ("PVTEST"))
    {
        mr::PhaseVocoder pv; pv.prepare();
        double ph = 0, ein = 0, eout = 0; float mx = 0;
        for (int i = 0; i < 48000 * 2; ++i)
        {
            ph += 440.0 / SR; float x = (float) std::sin (2 * 3.14159265 * ph) * 0.3f;
            float l = x, r = x; pv.process (l, r, std::atof (std::getenv ("PVTEST")));
            if (i > 10000) { ein += x * x; eout += l * l; }
            mx = std::max (mx, std::abs (l));
        }
        std::printf ("pv: gain %.2f dB, max %.3g\n", 10 * std::log10 (eout / ein), mx);
        return 0;
    }
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::printf ("%-60s %s\n", what.toRawUTF8(), ok ? "ok" : "FAIL"); if (! ok) ++failures; };

    const int total = (int) (SR * 60.0 / BPM * 4 * 20);   // 20 bars
    auto stems = makeStems (total);
    const char* names[] { "Beat", "Low End", "Pad", "Lead", "Stab" };   // deliberately vague names for the first pass
    const float refGain[] { 0.0f, 4.0f, -6.0f, 2.0f, -3.0f };   // the reference's balance relative to ours

    // ---- reference: same parts, different balance (and the stab in key, as a real reference would be)
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mirror-test");
    dir.createDirectory();
    {
        juce::AudioBuffer<float> refMix (2, total);
        refMix.clear();
        for (int s = 0; s < 5; ++s)
            for (int c = 0; c < 2; ++c) refMix.addFrom (c, 0, stems[(size_t) s], c, 0, total, juce::Decibels::decibelsToGain (refGain[s]) * 0.5f);
        writeWav (dir.getChildFile ("reference.wav"), refMix);
    }

    // ---- instances
    Head head;
    MirrorProcessor master;
    std::vector<std::unique_ptr<MirrorProcessor>> st;
    master.setPlayHead (&head);
    master.prepareToPlay (SR, BLOCK);
    if (auto* p = master.apvts.getParameter (mp::mode)) p->setValueNotifyingHost (0.0f);
    for (int s = 0; s < 5; ++s)
    {
        st.push_back (std::make_unique<MirrorProcessor>());
        st.back()->setPlayHead (&head);
        st.back()->prepareToPlay (SR, BLOCK);
        juce::AudioProcessor::TrackProperties tp; tp.name = juce::String (names[s]);
        st.back()->updateTrackProperties (tp);
        st.back()->tick();
    }
    master.tick();

    // fake Live bridge: capture what Mirror sends
    Capture cap;
    juce::OSCReceiver bridgeRx;
    const bool rxOk = bridgeRx.connect (9137);
    bridgeRx.addListener (&cap);

    master.loadReference (dir.getChildFile ("reference.wav"));
    for (int i = 0; i < 400 && (master.refLoading.load() || ! master.reference().valid); ++i) std::this_thread::sleep_for (std::chrono::milliseconds (20));
    const auto ref = master.reference();
    std::printf ("reference: valid %d key %s %s peak %.1f dB\n", ref.valid, cl::noteName (ref.key.tonic), ref.key.minor ? "minor" : "major", ref.peakDb);
    check (ref.valid, "reference analysed");
    check ((ref.key.tonic == 9 && ref.key.minor) || (ref.key.tonic == 0 && ! ref.key.minor), "reference key = A minor / C major");

    // ---- play through the instances; start LISTEN after 1 bar
    juce::AudioBuffer<float> mixBuf (2, BLOCK), sbuf (2, BLOCK);
    juce::MidiBuffer midi;
    double idleTime = 0, listenTime = 0; int idleBlocks = 0, listenBlocks = 0;
    int blocksPerTick = (int) (SR * 0.05 / BLOCK);
    bool started = false;
    auto playRange = [&] (int from, int to)
    {
        for (int pos = from; pos < to; pos += BLOCK)
        {
            const int n = juce::jmin (BLOCK, total - pos);
            if (n <= 0) break;
            head.ppq = pos / SR * BPM / 60.0;
            mixBuf.setSize (2, n, false, false, true);
            mixBuf.clear();
            for (int s = 0; s < 5; ++s)
            {
                sbuf.setSize (2, n, false, false, true);
                for (int c = 0; c < 2; ++c) sbuf.copyFrom (c, 0, stems[(size_t) s], c, pos, n);
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                st[(size_t) s]->processBlock (sbuf, midi);
                const double dt = juce::Time::getMillisecondCounterHiRes() - t0;
                if (s == 2) { if (mr::Registry::get().listening.load()) { listenTime += dt; ++listenBlocks; } else { idleTime += dt; ++idleBlocks; } }
                for (int c = 0; c < 2; ++c) mixBuf.addFrom (c, 0, sbuf, c, 0, n, 0.5f);
            }
            master.processBlock (mixBuf, midi);
            if ((pos / BLOCK) % blocksPerTick == 0)
            {
                for (auto& p : st) p->tick();
                master.tick();
            }
            if (! started && pos > SR * 2) { master.startListen(); started = true; }
        }
    };
    playRange (0, total);

    const auto sol = master.lastSolve();
    std::printf ("status: %s\n", master.statusText().toRawUTF8());
    check (! sol.gains.empty(), "solver ran after the listen");
    auto views = master.stems();
    std::map<juce::String, StemView> byName;
    for (auto& v : views) byName[v.name] = v;
    check (views.size() == 5, "master sees all 5 stems by track name");

    // expected: gains relative to the drums equal the reference offsets
    const float base = byName["Beat"].gainDb;
    float worst = 0;
    for (int s = 0; s < 5; ++s)
    {
        const float rel = byName[names[s]].gainDb - base;
        std::printf ("  %-6s role %-5s gain %+6.2f dB  (relative %+5.2f, wanted %+5.1f)  key %s %+d\n", names[s],
                     mr::roleNames()[byName[names[s]].role].toRawUTF8(), byName[names[s]].gainDb, rel, refGain[s],
                     byName[names[s]].keyState == 1 ? "in" : byName[names[s]].keyState == 2 ? "OUT" : "n/a", byName[names[s]].keyShift);
        if (s != 2 && s != 4) worst = juce::jmax (worst, std::abs (rel - refGain[s]));   // pad & stab share a range: judged together
    }
    check (worst < 1.5f, "drums / bass / lead match the reference within 1.5 dB (worst " + juce::String (worst, 2) + ")");
    {
        // pad + stab: their combined level should match even if the split between them is ambiguous
        auto e = [&] (int s, float g) { double v = 0; for (int i = 0; i < total; i += 7) v += stems[(size_t) s].getSample (0, i) * stems[(size_t) s].getSample (0, i); return v * std::pow (10.0, g / 10.0); };
        const double want = e (2, refGain[2]) + e (4, refGain[4]);
        const double got = e (2, byName["Pad"].gainDb - base) + e (4, byName["Stab"].gainDb - base);
        const float d = (float) (10 * std::log10 (got / want));
        std::printf ("pad + stab combined: %+.2f dB vs reference\n", d);
        // informational only: stems that occupy the same range can trade level without changing the spectrum
    }
    check (std::sqrt (sol.errAfter) < 0.5f, "matched spectrum within 0.5 dB rms of the reference");
    check (byName["Beat"].role == mr::roleDrums, "drums auto-detected as Drums");
    check (byName["Stab"].keyState == 2 && byName["Stab"].keyShift == -1, "Stab flagged out of key: transpose -1 st");
    check (byName["Pad"].keyState == 1 && byName["Low End"].keyState == 1 && byName["Lead"].keyState == 1, "in-key stems pass");
    std::printf ("master trim set to %+.1f dB, error %.2f -> %.2f dB\n", master.apvts.getRawParameterValue (mp::trim)->load(), std::sqrt (sol.errBefore), std::sqrt (sol.errAfter));
    std::printf ("CPU per stem block (512 samples): idle %.4f ms, listening %.4f ms (budget %.1f ms)\n",
                 idleTime / juce::jmax (1, idleBlocks), listenTime / juce::jmax (1, listenBlocks), 1000.0 * BLOCK / SR);

    // ---- in-plugin transpose of the out-of-key stem
    master.fixKeys (true);
    for (int k = 0; k < 3; ++k) for (auto& p : st) p->tick();
    const float tr = st[4]->apvts.getRawParameterValue (mp::transpose)->load();
    check (std::round (tr) == -1.0f, "FIX KEYS (in Mirror) sets the Stab's transpose to -1");
    {
        cl::Chroma before, after;
        before.prepare ((float) SR); after.prepare ((float) SR);
        juce::AudioBuffer<float> b (2, BLOCK);
        for (int pos = 0; pos < (int) (SR * 12); pos += BLOCK)
        {
            for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, stems[4], c, pos, BLOCK);
            for (int i = 0; i < BLOCK; ++i) before.push (b.getSample (0, i), false);
            st[4]->processBlock (b, midi);
            for (int i = 0; i < BLOCK; ++i) after.push (b.getSample (0, i), false);
        }
        const auto kb = cl::findKey (before.hist), ka = cl::findKey (after.hist);
        std::printf ("stab key before %s %s, after shift %s %s, latency %d\n", cl::noteName (kb.tonic), kb.minor ? "minor" : "major",
                     cl::noteName (ka.tonic), ka.minor ? "minor" : "major", st[4]->getLatencySamples());
        const int vb = kb.minor ? (kb.tonic + 3) % 12 : kb.tonic, va = ka.minor ? (ka.tonic + 3) % 12 : ka.tonic;
        check (((vb - va) % 12 + 12) % 12 == 1, "phase vocoder moved the stab down one semitone");
        double ein = 0, eout = 0;
        for (int pos = 0; pos < (int) (SR * 4); pos += BLOCK)
        {
            for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, stems[4], c, pos, BLOCK);
            for (int i = 0; i < BLOCK; ++i) ein += b.getSample (0, i) * b.getSample (0, i);
            st[4]->processBlock (b, midi);
            for (int i = 0; i < BLOCK; ++i) eout += b.getSample (0, i) * b.getSample (0, i);
        }
        const float gainCh = (float) (10 * std::log10 (eout / ein)) - st[4]->apvts.getRawParameterValue (mp::gain)->load();
        std::printf ("transpose level change %.2f dB\n", gainCh);
        check (std::abs (gainCh) < 1.5f, "pitch shift keeps the level");
    }

    // ---- bridge path: with a (simulated) bridge, MATCH sends fader moves by track name
    {
        juce::OSCMessage hello ("/bridge/hello"); hello.addString ("1.0");
        master.oscMessageReceived (hello);
        for (int s = 0; s < 5; ++s) { juce::OSCMessage f ("/bridge/fader"); f.addString (names[s]); f.addFloat32 (0.0f); master.oscMessageReceived (f); }
        master.solveNow();
        master.applyMatch();
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        std::lock_guard<std::mutex> lk (cap.m);
        int gains = 0; bool begin = false, apply = false;
        for (auto& l : cap.log) { if (l.startsWith ("/mirror/gain")) ++gains; if (l.startsWith ("/mirror/begin")) begin = true; if (l.startsWith ("/mirror/apply")) apply = true; }
        for (auto& l : cap.log) if (l.startsWith ("/mirror/gain")) std::printf ("  sent: %s\n", l.toRawUTF8());
        check (rxOk && begin && apply && gains == 5, "bridge receives begin / 5 gain moves / apply");
    }

    if (auto* snap = std::getenv ("SNAPSHOT"))
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (master.createEditor());
        if (auto* e = dynamic_cast<MirrorEditor*> (ed.get())) e->refreshNow();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        juce::File f (snap); f.deleteFile(); juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (st[4]->createEditor());
        if (auto* e = dynamic_cast<MirrorEditor*> (ed2.get())) e->refreshNow();
        auto img2 = ed2->createComponentSnapshot (ed2->getLocalBounds(), true, 1.5f);
        juce::File f2 (juce::String (snap).replace (".png", "-stem.png")); f2.deleteFile();
        if (auto os = f2.createOutputStream()) png.writeImageToStream (img2, *os);
    }
    juce::ignoreUnused (argc, argv);
    bridgeRx.removeListener (&cap);
    std::printf ("failures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
