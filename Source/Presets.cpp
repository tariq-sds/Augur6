#include "Presets.h"
#include "Params.h"

// Factory bank. Every patch starts from Init, so each one lists only what differs.
// Sync-rate indices: 0=4 bars 1=2 bars 2=1 bar 3=1/2 4=1/4. 5=1/4 6=1/4T 7=1/8. 8=1/8 9=1/8T 10=1/16 11=1/16T 12=1/32
// LFO shapes: 0=Triangle 1=Saw Up 2=Saw Down 3=Square 4=Random

using namespace pid;

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> bank
    {
        // ======================= PADS
        { "Velvet Horizon", "Pads", { { volume, -2.5f },
            { osc2Fine, 7 }, { mixOsc1, 0.7f }, { mixOsc2, 0.7f }, { slop, 0.45f },
            { lpfCutoff, 1400 }, { lpfRes, 0.12f }, { lpfEnv, 0.22f },
            { fA, 1.2f }, { fD, 2.5f }, { fS, 0.6f }, { fR, 2.0f },
            { aA, 0.9f }, { aD, 1.0f }, { aR, 2.4f }, { aVel, 0.2f },
            { lfoRate, 0.25f }, { lfoAmount, 0.1f }, { lfoFreq1, 0 }, { lfoFreq2, 0 }, { lfoLPF, 1 },
            { panSpread, 0.6f }, { choMix, 0.35f }, { choRate, 0.45f }, { revMix, 0.3f }, { revSize, 0.8f } } },

        { "Glass Cathedral", "Pads", { { volume, 2.0f },
            { osc1Shape, 0.15f }, { osc2Shape, 0.9f }, { osc2PW, 0.3f }, { osc2Freq, 12 }, { osc2Fine, 4 },
            { mixOsc1, 0.75f }, { mixOsc2, 0.45f },
            { lpfCutoff, 4200 }, { lpfRes, 0.2f }, { lpfEnv, 0.1f }, { hpfCutoff, 260 },
            { fA, 2.0f }, { fD, 3.0f }, { fS, 0.7f }, { fR, 3.0f },
            { aA, 1.5f }, { aR, 3.5f }, { aVel, 0.15f },
            { dlyMix, 0.22f }, { dlySyncRate, 4 }, { dlyFb, 0.45f },
            { revMix, 0.45f }, { revSize, 0.9f }, { revDamp, 0.25f }, { choMix, 0.25f }, { panSpread, 0.8f } } },

        { "Slow Bloom", "Pads", { { volume, -1.5f },
            { osc2Fine, 9 }, { mixOsc1, 0.75f }, { mixOsc2, 0.75f }, { slop, 0.5f },
            { lpfCutoff, 300 }, { lpfRes, 0.35f }, { lpfEnv, 0.55f },
            { fA, 3.5f }, { fD, 4.0f }, { fS, 0.55f }, { fR, 3.0f },
            { aA, 1.6f }, { aR, 3.0f }, { aVel, 0.15f },
            { revMix, 0.35f }, { revSize, 0.85f }, { choMix, 0.3f }, { panSpread, 0.7f } } },

        { "Dusk Strings", "Pads", { { volume, -4.0f },
            { osc2Fine, -9 }, { mixOsc1, 0.8f }, { mixOsc2, 0.8f }, { slop, 0.4f },
            { lpfCutoff, 3200 }, { lpfKey, 0.7f }, { hpfCutoff, 160 },
            { aA, 0.45f }, { aD, 0.5f }, { aS, 0.9f }, { aR, 1.4f }, { aVel, 0.25f },
            { lfoRate, 5.5f }, { lfoAmount, 0.06f },
            { choMix, 0.6f }, { choRate, 0.35f }, { choDepth, 0.7f }, { revMix, 0.28f }, { panSpread, 0.7f } } },

        { "Night Bus PWM", "Pads", { { volume, -5.5f },
            { osc1Shape, 1.0f }, { osc2Shape, 1.0f }, { osc2PW, 0.4f }, { osc2Freq, -12 }, { osc2Fine, 5 },
            { mixOsc1, 0.7f }, { mixOsc2, 0.5f },
            { lfoRate, 0.35f }, { lfoAmount, 0.35f }, { lfoFreq1, 0 }, { lfoFreq2, 0 }, { lfoPW, 1 },
            { lpfCutoff, 2200 }, { lpfEnv, 0.15f }, { fA, 0.8f }, { fD, 2.0f }, { fS, 0.6f }, { fR, 1.5f },
            { aA, 0.6f }, { aR, 1.8f }, { choMix, 0.2f }, { revMix, 0.3f }, { panSpread, 0.6f } } },

        { "Balearic Sunset", "Pads", { { volume, -0.5f },
            { osc1Shape, 0.4f }, { osc2Shape, 0.1f }, { osc2Freq, 12 }, { mixOsc1, 0.75f }, { mixOsc2, 0.5f },
            { lpfCutoff, 2000 }, { lpfRes, 0.1f }, { lpfEnv, 0.15f }, { fA, 0.6f }, { fD, 2.0f }, { fS, 0.6f },
            { lfoSync, 1 }, { lfoSyncRate, 1 }, { lfoAmount, 0.18f }, { lfoFreq1, 0 }, { lfoFreq2, 0 }, { lfoLPF, 1 },
            { aA, 0.7f }, { aR, 2.5f },
            { dlyMix, 0.18f }, { dlySyncRate, 4 }, { dlyFb, 0.4f }, { revMix, 0.4f }, { revSize, 0.85f },
            { choMix, 0.3f }, { panSpread, 0.7f } } },

        { "Afterglow", "Pads", { { volume, -0.5f },
            { osc1Shape, 0.55f }, { osc2Shape, 0.6f }, { osc2Fine, -6 }, { osc1Sub, 0.3f },
            { mixOsc1, 0.75f }, { mixOsc2, 0.65f },
            { lpfCutoff, 900 }, { lpfRes, 0.25f }, { lpfEnv, 0.35f }, { lpfVel, 0.3f },
            { fA, 0.4f }, { fD, 1.8f }, { fS, 0.35f }, { fR, 1.2f },
            { aA, 0.25f }, { aR, 1.6f }, { aVel, 0.3f },
            { choMix, 0.4f }, { dlyMix, 0.15f }, { revMix, 0.35f }, { panSpread, 0.6f } } },

        // ======================= BRASS
        { "Gold Brass", "Brass", { { volume, -2.5f },
            { osc2Fine, 6 }, { mixOsc1, 0.8f }, { mixOsc2, 0.8f }, { slop, 0.4f },
            { lpfCutoff, 520 }, { lpfRes, 0.08f }, { lpfEnv, 0.55f }, { lpfVel, 0.5f },
            { fA, 0.07f }, { fD, 0.7f }, { fS, 0.45f }, { fR, 0.35f },
            { aA, 0.03f }, { aD, 0.5f }, { aS, 0.9f }, { aR, 0.35f }, { aVel, 0.4f },
            { choMix, 0.15f }, { revMix, 0.15f } } },

        { "Fanfare Stack", "Brass", { { volume, -4.5f },
            { osc2Freq, 12 }, { osc2Fine, 4 }, { mixOsc1, 0.8f }, { mixOsc2, 0.6f }, { slop, 0.35f },
            { lpfCutoff, 700 }, { lpfEnv, 0.6f }, { lpfVel, 0.4f },
            { fA, 0.04f }, { fD, 0.5f }, { fS, 0.5f }, { fR, 0.3f },
            { aA, 0.02f }, { aR, 0.3f }, { aVel, 0.35f }, { drive, 0.15f }, { revMix, 0.18f } } },

        { "Soft Horns", "Brass", { { volume, -0.5f },
            { osc1Shape, 0.45f }, { osc2Shape, 0.45f }, { osc2Fine, 5 }, { mixOsc1, 0.8f }, { mixOsc2, 0.7f },
            { lpfCutoff, 380 }, { lpfEnv, 0.4f }, { lpfVel, 0.5f },
            { fA, 0.25f }, { fD, 1.2f }, { fS, 0.5f }, { fR, 0.6f },
            { aA, 0.15f }, { aR, 0.6f }, { aVel, 0.4f }, { revMix, 0.25f }, { choMix, 0.2f } } },

        { "Brass '82", "Brass", { { volume, -2.5f },
            { osc2Fine, 12 }, { mixOsc1, 0.8f }, { mixOsc2, 0.8f },
            { lpfCutoff, 450 }, { lpfRes, 0.25f }, { lpfEnv, 0.65f },
            { fA, 0.02f }, { fD, 0.35f }, { fS, 0.25f }, { fR, 0.3f },
            { aA, 0.01f }, { aR, 0.3f }, { choMix, 0.4f }, { drive, 0.1f }, { revMix, 0.15f } } },

        // ======================= KEYS
        { "Ivory Poly", "Keys", { { volume, 5.0f },
            { osc1Shape, 0.35f }, { osc2Freq, 12 }, { mixOsc1, 0.7f }, { mixOsc2, 0.25f },
            { lpfCutoff, 700 }, { lpfEnv, 0.5f }, { lpfVel, 0.6f }, { lpfKey, 0.8f },
            { fA, 0.002f }, { fD, 0.7f }, { fS, 0.15f }, { fR, 0.5f },
            { aA, 0.002f }, { aD, 1.8f }, { aS, 0.3f }, { aR, 0.6f }, { aVel, 0.6f },
            { choMix, 0.3f }, { revMix, 0.2f } } },

        { "Electric Bell", "Keys", { { volume, 2.5f },
            { osc1Shape, 0.0f }, { osc2Shape, 0.0f }, { osc2Freq, 19 }, { mixOsc1, 0.9f }, { mixOsc2, 0.0f },
            { pmOsc2, 0.14f }, { pmFreq1, 1 },
            { lpfCutoff, 9000 },
            { aA, 0.001f }, { aD, 2.8f }, { aS, 0.0f }, { aR, 1.6f }, { aVel, 0.7f },
            { choMix, 0.35f }, { revMix, 0.3f }, { dlyMix, 0.12f } } },

        { "Clav Funk", "Keys", { { volume, 5.0f },
            { osc1Shape, 1.0f }, { osc1PW, 0.18f }, { mixOsc1, 0.9f },
            { lpfCutoff, 1400 }, { lpfEnv, 0.45f }, { lpfVel, 0.7f },
            { fA, 0.001f }, { fD, 0.18f }, { fS, 0.1f }, { fR, 0.1f },
            { hpfCutoff, 280 }, { hpfRes, 0.2f },
            { aA, 0.001f }, { aD, 0.35f }, { aS, 0.25f }, { aR, 0.08f }, { aVel, 0.7f } } },

        { "Hollow Organ", "Keys", { { volume, -2.0f },
            { osc1Shape, 1.0f }, { osc2Shape, 1.0f }, { osc2Freq, 12 }, { osc1Sub, 0.4f },
            { mixOsc1, 0.7f }, { mixOsc2, 0.5f },
            { lpfCutoff, 5000 }, { lpfKey, 0.6f },
            { aA, 0.002f }, { aD, 0.1f }, { aR, 0.06f }, { aVel, 0.0f },
            { choMix, 0.55f }, { choRate, 5.8f }, { choDepth, 0.3f }, { drive, 0.2f } } },

        { "Velour Keys", "Keys", { { volume, 2.5f },
            { osc1Shape, 0.1f }, { osc2Shape, 0.05f }, { osc2Freq, 12 }, { osc2Fine, 3 },
            { mixOsc1, 0.9f }, { mixOsc2, 0.3f }, { pmOsc2, 0.05f }, { pmFreq1, 1 },
            { lpfCutoff, 2600 }, { lpfEnv, 0.25f }, { lpfVel, 0.5f }, { fD, 1.2f }, { fS, 0.2f },
            { aA, 0.002f }, { aD, 2.5f }, { aS, 0.2f }, { aR, 0.8f }, { aVel, 0.6f },
            { lfoRate, 4.5f }, { lfoAmount, 0.25f }, { lfoFreq1, 0 }, { lfoFreq2, 0 }, { lfoAmp, 1 },
            { choMix, 0.3f }, { revMix, 0.2f }, { panSpread, 0.5f } } },

        // ======================= BASS
        { "Mono Muscle", "Bass", { { volume, 1.5f },
            { unison, 1 }, { uniVoices, 3 }, { uniDetune, 0.18f },
            { osc2Fine, -8 }, { osc1Sub, 0.6f }, { mixOsc1, 0.8f }, { mixOsc2, 0.6f },
            { lpfCutoff, 260 }, { lpfRes, 0.25f }, { lpfEnv, 0.5f }, { lpfVel, 0.3f },
            { fA, 0.001f }, { fD, 0.3f }, { fS, 0.15f }, { fR, 0.15f },
            { aA, 0.001f }, { aD, 0.4f }, { aS, 0.85f }, { aR, 0.1f },
            { glide, 0.04f }, { panSpread, 0.2f }, { drive, 0.2f } } },

        { "Rubber Sub", "Bass", { { volume, 6.0f },
            { unison, 1 }, { uniVoices, 2 }, { uniDetune, 0.05f },
            { osc1Shape, 0.1f }, { osc1Sub, 0.7f }, { mixOsc1, 0.9f },
            { lpfCutoff, 220 }, { lpfEnv, 0.35f }, { lpfKey, 0.6f },
            { fA, 0.001f }, { fD, 0.18f }, { fS, 0.0f },
            { aA, 0.001f }, { aD, 0.5f }, { aS, 0.7f }, { aR, 0.08f }, { panSpread, 0.0f } } },

        { "Sync Growl", "Bass", { { volume, 2.0f },
            { unison, 1 }, { uniVoices, 2 }, { uniDetune, 0.12f },
            { osc1Sync, 1 }, { osc2Freq, -12 }, { mixOsc1, 0.9f }, { mixOsc2, 0.3f },
            { pmFiltEnv, 0.55f }, { pmFreq1, 1 },
            { lpfCutoff, 900 }, { lpfRes, 0.3f }, { lpfEnv, 0.35f },
            { fA, 0.001f }, { fD, 0.45f }, { fS, 0.2f }, { fR, 0.15f },
            { aA, 0.001f }, { aR, 0.1f }, { drive, 0.3f }, { panSpread, 0.2f } } },

        { "Acid Ladder", "Bass", { { volume, 2.5f },
            { unison, 1 }, { uniVoices, 2 }, { uniDetune, 0.0f }, { mixOsc1, 0.9f },
            { lpfCutoff, 180 }, { lpfRes, 0.78f }, { lpfEnv, 0.55f }, { lpfVel, 0.6f },
            { fA, 0.001f }, { fD, 0.22f }, { fS, 0.0f }, { fR, 0.1f },
            { aA, 0.001f }, { aD, 0.3f }, { aS, 0.9f }, { aR, 0.05f },
            { glide, 0.06f }, { drive, 0.35f }, { dlyMix, 0.12f }, { panSpread, 0.0f } } },

        { "Deep Pulse Bass", "Bass", { { volume, 6.0f },
            { unison, 1 }, { uniVoices, 2 }, { uniDetune, 0.08f },
            { osc1Shape, 0.85f }, { osc1PW, 0.3f }, { osc2Freq, -12 }, { osc2Fine, 4 }, { osc1Sub, 0.5f },
            { mixOsc1, 0.7f }, { mixOsc2, 0.6f },
            { lpfCutoff, 520 }, { lpfEnv, 0.42f }, { fA, 0.001f }, { fD, 0.35f }, { fS, 0.2f },
            { aA, 0.001f }, { aR, 0.12f }, { panSpread, 0.1f } } },

        { "Wide Unison Bass", "Bass", { { volume, 3.5f },
            { unison, 1 }, { uniVoices, 4 }, { uniDetune, 0.3f },
            { osc2Fine, 10 }, { osc1Sub, 0.4f }, { mixOsc1, 0.8f }, { mixOsc2, 0.7f },
            { lpfCutoff, 450 }, { lpfRes, 0.15f }, { lpfEnv, 0.4f },
            { fA, 0.001f }, { fD, 0.4f }, { fS, 0.25f },
            { aA, 0.001f }, { aR, 0.12f }, { panSpread, 0.5f }, { drive, 0.15f } } },

        // ======================= LEADS
        { "Sync Scream", "Leads", { { volume, -2.5f },
            { unison, 1 }, { uniVoices, 2 }, { uniDetune, 0.1f },
            { osc1Sync, 1 }, { mixOsc1, 0.9f }, { mixOsc2, 0.2f },
            { pmFiltEnv, 0.7f }, { pmFreq1, 1 },
            { lpfCutoff, 3500 }, { lpfRes, 0.1f }, { lpfEnv, 0.2f },
            { fA, 0.001f }, { fD, 0.9f }, { fS, 0.35f }, { fR, 0.4f },
            { aA, 0.003f }, { aR, 0.3f }, { glide, 0.05f }, { lfoModWheel, 0.4f },
            { dlyMix, 0.22f }, { dlySyncRate, 7 }, { revMix, 0.15f }, { drive, 0.25f }, { panSpread, 0.3f } } },

        { "Silk Lead", "Leads", { { volume, -1.5f },
            { unison, 1 }, { uniVoices, 4 }, { uniDetune, 0.2f },
            { osc2Fine, 6 }, { mixOsc1, 0.8f }, { mixOsc2, 0.7f },
            { lpfCutoff, 2200 }, { lpfRes, 0.15f }, { lpfEnv, 0.3f },
            { fA, 0.02f }, { fD, 0.6f }, { fS, 0.5f },
            { aA, 0.01f }, { aR, 0.3f }, { glide, 0.1f }, { lfoModWheel, 0.5f },
            { dlyMix, 0.25f }, { revMix, 0.2f }, { panSpread, 0.5f } } },

        { "Square Hero", "Leads", { { volume, 4.5f },
            { unison, 1 }, { uniVoices, 2 }, { uniDetune, 0.12f },
            { osc1Shape, 1.0f }, { osc1PW, 0.45f }, { osc2Shape, 1.0f }, { osc2PW, 0.35f },
            { osc2Freq, 12 }, { osc2Fine, 5 }, { mixOsc1, 0.8f }, { mixOsc2, 0.35f },
            { lpfCutoff, 2800 }, { lpfRes, 0.1f }, { lpfEnv, 0.2f },
            { glide, 0.07f }, { lfoModWheel, 0.4f }, { dlyMix, 0.2f }, { panSpread, 0.3f } } },

        { "Whistle Pipe", "Leads", { { volume, 4.5f },
            { unison, 1 }, { uniVoices, 2 }, { uniDetune, 0.02f },
            { osc1Shape, 0.0f }, { mixOsc1, 0.9f }, { mixNoise, 0.08f },
            { lpfCutoff, 6000 }, { lpfKey, 1.0f }, { hpfCutoff, 300 },
            { aA, 0.06f }, { aR, 0.25f }, { lfoRate, 5.2f }, { lfoAmount, 0.06f },
            { glide, 0.08f }, { revMix, 0.3f }, { dlyMix, 0.2f }, { panSpread, 0.2f } } },

        { "Feral Lead", "Leads", { { volume, -1.0f },
            { unison, 1 }, { uniVoices, 2 }, { uniDetune, 0.08f },
            { osc1Shape, 0.6f }, { osc2Freq, 7 }, { osc2Shape, 0.0f },
            { mixOsc1, 0.9f }, { mixOsc2, 0.15f }, { pmOsc2, 0.1f }, { pmFreq1, 1 },
            { lpfCutoff, 1800 }, { lpfRes, 0.3f }, { lpfEnv, 0.35f },
            { fA, 0.005f }, { fD, 0.5f }, { fS, 0.4f },
            { glide, 0.04f }, { drive, 0.3f }, { dlyMix, 0.18f }, { revMix, 0.12f } } },

        // ======================= PLUCKS
        { "Glass Pluck", "Plucks", { { volume, 4.5f },
            { osc1Shape, 0.3f }, { osc2Freq, 12 }, { mixOsc1, 0.7f }, { mixOsc2, 0.35f },
            { lpfCutoff, 500 }, { lpfEnv, 0.6f }, { lpfVel, 0.4f },
            { fA, 0.001f }, { fD, 0.28f }, { fS, 0.0f }, { fR, 0.25f },
            { aA, 0.001f }, { aD, 0.6f }, { aS, 0.0f }, { aR, 0.45f }, { aVel, 0.5f },
            { dlyMix, 0.28f }, { dlySyncRate, 7 }, { dlyFb, 0.4f }, { revMix, 0.25f }, { panSpread, 0.6f } } },

        { "House Stab", "Plucks", { { volume, 1.5f },
            { osc2Fine, 8 }, { mixOsc1, 0.8f }, { mixOsc2, 0.8f },
            { lpfCutoff, 900 }, { lpfRes, 0.12f }, { lpfEnv, 0.45f },
            { fA, 0.001f }, { fD, 0.32f }, { fS, 0.05f }, { fR, 0.2f },
            { aA, 0.001f }, { aD, 0.45f }, { aS, 0.0f }, { aR, 0.25f }, { aVel, 0.4f },
            { choMix, 0.3f }, { revMix, 0.25f }, { revSize, 0.5f } } },

        { "Marimba Mod", "Plucks", { { volume, 2.5f },
            { osc1Shape, 0.0f }, { osc2Shape, 0.0f }, { osc2Freq, 24 }, { mixOsc1, 0.9f },
            { pmOsc2, 0.12f }, { pmFreq1, 1 },
            { lpfCutoff, 2500 }, { lpfEnv, 0.3f }, { fA, 0.001f }, { fD, 0.2f }, { fS, 0.0f },
            { aA, 0.001f }, { aD, 0.45f }, { aS, 0.0f }, { aR, 0.3f }, { aVel, 0.5f },
            { revMix, 0.2f }, { panSpread, 0.5f } } },

        { "Harp Strum", "Plucks", { { volume, 2.5f },
            { osc1Shape, 0.45f }, { osc2Freq, 12 }, { mixOsc1, 0.8f }, { mixOsc2, 0.3f },
            { lpfCutoff, 650 }, { lpfEnv, 0.55f }, { lpfVel, 0.4f },
            { fA, 0.001f }, { fD, 0.5f }, { fS, 0.05f }, { fR, 0.8f },
            { aA, 0.001f }, { aD, 1.6f }, { aS, 0.0f }, { aR, 1.2f },
            { revMix, 0.35f }, { choMix, 0.2f }, { panSpread, 0.7f } } },

        { "Arp Pulse", "Plucks", { { volume, 2.5f },
            { osc1Shape, 0.9f }, { osc1PW, 0.25f }, { mixOsc1, 0.9f },
            { lpfCutoff, 1100 }, { lpfRes, 0.3f }, { lpfEnv, 0.35f },
            { fA, 0.001f }, { fD, 0.15f }, { fS, 0.0f },
            { aA, 0.001f }, { aD, 0.25f }, { aS, 0.0f }, { aR, 0.12f },
            { dlyMix, 0.3f }, { dlySyncRate, 9 }, { dlyFb, 0.45f }, { revMix, 0.15f } } },

        { "Muted Pluck", "Plucks", { { volume, 6.0f },
            { osc1Shape, 0.2f }, { osc2Shape, 0.5f }, { osc2Fine, 5 }, { mixOsc1, 0.8f }, { mixOsc2, 0.4f },
            { lpfCutoff, 480 }, { lpfEnv, 0.42f }, { lpfVel, 0.5f },
            { fA, 0.001f }, { fD, 0.12f }, { fS, 0.0f },
            { hpfCutoff, 90 },
            { aA, 0.001f }, { aD, 0.25f }, { aS, 0.0f }, { aR, 0.15f },
            { revMix, 0.18f }, { dlyMix, 0.15f }, { dlySyncRate, 8 } } },

        // ======================= POLY
        { "Eighties Poly", "Poly", { { volume, -3.0f },
            { osc2Shape, 0.85f }, { osc2PW, 0.4f }, { osc2Fine, 5 }, { mixOsc1, 0.8f }, { mixOsc2, 0.6f },
            { lpfCutoff, 1800 }, { lpfEnv, 0.35f },
            { fA, 0.01f }, { fD, 0.8f }, { fS, 0.4f }, { fR, 0.5f },
            { aA, 0.005f }, { aR, 0.5f },
            { choMix, 0.55f }, { choRate, 0.7f }, { revMix, 0.25f }, { panSpread, 0.6f } } },

        { "Motor City Chord", "Poly", { { volume, 0.5f },
            { osc2Shape, 0.8f }, { osc2Freq, 7 }, { mixOsc1, 0.8f }, { mixOsc2, 0.5f },
            { lpfCutoff, 1100 }, { lpfEnv, 0.45f },
            { fA, 0.001f }, { fD, 0.4f }, { fS, 0.1f },
            { aA, 0.001f }, { aD, 0.7f }, { aS, 0.2f }, { aR, 0.4f },
            { dlyMix, 0.25f }, { dlySyncRate, 8 }, { dlyFb, 0.35f }, { revMix, 0.3f } } },

        { "Sweep Machine", "Poly", { { volume, -1.0f },
            { osc2Fine, 10 }, { mixOsc1, 0.8f }, { mixOsc2, 0.8f },
            { lpfCutoff, 400 }, { lpfRes, 0.55f },
            { lfoSync, 1 }, { lfoSyncRate, 0 }, { lfoAmount, 0.55f }, { lfoFreq1, 0 }, { lfoFreq2, 0 }, { lfoLPF, 1 },
            { aA, 0.3f }, { aR, 1.2f }, { revMix, 0.3f }, { choMix, 0.25f }, { panSpread, 0.6f } } },

        { "Sync Brass Poly", "Poly", { { volume, -1.0f },
            { osc1Sync, 1 }, { osc1Freq, 7 }, { mixOsc1, 0.8f }, { mixOsc2, 0.4f },
            { pmFiltEnv, 0.4f }, { pmFreq1, 1 },
            { lpfCutoff, 1500 }, { lpfEnv, 0.3f },
            { fA, 0.01f }, { fD, 0.6f }, { fS, 0.3f },
            { aA, 0.01f }, { aR, 0.4f }, { choMix, 0.2f }, { revMix, 0.2f } } },

        // ======================= FX
        { "Wind Tunnel", "FX", { { volume, 6.0f },
            { mixOsc1, 0.0f }, { mixNoise, 1.0f },
            { lpfCutoff, 600 }, { lpfRes, 0.82f }, { lpfKey, 1.0f },
            { lfoRate, 0.12f }, { lfoAmount, 0.45f }, { lfoFreq1, 0 }, { lfoFreq2, 0 }, { lfoLPF, 1 },
            { aA, 1.5f }, { aR, 3.0f }, { revMix, 0.5f }, { revSize, 0.95f }, { panSpread, 1.0f } } },

        { "Random Bleeps", "FX", { { volume, 1.0f },
            { osc1Shape, 0.1f }, { mixOsc1, 0.9f },
            { lfoShape, 4 }, { lfoSync, 1 }, { lfoSyncRate, 10 }, { lfoAmount, 0.7f },
            { lfoFreq1, 0 }, { lfoFreq2, 0 }, { lfoLPF, 1 },
            { lpfCutoff, 900 }, { lpfRes, 0.65f }, { aS, 0.9f },
            { dlyMix, 0.3f }, { dlySyncRate, 8 }, { revMix, 0.2f } } },

        { "Laser Rain", "FX", { { volume, 6.0f },
            { osc1Shape, 0.6f }, { mixOsc1, 0.9f }, { pmFiltEnv, -0.85f }, { pmFreq1, 1 },
            { fA, 0.001f }, { fD, 0.25f }, { fS, 0.0f },
            { aA, 0.001f }, { aD, 0.4f }, { aS, 0.0f }, { aR, 0.3f },
            { dlyMix, 0.35f }, { dlyFb, 0.55f }, { revMix, 0.3f }, { panSpread, 0.8f } } },

        { "Wobble Voices", "FX", { { volume, -5.0f },
            { osc2Low, 1 }, { mixOsc2, 0.0f }, { osc1Sub, 0.5f }, { mixOsc1, 0.85f },
            { pmOsc2, 0.5f }, { pmLPF, 1 },
            { lpfCutoff, 500 }, { lpfRes, 0.45f },
            { aR, 0.3f }, { drive, 0.2f }, { revMix, 0.15f } } },

        { "Sci-Fi Drone", "FX", { { volume, 0.0f },
            { osc2Key, 0 }, { osc2Freq, -5 }, { mixOsc1, 0.7f }, { mixOsc2, 0.4f },
            { pmOsc2, 0.3f }, { pmFreq1, 1 },
            { lpfCutoff, 1200 }, { lpfRes, 0.4f },
            { lfoRate, 0.08f }, { lfoAmount, 0.3f }, { lfoFreq1, 0 }, { lfoFreq2, 0 }, { lfoLPF, 1 },
            { aA, 2.0f }, { aR, 4.0f }, { revMix, 0.6f }, { revSize, 0.95f }, { dlyMix, 0.3f }, { dlyFb, 0.6f } } },
    };
    return bank;
}

juce::StringArray presetCategories()
{
    return { "Pads", "Brass", "Keys", "Bass", "Leads", "Plucks", "Poly", "FX" };
}
