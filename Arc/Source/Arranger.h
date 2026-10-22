#pragma once
#include "Model.h"
#include <array>
#include <map>

// Turns four loops (drums, bass, chords, lead) into a full arrangement:
// intro, build, drop, break, build, drop, outro, with fills, call-and-response and variations.
namespace arc
{
enum SectionType { secIntro, secBuild, secDrop, secBreak, secOutro, secGroove };
inline const char* sectionName (int t) { static const char* n[] { "Intro", "Build", "Drop", "Break", "Outro", "Groove" }; return n[t]; }

struct Section { SectionType type; int bars; int startBar; int index; };

struct Template { const char* name; std::vector<std::pair<SectionType, int>> parts; };

inline const std::vector<Template>& templates()
{
    static const std::vector<Template> t {
        { "Club (House / Tech House)", { { secIntro, 16 }, { secBuild, 8 }, { secDrop, 32 }, { secBreak, 16 }, { secBuild, 8 }, { secDrop, 32 }, { secOutro, 16 } } },
        { "Techno (DJ length)",        { { secIntro, 32 }, { secGroove, 32 }, { secBreak, 16 }, { secBuild, 8 }, { secDrop, 32 }, { secGroove, 16 }, { secOutro, 32 } } },
        { "Melodic / Progressive",     { { secIntro, 16 }, { secBuild, 16 }, { secDrop, 32 }, { secBreak, 32 }, { secBuild, 16 }, { secDrop, 32 }, { secOutro, 16 } } },
        { "Afro / Organic House",      { { secIntro, 16 }, { secGroove, 16 }, { secBuild, 8 }, { secDrop, 32 }, { secBreak, 16 }, { secDrop, 32 }, { secOutro, 16 } } },
        { "Radio Edit",                { { secIntro, 8 }, { secBuild, 8 }, { secDrop, 16 }, { secBreak, 8 }, { secBuild, 4 }, { secDrop, 16 }, { secOutro, 8 } } },
    };
    return t;
}

struct Settings
{
    int templateIndex = 0;
    float variation = 0.6f;      // 0 = faithful loops, 1 = lots of change
    uint32_t seed = 1;
    std::array<bool, kNumLanes> enabled { true, true, true, true };
};

struct Tag { int bar; juce::String text; };

struct Arrangement
{
    std::vector<Section> sections;
    std::array<Clip, kNumLanes> lanes;
    std::array<std::vector<Tag>, kNumLanes> tags;
    std::vector<juce::String> tips;
    int totalBars = 0;
    Scale scale;
};

class Arranger
{
public:
    Arrangement build (const std::array<Clip, kNumLanes>& loops, const Settings& s)
    {
        rng = juce::Random ((juce::int64) s.seed * 7919 + 17);
        set = s;
        Arrangement a;
        std::vector<const Clip*> tonal;
        for (int l = laneBass; l < kNumLanes; ++l) if (! loops[(size_t) l].empty()) tonal.push_back (&loops[(size_t) l]);
        a.scale = keyOf (tonal);
        scale = a.scale;

        const auto& tp = templates()[(size_t) juce::jlimit (0, (int) templates().size() - 1, s.templateIndex)];
        int bar = 0, idx = 0;
        for (auto& p : tp.parts) { a.sections.push_back ({ p.first, p.second, bar, idx++ }); bar += p.second; }
        a.totalBars = bar;
        for (int l = 0; l < kNumLanes; ++l) a.lanes[(size_t) l].lengthBeats = bar * 4.0;

        int dropCount = 0;
        for (auto& sec : a.sections)
        {
            if (sec.type == secDrop) ++dropCount;
            for (int b = 0; b < sec.bars; ++b)
            {
                const int abs = sec.startBar + b;
                const BarCtx ctx { sec, b, abs, dropCount };
                if (s.enabled[laneDrums] && ! loops[laneDrums].empty())   emit (a, laneDrums, drumsBar (loops[laneDrums], ctx, a.tags[laneDrums]), abs);
                if (s.enabled[laneBass] && ! loops[laneBass].empty())     emit (a, laneBass, bassBar (loops[laneBass], ctx, a.tags[laneBass]), abs);
                if (s.enabled[laneChords] && ! loops[laneChords].empty()) emit (a, laneChords, chordsBar (loops[laneChords], ctx, a.tags[laneChords]), abs);
                if (s.enabled[laneLead] && ! loops[laneLead].empty())     emit (a, laneLead, leadBar (loops[laneLead], ctx, a.tags[laneLead]), abs);
            }
        }
        for (auto& c : a.lanes) c.sort();
        a.tips = tips (loops, a);
        return a;
    }

private:
    struct BarCtx { const Section& sec; int bar; int absBar; int dropNumber; };
    juce::Random rng;
    Settings set;
    Scale scale;

