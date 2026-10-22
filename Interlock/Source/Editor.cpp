#include "Editor.h"

namespace ic
{
    const juce::Colour bg      = toota::cream;
    const juce::Colour panel   = toota::cream2;
    const juce::Colour plotBg  = toota::cream3;
    const juce::Colour grid    = toota::ink.withAlpha (0.16f);
    const juce::Colour text    = toota::ink;
    const juce::Colour dim     = toota::inkSoft;
    const juce::Colour kick    = toota::coral;
    const juce::Colour bass    = toota::teal;
    const juce::Colour comp    = toota::plum;
    const juce::Colour curve   = toota::ink;
}

static juce::Font fnt (float size, bool bold = true, float kern = 0.0f) { return toota::font (size * 1.08f, bold ? 2 : 1, kern * 0.5f); }

//==============================================================================
void ILLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool hi, bool down)
{
    toota::Look::drawButtonBackground (g, b, c, hi, down);
    const float prog = (float) b.getProperties().getWithDefault ("progress", 0.0f);
    if (prog > 0.0f && prog < 1.0f)
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedRight (2.5f).withTrimmedBottom (2.5f);
        g.saveState();
        g.reduceClipRegion (r.withWidth (r.getWidth() * prog).toNearestInt());
        g.setColour (toota::coral.withAlpha (0.55f));
        g.fillRoundedRectangle (r.reduced (2), r.getHeight() * 0.5f);
        g.restoreState();
    }
}

//==============================================================================
static constexpr float kFMin = 20.0f, kFMax = 2000.0f, kEqRange = 18.0f;

EqDisplay::EqDisplay (InterlockProcessor& p) : proc (p) {}

float EqDisplay::xForFreq (float f) const { return plot.getX() + plot.getWidth() * std::log (f / kFMin) / std::log (kFMax / kFMin); }
float EqDisplay::freqForX (float x) const { return kFMin * std::pow (kFMax / kFMin, (x - plot.getX()) / plot.getWidth()); }
float EqDisplay::yForEq (float db) const { return plot.getCentreY() - db / kEqRange * plot.getHeight() * 0.5f; }
float EqDisplay::eqForY (float y) const { return (plot.getCentreY() - y) / (plot.getHeight() * 0.5f) * kEqRange; }
float EqDisplay::yForSpec (float db) const { return plot.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 84.0f) / 84.0f) * plot.getHeight(); }

static float pv (InterlockProcessor& p, const char* id) { return p.apvts.getRawParameterValue (id)->load(); }

juce::Point<float> EqDisplay::dipNode() const   { return { xForFreq (pv (proc, ilp::dipFreq)), yForEq (-pv (proc, ilp::dipDepth)) }; }
juce::Point<float> EqDisplay::boostNode() const { return { xForFreq (pv (proc, ilp::boostFreq)), yForEq (pv (proc, ilp::boostGain)) }; }

int EqDisplay::hitNode (juce::Point<float> pt) const
{
    if (pt.getDistanceFrom (dipNode()) < 16) return 0;
    if (pt.getDistanceFrom (boostNode()) < 16) return 1;
    return -1;
}

void EqDisplay::setParam (const char* id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (v)));
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitNode (e.position);
    if (dragging >= 0)
        for (auto* id : dragging == 0 ? std::initializer_list<const char*> { ilp::dipFreq, ilp::dipDepth }
                                      : std::initializer_list<const char*> { ilp::boostFreq, ilp::boostGain })
            proc.apvts.getParameter (id)->beginChangeGesture();
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const float f = freqForX (e.position.x), db = eqForY (e.position.y);
    if (dragging == 0) { setParam (ilp::dipFreq, f); setParam (ilp::dipDepth, -db); }
    else               { setParam (ilp::boostFreq, f); setParam (ilp::boostGain, db); }
    repaint();
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (auto* id : dragging == 0 ? std::initializer_list<const char*> { ilp::dipFreq, ilp::dipDepth }
                                      : std::initializer_list<const char*> { ilp::boostFreq, ilp::boostGain })
            proc.apvts.getParameter (id)->endChangeGesture();
    dragging = -1;
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int h = hitNode (e.position);
    if (h != hover) { hover = h; setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (hitNode (e.position) == 0)
    {
        auto* p = proc.apvts.getParameter (ilp::dipQ);
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + w.deltaY * 0.25f));
    }
}

