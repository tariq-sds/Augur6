# Toota Audio plugins

VST3 / Audio Unit plugins for Ableton Live (macOS 11+, Intel and Apple Silicon).

| Plugin | What it is | Folder |
|---|---|---|
| **Augur-6** | Six-voice analog-modelled polysynth, 42 presets | `Augur6/` |
| **Interlock** | Kick + bass balancer: auto-learned dynamic EQ dip, sub duck, bass focus and parallel compression | `Interlock/` |
| **Varia** | Movement and variation: per-hit changes, filter/stereo drift and phrase-end fills (rolls, throws, reverse swells, gates, tape stops) | `Varia/` |
| **Mirror** + **Mirror Bridge** | Reference-track mix matching across stems, key check per stem with transpose; the Max for Live Bridge moves Live's real faders and transposes clips | `Mirror/` |
| **Arc** | Turns your stems into a full arrangement (intro / build / drop / break / outro): tempo and bar-1 lock, key lock, fills, rolls and call-and-response from your own audio; drag the lanes into Live | `Arc/` |
| **Clarion** + **Clarion Room** | Vocal clarity pair: vocal EQ, de-ess, clarity, groove-glued parallel compression, vocal ride, key detection and key/root matching; Room carves the music only where the vocal needs space | `Clarion/` |

## Installing

Every push builds them all on GitHub. Open the **Actions** tab, click the latest green run, download
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

* **Every**: `STYLE` lets the style decide when things happen. Pick a bar count (1 to 16, 24, 32, 48 or 64) and Varia makes
  exactly one change at the end of every N bars, counted from bar 1 of your song, and is completely transparent in between,
  like switching it on for a beat every N bars without any automation. The display counts down to the next change.
* **Length**: how long each of those changes lasts (half a beat up to 2 bars, or AUTO).

Styles: Groove, Club Rolls, Dub Throws, Filter Ride, Glitch, Reverse Tide, Tape Stop, Breathe, Peak Time, Minimal, Wide Drift, Chaos.
Events are placed on the grid at 2, 4 or 8-bar phrase ends (by style) with smaller fills on ordinary bars, and are chosen at
random each time, so loops never repeat exactly. Nothing is added over silence.

## Clarion: quick start

Turning a vocal up rarely makes it clearer, because the music is still masking the same frequencies.
Clarion works on the vocal; Clarion Room makes space in the music. Use both.

1. **Clarion** on the vocal track. In its sidechain section set **Audio From** to your instrumental / music bus
   (a group of everything except the vocal). The header should read *SIDECHAIN: MUSIC DETECTED*.
2. **Clarion Room** on that music bus, with its sidechain set to the **vocal** track.
3. Play the song. Clarion learns an EQ for the voice on first play (or press **AUTO EQ**), rides the vocal so it sits
   **Target** dB above the music, and lifts **Clarity** only in the bands where the music is covering it.
   Room ducks those same bands in the music, only while the vocal sings, and only in the centre if **Mid only** is on.

**Key**: the vocal's key and live note are shown at the top left; the reference key and root come from whatever is in the
sidechain. To match the vocal to a particular element (a bassline, a chord stab), route that element to Clarion's
sidechain for a few seconds, press **HOLD REF**, then **MATCH**. Choose **KEY** (fits the vocal into the element's key,
treating relative major/minor as the same) or **ROOT** (moves the vocal's tonic onto the element's root note).
Then set the sidechain back to the music bus. The pitch shift is formant-preserving, so the voice keeps its character.
Clarion adds 40 ms of latency, which Live compensates automatically.

**Parallel dynamics**: Drive sets how hard the parallel layer is compressed (its threshold follows the vocal's own level),
Parallel blends it in, and Groove Glue makes that dense layer pulse with the music's drums so the vocal locks to the groove.

## Mirror: quick start

1. Put **Mirror** on every track you want balanced and leave it on **STEM** (the default). Name your tracks:
   Mirror uses the names to talk to Live and to guess roles (kick, bass, vocal...).
2. Put **Mirror** on the **Main** track and switch it to **MASTER**.
3. Optional but recommended: put **Mirror Bridge** (Live browser: Audio Effects > Max Audio Effect) on the Main track too.
   With it, Mirror moves your actual Live faders (one undo step, with Restore and A/B in the Bridge) and
   transposes out-of-key clips with Live's own clip Transpose. Without it, each stem's **Mirror Gain** does the job.
4. In the master, **LOAD REFERENCE** (or drop a WAV/AIFF/MP3 on it), play your loudest section, press **LISTEN & MATCH**.
5. Stems flagged out of key show the exact transpose. **FIX KEYS** applies it (in Live via the Bridge, or inside Mirror).

Mirror matches the *spectral balance* of the reference. Two stems that live in the same range (two pads, say) can't be told
apart by a reference, so Mirror keeps those close to where they were; use **Lock** on any stem you've already set by ear.
Mirror stems only analyse during a LISTEN pass, so they cost almost no CPU the rest of the time.

## Arc: quick start

Arc turns your stems into a finished arrangement, Loopworm-style, but with your own sounds. Three steps, left to right:

**1. Add stems.** Put Arc on an empty audio track. Then either:
* **Drop your saved Live set (.als)** onto Arc. Every audio track comes in as a lane, named after the track, in exactly the
  timing Live uses (Arc reads each clip's warp markers, so recordings with a pre-roll and warped clips line up perfectly).
  MIDI tracks need bouncing to audio first; Arc lists any it skipped.
* **Or drop audio files / a folder** (WAV, AIFF, FLAC, MP3, or MIDI clips). Files from inside a Live project are matched to
  the project's set automatically (save the set first). Loops from elsewhere are assumed to start on bar 1, and their
  tempo comes from their length.

Click a lane's coloured chip to change its role, crop it, transpose it, pick a different source tempo, reset bar 1,
replace or remove it. The black **1** flag on each waveform is bar 1: drag the waveform sideways to move it, double-click to reset.

**2. Shape it.** Pick a style (Club, Techno, Melodic, Afro, Radio, DJ Tool), set **Variation** and press **GENERATE**.
Each lane has a **STRAIGHT / SOME / LOTS** button: STRAIGHT plays the loop exactly as it is and only comes in and out
with the sections (kicks start STRAIGHT), SOME adds the odd fill, LOTS keeps it moving. The **↻** button re-rolls one lane.
**KEY LOCK** (off by default, since your own stems are already in key) moves bass, chords and lead into ROOT / SCALE.
UNDO / REDO cover everything.

To listen: with Live playing, Arc follows Live's position. With Live stopped, use Arc's PLAY; click or drag on the
timeline to move, click a section to jump to it, double-click it to loop it, scroll to zoom.

**3. Send to Live.** Drag a lane's arrow (or **DRAG ALL TO LIVE**) into the arrangement at bar 1: each lane lands as a
full-length, already-arranged audio clip. **EXPORT** writes every lane plus a mix to `~/Music/Arc Exports`.
