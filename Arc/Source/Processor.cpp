#include "Processor.h"
#include "Editor.h"

ArcProcessor::ArcProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ARC", [] {
          juce::AudioProcessorValueTreeState::ParameterLayout l;
          juce::StringArray names;
          for (auto& t : arc::templates()) names.add (t.name);
          l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ap::style, 1 }, "Arrangement", names, 0));
          l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ap::variation, 1 }, "Variation", juce::NormalisableRange<float> (0, 1), 0.6f,
              juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; })));
          l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ap::preview, 1 }, "Preview", true));
          l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ap::level, 1 }, "Preview Level", juce::NormalisableRange<float> (-30, 6), -6.0f));
          for (int i = 0; i < arc::kNumLanes; ++i)
              l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ap::laneOn (i), 1 }, juce::String (arc::laneName (i)) + " On", true));
          return l; }())
{
    for (auto* id : { ap::style, ap::variation, ap::laneOn (0), ap::laneOn (1), ap::laneOn (2), ap::laneOn (3) })
        apvts.addParameterListener (id, this);
    std::atomic_store (&arr, std::make_shared<const arc::Arrangement>());
    regenTimer.startTimerHz (5);
}

ArcProcessor::~ArcProcessor()
{
    regenTimer.stopTimer();
    pool.removeAllJobs (true, 20000);
    for (auto* id : { ap::style, ap::variation, ap::laneOn (0), ap::laneOn (1), ap::laneOn (2), ap::laneOn (3) })
        apvts.removeParameterListener (id, this);
}

bool ArcProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainInputChannelSet() == juce::AudioChannelSet::mono());
}

void ArcProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    preview.prepare (sampleRate);
}

void ArcProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    double ppq = 0, bpm = 120; bool isPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            isPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
        }
    hostBpm = bpm;
    // follow Live while it plays; otherwise run Arc's own transport (Play button / click to locate)
    double pos = ppq;
    bool run = isPlaying;
    if (isPlaying) arcPos = ppq;
    else if (arcPlaying.load())
    {
        pos = arcPos.load();
        const double le = loopEnd.load(), ls = loopStart.load();
        if (le > ls && pos >= le) pos = ls + std::fmod (pos - ls, le - ls);
        arcPos = pos + n * bpm / 60.0 / sr;
        run = true;
    }
    playPpq = run ? pos : arcPos.load();
    playing = run;
    if (apvts.getRawParameterValue (ap::preview)->load() > 0.5f)
    {
        auto a = std::atomic_load (&arr);
        if (a) preview.render (buffer.getWritePointer (0), buffer.getWritePointer (1), n, *a, pos, bpm, run,
                               juce::Decibels::decibelsToGain (apvts.getRawParameterValue (ap::level)->load()));
    }
}

// ---------------------------------------------------------------- loading
static bool isMidiFile (const juce::File& f) { return f.hasFileExtension ("mid;midi"); }

