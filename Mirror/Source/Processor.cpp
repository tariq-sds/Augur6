#include "Processor.h"
#include "Editor.h"

namespace
{
constexpr float kTonalThreshold = 0.5f;
constexpr int kBridgeIn = 9137;    // Mirror -> Bridge
constexpr int kBridgeOut = 9138;   // Bridge -> Mirror
const float kToneHz[10] { 31, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

juce::String dbStr (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
}

MirrorProcessor::MirrorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MIRROR", [] {
          juce::AudioProcessorValueTreeState::ParameterLayout l;
          auto F = [&l] (const char* id, const char* name, float lo, float hi, float def, std::function<juce::String (float)> fmt, float step = 0)
          {
              l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, step), def,
                  juce::AudioParameterFloatAttributes().withStringFromValueFunction ([fmt] (float v, int) { return fmt (v); })));
          };
          l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mp::mode, 1 }, "Mode", juce::StringArray { "Master", "Stem" }, 1));
          l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mp::role, 1 }, "Role", mr::roleNames(), 0));
          l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { mp::lock, 1 }, "Lock Level", false));
          F (mp::gain, "Mirror Gain", -24, 12, 0, dbStr);
          F (mp::transpose, "Transpose", -12, 12, 0, [] (float v) { const int s = juce::roundToInt (v); return (s > 0 ? "+" : "") + juce::String (s) + " st"; }, 1);
          F (mp::toneMatch, "Tone Match", 0, 1, 0.3f, [] (float v) { return juce::String (juce::roundToInt (v * 100)) + "%"; });
          F (mp::trim, "Master Trim", -24, 12, 0, dbStr);
          F (mp::headroom, "Headroom Target", -12, -3, -6, [] (float v) { return juce::String (v, 1) + " dBFS peak"; });
          l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mp::keySource, 1 }, "Key Source", juce::StringArray { "Project", "Reference" }, 0));
          l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { mp::listenBars, 1 }, "Listen Length", juce::StringArray { "8 bars", "16 bars", "32 bars" }, 1));
          return l; }())
{
    for (auto& t : toneGains) t = 0;
    startTimerHz (20);
}

MirrorProcessor::~MirrorProcessor()
{
    stopTimer();
    stopOsc();
    pool.removeAllJobs (true, 10000);
    if (slot >= 0) mr::Registry::get().release (slot);
}

bool MirrorProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainInputChannelSet() == juce::AudioChannelSet::mono());
}

void MirrorProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    analyzer.prepare (sampleRate);
    chroma.prepare ((float) sampleRate);
    vocoder.prepare();
    gainSmooth.reset (sampleRate, 0.05);
    trimSmooth.reset (sampleRate, 0.05);
    gainSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (mp::gain)->load()));
    trimSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (mp::trim)->load()));
    for (auto& b : toneEq) { b[0].reset(); b[1].reset(); }
}

void MirrorProcessor::updateTrackProperties (const TrackProperties& p)
{
    if (p.name.has_value() && p.name->isNotEmpty())
    {
        trackName = *p.name;
        if (slot >= 0) mr::Registry::get().setName (slot, trackName);
    }
}

void MirrorProcessor::ensureSlot()
{
    auto& reg = mr::Registry::get();
    if (! isMaster() && slot < 0)
    {
        slot = reg.claim();
        if (slot >= 0 && trackName.isNotEmpty()) reg.setName (slot, trackName);
        stopOsc();
    }
    else if (isMaster() && slot >= 0)
    {
        reg.release (slot);
        slot = -1;
    }
    if (isMaster() && ! oscInOk) startOsc();
}

void MirrorProcessor::startOsc()
{
    oscOutOk = oscOut.connect ("127.0.0.1", kBridgeIn);
    oscInOk = oscIn.connect (kBridgeOut);
    if (oscInOk) oscIn.addListener (this);
}

void MirrorProcessor::stopOsc()
{
    if (oscInOk) { oscIn.removeListener (this); oscIn.disconnect(); oscInOk = false; }
    if (oscOutOk) { oscOut.disconnect(); oscOutOk = false; }
}

