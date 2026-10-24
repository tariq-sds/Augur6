// Relay: twelve-machine stereo delay for a return channel.
#include <juce_audio_processors/juce_audio_processors.h>
#include "RelayEngine.h"
#include "../../Common/PedalUI.h"

namespace rid
{
    inline constexpr const char* machine = "machine";
    inline constexpr const char* delayTime = "time";
    inline constexpr const char* tempoSync = "sync";
    inline constexpr const char* division = "div";
    inline constexpr const char* repeats = "repeats";
    inline constexpr const char* mix     = "mix";
    inline constexpr const char* filter  = "filter";
    inline constexpr const char* grit    = "grit";
    inline constexpr const char* speed   = "speed";
    inline constexpr const char* depth   = "depth";
    inline constexpr const char* p1      = "p1";
    inline constexpr const char* p2      = "p2";
    inline constexpr const char* width   = "width";
    inline constexpr const char* lowCut  = "lowCut";
    inline constexpr const char* hold    = "hold";
    inline constexpr const char* killDry = "killDry";
    inline constexpr const char* output  = "output";
}

struct RelayPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
static const std::vector<RelayPreset>& relayPresets()
{
    using namespace rid;
    static const std::vector<RelayPreset> b {
        { "Clean Quarter",       { { machine, 0 }, { division, 8 }, { repeats, 0.35f }, { filter, 0.8f } } },
        { "Dotted Eighth Bounce", { { machine, 0 }, { division, 6 }, { repeats, 0.45f }, { width, 1.0f }, { filter, 0.72f } } },
        { "Smeared Digital",     { { machine, 0 }, { division, 8 }, { repeats, 0.55f }, { p1, 0.7f }, { p2, 0.6f } } },
        { "Warm Tape Eighths",   { { machine, 1 }, { division, 5 }, { repeats, 0.45f }, { grit, 0.35f }, { p1, 0.4f }, { p2, 0.35f }, { depth, 0.15f } } },
        { "Old Tape Wobble",     { { machine, 1 }, { division, 8 }, { repeats, 0.5f }, { grit, 0.55f }, { p1, 0.75f }, { p2, 0.8f }, { depth, 0.3f } } },
        { "Dub Bucket",          { { machine, 2 }, { division, 9 }, { repeats, 0.72f }, { grit, 0.3f }, { p1, 0.3f }, { p2, 0.3f } } },
        { "Dual Rhythm",         { { machine, 3 }, { division, 8 }, { repeats, 0.4f }, { p1, 0.6f }, { p2, 0.7f } } },
        { "Pattern Gallop",      { { machine, 4 }, { division, 11 }, { repeats, 0.3f }, { p1, 0.15f }, { p2, 0.3f } } },
        { "Reverse Bloom",       { { machine, 5 }, { division, 9 }, { repeats, 0.4f }, { p1, 0.0f } } },
        { "Ice Octaves",         { { machine, 6 }, { division, 8 }, { repeats, 0.45f }, { p1, 0.7f }, { p2, 0.6f } } },
        { "Ducked Vocal Throw",  { { machine, 7 }, { division, 8 }, { repeats, 0.5f }, { p1, 0.6f }, { p2, 0.35f } } },
        { "Swell Pad",           { { machine, 8 }, { division, 9 }, { repeats, 0.6f }, { p1, 0.5f }, { filter, 0.6f } } },
        { "Trem Repeats",        { { machine, 9 }, { division, 6 }, { repeats, 0.45f }, { speed, 0.55f }, { depth, 0.7f }, { p2, 1.0f } } },
        { "Filter Sweep Echo",   { { machine, 10 }, { division, 5 }, { repeats, 0.55f }, { speed, 0.2f }, { depth, 0.8f }, { p1, 0.6f }, { p2, 0.4f } } },
        { "Lo-Fi Radio",         { { machine, 11 }, { division, 8 }, { repeats, 0.45f }, { grit, 0.55f }, { p1, 0.35f }, { p2, 0.7f } } },
    };
    return b;
}

