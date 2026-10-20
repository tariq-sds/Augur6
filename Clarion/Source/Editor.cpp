#include "Editor.h"

namespace k
{
    const juce::Colour bg     { 0xff0f1115 };
    const juce::Colour panel  { 0xff171a20 };
    const juce::Colour inset  { 0xff0b0d10 };
    const juce::Colour line   { 0xff262b33 };
    const juce::Colour text   { 0xffe8ecf1 };
    const juce::Colour dim    { 0xff8a93a0 };
    const juce::Colour vocal  { 0xffffb547 };   // warm gold for the voice
    const juce::Colour ref    { 0xff53b8ff };   // blue for the reference / music
    const juce::Colour ride   { 0xff7ee0a1 };
    const juce::Colour dyn    { 0xffc792ff };
}

static juce::Font f (float size, bool bold = true, float kern = 0.0f)
{
    return juce::Font (juce::FontOptions (size).withStyle (bold ? "Bold" : "Regular")).withExtraKerningFactor (kern);
}

//==============================================================================
ClLook::ClLook()
{
    setColour (juce::TextButton::textColourOffId, k::text);
    setColour (juce::TextButton::textColourOnId, k::bg);
    setColour (juce::PopupMenu::backgroundColourId, k::panel);
    setColour (juce::PopupMenu::textColourId, k::text);
}

void ClLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const bool bip = (bool) s.getProperties().getWithDefault ("bipolar", false);
    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 2, r - 2, 0, a0, a1, true);
    g.setColour (k::line);
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = bip ? (a0 + a1) * 0.5f : a0;
    if (std::abs (ang - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r - 2, r - 2, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (accent);
        g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float body = r - 7;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c313a), c.x, c.y - body, juce::Colour (0xff121419), c.x, c.y + body, false));
    g.fillEllipse (c.x - body, c.y - body, body * 2, body * 2);
    juce::Path p;
    p.addRoundedRectangle (-1.4f, -body + 3, 2.8f, body * 0.48f, 1.4f);
    g.setColour (k::text);
    g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void ClLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (1);
    const bool on = b.getToggleState();
    const auto accent = b.findColour (juce::ToggleButton::tickColourId);
    g.setColour (on ? accent.withAlpha (0.2f) : k::inset);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (on ? accent : (hi ? k::dim : k::line.brighter (0.3f)));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
    g.setColour (on ? k::text : k::dim);
    g.setFont (f (10.5f, true, 0.08f));
    g.drawText (b.getButtonText().toUpperCase(), r, juce::Justification::centred);
}

void ClLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1);
    const auto accent = b.findColour (juce::TextButton::buttonOnColourId);
    const bool on = b.getToggleState();
    const float prog = (float) b.getProperties().getWithDefault ("progress", 0.0f);
    g.setColour (on ? accent : accent.withAlpha (down ? 0.35f : (hi ? 0.22f : 0.12f)));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    if (prog > 0 && prog < 1)
    {
        g.saveState();
        g.reduceClipRegion (r.withWidth (r.getWidth() * prog).toNearestInt());
        g.setColour (accent.withAlpha (0.45f));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.restoreState();
    }
    g.setColour (accent);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);
}

juce::Font ClLook::getTextButtonFont (juce::TextButton&, int h) { return f (juce::jmin (13.0f, (float) h * 0.45f), true, 0.12f); }
juce::Font ClLook::getSliderPopupFont (juce::Slider&) { return f (12.0f); }

void ClLook::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body)
{
    g.setColour (juce::Colour (0xf00b0d10));
    g.fillRoundedRectangle (body, 4);
    g.setColour (k::dim);
    g.drawRoundedRectangle (body.reduced (0.5f), 4, 1);
}

//==============================================================================
static constexpr int kPanelTop = 66;
static const juce::Rectangle<int> keyBox  { 16, kPanelTop, 318, 360 };
static const juce::Rectangle<int> toneBox { 346, kPanelTop, 352, 360 };
static const juce::Rectangle<int> rideBox { 710, kPanelTop, 274, 360 };
static const juce::Rectangle<int> dynBox  { 16, 440, 470, 144 };
static const juce::Rectangle<int> outBox  { 498, 440, 486, 144 };

