# Toota Audio plugins

Three VST3 / Audio Unit plugins for Ableton Live (macOS 11+, Intel and Apple Silicon).

| Plugin | What it is | Folder |
|---|---|---|
| **Augur-6** | Six-voice analog-modelled polysynth, 42 presets | `Augur6/` |
| **Interlock** | Kick + bass balancer: auto-learned dynamic EQ dip, sub duck, bass focus and parallel compression | `Interlock/` |
| **Varia** | Movement and variation: per-hit changes, filter/stereo drift and phrase-end fills (rolls, throws, reverse swells, gates, tape stops) | `Varia/` |

## Installing

Every push builds all three on GitHub. Open the **Actions** tab, click the latest green run, download
**Toota-Audio-Plugins-macOS**, unzip it, then in Terminal type `bash ` and drag `install.sh` in, press Return.
In Live: **Settings > Plug-Ins > Rescan**. They appear under **Plug-Ins > Toota Audio**.

## Interlock: quick start

1. Put Interlock on your **bass** track.
2. In the device title bar, open the sidechain section (the small triangle) and set **Audio From** to your **kick** track.
   The header should read *SIDECHAIN: KICK DETECTED*.
3. Play kick and bass together. Interlock listens for about five seconds the first time and sets itself up:
   kick fundamental, dip depth and width, sub duck, release (tempo-aware), the best "bass focus" frequency and
   the parallel compressor threshold. Press **RE-LEARN** any time the bassline or kick changes.
4. Fine-tune by dragging the **KICK DIP** and **BASS FOCUS** nodes on the display (mouse wheel on the kick node changes width),
   or with the knobs. **DELTA** lets you hear only what Interlock is changing.

How it works: the kick is low-passed around its fundamental and turned into an envelope that's normalised to the
kick's own recent hits, so every hit gets the full dip regardless of level and the bass comes straight back as the kick decays.
The dip is a dynamic bell at the kick fundamental plus a dynamic low shelf (Sub Duck); Bass Focus is a static bell
where the bass has energy but the kick doesn't. A parallel compressor (threshold set from the learned bass level, raised by Drive)
is blended in with Mix to keep the bass dense between kicks. Mono Lows folds stereo content under ~120 Hz to mono.

## Varia: quick start

Put it on any channel or bus with the transport running. **TEXTURE** (the big orb) is the master amount:
at 0 the plugin is completely transparent, higher values make the variation more frequent and deeper.
Pick a **style** with `< >` or by clicking the name.

* **Release**: tail length (echo feedback, gate smoothing, reverse length).
* **Brightness**: tone of the added movement and how far filter sweeps close.
* **Width**: stereo movement (mono-compatible, works on the side signal only).
* **Pitch**: transposes rolls, reverse swells and tape stops.
* **Wet Only**: hear only the added events. **Bypass**: click-free bypass.

Styles: Groove, Club Rolls, Dub Throws, Filter Ride, Glitch, Reverse Tide, Tape Stop, Breathe, Peak Time, Minimal, Wide Drift, Chaos.
Events are placed on the grid at 2, 4 or 8-bar phrase ends (by style) with smaller fills on ordinary bars, and are chosen at
random each time, so loops never repeat exactly. Nothing is added over silence.
