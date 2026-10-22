#pragma once
#include "Stems.h"

// Arc 2 arrangement plan: for every lane and every bar, whether it plays, its gain and filter ramps,
// and what each 16th plays (which slice of the loop, reversed, stuttered or muted). Everything the
// renderer does comes from this, so the timeline can draw exactly what you will hear.
namespace arc2
{
enum SecType { sIntro = 0, sGroove, sBuild, sDrop, sBreak, sOutro };
inline const char* secName (int t) { static const char* n[] { "INTRO", "GROOVE", "BUILD", "DROP", "BREAK", "OUTRO" }; return n[juce::jlimit (0, 5, t)]; }
inline juce::Colour secColour (int t)
{
    static const juce::uint32 c[] { 0xff3a4a5c, 0xff3d5a4a, 0xff6a5330, 0xff7a3340, 0xff3a3f6e, 0xff4a3a5c };
    return juce::Colour (c[juce::jlimit (0, 5, t)]);
}

struct SectionDef { int type, bars; };
struct Style { const char* name; std::vector<SectionDef> sections; };

inline const std::vector<Style>& styles()
{
    static const std::vector<Style> s {
        { "Club",     { { sIntro, 16 }, { sGroove, 16 }, { sBuild, 8 }, { sDrop, 32 }, { sBreak, 16 }, { sBuild, 8 }, { sDrop, 32 }, { sOutro, 16 } } },
        { "Techno",   { { sIntro, 16 }, { sGroove, 32 }, { sBreak, 16 }, { sBuild, 8 }, { sDrop, 32 }, { sOutro, 16 } } },
        { "Melodic",  { { sIntro, 16 }, { sBuild, 8 }, { sDrop, 16 }, { sBreak, 24 }, { sBuild, 8 }, { sDrop, 32 }, { sOutro, 16 } } },
        { "Afro",     { { sIntro, 16 }, { sGroove, 32 }, { sBreak, 12 }, { sBuild, 4 }, { sDrop, 32 }, { sOutro, 16 } } },
        { "Radio",    { { sIntro, 8 }, { sBuild, 4 }, { sDrop, 16 }, { sBreak, 8 }, { sBuild, 4 }, { sDrop, 16 }, { sOutro, 8 } } },
        { "DJ Tool",  { { sIntro, 32 }, { sDrop, 32 }, { sBreak, 8 }, { sDrop, 32 }, { sOutro, 32 } } },
    };
    return s;
}

struct SliceOp
{
    int16_t src = 0;        // first 16th of the source region (in the lane's loop)
    uint8_t span = 1;       // region length in 16ths (reverse works over the whole region)
    uint8_t phase = 0;      // which 16th of the region this step is
    uint8_t rep = 1;        // >1: retrigger the start of the slice rep times in this 16th
    bool rev = false, mute = false;
    float gain = 1;
    bool operator== (const SliceOp& o) const { return src == o.src && span == o.span && phase == o.phase && rep == o.rep && rev == o.rev && mute == o.mute && gain == o.gain; }
};

enum Fill : uint8_t { fNone = 0, fRoll, fStutter, fReverse, fDropout, fRepeat, fResponse, fSweep, fEnter };
inline const char* fillName (int f)
{
    static const char* n[] { "", "roll", "stutter", "reverse", "drop-out", "repeat", "response", "sweep", "enters" };
    return n[juce::jlimit (0, 8, f)];
}

struct BarPlan
{
    bool on = false;
    float g0 = 1, g1 = 1;                  // gain ramp across the bar
    float lp0 = 20000, lp1 = 20000;        // low-pass ramp (Hz)
    float hp0 = 20, hp1 = 20;              // high-pass ramp (Hz)
    std::array<SliceOp, 16> steps;
    uint8_t fill = fNone;
};

struct LanePlan { std::vector<BarPlan> bars; };

struct Section { int type, start, bars; };

struct Plan
{
    std::vector<Section> sections;
    std::vector<LanePlan> lanes;
    int totalBars = 0;
    int sectionAt (int bar) const { for (int i = 0; i < (int) sections.size(); ++i) if (bar >= sections[(size_t) i].start && bar < sections[(size_t) i].start + sections[(size_t) i].bars) return i; return -1; }
};

// what Plan needs to know about each lane
struct LaneInfo
{
    int role = rDrums, loopBars = 1;
    uint32_t seed = 1;
    std::array<float, 16> stepPeak {};     // loudest moment in each 16th of the loop's first bar (for picking roll hits)
    int strongestBackbeatStep = 4;         // 16th in bar 1 with the strongest hit on beats 2/4 (claps/snares)
};

inline LaneInfo laneInfoOf (const Stem& s)
{
    LaneInfo li;
    li.role = s.role; li.seed = s.seed;
    if (s.audio == nullptr) return li;
    li.loopBars = s.audio->bars;
    const auto& b = s.audio->loop;
    const int bar = b.getNumSamples() / juce::jmax (1, li.loopBars), step = juce::jmax (1, bar / 16);
    for (int k = 0; k < 16; ++k)
        for (int i = k * step; i < juce::jmin ((k + 1) * step, b.getNumSamples()); ++i)
            li.stepPeak[(size_t) k] = juce::jmax (li.stepPeak[(size_t) k], std::abs (b.getSample (0, i)) + std::abs (b.getSample (1, i)));
    // strongest hit near beats 2 / 4, else the strongest anywhere
    int best = -1; float bv = 0;
    for (int k : { 4, 12, 5, 13, 3, 11 }) if (li.stepPeak[(size_t) k] > bv * 1.15f) { bv = li.stepPeak[(size_t) k]; best = k; }
    if (best < 0 || bv < 0.05f) { best = 0; for (int k = 1; k < 16; ++k) if (li.stepPeak[(size_t) k] > li.stepPeak[(size_t) best]) best = k; }
    li.strongestBackbeatStep = best;
    return li;
}

// ---------------------------------------------------------------- building blocks
inline void setNormal (BarPlan& b, int loopBars, int bar)
{
    const int base = (bar % juce::jmax (1, loopBars)) * 16;
    for (int i = 0; i < 16; ++i) b.steps[(size_t) i] = SliceOp { (int16_t) (base + i), 1, 0, 1, false, false, 1 };
}
inline void muteSteps (BarPlan& b, int from, int to) { for (int i = juce::jmax (0, from); i < juce::jmin (16, to); ++i) b.steps[(size_t) i].mute = true; }
inline void reverseBeat (BarPlan& b, int beat)
{
    const int s0 = beat * 4, src = b.steps[(size_t) s0].src;
    for (int i = 0; i < 4; ++i) b.steps[(size_t) (s0 + i)] = SliceOp { (int16_t) src, 4, (uint8_t) i, 1, true, false, 1 };
}
inline void repeatBeat (BarPlan& b, int dstBeat, int srcBeat)
{
    for (int i = 0; i < 4; ++i) b.steps[(size_t) (dstBeat * 4 + i)] = b.steps[(size_t) (srcBeat * 4 + i)];
}
inline void stutter (BarPlan& b, int fromStep, int toStep, int srcStep, int rep, float g0 = 1, float g1 = 1)
{
    for (int i = fromStep; i < toStep; ++i)
    {
        const float t = toStep - fromStep > 1 ? (float) (i - fromStep) / (toStep - fromStep - 1) : 1;
        b.steps[(size_t) i] = SliceOp { (int16_t) srcStep, 1, 0, (uint8_t) rep, false, false, g0 + (g1 - g0) * t };
    }
}
// hits every `every` 16ths from one slice, rest silent (clap / snare rolls)
inline void roll (BarPlan& b, int srcStep, int every, int rep, float g0, float g1)
{
    for (int i = 0; i < 16; ++i)
    {
        const float t = i / 15.0f;
        SliceOp o { (int16_t) srcStep, 1, 0, (uint8_t) rep, false, false, g0 + (g1 - g0) * t };
        o.mute = (i % every) != 0;
        b.steps[(size_t) i] = o;
    }
}
// re-order the bar's beats (a "response" to the bar before), keeping beat 1 so it still lands
inline void respond (BarPlan& b, juce::Random& r)
{
    auto orig = b.steps;
    static const int perms[][4] { { 0, 2, 1, 3 }, { 0, 1, 0, 3 }, { 0, 3, 2, 1 }, { 0, 0, 2, 2 }, { 0, 1, 3, 3 }, { 0, 2, 2, 3 } };
    const auto& p = perms[r.nextInt (6)];
    for (int beat = 0; beat < 4; ++beat)
        for (int i = 0; i < 4; ++i) b.steps[(size_t) (beat * 4 + i)] = orig[(size_t) (p[beat] * 4 + i)];
    if (r.nextFloat() < 0.4f) muteSteps (b, 14, 16);
}

inline float lerpExp (float a, float b, float t) { return a * std::pow (b / a, t); }

// ---------------------------------------------------------------- the arranger
struct PlanSettings { int style = 0; float variation = 0.6f; };

inline Plan buildPlan (const std::vector<LaneInfo>& lanes, const PlanSettings& ps)
{
    Plan p;
    const auto& st = styles()[(size_t) juce::jlimit (0, (int) styles().size() - 1, ps.style)];
    int at = 0;
    for (auto& s : st.sections) { p.sections.push_back ({ s.type, at, s.bars }); at += s.bars; }
    p.totalBars = at;
    const float var = juce::jlimit (0.0f, 1.0f, ps.variation);

    bool hasClap = false, hasVocal = false, hasLead = false;
    for (auto& l : lanes) { hasClap |= l.role == rClap; hasVocal |= l.role == rVocal; hasLead |= l.role == rLead; }
    // which lane carries the build roll: a clap lane, else the full drum loop / tops
    int rollLane = -1;
    for (int i = 0; i < (int) lanes.size() && rollLane < 0; ++i) if (lanes[(size_t) i].role == rClap) rollLane = i;
    for (int i = 0; i < (int) lanes.size() && rollLane < 0; ++i) if (lanes[(size_t) i].role == rDrums) rollLane = i;
    for (int i = 0; i < (int) lanes.size() && rollLane < 0; ++i) if (lanes[(size_t) i].role == rTops || lanes[(size_t) i].role == rPerc) rollLane = i;

    for (int li = 0; li < (int) lanes.size(); ++li)
    {
        const auto& L = lanes[(size_t) li];
        const int role = L.role;
        juce::Random r ((juce::int64) L.seed * 1000003 + role * 7919 + 11);
        LanePlan lp;
        lp.bars.resize ((size_t) p.totalBars);
        // per-lane entry points (in whole 4-bar steps), so each GENERATE builds intros/outros differently
        auto jitter4 = [&r, var] (int base, int lo, int hi) { const int j = r.nextFloat() < var ? (r.nextInt (3) - 1) * 4 : 0; return juce::jlimit (lo, hi, base + j); };

        for (int si = 0; si < (int) p.sections.size(); ++si)
        {
            const auto& S = p.sections[(size_t) si];
            const bool nextIsDrop = si + 1 < (int) p.sections.size() && p.sections[(size_t) si + 1].type == sDrop;
            const bool prevIsBuild = si > 0 && p.sections[(size_t) si - 1].type == sBuild;
            const int half = S.bars / 2, q3 = S.bars * 3 / 4;
            // entry / exit bars for intro & outro
            int enter = 0, exit = S.bars;
            if (S.type == sIntro)
            {
                switch (role)
                {
                    case rKick: case rDrums: enter = 0; break;
                    case rHats:   enter = jitter4 (S.bars >= 16 ? 4 : 0, 0, S.bars - 4); break;
                    case rPerc:   enter = jitter4 (half, 4, S.bars - 4); break;
                    case rClap:   enter = jitter4 (half, 4, S.bars - 4); break;
                    case rTops:   enter = jitter4 (q3, 4, S.bars - 4); break;
                    case rChords: enter = jitter4 (half, 0, S.bars - 4); break;
                    case rBass:   enter = S.bars >= 16 && r.nextFloat() < 0.5f * var ? S.bars - 4 : S.bars; break;
                    case rFx:     enter = S.bars - 1; break;
                    default:      enter = S.bars; break;   // lead / vocal wait for the music
                }
            }
            if (S.type == sOutro)
            {
                switch (role)
                {
                    case rKick: case rDrums: exit = S.bars; break;
                    case rHats:   exit = jitter4 (q3, 4, S.bars); break;
                    case rPerc: case rTops: exit = jitter4 (half, 4, S.bars); break;
                    case rClap:   exit = jitter4 (half, 4, S.bars); break;
                    case rBass:   exit = jitter4 (half, 0, S.bars); break;
                    case rChords: exit = jitter4 (half, 0, S.bars); break;
                    case rLead: case rVocal: exit = r.nextFloat() < 0.4f ? 4 : 0; break;
                    default:      exit = 0; break;
                }
            }

            for (int k = 0; k < S.bars; ++k)
            {
                const int bar = S.start + k;
                auto& B = lp.bars[(size_t) bar];
                setNormal (B, L.loopBars, bar);
                const float t0 = (float) k / S.bars, t1 = (float) (k + 1) / S.bars;
                const bool phraseEnd = (k % 8) == 7, halfPhraseEnd = (k % 4) == 3;
                const bool lastBar = k == S.bars - 1;
                bool on = true;

                switch (S.type)
                {
                    case sIntro:
                        on = k >= enter;
                        if (on && k == enter && k > 0) B.fill = fEnter;
                        if (on && role == rChords) { B.lp0 = lerpExp (500, 9000, (float) (k - enter) / juce::jmax (1, S.bars - enter)); B.lp1 = lerpExp (500, 9000, (float) (k + 1 - enter) / juce::jmax (1, S.bars - enter)); }
                        if (on && role == rBass) { B.hp0 = B.hp1 = 180; }
                        if (on && role == rFx) { B.g0 = 0.2f; B.g1 = 1; }
                        break;
                    case sGroove:
                        on = role != rLead && role != rFx && (role != rVocal || k >= half);
                        if (role == rChords) { B.lp0 = lerpExp (3000, 12000, t0); B.lp1 = lerpExp (3000, 12000, t1); }
                        break;
                    case sBuild:
                        on = role != rVocal || k >= half;
                        if (role == rLead) on = k >= half || S.bars <= 4;
                        if (role == rBass || role == rChords || role == rLead) { B.hp0 = lerpExp (20, role == rBass ? 450 : 700, t0); B.hp1 = lerpExp (20, role == rBass ? 450 : 700, t1); B.fill = k == 0 ? fSweep : fNone; }
                        if (role == rFx) { B.g0 = 0.25f + 0.75f * t0; B.g1 = 0.25f + 0.75f * t1; B.hp0 = lerpExp (60, 600, t0); B.hp1 = lerpExp (60, 600, t1); }
                        if (li == rollLane)
                        {
                            // clap / snare roll speeding up through the build
                            const int left = S.bars - k;
                            const int every = left > S.bars / 2 ? 4 : left > 2 ? 2 : 1;
                            const int rep = left == 1 ? 2 : 1;
                            if (role == rClap || left <= juce::jmax (2, S.bars / 2))
                            {
                                roll (B, (bar % juce::jmax (1, L.loopBars)) * 16 + L.strongestBackbeatStep, every, rep, 0.55f + 0.45f * t0, 0.55f + 0.45f * t1);
                                B.fill = fRoll;
                            }
                        }
                        if ((role == rKick || role == rDrums) && lastBar && li != rollLane) { on = r.nextFloat() < 0.5f; if (on) muteSteps (B, 8, 16); B.fill = fDropout; }
                        if (role == rBass && lastBar) { on = false; }
                        if (role == rVocal && lastBar) { stutter (B, 8, 16, B.steps[0].src, r.nextBool() ? 2 : 4, 0.8f, 1.0f); B.fill = fStutter; }
                        break;
                    case sDrop:
                        on = true;
                        if (role == rFx) on = k == 0;   // impact / tail on the downbeat of the drop
                        break;
                    case sBreak:
                        switch (role)
                        {
                            case rKick: case rDrums: case rClap: on = false; break;
                            case rHats: case rTops: on = S.bars >= 16 && k >= q3; if (on) { B.lp0 = B.lp1 = 3500; } break;
                            case rPerc: on = k >= half && r.nextFloat() < 0.5f + 0.5f * var; if (on) { B.lp0 = B.lp1 = 1800; } break;
                            case rBass: on = S.bars >= 16 && k >= q3; if (on) { B.lp0 = B.lp1 = 400; } break;
                            case rFx: on = k == 0; break;
                            default: on = true; break;
                        }
                        if (role == rChords && k < 2 && prevIsBuild) { B.lp0 = 2000; B.lp1 = 9000; }
                        break;
                    case sOutro:
                        on = k < exit;
                        if (on && role == rChords) { B.lp0 = lerpExp (9000, 600, t0); B.lp1 = lerpExp (9000, 600, t1); }
                        if (on && role == rBass && k >= exit - 4) { B.hp0 = lerpExp (20, 300, (float) (k - exit + 4) / 4); B.hp1 = lerpExp (20, 300, (float) (k - exit + 5) / 4); }
                        if (role == rFx) on = false;
                        break;
                    default: break;
                }

                // ---- variation inside full sections: fills on phrase ends, call and response
                const bool busy = S.type == sDrop || S.type == sGroove || (S.type == sIntro && k >= enter) || (S.type == sOutro && k < exit);
                if (on && busy && B.fill != fRoll)
                {
                    if (isDrumRole (role))
                    {
                        if (phraseEnd && r.nextFloat() < 0.35f + 0.65f * var)
                        {
                            const float pick = r.nextFloat();
                            const int hitStep = (bar % juce::jmax (1, L.loopBars)) * 16 + (role == rClap || role == rDrums ? L.strongestBackbeatStep : 12);
                            if (role == rClap && pick < 0.5f)        { roll (B, hitStep, 1, 1, 0.6f, 1.0f); for (int i = 0; i < 8; ++i) B.steps[(size_t) i] = SliceOp { (int16_t) ((bar % juce::jmax (1, L.loopBars)) * 16 + i), 1, 0, 1, false, false, 1 }; B.fill = fRoll; }
                            else if (role == rKick && pick < 0.45f)  { muteSteps (B, 12, 16); B.fill = fDropout; }
                            else if (pick < 0.35f)                   { stutter (B, 12, 16, B.steps[12].src, 2, 0.7f, 1.0f); B.fill = fStutter; }
                            else if (pick < 0.6f && role != rKick)   { reverseBeat (B, 3); B.fill = fReverse; }
                            else if (pick < 0.8f)                    { repeatBeat (B, 3, 2); stutter (B, 14, 16, B.steps[14].src, 2); B.fill = fRepeat; }
                            else if (role != rKick)                  { muteSteps (B, 12, 16); B.fill = fDropout; }
                        }
                        else if (halfPhraseEnd && ! phraseEnd && r.nextFloat() < 0.45f * var && role != rKick)
                        {
                            stutter (B, 14, 16, B.steps[14].src, 2); B.fill = fStutter;
                        }
                    }
                    else if (role == rLead || role == rVocal)
                    {
                        // call and response: lead and vocal trade 4-bar phrases in drops; on its own the lead answers itself
                        const bool second = ((k / 4) % 2) == 1;
                        if (S.type == sDrop && hasLead && hasVocal && r.nextFloat() < 0.4f + 0.6f * var)
                        {
                            const bool mine = role == rLead ? ! second : second;
                            if (! mine) on = false;
                        }
                        else if ((k % 4) >= 2 && r.nextFloat() < 0.25f + 0.6f * var)
                        {
                            respond (B, r); B.fill = fResponse;
                        }
                        if (on && phraseEnd && r.nextFloat() < 0.5f * var) { stutter (B, 12, 16, B.steps[12].src, 2, 0.8f, 1.0f); B.fill = fStutter; }
                    }
                    else if (role == rBass)
                    {
                        if (phraseEnd && r.nextFloat() < 0.3f + 0.6f * var)
                        {
                            if (r.nextBool()) { repeatBeat (B, 3, 2); B.fill = fRepeat; }
                            else { muteSteps (B, 12, 16); B.fill = fDropout; }
                        }
                        else if (halfPhraseEnd && r.nextFloat() < 0.4f * var) { respond (B, r); B.fill = fResponse; }
                    }
                    else if (role == rChords)
                    {
                        if (phraseEnd && r.nextFloat() < 0.5f * var) { stutter (B, 12, 16, B.steps[12].src, 2, 0.9f, 1.0f); B.fill = fStutter; }
                        else if (phraseEnd && r.nextFloat() < 0.4f * var) { reverseBeat (B, 3); B.fill = fReverse; }
                    }
                }

                // ---- the last beat before a drop is a breath: everything but the roll and FX stops
                if (on && lastBar && nextIsDrop && li != rollLane && role != rFx && role != rVocal) muteSteps (B, 12, 16);
                // ---- end of a drop going into a break: chords / lead reverse into it
                if (on && lastBar && S.type == sDrop && si + 1 < (int) p.sections.size() && p.sections[(size_t) si + 1].type == sBreak && (role == rChords || role == rLead) && r.nextFloat() < 0.5f + 0.5f * var)
                { reverseBeat (B, 3); B.fill = fReverse; }

                B.on = on;
            }
        }
        p.lanes.push_back (std::move (lp));
    }
    juce::ignoreUnused (hasClap);
    return p;
}
} // namespace arc2
