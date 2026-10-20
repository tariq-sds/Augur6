#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Analyzer.h"

namespace ilp
{
    inline constexpr const char* dipFreq   = "dipFreq";
    inline constexpr const char* dipDepth  = "dipDepth";
    inline constexpr const char* dipQ      = "dipQ";
    inline constexpr const char* duck      = "duck";
    inline constexpr const char* release   = "release";
    inline constexpr const char* boostFreq = "boostFreq";
    inline constexpr const char* boostGain = "boostGain";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* parMix    = "parMix";
    inline constexpr const char* monoLows  = "monoLows";
    inline constexpr const char* delta     = "delta";
    inline constexpr const char* output    = "output";
}

class InterlockProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    InterlockProcessor();
    ~InterlockProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Interlock"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void startLearn() { analyzer.startLearning(); }
    void pollLearn();          // applies a finished Learn to the parameters (message thread)

    juce::AudioProcessorValueTreeState apvts;
    il::Analyzer analyzer;

    // Metering for the editor
    std::atomic<float> dipNowDb { 0 }, duckNowDb { 0 }, compGrDb { 0 }, sidechainLevel { 0 };
    std::atomic<bool> hasLearned { false };
    std::atomic<float> learnedKickHz { 0 }, learnedBoostHz { 0 }, learnedOverlap { 0 };
    std::atomic<float> bassRmsDb { -18.0f };
    double currentSampleRate = 48000.0;

private:
    void timerCallback() override { pollLearn(); }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float pv (const char* id) const { return raw.at (id)->load(); }

    std::map<std::string, std::atomic<float>*> raw;

    struct Svf { float ic1 = 0, ic2 = 0; };
    Svf scBand, dip[2], duckSh[2], boost[2], mono1, mono2;
    float env = 0, ref = 1e-5f, compEnv = 0;
    juce::SmoothedValue<float> sDipF, sDuckF, sBoostF, sBoostG, sDipQ, sMix, sOut;
    std::vector<float> scMono, bassMono;
    int autoLearnHold = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InterlockProcessor)
};
