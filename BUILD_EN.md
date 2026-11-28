# Building M-WORM FM-1 firmware

Short guide: build `m-worm.fwsc` and flash it to the FM-1.

## 1. Requirements

- Python 3.11+ and `Pillow` for the font generator: `python3 -m pip install Pillow`
- **JieLi AC79 AIoT SDK** — a local checkout only; SDK binaries are not in this repository.
- **JieLi Linux x86-64 toolchain** (`JIELI_TOOLCHAIN`). On macOS the build runs in a linux/amd64 container, so Docker + colima are needed.

## 2. Environment

```sh
# macOS: start the VM if it is not running yet
colima start --vm-type vz --vz-rosetta --cpu 4 --memory 6

export AC79_SDK=~/fw-AC79_AIoT_SDK
export JIELI_TOOLCHAIN=/path/to/jieli-linux-toolchain
```

Adjust the paths for your machine.

## 3. Quick check without hardware (host)

```sh
cd Work/fm1-base
python3 tools/build.py mworm --host
mworm/build/host/m-worm_host mworm/tests/basic.txt mworm/build/host

# core regressions
cd ../..
./scripts/run_core_tests.sh        # 28 PASS / 0 FAIL / 3 SKIP
```

The `-ffp-contract=off` flag in the tests is required: without it the compiler contracts multiply+add and the PCM reference drifts.

## 4. Device package

```sh
cd Work/fm1-base
python3 tools/build.py mworm
```

Output:

```text
Work/fm1-base/mworm/build/m-worm.fwsc   # identity FM-1_2100
```

Build figures: image `482828 B`, RAM `.data+.bss` `92280 / 98304`, pool `334984 / 344064`. Building this repository produces the bit-identical package: SHA256 `6b6a044d6e4644c48d2d643048675e422a0878a88177a745dcbec626d09b137d`.

## 5. Flashing

```sh
cd <repository root>
./flash.sh --info
./flash.sh Work/fm1-base/mworm/build/m-worm.fwsc
```

The first run creates `.flasher-venv` and installs `mido` + `python-rtmidi`. Use a data cable, keep the FM-1 powered on, and close other MIDI applications. The script asks for confirmation; `--yes` skips it.

## 6. Troubleshooting

- `python3 tools/build.py mworm --host` — fastest source check without the toolchain.
- `./flash.sh --info` — checks whether the FM-1 is visible.
- Stage 1 may normally report “OTA loader did not appear”, and stage 2 “FM-1 did not return after reboot”; power-cycle the device and run `./flash.sh --info` again.
- Do not modify the flashing protocol or tools: `mworm_flash.py`, `flash.sh`, `tools/build.py`, `web/fm1ota.js`, `web/fm1pkg.js`, `bsp/tools/*`.
