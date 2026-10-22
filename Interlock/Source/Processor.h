#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Analyzer.h"
#include "Spectrum.h"

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
    // EQ (Pro-Q style)
    inline constexpr const char* eqPhase   = "eqPhase";
    inline constexpr const char* gainScale = "gainScale";
    inline constexpr const char* autoGain  = "autoGain";
    inline constexpr const char* outPan    = "outPan";
    inline const char* const bandFields[] { "used", "on", "freq", "gain", "q", "shape", "slope", "place", "dyn", "range", "thr", "auto", "ext" };
    enum BandField { fUsed = 0, fOn, fFreq, fGain, fQ, fShape, fSlope, fPlace, fDyn, fRange, fThr, fAuto, fExt, kBandFields };
    inline juce::String band (int i, int field) { return "b" + juce::String (i + 1) + "_" + bandFields[field]; }
}

class InterlockProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    InterlockProcessor();
    ~InterlockProcessor() override { stopTimer(); ileq::Registry::get().remove (registryId); }

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

    // ---- EQ
    ileq::Engine eq;
    ileq::Analyser spectrum;
    int registryId = 0;
    juce::String trackName;
    std::atomic<bool> editorOpen { false };
    void updateTrackProperties (const TrackProperties& p) override { if (p.name.has_value()) trackName = *p.name; }
    ileq::BandParams bandParams (int i) const;                 // current parameter values of band i
    void setBandParams (int i, const ileq::BandParams&, bool gesture = true);
    int addBand (double hz, float gainDb, int shape = ileq::shBell);   // first free band, -1 if all 24 are used
    void removeBand (int i);
    void startMatch() { spectrum.startCapture(); }
    void cancelMatch() { spectrum.capturing = false; }
    int applyMatch (int maxBands, float amount);              // EQ Match: fits bells into free bands, returns how many
    float autoGainNowDb() const { return eq.autoGainDb; }
    void runTimerForTests() { timerCallback(); }

    // display settings (saved with the set, not automatable)
    int analyserMode = 3;        // 0 off, 1 pre, 2 post, 3 pre + post
    bool showSidechain = true, piano = false;
    int otherInstance = 0;       // registry id of another Interlock to compare against, 0 = none
    int rangeIndex = 2;          // display range: 3 / 6 / 12 / 30 dB
    // A / B: two EQ settings to compare
    int abSlot = 0;
    juce::ValueTree abStore[2];
    void abSelect (int slot);
    void abCopyToOther();
    juce::ValueTree eqSnapshot() const;
    void eqRestore (const juce::ValueTree&);
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
    void timerCallback() override;
    void computeAutoGain();
    std::vector<float> eqL, eqR, dry[2];
    int dryPos = 0, reportedLatency = 0;
    std::array<ileq::BandParams, ileq::kBands> lastAutoParams {};
    float lastAutoScale = -1;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float pv (const char* id) const { return raw.at (id)->load(); }

    std::map<std::string, std::atomic<float>*> raw;
    std::array<std::array<std::atomic<float>*, ilp::kBandFields>, ileq::kBands> bandRaw {};

    struct Svf { float ic1 = 0, ic2 = 0; };
    Svf scBand, dip[2], duckSh[2], boost[2], mono1, mono2;
    float env = 0, ref = 1e-5f, compEnv = 0;
    juce::SmoothedValue<float> sDipF, sDuckF, sBoostF, sBoostG, sDipQ, sMix, sOut;
    std::vector<float> scMono, bassMono;
    int autoLearnHold = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InterlockProcessor)
};
