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
    const auto a = b.findColour (juce::TextButton::buttonOnColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f);
    if (b.getToggleState()) { g.setColour (a.withAlpha (down ? 0.75f : hi ? 0.9f : 1.0f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); return; }
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
using namespace arc2;

ArcEditor::ArcEditor (ArcProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    for (auto& s : styles()) styleBox.addItem (s.name, styleBox.getNumItems() + 1);
    styleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ap::style, styleBox);
    for (int i = 0; i < 12; ++i) rootBox.addItem (cl::noteName (i), i + 1);
    rootAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ap::root, rootBox);
    addAndMakeVisible (styleBox);
    addAndMakeVisible (rootBox);

    for (auto* k : { &variationKnob, &levelKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k->setPopupDisplayEnabled (true, true, this);
        addAndMakeVisible (k);
    }
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, ap::variation, variationKnob));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, ap::level, levelKnob));

    for (auto* b : { &keyButton, &scaleButton, &followButton }) b->setClickingTogglesState (true);
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ap::keyOn, keyButton));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ap::minor, scaleButton));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ap::follow, followButton));

    auto colour = [] (juce::TextButton& b, juce::Colour c) { b.setColour (juce::TextButton::buttonOnColourId, c); };
    colour (keyButton, ac::acc); colour (scaleButton, ac::dim); colour (detectButton, ac::dim);
    colour (generateButton, ac::acc); colour (undoButton, ac::dim); colour (redoButton, ac::dim);
    colour (playButton, juce::Colour (0xff6fe3a1)); colour (followButton, juce::Colour (0xff4fc3ff));
    colour (addButton, ac::text); colour (exportButton, ac::text);
    for (auto* b : { &keyButton, &scaleButton, &detectButton, &generateButton, &undoButton, &redoButton, &playButton, &followButton, &addButton, &exportButton })
        addAndMakeVisible (b);

    keyButton.setTooltip ("Key lock: bass, chords and lead are transposed into ROOT / SCALE");
    detectButton.onClick = [this] { proc.detectKey(); };
    generateButton.onClick = [this] { proc.generate(); };
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.onClick = [this] { proc.redo(); };
    playButton.onClick = [this]
    {
        if (proc.liveDriving.load()) { hint ("Live is playing, so Arc follows Live. Stop Live (or turn off FOLLOW LIVE) to use Arc's own transport."); return; }
        proc.setPlaying (! proc.arcPlaying.load());
    };
    addButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add stems (audio or MIDI)", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  juce::StringArray paths;
                                  for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                                  if (! paths.isEmpty()) proc.addFiles (paths);
                              });
    };
    exportButton.onClick = [this] { proc.exportAll (true); };

    setSize (W, H);
    startTimerHz (30);
    timerCallback();
}

ArcEditor::~ArcEditor() { setLookAndFeel (nullptr); }

int ArcEditor::rowH() const
{
    const int n = juce::jmax (6, (int) cache.size() + 1);
    return juce::jlimit (40, 60, (H - kTop - kFooter - 8) / n);
}

void ArcEditor::resized()
{
    int x = 112;
    auto place = [&x] (juce::Component& c, int w, int y = 18, int h = 28) { c.setBounds (x, y, w, h); x += w + 8; };
    place (keyButton, 52);
    place (rootBox, 58);
    place (scaleButton, 50);
    place (detectButton, 68);
    x += 6;
    place (styleBox, 100);
    variationKnob.setBounds (x, 10, 44, 44); x += 52;
    place (generateButton, 108, 14, 36);
    place (undoButton, 56);
    place (redoButton, 56);
    x += 6;
    place (playButton, 64);
    place (followButton, 96);
    levelKnob.setBounds (W - 162, 10, 44, 44);
    exportButton.setBounds (W - 108, 18, 96, 28);
    addButton.setBounds (12, kTop - 28, 120, 22);
}

float ArcEditor::xOfBeat (double beat) const
{
    const auto t = timeline();
    const double song = juce::jmax (4.0, planCache ? planCache->totalBars * 4.0 : 4.0);
    const double vb = viewBeats > 0 ? viewBeats : song;
    return (float) (t.getX() + (beat - viewStart) / vb * t.getWidth());
}

double ArcEditor::beatOfX (float x) const
{
    const auto t = timeline();
    const double song = juce::jmax (4.0, planCache ? planCache->totalBars * 4.0 : 4.0);
    const double vb = viewBeats > 0 ? viewBeats : song;
    return viewStart + (x - t.getX()) / t.getWidth() * vb;
}

