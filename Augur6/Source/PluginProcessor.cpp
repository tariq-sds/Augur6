#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using API = juce::AudioParameterInt;

namespace
{
enum class Fmt { Plain, Pct, BiPct, Hz, Sec, Semi, Cents, dB };

juce::String formatValue (float v, Fmt f)
{
    switch (f)
    {
        case Fmt::Pct:   return juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case Fmt::BiPct: return (v > 0.004f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + "%";
        case Fmt::Hz:    return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " kHz"
                                             : juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz";
        case Fmt::Sec:   return v >= 1.0f ? juce::String (v, 2) + " s" : juce::String (juce::roundToInt (v * 1000.0f)) + " ms";
        case Fmt::Semi:  { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + juce::String (s) + " st"; }
        case Fmt::Cents: { const int c = juce::roundToInt (v); return (c > 0 ? "+" : "") + juce::String (c) + " ct"; }
        case Fmt::dB:    return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB";
        case Fmt::Plain: default: return juce::String (v, 2);
    }
}

void addFloat (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id, const char* name,
               float lo, float hi, float def, Fmt fmt, float skewCentre = -1.0f, float step = 0.0f)
{
    juce::NormalisableRange<float> range (lo, hi, step);
    if (skewCentre > 0.0f)
        range.setSkewForCentre (skewCentre);

    layout.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([fmt] (float v, int) { return formatValue (v, fmt); })));
}

