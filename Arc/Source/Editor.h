#pragma once
#include "Processor.h"
#include "../../Common/TootaTheme.h"

// Arc: 1) add stems  2) shape the arrangement  3) send it to Live.
// Stem lanes on the left (waveform with bar 1 marked), the arrangement they make on the right.
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
    void mouseExit (const juce::MouseEvent&) override { hint ({}); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void refreshNow() { timerCallback(); }

    static constexpr int W = 1240, H = 800;
    static constexpr int kHeader = 76, kSteps = 86, kStepsH = 66, kTop = 196, kFooter = 40, kLeftX = 12, kLeftW = 580, kTimelineX = 604;

    // layout (public for tests)
    int rowH() const;
    juce::Rectangle<int> row (int i) const { return { kLeftX, kTop + i * rowH(), kLeftW, rowH() - 6 }; }
    juce::Rectangle<int> chipR (int i) const  { auto r = row (i); return { r.getX(), r.getY(), 70, r.getHeight() }; }
    juce::Rectangle<int> infoR (int i) const  { auto r = row (i); return { r.getX() + 78, r.getY(), 108, r.getHeight() }; }
    juce::Rectangle<int> waveR (int i) const  { auto r = row (i); return { r.getX() + 190, r.getY() + 3, 150, r.getHeight() - 6 }; }
    juce::Rectangle<int> varyR (int i) const  { auto r = row (i); return { r.getX() + 348, r.getCentreY() - 12, 70, 24 }; }
    juce::Rectangle<int> diceR (int i) const  { auto r = row (i); return { r.getX() + 424, r.getCentreY() - 12, 26, 24 }; }
    juce::Rectangle<int> muteR (int i) const  { auto r = row (i); return { r.getX() + 456, r.getCentreY() - 12, 24, 24 }; }
    juce::Rectangle<int> soloR (int i) const  { auto r = row (i); return { r.getX() + 484, r.getCentreY() - 12, 24, 24 }; }
    juce::Rectangle<int> volR (int i) const   { auto r = row (i); return { r.getX() + 514, r.getCentreY() - 12, 26, 24 }; }
    juce::Rectangle<int> dragR (int i) const  { auto r = row (i); return { r.getRight() - 36, r.getY() + 2, 34, r.getHeight() - 4 }; }
    juce::Rectangle<int> timeline() const     { return { kTimelineX, kTop - 30, W - kTimelineX - 12, H - kFooter - (kTop - 30) - 10 }; }
    juce::Rectangle<int> sectionStrip() const { auto t = timeline(); return t.withHeight (26); }
    juce::Rectangle<int> step (int n) const;
    juce::Rectangle<int> dragAllR() const     { auto s = step (3); return { s.getX() + 52, s.getY() + 12, 104, s.getHeight() - 24 }; }
    float xOfBeat (double beat) const;
    double beatOfX (float x) const;

private:
    void timerCallback() override;
    void drawHeader (juce::Graphics&);
    void drawSteps (juce::Graphics&);
    void drawLane (juce::Graphics&, int i, const arc2::Stem&);
    void drawTimeline (juce::Graphics&, const std::vector<arc2::Stem>&, const arc2::Plan&);
    void drawEmpty (juce::Graphics&);
    void laneMenu (int i);
    void dragOut (const juce::Array<juce::File>&);
    int rowAt (juce::Point<int>) const;
    void hint (const juce::String& s) { if (s != hintText) { hintText = s; repaint (0, H - kFooter, W, kFooter); } }
    void openChooser();

    ArcProcessor& proc;
    toota::LookScope lookScope;
    juce::ComboBox styleBox, rootBox;
    juce::Slider variationKnob, levelKnob;
    juce::TextButton keyButton { "KEY LOCK" }, scaleButton { "MINOR" }, detectButton { "DETECT" }, generateButton { "GENERATE" },
        undoButton { "UNDO" }, redoButton { "REDO" }, playButton { "PLAY" }, addButton { "+ ADD" }, exportButton { "EXPORT" };
    juce::ToggleButton followToggle { "Follow Live" };
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAtt, rootAtt;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tips { this, 600 };

    std::vector<arc2::Stem> cache;
    std::shared_ptr<const arc2::Plan> planCache;
    int lastVersion = -1;
    enum class Drag { none, wave, volume, scrub, timelinePan };
    Drag drag = Drag::none;
    int dragRow = -1, dragNudgeBase = 0, dragNudgeNow = 0, dropRow = -2;
    float dragVolStart = 0;
    double viewStart = 0, viewBeats = 0, panStart = 0;
    bool dragOutStarted = false;
    juce::String hintText;
};