juce::Path EqDisplay::spectrumPath (std::array<std::atomic<float>, il::Analyzer::kBins + 1>& data, bool closed) const
{
    juce::Path p;
    const float binHz = proc.analyzer.displayBinHz.load();
    bool started = false;
    for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
    {
        const float f = freqForX (x);
        const float bf = f / binHz;
        const int b0 = juce::jlimit (0, il::Analyzer::kBins - 1, (int) bf);
        const float t = juce::jlimit (0.0f, 1.0f, bf - (float) b0);
        const float db = data[(size_t) b0].load (std::memory_order_relaxed) * (1 - t) + data[(size_t) b0 + 1].load (std::memory_order_relaxed) * t;
        const float y = yForSpec (db);
        if (! started) { p.startNewSubPath (x, closed ? plot.getBottom() : y); if (closed) p.lineTo (x, y); started = true; }
        else p.lineTo (x, y);
    }
    if (closed) { p.lineTo (plot.getRight(), plot.getBottom()); p.closeSubPath(); }
    return p;
}

void EqDisplay::paint (juce::Graphics& g)
{
    plot = getLocalBounds().toFloat().reduced (14, 10).withTrimmedBottom (14);
    toota::screen (g, getLocalBounds().toFloat().reduced (1), 10);

    // grid
    g.setFont (fnt (10.0f, false));
    for (float f : { 20.f, 30.f, 40.f, 50.f, 60.f, 80.f, 100.f, 150.f, 200.f, 300.f, 400.f, 500.f, 700.f, 1000.f, 2000.f })
    {
        const float x = xForFreq (f);
        const bool major = f == 50 || f == 100 || f == 200 || f == 500 || f == 1000;
        g.setColour (ic::grid.withAlpha (major ? 1.0f : 0.55f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (ic::dim.withAlpha (0.8f));
        g.drawText (f >= 1000 ? juce::String ((int) f / 1000) + "k" : juce::String ((int) f), (int) x - 20, (int) plot.getBottom() + 1, 40, 12, juce::Justification::centred);
    }
    for (float db : { -12.f, -6.f, 0.f, 6.f, 12.f })
    {
        const float y = yForEq (db);
        g.setColour (ic::grid.withAlpha (db == 0 ? 1.0f : 0.5f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (ic::dim.withAlpha (0.6f));
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), (int) plot.getRight() - 30, (int) y - 12, 28, 11, juce::Justification::centredRight);
    }

    // spectra
    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    auto bassFill = spectrumPath (proc.analyzer.bassDisplay, true);
    g.setGradientFill (juce::ColourGradient (ic::bass.withAlpha (0.35f), 0, plot.getY(), ic::bass.withAlpha (0.04f), 0, plot.getBottom(), false));
    g.fillPath (bassFill);
    g.setColour (ic::bass.withAlpha (0.85f));
    g.strokePath (spectrumPath (proc.analyzer.bassDisplay, false), juce::PathStrokeType (1.4f));
    auto kickFill = spectrumPath (proc.analyzer.kickDisplay, true);
    g.setGradientFill (juce::ColourGradient (ic::kick.withAlpha (0.28f), 0, plot.getY(), ic::kick.withAlpha (0.02f), 0, plot.getBottom(), false));
    g.fillPath (kickFill);
    g.setColour (ic::kick.withAlpha (0.9f));
    g.strokePath (spectrumPath (proc.analyzer.kickDisplay, false), juce::PathStrokeType (1.4f));

    // EQ curves: the full-depth dip (ghost) and the live curve that moves with each kick
    const double sr = 48000.0;
    const float dipF = pv (proc, ilp::dipFreq), dipQ = pv (proc, ilp::dipQ), depth = pv (proc, ilp::dipDepth);
    const float duck = pv (proc, ilp::duck), bF = pv (proc, ilp::boostFreq), bG = pv (proc, ilp::boostGain);
    const float liveFrac = depth > 0.01f ? juce::jlimit (0.0f, 1.0f, -proc.dipNowDb.load() / depth) : 0.0f;

    auto curveFor = [&] (float frac)
    {
        auto c1 = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, dipF, dipQ, juce::Decibels::decibelsToGain (-depth * frac));
        auto c2 = juce::dsp::IIR::Coefficients<float>::makeLowShelf (sr, dipF * 1.6f, 0.707f, juce::Decibels::decibelsToGain (-duck * frac));
        auto c3 = juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, bF, 0.9f, juce::Decibels::decibelsToGain (bG));
        juce::Path p;
        for (float x = plot.getX(); x <= plot.getRight(); x += 1.5f)
        {
            const double f = freqForX (x);
            const double mag = c1->getMagnitudeForFrequency (f, sr) * c2->getMagnitudeForFrequency (f, sr) * c3->getMagnitudeForFrequency (f, sr);
            const float y = yForEq ((float) juce::Decibels::gainToDecibels (mag));
            if (x == plot.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        return p;
    };

    auto ghost = curveFor (1.0f);
    juce::Path dashed;
    const float dash[] { 5.0f, 4.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, ghost, dash, 2);
    g.setColour (ic::kick.withAlpha (0.6f));
    g.fillPath (dashed);

    auto live = curveFor (liveFrac);
    juce::Path liveFill (live);
    liveFill.lineTo (plot.getRight(), yForEq (0)); liveFill.lineTo (plot.getX(), yForEq (0)); liveFill.closeSubPath();
    g.setColour (ic::curve.withAlpha (0.07f));
    g.fillPath (liveFill);
    g.setColour (ic::curve);
    g.strokePath (live, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();

    // nodes
    auto drawNode = [&] (juce::Point<float> c, juce::Colour col, const juce::String& name, const juce::String& val, bool hot)
    {
        const float r = hot ? 9.0f : 7.5f;
        g.setColour (col.withAlpha (0.25f));
        g.fillEllipse (c.x - r * 2, c.y - r * 2, r * 4, r * 4);
        g.setColour (col);
        g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (toota::ink);
        g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 2.0f);
        g.setColour (col);
        g.setFont (fnt (10.5f, true, 0.08f));
        const float ly = c.y < plot.getCentreY() ? c.y + 14 : c.y - 34;
        g.drawText (name, juce::Rectangle<float> (c.x - 60, ly, 120, 12), juce::Justification::centred);
        g.setColour (ic::text);
        g.setFont (fnt (11.0f, false));
        g.drawText (val, juce::Rectangle<float> (c.x - 60, ly + 12, 120, 12), juce::Justification::centred);
    };
    drawNode (dipNode(), ic::kick, "KICK DIP", juce::String (juce::roundToInt (dipF)) + " Hz  -" + juce::String (depth, 1) + " dB", hover == 0 || dragging == 0);
    drawNode (boostNode(), ic::bass, "BASS FOCUS", juce::String (juce::roundToInt (bF)) + " Hz  " + (bG >= 0 ? "+" : "") + juce::String (bG, 1) + " dB", hover == 1 || dragging == 1);

    // legend
    g.setFont (fnt (10.5f, true, 0.08f));
    g.setColour (ic::kick);  g.drawText ("KICK (SIDECHAIN)", (int) plot.getX() + 8, (int) plot.getY() + 4, 140, 14, juce::Justification::centredLeft);
    g.setColour (ic::bass);  g.drawText ("BASS (INPUT)", (int) plot.getX() + 150, (int) plot.getY() + 4, 120, 14, juce::Justification::centredLeft);
}

//==============================================================================
InterlockEditor::InterlockEditor (InterlockProcessor& p) : AudioProcessorEditor (p), proc (p), display (p)
{
    look.pillToggles = true;
    setLookAndFeel (&look);
    addAndMakeVisible (display);

    addAndMakeVisible (learnButton);
    learnButton.onClick = [this] { proc.startLearn(); };
    learnButton.setColour (juce::TextButton::buttonColourId, toota::orange);
    learnButton.setTooltip ("Play kick and bass together; Interlock listens for ~5 seconds and sets everything up");

    for (auto* t : { &monoButton, &deltaButton }) { t->setColour (juce::ToggleButton::tickColourId, toota::teal); addAndMakeVisible (t); }
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ilp::monoLows, monoButton));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ilp::delta, deltaButton));
    deltaButton.setTooltip ("Hear only what Interlock removes or adds");

    groups.push_back ({ "KICK DIP", 0, 3, ic::kick });
    knob (ilp::dipFreq, "FREQ", ic::kick);
    knob (ilp::dipDepth, "DEPTH", ic::kick);
    knob (ilp::dipQ, "WIDTH", ic::kick);
    groups.push_back ({ "BREATHE", 3, 2, toota::orange });
    knob (ilp::duck, "SUB DUCK", toota::orange);
    knob (ilp::release, "RELEASE", toota::orange);
    groups.push_back ({ "BASS FOCUS", 5, 2, ic::bass });
    knob (ilp::boostFreq, "FREQ", ic::bass);
    knob (ilp::boostGain, "GAIN", ic::bass);
    groups.push_back ({ "PARALLEL", 7, 2, ic::comp });
    knob (ilp::drive, "DRIVE", ic::comp);
    knob (ilp::parMix, "MIX", ic::comp);
    groups.push_back ({ "OUTPUT", 9, 1, ic::curve });
    knob (ilp::output, "GAIN", ic::curve);
    sliders[6]->getProperties().set ("bipolar", true);
    sliders[9]->getProperties().set ("bipolar", true);

    setResizable (true, true);
    setResizeLimits (760, 440, 1600, 920);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (940.0 / 540.0);
    setSize (940, 540);
    startTimerHz (30);
}

