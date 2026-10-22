#include "PluginEditor.h"
#include "Presets.h"

namespace colours
{
    const juce::Colour panel     = toota::cream;
    const juce::Colour panelHi   = toota::cream;
    const juce::Colour line      = toota::ink.withAlpha (0.25f);
    const juce::Colour legend    = toota::ink;
    const juce::Colour legendDim = toota::inkSoft;
    const juce::Colour amber     = toota::orange;
    const juce::Colour led       = toota::coral;
    const juce::Colour ledOff    = toota::cream3;
}

static juce::Font uiFont (float size, bool bold = false, float kerning = 0.0f) { return toota::font (size * 1.12f, bold ? 2 : 1, kerning * 0.4f); }

//==============================================================================
// LED switches, TOOTA style: a cream sticker key with a coral LED
void AugurLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedRight (2.0f).withTrimmedBottom (2.0f);
    if (down) r = r.translated (1.0f, 1.0f);
    toota::sticker (g, r, highlighted ? toota::cream2 : toota::cream, 6.0f, down ? 0.0f : 2.0f, 1.6f);
    const bool on = b.getToggleState();
    const float d = 8.0f;
    const auto c = juce::Point<float> (r.getX() + 10.0f, r.getCentreY());
    g.setColour (on ? colours::led : colours::ledOff); g.fillEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d);
    g.setColour (toota::ink); g.drawEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.3f);
    g.setColour (toota::ink);
    g.setFont (uiFont (10.0f, true, 0.02f));
    g.drawFittedText (b.getButtonText().toUpperCase(), r.withTrimmedLeft (19.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}

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
    // the classic wooden end cheeks, as chunky brand-coloured stripes
    toota::sticker (g, r, toota::teal, 8.0f, 0.0f, 2.2f);
    const float h = r.getHeight() / 3.0f;
    g.setColour (toota::coral); g.fillRect (r.reduced (2.2f).withTop (r.getY() + h).withHeight (h));
    g.setColour (toota::orange); g.fillRect (r.reduced (2.2f).withTop (r.getY() + h * 2).withBottom (r.getBottom() - 8));
    g.setColour (toota::ink); g.drawRoundedRectangle (r.reduced (1.1f), 8.0f, 2.2f);
}

void AugurPanel::paint (juce::Graphics& g)
{
    auto all = getLocalBounds().toFloat();
    g.fillAll (toota::cream);

    drawWood (g, { 6.0f, 6.0f, 22.0f, all.getHeight() - 12.0f });
    drawWood (g, { all.getWidth() - 28.0f, 6.0f, 22.0f, all.getHeight() - 12.0f });

    // header
    toota::header (g, { 32, 0, (int) all.getWidth() - 64, 60 }, "AUGUR-6", "six-voice polysynth");
    g.setColour (colours::legendDim);
    g.setFont (uiFont (9.5f, true, 0.1f));
    g.drawText ("PROGRAM", 452, 1, 120, 12, juce::Justification::centredLeft);

    // sections
    for (const auto& s : sections)
    {
        auto r = s.area.toFloat();
        toota::panel (g, r, 10.0f);
        g.setFont (uiFont (11.0f, true, 0.1f));
        const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), s.title) + 16;
        auto titleR = juce::Rectangle<int> (s.area.getX() + 10, s.area.getY() + 5, tw, 16);
        g.setColour (toota::ink);
        g.drawText (s.title, titleR, juce::Justification::centredLeft);
        g.setColour (colours::line);
        g.fillRect ((float) titleR.getRight(), (float) s.area.getY() + 12.5f, (float) s.area.getRight() - 10.0f - titleR.getRight(), 1.5f);
    }

    // captions
    for (const auto& c : captions)
    {
        g.setColour (c.small ? colours::legendDim : colours::legend);
        g.setFont (uiFont (c.small ? 8.0f : 9.0f, true, c.small ? 0.06f : 0.08f));
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
        g.setColour (on ? toota::accent (i % 3) : colours::ledOff);
        g.fillEllipse (cx - 6.0f, cy - 6.0f, 12.0f, 12.0f);
        g.setColour (toota::ink); g.drawEllipse (cx - 6.0f, cy - 6.0f, 12.0f, 12.0f, 1.6f);
        g.setColour (colours::legend);
        g.setFont (uiFont (9.0f, true));
        g.drawText (juce::String (i + 1), juce::Rectangle<float> (cx - 10.0f, cy + 10.0f, 20.0f, 12.0f), juce::Justification::centred);
    }
    g.setColour (colours::legendDim);
    g.setFont (uiFont (8.5f, true, 0.1f));
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
    g.fillAll (toota::cream);
}

void Augur6Editor::resized()
{
    const float scale = (float) getWidth() / (float) AugurPanel::kWidth;
    panel.setBounds (0, 0, AugurPanel::kWidth, AugurPanel::kHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));
}
