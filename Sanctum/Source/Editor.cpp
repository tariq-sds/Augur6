#include "Editor.h"

using namespace sp;

static const juce::Rectangle<int> kStage { 16, 72, 720, 440 };
static const juce::Rectangle<int> kSide  { 748, 72, 356, 440 };
static const juce::Rectangle<int> kKnobs { 16, 524, 1088, 160 };

static juce::String noteText (int m) { return m < 0 ? juce::String ("-") : juce::String (cl::noteName (m)) + juce::String (m / 12 - 1); }

// singers per section, the same way the engine shares them out
static std::array<int, sc::kSections> allocation (const sc::Settings& s)
{
    std::array<int, sc::kSections> a {};
    std::array<int, sc::kSections> order {}; int nAct = 0;
    for (int i = 0; i < sc::kSections; ++i) if (s.level[(size_t) i] > 0.01f) order[(size_t) nAct++] = i;
    if (nAct == 0) return a;
    const int total = juce::jlimit (1, nAct * sc::kPerSection, s.singers);
    for (int i = 0; i < total; ++i) ++a[(size_t) order[(size_t) (i % nAct)]];
    return a;
}

// risers curve up towards the sides, wrapping round the lead
static float riserLift (float dx, float width)
{
    const float rx = width * 0.6f, R = 380.0f, u = juce::jlimit (0.0f, 0.97f, std::abs (dx) / rx);
    return R * (1.0f - std::sqrt (1.0f - u * u));
}

//==============================================================================
juce::Colour StageView::colourOf (int s)
{
    const juce::Colour c[] { toota::teal, toota::coral, toota::orange, toota::plum };
    return c[juce::jlimit (0, 3, s)];
}

juce::Point<float> StageView::centreOf (int s) const
{
    const auto b = getLocalBounds().toFloat();
    const float front = b.getBottom() - 132.0f, back = b.getY() + 92.0f;
    const float dx = param (pan[s]) * b.getWidth() * 0.4f;
    return { b.getCentreX() + dx, front - param (depth[s]) * (front - back) - riserLift (dx, b.getWidth()) };
}

float StageView::scaleOf (int s) const { return 1.45f * (1.0f - 0.42f * param (depth[s])); }

int StageView::hit (juce::Point<float> p) const
{
    int best = -1; float bd = 70.0f;
    for (int s = 0; s < sc::kSections; ++s)
    {
        const float d = p.getDistanceFrom (centreOf (s) + juce::Point<float> (0, -22.0f * scaleOf (s)));
        if (d < bd) { bd = d; best = s; }
    }
    return best;
}

void StageView::drawSinger (juce::Graphics& g, juce::Point<float> feet, float k, juce::Colour c, float level, float phase, bool ghost)
{
    const float bob = std::sin (t * 5.0f + phase) * 1.6f * juce::jlimit (0.0f, 1.0f, level * 6.0f) * k;
    const float bw = 22.0f * k, bh = 24.0f * k, hr = 7.5f * k;
    auto body = juce::Rectangle<float> (feet.x - bw * 0.5f, feet.y - bh + bob, bw, bh);
    const float a = ghost ? 0.25f : 1.0f;
    juce::Path robe;
    robe.startNewSubPath (body.getX() + bw * 0.12f, body.getBottom());
    robe.quadraticTo (body.getX() - bw * 0.02f, body.getY() + bh * 0.25f, body.getCentreX(), body.getY());
    robe.quadraticTo (body.getRight() + bw * 0.02f, body.getY() + bh * 0.25f, body.getRight() - bw * 0.12f, body.getBottom());
    robe.closeSubPath();
    g.setColour (toota::ink.withAlpha (0.18f * a)); g.fillEllipse (feet.x - bw * 0.55f, feet.y - 3.0f * k, bw * 1.1f, 6.0f * k);
    g.setColour (c.withAlpha (a)); g.fillPath (robe);
    g.setColour (toota::ink.withAlpha (a)); g.strokePath (robe, juce::PathStrokeType (1.6f * k + 0.3f));
    const auto head = juce::Rectangle<float> (feet.x - hr, body.getY() - hr * 1.75f, hr * 2, hr * 2);
    g.setColour (toota::cream.withAlpha (a)); g.fillEllipse (head);
    g.setColour (toota::ink.withAlpha (a)); g.drawEllipse (head, 1.6f * k + 0.3f);
    // mouth opens with the level
    const float open = juce::jlimit (0.0f, 1.0f, level * 5.0f);
    if (! ghost && open > 0.05f)
    {
        g.setColour (toota::ink);
        g.fillEllipse (head.getCentreX() - 2.2f * k, head.getCentreY() + 1.0f * k, 4.4f * k, (1.0f + 4.0f * open) * k);
    }
    else
    {
        g.setColour (toota::ink.withAlpha (a));
        g.drawLine (head.getCentreX() - 2.2f * k, head.getCentreY() + 2.5f * k, head.getCentreX() + 2.2f * k, head.getCentreY() + 2.5f * k, 1.2f * k);
    }
}

