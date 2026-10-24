#include "Editor.h"

using namespace arc2;
namespace tt = toota;

// lane colours: warm, brand-friendly, each role distinct
static juce::Colour laneColour (int role)
{
    static const juce::uint32 c[] { 0xfffd694c, 0xff0294aa, 0xfffe9e3b, 0xffe9789f, 0xff7d9a3a, 0xffd9b23a,
                                    0xffc9472f, 0xff8a6bbe, 0xff2bb59a, 0xff8a4f7d, 0xff9c8b7a };
    return juce::Colour (c[juce::jlimit (0, kRoles - 1, role)]);
}
static juce::Colour sectionColour (int t)
{
    static const juce::uint32 c[] { 0xff7cc6d0, 0xffb7c98a, 0xfffed09d, 0xfffd694c, 0xff0294aa, 0xffc9a6c2 };
    return juce::Colour (c[juce::jlimit (0, 5, t)]);
}
static const char* varyName (int v) { return v <= 0 ? "STRAIGHT" : v == 1 ? "SOME" : "LOTS"; }

//==============================================================================
ArcEditor::ArcEditor (ArcProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    for (auto& s : styles()) styleBox.addItem (s.name, styleBox.getNumItems() + 1);
    styleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ap::style, styleBox);
    for (int i = 0; i < 12; ++i) rootBox.addItem (cl::noteName (i), i + 1);
    rootAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ap::root, rootBox);
    styleBox.setTooltip ("Arrangement style: how many bars of intro, build, drop, break and outro");
    rootBox.setTooltip ("The key's root note. With KEY LOCK on, bass, chords and lead are moved into it.");
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
    variationKnob.setColour (juce::Slider::rotarySliderFillColourId, tt::coral);
    levelKnob.setColour (juce::Slider::rotarySliderFillColourId, tt::teal);
    variationKnob.setTooltip ("How many fills, rolls and changes the lanes that are allowed to vary get");
    levelKnob.setTooltip ("Arc's output level");
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, ap::variation, variationKnob));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, ap::level, levelKnob));

    keyButton.setClickingTogglesState (true);
    scaleButton.setClickingTogglesState (true);
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ap::keyOn, keyButton));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ap::minor, scaleButton));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ap::follow, followToggle));
    keyButton.setColour (juce::TextButton::buttonOnColourId, tt::teal);
    scaleButton.setColour (juce::TextButton::buttonOnColourId, tt::cream);
    scaleButton.setColour (juce::TextButton::textColourOnId, tt::ink);
    generateButton.setColour (juce::TextButton::buttonColourId, tt::coral);
    generateButton.setColour (juce::TextButton::textColourOffId, tt::cream);
    playButton.setColour (juce::TextButton::buttonColourId, tt::teal);
    playButton.setColour (juce::TextButton::textColourOffId, tt::cream);
    addButton.setColour (juce::TextButton::buttonColourId, tt::orange);
    captureButton.setColour (juce::TextButton::buttonColourId, tt::teal);
    captureButton.setColour (juce::TextButton::textColourOffId, tt::cream);
    captureButton.setTooltip ("Record your tracks straight from Live while it plays: put Arc Tap on each track you want, press this, then play. No bouncing.");
    addButton.setTooltip ("Add audio files, a folder, MIDI or a saved Live set (.als)");
    followToggle.setColour (juce::ToggleButton::tickColourId, tt::teal);
    keyButton.setTooltip ("Key lock: keep bass, chords and lead in the key below. Drums are never retuned.");
    scaleButton.setTooltip ("Minor or major");
    detectButton.setTooltip ("Read the key from your bass, chord and lead stems");
    generateButton.setTooltip ("Build the arrangement again with new fills and variations (lanes set to STRAIGHT stay as they are)");
    followToggle.setTooltip ("When Live is playing, Arc plays along at Live's position. Turn off to use only Arc's own transport.");
    exportButton.setTooltip ("Write every lane and a mix, full length, to Music > Arc Exports");
    for (juce::Component* c : { (juce::Component*) &keyButton, (juce::Component*) &scaleButton, (juce::Component*) &detectButton, (juce::Component*) &generateButton,
                                (juce::Component*) &undoButton, (juce::Component*) &redoButton, (juce::Component*) &playButton, (juce::Component*) &addButton, (juce::Component*) &captureButton,
                                (juce::Component*) &exportButton, (juce::Component*) &followToggle })
        addAndMakeVisible (c);

    detectButton.onClick = [this] { proc.detectKey(); };
    generateButton.onClick = [this] { proc.generate(); };
    undoButton.onClick = [this] { proc.undo(); };
    redoButton.onClick = [this] { proc.redo(); };
    playButton.onClick = [this]
    {
        if (proc.liveDriving.load()) { hint ("Live is playing, so Arc is following Live. Stop Live to use Arc's own PLAY."); return; }
        proc.setPlaying (! proc.arcPlaying.load());
    };
    addButton.onClick = [this] { openChooser(); };
    captureButton.onClick = [this] { if (proc.capturing()) proc.cancelCapture(); else captureMenu(); };
    exportButton.onClick = [this] { proc.exportAll (true); };

    setSize (W, H);
    startTimerHz (30);
    timerCallback();
}

ArcEditor::~ArcEditor() = default;

void ArcEditor::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Add stems: audio, MIDI or a Live set", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi;*.als");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              juce::StringArray paths;
                              for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                              if (! paths.isEmpty()) proc.addFiles (paths);
                          });
}