    bool chance (float p) { return rng.nextFloat() < p * set.variation * 1.25f; }

    static void emit (Arrangement& a, int lane, std::vector<Note> notes, int absBar)
    {
        for (auto& n : notes)
        {
            n.start += absBar * 4.0;
            n.velocity = juce::jlimit (0.05f, 1.0f, n.velocity);
            if (n.length > 0) a.lanes[(size_t) lane].notes.push_back (n);
        }
    }

    // the notes of a loop that fall in bar b of a section (loops tile)
    static std::vector<Note> loopBar (const Clip& c, int b)
    {
        const int loopBars = juce::jmax (1, (int) std::round (c.lengthBeats / 4.0));
        const int lb = b % loopBars;
        std::vector<Note> out;
        for (auto& n : c.notes)
            if (n.start >= lb * 4.0 - 1e-6 && n.start < lb * 4.0 + 4.0 - 1e-6)
            {
                Note m = n;
                m.start -= lb * 4.0;
                m.length = juce::jmin (m.length, 4.0 - m.start + 4.0);   // allow ties into the next bar
                out.push_back (m);
            }
        return out;
    }

    static void scaleVel (std::vector<Note>& v, float g) { for (auto& n : v) n.velocity *= g; }

    static void tag (std::vector<Tag>& t, int bar, const juce::String& s)
    {
        if (t.empty() || t.back().bar != bar) t.push_back ({ bar, s });
        else t.back().text << " + " << s;
    }

    // ------------------------------------------------------------ drums
    std::vector<Note> drumFill (int kind, float strength)
    {
        std::vector<Note> f;
        auto hit = [&] (int p, double t, float v) { Note n; n.pitch = p; n.start = t; n.length = 0.125; n.velocity = v; n.tag = "fill"; f.push_back (n); };
        switch (kind)
        {
            case 0: hit (gm::snare, 3.5, 0.7f * strength); hit (gm::snare, 3.75, 0.9f * strength); hit (gm::openHat, 3.5, 0.6f); break;           // snare pickup
            case 1: hit (gm::kick, 3.25, 0.8f); hit (gm::kick, 3.75, 0.9f); hit (gm::clap, 3.5, 0.8f * strength); break;                         // kick double
            case 2: hit (gm::hiTom, 3.0, 0.8f); hit (gm::midTom, 3.25, 0.85f); hit (gm::midTom, 3.5, 0.9f); hit (gm::lowTom, 3.75, 1.0f); break; // tom run
            case 3: for (int k = 0; k < 8; ++k) hit (gm::snare, 3.0 + k * 0.125, (0.4f + 0.075f * k) * strength); break;                       // 32nd roll
            default: hit (gm::openHat, 3.5, 0.8f); hit (gm::clap, 3.75, 0.7f * strength); break;
        }
        return f;
    }

