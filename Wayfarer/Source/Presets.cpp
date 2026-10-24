#include "Presets.h"
#include "Params.h"

// Factory bank. Every patch starts from Init and lists only what differs.
// Octaves: 0=32' 1=16' 2=8' 3=4' 4=2' 5=1'.  Wave: 0 triangle, .333 saw, .667 square, 1 narrow pulse.
// Sources: 0 Tri 1 Square 2 S&H 3 Osc3 4 Noise 5 FiltEnv 6 AmpEnv 7 Pressure.  Shaping: 0 On 1 FiltEnv 2 Vel 3 Pressure.
// Destinations: 0 Pitch 1 Osc2 2 Osc3 3 Filter 4 Spacing 5 Wave 6 LFO Rate 7 Amp.
// Tempo rates: 0=4 bars 1=2 bars 2=1 bar 3=1/2 4=1/4. 5=1/4 6=1/4T 7=1/8. 8=1/8 9=1/8T 10=1/16 11=1/16T 12=1/32

using namespace pid;

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> bank
    {
        // ======================= BASS
        { "Wayfarer Bass", "Bass", { { volume, 0.0f },
            { o1Oct, 1 }, { o2Oct, 1 }, { o2Freq, 0.08f }, { o3Oct, 0 }, { o3Wave, 0.667f },
            { mix1, 0.9f }, { mix2, 0.8f }, { mix3, 0.5f }, { overload, 0.2f },
            { cutoff, 340 }, { res, 0.25f }, { kb, 0.4f }, { envAmt, 0.55f },
            { fD, 0.35f }, { fS, 0.2f }, { aD, 0.6f }, { aS, 0.85f } } },

        { "Taurus Floor", "Bass", { { volume, 0.0f },
            { o1Oct, 0 }, { o2Oct, 0 }, { o2Freq, 0.1f }, { o3Oct, 1 }, { o3Wave, 0.0f },
            { mix1, 0.9f }, { mix2, 0.85f }, { mix3, 0.4f }, { overload, 0.35f },
            { cutoff, 180 }, { res, 0.3f }, { envAmt, 0.6f }, { fA, 0.01f }, { fD, 1.2f }, { fS, 0.3f },
            { aR, 0.4f }, { glideOn, 1 }, { glide, 0.06f } } },

        { "Rubber Pluck", "Bass", { { volume, 0.0f },
            { o1Oct, 1 }, { o1Wave, 0.667f }, { o2Oct, 1 }, { o2Freq, -0.06f }, { mix2, 0.7f },
            { cutoff, 220 }, { res, 0.55f }, { envAmt, 0.7f }, { fD, 0.18f }, { fS, 0.0f },
            { aD, 0.35f }, { aS, 0.5f }, { aR, 0.08f } } },

        { "Acid Ladder", "Bass", { { volume, 0.0f },
            { o1Oct, 1 }, { mix1, 1.0f }, { overload, 0.45f },
            { cutoff, 300 }, { res, 0.78f }, { envAmt, 0.75f }, { fVel, 0.5f }, { fD, 0.22f }, { fS, 0.05f },
            { glideOn, 1 }, { glide, 0.08f } } },

        { "Sync Growl", "Bass", { { volume, 0.0f },
            { o1Oct, 1 }, { o2Oct, 2 }, { o2Freq, 5.0f }, { hardSync, 1 }, { mix1, 0.6f }, { mix2, 0.9f },
            { m2Src, 5 }, { m2Dest, 1 }, { m2Amt, 0.55f }, { overload, 0.5f },
            { cutoff, 900 }, { res, 0.2f }, { envAmt, 0.45f }, { fD, 0.4f }, { fS, 0.3f } } },

        { "Deep Sub", "Bass", { { volume, 0.0f },
            { o1Oct, 1 }, { o1Wave, 0.0f }, { mix1, 1.0f }, { o2Oct, 0 }, { o2Wave, 0.0f }, { mix2, 0.6f },
            { cutoff, 260 }, { envAmt, 0.15f }, { kb, 0.8f }, { aR, 0.2f }, { drift, 0.1f } } },

        { "FM Knock", "Bass", { { volume, 0.0f },
            { o1Oct, 1 }, { o1Wave, 0.0f }, { o3Oct, 1 }, { o3Freq, 7.0f }, { fm, 0.45f }, { mix1, 1.0f },
            { cutoff, 1200 }, { envAmt, 0.5f }, { fD, 0.15f }, { fS, 0.1f }, { aD, 0.5f }, { aS, 0.6f } } },

        { "Tempo Wobble", "Bass", { { volume, 0.0f },
            { o1Oct, 1 }, { o2Oct, 1 }, { o2Wave, 0.667f }, { o2Freq, -0.1f }, { mix2, 0.8f }, { overload, 0.3f },
            { lfoSync, 2 }, { lfoDiv, 8 }, { m2Src, 0 }, { m2Dest, 3 }, { m2Amt, 0.6f },
            { cutoff, 260 }, { res, 0.4f }, { envAmt, 0.1f } } },

        // ======================= LEADS
        { "Wayfarer Lead", "Leads", { { volume, -2.0f },
            { o2Freq, 0.07f }, { mix2, 0.8f }, { o3Oct, 3 }, { o3Wave, 0.667f }, { mix3, 0.35f },
            { cutoff, 1600 }, { res, 0.3f }, { envAmt, 0.35f }, { fA, 0.02f }, { fD, 0.6f }, { fS, 0.5f },
            { glideOn, 1 }, { glide, 0.1f }, { lfoRate, 5.5f }, { m1Amt, 0.3f },
            { dlyMix, 0.2f }, { dlyDiv, 7 } } },

        { "Hot Pulse Solo", "Leads", { { volume, -3.0f },
            { o1Wave, 0.85f }, { o2Oct, 3 }, { o2Freq, 0.05f }, { mix2, 0.5f }, { overload, 0.7f },
            { cutoff, 2600 }, { res, 0.45f }, { envAmt, 0.25f },
            { m2Src, 7 }, { m2Dest, 0 }, { m2Amt, 0.17f }, { glideOn, 1 }, { glide, 0.05f }, { revMix, 0.15f } } },

        { "Sync Scream", "Leads", { { volume, -3.0f },
            { o2Oct, 3 }, { o2Freq, 3.0f }, { hardSync, 1 }, { mix1, 0.3f }, { mix2, 1.0f },
            { m2Src, 5 }, { m2Dest, 1 }, { m2Amt, 0.6f },
            { cutoff, 3500 }, { res, 0.2f }, { envAmt, 0.2f }, { fD, 0.8f }, { fS, 0.2f }, { dlyMix, 0.2f } } },

        { "Breath Flute", "Leads", { { volume, -2.0f },
            { o1Oct, 3 }, { o1Wave, 0.0f }, { mix1, 1.0f }, { noise, 0.12f },
            { cutoff, 2400 }, { res, 0.1f }, { envAmt, 0.2f }, { aA, 0.05f }, { aR, 0.3f },
            { m1Amt, 0.3f }, { m2Src, 0 }, { m2Dest, 0 }, { m2Amt, 0.06f }, { lfoRate, 5.0f }, { revMix, 0.25f } } },

        { "Three-Osc Brass", "Leads", { { volume, -3.0f },
            { o2Freq, 0.12f }, { mix2, 0.8f }, { o3Freq, -0.1f }, { mix3, 0.7f },
            { cutoff, 500 }, { envAmt, 0.6f }, { fA, 0.08f }, { fD, 0.5f }, { fS, 0.55f }, { aA, 0.03f },
            { spacing, 0.5f }, { choMix, 0.3f } } },

        { "Square Hero", "Leads", { { volume, -2.0f },
            { o1Wave, 0.667f }, { o2Oct, 3 }, { o2Wave, 0.667f }, { o2Freq, 0.05f }, { mix2, 0.5f },
            { cutoff, 1800 }, { res, 0.2f }, { glideOn, 1 }, { glide, 0.08f }, { dlyMix, 0.25f }, { dlyDiv, 9 } } },

        // ======================= KEYS
        { "Glass Arp", "Keys", { { volume, 0.0f },
            { o1Oct, 3 }, { o1Wave, 0.0f }, { o2Freq, 7.0f }, { o2Wave, 0.667f }, { mix2, 0.4f },
            { cutoff, 2000 }, { envAmt, 0.3f }, { fD, 0.25f }, { fS, 0.2f },
            { aD, 0.35f }, { aS, 0.0f }, { aR, 0.3f }, { legato, 0 }, { dlyMix, 0.35f }, { dlyDiv, 8 } } },

        { "Funk Clav", "Keys", { { volume, 0.0f },
            { o1Wave, 0.95f }, { cutoff, 900 }, { res, 0.3f }, { envAmt, 0.7f }, { fVel, 0.5f },
            { fD, 0.12f }, { fS, 0.0f }, { aD, 0.25f }, { aS, 0.0f }, { aR, 0.08f }, { aVel, 0.6f }, { legato, 0 } } },

        { "Spaced Stereo Keys", "Keys", { { volume, -2.0f },
            { o2Freq, 0.15f }, { mix2, 0.8f }, { spacing, 1.2f }, { res, 0.35f }, { cutoff, 700 },
            { envAmt, 0.4f }, { fD, 0.8f }, { fS, 0.3f }, { aD, 1.2f }, { aS, 0.4f }, { aR, 0.6f }, { legato, 0 },
            { choMix, 0.25f }, { revMix, 0.2f } } },

        // ======================= SWEEPS
        { "Spacing Sweep", "Sweeps", { { volume, -3.0f },
            { o2Freq, 0.1f }, { mix2, 0.8f }, { o3Oct, 1 }, { o3Freq, -0.08f }, { mix3, 0.6f },
            { cutoff, 600 }, { res, 0.55f }, { spacing, 1.5f }, { m2Src, 0 }, { m2Dest, 4 }, { m2Amt, 0.55f }, { lfoRate, 0.15f },
            { fA, 1.0f }, { fD, 3.0f }, { aA, 0.4f }, { aR, 1.5f }, { revMix, 0.35f } } },

        { "Band-Pass Voice", "Sweeps", { { volume, 0.0f },
            { fMode, 1 }, { cutoff, 500 }, { spacing, 1.2f }, { res, 0.5f },
            { o2Freq, 0.06f }, { mix2, 0.8f }, { m2Src, 0 }, { m2Dest, 3 }, { m2Amt, 0.35f }, { lfoRate, 0.3f },
            { envAmt, 0.2f }, { aA, 0.2f }, { aR, 0.8f } } },

        { "Drone Weave", "Sweeps", { { volume, -4.0f },
            { gate, 2 }, { o1Oct, 1 }, { o2Oct, 1 }, { o2Freq, 0.03f }, { mix2, 0.8f }, { o3Freq, -0.04f }, { mix3, 0.7f },
            { cutoff, 400 }, { res, 0.4f }, { envAmt, 0.0f }, { m2Src, 2 }, { m2Dest, 3 }, { m2Amt, 0.45f }, { lfoRate, 1.5f },
            { aA, 1.5f }, { revMix, 0.4f }, { dlyMix, 0.25f } } },

        // ======================= FX
        { "Gate Pulse", "FX", { { volume, 0.0f },
            { gate, 1 }, { lfoSync, 2 }, { lfoDiv, 10 }, { cutoff, 900 }, { res, 0.3f }, { envAmt, 0.5f },
            { fD, 0.08f }, { fS, 0.0f }, { aD, 0.1f }, { aS, 0.0f }, { aR, 0.05f } } },

        { "Random Computer", "FX", { { volume, -2.0f },
            { o1Wave, 0.667f }, { m2Src, 2 }, { m2Dest, 0 }, { m2Amt, 0.5f }, { lfoRate, 8.0f },
            { cutoff, 1500 }, { res, 0.5f }, { dlyMix, 0.3f } } },

        { "Osc 3 Siren", "FX", { { volume, -3.0f },
            { o3Lo, 1 }, { o3KbOff, 1 }, { o3Wave, 0.0f }, { o3Freq, 3.0f }, { m2Src, 3 }, { m2Dest, 0 }, { m2Amt, 0.55f },
            { cutoff, 2500 }, { res, 0.3f }, { revMix, 0.3f } } },

        { "Noise Ring", "FX", { { volume, 6.0f },
            { noise, 0.7f }, { mix1, 0.3f }, { fMode, 1 }, { cutoff, 1200 }, { res, 0.7f }, { spacing, 0.5f },
            { m2Src, 2 }, { m2Dest, 3 }, { m2Amt, 0.5f }, { lfoRate, 6.0f }, { aR, 0.4f } } },

        { "Pad Theremin", "FX", { { volume, -2.0f },
            { o1Wave, 0.0f }, { o2Oct, 3 }, { o2Wave, 0.0f }, { mix2, 0.3f },
            { padXDest, 0 }, { padXAmt, 1.0f }, { padYDest, 3 }, { padYAmt, 0.7f }, { padGate, 1 },
            { glideOn, 1 }, { glide, 0.2f }, { m1Amt, 0.35f }, { cutoff, 1400 }, { envAmt, 0.0f }, { aA, 0.08f }, { aR, 0.5f },
            { revMix, 0.3f } } },
    };
    return bank;
}

juce::StringArray presetCategories()
{
    return { "Bass", "Leads", "Keys", "Sweeps", "FX" };
}
