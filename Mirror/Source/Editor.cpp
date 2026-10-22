#include "Editor.h"

namespace mc
{
    const juce::Colour bg    { 0xff0e1014 };
    const juce::Colour panel { 0xff161a20 };
    const juce::Colour inset { 0xff0a0c0f };
    const juce::Colour line  { 0xff252a32 };
    const juce::Colour text  { 0xffe9edf2 };
    const juce::Colour dim   { 0xff8790a0 };
    const juce::Colour ref   { 0xff5ab4ff };
    const juce::Colour mix   { 0xffffc04d };
    const juce::Colour good  { 0xff6fe0a0 };
    const juce::Colour warn  { 0xffff7a59 };
}

static juce::Font mf (float s, bool bold = true, float kern = 0) { return juce::Font (juce::FontOptions (s).withStyle (bold ? "Bold" : "Regular")).withExtraKerningFactor (kern); }

MirrorLook::MirrorLook()
{
    setColour (juce::TextButton::textColourOffId, mc::text);
    setColour (juce::TextButton::textColourOnId, mc::bg);
    setColour (juce::ComboBox::textColourId, mc::text);
    setColour (juce::PopupMenu::backgroundColourId, mc::panel);
    setColour (juce::PopupMenu::textColourId, mc::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, mc::mix);
    setColour (juce::PopupMenu::highlightedTextColourId, mc::bg);
}

void MirrorLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);
    const auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2);
    const float r = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float ang = a0 + pos * (a1 - a0);
    const bool bip = (bool) s.getProperties().getWithDefault ("bipolar", false);
    juce::Path tr; tr.addCentredArc (c.x, c.y, r - 2, r - 2, 0, a0, a1, true);
    g.setColour (mc::line); g.strokePath (tr, juce::PathStrokeType (3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = bip ? (a0 + a1) * 0.5f : a0;
    if (std::abs (ang - from) > 0.01f)
    {
        juce::Path arc; arc.addCentredArc (c.x, c.y, r - 2, r - 2, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
        g.setColour (accent); g.strokePath (arc, juce::PathStrokeType (3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    const float body = r - 7;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b3039), c.x, c.y - body, juce::Colour (0xff111318), c.x, c.y + body, false));
    g.fillEllipse (c.x - body, c.y - body, body * 2, body * 2);
    juce::Path p; p.addRoundedRectangle (-1.4f, -body + 3, 2.8f, body * 0.48f, 1.4f);
    g.setColour (mc::text); g.fillPath (p, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void MirrorLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1);
    const auto acc = b.findColour (juce::TextButton::buttonOnColourId);
    const float prog = (float) b.getProperties().getWithDefault ("progress", 0.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? acc : acc.withAlpha (down ? 0.35f : hi ? 0.22f : 0.12f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    if (prog > 0 && prog < 1)
    {
        g.saveState(); g.reduceClipRegion (r.withWidth (r.getWidth() * prog).toNearestInt());
        g.setColour (acc.withAlpha (0.5f)); g.fillRoundedRectangle (r, r.getHeight() * 0.5f); g.restoreState();
    }
    g.setColour (acc); g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.2f);
}

juce::Font MirrorLook::getTextButtonFont (juce::TextButton&, int h) { return mf (juce::jmin (12.5f, h * 0.45f), true, 0.1f); }

void MirrorLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();
    auto box = r.withWidth (r.getHeight()).reduced (4);
    g.setColour (on ? mc::good : mc::inset); g.fillRoundedRectangle (box, 3);
    g.setColour (on ? mc::good : mc::dim); g.drawRoundedRectangle (box, 3, 1);
    g.setColour (on ? mc::text : mc::dim); g.setFont (mf (11, true));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (r.getHeight() + 2), juce::Justification::centredLeft);
}

void MirrorLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (mc::inset); g.fillRoundedRectangle (r, 4);
    g.setColour (mc::line.brighter (0.3f)); g.drawRoundedRectangle (r, 4, 1);
    juce::Path a; const float ax = (float) w - 10, ay = (float) h * 0.5f;
    a.addTriangle (ax - 3.5f, ay - 2, ax + 3.5f, ay - 2, ax, ay + 2.5f);
    g.setColour (mc::dim); g.fillPath (a);
}