int ArcEditor::rowAt (juce::Point<int> p) const
{
    for (int i = 0; i < (int) cache.size(); ++i)
    {
        auto r = row (i);
        if (p.y >= r.getY() && p.y < r.getY() + rowH()) return i;
    }
    return -1;
}

void ArcEditor::timerCallback()
{
    const int v = proc.version.load();
    if (v != lastVersion)
    {
        lastVersion = v;
        cache = proc.stems();
        planCache = proc.plan();
        undoButton.setEnabled (proc.canUndo());
        redoButton.setEnabled (proc.canRedo());
        repaint();
    }
    const bool p = proc.playing.load();
    playButton.setButtonText (proc.arcPlaying.load() && ! proc.liveDriving.load() ? "STOP" : "PLAY");
    scaleButton.setButtonText (scaleButton.getToggleState() ? "MIN" : "MAJ");
    if (p || proc.jobs.load() > 0) repaint();
}

// ---------------------------------------------------------------- painting
void ArcEditor::paint (juce::Graphics& g)
{
    g.fillAll (ac::bg);
    drawHeader (g);
    auto plan = planCache;
    if (cache.empty() || plan == nullptr) { drawEmpty (g); }
    else
    {
        for (int i = 0; i < (int) cache.size(); ++i) drawLane (g, i, cache[(size_t) i], *plan);
        drawTimeline (g, cache, *plan);
    }
    // drop target highlight
    if (dropRow >= -1)
    {
        g.setColour (ac::acc.withAlpha (0.15f));
        if (dropRow >= 0) g.fillRect (row (dropRow).expanded (2));
        else g.fillRect (juce::Rectangle<int> (8, kTop - 4, kLeftW, H - kTop - kFooter));
        g.setColour (ac::acc);
        g.setFont (af (12));
        g.drawText (dropRow >= 0 ? "Drop to replace this stem" : "Drop to add stems", juce::Rectangle<int> (8, H - kFooter - 26, kLeftW, 20), juce::Justification::centred);
    }
    // footer
    auto f = juce::Rectangle<int> (0, H - kFooter, W, kFooter);
    g.setColour (ac::panel); g.fillRect (f);
    g.setColour (ac::line); g.drawHorizontalLine (f.getY(), 0, (float) W);
    g.setFont (af (11.5f, false));
    g.setColour (ac::dim);
    juce::String msg = hintText.isNotEmpty() ? hintText : proc.message();
    if (proc.jobs.load() > 0) msg = "Working... " + msg;
    g.drawText (msg, f.reduced (14, 0).withTrimmedRight (180), juce::Justification::centredLeft);
    if (! cache.empty())
    {
        auto d = dragAllR();
        g.setColour (ac::acc.withAlpha (0.14f)); g.fillRoundedRectangle (d.toFloat(), 6);
        g.setColour (ac::acc); g.drawRoundedRectangle (d.toFloat(), 6, 1.2f);
        g.setFont (af (11)); g.drawText ("DRAG ALL TO LIVE", d, juce::Justification::centred);
    }
}

void ArcEditor::drawHeader (juce::Graphics& g)
{
    g.setColour (ac::panel); g.fillRect (0, 0, W, 64);
    g.setColour (ac::line); g.drawHorizontalLine (64, 0, (float) W);
    g.setColour (ac::acc); g.setFont (af (30, true, 0.1f)); g.drawText ("ARC", 16, 10, 90, 34, juce::Justification::centredLeft);
    g.setColour (ac::dim); g.setFont (af (9, true, 0.15f)); g.drawText ("STEM ARRANGER", 18, 42, 100, 12, juce::Justification::centredLeft);
    g.setFont (af (9, true, 0.12f));
    g.drawText ("VARIATION", variationKnob.getBounds().translated (-12, 0).withWidth (68).withY (52).withHeight (12), juce::Justification::centred);
    g.drawText ("LEVEL", levelKnob.getBounds().withY (52).withHeight (12), juce::Justification::centred);
    // position readout
    const double pos = proc.playPpq.load();
    const int bar = (int) std::floor (pos / 4.0) + 1, beat = (int) std::floor (std::fmod (pos, 4.0)) + 1;
    juce::String sec;
    if (planCache) { const int si = planCache->sectionAt ((int) (pos / 4.0)); if (si >= 0) sec = secName (planCache->sections[(size_t) si].type); }
    g.setColour (ac::text); g.setFont (af (15, true));
    const int px = followButton.getRight() + 14;
    g.drawText (juce::String (bar) + "." + juce::String (beat), px, 10, 84, 20, juce::Justification::centredLeft);
    g.setColour (ac::dim); g.setFont (af (9.5f, true, 0.08f));
    g.drawText (sec, px, 30, 84, 12, juce::Justification::centredLeft);
    g.drawText (juce::String (proc.preparedBpm(), 1) + " BPM", px, 43, 84, 12, juce::Justification::centredLeft);
    // column titles
    g.setColour (ac::dim); g.setFont (af (10, true, 0.15f));
    g.drawText ("STEMS  " + juce::String ((int) cache.size()) + " / 12", 142, kTop - 28, 200, 22, juce::Justification::centredLeft);
}

