#pragma once
#include "Processor.h"
#include "../../Common/TootaTheme.h"

class ILLook : public toota::Look
{
public:
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
};

class EqDisplay : public juce::Component
{
public:
    explicit EqDisplay (InterlockProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForEq (float db) const;
    float eqForY (float y) const;
    float yForSpec (float db) const;
    juce::Point<float> dipNode() const;
    juce::Point<float> boostNode() const;
    int hitNode (juce::Point<float>) const;
    void setParam (const char* id, float value);
    juce::Path spectrumPath (std::array<std::atomic<float>, il::Analyzer::kBins + 1>&, bool closed) const;

    InterlockProcessor& proc;
    int dragging = -1, hover = -1;
    juce::Rectangle<float> plot;
};

class InterlockEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit InterlockEditor (InterlockProcessor&);
    ~InterlockEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    void knob (const char* id, const juce::String& label, juce::Colour accent);

    InterlockProcessor& proc;
    toota::LookScope lookScope;
    ILLook look;
    EqDisplay display;
    juce::TextButton learnButton;
    juce::ToggleButton monoButton { "Mono Lows" }, deltaButton { "Delta" };
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    juce::StringArray labels;
    juce::String status;
    bool scOk = false;
    float dipMeter = 0, compMeter = 0;

    struct Group { juce::String title; int first, count; juce::Colour accent; };
    std::vector<Group> groups;
};
