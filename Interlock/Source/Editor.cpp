#include "Editor.h"

namespace ic
{
    const juce::Colour bg      = toota::cream;
    const juce::Colour panel   = toota::cream2;
    const juce::Colour plotBg  = toota::cream3;
    const juce::Colour grid    = toota::ink.withAlpha (0.13f);
    const juce::Colour text    = toota::ink;
    const juce::Colour dim     = toota::inkSoft;
    const juce::Colour kick    = toota::coral;
    const juce::Colour bass    = toota::teal;
    const juce::Colour comp    = toota::plum;
    const juce::Colour curve   = toota::ink;
}

static juce::Font fnt (float size, bool bold = true, float kern = 0.0f) { return toota::font (size * 1.08f, bold ? 2 : 1, kern * 0.5f); }
static float pv (InterlockProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }
static juce::String hzText (double f) { return f >= 1000 ? juce::String (f / 1000.0, f >= 10000 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
static juce::String noteText (double f)
{
    const double midi = 69 + 12 * std::log2 (f / 440.0);
    const int n = (int) std::round (midi);
    const int cents = (int) std::round ((midi - n) * 100);
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1) + (cents != 0 ? juce::String (cents > 0 ? " +" : " ") + juce::String (cents) : juce::String());
}

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
static constexpr double kFMin = 10.0, kFMax = 30000.0;

EqDisplay::EqDisplay (InterlockProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (true);
}

float EqDisplay::xForFreq (double f) const { return plot.getX() + plot.getWidth() * (float) (std::log (f / kFMin) / std::log (kFMax / kFMin)); }
double EqDisplay::freqForX (float x) const { return kFMin * std::pow (kFMax / kFMin, (x - plot.getX()) / plot.getWidth()); }
float EqDisplay::rangeDb() const { const float r[] { 3, 6, 12, 30 }; return r[juce::jlimit (0, 3, proc.rangeIndex)]; }
float EqDisplay::yForEq (float db) const { return plot.getCentreY() - db / rangeDb() * plot.getHeight() * 0.46f; }
float EqDisplay::eqForY (float y) const { return (plot.getCentreY() - y) / (plot.getHeight() * 0.46f) * rangeDb(); }
float EqDisplay::yForSpec (float db) const { return plot.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 96.0f) / 108.0f) * plot.getHeight(); }

juce::Colour EqDisplay::bandColour (int b) const
{
    static const juce::uint32 c[] { 0xff0294aa, 0xfffd694c, 0xfffe9e3b, 0xff8a4f7d, 0xff7d9a3a, 0xff3f7fd0, 0xffe9789f, 0xffd9b23a };
    return juce::Colour (c[b % 8]);
}

juce::Point<float> EqDisplay::bandNode (int b) const
{
    const auto p = proc.bandParams (b);
    float db = ileq::hasGain (p.shape) ? p.gainDb * pv (proc, ilp::gainScale) : 0.0f;
    if (! ileq::hasGain (p.shape))
        db = (float) juce::jlimit (-rangeDb(), rangeDb(), (float) (20.0 * std::log10 (std::max (0.1f, p.q) / 0.7071)));   // cuts / notch / band pass: height shows Q
    return { xForFreq (p.freq), yForEq (juce::jlimit (-rangeDb(), rangeDb(), db)) };
}

double EqDisplay::interlockDb (double hz, bool live) const
{
    // Interlock's own kick dip (dynamic), sub duck and bass focus, as the processor builds them
    const double fs = 48000.0;
    const float dipF = pv (proc, ilp::dipFreq), dipQ = pv (proc, ilp::dipQ), depth = pv (proc, ilp::dipDepth);
    const float duck = pv (proc, ilp::duck), bF = pv (proc, ilp::boostFreq), bG = pv (proc, ilp::boostGain);
    const float frac = live ? (depth > 0.01f ? juce::jlimit (0.0f, 1.0f, -proc.dipNowDb.load() / depth) : 0.0f) : 1.0f;
    auto c1 = juce::dsp::IIR::Coefficients<float>::makePeakFilter (fs, dipF, dipQ, juce::Decibels::decibelsToGain (-depth * frac));
    auto c2 = juce::dsp::IIR::Coefficients<float>::makeLowShelf (fs, dipF * 1.6f, 0.707f, juce::Decibels::decibelsToGain (-duck * frac));
    auto c3 = juce::dsp::IIR::Coefficients<float>::makePeakFilter (fs, bF, 0.9f, juce::Decibels::decibelsToGain (bG));
    const double m = c1->getMagnitudeForFrequency (hz, fs) * c2->getMagnitudeForFrequency (hz, fs) * c3->getMagnitudeForFrequency (hz, fs);
    return juce::Decibels::gainToDecibels (m, -120.0);
}

double EqDisplay::totalDb (double hz, bool live) const
{
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const float scale = pv (proc, ilp::gainScale);
    double db = interlockDb (hz, live);
    for (int b = 0; b < ileq::kBands; ++b)
    {
        auto p = proc.bandParams (b);
        if (! p.used || ! p.on) continue;
        if (live && p.dynamic) p.gainDb = (ileq::hasGain (p.shape) ? p.gainDb : 0.0f) + proc.eq.dynNow[(size_t) b].load() / std::max (0.001f, scale);
        db += ileq::Engine::specDb (p, scale, fs, hz);
    }
    return db;
}

EqDisplay::Hit EqDisplay::hitTest (juce::Point<float> pt) const
{
    Hit best; float bd = 13.0f;
    for (int b = 0; b < ileq::kBands; ++b)
        if (proc.bandParams (b).used)
        {
            const float d = pt.getDistanceFrom (bandNode (b));
            if (d < bd) { bd = d; best = { 1, b }; }
        }
    const juce::Point<float> dip { xForFreq (pv (proc, ilp::dipFreq)), yForEq (juce::jlimit (-rangeDb(), rangeDb(), -pv (proc, ilp::dipDepth))) };
    const juce::Point<float> foc { xForFreq (pv (proc, ilp::boostFreq)), yForEq (juce::jlimit (-rangeDb(), rangeDb(), pv (proc, ilp::boostGain))) };
    if (pt.getDistanceFrom (dip) < bd) { bd = pt.getDistanceFrom (dip); best = { 2, 0 }; }
    if (pt.getDistanceFrom (foc) < bd) { best = { 3, 0 }; }
    return best;
}