juce::Slider* ClarionPanel::knob (const char* id, const juce::String& label, juce::Colour accent, int cx, int cy, int size, bool bipolar)
{
    auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
    s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s->setColour (juce::Slider::rotarySliderFillColourId, accent);
    s->setPopupDisplayEnabled (true, true, this, 1200);
    s->setMouseDragSensitivity (220);
    s->getProperties().set ("bipolar", bipolar);
    addAndMakeVisible (s);
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, *s));
    if (auto* p = proc.apvts.getParameter (id)) s->setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    s->setBounds (cx - size / 2, cy - size / 2, size, size);
    labels.push_back ({ label, { cx - 50, cy + size / 2 + 2, 100, 13 } });
    return s;
}

juce::ToggleButton* ClarionPanel::toggle (const char* id, const juce::String& text, juce::Rectangle<int> r)
{
    auto* t = toggles.add (new juce::ToggleButton (text));
    addAndMakeVisible (t);
    t->setBounds (r);
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, id, *t));
    return t;
}

ClarionPanel::ClarionPanel (ClarionProcessor& p) : proc (p)
{
    setLookAndFeel (&look);

    // ---- key
    addAndMakeVisible (matchButton);
    matchButton.setColour (juce::TextButton::buttonOnColourId, k::vocal);
    matchButton.setBounds (keyBox.getX() + 14, keyBox.getY() + 300, 120, 40);
    matchButton.onClick = [this] { proc.matchKey(); refresh(); };
    matchButton.setTooltip ("Shift the vocal so it sits in the reference's key (or on its root note)");

    for (auto* b : { &keyModeButton, &rootModeButton })
    {
        addAndMakeVisible (b);
        b->setColour (juce::TextButton::buttonOnColourId, k::ref);
        b->setClickingTogglesState (false);
    }
    keyModeButton.setBounds (keyBox.getX() + 142, keyBox.getY() + 300, 46, 18);
    rootModeButton.setBounds (keyBox.getX() + 142, keyBox.getY() + 322, 46, 18);
    auto setMode = [this] (int m)
    {
        if (auto* prm = proc.apvts.getParameter (cp::matchMode))
        { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) m)); prm->endChangeGesture(); }
        refresh();
    };
    keyModeButton.onClick = [setMode] { setMode (0); };
    rootModeButton.onClick = [setMode] { setMode (1); };

    auto* hold = toggle (cp::freezeRef, "Hold ref", { keyBox.getX() + 14, keyBox.getY() + 262, 84, 22 });
    hold->setColour (juce::ToggleButton::tickColourId, k::ref);
    hold->setTooltip ("Freeze the reference analysis, e.g. after learning the key from your bassline");
    addAndMakeVisible (resetButton);
    resetButton.setColour (juce::TextButton::buttonOnColourId, k::dim);
    resetButton.setBounds (keyBox.getX() + 104, keyBox.getY() + 262, 64, 22);
    resetButton.onClick = [this] { proc.resetKeys(); };
    knob (cp::shift, "SHIFT", k::vocal, keyBox.getRight() - 52, keyBox.getY() + 312, 54, true);

    // ---- tone
    addAndMakeVisible (autoEqButton);
    autoEqButton.setColour (juce::TextButton::buttonOnColourId, k::vocal);
    autoEqButton.setBounds (toneBox.getRight() - 104, toneBox.getY() + 6, 92, 22);
    autoEqButton.onClick = [this] { proc.startAutoEq(); };
    autoEqButton.setTooltip ("Listen to the vocal for ~6 seconds and set high-pass, mud, presence and air");
    const int tx = toneBox.getX(), ty = toneBox.getY();
    knob (cp::hpf,      "HIGH-PASS", k::vocal, tx + 46,  ty + 214);
    knob (cp::mud,      "MUD",       k::vocal, tx + 132, ty + 214);
    knob (cp::mudFreq,  "MUD FREQ",  k::vocal, tx + 218, ty + 214, 44);
    knob (cp::deess,    "DE-ESS",    k::vocal, tx + 304, ty + 214);
    knob (cp::presence, "PRESENCE",  k::vocal, tx + 46,  ty + 304, 54, true);
    knob (cp::presFreq, "PRES FREQ", k::vocal, tx + 132, ty + 304, 44);
    knob (cp::air,      "AIR",       k::vocal, tx + 218, ty + 304);
    knob (cp::clarity,  "CLARITY",   k::ref,   tx + 304, ty + 304);

    // ---- ride
    auto* rideT = toggle (cp::ride, "Ride", { rideBox.getRight() - 76, rideBox.getY() + 6, 64, 22 });
    rideT->setColour (juce::ToggleButton::tickColourId, k::ride);
    knob (cp::target, "TARGET", k::ride, rideBox.getX() + 196, rideBox.getY() + 92, 54, true);
    knob (cp::range,  "RANGE",  k::ride, rideBox.getX() + 196, rideBox.getY() + 186);
    knob (cp::speed,  "SPEED",  k::ride, rideBox.getX() + 196, rideBox.getY() + 280);

    // ---- dynamics / output
    knob (cp::drive,  "DRIVE",    k::dyn, dynBox.getX() + 60,  dynBox.getY() + 70, 60);
    knob (cp::parMix, "PARALLEL", k::dyn, dynBox.getX() + 160, dynBox.getY() + 70, 60);
    knob (cp::glue,   "GROOVE GLUE", k::dyn, dynBox.getX() + 260, dynBox.getY() + 70, 60);
    knob (cp::output, "OUTPUT", k::text, outBox.getRight() - 60, outBox.getY() + 70, 60, true);

    setSize (W, H);
    refresh();
}

