#include "Processor.h"
#include "Editor.h"

using namespace arc2;

ArcProcessor::ArcProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ARC", [] {
          juce::AudioProcessorValueTreeState::ParameterLayout l;
          juce::StringArray names;
          for (auto& s : styles()) names.add (s.name);
          l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ap::style, 2 }, "Arrangement", names, 0));
          l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ap::variation, 2 }, "Variation", juce::NormalisableRange<float> (0, 1), 0.6f,
              juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; })));
          l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ap::level, 2 }, "Level", juce::NormalisableRange<float> (-30, 6), -3.0f));
          l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ap::keyOn, 2 }, "Key Lock", false));
          juce::StringArray notes; for (int i = 0; i < 12; ++i) notes.add (cl::noteName (i));
          l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ap::root, 2 }, "Root", notes, 9));
          l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ap::minor, 2 }, "Minor", true));
          l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ap::follow, 2 }, "Follow Live", true));
          return l; }())
{
    for (auto* id : { ap::style, ap::variation, ap::keyOn, ap::root, ap::minor })
        apvts.addParameterListener (id, this);
    std::atomic_store (&planPtr, std::make_shared<const Plan> (buildPlan ({}, {})));
    std::atomic_store (&engine, std::make_shared<const Engine>());
    watch.startTimerHz (10);
}

ArcProcessor::~ArcProcessor()
{
    watch.stopTimer();
    pool.removeAllJobs (true, 30000);
    for (auto* id : { ap::style, ap::variation, ap::keyOn, ap::root, ap::minor })
        apvts.removeParameterListener (id, this);
}

bool ArcProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainInputChannelSet() == juce::AudioChannelSet::mono()
            || l.getMainInputChannelSet() == juce::AudioChannelSet::disabled());
}

void ArcProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    for (auto& s : states) s.reset();
    lastAudioBeat = -1;
}

int ArcProcessor::globalTonic() const { return (int) apvts.getRawParameterValue (ap::root)->load(); }
bool ArcProcessor::globalMinor() const { return apvts.getRawParameterValue (ap::minor)->load() > 0.5f; }

// ---------------------------------------------------------------- audio
void ArcProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2 || n == 0) return;
    double ppq = 0, bpm = hostBpm.load(); bool livePlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            livePlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    bpm = juce::jlimit (40.0, 250.0, bpm);
    hostBpm = bpm;
    auto eng = std::atomic_load (&engine);
    if (eng == nullptr || eng->plan == nullptr) return;
    const double song = eng->plan->totalBars * 4.0;
    const bool follow = apvts.getRawParameterValue (ap::follow)->load() > 0.5f;
    const float level = juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ap::level)->load());
    const double bps = bpm / 60.0 / sr;

    // which lanes changed identity since the last block: start their filters/gains fresh
    for (int l = 0; l < (int) eng->uid.size() && l < kMaxLanes; ++l)
        if (stateUid[(size_t) l] != eng->uid[(size_t) l]) { states[(size_t) l].reset(); stateUid[(size_t) l] = eng->uid[(size_t) l]; }

    auto renderAt = [&] (double beat, int off, int len)
    {
        if (std::abs (beat - lastAudioBeat) > 0.02) for (auto& s : states) s.gain = 0;   // jumped: fade in, no click
        float* L = buffer.getWritePointer (0) + off;
        float* R = buffer.getWritePointer (1) + off;
        for (int l = 0; l < (int) eng->audio.size() && l < kMaxLanes; ++l)
            if (eng->audio[(size_t) l] != nullptr && l < (int) eng->plan->lanes.size())
                renderLane (*eng->audio[(size_t) l], eng->plan->lanes[(size_t) l], states[(size_t) l], L, R, len, beat, bpm, sr, eng->gain[(size_t) l] * level);
        lastAudioBeat = beat + len * bps;
    };

    if (follow && livePlaying)
    {
        liveDriving = true;
        playing = true;
        renderAt (ppq, 0, n);
        playPpq = ppq;
        arcPos = ppq;
        return;
    }
    liveDriving = false;
    if (! arcPlaying.load()) { playing = false; lastAudioBeat = -1; return; }
    playing = true;
    double pos = arcPos.load();
    const double ls = loopStart.load(), le = loopEnd.load();
    const double end = le > ls + 0.01 ? le : song;
    const double start = le > ls + 0.01 ? ls : 0.0;
    if (pos >= end || pos < 0) pos = start;
    int done = 0;
    while (done < n)
    {
        const int len = juce::jmin (n - done, juce::jmax (1, (int) std::ceil ((end - pos) / bps)));
        renderAt (pos, done, len);
        pos += len * bps;
        done += len;
        if (pos >= end - 1e-9) pos = start;
    }
    arcPos = pos;
    playPpq = pos;
}