void ArcEditor::captureMenu()
{
    const auto taps = proc.taps();
    juce::PopupMenu m;
    if (taps.empty())
    {
        m.addSectionHeader ("No Arc Tap found yet");
        m.addItem (-1, "Add Arc Tap (Audio Effects > Plug-ins) to each track", false, false);
        m.addItem (-1, "you want in Arc, then come back here", false, false);
    }
    else
    {
        m.addSectionHeader ("Tracks with Arc Tap (" + juce::String ((int) taps.size()) + ")");
        for (auto& t : taps) m.addItem (-1, t.name, false, false);
    }
    m.addSeparator();
    m.addSectionHeader ("Record from the next bar while Live plays");
    for (int b : { 4, 8, 16, 32 }) m.addItem (b, "Capture " + juce::String (b) + " bars");
    juce::Component::SafePointer<ArcEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&captureButton).withMinimumWidth (300),
                     [safe] (int r) { if (safe != nullptr && r > 0) safe->proc.startCapture (r); });
}

int ArcEditor::rowH() const
{
    const int n = juce::jmax (6, (int) cache.size());
    return juce::jlimit (40, 66, (H - kTop - kFooter - 10) / n);
}

juce::Rectangle<int> ArcEditor::step (int n) const
{
    if (n == 1) return { 12, kSteps, 300, kStepsH };
    if (n == 2) return { 322, kSteps, 660, kStepsH };
    return { 992, kSteps, W - 12 - 992, kStepsH };
}

void ArcEditor::resized()
{
    // header: key, undo / redo, transport, follow, level
    keyButton.setBounds (318, 22, 92, 34);
    rootBox.setBounds (416, 22, 60, 34);
    scaleButton.setBounds (482, 22, 72, 34);
    detectButton.setBounds (560, 22, 74, 34);
    undoButton.setBounds (656, 22, 64, 34);
    redoButton.setBounds (724, 22, 64, 34);
    playButton.setBounds (808, 18, 88, 40);
    followToggle.setBounds (1036, 26, 120, 24);
    levelKnob.setBounds (W - 62, 12, 50, 50);
    // step 1
    { auto s = step (1); addButton.setBounds (s.getRight() - 118, s.getY() + 6, 106, 26); captureButton.setBounds (s.getRight() - 118, s.getY() + 35, 106, 26); }
    // step 2
    {
        auto s = step (2);
        styleBox.setBounds (s.getX() + 170, s.getY() + 17, 150, 34);
        variationKnob.setBounds (s.getX() + 400, s.getY() + 8, 50, 50);
        generateButton.setBounds (s.getRight() - 16 - 150, s.getY() + 12, 150, 44);
    }
    // step 3
    { auto s = step (3); exportButton.setBounds (s.getRight() - 76, s.getY() + 17, 66, 34); }
}

float ArcEditor::xOfBeat (double beat) const
{
    const auto t = timeline();
    const double song = juce::jmax (4.0, planCache ? planCache->totalBars * 4.0 : 4.0);
    const double vb = viewBeats > 0 ? viewBeats : song;
    return (float) (t.getX() + 6 + (beat - viewStart) / vb * (t.getWidth() - 12));
}

double ArcEditor::beatOfX (float x) const
{
    const auto t = timeline();
    const double song = juce::jmax (4.0, planCache ? planCache->totalBars * 4.0 : 4.0);
    const double vb = viewBeats > 0 ? viewBeats : song;
    return viewStart + (x - t.getX() - 6) / (t.getWidth() - 12) * vb;
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
        const bool any = ! cache.empty();
        for (juce::Component* c : { (juce::Component*) &generateButton, (juce::Component*) &exportButton, (juce::Component*) &detectButton, (juce::Component*) &playButton })
            c->setEnabled (any);
        repaint();
    }
    const bool own = proc.arcPlaying.load() && ! proc.liveDriving.load();
    playButton.setButtonText (proc.liveDriving.load() ? "LIVE" : own ? "STOP" : "PLAY");
    playButton.setColour (juce::TextButton::buttonColourId, own ? tt::coral : tt::teal);
    const bool capOn = proc.capturing();
    captureButton.setButtonText (capOn ? "STOP CAPTURE" : "CAPTURE LIVE");
    captureButton.setColour (juce::TextButton::buttonColourId, capOn ? tt::coral : tt::teal);
    if (capOn) repaint (0, H - kFooter, W, kFooter);
    scaleButton.setButtonText (scaleButton.getToggleState() ? "MINOR" : "MAJOR");
    if (proc.playing.load() || proc.jobs.load() > 0) repaint();
}

// ---------------------------------------------------------------- painting
void ArcEditor::paint (juce::Graphics& g)
{
    g.fillAll (tt::cream);
    drawHeader (g);
    drawSteps (g);
    auto plan = planCache;
    if (cache.empty() || plan == nullptr) drawEmpty (g);
    else
    {
        tt::label (g, "YOUR STEMS", { kLeftX + 4, kTop - 28, 200, 22 }, juce::Justification::centredLeft, 13.0f);
        tt::label (g, "STRAIGHT / SOME / LOTS = how much each lane changes", { kLeftX + 140, kTop - 28, kLeftW - 140, 22 }, juce::Justification::centredRight, 10.5f, tt::inkSoft);
        for (int i = 0; i < (int) cache.size(); ++i) drawLane (g, i, cache[(size_t) i]);
        drawTimeline (g, cache, *plan);
    }
    if (dropRow >= -1)
    {
        auto zone = dropRow >= 0 ? row (dropRow).toFloat().expanded (3) : juce::Rectangle<float> (8, (float) kTop - 34, (float) W - 16, (float) (H - kTop - kFooter + 26));
        g.setColour (tt::orange.withAlpha (0.25f)); g.fillRoundedRectangle (zone, 10);
        g.setColour (tt::ink); g.drawRoundedRectangle (zone, 10, 2.5f);
        auto msgR = zone.withSizeKeepingCentre (360, 50);
        if (dropRow >= 0) msgR = zone.withSizeKeepingCentre (300, 40);
        tt::sticker (g, msgR, tt::orange, 12);
        tt::label (g, dropRow >= 0 ? "DROP TO REPLACE THIS STEM" : "DROP TO ADD", msgR.toNearestInt(), juce::Justification::centred, 16.0f);
    }
    // footer
    auto f = juce::Rectangle<int> (0, H - kFooter, W, kFooter);
    g.setColour (tt::ink); g.fillRect (f);
    juce::String msg = hintText.isNotEmpty() ? hintText : proc.message();
    if (proc.jobs.load() > 0) msg = "Working...  " + msg;
    if (msg.isEmpty()) msg = cache.empty() ? "Drop your stems, a folder of them, or your saved Live set (.als) anywhere on Arc."
                                           : "Click the timeline to move, double-click a section to loop it, scroll to zoom. Drag lanes into Live when you're happy.";
    g.setColour (tt::cream); g.setFont (tt::font (13.5f, 1));
    g.drawText (msg, f.reduced (16, 0), juce::Justification::centredLeft, true);
}