void StageView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    toota::screen (g, b, 14.0f);
    g.saveState();
    juce::Path clip; clip.addRoundedRectangle (b.reduced (2.0f), 12.0f); g.reduceClipRegion (clip);

    // risers: curved steps wrapping round you, back to front
    const float cx = b.getCentreX();
    const float front = b.getBottom() - 132.0f, back = b.getY() + 92.0f;
    for (int r = 0; r < 5; ++r)
    {
        const float y = back + 6.0f + (float) r * (front - back) / 4.0f;
        juce::Path arc;
        for (int i = 0; i <= 60; ++i)
        {
            const float dx = ((float) i / 60.0f * 2.0f - 1.0f) * b.getWidth() * 0.56f;
            const juce::Point<float> pt { cx + dx, y - riserLift (dx, b.getWidth()) };
            if (i == 0) arc.startNewSubPath (pt); else arc.lineTo (pt);
        }
        g.setColour (r % 2 ? toota::cream2 : toota::cream);
        g.strokePath (arc, juce::PathStrokeType (34.0f));
        g.setColour (toota::ink.withAlpha (0.22f));
        g.strokePath (arc, juce::PathStrokeType (1.4f));
    }
    // floor lip
    g.setColour (toota::orangeSoft.withAlpha (0.55f));
    g.fillEllipse (cx - b.getWidth() * 0.36f, b.getBottom() - 104.0f, b.getWidth() * 0.72f, 150.0f);
    g.setColour (toota::ink.withAlpha (0.35f));
    g.drawEllipse (cx - b.getWidth() * 0.36f, b.getBottom() - 104.0f, b.getWidth() * 0.72f, 150.0f, 1.6f);

    const auto st = proc.settings();
    const auto alloc = allocation (st);
    std::array<int, sc::kSections> order { 0, 1, 2, 3 };
    std::sort (order.begin(), order.end(), [this] (int a, int c) { return param (depth[a]) > param (depth[c]); });

    for (int s : order)
    {
        const auto c = centreOf (s); const float k = scaleOf (s);
        const int n = juce::jmax (1, alloc[(size_t) s]);
        const bool ghost = alloc[(size_t) s] == 0;
        const float lvl = proc.engine.meter[(size_t) s].load();
        const int perRow = 3, rows = (n + perRow - 1) / perRow;
        const float sx = 21.0f * k * (0.75f + 0.4f * st.spread), sy = 15.0f * k;
        for (int row = rows - 1; row >= 0; --row)                 // back rows first
        {
            const int inRow = juce::jmin (perRow, n - row * perRow);
            for (int i = 0; i < inRow; ++i)
            {
                const float x = c.x + ((float) i - (float) (inRow - 1) * 0.5f) * sx + (row % 2 ? sx * 0.5f : 0.0f);
                const float y = c.y - (float) row * sy - (riserLift (x - cx, b.getWidth()) - riserLift (c.x - cx, b.getWidth()));
                drawSinger (g, { x, y }, k * (1.0f - 0.05f * (float) row), colourOf (s), lvl, (float) (s * 8 + row * 4 + i) * 0.9f, ghost);
            }
        }
        // section tag
        const int note = proc.engine.note[(size_t) s].load();
        juce::String tag = sc::kSectionNames[s];
        if (! ghost && note >= 0) tag << "  " << noteText (note);
        if (ghost) tag << "  OFF";
        const float tw = 18.0f + toota::font (11.0f, 2).getStringWidthFloat (tag);
        auto pill = juce::Rectangle<float> (c.x - tw * 0.5f, c.y - (float) rows * sy - 52.0f * k - 10.0f, tw, 20.0f);
        toota::sticker (g, pill, ghost ? toota::cream2 : colourOf (s), 10.0f, s == hover || s == dragging ? 3.0f : 2.0f, 1.8f);
        toota::label (g, tag, pill.toNearestInt(), juce::Justification::centred, 11.0f, ghost ? toota::inkSoft : toota::cream);
    }

    // you, front and centre
    {
        const juce::Point<float> feet { cx, b.getBottom() - 40.0f };
        const float lvl = juce::jmax (proc.engine.meter[0].load(), proc.engine.meter[1].load(), proc.engine.meter[2].load(), proc.engine.meter[3].load());
        drawSinger (g, feet, 1.25f, toota::ink, lvl, 0.0f, false);
        const int ln = proc.engine.leadNote.load();
        const juce::String tag = ln >= 0 ? "YOU  " + noteText (ln) : juce::String ("YOU");
        const float tw = 18.0f + toota::font (11.0f, 2).getStringWidthFloat (tag);
        auto pill = juce::Rectangle<float> (feet.x + 26.0f, feet.y - 34.0f, tw, 20.0f);
        toota::sticker (g, pill, toota::cream, 10.0f, 2.0f, 1.8f);
        toota::label (g, tag, pill.toNearestInt(), juce::Justification::centred, 11.0f);
    }
    g.restoreState();
    toota::label (g, "DRAG A SECTION: SIDEWAYS TO PLACE IT, UP TO SEND IT FURTHER BACK. DOUBLE-CLICK TO RESET.",
                  getLocalBounds().removeFromTop (26).reduced (12, 0), juce::Justification::centredLeft, 9.5f, toota::inkSoft);
}

