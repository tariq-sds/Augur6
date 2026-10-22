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

    // ---- EQ: global
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ilp::eqPhase, 1 }, "EQ Phase", juce::StringArray { "Zero Latency", "Linear Phase" }, 0));
    addF (l, ilp::gainScale, "Gain Scale", 0, 2, 1, -1, pctStr);
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ilp::autoGain, 1 }, "Auto Gain", false));
    addF (l, ilp::outPan, "Output Pan", -1, 1, 0, -1, [] (float v) { return std::abs (v) < 0.005f ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (v) * 100)) + (v < 0 ? "L" : "R"); });

    // ---- EQ: 24 bands
    auto logRange = [] (float lo, float hi)
    {
        return juce::NormalisableRange<float> (lo, hi,
            [] (float a, float b, float t) { return a * std::pow (b / a, t); },
            [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); });
    };
    juce::StringArray shapes, slopes, places;
    for (int i = 0; i < ileq::kNumShapes; ++i) shapes.add (ileq::shapeName (i));
    for (int sl : ileq::slopes()) slopes.add (sl == 0 ? juce::String ("Brickwall") : juce::String (sl) + " dB/oct");
    for (int i = 0; i < ileq::kNumPlaces; ++i) places.add (ileq::placeName (i));
    for (int b = 0; b < ileq::kBands; ++b)
    {
        const juce::String n = "Band " + juce::String (b + 1) + " ";
        auto id = [b] (int f) { return juce::ParameterID { ilp::band (b, f), 1 }; };
        l.add (std::make_unique<juce::AudioParameterBool> (id (ilp::fUsed), n + "Used", false));
        l.add (std::make_unique<juce::AudioParameterBool> (id (ilp::fOn), n + "Enabled", true));
        l.add (std::make_unique<APF> (id (ilp::fFreq), n + "Frequency", logRange (10, 30000), 1000.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hzStr (v); })));
        l.add (std::make_unique<APF> (id (ilp::fGain), n + "Gain", juce::NormalisableRange<float> (-30, 30), 0.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return dbStr (v); })));
        l.add (std::make_unique<APF> (id (ilp::fQ), n + "Q", logRange (0.025f, 40), 1.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 1 ? 3 : 2); })));
        l.add (std::make_unique<juce::AudioParameterChoice> (id (ilp::fShape), n + "Shape", shapes, 0));
        l.add (std::make_unique<juce::AudioParameterChoice> (id (ilp::fSlope), n + "Slope", slopes, 1));
        l.add (std::make_unique<juce::AudioParameterChoice> (id (ilp::fPlace), n + "Stereo Placement", places, 0));
        l.add (std::make_unique<juce::AudioParameterBool> (id (ilp::fDyn), n + "Dynamic", false));
        l.add (std::make_unique<APF> (id (ilp::fRange), n + "Dynamic Range", juce::NormalisableRange<float> (-30, 30), -6.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return dbStr (v); })));
        l.add (std::make_unique<APF> (id (ilp::fThr), n + "Threshold", juce::NormalisableRange<float> (-80, 0), -30.0f,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1) + " dB"; })));
        l.add (std::make_unique<juce::AudioParameterBool> (id (ilp::fAuto), n + "Auto Threshold", true));
        l.add (std::make_unique<juce::AudioParameterBool> (id (ilp::fExt), n + "External Sidechain", false));
    }
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
    for (int b = 0; b < ileq::kBands; ++b)
        for (int f = 0; f < ilp::kBandFields; ++f)
            bandRaw[(size_t) b][(size_t) f] = apvts.getRawParameterValue (ilp::band (b, f));
    registryId = ileq::Registry::get().add();
    startTimerHz (30);
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
    eq.prepare (sampleRate, samplesPerBlock);
    spectrum.fs = sampleRate;
    eqL.assign ((size_t) samplesPerBlock + 8, 0.0f);
    eqR.assign ((size_t) samplesPerBlock + 8, 0.0f);
    for (auto& d : dry) d.assign ((size_t) ileq::Engine::kLpSize + 1, 0.0f);
    dryPos = 0;
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
    if ((int) eqL.size() < n) { eqL.resize ((size_t) n); eqR.resize ((size_t) n); }

    // ---- EQ settings for this block
    for (int b = 0; b < ileq::kBands; ++b) eq.params[(size_t) b] = bandParams (b);
    eq.phase = (int) pv (ilp::eqPhase);
    eq.gainScale = pv (ilp::gainScale);
    eq.autoGain = pv (ilp::autoGain) > 0.5f;
    eq.outPan = pv (ilp::outPan);
    const int lat = eq.latency();
    spectrum.taps[ileq::Analyser::pre].push (bassMono.data(), n);
    spectrum.taps[ileq::Analyser::side].push (haveSc ? scMono.data() : nullptr, n);

    // ---- pass 1: Interlock's kick dip, sub duck and bass focus
    for (int i = 0; i < n; ++i)
    {
        const float fDip = sDipF.getNextValue(), fDuck = sDuckF.getNextValue();
        const float fBoost = sBoostF.getNextValue(), gBoostDb = sBoostG.getNextValue();
        const float q = sDipQ.getNextValue();
        const float gDip = std::tan (pi * juce::jmin (fDip, sr * 0.45f) / sr);
        const float gDuckBase = std::tan (pi * juce::jmin (fDuck, sr * 0.45f) / sr);
        const float gBoost = std::tan (pi * juce::jmin (fBoost, sr * 0.45f) / sr);
        const float gDet = std::tan (pi * juce::jmin (fDip * 2.2f, sr * 0.45f) / sr);

        // kick detector: band around the kick fundamental, envelope normalised to recent hits
        float v1, v2;
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
        for (int c = 0; c < nch; ++c)
        {
            float x = chans[c][i];
            dry[c][(size_t) dryPos] = x;   // for Delta: the input, delayed to line up with the output
            float s2 = x;
            {
                const float k = 1.0f / (q * Ad);
                svf (dip[c].ic1, dip[c].ic2, s2, gDip, k, v1, v2);
                s2 += k * (Ad * Ad - 1.0f) * v1;
            }
            {
                const float k = 1.41421356f;
                svf (duckSh[c].ic1, duckSh[c].ic2, s2, gDuckBase / std::sqrt (Ah), k, v1, v2);
                s2 += k * (Ah - 1.0f) * v1 + (Ah * Ah - 1.0f) * v2;
            }
            {
                const float k = 1.0f / (0.9f * Ab);
                svf (boost[c].ic1, boost[c].ic2, s2, gBoost, k, v1, v2);
                s2 += k * (Ab * Ab - 1.0f) * v1;
            }
            (c == 0 ? eqL : eqR)[(size_t) i] = s2;
        }
        dryPos = (dryPos + 1) % (int) dry[0].size();
        if (nch == 1) eqR[(size_t) i] = eqL[(size_t) i];
    }

    // ---- pass 2: the EQ (zero latency or linear phase)
    eq.process (eqL.data(), eqR.data(), n, haveSc ? scMono.data() : nullptr, nch == 2);

    // ---- pass 3: mono lows, parallel compression, delta, output
    const int dsz = (int) dry[0].size();
    int readPos = ((dryPos - n - lat) % dsz + dsz) % dsz;
    for (int i = 0; i < n; ++i)
    {
        const float mix = sMix.getNextValue(), outG = sOut.getNextValue();
        float y[2] { eqL[(size_t) i], eqR[(size_t) i] }, x[2] { dry[0][(size_t) readPos], dry[1][(size_t) readPos] };
        readPos = (readPos + 1) % dsz;

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
    {
        float* o = chans[0];
        if (nch == 2) { for (int i = 0; i < n; ++i) bassMono[(size_t) i] = 0.5f * (chans[0][i] + chans[1][i]); o = bassMono.data(); }
        spectrum.taps[ileq::Analyser::post].push (o, n);
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

// ---------------------------------------------------------------- EQ helpers
ileq::BandParams InterlockProcessor::bandParams (int i) const
{
    auto v = [this, i] (int f) { return bandRaw[(size_t) i][(size_t) f]->load(); };
    ileq::BandParams p;
    p.used = v (ilp::fUsed) > 0.5f;
    p.on = v (ilp::fOn) > 0.5f;
    p.freq = v (ilp::fFreq);
    p.gainDb = v (ilp::fGain);
    p.q = v (ilp::fQ);
    p.shape = (int) v (ilp::fShape);
    p.slopeIndex = (int) v (ilp::fSlope);
    p.place = (int) v (ilp::fPlace);
    p.dynamic = v (ilp::fDyn) > 0.5f;
    p.range = v (ilp::fRange);
    p.thresholdDb = v (ilp::fThr);
    p.autoThr = v (ilp::fAuto) > 0.5f;
    p.external = v (ilp::fExt) > 0.5f;
    return p;
}

void InterlockProcessor::setBandParams (int i, const ileq::BandParams& p, bool gesture)
{
    auto set = [&] (int f, float value)
    {
        if (auto* prm = apvts.getParameter (ilp::band (i, f)))
        {
            const float n = prm->convertTo0to1 (value);
            if (std::abs (prm->getValue() - n) < 1e-7f) return;
            if (gesture) prm->beginChangeGesture();
            prm->setValueNotifyingHost (n);
            if (gesture) prm->endChangeGesture();
        }
    };
    set (ilp::fShape, (float) p.shape); set (ilp::fSlope, (float) p.slopeIndex); set (ilp::fPlace, (float) p.place);
    set (ilp::fFreq, p.freq); set (ilp::fGain, p.gainDb); set (ilp::fQ, p.q);
    set (ilp::fDyn, p.dynamic ? 1.0f : 0.0f); set (ilp::fRange, p.range); set (ilp::fThr, p.thresholdDb);
    set (ilp::fAuto, p.autoThr ? 1.0f : 0.0f); set (ilp::fExt, p.external ? 1.0f : 0.0f);
    set (ilp::fOn, p.on ? 1.0f : 0.0f); set (ilp::fUsed, p.used ? 1.0f : 0.0f);
}

int InterlockProcessor::addBand (double hz, float gainDb, int shape)
{
    for (int i = 0; i < ileq::kBands; ++i)
        if (! bandParams (i).used)
        {
            ileq::BandParams p;
            p.used = true; p.on = true; p.shape = shape; p.freq = (float) juce::jlimit (10.0, 30000.0, hz);
            p.gainDb = ileq::hasGain (shape) ? gainDb : 0.0f;
            p.q = 1.0f; p.slopeIndex = ileq::defaultSlopeIndex (shape);
            setBandParams (i, p);
            return i;
        }
    return -1;
}

void InterlockProcessor::removeBand (int i)
{
    ileq::BandParams p;   // back to defaults, unused
    setBandParams (i, p);
    if (eq.soloBand.load() == i) eq.soloBand = -1;
}

int InterlockProcessor::applyMatch (int maxBands, float amount)
{
    // EQ Match: the difference between the sidechain (reference) and this track, fitted with bells in free bands
    if (spectrum.captureFrames < 5) { spectrum.capturing = false; return 0; }
    auto diff = spectrum.matchDiff();
    spectrum.capturing = false;
    std::array<double, ileq::kPoints> r {};
    for (int p = 0; p < ileq::kPoints; ++p)
    {
        const double hz = ileq::pointHz (p);
        r[(size_t) p] = (hz >= 25 && hz <= 18000) ? juce::jlimit (-12.0, 12.0, diff[(size_t) p] * amount) : 0.0;
    }
    const double fs = eq.sampleRate();
    int made = 0;
    for (int k = 0; k < maxBands; ++k)
    {
        int best = -1; double bv = 0.75;
        for (int p = 0; p < ileq::kPoints; ++p) if (std::abs (r[(size_t) p]) > bv) { bv = std::abs (r[(size_t) p]); best = p; }
        if (best < 0) break;
        const double g = r[(size_t) best];
        int lo = best, hi = best;
        while (lo > 0 && r[(size_t) lo - 1] * g > 0 && std::abs (r[(size_t) lo - 1]) > std::abs (g) * 0.5) --lo;
        while (hi < ileq::kPoints - 1 && r[(size_t) hi + 1] * g > 0 && std::abs (r[(size_t) hi + 1]) > std::abs (g) * 0.5) ++hi;
        // a difference that runs off either end of the range is a shelf, anything else a bell
        const bool lowEdge = ileq::pointHz (lo) < 40, highEdge = ileq::pointHz (hi) > 12000;
        int slot;
        if (lowEdge && ! highEdge)       slot = addBand (ileq::pointHz (hi), (float) g, ileq::shLowShelf);
        else if (highEdge && ! lowEdge)  slot = addBand (ileq::pointHz (lo), (float) g, ileq::shHighShelf);
        else
        {
            const double bw = juce::jlimit (0.25, 4.0, std::log2 (ileq::pointHz (hi) / ileq::pointHz (lo)) + 0.1);
            slot = addBand (ileq::pointHz (best), (float) g);
            if (slot >= 0) { auto bq = bandParams (slot); bq.q = (float) (std::sqrt (std::pow (2.0, bw)) / (std::pow (2.0, bw) - 1.0)); setBandParams (slot, bq); }
        }
        if (slot < 0) break;
        auto bp = bandParams (slot);
        if (! (lowEdge ^ highEdge)) {} else bp.q = 0.7071f;
        setBandParams (slot, bp);
        ++made;
        for (int p = 0; p < ileq::kPoints; ++p) r[(size_t) p] -= ileq::Engine::specDb (bp, 1.0f, fs, ileq::pointHz (p));
    }
    return made;
}

void InterlockProcessor::computeAutoGain()
{
    // pink-weighted loudness change of the static curve, so turning things up doesn't fool your ears
    std::array<ileq::BandParams, ileq::kBands> cur;
    for (int i = 0; i < ileq::kBands; ++i) cur[(size_t) i] = bandParams (i);
    const float scale = pv (ilp::gainScale);
    bool same = scale == lastAutoScale;
    for (int i = 0; i < ileq::kBands && same; ++i) same = cur[(size_t) i] == lastAutoParams[(size_t) i];
    if (same) return;
    lastAutoParams = cur; lastAutoScale = scale;
    const double fs = currentSampleRate;
    double sum = 0; int n = 0;
    for (double hz = 20; hz < 20000; hz *= 1.06, ++n)
    {
        double db = 0;
        for (auto& p : cur) db += ileq::Engine::specDb (p, scale, fs, hz);
        sum += std::pow (10.0, db / 10.0);
    }
    eq.autoGainDb = n ? (float) juce::jlimit (-24.0, 24.0, -10.0 * std::log10 (sum / n)) : 0.0f;
}

void InterlockProcessor::timerCallback()
{
    pollLearn();
    computeAutoGain();
    const int lat = (int) pv (ilp::eqPhase) == 1 ? ileq::Engine::kLpSize : 0;
    if (lat != reportedLatency) { reportedLatency = lat; setLatencySamples (lat); }
    if (editorOpen.load() || spectrum.capturing)
    {
        spectrum.analyse();
        ileq::Registry::get().publish (registryId, trackName, spectrum.curves[ileq::Analyser::post]);
    }
}

juce::ValueTree InterlockProcessor::eqSnapshot() const
{
    juce::ValueTree t ("EQ");
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            const auto id = r->paramID;
            if (id.startsWith ("b") || id == ilp::eqPhase || id == ilp::gainScale || id == ilp::autoGain || id == ilp::outPan)
                t.setProperty (id, r->getValue(), nullptr);
        }
    return t;
}

void InterlockProcessor::eqRestore (const juce::ValueTree& t)
{
    if (! t.isValid()) return;
    for (auto* prm : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm))
            if (t.hasProperty (r->paramID))
            {
                r->beginChangeGesture();
                r->setValueNotifyingHost ((float) t.getProperty (r->paramID));
                r->endChangeGesture();
            }
}

