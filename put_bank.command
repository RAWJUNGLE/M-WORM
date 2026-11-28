#!/bin/bash
# M-WORM: положить скачанный BANK_x.WPB на диск FM-1.
# Утилита сохраняет банк в Downloads/Documents; этот скрипт пишет его поверх
# существующего файла на /Volumes/M-WORM — без создания временного файла.
set -u

VOL="/Volumes/M-WORM"
if [ ! -d "$VOL" ]; then
  echo "FM-1 не смонтирован. Включите GLO → USB → MASS STORAGE и повторите."
  exit 1
fi

# Берём самый свежий BANK_?.WPB ровно 36864 байт из Downloads/Documents
FILE=$(find "$HOME/Downloads" "$HOME/Documents" -maxdepth 1 -type f -iname 'BANK_?.WPB' -size 36864c -exec stat -f '%m %N' {} + 2>/dev/null | sort -nr | head -1 | cut -d' ' -f2-)
if [ -z "$FILE" ]; then
  echo "Не найден BANK_?.WPB размером 36864 байт в ~/Downloads или ~/Documents."
  echo "Сначала нажмите «Скачать банк» в mworm_tool."
  exit 1
fi

NAME=$(basename "$FILE")
echo "Файл:   $FILE"
echo "Цель:   $VOL/$NAME"

if cp -f "$FILE" "$VOL/$NAME"; then
  SZ=$(stat -f %z "$VOL/$NAME" 2>/dev/null || echo '?')
  if [ "$SZ" = "36864" ]; then
    echo "Готово: $NAME записан на FM-1 (36864 байт)."
    echo "Переключите FM-1 в AUDIO + MIDI, чтобы банк подхватился."
  else
    echo "ВНИМАНИЕ: размер после копирования $SZ, ожидалось 36864."
    exit 1
  fi
else
  echo "Ошибка копирования. Проверьте, что диск M-WORM смонтирован и файл не занят."
  exit 1
fi