juce::Font MirrorLook::getComboBoxFont (juce::ComboBox&) { return mf (11.5f, true); }

//==============================================================================
static float apvts_transpose (MirrorProcessor& p) { return std::round (p.apvts.getRawParameterValue (mp::transpose)->load()); }

static void setupKnob (juce::Slider& s, juce::Colour c, juce::Component& parent, bool bip = false)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    s.setColour (juce::Slider::rotarySliderFillColourId, c);
    s.setPopupDisplayEnabled (true, true, &parent, 1500);
    s.getProperties().set ("bipolar", bip);
    parent.addAndMakeVisible (s);
}

MirrorEditor::MirrorEditor (MirrorProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    for (auto* b : { &masterTab, &stemTab }) { addAndMakeVisible (b); b->setColour (juce::TextButton::buttonOnColourId, mc::mix); }
    masterTab.onClick = [this] { if (auto* prm = proc.apvts.getParameter (mp::mode)) prm->setValueNotifyingHost (0.0f); layoutForMode(); };
    stemTab.onClick = [this] { if (auto* prm = proc.apvts.getParameter (mp::mode)) prm->setValueNotifyingHost (1.0f); layoutForMode(); };

    loadButton.setColour (juce::TextButton::buttonOnColourId, mc::ref);
    listenButton.setColour (juce::TextButton::buttonOnColourId, mc::mix);
    undoButton.setColour (juce::TextButton::buttonOnColourId, mc::dim);
    keyButton.setColour (juce::TextButton::buttonOnColourId, mc::warn);
    loadButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a reference track", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) proc.loadReference (fc.getResult()); });
    };
    listenButton.onClick = [this] { proc.startListen(); };
    undoButton.onClick = [this] { if (proc.bridgeConnected() && proc.useBridge.load()) proc.restoreFaders(); else proc.undoMatch(); };
    keyButton.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Transpose the clips in Live (best quality, needs the Bridge)", proc.bridgeConnected());
        m.addItem (2, "Transpose inside Mirror (works without the Bridge)");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyButton), [this] (int r) { if (r > 0) proc.fixKeys (r == 2); });
    };
    bridgeToggle.setToggleState (proc.useBridge.load(), juce::dontSendNotification);
    bridgeToggle.onClick = [this] { proc.useBridge = bridgeToggle.getToggleState(); };

    listenBox.addItemList ({ "8 bars", "16 bars", "32 bars" }, 1);
    keySourceBox.addItemList ({ "Key from project", "Key from reference" }, 1);
    roleBox.addItemList (mr::roleNames(), 1);
    cAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, mp::listenBars, listenBox));
    cAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, mp::keySource, keySourceBox));
    cAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.apvts, mp::role, roleBox));
    bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.apvts, mp::lock, lockToggle));

    setupKnob (toneKnob, mc::ref, *this);
    setupKnob (headroomKnob, mc::good, *this);
    setupKnob (trimKnob, mc::text, *this, true);
    setupKnob (gainKnob, mc::mix, *this, true);
    setupKnob (transposeKnob, mc::warn, *this, true);
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, mp::toneMatch, toneKnob));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, mp::headroom, headroomKnob));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, mp::trim, trimKnob));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, mp::gain, gainKnob));
    sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (proc.apvts, mp::transpose, transposeKnob));

    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadButton, &listenButton, &undoButton, &keyButton, &bridgeToggle, &lockToggle, &listenBox, &keySourceBox, &roleBox })
        addAndMakeVisible (c);

    masterMode = proc.isMaster();
    setSize (masterMode ? 1000 : 460, masterMode ? 640 : 300);
    layoutForMode();
    startTimerHz (15);
}

MirrorEditor::~MirrorEditor() { setLookAndFeel (nullptr); }

