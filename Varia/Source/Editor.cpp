#include "Editor.h"

namespace vc
{
    const juce::Colour bgTop   = toota::cream;
    const juce::Colour bgBot   = toota::cream;
    const juce::Colour text    = toota::ink;
    const juce::Colour dim     = toota::inkSoft;
    const juce::Colour pill    = toota::cream;
    const juce::Colour edge    = toota::ink;
}

static juce::Font vf (float size, bool bold = true, float kern = 0.0f) { return toota::font (size * 1.15f, bold ? 2 : 1, kern * 0.25f); }

static constexpr int kW = 420, kH = 740;

//==============================================================================
void VariaLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    if ((bool) s.getProperties().getWithDefault ("orb", false)) return;   // drawn by the editor
    s.setColour (juce::Slider::rotarySliderFillColourId, accent);
    toota::Look::drawRotarySlider (g, x, y, w, h, pos, a0, a1, s);
}

//==============================================================================
namespace
{
inline float hash2 (int x, int y)
{
    uint32_t h = (uint32_t) x * 374761393u + (uint32_t) y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float) ((h ^ (h >> 16)) & 0xffffff) / 16777215.0f;
}

inline float vnoise (float x, float y)
{
    const float fx = std::floor (x), fy = std::floor (y);
    const int ix = (int) fx, iy = (int) fy;
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    const float a = hash2 (ix, iy), b = hash2 (ix + 1, iy), c = hash2 (ix, iy + 1), d = hash2 (ix + 1, iy + 1);
    return a + (b - a) * tx + (c - a) * ty + (a - b - c + d) * tx * ty;
}

inline float fbm (float x, float y) { return 0.65f * vnoise (x, y) + 0.35f * vnoise (x * 2.03f + 7.1f, y * 2.03f + 3.7f); }
}

void Orb::update (float dt, float motion, float level, float fl, const va::Style& st)
{
    c1 = juce::Colour (st.c1); c2 = juce::Colour (st.c2); c3 = juce::Colour (st.c3);
    energy += (juce::jlimit (0.0f, 1.0f, motion * 0.8f + level * 0.6f + fl * 0.6f) - energy) * 0.15f;
    time += dt * (0.12f + 0.25f * value + 1.4f * energy);

    const int W = img.getWidth(), H = img.getHeight();
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    const float warp = 1.1f + 1.1f * value + 1.0f * energy;
    const float t = time;
    for (int py = 0; py < H; ++py)
    {
        const float v = ((float) py / (float) (H - 1)) * 2.0f - 1.0f;
        for (int px = 0; px < W; ++px)
        {
            const float u = ((float) px / (float) (W - 1)) * 2.0f - 1.0f;
            const float x = u * 1.05f + 0.45f * v, y = v * 1.05f - 0.35f * u;    // diagonal flow like poured marble
            const float qx = fbm (x + t * 0.11f, y - t * 0.05f);
            const float qy = fbm (x + 5.2f - t * 0.07f, y + 1.3f);
            const float rx = fbm (x + warp * qx + 1.7f + t * 0.13f, y + warp * qy + 9.2f);
            const float ry = fbm (x + warp * qx + 8.3f, y + warp * qy + 2.8f - t * 0.09f);
            const float f = fbm (x + warp * rx, y + warp * ry);

            const float band = 0.5f + 0.5f * std::sin ((f * 4.2f + rx * 1.6f + u * 0.8f) * 3.14159f);
            float m = juce::jlimit (0.0f, 1.0f, (f - 0.25f) * 1.9f);
            juce::Colour col = c1.interpolatedWith (c2, m);
            const float hi = std::pow (band, 3.0f) * (0.6f + 0.4f * ry);
            col = col.interpolatedWith (c3, juce::jlimit (0.0f, 1.0f, hi));
            const float grain = (hash2 (px * 7 + (int) (t * 30.0f), py * 13) - 0.5f) * 0.05f;
            const float shade = 1.0f - 0.35f * (u * u + v * v) + grain;
            bd.setPixelColour (px, py, juce::Colour::fromFloatRGBA (juce::jlimit (0.0f, 1.0f, col.getFloatRed() * shade),
                                                                    juce::jlimit (0.0f, 1.0f, col.getFloatGreen() * shade),
                                                                    juce::jlimit (0.0f, 1.0f, col.getFloatBlue() * shade), 1.0f));
        }
    }
    repaint();
}

void Orb::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::Path clip;
    clip.addEllipse (r);
    g.saveState();
    g.reduceClipRegion (clip);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (img, r, juce::RectanglePlacement::stretchToFit);
    // glassy inner shadow
    juce::ColourGradient vig (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                              toota::ink.withAlpha (0.25f), r.getCentreX(), r.getY() - r.getHeight() * 0.08f, true);
    g.setGradientFill (vig);
    g.fillEllipse (r);
    g.restoreState();
    g.setColour (toota::ink);
    g.drawEllipse (r.reduced (1.5f), 3.0f);
}