juce::AudioBuffer<float> ArcProcessor::render (int lane, double fromBeat, double beats)
{
    auto eng = std::atomic_load (&engine);
    const double bpm = prepBpm, rate = prepSr;
    const int total = (int) std::ceil (beats * 60.0 / bpm * rate);
    juce::AudioBuffer<float> out (2, juce::jmax (1, total));
    out.clear();
    if (eng == nullptr || eng->plan == nullptr) return out;
    for (int l = 0; l < (int) eng->audio.size(); ++l)
    {
        if (lane >= 0 && l != lane) continue;
        if (eng->audio[(size_t) l] == nullptr || l >= (int) eng->plan->lanes.size()) continue;
        LaneState st;
        float vol = 0;
        { Lock lk (mutex); if (l < (int) lanes.size()) vol = lanes[(size_t) l].volumeDb; }
        const float g = lane >= 0 ? juce::Decibels::decibelsToGain (vol) : eng->gain[(size_t) l];
        st.gain = 0;
        for (int i = 0; i < total; i += 4096)
        {
            const int len = juce::jmin (4096, total - i);
            renderLane (*eng->audio[(size_t) l], eng->plan->lanes[(size_t) l], st, out.getWritePointer (0) + i, out.getWritePointer (1) + i, len,
                        fromBeat + i * bpm / 60.0 / rate, bpm, rate, g);
        }
    }
    return out;
}

// ---------------------------------------------------------------- engine
void ArcProcessor::publishEngine()
{
    Lock lk (mutex);
    auto e = std::make_shared<Engine>();
    bool anySolo = false;
    for (auto& s : lanes) anySolo |= s.solo;
    for (auto& s : lanes)
    {
        e->audio.push_back (s.audio);
        const bool audible = anySolo ? s.solo : ! s.mute;
        e->gain.push_back (audible ? juce::Decibels::decibelsToGain (s.volumeDb) : 0.0f);
        e->uid.push_back (s.uid);
    }
    e->plan = std::atomic_load (&planPtr);
    std::atomic_store (&engine, std::shared_ptr<const Engine> (e));
    ++version;
}

void ArcProcessor::rebuild()
{
    Lock lk (mutex);
    std::vector<LaneInfo> info;
    for (auto& s : lanes) info.push_back (laneInfoOf (s));
    PlanSettings ps;
    ps.style = (int) apvts.getRawParameterValue (ap::style)->load();
    ps.variation = apvts.getRawParameterValue (ap::variation)->load();
    std::atomic_store (&planPtr, std::make_shared<const Plan> (buildPlan (info, ps)));
    publishEngine();
}

int ArcProcessor::indexOfUid (int uid) const
{
    Lock lk (mutex);
    for (int i = 0; i < (int) lanes.size(); ++i) if (lanes[(size_t) i].uid == uid) return i;
    return -1;
}

void ArcProcessor::prepare (int i)
{
    Stem copy;
    int gen;
    double bpm, rate;
    {
        Lock lk (mutex);
        if (i < 0 || i >= (int) lanes.size()) return;
        copy = lanes[(size_t) i];
        copy.peaks.clear();
        gen = ++prepGen[copy.uid];
        bpm = prepBpm; rate = prepSr;
    }
    ++jobs;
    pool.addJob ([this, copy, gen, bpm, rate]
    {
        auto la = prepareStem (copy, bpm, rate);
        auto pk = peaksOf (*la, 720);
        {
            Lock lk (mutex);
            const int idx = indexOfUid (copy.uid);
            if (idx >= 0 && prepGen[copy.uid] == gen)
            {
                lanes[(size_t) idx].audio = la;
                lanes[(size_t) idx].peaks = std::move (pk);
                rebuild();
            }
        }
        --jobs;
        ++version;
    });
}

void ArcProcessor::prepareAll()
{
    const int n = numStems();
    for (int i = 0; i < n; ++i) prepare (i);
}

void ArcProcessor::applyKey()
{
    Lock lk (mutex);
    const bool on = apvts.getRawParameterValue (ap::keyOn)->load() > 0.5f;
    cl::KeyResult g; g.tonic = globalTonic(); g.minor = globalMinor();
    for (int i = 0; i < (int) lanes.size(); ++i)
    {
        auto& s = lanes[(size_t) i];
        const bool tonal = isTonalRole (s.role) && s.key.tonic >= 0;
        s.suggested = tonal ? cl::keyShift (s.key, g) : 0;
        // like Loopworm: bass / chords / lead follow the key, drums never, vocals only if you ask
        const bool autoTune = s.role == rBass || s.role == rChords || s.role == rLead;
        const int want = s.manualTranspose != Stem::kAuto ? s.manualTranspose : (on && autoTune ? s.suggested : 0);
        if (want != s.transpose) { s.transpose = want; prepare (i); }
    }
    ++version;
}