void ArcEditor::drawEmpty (juce::Graphics& g)
{
    auto r = juce::Rectangle<int> (12, kTop, W - 24, H - kTop - kFooter - 12);
    g.setColour (ac::panel); g.fillRoundedRectangle (r.toFloat(), 10);
    juce::Path dash; dash.addRoundedRectangle (r.reduced (20).toFloat(), 10);
    juce::PathStrokeType (1.4f).createDashedStroke (dash, dash, std::array<float, 2> { 7, 6 }.data(), 2);
    g.setColour (ac::line.brighter (0.4f)); g.fillPath (dash);
    g.setColour (ac::text); g.setFont (af (22, true));
    g.drawText ("Drop your stems here", r.withTrimmedBottom (90), juce::Justification::centred);
    g.setColour (ac::dim); g.setFont (af (13, false));
    g.drawFittedText ("Kick, bass, hats, claps, percussion, chords, lead, vocal, FX: drop a few loops or whole stems at once (or a folder).\n"
                      "Arc reads the names, finds the tempo and bar 1, locks them to Live's tempo, puts the tonal parts in key\n"
                      "and builds a full track: intro, build, drop, break, drop, outro, with rolls, fills and call-and-response.",
                      r.withTrimmedTop (60), juce::Justification::centred, 4);
}

