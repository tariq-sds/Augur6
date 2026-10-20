#include "Processor.h"
#include "Editor.h"

namespace
{
using APF = juce::AudioParameterFloat;

void addF (juce::AudioProcessorValueTreeState::ParameterLayout& l, const char* id, const char* name, float lo, float hi,
           float def, float centre, std::function<juce::String (float)> fmt, float step = 0)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    if (centre > 0) r.setSkewForCentre (centre);
    l.add (std::make_unique<APF> (juce::ParameterID { id, 1 }, name, r, def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); })));
}
juce::String hz (float v)  { return v >= 1000 ? juce::String (v / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; }
juce::String db (float v)  { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; }
inline float toDb (float ms) { return 10.0f * std::log10 (ms + 1e-12f); }
inline float coef (float sec, float sr) { return 1.0f - std::exp (-1.0f / (juce::jmax (1e-4f, sec) * sr)); }
}

juce::AudioProcessorValueTreeState::ParameterLayout ClarionProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    addF (l, cp::shift, "Pitch Shift", -12, 12, 0, -1, [] (float v) { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + juce::String (s) + " st"; }, 1);
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { cp::matchMode, 1 }, "Match To", juce::StringArray { "Key", "Root" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { cp::freezeRef, 1 }, "Hold Reference", false));
    addF (l, cp::hpf,      "High-Pass",      30, 300, 80, 100, hz);
    addF (l, cp::mud,      "Mud Cut",        0, 12, 2, -1, [] (float v) { return "-" + juce::String (v, 1) + " dB"; });
    addF (l, cp::mudFreq,  "Mud Freq",       150, 800, 320, 320, hz);
    addF (l, cp::presence, "Presence",       -6, 9, 2, -1, db);
    addF (l, cp::presFreq, "Presence Freq",  1500, 6000, 3200, 3000, hz);
    addF (l, cp::air,      "Air",            0, 9, 1.5f, -1, db);
    addF (l, cp::deess,    "De-Ess",         0, 1, 0.4f, -1, pct);
    addF (l, cp::clarity,  "Clarity",        0, 1, 0.5f, -1, pct);
    addF (l, cp::drive,    "Comp Drive",     0, 1, 0.5f, -1, pct);
    addF (l, cp::parMix,   "Parallel",       0, 1, 0.4f, -1, pct);
    addF (l, cp::glue,     "Groove Glue",    0, 1, 0.3f, -1, pct);
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { cp::ride, 1 }, "Vocal Ride", true));
    addF (l, cp::target,   "Ride Target",    -6, 12, 3, -1, [] (float v) { return (v >= 0 ? "+" : "") + juce::String (v, 1) + " dB vs music"; });
    addF (l, cp::range,    "Ride Range",     0, 12, 6, -1, [] (float v) { return juce::String (v, 1) + " dB"; });
    addF (l, cp::speed,    "Ride Speed",     20, 1000, 180, 180, [] (float v) { return juce::String (juce::roundToInt (v)) + " ms"; });
    addF (l, cp::output,   "Output",         -12, 12, 0, -1, db);
    return l;
}

ClarionProcessor::ClarionProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CLARION", createLayout())
{
    for (auto* p : getParameters())
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            raw[w->paramID.toStdString()] = apvts.getRawParameterValue (w->paramID);
    startTimerHz (10);
}

bool ClarionProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void ClarionProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    const float sr = (float) sampleRate;
    tracker.prepare (sr);
    shifter.prepare (sr);
    chroma.prepare (sr);
    setLatencySamples (shifter.getLatency());
    musicDelay.assign ((size_t) shifter.getLatency() + 1, 0.0f);
    mdW = 0;
    scMono.assign ((size_t) samplesPerBlock + 16, 0.0f);
    for (int c = 0; c < 2; ++c)
    {
        hp1[c].reset(); hp2[c].reset(); mudF[c].reset(); presF[c].reset(); airF[c].reset(); deF[c].reset();
        for (auto& b : clarF) b[c].reset();
    }
    vMs = mMs = 0; rider = 0; compEnv = glueEnv = 0; glueRef = 1e-5f;
    pitchLog.reserve (8192);
}

void ClarionProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto main = getBusBuffer (buffer, true, 0);
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    const int inCh = main.getNumChannels();
    if (buffer.getNumChannels() < 2) return;
    if (inCh == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    if ((int) scMono.size() < n) scMono.resize ((size_t) n);
    bool haveSc = false;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        const int sch = sc.getNumChannels();
        if (sch > 0)
        {
            haveSc = true;
            for (int i = 0; i < n; ++i)
            {
                float s = 0;
                for (int c = 0; c < sch; ++c) s += sc.getSample (c, i);
                scMono[(size_t) i] = s / (float) sch;
            }
        }
    }
    if (! haveSc) std::fill (scMono.begin(), scMono.begin() + n, 0.0f);

    if (resetKeysRequest.exchange (false))
    {
        hist.fill (0);
        chroma.hist.fill (0); chroma.bassHist.fill (0);
    }
    if (autoEqRequest.exchange (false))
    {
        eMud = eBody = ePres = eAir = eMPres = 0;
        learnSamples = 0;
        learnTarget = (int64_t) (currentSampleRate * 6.0);
        pitchLog.clear();
        autoEqProgress = 0;
        autoEqState = 1;
    }

    const float sr = (float) currentSampleRate;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    // ---- parameters
    const float ratio = std::exp2 (std::round (pv (cp::shift)) / 12.0f);
    const bool freeze = pv (cp::freezeRef) > 0.5f;
    const float gHp = cl::gOf (pv (cp::hpf), sr);
    const float mudDb = -pv (cp::mud), gMud = cl::gOf (pv (cp::mudFreq), sr);
    const float presDb = pv (cp::presence), gPres = cl::gOf (pv (cp::presFreq), sr);
    const float airDb = pv (cp::air), gAir = cl::gOf (9500.0f, sr);
    const float deess = pv (cp::deess), clarity = pv (cp::clarity);
    const float drive = pv (cp::drive), mix = pv (cp::parMix), glue = pv (cp::glue);
    const bool rideOn = pv (cp::ride) > 0.5f;
    const float target = pv (cp::target), range = pv (cp::range);
    const float rideC = coef (pv (cp::speed) * 0.001f, sr);
    const float outG = juce::Decibels::decibelsToGain (pv (cp::output));
    const float gKwH = cl::gOf (100.0f, sr), gKwS = cl::gOf (1500.0f, sr), gSib = cl::gOf (7000.0f, sr), gDe = cl::gOf (5000.0f, sr);
    const float clarHz[3] { 1200.0f, 2500.0f, 4200.0f };
    float gClar[3];
    for (int b = 0; b < 3; ++b) gClar[b] = cl::gOf (clarHz[b], sr);
    const float cV = coef (0.08f, sr), cM = coef (0.4f, sr), cLong = coef (4.0f, sr), cBand = coef (0.06f, sr);
    const float cClar = coef (0.04f, sr), cSibA = coef (0.001f, sr), cSibR = coef (0.06f, sr), cDe = coef (0.01f, sr);
    const float cAtk = std::exp (-1.0f / (0.003f * sr)), cRel = std::exp (-1.0f / (0.09f * sr));
    const float glueAtk = coef (0.001f, sr), glueRel = coef (0.12f, sr), glueRefDecay = std::exp (-1.0f / (3.0f * sr));
    const float thr = vLong - 2.0f - drive * 14.0f, compRatio = 6.0f;
    auto gc = [thr, compRatio] (float lvl)
    {
        const float over = lvl - thr, knee = 6.0f;
        if (2 * over < -knee) return 0.0f;
        if (2 * std::abs (over) <= knee) return (1 / compRatio - 1) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
        return (1 / compRatio - 1) * over;
    };
    const float makeup = -gc (vLong + 4.0f);
    const float histDecay = std::exp (-(256.0f / sr) / 40.0f);

    float minComp = 0, deMax = 0;
    bool anyActive = false;

    for (int i = 0; i < n; ++i)
    {
        float l = L[i], r = R[i];

        // ---------------- pitch tracking + vocal key
        if (tracker.push (0.5f * (l + r)))
        {
            // the shifter works ~latency-T samples in the past; use the estimate made for that moment
            periodHist[(size_t) (histW++ & 7)] = tracker.voiced ? tracker.periodSamples() : 0.0f;
            const int back = juce::jlimit (0, 7, (int) std::lround ((shifter.getLatency() - 0.5f * (period > 0 ? period : 200.0f) - 0.0247f * sr) / 256.0f));
            period = periodHist[(size_t) ((histW - 1 - back) & 7)];
            liveHz = tracker.voiced ? tracker.hz : 0.0f;
            for (auto& h : hist) h *= histDecay;
            if (tracker.voiced)
            {
                const float midi = 69.0f + 12.0f * std::log2 (tracker.hz / 440.0f);
                const float cents = std::abs (midi - std::round (midi));
                const int pc = ((int) std::lround (midi) % 12 + 12) % 12;
                hist[(size_t) pc] += tracker.rms * (1.0f - tracker.aperiodicity) * (1.0f - cents);
                if (autoEqState.load() == 1 && pitchLog.size() < 8000) pitchLog.push_back (tracker.hz);
            }
        }

        // ---------------- reference element / music (sidechain), delayed to line up with the shifted vocal
        const float scNow = scMono[(size_t) i];
        chroma.push (scNow, freeze);
        musicDelay[(size_t) mdW] = scNow;
        mdW = (mdW + 1) % (int) musicDelay.size();
        const float mus = musicDelay[(size_t) mdW];

        // ---------------- key match pitch shift (formant-preserving PSOLA)
        shifter.process (l, r, ratio, period);

        // ---------------- tone: HPF 24 dB/oct, mud, presence, air
        float x[2] { l, r };
        for (int c = 0; c < 2; ++c)
        {
            float s = hp1[c].highPass (x[c], gHp, 0.541f);
            s = hp2[c].highPass (s, gHp, 1.307f);
            s = mudF[c].bell (s, gMud, 1.0f, mudDb);
            s = presF[c].bell (s, gPres, 0.9f, presDb);
            s = airF[c].highShelf (s, gAir, 0.7f, airDb);
            x[c] = s;
        }
        const float mono = 0.5f * (x[0] + x[1]);

        // ---------------- levels (K-ish weighting)
        float bp, lp;
        float kv = kwVh.highPass (mono, gKwH, 0.707f); kv = kwVs.highShelf (kv, gKwS, 0.707f, 4.0f);
        float km = kwMh.highPass (mus, gKwH, 0.707f);  km = kwMs.highShelf (km, gKwS, 0.707f, 4.0f);
        vMs += (kv * kv - vMs) * cV;
        mMs += (km * km - mMs) * cM;
        const float vDb = toDb (vMs), mDb = toDb (mMs);
        const bool active = vDb > -50.0f;
        const bool musicOn = mDb > -62.0f;
        anyActive |= active;
        if (active) vLong += (vDb - vLong) * cLong;

        // ---------------- clarity: lift the bands the music is covering
        for (int b = 0; b < 3; ++b)
        {
            const float vb = vBand[b].bandPass (mono, gClar[b], 1.4f);
            const float mb = mBand[b].bandPass (mus, gClar[b], 1.4f);
            vBandMs[b] += (vb * vb - vBandMs[b]) * cBand;
            mBandMs[b] += (mb * mb - mBandMs[b]) * cBand;
            float want = 0;
            if (active && musicOn)
            {
                const float mask = toDb (mBandMs[b]) - toDb (vBandMs[b]);
                want = clarity * 7.0f * juce::jlimit (0.0f, 1.0f, (mask + 3.0f) / 15.0f);
            }
            clarNow[b] += (want - clarNow[b]) * cClar;
        }
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < 3; ++b)
                if (clarNow[b] > 0.01f) x[c] = clarF[b][c].bell (x[c], gClar[b], 1.2f, clarNow[b]);
                else clarF[b][c].reset();

        // ---------------- de-esser (dynamic high shelf)
        {
            const float sib = sibBand.bandPass (mono, gSib, 1.0f);
            const float as = std::abs (sib), af = std::abs (mono);
            sibEnv += (as - sibEnv) * (as > sibEnv ? cSibA : cSibR);
            fullEnv += (af - fullEnv) * (af > fullEnv ? cSibA : cSibR);
            const float rel = juce::Decibels::gainToDecibels (sibEnv + 1e-9f) - juce::Decibels::gainToDecibels (fullEnv + 1e-9f);
            const float want = deess * 10.0f * juce::jlimit (0.0f, 1.0f, (rel + 9.0f) / 7.0f);
            deNow += (want - deNow) * cDe;
            deMax = juce::jmax (deMax, deNow);
            if (deNow > 0.05f)
                for (int c = 0; c < 2; ++c) x[c] = deF[c].highShelf (x[c], gDe, 0.7f, -deNow);
        }

        // ---------------- parallel compression with groove glue from the music's transients
        {
            const float am = std::abs (mus);
            glueEnv += (am - glueEnv) * (am > glueEnv ? glueAtk : glueRel);
            glueRef = glueEnv > glueRef ? glueEnv : juce::jmax (1e-5f, glueRef * glueRefDecay);
            const float kn = glueRef > 1e-3f ? std::pow (juce::jlimit (0.0f, 1.0f, glueEnv / glueRef), 2.0f) : 0.0f;

            const float pk = juce::jmax (std::abs (x[0]), std::abs (x[1]));
            const float gr = gc (juce::Decibels::gainToDecibels (pk, -120.0f)) - glue * 8.0f * kn;
            compEnv = gr < compEnv ? gr + (compEnv - gr) * cAtk : gr + (compEnv - gr) * cRel;
            minComp = juce::jmin (minComp, compEnv);
            const float cgain = juce::Decibels::decibelsToGain (compEnv + makeup);
            const float norm = 1.0f / (1.0f + 0.5f * mix);
            for (int c = 0; c < 2; ++c) x[c] = (x[c] + mix * x[c] * cgain) * norm;
        }

        // ---------------- vocal ride: hold the vocal at Target dB relative to the music
        if (rideOn)
        {
            if (active)
            {
                const float desired = musicOn ? (mDb + target) - vDb : vLong - vDb;
                rider += (juce::jlimit (-range, range, desired) - rider) * rideC;
            }
        }
        else rider += (0.0f - rider) * rideC;
        const float rg = juce::Decibels::decibelsToGain (rider) * outG;

        L[i] = x[0] * rg;
        R[i] = x[1] * rg;

        // ---------------- auto-EQ learning
        if (autoEqState.load (std::memory_order_relaxed) == 1 && active)
        {
            const float a = lrnMud.bandPass (mono, cl::gOf (300, sr), 0.8f);
            const float b = lrnBody.bandPass (mono, cl::gOf (1000, sr), 0.8f);
            const float c = lrnPres.bandPass (mono, cl::gOf (3200, sr), 0.8f);
            const float d = lrnAir.bandPass (mono, cl::gOf (10000, sr), 0.8f);
            const float e = lrnMPres.bandPass (mus, cl::gOf (3200, sr), 0.8f);
            eMud += a * a; eBody += b * b; ePres += c * c; eAir += d * d; eMPres += e * e;
            if (++learnSamples >= learnTarget)
            {
                const float body = toDb ((float) eBody), mudE = toDb ((float) eMud), pres = toDb ((float) ePres), airE = toDb ((float) eAir);
                const float musP = toDb ((float) eMPres);
                EqResult res;
                res.mud = juce::jlimit (0.0f, 8.0f, (mudE - body + 2.0f) * 0.8f);
                const float maskP = eMPres > 1e-9 ? juce::jlimit (0.0f, 1.0f, (musP - pres) / 12.0f) : 0.3f;
                res.presence = juce::jlimit (0.0f, 7.0f, 1.5f + (-(pres - body) - 6.0f) * 0.4f + maskP * 3.0f);
                res.air = juce::jlimit (0.0f, 6.0f, 1.5f + (-(airE - pres) - 14.0f) * 0.4f);
                if (! pitchLog.empty())
                {
                    std::vector<float> v (pitchLog);
                    std::nth_element (v.begin(), v.begin() + (long) (v.size() / 10), v.end());
                    res.hpf = juce::jlimit (60.0f, 180.0f, 0.7f * v[v.size() / 10]);
                }
                eqResult = res;
                autoEqState = 2;
            }
            autoEqProgress = (float) learnSamples / (float) juce::jmax ((int64_t) 1, learnTarget);
        }
        juce::ignoreUnused (bp, lp);
    }

    // first time a vocal comes through: learn the EQ automatically
    if (! hasAutoEq.load() && autoEqState.load() == 0)
    {
        autoStartHold = anyActive ? autoStartHold + n : 0;
        if (autoStartHold > currentSampleRate * 0.5) autoEqRequest = true;
    }

    // ---- publish
    for (int k = 0; k < 12; ++k)
    {
        vocalHist[(size_t) k].store (hist[(size_t) k], std::memory_order_relaxed);
        refHist[(size_t) k].store (chroma.hist[(size_t) k], std::memory_order_relaxed);
        refBass[(size_t) k].store (chroma.bassHist[(size_t) k], std::memory_order_relaxed);
    }
    riderDb = rider;
    vocalDb = toDb (vMs);
    musicDb = toDb (mMs);
    musicPresent = toDb (mMs) > -62.0f;
    vocalActive = anyActive;
    compGr = minComp;
    deessGr = -deMax;
    for (int b = 0; b < 3; ++b) clarityDb[(size_t) b] = clarNow[b];
}

