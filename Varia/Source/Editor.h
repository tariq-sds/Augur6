#pragma once
#include "Processor.h"
#include "../../Common/TootaTheme.h"

class VariaLook : public toota::Look
{
public:
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    juce::Colour accent = toota::teal;
};

// The animated marbled texture orb
class Orb : public juce::Component
{
public:
    void update (float dt, float motion, float level, float flash, const va::Style&);
    void paint (juce::Graphics&) override;
    float value = 0.5f;

private:
    juce::Image img { juce::Image::RGB, 176, 176, true };
    float time = 0.0f, energy = 0.0f;
    juce::Colour c1, c2, c3;
};

class Pill : public juce::Component
{
public:
    std::function<void()> onClick;
    juce::String text;
    bool led = false, lit = false;
    juce::Colour accent;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
};

class VariaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VariaEditor (VariaProcessor&);
    ~VariaEditor() override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void stepStyle (int delta);
    void showStyleMenu();
    void toggleParam (const char* id);

    VariaProcessor& proc;
    toota::LookScope lookScope;
    VariaLook look;
    Orb orb;
    juce::Slider textureSlider;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    Pill bypassPill, wetPill, stylePill;
    juce::Rectangle<int> prevArea, nextArea;
    float inMeter = 0, outMeter = 0, flash = 0;
    int lastCounter = 0;
    juce::String eventText;
    float eventAlpha = 0;
    double lastTime = 0;
};
