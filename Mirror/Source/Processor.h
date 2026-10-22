#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Registry.h"
#include "PhaseVocoder.h"
#include <juce_osc/juce_osc.h>

namespace mp
{
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* role      = "role";
    inline constexpr const char* lock      = "lock";
    inline constexpr const char* gain      = "gain";
    inline constexpr const char* transpose = "transpose";
    inline constexpr const char* toneMatch = "toneMatch";
    inline constexpr const char* trim      = "trim";
    inline constexpr const char* headroom  = "headroom";
    inline constexpr const char* keySource = "keySource";
    inline constexpr const char* listenBars = "listenBars";
}

struct StemView    // what the master shows per stem
{
    int slot = -1;
    juce::String name;
    int role = mr::roleTonal;
    bool locked = false;
    float gainDb = 0, suggestedDb = 0, level = 0, transpose = 0;
    int keyState = 0, keyShift = 0;
    float keyFit = 0;
    float faderDb = 0, changeDb = 0; bool faderKnown = false;
    juce::String bridgeMsg;
};

class MirrorProcessor : public juce::AudioProcessor, private juce::Timer,
                        private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    MirrorProcessor();
    ~MirrorProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Mirror"; }
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
    void updateTrackProperties (const TrackProperties&) override;

    bool isMaster() const { return apvts.getRawParameterValue (mp::mode)->load() < 0.5f; }

    // ---- master actions (message thread)
    void loadReference (const juce::File&);
    void startListen();                 // listen, then solve and apply
    void applyMatch();
    void undoMatch();
    void fixKeys (bool autoTranspose);
    void solveNow();
    void tick() { timerCallback(); }    // for tests
    void restoreFaders();
    bool bridgeConnected() const { return juce::Time::getMillisecondCounterHiRes() - lastBridgeHello < 5000.0; }
    std::map<juce::String, float> bridgeFaders() const { std::lock_guard<std::mutex> lk (refMutex); return faders; }
    std::atomic<bool> useBridge { true };
    void oscMessageReceived (const juce::OSCMessage&) override;   // public for tests

    // ---- master state for the UI
    std::vector<StemView> stems() const;
    mr::ReferenceInfo reference() const { std::lock_guard<std::mutex> lk (refMutex); return ref; }
    std::atomic<float> refProgress { 0 };
    std::atomic<bool> refLoading { false };
    std::atomic<float> listenProgress { 0 };
    std::atomic<bool> listeningNow { false };
    mr::SolveResult lastSolve () const { std::lock_guard<std::mutex> lk (refMutex); return solve; }
    cl::KeyResult projectKey() const { std::lock_guard<std::mutex> lk (refMutex); return projKey; }
    juce::String statusText() const { std::lock_guard<std::mutex> lk (refMutex); return status; }
    std::array<float, mr::kBands> mixDisplay() const { return analyzer.display; }
    std::atomic<float> mixPeakDb { -100 };

    // ---- stem state
    int slotIndex() const { return slot; }
    juce::String trackName;

    juce::AudioProcessorValueTreeState apvts;
    juce::File referenceFile;

private:
    void timerCallback() override;
    void setParam (const char* id, float value);
    void ensureSlot();
    void publishStemAnalysis();
    void computeKeys();

    mutable std::mutex refMutex;
    mr::ReferenceInfo ref;
    mr::SolveResult solve;
    cl::KeyResult projKey;
    juce::String status { "Load a reference track, then play your loudest section and press LISTEN & MATCH" };
    std::vector<int> solveSlots;
    std::map<int, float> undoGains;
    std::map<juce::String, float> faders;          // from the bridge: Live fader dB by track name
    std::map<juce::String, juce::String> bridgeResults;
    std::map<juce::String, float> lastSent;           // fader changes sent to the bridge
    double lastBridgeHello = -1e9;
    juce::OSCSender oscOut;
    juce::OSCReceiver oscIn;
    bool oscOutOk = false, oscInOk = false;
    void startOsc();
    void stopOsc();
    juce::ThreadPool pool { 1 };

    double sr = 48000;
    mr::BandAnalyzer analyzer;
    cl::Chroma chroma;
    mr::PhaseVocoder vocoder;
    float pvIn = 0, pvOut = 0, pvGain = 1;
    juce::SmoothedValue<float> gainSmooth, trimSmooth;
    int slot = -1;
    int lastListenSeq = -1;
    bool wasListening = false;
    float sustainSum = 0, sustainMax = 0, blockRms = 0; int sustainCount = 0;
    std::atomic<bool> publishPending { false };

    // master
    std::atomic<double> hostBpm { 120 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<int64_t> listenedSamples { 0 };
    int64_t listenTarget = 0;
    int solveCountdown = -1;
    bool applyAfterSolve = false;
    float peakDuringListen = 0;
    std::atomic<float> peakAcc { 0 };
    std::array<std::atomic<float>, 10> toneGains {};
    cl::Svf toneEq[10][2];

    // stem gain ramp
    float rampFrom = 0, rampTo = 0; int rampStep = -1;
    int lastTargetSeq = 0, lastTransposeSeq = 0;
    int currentLatency = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirrorProcessor)
};