bool EqDisplay::spectrumPeakNear (juce::Point<float> pt, double& hz, float& db) const
{
    // Spectrum Grab: the loudest peak of the post spectrum within a third of an octave of the mouse
    if (proc.analyserMode == 0) return false;
    const auto& c = proc.spectrum.curves[proc.analyserMode == 1 ? ileq::Analyser::pre : ileq::Analyser::post];
    const double f = freqForX (pt.x);
    const int p0 = (int) ileq::pointOf (f / 1.26), p1 = (int) ileq::pointOf (f * 1.26);
    int bi = -1; float bv = -200;
    for (int p = std::max (1, p0); p <= std::min (ileq::kPoints - 2, p1); ++p)
        if (c[(size_t) p] > bv && c[(size_t) p] >= c[(size_t) p - 1] && c[(size_t) p] >= c[(size_t) p + 1]) { bv = c[(size_t) p]; bi = p; }
    if (bi < 0 || bv < -80) return false;
    if (std::abs (yForSpec (bv) - pt.y) > 22) return false;
    hz = ileq::pointHz (bi); db = bv;
    return true;
}

void EqDisplay::setParam (const juce::String& id, float v)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (v)));
}

void EqDisplay::select (int band, bool add)
{
    if (! add) selected.clear();
    if (band >= 0)
    {
        auto it = std::find (selected.begin(), selected.end(), band);
        if (it != selected.end() && add) selected.erase (it); else if (it == selected.end()) selected.push_back (band);
    }
    if (onSelect) onSelect (primary());
    repaint();
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    downPos = e.position;
    drag = hitTest (e.position);
    pendingCreate = lasso = grabbing = false;
    dragStart.clear();
    if (drag.kind == 1)
    {
        if (e.mods.isPopupMenu()) { select (drag.index, false); bandMenu (drag.index); drag = {}; return; }
        if (e.mods.isAltDown()) { proc.removeBand (drag.index); select (-1, false); drag = {}; return; }
        const bool already = std::find (selected.begin(), selected.end(), drag.index) != selected.end();
        if (e.mods.isShiftDown()) select (drag.index, true);
        else if (! already) select (drag.index, false);
        else if (onSelect) { selected.erase (std::find (selected.begin(), selected.end(), drag.index)); selected.push_back (drag.index); onSelect (drag.index); }
        for (int b : selected) dragStart.push_back ({ b, proc.bandParams (b) });
        for (int b : selected)
            for (int f : { ilp::fFreq, ilp::fGain, ilp::fQ })
                if (auto* prm = proc.apvts.getParameter (ilp::band (b, f))) prm->beginChangeGesture();
        return;
    }
    if (drag.kind >= 2)
    {
        for (auto* id : drag.kind == 2 ? std::initializer_list<const char*> { ilp::dipFreq, ilp::dipDepth } : std::initializer_list<const char*> { ilp::boostFreq, ilp::boostGain })
            proc.apvts.getParameter (id)->beginChangeGesture();
        return;
    }
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { lasso = true; lassoRect = { e.position, e.position }; return; }
    double hz; float db;
    if (spectrumPeakNear (e.position, hz, db)) grabbing = true;
    pendingCreate = true;
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (lasso)
    {
        lassoRect = juce::Rectangle<float> (downPos, e.position);
        selected.clear();
        for (int b = 0; b < ileq::kBands; ++b) if (proc.bandParams (b).used && lassoRect.contains (bandNode (b))) selected.push_back (b);
        if (onSelect) onSelect (primary());
        repaint();
        return;
    }
    if (pendingCreate && e.getDistanceFromDragStart() > 3)
    {
        // click and drag on an empty spot makes a band there (Spectrum Grab: on a peak, it grabs the peak)
        pendingCreate = false;
        double hz = freqForX (downPos.x); float db = 0;
        int shape = ileq::shBell;
        if (grabbing) { float sdb; spectrumPeakNear (downPos, hz, sdb); }
        else
        {
            db = eqForY (downPos.y);
            if (hz < 30) shape = ileq::shLowCut; else if (hz > 15000) shape = ileq::shHighCut;
        }
        const int b = proc.addBand (hz, db, shape);
        if (b < 0) return;
        select (b, false);
        drag = { 1, b };
        dragStart = { { b, proc.bandParams (b) } };
        downPos = { xForFreq (hz), yForEq (db) };
    }
    if (drag.kind == 1)
    {
        const float dx = e.position.x - downPos.x, dy = e.position.y - downPos.y;
        const double ratio = freqForX (plot.getX() + dx) / kFMin;
        const float fine = e.mods.isShiftDown() && dragStart.size() == 1 ? 0.2f : 1.0f;
        for (auto& [b, start] : dragStart)
        {
            auto p = proc.bandParams (b);
            p.freq = (float) juce::jlimit (10.0, 30000.0, start.freq * std::pow (ratio, fine));
            if (ileq::hasGain (p.shape))
                p.gainDb = juce::jlimit (-30.0f, 30.0f, start.gainDb - dy / (plot.getHeight() * 0.46f) * rangeDb() * fine / std::max (0.01f, pv (proc, ilp::gainScale)));
            else
                p.q = juce::jlimit (0.025f, 40.0f, start.q * std::pow (2.0f, -dy / 60.0f * fine));
            proc.setBandParams (b, p, false);
        }
        repaint();
        return;
    }
    if (drag.kind >= 2)
    {
        const double f = freqForX (e.position.x);
        const float db = eqForY (e.position.y);
        if (drag.kind == 2) { setParam (ilp::dipFreq, (float) f); setParam (ilp::dipDepth, -db); }
        else { setParam (ilp::boostFreq, (float) f); setParam (ilp::boostGain, db); }
        repaint();
    }
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (drag.kind == 1)
        for (int b : selected)
            for (int f : { ilp::fFreq, ilp::fGain, ilp::fQ })
                if (auto* prm = proc.apvts.getParameter (ilp::band (b, f))) prm->endChangeGesture();
    if (drag.kind >= 2)
        for (auto* id : drag.kind == 2 ? std::initializer_list<const char*> { ilp::dipFreq, ilp::dipDepth } : std::initializer_list<const char*> { ilp::boostFreq, ilp::boostGain })
            proc.apvts.getParameter (id)->endChangeGesture();
    if (pendingCreate) select (-1, false);     // a plain click on empty space clears the selection
    pendingCreate = lasso = false;
    drag = {};
    repaint();
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position);
    if (h.kind == 1)
    {
        auto p = proc.bandParams (h.index); p.on = ! p.on; proc.setBandParams (h.index, p);   // double-click a band: bypass it
        return;
    }
    if (h.kind == 0)
    {
        const double hz = freqForX (e.position.x);
        const int b = proc.addBand (hz, eqForY (e.position.y), hz < 30 ? ileq::shLowCut : hz > 15000 ? ileq::shHighCut : ileq::shBell);
        if (b >= 0) select (b, false);
    }
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    hoverPos = e.position;
    hover = hitTest (e.position);
    double hz; float db;
    grabbing = hover.kind == 0 && spectrumPeakNear (e.position, hz, db);
    setMouseCursor (hover.kind > 0 ? juce::MouseCursor::DraggingHandCursor : grabbing ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto h = hitTest (e.position);
    if (h.kind == 2)
    {
        auto* p = proc.apvts.getParameter (ilp::dipQ);
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + w.deltaY * 0.25f));
        return;
    }
    if (h.kind != 1) return;
    auto p = proc.bandParams (h.index);
    if (e.mods.isShiftDown() || e.mods.isCommandDown())
    {
        // shift-wheel: the dynamic range ring
        if (! p.dynamic) { p.dynamic = true; p.range = 0; }
        p.range = juce::jlimit (-30.0f, 30.0f, p.range + w.deltaY * 6.0f);
    }
    else p.q = juce::jlimit (0.025f, 40.0f, p.q * std::pow (2.0f, w.deltaY * 1.5f));
    proc.setBandParams (h.index, p);
    repaint();
}