void ArcEditor::drawHeader (juce::Graphics& g)
{
    tt::header (g, { 0, 0, W, kHeader }, "ARC", "your stems, arranged");
    // position readout
    const double pos = proc.playPpq.load();
    const int bar = (int) std::floor (pos / 4.0) + 1, beat = (int) std::floor (std::fmod (pos, 4.0)) + 1;
    juce::String sec = "-";
    if (planCache) { const int si = planCache->sectionAt ((int) (pos / 4.0)); if (si >= 0) sec = secName (planCache->sections[(size_t) si].type); }
    auto rd = juce::Rectangle<float> (910, 16, 112, 44);
    tt::sticker (g, rd, tt::cream2, 10, 2.5f, 2.0f);
    g.setColour (tt::ink); g.setFont (tt::font (19, 2));
    g.drawText ("BAR " + juce::String (bar) + "." + juce::String (beat), rd.withTrimmedBottom (16).toNearestInt(), juce::Justification::centred);
    g.setColour (tt::inkSoft); g.setFont (tt::font (11, 2, 0.06f));
    g.drawText (sec + "  " + juce::String (proc.preparedBpm(), 1), rd.withTrimmedTop (26).toNearestInt(), juce::Justification::centred);
    tt::label (g, "LEVEL", { W - 112, 28, 48, 20 }, juce::Justification::centredRight, 11.0f, tt::inkSoft);
}

void ArcEditor::drawSteps (juce::Graphics& g)
{
    struct S { int n; const char* title; const char* sub; juce::Colour c; };
    const S steps[] { { 1, "ADD STEMS", "files, or from Live", tt::orange },
                      { 2, "SHAPE IT", "pick a style", tt::coral },
                      { 3, "SEND TO LIVE", "", tt::teal } };
    for (auto& s : steps)
    {
        auto r = step (s.n).toFloat();
        tt::sticker (g, r.withTrimmedRight (3).withTrimmedBottom (3), tt::cream2, 14, 3.0f, 2.2f);
        auto num = juce::Rectangle<float> (r.getX() + 12, r.getCentreY() - 17, 34, 34);
        g.setColour (s.c); g.fillEllipse (num);
        g.setColour (tt::ink); g.drawEllipse (num, 2.2f);
        g.setFont (tt::font (19, 2)); g.drawText (juce::String (s.n), num.translated (0, -1).toNearestInt(), juce::Justification::centred);
        if (s.n == 3) continue;
        auto t = juce::Rectangle<int> ((int) r.getX() + 54, (int) r.getY() + 13, s.n == 1 ? 150 : 60, 22);
        g.setColour (tt::ink); g.setFont (tt::font (14.5f, 2, 0.03f));
        if (s.n == 2)
        {
            g.drawText ("SHAPE IT", t.withWidth (110), juce::Justification::centredLeft);
            g.setColour (tt::inkSoft); g.setFont (tt::font (11.5f, 1));
            g.drawText ("pick a style", t.translated (0, 20).withWidth (110), juce::Justification::centredLeft);
            tt::label (g, "VARIATION", { (int) r.getX() + 336, (int) r.getY() + 14, 62, 20 }, juce::Justification::centredRight, 11.0f, tt::inkSoft);
            tt::label (g, "more fills", { (int) r.getX() + 336, (int) r.getY() + 32, 62, 16 }, juce::Justification::centredRight, 10.0f, tt::inkSoft);
        }
        else
        {
            g.drawText (s.title, t, juce::Justification::centredLeft);
            g.setColour (tt::inkSoft); g.setFont (tt::font (11.5f, 1));
            g.drawText (s.sub, t.translated (0, 20).withWidth (124), juce::Justification::centredLeft);
        }
    }
    // step 3: the big drag handle
    auto d = dragAllR().toFloat();
    const bool any = ! cache.empty();
    tt::sticker (g, d, any ? tt::teal : tt::cream3, 12, 3.0f, 2.2f);
    g.setColour (any ? tt::cream : tt::inkSoft); g.setFont (tt::font (13, 2, 0.04f));
    g.drawFittedText ("DRAG ALL\nTO LIVE", d.withTrimmedLeft (22).toNearestInt(), juce::Justification::centred, 2);
    juce::Path grip;
    for (int k = 0; k < 3; ++k) for (int j = 0; j < 2; ++j) grip.addEllipse (d.getX() + 10 + j * 6, d.getCentreY() - 8 + k * 6, 3, 3);
    g.fillPath (grip);
}

