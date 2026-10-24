// Arc 2 test: synthetic stems (with a non-project tempo and a delayed bar 1) go in, a full arrangement comes out.
// Checks tempo lock, bar 1, roles, key lock, the plan, the renderer, exports, undo, hold/dice, state and CPU.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../Source/Processor.h"
#include "../Source/Editor.h"
#define ARC_TAP_NO_ENTRY 1
#include "../Tap/Tap.cpp"
#include <cstdio>

using namespace arc2;
static constexpr double SR = 44100.0, SRC_BPM = 124.0, HOST_BPM = 120.0, HOST_SR = 48000.0;
static int failures = 0;
static void check (bool ok, const juce::String& w) { std::printf ("%-72s %s\n", w.toRawUTF8(), ok ? "ok" : "FAIL"); if (! ok) ++failures; }
static const double kPi2 = 2 * juce::MathConstants<double>::pi;

static void writeWav (const juce::File& f, const std::vector<float>& x)
{
    f.deleteFile();
    juce::AudioBuffer<float> b (2, (int) x.size());
    for (int i = 0; i < (int) x.size(); ++i) { b.setSample (0, i, x[(size_t) i]); b.setSample (1, i, x[(size_t) i]); }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (SR).withNumChannels (2).withBitsPerSample (24);
    if (auto w = wav.createWriterFor (os, opts)) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

static double spb() { return 60.0 / SRC_BPM; }
static int loopLen (int bars) { return (int) std::round (bars * 4 * spb() * SR); }
// a loop whose bar 1 starts `offsetBeats` into the file (the content wraps round)
static void place (std::vector<float>& x, double beat, double offsetBeats, const std::function<float (double)>& v, double dur)
{
    const int n = (int) x.size();
    const int s0 = (int) std::round ((beat + offsetBeats) * spb() * SR);
    for (int i = 0; i < (int) (dur * SR); ++i) { int k = (s0 + i) % n; x[(size_t) k] += v (i / SR); }
}
static std::vector<float> kickLoop (double off)
{
    std::vector<float> x ((size_t) loopLen (4), 0);
    for (int b = 0; b < 16; ++b) place (x, b, off, [] (double t) { return (float) (0.9 * std::sin (kPi2 * (52 * t + 2.2 * (1 - std::exp (-t / 0.03)))) * std::exp (-t / 0.18)); }, 0.4);
    return x;
}
static std::vector<float> hatLoop (double off)
{
    std::vector<float> x ((size_t) loopLen (4), 0);
    juce::Random r (2);
    for (int b = 0; b < 16; ++b) { float hp = 0; place (x, b + 0.5, off, [&] (double t) { const float w = r.nextFloat() * 2 - 1, h = w - hp; hp = w; return h * 0.3f * (float) std::exp (-t / 0.04); }, 0.15); }
    return x;
}
static std::vector<float> clapLoop (double off)
{
    std::vector<float> x ((size_t) loopLen (4), 0);
    juce::Random r (4);
    for (int b = 1; b < 16; b += 2) place (x, b, off, [&] (double t) { return (r.nextFloat() * 2 - 1) * 0.5f * (float) std::exp (-t / 0.1) + 0.2f * (float) std::sin (kPi2 * 900 * t) * (float) std::exp (-t / 0.02); }, 0.3);
    return x;
}
static std::vector<float> tonalLoop (const std::vector<std::pair<int, double>>& notes, double len, int voices, double off)   // (midi, start beat), all `len` long
{
    std::vector<float> x ((size_t) loopLen (4), 0);
    for (auto& nt : notes)
        for (int v = 0; v < voices; ++v)
        {
            const int midi = nt.first + (v == 0 ? 0 : v == 1 ? 3 : 7);   // minor triad when voices = 3
            const double f = 440 * std::exp2 ((midi - 69) / 12.0);
            place (x, nt.second, off, [f, len] (double t) { const double env = std::min (1.0, t / 0.005) * (t > len * spb() - 0.01 ? std::max (0.0, 1 - (t - len * spb() + 0.01) / 0.01) : 1.0);
                                                     return (float) (0.25 * env * (std::sin (kPi2 * f * t) + 0.4 * std::sin (2 * kPi2 * f * t) + 0.2 * std::sin (3 * kPi2 * f * t))); }, len * spb());
        }
    return x;
}

static void waitJobs (ArcProcessor& p) { for (int k = 0; k < 6000 && p.jobs.load() > 0; ++k) juce::Thread::sleep (5); }

// where the hits are in a buffer (sample of each onset)
static std::vector<int> onsets (const juce::AudioBuffer<float>& b, float thresh)
{
    // (a hit within 3 ms of the end is bar 1's hit wrapping round, not a new one)
    std::vector<int> o;
    float env = 0; bool above = false; int last = -100000;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float a = std::abs (b.getSample (0, i));
        env = juce::jmax (a, env * 0.999f);
        if (! above && a > thresh && i - last > 2000 && i < b.getNumSamples() - 144) { o.push_back (i); last = i; above = true; }
        if (above && env < thresh * 0.3f) above = false;
    }
    return o;
}