bool EqDisplay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        for (int b : selected) proc.removeBand (b);
        select (-1, false);
        return true;
    }
    if (k == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selected.clear();
        for (int b = 0; b < ileq::kBands; ++b) if (proc.bandParams (b).used) selected.push_back (b);
        if (onSelect) onSelect (primary());
        repaint();
        return true;
    }
    return false;
}

void EqDisplay::bandMenu (int b)
{
    const auto p = proc.bandParams (b);
    juce::PopupMenu m, shapes, sl, pl;
    for (int s = 0; s < ileq::kNumShapes; ++s) shapes.addItem (100 + s, ileq::shapeName (s), true, s == p.shape);
    for (int i = 0; i < (int) ileq::slopes().size(); ++i) sl.addItem (200 + i, ileq::slopes()[(size_t) i] == 0 ? juce::String ("Brickwall") : juce::String (ileq::slopes()[(size_t) i]) + " dB/oct", ileq::hasSlope (p.shape) && (ileq::slopes()[(size_t) i] != 0 || ileq::isCut (p.shape)), i == p.slopeIndex);
    for (int i = 0; i < ileq::kNumPlaces; ++i) pl.addItem (300 + i, ileq::placeName (i), true, i == p.place);
    m.addSectionHeader ("Band " + juce::String (b + 1) + "  " + hzText (p.freq));
    m.addSubMenu ("Shape", shapes);
    m.addSubMenu ("Slope", sl, ileq::hasSlope (p.shape));
    m.addSubMenu ("Stereo placement", pl);
    m.addItem (400, "Dynamic EQ", true, p.dynamic);
    m.addItem (401, "Use the external sidechain", p.dynamic, p.external);
    m.addItem (402, "Solo", true, proc.eq.soloBand.load() == b);
    m.addItem (403, "Enabled", true, p.on);
    m.addSeparator();
    m.addItem (500, "Delete");
    juce::Component::SafePointer<EqDisplay> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safe, b] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pr = safe->proc;
        auto q = pr.bandParams (b);
        if (r >= 100 && r < 100 + ileq::kNumShapes) { q.shape = r - 100; q.slopeIndex = ileq::defaultSlopeIndex (q.shape); }
        else if (r >= 200 && r < 300) q.slopeIndex = r - 200;
        else if (r >= 300 && r < 400) q.place = r - 300;
        else if (r == 400) q.dynamic = ! q.dynamic;
        else if (r == 401) q.external = ! q.external;
        else if (r == 402) { pr.eq.soloBand = pr.eq.soloBand.load() == b ? -1 : b; return; }
        else if (r == 403) q.on = ! q.on;
        else if (r == 500) { pr.removeBand (b); safe->select (-1, false); return; }
        pr.setBandParams (b, q);
        if (safe->onSelect) safe->onSelect (b);
    });
}

void EqDisplay::drawPiano (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (toota::cream); g.fillRect (r);
    for (int n = 12; n <= 127; ++n)
    {
        const double f0 = 440.0 * std::pow (2.0, (n - 69.5) / 12.0), f1 = 440.0 * std::pow (2.0, (n - 68.5) / 12.0);
        const float x0 = xForFreq (f0), x1 = xForFreq (f1);
        const int pc = n % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (black) { g.setColour (toota::ink); g.fillRect (juce::Rectangle<float> (x0, r.getY(), x1 - x0, r.getHeight() * 0.62f)); }
        else if (pc == 0 || pc == 5) { g.setColour (toota::ink.withAlpha (0.35f)); g.fillRect (x0, r.getY(), 1.0f, r.getHeight()); }
        if (pc == 0 && x1 - x0 > 1.2f)
        {
            g.setColour (toota::ink); g.setFont (fnt (8.0f));
            g.drawText ("C" + juce::String (n / 12 - 1), juce::Rectangle<float> (x0 + 1, r.getBottom() - 10, 26, 10), juce::Justification::left);
        }
    }
    g.setColour (toota::ink); g.drawRect (r, 1.2f);
}

void EqDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    toota::screen (g, area.reduced (1), 10);
    const bool pianoOn = proc.piano;
    plot = area.reduced (14, 10).withTrimmedBottom (pianoOn ? 34.0f : 16.0f);
    const double fs = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;

    // grid
    g.setFont (fnt (9.5f, false));
    for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 800.0, 1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 8000.0, 10000.0, 20000.0 })
    {
        const float x = xForFreq (f);
        const bool major = f == 100 || f == 1000 || f == 10000;
        g.setColour (ic::grid.withAlpha (major ? 0.3f : 0.13f));
        g.fillRect (x - 0.5f, plot.getY(), 1.0f, plot.getHeight());
        const bool labelled = f == 20 || f == 50 || f == 100 || f == 200 || f == 500 || f == 1000 || f == 2000 || f == 5000 || f == 10000 || f == 20000;
        if (labelled && ! pianoOn)
        {
            g.setColour (ic::dim);
            g.drawText (f >= 1000 ? juce::String ((int) f / 1000) + "k" : juce::String ((int) f), (int) x - 20, (int) plot.getBottom() + 2, 40, 12, juce::Justification::centred);
        }
    }
    const float R = rangeDb();
    for (float step : { R / 2, R })
        for (float db : { -step, 0.0f, step })
        {
            const float y = yForEq (db);
            g.setColour (ic::grid.withAlpha (db == 0 ? 0.35f : 0.13f));
            g.fillRect (plot.getX(), y - 0.5f, plot.getWidth(), 1.0f);
            g.setColour (ic::dim);
            g.drawText ((db > 0 ? "+" : "") + juce::String (db, R < 6 ? 1 : 0), (int) plot.getRight() - 34, (int) y - 12, 32, 11, juce::Justification::centredRight);
        }

    // ---- spectra
    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    auto specPath = [this] (const ileq::Curve& c, bool closed)
    {
        juce::Path p;
        for (int i = 0; i < ileq::kPoints; ++i)
        {
            const double hz = ileq::pointHz (i);
            if (hz < kFMin || hz > kFMax) continue;
            const float x = xForFreq (hz), y = yForSpec (c[(size_t) i]);
            if (p.isEmpty()) { p.startNewSubPath (x, closed ? plot.getBottom() : y); if (closed) p.lineTo (x, y); } else p.lineTo (x, y);
        }
        if (closed) { p.lineTo (xForFreq (std::min (kFMax, ileq::kHiHz)), plot.getBottom()); p.closeSubPath(); }
        return p;
    };
    const auto& pre = proc.spectrum.curves[ileq::Analyser::pre];
    const auto& post = proc.spectrum.curves[ileq::Analyser::post];
    const auto& sc = proc.spectrum.curves[ileq::Analyser::side];
    const int mode = proc.analyserMode;
    if (mode == 1 || mode == 3) { g.setColour (toota::tealSoft.withAlpha (0.35f)); g.fillPath (specPath (pre, true)); }
    if (mode >= 2) { g.setColour (toota::ink.withAlpha (0.12f)); g.fillPath (specPath (post, true)); g.setColour (toota::ink.withAlpha (0.45f)); g.strokePath (specPath (post, false), juce::PathStrokeType (1.2f)); }
    if (proc.showSidechain && mode != 0) { g.setColour (ic::kick.withAlpha (0.85f)); g.strokePath (specPath (sc, false), juce::PathStrokeType (1.4f)); }
    // collision with another Interlock in the session: red where both are strong and close in level
    haveOther = proc.otherInstance > 0 && ileq::Registry::get().curveOf (proc.otherInstance, other);
    if (haveOther && mode != 0)
    {
        g.setColour (toota::plum.withAlpha (0.8f)); g.strokePath (specPath (other, false), juce::PathStrokeType (1.3f));
        const auto& mine = mode == 1 ? pre : post;
        for (int i = 1; i < ileq::kPoints; ++i)
        {
            const float a = mine[(size_t) i], o = other[(size_t) i];
            if (a > -60 && o > -60 && std::abs (a - o) < 6.0f)
            {
                const float x0 = xForFreq (ileq::pointHz (i - 1)), x1 = xForFreq (ileq::pointHz (i));
                const float strength = juce::jlimit (0.0f, 1.0f, (std::min (a, o) + 60) / 40.0f) * (1.0f - std::abs (a - o) / 6.0f);
                g.setColour (juce::Colour (0xffe0302a).withAlpha (0.45f * strength));
                g.fillRect (juce::Rectangle<float> (x0, yForSpec (std::max (a, o)), x1 - x0 + 0.5f, plot.getBottom() - yForSpec (std::max (a, o))));
            }
        }
    }

    // ---- EQ curves
    const float scale = pv (proc, ilp::gainScale);
    const int sel = primary();
    for (int b = 0; b < ileq::kBands; ++b)
    {
        auto p = proc.bandParams (b);
        if (! p.used) continue;
        const bool isSel = std::find (selected.begin(), selected.end(), b) != selected.end();
        juce::Path bandFill; bool first = true;
        juce::Path dynArea; juce::Path dynTop;
        for (float x = plot.getX(); x <= plot.getRight(); x += 3.0f)
        {
            const double hz = freqForX (x);
            const float y = yForEq ((float) juce::jlimit (-60.0, 60.0, ileq::Engine::specDb (p, scale, fs, hz)));
            if (first) { bandFill.startNewSubPath (x, yForEq (0)); first = false; }
            bandFill.lineTo (x, y);
            if (p.dynamic && p.on)
            {
                auto q = p; q.gainDb = (ileq::hasGain (p.shape) ? p.gainDb : 0.0f) + p.range;
                const float y2 = yForEq ((float) juce::jlimit (-60.0, 60.0, ileq::Engine::specDb (q, scale, fs, hz)));
                if (dynTop.isEmpty()) dynTop.startNewSubPath (x, y2); else dynTop.lineTo (x, y2);
            }
        }
        bandFill.lineTo (plot.getRight(), yForEq (0)); bandFill.closeSubPath();
        const auto col = bandColour (b);
        g.setColour (col.withAlpha (! p.on ? 0.04f : isSel ? 0.28f : 0.14f)); g.fillPath (bandFill);
        if (! dynTop.isEmpty())
        {
            g.setColour (col.withAlpha (0.9f));
            juce::Path dashed; const float dash[] { 4.0f, 3.0f };
            juce::PathStrokeType (1.3f).createDashedStroke (dashed, dynTop, dash, 2);
            g.fillPath (dashed);
        }
    }
    // the total: dashed = static, solid = what's happening now (dynamics included)
    juce::Path stat, live;
    for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
    {
        const double hz = freqForX (x);
        const float ys = yForEq ((float) juce::jlimit (-60.0, 60.0, totalDb (hz, false)));
        const float yl = yForEq ((float) juce::jlimit (-60.0, 60.0, totalDb (hz, true)));
        if (x == plot.getX()) { stat.startNewSubPath (x, ys); live.startNewSubPath (x, yl); } else { stat.lineTo (x, ys); live.lineTo (x, yl); }
    }
    {
        juce::Path dashed; const float dash[] { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, stat, dash, 2);
        g.setColour (ic::curve.withAlpha (0.45f)); g.fillPath (dashed);
    }
    g.setColour (ic::curve);
    g.strokePath (live, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (lasso) { g.setColour (toota::teal.withAlpha (0.15f)); g.fillRect (lassoRect); g.setColour (toota::teal); g.drawRect (lassoRect, 1.5f); }
    g.restoreState();

    // ---- Interlock's nodes (diamonds) and the EQ bands (circles)
    auto diamond = [&] (juce::Point<float> c, juce::Colour col, const juce::String& name, bool hot)
    {
        const float r = hot ? 10.0f : 8.5f;
        juce::Path d; d.addQuadrilateral (c.x, c.y - r, c.x + r, c.y, c.x, c.y + r, c.x - r, c.y);
        g.setColour (toota::ink); g.fillPath (d, juce::AffineTransform::translation (2, 2));
        g.setColour (col); g.fillPath (d);
        g.setColour (toota::ink); g.strokePath (d, juce::PathStrokeType (2.0f));
        g.setFont (fnt (10.0f)); g.setColour (toota::ink);
        g.drawText (name, juce::Rectangle<float> (c.x - 60, c.y + (c.y < plot.getCentreY() ? 12.0f : -26.0f), 120, 12), juce::Justification::centred);
    };
    const float dipF = pv (proc, ilp::dipFreq), depth = pv (proc, ilp::dipDepth), bF = pv (proc, ilp::boostFreq), bG = pv (proc, ilp::boostGain);
    diamond ({ xForFreq (dipF), yForEq (juce::jlimit (-R, R, -depth)) }, ic::kick, "KICK DIP", hover.kind == 2 || drag.kind == 2);
    diamond ({ xForFreq (bF), yForEq (juce::jlimit (-R, R, bG)) }, ic::bass, "BASS FOCUS", hover.kind == 3 || drag.kind == 3);

    for (int b = 0; b < ileq::kBands; ++b)
    {
        const auto p = proc.bandParams (b);
        if (! p.used) continue;
        const auto c = bandNode (b);
        const bool isSel = std::find (selected.begin(), selected.end(), b) != selected.end();
        const bool hot = (hover.kind == 1 && hover.index == b) || (drag.kind == 1 && drag.index == b);
        const float r = hot || isSel ? 11.0f : 9.0f;
        const auto col = bandColour (b);
        // dynamic range ring
        if (p.dynamic)
        {
            const float dyn = proc.eq.dynNow[(size_t) b].load();
            juce::Path ring; const float a = juce::jlimit (-1.0f, 1.0f, dyn / 30.0f) * juce::MathConstants<float>::pi;
            ring.addCentredArc (c.x, c.y, r + 5, r + 5, 0, 0, a, true);
            g.setColour (col); g.strokePath (ring, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (toota::ink.withAlpha (0.4f)); g.drawEllipse (c.x - r - 5, c.y - r - 5, (r + 5) * 2, (r + 5) * 2, 1.0f);
        }
        g.setColour (toota::ink); g.fillEllipse (c.x - r + 2, c.y - r + 2, r * 2, r * 2);
        g.setColour (p.on ? col : toota::cream3); g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
        g.setColour (toota::ink); g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, isSel ? 2.6f : 1.8f);
        g.setColour (p.on && col.getPerceivedBrightness() < 0.65f ? toota::cream : toota::ink); g.setFont (fnt (10.0f));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (c.x - r, c.y - r, r * 2, r * 2), juce::Justification::centred);
        if (proc.eq.soloBand.load() == b) { g.setColour (toota::orange); g.drawEllipse (c.x - r - 3, c.y - r - 3, (r + 3) * 2, (r + 3) * 2, 2.0f); }
        if (p.place != ileq::plStereo)
        {
            const char* tag[] { "", "L", "R", "M", "S" };
            g.setColour (toota::ink); g.setFont (fnt (9.0f));
            g.drawText (tag[p.place], juce::Rectangle<float> (c.x + r - 2, c.y - r - 10, 14, 10), juce::Justification::left);
        }
    }

    // ---- hover readout (and the spectrum peak being offered for grabbing)
    if (hoverPos.x >= plot.getX() && hoverPos.x <= plot.getRight())
    {
        double hz = freqForX (hoverPos.x); float sdb = 0;
        juce::String txt;
        if (hover.kind == 1)
        {
            const auto p = proc.bandParams (hover.index);
            hz = p.freq;
            txt = "Band " + juce::String (hover.index + 1) + "  " + ileq::shapeName (p.shape) + "  " + hzText (p.freq) + (ileq::hasGain (p.shape) ? "  " + juce::String (p.gainDb, 1) + " dB" : juce::String()) + "  Q " + juce::String (p.q, 2);
        }
        else if (grabbing && spectrumPeakNear (hoverPos, hz, sdb))
        {
            const float x = xForFreq (hz), y = yForSpec (sdb);
            g.setColour (toota::orange); g.fillEllipse (x - 5, y - 5, 10, 10);
            g.setColour (toota::ink); g.drawEllipse (x - 5, y - 5, 10, 10, 1.5f);
            txt = "Grab peak  " + hzText (hz) + "  " + noteText (hz);
        }
        else txt = hzText (hz) + "  " + noteText (hz) + "  " + juce::String (eqForY (hoverPos.y), 1) + " dB";
        auto box = juce::Rectangle<float> (plot.getX() + 6, plot.getY() + 4, 300, 18);
        g.setColour (toota::cream.withAlpha (0.9f)); g.fillRoundedRectangle (box, 5);
        g.setColour (toota::ink); g.setFont (fnt (10.5f));
        g.drawText (txt, box.reduced (6, 0), juce::Justification::centredLeft);
    }

    // legend
    g.setFont (fnt (10.0f));
    float lx = plot.getRight() - 300;
    auto legend = [&] (const juce::String& t, juce::Colour c) { g.setColour (c); g.fillRoundedRectangle (lx, plot.getY() + 8, 10, 10, 2); g.setColour (toota::ink); g.drawText (t, juce::Rectangle<float> (lx + 14, plot.getY() + 5, 100, 16), juce::Justification::centredLeft); lx += 76; };
    if (mode == 1 || mode == 3) legend ("INPUT", toota::tealSoft);
    if (mode >= 2) legend ("OUTPUT", toota::ink.withAlpha (0.4f));
    if (proc.showSidechain && mode != 0) legend ("SIDECHAIN", ic::kick);
    if (haveOther && mode != 0) legend ("OTHER", toota::plum);

    if (pianoOn) drawPiano (g, juce::Rectangle<float> (plot.getX(), plot.getBottom() + 4, plot.getWidth(), 22));
}

