#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "VocalDSP.h"

// Clarion Room: goes on the music / instrumental bus with the vocal on its sidechain.
// It carves the music only where, and only while, the vocal needs the space.
namespace rp
{
    inline constexpr const char* depth   = "depth";
    inline constexpr const char* focus   = "focus";
    inline constexpr const char* width   = "width";
    inline constexpr const char* release = "release";
    inline constexpr const char* midOnly = "midOnly";
    inline constexpr const char* output  = "output";
}

class RoomProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kBands = 5;
    static constexpr float kCentres[kBands] { 250.0f, 700.0f, 1500.0f, 3000.0f, 6000.0f };
    static constexpr float kWeights[kBands] { 0.45f, 0.75f, 1.0f, 1.0f, 0.7f };

    RoomProcessor();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Clarion Room"; }
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

    juce::AudioProcessorValueTreeState apvts;
    std::array<std::atomic<float>, kBands> carveDb {};
    std::atomic<bool> vocalPresent { false };

private:
    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    double sr = 48000.0;
    cl::Svf vDet[kBands], mDet[kBands], eq[kBands][2];
    float vEnv[kBands] {}, vRef[kBands] {}, vMs[kBands] {}, mMs[kBands] {}, carve[kBands] {};
    std::vector<float> scMono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomProcessor)
};

class RoomLook : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};

class RoomEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RoomEditor (RoomProcessor&);
    ~RoomEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    RoomProcessor& proc;
    RoomLook look;
    juce::OwnedArray<juce::Slider> sliders;
    juce::ToggleButton midButton { "Mid only" };
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midAtt;
    std::array<float, RoomProcessor::kBands> shown {};
};