void ArcProcessor::detectKey()
{
    std::array<float, 12> sum {};
    {
        Lock lk (mutex);
        for (auto& s : lanes)
        {
            if (! isTonalRole (s.role)) continue;
            const float w = s.role == rBass ? 1.5f : s.role == rVocal ? 0.7f : 1.0f;
            float tot = 0; for (auto v : s.chroma) tot += v;
            if (tot <= 0) continue;
            for (int k = 0; k < 12; ++k) sum[(size_t) k] += w * s.chroma[(size_t) k] / tot;
        }
    }
    float tot = 0; for (auto v : sum) tot += v;
    if (tot <= 0) { setMessage ("Add a bass, chord or lead stem to find the key"); return; }
    const auto k = cl::findKey (sum);
    if (k.tonic < 0) return;
    pushUndo();
    if (auto* p = apvts.getParameter (ap::root)) p->setValueNotifyingHost (p->convertTo0to1 ((float) k.tonic));
    if (auto* p = apvts.getParameter (ap::minor)) p->setValueNotifyingHost (k.minor ? 1.0f : 0.0f);
    keyEverSet = true;
    setMessage (juce::String ("Key: ") + cl::noteName (k.tonic) + (k.minor ? " minor" : " major"));
    applyKey();
    rebuild();
}

// ---------------------------------------------------------------- loading
bool ArcProcessor::loadMidiStem (Stem& s, const juce::File& f)
{
    auto im = arc::readMidi (f);
    if (! im.ok && im.clip.notes.empty()) return false;
    // play the MIDI through Arc's preview sounds to make an audio loop
    const double bpm = 120, rate = 48000, beats = juce::jmax (4.0, im.clip.lengthBeats);
    arc::Arrangement a;
    a.totalBars = (int) (beats / 4) * 2;
    auto& c = a.lanes[(size_t) im.suggestedLane];
    for (int rep = 0; rep < 2; ++rep) for (auto n : im.clip.notes) { n.start += rep * beats; c.notes.push_back (n); }
    c.sort();
    arc::Preview pv; pv.prepare (rate);
    const int loopN = (int) std::round (beats * 60.0 / bpm * rate);
    juce::AudioBuffer<float> two (2, loopN * 2); two.clear();
    for (int i = 0; i < loopN * 2; i += 256)
    {
        const int len = juce::jmin (256, loopN * 2 - i);
        pv.render (two.getWritePointer (0) + i, two.getWritePointer (1) + i, len, a, i * bpm / 60.0 / rate, bpm, true, 1.4f);
    }
    auto orig = std::make_shared<juce::AudioBuffer<float>> (2, loopN);
    for (int ch = 0; ch < 2; ++ch) orig->copyFrom (ch, 0, two, ch, loopN, loopN);
    s.original = orig;
    s.fileSr = rate;
    s.name = f.getFileNameWithoutExtension();
    s.path = f.getFullPathName();
    s.fromMidi = true;
    static const int laneRole[] { rDrums, rBass, rChords, rLead };
    int role = roleFromName (s.name);
    s.role = role >= 0 ? role : laneRole[juce::jlimit (0, 3, im.suggestedLane)];
    s.timing.tempo = bpm; s.timing.beats = beats; s.timing.bars = (int) (beats / 4); s.timing.offsetSec = 0;
    s.chroma = chromaOf (*orig, rate);
    s.key = cl::findKey (s.chroma);
    s.report = juce::String (s.timing.bars) + " bar MIDI loop";
    return true;
}

// Stems bounced from one session share their start. A stem whose bar 1 can't be told from its own audio
// (a four-to-the-floor kick, hats, a bassline) takes it from a same-length drum loop or clap that can.
static int timingAuthority (int role) { return role == rDrums ? 4 : role == rClap ? 3 : (role == rTops || role == rPerc) ? 2 : role == rKick || role == rHats ? 1 : 0; }

void ArcProcessor::adoptTiming (Stem& s)
{
    // Same-length stems were bounced together: they share a tempo, and if a drum loop showed bar 1 isn't
    // at the file start, they share that too. Kicks, hats and basslines can't tell which beat is the 1.
    Lock lk (mutex);
    if (s.fromMidi || s.original == nullptr || s.timing.fromSet) return;   // the Live set already said exactly
    const double dur = s.original->getNumSamples() / s.fileSr;
    auto sameLength = [dur] (const Stem& o) { return o.original != nullptr && ! o.fromMidi && std::abs (o.original->getNumSamples() / o.fileSr - dur) < 0.005; };
    auto copyFrom = [] (Stem& to, const Stem& from)
    {
        to.timing.tempo = from.timing.tempo; to.timing.bars = from.timing.bars; to.timing.beats = from.timing.beats;
        if ((from.timing.shifted || from.timing.fromSet) && ! to.timing.shifted) { to.timing.offsetSec = from.timing.offsetSec; to.timing.shifted = true; }
        to.report = timingReport (to.timing) + (from.timing.shifted || from.timing.fromSet ? " (from " + from.name + ")" : juce::String());
    };
    const Stem* ref = nullptr;
    for (auto& o : lanes)
        if (sameLength (o) && timingAuthority (o.role) > timingAuthority (s.role) && (ref == nullptr || timingAuthority (o.role) > timingAuthority (ref->role)))
            ref = &o;
    if (ref != nullptr) { copyFrom (s, *ref); return; }
    for (int i = 0; i < (int) lanes.size(); ++i)
    {
        auto& o = lanes[(size_t) i];
        if (sameLength (o) && ! o.timing.fromSet && timingAuthority (s.role) > timingAuthority (o.role)
            && (std::abs (o.timing.tempo - s.timing.tempo) > 1e-6 || (s.timing.shifted && ! o.timing.shifted)))
        {
            copyFrom (o, s);
            prepare (i);
        }
    }
}