//==============================================================================
BandPanel::BandPanel (InterlockProcessor& p) : proc (p)
{
    for (int s = 0; s < ileq::kNumShapes; ++s) shapeBox.addItem (ileq::shapeName (s), s + 1);
    for (int i = 0; i < (int) ileq::slopes().size(); ++i) slopeBox.addItem (ileq::slopes()[(size_t) i] == 0 ? juce::String ("Brickwall") : juce::String (ileq::slopes()[(size_t) i]) + " dB/oct", i + 1);
    for (int i = 0; i < ileq::kNumPlaces; ++i) placeBox.addItem (ileq::placeName (i), i + 1);
    for (auto* c : { &shapeBox, &slopeBox, &placeBox }) addAndMakeVisible (c);
    for (auto* k : { &freqK, &gainK, &qK, &rangeK, &thrK })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k->setPopupDisplayEnabled (true, true, this, 1500);
        addAndMakeVisible (k);
    }
    gainK.getProperties().set ("bipolar", true);
    rangeK.getProperties().set ("bipolar", true);
    rangeK.setColour (juce::Slider::rotarySliderFillColourId, toota::coral);
    thrK.setColour (juce::Slider::rotarySliderFillColourId, toota::coral);
    for (auto* t : { &dynT, &autoT, &extT, &onT }) { t->setColour (juce::ToggleButton::tickColourId, toota::teal); addAndMakeVisible (t); }
    dynT.setColour (juce::ToggleButton::tickColourId, toota::coral);
    extT.setTooltip ("Trigger this band from the sidechain input (your kick) instead of its own signal");
    autoT.setTooltip ("Threshold follows the level automatically");
    soloB.setClickingTogglesState (false);
    soloB.onClick = [this] { if (current >= 0) proc.eq.soloBand = proc.eq.soloBand.load() == current ? -1 : current; refresh(); };
    delB.onClick = [this] { if (current >= 0) { proc.removeBand (current); if (onDelete) onDelete(); } };
    delB.setColour (juce::TextButton::buttonColourId, toota::cream);
    for (auto* b : { &soloB, &delB }) addAndMakeVisible (b);
    setBand (-1);
}