void addBool (juce::AudioProcessorValueTreeState::ParameterLayout& layout, const char* id, const char* name, bool def)
{
    layout.add (std::make_unique<APB> (juce::ParameterID { id, 1 }, name, def));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout Augur6Processor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    addFloat (l, pid::osc1Freq,  "Osc 1 Frequency", -24.0f, 24.0f, 0.0f, Fmt::Semi, -1.0f, 1.0f);
    addFloat (l, pid::osc1Shape, "Osc 1 Shape",     0.0f, 1.0f, 0.5f, Fmt::Pct);
    addFloat (l, pid::osc1PW,    "Osc 1 Pulse Width", 0.05f, 0.95f, 0.5f, Fmt::Pct);
    addFloat (l, pid::osc1Sub,   "Osc 1 Sub Level", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addBool  (l, pid::osc1Sync,  "Osc 1 Sync", false);

    addFloat (l, pid::osc2Freq,  "Osc 2 Frequency", -24.0f, 24.0f, 0.0f, Fmt::Semi, -1.0f, 1.0f);
    addFloat (l, pid::osc2Fine,  "Osc 2 Fine",      -50.0f, 50.0f, 0.0f, Fmt::Cents);
    addFloat (l, pid::osc2Shape, "Osc 2 Shape",     0.0f, 1.0f, 0.5f, Fmt::Pct);
    addFloat (l, pid::osc2PW,    "Osc 2 Pulse Width", 0.05f, 0.95f, 0.5f, Fmt::Pct);
    addBool  (l, pid::osc2Low,   "Osc 2 Low Freq", false);
    addBool  (l, pid::osc2Key,   "Osc 2 Key Track", true);

    addFloat (l, pid::mixOsc1,  "Osc 1 Level", 0.0f, 1.0f, 0.8f, Fmt::Pct);
    addFloat (l, pid::mixOsc2,  "Osc 2 Level", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::mixNoise, "Noise Level", 0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::slop,     "Slop",        0.0f, 1.0f, 0.25f, Fmt::Pct);

    addFloat (l, pid::pmFiltEnv, "Poly Mod Filter Env", -1.0f, 1.0f, 0.0f, Fmt::BiPct);
    addFloat (l, pid::pmOsc2,    "Poly Mod Osc 2",       0.0f, 1.0f, 0.0f, Fmt::Pct, 0.15f);
    addBool  (l, pid::pmFreq1,   "Poly Mod > Freq 1",  false);
    addBool  (l, pid::pmShape1,  "Poly Mod > Shape 1", false);
    addBool  (l, pid::pmPW1,     "Poly Mod > PW 1",    false);
    addBool  (l, pid::pmLPF,     "Poly Mod > LPF",     false);
    addBool  (l, pid::pmHPF,     "Poly Mod > HPF",     false);

    addFloat (l, pid::lfoRate,  "LFO Rate", 0.05f, 30.0f, 4.0f, Fmt::Hz, 3.0f);
    addBool  (l, pid::lfoSync,  "LFO Sync", false);
    l.add (std::make_unique<APC> (juce::ParameterID { pid::lfoSyncRate, 1 }, "LFO Sync Rate", choices::syncRates, 5));
    l.add (std::make_unique<APC> (juce::ParameterID { pid::lfoShape, 1 },    "LFO Shape",     choices::lfoShapes, 0));
    addFloat (l, pid::lfoAmount,   "LFO Amount",     0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::lfoModWheel, "LFO Mod Wheel",  0.0f, 1.0f, 0.3f, Fmt::Pct);
    addBool  (l, pid::lfoFreq1, "LFO > Freq 1", true);
    addBool  (l, pid::lfoFreq2, "LFO > Freq 2", true);
    addBool  (l, pid::lfoPW,    "LFO > PW",     false);
    addBool  (l, pid::lfoLPF,   "LFO > LPF",    false);
    addBool  (l, pid::lfoHPF,   "LFO > HPF",    false);
    addBool  (l, pid::lfoAmp,   "LFO > Amp",    false);

    addFloat (l, pid::lpfCutoff, "LPF Cutoff",     20.0f, 20000.0f, 8000.0f, Fmt::Hz, 900.0f);
    addFloat (l, pid::lpfRes,    "LPF Resonance",  0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::lpfEnv,    "LPF Env Amount", -1.0f, 1.0f, 0.0f, Fmt::BiPct);
    addFloat (l, pid::lpfVel,    "LPF Velocity",   0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::lpfKey,    "LPF Key Amount", 0.0f, 1.0f, 0.5f, Fmt::Pct);
    addFloat (l, pid::lpfAT,     "LPF Aftertouch", 0.0f, 1.0f, 0.0f, Fmt::Pct);

    addFloat (l, pid::hpfCutoff, "HPF Cutoff",     20.0f, 12000.0f, 20.0f, Fmt::Hz, 400.0f);
    addFloat (l, pid::hpfRes,    "HPF Resonance",  0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::hpfEnv,    "HPF Env Amount", -1.0f, 1.0f, 0.0f, Fmt::BiPct);
    addFloat (l, pid::hpfVel,    "HPF Velocity",   0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::hpfKey,    "HPF Key Amount", 0.0f, 1.0f, 0.0f, Fmt::Pct);

    addFloat (l, pid::fA, "Filter Attack",  0.001f, 10.0f, 0.005f, Fmt::Sec, 0.5f);
    addFloat (l, pid::fD, "Filter Decay",   0.001f, 10.0f, 0.4f,   Fmt::Sec, 0.5f);
    addFloat (l, pid::fS, "Filter Sustain", 0.0f, 1.0f, 0.5f,      Fmt::Pct);
    addFloat (l, pid::fR, "Filter Release", 0.001f, 10.0f, 0.3f,   Fmt::Sec, 0.5f);

    addFloat (l, pid::aA,   "Amp Attack",   0.001f, 10.0f, 0.003f, Fmt::Sec, 0.5f);
    addFloat (l, pid::aD,   "Amp Decay",    0.001f, 10.0f, 0.3f,   Fmt::Sec, 0.5f);
    addFloat (l, pid::aS,   "Amp Sustain",  0.0f, 1.0f, 1.0f,      Fmt::Pct);
    addFloat (l, pid::aR,   "Amp Release",  0.001f, 10.0f, 0.25f,  Fmt::Sec, 0.5f);
    addFloat (l, pid::aVel, "Amp Velocity", 0.0f, 1.0f, 0.3f,      Fmt::Pct);

    addFloat (l, pid::glide,     "Glide",         0.0f, 2.0f, 0.0f, Fmt::Sec, 0.2f);
    addBool  (l, pid::unison,    "Unison",        false);
    l.add (std::make_unique<API> (juce::ParameterID { pid::uniVoices, 1 }, "Unison Voices", 2, 6, 4));
    addFloat (l, pid::uniDetune, "Unison Detune", 0.0f, 1.0f, 0.25f, Fmt::Pct);
    addFloat (l, pid::panSpread, "Pan Spread",    0.0f, 1.0f, 0.3f,  Fmt::Pct);
    addFloat (l, pid::bendRange, "Bend Range",    0.0f, 24.0f, 2.0f, Fmt::Semi, -1.0f, 1.0f);
    addFloat (l, pid::volume,    "Volume",        -30.0f, 6.0f, 0.0f, Fmt::dB);

    addFloat (l, pid::drive,    "Distortion",     0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::choMix,   "Chorus Mix",     0.0f, 1.0f, 0.0f, Fmt::Pct);
    addFloat (l, pid::choRate,  "Chorus Rate",    0.05f, 8.0f, 0.6f, Fmt::Hz, 1.0f);
    addFloat (l, pid::choDepth, "Chorus Depth",   0.0f, 1.0f, 0.5f, Fmt::Pct);
    addFloat (l, pid::dlyTime,  "Delay Time",     0.01f, 2.0f, 0.375f, Fmt::Sec, 0.4f);
    addBool  (l, pid::dlySync,  "Delay Sync",     true);
    l.add (std::make_unique<APC> (juce::ParameterID { pid::dlySyncRate, 1 }, "Delay Sync Rate", choices::syncRates, 7));
    addFloat (l, pid::dlyFb,    "Delay Feedback", 0.0f, 0.95f, 0.35f, Fmt::Pct);
    addFloat (l, pid::dlyMix,   "Delay Mix",      0.0f, 1.0f, 0.0f, Fmt::Pct);
    addBool  (l, pid::dlyPing,  "Delay Ping-Pong", true);
    addFloat (l, pid::revSize,  "Reverb Size",    0.0f, 1.0f, 0.6f, Fmt::Pct);
    addFloat (l, pid::revDamp,  "Reverb Damping", 0.0f, 1.0f, 0.4f, Fmt::Pct);
    addFloat (l, pid::revMix,   "Reverb Mix",     0.0f, 1.0f, 0.0f, Fmt::Pct);

    return l;
}

//==============================================================================
Augur6Processor::Augur6Processor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AUGUR6", createLayout())
{
    for (auto* param : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
        {
            raw[withId->paramID.toStdString()] = apvts.getRawParameterValue (withId->paramID);
            apvts.addParameterListener (withId->paramID, this);
        }

    loadPreset (0);
}

Augur6Processor::~Augur6Processor()
{
    for (auto& kv : raw)
        apvts.removeParameterListener (kv.first, this);
}

float Augur6Processor::p (const char* id) const
{
    auto it = raw.find (id);
    jassert (it != raw.end());
    return it != raw.end() ? it->second->load() : 0.0f;
}

void Augur6Processor::parameterChanged (const juce::String&, float)
{
    if (! loadingPreset)
        presetEdited = true;
}

bool Augur6Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void Augur6Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing ((size_t) samplesPerBlock);
    setLatencySamples ((int) oversampler->getLatencyInSamples());

    const float osRate = (float) (sampleRate * 2.0);
    engine.prepare (osRate);
    lfoBuf.assign ((size_t) samplesPerBlock * 2 + 16, 0.0f);
    fx.prepare (sampleRate, samplesPerBlock);
    volSmooth = juce::Decibels::decibelsToGain (p (pid::volume));
}

void Augur6Processor::fillSettings (augur::Settings& s, augur::FxSettings& f, double bpm)
{
    s.osc1Freq = p (pid::osc1Freq);   s.osc1Shape = p (pid::osc1Shape); s.osc1PW = p (pid::osc1PW);
    s.osc1Sub = p (pid::osc1Sub);     s.osc1Sync = b (pid::osc1Sync);
    s.osc2Freq = p (pid::osc2Freq);   s.osc2Fine = p (pid::osc2Fine);   s.osc2Shape = p (pid::osc2Shape);
    s.osc2PW = p (pid::osc2PW);       s.osc2Low = b (pid::osc2Low);     s.osc2Key = b (pid::osc2Key);
    s.mixOsc1 = p (pid::mixOsc1);     s.mixOsc2 = p (pid::mixOsc2);     s.mixNoise = p (pid::mixNoise);
    s.slop = p (pid::slop);

    // knob response squared for finer control at low poly-mod amounts
    const float pmEnv = p (pid::pmFiltEnv);
    s.pmFiltEnv = pmEnv * std::fabs (pmEnv);
    s.pmOsc2 = p (pid::pmOsc2);
    s.pmFreq1 = b (pid::pmFreq1); s.pmShape1 = b (pid::pmShape1); s.pmPW1 = b (pid::pmPW1);
    s.pmLPF = b (pid::pmLPF);     s.pmHPF = b (pid::pmHPF);

    s.lfoDepth = juce::jlimit (0.0f, 1.0f, p (pid::lfoAmount) + modWheel * p (pid::lfoModWheel));
    s.lfoFreq1 = b (pid::lfoFreq1); s.lfoFreq2 = b (pid::lfoFreq2); s.lfoPW = b (pid::lfoPW);
    s.lfoLPF = b (pid::lfoLPF);     s.lfoHPF = b (pid::lfoHPF);     s.lfoAmp = b (pid::lfoAmp);

    s.lpfCutoff = p (pid::lpfCutoff); s.lpfRes = p (pid::lpfRes); s.lpfEnv = p (pid::lpfEnv);
    s.lpfVel = p (pid::lpfVel);       s.lpfKey = p (pid::lpfKey); s.lpfAT = p (pid::lpfAT);
    s.hpfCutoff = p (pid::hpfCutoff); s.hpfRes = p (pid::hpfRes); s.hpfEnv = p (pid::hpfEnv);
    s.hpfVel = p (pid::hpfVel);       s.hpfKey = p (pid::hpfKey);

    s.fA = p (pid::fA); s.fD = p (pid::fD); s.fS = p (pid::fS); s.fR = p (pid::fR);
    s.aA = p (pid::aA); s.aD = p (pid::aD); s.aS = p (pid::aS); s.aR = p (pid::aR); s.aVel = p (pid::aVel);

    s.glide = p (pid::glide);
    s.unison = b (pid::unison);
    s.uniVoices = juce::roundToInt (p (pid::uniVoices));
    s.uniDetune = p (pid::uniDetune);
    s.panSpread = p (pid::panSpread);
    s.bendSemis = bend * p (pid::bendRange);
    s.aftertouch = aftertouch;

    f.drive = p (pid::drive);
    f.choMix = p (pid::choMix); f.choRate = p (pid::choRate); f.choDepth = p (pid::choDepth);
    const double beatSec = 60.0 / juce::jlimit (20.0, 400.0, bpm);
    f.dlyTimeSec = b (pid::dlySync)
                       ? (float) juce::jmin (2.0, choices::syncBeats ((int) p (pid::dlySyncRate)) * beatSec)
                       : p (pid::dlyTime);
    f.dlyFb = p (pid::dlyFb); f.dlyMix = p (pid::dlyMix); f.dlyPing = b (pid::dlyPing);
    f.revSize = p (pid::revSize); f.revDamp = p (pid::revDamp); f.revMix = p (pid::revMix);
}

void Augur6Processor::handleMidi (const juce::MidiMessage& m, const augur::Settings& s)
{
    if (m.isNoteOn())
        engine.noteOn (m.getNoteNumber(), m.getFloatVelocity(), s);
    else if (m.isNoteOff())
        engine.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        bend = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8191.0f);
    else if (m.isController())
    {
        const int cc = m.getControllerNumber();
        if (cc == 1)        modWheel = (float) m.getControllerValue() / 127.0f;
        else if (cc == 64)  engine.setSustain (m.getControllerValue() >= 64);
        else if (cc == 123 || cc == 120) engine.allNotesOff();
    }
    else if (m.isChannelPressure())
        aftertouch = (float) m.getChannelPressureValue() / 127.0f;
    else if (m.isAftertouch())
        aftertouch = juce::jmax (aftertouch * 0.5f, (float) m.getAfterTouchValue() / 127.0f);
    else if (m.isAllNotesOff() || m.isAllSoundOff())
        engine.allNotesOff();
}