void MirrorEditor::layoutForMode()
{
    masterMode = proc.isMaster();
    masterTab.setToggleState (masterMode, juce::dontSendNotification);
    stemTab.setToggleState (! masterMode, juce::dontSendNotification);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &loadButton, &listenButton, &undoButton, &keyButton, &bridgeToggle, &listenBox, &keySourceBox, &toneKnob, &headroomKnob, &trimKnob })
        c->setVisible (masterMode);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &roleBox, &lockToggle, &gainKnob, &transposeKnob })
        c->setVisible (! masterMode);
    const int w = masterMode ? 1000 : 460, h = masterMode ? 640 : 300;
    if (getWidth() != w || getHeight() != h) setSize (w, h); else resized();
    repaint();
}

void MirrorEditor::resized()
{
    masterTab.setBounds (getWidth() - 186, 14, 84, 26);
    stemTab.setBounds (getWidth() - 96, 14, 80, 26);
    if (masterMode)
    {
        loadButton.setBounds (16, 66, 160, 30);
        listenButton.setBounds (16, 104, 160, 36);
        listenBox.setBounds (184, 110, 92, 24);
        undoButton.setBounds (16, 148, 76, 26);
        keyButton.setBounds (100, 148, 76, 26);
        keySourceBox.setBounds (184, 148, 150, 24);
        bridgeToggle.setBounds (16, 184, 260, 22);
        toneKnob.setBounds (40, 540, 56, 56);
        headroomKnob.setBounds (130, 540, 56, 56);
        trimKnob.setBounds (220, 540, 56, 56);
        tableArea = { 350, 300, 634, 324 };
    }
    else
    {
        roleBox.setBounds (16, 92, 120, 26);
        lockToggle.setBounds (16, 128, 120, 24);
        gainKnob.setBounds (190, 88, 90, 90);
        transposeKnob.setBounds (330, 100, 70, 70);
    }
}

bool MirrorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (! masterMode) return false;
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".wav") || f.endsWithIgnoreCase (".aif") || f.endsWithIgnoreCase (".aiff")
            || f.endsWithIgnoreCase (".flac") || f.endsWithIgnoreCase (".mp3") || f.endsWithIgnoreCase (".m4a")) return true;
    return false;
}

void MirrorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (! files.isEmpty()) proc.loadReference (juce::File (files[0]));
}

void MirrorEditor::mouseUp (const juce::MouseEvent& e)
{
    // clicking a stem's key flag in the master table transposes just that one (in Live if the Bridge is on)
    juce::ignoreUnused (e);
}

void MirrorEditor::timerCallback()
{
    if (masterMode != proc.isMaster()) layoutForMode();
    listenButton.getProperties().set ("progress", proc.listeningNow.load() ? proc.listenProgress.load() : 0.0f);
    listenButton.setButtonText (proc.listeningNow.load() ? "LISTENING " + juce::String (juce::roundToInt (proc.listenProgress.load() * 100)) + "%" : "LISTEN & MATCH");
    if (masterMode) rows = proc.stems();
    repaint();
}

void MirrorEditor::paint (juce::Graphics& g)
{
    g.fillAll (mc::bg);
    g.setColour (mc::text); g.setFont (mf (24, true, 0.3f));
    g.drawText ("MIRROR", 16, 10, 200, 30, juce::Justification::centredLeft);
    g.setColour (mc::dim); g.setFont (mf (9.5f, true, 0.2f));
    g.drawText (masterMode ? "REFERENCE MIX MATCH   |   TOOTA AUDIO" : "STEM   |   TOOTA AUDIO", 17, 40, 320, 12, juce::Justification::centredLeft);
    if (masterMode) paintMaster (g); else paintStem (g);
}