void MirrorProcessor::oscMessageReceived (const juce::OSCMessage& m)
{
    const auto addr = m.getAddressPattern().toString();
    if (addr == "/bridge/hello") lastBridgeHello = juce::Time::getMillisecondCounterHiRes();
    else if (addr == "/bridge/fader" && m.size() >= 2 && m[0].isString())
    {
        const float v = m[1].isFloat32() ? m[1].getFloat32() : (m[1].isInt32() ? (float) m[1].getInt32() : 0.0f);
        std::lock_guard<std::mutex> lk (refMutex);
        faders[m[0].getString()] = v;
        lastBridgeHello = juce::Time::getMillisecondCounterHiRes();
    }
    else if (addr == "/bridge/result" && m.size() >= 2 && m[0].isString() && m[1].isString())
    {
        std::lock_guard<std::mutex> lk (refMutex);
        bridgeResults[m[0].getString()] = m[1].getString();
    }
    else if (addr == "/bridge/status" && m.size() >= 1 && m[0].isString())
    {
        std::lock_guard<std::mutex> lk (refMutex);
        status = "Live: " + m[0].getString();
    }
}

void MirrorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() < 2) return;
    if (getTotalNumInputChannels() == 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (auto b = pos->getBpm()) hostBpm = *b;
        }

    auto& reg = mr::Registry::get();
    const bool master = isMaster();
    const bool listening = reg.listening.load();

    // ---- analysis (pre-gain, i.e. what this track contributes before Mirror's own fader)
    // Stems only analyse while the master is listening, so between listens they cost almost nothing.
    if (master || listening) analyzer.push (L, R, n, listening);
    float pk = 0;
    double ms = 0;
    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (L[i] + R[i]);
        if (! master && listening) chroma.push (m, false);
        pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
        ms += (double) m * m;
    }
    if (listening)
    {
        if (reg.listenSeq.load() != lastListenSeq)
        {
            lastListenSeq = reg.listenSeq.load();
            analyzer.reset();
            chroma.hist.fill (0); chroma.bassHist.fill (0); chroma.resetPeakiness();
            sustainSum = sustainMax = 0; sustainCount = 0;
            peakAcc = 0;
        }
        const float rms = (float) std::sqrt (ms / n);
        sustainSum += rms; sustainMax = juce::jmax (sustainMax, rms); ++sustainCount;
        peakAcc = juce::jmax (peakAcc.load(), pk);
        if (master && hostPlaying.load()) listenedSamples += n;
    }
    if (wasListening && ! listening && ! master) publishPending = true;
    wasListening = listening;
    if (slot >= 0) reg.slots[slot].level = pk;

    if (master)
    {
        // tone match EQ + trim
        float g[10];
        for (int b = 0; b < 10; ++b) g[b] = cl::gOf (kToneHz[b], (float) sr);
        const float amt = apvts.getRawParameterValue (mp::toneMatch)->load();
        trimSmooth.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (mp::trim)->load()));
        float tg[10]; bool any = false;
        for (int b = 0; b < 10; ++b) { tg[b] = toneGains[(size_t) b].load() * amt; any |= std::abs (tg[b]) > 0.01f; }
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            if (any)
                for (int b = 0; b < 10; ++b)
                    if (std::abs (tg[b]) > 0.01f)
                    {
                        const float q = b == 0 || b == 9 ? 0.7f : 1.0f;
                        l = toneEq[b][0].bell (l, g[b], q, tg[b]);
                        r = toneEq[b][1].bell (r, g[b], q, tg[b]);
                    }
            const float t = trimSmooth.getNextValue();
            L[i] = l * t; R[i] = r * t;
        }
        float opk = 0;
        for (int i = 0; i < n; ++i) opk = juce::jmax (opk, std::abs (L[i]), std::abs (R[i]));
        mixPeakDb = juce::jmax (juce::Decibels::gainToDecibels (opk, -100.0f), mixPeakDb.load() - 0.05f);
        return;
    }

    // ---- stem: optional in-plugin transpose, then Mirror Gain
    const float semis = std::round (apvts.getRawParameterValue (mp::transpose)->load());
    if (std::abs (semis) > 0.5f)
    {
        const float ratio = std::exp2 (semis / 12.0f);
        // the vocoder smears short transients a little; a slow level match keeps the stem's loudness unchanged
        const float c = 1.0f - std::exp (-1.0f / (2.0f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            const float e = 0.5f * (L[i] * L[i] + R[i] * R[i]);
            vocoder.process (L[i], R[i], ratio);
            pvIn += (e - pvIn) * c;
            pvOut += (0.5f * (L[i] * L[i] + R[i] * R[i]) - pvOut) * c;
            if ((i & 63) == 0) pvGain = juce::jlimit (0.5f, 2.0f, std::sqrt ((pvIn + 1e-12f) / (pvOut + 1e-12f)));
            L[i] *= pvGain; R[i] *= pvGain;
        }
    }
    gainSmooth.setTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (mp::gain)->load()));
    for (int i = 0; i < n; ++i) { const float gg = gainSmooth.getNextValue(); L[i] *= gg; R[i] *= gg; }
}

