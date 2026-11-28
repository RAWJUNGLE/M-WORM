#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""M-WORM: просмотр и извлечение паттернов из файлов BANK_x.WPB (с USB-диска FM-1).

Банк — это '1BPW' (0x57504231 в LE) + 16 пар {u16 длина, запись паттерна} подряд, хвост 0xFF.
Запись паттерна — упаковка worm_store.c/pack_rec: 4 звуковые дорожки (секвенция,
режимы, патч, automation), глобальные параметры, затем хвост MIDI-дорожек.

Команды:
  list [путь]                   список банков и занятых слотов
  show BANK SLOT [-d путь]      что внутри паттерна (ноты, ручки, automation)
  extract BANK SLOT [-o файл]   вытащить один паттерн в .pat
  extract-all [путь] [-o каталог]
  import ФАЙЛ [BANK SLOT] [-d путь]   вставить .pat обратно в банк
  verify [путь]                 проверить, что все записи читаются

BANK — A..H или имя/путь к BANK_x.WPB. SLOT — 1..16.
Путь по умолчанию: /Volumes/M-WORM PATTERNS (если смонтирован), иначе текущий каталог.
Перед изменением банка рядом создаётся .bak-копия.

Примеры:
  python3 mworm_bank_tool.py list
  python3 mworm_bank_tool.py show A 3
  python3 mworm_bank_tool.py extract A 3 -o ~/Desktop/mypat.pat
  python3 mworm_bank_tool.py import ~/Desktop/mypat.pat B 5