void InterlockProcessor::abSelect (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    if (slot == abSlot) return;
    abStore[abSlot] = eqSnapshot();
    abSlot = slot;
    if (abStore[slot].isValid()) eqRestore (abStore[slot]);
}

void InterlockProcessor::abCopyToOther()
{
    abStore[1 - abSlot] = eqSnapshot();
}

void InterlockProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("learned", hasLearned.load(), nullptr);
    st.setProperty ("bassRms", bassRmsDb.load(), nullptr);
    st.setProperty ("kickHz", learnedKickHz.load(), nullptr);
    st.setProperty ("boostHz", learnedBoostHz.load(), nullptr);
    st.setProperty ("overlap", learnedOverlap.load(), nullptr);
    st.setProperty ("anMode", analyserMode, nullptr);
    st.setProperty ("anSc", showSidechain, nullptr);
    st.setProperty ("piano", piano, nullptr);
    st.setProperty ("range", rangeIndex, nullptr);
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
            analyserMode = (int) tree.getProperty ("anMode", 3);
            showSidechain = (bool) tree.getProperty ("anSc", true);
            piano = (bool) tree.getProperty ("piano", false);
            rangeIndex = (int) tree.getProperty ("range", 2);
        }
}

juce::AudioProcessorEditor* InterlockProcessor::createEditor() { return new InterlockEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new InterlockProcessor(); }
