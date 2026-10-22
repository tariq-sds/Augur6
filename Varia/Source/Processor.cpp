#include "Processor.h"
#include "Editor.h"

juce::AudioProcessorValueTreeState::ParameterLayout VariaProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { vap::texture, 1 },    "Texture",    juce::NormalisableRange<float> (0, 1), 0.5f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { vap::release, 1 },    "Release",    juce::NormalisableRange<float> (0, 1), 0.5f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { vap::brightness, 1 }, "Brightness", juce::NormalisableRange<float> (0, 1), 0.6f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { vap::width, 1 },      "Width",      juce::NormalisableRange<float> (0, 1), 0.5f, pct));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { vap::pitch, 1 },      "Pitch",      juce::NormalisableRange<float> (-12, 12, 1), 0.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + juce::String (s) + " st"; })));
    juce::StringArray names;
    for (auto& s : va::styles()) names.add (juce::String (s.name));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { vap::style, 1 }, "Style", names, 1));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { vap::wetOnly, 1 }, "Wet Only", false));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { vap::bypass, 1 }, "Bypass", false));
    juce::StringArray every, lens;
    for (int b : va::everyChoices()) every.add (b == 0 ? juce::String ("STYLE") : juce::String (b) + (b == 1 ? " BAR" : " BARS"));
    for (int st : va::lengthChoices()) lens.add (va::lengthName (st));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { vap::every, 1 }, "Every", every, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { vap::length, 1 }, "Change Length", lens, 0));
    return l;
}

VariaProcessor::VariaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VARIA", createLayout())
{
}

bool VariaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() && (in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono());
}

void VariaProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, (uint32_t) juce::Time::getMillisecondCounter() ^ 0x9E3779B9u);
}

void VariaProcessor::setCurrentProgram (int i)
{
    if (auto* p = apvts.getParameter (vap::style))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, getNumPrograms() - 1, i)));
}

const juce::String VariaProcessor::getProgramName (int i)
{
    return juce::isPositiveAndBelow (i, getNumPrograms()) ? juce::String (va::styles()[(size_t) i].name) : juce::String();
}

void VariaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    const int inCh = getTotalNumInputChannels();
    if (buffer.getNumChannels() < 2) return;
    if (inCh == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    float inPk = 0;
    for (int i = 0; i < n; ++i) inPk = juce::jmax (inPk, std::abs (L[i]), std::abs (R[i]));

    va::Transport tp;
    auto* ph = testPlayHead != nullptr ? testPlayHead : getPlayHead();
    if (ph != nullptr)
        if (auto pos = ph->getPosition())
        {
            tp.playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) tp.bpm = *b;
            if (auto q = pos->getPpqPosition()) tp.ppq = *q; else tp.playing = false;
            if (auto ts = pos->getTimeSignature()) { tp.num = ts->numerator; tp.den = ts->denominator; }
        }

    va::Settings s;
    s.texture = pv (vap::texture);
    s.release = pv (vap::release);
    s.brightness = pv (vap::brightness);
    s.width = pv (vap::width);
    s.pitch = pv (vap::pitch);
    s.style = (int) pv (vap::style);
    s.wetOnly = pv (vap::wetOnly) > 0.5f;
    s.bypass = pv (vap::bypass) > 0.5f;
    s.everyBars = va::everyChoices()[(size_t) juce::jlimit (0, (int) va::everyChoices().size() - 1, (int) pv (vap::every))];
    s.eventSteps = va::lengthChoices()[(size_t) juce::jlimit (0, (int) va::lengthChoices().size() - 1, (int) pv (vap::length))];
    engine.process (L, R, n, s, tp);

    float outPk = 0;
    for (int i = 0; i < n; ++i) outPk = juce::jmax (outPk, std::abs (L[i]), std::abs (R[i]));
    inLevel = juce::jmax (inPk, inLevel.load() * 0.9f);
    outLevel = juce::jmax (outPk, outLevel.load() * 0.9f);
}

void VariaProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void VariaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            for (auto* p : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    auto child = tree.getChildWithProperty ("id", r->paramID);
                    if (child.isValid() && child.hasProperty ("value"))
                        r->setValueNotifyingHost (r->convertTo0to1 ((float) child.getProperty ("value")));
                }
        }
}

juce::AudioProcessorEditor* VariaProcessor::createEditor() { return new VariaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VariaProcessor(); }
