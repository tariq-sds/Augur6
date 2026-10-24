// Toota Strip: a pre-balanced chain for every channel, with seven macros and one-click gain staging.
#include <juce_audio_processors/juce_audio_processors.h>
#include "StripEngine.h"
#include "../../Common/TootaTheme.h"

namespace sid
{
    inline constexpr const char* type = "type";
    inline constexpr const char* punch = "punch";
    inline constexpr const char* body = "body";
    inline constexpr const char* air = "air";
    inline constexpr const char* glue = "glue";
    inline constexpr const char* drive = "drive";
    inline constexpr const char* width = "width";
    inline constexpr const char* clean = "clean";
    inline constexpr const char* trim = "trim";
    inline constexpr const char* output = "output";
    inline const char* const macros[] { punch, body, air, glue, drive, width, clean };
}

class StripProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    StripProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "STRIP", layout())
    {
        startTimerHz (10);
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        juce::StringArray names; for (int i = 0; i < strip::kNumTypes; ++i) names.add (strip::types()[i].name);
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { sid::type, 1 }, "Source", names, 2));
        auto bip = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { const int p = juce::roundToInt ((v - 0.5f) * 200); return (p > 0 ? "+" : "") + juce::String (p); });
        const char* names7[] { "Punch", "Body", "Air", "Glue", "Drive", "Width", "Clean" };
        for (int i = 0; i < 7; ++i)
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { sid::macros[i], 1 }, names7[i], juce::NormalisableRange<float> (0, 1), 0.5f, bip));
        auto db = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; });
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { sid::trim, 1 }, "Input Trim", juce::NormalisableRange<float> (-30, 30), 0, db));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { sid::output, 1 }, "Output", juce::NormalisableRange<float> (-24, 12), 0, db));
        return l;
    }

    void prepareToPlay (double r, int) override { engine.prepare (r); rate = r; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && (l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainInputChannelSet() == juce::AudioChannelSet::mono());
    }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int n = b.getNumSamples();
        if (getTotalNumInputChannels() == 1) b.copyFrom (1, 0, b, 0, 0, n);
        // gain-staging: measure the incoming level while LEVEL is lit
        if (learning.load())
        {
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.5f * (b.getSample (0, i) + b.getSample (1, i));
                if (std::fabs (x) > 0.001f) { learnSum += (double) x * x; ++learnN; }
            }
            if (learnN > (long) (rate * 6.0)) finishLearn();
        }
        float inPk = juce::jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n));
        strip::Settings s;
        s.type = (int) v (sid::type);
        s.punch = v (sid::punch); s.body = v (sid::body); s.air = v (sid::air); s.glue = v (sid::glue);
        s.drive = v (sid::drive); s.width = v (sid::width); s.clean = v (sid::clean);
        s.trimDb = v (sid::trim); s.outDb = v (sid::output);
        engine.process (b.getWritePointer (0), b.getWritePointer (1), n, s);
        inLevel = juce::jmax (inPk * mix::dbToGain (s.trimDb), inLevel.load() * 0.85f);
        outLevel = juce::jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n), outLevel.load() * 0.85f);
        grNow = engine.gr;
    }
    using AudioProcessor::processBlock;

    // LEVEL: listen for up to six seconds of signal, then set the trim so it sits at -18 dBFS RMS
    void startLearn() { learnSum = 0; learnN = 0; learnedDb = 999; learning = true; }
    void finishLearn()
    {
        learning = false;
        if (learnN > rate * 0.3) learnedDb = (float) (10.0 * std::log10 (learnSum / (double) learnN + 1e-12));
    }
    bool isLearning() const { return learning.load(); }
    float learnProgress() const { return (float) std::min (1.0, learnN / (rate * 6.0)); }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Toota Strip"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& d) override { if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d); }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
        {
            auto t = juce::ValueTree::fromXml (*x);
            apvts.replaceState (t);
            for (auto* prm : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    auto c = t.getChildWithProperty ("id", r->paramID);
                    if (c.isValid() && c.hasProperty ("value")) r->setValueNotifyingHost (r->convertTo0to1 ((float) c.getProperty ("value")));
                }
        }
    }
    void updateTrackProperties (const TrackProperties& p) override { const juce::ScopedLock sl (lock); trackName = p.name.value_or (juce::String()); }
    juce::String track() const { const juce::ScopedLock sl (lock); return trackName; }

    float v (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> inLevel { 0 }, outLevel { 0 }, grNow { 0 };
    float lastLearnResult = 999;
    void pollForTests() { timerCallback(); }

private:
    void timerCallback() override
    {
        // apply a finished level measurement on the message thread
        const float d = learnedDb.exchange (999);
        if (d < 900)
        {
            lastLearnResult = d;
            if (auto* p = apvts.getParameter (sid::trim))
            {
                const float target = juce::jlimit (-30.0f, 30.0f, -18.0f - d + v (sid::trim) * 0.0f);
                p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (target)); p->endChangeGesture();
            }
        }
    }
    strip::Engine engine;
    double rate = 48000;
    std::atomic<bool> learning { false };
    std::atomic<float> learnedDb { 999 };
    double learnSum = 0; long learnN = 0;
    juce::CriticalSection lock;
    juce::String trackName;
};

