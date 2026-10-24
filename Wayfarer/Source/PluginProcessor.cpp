#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;

namespace
{
enum class Fmt { Plain, Pct, BiPct, Hz, Sec, Semi, Oct, Cents, dB };

juce::String formatValue (float v, Fmt f)
{
    switch (f)
    {
        case Fmt::Pct:   return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case Fmt::BiPct: return (v > 0.004f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case Fmt::Hz:    return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                             : juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
        case Fmt::Sec:   return v >= 1.0f ? juce::String (v, 2) + " s" : juce::String (juce::roundToInt (v * 1000.0f)) + " ms";
        case Fmt::Semi:  return (v > 0.004f ? "+" : "") + juce::String (v, 2) + " st";
        case Fmt::Oct:   return (v > 0.004f ? "+" : "") + juce::String (v, 2) + " oct";
        case Fmt::Cents: { const int c = juce::roundToInt (v); return (c > 0 ? "+" : "") + juce::String (c) + " ct"; }
        case Fmt::dB:    return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
        case Fmt::Plain: default: return juce::String (v, 2);
    }
}

void addFloat (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id, const char* name,
               float lo, float hi, float def, Fmt fmt, float skewCentre = -1.0f)
{
    juce::NormalisableRange<float> range (lo, hi);
    if (skewCentre > 0.0f) range.setSkewForCentre (skewCentre);
    layout.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([fmt] (float v, int) { return formatValue (v, fmt); })));
}
void addBool (juce::AudioProcessorValueTreeState::ParameterLayout& l, const char* id, const char* name, bool def)
{
    l.add (std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def));
}
void addChoice (juce::AudioProcessorValueTreeState::ParameterLayout& l, const char* id, const char* name, const juce::StringArray& c, int def)
{
    l.add (std::make_unique<APC> (juce::ParameterID { id, 1 }, name, c, def));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout WayfarerProcessor::createLayout()
{
    using namespace choices;
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    addChoice (l, pid::o1Oct, "Osc 1 Octave", octaves, 2);
    addFloat  (l, pid::o1Wave, "Osc 1 Wave", 0.0f, 1.0f, 0.333f, Fmt::Pct);
    addChoice (l, pid::o2Oct, "Osc 2 Octave", octaves, 2);
    addFloat  (l, pid::o2Freq, "Osc 2 Frequency", -7.0f, 7.0f, 0.0f, Fmt::Semi);
    addFloat  (l, pid::o2Wave, "Osc 2 Wave", 0.0f, 1.0f, 0.333f, Fmt::Pct);
    addChoice (l, pid::o3Oct, "Osc 3 Octave", octaves, 2);
    addFloat  (l, pid::o3Freq, "Osc 3 Frequency", -7.0f, 7.0f, 0.0f, Fmt::Semi);
    addFloat  (l, pid::o3Wave, "Osc 3 Wave", 0.0f, 1.0f, 0.333f, Fmt::Pct);
    addBool   (l, pid::hardSync, "1-2 Sync", false);
    addFloat  (l, pid::fm, "3-1 FM", 0.0f, 1.0f, 0.0f, Fmt::Pct, 0.2f);
    addBool   (l, pid::o3KbOff, "Osc 3 Keyboard Off", false);
    addBool   (l, pid::o3Lo, "Osc 3 Low", false);

    addFloat (l, pid::mix1, "Osc 1 Level", 0.0f, 1.0f, 0.85f, Fmt::Pct);
    addFloat (l, pid::mix2, "Osc 2 Level", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::mix3, "Osc 3 Level", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::noise, "Noise Level", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::overload, "Overload", 0.0f, 1.0f, 0.0f, Fmt::Pct);

    addFloat  (l, pid::cutoff, "Cutoff", 20.0f, 20000.0f, 3000.0f, Fmt::Hz, 800.0f);
    addFloat  (l, pid::spacing, "Spacing", -3.0f, 3.0f, 0.0f, Fmt::Oct);
    addFloat  (l, pid::res, "Resonance", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat  (l, pid::kb, "Keyboard Control", 0.0f, 1.0f, 0.5f, Fmt::Pct);
    addChoice (l, pid::fMode, "Filter Mode", fModes, 0);
    addFloat  (l, pid::envAmt, "Env Amount", -1.0f, 1.0f, 0.4f, Fmt::BiPct);
    addFloat  (l, pid::fVel, "Filter Velocity", 0.0f, 1.0f, 0.0f, Fmt::Pct);

    addFloat (l, pid::fA, "Filter Attack", 0.001f, 10.0f, 0.003f, Fmt::Sec, 0.4f);
    addFloat (l, pid::fD, "Filter Decay", 0.002f, 10.0f, 0.5f, Fmt::Sec, 0.5f);
    addFloat (l, pid::fS, "Filter Sustain", 0.0f, 1.0f, 0.35f, Fmt::Pct);
    addFloat (l, pid::fR, "Filter Release", 0.002f, 10.0f, 0.3f, Fmt::Sec, 0.5f);
    addFloat (l, pid::aA, "Amp Attack", 0.001f, 10.0f, 0.002f, Fmt::Sec, 0.4f);
    addFloat (l, pid::aD, "Amp Decay", 0.002f, 10.0f, 0.4f, Fmt::Sec, 0.5f);
    addFloat (l, pid::aS, "Amp Sustain", 0.0f, 1.0f, 1.0f, Fmt::Pct);
    addFloat (l, pid::aR, "Amp Release", 0.002f, 10.0f, 0.15f, Fmt::Sec, 0.5f);
    addFloat (l, pid::aVel, "Amp Velocity", 0.0f, 1.0f, 0.25f, Fmt::Pct);
    addBool  (l, pid::release, "Release", true);
    addChoice (l, pid::gate, "Envelope Gate", gates, 0);

    addFloat  (l, pid::lfoRate, "LFO Rate", 0.05f, 50.0f, 5.0f, Fmt::Hz, 4.0f);
    addChoice (l, pid::lfoSync, "LFO Sync", lfoSyncs, 0);
    addChoice (l, pid::lfoDiv, "LFO Tempo Rate", divs, 8);
    addChoice (l, pid::m1Src, "Mod 1 Source", sources, sTri);
    addChoice (l, pid::m1Shape, "Mod 1 Shaping", shapes, shOn);
    addChoice (l, pid::m1Dest, "Mod 1 Destination", dests, dPitch);
    addFloat  (l, pid::m1Amt, "Mod 1 Amount", 0.0f, 1.0f, 0.25f, Fmt::Pct);
    addChoice (l, pid::m2Src, "Mod 2 Source", sources, sTri);
    addChoice (l, pid::m2Shape, "Mod 2 Shaping", shapes, shOn);
    addChoice (l, pid::m2Dest, "Mod 2 Destination", dests, dFilter);
    addFloat  (l, pid::m2Amt, "Mod 2 Amount", 0.0f, 1.0f, 0.0f, Fmt::Pct);

    addFloat  (l, pid::padX, "Pad X", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat  (l, pid::padY, "Pad Y", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addChoice (l, pid::padXDest, "Pad X Destination", dests, dPitch);
    addFloat  (l, pid::padXAmt, "Pad X Amount", -1.0f, 1.0f, 0.0f, Fmt::BiPct);
    addChoice (l, pid::padYDest, "Pad Y Destination", dests, dFilter);
    addFloat  (l, pid::padYAmt, "Pad Y Amount", -1.0f, 1.0f, 0.5f, Fmt::BiPct);
    addBool   (l, pid::padGate, "Pad Gate", false);

    addBool   (l, pid::glideOn, "Glide On", false);
    addFloat  (l, pid::glide, "Glide Rate", 0.0f, 3.0f, 0.12f, Fmt::Sec, 0.3f);
    addBool   (l, pid::legato, "Legato", true);
    addChoice (l, pid::priority, "Note Priority", priorities, 2);
    addFloat  (l, pid::bend, "Bend Range", 0.0f, 12.0f, 2.0f, Fmt::Semi);
    addFloat  (l, pid::tune, "Fine Tune", -100.0f, 100.0f, 0.0f, Fmt::Cents);

    addFloat  (l, pid::drift, "Drift", 0.0f, 1.0f, 0.25f, Fmt::Pct);
    addFloat  (l, pid::volume, "Volume", -30.0f, 6.0f, 0.0f, Fmt::dB);
    addFloat  (l, pid::choMix, "Chorus", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat  (l, pid::dlyMix, "Delay", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addChoice (l, pid::dlyDiv, "Delay Time", divs, 7);
    addFloat  (l, pid::dlyFb, "Delay Feedback", 0.0f, 0.9f, 0.35f, Fmt::Pct);
    addFloat  (l, pid::revMix, "Reverb", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat  (l, pid::revSize, "Reverb Size", 0.0f, 1.0f, 0.6f, Fmt::Pct);
    return l;
}

//==============================================================================
WayfarerProcessor::WayfarerProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "WAYFARER", createLayout())
{
    for (auto* param : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
        {
            raw[withId->paramID.toStdString()] = apvts.getRawParameterValue (withId->paramID);
            apvts.addParameterListener (withId->paramID, this);
        }
    loadPreset (0);
}

WayfarerProcessor::~WayfarerProcessor()
{
    for (auto& kv : raw) apvts.removeParameterListener (kv.first, this);
}

float WayfarerProcessor::p (const char* id) const
{
    auto it = raw.find (id);
    jassert (it != raw.end());
    return it != raw.end() ? it->second->load() : 0.0f;
}

void WayfarerProcessor::parameterChanged (const juce::String& id, float)
{
    if (! loadingPreset && id != pid::padX && id != pid::padY) presetEdited = true;
}

bool WayfarerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void WayfarerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples ((int) oversampler->getLatencyInSamples());
    engine.prepare ((float) (sampleRate * 2.0));
    fx.prepare (sampleRate, samplesPerBlock);
    volSmooth = juce::Decibels::decibelsToGain (p (pid::volume));
}

void WayfarerProcessor::fillSettings (wf::Settings& s, wf::FxSettings& f, double bpm)
{
    s.o1Oct = c (pid::o1Oct); s.o2Oct = c (pid::o2Oct); s.o3Oct = c (pid::o3Oct);
    s.o1Wave = p (pid::o1Wave); s.o2Wave = p (pid::o2Wave); s.o3Wave = p (pid::o3Wave);
    s.o2Freq = p (pid::o2Freq); s.o3Freq = p (pid::o3Freq);
    s.sync = b (pid::hardSync); s.fm = p (pid::fm); s.o3KbOff = b (pid::o3KbOff); s.o3Lo = b (pid::o3Lo);
    s.mix1 = p (pid::mix1); s.mix2 = p (pid::mix2); s.mix3 = p (pid::mix3); s.noise = p (pid::noise); s.overload = p (pid::overload);
    s.cutoff = p (pid::cutoff); s.spacing = p (pid::spacing); s.res = p (pid::res); s.kb = p (pid::kb);
    s.fMode = c (pid::fMode); s.envAmt = p (pid::envAmt); s.fVel = p (pid::fVel);
    s.fA = p (pid::fA); s.fD = p (pid::fD); s.fS = p (pid::fS); s.fR = p (pid::fR);
    s.aA = p (pid::aA); s.aD = p (pid::aD); s.aS = p (pid::aS); s.aR = p (pid::aR); s.aVel = p (pid::aVel);
    s.release = b (pid::release); s.gate = c (pid::gate);

    s.lfoSync = c (pid::lfoSync);
    s.lfoHz = s.lfoSync == 2 ? (float) (bpm / 60.0 / choices::divBeats (c (pid::lfoDiv))) : p (pid::lfoRate);
    // amounts: squared so the first part of each knob gives gentle vibrato / wobble
    auto sq = [] (float v) { return v * std::fabs (v); };
    s.bus[0] = { c (pid::m1Src), c (pid::m1Shape), c (pid::m1Dest), sq (p (pid::m1Amt)) * modWheel };
    s.bus[1] = { c (pid::m2Src), c (pid::m2Shape), c (pid::m2Dest), sq (p (pid::m2Amt)) };
    s.padX = p (pid::padX); s.padY = p (pid::padY);
    s.padXDest = c (pid::padXDest); s.padYDest = c (pid::padYDest);
    s.padXAmt = sq (p (pid::padXAmt)); s.padYAmt = sq (p (pid::padYAmt));
    s.padGate = b (pid::padGate); s.padTouch = padTouched.load();

    s.glideOn = b (pid::glideOn); s.glide = p (pid::glide); s.legato = b (pid::legato); s.priority = c (pid::priority);
    s.bendSemis = bendNorm * p (pid::bend);
    s.tune = p (pid::tune) * 0.01f;
    s.drift = p (pid::drift);
    s.pressure = pressure;

    f.choMix = p (pid::choMix); f.choRate = 0.5f; f.choDepth = 0.45f;
    f.dlyMix = p (pid::dlyMix);
    f.dlyTimeSec = (float) juce::jmin (2.0, choices::divBeats (c (pid::dlyDiv)) * 60.0 / juce::jlimit (20.0, 400.0, bpm));
    f.dlyFb = p (pid::dlyFb); f.dlyPing = true;
    f.revMix = p (pid::revMix); f.revSize = p (pid::revSize); f.revDamp = 0.45f;
}

void WayfarerProcessor::handleMidi (const juce::MidiMessage& m, const wf::Settings& s)
{
    if (m.isNoteOn())        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), s);
    else if (m.isNoteOff())  engine.noteOff (m.getNoteNumber(), s);
    else if (m.isPitchWheel()) bendNorm = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8191.0f);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc == 1)        modWheel = (float) m.getControllerValue() / 127.0f;
        else if (cc == 64)  engine.setSustain (m.getControllerValue() >= 64);
        else if (cc == 123 || cc == 120) engine.allNotesOff();
    }
    else if (m.isChannelPressure()) pressure = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch())      pressure = (float) m.getAfterTouchValue() / 127.0f;
    else if (m.isAllNotesOff() || m.isAllSoundOff()) engine.allNotesOff();
}

void WayfarerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || oversampler == nullptr) return;

    double bpm = 120.0, ppq = -1.0; bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto hb = pos->getBpm()) bpm = *hb;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }

    wf::Settings s;
    wf::FxSettings f;
    fillSettings (s, f, bpm);
    if (s.lfoSync == 2 && playing && ppq >= 0)
    {
        const double div = choices::divBeats (c (pid::lfoDiv));
        s.lfoPhaseAt = ppq / div - std::floor (ppq / div);
    }

    juce::dsp::AudioBlock<float> block (buffer);
    auto os = oversampler->processSamplesUp (block);
    os.clear();
    const int osN = (int) os.getNumSamples();
    float* L = os.getChannelPointer (0);
    float* R = os.getChannelPointer (1);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, osN, meta.samplePosition * 2);
        if (at > pos) { engine.render (L + pos, R + pos, at - pos, s); s.lfoPhaseAt = -1; pos = at; }
        handleMidi (meta.getMessage(), s);
        s.bendSemis = bendNorm * p (pid::bend);
        s.pressure = pressure;
        s.bus[0].amt = p (pid::m1Amt) * p (pid::m1Amt) * modWheel;
    }
    if (pos < osN) engine.render (L + pos, R + pos, osN - pos, s);

    oversampler->processSamplesDown (block);
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);
    fx.process (outL, outR, numSamples, f);

    const float target = juce::Decibels::decibelsToGain (p (pid::volume));
    float pk = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        volSmooth += (target - volSmooth) * 0.002f;
        outL[i] *= volSmooth; outR[i] *= volSmooth;
        pk = juce::jmax (pk, std::abs (outL[i]), std::abs (outR[i]));
    }
    outLevel = juce::jmax (pk, outLevel.load() * 0.85f);
    sounding = engine.active();
    lastPitch = engine.currentPitch();
}

