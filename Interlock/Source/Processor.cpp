#include "Processor.h"
#include "Editor.h"

namespace
{
using APF = juce::AudioParameterFloat;

void addF (juce::AudioProcessorValueTreeState::ParameterLayout& l, const char* id, const char* name,
           float lo, float hi, float def, float centre, std::function<juce::String (float)> fmt)
{
    juce::NormalisableRange<float> r (lo, hi);
    if (centre > 0) r.setSkewForCentre (centre);
    l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); })));
}

juce::String hzStr (float v)  { return v >= 1000 ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
juce::String dbStr (float v)  { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String pctStr (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }

// Zero-delay-feedback state-variable core (Simper). Returns bandpass (v1) and lowpass (v2).
inline void svf (float& ic1, float& ic2, float x, float g, float k, float& v1, float& v2)
{
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    v1 = a1 * ic1 + a2 * v3;
    v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
}

inline float dbToA (float db) { return std::exp (db * 0.0575646273f); }   // 10^(dB/40)
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout InterlockProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    addF (l, ilp::dipFreq,   "Kick Freq",       30, 200, 55, 70, hzStr);
    addF (l, ilp::dipDepth,  "Dip Depth",       0, 18, 6, -1, [] (float v) { return "-" + juce::String (v, 1) + " dB"; });
    addF (l, ilp::dipQ,      "Dip Width",       0.5f, 6.0f, 1.5f, 1.5f, [] (float v) { return "Q " + juce::String (v, 2); });
    addF (l, ilp::duck,      "Sub Duck",        0, 12, 2, -1, [] (float v) { return "-" + juce::String (v, 1) + " dB"; });
    addF (l, ilp::release,   "Release",         20, 500, 120, 120, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; });
    addF (l, ilp::boostFreq, "Bass Focus Freq", 50, 600, 110, 150, hzStr);
    addF (l, ilp::boostGain, "Bass Focus Gain", -6, 9, 2, -1, dbStr);
    addF (l, ilp::drive,     "Parallel Drive",  0, 1, 0.5f, -1, pctStr);
    addF (l, ilp::parMix,    "Parallel Mix",    0, 1, 0.35f, -1, pctStr);
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ilp::monoLows, 1 }, "Mono Lows", true));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ilp::delta, 1 }, "Delta Listen", false));
    addF (l, ilp::output,    "Output",          -12, 12, 0, -1, dbStr);
    return l;
}

InterlockProcessor::InterlockProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "INTERLOCK", createLayout())
{
    for (auto* p : getParameters())
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            raw[w->paramID.toStdString()] = apvts.getRawParameterValue (w->paramID);
    startTimerHz (10);
}

bool InterlockProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void InterlockProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    analyzer.prepare (sampleRate);
    scMono.assign ((size_t) samplesPerBlock + 8, 0.0f);
    bassMono.assign ((size_t) samplesPerBlock + 8, 0.0f);
    for (auto* s : { &sDipF, &sDuckF, &sBoostF, &sBoostG, &sDipQ, &sMix, &sOut })
        s->reset (sampleRate, 0.05);
    sDipF.setCurrentAndTargetValue (pv (ilp::dipFreq));
    sDuckF.setCurrentAndTargetValue (pv (ilp::dipFreq) * 1.6f);
    sBoostF.setCurrentAndTargetValue (pv (ilp::boostFreq));
    sBoostG.setCurrentAndTargetValue (pv (ilp::boostGain));
    sDipQ.setCurrentAndTargetValue (pv (ilp::dipQ));
    sMix.setCurrentAndTargetValue (pv (ilp::parMix));
    sOut.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pv (ilp::output)));
    scBand = mono1 = mono2 = {};
    for (int c = 0; c < 2; ++c) dip[c] = duckSh[c] = boost[c] = {};
    env = compEnv = 0;
    ref = 1e-5f;
}

void InterlockProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = main.getNumSamples();
    const int nch = juce::jmin (2, main.getNumChannels());
    if (n == 0 || nch == 0) return;

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                analyzer.hostBpm = (float) *bpm;

    if ((int) scMono.size() < n) { scMono.resize ((size_t) n); bassMono.resize ((size_t) n); }

    // ---- Sidechain (kick) mono sum
    bool haveSc = false;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            haveSc = true;
            const int sch = sc.getNumChannels();
            for (int i = 0; i < n; ++i)
            {
                float s = 0;
                for (int c = 0; c < sch; ++c) s += sc.getSample (c, i);
                scMono[(size_t) i] = s / (float) sch;
            }
        }
    }
    if (! haveSc) std::fill (scMono.begin(), scMono.begin() + n, 0.0f);

    for (int i = 0; i < n; ++i)
    {
        float s = 0;
        for (int c = 0; c < nch; ++c) s += main.getSample (c, i);
        bassMono[(size_t) i] = s / (float) nch;
    }
    analyzer.push (bassMono.data(), scMono.data(), n);

    // Learn automatically the first time kick and bass play together
    if (! hasLearned.load() && analyzer.learnState.load() == 0)
    {
        float scPk = 0, bPk = 0;
        for (int i = 0; i < n; ++i) { scPk = juce::jmax (scPk, std::abs (scMono[(size_t) i])); bPk = juce::jmax (bPk, std::abs (bassMono[(size_t) i])); }
        autoLearnHold = (scPk > 0.003f && bPk > 0.003f) ? autoLearnHold + n : 0;
        if (autoLearnHold > currentSampleRate * 0.25)
            analyzer.startLearning();
    }

    // ---- Parameters
    const float sr = (float) currentSampleRate;
    const float pi = juce::MathConstants<float>::pi;
    sDipF.setTargetValue (pv (ilp::dipFreq));
    sDuckF.setTargetValue (pv (ilp::dipFreq) * 1.6f);
    sBoostF.setTargetValue (pv (ilp::boostFreq));
    sBoostG.setTargetValue (pv (ilp::boostGain));
    sDipQ.setTargetValue (pv (ilp::dipQ));
    sMix.setTargetValue (pv (ilp::parMix));
    sOut.setTargetValue (juce::Decibels::decibelsToGain (pv (ilp::output)));
    const float depth = pv (ilp::dipDepth);
    const float duckDb = pv (ilp::duck);
    const float relCoef = std::exp (-1.0f / (pv (ilp::release) * 0.001f * sr));
    const float atkCoef = std::exp (-1.0f / (0.0004f * sr));
    const float refDecay = std::exp (-1.0f / (4.0f * sr));
    const bool mono = pv (ilp::monoLows) > 0.5f;
    const bool deltaOn = pv (ilp::delta) > 0.5f;
    const float gMono = std::tan (pi * 120.0f / sr);

    // Parallel compressor: threshold follows the learned bass level, Drive pushes it down
    const float drv = pv (ilp::drive);
    const float thr = bassRmsDb.load() + 4.0f - drv * 22.0f;
    const float ratio = 3.0f + drv * 7.0f;
    auto gainComputer = [thr, ratio] (float lvl)
    {
        const float over = lvl - thr, knee = 6.0f;
        if (2.0f * over < -knee) return 0.0f;
        if (2.0f * std::abs (over) <= knee) return (1.0f / ratio - 1.0f) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);
        return (1.0f / ratio - 1.0f) * over;
    };
    const float makeup = -gainComputer (bassRmsDb.load() + 3.0f);
    const float cAtk = std::exp (-1.0f / (0.012f * sr));
    const float cRel = std::exp (-1.0f / (0.14f * sr));

    float minDip = 0, minDuck = 0, minComp = 0, scPeak = 0;
    float* chans[2] { main.getWritePointer (0), nch > 1 ? main.getWritePointer (1) : nullptr };

    for (int i = 0; i < n; ++i)
    {
        const float fDip = sDipF.getNextValue(), fDuck = sDuckF.getNextValue();
        const float fBoost = sBoostF.getNextValue(), gBoostDb = sBoostG.getNextValue();
        const float q = sDipQ.getNextValue(), mix = sMix.getNextValue(), outG = sOut.getNextValue();
        const float gDip = std::tan (pi * juce::jmin (fDip, sr * 0.45f) / sr);
        const float gDuckBase = std::tan (pi * juce::jmin (fDuck, sr * 0.45f) / sr);
        const float gBoost = std::tan (pi * juce::jmin (fBoost, sr * 0.45f) / sr);
        const float gDet = std::tan (pi * juce::jmin (fDip * 2.2f, sr * 0.45f) / sr);

        // ---- Kick detector: band around the kick fundamental, envelope normalised to recent hits
        float v1, v2;
        // low-pass at ~2x the kick fundamental: catches the pitch-swept attack as well as the body
        svf (scBand.ic1, scBand.ic2, scMono[(size_t) i], gDet, 1.41421356f, v1, v2);
        const float a = std::abs (v2);
        scPeak = juce::jmax (scPeak, a);
        env = a > env ? a + (env - a) * atkCoef : a + (env - a) * relCoef;
        ref = env > ref ? env : juce::jmax (1e-5f, ref * refDecay);
        float kn = ref > 3e-4f ? juce::jlimit (0.0f, 1.0f, env / ref) : 0.0f;
        kn = std::pow (kn, 1.6f);   // full depth on the hit, lets the bass back in quickly as the kick fades

        const float dipDb = -depth * kn;
        const float duckNow = -duckDb * kn;
        minDip = juce::jmin (minDip, dipDb);
        minDuck = juce::jmin (minDuck, duckNow);

        const float Ad = dbToA (dipDb), Ah = dbToA (duckNow), Ab = dbToA (gBoostDb);
        float y[2] {}, x[2] {};
        for (int c = 0; c < nch; ++c)
        {
            float s = x[c] = chans[c][i];
            // dynamic dip (bell) at the kick fundamental
            {
                const float k = 1.0f / (q * Ad);
                svf (dip[c].ic1, dip[c].ic2, s, gDip, k, v1, v2);
                s += k * (Ad * Ad - 1.0f) * v1;
            }
            // dynamic sub duck (low shelf)
            {
                const float k = 1.41421356f;
                svf (duckSh[c].ic1, duckSh[c].ic2, s, gDuckBase / std::sqrt (Ah), k, v1, v2);
                s += k * (Ah - 1.0f) * v1 + (Ah * Ah - 1.0f) * v2;
            }
            // static bass focus (bell)
            {
                const float k = 1.0f / (0.9f * Ab);
                svf (boost[c].ic1, boost[c].ic2, s, gBoost, k, v1, v2);
                s += k * (Ab * Ab - 1.0f) * v1;
            }
            y[c] = s;
        }

        // ---- Mono the lows (removes low side content below ~120 Hz)
        if (mono && nch == 2)
        {
            float side = 0.5f * (y[0] - y[1]);
            const float mid = 0.5f * (y[0] + y[1]);
            float a1, l1, a2, l2;
            svf (mono1.ic1, mono1.ic2, side, gMono, 1.41421356f, a1, l1);
            svf (mono2.ic1, mono2.ic2, l1, gMono, 1.41421356f, a2, l2);
            side -= l2;
            y[0] = mid + side;
            y[1] = mid - side;
        }

        // ---- Parallel compression
        float pk = 0;
        for (int c = 0; c < nch; ++c) pk = juce::jmax (pk, std::abs (y[c]));
        const float gr = gainComputer (juce::Decibels::gainToDecibels (pk, -120.0f));
        compEnv = gr < compEnv ? gr + (compEnv - gr) * cAtk : gr + (compEnv - gr) * cRel;
        minComp = juce::jmin (minComp, compEnv);
        const float cg = juce::Decibels::decibelsToGain (compEnv + makeup);
        const float norm = 1.0f / (1.0f + 0.7f * mix);

        for (int c = 0; c < nch; ++c)
        {
            float o = (y[c] + mix * y[c] * cg) * norm;
            if (deltaOn) o = x[c] - o;
            chans[c][i] = o * outG;
        }
    }

    dipNowDb = minDip;
    duckNowDb = minDuck;
    compGrDb = minComp;
    sidechainLevel = ref > 3e-4f ? 1.0f : 0.0f;
    juce::ignoreUnused (scPeak);
}

