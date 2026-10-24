#include "PluginEditor.h"
#include "Presets.h"
#include <complex>

namespace tt = toota;

//==============================================================================
// LED keys: cream sticker with a coral LED
void WayLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedRight (2.0f).withTrimmedBottom (2.0f);
    if (down) r = r.translated (1.0f, 1.0f);
    tt::sticker (g, r, highlighted ? tt::cream2 : tt::cream, 6.0f, down ? 0.0f : 2.0f, 1.6f);
    const bool on = b.getToggleState();
    const float d = 9.0f;
    const auto c = juce::Point<float> (r.getX() + 11.0f, r.getCentreY());
    g.setColour (on ? tt::coral : tt::cream3); g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
    g.setColour (tt::ink); g.drawEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.3f);
    g.setFont (tt::font (11.0f, 2, 0.02f));
    g.drawFittedText (b.getButtonText().toUpperCase(), r.withTrimmedLeft (21.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

//==============================================================================
TouchPad::TouchPad (WayfarerProcessor& p) : proc (p)
{
    px = p.apvts.getParameter (pid::padX);
    py = p.apvts.getParameter (pid::padY);
    setTooltip ("Touch pad: drag to move X and Y (pick what they control on the right). Both are automatable. Double-click to return to the corner.");
    startTimerHz (30);
}

TouchPad::~TouchPad() { proc.padTouched = false; }

void TouchPad::setFrom (juce::Point<float> pt)
{
    const auto a = area();
    const float x = juce::jlimit (0.0f, 1.0f, (pt.x - a.getX()) / a.getWidth());
    const float y = juce::jlimit (0.0f, 1.0f, 1.0f - (pt.y - a.getY()) / a.getHeight());
    px->setValueNotifyingHost (px->convertTo0to1 (x));
    py->setValueNotifyingHost (py->convertTo0to1 (y));
}

void TouchPad::mouseDown (const juce::MouseEvent& e)
{
    touching = true;
    proc.padTouched = true;
    px->beginChangeGesture(); py->beginChangeGesture();
    setFrom (e.position);
}
void TouchPad::mouseDrag (const juce::MouseEvent& e) { setFrom (e.position); }
void TouchPad::mouseUp (const juce::MouseEvent&)
{
    touching = false;
    proc.padTouched = false;
    px->endChangeGesture(); py->endChangeGesture();
}
void TouchPad::mouseDoubleClick (const juce::MouseEvent&)
{
    for (auto* prm : { px, py }) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->getDefaultValue()); prm->endChangeGesture(); }
}

void TouchPad::paint (juce::Graphics& g)
{
    const auto a = area();
    tt::sticker (g, a, tt::cream3, 12.0f, 3.0f, 2.2f);
    g.setColour (tt::ink.withAlpha (0.12f));
    for (int k = 1; k < 8; ++k)
    {
        g.drawVerticalLine ((int) (a.getX() + a.getWidth() * k / 8.0f), a.getY() + 4, a.getBottom() - 4);
        g.drawHorizontalLine ((int) (a.getY() + a.getHeight() * k / 8.0f), a.getX() + 4, a.getRight() - 4);
    }
    const float x = px->convertFrom0to1 (px->getValue()), y = py->convertFrom0to1 (py->getValue());
    const juce::Point<float> c (a.getX() + x * a.getWidth(), a.getY() + (1.0f - y) * a.getHeight());
    g.setColour (tt::coral.withAlpha (0.35f));
    g.drawLine (c.x, a.getY() + 3, c.x, a.getBottom() - 3, 2.0f);
    g.drawLine (a.getX() + 3, c.y, a.getRight() - 3, c.y, 2.0f);
    const float r = touching ? 15.0f : 11.0f;
    g.setColour (tt::ink); g.fillEllipse (c.x - r + 2, c.y - r + 2, r * 2, r * 2);
    g.setColour (touching ? tt::coral : tt::orange); g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (tt::ink); g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 2.2f);
    tt::label (g, "X", { (int) a.getRight() - 22, (int) a.getBottom() - 22, 16, 16 }, juce::Justification::centred, 11.0f, tt::inkSoft);
    tt::label (g, "Y", { (int) a.getX() + 6, (int) a.getY() + 6, 16, 16 }, juce::Justification::centred, 11.0f, tt::inkSoft);
}

