#include "Sanctum.h"
#include "Editor.h"

using namespace sp;

const std::vector<ChoirPreset>& choirPresets()
{
    static const std::vector<ChoirPreset> list {
        { "Chapel 12", "Twelve voices in four parts, a warm stone room", {} },
        { "Cathedral 24", "Big four-part choir far back in a long hall",
            { { singers, 24 }, { humanise, 0.6f }, { room, 0.92f }, { reverb, 0.55f }, { tone, -0.1f },
              { depth[0], 0.75f }, { depth[1], 0.65f }, { depth[2], 0.65f }, { depth[3], 0.75f } } },
        { "Chamber 8", "Small, close, three-part ensemble",
            { { harmony, 3 }, { singers, 8 }, { humanise, 0.4f }, { room, 0.3f }, { reverb, 0.2f }, { spread, 0.5f },
              { depth[0], 0.2f }, { depth[1], 0.15f }, { depth[2], 0.15f }, { depth[3], 0.2f } } },
        { "Doubles", "Two or four tight doubles of the lead, no harmony",
            { { harmony, 0 }, { singers, 4 }, { humanise, 0.35f }, { character, 0.15f }, { spread, 0.6f }, { reverb, 0.1f }, { room, 0.2f },
              { level[0], 0.0f }, { level[3], 0.0f }, { pan[1], -0.7f }, { pan[2], 0.7f }, { depth[1], 0.05f }, { depth[2], 0.05f },
              { choir, -4.0f }, { breath, 0.1f } } },
        { "Wide BVs", "A third above and below, wide and dry: classic backing vocals",
            { { harmony, 2 }, { singers, 6 }, { humanise, 0.4f }, { level[1], 0.0f }, { level[3], 0.0f },
              { pan[0], -0.8f }, { pan[2], 0.8f }, { depth[0], 0.15f }, { depth[2], 0.15f }, { room, 0.3f }, { reverb, 0.18f } } },
        { "Octave Halo", "Octave up and down around the lead, airy and open",
            { { harmony, 1 }, { singers, 10 }, { level[1], 0.5f }, { level[2], 0.0f }, { level[3], 0.7f }, { tone, 0.3f },
              { breath, 0.35f }, { room, 0.7f }, { reverb, 0.5f } } },
        { "Gospel Stage", "Sixth, third and fifth stacked, pushed and lively",
            { { harmony, 7 }, { singers, 16 }, { humanise, 0.55f }, { energy, 0.55f }, { tone, 0.2f }, { room, 0.45f }, { reverb, 0.3f },
              { depth[0], 0.3f }, { depth[1], 0.3f }, { depth[2], 0.3f }, { depth[3], 0.35f } } },
        { "Power Anthem", "Root, fifth and octaves, big and driven",
            { { harmony, 5 }, { singers, 20 }, { energy, 0.5f }, { room, 0.6f }, { reverb, 0.45f }, { sub, 0.25f } } },
        { "Dark Monks", "Low voices only, darker vowels, long hall",
            { { harmony, 1 }, { singers, 12 }, { level[0], 0.0f }, { level[1], 0.0f }, { formant, -0.4f }, { character, 0.9f }, { tone, -0.4f },
              { room, 0.85f }, { reverb, 0.5f }, { sub, 0.4f }, { pan[2], -0.35f }, { pan[3], 0.35f } } },
        { "Angels", "High parts only, bright and breathy, far away",
            { { harmony, 3 }, { singers, 12 }, { level[2], 0.0f }, { level[3], 0.0f }, { formant, 0.3f }, { tone, 0.4f }, { breath, 0.4f },
              { room, 1.0f }, { reverb, 0.7f }, { depth[0], 0.8f }, { depth[1], 0.8f } } },
        { "House Diva Stack", "Triad above the lead, close and polished for house hooks",
            { { harmony, 3 }, { singers, 6 }, { humanise, 0.35f }, { energy, 0.4f }, { tone, 0.25f }, { room, 0.35f }, { reverb, 0.25f },
              { level[3], 0.0f }, { depth[0], 0.2f }, { depth[1], 0.2f }, { depth[2], 0.2f } } },
        { "Sub Choir", "Four parts with an octave-down layer underneath",
            { { singers, 16 }, { sub, 0.6f }, { room, 0.55f }, { reverb, 0.4f } } },
        { "Whisper Choir", "Unison voices with lots of breath, soft and close",
            { { harmony, 0 }, { singers, 10 }, { breath, 0.9f }, { tone, 0.3f }, { energy, 0.0f }, { room, 0.5f }, { reverb, 0.45f }, { choir, -5.0f } } },
        { "MIDI Choir", "Play chords on your keyboard: the choir sings them",
            { { midiChord, 1 }, { singers, 16 }, { room, 0.6f }, { reverb, 0.4f } } },
    };
    return list;
}