void InterlockProcessor::pollLearn()
{
    if (analyzer.learnState.load() != 2) return;
    const auto r = analyzer.result;
    auto set = [this] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    };
    set (ilp::dipFreq, r.kickHz);
    set (ilp::dipQ, r.dipQ);
    set (ilp::dipDepth, r.depthDb);
    set (ilp::duck, r.duckDb);
    set (ilp::boostFreq, r.boostHz);
    set (ilp::boostGain, r.boostDb);
    set (ilp::release, r.releaseMs);
    bassRmsDb = juce::jlimit (-60.0f, 0.0f, r.bassRmsDb);
    learnedKickHz = r.kickHz;
    learnedBoostHz = r.boostHz;
    learnedOverlap = r.overlap;
    hasLearned = true;
    analyzer.learnState = 3;
}

void InterlockProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("learned", hasLearned.load(), nullptr);
    st.setProperty ("bassRms", bassRmsDb.load(), nullptr);
    st.setProperty ("kickHz", learnedKickHz.load(), nullptr);
    st.setProperty ("boostHz", learnedBoostHz.load(), nullptr);
    st.setProperty ("overlap", learnedOverlap.load(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void InterlockProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            for (auto* p : getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    auto child = tree.getChildWithProperty ("id", r->paramID);
                    if (child.isValid() && child.hasProperty ("value"))
                        r->setValueNotifyingHost (r->convertTo0to1 ((float) child.getProperty ("value")));
                }
            hasLearned = (bool) tree.getProperty ("learned", false);
            bassRmsDb = (float) tree.getProperty ("bassRms", -18.0f);
            learnedKickHz = (float) tree.getProperty ("kickHz", 0.0f);
            learnedBoostHz = (float) tree.getProperty ("boostHz", 0.0f);
            learnedOverlap = (float) tree.getProperty ("overlap", 0.0f);
            if (hasLearned) analyzer.learnState = 3;
        }
}

juce::AudioProcessorEditor* InterlockProcessor::createEditor() { return new InterlockEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new InterlockProcessor(); }