//==============================================================================
void FilterView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    tt::screen (g, r, 10.0f);
    r = r.reduced (8.0f, 6.0f);
    auto get = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    const float cut = get (pid::cutoff), space = get (pid::spacing), res = get (pid::res);
    const int mode = (int) get (pid::fMode);
    const double k = res * 4.1;
    const double lo = 20.0, hi = 20000.0;
    auto xOf = [&] (double hz) { return r.getX() + (float) (std::log (hz / lo) / std::log (hi / lo)) * r.getWidth(); };
    auto yOf = [&] (double db) { return r.getY() + (float) ((12.0 - db) / 60.0) * r.getHeight(); };

    g.setColour (tt::ink.withAlpha (0.1f));
    for (double f : { 100.0, 1000.0, 10000.0 }) g.drawVerticalLine ((int) xOf (f), r.getY(), r.getBottom());
    g.drawHorizontalLine ((int) yOf (0), r.getX(), r.getRight());

    auto ladder = [k] (double hz, double fc)
    {
        const std::complex<double> s (0.0, hz / fc), one (1.0, 0.0);
        const auto g4 = std::pow (one / (one + s), 4.0);
        return std::abs (g4 / (one + k * g4)) * (1.0 + k);
    };
    auto highpass = [] (double hz, double fc) { const std::complex<double> s (0.0, hz / fc), one (1.0, 0.0); return std::abs (std::pow (s / (one + s), 4.0)); };

    auto curve = [&] (std::function<double (double)> mag, juce::Colour col)
    {
        juce::Path p;
        for (int i = 0; i <= 200; ++i)
        {
            const double hz = lo * std::pow (hi / lo, i / 200.0);
            const double db = juce::jlimit (-48.0, 12.0, 20.0 * std::log10 (std::max (1e-9, mag (hz))));
            const float x = xOf (hz), y = yOf (db);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (col); g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    const double fl = cut, fr = cut * std::exp2 (space);
    if (mode == 0)
    {
        if (std::abs (space) > 0.01f) curve ([&] (double hz) { return ladder (hz, fr); }, tt::coral);
        curve ([&] (double hz) { return ladder (hz, fl); }, tt::teal);
        tt::label (g, std::abs (space) > 0.01f ? "L  R" : "LP / LP", r.toNearestInt().removeFromTop (14).removeFromRight (60), juce::Justification::centredRight, 10.0f, tt::inkSoft);
    }
    else
    {
        curve ([&] (double hz) { return highpass (hz, fl) * ladder (hz, fr); }, tt::orange);
        tt::label (g, "HP / LP", r.toNearestInt().removeFromTop (14).removeFromRight (60), juce::Justification::centredRight, 10.0f, tt::inkSoft);
    }
}

//==============================================================================
juce::Rectangle<int> WayfarerPanel::section (const juce::String& title, int x, int y, int w, int h, juce::Colour accent)
{
    sections.push_back ({ title, { x, y, w, h }, accent });
    return { x, y, w, h };
}

juce::Slider* WayfarerPanel::knob (const char* id, const juce::String& label, int cx, int cy, int size, juce::Colour accent, bool bipolar)
{
    auto* sl = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
    sl->setLookAndFeel (&look);
    sl->setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    sl->setPopupDisplayEnabled (true, true, this, 900);
    sl->setMouseDragSensitivity (240);
    sl->getProperties().set ("bipolar", bipolar);
    sl->setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (sl);
    sliderAtts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, *sl));
    if (auto* param = proc.apvts.getParameter (id))
    {
        sl->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        sl->setTooltip (param->getName (64));
    }
    sl->setBounds (cx - size / 2, cy - size / 2, size, size);
    if (label.isNotEmpty()) caption (label, { cx - 44, cy + size / 2 + 1, 88, 14 }, 10.5f, tt::ink);
    return sl;
}

void WayfarerPanel::toggle (const char* id, const juce::String& text, juce::Rectangle<int> r)
{
    auto* t = toggles.add (new juce::ToggleButton (text));
    t->setLookAndFeel (&look);
    addAndMakeVisible (t);
    t->setBounds (r);
    if (auto* param = proc.apvts.getParameter (id)) t->setTooltip (param->getName (64));
    buttonAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, id, *t));
}