void MirrorEditor::paintStem (juce::Graphics& g)
{
    auto& reg = mr::Registry::get();
    const int s = proc.slotIndex();
    const bool linked = reg.masters.load() >= 0 && s >= 0;
    g.setColour (mc::text); g.setFont (mf (15, true));
    g.drawText (proc.trackName.isNotEmpty() ? proc.trackName : (s >= 0 ? reg.nameOf (s) : juce::String ("Stem")), 16, 62, 260, 20, juce::Justification::centredLeft);
    g.setColour (mc::dim); g.setFont (mf (9, true, 0.1f));
    g.drawText ("ROLE", 16, 80, 100, 12, juce::Justification::centredLeft);
    g.drawText ("MIRROR GAIN", 180, 182, 110, 12, juce::Justification::centred);
    g.drawText ("TRANSPOSE", 310, 182, 110, 12, juce::Justification::centred);
    g.setColour (mc::warn); g.setFont (mf (14, true));
    const float tp = apvts_transpose (proc);
    g.drawText ((tp > 0 ? "+" : "") + juce::String ((int) tp) + " st", 310, 196, 110, 24, juce::Justification::centred);
    g.setColour (mc::mix); g.setFont (mf (20, true));
    const float gd = proc.apvts.getRawParameterValue (mp::gain)->load();
    g.drawText ((gd >= 0 ? "+" : "") + juce::String (gd, 1) + " dB", 170, 196, 130, 24, juce::Justification::centred);

    juce::String key = "Key: waiting for a LISTEN pass";
    juce::Colour kc = mc::dim;
    if (s >= 0)
    {
        const int ks = reg.slots[s].keyState.load();
        if (ks == 1) { key = "In key"; kc = mc::good; }
        else if (ks == 2) { const int sh = reg.slots[s].keyShift.load(); key = "Out of key: transpose " + juce::String (sh > 0 ? "+" : "") + juce::String (sh) + " st"; kc = mc::warn; }
        else if (ks == 3) { key = "Not checked for key (drums / FX)"; }
    }
    g.setColour (kc); g.setFont (mf (12, true));
    g.drawText (key, 16, 236, 420, 18, juce::Justification::centredLeft);
    g.setColour (mc::dim); g.setFont (mf (10, false));
    g.drawText (linked ? "Put Mirror in MASTER mode on your Main track to run the match." : "Registry full", 16, 262, 430, 14, juce::Justification::centredLeft);
}