InterlockEditor::~InterlockEditor()
{
    for (auto* s : sliders) s->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void InterlockEditor::knob (const char* id, const juce::String& label, juce::Colour accent)
{
    auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
    s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s->setColour (juce::Slider::rotarySliderFillColourId, accent);
    s->setPopupDisplayEnabled (true, true, this, 1200);
    s->setMouseDragSensitivity (220);
    addAndMakeVisible (s);
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, *s));
    if (auto* prm = proc.apvts.getParameter (id))
        s->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    labels.add (label);
}

void InterlockEditor::timerCallback()
{
    const int st = proc.analyzer.learnState.load();
    const float prog = proc.analyzer.learnProgress.load();
    learnButton.getProperties().set ("progress", st == 1 ? prog : 0.0f);
    learnButton.setButtonText (st == 1 ? "LISTENING " + juce::String (juce::roundToInt (prog * 100)) + "%"
                                       : (proc.hasLearned.load() ? "RE-LEARN" : "LEARN"));
    scOk = proc.sidechainLevel.load() > 0.5f;

    if (st == 1)
        status = "Listening to kick and bass together...";
    else if (proc.hasLearned.load())
        status = "Kick " + juce::String (juce::roundToInt (proc.learnedKickHz.load())) + " Hz   |   Bass focus "
               + juce::String (juce::roundToInt (proc.learnedBoostHz.load())) + " Hz   |   Overlap "
               + juce::String (juce::roundToInt (proc.learnedOverlap.load() * 100)) + "%";
    else
        status = "Play kick and bass together to auto-learn, or press LEARN";

    dipMeter = juce::jmax (proc.dipNowDb.load() + proc.duckNowDb.load() * 0.5f, dipMeter - 1.5f);
    dipMeter = juce::jmin (dipMeter, proc.dipNowDb.load());
    compMeter = proc.compGrDb.load();
    learnButton.repaint();
    display.repaint();
    repaint (0, 0, getWidth(), 60);
    repaint (getWidth() - 70, 60, 70, getHeight());
}