ClarionPanel::~ClarionPanel()
{
    setLookAndFeel (nullptr);
}

void ClarionPanel::refresh()
{
    const bool rootMode = (int) proc.apvts.getRawParameterValue (cp::matchMode)->load() == 1;
    keyModeButton.setToggleState (! rootMode, juce::dontSendNotification);
    rootModeButton.setToggleState (rootMode, juce::dontSendNotification);
    const int st = proc.autoEqState.load();
    autoEqButton.getProperties().set ("progress", st == 1 ? proc.autoEqProgress.load() : 0.0f);
    autoEqButton.setButtonText (st == 1 ? "LISTENING" : "AUTO EQ");
    riderShown += (proc.riderDb.load() - riderShown) * 0.5f;
    repaint();
}

void ClarionPanel::drawChroma (juce::Graphics& g, juce::Rectangle<float> r, const std::array<std::atomic<float>, 12>& h, juce::Colour col, int highlight)
{
    float mx = 1e-9f;
    for (auto& v : h) mx = juce::jmax (mx, v.load());
    const float bw = r.getWidth() / 12.0f;
    for (int i = 0; i < 12; ++i)
    {
        const float v = h[(size_t) i].load() / mx;
        auto bar = juce::Rectangle<float> (r.getX() + i * bw + 2, r.getY(), bw - 4, r.getHeight() - 12);
        g.setColour (k::inset);
        g.fillRoundedRectangle (bar, 2);
        g.setColour (i == highlight ? col : col.withAlpha (0.55f));
        g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * v), 2);
        g.setColour (i == highlight ? k::text : k::dim);
        g.setFont (f (8.5f, true));
        g.drawText (cl::noteName (i), juce::Rectangle<float> (r.getX() + i * bw, r.getBottom() - 11, bw, 11), juce::Justification::centred);
    }
}

