#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "ChoirEngine.h"
#include "../../Common/MidiLearn.h"

namespace sp
{
    inline constexpr const char* harmony   = "harmony";
    inline constexpr const char* key       = "key";
    inline constexpr const char* scale     = "scale";
    inline constexpr const char* midiChord = "midiChord";
    inline constexpr const char* singers   = "singers";
    inline constexpr const char* humanise  = "humanise";
    inline constexpr const char* spread    = "spread";
    inline constexpr const char* character = "character";
    inline constexpr const char* formant   = "formant";
    inline constexpr const char* breath    = "breath";
    inline constexpr const char* tone      = "tone";
    inline constexpr const char* energy    = "energy";
    inline constexpr const char* room      = "space";
    inline constexpr const char* reverb    = "reverb";
    inline constexpr const char* sub       = "sub";
    inline constexpr const char* response  = "response";
    inline constexpr const char* lead      = "lead";
    inline constexpr const char* choir     = "choir";
    inline constexpr const char* output    = "output";
    inline const char* const level[sc::kSections] { "lvlS", "lvlA", "lvlT", "lvlB" };
    inline const char* const pan[sc::kSections]   { "panS", "panA", "panT", "panB" };
    inline const char* const depth[sc::kSections] { "depS", "depA", "depT", "depB" };
}

struct ChoirPreset
{
    juce::String name;
    juce::String blurb;
    std::vector<std::pair<const char*, float>> values;   // anything unlisted = default
};
const std::vector<ChoirPreset>& choirPresets();

class SanctumProcessor : public juce::AudioProcessor
{
public:
    SanctumProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Sanctum"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return (int) choirPresets().size(); }
    int getCurrentProgram() override { return juce::jmax (0, currentPreset.load()); }
    void setCurrentProgram (int i) override { loadPreset (i); }
    const juce::String getProgramName (int i) override { return juce::isPositiveAndBelow (i, (int) choirPresets().size()) ? choirPresets()[(size_t) i].name : juce::String(); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void loadPreset (int index);
    juce::String presetName() const;
    sc::Settings settings() const;
    void setParam (const char* id, float realValue);

    juce::AudioProcessorValueTreeState apvts;
    toota::MidiLearn midiLearn;
    sc::Engine engine;
    std::atomic<int> currentPreset { 0 };
    std::atomic<bool> presetEdited { false };
    std::atomic<int> soloSectionForTests { -1 };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    struct Watcher : juce::AudioProcessorValueTreeState::Listener
    {
        SanctumProcessor& p; explicit Watcher (SanctumProcessor& o) : p (o) {}
        void parameterChanged (const juce::String&, float) override { if (! p.loading) p.presetEdited = true; }
    } watcher { *this };
    bool loading = false;
    float leadS = 1, choirS = 0.7f, outS = 1;
    std::vector<float> monoL, monoR;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SanctumProcessor)
};
