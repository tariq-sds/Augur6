#include "Editor.h"

namespace ac
{
    const juce::Colour bg    { 0xff0f1013 };
    const juce::Colour panel { 0xff17191e };
    const juce::Colour inset { 0xff0b0c0f };
    const juce::Colour line  { 0xff262931 };
    const juce::Colour text  { 0xffeceef2 };
    const juce::Colour dim   { 0xff8b919c };
    const juce::Colour acc   { 0xffff9e3d };
    inline juce::Colour lane (int l) { static const juce::Colour c[] { juce::Colour (0xffff6b5b), juce::Colour (0xff4fc3ff), juce::Colour (0xffb98cff), juce::Colour (0xff6fe3a1) }; return c[l]; }
    inline juce::Colour section (int t) { static const juce::Colour c[] { juce::Colour (0xff3a4a5c), juce::Colour (0xff6b5a2e), juce::Colour (0xffa0452f), juce::Colour (0xff2f5a6b), juce::Colour (0xff3d3d4a), juce::Colour (0xff6b3a52) }; return c[t]; }
}

static juce::Font af (float s, bool bold = true, float kern = 0) { return juce::Font (juce::FontOptions (s).withStyle (bold ? "Bold" : "Regular")).withExtraKerningFactor (kern); }

ArcLook::ArcLook()
{
    setColour (juce::TextButton::textColourOffId, ac::text);
    setColour (juce::TextButton::textColourOnId, ac::bg);
    setColour (juce::ComboBox::textColourId, ac::text);
    setColour (juce::PopupMenu::backgroundColourId, ac::panel);
    setColour (juce::PopupMenu::textColourId, ac::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, ac::acc);
    setColour (juce::PopupMenu::highlightedTextColourId, ac::bg);
}

void ArcLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    juce::Path tr, arcp; tr.addCentredArc (c.x, c.y, r - 2, r - 2, 0, a0, a1, true); arcp.addCentredArc (c.x, c.y, r - 2, r - 2, 0, a0, ang, true);
    g.setColour (ac::line); g.strokePath (tr, juce::PathStrokeType (3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (ac::acc); g.strokePath (arcp, juce::PathStrokeType (3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float body = r - 7;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2f37), c.x, c.y - body, juce::Colour (0xff121317), c.x, c.y + body, false));
    g.fillEllipse (c.x - body, c.y - body, body * 2, body * 2);
    juce::Path p; p.addRoundedRectangle (-1.4f, -body + 3, 2.8f, body * 0.48f, 1.4f);
    g.setColour (ac::text); g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void ArcLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1);
    const auto a = b.findColour (juce::TextButton::buttonOnColourId);
    g.setColour (a.withAlpha (down ? 0.4f : hi ? 0.25f : 0.13f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (a); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);
}

juce::Font ArcLook::getTextButtonFont (juce::TextButton&, int h) { return af (juce::jmin (11.5f, h * 0.48f), true, 0.08f); }

void ArcLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (ac::inset); g.fillRoundedRectangle (r, 4);
    g.setColour (ac::line.brighter (0.3f)); g.drawRoundedRectangle (r, 4, 1);
    juce::Path a; const float ax = (float) w - 10, ay = (float) h * 0.5f; a.addTriangle (ax - 3.5f, ay - 2, ax + 3.5f, ay - 2, ax, ay + 2.5f);
    g.setColour (ac::dim); g.fillPath (a);
}

juce::Font ArcLook::getComboBoxFont (juce::ComboBox&) { return af (12, true); }

void ArcLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    const auto col = b.findColour (juce::ToggleButton::tickColourId);
    auto box = r.withWidth (r.getHeight()).reduced (4);
    g.setColour (on ? col : ac::inset); g.fillRoundedRectangle (box, 3);
    g.setColour (on ? col : ac::dim); g.drawRoundedRectangle (box, 3, 1);
    g.setColour (on ? ac::text : ac::dim); g.setFont (af (11, true));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (r.getHeight() + 2), juce::Justification::centredLeft);
}