void ArcProcessor::insertStem (Stem s)
{
    Lock lk (mutex);
    adoptTiming (s);
    s.uid = nextUid++;
    s.seed = (uint32_t) (++seedCounter * 2654435761u);
    // keep lanes in Loopworm order: kick, bass, hats, clap... (by role), new ones after existing of the same role
    auto it = std::find_if (lanes.begin(), lanes.end(), [&s] (const Stem& o) { return o.role > s.role; });
    const int idx = (int) (it - lanes.begin());
    lanes.insert (it, std::move (s));
    if (! keyEverSet && isTonalRole (lanes[(size_t) idx].role)) needKeyDetect = true;
    applyKey();
    prepare (idx);
    rebuild();
}

bool ArcProcessor::addFileSync (const juce::File& f, int role)
{
    Stem s;
    const auto ext = f.getFileExtension().toLowerCase();
    const bool ok = (ext == ".mid" || ext == ".midi") ? loadMidiStem (s, f) : loadStem (s, f, hostBpm.load());
    if (! ok) { setMessage ("Couldn't read " + f.getFileName()); return false; }
    if (role >= 0 && role != s.role) { s.role = role; analyseStem (s, hostBpm.load(), role); }
    if (numStems() >= kMaxLanes) { setMessage ("Arc holds 12 stems"); return false; }
    pushUndo();
    insertStem (std::move (s));
    if (needKeyDetect.exchange (false)) detectKey();
    return true;
}

