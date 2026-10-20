#include "PluginEditor.h"
#include "Presets.h"

namespace colours
{
    const juce::Colour panel     { 0xff141414 };
    const juce::Colour panelHi   { 0xff1c1c1c };
    const juce::Colour line      { 0xff3a3833 };
    const juce::Colour legend    { 0xffe9e2d0 };
    const juce::Colour legendDim { 0xffa9a294 };
    const juce::Colour amber     { 0xffe8a33d };
    const juce::Colour led       { 0xffff3b2f };
    const juce::Colour ledOff    { 0xff3a1512 };
}

static juce::Font uiFont (float size, bool bold = false, float kerning = 0.0f)
{
    auto opts = juce::FontOptions (size).withStyle (bold ? "Bold" : "Regular");
    return juce::Font (opts).withExtraKerningFactor (kerning);
}

//==============================================================================
AugurLook::AugurLook()
{
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1b1b1b));
    setColour (juce::PopupMenu::textColourId, colours::legend);
    setColour (juce::PopupMenu::headerTextColourId, colours::amber);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::amber);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
    setColour (juce::ComboBox::textColourId, colours::amber);
    setColour (juce::TextButton::textColourOffId, colours::amber);
    setColour (juce::TextButton::textColourOnId, colours::amber);
    setColour (juce::BubbleComponent::backgroundColourId, juce::Colour (0xee0d0d0d));
    setColour (juce::BubbleComponent::outlineColourId, colours::amber.withAlpha (0.6f));
    setColour (juce::TooltipWindow::textColourId, colours::legend);
}

void AugurLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                  float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);

    // scale track and value arc
    juce::Path track;
    track.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff2c2a27));
    g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, r - 1.5f, r - 1.5f, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (colours::amber.withAlpha (s.isEnabled() ? 0.95f : 0.35f));
        g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // skirt
    const float body = r - 6.0f;
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (c.x - body + 1.0f, c.y - body + 2.5f, body * 2.0f, body * 2.0f);
    juce::ColourGradient skirt (juce::Colour (0xff3b3936), c.x, c.y - body,
                                juce::Colour (0xff0b0b0b), c.x, c.y + body, false);
    g.setGradientFill (skirt);
    g.fillEllipse (c.x - body, c.y - body, body * 2.0f, body * 2.0f);
    g.setColour (juce::Colour (0xff050505));
    g.drawEllipse (c.x - body, c.y - body, body * 2.0f, body * 2.0f, 1.0f);

    // brushed cap
    const float cap = body * 0.62f;
    juce::ColourGradient capGrad (juce::Colour (0xff4a4844), c.x - cap, c.y - cap,
                                  juce::Colour (0xff1f1e1c), c.x + cap, c.y + cap, false);
    g.setGradientFill (capGrad);
    g.fillEllipse (c.x - cap, c.y - cap, cap * 2.0f, cap * 2.0f);

    // pointer
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.2f, -body + 1.5f, 2.4f, body * 0.55f, 1.0f);
    g.setColour (colours::legend);
    g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

void AugurLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? juce::Colour (0xff101010) : juce::Colour (0xff1f1e1c));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (highlighted ? colours::legendDim : colours::line);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    const bool on = b.getToggleState();
    const float d = 6.0f;
    const auto ledC = juce::Point<float> (r.getX() + 9.0f, r.getCentreY());
    if (on)
    {
        g.setColour (colours::led.withAlpha (0.25f));
        g.fillEllipse (ledC.x - d, ledC.y - d, d * 2.0f, d * 2.0f);
    }
    g.setColour (on ? colours::led : colours::ledOff);
    g.fillEllipse (ledC.x - d * 0.5f, ledC.y - d * 0.5f, d, d);

    g.setColour (on ? colours::legend : colours::legendDim);
    g.setFont (uiFont (10.5f, true, 0.04f));
    g.drawFittedText (b.getButtonText().toUpperCase(), r.withTrimmedLeft (17.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.8f);
}

void AugurLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colour (0xff0e0e0e));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.hasKeyboardFocus (true) ? colours::legendDim : colours::line);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 10.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 2.5f);
    g.setColour (colours::legendDim);
    g.fillPath (arrow);
}

juce::Font AugurLook::getComboBoxFont (juce::ComboBox&) { return uiFont (11.0f, true); }

void AugurLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 18, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void AugurLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? juce::Colour (0xff070707) : juce::Colour (0xff0e0e0e));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (highlighted ? colours::amber.withAlpha (0.7f) : colours::line);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

juce::Font AugurLook::getTextButtonFont (juce::TextButton&, int h) { return uiFont (juce::jmin (14.0f, h * 0.55f), true, 0.03f); }

void AugurLook::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body)
{
    g.setColour (juce::Colour (0xf00b0b0b));
    g.fillRoundedRectangle (body, 3.0f);
    g.setColour (colours::amber.withAlpha (0.7f));
    g.drawRoundedRectangle (body.reduced (0.5f), 3.0f, 1.0f);
}

juce::Font AugurLook::getSliderPopupFont (juce::Slider&) { return uiFont (12.0f, true); }

//==============================================================================
namespace
{
    constexpr int kCellW = 68, kCellH = 104, kPad = 8, kTitleH = 26;
    constexpr int kRow1Y = 70, kRow2Y = 320, kRow3Y = 570;
    constexpr int kContentX = 38;
}

AugurPanel::Section& AugurPanel::section (const juce::String& title, int x, int y, int cols, int rows)
{
    sections.push_back ({ title, { x, y, cols * kCellW + kPad * 2, kTitleH + rows * kCellH + 6 } });
    return sections.back();
}

juce::Rectangle<int> AugurPanel::cell (const Section& s, float col, float row) const
{
    return { s.area.getX() + kPad + juce::roundToInt (col * kCellW),
             s.area.getY() + kTitleH + juce::roundToInt (row * kCellH), kCellW, kCellH };
}

void AugurPanel::caption (const juce::String& text, juce::Rectangle<int> area, bool small)
{
    captions.push_back ({ text, area, small });
}

void AugurPanel::knob (const Section& s, const char* id, const juce::String& label, float col, float row, bool bipolar)
{
    auto* sl = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
    sl->setLookAndFeel (&look);
    sl->setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    sl->setPopupDisplayEnabled (true, true, this, 900);
    sl->setMouseDragSensitivity (220);
    sl->getProperties().set ("bipolar", bipolar);
    sl->setName (label);
    addAndMakeVisible (sl);
    sliderAtts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, *sl));
    if (auto* param = proc.apvts.getParameter (id))
        sl->setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));

    const auto c = cell (s, col, row);
    sl->setBounds (c.getX() + 7, c.getY() + 2, 54, 54);
    caption (label, { c.getX() - 4, c.getY() + 57, kCellW + 8, 14 }, false);
}

void AugurPanel::toggle (const juce::Rectangle<int>& r, const char* id, const juce::String& text)
{
    auto* t = toggles.add (new juce::ToggleButton (text));
    t->setLookAndFeel (&look);
    addAndMakeVisible (t);
    t->setBounds (r);
    buttonAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, id, *t));
}

void AugurPanel::combo (const juce::Rectangle<int>& r, const char* id)
{
    auto* cb = combos.add (new juce::ComboBox());
    cb->setLookAndFeel (&look);
    if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        cb->addItemList (param->choices, 1);
    addAndMakeVisible (cb);
    cb->setBounds (r);
    comboAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, id, *cb));
}