void BandPanel::setBand (int b)
{
    current = b;
    sAtt.clear(); bAtt.clear(); cAtt.clear();
    const bool ok = b >= 0;
    for (auto* c : getChildren()) c->setVisible (ok);
    if (! ok) { repaint(); return; }
    auto& a = proc.apvts;
    cAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (a, ilp::band (b, ilp::fShape), shapeBox));
    cAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (a, ilp::band (b, ilp::fSlope), slopeBox));
    cAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (a, ilp::band (b, ilp::fPlace), placeBox));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (a, ilp::band (b, ilp::fFreq), freqK));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (a, ilp::band (b, ilp::fGain), gainK));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (a, ilp::band (b, ilp::fQ), qK));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (a, ilp::band (b, ilp::fRange), rangeK));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (a, ilp::band (b, ilp::fThr), thrK));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (a, ilp::band (b, ilp::fDyn), dynT));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (a, ilp::band (b, ilp::fAuto), autoT));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (a, ilp::band (b, ilp::fExt), extT));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (a, ilp::band (b, ilp::fOn), onT));
    for (auto* k : { &freqK, &gainK, &qK, &rangeK, &thrK })
        if (auto* prm = a.getParameter (ilp::band (b, k == &freqK ? ilp::fFreq : k == &gainK ? ilp::fGain : k == &qK ? ilp::fQ : k == &rangeK ? ilp::fRange : ilp::fThr)))
            k->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
    static const juce::uint32 c[] { 0xff0294aa, 0xfffd694c, 0xfffe9e3b, 0xff8a4f7d, 0xff7d9a3a, 0xff3f7fd0, 0xffe9789f, 0xffd9b23a };
    for (auto* k : { &freqK, &gainK, &qK }) k->setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (c[b % 8]));
    refresh();
}

void BandPanel::refresh()
{
    if (current < 0) return;
    const auto p = proc.bandParams (current);
    gainK.setEnabled (ileq::hasGain (p.shape));
    slopeBox.setEnabled (ileq::hasSlope (p.shape));
    for (auto* c : { (juce::Component*) &rangeK, (juce::Component*) &autoT, (juce::Component*) &extT }) c->setEnabled (p.dynamic);
    thrK.setEnabled (p.dynamic && ! p.autoThr);
    soloB.setColour (juce::TextButton::buttonColourId, proc.eq.soloBand.load() == current ? toota::orange : toota::cream);
    repaint();
}

void BandPanel::resized()
{
    shapeBox.setBounds (24, 34, 140, 30);
    slopeBox.setBounds (24, 74, 140, 30);
    placeBox.setBounds (24, 114, 140, 30);
    freqK.setBounds (196, 32, 70, 70);
    gainK.setBounds (286, 32, 70, 70);
    qK.setBounds (376, 32, 70, 70);
    dynT.setBounds (480, 28, 120, 26);
    rangeK.setBounds (480, 66, 62, 62);
    thrK.setBounds (562, 66, 62, 62);
    autoT.setBounds (640, 72, 90, 24);
    extT.setBounds (640, 104, 130, 24);
    onT.setBounds (800, 28, 110, 26);
    soloB.setBounds (800, 66, 90, 30);
    delB.setBounds (800, 104, 90, 30);
}

