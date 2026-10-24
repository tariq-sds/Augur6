// Vault: twelve-machine stereo reverb for a return channel.
#include <juce_audio_processors/juce_audio_processors.h>
#include "VaultEngine.h"
#include "../../Common/PedalUI.h"

namespace vid
{
    inline constexpr const char* machine  = "machine";
    inline constexpr const char* decay    = "decay";
    inline constexpr const char* predelay = "predelay";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* tone     = "tone";
    inline constexpr const char* mod      = "mod";
    inline constexpr const char* p1       = "p1";
    inline constexpr const char* p2       = "p2";
    inline constexpr const char* lowCut   = "lowCut";
    inline constexpr const char* highCut  = "highCut";
    inline constexpr const char* width    = "width";
    inline constexpr const char* freeze   = "freeze";
    inline constexpr const char* killDry  = "killDry";
    inline constexpr const char* output   = "output";
}

struct VaultPreset { const char* name; std::vector<std::pair<const char*, float>> v; };
static const std::vector<VaultPreset>& vaultPresets()
{
    using namespace vid;
    static const std::vector<VaultPreset> b {
        { "Vocal Room",        { { machine, 0 }, { decay, 0.8f }, { predelay, 12 }, { p1, 0.5f }, { p2, 0.6f } } },
        { "Concert Hall",      { { machine, 1 }, { decay, 3.2f }, { predelay, 25 }, { p1, 0.6f }, { p2, 0.55f } } },
        { "Big Dark Hall",     { { machine, 1 }, { decay, 6.0f }, { predelay, 40 }, { p1, 0.9f }, { p2, 0.8f }, { tone, -0.5f } } },
        { "Vocal Plate",       { { machine, 2 }, { decay, 2.2f }, { predelay, 30 }, { p1, 0.4f }, { p2, 0.7f }, { tone, 0.2f } } },
        { "Snare Plate",       { { machine, 2 }, { decay, 1.3f }, { predelay, 5 }, { p1, 0.3f }, { p2, 0.9f }, { tone, 0.35f } } },
        { "Surf Spring",       { { machine, 3 }, { decay, 2.5f }, { p1, 0.5f }, { p2, 0.6f } } },
        { "Volume Swell",      { { machine, 4 }, { decay, 4.0f }, { p1, 0.5f } } },
        { "Bloom Pad",         { { machine, 5 }, { decay, 5.0f }, { p1, 0.6f }, { p2, 0.6f } } },
        { "Ambient Cloud",     { { machine, 6 }, { decay, 9.0f }, { predelay, 20 }, { p1, 0.8f }, { p2, 0.7f }, { mod, 0.45f } } },
        { "Distant Choir",     { { machine, 7 }, { decay, 6.0f }, { p1, 0.3f }, { p2, 0.5f } } },
        { "Octave Shimmer",    { { machine, 8 }, { decay, 7.0f }, { p1, 0.0f }, { p2, 0.5f }, { tone, 0.2f } } },
        { "Fifth Shimmer",     { { machine, 8 }, { decay, 6.0f }, { p1, 0.2f }, { p2, 0.4f } } },
        { "Tape Echo Verb",    { { machine, 9 }, { decay, 2.5f }, { p1, 0.5f }, { p2, 0.5f } } },
        { "Gated Drums",       { { machine, 10 }, { decay, 1.6f }, { p1, 0.1f }, { p2, 0.7f } } },
        { "Reverse Swoosh",    { { machine, 10 }, { decay, 2.4f }, { p1, 0.5f }, { p2, 0.6f } } },
        { "Studio Reflections", { { machine, 11 }, { decay, 0.6f }, { p1, 0.4f }, { p2, 0.5f } } },
    };
    return b;
}

