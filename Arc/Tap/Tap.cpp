// Arc Tap: put it on any track in Live. It passes the sound straight through, and when Arc asks it records
// that track in time with Live so Arc can arrange it. No bouncing.
#include "../Source/Capture.h"
#include "../../Common/TootaTheme.h"

namespace tt = toota;

class TapProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    TapProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        id = juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()).substring (0, 12);
        startTimerHz (5);
    }
    ~TapProcessor() override { stopTimer(); arccap::removeTap (id); }

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
        return out == in && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
    }
    void prepareToPlay (double r, int) override { rate = r; }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        const int n = b.getNumSamples();
        for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) b.clear (c, 0, n);
        double ppq = 0, bpm = lastBpm.load(); bool play = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                play = pos->getIsPlaying();
                if (auto q = pos->getPpqPosition()) ppq = *q;
                if (auto t = pos->getBpm()) bpm = *t;
            }
        lastBpm = juce::jlimit (20.0, 400.0, bpm);
        recorder.process (b, n, ppq, lastBpm.load(), play, rate);
        float pk = 0;
        for (int c = 0; c < b.getNumChannels(); ++c) pk = juce::jmax (pk, b.getMagnitude (c, 0, n));
        peak = juce::jmax (pk, peak.load() * 0.9f);
    }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Arc Tap"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& d) override
    {
        juce::ValueTree t ("ARCTAP");
        t.setProperty ("name", userName, nullptr);
        if (auto x = t.createXml()) copyXmlToBinary (*x, d);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size)) userName = x->getStringAttribute ("name");
    }
    void updateTrackProperties (const TrackProperties& p) override
    {
        const juce::ScopedLock sl (nameLock);
        hostName = p.name.value_or (juce::String());
    }

    juce::String trackName() const
    {
        const juce::ScopedLock sl (nameLock);
        if (userName.isNotEmpty()) return userName;
        return hostName.isNotEmpty() ? hostName : juce::String ("Untitled track");
    }
    bool hostGaveName() const { const juce::ScopedLock sl (nameLock); return hostName.isNotEmpty(); }
    void setUserName (const juce::String& s) { const juce::ScopedLock sl (nameLock); userName = s.trim(); }

    juce::String status() const { return state; }
    double progress() const { return recorder.progress(); }
    float level() const { return peak.load(); }
    int barsWanted() const { return recorder.bars(); }
    void poll() { timerCallback(); }     // tests

private:
    void timerCallback() override
    {
        // a new request from Arc?
        {
            const auto r = arccap::readRequest();
            if (r.valid() && r.id == requestId && r.cancel) { recorder.disarm(); state = "idle"; }
            else if (r.valid() && ! r.cancel && r.id != requestId && arccap::now() - r.created < 120000)
            {
                requestId = r.id;
                file = {};
                recorder.arm (r.bars, lastBpm.load(), rate);
                state = "armed";
            }
        }
        if (recorder.isArmed() && ! recorder.isDone()) state = recorder.started() ? "recording" : "armed";
        // finished: write the file and tell Arc where it is
        if (recorder.isDone() && file.isEmpty() && requestId.isNotEmpty())
        {
            const auto& a = recorder.audio();
            const int len = recorder.length();
            float pk = 0;
            for (int c = 0; c < 2; ++c) pk = juce::jmax (pk, a.getMagnitude (c, 0, len));
            if (pk < 1.0e-4f) { state = "silent"; file = "-"; }
            else
            {
                auto out = arccap::capturesDir().getChildFile (requestId).getChildFile (arccap::safeName (trackName()) + " " + id.substring (0, 4) + ".wav");
                if (arccap::writeWav (out, a, len, rate)) { file = out.getFullPathName(); state = "done"; }
                else { file = "-"; state = "silent"; }
            }
        }
        if (++beat % 2 == 0 || state != lastSent)
        {
            arccap::TapInfo i;
            i.id = id; i.name = trackName(); i.state = state; i.request = requestId;
            i.file = file == "-" ? juce::String() : file;
            i.progress = recorder.progress(); i.bpm = recorder.bpmUsed(); i.seen = arccap::now();
            arccap::writeTap (i);
            lastSent = state;
        }
    }

    arccap::Recorder recorder;
    juce::String id, requestId, file, state { "idle" };
    juce::String userName, hostName;
    juce::CriticalSection nameLock;
    std::atomic<double> lastBpm { 120.0 };
    std::atomic<float> peak { 0 };
    double rate = 48000;
    int beat = 0;
    juce::String lastSent;
};