//==============================================================================
void Pill::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f).withTrimmedRight (2.5f).withTrimmedBottom (2.5f);
    const float rad = r.getHeight() * 0.5f;
    toota::sticker (g, r, lit && led ? accent : toota::cream, rad, 2.5f, 2.0f);
    auto tr = r;
    if (led)
    {
        const float cx = r.getX() + 16, cy = r.getCentreY();
        g.setColour (lit ? toota::cream : toota::cream3); g.fillEllipse (cx - 4.5f, cy - 4.5f, 9, 9);
        g.setColour (toota::ink); g.drawEllipse (cx - 4.5f, cy - 4.5f, 9, 9, 1.5f);
        tr = r.withTrimmedLeft (22);
    }
    g.setColour (lit && led ? toota::cream : toota::ink);
    g.setFont (toota::font (led ? 11.5f : 15.0f, 2, 0.03f));
    g.drawText (text, tr.withTrimmedRight (led ? 8.0f : 0.0f), juce::Justification::centred);
}

//==============================================================================
VariaEditor::VariaEditor (VariaProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);

    addAndMakeVisible (orb);
    textureSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    textureSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    textureSlider.getProperties().set ("orb", true);
    textureSlider.setMouseDragSensitivity (300);
    textureSlider.setPopupDisplayEnabled (true, true, this, 900);
    textureSlider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible (textureSlider);
    att.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, vap::texture, textureSlider));
    textureSlider.setDoubleClickReturnValue (true, 0.5);
    textureSlider.onValueChange = [this] { repaint(); };

    for (auto* id : { vap::release, vap::brightness, vap::width, vap::pitch })
    {
        auto* k = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
        k->setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
        k->setPopupDisplayEnabled (true, true, this, 900);
        k->setMouseDragSensitivity (220);
        addAndMakeVisible (k);
        att.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, id, *k));
        if (auto* prm = proc.apvts.getParameter (id))
            k->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
    knobs[3]->getProperties().set ("bipolar", true);

    bypassPill.text = "BYPASS"; bypassPill.led = true;
    wetPill.text = "WET ONLY";  wetPill.led = true;
    bypassPill.onClick = [this] { toggleParam (vap::bypass); };
    wetPill.onClick = [this] { toggleParam (vap::wetOnly); };
    stylePill.onClick = [this] { showStyleMenu(); };
    for (auto* pl : { &bypassPill, &wetPill, &stylePill }) addAndMakeVisible (pl);
    stylePill.setInterceptsMouseClicks (false, false);   // handled by the editor so < > work too

    // EVERY: a change at the end of every N bars and nothing in between (like switching Varia on for one beat
    // every N bars). LENGTH: how long each change lasts.
    for (int b : va::everyChoices()) everyBox.addItem (b == 0 ? juce::String ("STYLE") : juce::String (b) + (b == 1 ? " BAR" : " BARS"), everyBox.getNumItems() + 1);
    for (int st : va::lengthChoices()) lengthBox.addItem (va::lengthName (st), lengthBox.getNumItems() + 1);
    everyAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, vap::every, everyBox);
    lengthAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, vap::length, lengthBox);
    everyBox.setTooltip ("STYLE: the style decides when things happen. Pick a bar count to get exactly one change at the end of every N bars (counted from bar 1), with Varia completely transparent in between.");
    lengthBox.setTooltip ("How long each change lasts (AUTO lets the style choose)");
    addAndMakeVisible (everyBox);
    addAndMakeVisible (lengthBox);

    setResizable (true, true);
    setResizeLimits ((int) (kW * 0.75), (int) (kH * 0.75), (int) (kW * 1.6), (int) (kH * 1.6));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / (double) kH);
    setSize (kW, kH);
    lastTime = juce::Time::getMillisecondCounterHiRes();
    timerCallback();
    startTimerHz (30);
}

VariaEditor::~VariaEditor()
{
    setLookAndFeel (nullptr);
}

void VariaEditor::toggleParam (const char* id)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
}

void VariaEditor::stepStyle (int delta)
{
    const int n = (int) va::styles().size();
    const int cur = (int) proc.apvts.getRawParameterValue (vap::style)->load();
    if (auto* p = proc.apvts.getParameter (vap::style))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) ((cur + delta + n) % n)));
        p->endChangeGesture();
    }
}