void MirrorEditor::paintMaster (juce::Graphics& g)
{
    const auto ref = proc.reference();
    const bool bridge = proc.bridgeConnected();
    g.setColour (bridge ? mc::good : mc::dim); g.fillEllipse (420, 26, 8, 8);
    g.setFont (mf (10, true, 0.06f));
    g.drawText (bridge ? "LIVE BRIDGE CONNECTED: FADERS WILL MOVE" : "NO LIVE BRIDGE: MIRROR GAIN ON EACH STEM IS USED", 434, 22, 420, 16, juce::Justification::centredLeft);

    // reference panel
    g.setColour (mc::panel); g.fillRoundedRectangle (juce::Rectangle<float> (350, 62, 634, 222), 8);
    g.setColour (mc::dim); g.setFont (mf (9, true, 0.15f));
    g.drawText ("REFERENCE", 366, 70, 200, 12, juce::Justification::centredLeft);
    g.setColour (ref.valid ? mc::ref : mc::dim); g.setFont (mf (15, true));
    g.drawText (proc.refLoading.load() ? "Analysing... " + juce::String (juce::roundToInt (proc.refProgress.load() * 100)) + "%"
                                       : (ref.valid ? ref.name : juce::String ("Drop a WAV / AIFF / MP3 here or press LOAD REFERENCE")),
                366, 84, 420, 20, juce::Justification::centredLeft, true);
    if (ref.valid)
    {
        g.setColour (mc::text); g.setFont (mf (11, false));
        g.drawText ("Key " + juce::String (cl::noteName (ref.key.tonic)) + (ref.key.minor ? " minor" : " major")
                    + "   |   peak " + juce::String (ref.peakDb, 1) + " dBFS   |   " + juce::String (ref.lengthSec / 60.0, 1) + " min",
                    366, 104, 420, 16, juce::Justification::centredLeft);
    }
    const auto pk = proc.projectKey();
    g.setColour (mc::dim); g.setFont (mf (9, true, 0.15f));
    g.drawText ("PROJECT KEY", 800, 70, 170, 12, juce::Justification::centredRight);
    g.setColour (mc::mix); g.setFont (mf (18, true));
    g.drawText (pk.tonic < 0 ? juce::String ("--") : juce::String (cl::noteName (pk.tonic)) + (pk.minor ? " minor" : " major"), 760, 82, 210, 24, juce::Justification::centredRight);

    // spectrum comparison
    const auto sol = proc.lastSolve();
    auto plot = juce::Rectangle<float> (366, 126, 602, 146);
    g.setColour (mc::inset); g.fillRoundedRectangle (plot, 6);
    auto curve = [&] (const mr::Bands& b, juce::Colour c, float width, bool fill)
    {
        float mean = 0, cnt = 0;
        for (int k = 0; k < mr::kBands; ++k) if (b[(size_t) k] > 0) { mean += mr::toDb (b[(size_t) k]); ++cnt; }
        if (cnt == 0) return;
        mean /= cnt;
        juce::Path p;
        for (int k = 0; k < mr::kBands; ++k)
        {
            const float x = plot.getX() + 10 + (plot.getWidth() - 20) * k / (float) (mr::kBands - 1);
            const float d = mr::toDb (b[(size_t) k]) - mean;
            const float y = juce::jlimit (plot.getY() + 4, plot.getBottom() - 14, plot.getCentreY() - d * 2.2f);
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (fill) { juce::Path f (p); f.lineTo (plot.getRight() - 10, plot.getBottom() - 14); f.lineTo (plot.getX() + 10, plot.getBottom() - 14); f.closeSubPath(); g.setColour (c.withAlpha (0.12f)); g.fillPath (f); }
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };
    if (ref.valid) curve (ref.bands, mc::ref, 2.2f, true);
    if (! sol.gains.empty()) { curve (sol.before, mc::dim, 1.4f, false); curve (sol.after, mc::mix, 2.0f, false); }
    g.setFont (mf (8.5f, true));
    for (int k = 0; k < mr::kBands; k += 4)
    {
        const float x = plot.getX() + 10 + (plot.getWidth() - 20) * k / (float) (mr::kBands - 1);
        const float f = mr::bandCentre (k);
        g.setColour (mc::dim);
        g.drawText (f >= 1000 ? juce::String (f / 1000, 1) + "k" : juce::String ((int) f), (int) x - 20, (int) plot.getBottom() - 13, 40, 11, juce::Justification::centred);
    }
    g.setColour (mc::ref); g.drawText ("REFERENCE", (int) plot.getX() + 10, (int) plot.getY() + 4, 80, 11, juce::Justification::centredLeft);
    g.setColour (mc::dim); g.drawText ("YOUR MIX", (int) plot.getX() + 90, (int) plot.getY() + 4, 70, 11, juce::Justification::centredLeft);
    g.setColour (mc::mix); g.drawText ("MATCHED", (int) plot.getX() + 160, (int) plot.getY() + 4, 70, 11, juce::Justification::centredLeft);

    // left column labels
    g.setColour (mc::dim); g.setFont (mf (9, true, 0.1f));
    g.drawText ("TONE MATCH", 18, 598, 100, 12, juce::Justification::centred);
    g.drawText ("HEADROOM", 108, 598, 100, 12, juce::Justification::centred);
    g.drawText ("MASTER TRIM", 198, 598, 100, 12, juce::Justification::centred);
    g.setColour (mc::panel); g.fillRoundedRectangle (juce::Rectangle<float> (16, 216, 318, 300), 8);
    g.setColour (mc::text); g.setFont (mf (11, false));
    juce::AttributedString as;
    as.setText (proc.statusText());
    as.setColour (mc::text);
    as.setFont (mf (11.5f, false));
    as.draw (g, juce::Rectangle<float> (28, 228, 296, 120));
    g.setColour (mc::dim); g.setFont (mf (10, false));
    const char* steps[] { "1  Mirror in STEM mode on every track", "2  Mirror in MASTER mode on Main", "3  Load your reference track",
                          "4  Play your loudest section", "5  LISTEN & MATCH, then check by ear", "6  FIX KEYS for any flagged stems" };
    for (int i = 0; i < 6; ++i) g.drawText (steps[i], 28, 380 + i * 20, 300, 16, juce::Justification::centredLeft);
    g.setColour (mc::dim); g.drawText ("Mix peak " + juce::String (proc.mixPeakDb.load(), 1) + " dBFS", 28, 500, 200, 12, juce::Justification::centredLeft);

    // stem table
    auto t = tableArea.toFloat();
    g.setColour (mc::panel); g.fillRoundedRectangle (t, 8);
    const int cols[] { 14, 168, 246, 336, 420, 496 };
    const char* heads[] { "STEM", "ROLE", "LEVEL", "LIVE FADER", "CHANGE", "KEY" };
    g.setColour (mc::dim); g.setFont (mf (9, true, 0.12f));
    for (int i = 0; i < 6; ++i) g.drawText (heads[i], (int) t.getX() + cols[i], (int) t.getY() + 8, 100, 12, juce::Justification::centredLeft);
    if (rows.empty())
    {
        g.setColour (mc::dim); g.setFont (mf (11, false));
        g.drawText ("No stems yet. Add Mirror to each track and set it to STEM.", t.reduced (14).withTrimmedTop (30), juce::Justification::topLeft);
    }
    const int rowH = 22;
    const int maxRows = (int) ((t.getHeight() - 30) / rowH);
    for (int i = 0; i < (int) rows.size() && i < maxRows; ++i)
    {
        const auto& r = rows[(size_t) i];
        const float y = t.getY() + 26 + i * rowH;
        if (i % 2) { g.setColour (juce::Colours::white.withAlpha (0.025f)); g.fillRect (t.getX() + 4, y, t.getWidth() - 8, (float) rowH); }
        g.setColour (mc::text); g.setFont (mf (11, true));
        g.drawText (r.name, (int) t.getX() + cols[0], (int) y, 150, rowH, juce::Justification::centredLeft, true);
        g.setColour (mc::dim); g.setFont (mf (10.5f, false));
        g.drawText (mr::roleNames()[r.role] + (r.locked ? "  (locked)" : ""), (int) t.getX() + cols[1], (int) y, 90, rowH, juce::Justification::centredLeft);
        const float lv = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (r.level, -60.0f) + 60.0f) / 60.0f);
        g.setColour (mc::inset); g.fillRoundedRectangle (t.getX() + cols[2], y + 8, 70, 6, 2);
        g.setColour (mc::good); g.fillRoundedRectangle (t.getX() + cols[2], y + 8, 70 * lv, 6, 2);
        g.setColour (mc::text);
        g.drawText (r.faderKnown ? juce::String (r.faderDb, 1) + " dB" : juce::String ("-"), (int) t.getX() + cols[3], (int) y, 80, rowH, juce::Justification::centredLeft);
        const float change = r.changeDb;
        g.setColour (std::abs (change) < 0.1f ? mc::dim : mc::mix);
        g.drawText (sol.gains.empty() ? juce::String ("-") : ((change >= 0 ? "+" : "") + juce::String (change, 1) + " dB"), (int) t.getX() + cols[4], (int) y, 70, rowH, juce::Justification::centredLeft);
        juce::String ks = "-"; juce::Colour kc = mc::dim;
        if (r.keyState == 1) { ks = "In key"; kc = mc::good; }
        else if (r.keyState == 2) { ks = "Transpose " + juce::String (r.keyShift > 0 ? "+" : "") + juce::String (r.keyShift) + " st"; kc = mc::warn; }
        else if (r.keyState == 3) ks = "n/a";
        if (std::abs (r.transpose) > 0.5f) ks += "  (Mirror " + juce::String ((int) r.transpose) + ")";
        g.setColour (kc); g.drawText (ks, (int) t.getX() + cols[5], (int) y, 134, rowH, juce::Justification::centredLeft);
    }
}