void ArcProcessor::convert (int laneIn, const juce::File& f)
{
    LaneSource src;
    src.source = f.getFileName();
    int lane = laneIn;
    if (isMidiFile (f))
    {
        auto m = arc::readMidi (f);
        if (! m.ok) { src.report = "Could not read that MIDI file"; }
        else
        {
            if (lane < 0) lane = m.suggestedLane;
            src.loop = m.clip;
            src.report = juce::String ((int) m.clip.notes.size()) + " notes, " + juce::String (m.clip.lengthBeats / 4.0, 0) + " bars (MIDI)";
        }
    }
    else
    {
        auto a = arc::loadAudio (f);
        if (! a.ok) src.report = "Could not read that audio file";
        else
        {
            
            if (lane < 0)
            {
                // guess: noisy/percussive -> drums; pitched and low -> bass; pitched -> lead; else chords
                cl::PitchTracker tr; tr.prepare ((float) a.sr);
                int frames = 0, voiced = 0; double sumMidi = 0; float clean = 0;
                for (auto s : a.mono) if (tr.push (s) && tr.rms > 0.01f) { ++frames; if (tr.voiced) { ++voiced; clean += 1.0f - tr.aperiodicity; sumMidi += 69 + 12 * std::log2 (tr.hz / 440.0f); } }
                const float vf = frames ? (float) voiced / frames : 0;        // share of sounding time that has a clear pitch
                const float periodic = voiced ? clean / voiced : 0;            // single notes are very periodic, chords much less
                if (std::getenv ("ARC_DEBUG")) std::printf ("    [guess %s] voiced %.2f periodic %.2f mean %.1f\n", f.getFileName().toRawUTF8(), vf, periodic, voiced ? sumMidi / voiced : 0.0);
                if (vf < 0.35f || (vf < 0.6f && voiced && sumMidi / voiced < 40)) lane = arc::laneDrums;
                else if (voiced && sumMidi / voiced < 52) lane = arc::laneBass;
                else if (vf > 0.6f && periodic > 0.8f) lane = arc::laneLead;
                else lane = arc::laneChords;
            }
            // only drum loops get a downbeat search: tonal stems (offbeat basslines!) are trusted to start on the 1
            const auto tm = arc::analyseTiming (a, hostBpm.load(), lane == arc::laneDrums);
            juce::String rep;
            if (lane == arc::laneDrums) src.loop = arc::drumsToMidi (a, tm, &rep);
            else if (lane == arc::laneChords) src.loop = arc::polyToMidi (a, tm, &rep);
            else src.loop = arc::monoToMidi (a, tm, &rep);
            src.fromAudio = true;
            src.tempo = tm.tempo;
            src.report = rep + ", " + juce::String (tm.bars) + " bars @ " + juce::String (tm.tempo, 1) + " BPM"
                       + (tm.offsetSec > 0.005 ? ", bar 1 found " + juce::String (juce::roundToInt (tm.offsetSec * 1000)) + " ms in" : juce::String());
        }
    }
    {
        std::lock_guard<std::mutex> lk (mutex);
        if (lane >= 0) lanes[(size_t) lane] = src;
    }
    busyLane = -1;
    triggerRegenerate = true;
}

void ArcProcessor::loadFile (int lane, const juce::File& f)
{
    busyLane = lane < 0 ? 99 : lane;
    pool.addJob ([this, lane, f] { convert (lane, f); });
}

void ArcProcessor::loadFileSync (int lane, const juce::File& f) { busyLane = lane; convert (lane, f); regenerate (false); }

void ArcProcessor::clearLane (int lane)
{
    { std::lock_guard<std::mutex> lk (mutex); lanes[(size_t) lane] = LaneSource(); }
    regenerate (false);
}

void ArcProcessor::nudgeLane (int lane, int steps)
{
    {
        std::lock_guard<std::mutex> lk (mutex);
        auto& L = lanes[(size_t) lane];
        if (L.loop.empty() || steps == 0) return;
        const double len = L.loop.lengthBeats, d = steps * 0.25;
        for (auto& nt : L.loop.notes) { nt.start = std::fmod (nt.start - d, len); if (nt.start < 0) nt.start += len; }
        L.loop.sort();
        L.nudge += steps;
    }
    regenerate (false);
}

void ArcProcessor::regenerate (bool newSeed)
{
    if (newSeed) seed = (uint32_t) juce::Random::getSystemRandom().nextInt (1 << 30);
    std::array<arc::Clip, arc::kNumLanes> loops;
    { std::lock_guard<std::mutex> lk (mutex); for (int l = 0; l < arc::kNumLanes; ++l) loops[(size_t) l] = lanes[(size_t) l].loop; }
    arc::Settings s;
    s.templateIndex = (int) apvts.getRawParameterValue (ap::style)->load();
    s.variation = apvts.getRawParameterValue (ap::variation)->load();
    s.seed = seed;
    for (int l = 0; l < arc::kNumLanes; ++l) s.enabled[(size_t) l] = apvts.getRawParameterValue (ap::laneOn (l))->load() > 0.5f;
    arc::Arranger g;
    auto a = std::make_shared<const arc::Arrangement> (g.build (loops, s));
    std::atomic_store (&arr, a);
    ++version;
}