void VariaEditor::showStyleMenu()
{
    juce::PopupMenu m;
    const int cur = (int) proc.apvts.getRawParameterValue (vap::style)->load();
    for (int i = 0; i < (int) va::styles().size(); ++i)
        m.addItem (i + 1, va::styles()[(size_t) i].name, true, i == cur);
    juce::Component::SafePointer<VariaEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&stylePill), [safe] (int r)
    {
        if (safe == nullptr || r == 0) return;
        if (auto* p = safe->proc.apvts.getParameter (vap::style))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) (r - 1)));
            p->endChangeGesture();
        }
    });
}

void VariaEditor::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked()) return;
    const auto pt = e.getPosition();
    if (prevArea.contains (pt)) stepStyle (-1);
    else if (nextArea.contains (pt)) stepStyle (1);
    else if (stylePill.getBounds().contains (pt)) showStyleMenu();
}

void VariaEditor::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTime) * 0.001);
    lastTime = now;

    const auto& st = va::styles()[(size_t) juce::jlimit (0, (int) va::styles().size() - 1, (int) proc.apvts.getRawParameterValue (vap::style)->load())];
    look.accent = toota::accent ((int) proc.apvts.getRawParameterValue (vap::style)->load() % 3);

    const int counter = proc.engine.eventCounter.load();
    if (counter != lastCounter)
    {
        lastCounter = counter;
        flash = 1.0f;
        eventText = va::eventName (proc.engine.lastEvent.load());
        eventAlpha = 1.0f;
    }
    flash *= 0.88f;
    eventAlpha = juce::jmax (0.0f, eventAlpha - dt * 0.6f);

    inMeter = juce::jmax (proc.inLevel.load(), inMeter * 0.85f);
    outMeter = juce::jmax (proc.outLevel.load(), outMeter * 0.85f);

    orb.value = (float) textureSlider.getValue();
    orb.update (dt, proc.engine.motion.load(), outMeter, flash, st);

    stylePill.text = st.name;
    barsToNext = proc.engine.barsToNext.load();
    bypassPill.lit = proc.apvts.getRawParameterValue (vap::bypass)->load() > 0.5f;
    wetPill.lit = proc.apvts.getRawParameterValue (vap::wetOnly)->load() > 0.5f;
    for (auto* pl : { &bypassPill, &wetPill, &stylePill }) { pl->accent = look.accent; pl->repaint(); }
    for (auto* k : knobs) k->repaint();
    repaint();
}

void VariaEditor::resized()
{
    const float s = (float) getWidth() / (float) kW;
    auto R = [s] (float x, float y, float w, float h) { return juce::Rectangle<float> (x, y, w, h).transformedBy (juce::AffineTransform::scale (s)).toNearestInt(); };
    bypassPill.setBounds (R (26, 90, 102, 32));
    stylePill.setBounds (R (138, 88, 144, 36));
    wetPill.setBounds (R (292, 90, 102, 32));
    prevArea = R (138, 88, 34, 36);
    everyBox.setBounds (R (84, 136, 116, 30));
    lengthBox.setBounds (R (272, 136, 122, 30));
    nextArea = R (248, 88, 34, 36);
    orb.setBounds (R (210 - 106, 300 - 106, 212, 212));
    textureSlider.setBounds (R (210 - 140, 300 - 140, 280, 280));
    const float kx[] { 132, 288, 132, 288 }, ky[] { 560, 560, 662, 662 };
    for (int i = 0; i < 4; ++i)
        knobs[i]->setBounds (R (kx[i] - 40, ky[i] - 40, 80, 80));
}