cl::KeyResult ClarionProcessor::vocalKey() const
{
    std::array<float, 12> h;
    for (int i = 0; i < 12; ++i) h[(size_t) i] = vocalHist[(size_t) i].load();
    return cl::findKey (h);
}

cl::KeyResult ClarionProcessor::refKey() const
{
    std::array<float, 12> h;
    for (int i = 0; i < 12; ++i) h[(size_t) i] = refHist[(size_t) i].load();
    return cl::findKey (h);
}

int ClarionProcessor::refRoot() const
{
    // The root is the reference key's tonic or its relative, whichever the bass register leans on.
    const auto rk = refKey();
    if (rk.tonic < 0) return -1;
    const int a = rk.tonic, b = rk.minor ? (rk.tonic + 3) % 12 : (rk.tonic + 9) % 12;
    float ba = refBass[(size_t) a].load(), bb = refBass[(size_t) b].load();
    if (ba + bb <= 1e-9f) { ba = refHist[(size_t) a].load(); bb = refHist[(size_t) b].load(); }
    return bb > ba ? b : a;
}

int ClarionProcessor::suggestedShift() const
{
    const auto v = vocalKey();
    const int current = juce::roundToInt (apvts.getRawParameterValue (cp::shift)->load());
    // The vocal key is measured before shifting, so the suggestion is absolute.
    juce::ignoreUnused (current);
    if ((int) apvts.getRawParameterValue (cp::matchMode)->load() == 1)
        return cl::rootShift (v.tonic, refRoot());
    return cl::keyShift (v, refKey());
}

void ClarionProcessor::matchKey()
{
    const int s = suggestedShift();
    if (auto* p = apvts.getParameter (cp::shift))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) s));
        p->endChangeGesture();
    }
    lastMatch = s;
    matched = true;
}

void ClarionProcessor::pollLearn()
{
    if (autoEqState.load() != 2) return;
    auto set = [this] (const char* id, float v)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    };
    set (cp::hpf, eqResult.hpf);
    set (cp::mud, eqResult.mud);
    set (cp::presence, eqResult.presence);
    set (cp::air, eqResult.air);
    hasAutoEq = true;
    autoEqState = 3;
}

void ClarionProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("autoEq", hasAutoEq.load(), nullptr);
    st.setProperty ("vLong", vLong, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void ClarionProcessor::setStateInformation (const void* data, int size)
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
            hasAutoEq = (bool) tree.getProperty ("autoEq", false);
            vLong = (float) tree.getProperty ("vLong", -24.0f);
            if (hasAutoEq) autoEqState = 3;
        }
}

juce::AudioProcessorEditor* ClarionProcessor::createEditor() { return new ClarionEditor (*this); }

#if ! CLARION_NO_FACTORY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ClarionProcessor(); }
#endif
