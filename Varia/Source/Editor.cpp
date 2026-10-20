#include "Editor.h"

namespace vc
{
    const juce::Colour bgTop   { 0xff23262c };
    const juce::Colour bgBot   { 0xff15171b };
    const juce::Colour text    { 0xffd9dde3 };
    const juce::Colour dim     { 0xff7e858f };
    const juce::Colour pill    { 0xff17191d };
    const juce::Colour edge    { 0xff32363d };
}

static juce::Font vf (float size, bool bold = true, float kern = 0.0f)
{
    return juce::Font (juce::FontOptions (size).withStyle (bold ? "Bold" : "Regular")).withExtraKerningFactor (kern);
}

static constexpr int kW = 420, kH = 740;

//==============================================================================
void VariaLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    if ((bool) s.getProperties().getWithDefault ("orb", false)) return;   // drawn by the editor

    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f - 2.0f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const bool bip = (bool) s.getProperties().getWithDefault ("bipolar", false);

    // soft drop shadow
    for (int i = 4; i > 0; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.08f * (float) i));
        g.fillEllipse (c.x - r * 0.78f - i, c.y - r * 0.78f - i + 4, (r * 0.78f + i) * 2, (r * 0.78f + i) * 2);
    }

    // value arc hugging the knob
    const float ar = r * 0.9f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, ar, ar, 0, a0, a1, true);
    g.setColour (juce::Colour (0xff0c0d10));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = bip ? (a0 + a1) * 0.5f : a0;
    if (std::abs (ang - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ar, ar, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (accent.withAlpha (0.35f));
        g.strokePath (arc, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent.brighter (0.4f));
        g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // knob body
    const float kr = r * 0.74f;
    juce::ColourGradient body (juce::Colour (0xff2e3138), c.x - kr * 0.5f, c.y - kr, juce::Colour (0xff0b0c0e), c.x + kr * 0.4f, c.y + kr, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawEllipse (c.x - kr + 1, c.y - kr + 1, kr * 2 - 2, kr * 2 - 2, 1.0f);
    juce::ColourGradient cap (juce::Colour (0xff1d1f24), c.x, c.y - kr * 0.7f, juce::Colour (0xff111215), c.x, c.y + kr * 0.7f, false);
    g.setGradientFill (cap);
    g.fillEllipse (c.x - kr * 0.8f, c.y - kr * 0.8f, kr * 1.6f, kr * 1.6f);

    juce::Path ptr;
    ptr.addRoundedRectangle (-1.3f, -kr * 0.8f + 2.0f, 2.6f, kr * 0.32f, 1.3f);
    g.setColour (juce::Colours::white.withAlpha (0.9f));
    g.fillPath (ptr, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void VariaLook::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body)
{
    g.setColour (juce::Colour (0xf00e0f12));
    g.fillRoundedRectangle (body, 4);
    g.setColour (accent.withAlpha (0.7f));
    g.drawRoundedRectangle (body.reduced (0.5f), 4, 1);
}

juce::Font VariaLook::getSliderPopupFont (juce::Slider&) { return vf (12.0f, true, 0.05f); }

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
                              juce::Colours::black.withAlpha (0.55f), r.getCentreX(), r.getY() - r.getHeight() * 0.08f, true);
    g.setGradientFill (vig);
    g.fillEllipse (r);
    g.restoreState();
}

//==============================================================================
void Pill::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float rad = r.getHeight() * 0.5f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 2), rad);
    juce::ColourGradient grad (juce::Colour (0xff1c1e23), 0, r.getY(), juce::Colour (0xff101114), 0, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, rad);
    g.setColour (vc::edge);
    g.drawRoundedRectangle (r, rad, 1.0f);

    auto tr = r;
    if (led)
    {
        const float cx = r.getX() + 16, cy = r.getCentreY();
        if (lit) { g.setColour (accent.withAlpha (0.35f)); g.fillEllipse (cx - 6, cy - 6, 12, 12); }
        g.setColour (lit ? accent.brighter (0.5f) : juce::Colour (0xff2a2d33));
        g.fillEllipse (cx - 2.5f, cy - 2.5f, 5, 5);
        tr = r.withTrimmedLeft (22);
    }
    g.setColour (lit || ! led ? vc::text : vc::dim);
    g.setFont (vf (led ? 9.0f : 11.0f, true, led ? 0.16f : 0.22f));
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
    look.accent = juce::Colour (st.c2).withMultipliedBrightness (1.15f);

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
    g.fillAll (vc::bgBot);
    g.addTransform (juce::AffineTransform::scale (s));
    const auto full = juce::Rectangle<float> (0, 0, (float) kW, (float) kH);

    // body
    g.setGradientFill (juce::ColourGradient (vc::bgTop, 0, 0, vc::bgBot, 0, (float) kH, false));
    g.fillRoundedRectangle (full.reduced (4), 14);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (full.reduced (4.5f), 14, 1.0f);

    // lower deck with a curved lip
    juce::Path deck;
    deck.startNewSubPath (5, 500);
    deck.quadraticTo (210, 466, 415, 500);
    deck.lineTo (415, 722); deck.quadraticTo (415, 736, 400, 736);
    deck.lineTo (20, 736); deck.quadraticTo (5, 736, 5, 722);
    deck.closeSubPath();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1d22), 0, 480, juce::Colour (0xff131418), 0, 736, false));
    g.fillPath (deck);
    juce::Path lip;
    lip.startNewSubPath (5, 500); lip.quadraticTo (210, 466, 415, 500);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.strokePath (lip, juce::PathStrokeType (2.0f));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.strokePath (lip, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0, 2));

    // screws
    for (auto pt : { juce::Point<float> (34, 40), juce::Point<float> (386, 40), juce::Point<float> (34, 706), juce::Point<float> (386, 706) })
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff55595f), pt.x - 5, pt.y - 5, juce::Colour (0xff17181b), pt.x + 5, pt.y + 5, false));
        g.fillEllipse (pt.x - 6, pt.y - 6, 12, 12);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawLine (pt.x - 3.5f, pt.y + 2, pt.x + 3.5f, pt.y - 2, 1.2f);
    }

    // wordmark
    g.setColour (vc::text);
    g.setFont (vf (21.0f, true, 0.9f));
    g.drawText ("VARIA", juce::Rectangle<float> (60, 24, 300, 34), juce::Justification::centred);
    g.setColour (vc::dim.withAlpha (0.8f));
    g.setFont (vf (7.5f, true, 0.45f));
    g.drawText ("MOVEMENT  +  VARIATION", juce::Rectangle<float> (60, 58, 300, 12), juce::Justification::centred);

    // meters
    auto meter = [&] (float x, float lvl)
    {
        const juce::Rectangle<float> r (x, 180, 5, 240);
        g.setColour (juce::Colour (0xff0c0d0f));
        g.fillRoundedRectangle (r.expanded (2, 2), 4);
        const float db = juce::Decibels::gainToDecibels (lvl, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = r.withTop (r.getBottom() - r.getHeight() * frac);
        g.setGradientFill (juce::ColourGradient (look.accent.withAlpha (0.8f), 0, r.getBottom(), juce::Colours::white, 0, r.getY(), false));
        g.fillRoundedRectangle (fill, 2.5f);
    };
    meter (40, inMeter);
    meter (375, outMeter);

    // orb bezel + texture ring
    const juce::Point<float> c (210, 300);
    for (int i = 10; i > 0; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.045f * (float) i));
        g.fillEllipse (c.x - 128 - i, c.y - 128 - i + 6, (128 + i) * 2.0f, (128 + i) * 2.0f);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3e45), c.x - 90, c.y - 128, juce::Colour (0xff0d0e10), c.x + 60, c.y + 128, false));
    g.fillEllipse (c.x - 128, c.y - 128, 256, 256);
    g.setColour (juce::Colour (0xff0a0b0d));
    g.fillEllipse (c.x - 114, c.y - 114, 228, 228);

    const float a0 = juce::MathConstants<float>::pi * 1.2f, a1 = juce::MathConstants<float>::pi * 2.8f;
    const float val = (float) textureSlider.valueToProportionOfLength (textureSlider.getValue());
    const float ang = a0 + val * (a1 - a0);
    const float rr = 120;
    juce::Path ring;
    ring.addCentredArc (c.x, c.y, rr, rr, 0, a0, ang, true);
    g.setColour (look.accent.withAlpha (0.25f));
    g.strokePath (ring, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (look.accent.brighter (0.3f));
    g.strokePath (ring, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const juce::Point<float> dot (c.x + rr * std::sin (ang), c.y - rr * std::cos (ang));
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillEllipse (dot.x - 7, dot.y - 7, 14, 14);
    g.setColour (juce::Colours::white);
    g.fillEllipse (dot.x - 3.5f, dot.y - 3.5f, 7, 7);

    // labels
    g.setColour (vc::text.withAlpha (0.85f));
    g.setFont (vf (10.0f, true, 0.6f));
    g.drawText ("TEXTURE", juce::Rectangle<float> (110, 436, 200, 14), juce::Justification::centred);
    if (eventAlpha > 0.01f && eventText.isNotEmpty())
    {
        g.setColour (look.accent.brighter (0.4f).withAlpha (eventAlpha));
        g.setFont (vf (8.5f, true, 0.4f));
        g.drawText (eventText, juce::Rectangle<float> (110, 454, 200, 12), juce::Justification::centred);
    }
    g.setColour (vc::text.withAlpha (0.75f));
    g.setFont (vf (9.0f, true, 0.38f));
    const char* names[] { "RELEASE", "BRIGHTNESS", "WIDTH", "PITCH" };
    const float kx[] { 132, 288, 132, 288 }, ky[] { 560, 560, 662, 662 };
    for (int i = 0; i < 4; ++i)
        g.drawText (names[i], juce::Rectangle<float> (kx[i] - 60, ky[i] + 38, 120, 12), juce::Justification::centred);
}

void VariaEditor::paintOverChildren (juce::Graphics& g)
{
    // arrows on the style pill
    const float s = (float) getWidth() / (float) kW;
    g.addTransform (juce::AffineTransform::scale (s));
    g.setColour (vc::dim);
    g.setFont (vf (12.0f, false));
    g.drawText ("<", juce::Rectangle<float> (140, 88, 26, 36), juce::Justification::centred);
    g.drawText (">", juce::Rectangle<float> (254, 88, 26, 36), juce::Justification::centred);
}