void ArcEditor::drawLane (juce::Graphics& g, int i, const Stem& s, const Plan& plan)
{
    auto r = row (i);
    const auto col = roleColour (s.role);
    g.setColour (ac::panel); g.fillRoundedRectangle (r.toFloat(), 6);
    // role chip
    auto chip = chipR (i).toFloat().reduced (1);
    g.setColour (col.withAlpha (s.mute ? 0.25f : 0.9f)); g.fillRoundedRectangle (chip, 6);
    g.setColour (ac::bg); g.setFont (af (12, true, 0.06f));
    g.drawText (roleName (s.role), chip, juce::Justification::centred);
    // info
    auto info = infoR (i).reduced (0, 4);
    g.setColour (ac::text); g.setFont (af (12, true));
    g.drawText (s.name, info.removeFromTop (16), juce::Justification::centredLeft, true);
    g.setColour (ac::dim); g.setFont (af (10, false));
    g.drawText (s.report, info.removeFromTop (13), juce::Justification::centredLeft, true);
    if (info.getHeight() >= 11)
    {
        juce::String k;
        if (isTonalRole (s.role) && s.key.tonic >= 0)
        {
            k = juce::String (cl::noteName (s.key.tonic)) + (s.key.minor ? "m" : "");
            if (s.transpose != 0) k << "  shifted " << (s.transpose > 0 ? "+" : "") << s.transpose;
            else if (s.suggested != 0) k << "  (" << (s.suggested > 0 ? "+" : "") << s.suggested << " fits key)";
            else k << "  in key";
        }
        else k = s.fromMidi ? "MIDI" : (isDrumRole (s.role) ? "not retuned" : "");
        g.setColour (s.transpose != 0 ? ac::acc : ac::dim);
        g.drawText (k, info.removeFromTop (13), juce::Justification::centredLeft, true);
    }
    // waveform with the bar-1 marker and beat grid
    auto w = waveR (i);
    g.setColour (ac::inset); g.fillRoundedRectangle (w.toFloat(), 4);
    if (s.audio != nullptr && ! s.peaks.empty())
    {
        const int bars = s.audio->bars, steps = bars * 16;
        const float stepW = (float) w.getWidth() / steps;
        const float shift = drag == Drag::wave && dragRow == i ? (dragNudgeNow - dragNudgeBase) * stepW : 0.0f;
        g.saveState();
        g.reduceClipRegion (w);
        for (int b = 0; b < bars * 4; ++b)
        {
            const float x = w.getX() + b * 4 * stepW;
            g.setColour (b % 4 == 0 ? ac::line.brighter (0.5f) : ac::line);
            g.drawVerticalLine ((int) x, (float) w.getY(), (float) w.getBottom());
        }
        g.setColour (col.withAlpha (0.85f));
        const float mid = (float) w.getCentreY(), half = w.getHeight() * 0.46f;
        const int n = (int) s.peaks.size();
        for (int px = 0; px < w.getWidth(); ++px)
        {
            float src = (px - shift) / w.getWidth() * n;
            src = std::fmod (src, (float) n); if (src < 0) src += n;
            const float v = s.peaks[(size_t) juce::jlimit (0, n - 1, (int) src)];
            const float hgt = juce::jmin (1.0f, v) * half;
            g.drawVerticalLine (w.getX() + px, mid - hgt, mid + hgt);
        }
        g.restoreState();
        // bar 1
        g.setColour (ac::acc);
        g.fillRect (w.getX(), w.getY(), 2, w.getHeight());
        g.setFont (af (9, true)); g.drawText ("1", w.getX() + 4, w.getY() + 1, 10, 10, juce::Justification::topLeft);
        {
            const juce::String lbl = juce::String (bars) + (bars == 1 ? " BAR" : " BARS");
            auto lr = juce::Rectangle<int> (w.getRight() - 40, w.getY() + 1, 38, 11);
            g.setColour (ac::inset.withAlpha (0.8f)); g.fillRoundedRectangle (lr.toFloat(), 3);
            g.setColour (ac::dim); g.drawText (lbl, lr, juce::Justification::centred);
        }
        if (s.nudge != 0 || shift != 0)
        {
            const int nd = drag == Drag::wave && dragRow == i ? dragNudgeNow : s.nudge;
            g.setColour (ac::acc);
            g.drawText ((nd > 0 ? "+" : "") + juce::String (nd) + "/16", w.withTrimmedRight (4).withTrimmedTop (w.getHeight() - 12), juce::Justification::bottomRight);
        }
    }
    else
    {
        g.setColour (ac::dim); g.setFont (af (10, false));
        g.drawText ("preparing...", w, juce::Justification::centred);
    }
    // HOLD, dice, M, S, volume
    auto pill = [&g] (juce::Rectangle<int> b, const juce::String& t, bool on, juce::Colour c)
    {
        g.setColour (on ? c : ac::inset); g.fillRoundedRectangle (b.toFloat(), 4);
        g.setColour (on ? ac::bg : ac::dim); g.setFont (af (9.5f, true, 0.05f));
        g.drawText (t, b, juce::Justification::centred);
    };
    pill (holdR (i), "HOLD", s.hold, ac::acc);
    pill (diceR (i), "DICE", false, ac::text);
    pill (muteR (i), "M", s.mute, juce::Colour (0xffff6b5b));
    pill (soloR (i), "S", s.solo, juce::Colour (0xffffd166));
    auto v = volR (i).toFloat();
    g.setColour (ac::inset); g.fillRoundedRectangle (v, 4);
    const float frac = juce::jlimit (0.0f, 1.0f, (s.volumeDb + 36.0f) / 42.0f);
    g.setColour (col.withAlpha (0.6f)); g.fillRoundedRectangle (v.withWidth (v.getWidth() * frac), 4);
    g.setColour (ac::text); g.setFont (af (9, true));
    g.drawText (juce::String (s.volumeDb, 0) + " dB", v, juce::Justification::centred);
    // drag handle
    auto d = dragR (i).toFloat();
    g.setColour (col.withAlpha (0.16f)); g.fillRoundedRectangle (d, 5);
    g.setColour (col); g.drawRoundedRectangle (d.reduced (0.5f), 5, 1);
    juce::Path arrow;
    const auto c = d.getCentre();
    arrow.startNewSubPath (c.x - 7, c.y); arrow.lineTo (c.x + 6, c.y);
    arrow.startNewSubPath (c.x + 1, c.y - 5); arrow.lineTo (c.x + 6, c.y); arrow.lineTo (c.x + 1, c.y + 5);
    g.strokePath (arrow, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::ignoreUnused (plan);
}

void ArcEditor::drawTimeline (juce::Graphics& g, const std::vector<Stem>& st, const Plan& plan)
{
    const auto t = timeline();
    g.setColour (ac::panel); g.fillRoundedRectangle (t.toFloat(), 6);
    g.saveState();
    g.reduceClipRegion (t);
    const float pxPerBar = xOfBeat (4) - xOfBeat (0);
    // sections
    const auto strip = sectionStrip();
    for (auto& s : plan.sections)
    {
        const float x0 = xOfBeat (s.start * 4.0), x1 = xOfBeat ((s.start + s.bars) * 4.0);
        g.setColour (secColour (s.type)); g.fillRect (juce::Rectangle<float> (x0, (float) strip.getY(), x1 - x0 - 1, (float) strip.getHeight()));
        g.setColour (secColour (s.type).withAlpha (0.12f)); g.fillRect (juce::Rectangle<float> (x0, (float) strip.getBottom(), x1 - x0 - 1, (float) (t.getBottom() - strip.getBottom())));
        g.setColour (ac::text); g.setFont (af (10, true, 0.1f));
        if (x1 - x0 > 30) g.drawText (juce::String (secName (s.type)) + "  " + juce::String (s.bars), juce::Rectangle<float> (x0 + 5, (float) strip.getY(), x1 - x0 - 6, (float) strip.getHeight()), juce::Justification::centredLeft, true);
    }
    // bar lines (every 4 or 8 bars depending on zoom)
    const int every = pxPerBar > 14 ? 1 : pxPerBar > 5 ? 4 : 8;
    for (int b = 0; b <= plan.totalBars; b += every)
    {
        const float x = xOfBeat (b * 4.0);
        g.setColour (b % 8 == 0 ? ac::line.brighter (0.25f) : ac::line);
        g.drawVerticalLine ((int) x, (float) strip.getBottom(), (float) t.getBottom());
    }
    // loop range
    const double ls = proc.loopStart.load(), le = proc.loopEnd.load();
    if (le > ls + 0.01)
    {
        const float x0 = xOfBeat (ls), x1 = xOfBeat (le);
        g.setColour (juce::Colour (0xff6fe3a1).withAlpha (0.1f)); g.fillRect (juce::Rectangle<float> (x0, (float) strip.getBottom(), x1 - x0, (float) (t.getBottom() - strip.getBottom())));
        g.setColour (juce::Colour (0xff6fe3a1)); g.fillRect (juce::Rectangle<float> (x0, (float) strip.getBottom(), x1 - x0, 3.0f));
    }
    // lane activity
    for (int i = 0; i < (int) st.size() && i < (int) plan.lanes.size(); ++i)
    {
        auto r = row (i);
        const auto col = roleColour (st[(size_t) i].role);
        const auto& lp = plan.lanes[(size_t) i];
        const float y0 = (float) r.getY() + 4, h = (float) r.getHeight() - 8;
        for (int b = 0; b < (int) lp.bars.size(); ++b)
        {
            const auto& B = lp.bars[(size_t) b];
            const float x0 = xOfBeat (b * 4.0), x1 = xOfBeat (b * 4.0 + 4.0);
            if (x1 < t.getX() || x0 > t.getRight() || ! B.on) continue;
            const float gAvg = juce::jlimit (0.15f, 1.0f, 0.5f * (B.g0 + B.g1));
            const float a = (st[(size_t) i].mute ? 0.2f : 0.35f + 0.5f * gAvg);
            if (pxPerBar >= 40)
            {
                // per 16th detail: gaps where muted, brighter where edited
                const float sw = (x1 - x0) / 16;
                for (int k = 0; k < 16; ++k)
                {
                    const auto& op = B.steps[(size_t) k];
                    if (op.mute) continue;
                    const bool edited = op.rep > 1 || op.rev || op.span > 1;
                    g.setColour (edited ? col.brighter (0.6f).withAlpha (0.95f) : col.withAlpha (a * (0.6f + 0.4f * op.gain)));
                    g.fillRect (juce::Rectangle<float> (x0 + k * sw, y0, juce::jmax (1.0f, sw - 0.6f), h));
                }
            }
            else
            {
                g.setColour (col.withAlpha (a));
                g.fillRect (juce::Rectangle<float> (x0, y0, juce::jmax (1.0f, x1 - x0 - (pxPerBar > 4 ? 1.0f : 0.0f)), h));
            }
            // filters: shade the top for a low-pass, the bottom for a high-pass
            const float lpAvg = std::sqrt (B.lp0 * B.lp1), hpAvg = std::sqrt (B.hp0 * B.hp1);
            if (lpAvg < 15000)
            {
                const float amt = juce::jlimit (0.0f, 0.85f, std::log2 (15000.0f / lpAvg) / 5.5f);
                g.setColour (ac::bg.withAlpha (0.7f)); g.fillRect (juce::Rectangle<float> (x0, y0, x1 - x0, h * amt));
            }
            if (hpAvg > 40)
            {
                const float amt = juce::jlimit (0.0f, 0.85f, std::log2 (hpAvg / 40.0f) / 6.0f);
                g.setColour (ac::bg.withAlpha (0.7f)); g.fillRect (juce::Rectangle<float> (x0, y0 + h * (1 - amt), x1 - x0, h * amt));
            }
            if (B.fill != fNone && B.fill != fSweep && pxPerBar > 3)
            {
                juce::Path tri; tri.addTriangle (x1 - 7, y0, x1 - 1, y0, x1 - 1, y0 + 6);
                g.setColour (ac::text); g.fillPath (tri);
                if (pxPerBar > 46) { g.setFont (af (8.5f, true)); g.drawText (fillName (B.fill), juce::Rectangle<float> (x0 + 2, y0 + h - 11, x1 - x0 - 4, 10), juce::Justification::bottomRight); }
            }
        }
    }
    // playhead
    const double pos = proc.playPpq.load();
    const float px = xOfBeat (pos);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillRect (juce::Rectangle<float> (px - 0.75f, (float) t.getY(), 1.5f, (float) t.getHeight()));
    juce::Path head; head.addTriangle (px - 5, (float) t.getY(), px + 5, (float) t.getY(), px, (float) t.getY() + 7);
    g.fillPath (head);
    g.restoreState();
    g.setColour (ac::line); g.drawRoundedRectangle (t.toFloat(), 6, 1);
}

// ---------------------------------------------------------------- files in
bool ArcEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const juce::File fl (f);
        if (fl.isDirectory() || fl.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;mid;midi")) return true;
    }
    return false;
}