void ClarionPanel::drawEq (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (k::inset);
    g.fillRoundedRectangle (r, 6);
    const double sr = 48000.0;
    auto P = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const float fmin = 20, fmax = 20000;
    auto xFor = [&] (float hz) { return r.getX() + r.getWidth() * std::log (hz / fmin) / std::log (fmax / fmin); };
    auto yFor = [&] (float db) { return r.getCentreY() - db / 15.0f * r.getHeight() * 0.5f; };
    g.setColour (k::line);
    for (float hz : { 100.f, 1000.f, 10000.f }) g.drawVerticalLine ((int) xFor (hz), r.getY(), r.getBottom());
    g.drawHorizontalLine ((int) yFor (0), r.getX(), r.getRight());
    g.setFont (f (8.5f, false));
    g.setColour (k::dim);
    g.drawText ("100", (int) xFor (100) + 2, (int) r.getBottom() - 12, 30, 10, juce::Justification::left);
    g.drawText ("1k", (int) xFor (1000) + 2, (int) r.getBottom() - 12, 30, 10, juce::Justification::left);
    g.drawText ("10k", (int) xFor (10000) + 2, (int) r.getBottom() - 12, 30, 10, juce::Justification::left);

    using C = juce::dsp::IIR::Coefficients<float>;
    std::vector<C::Ptr> cs;
    cs.push_back (C::makeHighPass (sr, P (cp::hpf), 0.541f));
    cs.push_back (C::makeHighPass (sr, P (cp::hpf), 1.307f));
    cs.push_back (C::makePeakFilter (sr, P (cp::mudFreq), 1.0f, juce::Decibels::decibelsToGain (-P (cp::mud))));
    cs.push_back (C::makePeakFilter (sr, P (cp::presFreq), 0.9f, juce::Decibels::decibelsToGain (P (cp::presence))));
    cs.push_back (C::makeHighShelf (sr, 9500.0f, 0.7f, juce::Decibels::decibelsToGain (P (cp::air))));
    std::vector<C::Ptr> dyn;
    const float clarHz[3] { 1200, 2500, 4200 };
    for (int b = 0; b < 3; ++b) dyn.push_back (C::makePeakFilter (sr, clarHz[b], 1.2f, juce::Decibels::decibelsToGain (proc.clarityDb[(size_t) b].load())));
    dyn.push_back (C::makeHighShelf (sr, 5000.0f, 0.7f, juce::Decibels::decibelsToGain (proc.deessGr.load())));

    juce::Path stat, live;
    for (float x = r.getX(); x <= r.getRight(); x += 1.5f)
    {
        const double hz = fmin * std::pow (fmax / fmin, (x - r.getX()) / r.getWidth());
        double m = 1;
        for (auto& c : cs) m *= c->getMagnitudeForFrequency (hz, sr);
        double md = m;
        for (auto& c : dyn) md *= c->getMagnitudeForFrequency (hz, sr);
        const float y1 = juce::jlimit (r.getY(), r.getBottom(), yFor ((float) juce::Decibels::gainToDecibels (m)));
        const float y2 = juce::jlimit (r.getY(), r.getBottom(), yFor ((float) juce::Decibels::gainToDecibels (md)));
        if (x == r.getX()) { stat.startNewSubPath (x, y1); live.startNewSubPath (x, y2); }
        else { stat.lineTo (x, y1); live.lineTo (x, y2); }
    }
    g.setColour (k::ref.withAlpha (0.85f));
    g.strokePath (live, juce::PathStrokeType (1.5f));
    g.setColour (k::vocal);
    g.strokePath (stat, juce::PathStrokeType (2.0f));
}