//==============================================================================
int WayfarerProcessor::getNumPrograms() { return (int) factoryPresets().size(); }
void WayfarerProcessor::setCurrentProgram (int index) { loadPreset (index); }

const juce::String WayfarerProcessor::getProgramName (int index)
{
    const auto& list = factoryPresets();
    if (juce::isPositiveAndBelow (index, (int) list.size()))
        return list[(size_t) index].category + " - " + list[(size_t) index].name;
    return {};
}

void WayfarerProcessor::loadPreset (int index)
{
    const auto& list = factoryPresets();
    loadingPreset = true;
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    if (juce::isPositiveAndBelow (index, (int) list.size()))
    {
        for (const auto& [id, value] : list[(size_t) index].values)
            if (auto* param = apvts.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
            else
                jassertfalse;
        currentPreset = index;
    }
    else currentPreset = -1;
    loadingPreset = false;
    presetEdited = false;
}

juce::String WayfarerProcessor::getPresetDisplayName() const
{
    const auto& list = factoryPresets();
    const int idx = currentPreset.load();
    juce::String n = juce::isPositiveAndBelow (idx, (int) list.size()) ? list[(size_t) idx].name : juce::String ("Init");
    if (presetEdited) n << " *";
    return n;
}

void WayfarerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    state.setProperty ("edited", presetEdited.load(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void WayfarerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            loadingPreset = true;
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            for (auto* param : getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
                {
                    auto child = tree.getChildWithProperty ("id", ranged->paramID);
                    if (child.isValid() && child.hasProperty ("value"))
                        ranged->setValueNotifyingHost (ranged->convertTo0to1 ((float) child.getProperty ("value")));
                }
            currentPreset = (int) tree.getProperty ("preset", 0);
            presetEdited = (bool) tree.getProperty ("edited", false);
            loadingPreset = false;
        }
}

juce::AudioProcessorEditor* WayfarerProcessor::createEditor() { return new WayfarerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WayfarerProcessor(); }