juce::AudioProcessorValueTreeState::ParameterLayout SanctumProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto pct = [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + "%"; };
    auto bip = [] (float v, int) { return (v > 0 ? "+" : "") + String (juce::roundToInt (v * 100.0f)); };
    auto f = [&] (const char* id, const String& name, float lo, float hi, float def, std::function<String (float, int)> txt)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), def,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction (std::move (txt))));
    };
    auto db = [&] (const char* id, const String& name, float def)
    {
        f (id, name, -36.0f, 6.0f, def, [] (float v, int) { return v <= -35.9f ? String ("Off") : String (v, 1) + " dB"; });
    };
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { harmony, 1 }, "Harmony", StringArray (sc::kHarmonyNames, sc::kNumHarmonies), 6));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { key, 1 }, "Key", StringArray { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }, 0));
    p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { scale, 1 }, "Scale", StringArray (sc::kScaleNames, sc::kNumScales), 0));
    p.push_back (std::make_unique<AudioParameterBool> (ParameterID { midiChord, 1 }, "MIDI Chords", true));
    p.push_back (std::make_unique<AudioParameterInt> (ParameterID { singers, 1 }, "Singers", 1, sc::kMaxSingers, 12));
    f (humanise,  "Humanise",  0, 1, 0.5f, pct);
    f (spread,    "Spread",    0, 1, 0.7f, pct);
    f (character, "Character", 0, 1, 0.6f, pct);
    f (formant,   "Formant",  -1, 1, 0.0f, bip);
    f (breath,    "Breath",    0, 1, 0.2f, pct);
    f (tone,      "Tone",     -1, 1, 0.0f, bip);
    f (energy,    "Energy",    0, 1, 0.3f, pct);
    f (room,     "Space",     0, 1, 0.5f, pct);
    f (reverb,    "Reverb",    0, 1, 0.35f, pct);
    f (sub,       "Sub Octave", 0, 1, 0.0f, pct);
    f (response,  "Response",  0, 1, 0.3f, pct);
    db (lead, "Lead", 0.0f);
    db (choir, "Choir", -3.0f);
    f (output, "Output", -24.0f, 12.0f, 0.0f, [] (float v, int) { return String (v, 1) + " dB"; });
    const float defPan[] { -0.72f, -0.24f, 0.24f, 0.72f }, defDepth[] { 0.5f, 0.4f, 0.4f, 0.5f };
    const char* names[] { "Soprano", "Alto", "Tenor", "Bass" };
    for (int s = 0; s < sc::kSections; ++s)
    {
        f (level[s], String (names[s]) + " Level", 0, 1, 1.0f, pct);
        f (pan[s],   String (names[s]) + " Position", -1, 1, defPan[s], bip);
        f (depth[s], String (names[s]) + " Depth", 0, 1, defDepth[s], pct);
    }
    return { p.begin(), p.end() };
}

SanctumProcessor::SanctumProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SANCTUM", layout())
{
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) apvts.addParameterListener (r->paramID, &watcher);
    midiLearn.attach (*this);
    midiLearn.setDefaults ({ { 74, room }, { 71, reverb }, { 70, humanise }, { 73, spread },
                             { 75, breath }, { 79, energy }, { 72, tone }, { 80, choir }, { 81, lead }, { 82, sub }, { 83, formant } });
    loadPreset (0);
}

bool SanctumProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void SanctumProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatency());
    monoL.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    monoR.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    const auto s = settings(); leadS = s.leadGain; choirS = s.choirGain; outS = s.outGain;
}