void ArcEditor::drawEmpty (juce::Graphics& g)
{
    auto r = juce::Rectangle<float> (12, (float) kTop - 30, (float) W - 24, (float) (H - kTop - kFooter + 18));
    g.setColour (tt::cream2); g.fillRoundedRectangle (r, 18);
    juce::Path dash; dash.addRoundedRectangle (r.reduced (2), 18);
    juce::PathStrokeType (2.5f).createDashedStroke (dash, dash, std::array<float, 2> { 12, 9 }.data(), 2);
    g.setColour (tt::ink); g.fillPath (dash);
    auto c = r.withSizeKeepingCentre (760, 300);
    g.setColour (tt::ink); g.setFont (tt::font (34, 2));
    g.drawText ("Drop your stems here", c.removeFromTop (54).toNearestInt(), juce::Justification::centred);
    g.setColour (tt::inkSoft); g.setFont (tt::font (16, 1));
    g.drawText ("or capture them straight from Live with CAPTURE LIVE", c.removeFromTop (30).toNearestInt(), juce::Justification::centred);
    c.removeFromTop (24);
    struct Card { const char* t; const char* s; juce::Colour col; };
    const Card cards[] { { "Straight from Live", "Put Arc Tap on the tracks you want, press CAPTURE LIVE, then play. Every track arrives in time. No bouncing.", tt::teal },
                         { "Stems or loops", "Drop files or a whole folder. Names like Kick, Bass, Hats tell Arc what each one is.", tt::coral },
                         { "Then GENERATE", "Intro, build, drop, break, outro. Set each lane STRAIGHT, SOME or LOTS.", tt::orange } };
    const float cw = (c.getWidth() - 32) / 3.0f;
    for (int k = 0; k < 3; ++k)
    {
        auto cr = juce::Rectangle<float> (c.getX() + k * (cw + 16), c.getY(), cw, 150);
        tt::sticker (g, cr, tt::cream, 14, 3.5f, 2.2f);
        g.setColour (cards[k].col); g.fillRoundedRectangle (cr.withHeight (12).reduced (2, 0).translated (0, 2), 6);
        g.setColour (tt::ink); g.setFont (tt::font (18, 2));
        g.drawText (cards[k].t, cr.withTrimmedTop (24).withHeight (26).toNearestInt(), juce::Justification::centred);
        g.setColour (tt::inkSoft); g.setFont (tt::font (13.5f, 1));
        g.drawFittedText (cards[k].s, cr.withTrimmedTop (56).reduced (16, 6).toNearestInt(), juce::Justification::centredTop, 4);
    }
}

