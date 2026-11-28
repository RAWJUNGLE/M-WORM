# Сборка прошивки M-WORM FM-1

Краткая инструкция: собрать пакет `m-worm.fwsc` и записать его на FM-1.

## 1. Что нужно

- Python 3.11+ и `Pillow` (генератор шрифта): `python3 -m pip install Pillow`
- **JieLi AC79 AIoT SDK** — только локальный checkout, в репозиторий не входит.
- **JieLi Linux x86-64 toolchain** (`JIELI_TOOLCHAIN`). На macOS сборка идёт в linux/amd64-контейнере, поэтому нужен Docker + colima.

## 2. Окружение

```sh
# macOS: поднять VM, если она ещё не запущена
colima start --vm-type vz --vz-rosetta --cpu 4 --memory 6

export AC79_SDK=~/fw-AC79_AIoT_SDK
export JIELI_TOOLCHAIN=/path/to/jieli-linux-toolchain
```

Пути поменяй под своё окружение.

## 3. Быстрая проверка без железа (host)

```sh
cd Work/fm1-base
python3 tools/build.py mworm --host
mworm/build/host/m-worm_host mworm/tests/basic.txt mworm/build/host

# регрессии ядра
cd ../..
./scripts/run_core_tests.sh        # 28 PASS / 0 FAIL / 3 SKIP
```

Флаг `-ffp-contract=off` в тестах обязателен — без него компилятор сворачивает умножение со сложением и PCM-эталон расходится.

## 4. Сборка пакета для устройства

```sh
cd Work/fm1-base
python3 tools/build.py mworm
```

Результат:

```text
Work/fm1-base/mworm/build/m-worm.fwsc   # identity FM-1_239300
```

Контрольные числа сборки: образ `482828 B`, RAM `.data+.bss` `92280 / 98304`, pool `334984 / 344064`. Если сборка из этого репозитория, пакет бит-идентичен приложенному: SHA256 `0629630fdb0df77ef4ab8dd86ce7d0c5644f13041e53050037bee435aa41d80a`.

## 5. Прошивка

```sh
cd <корень репозитория>
./flash.sh --info
./flash.sh Work/fm1-base/mworm/build/m-worm.fwsc
```

Первый запуск создаст `.flasher-venv` и поставит `mido` + `python-rtmidi`. Нужен data-кабель, FM-1 включён, другие MIDI-программы закрыты. Скрипт спрашивает подтверждение; `--yes` пропускает запрос.

## 6. Если что-то не так

- `python3 tools/build.py mworm --host` — самая быстрая проверка исходников без toolchain.
- `./flash.sh --info` — видит ли система FM-1.
- Стадия 1 прошивки может штатно писать «OTA-загрузчик не появился», стадия 2 — «FM-1 не вернулся после перезагрузки»; после этого выключи/включи питание и снова проверь `./flash.sh --info`.
- Протокол и инструменты прошивки менять нельзя: `mworm_flash.py`, `flash.sh`, `tools/build.py`, `web/fm1ota.js`, `web/fm1pkg.js`, `bsp/tools/*`.
