# M-WORM FM-1

Open-source groovebox firmware for the **M-VAVE FM-1** (JieLi AC791N).

Русская версия: [README.md](README.md) · Signal flow: [SCHEME_EN.md](SCHEME_EN.md)

## Current design

- **6 audio tracks = 6 monophonic voices**, plus 4 MIDI tracks. No voice pool or stealing: a retrigger restarts the same voice.
- Three engines: **VA** (`WAVE 0..5`), **WAVETABLE** (`WAVE 6..57`) and **FM** (8 algorithms, operators `C/A/B1/B2`).
- **BPM lives in the pattern**; pattern changes are applied at the bar boundary.
- Per track: own clock (length, direction, speed, swing), 4 LFOs, free `ENV4`, p-lock/automation.
- Per voice: 5 ADSRs (`E0/E3` amp, `E1/E2` filters, `E4` free), 2 TPT SVF filters (`OFF/LP/HP/BP`), amp distortion, VOL/PAN, TRANSIENT and three sends (`DELAY/REVERB/CHORUS`).
- Rendering is split between **CPU0 and CPU1**: the second core handles part of the voices and the reverb, with a fallback to one core.
- Output: speaker via I2S/ALNK0, USB audio (master 2ch/24bit/48k or FX 6ch/16bit/44.1k) and an MSC virtual disk (`WTABLE.WPT`, `.WPB` banks, `.pat` patterns).

> Older comments and `Work/fm1-base/mworm/docs/MANUAL.md` still mention 4 tracks and an "unused CPU1". The code in `mworm/worm/` is authoritative.

## Layout

```text
Work/fm1-base/           fm1-base platform + BSP (vendored with local FM-1 fixes)
  mworm/                 M-WORM firmware sources, docs, web, tests
  mworm/build/m-worm.fwsc ready-to-flash package FM-1_2100
Work/tables/             40 source WAVs for the built-in wavetables
scripts/run_core_tests.sh host regressions (-ffp-contract=off)
flash.sh, mworm_flash.py USB-MIDI flasher (Felucca protocol)
mworm_tool_*.html         browser utility for banks/patterns
```

Not shipped on purpose: JieLi SDK, toolchain, Python venvs, backups, stock firmware images. See [THIRD_PARTY.md](THIRD_PARTY.md).

## Flash a ready build

```sh
./flash.sh --info
./flash.sh Work/fm1-base/mworm/build/m-worm.fwsc
```

Package SHA256: `6b6a044d6e4644c48d2d643048675e422a0878a88177a745dcbec626d09b137d` (rebuilding from this repository produces the identical package).

See [FLASHER.md](FLASHER.md). Flashing is done only on an explicit request; the script always asks for confirmation.

## Build from source

Requirements: Python 3.11+ (`pip install -r requirements-build.txt`), `AC79_SDK`, `JIELI_TOOLCHAIN` (Linux x86-64; on macOS use a linux/amd64 container such as colima).

```sh
cd Work/fm1-base
python3 tools/build.py mworm --host
mworm/build/host/m-worm_host mworm/tests/basic.txt mworm/build/host

AC79_SDK=/path/to/sdk JIELI_TOOLCHAIN=/path/to/toolchain python3 tools/build.py mworm
./../../scripts/run_core_tests.sh   # 28 PASS / 0 FAIL / 3 SKIP
```

## Docs

[MANUAL_EN.md](MANUAL_EN.md) · [SCHEME_EN.md](SCHEME_EN.md) · [FLASHER.md](FLASHER.md) · [ХАРАКТЕРИСТИКИ_EN.md](ХАРАКТЕРИСТИКИ_EN.md) · [AGENTS.md](AGENTS.md)

M-WORM code is GPL-3.0-only; fm1-base/BSP are MPL-2.0 with third-party notices. See [THIRD_PARTY.md](THIRD_PARTY.md).