void BandPanel::paint (juce::Graphics& g)
{
    toota::panel (g, getLocalBounds().toFloat().reduced (1), 12);
    if (current < 0)
    {
        g.setColour (toota::inkSoft); g.setFont (fnt (14.0f, false));
        g.drawFittedText ("Click and drag on the display to make a band (on a spectrum peak it grabs the peak), double-click to add one.\n"
                          "Drag a band to move it, scroll for Q, shift-scroll for its dynamic range, right-click for everything else.\n"
                          "Shift-click or Cmd-drag selects several. Alt-click or Delete removes. Double-click a band to bypass it.",
                          getLocalBounds().reduced (30, 20), juce::Justification::centred, 4);
        return;
    }
    const auto p = proc.bandParams (current);
    g.setColour (toota::ink); g.setFont (fnt (12.0f));
    g.drawText ("BAND " + juce::String (current + 1), 24, 8, 140, 20, juce::Justification::centredLeft);
    auto lbl = [&] (const juce::String& t, juce::Rectangle<int> r) { g.setColour (toota::ink); g.setFont (fnt (10.0f)); g.drawText (t, r, juce::Justification::centred); };
    lbl ("FREQ  " + hzText (p.freq), { 166, 104, 130, 14 });
    lbl (ileq::hasGain (p.shape) ? "GAIN  " + juce::String (p.gainDb, 1) + " dB" : "GAIN", { 256, 104, 130, 14 });
    lbl ("Q  " + juce::String (p.q, 2), { 346, 104, 130, 14 });
    lbl ("RANGE  " + juce::String (p.range, 1), { 451, 130, 120, 14 });
    lbl (p.autoThr ? "THRESHOLD  AUTO" : "THRESHOLD  " + juce::String (p.thresholdDb, 0), { 533, 130, 120, 14 });
    // the dynamic range ring around the gain knob (like Pro-Q)
    if (p.dynamic)
    {
        auto c = gainK.getBounds().toFloat().getCentre();
        const float r = gainK.getWidth() * 0.5f + 5;
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        const float mid = (a0 + a1) * 0.5f, span = (a1 - a0) * 0.5f;
        const float g0 = mid + juce::jlimit (-1.0f, 1.0f, p.gainDb / 30.0f) * span;
        const float g1 = mid + juce::jlimit (-1.0f, 1.0f, (p.gainDb + p.range) / 30.0f) * span;
        juce::Path ring; ring.addCentredArc (c.x, c.y, r, r, 0, std::min (g0, g1), std::max (g0, g1), true);
        g.setColour (toota::coral); g.strokePath (ring, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float now = mid + juce::jlimit (-1.0f, 1.0f, (p.gainDb + proc.eq.dynNow[(size_t) current].load()) / 30.0f) * span;
        g.setColour (toota::ink); g.fillEllipse (c.x + r * std::sin (now) - 3, c.y - r * std::cos (now) - 3, 6, 6);
    }
}

//==============================================================================
InterlockEditor::InterlockEditor (InterlockProcessor& p) : AudioProcessorEditor (p), proc (p), display (p), bandPanel (p)
{
    look.pillToggles = true;
    setLookAndFeel (&look);
    addAndMakeVisible (content);
    content.addAndMakeVisible (display);
    content.addChildComponent (bandPanel);
    proc.editorOpen = true;

    display.onSelect = [this] (int b) { bandPanel.setBand (b); showPage (b >= 0 ? 1 : page); };
    bandPanel.onDelete = [this] { display.select (-1, false); };

    content.addAndMakeVisible (learnButton);
    learnButton.onClick = [this] { proc.startLearn(); };
    learnButton.setColour (juce::TextButton::buttonColourId, toota::orange);
    learnButton.setTooltip ("Play kick and bass together; Interlock listens for ~5 seconds and sets the kick dip, sub duck, bass focus and parallel compression");

    for (auto* t : { &monoButton, &deltaButton }) { t->setColour (juce::ToggleButton::tickColourId, toota::teal); content.addAndMakeVisible (t); }
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ilp::monoLows, monoButton));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ilp::delta, deltaButton));
    deltaButton.setTooltip ("Hear only what Interlock removes or adds");

    for (auto* b : { &pageInterlock, &pageBand }) { b->setClickingTogglesState (false); content.addAndMakeVisible (b); b->setColour (juce::TextButton::buttonOnColourId, toota::ink); }
    pageInterlock.onClick = [this] { showPage (0); };
    pageBand.onClick = [this] { showPage (1); };

    // toolbar
    phaseBox.addItem ("ZERO LATENCY", 1); phaseBox.addItem ("LINEAR PHASE", 2);
    phaseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ilp::eqPhase, phaseBox);
    phaseBox.setTooltip ("Zero latency: analogue-matched filters, no delay. Linear phase: no phase shift at all, adds about 85 ms of latency (Live compensates)");
    analyserBox.addItem ("ANALYSER OFF", 1); analyserBox.addItem ("PRE", 2); analyserBox.addItem ("POST", 3); analyserBox.addItem ("PRE + POST", 4);
    analyserBox.setSelectedId (proc.analyserMode + 1, juce::dontSendNotification);
    analyserBox.onChange = [this] { proc.analyserMode = analyserBox.getSelectedId() - 1; };
    rangeBox.addItem ("3 dB", 1); rangeBox.addItem ("6 dB", 2); rangeBox.addItem ("12 dB", 3); rangeBox.addItem ("30 dB", 4);
    rangeBox.setSelectedId (proc.rangeIndex + 1, juce::dontSendNotification);
    rangeBox.onChange = [this] { proc.rangeIndex = rangeBox.getSelectedId() - 1; display.repaint(); };
    otherBox.setTextWhenNothingSelected ("COLLISION: NONE");
    otherBox.setTooltip ("Show another Interlock in this session and where the two collide");
    otherBox.onChange = [this]
    {
        const int i = otherBox.getSelectedId() - 2;
        proc.otherInstance = i >= 0 && i < (int) others.size() ? others[(size_t) i].first : 0;
    };
    for (auto* c : { &phaseBox, &analyserBox, &rangeBox, &otherBox }) content.addAndMakeVisible (c);
    scToggle.setToggleState (proc.showSidechain, juce::dontSendNotification);
    scToggle.onClick = [this] { proc.showSidechain = scToggle.getToggleState(); };
    pianoToggle.setToggleState (proc.piano, juce::dontSendNotification);
    pianoToggle.onClick = [this] { proc.piano = pianoToggle.getToggleState(); display.repaint(); };
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, ilp::autoGain, autoGainToggle));
    autoGainToggle.setTooltip ("Keeps the level the same as you EQ, so louder doesn't fool you into thinking better");
    for (auto* t : { &scToggle, &pianoToggle, &autoGainToggle }) { t->setColour (juce::ToggleButton::tickColourId, toota::teal); content.addAndMakeVisible (t); }
    for (auto* k : { &gainScaleK, &panK })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        k->setPopupDisplayEnabled (true, true, &content, 1200);
        content.addAndMakeVisible (k);
    }
    panK.getProperties().set ("bipolar", true);
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, ilp::gainScale, gainScaleK));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, ilp::outPan, panK));
    gainScaleK.setDoubleClickReturnValue (true, 1.0); panK.setDoubleClickReturnValue (true, 0.0);
    gainScaleK.setTooltip ("Gain scale: scales every band's gain (and dynamic range) together");
    matchButton.setTooltip ("EQ Match: press, play this track with the reference on the sidechain for a few seconds, press again to apply");
    matchButton.onClick = [this]
    {
        if (! proc.spectrum.capturing) { proc.startMatch(); return; }
        const int n = proc.applyMatch (8, 1.0f);
        status = n > 0 ? "EQ Match made " + juce::String (n) + " bands" : "EQ Match needs a few seconds of both signals";
    };
    aButton.onClick = [this] { proc.abSelect (0); display.select (-1, false); };
    bButton.onClick = [this] { proc.abSelect (1); display.select (-1, false); };
    copyButton.onClick = [this] { proc.abCopyToOther(); status = juce::String ("Copied ") + (proc.abSlot == 0 ? "A to B" : "B to A"); };
    for (auto* b : { &aButton, &bButton }) b->setColour (juce::TextButton::buttonOnColourId, toota::teal);
    for (auto* b : { &matchButton, &aButton, &bButton, &copyButton }) content.addAndMakeVisible (b);

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
    setResizeLimits (W * 2 / 3, H * 2 / 3, W * 3 / 2, H * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) W / H);
    setSize (W, H);
    showPage (0);
    startTimerHz (30);
}

InterlockEditor::~InterlockEditor()
{
    proc.editorOpen = false;
    setLookAndFeel (nullptr);
}

void InterlockEditor::showPage (int p)
{
    page = p;
    const bool band = p == 1;
    bandPanel.setVisible (band);
    for (auto* s : sliders) s->setVisible (! band);
    pageInterlock.setToggleState (! band, juce::dontSendNotification);
    pageBand.setToggleState (band, juce::dontSendNotification);
    repaint();
}