void MirrorProcessor::publishStemAnalysis()
{
    if (slot < 0) return;
    auto& s = mr::Registry::get().slots[slot];
    const auto m = analyzer.mean();
    for (int k = 0; k < mr::kBands; ++k) s.power[(size_t) k] = m[(size_t) k];
    for (int k = 0; k < 12; ++k) s.chroma[(size_t) k] = chroma.hist[(size_t) k];

    // auto role: percussive material has a low average-to-peak level over time
    int role = (int) apvts.getRawParameterValue (mp::role)->load();
    if (role == mr::roleAuto)
    {
        const float sustain = sustainMax > 0 ? (sustainSum / juce::jmax (1, sustainCount)) / sustainMax : 1.0f;
        float low = 0, total = 0;
        for (int k = 0; k < mr::kBands; ++k) { total += m[(size_t) k]; if (mr::bandCentre (k) < 200) low += m[(size_t) k]; }
        float cmax = 0, csum = 0;
        for (float c : chroma.hist) { cmax = juce::jmax (cmax, c); csum += c; }
        const float peaky = csum > 0 ? cmax / (csum / 12.0f) : 0;
        const float tonal = analyzer.tonality();
        const float framePeaky = chroma.peakiness();
        if (std::getenv ("MIRROR_DEBUG")) std::printf ("[%s] framePeaky %.2f tonal %.2f peaky %.2f low %.2f sustain %.2f\n", trackName.toRawUTF8(), framePeaky, tonal, peaky, total > 0 ? low / total : 0, sustain);
        juce::ignoreUnused (peaky);
        // pitched material concentrates its energy in a few pitch classes; drums and noise don't
        const float lowShare = total > 0 ? low / total : 0.0f;
        const juce::String nm = trackName.toLowerCase();
        auto has = [&nm] (std::initializer_list<const char*> words) { for (auto* w : words) if (nm.contains (w)) return true; return false; };
        // 1) the track name, when it says what it is
        if (has ({ "drum", "kick", "snare", "clap", "hat", "perc", "shaker", "ride", "crash", "top", "loop", "beat", "tom" })) role = mr::roleDrums;
        else if (has ({ "fx", "riser", "impact", "sweep", "noise", "atmos", "transition" })) role = mr::roleFx;
        else if (has ({ "vocal", "vox", "voice", "acapella" })) role = mr::roleVocal;
        else if (has ({ "bass", "sub", "808" })) role = mr::roleBass;
        // 2) otherwise the sound: noisy, or punchy-and-low (a kit), means drums
        else if (tonal < kTonalThreshold || (sustain < 0.4f && framePeaky < 2.6f && lowShare > 0.5f)) role = mr::roleDrums;
        else if (lowShare > 0.6f) role = mr::roleBass;
        else role = mr::roleTonal;
        juce::ignoreUnused (sustain);
    }
    s.resolvedRole = role;
    s.analysisVersion = s.analysisVersion.load() + 1;
}

void MirrorProcessor::setParam (const char* id, float value)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value)));
        p->endChangeGesture();
    }
}

