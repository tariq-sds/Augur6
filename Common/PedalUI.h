#pragma once
// Shared pedal-style editor for Relay and Vault: machine selector, a screen, a row of big knobs whose
// labels follow the selected machine, and a row of secondary controls. TOOTA brand throughout.
#include <juce_audio_processors/juce_audio_processors.h>
#include "TootaTheme.h"

namespace toota
{
struct PedalHost
{
    virtual ~PedalHost() = default;
    virtual juce::AudioProcessorValueTreeState& state() = 0;
    virtual int numPresets() const = 0;
    virtual juce::String presetName (int) const = 0;
    virtual void loadPreset (int) = 0;
    virtual int presetIndex() const = 0;
    virtual bool presetEdited() const = 0;
    virtual float outputLevel() const = 0;
};

struct PedalSpec
{
    juce::String name, tagline;
    juce::Colour accent = teal;
    const char* machineId = "machine";
    juce::StringArray machines;
    std::function<juce::String (int)> info;
    std::function<std::pair<juce::String, juce::String> (int)> paramNames;   // labels for "p1" / "p2"
    std::function<juce::String()> readout;                                  // big value on the screen
    std::function<juce::String()> readoutSub;
    struct Knob { const char* id; juce::String label; bool bipolar = false; };
    std::vector<Knob> main, small;
    struct Toggle { const char* id; juce::String label; };
    std::vector<Toggle> toggles;
    struct Combo { const char* id; juce::String label; };
    std::vector<Combo> combos;
    std::function<juce::Component*()> extra;   // optional: e.g. a TAP button for the bottom row (the panel owns it)
};

class PedalPanel : public juce::Component, private juce::Timer
{
public:
    static constexpr int W = 1040, H = 560;