void ArcEditor::fileDragMove (const juce::StringArray& files, int x, int y)
{
    const int r = x < kLeftW ? rowAt ({ x, y }) : -1;
    const int nd = files.size() == 1 && ! juce::File (files[0]).isDirectory() ? r : -1;
    if (nd != dropRow) { dropRow = nd; repaint(); }
}

void ArcEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int r = x < kLeftW ? rowAt ({ x, y }) : -1;
    dropRow = -2;
    if (r >= 0 && files.size() == 1 && ! juce::File (files[0]).isDirectory()) proc.replaceStem (r, juce::File (files[0]));
    else proc.addFiles (files);
    repaint();
}

// ---------------------------------------------------------------- mouse
void ArcEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int i = rowAt (p);
    if (timeline().contains (p))
        hint (sectionStrip().contains (p) ? "Click a section to jump there, double-click to loop it (double-click again to unloop). Scroll to zoom, shift-scroll to move."
                                          : "Click or drag to move the playhead. Scroll to zoom in on the fills.");
    else if (dragAllR().contains (p)) hint ("Drag onto Live's arrangement: every lane lands on its own track, already arranged.");
    else if (i >= 0 && waveR (i).contains (p)) hint ("Drag sideways to move where bar 1 is (in 16ths). Double-click to reset. The orange line is bar 1.");
    else if (i >= 0 && chipR (i).contains (p)) hint ("Click to change the role, crop, transpose, replace or remove this stem.");
    else if (i >= 0 && dragR (i).contains (p)) hint ("Drag this lane into Live as an arranged audio clip (full length, starts at bar 1).");
    else if (i >= 0 && holdR (i).contains (p)) hint ("HOLD keeps this lane's variations when you press GENERATE.");
    else if (i >= 0 && diceR (i).contains (p)) hint ("New fills and variations for this lane only.");
    else if (i >= 0 && volR (i).contains (p)) hint ("Drag for lane volume, double-click for 0 dB.");
    else hint ({});
}

void ArcEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    dragOutStarted = false;
    drag = Drag::none;
    const int i = rowAt (p);
    if (i >= 0 && p.x < kLeftW)
    {
        const auto& s = cache[(size_t) i];
        if (chipR (i).contains (p)) { laneMenu (i); return; }
        if (holdR (i).contains (p)) { proc.setHold (i, ! s.hold); return; }
        if (diceR (i).contains (p)) { proc.dice (i); return; }
        if (muteR (i).contains (p)) { proc.setMute (i, ! s.mute); return; }
        if (soloR (i).contains (p)) { proc.setSolo (i, ! s.solo); return; }
        if (volR (i).contains (p)) { drag = Drag::volume; dragRow = i; dragVolStart = s.volumeDb; proc.beginVolumeGesture(); return; }
        if (waveR (i).contains (p) && s.audio != nullptr) { drag = Drag::wave; dragRow = i; dragNudgeBase = dragNudgeNow = s.nudge; return; }
        if (dragR (i).contains (p)) { dragRow = i; return; }
        return;
    }
    if (dragAllR().contains (p) && ! cache.empty()) { dragRow = -10; return; }
    const auto t = timeline();
    if (t.contains (p) && planCache)
    {
        if (e.mods.isPopupMenu() || e.mods.isAltDown()) { drag = Drag::timelinePan; panStart = viewStart; return; }
        if (sectionStrip().contains (p))
        {
            const int bar = (int) (beatOfX ((float) p.x) / 4.0);
            const int si = planCache->sectionAt (bar);
            if (si >= 0) proc.setPosition (planCache->sections[(size_t) si].start * 4.0);
        }
        else
        {
            drag = Drag::scrub;
            proc.setPosition (juce::jmax (0.0, std::floor (beatOfX ((float) p.x))));
        }
        if (proc.liveDriving.load()) hint ("Live is playing, so Arc follows Live's position. Stop Live to move Arc's playhead.");
        proc.playPpq = proc.arcPos.load();
        repaint();
    }
}

void ArcEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (drag == Drag::volume && dragRow >= 0)
    {
        const float db = juce::jlimit (-36.0f, 6.0f, dragVolStart + e.getDistanceFromDragStartX() * 0.25f);
        proc.setVolume (dragRow, db);
        return;
    }
    if (drag == Drag::wave && dragRow >= 0 && dragRow < (int) cache.size() && cache[(size_t) dragRow].audio != nullptr)
    {
        const int steps = cache[(size_t) dragRow].audio->bars * 16;
        const float stepW = (float) waveR (dragRow).getWidth() / steps;
        dragNudgeNow = dragNudgeBase - juce::roundToInt (e.getDistanceFromDragStartX() / stepW);
        repaint (waveR (dragRow));
        return;
    }
    if (drag == Drag::scrub)
    {
        proc.setPosition (juce::jmax (0.0, std::floor (beatOfX ((float) p.x) * 4.0) / 4.0));
        proc.playPpq = proc.arcPos.load();
        repaint();
        return;
    }
    if (drag == Drag::timelinePan && planCache)
    {
        const double song = planCache->totalBars * 4.0, vb = viewBeats > 0 ? viewBeats : song;
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, song - vb), panStart - e.getDistanceFromDragStartX() / (double) timeline().getWidth() * vb);
        repaint();
        return;
    }
    // drag a lane (or all) out into Live
    if (! dragOutStarted && e.getDistanceFromDragStart() > 6 && (dragRow == -10 || (dragRow >= 0 && dragRow < (int) cache.size() && dragR (dragRow).contains (e.getMouseDownPosition()))))
    {
        dragOutStarted = true;
        const auto folder = proc.exportFolder().getChildFile ("Dragged").getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"));
        juce::Array<juce::File> files;
        if (dragRow == -10) { for (int i = 0; i < (int) cache.size(); ++i) if (auto f = proc.exportLane (i, folder); f.existsAsFile()) files.add (f); }
        else if (auto f = proc.exportLane (dragRow, folder); f.existsAsFile()) files.add (f);
        dragOut (files);
    }
}

void ArcEditor::dragOut (const juce::Array<juce::File>& files)
{
    if (files.isEmpty()) return;
    juce::StringArray paths;
    for (auto& f : files) paths.add (f.getFullPathName());
    juce::DragAndDropContainer::performExternalDragDropOfFiles (paths, false, this);
    hint ("Drop it at bar 1 of Live's arrangement. Files are kept in Music > Arc Exports > Dragged.");
}

void ArcEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::wave && dragRow >= 0 && dragNudgeNow != dragNudgeBase) proc.setNudge (dragRow, dragNudgeNow);
    drag = Drag::none;
    dragRow = -1;
}

void ArcEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int i = rowAt (p);
    if (i >= 0 && waveR (i).contains (p)) { proc.setNudge (i, 0); return; }
    if (i >= 0 && volR (i).contains (p)) { proc.setVolume (i, 0, true); return; }
    if (planCache && timeline().contains (p))
    {
        const int bar = (int) (beatOfX ((float) p.x) / 4.0);
        const int si = planCache->sectionAt (bar);
        if (si < 0) return;
        const auto& s = planCache->sections[(size_t) si];
        const double a = s.start * 4.0, b = (s.start + s.bars) * 4.0;
        if (std::abs (proc.loopStart.load() - a) < 1e-6 && std::abs (proc.loopEnd.load() - b) < 1e-6) { proc.setLoop (0, 0); hint ("Loop off"); }
        else { proc.setLoop (a, b); proc.setPosition (a); hint (juce::String ("Looping ") + secName (s.type) + " (bars " + juce::String (s.start + 1) + "-" + juce::String (s.start + s.bars) + "). Double-click again to stop looping."); }
        repaint();
    }
}

void ArcEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! timeline().contains (e.getPosition()) || planCache == nullptr) return;
    const double song = planCache->totalBars * 4.0;
    double vb = viewBeats > 0 ? viewBeats : song;
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, song - vb), viewStart - d * vb * 0.5);
    }
    else
    {
        const double anchor = beatOfX ((float) e.x);
        const double nb = juce::jlimit (8.0, song, vb * (w.deltaY > 0 ? 0.8 : 1.25));
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, song - nb), anchor - (anchor - viewStart) * nb / vb);
        viewBeats = nb >= song - 1e-6 ? 0.0 : nb;
        if (viewBeats == 0) viewStart = 0;
    }
    repaint();
}

// ---------------------------------------------------------------- lane menu
void ArcEditor::laneMenu (int i)
{
    if (i < 0 || i >= (int) cache.size()) return;
    const auto& s = cache[(size_t) i];
    juce::PopupMenu m, roles, crop, tp;
    for (int r = 0; r < kRoles; ++r) roles.addItem (100 + r, roleName (r), true, r == s.role);
    const int src = sourceBars (s);
    crop.addItem (200, "Whole loop" + juce::String (src > 8 ? " (first 8 bars)" : ""), true, s.cropBars == 0);
    for (int len : { 1, 2, 4, 8 })
        if (len < src || (len == src && src > 8))
            for (int st = 0; st + len <= juce::jmin (src, 32); st += len)
                crop.addItem (1000 + len * 100 + st, "Bars " + juce::String (st + 1) + (len > 1 ? "-" + juce::String (st + len) : juce::String()), true, s.cropBars == len && s.cropStart == st);
    tp.addItem (500, "Auto (follow KEY)", true, s.manualTranspose == Stem::kAuto);
    if (s.suggested != 0) tp.addItem (501, "Into key: " + juce::String (s.suggested > 0 ? "+" : "") + juce::String (s.suggested), true, s.manualTranspose == s.suggested);
    for (int k = -7; k <= 7; ++k) tp.addItem (520 + k, (k > 0 ? "+" : "") + juce::String (k) + " semitones", true, s.manualTranspose == k);
    m.addSectionHeader (s.name);
    m.addSubMenu ("Role", roles);
    m.addSubMenu ("Crop", crop);
    m.addSubMenu ("Transpose", tp, s.role != rKick);
    m.addItem (600, "Reset bar 1 (nudge)", s.nudge != 0);
    m.addSeparator();
    m.addItem (700, "Replace...");
    m.addItem (800, "Remove");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (chipR (i))), [this, i, s] (int r)
    {
        if (r >= 100 && r < 100 + kRoles) proc.setRole (i, r - 100);
        else if (r == 200) proc.setCrop (i, 0, 0);
        else if (r >= 1000) { const int len = (r - 1000) / 100, st = (r - 1000) % 100; proc.setCrop (i, st, len); }
        else if (r == 500) proc.setManualTranspose (i, Stem::kAuto);
        else if (r == 501) proc.setManualTranspose (i, s.suggested);
        else if (r >= 513 && r <= 527) proc.setManualTranspose (i, r - 520);
        else if (r == 600) proc.setNudge (i, 0);
        else if (r == 700)
        {
            chooser = std::make_unique<juce::FileChooser> ("Replace stem", juce::File (s.path).getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, i] (const juce::FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) proc.replaceStem (i, fc.getResult());
            });
        }
        else if (r == 800) proc.removeStem (i);
    });
}
