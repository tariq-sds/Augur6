#pragma once
#include "Sanctum.h"
#include "../../Common/TootaTheme.h"

// The stage: your choir drawn as singers on risers around you. Drag a section to move it.
class StageView : public juce::Component
{
public:
    explicit StageView (SanctumProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void tick() { t += 1.0f / 30.0f; repaint(); }

    static juce::Colour colourOf (int section);

private:
    juce::Point<float> centreOf (int section) const;
    float scaleOf (int section) const;
    int hit (juce::Point<float>) const;
    void drawSinger (juce::Graphics&, juce::Point<float> feet, float scale, juce::Colour, float level, float phase, bool ghost);
    float param (const char* id) const { return proc.apvts.getRawParameterValue (id)->load(); }

    SanctumProcessor& proc;
    int dragging = -1, hover = -1;
    juce::Point<float> grabOffset;
    float t = 0;
};

class SanctumEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SanctumEditor (SanctumProcessor&);
    ~SanctumEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int W = 1120, H = 700;

private:
    struct Panel : juce::Component
    {
        explicit Panel (SanctumEditor& e) : ed (e) {}
        void paint (juce::Graphics& g) override { ed.paintPanel (g); }
        SanctumEditor& ed;
    };
    void paintPanel (juce::Graphics&);
    void timerCallback() override;
    juce::Slider* knob (const char* id, const juce::String& label, juce::Colour c, int cx, int cy, int size = 52, bool bipolar = false);
    juce::ComboBox* combo (const char* id, juce::Rectangle<int>);
    void showPresetMenu();
    void step (int dir);

    SanctumProcessor& proc;
    toota::LookScope lookScope;
    toota::Look look;
    Panel panel { *this };
    StageView stage;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiAtt;
    juce::ToggleButton midiToggle { "MIDI chords" };
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton;
    struct Label { juce::String text; juce::Rectangle<int> r; };
    std::vector<Label> labels;
    toota::MidiLearnClicks learnClicks { proc.midiLearn };
    juce::String partsText, learnText;
};