void ArcEditor::drawLane (juce::Graphics& g, int i, const Stem& s)
{
    auto r = row (i).toFloat();
    const auto col = laneColour (s.role);
    tt::sticker (g, r, tt::cream2, 10, 0.0f, 1.8f);
    // role chip
    auto chip = chipR (i).toFloat().reduced (3.5f).withTrimmedRight (2).withTrimmedBottom (2);
    tt::sticker (g, chip, s.mute ? tt::cream3 : col, 8, 2.0f, 1.8f);
    g.setColour (col.getPerceivedBrightness() > 0.6f || s.mute ? tt::ink : tt::cream);
    g.setFont (tt::font (12.5f, 2, 0.04f));
    g.drawFittedText (roleName (s.role), chip.toNearestInt(), juce::Justification::centred, 1);
    // name + status
    auto info = infoR (i).reduced (0, 5);
    g.setColour (tt::ink); g.setFont (tt::font (14, 2));
    g.drawText (s.name, info.removeFromTop (18), juce::Justification::centredLeft, true);
    g.setColour (tt::inkSoft); g.setFont (tt::font (11, 1));
    juce::String line1 = juce::String (s.audio ? s.audio->bars : s.timing.bars) + (s.audio && s.audio->bars == 1 ? " bar" : " bars")
                       + (s.timing.fromSet ? "  \xe2\x9c\x93 from Live" : "");
    g.drawText (line1, info.removeFromTop (14), juce::Justification::centredLeft, true);
    if (info.getHeight() >= 12)
    {
        juce::String k;
        if (isTonalRole (s.role) && s.key.tonic >= 0)
        {
            k = juce::String (cl::noteName (s.key.tonic)) + (s.key.minor ? "m" : "");
            if (s.transpose != 0) k << "  moved " << (s.transpose > 0 ? "+" : "") << s.transpose;
            else if (s.suggested != 0) k << "  (" << (s.suggested > 0 ? "+" : "") << s.suggested << " to fit)";
        }
        g.setColour (s.transpose != 0 ? tt::coral : tt::inkSoft);
        g.drawText (k, info.removeFromTop (14), juce::Justification::centredLeft, true);
    }
    // waveform, bar 1 marked
    auto w = waveR (i);
    tt::screen (g, w.toFloat(), 6);
    if (s.audio != nullptr && ! s.peaks.empty())
    {
        const int bars = s.audio->bars, steps = bars * 16;
        const float stepW = (float) (w.getWidth() - 4) / steps;
        const float shift = drag == Drag::wave && dragRow == i ? (dragNudgeNow - dragNudgeBase) * -stepW : 0.0f;
        g.saveState();
        g.reduceClipRegion (w.reduced (2));
        for (int b = 1; b < bars * 4; ++b)
        {
            const float x = w.getX() + 2 + b * 4 * stepW;
            g.setColour (b % 4 == 0 ? tt::ink.withAlpha (0.45f) : tt::ink.withAlpha (0.12f));
            g.fillRect (x - (b % 4 == 0 ? 0.75f : 0.5f), (float) w.getY(), b % 4 == 0 ? 1.5f : 1.0f, (float) w.getHeight());
        }
        g.setColour (col.darker (0.15f));
        const float mid = (float) w.getCentreY(), half = w.getHeight() * 0.42f;
        const int n = (int) s.peaks.size();
        for (int px = 2; px < w.getWidth() - 2; ++px)
        {
            float src = (px - 2 - shift) / (w.getWidth() - 4) * n;
            src = std::fmod (src, (float) n); if (src < 0) src += n;
            const float v = s.peaks[(size_t) juce::jlimit (0, n - 1, (int) src)];
            const float hgt = juce::jmax (0.5f, juce::jmin (1.0f, v) * half);
            g.fillRect ((float) (w.getX() + px), mid - hgt, 1.0f, hgt * 2);
        }
        g.restoreState();
        // bar 1 flag
        g.setColour (tt::ink); g.fillRect ((float) w.getX() + 2, (float) w.getY() + 2, 2.5f, (float) w.getHeight() - 4);
        auto flag = juce::Rectangle<float> ((float) w.getX() + 4, (float) w.getY() + 3, 14, 12);
        g.fillRoundedRectangle (flag, 3);
        g.setColour (tt::cream); g.setFont (tt::font (10, 2)); g.drawText ("1", flag.toNearestInt(), juce::Justification::centred);
        const int nd = drag == Drag::wave && dragRow == i ? dragNudgeNow : s.nudge;
        if (nd != 0)
        {
            auto tag = juce::Rectangle<float> ((float) w.getRight() - 44, (float) w.getBottom() - 15, 40, 12);
            g.setColour (tt::coral); g.fillRoundedRectangle (tag, 3);
            g.setColour (tt::cream); g.setFont (tt::font (9.5f, 2));
            g.drawText ((nd > 0 ? "+" : "") + juce::String (nd) + "/16", tag.toNearestInt(), juce::Justification::centred);
        }
    }
    else
    {
        g.setColour (tt::inkSoft); g.setFont (tt::font (11, 1));
        g.drawText ("getting ready...", w, juce::Justification::centred);
    }
    // vary pill
    auto vr = varyR (i).toFloat();
    const juce::Colour vc = s.vary <= 0 ? tt::cream : s.vary == 1 ? tt::orangeSoft : tt::coral;
    tt::sticker (g, vr.withTrimmedRight (2).withTrimmedBottom (2), vc, 8, 2.0f, 1.6f);
    g.setColour (s.vary >= 2 ? tt::cream : tt::ink); g.setFont (tt::font (10.5f, 2, 0.04f));
    g.drawText (varyName (s.vary), vr.withTrimmedRight (2).withTrimmedBottom (2).toNearestInt(), juce::Justification::centred);
    // dice / mute / solo
    auto small = [&g] (juce::Rectangle<int> b, const juce::String& t, bool on, juce::Colour c, bool enabled = true)
    {
        auto f = b.toFloat().withTrimmedRight (2).withTrimmedBottom (2);
        tt::sticker (g, f, on ? c : tt::cream, 6, 2.0f, 1.6f);
        g.setColour (enabled ? (on ? tt::cream : tt::ink) : tt::inkSoft.withAlpha (0.5f)); g.setFont (tt::font (11.5f, 2));
        g.drawText (t, f.toNearestInt(), juce::Justification::centred);
    };
    small (diceR (i), juce::CharPointer_UTF8 ("\xe2\x86\xbb"), false, tt::ink, s.vary > 0);
    small (muteR (i), "M", s.mute, tt::coral);
    small (soloR (i), "S", s.solo, tt::orange);
    // volume: a tiny knob
    {
        auto vb = volR (i).toFloat().withTrimmedRight (2).withTrimmedBottom (2);
        const float frac = juce::jlimit (0.0f, 1.0f, (s.volumeDb + 36.0f) / 42.0f);
        const auto c = vb.getCentre(); const float rad = juce::jmin (vb.getWidth(), vb.getHeight()) * 0.5f;
        g.setColour (tt::cream); g.fillEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2);
        juce::Path a; a.addCentredArc (c.x, c.y, rad - 2.5f, rad - 2.5f, 0, -2.4f, -2.4f + 4.8f * frac, true);
        g.setColour (col); g.strokePath (a, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (tt::ink); g.drawEllipse (c.x - rad, c.y - rad, rad * 2, rad * 2, 1.6f);
        const float ang = -2.4f + 4.8f * frac;
        g.drawLine (c.x, c.y, c.x + std::sin (ang) * (rad - 5), c.y - std::cos (ang) * (rad - 5), 2.0f);
    }
    // drag handle
    auto d = dragR (i).toFloat().reduced (2).withTrimmedRight (2).withTrimmedBottom (2);
    tt::sticker (g, d, col, 8, 2.0f, 1.6f);
    const auto cc = d.getCentre();
    juce::Path arrow;
    arrow.startNewSubPath (cc.x - 6, cc.y + 6); arrow.lineTo (cc.x + 6, cc.y - 6);
    arrow.startNewSubPath (cc.x - 1, cc.y - 6); arrow.lineTo (cc.x + 6, cc.y - 6); arrow.lineTo (cc.x + 6, cc.y + 1);
    g.setColour (col.getPerceivedBrightness() > 0.6f ? tt::ink : tt::cream);
    g.strokePath (arrow, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (s.hold)
    {
        auto lk = juce::Rectangle<float> (chip.getRight() - 12, chip.getY() - 4, 14, 14);
        g.setColour (tt::ink); g.fillEllipse (lk);
        g.setColour (tt::cream); g.setFont (tt::font (9, 2)); g.drawText ("H", lk.toNearestInt(), juce::Justification::centred);
    }
}

void ArcEditor::drawTimeline (juce::Graphics& g, const std::vector<Stem>& st, const Plan& plan)
{
    const auto t = timeline();
    tt::sticker (g, t.toFloat(), tt::cream2, 12, 0.0f, 2.0f);
    g.saveState();
    g.reduceClipRegion (t.reduced (2));
    const float pxPerBar = xOfBeat (4) - xOfBeat (0);
    const auto strip = sectionStrip().reduced (4, 3);
    // sections
    for (auto& s : plan.sections)
    {
        const float x0 = xOfBeat (s.start * 4.0), x1 = xOfBeat ((s.start + s.bars) * 4.0);
        auto sr = juce::Rectangle<float> (x0 + 1, (float) strip.getY(), x1 - x0 - 2, (float) strip.getHeight());
        g.setColour (sectionColour (s.type)); g.fillRoundedRectangle (sr, 6);
        g.setColour (tt::ink); g.drawRoundedRectangle (sr.reduced (0.8f), 6, 1.6f);
        g.setColour (sectionColour (s.type).withAlpha (0.16f));
        g.fillRect (juce::Rectangle<float> (x0, (float) strip.getBottom() + 3, x1 - x0, (float) (t.getBottom() - strip.getBottom() - 3)));
        if (x1 - x0 > 34)
        {
            g.setColour (s.type == sDrop || s.type == sBreak ? tt::cream : tt::ink); g.setFont (tt::font (11.5f, 2, 0.05f));
            g.drawText (juce::String (secName (s.type)) + (x1 - x0 > 80 ? "  " + juce::String (s.bars) : juce::String()), sr.reduced (6, 0).toNearestInt(), juce::Justification::centredLeft, true);
        }
    }
    // bar lines
    const int every = pxPerBar > 14 ? 1 : pxPerBar > 5 ? 4 : 8;
    for (int b = 0; b <= plan.totalBars; b += every)
    {
        const float x = xOfBeat (b * 4.0);
        g.setColour (tt::ink.withAlpha (b % 8 == 0 ? 0.22f : 0.08f));
        g.fillRect (x - 0.5f, (float) strip.getBottom() + 4, 1.0f, (float) (t.getBottom() - strip.getBottom() - 6));
    }
    // loop range
    const double ls = proc.loopStart.load(), le = proc.loopEnd.load();
    if (le > ls + 0.01)
    {
        const float x0 = xOfBeat (ls), x1 = xOfBeat (le);
        g.setColour (tt::teal.withAlpha (0.12f)); g.fillRect (juce::Rectangle<float> (x0, (float) strip.getBottom() + 3, x1 - x0, (float) (t.getBottom() - strip.getBottom())));
        g.setColour (tt::teal); g.fillRoundedRectangle (juce::Rectangle<float> (x0, (float) strip.getBottom() + 2, x1 - x0, 5.0f), 2.5f);
    }
    // lanes: one rounded block per run of bars that play, step detail when zoomed in
    for (int i = 0; i < (int) st.size() && i < (int) plan.lanes.size(); ++i)
    {
        auto r = row (i);
        const auto col = laneColour (st[(size_t) i].role);
        const auto& lp = plan.lanes[(size_t) i];
        const float y0 = (float) r.getY() + 5, h = (float) r.getHeight() - 10;
        const float alpha = st[(size_t) i].mute ? 0.3f : 1.0f;
        int b = 0;
        while (b < (int) lp.bars.size())
        {
            if (! lp.bars[(size_t) b].on) { ++b; continue; }
            int e = b; while (e < (int) lp.bars.size() && lp.bars[(size_t) e].on) ++e;
            const float x0 = xOfBeat (b * 4.0) + 1, x1 = xOfBeat (e * 4.0) - 1;
            if (x1 >= t.getX() && x0 <= t.getRight())
            {
                auto blk = juce::Rectangle<float> (x0, y0, juce::jmax (2.0f, x1 - x0), h);
                g.setColour (col.withMultipliedAlpha (alpha)); g.fillRoundedRectangle (blk, 5);
                // inside the block: gain ramps, filters, edits
                for (int k = b; k < e; ++k)
                {
                    const auto& B = lp.bars[(size_t) k];
                    const float bx0 = xOfBeat (k * 4.0), bx1 = xOfBeat (k * 4.0 + 4.0);
                    if (bx1 < t.getX() || bx0 > t.getRight()) continue;
                    const float lpAvg = std::sqrt (B.lp0 * B.lp1), hpAvg = std::sqrt (B.hp0 * B.hp1);
                    float shade = 0;
                    if (lpAvg < 15000) shade = juce::jmax (shade, juce::jlimit (0.0f, 0.6f, std::log2 (15000.0f / lpAvg) / 7.0f));
                    if (hpAvg > 40) shade = juce::jmax (shade, juce::jlimit (0.0f, 0.6f, std::log2 (hpAvg / 40.0f) / 7.0f));
                    const float gAvg = 0.5f * (B.g0 + B.g1);
                    if (gAvg < 0.95f) shade = juce::jmax (shade, (1.0f - gAvg) * 0.6f);
                    if (shade > 0.01f) { g.setColour (tt::cream.withAlpha (shade)); g.fillRect (juce::Rectangle<float> (bx0, y0, bx1 - bx0, h).getIntersection (blk)); }
                    const float sw = (bx1 - bx0) / 16;
                    for (int q = 0; q < 16; ++q)
                    {
                        const auto& op = B.steps[(size_t) q];
                        auto cell = juce::Rectangle<float> (bx0 + q * sw, y0, sw, h).getIntersection (blk);
                        if (op.mute) { g.setColour (tt::cream2); g.fillRect (cell); }
                        else if ((op.rep > 1 || op.rev || op.span > 1) && sw >= 1.5f) { g.setColour (tt::ink.withAlpha (0.55f)); g.fillRect (cell.withTrimmedTop (h * 0.55f)); }
                    }
                    if (B.fill != fNone && B.fill != fSweep && B.fill != fEnter && pxPerBar > 3)
                    {
                        juce::Path tri; tri.addTriangle (bx1 - 7, y0, bx1 - 1, y0, bx1 - 1, y0 + 6);
                        g.setColour (tt::ink); g.fillPath (tri);
                        if (pxPerBar > 60) { g.setFont (tt::font (9.5f, 2)); g.drawText (fillName (B.fill), juce::Rectangle<float> (bx0 + 2, y0 + h - 12, bx1 - bx0 - 4, 11).toNearestInt(), juce::Justification::bottomRight); }
                    }
                }
                g.setColour (tt::ink.withMultipliedAlpha (alpha)); g.drawRoundedRectangle (blk.reduced (0.7f), 5, 1.5f);
            }
            b = e;
        }
    }
    // playhead
    const float px = xOfBeat (proc.playPpq.load());
    g.setColour (tt::ink);
    g.fillRect (juce::Rectangle<float> (px - 1.0f, (float) strip.getBottom() + 2, 2.0f, (float) (t.getBottom() - strip.getBottom())));
    juce::Path head; head.addTriangle (px - 7, (float) strip.getBottom() - 2, px + 7, (float) strip.getBottom() - 2, px, (float) strip.getBottom() + 8);
    g.fillPath (head);
    g.restoreState();
}

// ---------------------------------------------------------------- files in
bool ArcEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const juce::File fl (f);
        if (fl.isDirectory() || fl.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;mid;midi;als")) return true;
    }
    return false;
}