void MirrorProcessor::timerCallback()
{
    ensureSlot();
    auto& reg = mr::Registry::get();

    if (! isMaster() && slot >= 0)
    {
        auto& s = reg.slots[slot];
        s.heartbeat = juce::Time::getMillisecondCounterHiRes();
        s.role = (int) apvts.getRawParameterValue (mp::role)->load();
        s.locked = apvts.getRawParameterValue (mp::lock)->load() > 0.5f;
        s.gainDb = apvts.getRawParameterValue (mp::gain)->load();
        s.transpose = apvts.getRawParameterValue (mp::transpose)->load();
        if (publishPending.exchange (false)) publishStemAnalysis();

        // gain target from the master: glide there over ~1.5 s so you can watch it move
        if (s.targetSeq.load() != lastTargetSeq)
        {
            lastTargetSeq = s.targetSeq.load();
            rampFrom = s.gainDb.load(); rampTo = s.targetGainDb.load(); rampStep = 0;
        }
        if (rampStep >= 0)
        {
            ++rampStep;
            const float t = juce::jmin (1.0f, (float) rampStep / 30.0f);
            setParam (mp::gain, rampFrom + (rampTo - rampFrom) * (t * t * (3 - 2 * t)));
            if (t >= 1.0f) rampStep = -1;
        }
        if (s.transposeSeq.load() != lastTransposeSeq)
        {
            lastTransposeSeq = s.transposeSeq.load();
            setParam (mp::transpose, (float) s.targetTranspose.load());
        }
        const int lat = std::abs (s.transpose.load()) > 0.5f ? vocoder.latency() : 0;
        if (lat != currentLatency) { currentLatency = lat; setLatencySamples (lat); }
        return;
    }

    if (! isMaster()) return;

    // ---- master: listening
    if (reg.listening.load())
    {
        const float p = (float) listenedSamples.load() / (float) juce::jmax ((int64_t) 1, listenTarget);
        listenProgress = juce::jmin (1.0f, p);
        if (p >= 1.0f)
        {
            reg.listening = false;
            listeningNow = false;
            peakDuringListen = peakAcc.load();
            solveCountdown = 6;   // give the stems a moment to publish
            std::lock_guard<std::mutex> lk (refMutex);
            status = "Analysing...";
        }
    }
    if (solveCountdown > 0 && --solveCountdown == 0)
    {
        solveNow();
        if (applyAfterSolve) applyMatch();
        applyAfterSolve = false;
    }
    computeKeys();

    if (oscOutOk && reg.masters.load() >= 0)
    {
        static int ping = 0;
        if (++ping % 40 == 0) oscOut.send ("/mirror/hello", juce::String ("1.0"));
    }
}

void MirrorProcessor::loadReference (const juce::File& f)
{
    if (! f.existsAsFile()) return;
    referenceFile = f;
    refLoading = true;
    refProgress = 0;
    {
        std::lock_guard<std::mutex> lk (refMutex);
        status = "Analysing reference: " + f.getFileName();
    }
    pool.addJob ([this, f]
    {
        auto info = mr::analyseReference (f, &refProgress);
        std::lock_guard<std::mutex> lk (refMutex);
        ref = info;
        status = info.valid ? "Reference ready: " + info.name + ". Play your loudest section and press LISTEN & MATCH."
                            : "Could not read that file. Use WAV, AIFF, FLAC or MP3.";
        refLoading = false;
    });
}

void MirrorProcessor::startListen()
{
    auto& reg = mr::Registry::get();
    const int bars[] { 8, 16, 32 };
    const int nb = bars[(int) apvts.getRawParameterValue (mp::listenBars)->load()];
    listenTarget = (int64_t) (sr * 60.0 / juce::jlimit (40.0, 250.0, hostBpm.load()) * 4.0 * nb);
    listenedSamples = 0;
    reg.listenSeq = reg.listenSeq.load() + 1;
    reg.listening = true;
    listeningNow = true;
    applyAfterSolve = true;
    std::lock_guard<std::mutex> lk (refMutex);
    status = "Listening for " + juce::String (nb) + " bars - play your loudest section (it pauses when the transport stops)";
}

std::vector<StemView> MirrorProcessor::stems() const
{
    auto& reg = mr::Registry::get();
    std::vector<StemView> out;
    std::map<juce::String, float> fz;
    std::map<juce::String, juce::String> results;
    { std::lock_guard<std::mutex> lk (refMutex); fz = faders; results = bridgeResults; }
    for (int i = 0; i < mr::kMaxStems; ++i)
    {
        if (! reg.alive (i)) continue;
        auto& s = reg.slots[i];
        StemView v;
        v.slot = i;
        v.name = reg.nameOf (i);
        const int r = s.role.load();
        v.role = r == mr::roleAuto ? s.resolvedRole.load() : r;
        v.locked = s.locked.load();
        v.gainDb = s.gainDb.load();
        v.level = s.level.load();
        v.transpose = s.transpose.load();
        v.keyState = s.keyState.load();
        v.keyShift = s.keyShift.load();
        v.suggestedDb = s.targetGainDb.load();
        if (auto it = fz.find (v.name); it != fz.end()) { v.faderDb = it->second; v.faderKnown = true; }
        if (auto it = results.find (v.name); it != results.end()) v.bridgeMsg = it->second;
        v.changeDb = v.suggestedDb - v.gainDb;
        { std::lock_guard<std::mutex> lk (refMutex); if (auto it = lastSent.find (v.name); it != lastSent.end()) v.changeDb = it->second; }
        out.push_back (v);
    }
    return out;
}