class VaultProcessor : public juce::AudioProcessor, public toota::PedalHost, private juce::AudioProcessorValueTreeState::Listener
{
public:
    VaultProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "VAULT", layout())
    {
        for (auto* p : getParameters()) if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (p)) apvts.addParameterListener (w->paramID, this);
        loadPreset (1);
    }
    ~VaultProcessor() override { for (auto* p : getParameters()) if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (p)) apvts.removeParameterListener (w->paramID, this); }

    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace vid;
        juce::AudioProcessorValueTreeState::ParameterLayout l;
        juce::StringArray machines; for (int i = 0; i < vault::kNumMachines; ++i) machines.add (vault::machineName (i));
        auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
        auto addF = [&] (const char* id, const char* name, float lo, float hi, float def, float centre = -1, juce::AudioParameterFloatAttributes a = {})
        {
            juce::NormalisableRange<float> r (lo, hi); if (centre > 0) r.setSkewForCentre (centre);
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def, a));
        };
        auto hz = juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000 ? juce::String (v / 1000, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; });
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { machine, 1 }, "Machine", machines, 1));
        addF (decay, "Decay", 0.2f, 60, 2.5f, 3.0f, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10 ? 1 : 0) + " s"; }));
        addF (predelay, "Pre-Delay", 0, 500, 20, 60, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + " ms"; }));
        addF (mix, "Mix", 0, 1, 0.5f, -1, pct);
        addF (tone, "Tone", -1, 1, 0, -1, juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < -0.02f ? "darker " + juce::String (juce::roundToInt (-v * 100)) : v > 0.02f ? "brighter " + juce::String (juce::roundToInt (v * 100)) : juce::String ("flat"); }));
        addF (mod, "Mod", 0, 1, 0.3f, -1, pct);
        addF (p1, "Param 1", 0, 1, 0.5f, -1, pct);
        addF (p2, "Param 2", 0, 1, 0.5f, -1, pct);
        addF (lowCut, "Low Cut", 20, 1000, 150, 200, hz);
        addF (highCut, "High Cut", 1000, 20000, 12000, 6000, hz);
        addF (width, "Width", 0, 1, 0.5f, -1, pct);
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { freeze, 1 }, "Freeze", false));
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
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        const int n = b.getNumSamples();
        if (getTotalNumInputChannels() == 1) b.copyFrom (1, 0, b, 0, 0, n);
        vault::Settings s;
        s.machine = juce::roundToInt (v (vid::machine));
        s.decay = v (vid::decay); s.predelay = v (vid::predelay) * 0.001f; s.mix = v (vid::mix); s.tone = v (vid::tone);
        s.mod = v (vid::mod); s.p1 = v (vid::p1); s.p2 = v (vid::p2); s.lowCut = v (vid::lowCut); s.highCut = v (vid::highCut);
        s.width = v (vid::width); s.freeze = v (vid::freeze) > 0.5f; s.killDry = v (vid::killDry) > 0.5f; s.outDb = v (vid::output);
        engine.process (b.getWritePointer (0), b.getWritePointer (1), n, s);
        level = juce::jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n), level.load() * 0.8f);
    }
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vault"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }
    int getNumPrograms() override { return (int) vaultPresets().size(); }
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

    juce::AudioProcessorValueTreeState& state() override { return apvts; }
    int numPresets() const override { return (int) vaultPresets().size(); }
    juce::String presetName (int i) const override { return juce::isPositiveAndBelow (i, numPresets()) ? juce::String (vaultPresets()[(size_t) i].name) : juce::String(); }
    void loadPreset (int i) override
    {
        loading = true;
        for (auto* p : getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (r->paramID != vid::killDry && r->paramID != vid::lowCut && r->paramID != vid::output)
                    r->setValueNotifyingHost (r->getDefaultValue());
        if (juce::isPositiveAndBelow (i, numPresets()))
            for (auto& [id, val] : vaultPresets()[(size_t) i].v)
                if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (val));
        current = i; edited = false; loading = false;
    }
    int presetIndex() const override { return current; }
    bool presetEdited() const override { return edited; }
    float outputLevel() const override { return level.load(); }

    float v (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    juce::AudioProcessorValueTreeState apvts;

private:
    void parameterChanged (const juce::String&, float) override { if (! loading) edited = true; }
    vault::Engine engine;
    std::atomic<float> level { 0 };
    int current = 0;
    bool edited = false, loading = false;
};

juce::AudioProcessorEditor* VaultProcessor::createEditor()
{
    toota::PedalSpec s;
    s.name = "VAULT"; s.tagline = "twelve-machine stereo reverb"; s.accent = toota::coral;
    for (int i = 0; i < vault::kNumMachines; ++i) s.machines.add (vault::machineName (i));
    s.info = [] (int m) { return juce::String (vault::machineInfo (m)); };
    s.paramNames = [] (int m) { auto p = vault::paramNames (m); return std::pair<juce::String, juce::String> { p.first, p.second }; };
    s.readout = [this] { return v (vid::freeze) > 0.5f ? juce::String ("FROZEN") : juce::String (v (vid::decay), v (vid::decay) < 10 ? 1 : 0) + " s"; };
    s.readoutSub = [this] { return "PRE-DELAY " + juce::String (juce::roundToInt (v (vid::predelay))) + " MS"; };
    s.main = { { vid::decay, "DECAY" }, { vid::predelay, "PRE-DELAY" }, { vid::mix, "MIX" }, { vid::tone, "TONE", true }, { vid::mod, "MOD" },
               { vid::p1, "P1" }, { vid::p2, "P2" } };
    s.small = { { vid::lowCut, "LOW CUT" }, { vid::highCut, "HIGH CUT" }, { vid::width, "WIDTH" }, { vid::output, "OUTPUT", true } };
    s.toggles = { { vid::freeze, "FREEZE" }, { vid::killDry, "KILL DRY" } };
    return new toota::PedalEditor (*this, *this, std::move (s));
}

#ifndef VAULT_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VaultProcessor(); }
#endif
