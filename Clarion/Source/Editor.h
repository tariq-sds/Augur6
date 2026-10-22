#pragma once
#include "Processor.h"
#include "../../Common/TootaTheme.h"

class ClLook : public toota::Look
{
public:
    ClLook() { pillToggles = true; }
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
};

class ClarionPanel : public juce::Component
{
public:
    explicit ClarionPanel (ClarionProcessor&);
    ~ClarionPanel() override;
    void paint (juce::Graphics&) override;
    void refresh();
    static constexpr int W = 1000, H = 600;

private:
    juce::Slider* knob (const char* id, const juce::String& label, juce::Colour accent, int cx, int cy, int size = 54, bool bipolar = false);
    juce::ToggleButton* toggle (const char* id, const juce::String& text, juce::Rectangle<int>);
    void drawChroma (juce::Graphics&, juce::Rectangle<float>, const std::array<std::atomic<float>, 12>&, juce::Colour, int highlight);
    void drawEq (juce::Graphics&, juce::Rectangle<float>);

    ClarionProcessor& proc;
    toota::LookScope lookScope;
    ClLook look;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
    struct Label { juce::String text; juce::Rectangle<int> r; };
    std::vector<Label> labels;
    juce::TextButton matchButton { "MATCH" }, autoEqButton { "AUTO EQ" }, resetButton { "RESET" };
    juce::TextButton keyModeButton { "KEY" }, rootModeButton { "ROOT" };
    float riderShown = 0;
};

class ClarionEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ClarionEditor (ClarionProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (toota::cream); }
    void refreshNow() { panel.refresh(); }

private:
    void timerCallback() override { panel.refresh(); }
    ClarionPanel panel;
};
