#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "../../Clarion/Source/VocalDSP.h"
#include <vector>
#include <algorithm>

namespace arc
{
struct Note
{
    int pitch = 60;
    double start = 0;     // beats (quarter notes)
    double length = 0.25; // beats
    float velocity = 0.8f;
    juce::String tag;     // why it exists: "fill", "call", "response", "ghost", ...
};

struct Clip
{
    std::vector<Note> notes;
    double lengthBeats = 4.0;
    bool empty() const { return notes.empty(); }
    void sort() { std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start || (a.start == b.start && a.pitch < b.pitch); }); }
};

enum Lane { laneDrums = 0, laneBass, laneChords, laneLead, kNumLanes };
inline const char* laneName (int l) { static const char* n[] { "Drums", "Bass", "Chords", "Lead" }; return n[juce::jlimit (0, 3, l)]; }

// General MIDI drum notes used throughout
namespace gm { constexpr int kick = 36, rim = 37, snare = 38, clap = 39, closedHat = 42, pedalHat = 44, openHat = 46, lowTom = 45, midTom = 47, hiTom = 50, crash = 49, ride = 51, perc = 56; }

inline bool isKick (int p)  { return p == 35 || p == 36; }
inline bool isSnare (int p) { return p == 37 || p == 38 || p == 39 || p == 40; }
inline bool isHat (int p)   { return p == 42 || p == 44 || p == 46 || p == 51 || p == 53 || p == 59; }

// ---------------------------------------------------------------- key / scale
struct Scale
{
    int tonic = 9; bool minor = true;
    bool contains (int pitch) const
    {
        static const int maj[7] { 0, 2, 4, 5, 7, 9, 11 }, mnr[7] { 0, 2, 3, 5, 7, 8, 10 };
        const int pc = ((pitch - tonic) % 12 + 12) % 12;
        for (int d : (minor ? mnr : maj)) if (d == pc) return true;
        return false;
    }
    int snap (int pitch) const
    {
        if (contains (pitch)) return pitch;
        for (int d = 1; d < 3; ++d) { if (contains (pitch - d)) return pitch - d; if (contains (pitch + d)) return pitch + d; }
        return pitch;
    }
    // move a pitch by n scale steps (diatonic transposition)
    int step (int pitch, int n) const
    {
        int p = snap (pitch);
        const int dir = n > 0 ? 1 : -1;
        for (int k = 0; k != n; k += dir)
        {
            p += dir;
            while (! contains (p)) p += dir;
        }
        return p;
    }
};

inline Scale keyOf (const std::vector<const Clip*>& clips)
{
    std::array<float, 12> h {};
    for (auto* c : clips)
        for (auto& n : c->notes) h[(size_t) (n.pitch % 12)] += (float) n.length * n.velocity;
    const auto k = cl::findKey (h);
    Scale s;
    if (k.tonic >= 0) { s.tonic = k.tonic; s.minor = k.minor; }
    return s;
}

// ---------------------------------------------------------------- MIDI file I/O
inline juce::MidiFile toMidiFile (const Clip& c, double bpm, bool drums, const juce::String& name)
{
    juce::MidiMessageSequence seq;
    const int tpq = 960;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    const int ch = drums ? 10 : 1;
    for (auto& n : c.notes)
    {
        const double t0 = n.start * tpq, t1 = (n.start + juce::jmax (1.0 / 64.0, n.length)) * tpq;
        seq.addEvent (juce::MidiMessage::noteOn (ch, juce::jlimit (0, 127, n.pitch), (juce::uint8) juce::jlimit (1, 127, (int) std::round (n.velocity * 127))), t0);
        seq.addEvent (juce::MidiMessage::noteOff (ch, juce::jlimit (0, 127, n.pitch)), t1);
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), c.lengthBeats * tpq);
    seq.updateMatchedPairs();
    juce::MidiFile f;
    f.setTicksPerQuarterNote (tpq);
    f.addTrack (seq);
    return f;
}

inline bool writeMidi (const juce::File& file, const Clip& c, double bpm, bool drums, const juce::String& name)
{
    file.deleteFile();
    auto f = toMidiFile (c, bpm, drums, name);
    if (auto os = file.createOutputStream()) return f.writeTo (*os);
    return false;
}

struct ImportedMidi { Clip clip; int suggestedLane = laneChords; bool ok = false; };

inline ImportedMidi readMidi (const juce::File& file)
{
    ImportedMidi r;
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in)) return r;
    const int tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
    int drumCh = 0, total = 0;
    double end = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* e = seq.getEventPointer (i);
            if (! e->message.isNoteOn()) continue;
            Note n;
            n.pitch = e->message.getNoteNumber();
            n.velocity = e->message.getFloatVelocity();
            n.start = e->message.getTimeStamp() / tpq;
            const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() / tpq : n.start + 0.25;
            n.length = juce::jmax (1.0 / 64.0, off - n.start);
            end = juce::jmax (end, n.start + n.length);
            if (e->message.getChannel() == 10) ++drumCh;
            ++total;
            r.clip.notes.push_back (n);
        }
    }
    if (total == 0) return r;
    // loop length: whole bars covering the notes
    double first = 1e9; for (auto& n : r.clip.notes) first = std::min (first, n.start);
    const double barStart = std::floor (first / 4.0) * 4.0;
    for (auto& n : r.clip.notes) n.start -= barStart;
    const double bars = std::max (1.0, std::ceil ((end - barStart - 1e-6) / 4.0));
    int pow2 = 1; while (pow2 < bars) pow2 *= 2;
    r.clip.lengthBeats = (pow2 - bars <= 0.01 ? pow2 : bars) * 4.0;
    r.clip.sort();

    // which lane does it look like?
    double sumPitch = 0; int drumish = 0;
    for (auto& n : r.clip.notes) { sumPitch += n.pitch; if (n.pitch >= 35 && n.pitch <= 59 && n.length <= 0.3) ++drumish; }
    double overlap = 0;
    for (size_t i = 0; i < r.clip.notes.size(); ++i)
        for (size_t j = i + 1; j < r.clip.notes.size() && r.clip.notes[j].start < r.clip.notes[i].start + r.clip.notes[i].length; ++j) overlap += 1;
    const double mean = sumPitch / total, poly = overlap / total;
    if (drumCh > total / 2 || (drumish > total * 0.8 && poly > 0.2 && mean < 50)) r.suggestedLane = laneDrums;
    else if (mean < 52 && poly < 0.3) r.suggestedLane = laneBass;
    else if (poly > 0.8) r.suggestedLane = laneChords;
    else r.suggestedLane = laneLead;
    r.ok = true;
    return r;
}
} // namespace arc
