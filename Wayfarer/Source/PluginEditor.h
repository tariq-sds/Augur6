#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "../../Common/TootaTheme.h"

class WayLook : public toota::Look
{
public:
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

// X/Y touch surface: drag to play the pad parameters (automatable in Live). With PAD GATE on, touching it
// also opens the envelopes, so it plays like a little theremin.
class TouchPad : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    TouchPad (WayfarerProcessor&);
    ~TouchPad() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    void setFrom (juce::Point<float>);
    juce::Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (4.0f); }
    WayfarerProcessor& proc;
    juce::RangedAudioParameter *px, *py;
    bool touching = false;
};

// live picture of the two filters
class FilterView : public juce::Component, private juce::Timer
{
public:
    explicit FilterView (WayfarerProcessor& p) : proc (p) { startTimerHz (20); }
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }
    WayfarerProcessor& proc;
};

class WayfarerPanel : public juce::Component
{
public:
    explicit WayfarerPanel (WayfarerProcessor&);
    ~WayfarerPanel() override;
    void paint (juce::Graphics&) override;
    void refresh();

    static constexpr int kWidth = 1360, kHeight = 800;

private:
    struct Caption { juce::String text; juce::Rectangle<int> area; float size; juce::Colour colour; };
    struct Section { juce::String title; juce::Rectangle<int> area; juce::Colour accent; };

    juce::Slider* knob (const char* id, const juce::String& label, int cx, int cy, int size, juce::Colour accent, bool bipolar = false);
    void toggle (const char* id, const juce::String& text, juce::Rectangle<int>);
    void combo (const char* id, juce::Rectangle<int>, const juce::String& caption = {});
    void caption (const juce::String& t, juce::Rectangle<int> r, float size = 10.5f, juce::Colour c = toota::inkSoft) { captions.push_back ({ t, r, size, c }); }
    juce::Rectangle<int> section (const juce::String& title, int x, int y, int w, int h, juce::Colour accent);
    void showPresetMenu();
    void drawCheek (juce::Graphics&, juce::Rectangle<float>);

    WayfarerProcessor& proc;
    toota::LookScope lookScope;
    WayLook look;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtts;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtts;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAtts;
    std::vector<Caption> captions;
    std::vector<Section> sections;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> bubbles;   // numbered oscillator bubbles etc.
    TouchPad pad;
    FilterView filterView;
    juce::TextButton prevButton { "<" }, nextButton { ">" }, presetButton;
    juce::String lastPresetText;
    juce::Rectangle<int> ledArea;
    bool lastLed = false;
    juce::TooltipWindow tips { this, 700 };
};

class WayfarerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit WayfarerEditor (WayfarerProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { panel.refresh(); }
    WayfarerPanel panel;
};
