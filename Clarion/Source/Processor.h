#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VocalDSP.h"

namespace cp
{
    inline constexpr const char* shift     = "shift";
    inline constexpr const char* matchMode = "matchMode";
    inline constexpr const char* freezeRef = "freezeRef";
    inline constexpr const char* hpf       = "hpf";
    inline constexpr const char* mud       = "mud";
    inline constexpr const char* mudFreq   = "mudFreq";
    inline constexpr const char* presence  = "presence";
    inline constexpr const char* presFreq  = "presFreq";
    inline constexpr const char* air       = "air";
    inline constexpr const char* deess     = "deess";
    inline constexpr const char* clarity   = "clarity";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* parMix    = "parMix";
    inline constexpr const char* glue      = "glue";
    inline constexpr const char* ride      = "ride";
    inline constexpr const char* target    = "target";
    inline constexpr const char* range     = "range";
    inline constexpr const char* speed     = "speed";
    inline constexpr const char* output    = "output";
}

class ClarionProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    ClarionProcessor();
    ~ClarionProcessor() override { stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Clarion"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- actions from the editor (message thread)
    void startAutoEq() { autoEqRequest = true; }
    void matchKey();
    void resetKeys() { resetKeysRequest = true; }
    void pollLearn();

    // ---- analysis results for the editor
    cl::KeyResult vocalKey() const;
    cl::KeyResult refKey() const;
    int refRoot() const;
    int suggestedShift() const;

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>, 12> vocalHist {}, refHist {}, refBass {};
    std::atomic<float> liveHz { 0 }, riderDb { 0 }, vocalDb { -100 }, musicDb { -100 }, compGr { 0 }, deessGr { 0 };
    std::array<std::atomic<float>, 3> clarityDb {};
    std::atomic<bool> musicPresent { false }, vocalActive { false };
    std::atomic<int> autoEqState { 0 };          // 0 idle, 1 learning, 2 ready, 3 applied
    std::atomic<float> autoEqProgress { 0 };
    std::atomic<bool> hasAutoEq { false };
    std::atomic<int> lastMatch { 0 };
    std::atomic<bool> matched { false };
    double currentSampleRate = 48000.0;

private:
    void timerCallback() override { pollLearn(); }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float pv (const char* id) const { return raw.at (id)->load(); }
    std::map<std::string, std::atomic<float>*> raw;

    cl::PitchTracker tracker;
    cl::PitchShifter shifter;
    cl::Chroma chroma;
    float period = 0;
    std::array<float, 8> periodHist {};
    int histW = 0;
    std::array<float, 12> hist {};

    // filters
    cl::Svf hp1[2], hp2[2], mudF[2], presF[2], airF[2], clarF[3][2], deF[2];
    cl::Svf kwVh, kwVs, kwMh, kwMs, sibBand, vBand[3], mBand[3];
    cl::Svf lrnMud, lrnBody, lrnPres, lrnAir, lrnMPres;
    float vMs = 0, mMs = 0, vLong = -24.0f, rider = 0;
    float vBandMs[3] {}, mBandMs[3] {}, clarNow[3] {};
    float sibEnv = 0, fullEnv = 0, deNow = 0;
    float compEnv = 0, glueEnv = 0, glueRef = 1e-5f;
    std::vector<float> musicDelay;
    int mdW = 0;
    std::vector<float> scMono;

    // auto-EQ learning
    std::atomic<bool> autoEqRequest { false }, resetKeysRequest { false };
    double eMud = 0, eBody = 0, ePres = 0, eAir = 0, eMPres = 0;
    int64_t learnSamples = 0, learnTarget = 0;
    std::vector<float> pitchLog;
    struct EqResult { float hpf = 80, mud = 2, presence = 2, air = 1.5f; } eqResult;
    int64_t autoStartHold = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClarionProcessor)
};