void Augur6Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || oversampler == nullptr)
        return;

    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hb = pos->getBpm())
                bpm = *hb;

    augur::Settings s;
    augur::FxSettings f;
    fillSettings (s, f, bpm);
    engine.setUnison (s.unison, s.uniVoices);

    // LFO rate (free or tempo-synced)
    float lfoHz = p (pid::lfoRate);
    if (b (pid::lfoSync))
        lfoHz = (float) (bpm / 60.0 / choices::syncBeats ((int) p (pid::lfoSyncRate)));

    juce::dsp::AudioBlock<float> block (buffer);
    auto os = oversampler->processSamplesUp (block);
    os.clear();

    const int osN = (int) os.getNumSamples();
    if ((int) lfoBuf.size() < osN)
        lfoBuf.resize ((size_t) osN);

    const float osRate = (float) (hostSampleRate * 2.0);
    lfo.render (lfoBuf.data(), osN, lfoHz, (int) p (pid::lfoShape), osRate);

    float* L = os.getChannelPointer (0);
    float* R = os.getChannelPointer (1);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, osN, meta.samplePosition * 2);
        if (at > pos)
        {
            s.bendSemis = bend * p (pid::bendRange);
            s.aftertouch = aftertouch;
            engine.render (L + pos, R + pos, at - pos, s, lfoBuf.data() + pos);
            pos = at;
        }
        handleMidi (meta.getMessage(), s);
        s.lfoDepth = juce::jlimit (0.0f, 1.0f, p (pid::lfoAmount) + modWheel * p (pid::lfoModWheel));
    }
    if (pos < osN)
    {
        s.bendSemis = bend * p (pid::bendRange);
        s.aftertouch = aftertouch;
        engine.render (L + pos, R + pos, osN - pos, s, lfoBuf.data() + pos);
    }

    oversampler->processSamplesDown (block);

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (1);
    fx.process (outL, outR, numSamples, f);

    const float target = juce::Decibels::decibelsToGain (p (pid::volume));
    for (int i = 0; i < numSamples; ++i)
    {
        volSmooth += (target - volSmooth) * 0.002f;
        outL[i] *= volSmooth;
        outR[i] *= volSmooth;
    }

    voicesPlaying = engine.activeVoices();
}

