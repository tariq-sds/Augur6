#include "Room.h"

namespace
{
inline float coefOf (float sec, double sr) { return 1.0f - std::exp (-1.0f / (juce::jmax (1e-4f, sec) * (float) sr)); }
inline float toDb (float ms) { return 10.0f * std::log10 (ms + 1e-12f); }
}

RoomProcessor::RoomProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Vocal Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CLARIONROOM", [] {
          juce::AudioProcessorValueTreeState::ParameterLayout l;
          auto fmt = [] (std::function<juce::String (float)> fn) { return juce::AudioParameterFloatAttributes().withStringFromValueFunction ([fn] (float v, int) { return fn (v); }); };
          l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { rp::depth, 1 }, "Depth", juce::NormalisableRange<float> (0, 12), 5.0f,
                     fmt ([] (float v) { return "-" + juce::String (v, 1) + " dB"; })));
          l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { rp::focus, 1 }, "Focus", juce::NormalisableRange<float> (0.7f, 1.4f), 1.0f,
                     fmt ([] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; })));
          l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { rp::width, 1 }, "Width", juce::NormalisableRange<float> (0.5f, 3.0f), 1.2f,
                     fmt ([] (float v) { return "Q " + juce::String (v, 2); })));
          juce::NormalisableRange<float> rel (30, 600); rel.setSkewForCentre (150);
          l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { rp::release, 1 }, "Release", rel, 150.0f,
                     fmt ([] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; })));
          l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { rp::midOnly, 1 }, "Mid Only", true));
          l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { rp::output, 1 }, "Output", juce::NormalisableRange<float> (-12, 12), 0.0f,
                     fmt ([] (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; })));
          return l; }())
{
}

bool RoomProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void RoomProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    scMono.assign ((size_t) samplesPerBlock + 16, 0.0f);
    for (int b = 0; b < kBands; ++b)
    {
        vDet[b].reset(); mDet[b].reset(); eq[b][0].reset(); eq[b][1].reset();
        vEnv[b] = vMs[b] = mMs[b] = carve[b] = 0; vRef[b] = 1e-5f;
    }
}

void RoomProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if ((int) scMono.size() < n) scMono.resize ((size_t) n);

    bool haveSc = false;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            haveSc = true;
            for (int i = 0; i < n; ++i)
            {
                float s = 0;
                for (int c = 0; c < sc.getNumChannels(); ++c) s += sc.getSample (c, i);
                scMono[(size_t) i] = s / (float) sc.getNumChannels();
            }
        }
    }
    if (! haveSc) std::fill (scMono.begin(), scMono.begin() + n, 0.0f);

    const float depth = pv (rp::depth), focus = pv (rp::focus), q = pv (rp::width);
    const bool midOnly = pv (rp::midOnly) > 0.5f;
    const float outG = juce::Decibels::decibelsToGain (pv (rp::output));
    const float atk = coefOf (0.003f, sr), rel = coefOf (pv (rp::release) * 0.001f, sr);
    const float cMs = coefOf (0.05f, sr), cAtk = coefOf (0.005f, sr);
    const float refDecay = std::exp (-1.0f / (4.0f * (float) sr));
    float g[kBands];
    for (int b = 0; b < kBands; ++b) g[b] = cl::gOf (kCentres[b] * focus, (float) sr);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    float peak[kBands] {};
    bool anyVocal = false;

    for (int i = 0; i < n; ++i)
    {
        const float v = scMono[(size_t) i];
        const float mono = 0.5f * (L[i] + R[i]);
        for (int b = 0; b < kBands; ++b)
        {
            const float vb = vDet[b].bandPass (v, g[b], 1.2f);
            const float mb = mDet[b].bandPass (mono, g[b], 1.2f);
            const float a = std::abs (vb);
            vEnv[b] += (a - vEnv[b]) * (a > vEnv[b] ? atk : rel);
            vRef[b] = vEnv[b] > vRef[b] ? vEnv[b] : juce::jmax (1e-5f, vRef[b] * refDecay);
            vMs[b] += (vb * vb - vMs[b]) * cMs;
            mMs[b] += (mb * mb - mMs[b]) * cMs;

            float want = 0;
            if (vRef[b] > 2e-4f)
            {
                const float kn = std::pow (juce::jlimit (0.0f, 1.0f, vEnv[b] / vRef[b]), 1.2f);
                const float mask = juce::jlimit (0.0f, 1.0f, (toDb (mMs[b]) - toDb (vMs[b]) + 18.0f) / 18.0f);
                want = depth * kWeights[b] * kn * (0.5f + 0.5f * mask);
                anyVocal |= kn > 0.2f;
            }
            carve[b] += (want - carve[b]) * (want > carve[b] ? cAtk : rel);
            peak[b] = juce::jmax (peak[b], carve[b]);
        }

        float l = L[i], r = R[i];
        if (midOnly)
        {
            float m = 0.5f * (l + r);
            const float s = 0.5f * (l - r);
            for (int b = 0; b < kBands; ++b) m = eq[b][0].bell (m, g[b], q, -carve[b]);
            l = m + s; r = m - s;
        }
        else
            for (int b = 0; b < kBands; ++b)
            {
                l = eq[b][0].bell (l, g[b], q, -carve[b]);
                r = eq[b][1].bell (r, g[b], q, -carve[b]);
            }
        L[i] = l * outG;
        R[i] = r * outG;
    }
    for (int b = 0; b < kBands; ++b) carveDb[(size_t) b] = peak[b];
    vocalPresent = anyVocal;
}

void RoomProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void RoomProcessor::setStateInformation (const void* data, int size)
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

juce::AudioProcessorEditor* RoomProcessor::createEditor() { return new RoomEditor (*this); }

//==============================================================================
namespace rc
{
    const juce::Colour bg   = toota::cream;
    const juce::Colour pnl  = toota::cream2;
    const juce::Colour ins  = toota::cream3;
    const juce::Colour text = toota::ink;
    const juce::Colour dim  = toota::inkSoft;
    const juce::Colour blue = toota::teal;
    const juce::Colour gold = toota::coral;
}

static juce::Font rf (float s, bool bold = true, float kern = 0) { return toota::font (s * 1.1f, bold ? 2 : 1, kern * 0.4f); }

RoomEditor::RoomEditor (RoomProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    for (auto* id : { rp::depth, rp::focus, rp::width, rp::release, rp::output })
    {
        auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s->setPopupDisplayEnabled (true, true, this, 1200);
        addAndMakeVisible (s);
        att.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, *s));
        if (auto* prm = proc.apvts.getParameter (id)) s->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
    addAndMakeVisible (midButton);
    midAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, rp::midOnly, midButton);
    setSize (620, 360);
    startTimerHz (30);
}

RoomEditor::~RoomEditor() { setLookAndFeel (nullptr); }

void RoomEditor::resized()
{
    const int y = 262;
    for (int i = 0; i < sliders.size(); ++i) sliders[i]->setBounds (24 + i * 96, y, 64, 64);
    midButton.setBounds (506, y + 19, 96, 26);
}

void RoomEditor::timerCallback()
{
    for (int b = 0; b < RoomProcessor::kBands; ++b)
        shown[(size_t) b] = juce::jmax (proc.carveDb[(size_t) b].load(), shown[(size_t) b] - 0.4f);
    repaint();
}

void RoomEditor::paint (juce::Graphics& g)
{
    g.fillAll (rc::bg);
    toota::header (g, { 0, 0, 620, 56 }, "CLARION ROOM", "makes space for the vocal");
    const bool v = proc.vocalPresent.load();
    g.setColour (v ? toota::teal : rc::gold);
    g.fillEllipse (16, 340, 8, 8);
    g.setColour (rc::text); g.setFont (rf (10, true, 0.06f));
    g.drawText (v ? "VOCAL SIDECHAIN: ACTIVE" : "SIDECHAIN: ROUTE THE VOCAL HERE", 30, 335, 300, 16, juce::Justification::centredLeft);

    auto area = juce::Rectangle<float> (16, 68, 588, 172);
    toota::panel (g, area, 12);
    const float focus = proc.apvts.getRawParameterValue (rp::focus)->load();
    const float bw = (area.getWidth() - 40) / RoomProcessor::kBands;
    for (int b = 0; b < RoomProcessor::kBands; ++b)
    {
        auto bar = juce::Rectangle<float> (area.getX() + 20 + b * bw + 14, area.getY() + 26, bw - 28, area.getHeight() - 60);
        toota::screen (g, bar, 6);
        const float fr = juce::jlimit (0.0f, 1.0f, shown[(size_t) b] / 12.0f);
        g.setColour (rc::blue);
        g.fillRoundedRectangle (bar.reduced (2.5f).withHeight ((bar.getHeight() - 5) * fr), 4);
        g.setColour (rc::text); g.setFont (rf (10, true));
        const float hz = RoomProcessor::kCentres[b] * focus;
        g.drawText (hz >= 1000 ? juce::String (hz / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (hz)), bar.withY (bar.getBottom() + 6).withHeight (12).toNearestInt(), juce::Justification::centred);
        g.setColour (rc::dim); g.setFont (rf (9, false));
        g.drawText ("-" + juce::String (shown[(size_t) b], 1), bar.withY (bar.getBottom() + 20).withHeight (12).toNearestInt(), juce::Justification::centred);
    }
    g.setColour (rc::dim); g.setFont (rf (9, true, 0.18f));
    g.drawText ("LIVE CARVE IN THE MUSIC", (int) area.getX() + 14, (int) area.getY() + 8, 300, 12, juce::Justification::centredLeft);

    const char* names[] { "DEPTH", "FOCUS", "WIDTH", "RELEASE", "OUTPUT" };
    g.setFont (rf (9, true, 0.1f));
    g.setColour (rc::text);
    for (int i = 0; i < 5; ++i) g.drawText (names[i], 8 + i * 96, 318, 96, 14, juce::Justification::centred);
}

#if ! ROOM_NO_FACTORY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RoomProcessor(); }
#endif