// ---------------------------------------------------------------- export
juce::File ArcProcessor::exportFolder() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Arc Exports");
    d.createDirectory();
    return d;
}

juce::File ArcProcessor::exportLane (int l)
{
    auto a = arrangement();
    if (! a || a->lanes[(size_t) l].empty()) return {};
    const juce::String style = arc::templates()[(size_t) juce::jlimit (0, (int) arc::templates().size() - 1, (int) apvts.getRawParameterValue (ap::style)->load())].name;
    auto f = exportFolder().getChildFile ("Arc " + juce::String (arc::laneName (l)) + " - " + style.upToFirstOccurrenceOf (" (", false, false).replaceCharacter ('/', '-') + ".mid");
    arc::writeMidi (f, a->lanes[(size_t) l], hostBpm.load(), l == arc::laneDrums, juce::String ("Arc ") + arc::laneName (l));
    return f;
}

juce::Array<juce::File> ArcProcessor::exportAll()
{
    juce::Array<juce::File> out;
    for (int l = 0; l < arc::kNumLanes; ++l) { auto f = exportLane (l); if (f.existsAsFile()) out.add (f); }
    return out;
}

// ---------------------------------------------------------------- state
void ArcProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("seed", (int) seed, nullptr);
    juce::ValueTree ls ("LANES");
    {
        std::lock_guard<std::mutex> lk (mutex);
        for (int l = 0; l < arc::kNumLanes; ++l)
        {
            juce::ValueTree t ("LANE");
            t.setProperty ("i", l, nullptr);
            t.setProperty ("src", lanes[(size_t) l].source, nullptr);
            t.setProperty ("report", lanes[(size_t) l].report, nullptr);
            t.setProperty ("len", lanes[(size_t) l].loop.lengthBeats, nullptr);
            t.setProperty ("audio", lanes[(size_t) l].fromAudio, nullptr);
            t.setProperty ("nudge", lanes[(size_t) l].nudge, nullptr);
            juce::String notes;
            for (auto& n : lanes[(size_t) l].loop.notes)
                notes << n.pitch << "," << n.start << "," << n.length << "," << n.velocity << ";";
            t.setProperty ("notes", notes, nullptr);
            ls.appendChild (t, nullptr);
        }
    }
    st.removeChild (st.getChildWithName ("LANES"), nullptr);
    st.appendChild (ls, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void ArcProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            auto ls = tree.getChildWithName ("LANES");
            {
                std::lock_guard<std::mutex> lk (mutex);
                for (int i = 0; i < ls.getNumChildren(); ++i)
                {
                    auto t = ls.getChild (i);
                    const int l = (int) t.getProperty ("i", -1);
                    if (l < 0 || l >= arc::kNumLanes) continue;
                    LaneSource s;
                    s.source = t.getProperty ("src").toString();
                    s.report = t.getProperty ("report").toString();
                    s.fromAudio = (bool) t.getProperty ("audio", false);
                    s.nudge = (int) t.getProperty ("nudge", 0);
                    s.loop.lengthBeats = (double) t.getProperty ("len", 4.0);
                    for (auto& tok : juce::StringArray::fromTokens (t.getProperty ("notes").toString(), ";", ""))
                    {
                        auto p = juce::StringArray::fromTokens (tok, ",", "");
                        if (p.size() < 4) continue;
                        arc::Note n; n.pitch = p[0].getIntValue(); n.start = p[1].getDoubleValue(); n.length = p[2].getDoubleValue(); n.velocity = p[3].getFloatValue();
                        s.loop.notes.push_back (n);
                    }
                    lanes[(size_t) l] = s;
                }
            }
            tree.removeChild (ls, nullptr);
            seed = (uint32_t) (int) tree.getProperty ("seed", 1);
            apvts.replaceState (tree);
            for (auto* p : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    auto child = tree.getChildWithProperty ("id", r->paramID);
                    if (child.isValid() && child.hasProperty ("value"))
                        r->setValueNotifyingHost (r->convertTo0to1 ((float) child.getProperty ("value")));
                }
            regenerate (false);
        }
}

juce::AudioProcessorEditor* ArcProcessor::createEditor() { return new ArcEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ArcProcessor(); }