//==============================================================================
class TapEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit TapEditor (TapProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        nameBox.setText (p.trackName(), false);
        nameBox.setFont (tt::font (16, 2));
        nameBox.setJustification (juce::Justification::centredLeft);
        nameBox.setColour (juce::TextEditor::backgroundColourId, tt::cream);
        nameBox.setColour (juce::TextEditor::textColourId, tt::ink);
        nameBox.setColour (juce::TextEditor::outlineColourId, tt::ink);
        nameBox.setColour (juce::TextEditor::focusedOutlineColourId, tt::teal);
        nameBox.setTooltip ("The name this track gets in Arc. Live fills it in when it can; type to change it.");
        nameBox.onReturnKey = nameBox.onFocusLost = [this] { proc.setUserName (nameBox.getText() == hostLast ? juce::String() : nameBox.getText()); };
        addAndMakeVisible (nameBox);
        setSize (380, 214);
        startTimerHz (15);
    }

    void resized() override { nameBox.setBounds (88, 84, getWidth() - 104, 30); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (tt::cream);
        tt::header (g, { 0, 0, getWidth(), 70 }, "ARC TAP", "sends this track to Arc");
        tt::label (g, "TRACK", { 16, 84, 66, 30 }, juce::Justification::centredLeft, 12.0f, tt::inkSoft);

        const auto st = proc.status();
        juce::Colour c = tt::cream2; juce::String text;
        if (st == "armed")          { c = tt::orange; text = "Ready. Press play in Live: recording starts on the next bar."; }
        else if (st == "recording") { c = tt::coral;  text = "Recording bar " + juce::String (juce::jmin (proc.barsWanted(), 1 + (int) (proc.progress() * proc.barsWanted()))) + " of " + juce::String (proc.barsWanted()); }
        else if (st == "done")      { c = tt::teal;   text = "Sent to Arc"; }
        else if (st == "silent")    { c = tt::cream3; text = "This track was silent, so nothing was sent"; }
        else                        { text = "Waiting for Arc. Open Arc and press CAPTURE."; }
        auto box = juce::Rectangle<float> (16, 128, (float) getWidth() - 32, 44);
        tt::sticker (g, box, tt::cream2, 10, 2.5f, 2.0f);
        if (st == "recording" || st == "done")
        {
            auto fill = box.reduced (2.5f);
            fill.setWidth (fill.getWidth() * (float) (st == "done" ? 1.0 : proc.progress()));
            g.setColour (c.withAlpha (0.55f)); g.fillRoundedRectangle (fill, 8);
        }
        auto dot = juce::Rectangle<float> (box.getX() + 12, box.getCentreY() - 7, 14, 14);
        g.setColour (c); g.fillEllipse (dot);
        g.setColour (tt::ink); g.drawEllipse (dot, 2.0f);
        g.setFont (tt::font (13.5f, 2));
        g.drawFittedText (text, box.withTrimmedLeft (36).reduced (6, 2).toNearestInt(), juce::Justification::centredLeft, 2);

        // level
        auto m = juce::Rectangle<float> (16, 184, (float) getWidth() - 32, 12);
        tt::sticker (g, m, tt::cream, 6, 0, 1.6f);
        const float lv = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (proc.level(), -60.0f) + 60.0f) / 60.0f);
        g.setColour (tt::teal); g.fillRoundedRectangle (m.reduced (2).withWidth ((m.getWidth() - 4) * lv), 4);
    }

private:
    void timerCallback() override
    {
        if (! nameBox.hasKeyboardFocus (true))
        {
            const auto n = proc.trackName();
            if (n != nameBox.getText()) { nameBox.setText (n, false); hostLast = n; }
        }
        repaint();
    }

    TapProcessor& proc;
    tt::LookScope lookScope;
    juce::TextEditor nameBox;
    juce::String hostLast;
    juce::TooltipWindow tips { this, 600 };
};

juce::AudioProcessorEditor* TapProcessor::createEditor() { return new TapEditor (*this); }

#ifndef ARC_TAP_NO_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TapProcessor(); }
#endif