void ArcEditor::fileDragMove (const juce::StringArray& files, int x, int y)
{
    const int r = x < kLeftX + kLeftW ? rowAt ({ x, y }) : -1;
    const bool single = files.size() == 1 && ! juce::File (files[0]).isDirectory() && ! juce::File (files[0]).hasFileExtension ("als");
    const int nd = single ? r : -1;
    if (nd != dropRow) { dropRow = nd; repaint(); }
}

void ArcEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int r = x < kLeftX + kLeftW ? rowAt ({ x, y }) : -1;
    dropRow = -2;
    const bool single = files.size() == 1 && ! juce::File (files[0]).isDirectory() && ! juce::File (files[0]).hasFileExtension ("als");
    if (r >= 0 && single) proc.replaceStem (r, juce::File (files[0]));
    else proc.addFiles (files);
    repaint();
}

// ---------------------------------------------------------------- mouse
void ArcEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    const int i = rowAt (p);
    const bool inLanes = p.x < kLeftX + kLeftW;
    if (timeline().contains (p) && ! cache.empty())
        hint (sectionStrip().contains (p) ? "Click a section to jump to it. Double-click to loop it (double-click again to stop)."
                                          : "Click or drag to move the playhead. Scroll to zoom in on the fills, shift-scroll to move along.");
    else if (dragAllR().contains (p)) hint ("Drag onto Live's arrangement at bar 1: each lane lands on its own track, full length and already arranged.");
    else if (i >= 0 && inLanes)
    {
        const auto& s = cache[(size_t) i];
        if (waveR (i).contains (p)) hint ("Bar 1 is the black flag. If it's wrong, drag the waveform sideways (16ths). Double-click resets.  " + s.report);
        else if (chipR (i).contains (p)) hint ("Change role, crop, transpose, tempo or bar 1, keep or remove this stem.");
        else if (varyR (i).contains (p)) hint ("Click to cycle: STRAIGHT plays the loop as it is, SOME adds the odd fill, LOTS keeps it moving.");
        else if (diceR (i).contains (p)) hint ("New fills and variations for this lane only.");
        else if (muteR (i).contains (p)) hint ("Mute this lane");
        else if (soloR (i).contains (p)) hint ("Solo this lane");
        else if (volR (i).contains (p)) hint ("Lane volume: drag up or down, double-click for 0 dB.  Now " + juce::String (s.volumeDb, 1) + " dB");
        else if (dragR (i).contains (p)) hint ("Drag this lane into Live: a full-length audio clip, arranged, starting at bar 1.");
        else hint ({});
    }
    else hint ({});
    const bool clicky = (i >= 0 && inLanes && (chipR (i).contains (p) || varyR (i).contains (p) || diceR (i).contains (p) || muteR (i).contains (p) || soloR (i).contains (p)))
                      || timeline().contains (p);
    const bool draggy = dragAllR().contains (p) || (i >= 0 && inLanes && (dragR (i).contains (p) || waveR (i).contains (p)));
    setMouseCursor (draggy ? juce::MouseCursor::DraggingHandCursor : clicky ? juce::MouseCursor::PointingHandCursor
                    : (i >= 0 && volR (i).contains (p)) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
}

void ArcEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    dragOutStarted = false;
    drag = Drag::none;
    dragRow = -1;
    const int i = rowAt (p);
    if (i >= 0 && p.x < kLeftX + kLeftW)
    {
        const auto& s = cache[(size_t) i];
        if (chipR (i).contains (p)) { laneMenu (i); return; }
        if (varyR (i).contains (p)) { proc.setVary (i, (s.vary + 1) % 3); return; }
        if (diceR (i).contains (p)) { if (s.vary > 0) proc.dice (i); else hint ("This lane is STRAIGHT, so it has no variations to re-roll. Click its STRAIGHT button first."); return; }
        if (muteR (i).contains (p)) { proc.setMute (i, ! s.mute); return; }
        if (soloR (i).contains (p)) { proc.setSolo (i, ! s.solo); return; }
        if (volR (i).contains (p)) { drag = Drag::volume; dragRow = i; dragVolStart = s.volumeDb; proc.beginVolumeGesture(); return; }
        if (waveR (i).contains (p) && s.audio != nullptr) { drag = Drag::wave; dragRow = i; dragNudgeBase = dragNudgeNow = s.nudge; return; }
        if (dragR (i).contains (p)) { dragRow = i; return; }
        return;
    }
    if (dragAllR().contains (p) && ! cache.empty()) { dragRow = -10; return; }
    const auto t = timeline();
    if (t.contains (p) && planCache && ! cache.empty())
    {
        if (e.mods.isPopupMenu() || e.mods.isAltDown()) { drag = Drag::timelinePan; panStart = viewStart; return; }
        if (sectionStrip().contains (p))
        {
            const int si = planCache->sectionAt ((int) (beatOfX ((float) p.x) / 4.0));
            if (si >= 0) proc.setPosition (planCache->sections[(size_t) si].start * 4.0);
        }
        else
        {
            drag = Drag::scrub;
            proc.setPosition (juce::jmax (0.0, std::floor (beatOfX ((float) p.x))));
        }
        if (proc.liveDriving.load()) hint ("Live is playing, so Arc follows Live's position. Stop Live to move Arc's playhead.");
        else proc.playPpq = proc.arcPos.load();
        repaint();
    }
}

void ArcEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.getPosition();
    if (drag == Drag::volume && dragRow >= 0)
    {
        const float db = juce::jlimit (-36.0f, 6.0f, dragVolStart - e.getDistanceFromDragStartY() * 0.3f);
        proc.setVolume (dragRow, db);
        hint ("Lane volume " + juce::String (db, 1) + " dB");
        return;
    }
    if (drag == Drag::wave && dragRow >= 0 && dragRow < (int) cache.size() && cache[(size_t) dragRow].audio != nullptr)
    {
        const int steps = cache[(size_t) dragRow].audio->bars * 16;
        const float stepW = (float) (waveR (dragRow).getWidth() - 4) / steps;
        dragNudgeNow = dragNudgeBase - juce::roundToInt (e.getDistanceFromDragStartX() / stepW);
        repaint (waveR (dragRow));
        return;
    }
    if (drag == Drag::scrub)
    {
        proc.setPosition (juce::jmax (0.0, std::floor (beatOfX ((float) p.x) * 4.0) / 4.0));
        if (! proc.liveDriving.load()) proc.playPpq = proc.arcPos.load();
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
    if (! dragOutStarted && e.getDistanceFromDragStart() > 6 && (dragRow == -10 || (dragRow >= 0 && dragRow < (int) cache.size() && dragR (dragRow).contains (e.getMouseDownPosition()))))
    {
        dragOutStarted = true;
        hint ("Rendering...");
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
    hint ("Drop it at bar 1 in Live's arrangement. Copies are kept in Music > Arc Exports > Dragged.");
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
    if (i >= 0 && p.x < kLeftX + kLeftW)
    {
        if (waveR (i).contains (p)) proc.setNudge (i, 0);
        else if (volR (i).contains (p)) proc.setVolume (i, 0, true);
        return;
    }
    if (planCache && timeline().contains (p) && ! cache.empty())
    {
        const int si = planCache->sectionAt ((int) (beatOfX ((float) p.x) / 4.0));
        if (si < 0) return;
        const auto& s = planCache->sections[(size_t) si];
        const double a = s.start * 4.0, b = (s.start + s.bars) * 4.0;
        if (std::abs (proc.loopStart.load() - a) < 1e-6 && std::abs (proc.loopEnd.load() - b) < 1e-6) { proc.setLoop (0, 0); hint ("Loop off"); }
        else { proc.setLoop (a, b); proc.setPosition (a); hint (juce::String ("Looping the ") + secName (s.type) + " (bars " + juce::String (s.start + 1) + "-" + juce::String (s.start + s.bars) + "). Double-click again to stop looping."); }
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
    const auto s = cache[(size_t) i];
    juce::PopupMenu m, roles, crop, tp, tempo;
    for (int r = 0; r < kRoles; ++r) roles.addItem (100 + r, roleName (r), true, r == s.role);
    const int src = sourceBars (s);
    crop.addItem (200, "Whole loop" + juce::String (src > 8 ? " (8 busiest bars)" : ""), true, s.cropBars == 0);
    for (int len : { 1, 2, 4, 8 })
        if (len < src || (len == src && src > 8))
            for (int st = 0; st + len <= juce::jmin (src, 32); st += len)
                crop.addItem (1000 + len * 100 + st, "Bars " + juce::String (st + 1) + (len > 1 ? "-" + juce::String (st + len) : juce::String()), true, s.cropBars == len && s.cropStart == st);
    tp.addItem (500, "Auto (follow KEY LOCK)", true, s.manualTranspose == Stem::kAuto);
    if (s.suggested != 0) tp.addItem (501, "Into key: " + juce::String (s.suggested > 0 ? "+" : "") + juce::String (s.suggested), true, s.manualTranspose == s.suggested);
    for (int k = -7; k <= 7; ++k) tp.addItem (520 + k, k == 0 ? juce::String ("Off (0)") : (k > 0 ? "+" : "") + juce::String (k) + " semitones", true, s.manualTranspose == k);
    if (s.timing.fromSet) tempo.addItem (-1, juce::String (s.timing.tempo, 2) + " BPM, from your Live set", false, true);
    else
        for (int k = 0; k < (int) s.tempoChoices.size(); ++k)
        {
            const double c = s.tempoChoices[(size_t) k];
            const int bars = juce::jmax (1, (int) std::round (s.original ? s.original->getNumSamples() / s.fileSr * c / 240.0 : 4));
            tempo.addItem (300 + k, juce::String (c, 2) + " BPM (" + juce::String (bars) + " bars)", true, std::abs (c - s.timing.tempo) < 0.01);
        }
    tempo.addItem (399, "Same as Live (" + juce::String (proc.hostBpm.load(), 2) + " BPM)");
    m.addSectionHeader (s.name + "  -  " + s.report);
    m.addSubMenu ("Role", roles);
    m.addSubMenu ("Crop", crop);
    m.addSubMenu ("Transpose", tp, ! isDrumRole (s.role));
    m.addSubMenu ("Source tempo", tempo);
    m.addItem (600, "Bar 1 = start of the file", s.timing.shifted || s.nudge != 0);
    m.addItem (650, "Keep this lane's variations on GENERATE", true, s.hold);
    m.addSeparator();
    m.addItem (700, "Replace...");
    m.addItem (800, "Remove");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (chipR (i))), [this, i, s] (int r)
    {
        if (r >= 100 && r < 100 + kRoles) proc.setRole (i, r - 100);
        else if (r == 200) proc.setCrop (i, 0, 0);
        else if (r >= 1000) { const int len = (r - 1000) / 100, st = (r - 1000) % 100; proc.setCrop (i, st, len); }
        else if (r >= 300 && r < 300 + (int) s.tempoChoices.size()) proc.setSourceTempo (i, s.tempoChoices[(size_t) (r - 300)]);
        else if (r == 399) proc.setSourceTempo (i, proc.hostBpm.load());
        else if (r == 500) proc.setManualTranspose (i, Stem::kAuto);
        else if (r == 501) proc.setManualTranspose (i, s.suggested);
        else if (r >= 513 && r <= 527) proc.setManualTranspose (i, r - 520);
        else if (r == 600) proc.barOneAtStart (i);
        else if (r == 650) proc.setHold (i, ! s.hold);
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
