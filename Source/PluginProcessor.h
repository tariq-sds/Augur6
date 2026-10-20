#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "Synth.h"
#include "Effects.h"

class Augur6Processor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    Augur6Processor();
    ~Augur6Processor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Augur-6"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    // Factory presets exposed as host programs too
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
    std::atomic<int> voicesPlaying { 0 };

private:
    float p (const char* id) const;
    bool  b (const char* id) const { return p (id) > 0.5f; }
    void fillSettings (augur::Settings&, augur::FxSettings&, double bpm);
    void parameterChanged (const juce::String&, float) override;
    void handleMidi (const juce::MidiMessage&, const augur::Settings&);

    std::unordered_map<std::string, std::atomic<float>*> raw;

    augur::Engine engine;
    augur::Lfo lfo;
    augur::Effects fx;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::vector<float> lfoBuf;

    double hostSampleRate = 48000.0;
    float modWheel = 0.0f, bend = 0.0f, aftertouch = 0.0f;
    float volSmooth = 1.0f;
    bool loadingPreset = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Augur6Processor)
};