//==============================================================================
class StripEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int W = 780, H = 420;
    explicit StripEditor (StripProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        for (int i = 0; i < strip::kNumTypes; ++i)
        {
            auto* b = typeButtons.add (new juce::TextButton (strip::types()[i].name));
            b->setColour (juce::TextButton::buttonOnColourId, toota::teal);
            b->onClick = [this, i] { if (auto* prm = proc.apvts.getParameter (sid::type)) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i)); prm->endChangeGesture(); } };
            addAndMakeVisible (b);
        }
        const char* labels[] { "PUNCH", "BODY", "AIR", "GLUE", "DRIVE", "WIDTH", "CLEAN" };
        const char* tips[] { "Attack: up for snap, down for softer hits", "Low-mid weight at this source's body frequency", "Top-end shelf",
                             "Compression: more = steadier, glued level", "Saturation: warmth and harmonics", "Stereo width (bass stays mono on kick, bass and chords)", "Clean-up high-pass: higher cuts more low end, fully left turns it off" };
        const juce::Colour cols[] { toota::coral, toota::orange, toota::teal, toota::coral, toota::orange, toota::teal, toota::inkSoft };
        for (int i = 0; i < 7; ++i)
        {
            auto* k = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
            k->setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
            k->getProperties().set ("bipolar", true);
            k->setPopupDisplayEnabled (true, true, this, 900);
            k->setColour (juce::Slider::rotarySliderFillColourId, cols[i]);
            k->setTooltip (tips[i]);
            k->setDoubleClickReturnValue (true, 0.5);
            addAndMakeVisible (k);
            att.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, sid::macros[i], *k));
            knobLabels.add (labels[i]);
        }
        for (auto* k : { &trimK, &outK })
        {
            k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            k->setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
            k->getProperties().set ("bipolar", true);
            k->setPopupDisplayEnabled (true, true, this, 900);
            k->setDoubleClickReturnValue (true, 0.0);
            addAndMakeVisible (k);
        }
        att.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, sid::trim, trimK));
        att.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, sid::output, outK));
        outK.getProperties().set ("bipolar", false);
        trimK.setTooltip ("Input trim (set automatically by LEVEL)");
        outK.setTooltip ("Output level");
        levelB.setColour (juce::TextButton::buttonColourId, toota::orange);
        levelB.setTooltip ("Gain staging: press, then play the loudest part of this track. After a few seconds the trim is set so the channel sits at -18 dBFS RMS.");
        levelB.onClick = [this] { proc.startLearn(); };
        addAndMakeVisible (levelB);
        setSize (W, H);
        startTimerHz (25);
    }

    void resized() override
    {
        const int cols = 6, cw = (W - 48 - (cols - 1) * 8) / cols;
        for (int i = 0; i < typeButtons.size(); ++i) typeButtons[i]->setBounds (24 + (i % cols) * (cw + 8), 90 + (i / cols) * 42, cw, 34);
        const int slot = (W - 48) / 7;
        for (int i = 0; i < 7; ++i) knobs[i]->setBounds (24 + slot * i + slot / 2 - 34, 214, 68, 68);
        levelB.setBounds (32, 344, 120, 40);
        trimK.setBounds (168, 336, 48, 48);
        outK.setBounds (W - 76, 336, 48, 48);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (toota::cream);
        const auto tn = proc.track();
        toota::header (g, { 0, 0, W, 72 }, "STRIP", tn.isNotEmpty() ? "on " + tn : juce::String ("pre-balanced channel chain"));
        toota::panel (g, juce::Rectangle<float> (14, 82, W - 28, 96), 14);
        const auto& t = strip::types()[juce::jlimit (0, strip::kNumTypes - 1, (int) proc.v (sid::type))];
        toota::label (g, t.info, { 24, 182, W - 48, 20 }, juce::Justification::centred, 12.0f, toota::inkSoft);
        toota::panel (g, juce::Rectangle<float> (14, 204, W - 28, 116), 14);
        const int slot = (W - 48) / 7;
        for (int i = 0; i < 7; ++i) toota::label (g, knobLabels[i], { 24 + slot * i, 288, slot, 18 }, juce::Justification::centred, 12.5f);
        toota::panel (g, juce::Rectangle<float> (14, 328, W - 28, 80), 14);
        toota::label (g, "TRIM", { 160, 386, 64, 14 }, juce::Justification::centred, 10, toota::inkSoft);
        toota::label (g, "OUT", { W - 84, 386, 64, 14 }, juce::Justification::centred, 10, toota::inkSoft);
        // meters: in, gain reduction, out
        auto meter = [&g] (juce::Rectangle<float> r, float v01, juce::Colour c, const juce::String& txt)
        {
            toota::sticker (g, r, toota::cream, 6, 0, 1.6f);
            g.setColour (c); g.fillRoundedRectangle (r.reduced (2.5f).withWidth ((r.getWidth() - 5) * juce::jlimit (0.0f, 1.0f, v01)), 4);
            toota::label (g, txt, r.toNearestInt().translated (0, 16).withHeight (14), juce::Justification::centredLeft, 10, toota::inkSoft);
        };
        auto lv = [] (float x) { return (mix::gainToDb (x) + 48.0f) / 48.0f; };
        auto dbs = [] (float x) { const float d = mix::gainToDb (x); return d < -60 ? juce::String ("--") : juce::String (d, 1); };
        meter ({ 240, 344, 140, 12 }, lv (proc.inLevel.load()), toota::teal, "IN " + dbs (proc.inLevel.load()));
        meter ({ 396, 344, 120, 12 }, proc.grNow.load() / 12.0f, toota::coral, "COMP -" + juce::String (proc.grNow.load(), 1) + " dB");
        meter ({ 532, 344, 140, 12 }, lv (proc.outLevel.load()), toota::orange, "OUT " + dbs (proc.outLevel.load()));
        if (proc.isLearning())
            toota::label (g, "Play the loudest part... " + juce::String (juce::roundToInt (proc.learnProgress() * 100)) + "%", { 240, 382, 430, 16 }, juce::Justification::centredLeft, 11, toota::coral);
        else if (proc.lastLearnResult < 900)
            toota::label (g, "Levelled: was " + juce::String (proc.lastLearnResult, 1) + " dBFS RMS, now -18", { 240, 382, 430, 16 }, juce::Justification::centredLeft, 11, toota::teal);
    }

private:
    void timerCallback() override
    {
        const int t = (int) proc.v (sid::type);
        for (int i = 0; i < typeButtons.size(); ++i) typeButtons[i]->setToggleState (i == t, juce::dontSendNotification);
        levelB.setButtonText (proc.isLearning() ? "LISTENING" : "LEVEL");
        repaint (0, 0, W, 72); repaint (0, 178, W, 26); repaint (0, 328, W, 92);
    }
    StripProcessor& proc;
    toota::LookScope lookScope;
    juce::OwnedArray<juce::TextButton> typeButtons;
    juce::OwnedArray<juce::Slider> knobs;
    juce::StringArray knobLabels;
    juce::Slider trimK, outK;
    juce::TextButton levelB { "LEVEL" };
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    juce::TooltipWindow tips { this, 600 };
};

juce::AudioProcessorEditor* StripProcessor::createEditor() { return new StripEditor (*this); }

#ifndef STRIP_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new StripProcessor(); }
#endif
