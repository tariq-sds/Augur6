#pragma once
#include "Processor.h"

class ArcLook : public juce::LookAndFeel_V4
{
public:
    ArcLook();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
};

class ArcEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit ArcEditor (ArcProcessor&);
    ~ArcEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override { hoverLane = -1; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void refreshNow() { timerCallback(); }

    static constexpr int W = 1120, H = 720;

private:
    void timerCallback() override;
    int laneAt (juce::Point<int>) const;
    juce::Rectangle<int> laneRow (int l) const { return { 16, 116 + l * 112, 1088, 104 }; }
    juce::Rectangle<int> dragHandle (int l) const { auto r = laneRow (l); return { r.getX() + 196, r.getY() + 70, 84, 26 }; }
    void drawLaneInfo (juce::Graphics&, int l, const arc::Arrangement&);
    void drawTimeline (juce::Graphics&, const arc::Arrangement&);

    ArcProcessor& proc;
    ArcLook look;
    juce::ComboBox styleBox;
    juce::Slider variationKnob, levelKnob;
    juce::TextButton regenButton { "NEW VARIATIONS" }, exportButton { "EXPORT ALL" }, folderButton { "SHOW FILES" }, loadButtons[arc::kNumLanes], clearButtons[arc::kNumLanes];
    juce::ToggleButton previewToggle { "Preview with Live's transport" }, laneToggles[arc::kNumLanes];
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    int hoverLane = -1, dragLane = -1;
    bool dragStarted = false;
    int lastVersion = -1;
};
