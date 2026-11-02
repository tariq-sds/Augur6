#pragma once
// MIDI Learn for Toota instruments: any knob can follow a CC from your keyboard. Right-click a control,
// choose MIDI Learn, turn a knob on the keyboard. A default map puts the envelopes and filter on the usual
// "sound controller" CCs straight away. Mappings are saved with the Live set.
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace toota
{
class MidiLearn
{
public:
    void attach (juce::AudioProcessor& p)
    {
        params.clear();
        for (auto* prm : p.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm)) params.push_back (r);
        for (auto& m : map) m = -1;
    }

    void setDefaults (const std::vector<std::pair<int, juce::String>>& d) { defaults = d; resetToDefaults(); }

    void resetToDefaults()
    {
        for (auto& m : map) m = -1;
        for (auto& [cc, id] : defaults) assign (cc, indexOf (id));
        ++version;
    }

    // audio thread: returns true when the message was used for a mapping (or to learn one)
    bool handle (const juce::MidiMessage& m)
    {
        if (! m.isController()) return false;
        const int cc = m.getControllerNumber();
        if (cc < 0 || cc >= 120 || cc == 64) return false;   // keep sustain and channel-mode messages
        const int learn = learning.exchange (-1);
        if (learn >= 0) { assign (cc, learn); lastLearnedCc = cc; ++version; return true; }
        const int pi = map[(size_t) cc].load();
        if (pi < 0 || pi >= (int) params.size()) return false;
        params[(size_t) pi]->setValueNotifyingHost ((float) m.getControllerValue() / 127.0f);
        return true;
    }

    // message thread
    void learn (const juce::String& paramId) { learning = indexOf (paramId); }
    void cancelLearn() { learning = -1; }
    bool isLearning() const { return learning.load() >= 0; }
    juce::String learningName() const { const int i = learning.load(); return i >= 0 && i < (int) params.size() ? params[(size_t) i]->getName (40) : juce::String(); }
    int ccFor (const juce::String& paramId) const
    {
        const int pi = indexOf (paramId);
        for (int cc = 0; cc < 128; ++cc) if (pi >= 0 && map[(size_t) cc].load() == pi) return cc;
        return -1;
    }
    void forget (const juce::String& paramId)
    {
        const int pi = indexOf (paramId);
        for (auto& m : map) if (pi >= 0 && m.load() == pi) m = -1;
        ++version;
    }

    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("MIDIMAP");
        for (int cc = 0; cc < 128; ++cc)
        {
            const int pi = map[(size_t) cc].load();
            if (pi >= 0 && pi < (int) params.size())
            {
                juce::ValueTree e ("CC");
                e.setProperty ("cc", cc, nullptr);
                e.setProperty ("param", params[(size_t) pi]->getParameterID(), nullptr);
                t.appendChild (e, nullptr);
            }
        }
        return t;
    }
    void fromTree (const juce::ValueTree& t)
    {
        if (! t.isValid()) { resetToDefaults(); return; }
        for (auto& m : map) m = -1;
        for (int i = 0; i < t.getNumChildren(); ++i)
        {
            auto e = t.getChild (i);
            assign ((int) e.getProperty ("cc", -1), indexOf (e.getProperty ("param").toString()));
        }
        ++version;
    }

    // right-click menu for one control (editor side)
    void showMenu (juce::Component& target, const juce::String& paramId)
    {
        juce::PopupMenu menu;
        const int cc = ccFor (paramId);
        menu.addSectionHeader (cc >= 0 ? "Mapped to CC " + juce::String (cc) : juce::String ("Not mapped"));
        menu.addItem (1, "MIDI Learn (then turn a knob on your keyboard)");
        menu.addItem (2, "Forget mapping", cc >= 0);
        menu.addSeparator();
        menu.addItem (3, "Reset all to the default knob map");
        juce::Component::SafePointer<juce::Component> safe (&target);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target),
                            [this, paramId, safe] (int r)
                            {
                                if (safe == nullptr) return;
                                if (r == 1) learn (paramId);
                                else if (r == 2) forget (paramId);
                                else if (r == 3) resetToDefaults();
                            });
    }

    std::atomic<int> version { 0 }, lastLearnedCc { -1 };

private:
    int indexOf (const juce::String& id) const
    {
        for (size_t i = 0; i < params.size(); ++i) if (params[i]->getParameterID() == id) return (int) i;
        return -1;
    }
    void assign (int cc, int pi)
    {
        if (cc < 0 || cc >= 128 || pi < 0) return;
        for (auto& m : map) if (m.load() == pi) m = -1;   // one CC per control
        map[(size_t) cc] = pi;
    }

    std::vector<juce::RangedAudioParameter*> params;
    std::array<std::atomic<int>, 128> map {};
    std::vector<std::pair<int, juce::String>> defaults;
    std::atomic<int> learning { -1 };
};

// Opens the MIDI Learn menu on right-click for any control it is added to.
struct MidiLearnClicks : public juce::MouseListener
{
    MidiLearnClicks (MidiLearn& m) : ml (m) {}
    void add (juce::Component& c, const juce::String& paramId) { c.getProperties().set ("midiParam", paramId); c.addMouseListener (this, false); comps.push_back (&c); }
    void removeAll() { for (auto* c : comps) c->removeMouseListener (this); comps.clear(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        auto* c = e.eventComponent;
        while (c != nullptr && ! c->getProperties().contains ("midiParam")) c = c->getParentComponent();
        if (c != nullptr) ml.showMenu (*c, c->getProperties()["midiParam"].toString());
    }
    MidiLearn& ml;
    std::vector<juce::Component*> comps;
};
} // namespace toota
