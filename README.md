# Augur-6

A six-voice analog-modelled polysynth (VST3 + Audio Unit) for Ableton Live, built around the classic
two-oscillator, poly-mod, low-pass-into-high-pass architecture of late-'70s/2010s American polysynths.
42 factory presets, level-matched, ready to play.

## Getting it onto your Mac (one-off, about 10 minutes)

The plugin is compiled for you by GitHub's free Mac build servers, so you don't need Xcode.

1. Create a free account at github.com if you don't have one.
2. Click **New repository**, call it `augur-6`, set it to **Private**, and create it.
3. On the empty repo page, click **uploading an existing file**.
4. In Finder, open this `Augur6` folder and press **Cmd + Shift + .** so the hidden `.github` folder shows.
   Select **everything** inside the folder (including `.github`) and drag it into the browser. Commit.
5. Open the **Actions** tab. A build called *Build Augur-6 for macOS* starts automatically (about 8 minutes).
6. When it shows a green tick, open it and download **Augur-6-macOS** at the bottom of the page.
7. Unzip it (it may be a zip inside a zip), open Terminal, type `bash ` (with a space), drag `install.sh`
   from the unzipped `Augur-6` folder onto the Terminal window, and press Return.
8. In Ableton Live: **Settings > Plug-Ins**, make sure *Use VST3 Plug-In System Folders* and
   *Use Audio Units v2* are on, then press **Rescan**. Augur-6 appears under **Plug-Ins > Toota Audio**.

The build is a universal binary, so the same file runs on Intel and Apple Silicon Macs (macOS 11 or later).

## Presets

Use the **< >** arrows or click the program name to pick by category. All factory sounds are original.

| Category | Presets |
|---|---|
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
