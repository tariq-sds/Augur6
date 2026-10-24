#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Wayfarer parameter IDs: engine, editor and presets all use these.
namespace pid
{
    // Oscillators (octave choices: 32' 16' 8' 4' 2' 1')
    inline constexpr const char* o1Oct  = "o1Oct";
    inline constexpr const char* o1Wave = "o1Wave";
    inline constexpr const char* o2Oct  = "o2Oct";
    inline constexpr const char* o2Freq = "o2Freq";
    inline constexpr const char* o2Wave = "o2Wave";
    inline constexpr const char* o3Oct  = "o3Oct";
    inline constexpr const char* o3Freq = "o3Freq";
    inline constexpr const char* o3Wave = "o3Wave";
    inline constexpr const char* hardSync = "sync";      // 1-2 sync
    inline constexpr const char* fm     = "fm";        // 3 -> 1 linear FM amount
    inline constexpr const char* o3KbOff = "o3KbOff";
    inline constexpr const char* o3Lo   = "o3Lo";
    // Mixer
    inline constexpr const char* mix1   = "mix1";
    inline constexpr const char* mix2   = "mix2";
    inline constexpr const char* mix3   = "mix3";
    inline constexpr const char* noise  = "noise";
    inline constexpr const char* overload = "overload";
    // Filters
    inline constexpr const char* cutoff  = "cutoff";
    inline constexpr const char* spacing = "spacing";
    inline constexpr const char* res     = "res";
    inline constexpr const char* kb      = "kb";
    inline constexpr const char* fMode   = "fMode";
    inline constexpr const char* envAmt  = "envAmt";
    inline constexpr const char* fVel    = "fVel";
    // Envelopes
    inline constexpr const char* fA = "fA";
    inline constexpr const char* fD = "fD";
    inline constexpr const char* fS = "fS";
    inline constexpr const char* fR = "fR";
    inline constexpr const char* aA = "aA";
    inline constexpr const char* aD = "aD";
    inline constexpr const char* aS = "aS";
    inline constexpr const char* aR = "aR";
    inline constexpr const char* aVel = "aVel";
    inline constexpr const char* release = "release";
    inline constexpr const char* gate    = "gate";
    // LFO + modulation busses
    inline constexpr const char* lfoRate = "lfoRate";
    inline constexpr const char* lfoSync = "lfoSync";
    inline constexpr const char* lfoDiv  = "lfoDiv";
    inline constexpr const char* m1Src   = "m1Src";
    inline constexpr const char* m1Shape = "m1Shape";
    inline constexpr const char* m1Dest  = "m1Dest";
    inline constexpr const char* m1Amt   = "m1Amt";
    inline constexpr const char* m2Src   = "m2Src";
    inline constexpr const char* m2Shape = "m2Shape";
    inline constexpr const char* m2Dest  = "m2Dest";
    inline constexpr const char* m2Amt   = "m2Amt";
    // Touch pad
    inline constexpr const char* padX     = "padX";
    inline constexpr const char* padY     = "padY";
    inline constexpr const char* padXDest = "padXDest";
    inline constexpr const char* padXAmt  = "padXAmt";
    inline constexpr const char* padYDest = "padYDest";
    inline constexpr const char* padYAmt  = "padYAmt";
    inline constexpr const char* padGate  = "padGate";
    // Keys
    inline constexpr const char* glideOn  = "glideOn";
    inline constexpr const char* glide    = "glide";
    inline constexpr const char* legato   = "legato";
    inline constexpr const char* priority = "priority";
    inline constexpr const char* bend     = "bend";
    inline constexpr const char* tune     = "tune";
    // Output
    inline constexpr const char* drift   = "drift";
    inline constexpr const char* volume  = "volume";
    inline constexpr const char* choMix  = "choMix";
    inline constexpr const char* dlyMix  = "dlyMix";
    inline constexpr const char* dlyDiv  = "dlyDiv";
    inline constexpr const char* dlyFb   = "dlyFb";
    inline constexpr const char* revMix  = "revMix";
    inline constexpr const char* revSize = "revSize";
}

namespace choices
{
    inline const juce::StringArray octaves  { "32'", "16'", "8'", "4'", "2'", "1'" };
    inline const juce::StringArray fModes   { "LP / LP", "HP / LP" };
    inline const juce::StringArray gates    { "Keys", "LFO", "Drone" };
    inline const juce::StringArray lfoSyncs { "Free", "Key", "Tempo" };
    inline const juce::StringArray sources  { "Triangle", "Square", "S&H", "Osc 3", "Noise", "Filter Env", "Amp Env", "Pressure" };
    inline const juce::StringArray shapes   { "On", "Filter Env", "Velocity", "Pressure" };
    inline const juce::StringArray dests    { "Pitch", "Osc 2", "Osc 3", "Filter", "Spacing", "Wave", "LFO Rate", "Amp" };
    inline const juce::StringArray priorities { "Low", "High", "Last" };
    inline const juce::StringArray divs { "4 bars", "2 bars", "1 bar", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32" };

    enum Src { sTri, sSqr, sSH, sOsc3, sNoise, sFEnv, sAEnv, sPressure };
    enum Shape { shOn, shFEnv, shVel, shPressure };
    enum Dest { dPitch, dOsc2, dOsc3, dFilter, dSpacing, dWave, dLfoRate, dAmp, kNumDests };

    inline double divBeats (int i)
    {
        static const double b[] { 16.0, 8.0, 4.0, 2.0, 1.5, 1.0, 2.0 / 3.0, 0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        return b[juce::jlimit (0, 12, i)];
    }
}
