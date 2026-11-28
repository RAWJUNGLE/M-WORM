# M-WORM FM-1

**Open-source groovebox firmware for the M-VAVE FM-1 (JieLi AC791N).**

English version: [README_EN.md](README_EN.md). Схема звука: [RU](SCHEME_RU.md) / [EN](SCHEME_EN.md). Русская версия ниже.

---

## Что это сейчас

- **6 аудиодорожек = 6 монофонических голосов**, плюс 4 MIDI-дорожки. Пула голосов и стилинга нет: ретриггер перезапускает тот же голос.
- Три движка: **VA** (`WAVE 0..5`), **WAVETABLE** (`WAVE 6..57`) и **FM** (8 алгоритмов, операторы `C/A/B1/B2`).
- **BPM лежит в паттерне**; паттерны переключаются строго на границе такта.
- У каждой дорожки: свои часы (длина, направление, скорость, свинг), 4 LFO, свободный `ENV4`, p-lock/automation.
- На голос: 5 ADSR (`E0/E3` — AMP, `E1/E2` — фильтры, `E4` — свободная), 2 фильтра TPT SVF (`OFF/LP/HP/BP`), AMP DIST, VOL/PAN, TRANSIENT и 3 send-а (`DELAY/REVERB/CHORUS`).
- Рендер делится между **CPU0 и CPU1**: второй ядро считает часть голосов и реверб; при отказе есть откат на одно ядро.
- Выход: динамик через I2S/ALNK0, USB-аудио (мастер 2ch/24bit/48k или FX 6ch/16bit/44.1k) и виртуальный диск MSC (`WTABLE.WPT`, банки `.WPB`, паттерны `.pat`).

Полная схема сигнального тракта — **[SCHEME_RU.md](SCHEME_RU.md)** / **[SCHEME_EN.md](SCHEME_EN.md)**.

> Старые комментарии в коде и `Work/fm1-base/mworm/docs/MANUAL.md` местами говорят про 4 дорожки и «неиспользуемый CPU1» — это устарело. Актуально только то, что в коде `mworm/worm/`.

---

## Состав репозитория

```text
Work/fm1-base/           платформа fm1-base + BSP (включён со локальными правками)
  mworm/                  исходники прошивки M-WORM (app.c, worm/*.c, docs, web, tests)
  mworm/build/m-worm.fwsc готовый прошиваемый пакет FM-1_2100
  tools/, device/, bsp/   сборщик пакета, платформа, BSP
Work/tables/              40 исходных WAV для генерации встроенных wavetable
scripts/run_core_tests.sh регрессионные тесты ядра (host, -ffp-contract=off)
flash.sh, mworm_flash.py  прошивка по USB-MIDI (Felucca-протокол)
mworm_tool_RU/EN.html     веб-утилита для банков/паттернов
mworm_bank_tool.*         CLI для BANK_x.WPB
MANUAL_RU/EN.md           руководство пользователя
FLASHER.md                инструкция по прошивке
AGENTS.md                 правила для ИИ-агентов (прошивку и протокол менять нельзя)
```

**Не включено намеренно:** JieLi AC79 SDK, Linux toolchain, `.venv`, `.flasher-venv`, бэкапы, старые прототипы и бинарные образы стоковой прошивки. См. [THIRD_PARTY.md](THIRD_PARTY.md).

---

## Быстрый старт: прошить готовую прошивку

```sh
./flash.sh --info
./flash.sh Work/fm1-base/mworm/build/m-worm.fwsc
```

Нужен data-кабель, FM-1 включён, другие MIDI-программы закрыты. Первый запуск сам создаст `.flasher-venv` и поставит `mido` + `python-rtmidi`.

Пакет в репозитории:

```text
Work/fm1-base/mworm/build/m-worm.fwsc
SHA256 6b6a044d6e4644c48d2d643048675e422a0878a88177a745dcbec626d09b137d
Identity FM-1_2100
```

Пересборка из этого репозитория даёт бит-идентичный пакет (проверено host/device-сборкой).

Контрольные суммы всех инструментов — в `SHA256SUMS`.

Подробности: **[FLASHER.md](FLASHER.md)**. Прошивка производится только по явному запросу — скрипт всегда спрашивает подтверждение, `--yes` пропускает его.

---

## Сборка из исходников

Нужны:

- Python 3.11+ и `pip install -r requirements-build.txt` (Pillow нужен генератору шрифта);
- `AC79_SDK` — checkout JieLi AC79 AIoT SDK (в репозиторий не входит, не перераспределяется);
- `JIELI_TOOLCHAIN` — Linux x86-64 toolchain JieLi. На macOS сборка идёт через docker/linux/amd64 (подними `colima start --arch x86_64`);
- `cc` (host-тесты), `emcc` — только для web/rfm1 сборок.

```sh
cd Work/fm1-base

# Хост-симулятор (быстро, без toolchain)
python3 tools/build.py mworm --host
mworm/build/host/m-worm_host mworm/tests/basic.txt mworm/build/host

# Прошивка для устройства
AC79_SDK=/path/to/fw-AC79_AIoT_SDK \
JIELI_TOOLCHAIN=/path/to/jieli-linux-toolchain \
python3 tools/build.py mworm
```

Результат: `Work/fm1-base/mworm/build/m-worm.fwsc`.

Регрессии ядра (флаг `-ffp-contract=off` обязателен, иначе плавающая арифметика даёт лишние расхождения):

```sh
./scripts/run_core_tests.sh
# ожидаемо: 28 PASS / 0 FAIL / 3 SKIP
# SKIP: t_euclid, t_pattern_pick, t_store_tail (устаревшие тесты FM4)
```

---

## Утилиты

Открой `mworm_tool_RU.html` / `mworm_tool_EN.html` в браузере — это автономные страницы для банков, паттернов и wavetable.

```sh
python3 mworm_bank_tool.py list
python3 mworm_bank_tool.py show A 3
python3 mworm_bank_tool.py extract A 3 -o pat.pat
python3 mworm_bank_tool.py import pat.pat B 5
python3 mworm_bank_tool.py verify
./put_bank.command                 # положить скачанный BANK_x.WPB на диск FM-1
```

`mworm_tool_RU.html` / `mworm_tool_EN.html` — автономные HTML-файлы, просто открой их в браузере.

---

## Документация

- [MANUAL_RU.md](MANUAL_RU.md) / [MANUAL_EN.md](MANUAL_EN.md) — пользовательское руководство.
- [SCHEME_RU.md](SCHEME_RU.md) / [SCHEME_EN.md](SCHEME_EN.md) — как устроен звук.
- [FLASHER.md](FLASHER.md) — прошивка.
- [ХАРАКТЕРИСТИКИ.md](ХАРАКТЕРИСТИКИ.md) / [ХАРАКТЕРИСТИКИ_EN.md](ХАРАКТЕРИСТИКИ_EN.md) — спецификация.
- [ХОТКЕИ.txt](ХОТКЕИ.txt) — хоткеи по коду.
- [AGENTS.md](AGENTS.md) — запрет на изменение протокола/инструментов прошивки.

---

## Лицензия

Код M-WORM — **GPL-3.0-only** (`LICENSE`). Платформа `Work/fm1-base` — **MPL-2.0**, BSP и третьи части — под своими лицензиями; детали в `Work/fm1-base/LICENSING.md` и `Work/fm1-base/LICENSES/`. Сторонние/sdk-зависимости описаны в [THIRD_PARTY.md](THIRD_PARTY.md).