void ArcProcessor::addFiles (const juce::StringArray& paths)
{
    for (auto& p : paths)
    {
        const juce::File f (p);
        if (f.isDirectory())
        {
            juce::StringArray inner;
            for (auto& c : f.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi"))
                inner.add (c.getFullPathName());
            inner.sort (true);
            addFiles (inner);
            continue;
        }
        if (f.hasFileExtension ("als")) { importSet (f); continue; }
        ++jobs;
        pool.addJob ([this, f]
        {
            Stem s;
            const auto ext = f.getFileExtension().toLowerCase();
            const bool ok = (ext == ".mid" || ext == ".midi") ? loadMidiStem (s, f) : loadStem (s, f, hostBpm.load());
            if (! ok) setMessage ("Couldn't read " + f.getFileName());
            else if (numStems() >= kMaxLanes) setMessage ("Arc holds 12 stems");
            else
            {
                Lock lk (mutex);
                undoStack.push_back ({ lanes, globalTonic(), globalMinor() ? 1 : 0, (int) apvts.getRawParameterValue (ap::keyOn)->load() });
                redoStack.clear();
                insertStem (std::move (s));
                setMessage ("Added " + f.getFileName());
            }
            --jobs;
            ++version;
        });
    }
}

void ArcProcessor::importSet (const juce::File& als)
{
    ++jobs;
    pool.addJob ([this, als]
    {
        const auto set = live::read (als);
        if (! set.ok) { setMessage ("Couldn't read " + als.getFileName()); --jobs; return; }
        // one lane per audio track: the first clip it plays in the arrangement (or its first session clip)
        juce::StringArray done;
        int added = 0;
        for (auto& c : set.clips)
        {
            if (done.contains (c.track)) continue;
            done.add (c.track);
            if (numStems() >= kMaxLanes) break;
            Stem s;
            if (! stemFromClip (s, c)) continue;
            analyseStem (s, hostBpm.load(), -1);
            {
                Lock lk (mutex);
                undoStack.push_back ({ lanes, globalTonic(), globalMinor() ? 1 : 0, apvts.getRawParameterValue (ap::keyOn)->load() > 0.5f ? 1 : 0 });
                redoStack.clear();
                insertStem (std::move (s));
            }
            ++added;
        }
        juce::String msg = "Imported " + juce::String (added) + " audio tracks from " + als.getFileNameWithoutExtension()
                         + " (" + juce::String (set.tempo, 0) + " BPM)";
        if (! set.midiTracks.isEmpty()) msg << ". MIDI tracks need bouncing to audio first: " << set.midiTracks.joinIntoString (", ");
        setMessage (msg);
        --jobs;
        ++version;
    });
}

void ArcProcessor::removeStem (int i)
{
    pushUndo();
    Lock lk (mutex);
    if (i < 0 || i >= (int) lanes.size()) return;
    lanes.erase (lanes.begin() + i);
    rebuild();
}

void ArcProcessor::replaceStem (int i, const juce::File& f)
{
    Stem s;
    const auto ext = f.getFileExtension().toLowerCase();
    const bool ok = (ext == ".mid" || ext == ".midi") ? loadMidiStem (s, f) : loadStem (s, f, hostBpm.load());
    if (! ok) { setMessage ("Couldn't read " + f.getFileName()); return; }
    pushUndo();
    Lock lk (mutex);
    if (i < 0 || i >= (int) lanes.size()) return;
    s.role = lanes[(size_t) i].role;
    s.volumeDb = lanes[(size_t) i].volumeDb;
    s.uid = nextUid++;
    s.seed = lanes[(size_t) i].seed;
    lanes[(size_t) i] = std::move (s);
    applyKey();
    prepare (i);
    rebuild();
}

// ---------------------------------------------------------------- edits
void ArcProcessor::pushUndo()
{
    Lock lk (mutex);
    undoStack.push_back ({ lanes, globalTonic(), globalMinor() ? 1 : 0, apvts.getRawParameterValue (ap::keyOn)->load() > 0.5f ? 1 : 0 });
    if (undoStack.size() > 64) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

static bool needsPrep (const Stem& a, const Stem& b)
{
    return a.uid != b.uid || a.cropStart != b.cropStart || a.cropBars != b.cropBars || a.nudge != b.nudge || a.transpose != b.transpose || a.audio == nullptr;
}

void ArcProcessor::undo()
{
    Lock lk (mutex);
    if (undoStack.empty()) return;
    redoStack.push_back ({ lanes, globalTonic(), globalMinor() ? 1 : 0, apvts.getRawParameterValue (ap::keyOn)->load() > 0.5f ? 1 : 0 });
    auto snap = undoStack.back(); undoStack.pop_back();
    auto old = lanes;
    lanes = snap.lanes;
    if (auto* p = apvts.getParameter (ap::root)) p->setValueNotifyingHost (p->convertTo0to1 ((float) snap.root));
    if (auto* p = apvts.getParameter (ap::minor)) p->setValueNotifyingHost ((float) snap.minor);
    if (auto* p = apvts.getParameter (ap::keyOn)) p->setValueNotifyingHost ((float) snap.keyOn);
    dirty = false;
    for (int i = 0; i < (int) lanes.size(); ++i)
    {
        auto it = std::find_if (old.begin(), old.end(), [&] (const Stem& o) { return o.uid == lanes[(size_t) i].uid; });
        if (lanes[(size_t) i].audio == nullptr || (it != old.end() && needsPrep (*it, lanes[(size_t) i]))
            || (lanes[(size_t) i].audio != nullptr && (std::abs (lanes[(size_t) i].audio->bpm - prepBpm) > 1e-3 || lanes[(size_t) i].audio->sr != prepSr)))
            prepare (i);
    }
    applyKey();
    rebuild();
}

void ArcProcessor::redo()
{
    Lock lk (mutex);
    if (redoStack.empty()) return;
    undoStack.push_back ({ lanes, globalTonic(), globalMinor() ? 1 : 0, apvts.getRawParameterValue (ap::keyOn)->load() > 0.5f ? 1 : 0 });
    auto snap = redoStack.back(); redoStack.pop_back();
    lanes = snap.lanes;
    if (auto* p = apvts.getParameter (ap::root)) p->setValueNotifyingHost (p->convertTo0to1 ((float) snap.root));
    if (auto* p = apvts.getParameter (ap::minor)) p->setValueNotifyingHost ((float) snap.minor);
    if (auto* p = apvts.getParameter (ap::keyOn)) p->setValueNotifyingHost ((float) snap.keyOn);
    dirty = false;
    for (int i = 0; i < (int) lanes.size(); ++i)
        if (lanes[(size_t) i].audio == nullptr || std::abs (lanes[(size_t) i].audio->bpm - prepBpm) > 1e-3 || lanes[(size_t) i].audio->sr != prepSr
            || lanes[(size_t) i].audio->bars != (lanes[(size_t) i].cropBars > 0 ? juce::jmin (lanes[(size_t) i].cropBars, sourceBars (lanes[(size_t) i])) : juce::jmin (sourceBars (lanes[(size_t) i]), 8)))
            prepare (i);
    applyKey();
    rebuild();
}

#define ARC_EDIT(body) { pushUndo(); Lock lk (mutex); if (i < 0 || i >= (int) lanes.size()) return; auto& s = lanes[(size_t) i]; body }

void ArcProcessor::setRole (int i, int role) ARC_EDIT (s.role = juce::jlimit (0, kRoles - 1, role); s.vary = defaultVary (s.role); applyKey(); rebuild();)
void ArcProcessor::setVary (int i, int v) ARC_EDIT (s.vary = juce::jlimit (0, 2, v); rebuild();)
void ArcProcessor::setSourceTempo (int i, double bpm) ARC_EDIT (
    const double dur = s.original != nullptr ? s.original->getNumSamples() / s.fileSr : 0;
    if (dur <= 0 || bpm < 40) return;
    s.timing.bars = juce::jmax (1, (int) std::round (dur * bpm / 240.0)); s.timing.beats = s.timing.bars * 4.0; s.timing.tempo = s.timing.bars * 240.0 / dur;
    s.report = timingReport (s.timing); prepare (i);)
void ArcProcessor::barOneAtStart (int i) ARC_EDIT (s.timing.offsetSec = 0; s.timing.shifted = false; s.nudge = 0; s.report = timingReport (s.timing); prepare (i);)
void ArcProcessor::setNudge (int i, int st) ARC_EDIT (s.nudge = ((st % 64) + 64) % 64; prepare (i);)
void ArcProcessor::setCrop (int i, int startBar, int bars) ARC_EDIT (s.cropStart = juce::jlimit (0, sourceBars (s) - 1, startBar); s.cropBars = juce::jlimit (0, 16, bars); prepare (i);)
void ArcProcessor::setManualTranspose (int i, int semis) ARC_EDIT (s.manualTranspose = semis; applyKey(); rebuild();)
void ArcProcessor::setMute (int i, bool v) ARC_EDIT (s.mute = v; publishEngine();)
void ArcProcessor::setSolo (int i, bool v) ARC_EDIT (s.solo = v; publishEngine();)
void ArcProcessor::setHold (int i, bool v) ARC_EDIT (s.hold = v; ++version;)
void ArcProcessor::dice (int i) ARC_EDIT (s.seed = (uint32_t) (++seedCounter * 2654435761u) ^ (uint32_t) juce::Time::getMillisecondCounter(); rebuild();)

void ArcProcessor::setVolume (int i, float dB, bool undoable)
{
    if (undoable) pushUndo();
    Lock lk (mutex);
    if (i < 0 || i >= (int) lanes.size()) return;
    lanes[(size_t) i].volumeDb = juce::jlimit (-60.0f, 6.0f, dB);
    publishEngine();
}

void ArcProcessor::generate()
{
    pushUndo();
    Lock lk (mutex);
    const auto t = (uint32_t) juce::Time::getMillisecondCounter();
    for (auto& s : lanes)
        if (! s.hold) s.seed = (uint32_t) (++seedCounter * 2654435761u) ^ t;
    rebuild();
}

void ArcProcessor::Watch::timerCallback()
{
    if (p.needKeyDetect.exchange (false)) p.detectKey();
    if (p.dirty.exchange (false)) { p.applyKey(); p.rebuild(); }
    // follow Live's tempo / sample rate: re-stretch once it has settled
    const double bpm = p.hostBpm.load();
    if (std::abs (bpm - p.prepBpm) > 0.005 || std::abs (p.sr - p.prepSr) > 0.5)
    {
        if (++tempoQuiet >= 4)
        {
            tempoQuiet = 0;
            { Lock lk (p.mutex); p.prepBpm = bpm; p.prepSr = p.sr; }
            p.prepareAll();
        }
    }
    else tempoQuiet = 0;
    if (++tick % 5 == 0) p.pollCapture();
}

// ---------------------------------------------------------------- live capture
void ArcProcessor::startCapture (int bars)
{
    arccap::Request r;
    r.id = juce::String (arccap::now());
    r.bars = juce::jlimit (1, 64, bars);
    r.created = arccap::now();
    arccap::writeRequest (r);
    Lock lk (mutex);
    cap = {};
    cap.active = true; cap.id = r.id; cap.bars = r.bars; cap.started = r.created;
    cap.status = "Press play in Live. Arc Tap records " + juce::String (r.bars) + " bars from the next bar line.";
    setMessage (cap.status);
}

void ArcProcessor::cancelCapture()
{
    Lock lk (mutex);
    if (! cap.active) return;
    arccap::Request r; r.id = cap.id; r.bars = cap.bars; r.created = cap.started; r.cancel = true;
    arccap::writeRequest (r);
    cap.active = false;
    cap.status = {};
    setMessage ("Capture cancelled");
}

void ArcProcessor::pollCapture()
{
    auto list = arccap::liveTaps();
    Lock lk (mutex);
    const bool changed = list.size() != tapList.size()
        || ! std::equal (list.begin(), list.end(), tapList.begin(), [] (const arccap::TapInfo& a, const arccap::TapInfo& b)
                         { return a.id == b.id && a.name == b.name && a.state == b.state && std::abs (a.progress - b.progress) < 0.01; });
    tapList = list;
    if (changed) ++version;
    if (! cap.active) return;

    int waiting = 0, recording = 0, finished = 0, mine = 0;
    double prog = 0;
    for (auto& t : list)
    {
        if (t.request != cap.id) continue;
        ++mine;
        if (t.state == "armed") ++waiting;
        else if (t.state == "recording") { ++recording; prog = juce::jmax (prog, t.progress); }
        else if (t.state == "done" || t.state == "silent") ++finished;
        if (t.state == "done" && t.file.isNotEmpty() && ! cap.loaded.contains (t.id))
        {
            cap.loaded.add (t.id);
            loadCaptured (t, cap.bars);
        }
    }
    const auto age = arccap::now() - cap.started;
    if (mine == 0)
        cap.status = age > 3000 ? "No Arc Tap found. Put Arc Tap on the tracks you want (it's in Live's plug-in list), then press CAPTURE again."
                                : "Looking for Arc Tap on your tracks...";
    else if (recording > 0)
        cap.status = "Capturing " + juce::String (mine) + (mine == 1 ? " track" : " tracks") + ": bar "
                   + juce::String (juce::jmin (cap.bars, 1 + (int) (prog * cap.bars))) + " of " + juce::String (cap.bars);
    else if (waiting > 0)
        cap.status = juce::String (mine) + (mine == 1 ? " track" : " tracks") + " ready. Press play in Live: recording starts on the next bar.";
    if (mine > 0 && waiting == 0 && recording == 0 && finished == mine && age > 1500)
    {
        cap.active = false;
        const int got = cap.loaded.size();
        cap.status = {};
        setMessage (got > 0 ? "Captured " + juce::String (got) + (got == 1 ? " track" : " tracks") + " from Live, in time at "
                                  + juce::String (list.empty() ? hostBpm.load() : list.front().bpm, 1) + " BPM. Bar 1 = the bar the capture started on."
                            : "Every tapped track was silent. Check the tracks are playing, then CAPTURE again.");
        return;
    }
    if (age > 15 * 60 * 1000) { cap.active = false; cap.status = {}; setMessage ("Capture timed out"); return; }
    setMessage (cap.status);
}

void ArcProcessor::loadCaptured (const arccap::TapInfo& t, int bars)
{
    ++jobs;
    const juce::File f (t.file);
    const auto name = t.name;
    const double bpm = t.bpm;
    pool.addJob ([this, f, name, bpm, bars] { addCapturedSync (f, name, bpm, bars); --jobs; ++version; });
}

void ArcProcessor::addCapturedSync (const juce::File& f, const juce::String& track, double bpm, int bars)
{
    Stem s;
    if (! decodeStem (s, f)) { setMessage ("Couldn't read the capture of " + track); return; }
    s.name = track;
    s.clipTrack = track;
    s.timing.tempo = bpm > 20 ? bpm : hostBpm.load();
    s.timing.bars = juce::jmax (1, bars);
    s.timing.beats = s.timing.bars * 4.0;
    s.timing.offsetSec = 0;
    s.timing.fromSet = true;
    s.timing.shifted = false;
    // captured again? replace the lane that came from this track and keep how it was set up
    int existing = -1;
    {
        Lock lk (mutex);
        for (int i = 0; i < (int) lanes.size(); ++i)
            if (lanes[(size_t) i].clipTrack == track && lanes[(size_t) i].timing.fromSet) { existing = i; break; }
    }
    if (existing >= 0)
    {
        int role;
        { Lock lk (mutex); role = lanes[(size_t) existing].role; }
        analyseStem (s, hostBpm.load(), role);
        pushUndo();
        Lock lk (mutex);
        if (existing >= (int) lanes.size()) return;
        auto& old = lanes[(size_t) existing];
        s.role = old.role; s.vary = old.vary; s.volumeDb = old.volumeDb; s.mute = old.mute; s.solo = old.solo;
        s.hold = old.hold; s.nudge = old.nudge; s.manualTranspose = old.manualTranspose; s.seed = old.seed;
        s.uid = nextUid++;
        old = std::move (s);
        applyKey();
        prepare (existing);
        rebuild();
        return;
    }
    analyseStem (s, hostBpm.load(), -1);
    if (numStems() >= kMaxLanes) { setMessage ("Arc holds 12 stems"); return; }
    pushUndo();
    insertStem (std::move (s));
    if (needKeyDetect.exchange (false)) detectKey();
}

// ---------------------------------------------------------------- export
juce::File ArcProcessor::exportFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Arc Exports");
}

static bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate)
{
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, 2, 24, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

juce::File ArcProcessor::exportLane (int i, const juce::File& folder)
{
    juce::String name;
    {
        Lock lk (mutex);
        if (i < 0 || i >= (int) lanes.size()) return {};
        name = juce::String (i + 1).paddedLeft ('0', 2) + " " + roleName (lanes[(size_t) i].role) + " - " + juce::File::createLegalFileName (lanes[(size_t) i].name);
    }
    folder.createDirectory();
    const auto f = folder.getChildFile (name + ".wav");
    auto b = render (i, 0, songBeats());
    return writeWav (f, b, prepSr) ? f : juce::File();
}

juce::File ArcProcessor::exportMix (const juce::File& folder)
{
    folder.createDirectory();
    const auto f = folder.getChildFile ("00 Arc Mix.wav");
    auto b = render (-1, 0, songBeats());
    return writeWav (f, b, prepSr) ? f : juce::File();
}

juce::Array<juce::File> ArcProcessor::exportAll (bool reveal)
{
    const auto folder = exportFolder().getChildFile ("Arc " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S") + " " + juce::String (prepBpm, 0) + " BPM");
    juce::Array<juce::File> out;
    for (int i = 0; i < numStems(); ++i) if (auto f = exportLane (i, folder); f.existsAsFile()) out.add (f);
    if (auto f = exportMix (folder); f.existsAsFile()) out.add (f);
    if (reveal && ! out.isEmpty()) out.getFirst().revealToUser();
    setMessage ("Exported " + juce::String (out.size()) + " files to " + folder.getFileName());
    return out;
}

// ---------------------------------------------------------------- state
void ArcProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.removeChild (st.getChildWithName ("STEMS"), nullptr);
    juce::ValueTree ls ("STEMS");
    ls.setProperty ("keySet", keyEverSet.load(), nullptr);
    {
        Lock lk (mutex);
        for (auto& s : lanes)
        {
            juce::ValueTree t ("STEM");
            t.setProperty ("path", s.path, nullptr);
            t.setProperty ("role", s.role, nullptr);
            t.setProperty ("cropStart", s.cropStart, nullptr);
            t.setProperty ("cropBars", s.cropBars, nullptr);
            t.setProperty ("nudge", s.nudge, nullptr);
            t.setProperty ("manual", s.manualTranspose, nullptr);
            t.setProperty ("vol", s.volumeDb, nullptr);
            t.setProperty ("mute", s.mute, nullptr);
            t.setProperty ("solo", s.solo, nullptr);
            t.setProperty ("hold", s.hold, nullptr);
            t.setProperty ("vary", s.vary, nullptr);
            t.setProperty ("tempo", s.timing.tempo, nullptr);
            t.setProperty ("bars", s.timing.bars, nullptr);
            t.setProperty ("offset", s.timing.offsetSec, nullptr);
            t.setProperty ("shifted", s.timing.shifted, nullptr);
            t.setProperty ("fromSet", s.timing.fromSet, nullptr);
            t.setProperty ("track", s.clipTrack, nullptr);
            t.setProperty ("seed", (juce::int64) s.seed, nullptr);
            ls.appendChild (t, nullptr);
        }
    }
    st.appendChild (ls, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void ArcProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto st = juce::ValueTree::fromXml (*xml);
    if (! st.isValid()) return;
    auto ls = st.getChildWithName ("STEMS");
    auto params = st.createCopy();
    params.removeChild (params.getChildWithName ("STEMS"), nullptr);
    apvts.replaceState (params);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            for (int i = 0; i < params.getNumChildren(); ++i)
                if (params.getChild (i).getProperty ("id").toString() == rp->getParameterID())
                    rp->setValueNotifyingHost (rp->convertTo0to1 ((float) params.getChild (i).getProperty ("value")));
    keyEverSet = (bool) ls.getProperty ("keySet", false);
    {
        Lock lk (mutex);
        lanes.clear();
        undoStack.clear(); redoStack.clear();
        rebuild();
    }
    if (! ls.isValid() || ls.getNumChildren() == 0) return;
    // reload the stems in the background, in their saved order, with their saved settings
    ++jobs;
    pool.addJob ([this, ls]
    {
        juce::StringArray missing;
        for (int k = 0; k < ls.getNumChildren(); ++k)
        {
            auto t = ls.getChild (k);
            const juce::File f (t.getProperty ("path").toString());
            Stem s;
            const auto ext = f.getFileExtension().toLowerCase();
            const bool ok = f.existsAsFile() && ((ext == ".mid" || ext == ".midi") ? loadMidiStem (s, f) : loadStem (s, f, hostBpm.load()));
            if (! ok) { missing.add (f.getFileName()); continue; }
            const int role = (int) t.getProperty ("role", s.role);
            if (role != s.role && ! s.fromMidi) analyseStem (s, hostBpm.load(), role);
            s.role = role;
            s.cropStart = t.getProperty ("cropStart", s.cropStart);
            s.cropBars = t.getProperty ("cropBars", s.cropBars);
            s.nudge = t.getProperty ("nudge", 0);
            s.manualTranspose = t.getProperty ("manual", Stem::kAuto);
            s.volumeDb = (float) (double) t.getProperty ("vol", 0.0);
            s.mute = t.getProperty ("mute", false);
            s.solo = t.getProperty ("solo", false);
            s.hold = t.getProperty ("hold", false);
            s.vary = t.getProperty ("vary", defaultVary (s.role));
            if (t.hasProperty ("tempo"))
            {
                s.timing.tempo = t.getProperty ("tempo"); s.timing.bars = t.getProperty ("bars"); s.timing.beats = s.timing.bars * 4.0;
                s.timing.offsetSec = t.getProperty ("offset"); s.timing.shifted = t.getProperty ("shifted");
                s.timing.fromSet = t.getProperty ("fromSet", s.timing.fromSet);
                if (t.hasProperty ("track")) { s.clipTrack = t.getProperty ("track").toString(); if (s.clipTrack.isNotEmpty()) s.name = s.clipTrack; }
                s.report = timingReport (s.timing);
            }
            Lock lk (mutex);
            s.uid = nextUid++;
            s.seed = (uint32_t) (juce::int64) t.getProperty ("seed", 1);
            lanes.push_back (std::move (s));     // saved order is kept as it was
            applyKey();
            prepare ((int) lanes.size() - 1);
            rebuild();
        }
        if (! missing.isEmpty()) setMessage ("Couldn't find: " + missing.joinIntoString (", "));
        --jobs;
        ++version;
    });
}

juce::AudioProcessorEditor* ArcProcessor::createEditor() { return new ArcEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ArcProcessor(); }
