// Toota Master: mastering chain with presets, an automatic loudness target and a true-peak limiter.
#include <juce_audio_processors/juce_audio_processors.h>
#include "MasterEngine.h"
#include "../../Common/TootaTheme.h"

namespace mid
{
    inline constexpr const char* input = "input";
    inline constexpr const char* tilt = "tilt";
    inline constexpr const char* low = "low";
    inline constexpr const char* high = "high";
    inline constexpr const char* glue = "glue";
    inline constexpr const char* warmth = "warmth";
    inline constexpr const char* width = "width";
    inline constexpr const char* target = "target";
    inline constexpr const char* autoGain = "autoGain";
    inline constexpr const char* gain = "gain";
    inline constexpr const char* ceiling = "ceiling";
    inline constexpr const char* release = "release";
}
static const juce::StringArray kTargets { "Off", "-14 LUFS (streaming)", "-11 LUFS", "-9 LUFS", "-8 LUFS (club)", "-7 LUFS (loud)" };
static float targetLufs (int i) { static const float t[] { 0, -14, -11, -9, -8, -7 }; return t[juce::jlimit (0, 5, i)]; }

struct MasterPreset { const char* name; const char* info; std::vector<std::pair<const char*, float>> v; };
static const std::vector<MasterPreset>& masterPresets()
{
    using namespace mid;
    static const std::vector<MasterPreset> p {
        { "Club",      "Loud and punchy for the system: -8 LUFS, -1 dBTP.",        { { target, 4 }, { glue, 0.35f }, { warmth, 0.25f }, { low, 0.5f }, { high, 0.5f }, { release, 0.4f } } },
        { "Streaming", "Spotify / Apple level: -14 LUFS, open and dynamic.",        { { target, 1 }, { glue, 0.2f }, { warmth, 0.1f }, { release, 0.6f } } },
        { "Deep House", "Warm lows, soft top, gentle glue: -9 LUFS.",               { { target, 3 }, { glue, 0.3f }, { warmth, 0.4f }, { tilt, -0.15f }, { low, 1.0f } } },
        { "Tech Drive", "Tight and forward with more saturation: -8 LUFS.",         { { target, 4 }, { glue, 0.45f }, { warmth, 0.5f }, { tilt, 0.1f }, { release, 0.3f } } },
        { "Bright Pop", "Air and clarity on top: -9 LUFS.",                          { { target, 3 }, { glue, 0.3f }, { warmth, 0.15f }, { tilt, 0.2f }, { high, 1.5f } } },
        { "Loud",      "As loud as it sensibly goes: -7 LUFS.",                     { { target, 5 }, { glue, 0.5f }, { warmth, 0.45f }, { release, 0.25f } } },
        { "Gentle",    "Just glue and safety: -11 LUFS.",                            { { target, 2 }, { glue, 0.15f }, { warmth, 0.1f }, { release, 0.7f } } },
        { "Demo Check", "Off target: limiter only, so you hear your mix as it is.",  { { target, 0 }, { glue, 0.0f }, { warmth, 0.0f }, { autoGain, 0 } } },
    };
    return p;
}

class MasterProcessor : public juce::AudioProcessor
{
public:
    MasterProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "MASTER", layout())
    { loadPreset (0); }

    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace mid;
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        auto db = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0.04f ? "+" : "") + juce::String (v, 1) + " dB"; });
        auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
        auto f = [&l] (const char* id, const char* nm, float lo, float hi, float def, juce::AudioParameterFloatAttributes a)
        { l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, nm, juce::NormalisableRange<float> (lo, hi), def, a)); };
        f (input, "Input", -12, 12, 0, db);
        f (tilt, "Tone", -1, 1, 0, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < -0.02f ? "warmer " + juce::String (juce::roundToInt (-v * 100)) : v > 0.02f ? "brighter " + juce::String (juce::roundToInt (v * 100)) : juce::String ("flat"); }));
        f (low, "Low", -4, 4, 0, db);
        f (high, "Air", -4, 4, 0, db);
        f (glue, "Glue", 0, 1, 0.3f, pct);
        f (warmth, "Warmth", 0, 1, 0.2f, pct);
        f (width, "Width", 0, 1, 0.5f, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 200)) + "%"; }));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { target, 1 }, "Loudness Target", kTargets, 4));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { autoGain, 1 }, "Auto Gain", true));
        f (gain, "Manual Gain", -6, 18, 0, db);
        f (ceiling, "Ceiling", -3, -0.1f, -1.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dBTP"; }));
        f (release, "Release", 0, 1, 0.5f, pct);
        return l;
    }

    void prepareToPlay (double r, int block) override { engine.prepare (r, block); setLatencySamples (engine.latency()); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        master::Settings s;
        s.inputDb = v (mid::input); s.tilt = v (mid::tilt); s.low = v (mid::low); s.high = v (mid::high);
        s.glue = v (mid::glue); s.warmth = v (mid::warmth); s.width = v (mid::width);
        s.targetLufs = targetLufs ((int) v (mid::target)); s.autoGain = v (mid::autoGain) > 0.5f; s.manualGainDb = v (mid::gain);
        s.ceilingDb = v (mid::ceiling); s.release = v (mid::release);
        engine.process (b, s);
    }
    using AudioProcessor::processBlock;

    void loadPreset (int i)
    {
        loading = true;
        for (auto* p : getParameters()) if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p)) r->setValueNotifyingHost (r->getDefaultValue());
        if (juce::isPositiveAndBelow (i, (int) masterPresets().size()))
            for (auto& [id, val] : masterPresets()[(size_t) i].v) if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (val));
        current = i; loading = false;
    }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Toota Master"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return (int) masterPresets().size(); }
    int getCurrentProgram() override { return current; }
    void setCurrentProgram (int i) override { loadPreset (i); }
    const juce::String getProgramName (int i) override { return juce::isPositiveAndBelow (i, getNumPrograms()) ? masterPresets()[(size_t) i].name : ""; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& d) override { auto st = apvts.copyState(); st.setProperty ("preset", current, nullptr); if (auto x = st.createXml()) copyXmlToBinary (*x, d); }
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
            current = t.getProperty ("preset", 0);
        }
    }
    float v (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    juce::AudioProcessorValueTreeState apvts;
    master::Engine engine;
    int current = 0;
    bool loading = false;
};

