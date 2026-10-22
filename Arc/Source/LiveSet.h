#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>

// Reads an Ableton Live set (.als = gzipped XML) so Arc knows exactly how each audio file is used in the
// project: where bar 1 is in the file (recordings start with a pre-roll), the tempo (from the warp markers)
// and which part of the file the clip plays. With that, stems taken from inside a project line up exactly.
namespace live
{
struct ClipUse
{
    juce::File file;
    juce::String track;
    double arrangeTime = 0;           // where the clip sits in the arrangement (beats)
    double startSec = 0;              // file position of the clip's first beat
    double secPerBeat = 0.5;          // average over the region
    double beats = 16;                // length of the region the clip plays
    bool session = false, warped = true;
};

struct LiveSet
{
    juce::File file;
    double tempo = 120;
    std::vector<ClipUse> clips;       // every audio clip in the set, arrangement first
    juce::StringArray midiTracks;     // tracks Arc can't take (MIDI)
    bool ok = false;
};

inline std::unique_ptr<juce::XmlElement> readXml (const juce::File& als)
{
    juce::FileInputStream raw (als);
    if (! raw.openedOk()) return {};
    juce::GZIPDecompressorInputStream gz (&raw, false, juce::GZIPDecompressorInputStream::gzipFormat);
    const auto text = gz.readEntireStreamAsString();
    if (text.isEmpty()) return {};
    return juce::parseXML (text);
}

inline double val (const juce::XmlElement* e, const char* childPath, double def = 0)
{
    auto* c = e;
    for (auto& part : juce::StringArray::fromTokens (childPath, "/", ""))
    {
        if (c == nullptr) return def;
        c = c->getChildByName (part);
    }
    return c != nullptr && c->hasAttribute ("Value") ? c->getDoubleAttribute ("Value", def) : def;
}
inline juce::String sval (const juce::XmlElement* e, const char* childPath)
{
    auto* c = e;
    for (auto& part : juce::StringArray::fromTokens (childPath, "/", ""))
    {
        if (c == nullptr) return {};
        c = c->getChildByName (part);
    }
    return c != nullptr ? c->getStringAttribute ("Value") : juce::String();
}

// beat -> seconds in the file, following the warp markers (extrapolating past the first / last)
inline double secOfBeat (const std::vector<std::pair<double, double>>& wm, double beat, double fallbackSpb)
{
    if (wm.empty()) return beat * fallbackSpb;
    if (wm.size() == 1) return wm[0].first + (beat - wm[0].second) * fallbackSpb;
    size_t i = 0;
    while (i + 2 < wm.size() && beat > wm[i + 1].second) ++i;
    const auto& a = wm[i]; const auto& b = wm[i + 1];
    const double spb = (b.first - a.first) / juce::jmax (1e-9, b.second - a.second);
    return a.first + (beat - a.second) * spb;
}

inline void collect (const juce::XmlElement& clip, const juce::String& track, const juce::File& projectDir, double tempo, bool session, std::vector<ClipUse>& out)
{
    ClipUse u;
    u.track = track;
    u.session = session;
    u.arrangeTime = clip.getDoubleAttribute ("Time", val (&clip, "CurrentStart"));
    const auto abs = sval (&clip, "SampleRef/FileRef/Path");
    const auto rel = sval (&clip, "SampleRef/FileRef/RelativePath");
    juce::File f;
    if (abs.isNotEmpty() && juce::File::isAbsolutePath (abs)) f = juce::File (abs);
    if (! f.existsAsFile() && rel.isNotEmpty()) f = projectDir.getChildFile (rel);
    if (! f.existsAsFile() && abs.isNotEmpty()) f = projectDir.getChildFile ("Samples").findChildFiles (juce::File::findFiles, true, juce::File (abs).getFileName()).getFirst();
    if (! f.existsAsFile()) return;
    u.file = f;
    u.warped = sval (&clip, "IsWarped") != "false";
    const bool loopOn = sval (&clip, "Loop/LoopOn") == "true";
    const double ls = val (&clip, "Loop/LoopStart"), le = val (&clip, "Loop/LoopEnd"), rel0 = val (&clip, "Loop/StartRelative");
    const double cs = val (&clip, "CurrentStart"), ce = val (&clip, "CurrentEnd");
    const double hostSpb = 60.0 / juce::jmax (20.0, tempo);
    if (u.warped)
    {
        std::vector<std::pair<double, double>> wm;   // (sec, beat)
        if (auto* w = clip.getChildByName ("WarpMarkers"))
            for (auto* m : w->getChildWithTagNameIterator ("WarpMarker"))
                wm.push_back ({ m->getDoubleAttribute ("SecTime"), m->getDoubleAttribute ("BeatTime") });
        std::sort (wm.begin(), wm.end(), [] (auto& x, auto& y) { return x.second < y.second; });
        const double startBeat = loopOn ? ls : ls + rel0;
        double beats = loopOn ? (le - ls) : juce::jmin (ce - cs, le - startBeat);
        if (beats <= 0) beats = le - ls;
        u.startSec = secOfBeat (wm, startBeat, hostSpb);
        const double endSec = secOfBeat (wm, startBeat + beats, hostSpb);
        u.beats = beats;
        u.secPerBeat = (endSec - u.startSec) / juce::jmax (1e-6, beats);
    }
    else
    {
        // unwarped: the loop values are seconds, and the audio plays at its own speed
        const double startS = loopOn ? ls : ls + rel0;
        double lenS = loopOn ? (le - ls) : juce::jmin ((ce - cs) * hostSpb, le - startS);
        if (lenS <= 0) lenS = le - ls;
        u.startSec = startS;
        u.secPerBeat = hostSpb;
        u.beats = lenS / hostSpb;
    }
    if (u.beats > 0.05 && u.secPerBeat > 0.05 && u.secPerBeat < 3.0) out.push_back (u);
}

inline LiveSet read (const juce::File& als)
{
    LiveSet s;
    s.file = als;
    auto doc = readXml (als);
    if (doc == nullptr) return s;
    auto* xml = doc->hasTagName ("LiveSet") ? doc.get() : doc->getChildByName ("LiveSet");
    if (xml == nullptr) return s;
    const auto dir = als.getParentDirectory();
    for (auto* mt : { xml->getChildByName ("MainTrack"), xml->getChildByName ("MasterTrack") })
        if (mt != nullptr) { const double t = val (mt, "DeviceChain/Mixer/Tempo/Manual", 0); if (t > 0) { s.tempo = t; break; } }
    if (auto* tracks = xml->getChildByName ("Tracks"))
    {
        for (auto* tr : tracks->getChildIterator())
        {
            const auto name = sval (tr, "Name/EffectiveName");
            if (tr->hasTagName ("MidiTrack")) { s.midiTracks.add (name); continue; }
            if (! tr->hasTagName ("AudioTrack")) continue;
            auto* seq = tr->getChildByName ("DeviceChain") != nullptr ? tr->getChildByName ("DeviceChain")->getChildByName ("MainSequencer") : nullptr;
            if (seq == nullptr) continue;
            std::vector<ClipUse> arr;
            if (auto* sample = seq->getChildByName ("Sample"))
                if (auto* aa = sample->getChildByName ("ArrangerAutomation"))
                    if (auto* ev = aa->getChildByName ("Events"))
                        for (auto* c : ev->getChildWithTagNameIterator ("AudioClip")) collect (*c, name, dir, s.tempo, false, arr);
            std::sort (arr.begin(), arr.end(), [] (auto& a, auto& b) { return a.arrangeTime < b.arrangeTime; });
            for (auto& c : arr) s.clips.push_back (c);
            if (auto* slots = seq->getChildByName ("ClipSlotList"))
                for (auto* slot : slots->getChildIterator())
                    if (auto* inner = slot->getChildByName ("ClipSlot"))
                        if (auto* v = inner->getChildByName ("Value"))
                            for (auto* c : v->getChildWithTagNameIterator ("AudioClip")) collect (*c, name, dir, s.tempo, true, s.clips);
        }
    }
    s.ok = true;
    return s;
}

// The Live set a file belongs to: walk up to the "... Project" folder and take its newest .als
inline juce::File setFor (const juce::File& audio)
{
    auto d = audio.getParentDirectory();
    for (int up = 0; up < 5 && d.exists() && ! d.isRoot(); ++up, d = d.getParentDirectory())
    {
        auto sets = d.findChildFiles (juce::File::findFiles, false, "*.als");
        if (! sets.isEmpty())
        {
            juce::File newest = sets[0];
            for (auto& f : sets) if (f.getLastModificationTime() > newest.getLastModificationTime()) newest = f;
            return newest;
        }
    }
    return {};
}

// How the project uses this file (the first arrangement clip that plays it, else a session clip)
inline bool useOf (const juce::File& audio, ClipUse& out, double* setTempo = nullptr)
{
    const auto als = setFor (audio);
    if (! als.existsAsFile()) return false;
    const auto s = read (als);
    for (auto& c : s.clips)
        if (c.file == audio || (c.file.getFileName() == audio.getFileName() && c.file.getSize() == audio.getSize()))
        {
            out = c;
            if (setTempo != nullptr) *setTempo = s.tempo;
            return true;
        }
    return false;
}
} // namespace live