void WayfarerPanel::combo (const char* id, juce::Rectangle<int> r, const juce::String& cap)
{
    auto* cb = combos.add (new juce::ComboBox());
    cb->setLookAndFeel (&look);
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
    {
        cb->addItemList (param->choices, 1);
        cb->setTooltip (param->getName (64));
    }
    addAndMakeVisible (cb);
    cb->setBounds (r);
    comboAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, id, *cb));
    if (cap.isNotEmpty()) caption (cap, { r.getX() - 58, r.getY(), 54, r.getHeight() }, 10.0f, tt::inkSoft);
}

WayfarerPanel::WayfarerPanel (WayfarerProcessor& p) : proc (p), pad (p), filterView (p)
{
    setLookAndFeel (&look);
    using namespace pid;
    const auto teal = tt::teal, coral = tt::coral, orange = tt::orange;
    constexpr int row1 = 86, row1H = 352, row2 = 450, row2H = 336, bandH = 104, top = 34;

    // ---------------- MODULATION
    {
        auto s = section ("MODULATION", 40, row1, 330, row1H, coral);
        const char* chips[] { "LFO", "MOD 1  WHEEL", "MOD 2" };
        for (int b = 0; b < 3; ++b)
        {
            const int by = s.getY() + top + b * bandH;
            bubbles.push_back ({ { s.getX() + 10, by, 92, 18 }, chips[b] });
            const int cx = s.getX() + 56, cy = by + 48;
            const int rx = s.getX() + 166, rw = 152;
            if (b == 0)
            {
                knob (lfoRate, "RATE", cx, cy, 52, coral);
                combo (lfoSync, { rx, by + 10, rw, 26 }, "SYNC");
                combo (lfoDiv, { rx, by + 42, rw, 26 }, "TEMPO");
                caption ("TEMPO rate is used when SYNC = Tempo", { rx - 58, by + 72, rw + 58, 14 }, 9.5f);
            }
            else
            {
                knob (b == 1 ? m1Amt : m2Amt, b == 1 ? "AMOUNT" : "AMOUNT", cx, cy, 52, coral);
                combo (b == 1 ? m1Src : m2Src,     { rx, by + 4, rw, 24 }, "SOURCE");
                combo (b == 1 ? m1Shape : m2Shape, { rx, by + 34, rw, 24 }, "SHAPE");
                combo (b == 1 ? m1Dest : m2Dest,   { rx, by + 64, rw, 24 }, "DEST");
            }
        }
    }

    // ---------------- OSCILLATORS
    {
        auto s = section ("OSCILLATORS", 380, row1, 420, row1H, teal);
        for (int o = 0; o < 3; ++o)
        {
            const int by = s.getY() + top + o * bandH;
            bubbles.push_back ({ { s.getX() + 12, by + 36, 26, 26 }, juce::String (o + 1) });
            const char* oct = o == 0 ? o1Oct : o == 1 ? o2Oct : o3Oct;
            const char* wave = o == 0 ? o1Wave : o == 1 ? o2Wave : o3Wave;
            combo (oct, { s.getX() + 48, by + 36, 74, 26 });
            caption ("OCTAVE", { s.getX() + 48, by + 64, 74, 14 }, 10.5f, tt::ink);
            if (o > 0) knob (o == 1 ? o2Freq : o3Freq, "FREQUENCY", s.getX() + 162, by + 46, 54, teal, true);
            else caption ("master pitch", { s.getX() + 120, by + 40, 84, 14 }, 9.5f);
            knob (wave, "WAVE", s.getX() + 240, by + 46, 54, teal);
            caption ("TRI  SAW  SQR  PULSE", { s.getX() + 186, by + 88, 108, 12 }, 8.5f);
            const int sx = s.getX() + 300;
            if (o == 0) knob (fm, "3 > 1 FM", sx + 52, by + 46, 46, teal);
            if (o == 1) toggle (hardSync, "1-2 Sync", { sx, by + 34, 112, 26 });
            if (o == 2)
            {
                toggle (o3KbOff, "3 KB Off", { sx, by + 18, 112, 26 });
                toggle (o3Lo, "3 Lo Freq", { sx, by + 52, 112, 26 });
            }
        }
    }

    // ---------------- MIXER
    {
        auto s = section ("MIXER", 810, row1, 150, row1H, orange);
        const int ca = s.getX() + 42, cb = s.getX() + 108;
        for (int k = 0; k < 3; ++k)
            knob (k == 0 ? mix1 : k == 1 ? mix2 : mix3, "OSC " + juce::String (k + 1), ca, s.getY() + top + k * bandH + 44, 50, orange);
        knob (noise, "NOISE", cb, s.getY() + top + 44, 50, orange);
        knob (overload, "OVERLOAD", cb, s.getY() + top + bandH + 44, 50, coral);
        caption ("drive the filters", { cb - 44, s.getY() + top + bandH + 86, 88, 12 }, 9.0f);
    }

    // ---------------- FILTERS
    {
        auto s = section ("FILTERS", 970, row1, 350, row1H, teal);
        addAndMakeVisible (filterView);
        filterView.setBounds (s.getX() + 14, s.getY() + top, s.getWidth() - 28, 104);
        knob (cutoff, "CUTOFF", s.getX() + 68, s.getY() + 232, 92, teal);
        knob (res, "RESONANCE", s.getX() + 172, s.getY() + 190, 48, teal);
        knob (spacing, "SPACING", s.getX() + 238, s.getY() + 190, 48, teal, true);
        knob (envAmt, "ENV AMT", s.getX() + 304, s.getY() + 190, 48, coral, true);
        knob (kb, "KB CONTROL", s.getX() + 172, s.getY() + 286, 48, teal);
        knob (fVel, "VELOCITY", s.getX() + 238, s.getY() + 286, 48, coral);
        combo (fMode, { s.getX() + 268, s.getY() + 274, 76, 26 });
        caption ("MODE", { s.getX() + 268, s.getY() + 302, 76, 14 }, 10.5f, tt::ink);
    }

    // ---------------- ENVELOPES
    {
        auto s = section ("ENVELOPES", 40, row2, 420, row2H, coral);
        const char* names[] { "ATTACK", "DECAY", "SUSTAIN", "RELEASE" };
        const char* fid[] { fA, fD, fS, fR };
        const char* aid[] { aA, aD, aS, aR };
        bubbles.push_back ({ { s.getX() + 12, s.getY() + top + 32, 70, 22 }, "FILTER" });
        bubbles.push_back ({ { s.getX() + 12, s.getY() + top + 134, 70, 22 }, "AMP" });
        for (int k = 0; k < 4; ++k)
        {
            knob (fid[k], names[k], s.getX() + 128 + k * 68, s.getY() + top + 42, 52, coral);
            knob (aid[k], names[k], s.getX() + 128 + k * 68, s.getY() + top + 144, 52, orange);
        }
        knob (aVel, "VELOCITY", s.getX() + 128 + 4 * 66, s.getY() + top + 144, 40, orange);
        combo (gate, { s.getX() + 76, s.getY() + top + 234, 120, 26 }, "GATE");
        toggle (release, "Release", { s.getX() + 214, s.getY() + top + 234, 110, 26 });
        caption ("Keys = play normally   LFO = re-trigger at the LFO rate   Drone = always on", { s.getX() + 14, s.getY() + top + 268, s.getWidth() - 28, 14 }, 9.5f);
    }

    // ---------------- KEYS
    {
        auto s = section ("KEYS", 470, row2, 200, row2H, orange);
        knob (glide, "GLIDE", s.getX() + 50, s.getY() + top + 44, 54, orange);
        toggle (glideOn, "Glide", { s.getX() + 96, s.getY() + top + 14, 92, 26 });
        toggle (legato, "Legato", { s.getX() + 96, s.getY() + top + 48, 92, 26 });
        combo (priority, { s.getX() + 96, s.getY() + top + 110, 92, 26 }, "PRIORITY");
        knob (bend, "BEND", s.getX() + 50, s.getY() + top + 210, 48, orange);
        knob (tune, "FINE TUNE", s.getX() + 140, s.getY() + top + 210, 48, orange, true);
    }

    // ---------------- OUTPUT
    {
        auto s = section ("OUTPUT", 680, row2, 270, row2H, teal);
        knob (drift, "DRIFT", s.getX() + 50, s.getY() + top + 44, 50, teal);
        knob (volume, "VOLUME", s.getX() + 135, s.getY() + top + 44, 60, teal, true);
        knob (choMix, "CHORUS", s.getX() + 220, s.getY() + top + 44, 50, teal);
        knob (dlyMix, "DELAY", s.getX() + 42, s.getY() + top + 150, 44, coral);
        knob (dlyFb, "FEEDBACK", s.getX() + 104, s.getY() + top + 150, 44, coral);
        knob (revMix, "REVERB", s.getX() + 166, s.getY() + top + 150, 44, orange);
        knob (revSize, "SIZE", s.getX() + 228, s.getY() + top + 150, 44, orange);
        combo (dlyDiv, { s.getX() + 74, s.getY() + top + 222, 100, 26 }, "DELAY");
        ledArea = { s.getX() + 196, s.getY() + top + 224, 60, 22 };
    }

    // ---------------- TOUCH PAD
    {
        auto s = section ("TOUCH PAD", 960, row2, 360, row2H, coral);
        addAndMakeVisible (pad);
        pad.setBounds (s.getX() + 10, s.getY() + top, 228, 228);
        const int rx = s.getX() + 250;
        caption ("X CONTROLS", { rx, s.getY() + top + 2, 100, 14 }, 10.0f, tt::ink);
        combo (padXDest, { rx, s.getY() + top + 18, 100, 24 });
        knob (padXAmt, "X AMOUNT", rx + 50, s.getY() + top + 74, 44, coral, true);
        caption ("Y CONTROLS", { rx, s.getY() + top + 114, 100, 14 }, 10.0f, tt::ink);
        combo (padYDest, { rx, s.getY() + top + 130, 100, 24 });
        knob (padYAmt, "Y AMOUNT", rx + 50, s.getY() + top + 186, 44, coral, true);
        toggle (padGate, "Pad Gate", { s.getX() + 10, s.getY() + top + 240, 120, 26 });
        caption ("touching the pad plays the envelopes", { s.getX() + 134, s.getY() + top + 240, 216, 26 }, 9.5f);
    }

    // ---------------- header / presets
    for (auto* b : { &prevButton, &nextButton, &presetButton })
    {
        b->setLookAndFeel (&look);
        addAndMakeVisible (b);
    }
    prevButton.setBounds (520, 18, 34, 36);
    presetButton.setBounds (560, 18, 380, 36);
    nextButton.setBounds (946, 18, 34, 36);
    presetButton.setTooltip ("Browse presets");
    const int numPresets = (int) factoryPresets().size();
    prevButton.onClick = [this, numPresets] { const int cur = proc.currentPreset.load(); proc.loadPreset (cur <= 0 ? numPresets - 1 : cur - 1); refresh(); };
    nextButton.onClick = [this, numPresets] { const int cur = proc.currentPreset.load(); proc.loadPreset (cur < 0 || cur >= numPresets - 1 ? 0 : cur + 1); refresh(); };
    presetButton.onClick = [this] { showPresetMenu(); };

    setSize (kWidth, kHeight);
    refresh();
}