void StageView::mouseMove (const juce::MouseEvent& e)
{
    const int h = hit (e.position);
    if (h != hover) { hover = h; setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }
}

void StageView::mouseDown (const juce::MouseEvent& e)
{
    dragging = hit (e.position);
    if (dragging < 0) return;
    grabOffset = e.position - centreOf (dragging);
    for (auto* id : { pan[dragging], depth[dragging] }) if (auto* p = proc.apvts.getParameter (id)) p->beginChangeGesture();
}

void StageView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto b = getLocalBounds().toFloat();
    const float front = b.getBottom() - 132.0f, back = b.getY() + 92.0f;
    const auto p = e.position - grabOffset;
    const float pn = juce::jlimit (-1.0f, 1.0f, (p.x - b.getCentreX()) / (b.getWidth() * 0.4f));
    proc.setParam (pan[dragging], pn);
    proc.setParam (depth[dragging], juce::jlimit (0.0f, 1.0f, (front - p.y - riserLift (pn * b.getWidth() * 0.4f, b.getWidth())) / (front - back)));
    repaint();
}

void StageView::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (auto* id : { pan[dragging], depth[dragging] }) if (auto* p = proc.apvts.getParameter (id)) p->endChangeGesture();
    dragging = -1;
}

void StageView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int s = hit (e.position);
    if (s < 0) return;
    for (auto* id : { pan[s], depth[s] })
        if (auto* p = proc.apvts.getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
}

