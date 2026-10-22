#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "AudioToMidi.h"
#include "Arranger.h"
#include "Preview.h"
#include <mutex>

namespace ap
{
    inline constexpr const char* style     = "style";
    inline constexpr const char* variation = "variation";
    inline constexpr const char* preview   = "preview";
    inline constexpr const char* level     = "level";
    inline const char* laneOn (int l) { static const char* ids[] { "onDrums", "onBass", "onChords", "onLead" }; return ids[l]; }
}

struct LaneSource
{
    arc::Clip loop;
    juce::String source, report;
    bool fromAudio = false;
    double tempo = 0;
};

class ArcProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener
{
public:
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
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- actions (message thread)
    void loadFile (int lane, const juce::File& f);      // MIDI or audio; lane -1 = guess from content
    void loadFileSync (int lane, const juce::File& f);  // same, blocking (tests)
    void clearLane (int lane);
    void regenerate (bool newSeed);
    juce::File exportLane (int lane);                    // writes the lane's full-arrangement MIDI, returns the file
    juce::Array<juce::File> exportAll();
    juce::File exportFolder() const;

    std::shared_ptr<const arc::Arrangement> arrangement() const { return std::atomic_load (&arr); }
    LaneSource lane (int l) const { std::lock_guard<std::mutex> lk (mutex); return lanes[(size_t) l]; }
    std::atomic<int> busyLane { -1 };
    std::atomic<double> hostBpm { 124 }, playPpq { 0 };
    std::atomic<bool> playing { false };
    std::atomic<int> version { 0 };

    juce::AudioProcessorValueTreeState apvts;

private:
    void parameterChanged (const juce::String&, float) override { triggerRegenerate = true; }
    void convert (int lane, const juce::File& f);

    mutable std::mutex mutex;
    std::array<LaneSource, arc::kNumLanes> lanes;
    std::shared_ptr<const arc::Arrangement> arr;
    uint32_t seed = 1;
    arc::Preview preview;
    juce::ThreadPool pool { 1 };
    std::atomic<bool> triggerRegenerate { false };
    struct Regen : juce::Timer { ArcProcessor& p; Regen (ArcProcessor& o) : p (o) {} void timerCallback() override { if (p.triggerRegenerate.exchange (false)) p.regenerate (false); } } regenTimer { *this };
    double sr = 48000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArcProcessor)
};
