#pragma once
#include "Processor.h"

class MirrorLook : public juce::LookAndFeel_V4
{
public:
    MirrorLook();
    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
};

class MirrorEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit MirrorEditor (MirrorProcessor&);
    ~MirrorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refreshNow() { timerCallback(); }

private:
    void timerCallback() override;
    void layoutForMode();
    void paintMaster (juce::Graphics&);
    void paintStem (juce::Graphics&);

    MirrorProcessor& proc;
    MirrorLook look;
    bool masterMode = true;
    juce::TextButton masterTab { "MASTER" }, stemTab { "STEM" };
    juce::TextButton loadButton { "LOAD REFERENCE" }, listenButton { "LISTEN & MATCH" }, undoButton { "UNDO" }, keyButton { "FIX KEYS" };
    juce::ToggleButton bridgeToggle { "Move Live faders (Bridge)" }, lockToggle { "Lock level" };
    juce::ComboBox listenBox, keySourceBox, roleBox;
    juce::Slider toneKnob, headroomKnob, trimKnob, gainKnob, transposeKnob;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    std::vector<StemView> rows;
    juce::Rectangle<int> tableArea;
};