void MirrorProcessor::computeKeys()
{
    auto& reg = mr::Registry::get();
    std::array<float, 12> sum {};
    std::vector<int> tonal;
    for (int i = 0; i < mr::kMaxStems; ++i)
    {
        if (! reg.alive (i) || reg.slots[i].analysisVersion.load() == 0) continue;
        const int r = reg.slots[i].role.load() == mr::roleAuto ? reg.slots[i].resolvedRole.load() : reg.slots[i].role.load();
        if (r == mr::roleDrums || r == mr::roleFx) { reg.slots[i].keyState = 3; continue; }
        float tot = 0;
        for (int k = 0; k < 12; ++k) tot += reg.slots[i].chroma[(size_t) k].load();
        if (tot <= 0) continue;
        for (int k = 0; k < 12; ++k) sum[(size_t) k] += reg.slots[i].chroma[(size_t) k].load() / tot;   // each stem one vote
        tonal.push_back (i);
    }
    cl::KeyResult key = cl::findKey (sum);
    {
        std::lock_guard<std::mutex> lk (refMutex);
        if ((int) apvts.getRawParameterValue (mp::keySource)->load() == 1 && ref.valid) key = ref.key;
        projKey = key;
    }
    for (int i : tonal)
    {
        std::array<float, 12> h;
        for (int k = 0; k < 12; ++k) h[(size_t) k] = reg.slots[i].chroma[(size_t) k].load();
        const auto fit = mr::fitToKey (h, key);
        // out of key when another transposition fits clearly better and the current fit is weak
        const bool out = fit.shift != 0 && fit.bestFit - fit.fit > 0.15f && fit.fit < 0.6f;
        reg.slots[i].keyState = out ? 2 : 1;
        reg.slots[i].keyShift = out ? fit.shift : 0;
    }
}

void MirrorProcessor::solveNow()
{
    auto& reg = mr::Registry::get();
    mr::ReferenceInfo r = reference();
    std::map<juce::String, float> fz;
    { std::lock_guard<std::mutex> lk (refMutex); fz = faders; }
    mr::SolveInput in;
    solveSlots.clear();
    for (int i = 0; i < mr::kMaxStems; ++i)
    {
        if (! reg.alive (i) || reg.slots[i].analysisVersion.load() == 0) continue;
        mr::Bands b;
        float tot = 0;
        for (int k = 0; k < mr::kBands; ++k) { b[(size_t) k] = reg.slots[i].power[(size_t) k].load(); tot += b[(size_t) k]; }
        if (tot <= 1e-12f) continue;   // silent during the listen: leave alone
        // the track's contribution includes its Live fader (if the bridge reports it)
        float post = reg.slots[i].gainDb.load();
        if (auto it = fz.find (reg.nameOf (i)); it != fz.end()) post += it->second;
        const float lin = std::pow (10.0f, post / 10.0f);
        for (auto& v : b) v *= lin;
        in.stems.push_back (b);
        in.locked.push_back (reg.slots[i].locked.load());
        in.start.push_back (0.0f);
        solveSlots.push_back (i);
    }
    juce::String st;
    if (! r.valid) st = "Load a reference track first.";
    else if (in.stems.empty()) st = "No stems heard. Put Mirror (Stem mode) on each track and play.";
    else
    {
        in.target = r.bands;
        auto res = mr::solveBalance (in);
        // residual tone curve for the master EQ (octave bands, +-6 dB)
        float off = 0;
        mr::shapeError (res.after, res.target, &off);
        for (int b = 0; b < 10; ++b)
        {
            float acc = 0, wsum = 0;
            for (int k = 0; k < mr::kBands; ++k)
            {
                const float d = std::abs (std::log2 (mr::bandCentre (k) / kToneHz[b]));
                if (d > 0.75f) continue;
                const float w = 1.0f - d / 0.75f;
                acc += w * (mr::toDb (res.target[(size_t) k]) - mr::toDb (res.after[(size_t) k]) + off);
                wsum += w;
            }
            toneGains[(size_t) b] = wsum > 0 ? juce::jlimit (-6.0f, 6.0f, acc / wsum) : 0.0f;
        }
        for (size_t j = 0; j < solveSlots.size(); ++j)
            reg.slots[solveSlots[j]].targetGainDb = res.gains[j];
        std::lock_guard<std::mutex> lk (refMutex);
        solve = res;
        st = juce::String ((int) in.stems.size()) + " stems analysed. Balance error " + juce::String (std::sqrt (res.errBefore), 1)
           + " dB -> " + juce::String (std::sqrt (res.errAfter), 1) + " dB.";
    }
    std::lock_guard<std::mutex> lk (refMutex);
    status = st;
}

