#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "Engine.h"
#include "Effects.h"

class WayfarerProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener
{
public:
    WayfarerProcessor();
    ~WayfarerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Wayfarer"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void loadPreset (int index);     // -1 = Init
    juce::String getPresetDisplayName() const;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::atomic<int> currentPreset { 0 };
    std::atomic<bool> presetEdited { false };
    std::atomic<bool> padTouched { false };      // the editor's touch pad is being held
    std::atomic<bool> sounding { false };
    std::atomic<float> lastPitch { 60 }, outLevel { 0 };

private:
    float p (const char* id) const;
    bool  b (const char* id) const { return p (id) > 0.5f; }
    int   c (const char* id) const { return juce::roundToInt (p (id)); }
    void fillSettings (wf::Settings&, wf::FxSettings&, double bpm);
    void parameterChanged (const juce::String&, float) override;
    void handleMidi (const juce::MidiMessage&, const wf::Settings&);

    std::unordered_map<std::string, std::atomic<float>*> raw;

    wf::Engine engine;
    wf::Effects fx;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    double hostSampleRate = 48000.0;
    float modWheel = 0.0f, bendNorm = 0.0f, pressure = 0.0f;
    float volSmooth = 1.0f;
    bool loadingPreset = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WayfarerProcessor)
};