//==============================================================================
class MasterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    static constexpr int W = 900, H = 500;
    explicit MasterEditor (MasterProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        for (int i = 0; i < (int) masterPresets().size(); ++i)
        {
            auto* b = presets.add (new juce::TextButton (masterPresets()[(size_t) i].name));
            b->setColour (juce::TextButton::buttonOnColourId, toota::coral);
            b->onClick = [this, i] { proc.loadPreset (i); };
            addAndMakeVisible (b);
        }
        struct K { const char* id; const char* label; bool bip; juce::Colour c; };
        const K ks[] { { mid::tilt, "TONE", true, toota::teal }, { mid::low, "LOW", true, toota::orange }, { mid::high, "AIR", true, toota::teal },
                       { mid::glue, "GLUE", false, toota::coral }, { mid::warmth, "WARMTH", false, toota::orange }, { mid::width, "WIDTH", true, toota::teal },
                       { mid::input, "INPUT", true, toota::inkSoft }, { mid::gain, "GAIN", true, toota::inkSoft }, { mid::ceiling, "CEILING", false, toota::coral }, { mid::release, "RELEASE", false, toota::inkSoft } };
        for (auto& k : ks)
        {
            auto* s = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
            s->setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
            s->getProperties().set ("bipolar", k.bip);
            s->setPopupDisplayEnabled (true, true, this, 900);
            s->setColour (juce::Slider::rotarySliderFillColourId, k.c);
            addAndMakeVisible (s);
            att.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, k.id, *s));
            if (auto* prm = proc.apvts.getParameter (k.id)) { s->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue())); s->setTooltip (prm->getName (64)); }
            labels.add (k.label);
        }
        targetBox.addItemList (kTargets, 1);
        addAndMakeVisible (targetBox);
        cAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, mid::target, targetBox);
        autoB.setClickingTogglesState (true);
        autoB.setColour (juce::TextButton::buttonOnColourId, toota::teal);
        autoB.setTooltip ("Auto gain: pushes the level into the limiter until the output reaches the loudness target");
        addAndMakeVisible (autoB);
        bAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, mid::autoGain, autoB);
        resetB.setTooltip ("Reset the integrated loudness reading");
        resetB.onClick = [this] { proc.engine.resetLoud = true; };
        addAndMakeVisible (resetB);
        setSize (W, H);
        startTimerHz (20);
    }

    void resized() override
    {
        const int n = presets.size(), cw = (W - 48 - (n - 1) * 8) / n;
        for (int i = 0; i < n; ++i) presets[i]->setBounds (24 + i * (cw + 8), 88, cw, 36);
        for (int i = 0; i < 6; ++i) knobs[i]->setBounds (36 + i * 92, 300, 70, 70);
        for (int i = 6; i < 10; ++i) knobs[i]->setBounds (36 + (i - 6) * 92, 410, 52, 52);
        targetBox.setBounds (W - 300, 312, 190, 30);
        autoB.setBounds (W - 100, 312, 72, 30);
        resetB.setBounds (W - 100, 420, 72, 28);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (toota::cream);
        toota::header (g, { 0, 0, W, 72 }, "MASTER", "mastering chain + loudness");
        toota::panel (g, juce::Rectangle<float> (14, 80, W - 28, 52), 14);
        // screen: loudness and limiter
        auto scr = juce::Rectangle<float> (14, 142, W - 28, 140);
        toota::sticker (g, scr, toota::ink, 12, 3, 2.2f);
        auto& L = proc.engine.loud;
        auto big = [&g] (juce::Rectangle<int> r, const juce::String& v, const juce::String& cap, juce::Colour c)
        {
            g.setColour (c); g.setFont (toota::font (34, 2)); g.drawText (v, r.withHeight (44), juce::Justification::centred);
            g.setColour (toota::cream.withAlpha (0.7f)); g.setFont (toota::font (11.5f, 2, 0.08f)); g.drawText (cap, r.withTrimmedTop (46).withHeight (16), juce::Justification::centred);
        };
        auto fmt = [] (double l) { return l <= -69 ? juce::String ("--") : juce::String (l, 1); };
        const int cw = (W - 60) / 5;
        const float tgt = targetLufs ((int) proc.v (mid::target));
        big ({ 30, 160, cw, 70 }, fmt (L.shortTerm), "SHORT-TERM LUFS", toota::teal.brighter (0.3f));
        big ({ 30 + cw, 160, cw, 70 }, fmt (L.integrated), "INTEGRATED LUFS", toota::cream);
        big ({ 30 + cw * 2, 160, cw, 70 }, juce::String (mix::gainToDb (proc.engine.truePeak), 1), "TRUE PEAK dBTP", proc.engine.truePeak > mix::dbToGain (proc.v (mid::ceiling)) ? toota::coral : toota::cream);
        big ({ 30 + cw * 3, 160, cw, 70 }, "-" + juce::String (proc.engine.limiterGr(), 1), "LIMITER dB", proc.engine.limiterGr() > 4 ? toota::coral : toota::orange);
        big ({ 30 + cw * 4, 160, cw, 70 }, (proc.engine.autoDb >= 0 ? "+" : "") + juce::String (proc.engine.autoDb, 1), "AUTO GAIN dB", toota::cream);
        // short-term vs target bar
        auto bar = juce::Rectangle<float> (40, 246, W - 80, 14);
        g.setColour (toota::cream.withAlpha (0.12f)); g.fillRoundedRectangle (bar, 7);
        auto xOf = [&bar] (double l) { return bar.getX() + (float) juce::jlimit (0.0, 1.0, (l + 30.0) / 26.0) * bar.getWidth(); };
        g.setColour (toota::teal); g.fillRoundedRectangle (bar.withRight (xOf (L.shortTerm)), 7);
        if (tgt < 0) { g.setColour (toota::coral); g.fillRect (xOf (tgt) - 1.5f, bar.getY() - 4, 3.0f, bar.getHeight() + 8); }
        g.setColour (toota::cream.withAlpha (0.6f)); g.setFont (toota::font (10.5f, 1));
        g.drawText (proc.engine.limiterGr() > 6 ? "Limiter working hard: lower the target or turn the mix down a little" : (tgt < 0 ? "Target " + juce::String (tgt, 0) + " LUFS" : "No loudness target"),
                    juce::Rectangle<int> (40, 262, W - 80, 16), juce::Justification::centred);
        toota::panel (g, juce::Rectangle<float> (14, 292, W - 28, 196), 14);
        for (int i = 0; i < 6; ++i) toota::label (g, labels[i], { 36 + i * 92 - 10, 372, 90, 16 }, juce::Justification::centred, 12.5f);
        for (int i = 6; i < 10; ++i) toota::label (g, labels[i], { 36 + (i - 6) * 92 - 20, 464, 92, 14 }, juce::Justification::centred, 10.5f, toota::inkSoft);
        toota::label (g, "LOUDNESS TARGET", { W - 300, 292, 270, 18 }, juce::Justification::centredLeft, 10.5f, toota::inkSoft);
        const int pi = proc.current;
        if (juce::isPositiveAndBelow (pi, (int) masterPresets().size()))
            g.setColour (toota::inkSoft), g.setFont (toota::font (12, 1)),
            g.drawFittedText (masterPresets()[(size_t) pi].info, juce::Rectangle<int> (W - 300, 352, 270, 54), juce::Justification::topLeft, 3);
    }

private:
    void timerCallback() override
    {
        for (int i = 0; i < presets.size(); ++i) presets[i]->setToggleState (i == proc.current, juce::dontSendNotification);
        repaint (14, 142, W - 28, 142);
        repaint (W - 310, 340, 300, 80);
    }
    MasterProcessor& proc;
    toota::LookScope lookScope;
    juce::OwnedArray<juce::TextButton> presets;
    juce::OwnedArray<juce::Slider> knobs;
    juce::StringArray labels;
    juce::ComboBox targetBox;
    juce::TextButton autoB { "AUTO" }, resetB { "RESET" };
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    juce::TooltipWindow tips { this, 600 };
};

juce::AudioProcessorEditor* MasterProcessor::createEditor() { return new MasterEditor (*this); }

#ifndef MASTER_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MasterProcessor(); }
#endif