void ClarionPanel::paint (juce::Graphics& g)
{
    g.fillAll (k::bg);

    // header
    g.setColour (k::text);
    g.setFont (f (24.0f, true, 0.24f));
    g.drawText ("CLARION", 18, 12, 220, 28, juce::Justification::centredLeft);
    g.setColour (k::dim);
    g.setFont (f (9.5f, true, 0.2f));
    g.drawText ("VOCAL CLARITY   |   TOOTA AUDIO", 19, 40, 300, 12, juce::Justification::centredLeft);
    const bool sc = proc.musicPresent.load();
    g.setColour (sc ? k::ride : k::vocal);
    g.fillEllipse (560, 26, 8, 8);
    g.setFont (f (10.0f, true, 0.06f));
    g.drawText (sc ? "SIDECHAIN: MUSIC DETECTED" : "SIDECHAIN: ROUTE YOUR MUSIC / REFERENCE HERE", 574, 22, 400, 16, juce::Justification::centredLeft);

    auto box = [&] (juce::Rectangle<int> b, const juce::String& title, juce::Colour acc)
    {
        g.setColour (k::panel);
        g.fillRoundedRectangle (b.toFloat(), 8);
        g.setColour (acc);
        g.fillRoundedRectangle (b.toFloat().withHeight (3).reduced (12, 0), 1.5f);
        g.setFont (f (10.5f, true, 0.2f));
        g.drawText (title, b.getX() + 14, b.getY() + 10, 200, 14, juce::Justification::centredLeft);
    };
    box (keyBox, "KEY", k::vocal);
    box (toneBox, "TONE", k::vocal);
    box (rideBox, "RIDE", k::ride);
    box (dynBox, "PARALLEL DYNAMICS", k::dyn);
    box (outBox, "MIX", k::text);

    // ---- key panel
    const auto vk = proc.vocalKey();
    const auto rk = proc.refKey();
    const int root = proc.refRoot();
    const int shift = juce::roundToInt (proc.apvts.getRawParameterValue (cp::shift)->load());
    auto keyStr = [] (cl::KeyResult kr) { return kr.tonic < 0 ? juce::String ("--") : juce::String (cl::noteName (kr.tonic)) + (kr.minor ? " minor" : " major"); };
    const int kx = keyBox.getX() + 14;
    g.setColour (k::dim); g.setFont (f (9.0f, true, 0.15f));
    g.drawText ("VOCAL", kx, keyBox.getY() + 30, 120, 12, juce::Justification::centredLeft);
    g.setColour (k::vocal); g.setFont (f (24.0f, true));
    g.drawText (keyStr (vk), kx, keyBox.getY() + 42, 190, 28, juce::Justification::centredLeft);

    // live note
    const float hz = proc.liveHz.load();
    juce::String note = "-";
    float cents = 0;
    if (hz > 0)
    {
        const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        const int m = (int) std::lround (midi);
        cents = (midi - (float) m) * 100.0f;
        note = juce::String (cl::noteName (m)) + juce::String (m / 12 - 1);
    }
    g.setColour (k::text); g.setFont (f (15.0f, true));
    g.drawText (note, keyBox.getRight() - 112, keyBox.getY() + 30, 98, 18, juce::Justification::centredRight);
    auto tun = juce::Rectangle<float> ((float) keyBox.getRight() - 112, (float) keyBox.getY() + 54, 98, 5);
    g.setColour (k::inset); g.fillRoundedRectangle (tun, 2);
    g.setColour (k::dim); g.fillRect (tun.getCentreX() - 0.5f, tun.getY() - 2, 1.0f, 9.0f);
    if (hz > 0)
    {
        g.setColour (std::abs (cents) < 15 ? k::ride : k::vocal);
        const float cx = tun.getCentreX() + cents / 50.0f * tun.getWidth() * 0.5f;
        g.fillEllipse (cx - 4, tun.getCentreY() - 4, 8, 8);
    }
    drawChroma (g, { (float) kx, (float) keyBox.getY() + 76, 290, 52 }, proc.vocalHist, k::vocal, vk.tonic);

    g.setColour (k::dim); g.setFont (f (9.0f, true, 0.15f));
    g.drawText ("REFERENCE (SIDECHAIN)", kx, keyBox.getY() + 140, 200, 12, juce::Justification::centredLeft);
    g.setColour (k::ref); g.setFont (f (20.0f, true));
    g.drawText (keyStr (rk), kx, keyBox.getY() + 152, 180, 26, juce::Justification::centredLeft);
    g.setColour (k::text); g.setFont (f (12.0f, true));
    g.drawText ("Root " + juce::String (root >= 0 ? cl::noteName (root) : "--"), keyBox.getRight() - 112, keyBox.getY() + 156, 98, 18, juce::Justification::centredRight);
    drawChroma (g, { (float) kx, (float) keyBox.getY() + 184, 290, 52 }, proc.refHist, k::ref, rk.tonic);

    const int sug = proc.suggestedShift();
    g.setColour (k::dim); g.setFont (f (10.0f, false));
    juce::String after = vk.tonic >= 0 ? (juce::String (cl::noteName (vk.tonic + shift)) + (vk.minor ? " minor" : " major")) : "--";
    g.drawText ("Suggested " + juce::String (sug > 0 ? "+" : "") + juce::String (sug) + " st   |   Now: " + after,
                kx, keyBox.getY() + 240, 300, 14, juce::Justification::centredLeft);

    // ---- tone panel
    drawEq (g, { (float) toneBox.getX() + 14, (float) toneBox.getY() + 34, (float) toneBox.getWidth() - 28, 120 });
    g.setFont (f (8.5f, true, 0.08f));
    g.setColour (k::vocal); g.drawText ("YOUR EQ", toneBox.getX() + 20, toneBox.getY() + 156, 80, 12, juce::Justification::centredLeft);
    g.setColour (k::ref);   g.drawText ("+ LIVE CLARITY / DE-ESS", toneBox.getX() + 80, toneBox.getY() + 156, 200, 12, juce::Justification::centredLeft);

    // ---- ride panel: big gain meter + vocal vs music levels
    {
        const float range = juce::jmax (1.0f, proc.apvts.getRawParameterValue (cp::range)->load());
        auto m = juce::Rectangle<float> ((float) rideBox.getX() + 22, (float) rideBox.getY() + 40, 34, 290);
        g.setColour (k::inset); g.fillRoundedRectangle (m, 6);
        const float mid = m.getCentreY();
        const float v = juce::jlimit (-1.0f, 1.0f, riderShown / range);
        const float y = mid - v * m.getHeight() * 0.5f;
        g.setColour (k::ride.withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (m.getX() + 3, juce::jmin (y, mid), m.getWidth() - 6, std::abs (y - mid) + 1), 3);
        g.setColour (k::dim); g.drawHorizontalLine ((int) mid, m.getX(), m.getRight());
        g.setFont (f (8.5f, false));
        g.drawText ("+" + juce::String ((int) range), (int) m.getRight() + 2, (int) m.getY(), 30, 10, juce::Justification::left);
        g.drawText ("-" + juce::String ((int) range), (int) m.getRight() + 2, (int) m.getBottom() - 10, 30, 10, juce::Justification::left);
        g.setColour (k::text); g.setFont (f (13.0f, true));
        g.drawText ((riderShown >= 0 ? "+" : "") + juce::String (riderShown, 1) + " dB", rideBox.getX() + 14, rideBox.getBottom() - 26, 90, 16, juce::Justification::centredLeft);

        auto lvl = [&] (float x, float dbv, juce::Colour c, const char* name)
        {
            auto b = juce::Rectangle<float> (x, (float) rideBox.getY() + 40, 12, 290);
            g.setColour (k::inset); g.fillRoundedRectangle (b, 3);
            const float fr = juce::jlimit (0.0f, 1.0f, (dbv + 60.0f) / 60.0f);
            g.setColour (c); g.fillRoundedRectangle (b.withTop (b.getBottom() - b.getHeight() * fr), 3);
            g.setColour (k::dim); g.setFont (f (8.0f, true));
            g.drawText (name, (int) x - 10, rideBox.getBottom() - 26, 32, 10, juce::Justification::centred);
        };
        lvl ((float) rideBox.getX() + 96, proc.vocalDb.load(), k::vocal, "VOX");
        lvl ((float) rideBox.getX() + 118, proc.musicDb.load(), k::ref, "MUSIC");
    }

    // ---- dynamics GR meter
    {
        auto m = juce::Rectangle<float> ((float) dynBox.getRight() - 140, (float) dynBox.getY() + 46, 120, 10);
        g.setColour (k::inset); g.fillRoundedRectangle (m, 3);
        const float fr = juce::jlimit (0.0f, 1.0f, -proc.compGr.load() / 24.0f);
        g.setColour (k::dyn); g.fillRoundedRectangle (m.withWidth (m.getWidth() * fr), 3);
        g.setColour (k::dim); g.setFont (f (8.5f, true, 0.1f));
        g.drawText ("PARALLEL BUS GR  " + juce::String (proc.compGr.load(), 1) + " dB", (int) m.getX(), (int) m.getBottom() + 4, 160, 12, juce::Justification::centredLeft);
        g.setFont (f (9.5f, false));
        g.drawText ("Glue pulses the dense layer", (int) m.getX(), (int) m.getBottom() + 26, 160, 12, juce::Justification::centredLeft);
        g.drawText ("with the music's drums.", (int) m.getX(), (int) m.getBottom() + 40, 160, 12, juce::Justification::centredLeft);
    }

    // ---- mix panel: clarity band readouts + tips
    {
        const char* names[] { "1.2k", "2.5k", "4.2k" };
        g.setFont (f (9.0f, true, 0.1f));
        g.setColour (k::dim);
        g.drawText ("CLARITY LIFT (WHERE THE MUSIC MASKS THE VOCAL)", outBox.getX() + 14, outBox.getY() + 32, 330, 12, juce::Justification::centredLeft);
        for (int b = 0; b < 3; ++b)
        {
            auto bar = juce::Rectangle<float> ((float) outBox.getX() + 14, (float) outBox.getY() + 52 + b * 24, 220, 12);
            g.setColour (k::inset); g.fillRoundedRectangle (bar, 3);
            const float v = juce::jlimit (0.0f, 1.0f, proc.clarityDb[(size_t) b].load() / 7.0f);
            g.setColour (k::ref); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * v), 3);
            g.setColour (k::text); g.setFont (f (9.0f, true));
            g.drawText (juce::String (names[b]) + "  +" + juce::String (proc.clarityDb[(size_t) b].load(), 1) + " dB", (int) bar.getRight() + 8, (int) bar.getY(), 110, 12, juce::Justification::centredLeft);
        }
    }

    // knob labels
    g.setColour (k::dim);
    g.setFont (f (9.0f, true, 0.1f));
    for (auto& l : labels) g.drawText (l.text, l.r, juce::Justification::centred);
}

//==============================================================================
ClarionEditor::ClarionEditor (ClarionProcessor& p) : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    setResizable (true, true);
    setResizeLimits (700, 420, 1600, 960);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) ClarionPanel::W / ClarionPanel::H);
    setSize (ClarionPanel::W, ClarionPanel::H);
    startTimerHz (25);
}

void ClarionEditor::resized()
{
    panel.setBounds (0, 0, ClarionPanel::W, ClarionPanel::H);
    panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) ClarionPanel::W));
}