class RelayProcessor : public juce::AudioProcessor, public toota::PedalHost, private juce::AudioProcessorValueTreeState::Listener
{
public:
    RelayProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "RELAY", layout())
    {
        for (auto* p : getParameters()) if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (p)) apvts.addParameterListener (w->paramID, this);
        loadPreset (0);
    }
    ~RelayProcessor() override { for (auto* p : getParameters()) if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (p)) apvts.removeParameterListener (w->paramID, this); }

    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace rid;
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        juce::StringArray machines; for (int i = 0; i < relay::kNumMachines; ++i) machines.add (relay::machineName (i));
        juce::StringArray divs; for (int i = 0; i < space::kNumDivs; ++i) divs.add (space::kDivNames[i]);
        auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
        auto addF = [&] (const char* id, const char* name, float lo, float hi, float def, float centre = -1, juce::AudioParameterFloatAttributes a = {})
        {
            juce::NormalisableRange<float> r (lo, hi); if (centre > 0) r.setSkewForCentre (centre);
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, a));
        };
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { machine, 1 }, "Machine", machines, 0));
        addF (delayTime, "Time", 10, 3000, 500, 400, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000 ? juce::String (v / 1000, 2) + " s" : juce::String (juce::roundToInt (v)) + " ms"; }));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { tempoSync, 1 }, "Sync to Tempo", true));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { division, 1 }, "Note Value", divs, 8));
        addF (repeats, "Repeats", 0, 1.1f, 0.4f, -1, pct);
        addF (mix, "Mix", 0, 1, 0.5f, -1, pct);
        addF (filter, "Filter", 0, 1, 0.75f, -1, pct);
        addF (grit, "Grit", 0, 1, 0, -1, pct);
        addF (speed, "Speed", 0, 1, 0.35f, -1, pct);
        addF (depth, "Depth", 0, 1, 0.15f, -1, pct);
        addF (p1, "Param 1", 0, 1, 0.5f, -1, pct);
        addF (p2, "Param 2", 0, 1, 0.5f, -1, pct);
        addF (width, "Stereo", 0, 1, 0.5f, -1, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 0.02f ? juce::String ("mono") : v < 0.5f ? "narrow" : v < 0.52f ? "stereo" : "ping-pong " + juce::String (juce::roundToInt ((v - 0.5f) * 200)) + "%"; }));
        addF (lowCut, "Low Cut", 20, 800, 120, 150, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " Hz"; }));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { hold, 1 }, "Hold", false));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { killDry, 1 }, "Kill Dry", true));
        addF (output, "Output", -24, 12, 0, -1, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; }));
        return l;
    }

    void prepareToPlay (double r, int) override { engine.prepare (r); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && (l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainInputChannelSet() == juce::AudioChannelSet::mono());
    }

    double delaySeconds (double bpm) const
    {
        if (v (rid::tempoSync) > 0.5f) return space::divBeats (juce::roundToInt (v (rid::division))) * 60.0 / juce::jlimit (30.0, 300.0, bpm);
        return v (rid::delayTime) * 0.001;
    }

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int n = b.getNumSamples();
        if (getTotalNumInputChannels() == 1) b.copyFrom (1, 0, b, 0, 0, n);
        double bpm = 120;
        if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto t = pos->getBpm()) bpm = *t;
        hostBpm = bpm;
        relay::Settings s;
        s.machine = juce::roundToInt (v (rid::machine));
        s.timeSec = (float) delaySeconds (bpm);
        s.repeats = v (rid::repeats); s.mix = v (rid::mix); s.filter = v (rid::filter); s.grit = v (rid::grit);
        s.speed = v (rid::speed); s.depth = v (rid::depth); s.p1 = v (rid::p1); s.p2 = v (rid::p2); s.width = v (rid::width);
        s.lowCut = v (rid::lowCut); s.hold = v (rid::hold) > 0.5f; s.killDry = v (rid::killDry) > 0.5f; s.outDb = v (rid::output);
        engine.process (b.getWritePointer (0), b.getWritePointer (1), n, s);
        level = juce::jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n), level.load() * 0.8f);
    }
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Relay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }
    int getNumPrograms() override { return (int) relayPresets().size(); }
    int getCurrentProgram() override { return current; }
    void setCurrentProgram (int i) override { loadPreset (i); }
    const juce::String getProgramName (int i) override { return presetName (i); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& d) override
    {
        auto st = apvts.copyState(); st.setProperty ("preset", current, nullptr); st.setProperty ("edited", edited, nullptr);
        if (auto x = st.createXml()) copyXmlToBinary (*x, d);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
        {
            loading = true;
            auto t = juce::ValueTree::fromXml (*x);
            apvts.replaceState (t);
            // push every value through so switches land exactly on 0 / 1
            for (auto* prm : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
                {
                    auto child = t.getChildWithProperty ("id", r->paramID);
                    if (child.isValid() && child.hasProperty ("value")) r->setValueNotifyingHost (r->convertTo0to1 ((float) child.getProperty ("value")));
                }
            current = t.getProperty ("preset", 0); edited = t.getProperty ("edited", false);
            loading = false;
        }
    }

    // PedalHost
    juce::AudioProcessorValueTreeState& state() override { return apvts; }
    int numPresets() const override { return (int) relayPresets().size(); }
    juce::String presetName (int i) const override { return juce::isPositiveAndBelow (i, numPresets()) ? juce::String (relayPresets()[(size_t) i].name) : juce::String(); }
    void loadPreset (int i) override
    {
        loading = true;
        for (auto* p : getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (r->paramID != rid::killDry && r->paramID != rid::lowCut && r->paramID != rid::output)   // keep the return-channel setup
                    r->setValueNotifyingHost (r->getDefaultValue());
        if (juce::isPositiveAndBelow (i, numPresets()))
            for (auto& [id, val] : relayPresets()[(size_t) i].v)
                if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (val));
        current = i; edited = false; loading = false;
    }
    int presetIndex() const override { return current; }
    bool presetEdited() const override { return edited; }
    float outputLevel() const override { return level.load(); }

    float v (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    std::atomic<double> hostBpm { 120 };
    juce::AudioProcessorValueTreeState apvts;

private:
    void parameterChanged (const juce::String&, float) override { if (! loading) edited = true; }
    relay::Engine engine;
    std::atomic<float> level { 0 };
    int current = 0;
    bool edited = false, loading = false;
};

//==============================================================================
class TapButton : public juce::TextButton
{
public:
    explicit TapButton (RelayProcessor& p) : juce::TextButton ("TAP"), proc (p)
    {
        setColour (juce::TextButton::buttonColourId, toota::orange);
        setTooltip ("Tap the tempo of your echoes (turns SYNC off). Tap twice or more.");
        onClick = [this]
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (last > 0 && now - last < 3000)
            {
                const double ms = juce::jlimit (10.0, 3000.0, now - last);
                avg = avg > 0 ? avg * 0.5 + ms * 0.5 : ms;
                for (auto* id : { rid::tempoSync, rid::delayTime })
                    if (auto* prm = proc.apvts.getParameter (id))
                    {
                        prm->beginChangeGesture();
                        prm->setValueNotifyingHost (id == rid::tempoSync ? 0.0f : prm->convertTo0to1 ((float) avg));
                        prm->endChangeGesture();
                    }
            }
            else avg = 0;
            last = now;
        };
    }
private:
    RelayProcessor& proc;
    double last = 0, avg = 0;
};

