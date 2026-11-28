# Third-party components and things intentionally not shipped

## Licenses

| Component | License | Where |
| --- | --- | --- |
| M-WORM application (`mworm/app.c`, `mworm/worm/*`) | GPL-3.0-only | root `LICENSE` |
| fm1-base platform and its tools | MPL-2.0 | `Work/fm1-base/LICENSE`, `Work/fm1-base/LICENSING.md` |
| felucca-bsp (vendored into `Work/fm1-base/bsp`, **with local FM-1 fixes**) | MPL-2.0; parts Apache-2.0 / BSD-2-Clause | `bsp/LICENSE`, `bsp/NOTICE` |
| Barlow Semi Condensed, Terminus | SIL OFL 1.1 | `Work/fm1-base/assets/fonts/Barlow-OFL.txt`, `Terminus-LICENSE.txt` |
| Press Start 2P (`PressStart2P-Regular.ttf`) | SIL OFL 1.1 | font by CodeMan38; license text: <https://scripts.sil.org/OFL> |
| m-bootloader, web installer parts | MIT / as marked | `Work/fm1-base/web/m-bootloader/` |

All SPDX headers and upstream license texts were kept as-is. `Work/fm1-base/bsp` is included as plain source, not as a git submodule, because the local USB/UAC/loader changes must be part of this repository for a reproducible build.

## Not included, on purpose

- **JieLi AC79 AIoT SDK** — binary/boot files (`uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`), BT/WiFi libraries. Set `AC79_SDK` to your own checkout. Never commit SDK binaries.
- **JieLi Linux x86-64 toolchain** — set `JIELI_TOOLCHAIN`; on macOS use a linux/amd64 container.
- **Python virtual environments** (`.venv`, `.flasher-venv`) and build caches.
- **Backups, old prototypes, stock vendor firmware images and device dumps.**
- **SDK/radio binaries** copied by the build into `Work/fm1-base/mworm/build/` — regenerated on every build.

## Wavetable source files

`Work/tables/*.WAV` are the 40 source cycles used by `Work/fm1-base/tools/gen_wt_data.py` to generate the built-in table data in `mworm/worm/worm_wt_data.c` bit-for-bit. `Work/fm1-base/tables_src/*.WAV` feed `tools/gen_wt_main.py` (5 MAIN tables). They are kept for reproducibility only; the firmware already contains the generated C data. Check the rights for the original wavetable collections before redistributing the raw WAV files in a public release.
