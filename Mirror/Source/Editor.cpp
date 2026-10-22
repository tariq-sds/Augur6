#include "Editor.h"

namespace mc
{
    const juce::Colour bg    = toota::cream;
    const juce::Colour panel = toota::cream2;
    const juce::Colour inset = toota::cream3;
    const juce::Colour line  = toota::ink.withAlpha (0.18f);
    const juce::Colour text  = toota::ink;
    const juce::Colour dim   = toota::inkSoft;
    const juce::Colour ref   = toota::teal;
    const juce::Colour mix   = toota::orange;
    const juce::Colour good  = toota::olive;
    const juce::Colour warn  = toota::coral;
}

static juce::Font mf (float s, bool bold = true, float kern = 0) { return toota::font (s * 1.1f, bold ? 2 : 1, kern * 0.4f); }

void MirrorLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool hi, bool down)
{
    toota::Look::drawButtonBackground (g, b, c, hi, down);
    const float prog = (float) b.getProperties().getWithDefault ("progress", 0.0f);
    if (prog > 0 && prog < 1)
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f).withTrimmedRight (2.5f).withTrimmedBottom (2.5f);
        g.saveState(); g.reduceClipRegion (r.withWidth (r.getWidth() * prog).toNearestInt());
        g.setColour (toota::orange.withAlpha (0.6f)); g.fillRoundedRectangle (r.reduced (2), r.getHeight() * 0.5f); g.restoreState();
    }
}

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
    for (auto* b : { &masterTab, &stemTab }) { addAndMakeVisible (b); b->setColour (juce::TextButton::buttonOnColourId, toota::teal); }
    masterTab.onClick = [this] { if (auto* prm = proc.apvts.getParameter (mp::mode)) prm->setValueNotifyingHost (0.0f); layoutForMode(); };
    stemTab.onClick = [this] { if (auto* prm = proc.apvts.getParameter (mp::mode)) prm->setValueNotifyingHost (1.0f); layoutForMode(); };

    loadButton.setColour (juce::TextButton::buttonColourId, toota::cream);
    listenButton.setColour (juce::TextButton::buttonColourId, toota::coral);
    listenButton.setColour (juce::TextButton::textColourOffId, toota::cream);
    keyButton.setColour (juce::TextButton::buttonColourId, toota::orange);
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
    toota::header (g, { 0, 0, getWidth(), 56 }, "MIRROR", masterMode ? "reference mix match" : "stem");
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
    toota::panel (g, juce::Rectangle<float> (350, 62, 634, 222), 12);
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
    toota::screen (g, plot, 8);
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
    toota::panel (g, juce::Rectangle<float> (16, 216, 318, 300), 12);
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
    toota::panel (g, t, 12);
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
