// Offline test: renders every factory preset through the real plugin processor and
// reports level, NaN/denormal problems and tail behaviour. Optionally writes WAVs.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include "../Source/Presets.h"
#include <cstdio>
#include <juce_gui_basics/juce_gui_basics.h>

struct Stats { float peak = 0, rmsHeld = 0, rmsTail = 0; bool bad = false; };

static Stats renderPreset (int index, double sr, int block, juce::AudioBuffer<float>* capture)
{
    Augur6Processor proc;
    proc.setRateAndBufferSizeDetails (sr, block);
    proc.prepareToPlay (sr, block);
    proc.loadPreset (index);

    const auto& p = factoryPresets()[(size_t) index];
    std::vector<int> notes;
    if (p.category == "Bass")       notes = { 36 };
    else if (p.category == "Leads") notes = { 67 };
    else                            notes = { 60, 64, 67, 71 };

    const int heldSamples = (int) (sr * 1.6);
    const int total = (int) (sr * 4.5);
    juce::AudioBuffer<float> buf (2, block);
    if (capture) capture->setSize (2, total);

    Stats st;
    double sumHeld = 0, sumTail = 0; int nHeld = 0, nTail = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buf.setSize (2, n, false, false, true);
        juce::MidiBuffer midi;
        if (pos == 0)
            for (int nn : notes) midi.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) 100), 0);
        // leads: legato second note to exercise glide/note stack
        if (p.category == "Leads" && pos <= heldSamples / 2 && heldSamples / 2 < pos + n)
            midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), heldSamples / 2 - pos);
        if (pos <= heldSamples && heldSamples < pos + n)
        {
            for (int nn : notes) midi.addEvent (juce::MidiMessage::noteOff (1, nn), heldSamples - pos);
            if (p.category == "Leads") midi.addEvent (juce::MidiMessage::noteOff (1, 72), heldSamples - pos);
        }

        proc.processBlock (buf, midi);

        for (int ch = 0; ch < 2; ++ch)
        {
            const float* d = buf.getReadPointer (ch);
            for (int i = 0; i < n; ++i)
            {
                const float v = d[i];
                if (! std::isfinite (v)) st.bad = true;
                st.peak = std::max (st.peak, std::abs (v));
                const int t = pos + i;
                if (t > sr * 0.3 && t < heldSamples) { sumHeld += v * v; ++nHeld; }
                if (t > total - sr * 0.5)            { sumTail += v * v; ++nTail; }
            }
            if (capture) capture->copyFrom (ch, pos, buf, ch, 0, n);
        }
    }
    st.rmsHeld = (float) std::sqrt (sumHeld / std::max (1, nHeld));
    st.rmsTail = (float) std::sqrt (sumTail / std::max (1, nTail));
    return st;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File();
    if (outDir != juce::File()) outDir.createDirectory();

    const auto& bank = factoryPresets();
    int failures = 0;
    std::printf ("%-3s %-20s %-7s %8s %8s %8s  %s\n", "#", "preset", "cat", "peak", "rmsHeld", "rmsTail", "status");

    for (int i = 0; i < (int) bank.size(); ++i)
    {
        juce::AudioBuffer<float> cap;
        auto s = renderPreset (i, 48000.0, 512, outDir != juce::File() ? &cap : nullptr);
        auto s2 = renderPreset (i, 44100.0, 437, nullptr);   // odd block size, other rate
        auto s3 = renderPreset (i, 96000.0, 64, nullptr);

        juce::String status = "ok";
        const float peak = std::max ({ s.peak, s2.peak, s3.peak });
        if (s.bad || s2.bad || s3.bad)             status = "NaN!";
        else if (s.peak < 0.25f)                   status = "TOO QUIET";
        else if (peak > 1.0f)                      status = "TOO LOUD";
        else if (std::abs (s.rmsHeld - s2.rmsHeld) > 0.5f * s.rmsHeld) status = "SR MISMATCH";
        if (status != "ok") ++failures;

        std::printf ("%-3d %-20s %-7s %8.3f %8.4f %8.5f  %s\n", i, bank[(size_t) i].name.toRawUTF8(),
                     bank[(size_t) i].category.toRawUTF8(), peak, s.rmsHeld, s.rmsTail, status.toRawUTF8());

        if (outDir != juce::File())
        {
            auto file = outDir.getChildFile (juce::String (i).paddedLeft ('0', 2) + "_" + bank[(size_t) i].name.replaceCharacters (" '", "__") + ".wav");
            file.deleteFile();
            juce::WavAudioFormat wav;
            if (auto os = file.createOutputStream())
                if (auto* w = wav.createWriterFor (os.get(), 48000.0, 2, 24, {}, 0))
                {
                    os.release();
                    std::unique_ptr<juce::AudioFormatWriter> writer (w);
                    writer->writeFromAudioSampleBuffer (cap, 0, cap.getNumSamples());
                }
        }
    }

    // State save/restore round-trip
    {
        Augur6Processor a, b;
        a.loadPreset (5);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = b.getCurrentProgram() == 5;
        for (auto* prm : a.getParameters())
        {
            auto* pa = dynamic_cast<juce::RangedAudioParameter*> (prm);
            auto* pb = dynamic_cast<juce::RangedAudioParameter*> (b.getParameters()[prm->getParameterIndex()]);
            if (pa && pb && std::abs (pa->getValue() - pb->getValue()) > 1e-5f) same = false;
        }
        std::printf ("state round-trip: %s\n", same ? "ok" : "FAILED");
        if (! same) ++failures;
    }

    // Editor snapshot (needs a display, e.g. xvfb-run)
    if (auto* snap = std::getenv ("AUGUR_SNAPSHOT"))
    {
        Augur6Processor proc;
        proc.loadPreset (std::getenv ("AUGUR_SNAP_PRESET") ? std::atoi (std::getenv ("AUGUR_SNAP_PRESET")) : 0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (1216, 716);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.5f);
        juce::File f (snap);
        f.deleteFile();
        juce::PNGImageFormat png;
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        std::printf ("snapshot written: %s\n", snap);
    }
    std::printf ("presets: %d, failures: %d\n", (int) bank.size(), failures);
    return failures == 0 ? 0 : 1;
}