void VariaEditor::paint (juce::Graphics& g)
{
    const float s = (float) getWidth() / (float) kW;
    g.fillAll (toota::cream);
    g.addTransform (juce::AffineTransform::scale (s));
    const auto full = juce::Rectangle<float> (0, 0, (float) kW, (float) kH);

    // body
    toota::sticker (g, full.reduced (6).withTrimmedRight (3).withTrimmedBottom (3), toota::cream2, 22, 3.5f, 2.5f);

    // lower deck with a curved lip
    juce::Path deck;
    deck.startNewSubPath (8, 500);
    deck.quadraticTo (210, 466, 410, 500);
    deck.lineTo (410, 712); deck.quadraticTo (410, 726, 396, 726);
    deck.lineTo (22, 726); deck.quadraticTo (8, 726, 8, 712);
    deck.closeSubPath();
    g.setColour (toota::cream3); g.fillPath (deck);
    juce::Path lip;
    lip.startNewSubPath (8, 500); lip.quadraticTo (210, 466, 410, 500);
    g.setColour (toota::ink); g.strokePath (lip, juce::PathStrokeType (2.5f));

    // logo + wordmark
    const auto& img = toota::logo();
    const float lh = 34, lw = lh * img.getWidth() / (float) juce::jmax (1, img.getHeight());
    g.drawImage (img, juce::Rectangle<float> (210 - (lw + 108) * 0.5f, 18, lw, lh), juce::RectanglePlacement::centred);
    g.setColour (toota::ink); g.setFont (toota::font (28.0f, 2));
    g.drawText ("VARIA", juce::Rectangle<float> (210 - (lw + 108) * 0.5f + lw + 8, 16, 110, 38), juce::Justification::centredLeft);
    g.setColour (vc::dim); g.setFont (toota::font (10.0f, 2, 0.08f));
    g.drawText ("MOVEMENT  +  VARIATION", juce::Rectangle<float> (60, 56, 300, 14), juce::Justification::centred);

    // meters
    auto meter = [&] (float x, float lvl)
    {
        const juce::Rectangle<float> r (x - 2, 180, 9, 240);
        toota::screen (g, r, 4.5f);
        const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = r.reduced (2.0f);
        fill = fill.withTop (fill.getBottom() - fill.getHeight() * frac);
        g.setColour (look.accent); g.fillRoundedRectangle (fill, 2.5f);
    };
    meter (40, inMeter);
    meter (375, outMeter);

    // orb bezel + texture ring
    const juce::Point<float> c (210, 300);
    g.setColour (toota::ink); g.fillEllipse (c.x - 128 + 4, c.y - 128 + 4, 256, 256);
    g.setColour (toota::cream); g.fillEllipse (c.x - 128, c.y - 128, 256, 256);
    g.setColour (toota::ink); g.drawEllipse (c.x - 127, c.y - 127, 254, 254, 2.5f);
    g.setColour (toota::cream3); g.fillEllipse (c.x - 112, c.y - 112, 224, 224);

    const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
    const float val = (float) textureSlider.valueToProportionOfLength (textureSlider.getValue());
    const float ang = a0 + val * (a1 - a0);
    const float rr = 119;
    juce::Path track, ring;
    track.addCentredArc (c.x, c.y, rr, rr, 0, a0, a1, true);
    ring.addCentredArc (c.x, c.y, rr, rr, 0, a0, ang, true);
    g.setColour (toota::cream3); g.strokePath (track, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (look.accent); g.strokePath (ring, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const juce::Point<float> dot (c.x + rr * std::sin (ang), c.y - rr * std::cos (ang));
    g.setColour (toota::cream); g.fillEllipse (dot.x - 8, dot.y - 8, 16, 16);
    g.setColour (toota::ink); g.drawEllipse (dot.x - 8, dot.y - 8, 16, 16, 2.2f);

    // labels
    g.setColour (vc::text);
    g.setFont (toota::font (14.0f, 2, 0.1f));
    g.drawText ("TEXTURE", juce::Rectangle<float> (110, 434, 200, 18), juce::Justification::centred);
    g.setColour (vc::text); g.setFont (toota::font (11.5f, 2, 0.06f));
    g.drawText ("EVERY", juce::Rectangle<float> (26, 136, 56, 28), juce::Justification::centredLeft);
    g.drawText ("LENGTH", juce::Rectangle<float> (208, 136, 62, 28), juce::Justification::centredLeft);
    if (barsToNext > 0 && ! (eventAlpha > 0.01f && eventText.isNotEmpty()))
    {
        g.setColour (vc::dim); g.setFont (toota::font (11.0f, 2, 0.06f));
        g.drawText ("NEXT CHANGE IN " + juce::String (barsToNext) + (barsToNext == 1 ? " BAR" : " BARS"), juce::Rectangle<float> (90, 454, 240, 14), juce::Justification::centred);
    }
    if (eventAlpha > 0.01f && eventText.isNotEmpty())
    {
        g.setColour (look.accent.darker (0.2f).withAlpha (eventAlpha));
        g.setFont (toota::font (11.0f, 2, 0.06f));
        g.drawText (eventText, juce::Rectangle<float> (90, 454, 240, 14), juce::Justification::centred);
    }
    g.setColour (vc::text);
    g.setFont (toota::font (11.5f, 2, 0.08f));
    const char* names[] { "RELEASE", "BRIGHTNESS", "WIDTH", "PITCH" };
    const float kx[] { 132, 288, 132, 288 }, ky[] { 560, 560, 662, 662 };
    for (int i = 0; i < 4; ++i)
        g.drawText (names[i], juce::Rectangle<float> (kx[i] - 60, ky[i] + 38, 120, 14), juce::Justification::centred);
}

void VariaEditor::paintOverChildren (juce::Graphics& g)
{
    // arrows on the style pill
    const float s = (float) getWidth() / (float) kW;
    g.addTransform (juce::AffineTransform::scale (s));
    g.setColour (toota::ink);
    g.setFont (toota::font (16.0f, 2));
    g.drawText ("<", juce::Rectangle<float> (140, 88, 26, 36), juce::Justification::centred);
    g.drawText (">", juce::Rectangle<float> (254, 88, 26, 36), juce::Justification::centred);
}
