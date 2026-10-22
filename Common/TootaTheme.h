#pragma once
// TOOTA brand theme shared by every plugin: cream paper, chunky ink outlines, teal / coral / orange,
// Fredoka type and the TOOTA logo. Each plugin links the TootaAssets binary-data target (see CMake).
#include <juce_gui_basics/juce_gui_basics.h>
#include "TootaAssets.h"

namespace toota
{
// ---------------------------------------------------------------- palette (from the logo)
const juce::Colour cream   { 0xfffdfbf2 };   // paper
const juce::Colour cream2  { 0xfff5eedd };   // panels
const juce::Colour cream3  { 0xffece2cb };   // insets, screens, tracks
const juce::Colour ink     { 0xff1c0b06 };   // outlines and text
const juce::Colour inkSoft { 0xff6b5a4e };   // secondary text
const juce::Colour teal    { 0xff0294aa };
const juce::Colour coral   { 0xfffd694c };
const juce::Colour orange  { 0xfffe9e3b };
// a few supporting tints for plots with more than three things on them
const juce::Colour tealSoft   { 0xff7cc6d0 };
const juce::Colour coralSoft  { 0xfffdb3a3 };
const juce::Colour orangeSoft { 0xfffed09d };
const juce::Colour plum       { 0xff8a4f7d };
const juce::Colour olive      { 0xff7d9a3a };

inline juce::Colour accent (int i) { const juce::Colour c[] { teal, coral, orange, plum, olive }; return c[((i % 5) + 5) % 5]; }

// ---------------------------------------------------------------- type
struct Typefaces
{
    juce::Typeface::Ptr bold = juce::Typeface::createSystemTypefaceFor (TootaAssets::FredokaBold_ttf, TootaAssets::FredokaBold_ttfSize);
    juce::Typeface::Ptr semi = juce::Typeface::createSystemTypefaceFor (TootaAssets::FredokaSemiBold_ttf, TootaAssets::FredokaSemiBold_ttfSize);
    juce::Typeface::Ptr medium = juce::Typeface::createSystemTypefaceFor (TootaAssets::FredokaMedium_ttf, TootaAssets::FredokaMedium_ttfSize);
};
inline Typefaces& typefaces() { static Typefaces t; return t; }

// weight: 0 medium, 1 semibold, 2 bold
inline juce::Font font (float size, int weight = 2, float kern = 0.0f)
{
    auto& t = typefaces();
    auto tf = weight >= 2 ? t.bold : weight == 1 ? t.semi : t.medium;
    return juce::Font (juce::FontOptions (tf).withHeight (size)).withExtraKerningFactor (kern);
}

inline const juce::Image& logo()
{
    static juce::Image img = juce::ImageCache::getFromMemory (TootaAssets::tootalogo_png, TootaAssets::tootalogo_pngSize);
    return img;
}

// ---------------------------------------------------------------- shapes
// The brand's "sticker": flat fill, thick ink outline and a hard ink shadow down and right
inline void sticker (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill, float radius = 10.0f, float shadow = 3.0f, float outline = 2.2f)
{
    if (shadow > 0) { g.setColour (ink); g.fillRoundedRectangle (r.translated (shadow, shadow), radius); }
    g.setColour (fill); g.fillRoundedRectangle (r, radius);
    g.setColour (ink); g.drawRoundedRectangle (r.reduced (outline * 0.5f), radius, outline);
}
inline void panel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 14.0f) { sticker (g, r, cream2, radius, 0.0f, 2.0f); }
inline void screen (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
{
    g.setColour (cream3); g.fillRoundedRectangle (r, radius);
    g.setColour (ink); g.drawRoundedRectangle (r.reduced (0.9f), radius, 1.8f);
}
inline void label (juce::Graphics& g, const juce::String& t, juce::Rectangle<int> r, juce::Justification j = juce::Justification::centred, float size = 11.0f, juce::Colour c = ink)
{
    g.setColour (c); g.setFont (font (size, 2, 0.04f)); g.drawText (t, r, j, true);
}

// Header strip: logo, plugin name, tagline. Returns the area to the right of the title for controls.
inline juce::Rectangle<int> header (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& name, const juce::String& tagline, juce::Colour nameColour = ink)
{
    g.setColour (cream); g.fillRect (area);
    g.setColour (ink); g.fillRect (area.getX(), area.getBottom() - 3, area.getWidth(), 3);
    auto r = area.reduced (14, 8).withTrimmedBottom (3);
    const auto& img = logo();
    const float lh = (float) r.getHeight() * 0.92f, lw = lh * (float) img.getWidth() / (float) juce::jmax (1, img.getHeight());
    g.drawImage (img, juce::Rectangle<float> ((float) r.getX(), (float) r.getY() + (r.getHeight() - lh) * 0.5f, lw, lh), juce::RectanglePlacement::centred);
    auto t = r.withTrimmedLeft ((int) lw + 12);
    const float nameSize = juce::jmin (30.0f, (float) r.getHeight() * 0.62f);
    juce::GlyphArrangement ga; ga.addLineOfText (font (nameSize, 2), name, 0, 0);
    const int nameW = (int) std::ceil (ga.getBoundingBox (0, -1, true).getWidth()) + 4;
    g.setColour (nameColour); g.setFont (font (nameSize, 2)); g.drawText (name, t.withWidth (nameW).withTrimmedBottom (tagline.isEmpty() ? 0 : (int) (r.getHeight() * 0.3f)), juce::Justification::centredLeft);
    if (tagline.isNotEmpty())
    {
        g.setColour (inkSoft); g.setFont (font (juce::jmin (11.5f, r.getHeight() * 0.24f), 1, 0.06f));
        g.drawText (tagline.toUpperCase(), t.withTop (t.getBottom() - (int) (r.getHeight() * 0.32f)), juce::Justification::centredLeft);
    }
    return t.withTrimmedLeft (juce::jmax (nameW, 150) + 16);
}

// ---------------------------------------------------------------- look and feel
class Look : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour (juce::ResizableWindow::backgroundColourId, cream);
        setColour (juce::TextButton::buttonColourId, cream);
        setColour (juce::TextButton::buttonOnColourId, teal);
        setColour (juce::TextButton::textColourOffId, ink);
        setColour (juce::TextButton::textColourOnId, cream);
        setColour (juce::ToggleButton::textColourId, ink);
        setColour (juce::ToggleButton::tickColourId, teal);
        setColour (juce::ComboBox::backgroundColourId, cream);
        setColour (juce::ComboBox::textColourId, ink);
        setColour (juce::ComboBox::outlineColourId, ink);
        setColour (juce::ComboBox::arrowColourId, ink);
        setColour (juce::PopupMenu::backgroundColourId, cream);
        setColour (juce::PopupMenu::textColourId, ink);
        setColour (juce::PopupMenu::headerTextColourId, coral);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, teal);
        setColour (juce::PopupMenu::highlightedTextColourId, cream);
        setColour (juce::Slider::rotarySliderFillColourId, teal);
        setColour (juce::Slider::rotarySliderOutlineColourId, cream3);
        setColour (juce::Slider::thumbColourId, cream);
        setColour (juce::Slider::trackColourId, teal);
        setColour (juce::Slider::backgroundColourId, cream3);
        setColour (juce::Slider::textBoxTextColourId, ink);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, ink);
        setColour (juce::BubbleComponent::backgroundColourId, cream);
        setColour (juce::BubbleComponent::outlineColourId, ink);
        setColour (juce::TooltipWindow::backgroundColourId, cream);
        setColour (juce::TooltipWindow::textColourId, ink);
        setColour (juce::TooltipWindow::outlineColourId, ink);
        setColour (juce::TextEditor::backgroundColourId, cream);
        setColour (juce::TextEditor::textColourId, ink);
        setColour (juce::TextEditor::outlineColourId, ink);
        setColour (juce::TextEditor::focusedOutlineColourId, teal);
        setColour (juce::ScrollBar::thumbColourId, ink.withAlpha (0.35f));
        setColour (juce::AlertWindow::backgroundColourId, cream);
        setColour (juce::AlertWindow::textColourId, ink);
        setColour (juce::AlertWindow::outlineColourId, ink);
    }

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        auto& t = typefaces();
        return f.isBold() ? t.bold : t.semi;
    }

    // chunky knob: ink-outlined cream cap, coloured value arc, ink pointer
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3);
        const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float ang = a0 + pos * (a1 - a0);
        const float tw = juce::jmax (3.0f, r * 0.16f);
        const auto col = s.findColour (juce::Slider::rotarySliderFillColourId);
        juce::Path track, val;
        track.addCentredArc (c.x, c.y, r - tw * 0.5f, r - tw * 0.5f, 0, a0, a1, true);
        const bool bipolar = s.getProperties()["bipolar"];
        const float from = bipolar ? (a0 + a1) * 0.5f : a0;
        val.addCentredArc (c.x, c.y, r - tw * 0.5f, r - tw * 0.5f, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (cream3); g.strokePath (track, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (s.isEnabled() ? col : inkSoft.withAlpha (0.4f)); g.strokePath (val, juce::PathStrokeType (tw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float body = r - tw - juce::jmax (2.0f, r * 0.08f);
        const float sh = juce::jmax (1.5f, body * 0.07f);
        g.setColour (ink); g.fillEllipse (c.x - body + sh, c.y - body + sh, body * 2, body * 2);
        g.setColour (cream); g.fillEllipse (c.x - body, c.y - body, body * 2, body * 2);
        g.setColour (ink); g.drawEllipse (c.x - body, c.y - body, body * 2, body * 2, juce::jmax (1.6f, body * 0.09f));
        juce::Path p;
        const float pw = juce::jmax (2.4f, body * 0.16f);
        p.addRoundedRectangle (-pw * 0.5f, -body + body * 0.18f, pw, body * 0.55f, pw * 0.5f);
        g.setColour (ink); g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
    }

    // horizontal / vertical faders: ink-outlined track with a coloured fill and a cream sticker thumb
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle st, juce::Slider& s) override
    {
        const bool vert = st == juce::Slider::LinearVertical || st == juce::Slider::LinearBarVertical;
        if (st == juce::Slider::LinearBar || st == juce::Slider::LinearBarVertical)
        {
            auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (1);
            g.setColour (cream3); g.fillRoundedRectangle (r, 6);
            g.setColour (s.findColour (juce::Slider::trackColourId));
            g.fillRoundedRectangle (vert ? r.withTop (pos) : r.withRight (pos), 6);
            g.setColour (ink); g.drawRoundedRectangle (r, 6, 1.6f);
            return;
        }
        const float t = 7.0f;
        auto tr = vert ? juce::Rectangle<float> (x + w * 0.5f - t * 0.5f, (float) y + 4, t, (float) h - 8)
                       : juce::Rectangle<float> ((float) x + 4, y + h * 0.5f - t * 0.5f, (float) w - 8, t);
        g.setColour (cream3); g.fillRoundedRectangle (tr, t * 0.5f);
        g.setColour (s.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (vert ? tr.withTop (pos) : tr.withRight (pos), t * 0.5f);
        g.setColour (ink); g.drawRoundedRectangle (tr, t * 0.5f, 1.5f);
        const float th = 16.0f;
        auto thumb = vert ? juce::Rectangle<float> (x + w * 0.5f - th * 0.8f, pos - th * 0.5f, th * 1.6f, th)
                          : juce::Rectangle<float> (pos - th * 0.5f, y + h * 0.5f - th * 0.8f, th, th * 1.6f);
        sticker (g, thumb, cream, 5.0f, 2.0f, 1.8f);
    }

    // buttons: pill sticker, filled with the button's "on" colour when toggled on
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedRight (2.5f).withTrimmedBottom (2.5f);
        const bool on = b.getToggleState();
        const auto onCol = b.findColour (juce::TextButton::buttonOnColourId);
        auto fill = on ? onCol : b.findColour (juce::TextButton::buttonColourId);
        if (hi && ! down) fill = on ? fill.brighter (0.08f) : fill.darker (0.04f);
        if (! b.isEnabled()) fill = fill.withMultipliedSaturation (0.3f);
        const float rad = juce::jmin (r.getHeight() * 0.5f, 14.0f);
        if (down) r = r.translated (1.5f, 1.5f);
        g.setColour (ink.withAlpha (b.isEnabled() ? 1.0f : 0.35f));
        if (! down) g.fillRoundedRectangle (r.translated (2.5f, 2.5f), rad);
        g.setColour (fill); g.fillRoundedRectangle (r, rad);
        g.setColour (ink.withAlpha (b.isEnabled() ? 1.0f : 0.35f)); g.drawRoundedRectangle (r.reduced (0.9f), rad, 1.8f);
    }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return font (juce::jmin (14.0f, h * 0.5f), 2, 0.04f); }
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        auto r = b.getLocalBounds().withTrimmedRight (3).withTrimmedBottom (3);
        if (down) r = r.translated (1, 1);
        const bool on = b.getToggleState();
        auto c = b.findColour (on ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        if (on && b.findColour (juce::TextButton::buttonOnColourId).getPerceivedBrightness() > 0.72f) c = ink;   // light fills keep ink text
        g.setColour (c.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.setFont (getTextButtonFont (b, b.getHeight()));
        g.drawFittedText (b.getButtonText(), r.reduced (4, 0), juce::Justification::centred, 1, 0.8f);
    }

    bool pillToggles = false;   // draw toggle buttons as pill buttons (filled when on) instead of switches

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hi, bool down) override
    {
        if (pillToggles)
        {
            auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedRight (2.5f).withTrimmedBottom (2.5f);
            const bool on = b.getToggleState();
            const auto col = b.findColour (juce::ToggleButton::tickColourId);
            if (down) r = r.translated (1.5f, 1.5f);
            sticker (g, r, on ? col : (hi ? cream3 : cream), r.getHeight() * 0.5f, down ? 0.0f : 2.5f, 1.8f);
            g.setColour (on && col.getPerceivedBrightness() < 0.72f ? cream : ink);
            g.setFont (font (juce::jmin (12.5f, r.getHeight() * 0.5f), 2, 0.04f));
            g.drawFittedText (b.getButtonText().toUpperCase(), r.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.8f);
            return;
        }
        auto r = b.getLocalBounds().toFloat();
        const bool on = b.getToggleState();
        const auto col = b.findColour (juce::ToggleButton::tickColourId);
        const float bh = juce::jmin (r.getHeight() - 4.0f, 18.0f);
        // a little pill switch
        auto sw = juce::Rectangle<float> (r.getX() + 2, r.getCentreY() - bh * 0.5f, bh * 1.75f, bh);
        g.setColour (on ? col : (hi ? cream3.darker (0.05f) : cream3)); g.fillRoundedRectangle (sw, bh * 0.5f);
        g.setColour (ink); g.drawRoundedRectangle (sw.reduced (0.8f), bh * 0.5f, 1.6f);
        const float d = bh - 6;
        const float cx = on ? sw.getRight() - 3 - d : sw.getX() + 3;
        g.setColour (cream); g.fillEllipse (cx, sw.getY() + 3, d, d);
        g.setColour (ink); g.drawEllipse (cx, sw.getY() + 3, d, d, 1.4f);
        g.setColour (b.findColour (juce::ToggleButton::textColourId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.setFont (font (juce::jmin (13.0f, r.getHeight() * 0.62f), 1));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (sw.getWidth() + 8), juce::Justification::centredLeft, true);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& c) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f).withTrimmedRight (2.5f).withTrimmedBottom (2.5f);
        const float rad = juce::jmin (r.getHeight() * 0.5f, 10.0f);
        g.setColour (ink); g.fillRoundedRectangle (r.translated (2.5f, 2.5f), rad);
        g.setColour (c.findColour (juce::ComboBox::backgroundColourId)); g.fillRoundedRectangle (r, rad);
        g.setColour (ink); g.drawRoundedRectangle (r.reduced (0.9f), rad, 1.8f);
        juce::Path a;
        const float ax = r.getRight() - 13, ay = r.getCentreY();
        a.addTriangle (ax - 4.5f, ay - 2.5f, ax + 4.5f, ay - 2.5f, ax, ay + 3.5f);
        g.setColour (ink); g.fillPath (a);
    }
    juce::Font getComboBoxFont (juce::ComboBox& c) override { return font (juce::jmin (14.0f, c.getHeight() * 0.5f), 2); }
    void positionComboBoxText (juce::ComboBox& box, juce::Label& l) override
    {
        l.setBounds (8, 0, box.getWidth() - 30, box.getHeight() - 3);
        l.setFont (getComboBoxFont (box));
    }
    juce::Font getPopupMenuFont() override { return font (15.0f, 1); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (cream);
        g.setColour (ink); g.drawRect (0, 0, w, h, 2);
    }
    juce::Font getLabelFont (juce::Label& l) override { return font (juce::jmax (10.0f, l.getFont().getHeight()), l.getFont().isBold() ? 2 : 1); }
    juce::Font getSliderPopupFont (juce::Slider&) override { return font (14.0f, 2); }
    int getSliderPopupPlacement (juce::Slider&) override { return juce::BubbleComponent::above; }
    void drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>&, const juce::Rectangle<float>& body) override
    {
        sticker (g, body.reduced (1).withTrimmedRight (2).withTrimmedBottom (2), cream, 7.0f, 2.0f, 1.8f);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        g.fillAll (cream);
        g.setColour (ink); g.drawRect (0, 0, w, h, 2);
        g.setFont (font (14.0f, 1)); g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (8, 4), juce::Justification::centredLeft, 4);
    }
};

// Installs the brand look as the default for the whole plugin (so every font is Fredoka), and removes
// it again when the last editor closes.
struct LookScope
{
    juce::SharedResourcePointer<Look> look;
    LookScope() { juce::LookAndFeel::setDefaultLookAndFeel (&look.get()); }
    ~LookScope() { if (look.getReferenceCount() <= 1) juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
};
} // namespace toota