    PedalPanel (juce::AudioProcessor& p, PedalHost& h, PedalSpec s) : host (h), spec (std::move (s))
    {
        juce::ignoreUnused (p);
        auto& st = host.state();
        // machines
        for (int i = 0; i < spec.machines.size(); ++i)
        {
            auto* b = machineButtons.add (new juce::TextButton (spec.machines[i]));
            b->setClickingTogglesState (false);
            b->onClick = [this, i]
            {
                if (auto* prm = host.state().getParameter (spec.machineId))
                {
                    prm->beginChangeGesture();
                    prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                    prm->endChangeGesture();
                }
            };
            b->setColour (juce::TextButton::buttonOnColourId, spec.accent);
            addAndMakeVisible (b);
        }
        auto makeKnob = [this, &st] (const PedalSpec::Knob& k, bool big)
        {
            auto* sl = knobs.add (new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox));
            sl->setRotaryParameters (juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
            sl->setPopupDisplayEnabled (true, true, this, 900);
            sl->setMouseDragSensitivity (big ? 260 : 220);
            sl->getProperties().set ("bipolar", k.bipolar);
            sl->setColour (juce::Slider::rotarySliderFillColourId, big ? spec.accent : coral);
            addAndMakeVisible (sl);
            sAtt.add (new juce::AudioProcessorValueTreeState::SliderAttachment (st, k.id, *sl));
            if (auto* prm = st.getParameter (k.id)) { sl->setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue())); sl->setTooltip (prm->getName (64)); }
            knobIds.push_back (k.id);
            knobLabels.push_back (k.label);
            knobBig.push_back (big);
        };
        for (auto& k : spec.main) makeKnob (k, true);
        for (auto& k : spec.small) makeKnob (k, false);
        for (auto& t : spec.toggles)
        {
            auto* b = toggles.add (new juce::TextButton (t.label));
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonOnColourId, coral);
            addAndMakeVisible (b);
            bAtt.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (st, t.id, *b));
            if (auto* prm = st.getParameter (t.id)) b->setTooltip (prm->getName (64));
        }
        for (auto& c : spec.combos)
        {
            auto* cb = combos.add (new juce::ComboBox());
            if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (st.getParameter (c.id))) { cb->addItemList (prm->choices, 1); cb->setTooltip (prm->getName (64)); }
            addAndMakeVisible (cb);
            cAtt.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (st, c.id, *cb));
        }
        if (spec.extra) { extraComp.reset (spec.extra()); addAndMakeVisible (*extraComp); }

        for (auto* b : { &prev, &next, &presetBtn }) addAndMakeVisible (b);
        prev.onClick = [this] { const int n = host.numPresets(), c = host.presetIndex(); host.loadPreset (c <= 0 ? n - 1 : c - 1); };
        next.onClick = [this] { const int n = host.numPresets(), c = host.presetIndex(); host.loadPreset (c < 0 || c >= n - 1 ? 0 : c + 1); };
        presetBtn.onClick = [this]
        {
            juce::PopupMenu m;
            for (int i = 0; i < host.numPresets(); ++i) m.addItem (i + 1, host.presetName (i), true, i == host.presetIndex());
            juce::Component::SafePointer<PedalPanel> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBtn).withMinimumWidth (260),
                             [safe] (int r) { if (safe != nullptr && r > 0) safe->host.loadPreset (r - 1); });
        };
        layout();
        startTimerHz (20);
        timerCallback();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (cream);
        header (g, { 0, 0, W, 72 }, spec.name, spec.tagline);
        // machine panel
        panel (g, juce::Rectangle<float> (16, 84, W - 32, 104), 14);
        // screen
        auto scr = juce::Rectangle<float> (16, 198, W - 32, 78);
        sticker (g, scr, ink, 12, 3, 2.2f);
        const int m = machine();
        g.setColour (spec.accent.brighter (0.25f));
        g.setFont (font (30, 2, 0.04f));
        g.drawText (spec.machines[juce::jlimit (0, spec.machines.size() - 1, m)], juce::Rectangle<int> (36, 206, 360, 38), juce::Justification::centredLeft);
        g.setColour (cream.withAlpha (0.8f));
        g.setFont (font (13.5f, 1));
        g.drawText (spec.info ? spec.info (m) : juce::String(), juce::Rectangle<int> (36, 244, 620, 22), juce::Justification::centredLeft, true);
        g.setColour (cream);
        g.setFont (font (30, 2));
        g.drawText (spec.readout ? spec.readout() : juce::String(), juce::Rectangle<int> (W - 380, 206, 330, 38), juce::Justification::centredRight);
        g.setColour (cream.withAlpha (0.7f));
        g.setFont (font (12.5f, 2, 0.06f));
        g.drawText (spec.readoutSub ? spec.readoutSub() : juce::String(), juce::Rectangle<int> (W - 380, 244, 330, 20), juce::Justification::centredRight);
        // level meter in the screen
        const float lv = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (host.outputLevel(), -60.0f) + 60.0f) / 60.0f);
        auto mr = juce::Rectangle<float> ((float) W - 230, 266, 180, 5);
        g.setColour (cream.withAlpha (0.15f)); g.fillRoundedRectangle (mr, 4);
        g.setColour (spec.accent); g.fillRoundedRectangle (mr.withWidth (mr.getWidth() * lv), 4);

        panel (g, juce::Rectangle<float> (16, 288, W - 32, 140), 14);
        panel (g, juce::Rectangle<float> (16, 438, W - 32, 110), 14);
        for (size_t k = 0; k < knobIds.size(); ++k)
        {
            auto b = knobs[(int) k]->getBounds();
            juce::String lab = knobLabels[k];
            if (lab == "P1" || lab == "P2")
            {
                const auto pn = spec.paramNames ? spec.paramNames (m) : std::pair<juce::String, juce::String> { "P1", "P2" };
                lab = lab == "P1" ? pn.first : pn.second;
            }
            label (g, lab, { b.getCentreX() - 70, b.getBottom() + 2, 140, 16 }, juce::Justification::centred, knobBig[k] ? 12.5f : 10.5f, knobBig[k] ? ink : inkSoft);
        }
        for (int i = 0; i < combos.size(); ++i)
            label (g, spec.combos[(size_t) i].label, combos[i]->getBounds().translated (0, 30).withHeight (16), juce::Justification::centred, 10.5f, inkSoft);
    }

    void resized() override { layout(); }
    int machine() const { return juce::roundToInt (host.state().getRawParameterValue (spec.machineId)->load()); }