//==============================================================================
ArcEditor::ArcEditor (ArcProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    for (auto& t : arc::templates()) styleBox.addItem (t.name, styleBox.getNumItems() + 1);
    styleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ap::style, styleBox);
    addAndMakeVisible (styleBox);

    for (auto* k : { &variationKnob, &levelKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k->setPopupDisplayEnabled (true, true, this, 1200);
        addAndMakeVisible (k);
    }
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, ap::variation, variationKnob));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, ap::level, levelKnob));

    for (auto* b : { &regenButton, &exportButton, &folderButton }) { addAndMakeVisible (b); b->setColour (juce::TextButton::buttonOnColourId, ac::acc); }
    regenButton.onClick = [this] { proc.regenerate (true); };
    exportButton.onClick = [this] { auto files = proc.exportAll(); if (! files.isEmpty()) files[0].getParentDirectory().startAsProcess(); };
    folderButton.onClick = [this] { proc.exportFolder().startAsProcess(); };

    playButton.setColour (juce::TextButton::buttonOnColourId, ac::acc);
    playButton.setTooltip ("Play Arc's preview from the playhead (Space in Live still drives it when Live is playing)");
    playButton.onClick = [this] { proc.setPlaying (! proc.arcPlaying.load()); };
    addAndMakeVisible (playButton);
    previewToggle.setColour (juce::ToggleButton::tickColourId, ac::acc);
    addAndMakeVisible (previewToggle);
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ap::preview, previewToggle));

    for (int l = 0; l < arc::kNumLanes; ++l)
    {
        auto& lb = loadButtons[l];
        lb.setButtonText ("LOAD");
        lb.setColour (juce::TextButton::buttonOnColourId, ac::lane (l));
        lb.onClick = [this, l]
        {
            chooser = std::make_unique<juce::FileChooser> (juce::String ("Load a loop for ") + arc::laneName (l), juce::File(), "*.mid;*.midi;*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this, l] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadFile (l, fc.getResult()); });
        };
        addAndMakeVisible (lb);
        auto& cb = clearButtons[l];
        cb.setButtonText ("CLEAR");
        cb.setColour (juce::TextButton::buttonOnColourId, ac::dim);
        cb.onClick = [this, l] { proc.clearLane (l); };
        addAndMakeVisible (cb);
        auto& t = laneToggles[l];
        t.setButtonText ("On");
        t.setColour (juce::ToggleButton::tickColourId, ac::lane (l));
        addAndMakeVisible (t);
        bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ap::laneOn (l), t));
    }
    setSize (W, H);
    startTimerHz (20);
}

ArcEditor::~ArcEditor() { setLookAndFeel (nullptr); }

void ArcEditor::resized()
{
    styleBox.setBounds (300, 18, 230, 26);
    variationKnob.setBounds (560, 10, 42, 42);
    regenButton.setBounds (650, 18, 130, 26);
    exportButton.setBounds (790, 18, 100, 26);
    folderButton.setBounds (898, 18, 92, 26);
    levelKnob.setBounds (1060, 10, 42, 42);
    previewToggle.setBounds (16, 58, 230, 22);
    playButton.setBounds (312, 54, 70, 24);
    for (int l = 0; l < arc::kNumLanes; ++l)
    {
        auto r = laneRow (l);
        loadButtons[l].setBounds (r.getX() + 12, r.getY() + 70, 60, 26);
        clearButtons[l].setBounds (r.getX() + 78, r.getY() + 70, 60, 26);
        laneToggles[l].setBounds (r.getX() + 142, r.getY() + 72, 52, 22);
    }
}

bool ArcEditor::isInterestedInFileDrag (const juce::StringArray& f)
{
    for (auto& s : f) if (juce::File (s).hasFileExtension ("mid;midi;wav;aif;aiff;flac;mp3;m4a")) return true;
    return false;
}

int ArcEditor::laneAt (juce::Point<int> p) const
{
    for (int l = 0; l < arc::kNumLanes; ++l) if (laneRow (l).contains (p)) return l;
    return -1;
}

void ArcEditor::fileDragMove (const juce::StringArray&, int x, int y) { const int l = laneAt ({ x, y }); if (l != hoverLane) { hoverLane = l; repaint(); } }

void ArcEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int l = laneAt ({ x, y });
    hoverLane = -1;
    if (files.size() == 1) proc.loadFile (l, juce::File (files[0]));
    else for (auto& f : files) proc.loadFile (-1, juce::File (f));   // several at once: each goes to the lane it sounds like
}

float ArcEditor::xOfBeat (double beat) const
{
    auto a = proc.arrangement();
    const double total = juce::jmax (4.0, (a ? a->totalBars : 1) * 4.0);
    const double span = viewBeats > 0 ? viewBeats : total;
    return 312.0f + (float) ((beat - viewStart) / span) * 784.0f;
}

double ArcEditor::beatOfX (float x) const
{
    auto a = proc.arrangement();
    const double total = juce::jmax (4.0, (a ? a->totalBars : 1) * 4.0);
    const double span = viewBeats > 0 ? viewBeats : total;
    return juce::jlimit (0.0, total, viewStart + (x - 312.0) / 784.0 * span);
}

void ArcEditor::mouseDown (const juce::MouseEvent& e)
{
    dragLane = -1; dragStarted = false; nudgeLane = -1; nudgeApplied = 0; scrubbing = false;
    const auto p = e.getPosition();
    for (int l = 0; l < arc::kNumLanes; ++l)
    {
        if (dragHandle (l).contains (p)) dragLane = l;
        if (loopView (l).contains (p) && ! proc.lane (l).loop.empty()) nudgeLane = l;
    }
    if (dragLane < 0 && nudgeLane < 0 && timelineArea().contains (p))
    {
        // click a section name to jump to it; anywhere else to put the playhead there
        auto a = proc.arrangement();
        double target = beatOfX ((float) p.x);
        if (a && p.y >= 86 && p.y <= 110)
        {
            for (auto& sct : a->sections)
                if (target >= sct.startBar * 4.0 && target < (sct.startBar + sct.bars) * 4.0) { target = sct.startBar * 4.0; break; }
        }
        else
        {
            target = std::floor (target * 4.0 + 0.5) / 4.0;     // snap to the 16th
        }
        proc.setPosition (target);
        scrubbing = true;
        repaint();
    }
}

void ArcEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (scrubbing) { proc.setPosition (std::floor (beatOfX ((float) e.position.x) * 4.0 + 0.5) / 4.0); repaint(); return; }
    if (nudgeLane >= 0)
    {
        // drag the loop left/right in 16ths to choose which hit is the "1"
        const auto lp = proc.lane (nudgeLane);
        const double stepPx = (double) loopView (nudgeLane).getWidth() / juce::jmax (1.0, lp.loop.lengthBeats * 4.0);
        const int want = (int) std::round (-e.getDistanceFromDragStartX() / stepPx);
        if (want != nudgeApplied) { proc.nudgeLane (nudgeLane, want - nudgeApplied); nudgeApplied = want; }
        return;
    }
    if (dragLane < 0 || dragStarted || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    auto f = proc.exportLane (dragLane);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void ArcEditor::mouseUp (const juce::MouseEvent&) { scrubbing = false; nudgeLane = -1; }

void ArcEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    for (int l = 0; l < arc::kNumLanes; ++l)
        if (loopView (l).contains (p)) { const int back = -proc.lane (l).nudge; if (back != 0) proc.nudgeLane (l, back); return; }   // reset the start
    if (! timelineArea().contains (p)) return;
    // double-click a section: loop it (double-click again to stop looping)
    auto a = proc.arrangement();
    if (! a) return;
    const double b = beatOfX ((float) p.x);
    for (auto& sct : a->sections)
        if (b >= sct.startBar * 4.0 && b < (sct.startBar + sct.bars) * 4.0)
        {
            const double s0 = sct.startBar * 4.0, s1 = (sct.startBar + sct.bars) * 4.0;
            if (proc.loopEnd.load() == s1 && proc.loopStart.load() == s0) proc.setLoop (0, 0);
            else { proc.setLoop (s0, s1); proc.setPosition (s0); }
        }
    repaint();
}

void ArcEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! timelineArea().contains (e.getPosition())) return;
    auto a = proc.arrangement();
    const double total = juce::jmax (4.0, (a ? a->totalBars : 1) * 4.0);
    double span = viewBeats > 0 ? viewBeats : total;
    if (std::abs (w.deltaX) > std::abs (w.deltaY))
        viewStart -= w.deltaX * span * 0.5;                                  // sideways = scroll
    else
    {
        const double anchor = beatOfX (e.position.x);                         // zoom around the mouse
        span = juce::jlimit (16.0, total, span * (w.deltaY > 0 ? 0.8 : 1.25));
        viewStart = anchor - (e.position.x - 312.0) / 784.0 * span;
    }
    viewBeats = span >= total - 1e-6 ? 0 : span;
    viewStart = juce::jlimit (0.0, juce::jmax (0.0, total - span), viewStart);
    if (viewBeats == 0) viewStart = 0;
    repaint();
}

void ArcEditor::timerCallback()
{
    playButton.setButtonText (proc.arcPlaying.load() ? "STOP" : "PLAY");
    repaint();
}

void ArcEditor::drawLaneInfo (juce::Graphics& g, int l, const arc::Arrangement& a)
{
    auto r = laneRow (l);
    const auto src = proc.lane (l);
    const auto col = ac::lane (l);
    g.setColour (hoverLane == l ? col.withAlpha (0.18f) : ac::panel);
    g.fillRoundedRectangle (r.toFloat(), 8);
    if (hoverLane == l) { g.setColour (col); g.drawRoundedRectangle (r.toFloat().reduced (1), 8, 2); }
    g.setColour (col); g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 10, 4, (float) r.getHeight() - 20, 2);
    g.setColour (ac::text); g.setFont (af (14, true, 0.12f));
    g.drawText (juce::String (arc::laneName (l)).toUpperCase(), r.getX() + 14, r.getY() + 8, 180, 18, juce::Justification::centredLeft);
    g.setFont (af (10.5f, false));
    if (proc.busyLane.load() == l) { g.setColour (ac::acc); g.drawText ("Converting...", r.getX() + 14, r.getY() + 28, 270, 14, juce::Justification::centredLeft); }
    else if (src.source.isEmpty()) { g.setColour (ac::dim); g.drawText ("Drop a MIDI clip or an audio loop here", r.getX() + 14, r.getY() + 28, 270, 14, juce::Justification::centredLeft); }
    else
    {
        g.setColour (ac::text); g.drawText (src.source + "  ", r.getX() + 14, r.getY() + 26, 270, 14, juce::Justification::centredLeft, true);
        g.setColour (ac::dim); g.setFont (af (9.5f, false));
        g.drawText (src.report, r.getX() + 120, r.getY() + 8, 166, 14, juce::Justification::centredRight, true);
        // the loop itself: drag sideways to choose which hit is bar 1, double-click to reset
        auto lv = loopView (l).toFloat();
        g.setColour (ac::inset); g.fillRoundedRectangle (lv, 3);
        const double len = juce::jmax (1.0, src.loop.lengthBeats);
        for (int b = 0; b <= (int) len; ++b)
        {
            const float x = lv.getX() + (float) (b / len) * lv.getWidth();
            g.setColour (b % 4 == 0 ? ac::dim.withAlpha (0.6f) : ac::line);
            g.drawVerticalLine ((int) x, lv.getY(), lv.getBottom());
        }
        int lo = 127, hi = 0;
        for (auto& n : src.loop.notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
        for (auto& n : src.loop.notes)
        {
            const float x = lv.getX() + (float) (n.start / len) * lv.getWidth();
            const float w = juce::jmax (2.0f, (float) (n.length / len) * lv.getWidth());
            const float y = hi > lo ? lv.getBottom() - 3 - (lv.getHeight() - 6) * (float) (n.pitch - lo) / (float) (hi - lo) : lv.getCentreY();
            g.setColour (col.withAlpha (0.4f + 0.6f * n.velocity)); g.fillRect (x, y - 1, l == arc::laneDrums ? 2.0f : w, 2.0f);
        }
        g.setColour (ac::acc); g.fillRect (lv.getX(), lv.getY(), 2.0f, lv.getHeight());
        g.setFont (af (8, true)); g.drawText ("1", (int) lv.getX() + 4, (int) lv.getY() + 1, 10, 9, juce::Justification::left);
        if (src.nudge != 0) { g.setColour (ac::dim); g.drawText ((src.nudge > 0 ? "+" : "") + juce::String (src.nudge) + "/16", (int) lv.getRight() - 40, (int) lv.getY() + 1, 38, 9, juce::Justification::right); }
    }
    auto h = dragHandle (l).toFloat();
    const bool has = ! a.lanes[(size_t) l].empty();
    g.setColour (has ? col.withAlpha (0.2f) : ac::inset); g.fillRoundedRectangle (h, 13);
    g.setColour (has ? col : ac::line); g.drawRoundedRectangle (h, 13, 1.2f);
    g.setColour (has ? ac::text : ac::dim); g.setFont (af (10, true, 0.06f));
    g.drawText ("DRAG TO LIVE", h, juce::Justification::centred);
}