    std::vector<Note> drumsBar (const Clip& loop, const BarCtx& c, std::vector<Tag>& tags)
    {
        auto n = loopBar (loop, c.bar);
        const int B = c.sec.bars, b = c.bar;
        auto keep = [&] (auto pred) { std::vector<Note> o; for (auto& x : n) if (pred (x)) o.push_back (x); n = o; };
        auto removeLastBeat = [&] { keep ([] (const Note& x) { return x.start < 3.0 - 1e-6; }); };
        const bool phrase4 = b % 4 == 3, phrase8 = b % 8 == 7, lastBar = b == B - 1;

        switch (c.sec.type)
        {
            case secIntro:
                if (b < B / 2) { keep ([] (const Note& x) { return isKick (x.pitch) || isHat (x.pitch); }); if (b == 0) tag (tags, c.absBar, "kick + hats"); }
                else if (b < B - 4) { keep ([] (const Note& x) { return ! isSnare (x.pitch); }); if (b == B / 2) tag (tags, c.absBar, "percussion in"); }
                else if (b == B - 4) tag (tags, c.absBar, "snare / clap in");
                if (lastBar) { removeLastBeat(); auto f = drumFill (0, 0.8f); n.insert (n.end(), f.begin(), f.end()); tag (tags, c.absBar, "fill"); }
                break;
            case secBuild:
                if (b >= B - 2)
                {
                    keep ([] (const Note& x) { return isHat (x.pitch); });
                    const bool final = lastBar;
                    const int perBeat = final ? 4 : 2;
                    for (int k = 0; k < 4 * perBeat; ++k)
                    {
                        if (final && k >= 4 * perBeat - 2) break;    // a breath before the drop
                        Note r; r.pitch = gm::snare; r.start = k / (double) perBeat; r.length = 0.1;
                        const float prog = ((float) (b - (B - 2)) * 4 * perBeat + k) / (8.0f * perBeat);
                        r.velocity = 0.35f + 0.6f * prog; r.tag = "roll";
                        n.push_back (r);
                    }
                    tag (tags, c.absBar, final ? "16th roll, kick out" : "8th roll");
                }
                else if (b == 0) tag (tags, c.absBar, "build");
                break;
            case secBreak:
                if (b < B / 2) { n.clear(); if (b == 0) tag (tags, c.absBar, "drums out"); }
                else
                {
                    keep ([] (const Note& x) { return ! isKick (x.pitch); });
                    if (b == B / 2) tag (tags, c.absBar, "hats + claps, no kick");
                    if (lastBar && B >= 8) { removeLastBeat(); auto f = drumFill (3, 0.9f); n.insert (n.end(), f.begin(), f.end()); tag (tags, c.absBar, "roll"); }
                }
                break;
            case secOutro:
                if (b >= B - 4) { keep ([] (const Note& x) { return isKick (x.pitch); }); if (b == B - 4) tag (tags, c.absBar, "kick only"); }
                else if (b >= B / 2) { keep ([] (const Note& x) { return isKick (x.pitch) || isHat (x.pitch); }); if (b == B / 2) tag (tags, c.absBar, "kick + hats"); }
                break;
            case secDrop:
            case secGroove:
            {
                const bool drop = c.sec.type == secDrop;
                if (b == 0 && drop) { Note cr; cr.pitch = gm::crash; cr.start = 0; cr.length = 1; cr.velocity = 0.9f; cr.tag = "crash"; n.push_back (cr); tag (tags, c.absBar, "crash"); }
                // ghost notes: soft snares on empty 16ths
                if (chance (0.5f))
                {
                    for (int k = 0; k < 16; ++k)
                    {
                        const double t = k * 0.25;
                        bool busy = false; for (auto& x : n) if (std::abs (x.start - t) < 0.01 && isSnare (x.pitch)) busy = true;
                        if (! busy && (k % 4 == 1 || k % 4 == 3) && rng.nextFloat() < 0.18f * set.variation)
                        { Note g; g.pitch = gm::snare; g.start = t; g.length = 0.1; g.velocity = 0.22f + 0.1f * rng.nextFloat(); g.tag = "ghost"; n.push_back (g); }
                    }
                }
                // hats: human velocity
                for (auto& x : n) if (isHat (x.pitch)) x.velocity *= 0.85f + 0.3f * rng.nextFloat();
                if (phrase8 || lastBar)
                {
                    removeLastBeat();
                    const int kind = rng.nextInt (4);
                    auto f = drumFill (kind == 1 ? 2 : kind, 1.0f);
                    n.insert (n.end(), f.begin(), f.end());
                    tag (tags, c.absBar, kind == 2 || kind == 1 ? "tom fill" : "big fill");
                }
                else if (phrase4 && chance (0.9f))
                {
                    keep ([] (const Note& x) { return x.start < 3.5 - 1e-6; });
                    auto f = drumFill (rng.nextBool() ? 0 : 4, 0.8f);
                    n.insert (n.end(), f.begin(), f.end());
                    tag (tags, c.absBar, "pickup");
                }
                if (! drop) scaleVel (n, 0.92f);
                break;
            }
        }
        return n;
    }

