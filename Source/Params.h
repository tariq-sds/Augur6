#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Parameter IDs for the Augur-6. Kept in one place so the engine, editor and presets agree.
namespace pid
{
    // Oscillator 1
    inline constexpr const char* osc1Freq  = "osc1Freq";
    inline constexpr const char* osc1Shape = "osc1Shape";
    inline constexpr const char* osc1PW    = "osc1PW";
    inline constexpr const char* osc1Sub   = "osc1Sub";
    inline constexpr const char* osc1Sync  = "osc1Sync";
    // Oscillator 2
    inline constexpr const char* osc2Freq  = "osc2Freq";
    inline constexpr const char* osc2Fine  = "osc2Fine";
    inline constexpr const char* osc2Shape = "osc2Shape";
    inline constexpr const char* osc2PW    = "osc2PW";
    inline constexpr const char* osc2Low   = "osc2Low";
    inline constexpr const char* osc2Key   = "osc2Key";
    // Mixer
    inline constexpr const char* mixOsc1   = "mixOsc1";
    inline constexpr const char* mixOsc2   = "mixOsc2";
    inline constexpr const char* mixNoise  = "mixNoise";
    inline constexpr const char* slop      = "slop";
    // Poly mod
    inline constexpr const char* pmFiltEnv = "pmFiltEnv";
    inline constexpr const char* pmOsc2    = "pmOsc2";
    inline constexpr const char* pmFreq1   = "pmFreq1";
    inline constexpr const char* pmShape1  = "pmShape1";
    inline constexpr const char* pmPW1     = "pmPW1";
    inline constexpr const char* pmLPF     = "pmLPF";
    inline constexpr const char* pmHPF     = "pmHPF";
    // LFO
    inline constexpr const char* lfoRate     = "lfoRate";
    inline constexpr const char* lfoSync     = "lfoSync";
    inline constexpr const char* lfoSyncRate = "lfoSyncRate";
    inline constexpr const char* lfoShape    = "lfoShape";
    inline constexpr const char* lfoAmount   = "lfoAmount";
    inline constexpr const char* lfoModWheel = "lfoModWheel";
    inline constexpr const char* lfoFreq1    = "lfoFreq1";
    inline constexpr const char* lfoFreq2    = "lfoFreq2";
    inline constexpr const char* lfoPW       = "lfoPW";
    inline constexpr const char* lfoLPF      = "lfoLPF";
    inline constexpr const char* lfoHPF      = "lfoHPF";
    inline constexpr const char* lfoAmp      = "lfoAmp";
    // Low-pass
    inline constexpr const char* lpfCutoff = "lpfCutoff";
    inline constexpr const char* lpfRes    = "lpfRes";
    inline constexpr const char* lpfEnv    = "lpfEnv";
    inline constexpr const char* lpfVel    = "lpfVel";
    inline constexpr const char* lpfKey    = "lpfKey";
    inline constexpr const char* lpfAT     = "lpfAT";
    // High-pass
    inline constexpr const char* hpfCutoff = "hpfCutoff";
    inline constexpr const char* hpfRes    = "hpfRes";
    inline constexpr const char* hpfEnv    = "hpfEnv";
    inline constexpr const char* hpfVel    = "hpfVel";
    inline constexpr const char* hpfKey    = "hpfKey";
    // Filter envelope
    inline constexpr const char* fA = "fA";
    inline constexpr const char* fD = "fD";
    inline constexpr const char* fS = "fS";
    inline constexpr const char* fR = "fR";
    // Amp envelope
    inline constexpr const char* aA   = "aA";
    inline constexpr const char* aD   = "aD";
    inline constexpr const char* aS   = "aS";
    inline constexpr const char* aR   = "aR";
    inline constexpr const char* aVel = "aVel";
    // Global
    inline constexpr const char* glide      = "glide";
    inline constexpr const char* unison     = "unison";
    inline constexpr const char* uniVoices  = "uniVoices";
    inline constexpr const char* uniDetune  = "uniDetune";
    inline constexpr const char* panSpread  = "panSpread";
    inline constexpr const char* bendRange  = "bendRange";
    inline constexpr const char* volume     = "volume";
    // Effects
    inline constexpr const char* drive       = "drive";
    inline constexpr const char* choMix      = "choMix";
    inline constexpr const char* choRate     = "choRate";
    inline constexpr const char* choDepth    = "choDepth";
    inline constexpr const char* dlyTime     = "dlyTime";
    inline constexpr const char* dlySync     = "dlySync";
    inline constexpr const char* dlySyncRate = "dlySyncRate";
    inline constexpr const char* dlyFb       = "dlyFb";
    inline constexpr const char* dlyMix      = "dlyMix";
    inline constexpr const char* dlyPing     = "dlyPing";
    inline constexpr const char* revSize     = "revSize";
    inline constexpr const char* revDamp     = "revDamp";
    inline constexpr const char* revMix      = "revMix";
}

namespace choices
{
    inline const juce::StringArray lfoShapes { "Triangle", "Saw Up", "Saw Down", "Square", "Random" };

    inline const juce::StringArray syncRates { "4 bars", "2 bars", "1 bar", "1/2", "1/4.", "1/4", "1/4T",
                                               "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32" };

    // Length of each sync rate in quarter-note beats
    inline double syncBeats (int index)
    {
        static const double beats[] { 16.0, 8.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0,
                                      0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        return beats[juce::jlimit (0, 12, index)];
    }
}
