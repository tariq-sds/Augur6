# Augur-6

A six-voice analog-modelled polysynth (VST3 + Audio Unit) for Ableton Live, built around the classic
two-oscillator, poly-mod, low-pass-into-high-pass architecture of late-'70s/2010s American polysynths.
42 factory presets, level-matched, ready to play.

## Installing

See the README at the top of this repository.

## Presets

Use the **< >** arrows or click the program name to pick by category. All factory sounds are original.

| Category | Presets |
|---|---|
| House Chords | Deep House Stab, Warm Organ Chords, House Keys, 90s Piano Stab, Detroit Strings, Dub Chord, Soft Pad Chords, Garage Chord Pluck, Minor 9 Keys, Filtered Disco Chords, Velvet Stab, Sunset Pad |
| Pads | Velvet Horizon, Glass Cathedral, Slow Bloom, Dusk Strings, Night Bus PWM, Balearic Sunset, Afterglow |
| Brass | Gold Brass, Fanfare Stack, Soft Horns, Brass '82 |
| Keys | Ivory Poly, Electric Bell, Clav Funk, Hollow Organ, Velour Keys |
| Bass | Mono Muscle, Rubber Sub, Sync Growl, Acid Ladder, Deep Pulse Bass, Wide Unison Bass |
| Leads | Sync Scream, Silk Lead, Square Hero, Whistle Pipe, Feral Lead |
| Plucks | Glass Pluck, House Stab, Marimba Mod, Harp Strum, Arp Pulse, Muted Pluck |
| Poly | Eighties Poly, Motor City Chord, Sweep Machine, Sync Brass Poly |
| FX | Wind Tunnel, Random Bleeps, Laser Rain, Wobble Voices, Sci-Fi Drone |

To keep your own sounds, tweak a preset and use Live's **Save Preset** button on the device title bar.
Edited presets show a `*` after the name. Everything is saved with your Live Set.

## MIDI Learn (your keyboard's knobs)

Right-click any knob, switch or menu and choose **MIDI Learn**, then turn a knob on your keyboard. Right-click
again to see which CC it is on, forget it, or reset everything to the default map. Mappings are saved with your
Live Set. The default map works straight away on most keyboards that send the standard sound-controller CCs:

| CC | Control | CC | Control |
|---|---|---|---|
| 74 | Filter cutoff | 80 | Filter env attack |
| 71 | Filter resonance | 81 | Filter env decay |
| 70 | Filter env amount | 82 | Filter env sustain |
| 73 | Amp attack | 83 | Filter env release |
| 75 | Amp decay | 79 | Amp sustain |
| 72 | Amp release | | |

If your keyboard sends different CCs, learn them once and save the Live Set (or a Live preset) so they come back.
Every control is also an automatable parameter, so Live's own **MIDI Map** mode works on it too.

## Signal flow

**Osc 1** (tri > saw > pulse morph, pulse width, sub-octave, hard sync to Osc 2) and **Osc 2**
(morphing shape, fine tune, low-frequency mode, key-track off for drones) plus noise feed a
**4-pole resonant ladder low-pass**, then a **2-pole resonant high-pass**, then the **amp envelope**.

* **Poly Mod**: filter envelope and Osc 2 modulate Osc 1 pitch, shape, pulse width, and either filter, per voice.
  Osc 2 in Low Freq mode turns it into a per-voice LFO.
* **LFO**: five shapes, free or synced to Live's tempo; depth = Amount + Mod Wheel.
* **Slop**: per-voice drift and component tolerance, for the slightly unstable analog feel.
* **Unison**: stacks 2-6 voices on one note with detune and stereo spread; plays mono with last-note priority.
* **Effects**: drive, stereo chorus, tempo-synced ping-pong delay, reverb.
* Velocity, mod wheel, pitch bend, aftertouch (to cutoff) and sustain pedal all respond.
* Double-click any knob to reset it. Drag the bottom-right corner to resize the window.

## Building locally (optional)

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Augur6_VST3 Augur6_AU
```