    // ------------------------------------------------------------ bass
    std::vector<Note> bassBar (const Clip& loop, const BarCtx& c, std::vector<Tag>& tags)
    {
        auto n = loopBar (loop, c.bar);
        const int B = c.sec.bars, b = c.bar;
        int root = 40;
        if (! n.empty()) { root = n.front().pitch; for (auto& x : n) if (x.start < 0.01) root = x.pitch; }
        else if (! loop.notes.empty()) root = loop.notes.front().pitch;

        switch (c.sec.type)
        {
            case secIntro:
                if (b < B - 8 || B < 12) { n.clear(); break; }
                // tease: root on the offbeats, low velocity, out for the last 2 bars
                n.clear();
                if (b < B - 2) { for (int k = 0; k < 4; ++k) { Note o; o.pitch = root; o.start = k + 0.5; o.length = 0.25; o.velocity = 0.5f; o.tag = "tease"; n.push_back (o); } if (b == B - 8) tag (tags, c.absBar, "offbeat tease"); }
                break;
            case secBuild:
            {
                n.clear();
                const bool lastBar = b == B - 1;
                for (int k = 0; k < 8; ++k)
                {
                    if (lastBar && k >= 6) break;                    // silence on the last beat
                    Note o; o.pitch = root + (lastBar && k >= 4 ? 12 : 0); o.start = k * 0.5; o.length = 0.4;
                    o.velocity = 0.45f + 0.5f * (float) (b * 8 + k) / (float) (B * 8); o.tag = "pulse";
                    n.push_back (o);
                }
                if (b == 0) tag (tags, c.absBar, "root pulse");
                if (lastBar) tag (tags, c.absBar, "octave lift, gap");
                break;
            }
            case secBreak:
            {
                n.clear();
                if (b >= B / 2 || c.sec.bars <= 8)
                {
                    Note o; o.pitch = root; o.start = 0; o.length = 4; o.velocity = 0.55f; o.tag = "sustain"; n.push_back (o);
                    if (b == (c.sec.bars <= 8 ? 0 : B / 2)) tag (tags, c.absBar, "held roots");
                }
                break;
            }
            case secOutro:
                if (b >= B / 2) n.clear();
                if (b == B / 2) tag (tags, c.absBar, "bass out");
                break;
            case secDrop:
            case secGroove:
            {
                const bool response = b % 4 == 3, big = b % 8 == 7;
                if (big && chance (0.9f))
                {
                    // syncopate the second half of the bar by a 16th
                    for (auto& x : n) if (x.start >= 2.0) x.start = juce::jmin (3.75, x.start + 0.25);
                    Note p; p.pitch = scale.step (root, -1); p.start = 3.75; p.length = 0.25; p.velocity = 0.8f; p.tag = "pickup"; n.push_back (p);
                    tag (tags, c.absBar, "syncopated + pickup");
                }
                else if (response && chance (0.85f))
                {
                    const int kind = rng.nextInt (3);
                    if (kind == 0) { int k = 0; for (auto& x : n) if (std::fmod (x.start, 1.0) > 0.01 && (k++ % 2 == 0)) { x.pitch += 12; x.tag = "octave"; } tag (tags, c.absBar, "octave answer"); }
                    else if (kind == 1 && ! n.empty()) { n.back().pitch = scale.step (n.back().pitch, rng.nextBool() ? 2 : -2); n.back().tag = "passing"; tag (tags, c.absBar, "passing note"); }
                    else { Note p; p.pitch = scale.step (root, 1); p.start = 3.5; p.length = 0.25; p.velocity = 0.75f; p.tag = "pickup";
                           for (auto it = n.begin(); it != n.end();) it = it->start >= 3.5 - 1e-6 ? n.erase (it) : it + 1;
                           n.push_back (p); tag (tags, c.absBar, "walk-up"); }
                }
                if (c.sec.type == secGroove) for (auto& x : n) x.velocity *= 0.9f;
                break;
            }
        }
        return n;
    }

    // ------------------------------------------------------------ chords
    static std::vector<std::vector<Note>> groupChords (const std::vector<Note>& n)
    {
        std::vector<std::vector<Note>> g;
        for (auto& x : n)
        {
            if (g.empty() || std::abs (g.back().front().start - x.start) > 0.06) g.push_back ({});
            g.back().push_back (x);
        }
        return g;
    }