//==============================================================================
int Augur6Processor::getNumPrograms()
{
    return (int) factoryPresets().size();
}

void Augur6Processor::setCurrentProgram (int index)
{
    loadPreset (index);
}

const juce::String Augur6Processor::getProgramName (int index)
{
    const auto& list = factoryPresets();
    if (juce::isPositiveAndBelow (index, (int) list.size()))
        return list[(size_t) index].category + " - " + list[(size_t) index].name;
    return {};
}

void Augur6Processor::loadPreset (int index)
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
                jassertfalse; // typo in a preset
        currentPreset = index;
    }
    else
    {
        currentPreset = -1;   // Init
    }

    loadingPreset = false;
    presetEdited = false;
}

juce::String Augur6Processor::getPresetDisplayName() const
{
    const auto& list = factoryPresets();
    const int idx = currentPreset.load();
    juce::String n = juce::isPositiveAndBelow (idx, (int) list.size()) ? list[(size_t) idx].name : juce::String ("Init");
    if (presetEdited) n << " *";
    return n;
}

void Augur6Processor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("preset", currentPreset.load(), nullptr);
    state.setProperty ("edited", presetEdited.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void Augur6Processor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            loadingPreset = true;
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);

            // Push every stored value through explicitly so switch parameters land exactly on 0 or 1
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

juce::AudioProcessorEditor* Augur6Processor::createEditor()
{
    return new Augur6Editor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Augur6Processor();
}