juce::AudioProcessorEditor* RelayProcessor::createEditor()
{
    toota::PedalSpec s;
    s.name = "RELAY"; s.tagline = "twelve-machine stereo delay"; s.accent = toota::teal;
    for (int i = 0; i < relay::kNumMachines; ++i) s.machines.add (relay::machineName (i));
    s.info = [] (int m) { return juce::String (relay::machineInfo (m)); };
    s.paramNames = [] (int m) { auto p = relay::paramNames (m); return std::pair<juce::String, juce::String> { p.first, p.second }; };
    s.readout = [this]
    {
        const double sec = delaySeconds (hostBpm.load());
        return sec >= 1.0 ? juce::String (sec, 2) + " s" : juce::String (juce::roundToInt (sec * 1000)) + " ms";
    };
    s.readoutSub = [this]
    {
        juce::String t = v (rid::tempoSync) > 0.5f ? juce::String (space::kDivNames[juce::roundToInt (v (rid::division))]) + " AT " + juce::String (hostBpm.load(), 1) + " BPM" : juce::String ("FREE TIME");
        if (v (rid::hold) > 0.5f) t = "HOLD   " + t;
        return t;
    };
    s.main = { { rid::delayTime, "TIME" }, { rid::repeats, "REPEATS" }, { rid::mix, "MIX" }, { rid::filter, "FILTER" }, { rid::grit, "GRIT" },
               { rid::p1, "P1" }, { rid::p2, "P2" } };
    s.small = { { rid::speed, "SPEED" }, { rid::depth, "DEPTH" }, { rid::width, "STEREO" }, { rid::lowCut, "LOW CUT" }, { rid::output, "OUTPUT", true } };
    s.combos = { { rid::division, "NOTE VALUE" } };
    s.toggles = { { rid::tempoSync, "SYNC" }, { rid::hold, "HOLD" }, { rid::killDry, "KILL DRY" } };
    s.extra = [this] { return new TapButton (*this); };
    return new toota::PedalEditor (*this, *this, std::move (s));
}

#ifndef RELAY_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RelayProcessor(); }
#endif
