#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Renderer.h"
#include "Preview.h"
#include <mutex>
#include <map>

namespace ap
{
    inline constexpr const char* style     = "style";
    inline constexpr const char* variation = "variation";
    inline constexpr const char* level     = "level";
    inline constexpr const char* keyOn     = "keyOn";
    inline constexpr const char* root      = "root";
    inline constexpr const char* minor     = "minor";
    inline constexpr const char* follow    = "follow";
}

// Everything the audio thread needs, swapped in one go
struct Engine
{
    std::vector<std::shared_ptr<const arc2::LaneAudio>> audio;
    std::vector<float> gain;     // linear, mute / solo applied
    std::vector<int> uid;
    std::shared_ptr<const arc2::Plan> plan;
};

class ArcProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int kMaxLanes = 12;
    using Lock = std::lock_guard<std::recursive_mutex>;

    ArcProcessor();
    ~ArcProcessor() override;
    void prepareToPlay (double sampleRate, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Arc"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- stems (message thread)
    void addFiles (const juce::StringArray& paths);              // background
    bool addFileSync (const juce::File& f, int role = -1);       // blocking (tests)
    void removeStem (int i);
    void importSet (const juce::File& als);                      // a whole Live set: one lane per audio track
    void replaceStem (int i, const juce::File& f);
    std::vector<arc2::Stem> stems() const { Lock lk (mutex); return lanes; }
    int numStems() const { Lock lk (mutex); return (int) lanes.size(); }

    // per-lane edits (all undoable)
    void setRole (int i, int role);
    void setNudge (int i, int sixteenths);
    void setCrop (int i, int startBar, int bars);
    void setManualTranspose (int i, int semis);                 // arc2::Stem::kAuto = follow KEY
    void setVolume (int i, float dB, bool undoable = false);
    void setMute (int i, bool);
    void setSolo (int i, bool);
    void setHold (int i, bool);
    void setVary (int i, int amount);                           // 0 straight, 1 some, 2 lots
    void setSourceTempo (int i, double bpm);                    // pick a different bar count / tempo for the file
    void barOneAtStart (int i);                                 // undo any bar-1 detection: the file starts on the 1
    void dice (int i);                                          // new variations for one lane
    void generate();                                            // new variations for every lane not on HOLD
    void detectKey();                                           // sets ROOT / SCALE from the tonal stems
    void beginVolumeGesture() { pushUndo(); }
    void refreshKeyForTests() { dirty = false; applyKey(); rebuild(); }
    void syncTempo (double bpm, double rate) { { Lock lk (mutex); prepBpm = bpm; prepSr = rate; } hostBpm = bpm; prepareAll(); }   // re-stretch everything now

    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();

    // ---- transport (Arc's own when Live isn't playing)
    void setPosition (double beats) { arcPos = juce::jmax (0.0, beats); }
    void setPlaying (bool p) { arcPlaying = p; }
    void setLoop (double startBeat, double endBeat) { loopStart = startBeat; loopEnd = endBeat; }
    std::atomic<bool> arcPlaying { false };
    std::atomic<double> arcPos { 0 }, loopStart { 0 }, loopEnd { 0 };   // loopEnd 0 = whole song

    // ---- export
    juce::File exportFolder() const;
    juce::File exportLane (int i, const juce::File& folder);    // full-length arranged WAV of one lane
    juce::File exportMix (const juce::File& folder);
    juce::Array<juce::File> exportAll (bool reveal);            // every lane + the mix into a new folder

    // ---- read by the UI
    std::shared_ptr<const arc2::Plan> plan() const { return std::atomic_load (&planPtr); }
    std::atomic<int> jobs { 0 };
    std::atomic<double> hostBpm { 124 }, playPpq { 0 };
    std::atomic<bool> playing { false }, liveDriving { false };
    std::atomic<int> version { 0 };
    juce::String message() const { Lock lk (mutex); return lastMessage; }
    void setMessage (const juce::String& m) { Lock lk (mutex); lastMessage = m; ++version; }
    int globalTonic() const;
    bool globalMinor() const;
    double songBeats() const { auto p = plan(); return p ? p->totalBars * 4.0 : 0.0; }
    double preparedBpm() const { return prepBpm; }

    // offline render of a range (tests / export); lane -1 = mix
    juce::AudioBuffer<float> render (int lane, double fromBeat, double beats);

    juce::AudioProcessorValueTreeState apvts;

private:
    void parameterChanged (const juce::String&, float) override { dirty = true; }
    void pushUndo();
    void rebuild();                         // plan + engine from current stems (fast)
    void prepare (int i);                   // re-render stem i's loop in the background
    void prepareAll();
    void applyKey();                        // works out transpose for each stem; re-prepares those that changed
    void publishEngine();
    void insertStem (arc2::Stem s);
    void adoptTiming (arc2::Stem& s);
    int indexOfUid (int uid) const;

    mutable std::recursive_mutex mutex;
    juce::String lastMessage;
    std::vector<arc2::Stem> lanes;
    struct Snapshot { std::vector<arc2::Stem> lanes; int root, minor, keyOn; };
    std::vector<Snapshot> undoStack, redoStack;
    std::shared_ptr<const arc2::Plan> planPtr;
    std::shared_ptr<const Engine> engine;
    std::array<arc2::LaneState, kMaxLanes> states;
    std::array<int, kMaxLanes> stateUid {};
    int nextUid = 1;
    uint32_t seedCounter = 1;
    std::map<int, int> prepGen;            // uid -> latest preparation number
    double prepBpm = 124, prepSr = 48000;
    std::atomic<bool> keyEverSet { false }, needKeyDetect { false };
    bool loadMidiStem (arc2::Stem&, const juce::File&);
    juce::ThreadPool pool { 2 };
    std::atomic<bool> dirty { false };
    struct Watch : juce::Timer { ArcProcessor& p; Watch (ArcProcessor& o) : p (o) {} void timerCallback() override; int tempoQuiet = 0; } watch { *this };
    double sr = 48000;
    double lastAudioBeat = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArcProcessor)
};