void ArcEditor::drawTimeline (juce::Graphics& g, const arc::Arrangement& a)
{
    const float x0 = 312, x1 = 1096;
    const int bars = juce::jmax (1, a.totalBars);
    auto xOf = [&] (double beat) { return xOfBeat (beat); };
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<int> ((int) x0, 66, (int) (x1 - x0), laneRow (3).getBottom() - 60));

    // sections
    for (auto& s : a.sections)
    {
        auto r = juce::Rectangle<float> (xOf (s.startBar * 4.0) + 1, 86, xOf ((s.startBar + s.bars) * 4.0) - xOf (s.startBar * 4.0) - 2, 24);
        g.setColour (ac::section (s.type)); g.fillRoundedRectangle (r, 4);
        g.setColour (ac::text); g.setFont (af (10, true, 0.06f));
        g.drawText (juce::String (arc::sectionName (s.type)).toUpperCase() + "  " + juce::String (s.bars), r.reduced (6, 0), juce::Justification::centredLeft, true);
        for (int l = 0; l < arc::kNumLanes; ++l)
        {
            auto lr = laneRow (l);
            g.setColour (ac::section (s.type).withAlpha (0.08f));
            g.fillRect (r.getX(), (float) lr.getY() + 4, r.getWidth(), (float) lr.getHeight() - 8);
        }
    }
    // lanes: notes as dashes, scaled to each lane's pitch range
    for (int l = 0; l < arc::kNumLanes; ++l)
    {
        auto lr = laneRow (l).toFloat();
        const auto& c = a.lanes[(size_t) l];
        int lo = 127, hi = 0;
        for (auto& n : c.notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
        const float top = lr.getY() + 22, bot = lr.getBottom() - 8;
        for (auto& n : c.notes)
        {
            const float y = hi > lo ? bot - (bot - top) * (float) (n.pitch - lo) / (float) (hi - lo) : (top + bot) * 0.5f;
            const float xa = xOf (n.start), xb = juce::jmax (xa + 1.0f, xOf (n.start + n.length));
            const bool special = n.tag.isNotEmpty();
            g.setColour ((special ? ac::text : ac::lane (l)).withAlpha (0.35f + 0.6f * n.velocity));
            g.fillRect (xa, y - 1, xb - xa, 2.0f);
        }
        // tags: small markers with the first few
        g.setFont (af (8.5f, true));
        float lastX = -100;
        for (auto& t : a.tags[(size_t) l])
        {
            const float x = xOf (t.bar * 4.0);
            g.setColour (ac::lane (l).withAlpha (0.9f));
            g.fillEllipse (x - 2, lr.getY() + 8, 4, 4);
            if (x - lastX > 64) { g.drawText (t.text, (int) x + 4, (int) lr.getY() + 3, 100, 12, juce::Justification::centredLeft, true); lastX = x; }
        }
    }
    // loop region, bar ruler, playhead
    if (proc.loopEnd.load() > proc.loopStart.load())
    {
        const float la = xOf (proc.loopStart.load()), lb = xOf (proc.loopEnd.load());
        g.setColour (ac::acc.withAlpha (0.12f)); g.fillRect (la, 70.0f, lb - la, 14.0f);
        g.setColour (ac::acc); g.drawRect (la, 70.0f, lb - la, 14.0f, 1.0f);
    }
    const double span = viewBeats > 0 ? viewBeats / 4.0 : bars;
    const int every = span > 96 ? 16 : span > 48 ? 8 : span > 16 ? 4 : 1;
    g.setColour (ac::dim); g.setFont (af (8.5f, false));
    for (int b = 0; b <= bars; b += every) g.drawText (juce::String (b + 1), (int) xOf (b * 4.0) - 12, 72, 24, 12, juce::Justification::centred);
    {
        const float x = xOf (proc.playPpq.load());
        if (x >= x0 && x <= x1)
        {
            g.setColour (proc.playing.load() ? ac::acc : ac::acc.withAlpha (0.6f));
            g.drawVerticalLine ((int) x, 70, (float) laneRow (3).getBottom());
            juce::Path tri; tri.addTriangle (x - 5, 70, x + 5, 70, x, 78); g.fillPath (tri);
        }
    }
    g.restoreState();
}

