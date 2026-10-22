#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "../../Common/TootaTheme.h"

class AugurLook : public toota::Look
{
public:
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

// The full instrument panel, laid out at a fixed native size and scaled by the editor
class AugurPanel : public juce::Component
{
public:
    AugurPanel (Augur6Processor&);
    ~AugurPanel() override;
    void paint (juce::Graphics&) override;
    void refresh();

    static constexpr int kWidth = 1216;
    static constexpr int kHeight = 716;

private:
    struct Caption { juce::String text; juce::Rectangle<int> area; bool small; };
    struct Section { juce::String title; juce::Rectangle<int> area; };

    juce::Rectangle<int> cell (const Section&, float col, float row) const;
    void knob (const Section&, const char* id, const juce::String& label, float col, float row, bool bipolar = false);
    void toggle (const juce::Rectangle<int>&, const char* id, const juce::String& text);
    void combo (const juce::Rectangle<int>&, const char* id);
    void caption (const juce::String& text, juce::Rectangle<int> area, bool small = true);
    Section& section (const juce::String& title, int x, int y, int cols, int rows);
    void showPresetMenu();
    void drawWood (juce::Graphics&, juce::Rectangle<float>);

    Augur6Processor& proc;
    toota::LookScope lookScope;
    AugurLook look;

    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtts;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtts;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtts;

    std::vector<Caption> captions;
    std::vector<Section> sections;

    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton;
    juce::Rectangle<int> voiceLedArea;
    int lastVoices = -1;
    juce::String lastPresetText;
};

class Augur6Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Augur6Editor (Augur6Processor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { panel.refresh(); }
    AugurPanel panel;
};