AugurPanel::AugurPanel (Augur6Processor& p) : proc (p)
{
    setLookAndFeel (&look);
    sections.reserve (24);

    auto widthOf = [] (int cols) { return cols * kCellW + kPad * 2; };
    constexpr int gap = 10;

    // ---------------- Row 1
    int x = kContentX;
    auto& o1 = section ("OSCILLATOR 1", x, kRow1Y, 3, 2);      x += widthOf (3) + gap;
    auto& o2 = section ("OSCILLATOR 2", x, kRow1Y, 3, 2);      x += widthOf (3) + gap;
    auto& mx = section ("MIXER", x, kRow1Y, 2, 2);             x += widthOf (2) + gap;
    auto& pm = section ("POLY MOD", x, kRow1Y, 3, 2);          x += widthOf (3) + gap;
    auto& lf = section ("LFO", x, kRow1Y, 4, 2);

    knob (o1, pid::osc1Freq,  "FREQUENCY", 0, 0, true);
    knob (o1, pid::osc1Shape, "SHAPE", 1, 0);
    knob (o1, pid::osc1PW,    "PULSE WIDTH", 2, 0, true);
    knob (o1, pid::osc1Sub,   "SUB OCTAVE", 0, 1);
    { auto c = cell (o1, 1, 1); toggle ({ c.getX() + 3, c.getY() + 18, 64, 22 }, pid::osc1Sync, "Sync"); }
    caption ("TRI > SAW > PULSE", { cell (o1, 1, 0).getX() - 14, cell (o1, 1, 0).getY() + 71, kCellW + 28, 12 });

    knob (o2, pid::osc2Freq,  "FREQUENCY", 0, 0, true);
    knob (o2, pid::osc2Fine,  "FINE", 1, 0, true);
    knob (o2, pid::osc2Shape, "SHAPE", 2, 0);
    knob (o2, pid::osc2PW,    "PULSE WIDTH", 0, 1, true);
    { auto c = cell (o2, 1, 1); toggle ({ c.getX() + 2, c.getY() + 4, 132, 22 }, pid::osc2Low, "Low Freq"); }
    { auto c = cell (o2, 1, 1); toggle ({ c.getX() + 2, c.getY() + 32, 132, 22 }, pid::osc2Key, "Key Track"); }
    caption ("TRI > SAW > PULSE", { cell (o2, 2, 0).getX() - 14, cell (o2, 2, 0).getY() + 71, kCellW + 28, 12 });

    knob (mx, pid::mixOsc1,  "OSC 1", 0, 0);
    knob (mx, pid::mixOsc2,  "OSC 2", 1, 0);
    knob (mx, pid::mixNoise, "NOISE", 0, 1);
    knob (mx, pid::slop,     "SLOP", 1, 1);

    knob (pm, pid::pmFiltEnv, "FILTER ENV", 0.5f, 0, true);
    knob (pm, pid::pmOsc2,    "OSC 2", 1.5f, 0);
    {
        auto c = cell (pm, 0, 1);
        const int bx = c.getX() + 2, by = c.getY() + 4;
        toggle ({ bx,       by, 64, 22 }, pid::pmFreq1,  "Freq 1");
        toggle ({ bx + 68,  by, 64, 22 }, pid::pmShape1, "Shape 1");
        toggle ({ bx + 136, by, 64, 22 }, pid::pmPW1,    "PW 1");
        toggle ({ bx + 34,  by + 28, 64, 22 }, pid::pmLPF, "LPF");
        toggle ({ bx + 102, by + 28, 64, 22 }, pid::pmHPF, "HPF");
        caption ("DESTINATIONS", { c.getX(), c.getY() + 57, 204, 14 }, false);
    }

    knob (lf, pid::lfoRate,     "RATE", 0, 0);
    knob (lf, pid::lfoAmount,   "AMOUNT", 1, 0);
    knob (lf, pid::lfoModWheel, "MOD WHEEL", 2, 0);
    {
        auto c = cell (lf, 3, 0);
        combo  ({ c.getX() + 2, c.getY() + 2,  64, 22 }, pid::lfoShape);
        toggle ({ c.getX() + 2, c.getY() + 28, 64, 22 }, pid::lfoSync, "Sync");
        combo  ({ c.getX() + 2, c.getY() + 54, 64, 22 }, pid::lfoSyncRate);
        auto d = cell (lf, 0, 1);
        const int bx = d.getX() + 4, by = d.getY() + 4, w = 84;
        toggle ({ bx,              by, w, 22 }, pid::lfoFreq1, "Freq 1");
        toggle ({ bx + w + 4,      by, w, 22 }, pid::lfoFreq2, "Freq 2");
        toggle ({ bx + 2 * (w + 4), by, w, 22 }, pid::lfoPW,   "Pulse W");
        toggle ({ bx,              by + 28, w, 22 }, pid::lfoLPF, "LPF");
        toggle ({ bx + w + 4,      by + 28, w, 22 }, pid::lfoHPF, "HPF");
        toggle ({ bx + 2 * (w + 4), by + 28, w, 22 }, pid::lfoAmp, "Amp");
        caption ("DESTINATIONS", { d.getX(), d.getY() + 57, 272, 14 }, false);
    }

    // ---------------- Row 2
    x = kContentX;
    auto& lp = section ("LOW-PASS  4-POLE", x, kRow2Y, 3, 2); x += widthOf (3) + gap;
    auto& hp = section ("HIGH-PASS  2-POLE", x, kRow2Y, 3, 2); x += widthOf (3) + gap;
    auto& fe = section ("FILTER ENVELOPE", x, kRow2Y, 2, 2);               x += widthOf (2) + gap;
    auto& ae = section ("AMP ENVELOPE", x, kRow2Y, 3, 2);                  x += widthOf (3) + gap;
    auto& gl = section ("GLOBAL", x, kRow2Y, 4, 2);

    knob (lp, pid::lpfCutoff, "CUTOFF", 0, 0);
    knob (lp, pid::lpfRes,    "RESO", 1, 0);
    knob (lp, pid::lpfEnv,    "ENV AMT", 2, 0, true);
    knob (lp, pid::lpfVel,    "VELOCITY", 0, 1);
    knob (lp, pid::lpfKey,    "KEY AMT", 1, 1);
    knob (lp, pid::lpfAT,     "AFTERTOUCH", 2, 1);

    knob (hp, pid::hpfCutoff, "CUTOFF", 0, 0);
    knob (hp, pid::hpfRes,    "RESO", 1, 0);
    knob (hp, pid::hpfEnv,    "ENV AMT", 2, 0, true);
    knob (hp, pid::hpfVel,    "VELOCITY", 0, 1);
    knob (hp, pid::hpfKey,    "KEY AMT", 1, 1);

    knob (fe, pid::fA, "ATTACK", 0, 0);
    knob (fe, pid::fD, "DECAY", 1, 0);
    knob (fe, pid::fS, "SUSTAIN", 0, 1);
    knob (fe, pid::fR, "RELEASE", 1, 1);

    knob (ae, pid::aA,   "ATTACK", 0, 0);
    knob (ae, pid::aD,   "DECAY", 1, 0);
    knob (ae, pid::aVel, "VELOCITY", 2, 0);
    knob (ae, pid::aS,   "SUSTAIN", 0, 1);
    knob (ae, pid::aR,   "RELEASE", 1, 1);

    knob (gl, pid::glide, "GLIDE", 0, 0);
    { auto c = cell (gl, 1, 0); toggle ({ c.getX() + 2, c.getY() + 18, 64, 22 }, pid::unison, "Unison"); }
    knob (gl, pid::uniVoices, "VOICES", 2, 0);
    knob (gl, pid::uniDetune, "DETUNE", 3, 0);
    knob (gl, pid::panSpread, "SPREAD", 0, 1);
    knob (gl, pid::bendRange, "BEND", 1, 1);
    knob (gl, pid::volume,    "VOLUME", 3, 1);

    // ---------------- Row 3
    x = kContentX;
    auto& dr = section ("DRIVE", x, kRow3Y, 1, 1);    x += widthOf (1) + gap;
    auto& ch = section ("CHORUS", x, kRow3Y, 3, 1);   x += widthOf (3) + gap;
    auto& dl = section ("DELAY", x, kRow3Y, 4, 1);    x += widthOf (4) + gap;
    auto& rv = section ("REVERB", x, kRow3Y, 3, 1);   x += widthOf (3) + gap;
    auto& vc = section ("VOICES", x, kRow3Y, 4, 1);

    knob (dr, pid::drive, "AMOUNT", 0, 0);
    knob (ch, pid::choMix,   "MIX", 0, 0);
    knob (ch, pid::choRate,  "RATE", 1, 0);
    knob (ch, pid::choDepth, "DEPTH", 2, 0);
    knob (dl, pid::dlyTime, "TIME", 0, 0);
    knob (dl, pid::dlyFb,   "FEEDBACK", 1, 0);
    knob (dl, pid::dlyMix,  "MIX", 2, 0);
    {
        auto c = cell (dl, 3, 0);
        toggle ({ c.getX() + 2, c.getY() + 2,  64, 22 }, pid::dlySync, "Sync");
        combo  ({ c.getX() + 2, c.getY() + 28, 64, 22 }, pid::dlySyncRate);
        toggle ({ c.getX() + 2, c.getY() + 54, 64, 22 }, pid::dlyPing, "Ping");
    }
    knob (rv, pid::revSize, "SIZE", 0, 0);
    knob (rv, pid::revDamp, "DAMPING", 1, 0);
    knob (rv, pid::revMix,  "MIX", 2, 0);

    voiceLedArea = { vc.area.getX() + 18, vc.area.getY() + kTitleH + 8, vc.area.getWidth() - 36, 30 };

    // ---------------- Header / preset browser
    addAndMakeVisible (prevButton);
    addAndMakeVisible (nextButton);
    addAndMakeVisible (presetButton);
    for (auto* b : { &prevButton, &nextButton, &presetButton })
        b->setLookAndFeel (&look);
    prevButton.setBounds (452, 15, 30, 30);
    presetButton.setBounds (486, 15, 340, 30);
    nextButton.setBounds (830, 15, 30, 30);
    presetButton.setTooltip ("Browse presets");

    const int numPresets = (int) factoryPresets().size();
    prevButton.onClick = [this, numPresets]
    {
        const int cur = proc.currentPreset.load();
        proc.loadPreset (cur <= 0 ? numPresets - 1 : cur - 1);
        refresh();
    };
    nextButton.onClick = [this, numPresets]
    {
        const int cur = proc.currentPreset.load();
        proc.loadPreset (cur < 0 || cur >= numPresets - 1 ? 0 : cur + 1);
        refresh();
    };
    presetButton.onClick = [this] { showPresetMenu(); };

    setSize (kWidth, kHeight);
    refresh();
}