//==============================================================================
juce::Slider* SanctumEditor::knob (const char* id, const juce::String& label, juce::Colour c, int cx, int cy, int size, bool bipolar)
{
    auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
    s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s->setColour (juce::Slider::rotarySliderFillColourId, c);
    s->setPopupDisplayEnabled (true, true, &panel, 1200);
    s->setMouseDragSensitivity (220);
    s->getProperties().set ("bipolar", bipolar);
    panel.addAndMakeVisible (s);
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, *s));
    if (auto* p = proc.apvts.getParameter (id)) s->setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    s->setBounds (cx - size / 2, cy - size / 2, size, size);
    labels.push_back ({ label, { cx - 50, cy + size / 2 + 3, 100, 14 } });
    learnClicks.add (*s, id);
    return s;
}

juce::ComboBox* SanctumEditor::combo (const char* id, juce::Rectangle<int> r)
{
    auto* c = combos.add (new juce::ComboBox());
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id))) c->addItemList (p->choices, 1);
    panel.addAndMakeVisible (c);
    c->setBounds (r);
    cAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, id, *c));
    learnClicks.add (*c, id);
    return c;
}

SanctumEditor::SanctumEditor (SanctumProcessor& p) : AudioProcessorEditor (p), proc (p), stage (p)
{
    panel.setLookAndFeel (&look);
    addAndMakeVisible (panel);
    panel.addAndMakeVisible (stage);
    stage.setBounds (kStage);

    // ---- header: presets
    for (auto* b : { &prevButton, &nextButton, &presetButton }) panel.addAndMakeVisible (b);
    prevButton.setBounds (W - 404, 15, 32, 30);
    presetButton.setBounds (W - 366, 15, 280, 30);
    nextButton.setBounds (W - 80, 15, 32, 30);
    prevButton.onClick = [this] { step (-1); };
    nextButton.onClick = [this] { step (1); };
    presetButton.onClick = [this] { showPresetMenu(); };

    // ---- harmony side panel
    const int x = kSide.getX() + 16, y = kSide.getY();
    combo (harmony, { x, y + 40, 324, 30 });
    combo (key,     { x, y + 96, 96, 30 });
    combo (scale,   { x + 106, y + 96, 218, 30 });
    panel.addAndMakeVisible (midiToggle);
    midiToggle.setBounds (x, y + 136, 130, 24);
    midiToggle.setTooltip ("When MIDI notes are held, the choir sings those notes instead of the harmony above");
    midiAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, midiChord, midiToggle);
    learnClicks.add (midiToggle, midiChord);
    knob (singers,  "SINGERS",  toota::ink,   x + 50,  y + 236, 66);
    knob (response, "RESPONSE", toota::inkSoft, x + 150, y + 240, 48);
    for (int s = 0; s < sc::kSections; ++s)
        knob (level[s], sc::kSectionNames[s], StageView::colourOf (s), x + 40 + s * 82, y + 362, 50);

    // ---- bottom row
    struct K { const char* id; const char* name; bool bip; juce::Colour c; };
    const K row[] {
        { humanise, "HUMANISE", false, toota::teal }, { spread, "SPREAD", false, toota::teal }, { character, "CHARACTER", false, toota::teal }, { formant, "FORMANT", true, toota::teal },
        { breath, "BREATH", false, toota::coral }, { tone, "TONE", true, toota::coral }, { energy, "ENERGY", false, toota::coral },
        { room, "SPACE", false, toota::plum }, { reverb, "REVERB", false, toota::plum },
        { sub, "SUB OCTAVE", false, toota::orange }, { lead, "LEAD", false, toota::orange }, { choir, "CHOIR", false, toota::orange }, { output, "OUTPUT", true, toota::ink } };
    const float stepX = (float) kKnobs.getWidth() / 13.0f;
    for (int i = 0; i < 13; ++i)
        knob (row[i].id, row[i].name, row[i].c, kKnobs.getX() + (int) (stepX * ((float) i + 0.5f)), kKnobs.getY() + 84, 54, row[i].bip);

    setResizable (true, true);
    setResizeLimits (W / 2, H / 2, W * 2, H * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    timerCallback();
    startTimerHz (30);
}

SanctumEditor::~SanctumEditor()
{
    learnClicks.removeAll();
    panel.setLookAndFeel (nullptr);
}