static float dbg (float db) { return db <= -35.9f ? 0.0f : juce::Decibels::decibelsToGain (db); }

sc::Settings SanctumProcessor::settings() const
{
    sc::Settings s;
    s.harmony = juce::roundToInt (pv (harmony)); s.key = juce::roundToInt (pv (key)); s.scale = juce::roundToInt (pv (scale));
    s.midiChord = pv (midiChord) > 0.5f; s.singers = juce::roundToInt (pv (singers));
    s.humanise = pv (humanise); s.spread = pv (spread); s.character = pv (character); s.formant = pv (formant);
    s.breath = pv (breath); s.tone = pv (tone); s.energy = pv (energy); s.space = pv (room); s.reverb = pv (reverb);
    s.sub = pv (sub); s.response = pv (response);
    s.leadGain = dbg (pv (lead)); s.choirGain = dbg (pv (choir)); s.outGain = juce::Decibels::decibelsToGain (pv (output));
    for (int i = 0; i < sc::kSections; ++i) { s.level[(size_t) i] = pv (level[i]); s.pan[(size_t) i] = pv (pan[i]); s.depth[(size_t) i] = pv (depth[i]); }
    s.soloSection = soloSectionForTests.load();
    return s;
}

void SanctumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (midiLearn.handle (m)) continue;
        if (m.isNoteOn()) engine.noteOn (m.getNoteNumber());
        else if (m.isNoteOff()) engine.noteOff (m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
    }
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if ((int) monoL.size() < n) { monoL.resize ((size_t) n); monoR.resize ((size_t) n); }
    auto s = settings();
    // smooth the level controls between blocks
    leadS += (s.leadGain - leadS) * 0.3f; choirS += (s.choirGain - choirS) * 0.3f; outS += (s.outGain - outS) * 0.3f;
    s.leadGain = leadS; s.choirGain = choirS; s.outGain = outS;

    const float* inL = buffer.getReadPointer (0);
    const float* inR = getTotalNumInputChannels() > 1 ? buffer.getReadPointer (1) : inL;
    engine.process (inL, inR, monoL.data(), monoR.data(), n, s);
    buffer.copyFrom (0, 0, monoL.data(), n);
    if (buffer.getNumChannels() > 1) buffer.copyFrom (1, 0, monoR.data(), n);
}

void SanctumProcessor::setParam (const char* id, float realValue)
{
    if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

void SanctumProcessor::loadPreset (int index)
{
    const auto& list = choirPresets();
    loading = true;
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) r->setValueNotifyingHost (r->getDefaultValue());
    if (juce::isPositiveAndBelow (index, (int) list.size()))
    {
        for (const auto& [id, v] : list[(size_t) index].values)
        {
            jassert (apvts.getParameter (id) != nullptr);
            setParam (id, v);
        }
        currentPreset = index;
    }
    loading = false;
    presetEdited = false;
}

juce::String SanctumProcessor::presetName() const
{
    const auto& list = choirPresets();
    const int i = currentPreset.load();
    juce::String n = juce::isPositiveAndBelow (i, (int) list.size()) ? list[(size_t) i].name : juce::String ("Init");
    return presetEdited ? n + " *" : n;
}

void SanctumProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    state.setProperty ("edited", presetEdited.load(), nullptr);
    state.removeChild (state.getChildWithName ("MIDIMAP"), nullptr);
    state.appendChild (midiLearn.toTree(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SanctumProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            loading = true;
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            for (auto* prm : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    auto c = tree.getChildWithProperty ("id", r->paramID);
                    if (c.isValid() && c.hasProperty ("value")) r->setValueNotifyingHost (r->convertTo0to1 ((float) c.getProperty ("value")));
                }
            currentPreset = (int) tree.getProperty ("preset", 0);
            presetEdited = (bool) tree.getProperty ("edited", false);
            midiLearn.fromTree (tree.getChildWithName ("MIDIMAP"));
            loading = false;
        }
}

juce::AudioProcessorEditor* SanctumProcessor::createEditor() { return new SanctumEditor (*this); }

#ifndef SANCTUM_NO_FACTORY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SanctumProcessor(); }
#endif
