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

// Loopworm-style stem lanes on the left, the arrangement they make on the right.
class ArcEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit ArcEditor (ArcProcessor&);
    ~ArcEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray& f, int x, int y) override { fileDragMove (f, x, y); }
    void fileDragMove (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override { dropRow = -2; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void refreshNow() { timerCallback(); }

    static constexpr int W = 1240, H = 780;

    // layout (public for tests)
    static constexpr int kTop = 96, kLeftW = 566, kTimelineX = 580, kFooter = 40;
    int rowH() const;
    juce::Rectangle<int> row (int i) const { return { 12, kTop + i * rowH(), kLeftW - 12, rowH() - 4 }; }
    juce::Rectangle<int> chipR (int i) const  { auto r = row (i); return { r.getX(), r.getY(), 72, r.getHeight() }; }
    juce::Rectangle<int> infoR (int i) const  { auto r = row (i); return { r.getX() + 78, r.getY(), 128, r.getHeight() }; }
    juce::Rectangle<int> waveR (int i) const  { auto r = row (i); return { r.getX() + 210, r.getY() + 3, 196, r.getHeight() - 6 }; }
    juce::Rectangle<int> holdR (int i) const  { auto r = row (i); return { r.getX() + 412, r.getY() + 3, 46, r.getHeight() / 2 - 4 }; }
    juce::Rectangle<int> diceR (int i) const  { auto r = row (i); return { r.getX() + 462, r.getY() + 3, 46, r.getHeight() / 2 - 4 }; }
    juce::Rectangle<int> muteR (int i) const  { auto r = row (i); return { r.getX() + 412, r.getCentreY() + 1, 21, r.getHeight() / 2 - 4 }; }
    juce::Rectangle<int> soloR (int i) const  { auto r = row (i); return { r.getX() + 437, r.getCentreY() + 1, 21, r.getHeight() / 2 - 4 }; }
    juce::Rectangle<int> volR (int i) const   { auto r = row (i); return { r.getX() + 462, r.getCentreY() + 1, 46, r.getHeight() / 2 - 4 }; }
    juce::Rectangle<int> dragR (int i) const  { auto r = row (i); return { r.getRight() - 40, r.getY() + 3, 38, r.getHeight() - 6 }; }
    juce::Rectangle<int> timeline() const     { return { kTimelineX, kTop - 26, W - kTimelineX - 12, H - kFooter - (kTop - 26) - 8 }; }
    juce::Rectangle<int> sectionStrip() const { auto t = timeline(); return t.withHeight (22); }
    juce::Rectangle<int> dragAllR() const     { return { W - 168, H - kFooter + 6, 156, kFooter - 12 }; }
    float xOfBeat (double beat) const;
    double beatOfX (float x) const;

private:
    void timerCallback() override;
    void drawHeader (juce::Graphics&);
    void drawLane (juce::Graphics&, int i, const arc2::Stem&, const arc2::Plan&);
    void drawTimeline (juce::Graphics&, const std::vector<arc2::Stem>&, const arc2::Plan&);
    void drawEmpty (juce::Graphics&);
    void laneMenu (int i);
    void dragOut (const juce::Array<juce::File>&);
    int rowAt (juce::Point<int>) const;
    void hint (const juce::String& s) { if (s != hintText) { hintText = s; repaint (0, H - kFooter, W, kFooter); } }

    ArcProcessor& proc;
    ArcLook look;
    juce::ComboBox styleBox, rootBox;
    juce::Slider variationKnob, levelKnob;
    juce::TextButton keyButton { "KEY" }, scaleButton { "MIN" }, detectButton { "DETECT" }, generateButton { "GENERATE" },
        undoButton { "UNDO" }, redoButton { "REDO" }, playButton { "PLAY" }, followButton { "FOLLOW LIVE" }, addButton { "+ ADD STEMS" },
        exportButton { "EXPORT" };
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAtt, rootAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<arc2::Stem> cache;          // what's drawn (copied from the processor when it changes)
    std::shared_ptr<const arc2::Plan> planCache;
    int lastVersion = -1;
    // interaction
    enum class Drag { none, wave, volume, scrub, timelinePan };
    Drag drag = Drag::none;
    int dragRow = -1, dragNudgeBase = 0, dragNudgeNow = 0, dropRow = -2;
    float dragVolStart = 0;
    double viewStart = 0, viewBeats = 0, panStart = 0;
    bool dragOutStarted = false;
    juce::String hintText;
};