static float yinHz (const juce::AudioBuffer<float>& b, int from, int n, double sr)
{
    cl::PitchTracker t; t.prepare ((float) sr);
    double sum = 0; int cnt = 0;
    for (int i = from; i < from + n && i < b.getNumSamples(); ++i)
        if (t.push (b.getSample (0, i)) && t.voiced) { sum += t.hz; ++cnt; }
    return cnt ? (float) (sum / cnt) : 0.0f;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("arc2test");
    dir.deleteRecursively(); dir.createDirectory();

    // ---- names -> roles
    check (roleFromName ("Kick_124") == rKick && roleFromName ("Sub Bass Am") == rBass && roleFromName ("closed hat loop") == rHats
        && roleFromName ("Clap 02") == rClap && roleFromName ("Vox chop") == rVocal && roleFromName ("Pad Chords") == rChords
        && roleFromName ("Lead Arp") == rLead && roleFromName ("Top loop") == rTops && roleFromName ("riser") == rFx && roleFromName ("Metal Hit") == rPerc && roleFromName ("xyz") == -1,
           "roles from file names");

    // ---- stretch keeps the grid: a click every beat at 124 BPM -> 120 BPM
    {
        juce::AudioBuffer<float> c (2, (int) std::round (16 * 60.0 / 124 * HOST_SR)); c.clear();
        for (int b = 0; b < 16; ++b) { const int s = (int) std::round (b * 60.0 / 124 * HOST_SR); for (int i = 0; i < 400; ++i) { const float v = (float) std::exp (-i / 60.0) * (float) std::sin (i * 0.3); c.setSample (0, s + i, v); c.setSample (1, s + i, v); } }
        const int out = (int) std::round (16 * 60.0 / 120 * HOST_SR);
        auto st = stretchLoop (c, out, HOST_SR);
        auto o = onsets (st, 0.3f);
        double worst = 0;
        for (int k = 0; k < (int) o.size(); ++k) { const double ideal = std::round (o[(size_t) k] / (HOST_SR * 0.5)) * HOST_SR * 0.5; worst = std::max (worst, std::abs (o[(size_t) k] - ideal)); if (getenv ("ARC_DEBUG")) std::printf ("   click %d %+.2f ms\n", k, (o[(size_t) k] - ideal) / HOST_SR * 1000); }
        check (st.getNumSamples() == out && o.size() == 16 && worst < 0.006 * HOST_SR, "stretch 124 -> 120 BPM: 16 beats, worst " + juce::String (worst / HOST_SR * 1000, 1) + " ms off grid");
    }

    // ---- transpose: a 220 Hz tone down 3 semitones
    {
        juce::AudioBuffer<float> t (2, (int) (2 * HOST_SR));
        for (int i = 0; i < t.getNumSamples(); ++i) { const float v = 0.4f * (float) std::sin (kPi2 * 220 * i / HOST_SR); t.setSample (0, i, v); t.setSample (1, i, v); }
        auto d = transposeLoop (t, -3);
        auto sx = stretchLoop (t, (int) (t.getNumSamples() * 124.0 / 120.0), HOST_SR);
        const float hzS = yinHz (sx, (int) (0.2 * HOST_SR), (int) HOST_SR, HOST_SR);
        check (std::abs (hzS - 220) < 1.5f, "stretch keeps pitch: " + juce::String (hzS, 1) + " Hz");
        const float hz = yinHz (d, (int) (0.2 * HOST_SR), (int) HOST_SR, HOST_SR), want = 220.0f * std::exp2 (-3 / 12.0f);
        check (std::abs (hz - want) < 2, "transpose 220 Hz by -3: " + juce::String (hz, 1) + " Hz (want " + juce::String (want, 1) + ")");
        juce::AudioBuffer<float> b (2, (int) (2 * HOST_SR));
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.4f * (float) std::sin (kPi2 * 110 * i / HOST_SR); b.setSample (0, i, v); b.setSample (1, i, v); }
        auto bd = transposeLoop (b, -3, HOST_SR, true);
        const float hzb = yinHz (bd, (int) (0.2 * HOST_SR), (int) HOST_SR, HOST_SR), wantb = 110.0f * std::exp2 (-3 / 12.0f);
        check (bd.getNumSamples() == b.getNumSamples() && std::abs (hzb - wantb) < 1.5f, "bass-style transpose 110 Hz by -3: " + juce::String (hzb, 1) + " Hz (want " + juce::String (wantb, 1) + ")");
    }

    // ---- the stems: 124 BPM, 4 bars, bounced from bar 1 like real loops (hats and bass on the offbeat, so those files start silent)
    const double off = 0;
    writeWav (dir.getChildFile ("Kick 124.wav"), kickLoop (off));
    writeWav (dir.getChildFile ("Hats.wav"), hatLoop (off));
    writeWav (dir.getChildFile ("Clap.wav"), clapLoop (off));
    // offbeat bass in C minor (C, Eb, G, Bb); chords Cm / Fm / Gm / Cm
    std::vector<std::pair<int, double>> bassN, chordN;
    const int bassSeq[] { 36, 36, 48, 36, 39, 39, 43, 46 };
    for (int b = 0; b < 16; ++b) bassN.push_back ({ bassSeq[b % 8], b + 0.5 });
    for (int bar = 0; bar < 4; ++bar) { static const int roots[] { 60, 65, 67, 60 }; chordN.push_back ({ roots[bar], bar * 4.0 }); }
    writeWav (dir.getChildFile ("Bass.wav"), tonalLoop (bassN, 0.4, 1, off));
    writeWav (dir.getChildFile ("Chords.wav"), tonalLoop (chordN, 3.8, 3, off));

    ArcProcessor proc;
    proc.prepareToPlay (HOST_SR, 512);
    proc.syncTempo (HOST_BPM, HOST_SR);
    for (auto n : { "Kick 124.wav", "Hats.wav", "Clap.wav", "Bass.wav", "Chords.wav" })
        check (proc.addFileSync (dir.getChildFile (n)), juce::String ("load ") + n);
    waitJobs (proc);
    auto st = proc.stems();
    check (st.size() == 5, "five lanes");
    check (st.size() == 5 && st[0].role == rKick && st[1].role == rBass && st[2].role == rHats && st[3].role == rClap && st[4].role == rChords, "lanes sorted kick, bass, hats, clap, chords");
    for (auto& s : st)
        std::printf ("    %-8s %-8s %s  key %s%s  transpose %d\n", roleName (s.role), s.name.toRawUTF8(), s.report.toRawUTF8(),
                     s.key.tonic >= 0 ? cl::noteName (s.key.tonic) : "-", s.key.minor ? "m" : "", s.transpose);
    bool allExact = st.size() == 5;
    for (auto& s : st) allExact &= s.timing.bars == 4 && std::abs (s.timing.tempo - SRC_BPM) < 0.01 && s.timing.offsetSec == 0;
    check (allExact, "every stem: exactly 4 bars at 124 BPM, bar 1 = first sample");
    check (st.size() == 5 && st[0].vary == 0, "kick defaults to STRAIGHT");
    {
        // a drum loop bounced late (a pre-roll of 1.25 beats): bar 1 is found
        auto late = kickLoop (1.25); auto cl2 = clapLoop (1.25);
        for (size_t i = 0; i < late.size(); ++i) late[i] += cl2[i];
        writeWav (dir.getChildFile ("Drum loop late.wav"), late);
        Stem d; check (loadStem (d, dir.getChildFile ("Drum loop late.wav"), HOST_BPM) && d.role == rDrums, "late drum loop loads as DRUMS");
        check (d.timing.shifted && std::abs (d.timing.offsetSec - 1.25 * spb()) < 0.004, "late drum loop: bar 1 found " + juce::String (d.timing.offsetSec * 1000, 1) + " ms in (true " + juce::String (1.25 * spb() * 1000, 1) + ")");
        // long stems: 64 bars at 124 with Live at 120 must not be read as 62 bars at 120
        std::vector<float> longK; for (int k = 0; k < 16; ++k) { auto l = kickLoop (0); longK.insert (longK.end(), l.begin(), l.end()); }
        writeWav (dir.getChildFile ("Kick long.wav"), longK);
        Stem lk; loadStem (lk, dir.getChildFile ("Kick long.wav"), HOST_BPM);
        check (lk.timing.bars == 64 && std::abs (lk.timing.tempo - SRC_BPM) < 0.01 && lk.timing.offsetSec == 0, "64-bar kick stem: " + juce::String (lk.timing.bars) + " bars at " + juce::String (lk.timing.tempo, 2) + " BPM");
        Stem lk2; loadStem (lk2, dir.getChildFile ("Kick long.wav"), SRC_BPM);
        check (lk2.timing.bars == 64 && std::abs (lk2.timing.tempo - SRC_BPM) < 0.01, "same stem with Live at its own tempo: unchanged");
    }
    check (proc.globalTonic() == 0 && proc.globalMinor(), juce::String ("key detected: ") + cl::noteName (proc.globalTonic()) + (proc.globalMinor() ? " minor" : " major"));

    // prepared kick: at host tempo, first hit on sample 0, one hit per beat
    if (st.size() == 5 && st[0].audio != nullptr)
    {
        const auto& la = *st[0].audio;
        const int expectLen = (int) std::round (4 * 4 * 60.0 / HOST_BPM * HOST_SR);
        auto o = onsets (la.loop, 0.3f);
        double worst = 0;
        for (auto s : o) { const double ideal = std::round (s / (HOST_SR * 0.5)) * HOST_SR * 0.5; worst = std::max (worst, std::abs (s - ideal)); }
        check (la.loop.getNumSamples() == expectLen && la.bpm == HOST_BPM, "kick loop stretched to 120 BPM at 48 kHz");
        check (! o.empty() && o[0] < 0.01 * HOST_SR && o.size() == 16 && worst < 0.008 * HOST_SR,
               "kick hits on every beat from bar 1 (" + juce::String ((int) o.size()) + " hits, first at " + juce::String (o.empty() ? -1.0 : o[0] / HOST_SR * 1000, 1) + " ms, worst " + juce::String (worst / HOST_SR * 1000, 1) + " ms)");
    }


    // ---- stems from inside a Live project: a recording with a pre-roll, used by a clip in the set
    {
        auto proj = dir.getChildFile ("Song Project"); proj.getChildFile ("Samples/Recorded").createDirectory();
        // 0.889 s of pre-roll (count-in noise), then 4 bars of kick at 126 BPM, then a tail
        const double bpm = 126, pre = 0.889;
        std::vector<float> rec ((size_t) ((pre + 16 * 60.0 / bpm + 0.7) * SR), 0.0f);
        for (int b = 0; b < 16; ++b)
        {
            const int s0 = (int) std::round ((pre + b * 60.0 / bpm) * SR);
            for (int i = 0; i < (int) (0.35 * SR); ++i) rec[(size_t) (s0 + i)] += (float) (0.9 * std::sin (kPi2 * (52 * i / SR + 2.2 * (1 - std::exp (-i / SR / 0.03)))) * std::exp (-i / SR / 0.18));
        }
        auto recFile = proj.getChildFile ("Samples/Recorded/4-Audio 0001.aif");
        writeWav (recFile.withFileExtension ("wav"), rec);
        recFile = recFile.withFileExtension ("wav");
        const double spbSec = 60.0 / bpm;
        juce::String xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?><Ableton><LiveSet><Tracks>"
            << "<AudioTrack Id=\"1\"><Name><EffectiveName Value=\"Kick\"/></Name><DeviceChain><MainSequencer><ClipSlotList/><Sample><ArrangerAutomation><Events>"
            << "<AudioClip Id=\"0\" Time=\"0\"><CurrentStart Value=\"0\"/><CurrentEnd Value=\"32\"/><Loop><LoopStart Value=\"0\"/><LoopEnd Value=\"16\"/><StartRelative Value=\"0\"/><LoopOn Value=\"true\"/></Loop>"
            << "<IsWarped Value=\"true\"/><WarpMarkers><WarpMarker Id=\"0\" SecTime=\"" << pre << "\" BeatTime=\"0\"/><WarpMarker Id=\"1\" SecTime=\"" << (pre + 0.03125 * spbSec) << "\" BeatTime=\"0.03125\"/></WarpMarkers>"
            << "<SampleRef><FileRef><RelativePath Value=\"Samples/Recorded/4-Audio 0001.wav\"/><Path Value=\"" << recFile.getFullPathName() << "\"/></FileRef></SampleRef></AudioClip>"
            << "</Events></ArrangerAutomation></Sample></MainSequencer></DeviceChain></AudioTrack>"
            << "<MidiTrack Id=\"2\"><Name><EffectiveName Value=\"Serum Bass\"/></Name></MidiTrack>"
            << "</Tracks><MainTrack><DeviceChain><Mixer><Tempo><Manual Value=\"126\"/></Tempo></Mixer></DeviceChain></MainTrack></LiveSet></Ableton>";
        auto als = proj.getChildFile ("Song.als");
        {
            als.deleteFile();
            juce::FileOutputStream raw (als);
            juce::GZIPCompressorOutputStream gz (raw, 9, juce::GZIPCompressorOutputStream::windowBitsGZIP);
            gz.writeText (xml, false, false, nullptr);
        }
        // 1) the recording dropped on its own: Arc finds the set and uses the clip's bar 1 and tempo
        Stem k; check (loadStem (k, recFile, 126), "project recording loads");
        check (k.timing.fromSet && std::abs (k.timing.offsetSec - pre) < 1e-4 && std::abs (k.timing.tempo - bpm) < 0.01 && k.timing.bars == 4 && k.role == rKick && k.name == "Kick",
               "recording synced from the Live set: bar 1 at " + juce::String (k.timing.offsetSec * 1000, 1) + " ms, " + juce::String (k.timing.tempo, 2) + " BPM, " + juce::String (k.timing.bars) + " bars, named " + k.name);
        // 2) the whole set dropped: one lane per audio track, kick dead on the grid at 126
        ArcProcessor p3;
        p3.prepareToPlay (HOST_SR, 512);
        p3.syncTempo (126, HOST_SR);
        p3.addFiles (juce::StringArray (als.getFullPathName()));
        waitJobs (p3);
        check (p3.numStems() == 1 && p3.message().contains ("Serum Bass"), "Live set import: 1 audio track, MIDI track flagged (" + p3.message() + ")");
        auto kk = p3.render (0, 0, 32);
        auto o = onsets (kk, 0.3f);
        const double beat = 60.0 / 126 * HOST_SR;
        double worst = 0; for (auto x : o) worst = std::max (worst, std::abs (x - std::round (x / beat) * beat));
        check (o.size() >= 28 && ! o.empty() && o[0] < 0.002 * HOST_SR && worst < 0.0015 * HOST_SR, "imported kick: " + juce::String ((int) o.size()) + " hits, first at " + juce::String (o.empty() ? -1.0 : o[0] / HOST_SR * 1000, 2) + " ms, worst " + juce::String (worst / HOST_SR * 1000, 2) + " ms");
    }

    // ---- optional: a real Live set (ARC_SET=/path/to/set.als)
    if (auto* real = std::getenv ("ARC_SET"))
    {
        const auto set = live::read (juce::File (real));
        std::printf ("    real set %s: %.2f BPM, %d clips\n", real, set.tempo, (int) set.clips.size());
        ArcProcessor p4;
        p4.prepareToPlay (HOST_SR, 512);
        p4.syncTempo (set.tempo, HOST_SR);
        p4.addFiles (juce::StringArray (juce::String (real)));
        waitJobs (p4);
        for (auto& s : p4.stems()) std::printf ("    %-8s %-14s %s  (file starts %.0f ms before bar 1)\n", roleName (s.role), s.name.toRawUTF8(), s.report.toRawUTF8(), s.timing.offsetSec * 1000);
        std::printf ("    %s\n", p4.message().toRawUTF8());
        auto st4 = p4.stems();
        for (int i = 0; i < (int) st4.size(); ++i)
            if (st4[(size_t) i].role == rKick)
            {
                auto kk = p4.render (i, 0, 64);
                auto o = onsets (kk, 0.25f);
                const double beat = 60.0 / set.tempo * HOST_SR;
                double lo = 1e9, hi = -1e9; for (auto x : o) { const double d = x - std::round (x / beat) * beat; lo = std::min (lo, d); hi = std::max (hi, d); }
                check (o.size() >= 60 && hi - lo < 0.001 * HOST_SR, "real kick: " + juce::String ((int) o.size()) + " hits in 16 bars, all within " + juce::String ((hi - lo) / HOST_SR * 1000, 2) + " ms of each other (" + juce::String (lo / HOST_SR * 1000, 1) + " ms after the grid, as in the recording)");
                juce::File out ("/tmp/claude-0/real_kick.wav"); juce::WavAudioFormat wf; out.deleteFile();
                std::unique_ptr<juce::OutputStream> os (out.createOutputStream().release());
                if (auto w = wf.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (HOST_SR).withNumChannels (2).withBitsPerSample (24))) w->writeFromAudioSampleBuffer (kk, 0, kk.getNumSamples());
            }
    }

    // ---- key lock: move the song to A minor, bass and chords follow (-3), drums don't
    {
        const float before = st.size() == 5 && st[1].audio ? yinHz (st[1].audio->loop, 0, (int) HOST_SR, HOST_SR) : 0;
        if (getenv ("ARC_DEBUG") && st.size() == 5)
        {
            auto kb = cl::findKey (chromaOf (st[4].audio->loop, HOST_SR));
            auto rs = resample (*st[4].original, HOST_SR / st[4].fileSr);
            auto k1 = cl::findKey (chromaOf (rs, HOST_SR));
            auto k2 = cl::findKey (chromaOf (stretchLoop (rs, (int) (rs.getNumSamples() * 124.0 / 120.0), HOST_SR), HOST_SR));
            auto k0 = cl::findKey (chromaOf (*st[4].original, st[4].fileSr));
            std::printf ("   orig %s%s resampled %s%s stretched %s%s  (fileSr %.0f, n %d -> %d)\n", cl::noteName (k0.tonic), k0.minor ? "m" : "", cl::noteName (k1.tonic), k1.minor ? "m" : "", cl::noteName (k2.tonic), k2.minor ? "m" : "", st[4].fileSr, st[4].original->getNumSamples(), rs.getNumSamples());
            auto tt = transposeLoop (st[4].audio->loop, -3);
            auto kt = cl::findKey (chromaOf (tt, HOST_SR));
            std::printf ("   chords prepared key %s%s, transposed by hand %s%s, bass hz %.1f -> %.1f\n", cl::noteName (kb.tonic), kb.minor ? "m" : "", cl::noteName (kt.tonic), kt.minor ? "m" : "",
                         before, yinHz (transposeLoop (st[1].audio->loop, -3), 0, (int) HOST_SR, HOST_SR));
        }
        proc.apvts.getParameter (ap::keyOn)->setValueNotifyingHost (1.0f);   // KEY LOCK is off by default (your own stems are already in key)
        auto* root = proc.apvts.getParameter (ap::root);
        root->setValueNotifyingHost (root->convertTo0to1 (9.0f));
        proc.refreshKeyForTests();
        waitJobs (proc);
        auto s2 = proc.stems();
        const float after = s2.size() == 5 && s2[1].audio ? yinHz (s2[1].audio->loop, 0, (int) HOST_SR, HOST_SR) : 0;
        const float semis = before > 0 && after > 0 ? 12 * std::log2 (after / before) : 99;
        const auto kAfter = s2.size() == 5 && s2[4].audio ? cl::findKey (chromaOf (s2[4].audio->loop, HOST_SR)) : cl::KeyResult();
        check (kAfter.tonic == 9 && kAfter.minor, juce::String ("chords now play in ") + (kAfter.tonic >= 0 ? cl::noteName (kAfter.tonic) : "?") + (kAfter.minor ? "m" : ""));
        check (s2.size() == 5 && s2[1].transpose == -3 && s2[4].transpose == -3 && s2[0].transpose == 0, "A minor: bass and chords -3, kick untouched");
        const auto bAfter = s2.size() == 5 && s2[1].audio ? cl::findKey (chromaOf (s2[1].audio->loop, HOST_SR)) : cl::KeyResult();
        check (bAfter.tonic == 9 && bAfter.minor, juce::String ("bass now plays in ") + (bAfter.tonic >= 0 ? cl::noteName (bAfter.tonic) : "?") + (bAfter.minor ? "m" : "") + " (pitch tracker: " + juce::String (semis, 1) + " st, octave-unsure that low)");
        root->setValueNotifyingHost (root->convertTo0to1 (0.0f));
        proc.refreshKeyForTests();
        waitJobs (proc);
    }

    // ---- the plan
    auto plan = proc.plan();
    check (plan != nullptr && plan->totalBars == 144 && plan->lanes.size() == 5, "Club plan: 144 bars, 5 lanes");
    if (plan != nullptr && plan->lanes.size() == 5)
    {
        int brk = -1, build = -1, drop = -1;
        for (auto& s : plan->sections) { if (s.type == sBreak && brk < 0) brk = s.start; if (s.type == sBuild && build < 0) build = s.start; if (s.type == sDrop && drop < 0) drop = s.start; }
        check (! plan->lanes[0].bars[(size_t) brk + 2].on, "kick out in the break");
        check (plan->lanes[0].bars[0].on && ! plan->lanes[1].bars[0].on, "intro starts with kick, bass waits");
        bool roll = false; for (int b = build; b < drop; ++b) roll |= plan->lanes[3].bars[(size_t) b].fill == fRoll;
        check (roll, "clap roll in the build");
        const auto& lastBuild = plan->lanes[2].bars[(size_t) drop - 1];   // hats
        check (lastBuild.steps[13].mute && ! plan->lanes[2].bars[(size_t) drop].steps[0].mute, "a breath on the last beat before the drop");
        int fills = 0; for (int b = drop; b < drop + 32; ++b) for (int l = 0; l < 5; ++l) fills += plan->lanes[(size_t) l].bars[(size_t) b].fill != fNone;
        check (fills >= 4, "fills and variations in the first drop (" + juce::String (fills) + ")");
        bool sweep = plan->lanes[1].bars[(size_t) build + 4].hp1 > 100;
        check (sweep, "bass high-pass sweep in the build");
    }

    // ---- render
    {
        const double song = proc.songBeats();
        auto mix = proc.render (-1, 0, song);
        bool finite = true; float peak = 0;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < mix.getNumSamples(); ++i) { const float v = mix.getSample (c, i); finite &= std::isfinite (v); peak = juce::jmax (peak, std::abs (v)); }
        check (finite && peak > 0.2f && peak < 4.0f, "mix renders, peak " + juce::String (peak, 2));
        auto rmsBars = [&] (int b0, int b1) { const int s0 = (int) (b0 * 4 * 60.0 / HOST_BPM * HOST_SR), s1 = (int) (b1 * 4 * 60.0 / HOST_BPM * HOST_SR); return mix.getRMSLevel (0, s0, s1 - s0); };
        int brk = 0, drop = 0;
        for (auto& s : plan->sections) { if (s.type == sBreak && ! brk) brk = s.start; if (s.type == sDrop && ! drop) drop = s.start; }
        const float rDrop = rmsBars (drop + 4, drop + 12), rBreak = rmsBars (brk + 2, brk + 6), rIntro = rmsBars (0, 2);
        check (rDrop > rBreak * 1.3f && rDrop > rIntro, "drop louder than break and intro (" + juce::String (rDrop, 3) + " / " + juce::String (rBreak, 3) + " / " + juce::String (rIntro, 3) + ")");
        // the kick lane's first hit is at sample 0 of the song
        auto k = proc.render (0, 0, 8);
        auto o = onsets (k, 0.3f);
        check (! o.empty() && o[0] < 0.01 * HOST_SR, "kick lane starts on the 1 (" + juce::String (o.empty() ? -1.0 : o[0] / HOST_SR * 1000, 1) + " ms)");
        // live playback = offline render
        proc.setPosition (0); proc.setPlaying (true);
        juce::AudioBuffer<float> live (2, (int) (8 * 60.0 / HOST_BPM * HOST_SR)); live.clear();
        juce::MidiBuffer mb;
        for (int i = 0; i < live.getNumSamples(); i += 512)
        {
            const int n = juce::jmin (512, live.getNumSamples() - i);
            juce::AudioBuffer<float> blk (2, n); blk.clear();
            proc.processBlock (blk, mb);
            for (int c = 0; c < 2; ++c) live.copyFrom (c, i, blk, c, 0, n);
        }
        proc.setPlaying (false);
        auto off8 = proc.render (-1, 0, 8);
        const float lvl = juce::Decibels::decibelsToGain (proc.apvts.getRawParameterValue (ap::level)->load());
        double err = 0, ref = 0;
        for (int i = 0; i < live.getNumSamples(); ++i) { err += std::pow (live.getSample (0, i) - off8.getSample (0, i) * lvl, 2); ref += std::pow (off8.getSample (0, i) * lvl, 2); }
        check (ref > 0 && err / ref < 1e-3, "live playback matches the export");
        // section loop: transport wraps
        proc.setLoop (8, 16); proc.setPosition (15.9); proc.setPlaying (true);
        juce::AudioBuffer<float> blk (2, 4800); proc.processBlock (blk, mb);
        check (proc.arcPos.load() > 8 && proc.arcPos.load() < 9, "section loop wraps back (" + juce::String (proc.arcPos.load(), 2) + ")");
        proc.setPlaying (false); proc.setLoop (0, 0);
    }

    // ---- the kick is dead straight: no edits in the plan, every hit on the grid through the whole song
    {
        auto pl = proc.plan();
        bool clean = true; int activeBars = 0;
        for (auto& B : pl->lanes[0].bars)
        {
            if (! B.on) continue;
            ++activeBars;
            for (int k = 0; k < 16; ++k) { const auto& op = B.steps[(size_t) k]; clean &= ! op.mute && ! op.rev && op.rep == 1 && op.span == 1 && op.gain == 1.0f && (op.src % 16) == k; }
        }
        check (clean && activeBars > 64, "kick plan: " + juce::String (activeBars) + " bars, all straight");
        auto kk = proc.render (0, 0, proc.songBeats());
        auto o = onsets (kk, 0.3f);
        const double beat = 60.0 / HOST_BPM * HOST_SR;
        double worst = 0;
        for (auto x : o) worst = std::max (worst, std::abs (x - std::round (x / beat) * beat));
        check (o.size() >= (size_t) activeBars * 4 - 2 && worst < 0.0015 * HOST_SR, "kick across the song: " + juce::String ((int) o.size()) + " hits, worst " + juce::String (worst / HOST_SR * 1000, 2) + " ms off the beat");
    }

    // ---- nudge: move bar 1 of the clap by one beat
    {
        auto before = proc.stems()[3];
        proc.setNudge (3, 4);
        waitJobs (proc);
        auto after = proc.stems()[3];
        auto ob = onsets (before.audio->loop, 0.2f), oa = onsets (after.audio->loop, 0.2f);
        const double beat = 60.0 / HOST_BPM * HOST_SR;
        check (! ob.empty() && ! oa.empty() && std::abs ((ob[0] - oa[0]) - beat) < 0.01 * HOST_SR, "nudge +4/16 moves the clap a beat earlier");
        proc.undo(); waitJobs (proc);
        check (proc.stems()[3].nudge == 0, "undo puts it back");
        proc.redo(); waitJobs (proc);
        check (proc.stems()[3].nudge == 4, "redo");
        proc.setNudge (3, 0); waitJobs (proc);
    }

    // ---- hold / dice / generate
    {
        proc.setHold (1, true);
        auto s0 = proc.stems();
        proc.generate();
        auto s1 = proc.stems();
        bool othersChanged = true; for (int i = 0; i < 5; ++i) if (i != 1) othersChanged &= s1[(size_t) i].seed != s0[(size_t) i].seed;
        check (s1[1].seed == s0[1].seed && othersChanged, "GENERATE re-rolls every lane except the one on HOLD");
        auto pA = proc.plan();
        proc.dice (4);
        auto pB = proc.plan();
        bool lane4diff = false, lane1same = true;
        for (int b = 0; b < pA->totalBars; ++b)
        {
            lane4diff |= ! (pA->lanes[4].bars[(size_t) b].steps == pB->lanes[4].bars[(size_t) b].steps) || pA->lanes[4].bars[(size_t) b].on != pB->lanes[4].bars[(size_t) b].on;
            lane1same &= pA->lanes[1].bars[(size_t) b].steps == pB->lanes[1].bars[(size_t) b].steps && pA->lanes[1].bars[(size_t) b].on == pB->lanes[1].bars[(size_t) b].on;
        }
        check (lane4diff && lane1same, "DICE changes only its own lane");
        proc.setRole (4, rLead);
        check (proc.stems()[4].role == rLead, "role change");
        proc.undo();
        check (proc.stems()[4].role == rChords, "undo role change");
    }

    // ---- styles
    {
        auto* p = proc.apvts.getParameter (ap::style);
        bool allOk = true;
        for (int s = 0; s < (int) styles().size(); ++s)
        {
            p->setValueNotifyingHost (p->convertTo0to1 ((float) s));
            proc.refreshKeyForTests();
            auto pl = proc.plan();
            int tot = 0; for (auto& sd : styles()[(size_t) s].sections) tot += sd.bars;
            allOk &= pl->totalBars == tot && pl->lanes.size() == 5;
        }
        check (allOk, "every arrangement style builds");
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
        proc.refreshKeyForTests();
    }

    // ---- export
    {
        auto files = proc.exportAll (false);
        const int expect = (int) std::round (proc.songBeats() * 60.0 / HOST_BPM * HOST_SR);
        bool lengths = files.size() == 6;
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        for (auto& f : files) { std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f)); lengths &= r != nullptr && std::abs ((int) r->lengthInSamples - expect) <= 1; }
        check (lengths, "export: 5 lanes + mix, each exactly the song length (" + juce::String (files.size()) + " files)");
        if (! files.isEmpty()) files.getFirst().getParentDirectory().deleteRecursively();
    }

    // ---- MIDI files still work (rendered through Arc's sounds)
    {
        juce::MidiMessageSequence seq;
        for (int b = 0; b < 8; ++b) { seq.addEvent (juce::MidiMessage::noteOn (1, 45, 0.9f), b * 480.0); seq.addEvent (juce::MidiMessage::noteOff (1, 45), b * 480.0 + 400); }
        juce::MidiFile mf; mf.setTicksPerQuarterNote (960); mf.addTrack (seq);
        auto f = dir.getChildFile ("Midi Bass.mid");
        { f.deleteFile(); juce::FileOutputStream os (f); mf.writeTo (os); }
        check (proc.addFileSync (f), "MIDI bass loads as a lane");
        waitJobs (proc);
        auto s = proc.stems();
        bool found = false; for (auto& x : s) found |= x.fromMidi && x.role == rBass && x.audio != nullptr;
        check (found, "MIDI lane is a bass with audio");
    }

    // ---- state
    {
        juce::MemoryBlock mb;
        proc.setVolume (2, -4.5f);
        proc.setCrop (4, 0, 2);
        waitJobs (proc);
        proc.getStateInformation (mb);
        ArcProcessor p2;
        p2.prepareToPlay (HOST_SR, 512);
        p2.syncTempo (HOST_BPM, HOST_SR);
        p2.setStateInformation (mb.getData(), (int) mb.getSize());
        waitJobs (p2);
        auto a = proc.stems(), b = p2.stems();
        bool same = a.size() == b.size();
        for (size_t i = 0; same && i < a.size(); ++i)
            same &= a[i].role == b[i].role && a[i].volumeDb == b[i].volumeDb && a[i].cropBars == b[i].cropBars && a[i].seed == b[i].seed && a[i].path == b[i].path && b[i].audio != nullptr;
        check (same, "session restore: stems, settings and seeds come back");
        auto pa = proc.plan(), pb = p2.plan();
        bool planSame = pa->lanes.size() == pb->lanes.size();
        for (size_t l = 0; planSame && l < pa->lanes.size(); ++l)
            for (int bar = 0; bar < pa->totalBars; ++bar) planSame &= pa->lanes[l].bars[(size_t) bar].on == pb->lanes[l].bars[(size_t) bar].on;
        check (planSame, "same arrangement after restore");
    }

    // ---- CPU with 12 lanes
    {
        for (int k = 0; proc.numStems() < 12 && k < 10; ++k) proc.addFileSync (dir.getChildFile (k % 2 ? "Hats.wav" : "Chords.wav"));
        waitJobs (proc);
        proc.setPosition (4 * 72); proc.setPlaying (true);
        juce::AudioBuffer<float> blk (2, 512); juce::MidiBuffer mb;
        const int blocks = (int) (20 * HOST_SR / 512);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < blocks; ++i) proc.processBlock (blk, mb);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        proc.setPlaying (false);
        check (proc.numStems() == 12 && ms < 20000 * 0.08, "12 lanes: " + juce::String (ms / 200.0, 2) + "% of one core");
    }

    // ---- live capture: Arc Tap on tracks in Live, no bouncing
    {
        struct Head : juce::AudioPlayHead
        {
            double ppq = 0, bpm = 125; bool play = false;
            juce::Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setIsPlaying (play); p.setBpm (bpm); p.setPpqPosition (ppq); return p; }
        } head;
        const double bpm = 125, rate = HOST_SR;
        TapProcessor kickTap, padTap, quietTap;
        const char* names[] { "Kick", "Pad", "Muted FX" };
        TapProcessor* tapsArr[] { &kickTap, &padTap, &quietTap };
        for (int k = 0; k < 3; ++k)
        {
            tapsArr[k]->setPlayHead (&head);
            tapsArr[k]->prepareToPlay (rate, 512);
            juce::AudioProcessor::TrackProperties tp; tp.name = juce::String (names[k]);
            tapsArr[k]->updateTrackProperties (tp);
        }
        ArcProcessor arc; arc.prepareToPlay (rate, 512); arc.syncTempo (bpm, rate);

        // what each track plays: the kick hits every beat of Live's timeline, louder on the 1 of each bar
        auto runCapture = [&] (double fromPpq, double beats)
        {
            for (auto* t : tapsArr) t->poll();
            arc.pollCapture();
            head.play = true; head.ppq = fromPpq;
            const int block = 512;
            const int blocks = (int) std::ceil (beats * 60.0 / bpm * rate / block);
            juce::MidiBuffer midi;
            for (int bl = 0; bl < blocks; ++bl)
            {
                juce::AudioBuffer<float> kb (2, block), pb (2, block), qb (2, block);
                qb.clear();
                for (int i = 0; i < block; ++i)
                {
                    const double beat = head.ppq + i * bpm / 60.0 / rate;
                    const double since = (beat - std::floor (beat)) * 60.0 / bpm;
                    const bool one = ((long long) std::floor (beat)) % 4 == 0;
                    const float kv = since < 0.12 ? (float) ((one ? 0.9 : 0.45) * std::exp (-since * 30) * std::cos (kPi2 * 55 * since)) : 0.0f;
                    const float pv = (float) (0.2 * std::sin (kPi2 * 220 * beat * 60.0 / bpm));
                    kb.setSample (0, i, kv); kb.setSample (1, i, kv); pb.setSample (0, i, pv); pb.setSample (1, i, pv);
                }
                kickTap.processBlock (kb, midi); padTap.processBlock (pb, midi); quietTap.processBlock (qb, midi);
                head.ppq += block * bpm / 60.0 / rate;
                if (bl % 40 == 0) { for (auto* t : tapsArr) t->poll(); arc.pollCapture(); }
            }
            head.play = false;
            juce::Thread::sleep (1700);
            for (int k = 0; k < 3; ++k) { for (auto* t : tapsArr) t->poll(); arc.pollCapture(); }
            waitJobs (arc);
        };

        arc.startCapture (4);
        for (auto* t : tapsArr) t->poll();
        arc.pollCapture();
        check (arc.taps().size() >= 3 && arc.captureStatus().contains ("ready"), "capture: 3 Arc Taps found and armed (" + arc.captureStatus() + ")");
        runCapture (6.3, 1.7 + 16 + 2);     // Live starts mid bar 2: capture runs from bar 3
        auto st = arc.stems();
        check (! arc.capturing() && st.size() == 2, "capture: kick and pad arrive, the silent track is skipped (" + juce::String ((int) st.size()) + " lanes, " + arc.message() + ")");
        const Stem* kick = nullptr;
        for (auto& s : st) if (s.name == "Kick") kick = &s;
        check (kick != nullptr && kick->role == rKick && kick->timing.fromSet && kick->timing.bars == 4 && std::abs (kick->timing.tempo - bpm) < 1e-6 && kick->timing.offsetSec == 0,
               "captured kick: named from the track, KICK, 4 bars at Live's 125 BPM, bar 1 at the start");
        if (kick != nullptr)
        {
            auto o = onsets (*kick->original, 0.05f);
            int worst = 0; for (int k = 0; k < (int) o.size(); ++k) worst = juce::jmax (worst, std::abs (o[(size_t) k] - (int) std::llround (k * 60.0 / bpm * rate)));
            const auto& b = *kick->original;
            auto peakAt = [&] (int beat) { const int s0 = (int) std::llround (beat * 60.0 / bpm * rate); return b.getMagnitude (0, s0, (int) (0.1 * rate)); };
            check (o.size() == 16 && worst <= 2 && peakAt (0) > 1.5f * peakAt (1) && peakAt (4) > 1.5f * peakAt (5),
                   "captured kick: 16 hits on the grid (worst " + juce::String (worst) + " samples), the loud hit is beat 1 of each bar");
            check (b.getNumSamples() == (int) std::llround (16 * 60.0 / bpm * rate), "captured kick: exactly 4 bars long");
        }
        // capture again: same lanes, settings kept
        int ki = -1; for (int k = 0; k < (int) st.size(); ++k) if (st[(size_t) k].name == "Kick") ki = k;
        if (ki >= 0) arc.setVolume (ki, -6.0f);
        juce::Thread::sleep (5);
        arc.startCapture (4);
        runCapture (30.0, 18 + 2);
        auto st2 = arc.stems();
        bool kept = false; for (auto& s : st2) if (s.name == "Kick") kept = std::abs (s.volumeDb + 6.0f) < 0.01f;
        check (st2.size() == 2 && kept, "capture again: lanes replaced in place, kick volume kept");
        // saved and reopened: timing comes back as captured
        juce::MemoryBlock mb; arc.getStateInformation (mb);
        ArcProcessor back; back.prepareToPlay (rate, 512); back.setStateInformation (mb.getData(), (int) mb.getSize()); waitJobs (back);
        auto st3 = back.stems();
        bool same = st3.size() == 2; for (auto& s : st3) same &= s.timing.fromSet && s.timing.bars == 4 && s.timing.offsetSec == 0 && (s.name == "Kick" || s.name == "Pad");
        check (same, "captured lanes reload from the saved set with their timing and names");
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (kickTap.createEditor());
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            juce::FileOutputStream os (juce::File::getCurrentWorkingDirectory().getChildFile ("arc_tap_ui.png").withFileExtension ("png"));
            os.setPosition (0); os.truncate();
            juce::PNGImageFormat png; png.writeImageToStream (img, os);
            check (img.isValid(), "Arc Tap editor snapshot");
        }
    }

    // ---- editor snapshots
    auto snap = [] (ArcProcessor& pr, const juce::String& name, double pos)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (pr.createEditor());
        auto* e = dynamic_cast<ArcEditor*> (ed.get());
        e->refreshNow();
        pr.playPpq = pos;
        auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        auto out = juce::File::getCurrentWorkingDirectory().getChildFile (name);
        out.deleteFile();
        juce::FileOutputStream os (out); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        return img.isValid();
    };
    { ArcProcessor empty; check (snap (empty, "arc_ui_empty.png", 0), "empty editor snapshot"); }
    if (auto* real = std::getenv ("ARC_SET"))
    {
        ArcProcessor p5; p5.prepareToPlay (HOST_SR, 512);
        p5.syncTempo (live::read (juce::File (real)).tempo, HOST_SR);
        p5.addFiles (juce::StringArray (juce::String (real)));
        waitJobs (p5);
        check (snap (p5, "arc_ui_real.png", 4 * 40.5), "real set editor snapshot");
    }
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* e = dynamic_cast<ArcEditor*> (ed.get());
        e->refreshNow();
        proc.playPpq = 4 * 75.5;
        auto img = e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f);
        auto out = juce::File::getCurrentWorkingDirectory().getChildFile ("arc_ui.png");
        out.deleteFile();
        juce::FileOutputStream os (out); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        check (img.isValid(), "editor snapshot -> " + out.getFullPathName());
    }

    dir.deleteRecursively();
    std::printf ("\nfailures: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
