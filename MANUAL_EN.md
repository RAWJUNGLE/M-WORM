# M-WORM — User Manual

M-WORM — a groovebox inspired by Electron.

- 4 independent audio tracks, 4 monophonic voices: VA/FM synth, sequencer, effects, p-locks and automation.
- 4 MIDI tracks M1–M4: notes, CC, program changes and banks over USB.

Signal path of one track:

```
TRACK → SEQ → OSC (VA/FM) → AMP (E0/E3) → FLT1 / FLT2 (E1/E2) → SENDS → MASTER + LIMITER
```

Delay, reverb and chorus are shared; the send level is set per track in `AMP → SEND`.

---

## Contents

1. [Quick start](#1-quick-start)
2. [Buttons, keys and knobs](#2-buttons-keys-and-knobs)
3. [Pages and tabs](#3-pages-and-tabs)
4. [Sequencer](#4-sequencer)
5. [MIDI tracks M1–M4](#5-midi-tracks-m1m4)
6. [Saving, presets and banks](#6-saving-presets-and-banks)
7. [MIDI and USB](#7-midi-and-usb)

---

## 1. Quick start

1. Power on the FM-1 — it starts in synthesizer mode.
2. Press **REC** to enter the step sequencer (the playhead runs, no sound yet).
3. Select a step with a white key and, **while holding it**, turn the knobs:
   - **K1** — step note (ROOT);
   - **K2** — velocity;
   - **K3** — gate length;
   - **K4** — probability (PROB).
4. Press **EDIT** in REC — K4 becomes **COND** (Trig Condition: `--`, 1:2 … 8:8).
   Press **EDIT** again to return to the normal knobs.
5. **OP1–OP4** (black keys) open NOTE TRIG / OSC / AMP / FLT; hold an OP key or use
   **HOME + OP** to switch tabs inside a page.
6. **HOME** works as Shift/Func; see the combinations below.
7. **PLAY** starts/stops playback. **SAVE** saves the pattern.

---

## 2. Buttons, keys and knobs

### Buttons

| Button | Action |
| --- | --- |
| **OCT− / OCT+** | Octave (−2…+2). In dialogs: OCT− = NO, OCT+ = YES. In REC with a step held: microtiming. |
| **FX** | Effects page: DELAY / REVERB / CHORUS tabs. Hold to switch tabs. |
| **SEL** | TRACKS page: levels and MUTE. **SEL + white key** selects a track. |
| **ENV** | Envelopes for OSC1 / OSC2 / FREE ENV; repeated presses cycle pages. |
| **LFO** | LFO page. Hold ~0.5 s for LFO MAP (assign an LFO to a parameter). |
| **EDIT** | Short press: pattern select; hold: bank select. In REC: TRIG (RETRIG / R.LEN / R.VEL / COND). |
| **GLO** | Global settings: AUDIO / MIDI / SPEAKER / USB. |
| **HOME** | Shift/Func. On SEQ opens EUCLID; on NOTE TRIG in REC shows the microtiming overview. |
| **SAVE** | Short press: `SAVE PATTERN?`; hold 2 s: save the track sound into a preset. |
| **ARP** | Arpeggiator; hold to switch tabs. |
| **SEQ** | Sequencer page; hold + white key selects a pattern page. |
| **PLAY** | Start/stop. **HOME + PLAY** opens `BPM / CLICK` with diagnostics. |
| **REC** | Enter/exit the step sequencer. **REC + PLAY** starts Live Record. |

### Black keys OP1–OP4

Open synth pages (usually in REC or with HOME held):

| Key | Page |
| --- | --- |
| **OP1** (F#3) | NOTE TRIG — note, velocity, gate, probability, retrig, trig conditions |
| **OP2** (G#3) | OSC — two oscillators, VA or FM |
| **OP3** (A#3) | AMP — volume, pan, distortion, transient, FX sends |
| **OP4** (C#4) | FLT — two filters and their envelopes |

Holding an OP key plus a white key switches tabs inside the page.

### Knobs

| Knob | Action |
| --- | --- |
| **K1–K4** | Four parameters of the current page (labelled at the bottom of the screen). |
| **SELECT** | Contextual: track volume, bank/track/BPM selection, yes/no in dialogs. |
| **ALGO** | Choose **VA** or **FM** (OCT+ apply, OCT− cancel). |
| **PRESET** | Presets A01…A32; with a step held, writes a preset into the step. |

### Combinations

| Combination | Action |
| --- | --- |
| **SEL + white key 1–4** | Select audio track 1–4. |
| **SEL + white key 13–16** | Select MIDI track: 16 → M1, 15 → M2, 14 → M3, 13 → M4. |
| **SEL + OP1…OP4** | Mute audio tracks. |
| **SEL + HOME + OP1…OP4** | Mute MIDI tracks; also D#5 / C#5 / A#4 / G#4 = M1…M4. |
| **HOME + OP1…OP4** | Open NOTE TRIG / OSC / AMP / FLT. |
| **OP held + white key** | Switch the page tab. |
| **SEQ held + white key** | Select pattern page 1–8. |
| **HOME + SEQ** | EUCLID instead of SEQ PAGE (toggles back and forth). |
| **HOME + PLAY** | BPM / CLICK screen: CPU, XRUN, MAX, UND, MISS, LIMdB. |
| **PLAY + REC** | Live Record. |
| **REC + EDIT** | TRIG CONDITION: RETRIG / R.LEN / R.VEL / COND. |
| **GLO + EDIT** | Clear all automation of the current track. |
| **EDIT + OCT−** | Clear automation of the four knobs in the current menu. |
| **GLO + SAVE** (short) | `SAVE BANK?` (outside REC). |
| **HOME + SAVE** (2 s) | Firmware update mode. |
| **Step held + OCT− / OCT+** | Microtiming, only in REC. |
| **Step held + HOME** | Microtiming overview on NOTE TRIG in REC. |
| **Step held + PRESET** | Step preset (7th p-lock slot). |

With **HOME** held, K1–K4 give alternative parameters:

| Menu | HOME + knobs |
| --- | --- |
| OSC (FM) | RATIO / FDBK / I.ENV / I.VEL of the selected oscillator |
| AMP | K2 = DRIVE instead of DIST |
| FLT, tabs 1/2 | K4 = D/WET instead of ENV |
| FLT, tabs 3/4 | DRIVE / PAN / VOL of the filter envelope |
| LFO | MODE / SYNC / SPEED; for TIDES: MODE / SLOPE / SMOOTH / SHIFT |
| ENV (FREE) | K1/K2 = DEST / DEPTH |
| MIDI → LFO | Extra CC line parameters |

---

## 3. Pages and tabs

### NOTE TRIG (OP1)

ROOT, VEL, LEN, PROB. In REC, **EDIT** opens the TRIG window:

- **RETRIG** — OFF, 1/2, 1/4, 1/8, 1/12, 1/16, 1/24, 1/32, 1/48, 1/64;
- **R.LEN** — LEN or 1/16, 1/8, 1/4, 1/2, 1/1;
- **R.VEL** — velocity ramp of the retrig series (−63…+63): negative fades out, positive fades in;
- **COND** — trig conditions 1:2 … 8:8 (`--` = off).

Tab 2 holds filter/LFO triggers and portamento (on/off and time).

### OSC (OP2)

Algorithms **VA** and **FM** (the WT/HIVE/CHIP/TOY/AT/GS/ELMYRA engines were removed; old
patches open as VA).

- **VA**: WAVE, PW, TUNE, OCT, DETUNE, SUB, PITCH, LEVEL. PW only works with the PLS wave.
- **FM**: same knobs plus `HOME → OSC…FM EXT`: RATIO (×0.25…×4), FDBK, I.ENV, I.VEL.
  Instead of two volumes there is **MIX** (equal-power crossfade OSC1↔OSC2), e.g. `70:30`.

Tabs: OSC1 (WAVE/PW/TUNE/OCT), OSC1 (DETUNE/SUB/PITCH/LEVEL/MIX), OSC2 — the same.

### AMP (OP3)

DIST + D/WET, TRANSIENT, PAN, VOL, DRIVE (HOME + K2). The SEND tab holds the send levels
for delay / reverb / chorus. The transient shaper is per track.

### FLT (OP4)

Two filters: TYPE (OFF/LP/HP/BP), CUTOFF, RESO, ENV depth; on tabs 1/2 with HOME, K4 = D/WET.
The graph shows the response from 20 Hz to 20 kHz, ±36 dB: dashed line = base CUTOFF, solid
needle = live value with modulation. Tabs 3/4 are the filter envelopes E1/E2 (ATK/DEC/SUS/REL);
with HOME: DRIVE / PAN / VOL of the envelope.

### LFO

4 LFOs per track: DEST, SPEED, DEPTH, WAVE, MODE, SYNC, SLOPE, SMOOTH, SHIFT.
Shapes: TRI / SIN / SQR / SAW / RND / EXP / RAMP / TDS; modes FREE / HOLD / ONE / HALF.
HOME + K1…K4 = MODE / SYNC / SPEED (for TDS: MODE / SLOPE / SMOOTH / SHIFT).
Holding the LFO button opens LFO MAP — assign a parameter with a knob.

### ENV

Three pages: OSC1 ENV (E0), OSC2 ENV (E3), FREE ENV (E4). Each has ATK/DEC/SUS/REL.
FREE ENV is a free ADSR modulator: HOME sets DEST and DEPTH; while the note sounds, the
envelope runs ATK→DEC→SUS→REL and its value × DEPTH is added to the destination. Typical
destinations are PW, FM INDEX and DLY/REV/CHO sends. Filter envelopes E1/E2 live on FLT tabs 3/4.

### FX

Shared effects:

| Effect | Parameters |
| --- | --- |
| DELAY | TIME / FDBK / FILT / MIX |
| REVERB | SIZE / DECAY / DAMP / MIX |
| CHORUS | RATE / DEPTH / DELAY / MIX |

### SEQ / ARP / SEL / GLO

- **SEQ** — number of pages, direction (`>>`, `<<`, `<>`, `??`), speed (1/2×, 1×, 2×),
  swing (50…75%), length (up to 128 steps). HOME + SEQ opens EUCLID: E.HITS 0…16,
  E.OFS 0…15, E.LIMIT 1…16, E.MUTE.
- **ARP** — mode OFF/UP/DOWN/UPDN/RND, speed, octaves 1…4, gate. Tab 2 — ARP > MIDI
  (OFF / M1…M4): duplicate arpeggiator notes onto a MIDI track.
- **SEL** — track list, level and MUTE.
- **GLO** — AUDIO (USB audio output), MIDI (USB MIDI mode, panic), SPEAKER (speaker level),
  USB (mass storage / audio+MIDI).

Modulation is visible on screen: the knob pointer moves to the modulated value while a dot on
the rim marks the base; on AMP SEND the bars follow modulation; on FLT CUTOFF the dashed line
is the base and the solid needle is the live value.

---

## 4. Sequencer

Each track has **128 steps** (8 pages of 16).

### Step entry

- **REC** — enter/exit; white keys 1–16 toggle steps on the current page.
- **SEQ + white key** — select page 1–8.
- Hold a step and turn a knob for a **p-lock**: the value applies only to that step. There are
  7 slots; the 7th is reserved for the step preset.
- **REC + EDIT** — RETRIG / R.LEN / R.VEL / COND for the held step.
- **PLAY + REC** — Live Record: play with the keys; knob movement is captured as automation.

### Step preset

Hold a trigger and turn **PRESET**. The preset (A01…A32) is stored as the 7th p-lock; the
track's own sound does not change. During playback the preset loads before the normal locks,
so knobs recorded on the step override the preset. The `STEP PRESET` window stays 2 seconds.

### Microtiming

In REC, hold a step and press **OCT− / OCT+**: shift from −24 to +24 ticks (1 tick = 1/24 of
a 1× step). While the key is held, a scale and the shifts of all 16 steps on the page are shown.
**HOME** on NOTE TRIG in REC shows the `MICROTIMING x/y` overview (bars up = later, down =
earlier). The metronome and beat indicator use the same delay. Outside REC, OCT± changes octave
as usual.

### EUCLID (Euclidean rhythm)

**HOME + SEQ** opens the Euclidean sequencer: E.HITS (0…16), E.OFS (0…15), E.LIMIT (1…16),
E.MUTE. Press HOME + SEQ again to return to the normal SEQ page.

---

## 5. MIDI tracks M1–M4

Besides the audio tracks there are 4 MIDI tracks — the same kind of sequences, but instead of
synth sound they send MIDI over USB.

- Select with **SEL + white key 16/15/14/13** = M1/M2/M3/M4.
- Channels 1…4 by default; on the MIDI page you can set 1…16, PROG (program change),
  BANK MSB/LSB, VOL (CC7), PAN (CC10), quantization and MUTE.
- 4 CC LFO lines per track: CC number, BASE, DEPTH, SPEED, WAVE, SYNC, PHASE.
- P-locks and microtiming work as on audio tracks.
- **ARP > MIDI** sends arpeggiator notes to the selected MIDI track.
- Mute with **SEL + OP1…OP4** or **SEL + D#5 / C#5 / A#4 / G#4**; **GLO → MIDI → OCT+**
  sends panic (All Notes Off).

---

## 6. Saving, presets and banks

| Action | Result |
| --- | --- |
| **SAVE** (short) | `SAVE PATTERN?` → OCT+ yes / OCT− no. Writes the pattern and project. |
| **SAVE** (2 s) | Current track sound → selected preset A01…A32 (`PRESET SAVED`). |
| **GLO + SAVE** (short, outside REC) | `SAVE BANK?` — save the whole bank. |
| **EDIT** (short) | Pattern select. |
| **EDIT** (2 s) | Bank select. |

There are **8 banks A–H × 16 patterns = 128 patterns**. A pattern is a snapshot of everything:
the four track sequences, each track's sound (patch, length/direction/swing, MUTE, automation),
global FX and parameters. BPM is project-wide. A new pattern starts with empty sequences but
takes the current sound and effects; switching bank loads pattern 1 of the new bank.


**Presets** A01…A32 start with factory sounds; user presets are stored in NOR and survive
pattern changes. A preset changes only the track sound (OSC/AMP/SEND/FLT/LFO/ENV), not FX or
global parameters.

On a computer the banks appear as a disk `M-WORM` with files `BANK_A.WPB … BANK_H.WPB`
(GLO → USB → MASS STORAGE). For individual patterns use `mworm_tool.html`.

---

## 7. MIDI and USB

- **MIDI in** — USB and TRS jack; notes play the synth (quantized to scale/chord).
- **GLO → USB** → `USB MODE`:
  - `MASS STORAGE` — FM-1 mounts as `M-WORM PATTERNS`; audio and MIDI are unavailable.
  - `AUDIO + MIDI` — normal operation: USB audio and USB MIDI.
  Select with SELECT, then **OCT+ apply / OCT− cancel**; only one mode at a time.
- **GLO → MIDI** → `USB MIDI MODE`:
  - `COMPUTER (DEVICE)` (default) — FM-1 connects to a DAW, plus USB audio.
  - `EXTERNAL (HOST)` — FM-1 is the host: connect a synth/controller (e.g. Roland S-1).
    An active hub or device-powered port is required: FM-1 does not supply 5 V. There is no
    computer connection in this mode (no USB audio or firmware update); after reboot the mode
    is always COMPUTER.
  - Status: STARTING / LOOKING FOR DEVICE / CONNECTED. **OCT+** sends panic (All Notes Off).
- **GLO → AUDIO** — USB audio output: `MASTER 24BIT 48K`, `4 TRACK 16BIT 44K`, `FX 16BIT 44K`.
- **GLO → SPEAKER** — attenuate the speaker/headphones when USB is connected (USB audio unaffected).
- **USB audio** — FM-1 is a sound card: record from «M-WORM FM-1» and select «M-WORM FM-1»
  as the playback output. Volume is controlled by the MASTER knob.