private:
    void layout()
    {
        const int cols = (spec.machines.size() + 1) / 2, cw = (W - 48 - (cols - 1) * 8) / cols;
        for (int i = 0; i < machineButtons.size(); ++i)
            machineButtons[i]->setBounds (24 + (i % cols) * (cw + 8), 94 + (i / cols) * 46, cw, 38);
        int nMain = 0; for (bool b : knobBig) nMain += b ? 1 : 0;
        int mi = 0, si = 0;
        const int slotMain = (W - 48) / juce::jmax (1, nMain);
        // bottom row: small knobs, then combos, toggles and extra
        const int nBottom = (int) spec.small.size() + combos.size() + toggles.size() + (spec.extra ? 1 : 0);
        const int slotB = (W - 48) / juce::jmax (1, nBottom);
        int bi = 0;
        for (int k = 0; k < knobs.size(); ++k)
        {
            if (knobBig[(size_t) k]) { const int cx = 24 + slotMain * mi + slotMain / 2; knobs[k]->setBounds (cx - 40, 300, 80, 80); ++mi; }
            else { const int cx = 24 + slotB * bi + slotB / 2; knobs[k]->setBounds (cx - 26, 448, 52, 52); ++bi; ++si; }
        }
        const int bw = juce::jmin (112, slotB - 10);
        for (auto* c : combos) { const int cx = 24 + slotB * bi + slotB / 2; c->setBounds (cx - bw / 2, 458, bw, 28); ++bi; }
        for (auto* t : toggles) { const int cx = 24 + slotB * bi + slotB / 2; t->setBounds (cx - bw / 2, 456, bw, 34); ++bi; }
        if (extraComp) { const int cx = 24 + slotB * bi + slotB / 2; extraComp->setBounds (cx - bw / 2, 456, bw, 34); }
        prev.setBounds (440, 18, 34, 36);
        presetBtn.setBounds (480, 18, 340, 36);
        next.setBounds (826, 18, 34, 36);
    }

    void timerCallback() override
    {
        const int m = machine();
        for (int i = 0; i < machineButtons.size(); ++i) machineButtons[i]->setToggleState (i == m, juce::dontSendNotification);
        const int pi = host.presetIndex();
        juce::String t = pi >= 0 ? host.presetName (pi) : juce::String ("Init");
        if (host.presetEdited()) t << " *";
        if (presetBtn.getButtonText() != t) presetBtn.setButtonText (t);
        if (m != lastMachine) { lastMachine = m; repaint(); }
        else repaint (16, 198, W - 32, 80);
    }

    PedalHost& host;
    PedalSpec spec;
    LookScope lookScope;
    juce::OwnedArray<juce::TextButton> machineButtons, toggles;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<juce::ComboBox> combos;
    std::vector<const char*> knobIds;
    std::vector<juce::String> knobLabels;
    std::vector<bool> knobBig;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> sAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> bAtt;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
    std::unique_ptr<juce::Component> extraComp;
    juce::TextButton prev { "<" }, next { ">" }, presetBtn;
    juce::TooltipWindow tips { this, 700 };
    int lastMachine = -1;
};

class PedalEditor : public juce::AudioProcessorEditor
{
public:
    PedalEditor (juce::AudioProcessor& p, PedalHost& h, PedalSpec s) : AudioProcessorEditor (p), panel (p, h, std::move (s))
    {
        addAndMakeVisible (panel);
        setResizable (true, true);
        setResizeLimits (PedalPanel::W / 2, PedalPanel::H / 2, PedalPanel::W * 3 / 2, PedalPanel::H * 3 / 2);
        if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) PedalPanel::W / PedalPanel::H);
        setSize (PedalPanel::W * 9 / 10, PedalPanel::H * 9 / 10);
    }
    void paint (juce::Graphics& g) override { g.fillAll (cream); }
    void resized() override
    {
        panel.setBounds (0, 0, PedalPanel::W, PedalPanel::H);
        panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / PedalPanel::W));
    }
    PedalPanel panel;
};
} // namespace toota