void ArcEditor::paint (juce::Graphics& g)
{
    g.fillAll (ac::bg);
    auto a = proc.arrangement();
    g.setColour (ac::text); g.setFont (af (26, true, 0.35f));
    g.drawText ("ARC", 16, 12, 120, 30, juce::Justification::centredLeft);
    g.setColour (ac::dim); g.setFont (af (9.5f, true, 0.2f));
    g.drawText ("LOOP TO FULL TRACK   |   TOOTA AUDIO", 17, 42, 280, 12, juce::Justification::centredLeft);
    g.drawText ("VARIATION", 540, 54, 80, 12, juce::Justification::centred);
    {
        const double pos = proc.playPpq.load();
        const int bar = (int) (pos / 4.0) + 1, beat = (int) std::fmod (pos, 4.0) + 1;
        g.setColour (ac::text); g.setFont (af (12, true));
        g.drawText (juce::String (bar) + "." + juce::String (beat), 390, 58, 60, 18, juce::Justification::centredLeft);
        if (proc.loopEnd.load() > proc.loopStart.load())
        {
            g.setColour (ac::acc); g.setFont (af (9, true));
            g.drawText ("LOOPING", 440, 60, 60, 14, juce::Justification::centredLeft);
        }
    }
    g.drawText ("PREVIEW", 1041, 54, 80, 12, juce::Justification::centred);
    if (a)
    {
        g.setColour (ac::acc); g.setFont (af (13, true));
        g.drawText (juce::String (cl::noteName (a->scale.tonic)) + (a->scale.minor ? " minor" : " major") + "   |   "
                    + juce::String (proc.hostBpm.load(), 1) + " BPM   |   " + juce::String (a->totalBars) + " bars ("
                    + juce::String (a->totalBars * 4.0 * 60.0 / juce::jmax (40.0, proc.hostBpm.load()) / 60.0, 1) + " min)",
                    600, 56, 420, 18, juce::Justification::centredRight);
        for (int l = 0; l < arc::kNumLanes; ++l) drawLaneInfo (g, l, *a);
        drawTimeline (g, *a);
        // tips
        auto t = juce::Rectangle<float> (16, 568, 1088, 140);
        g.setColour (ac::panel); g.fillRoundedRectangle (t, 8);
        g.setColour (ac::dim); g.setFont (af (9, true, 0.15f));
        g.drawText ("NOTES", (int) t.getX() + 14, (int) t.getY() + 8, 200, 12, juce::Justification::centredLeft);
        g.drawText ("CLICK THE TIMELINE TO MOVE   |   DOUBLE-CLICK A SECTION TO LOOP IT   |   SCROLL TO ZOOM   |   DRAG A LANE'S LOOP TO SET BAR 1",
                    (int) t.getRight() - 760, (int) t.getY() + 8, 746, 12, juce::Justification::centredRight);
        g.setColour (ac::text); g.setFont (af (11.5f, false));
        int y = (int) t.getY() + 26;
        if (a->tips.empty()) g.drawText ("Drop loops onto the lanes (MIDI or audio). Arc builds the full track; drag each lane into Live.", (int) t.getX() + 14, y, 1060, 16, juce::Justification::centredLeft);
        for (auto& s : a->tips) { g.drawText ("-  " + s, (int) t.getX() + 14, y, 1060, 16, juce::Justification::centredLeft, true); y += 19; if (y > t.getBottom() - 12) break; }
    }
}