WayfarerPanel::~WayfarerPanel()
{
    for (auto* s : sliders) s->setLookAndFeel (nullptr);
    for (auto* t : toggles) t->setLookAndFeel (nullptr);
    for (auto* c : combos)  c->setLookAndFeel (nullptr);
    for (auto* b : { &prevButton, &nextButton, &presetButton }) b->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void WayfarerPanel::showPresetMenu()
{
    const auto& list = factoryPresets();
    juce::PopupMenu menu;
    menu.setLookAndFeel (&look);
    menu.addItem (1, "Init", true, proc.currentPreset.load() < 0);
    menu.addSeparator();
    for (const auto& cat : presetCategories())
    {
        juce::PopupMenu sub;
        bool containsCurrent = false;
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i].category == cat)
            {
                const bool cur = proc.currentPreset.load() == i;
                containsCurrent |= cur;
                sub.addItem (i + 2, list[(size_t) i].name, true, cur);
            }
        menu.addSubMenu (cat, sub, true, nullptr, containsCurrent);
    }
    juce::Component::SafePointer<WayfarerPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (240),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            safe->proc.loadPreset (result - 2);
                            safe->refresh();
                        });
}

void WayfarerPanel::refresh()
{
    const auto& list = factoryPresets();
    const int idx = proc.currentPreset.load();
    const juce::String text = juce::isPositiveAndBelow (idx, (int) list.size())
                                  ? list[(size_t) idx].category.toUpperCase() + "   " + proc.getPresetDisplayName()
                                  : proc.getPresetDisplayName();
    if (text != lastPresetText) { lastPresetText = text; presetButton.setButtonText (text); }
    const bool led = proc.sounding.load();
    if (led != lastLed) { lastLed = led; repaint (ledArea.expanded (4)); }
}

