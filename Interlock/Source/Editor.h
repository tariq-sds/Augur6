#pragma once
#include "Processor.h"
#include "../../Common/TootaTheme.h"

class ILLook : public toota::Look
{
public:
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
};

// The EQ display: spectra, collision, Interlock's kick dip / bass focus nodes and the EQ bands.
class EqDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit EqDisplay (InterlockProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = {}; hoverPos = { -1, -1 }; repaint(); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void (int)> onSelect;        // band selected (-1 = none)
    std::vector<int> selected;                 // selected EQ bands
    int primary() const { return selected.empty() ? -1 : selected.back(); }
    void select (int band, bool add);
    float xForFreq (double f) const;
    double freqForX (float x) const;

private:
    struct Hit { int kind = 0; int index = -1; };   // kind: 0 none, 1 EQ band, 2 kick dip, 3 bass focus
    float yForEq (float db) const;
    float eqForY (float y) const;
    float yForSpec (float db) const;
    float rangeDb() const;
    juce::Point<float> bandNode (int b) const;
    Hit hitTest (juce::Point<float>) const;
    void setParam (const juce::String& id, float value);
    double totalDb (double hz, bool live) const;
    double interlockDb (double hz, bool live) const;
    bool spectrumPeakNear (juce::Point<float>, double& hz, float& db) const;
    juce::Colour bandColour (int b) const;
    void bandMenu (int b);
    void drawPiano (juce::Graphics&, juce::Rectangle<float>);

    InterlockProcessor& proc;
    juce::Rectangle<float> plot;
    Hit hover, drag;
    juce::Point<float> hoverPos { -1, -1 }, downPos;
    std::vector<std::pair<int, ileq::BandParams>> dragStart;
    bool pendingCreate = false, lasso = false, grabbing = false;
    juce::Rectangle<float> lassoRect;
    ileq::Curve other {};
    bool haveOther = false;
};

// Settings of the selected band (Pro-Q's floating band panel)
class BandPanel : public juce::Component
{
public:
    explicit BandPanel (InterlockProcessor&);
    void setBand (int b);
    int band() const { return current; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    std::function<void()> onDelete;

private:
    InterlockProcessor& proc;
    int current = -1;
    juce::ComboBox shapeBox, slopeBox, placeBox;
    juce::Slider freqK, gainK, qK, rangeK, thrK;
    juce::ToggleButton dynT { "Dynamic" }, autoT { "Auto" }, extT { "External SC" }, onT { "Enabled" };
    juce::TextButton soloB { "SOLO" }, delB { "DELETE" };
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
};

class InterlockEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit InterlockEditor (InterlockProcessor&);
    ~InterlockEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }
    EqDisplay& getDisplay() { return display; }
    void showPage (int p);   // 0 Interlock, 1 band

    static constexpr int W = 1100, H = 720;

private:
    void timerCallback() override;
    void knob (const char* id, const juce::String& label, juce::Colour accent);

    InterlockProcessor& proc;
    toota::LookScope lookScope;
    ILLook look;
    juce::Component content;              // everything at native size, scaled as one
    EqDisplay display;
    BandPanel bandPanel;
    juce::TextButton learnButton;
    juce::ToggleButton monoButton { "Mono Lows" }, deltaButton { "Delta" };
    juce::TextButton pageInterlock { "INTERLOCK" }, pageBand { "BAND" };
    // toolbar
    juce::ComboBox phaseBox, analyserBox, otherBox, rangeBox;
    juce::ToggleButton scToggle { "SC" }, pianoToggle { "Piano" }, autoGainToggle { "Auto Gain" };
    juce::Slider gainScaleK, panK;
    juce::TextButton matchButton { "MATCH" }, aButton { "A" }, bButton { "B" }, copyButton { "A>B" };
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> phaseAtt;
    juce::StringArray labels;
    juce::String status;
    bool scOk = false;
    float dipMeter = 0, compMeter = 0;
    int page = 0;
    std::vector<std::pair<int, juce::String>> others;
    juce::TooltipWindow tips { this, 700 };

    struct Group { juce::String title; int first, count; juce::Colour accent; };
    std::vector<Group> groups;
};