    std::vector<Note> chordsBar (const Clip& loop, const BarCtx& c, std::vector<Tag>& tags)
    {
        auto n = loopBar (loop, c.bar);
        const int B = c.sec.bars, b = c.bar;
        auto groups = groupChords (n);
        auto sustainThin = [&] (float vel, bool thin)
        {
            std::vector<Note> o;
            for (size_t i = 0; i < groups.size(); ++i)
            {
                const double st = groups[i].front().start;
                const double en = i + 1 < groups.size() ? groups[i + 1].front().start : 4.0;
                auto g = groups[i];
                std::sort (g.begin(), g.end(), [] (const Note& x, const Note& y) { return x.pitch < y.pitch; });
                for (size_t k = 0; k < g.size(); ++k)
                {
                    if (thin && g.size() > 2 && k != 0 && k != g.size() - 1) continue;
                    Note m = g[k]; m.start = st; m.length = en - st; m.velocity = vel; m.tag = thin ? "thin" : "pad";
                    o.push_back (m);
                }
            }
            return o;
        };

        switch (c.sec.type)
        {
            case secIntro:
                if (b < B / 2 && c.sec.index == 0 && set.templateIndex != 2) { n.clear(); break; }
                n = sustainThin (0.55f, true);
                if (b == (set.templateIndex == 2 ? 0 : B / 2)) tag (tags, c.absBar, "thin sustained voicing");
                break;
            case secBuild:
            {
                std::vector<Note> o;
                for (size_t i = 0; i < groups.size(); ++i)
                {
                    const double st = groups[i].front().start;
                    const double en = i + 1 < groups.size() ? groups[i + 1].front().start : 4.0;
                    for (double t = std::ceil (st * 2 - 1e-6) / 2; t < en - 1e-6; t += 0.5)
                    {
                        if (b == B - 1 && t >= 3.0) break;
                        for (auto& g : groups[i]) { Note m = g; m.start = t; m.length = 0.3; m.velocity = 0.4f + 0.55f * (float) (b * 4 + t) / (float) (B * 4); m.tag = "stab"; o.push_back (m); }
                    }
                }
                n = o;
                if (b == 0) tag (tags, c.absBar, "8th stabs rising");
                break;
            }
            case secBreak:
            {
                n = sustainThin (0.7f, false);
                std::vector<Note> add;
                for (auto& g : groups) if (! g.empty()) { auto top = *std::max_element (g.begin(), g.end(), [] (const Note& x, const Note& y) { return x.pitch < y.pitch; }); top.pitch += 12; top.velocity = 0.45f; top.tag = "octave";
                    const double st = top.start; double en = 4.0; for (auto& h : groups) if (h.front().start > st + 0.06) { en = h.front().start; break; } top.length = en - st; add.push_back (top); }
                n.insert (n.end(), add.begin(), add.end());
                if (b == 0) tag (tags, c.absBar, "wide pad + octave");
                break;
            }
            case secOutro:
                n = sustainThin (0.6f * (1.0f - 0.6f * (float) b / (float) B), true);
                if (b == 0) tag (tags, c.absBar, "fading sustain");
                break;
            case secDrop:
            case secGroove:
                if (b % 8 == 7 && chance (0.8f))
                {
                    // revoice: top note of each chord drops an octave (or the bottom goes up)
                    std::vector<Note> o;
                    for (auto& g : groups)
                    {
                        auto v = g;
                        std::sort (v.begin(), v.end(), [] (const Note& x, const Note& y) { return x.pitch < y.pitch; });
                        if (v.size() >= 2) { if (rng.nextBool()) v.back().pitch -= 12; else v.front().pitch += 12; }
                        for (auto& x : v) { x.tag = "voicing"; o.push_back (x); }
                    }
                    n = o;
                    tag (tags, c.absBar, "inversion");
                }
                if (c.sec.type == secGroove) scaleVel (n, 0.85f);
                break;
        }
        return n;
    }