void SanctumEditor::resized()
{
    panel.setBounds (0, 0, W, H);
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) W));
}

void SanctumEditor::paint (juce::Graphics& g) { g.fillAll (toota::cream); }

void SanctumEditor::paintPanel (juce::Graphics& g)
{
    g.fillAll (toota::cream);
    toota::header (g, { 0, 0, W, 60 }, "SANCTUM", "choir and harmony generator");
    if (learnText.isNotEmpty()) toota::label (g, learnText, { 360, 18, 340, 26 }, juce::Justification::centredLeft, 12.0f, toota::coral);

    toota::panel (g, kSide.toFloat());
    toota::label (g, "HARMONY", { kSide.getX() + 16, kSide.getY() + 12, 200, 20 }, juce::Justification::centredLeft, 13.0f);
    toota::label (g, "KEY", { kSide.getX() + 16, kSide.getY() + 76, 96, 18 }, juce::Justification::centredLeft, 10.0f, toota::inkSoft);
    toota::label (g, "SCALE", { kSide.getX() + 122, kSide.getY() + 76, 120, 18 }, juce::Justification::centredLeft, 10.0f, toota::inkSoft);
    auto parts = juce::Rectangle<int> (kSide.getX() + 152, kSide.getY() + 134, 188, 28);
    toota::screen (g, parts.toFloat(), 8.0f);
    toota::label (g, partsText, parts.reduced (8, 0), juce::Justification::centredLeft, 10.0f);
    toota::label (g, "SECTIONS", { kSide.getX() + 16, kSide.getY() + 304, 200, 20 }, juce::Justification::centredLeft, 13.0f);

    toota::panel (g, kKnobs.toFloat());
    const float stepX = (float) kKnobs.getWidth() / 13.0f;
    struct G { int from, to; const char* name; };
    for (const G gr : { G { 0, 4, "ENSEMBLE" }, G { 4, 7, "COLOUR" }, G { 7, 9, "ROOM" }, G { 9, 13, "MIX" } })
    {
        const int x0 = kKnobs.getX() + (int) (stepX * (float) gr.from) + 8, x1 = kKnobs.getX() + (int) (stepX * (float) gr.to) - 8;
        toota::label (g, gr.name, { x0, kKnobs.getY() + 14, x1 - x0, 18 }, juce::Justification::centred, 11.5f, toota::inkSoft);
        g.setColour (toota::ink.withAlpha (0.2f));
        g.drawLine ((float) x0, (float) kKnobs.getY() + 36, (float) x1, (float) kKnobs.getY() + 36, 1.5f);
    }
    for (auto& l : labels) toota::label (g, l.text, l.r, juce::Justification::centred, 10.0f);
}

void SanctumEditor::timerCallback()
{
    presetButton.setButtonText (proc.presetName());
    juce::String pt;
    const int lead = proc.engine.leadNote.load();
    if (lead < 0) pt = "Sing to hear the parts";
    else
    {
        const char* ab[] { "S", "A", "T", "B" };
        for (int s = 0; s < sc::kSections; ++s)
        {
            const int n = proc.engine.note[(size_t) s].load();
            if (n >= 0) pt << ab[s] << " " << noteText (n) << "  ";
        }
    }
    juce::String lt;
    if (proc.midiLearn.isLearning()) lt = "MIDI LEARN: turn a knob for " + proc.midiLearn.learningName();
    if (pt != partsText || lt != learnText) { partsText = pt; learnText = lt; panel.repaint(); }
    stage.tick();
}

void SanctumEditor::step (int dir)
{
    const int n = (int) choirPresets().size();
    proc.loadPreset (((proc.currentPreset.load() + dir) % n + n) % n);
}

void SanctumEditor::showPresetMenu()
{
    juce::PopupMenu m;
    const auto& list = choirPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        m.addItem (juce::PopupMenu::Item (list[(size_t) i].name + "   -   " + list[(size_t) i].blurb).setID (i + 1).setTicked (i == proc.currentPreset.load()));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton), [this] (int r) { if (r > 0) proc.loadPreset (r - 1); });
}