void InterlockEditor::resized()
{
    const float s = (float) getWidth() / 940.0f;
    auto sc = [s] (int v) { return juce::roundToInt ((float) v * s); };
    learnButton.setBounds (sc (330), sc (14), sc (150), sc (32));
    monoButton.setBounds (sc (760), sc (16), sc (80), sc (26));
    deltaButton.setBounds (sc (846), sc (16), sc (78), sc (26));
    display.setBounds (sc (16), sc (60), sc (860), sc (316));

    // knob row
    int x = 16;
    const int y = 404, kw = 78;
    int idx = 0;
    for (auto& gr : groups)
    {
        for (int i = 0; i < gr.count; ++i, ++idx)
            sliders[idx]->setBounds (sc (x + 10 + i * kw), sc (y + 12), sc (58), sc (58));
        x += gr.count * kw + 22;
    }
}

void InterlockEditor::paint (juce::Graphics& g)
{
    g.fillAll (ic::bg);
    const float s = (float) getWidth() / 940.0f;
    g.addTransform (juce::AffineTransform::scale (s));

    // header
    toota::header (g, { 0, 0, 940, 56 }, "INTERLOCK", "kick + bass balancer");
    g.setFont (fnt (11.0f, false));
    g.setColour (ic::text.withAlpha (0.85f));
    g.drawText (status, 494, 14, 260, 16, juce::Justification::centredLeft, true);

    // sidechain status
    const juce::Colour led = scOk ? toota::teal : ic::kick;
    g.setColour (led);
    g.fillEllipse (494, 36, 8, 8);
    g.setFont (fnt (10.0f, true, 0.06f));
    g.drawText (scOk ? "SIDECHAIN: KICK DETECTED" : "SIDECHAIN: ROUTE YOUR KICK HERE", 508, 33, 260, 14, juce::Justification::centredLeft);

    // meters
    auto meter = [&] (int mx, const juce::String& name, float db, juce::Colour col)
    {
        const juce::Rectangle<float> r ((float) mx, 72, 18, 290);
        toota::screen (g, r, 5);
        const float frac = juce::jlimit (0.0f, 1.0f, -db / 18.0f);
        g.setColour (col);
        g.fillRoundedRectangle (r.withHeight (r.getHeight() * frac), 4);
        g.setColour (ic::dim);
        g.setFont (fnt (8.5f, true, 0.05f));
        g.drawText (name, mx - 8, 364, 34, 12, juce::Justification::centred);
    };
    meter (886, "DIP", dipMeter, ic::kick);
    meter (910, "COMP", compMeter, ic::comp);

    // knob groups
    int x = 16, idx = 0;
    const int y = 390;
    for (auto& gr : groups)
    {
        const int w = gr.count * 78;
        const juce::Rectangle<float> box ((float) x, (float) y, (float) w, 130);
        toota::panel (g, box, 12);
        g.setColour (gr.accent.withAlpha (0.9f));
        g.fillRoundedRectangle (box.withHeight (3).reduced (10, 0), 1.5f);
        g.setFont (fnt (10.0f, true, 0.18f));
        g.drawText (gr.title, box.withTop (box.getBottom() - 24).toNearestInt(), juce::Justification::centred);
        for (int i = 0; i < gr.count; ++i, ++idx)
        {
            g.setColour (ic::dim);
            g.setFont (fnt (9.5f, true, 0.1f));
            g.drawText (labels[idx], x + i * 78, y + 76, 78, 14, juce::Justification::centred);
        }
        x += w + 22;
    }
}
