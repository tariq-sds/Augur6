#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

namespace vap
{
    inline constexpr const char* texture    = "texture";
    inline constexpr const char* release    = "release";
    inline constexpr const char* brightness = "brightness";
    inline constexpr const char* width      = "width";
    inline constexpr const char* pitch      = "pitch";
    inline constexpr const char* style      = "style";
    inline constexpr const char* wetOnly    = "wetOnly";
    inline constexpr const char* bypass     = "bypass";
    inline constexpr const char* every      = "every";
    inline constexpr const char* length     = "length";
}

class VariaProcessor : public juce::AudioProcessor
{
public:
    VariaProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Varia"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }

    int getNumPrograms() override { return (int) va::styles().size(); }
    int getCurrentProgram() override { return (int) apvts.getRawParameterValue (vap::style)->load(); }
    void setCurrentProgram (int i) override;
    const juce::String getProgramName (int i) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    va::Engine engine;
    std::atomic<float> inLevel { 0 }, outLevel { 0 };
    juce::AudioPlayHead* testPlayHead = nullptr;   // used by the offline test

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    float pv (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VariaProcessor)
};