"""

import argparse
import os
import re
import shutil
import struct
import sys
import zlib

# --- формат (совпадает с worm_store.c / worm.h) ---
NP = 175                 # число звуковых/глобальных параметров
NPB = (NP + 7) // 8      # 22 байта карты отличий
NMP = 50                 # значений на MIDI-дорожку
NMPB = (NMP + 7) // 8    # 7 байт
NTRK, NMTRK, NPAT, NBANK = 4, 4, 16, 8
BANK_SZ = 9 * 4096       # 36864
BANK_MAGIC = b"1BPW"   # 0x57504231 в little-endian (в прошивке комментарий 'WPB1' — порядок байт обратный)
PAT_MAGIC = b"MWPT"
PAT_VER = 1
PAT_HDR = struct.Struct("<4sHBBII")   # magic, ver, bank, slot, len, crc32
DEFAULT_DISK = "/Volumes/M-WORM PATTERNS"

# Имена параметров из worm.h (PLIST) — для читаемого вывода.
PLIST = [
    'ROOT', 'VEL', 'LEN', 'PROB', 'COND', 'FLT.T', 'LFO.T', 'PORT.T', 'PORTA', 'ALGO',
    'WAVE', 'PW', 'TUNE', 'OCT', 'DETUNE', 'SUB', 'LEVEL', 'WAVE2', 'PW2', 'TUNE2',
    'OCT2', 'DETUNE2', 'SUB2', 'LEVEL2', 'PITCH', 'PITCH', 'RATIO', 'FDBK', 'I.ENV', 'I.VEL',
    'RATIO', 'FDBK', 'I.ENV', 'I.VEL', 'HARMON', 'FOLD', 'MORPH', 'D/WET', 'DIST', 'PAN',
    'VOL', 'DRIVE', 'DELAY', 'REVERB', 'CHORUS', 'TIME', 'FDBK', 'FILT', 'MIX', 'SIZE',
    'DECAY', 'DAMP', 'MIX', 'RATE', 'DEPTH', 'DELAY', 'MIX', 'TRANS', 'CUTOFF', 'RESO',
    'TYPE', 'ENV', 'CUTOFF', 'RESO', 'TYPE', 'ENV', 'D/WET', 'D/WET', 'DEST', 'SPEED',
    'DEPTH', 'WAVE', 'MODE', 'SYNC', 'SLOPE', 'SMOOTH', 'SHIFT', 'DEST', 'SPEED', 'DEPTH',
    'WAVE', 'MODE', 'SYNC', 'SLOPE', 'SMOOTH', 'SHIFT', 'DEST', 'SPEED', 'DEPTH', 'WAVE',
    'MODE', 'SYNC', 'SLOPE', 'SMOOTH', 'SHIFT', 'DEST', 'SPEED', 'DEPTH', 'WAVE', 'MODE',
    'SYNC', 'SLOPE', 'SMOOTH', 'SHIFT', 'ATK', 'DEC', 'SUS', 'REL', 'ATK', 'DEC',
    'SUS', 'REL', 'ATK', 'DEC', 'SUS', 'REL', 'ATK', 'DEC', 'SUS', 'REL',
    'ATK', 'DEC', 'SUS', 'REL', 'DEST', 'DEPTH', 'DRIVE', 'PAN', 'VOL', 'DRIVE',
    'PAN', 'VOL', 'DRIVE', 'PAN', 'VOL', 'DRIVE', 'PAN', 'VOL', 'E.HITS', 'E.OFS',
    'E.LIMIT', 'E.MUTE', 'SCALE', 'ROOT', 'CHORD', 'TRANSP', 'CLICK', 'VOL', 'PRE', 'SOUND',
    '-', '-', '-', '-', 'DIR', 'LENGTH', 'SPEED', 'SWING', 'PAGES', 'VIEW',
    'STEP', 'mTIME', 'LEVEL', 'LEVEL', 'LEVEL', 'LEVEL', 'LEVEL', 'LEVEL', 'LEVEL', 'LEVEL',
    'ARPMOD', 'ARPSPD', 'ARPOCT', 'ARPGAT', 'QUANT',
]
MLIST = [
    'CHAN', 'PROG', 'BK.MSB', 'BK.LSB', 'VOL', 'PAN', 'QUANT', 'CHORD', 'MUTE', 'ARPMOD',
    'ARPSPD', 'ARPOCT', 'ARPGAT', 'CC', 'BASE', 'DEPTH', 'SPEED', 'WAVE', 'SYNC', 'PHASE',
    'CC', 'BASE', 'DEPTH', 'SPEED', 'WAVE', 'SYNC', 'PHASE', 'CC', 'BASE', 'DEPTH',
    'SPEED', 'WAVE', 'SYNC', 'PHASE', 'CC', 'BASE', 'DEPTH', 'SPEED', 'WAVE', 'SYNC',
    'PHASE', 'TO MIDI', 'RETRIG', 'R.LEN', 'R.VEL',
]
assert len(PLIST) == NP and len(MLIST) == 45
NOTE_NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")


def pname(i):
    return PLIST[i] if 0 <= i < NP else "P%d" % i


def mname(q):
    """индекс значения MIDI-дорожки -> имя (worm_mpid / worm_mpos)."""
    if q < 41:
        return MLIST[q]
    if q < 46:
        return PLIST[q - 41] + " (NOTE)"
    if q < NMP:
        return PLIST[138 + q - 46] + " (EUCL)"
    return "M%d" % q


def note_name(n):
    return "%s%d" % (NOTE_NAMES[n % 12], n // 12 - 1)


def s8(v):
    return v - 256 if v >= 128 else v


class Reader:
    def __init__(self, buf):
        self.b, self.i = buf, 0

    def need(self, n):
        if self.i + n > len(self.b):
            raise ValueError("запись паттерна обрезана (нужно %d байт, есть %d)" % (n, len(self.b) - self.i))

    def u8(self):
        self.need(1)
        v = self.b[self.i]
        self.i += 1
        return v

    def u16(self):
        self.need(2)
        v = self.b[self.i] | (self.b[self.i + 1] << 8)
        self.i += 2
        return v

    def take(self, n):
        self.need(n)
        v = self.b[self.i:self.i + n]
        self.i += n
        return v

    def vi(self):
        z = sh = 0
        while True:
            c = self.u8()
            z |= (c & 0x7F) << sh
            sh += 7
            if not (c & 0x80):
                break
            if sh > 28:
                raise ValueError("битовое число слишком длинное")
        return (z >> 1) if not (z & 1) else -((z >> 1) + 1)


def read_seq(r):
    n = r.u16()
    if n > 128:
        raise ValueError("слишком много шагов: %d" % n)
    steps = []
    for _ in range(n):
        raw = r.take(13)
        nl = raw[8]
        if nl > 6:
            raise ValueError("слишком много p-lock: %d" % nl)
        links = [(r.u8(), r.u16()) for _ in range(nl)]
        rv = (raw[1] >> 1) & 0x7F
        if rv & 0x40:
            rv -= 128
        steps.append(dict(step=raw[0] & 0x7F, on=raw[0] & 1, rtv=rv,
                          note=raw[2], vel=raw[3], length=raw[4] & 7, rtr=(raw[4] >> 3) & 15,
                          prob=raw[5], cond=raw[6] & 15, rtl=(raw[6] >> 4) & 7,
                          mt=s8(raw[7]), chord=list(map(s8, raw[9:13])), links=links))
    return steps


def read_patch(r, count):
    bm = r.take((count + 7) // 8)
    out = {}
    for i in range(count):
        if bm[i >> 3] & (1 << (i & 7)):
            out[i] = r.vi()
    return out


def parse_pattern(rec):
    r = Reader(rec)
    tracks = []
    for _ in range(NTRK):
        steps = read_seq(r)
        modes = dict(zip(("len", "dir", "spd", "swg", "pg", "vp", "mute"), [r.u8() for _ in range(7)]))
        patch = read_patch(r, NP)
        na = r.u8()
        aut = [(r.u8(), r.u8(), r.u16()) for _ in range(min(na, 64))]
        tracks.append(dict(steps=steps, modes=modes, patch=patch, aut=aut))
    gpatch = read_patch(r, NP)
    if r.i + 2 <= len(rec):
        mmask, amo = r.u8(), r.u8()
    else:
        mmask, amo = 0, 0                    # старая запись без MIDI-хвоста
    mtrks = []
    for k in range(NMTRK):
        if mmask & (1 << k):
            steps = read_seq(r)
            modes = dict(zip(("len", "dir", "spd", "swg", "pg", "vp"), [r.u8() for _ in range(6)]))
            patch = read_patch(r, NMP)
            mtrks.append(dict(k=k, steps=steps, modes=modes, patch=patch))
    return dict(tracks=tracks, gpatch=gpatch, amo=amo, mtrks=mtrks)


# --- банк ---

def bank_path(arg, disk):
    if not arg:
        raise ValueError("нужен bank (A..H)")
    if len(arg) == 1 and arg.upper() in "ABCDEFGH":
        return os.path.join(disk, "BANK_%s.WPB" % arg.upper())
    return arg


def read_bank(path):
    data = open(path, "rb").read()
    if len(data) < 36 or data[:4] != BANK_MAGIC:
        raise ValueError("%s: не банк M-WORM (ожидался 1BPW)" % path)
    off, recs = 4, []
    for _ in range(NPAT):                       # {u16 len, bytes} подряд, как в worm_store.c/parse_img
        if off + 2 > len(data):
            raise ValueError("%s: обрезана длина записи" % path)
        n = data[off] | (data[off + 1] << 8)
        off += 2
        if off + n > len(data):
            raise ValueError("%s: запись выходит за размер файла" % path)
        recs.append(data[off:off + n])
        off += n
    return recs


def write_bank(path, recs):
    total = 4 + sum(2 + len(r) for r in recs)
    if total > BANK_SZ:
        raise ValueError("банк переполнен: %d из %d байт" % (total, BANK_SZ))
    out = bytearray(BANK_MAGIC)
    for r in recs:
        out += struct.pack("<H", len(r))
        out += r
    out += b"\xFF" * (BANK_SZ - len(out))
    if os.path.exists(path):
        bak = path + ".bak"
        shutil.copy2(path, bak)
        print("резервная копия: " + bak)
    with open(path, "wb") as f:
        f.write(out)


def pick_disk(arg):
    if arg:
        return arg
    if os.path.isdir(DEFAULT_DISK):
        return DEFAULT_DISK
    return "."


def bank_letters(disk):
    found = []
    for c in "ABCDEFGH":
        p = os.path.join(disk, "BANK_%s.WPB" % c)
        if os.path.exists(p):
            found.append((c, p))
    if not found:
        raise ValueError("в %s не найдено BANK_x.WPB" % disk)
    return found


# --- команды ---

def cmd_list(a):
    disk = pick_disk(a.disk)
    total = 0
    for c, path in bank_letters(disk):
        recs = read_bank(path)
        used = [(i + 1, len(r)) for i, r in enumerate(recs) if r]
        total += len(used)
        print("BANK %s  %-40s  %2d/16 паттернов, %d байт" % (c, os.path.basename(path), len(used), sum(n for _, n in used)))
        if used:
            print("   " + "  ".join("%02d:%d" % (s, n) for s, n in used))
    print("ИТОГО: %d паттернов" % total)


def fmt_patch(patch, namer):
    if not patch:
        return ""
    return " ".join("%s=%d" % (namer(i), v) for i, v in sorted(patch.items()))


def cmd_show(a):
    path = bank_path(a.bank, pick_disk(a.disk))
    recs = read_bank(path)
    slot = a.slot - 1
    if not (0 <= slot < NPAT):
        raise ValueError("SLOT должен быть 1..16")
    rec = recs[slot]
    print("BANK %s / PATTERN %02d   %d байт" % (a.bank.upper(), a.slot, len(rec)))
    if not rec:
        print("  (пусто — заводское состояние)")
        return
    p = parse_pattern(rec)
    for t, tr in enumerate(p["tracks"], 1):
        m = tr["modes"]
        print("  T%d: %d шагов, len=%d dir=%d spd=%d swg=%d pg=%d vp=%d mute=%d" %
              (t, len(tr["steps"]), m["len"], m["dir"], m["spd"], m["swg"], m["pg"], m["vp"], m["mute"]))
        if tr["steps"]:
            ss = []
            for s in tr["steps"][:48]:
                ss.append("%02d:%s/%d/%d" % (s["step"] + 1, note_name(s["note"]), s["vel"], s["length"] + 1))
            print("      " + " ".join(ss) + (" ..." if len(tr["steps"]) > 48 else ""))
        pl = fmt_patch(tr["patch"], pname)
        if pl:
            print("      звук: " + pl)
        if tr["aut"]:
            print("      automation: %d точек (%s)" % (len(tr["aut"]), ", ".join(pname(i) for _, i, _ in tr["aut"][:8])))
    gl = fmt_patch(p["gpatch"], pname)
    if gl:
        print("  GLOBAL: " + gl)
    print("  TO MIDI=%d, MIDI-дорожек=%d" % (p["amo"], len(p["mtrks"])))
    for mt in p["mtrks"]:
        print("      M%d: %d шагов, %s" % (mt["k"] + 1, len(mt["steps"]), fmt_patch(mt["patch"], mname)))


def cmd_extract(a):
    path = bank_path(a.bank, pick_disk(a.disk))
    recs = read_bank(path)
    slot = a.slot - 1
    if not (0 <= slot < NPAT):
        raise ValueError("SLOT должен быть 1..16")
    rec = recs[slot]
    if not rec:
        raise ValueError("слот %d пуст — нечего извлекать" % a.slot)
    # проверить, что запись вообще читается
    parse_pattern(rec)
    bank_i = "ABCDEFGH".index(a.bank.upper())
    out = a.out or os.path.join(os.getcwd(), "PAT_%s%02d.pat" % (a.bank.upper(), a.slot))
    with open(out, "wb") as f:
        f.write(PAT_HDR.pack(PAT_MAGIC, PAT_VER, bank_i, slot, len(rec), zlib.crc32(rec)))
        f.write(rec)
    print("извлечён %s / %02d -> %s (%d байт)" % (a.bank.upper(), a.slot, out, len(rec)))


def cmd_extract_all(a):
    disk = pick_disk(a.disk)
    outdir = a.out or os.path.join(os.getcwd(), "mworm_patterns")
    os.makedirs(outdir, exist_ok=True)
    n = 0
    for c, path in bank_letters(disk):
        recs = read_bank(path)
        for i, rec in enumerate(recs):
            if not rec:
                continue
            parse_pattern(rec)
            out = os.path.join(outdir, "PAT_%s%02d.pat" % (c, i + 1))
            with open(out, "wb") as f:
                f.write(PAT_HDR.pack(PAT_MAGIC, PAT_VER, "ABCDEFGH".index(c), i, len(rec), zlib.crc32(rec)))
                f.write(rec)
            n += 1
    print("извлечено %d паттернов в %s" % (n, outdir))


def cmd_import(a):
    raw = open(a.file, "rb").read()
    if len(raw) < PAT_HDR.size:
        raise ValueError("%s: слишком короткий .pat" % a.file)
    magic, ver, bank_i, slot, length, crc = PAT_HDR.unpack_from(raw)
    if magic != PAT_MAGIC or ver != PAT_VER:
        raise ValueError("%s: не паттерн M-WORM (magic/версия)" % a.file)
    rec = raw[PAT_HDR.size:]
    if len(rec) != length or zlib.crc32(rec) != crc:
        raise ValueError("%s: длина/CRC не совпадают — файл повреждён" % a.file)
    parse_pattern(rec)
    bank = a.bank or "ABCDEFGH"[bank_i]
    sl = (a.slot if a.slot is not None else slot + 1) - 1
    if not (0 <= sl < NPAT):
        raise ValueError("SLOT должен быть 1..16")
    path = bank_path(bank, pick_disk(a.disk))
    recs = read_bank(path)
    recs[sl] = rec
    write_bank(path, recs)
    print("вставлено в BANK %s / %02d" % (bank.upper(), sl + 1))


def cmd_verify(a):
    disk = pick_disk(a.disk)
    bad = 0
    for c, path in bank_letters(disk):
        recs = read_bank(path)
        ok = 0
        for i, rec in enumerate(recs):
            if not rec:
                continue
            try:
                parse_pattern(rec)
                ok += 1
            except Exception as e:
                bad += 1
                print("ОШИБКА %s/%02d: %s" % (c, i + 1, e))
        print("BANK %s: %d/%d записей читаются" % (c, ok, sum(1 for r in recs if r)))
    print("ПРОВЕРКА: %s" % ("OK" if not bad else "%d ошибок" % bad))
    return bad


def main():
    ap = argparse.ArgumentParser(description="M-WORM: паттерны из BANK_x.WPB")
    sp = ap.add_subparsers(dest="cmd")
    p = sp.add_parser("list", help="список банков и паттернов")
    p.add_argument("disk", nargs="?")
    p.set_defaults(fn=cmd_list)
    p = sp.add_parser("show", help="показать содержимое паттерна")
    p.add_argument("bank")
    p.add_argument("slot", type=int)
    p.add_argument("-d", "--disk")
    p.set_defaults(fn=cmd_show)
    p = sp.add_parser("extract", help="извлечь паттерн в .pat")
    p.add_argument("bank")
    p.add_argument("slot", type=int)
    p.add_argument("-o", "--out")
    p.add_argument("-d", "--disk")
    p.set_defaults(fn=cmd_extract)
    p = sp.add_parser("extract-all", help="извлечь все паттерны")
    p.add_argument("disk", nargs="?")
    p.add_argument("-o", "--out")
    p.set_defaults(fn=cmd_extract_all)
    p = sp.add_parser("import", help="вставить .pat в банк")
    p.add_argument("file")
    p.add_argument("bank", nargs="?")
    p.add_argument("slot", nargs="?", type=int)
    p.add_argument("-d", "--disk")
    p.set_defaults(fn=cmd_import)
    p = sp.add_parser("verify", help="проверить все банки")
    p.add_argument("disk", nargs="?")
    p.set_defaults(fn=cmd_verify)
    a = ap.parse_args()
    if not a.cmd:
        ap.print_help()
        return 1
    try:
        return a.fn(a) or 0
    except (ValueError, OSError, struct.error) as e:
        print("ошибка: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
