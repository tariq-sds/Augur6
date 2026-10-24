#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

// Live capture: Arc Tap sits on any track in Live (audio or instrument), Arc asks every tap to record the
// next N bars while Live plays, and the taps hand back WAVs that start exactly on a bar line at Live's tempo.
// Arc and the taps are separate plugins (separate binaries), so they talk through small JSON files.
namespace arccap
{
inline juce::File root()
{
    auto lib = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    lib = lib.getChildFile ("Application Support");
   #endif
    return lib.getChildFile ("Toota Audio").getChildFile ("Arc Live");
}
inline juce::File tapsDir()     { return root().getChildFile ("taps"); }
inline juce::File requestFile() { return root().getChildFile ("request.json"); }
inline juce::File capturesDir() { return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Arc Exports").getChildFile ("Live Captures"); }
inline juce::int64 now() { return juce::Time::currentTimeMillis(); }

inline bool writeJson (const juce::File& f, const juce::var& v)
{
    f.getParentDirectory().createDirectory();
    auto tmp = f.getSiblingFile (f.getFileName() + ".tmp");
    if (! tmp.replaceWithText (juce::JSON::toString (v, true))) return false;
    return tmp.moveFileTo (f);     // atomic: readers never see half a file
}
inline juce::var readJson (const juce::File& f) { return f.existsAsFile() ? juce::JSON::parse (f.loadFileAsString()) : juce::var(); }

struct Request
{
    juce::String id;
    int bars = 8;
    juce::int64 created = 0;
    bool cancel = false;
    bool valid() const { return id.isNotEmpty(); }
};

inline void writeRequest (const Request& r)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", r.id); o->setProperty ("bars", r.bars);
    o->setProperty ("created", r.created); o->setProperty ("cancel", r.cancel);
    writeJson (requestFile(), juce::var (o));
}
inline Request readRequest()
{
    Request r;
    auto v = readJson (requestFile());
    if (auto* o = v.getDynamicObject())
    {
        r.id = o->getProperty ("id").toString();
        r.bars = juce::jlimit (1, 64, (int) o->getProperty ("bars"));
        r.created = (juce::int64) o->getProperty ("created");
        r.cancel = (bool) o->getProperty ("cancel");
    }
    return r;
}

// what each tap tells Arc, twice a second
struct TapInfo
{
    juce::String id, name, state, request, file;   // state: idle, armed, recording, done, silent
    double progress = 0, bpm = 0;
    juce::int64 seen = 0;
};

inline void writeTap (const TapInfo& t)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", t.id); o->setProperty ("name", t.name); o->setProperty ("state", t.state);
    o->setProperty ("request", t.request); o->setProperty ("file", t.file);
    o->setProperty ("progress", t.progress); o->setProperty ("bpm", t.bpm); o->setProperty ("seen", t.seen);
    writeJson (tapsDir().getChildFile (t.id + ".json"), juce::var (o));
}
inline void removeTap (const juce::String& id) { tapsDir().getChildFile (id + ".json").deleteFile(); }

// taps that are alive right now (heard from in the last few seconds); old files are cleared away
inline std::vector<TapInfo> liveTaps()
{
    std::vector<TapInfo> out;
    const auto t = now();
    for (auto& f : tapsDir().findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        auto v = readJson (f);
        auto* o = v.getDynamicObject();
        if (o == nullptr) continue;
        TapInfo i;
        i.id = o->getProperty ("id").toString(); i.name = o->getProperty ("name").toString();
        i.state = o->getProperty ("state").toString(); i.request = o->getProperty ("request").toString();
        i.file = o->getProperty ("file").toString();
        i.progress = o->getProperty ("progress"); i.bpm = o->getProperty ("bpm");
        i.seen = (juce::int64) o->getProperty ("seen");
        if (t - i.seen > 60000) { f.deleteFile(); continue; }
        if (t - i.seen > 4000) continue;
        out.push_back (i);
    }
    std::sort (out.begin(), out.end(), [] (const TapInfo& a, const TapInfo& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

inline juce::String safeName (juce::String s)
{
    s = s.removeCharacters ("\\/:*?\"<>|").trim();
    return s.isEmpty() ? juce::String ("Track") : s.substring (0, 60);
}

inline bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, int numSamples, double rate)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, 2, 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, numSamples);
}

// The audio-thread half of a tap: records the requested bars, starting on the first bar line it sees
// once Live is playing. Positions come from Live's play head, so the file starts exactly on a bar.
class Recorder
{
public:
    // message thread
    void arm (int bars, double bpmGuess, double rate)
    {
        disarm();
        const double secs = bars * 4.0 * 60.0 / juce::jlimit (40.0, 300.0, bpmGuess) * 1.15 + 1.0;
        buffer.setSize (2, (int) (secs * rate) + 16, false, true, false);
        buffer.clear();
        barsWanted = bars;
        startPpq = -1; filled = 0; total = 0; done = false;
        armed = true;
    }
    void disarm() { armed = false; done = false; startPpq = -1; filled = 0; }

    // audio thread
    void process (const juce::AudioBuffer<float>& in, int n, double ppq, double bpm, bool playing, double rate)
    {
        if (! armed.load() || done.load()) return;
        if (! playing)
        {
            if (startPpq.load() >= 0) { startPpq = -1.0; filled = 0; }   // stopped part way: start again next time Live plays
            return;
        }
        if (startPpq.load() < 0)
        {
            const double bar = std::ceil (ppq / 4.0 - 1e-4) * 4.0;
            const int want = (int) std::llround (barsWanted * 4.0 * 60.0 / bpm * rate);
            if (want > buffer.getNumSamples()) { armed = false; return; }   // tempo far slower than when armed
            total = want;
            usedBpm = bpm;
            startPpq = bar;
        }
        const double spb = rate * 60.0 / bpm;
        const juce::int64 idx0 = (juce::int64) std::llround ((ppq - startPpq.load()) * spb);
        const int t = total.load();
        int top = filled.load();
        for (int c = 0; c < 2; ++c)
        {
            const float* src = in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1));
            float* dst = buffer.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const juce::int64 k = idx0 + i;
                if (k >= 0 && k < t) { dst[k] = src[i]; if (c == 0) top = juce::jmax (top, (int) k + 1); }
            }
        }
        filled = top;
        if (top >= t) done = true;
    }

    // either thread
    bool isArmed() const { return armed.load(); }
    bool isDone() const { return done.load(); }
    bool started() const { return startPpq.load() >= 0; }
    double progress() const { const int t = total.load(); return t > 0 ? (double) filled.load() / t : 0.0; }
    double bpmUsed() const { return usedBpm.load(); }
    int bars() const { return barsWanted; }
    int length() const { return total.load(); }
    const juce::AudioBuffer<float>& audio() const { return buffer; }

private:
    juce::AudioBuffer<float> buffer;
    int barsWanted = 8;
    std::atomic<bool> armed { false }, done { false };
    std::atomic<double> startPpq { -1.0 }, usedBpm { 120.0 };
    std::atomic<int> filled { 0 }, total { 0 };
};
} // namespace arccap