    // ------------------------------------------------------------ lead (call and response)
    std::vector<Note> leadBar (const Clip& loop, const BarCtx& c, std::vector<Tag>& tags)
    {
        const int B = c.sec.bars, b = c.bar;
        const int loopBars = juce::jmax (1, (int) std::round (loop.lengthBeats / 4.0));
        const int phraseBars = juce::jmax (2, loopBars);         // call = one loop; response = the next loop's worth
        auto n = loopBar (loop, c.bar);

        switch (c.sec.type)
        {
            case secIntro:
                n.clear();
                break;
            case secBuild:
                if (b < B / 2) { n.clear(); break; }
                // tease the first notes of the hook every other bar
                if ((b - B / 2) % 2 == 0) { if (n.size() > 2) n.resize (2); for (auto& x : n) { x.velocity *= 0.7f; x.tag = "tease"; } if (b == B / 2) tag (tags, c.absBar, "hook tease"); }
                else n.clear();
                break;
            case secBreak:
            {
                // the hook at half speed: notes of loop bar (b/2) stretched x2
                const auto src = loopBar (loop, b / 2);
                n.clear();
                const double off = (b % 2) * 4.0;
                for (auto x : src) { x.start = x.start * 2.0 - off; x.length *= 2.0; if (x.start >= 0 && x.start < 4.0) { x.velocity *= 0.75f; x.tag = "half-time"; n.push_back (x); } }
                if (b == 0) tag (tags, c.absBar, "hook at half speed");
                break;
            }
            case secOutro:
                n.clear();
                break;
            case secDrop:
            case secGroove:
            {
                const bool inResponse = (b / phraseBars) % 2 == 1;
                if (inResponse && chance (0.95f))
                {
                    // answer: the call moved by a scale step, last note resolving to the tonic
                    const int stepBy = (c.absBar / (phraseBars * 2)) % 2 == 0 ? -2 : 2;
                    for (auto& x : n) { x.pitch = scale.step (x.pitch, stepBy); x.tag = "response"; }
                    const bool phraseEnd = (b % phraseBars) == phraseBars - 1;
                    if (phraseEnd && ! n.empty())
                    {
                        auto& last = n.back();
                        int best = last.pitch, bd = 99;
                        for (int p = last.pitch - 6; p <= last.pitch + 6; ++p) if (((p - scale.tonic) % 12 + 12) % 12 == 0 && std::abs (p - last.pitch) < bd) { bd = std::abs (p - last.pitch); best = p; }
                        last.pitch = best; last.length = juce::jmax (last.length, 0.5);
                    }
                    if (b % phraseBars == 0) tag (tags, c.absBar, "response");
                }
                else if (b % phraseBars == 0) tag (tags, c.absBar, "call");
                // second drop: lift the second half an octave
                if (c.dropNumber >= 2 && b >= B / 2 && c.sec.type == secDrop) { for (auto& x : n) x.pitch += 12; if (b == B / 2) tag (tags, c.absBar, "octave up"); }
                break;
            }
        }
        return n;
    }

    // ------------------------------------------------------------ advice
    std::vector<juce::String> tips (const std::array<Clip, kNumLanes>& loops, const Arrangement& a)
    {
        std::vector<juce::String> t;
        t.push_back ("Key: " + juce::String (cl::noteName (a.scale.tonic)) + (a.scale.minor ? " minor" : " major") + ". Response phrases and passing notes stay in this scale.");
        for (int l = 0; l < kNumLanes; ++l)
            if (! loops[(size_t) l].empty() && loops[(size_t) l].lengthBeats <= 4.0)
                t.push_back (juce::String (laneName (l)) + " loop is 1 bar: variations at every 4th and 8th bar stop it sounding stuck.");
        if (loops[laneLead].empty() && ! loops[laneChords].empty())
            t.push_back ("No lead: drop a hook (or vocal) on the Lead lane to get call-and-response phrases.");
        if (! loops[laneBass].empty() && ! loops[laneDrums].empty())
        {
            int kickBass = 0, kicks = 0;
            for (auto& k : loops[laneDrums].notes) if (isKick (k.pitch)) { ++kicks; for (auto& b : loops[laneBass].notes) if (std::abs (std::fmod (b.start, loops[laneDrums].lengthBeats) - k.start) < 0.05) { ++kickBass; break; } }
            if (kicks > 0 && kickBass * 2 > kicks) t.push_back ("Bass lands on most kicks: try Interlock on the bass, or move bass notes to the offbeats.");
        }
        t.push_back ("Total " + juce::String (a.totalBars) + " bars. Drag each lane into Live at bar 1 to lay out the whole track.");
        return t;
    }
};
} // namespace arc