void MirrorProcessor::applyMatch()
{
    auto& reg = mr::Registry::get();
    if (solveSlots.empty()) return;
    undoGains.clear();
    { std::lock_guard<std::mutex> lk (refMutex); lastSent.clear(); }
    const bool viaBridge = useBridge.load() && bridgeConnected() && oscOutOk;
    if (viaBridge) oscOut.send ("/mirror/begin", (juce::int32) juce::Random::getSystemRandom().nextInt (1 << 30));
    float before = 0, after = 0;
    for (size_t j = 0; j < solveSlots.size(); ++j)
    {
        const int i = solveSlots[j];
        auto& s = reg.slots[i];
        if (s.locked.load()) continue;
        const float target = s.targetGainDb.load();
        float pw = 0;
        for (auto& p : s.power) pw += p.load();
        const float cur = s.gainDb.load();
        before += pw * std::pow (10.0f, cur / 10.0f);
        if (viaBridge)
        {
            // move the real Live fader by the change; Mirror's own gain stays where it is
            oscOut.send ("/mirror/gain", reg.nameOf (i), target - cur);
            { std::lock_guard<std::mutex> lk (refMutex); lastSent[reg.nameOf (i)] = target - cur; }
            after += pw * std::pow (10.0f, target / 10.0f);
        }
        else
        {
            undoGains[i] = cur;
            s.targetGainDb = target;
            s.targetSeq = s.targetSeq.load() + 1;
            after += pw * std::pow (10.0f, target / 10.0f);
        }
    }
    if (viaBridge) oscOut.send ("/mirror/apply", (juce::int32) 1);

    // headroom: set the master trim so the predicted mix peak lands on the target
    if (peakDuringListen > 0 && before > 0)
    {
        const float predicted = juce::Decibels::gainToDecibels (peakDuringListen) + 10.0f * std::log10 (juce::jmax (1e-9f, after / before));
        const float targetPk = apvts.getRawParameterValue (mp::headroom)->load();
        setParam (mp::trim, juce::jlimit (-24.0f, 12.0f, targetPk - predicted));
    }
    std::lock_guard<std::mutex> lk (refMutex);
    status += viaBridge ? "  Live faders moved (Restore in the Bridge to undo)." : "  Applied with each stem's Mirror Gain (UNDO to revert).";
}

void MirrorProcessor::undoMatch()
{
    auto& reg = mr::Registry::get();
    for (auto& [i, g] : undoGains)
    {
        reg.slots[i].targetGainDb = g;
        reg.slots[i].targetSeq = reg.slots[i].targetSeq.load() + 1;
    }
    undoGains.clear();
}

void MirrorProcessor::restoreFaders()
{
    if (oscOutOk) oscOut.send ("/mirror/restore", (juce::int32) 1);
}

void MirrorProcessor::fixKeys (bool autoTranspose)
{
    auto& reg = mr::Registry::get();
    const bool viaBridge = useBridge.load() && bridgeConnected() && oscOutOk;
    for (int i = 0; i < mr::kMaxStems; ++i)
    {
        if (! reg.alive (i) || reg.slots[i].keyState.load() != 2) continue;
        const int shift = reg.slots[i].keyShift.load();
        if (viaBridge && ! autoTranspose)
            oscOut.send ("/mirror/transpose", reg.nameOf (i), (juce::int32) shift);   // clip transpose in Live
        else
        {
            reg.slots[i].targetTranspose = juce::roundToInt (reg.slots[i].transpose.load()) + shift;
            reg.slots[i].transposeSeq = reg.slots[i].transposeSeq.load() + 1;
        }
    }
    if (viaBridge && ! autoTranspose) oscOut.send ("/mirror/apply", (juce::int32) 2);
}

void MirrorProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = apvts.copyState();
    st.setProperty ("reference", referenceFile.getFullPathName(), nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void MirrorProcessor::setStateInformation (const void* data, int size)
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
            const juce::String path = tree.getProperty ("reference", "").toString();
            if (path.isNotEmpty() && isMaster()) loadReference (juce::File (path));
        }
}

juce::AudioProcessorEditor* MirrorProcessor::createEditor() { return new MirrorEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MirrorProcessor(); }