void InterlockEditor::knob (const char* id, const juce::String& label, juce::Colour accent)
{
    auto* s = sliders.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
    s->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s->setColour (juce::Slider::rotarySliderFillColourId, accent);
    s->setPopupDisplayEnabled (true, true, &content, 1200);
    s->setMouseDragSensitivity (220);
    content.addAndMakeVisible (s);
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
    if (proc.spectrum.capturing) status = "EQ Match: listening (" + juce::String (proc.spectrum.captureFrames / 30) + " s). Press MATCH again to apply.";
    else if (st == 1) status = "Listening to kick and bass together...";
    else if (status.isEmpty() || status.startsWith ("Kick ") || status.startsWith ("Play kick"))
        status = proc.hasLearned.load() ? "Kick " + juce::String (juce::roundToInt (proc.learnedKickHz.load())) + " Hz   |   Bass focus "
                                          + juce::String (juce::roundToInt (proc.learnedBoostHz.load())) + " Hz   |   Overlap " + juce::String (juce::roundToInt (proc.learnedOverlap.load() * 100)) + "%"
                                        : "Play kick and bass together to auto-learn, or press LEARN";
    matchButton.setButtonText (proc.spectrum.capturing ? "APPLY MATCH" : "MATCH");
    matchButton.setColour (juce::TextButton::buttonColourId, proc.spectrum.capturing ? toota::coral : toota::cream);
    aButton.setToggleState (proc.abSlot == 0, juce::dontSendNotification);
    bButton.setToggleState (proc.abSlot == 1, juce::dontSendNotification);
    copyButton.setButtonText (proc.abSlot == 0 ? "A>B" : "B>A");

    // other Interlocks in the session
    auto now = ileq::Registry::get().others (proc.registryId);
    if (now != others)
    {
        others = now;
        otherBox.clear (juce::dontSendNotification);
        otherBox.addItem ("COLLISION: NONE", 1);
        int sel = 1;
        for (size_t i = 0; i < others.size(); ++i) { otherBox.addItem ("VS " + others[i].second.toUpperCase(), (int) i + 2); if (others[i].first == proc.otherInstance) sel = (int) i + 2; }
        otherBox.setSelectedId (sel, juce::dontSendNotification);
    }

    dipMeter = juce::jmax (proc.dipNowDb.load() + proc.duckNowDb.load() * 0.5f, dipMeter - 1.5f);
    dipMeter = juce::jmin (dipMeter, proc.dipNowDb.load());
    compMeter = proc.compGrDb.load();
    learnButton.repaint();
    display.repaint();
    if (page == 1) bandPanel.refresh();
    repaint();
}

void InterlockEditor::resized()
{
    content.setBounds (0, 0, W, H);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) W));
    learnButton.setBounds (330, 12, 150, 34);
    monoButton.setBounds (900, 14, 92, 28);
    deltaButton.setBounds (1000, 14, 84, 28);
    display.setBounds (12, 62, 1030, 404);
    // toolbar
    int x = 12; const int ty = 474, th = 30;
    auto place = [&] (juce::Component& c, int w) { c.setBounds (x, ty, w, th); x += w + 8; };
    place (phaseBox, 132);
    place (analyserBox, 112);
    place (scToggle, 52);
    place (otherBox, 140);
    place (rangeBox, 70);
    place (pianoToggle, 64);
    place (autoGainToggle, 92);
    gainScaleK.setBounds (726, ty - 4, 36, 36);
    panK.setBounds (804, ty - 4, 36, 36);
    matchButton.setBounds (872, ty, 82, th);
    aButton.setBounds (960, ty, 38, th);
    bButton.setBounds (1004, ty, 38, th);
    copyButton.setBounds (1048, ty, 40, th);
    // bottom
    pageInterlock.setBounds (12, 524, 112, 30);
    pageBand.setBounds (12, 562, 112, 30);
    bandPanel.setBounds (136, 514, W - 148, 194);
    int gx = 140, idx = 0;
    const int y = 524, kw = 78;
    for (auto& gr : groups)
    {
        for (int i = 0; i < gr.count; ++i, ++idx)
            sliders[idx]->setBounds (gx + 10 + i * kw, y + 14, 58, 58);
        gx += gr.count * kw + 22;
    }
}

void InterlockEditor::paint (juce::Graphics& g)
{
    g.fillAll (ic::bg);
    const float s = (float) getWidth() / (float) W;
    g.addTransform (juce::AffineTransform::scale (s));

    toota::header (g, { 0, 0, W, 56 }, "INTERLOCK", "kick + bass balancer, dynamic eq");
    g.setFont (fnt (11.0f, false));
    g.setColour (ic::text);
    g.drawText (status, 494, 10, 400, 16, juce::Justification::centredLeft, true);
    const juce::Colour led = scOk ? toota::teal : ic::kick;
    g.setColour (led);
    g.fillEllipse (494, 32, 8, 8);
    g.setFont (fnt (10.0f, true, 0.06f));
    g.drawText (scOk ? "SIDECHAIN: KICK DETECTED" : "SIDECHAIN: ROUTE YOUR KICK HERE", 508, 29, 300, 14, juce::Justification::centredLeft);

    // meters
    auto meter = [&] (int mx, const juce::String& name, float db, juce::Colour col)
    {
        const juce::Rectangle<float> r ((float) mx, 66, 18, 380);
        toota::screen (g, r, 5);
        const float frac = juce::jlimit (0.0f, 1.0f, -db / 18.0f);
        g.setColour (col);
        g.fillRoundedRectangle (r.reduced (2).withHeight ((r.getHeight() - 4) * frac), 4);
        g.setColour (ic::dim);
        g.setFont (fnt (8.5f, true, 0.05f));
        g.drawText (name, mx - 8, 450, 34, 12, juce::Justification::centred);
    };
    meter (1052, "DIP", dipMeter, ic::kick);
    meter (1076, "COMP", compMeter, ic::comp);

    // toolbar labels
    g.setColour (ic::dim); g.setFont (fnt (8.5f, true));
    g.drawText ("SCALE", gainScaleK.getRight() + 2, 482, 38, 14, juce::Justification::centredLeft);
    g.drawText ("PAN", panK.getRight() + 2, 482, 28, 14, juce::Justification::centredLeft);
    if (proc.eq.autoGain && std::abs (proc.autoGainNowDb()) > 0.05f)
        g.drawText (juce::String (proc.autoGainNowDb(), 1) + " dB", autoGainToggle.getX() + 20, 503, 80, 12, juce::Justification::centredLeft);

    if (page == 0)
    {
        int x = 140, idx = 0;
        const int y = 516;
        for (auto& gr : groups)
        {
            const int w = gr.count * 78;
            const juce::Rectangle<float> box ((float) x, (float) y, (float) w, 192);
            toota::panel (g, box, 12);
            g.setColour (gr.accent);
            g.fillRoundedRectangle (box.withHeight (5).reduced (14, 0).translated (0, 2), 2.5f);
            g.setColour (gr.accent == ic::curve ? toota::ink : gr.accent.darker (0.1f));
            g.setFont (fnt (10.5f, true, 0.18f));
            g.drawText (gr.title, box.withTop (box.getBottom() - 30).toNearestInt(), juce::Justification::centred);
            for (int i = 0; i < gr.count; ++i, ++idx)
            {
                g.setColour (ic::text);
                g.setFont (fnt (9.5f, true, 0.1f));
                g.drawText (labels[idx], x + i * 78, y + 92, 78, 14, juce::Justification::centred);
            }
            x += w + 22;
        }
    }
}