void WayfarerPanel::drawCheek (juce::Graphics& g, juce::Rectangle<float> r)
{
    // the wooden end cheeks, as chunky brand stripes
    tt::sticker (g, r, tt::orange, 8.0f, 0.0f, 2.2f);
    const float h = r.getHeight() / 3.0f;
    g.setColour (tt::coral); g.fillRect (r.reduced (2.2f).withTop (r.getY() + h).withHeight (h));
    g.setColour (tt::teal);  g.fillRect (r.reduced (2.2f).withTop (r.getY() + h * 2).withBottom (r.getBottom() - 8));
    g.setColour (tt::ink); g.drawRoundedRectangle (r.reduced (1.1f), 8.0f, 2.2f);
}

void WayfarerPanel::paint (juce::Graphics& g)
{
    g.fillAll (tt::cream);
    tt::header (g, { 30, 0, kWidth - 60, 72 }, "WAYFARER", "three-oscillator mono synth");
    drawCheek (g, { 6.0f, 6.0f, 22.0f, (float) kHeight - 12.0f });
    drawCheek (g, { (float) kWidth - 28.0f, 6.0f, 22.0f, (float) kHeight - 12.0f });

    for (auto& s : sections)
    {
        auto r = s.area.toFloat();
        tt::panel (g, r, 14.0f);
        auto tab = juce::Rectangle<float> (r.getX() + 12, r.getY() - 10, juce::jmax (90.0f, 14.0f + 8.6f * s.title.length()), 24);
        tt::sticker (g, tab, s.accent, 8.0f, 2.0f, 2.0f);
        g.setColour (s.accent == tt::orange ? tt::ink : tt::cream);
        g.setFont (tt::font (12.5f, 2, 0.08f));
        g.drawText (s.title, tab.toNearestInt(), juce::Justification::centred);
    }
    for (auto& [r, t] : bubbles)
    {
        const bool round = t.length() == 1;
        tt::sticker (g, r.toFloat(), round ? tt::teal : tt::cream3, round ? r.getHeight() * 0.5f : 6.0f, round ? 2.0f : 0.0f, 1.8f);
        g.setColour (round ? tt::cream : tt::ink);
        g.setFont (tt::font (round ? 15.0f : 10.5f, 2, 0.06f));
        g.drawText (t, r, juce::Justification::centred);
    }
    for (auto& c : captions)
    {
        g.setColour (c.colour);
        g.setFont (tt::font (c.size, 2, 0.05f));
        g.drawFittedText (c.text, c.area, juce::Justification::centred, 1, 0.8f);
    }
    // note LED
    const auto lr = ledArea.toFloat();
    const float d = 14.0f;
    g.setColour (lastLed ? tt::coral : tt::cream3);
    g.fillEllipse (lr.getX() + 4, lr.getCentreY() - d * 0.5f, d, d);
    g.setColour (tt::ink); g.drawEllipse (lr.getX() + 4, lr.getCentreY() - d * 0.5f, d, d, 2.0f);
    tt::label (g, "NOTE", lr.withTrimmedLeft (22).toNearestInt(), juce::Justification::centredLeft, 10.5f, tt::inkSoft);
}

//==============================================================================
WayfarerEditor::WayfarerEditor (WayfarerProcessor& p) : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    const double ratio = (double) WayfarerPanel::kWidth / (double) WayfarerPanel::kHeight;
    setResizable (true, true);
    setResizeLimits ((int) (WayfarerPanel::kWidth * 0.55), (int) (WayfarerPanel::kHeight * 0.55),
                     (int) (WayfarerPanel::kWidth * 1.5), (int) (WayfarerPanel::kHeight * 1.5));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio (ratio);
    setSize ((int) (WayfarerPanel::kWidth * 0.8), (int) (WayfarerPanel::kHeight * 0.8));
    startTimerHz (15);
}

void WayfarerEditor::paint (juce::Graphics& g) { g.fillAll (tt::cream); }

void WayfarerEditor::resized()
{
    const float scale = (float) getWidth() / (float) WayfarerPanel::kWidth;
    panel.setBounds (0, 0, WayfarerPanel::kWidth, WayfarerPanel::kHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));
}
