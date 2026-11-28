#!/usr/bin/env bash
# Обёртка для mworm_flash.py: создаёт venv с mido/python-rtmidi (если нет) и запускает флешер.
#   ./flash.sh <file.fwsc>            прошить
#   ./flash.sh --info                  показать подключённый FM-1
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV="$ROOT/.flasher-venv"

if [ ! -x "$VENV/bin/python" ]; then
  echo "создаю venv + ставлю mido/python-rtmidi ..."
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install --quiet --upgrade pip
  "$VENV/bin/pip" install --quiet mido python-rtmidi
fi

exec "$VENV/bin/python" "$ROOT/mworm_flash.py" "$@"