AugurPanel::~AugurPanel()
{
    for (auto* s : sliders) s->setLookAndFeel (nullptr);
    for (auto* t : toggles) t->setLookAndFeel (nullptr);
    for (auto* c : combos)  c->setLookAndFeel (nullptr);
    for (auto* b : { &prevButton, &nextButton, &presetButton }) b->setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void AugurPanel::showPresetMenu()
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

    juce::Component::SafePointer<AugurPanel> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (220),
                        [safe] (int result)
                        {
                            if (safe == nullptr || result == 0) return;
                            safe->proc.loadPreset (result - 2);
                            safe->refresh();
                        });
}

void AugurPanel::refresh()
{
    const auto& list = factoryPresets();
    const int idx = proc.currentPreset.load();
    juce::String text = juce::isPositiveAndBelow (idx, (int) list.size())
                            ? list[(size_t) idx].category.toUpperCase() + "   " + proc.getPresetDisplayName()
                            : proc.getPresetDisplayName();
    if (text != lastPresetText)
    {
        lastPresetText = text;
        presetButton.setButtonText (text);
    }

    const int v = proc.voicesPlaying.load();
    if (v != lastVoices)
    {
        lastVoices = v;
        repaint (voiceLedArea.expanded (4));
    }
}

void AugurPanel::drawWood (juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::ColourGradient wood (juce::Colour (0xff6b3f1f), r.getX(), r.getY(),
                               juce::Colour (0xff3a2110), r.getRight(), r.getY(), false);
    wood.addColour (0.45, juce::Colour (0xff57321a));
    g.setGradientFill (wood);
    g.fillRoundedRectangle (r, 5.0f);

    juce::Random rnd ((juce::int64) (r.getX() * 31.0f + 7.0f));
    for (int i = 0; i < 46; ++i)
    {
        const float gx = r.getX() + 2.0f + rnd.nextFloat() * (r.getWidth() - 4.0f);
        g.setColour (juce::Colour (0xff1e0f05).withAlpha (0.10f + rnd.nextFloat() * 0.22f));
        juce::Path grain;
        grain.startNewSubPath (gx, r.getY() + 3.0f);
        float cx = gx;
        for (float yy = r.getY(); yy < r.getBottom(); yy += 24.0f)
        {
            cx = juce::jlimit (r.getX() + 1.0f, r.getRight() - 1.0f, cx + (rnd.nextFloat() - 0.5f) * 1.6f);
            grain.lineTo (cx, juce::jmin (yy + 24.0f, r.getBottom() - 3.0f));
        }
        g.strokePath (grain, juce::PathStrokeType (0.6f + rnd.nextFloat() * 0.8f));
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void AugurPanel::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.fillAll (juce::Colour (0xff0a0a0a));

    // wooden end cheeks
    drawWood (g, { 6.0f, 6.0f, 26.0f, all.getHeight() - 12.0f });
    drawWood (g, { all.getWidth() - 32.0f, 6.0f, 26.0f, all.getHeight() - 12.0f });

    // main panel
    auto panelR = juce::Rectangle<float> (32.0f, 6.0f, all.getWidth() - 64.0f, all.getHeight() - 12.0f);
    juce::ColourGradient bg (colours::panelHi, 0, panelR.getY(), colours::panel, 0, panelR.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRect (panelR);

    // header strip
    g.setColour (juce::Colour (0xff0f0f0f));
    g.fillRect (panelR.withHeight (54.0f));
    g.setColour (colours::line);
    g.drawHorizontalLine (60, panelR.getX(), panelR.getRight());

    g.setColour (colours::legend);
    g.setFont (uiFont (26.0f, true, 0.18f));
    g.drawText ("AUGUR-6", kContentX + 6, 12, 200, 32, juce::Justification::centredLeft);
    g.setColour (colours::legendDim);
    g.setFont (uiFont (9.5f, true, 0.22f));
    g.drawText ("SIX-VOICE POLYPHONIC SYNTHESIZER", kContentX + 8, 41, 300, 12, juce::Justification::centredLeft);

    g.setColour (colours::legendDim);
    g.setFont (uiFont (9.5f, true, 0.22f));
    g.drawText ("PROGRAM", 452, 2, 120, 12, juce::Justification::centredLeft);
    g.drawText ("TOOTA AUDIO", getWidth() - 260, 22, 220, 14, juce::Justification::centredRight);

    // sections
    for (const auto& s : sections)
    {
        auto r = s.area.toFloat();
        g.setColour (juce::Colour (0xff181715));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (colours::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        g.setFont (uiFont (10.5f, true, 0.2f));
        const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s.title) + 16;
        auto titleR = juce::Rectangle<int> (s.area.getX() + 10, s.area.getY() + 6, tw, 14);
        g.setColour (colours::legend);
        g.drawText (s.title, titleR, juce::Justification::centredLeft);
        g.setColour (colours::line);
        g.drawHorizontalLine (s.area.getY() + 13, (float) titleR.getRight(), (float) s.area.getRight() - 10.0f);
    }

    // captions
    for (const auto& c : captions)
    {
        g.setColour (c.small ? colours::legendDim.withAlpha (0.75f) : colours::legendDim);
        g.setFont (uiFont (c.small ? 8.0f : 9.5f, true, c.small ? 0.06f : 0.1f));
        g.drawText (c.text, c.area, juce::Justification::centred);
    }

    // voice activity LEDs
    const int v = juce::jmax (0, lastVoices);
    const float ledW = (float) voiceLedArea.getWidth() / 6.0f;
    for (int i = 0; i < 6; ++i)
    {
        const float cx = (float) voiceLedArea.getX() + ledW * ((float) i + 0.5f);
        const float cy = (float) voiceLedArea.getCentreY();
        const bool on = i < v;
        if (on)
        {
            g.setColour (colours::led.withAlpha (0.22f));
            g.fillEllipse (cx - 9.0f, cy - 9.0f, 18.0f, 18.0f);
        }
        g.setColour (on ? colours::led : colours::ledOff);
        g.fillEllipse (cx - 4.5f, cy - 4.5f, 9.0f, 9.0f);
        g.setColour (colours::legendDim);
        g.setFont (uiFont (9.0f, true));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (cx - 10.0f, cy + 10.0f, 20.0f, 12.0f), juce::Justification::centred);
    }
    g.setColour (colours::legendDim.withAlpha (0.6f));
    g.setFont (uiFont (8.5f, true, 0.18f));
    g.drawText ("ANALOG-MODELLED POLYSYNTH",
                voiceLedArea.getX() - 10, voiceLedArea.getBottom() + 34, voiceLedArea.getWidth() + 20, 12,
                juce::Justification::centred);
}

//==============================================================================
Augur6Editor::Augur6Editor (Augur6Processor& p) : AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    const double ratio = (double) AugurPanel::kWidth / (double) AugurPanel::kHeight;
    setResizable (true, true);
    setResizeLimits ((int) (AugurPanel::kWidth * 0.6), (int) (AugurPanel::kHeight * 0.6),
                     (int) (AugurPanel::kWidth * 1.6), (int) (AugurPanel::kHeight * 1.6));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (ratio);
    setSize ((int) (AugurPanel::kWidth * 0.9), (int) (AugurPanel::kHeight * 0.9));
    startTimerHz (15);
}

void Augur6Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0a0a0a));
}

void Augur6Editor::resized()
{
    const float scale = (float) getWidth() / (float) AugurPanel::kWidth;
    panel.setBounds (0, 0, AugurPanel::kWidth, AugurPanel::kHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));
}
