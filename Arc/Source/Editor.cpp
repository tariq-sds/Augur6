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
    previewToggle.setBounds (16, 58, 270, 22);
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

void ArcEditor::mouseDown (const juce::MouseEvent& e)
{
    dragLane = -1; dragStarted = false;
    for (int l = 0; l < arc::kNumLanes; ++l) if (dragHandle (l).contains (e.getPosition())) dragLane = l;
}

void ArcEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane < 0 || dragStarted || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    auto f = proc.exportLane (dragLane);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void ArcEditor::timerCallback() { repaint(); }

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
        g.setColour (ac::text); g.drawText (src.source, r.getX() + 14, r.getY() + 28, 270, 14, juce::Justification::centredLeft, true);
        g.setColour (ac::dim); g.drawText (src.report, r.getX() + 14, r.getY() + 44, 270, 14, juce::Justification::centredLeft, true);
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
    auto xOf = [&] (double beat) { return x0 + (float) (beat / (bars * 4.0)) * (x1 - x0); };

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
    // bar ruler + playhead
    g.setColour (ac::dim); g.setFont (af (8.5f, false));
    for (int b = 0; b <= bars; b += 16) g.drawText (juce::String (b + 1), (int) xOf (b * 4.0) - 12, 72, 24, 12, juce::Justification::centred);
    if (proc.playing.load())
    {
        const float x = xOf (proc.playPpq.load());
        if (x >= x0 && x <= x1) { g.setColour (ac::acc); g.drawVerticalLine ((int) x, 84, (float) laneRow (3).getBottom()); }
    }
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
        g.setColour (ac::text); g.setFont (af (11.5f, false));
        int y = (int) t.getY() + 26;
        if (a->tips.empty()) g.drawText ("Drop loops onto the lanes (MIDI or audio). Arc builds the full track; drag each lane into Live.", (int) t.getX() + 14, y, 1060, 16, juce::Justification::centredLeft);
        for (auto& s : a->tips) { g.drawText ("-  " + s, (int) t.getX() + 14, y, 1060, 16, juce::Justification::centredLeft, true); y += 19; if (y > t.getBottom() - 12) break; }
    }
}
