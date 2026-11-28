#!/usr/bin/env python3
"""M-WORM flasher — прошивает .fwsc на M-VAVE FM-1 по USB-MIDI.

Работает с Felucca-прошивками (M-WORM, MNM-1, Hortator и др.), в т.ч. когда на
FM-1 уже стоит MNM-1/Felucca. Двухстадийный device-pull протокол (как у
fm1_install.py из fm1-base/MNM-1):

  1. Работающая прошивка проверяет пакет и запускает свой OTA-загрузчик.
  2. Загрузчик читает весь пакет и перезаписывает флеш.

Требует mido + python-rtmidi.

  python3 mworm_flash.py <file.fwsc>          # прошить
  python3 mworm_flash.py --info                # показать подключённый FM-1
  python3 mworm_flash.py <file.fwsc> --port FM-1 --yes   # без подтверждения

Data-кабель, без хаба, закрой другие MIDI-программы.
"""
import argparse
import hashlib
import queue
import re
import sys
import time

try:
    import mido
except ImportError:
    sys.exit("нужны mido + python-rtmidi:\n  pip install mido python-rtmidi")

# ---------------------------------------------------------------- константы ---
HS_QUERY  = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
UPGRADE   = bytes([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7])
FINISH_CHECK, FINISH_WRITE = 0xE0000000, 0xF0000000
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
MAXDATA = 512
LOADER_MARK = b"FELUCCA-LOADER-1"
PORT_RE = re.compile(r"fm-1|felucca|hortator|mnm|ota|composite|sinco|usb-midi", re.I)

# тайминги (сек)
DELAY = {"open": 0.3, "start": 2.0, "reply": 0.01, "loader": 3.0, "reboot": 3.0,
         "retry": 1.0, "poll": 1.0, "hs": 1.0, "idle_check": 8.0, "idle_write": 180.0,
         "wait_loader": 30.0, "wait_reboot": 40.0}


# ---------------------------------------------------------------- 7-бит ---
def pack7(data):
    out, acc, nb = bytearray(), 0, 0
    for b in data:
        acc |= (b & 0xFF) << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(data):
    out, acc, nb = bytearray(), 0, 0
    for b in data:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


# ---------------------------------------------------------------- пакет ---
def product_of(raw):
    if len(raw) < BLOCKS * BLK:
        return None
    return "".join(chr((raw[i * BLK + KEEP] - i - 1) & 0xFF)
                   for i in range(BLOCKS) if raw[i * BLK + KEEP] != 0x7D)


def logical_image(raw):
    out = bytearray()
    for i in range(BLOCKS):
        out += raw[i * BLK:i * BLK + KEEP]
    return bytes(out + raw[BLOCKS * BLK:])


# ---------------------------------------------------------------- MIDI ---
class Link:
    def __init__(self, in_name, out_name):
        self.name = in_name
        self.q = queue.Queue()
        self.inp = mido.open_input(in_name, callback=self._rx)
        try:
            self.out = mido.open_output(out_name)
        except Exception:
            self.inp.close()
            raise
        time.sleep(DELAY["open"])   # CoreMIDI может уронить сообщение сразу после открытия

    def _rx(self, msg):
        if msg.type == "sysex":
            self.q.put(bytes([0xF0, *msg.data, 0xF7]))

    @property
    def lost(self):
        try:
            return self.name not in mido.get_input_names()
        except Exception:
            return True

    def send(self, pkt):
        try:
            self.out.send(mido.Message("sysex", data=pkt[1:-1]))
            return True
        except Exception:
            return False

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        for p in (self.inp, self.out):
            try:
                p.close()
            except Exception:
                pass


def pair_output(name, outs):
    if name in outs:
        return name
    base = re.sub(r"\s+\d+$", "", name)
    same = [o for o in outs if re.sub(r"\s+\d+$", "", o) == base]
    if len(same) == 1:
        return same[0]
    return outs[0] if len(outs) == 1 else None


# ---------------------------------------------------------------- протокол ---
def response(addr, data, fl=0):
    n = len(data)
    body = bytearray([0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl])
    body += addr.to_bytes(4, "little") + bytes([n & 0xFF, (n >> 8) & 0xFF, 0]) + bytes(data)
    body.append(~sum(body[6:]) & 0xFF)
    return b"\xF0" + pack7(bytes(body)) + b"\xF7"


def parse_request(pkt):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    u = unpack7(pkt[1:-1])
    if len(u) != 15 or u[:3] != b"\x00\x59\x30" or ~sum(u[6:14]) & 0xFF != u[14]:
        return None
    return u[6], int.from_bytes(u[7:11], "little"), int.from_bytes(u[11:14], "little")


class Identity:
    def __init__(self, text, model, version):
        self.text, self.model, self.version = text, model, version

    @property
    def loader(self):
        return self.model.lower().startswith("ota-")


def parse_identity(pkt):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[1:-1])
    if len(d) != 34 or d[:3] != b"\x00\x59\x11":
        return None
    txt = d[6:33].rstrip(b"\0").decode("latin-1")
    m = re.fullmatch(r"([^_]+)_(\d+)", txt)
    return Identity(txt, m[1], int(m[2])) if m else None


def model_of(text):
    return text.split("_")[0].removeprefix("ota-")


def handshake(link, tries=3):
    link.drain()
    for _ in range(tries):
        if not link.send(HS_QUERY):
            return None
        end = time.monotonic() + DELAY["hs"]
        while time.monotonic() < end:
            p = link.read(end - time.monotonic())
            if p is None:
                break
            ident = parse_identity(p)
            if ident:
                return ident
    return None


# ---------------------------------------------------------------- updater ---
class Updater:
    def __init__(self, port=None):
        self.port = port

    def candidates(self):
        names = mido.get_input_names()
        if self.port:
            return [n for n in names if self.port.lower() in n.lower()]
        return [n for n in names if PORT_RE.search(n)]

    def find(self, want=None):
        outs = mido.get_output_names()
        for name in self.candidates():
            out = pair_output(name, outs)
            if not out:
                continue
            try:
                link = Link(name, out)
            except Exception:
                continue
            ident = handshake(link, 2)
            if ident and (want is None or want(ident)):
                return Device(link, ident, name)
            link.close()
        return None

    def wait_for(self, want, secs):
        end = time.monotonic() + secs
        while time.monotonic() < end:
            dev = self.find(want)
            if dev:
                return dev
            time.sleep(DELAY["retry"])
        return None

    def serve(self, link, image, finish, idle, progress):
        served, last = 0, time.monotonic()
        while True:
            pkt = link.read(DELAY["poll"])
            if pkt is None:
                if link.lost:
                    return served, False, True
                if time.monotonic() - last > idle:
                    return served, False, False
                continue
            r = parse_request(pkt)
            if not r:
                continue
            fl, addr, n = r
            last = time.monotonic()
            if addr in (FINISH_CHECK, FINISH_WRITE):
                link.send(response(addr, b"success\0"))
                if addr == finish:
                    return served, True, False
                continue
            if n > MAXDATA or addr + n > len(image):
                print(f"  ошибка: запрос {addr:#x}+{n} за границей пакета", file=sys.stderr)
                return served, False, False
            time.sleep(DELAY["reply"])
            if not link.send(response(addr, image[addr:addr + n], fl)):
                return served, False, True
            served += 1
            progress(served, addr + n)

    def check(self, dev, image, step):
        step("start", dev.id.text)
        dev.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(dev.link, image, FINISH_CHECK, DELAY["idle_check"],
                                            lambda k, e: step("check", k))
        dev.link.close()
        if not finished:
            raise RuntimeError("FM-1 перестал отвечать до запуска загрузчика. Переподключи USB и повтори.")
        step("loader")
        time.sleep(DELAY["loader"])
        ota = self.wait_for(lambda i: i.loader, DELAY["wait_loader"])
        if not ota:
            raise RuntimeError("OTA-загрузчик не появился. Переподключи USB и повтори "
                               "(устройство останется в режиме обновления).")
        return ota

    def write(self, ota, image, step):
        step("write", 0)
        ota.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        total = len(image)
        served, finished, lost = self.serve(ota.link, image, FINISH_WRITE, DELAY["idle_write"],
                                            lambda k, e: step("write", min(99, e * 100 // total)))
        ota.link.close()
        if not finished:
            raise RuntimeError("загрузчик перестал отвечать. Переподключи USB и повтори — запись продолжится.")
        step("write", 100)

    def verify(self, product, step):
        step("reboot")
        time.sleep(DELAY["reboot"])
        back = self.wait_for(lambda i: not i.loader, DELAY["wait_reboot"])
        if not back:
            raise RuntimeError("FM-1 не вернулся после перезагрузки — выключи и включи питание.")
        back.link.close()
        if back.id.text != product:
            print(f"  записано, но FM-1 сообщает {back.id.text} (ожидалось {product}). "
                  f"Перезагрузи питание.", file=sys.stderr)
        else:
            step("done", back.id.text)

    def install(self, dev, image, product, step=lambda *a: None):
        if model_of(dev.id.text) != model_of(product):
            dev.link.close()
            raise RuntimeError(f"устройство — {dev.id.text}, а пакет для {product}")
        ota = dev if dev.id.loader else self.check(dev, image, step)
        self.write(ota, image, step)
        self.verify(product, step)


class Device:
    def __init__(self, link, ident, name):
        self.link, self.id, self.name = link, ident, name


class Progress:
    def __init__(self):
        self.line = False

    def __call__(self, k, a=None):
        if k == "check":
            self.put(f"проверка пакета ({a} чтений)")
        elif k == "write":
            self.put(f"запись {a:3d}%")
        else:
            self.end()
            msg = {"start": f"запуск обновления на {a}", "loader": "переключение в режим обновления",
                   "reboot": "перезагрузка", "done": f"готово: FM-1 работает на {a}"}[k]
            print(msg, flush=True)

    def put(self, s):
        print(f"\r  {s}   ", end="", flush=True)
        self.line = True

    def end(self):
        if self.line:
            print(flush=True)
            self.line = False


# ---------------------------------------------------------------- CLI ---
def install(path, port, yes):
    raw = open(path, "rb").read()
    product = product_of(raw)
    if not product or not re.fullmatch(r"[^_]+_\d+", product):
        sys.exit(f"{path}: не FM-1 пакет (identity {product!r})")
    if LOADER_MARK not in raw:
        print(f"внимание: в пакете нет загрузчика Felucca (identity {product})", file=sys.stderr)
        if not yes and input("Всё равно прошить? [y/N] ").strip().lower() != "y":
            sys.exit("отменено")
    image = logical_image(raw)
    print(f"пакет:   {product}  ({len(raw)} байт)")

    up = Updater(port)
    print("поиск FM-1 (подключи по USB, data-кабель, закрой другие MIDI-программы) ...")
    dev = up.find()
    if not dev:
        names = mido.get_input_names()
        sys.exit(f"FM-1 не найден.\n  MIDI-входы: {', '.join(names) if names else '(нет)'}")
    print(f"устройство: {dev.id.text}  ({dev.name})"
          + ("  [режим обновления — допишу запись]" if dev.id.loader else ""))

    if not yes and input("Прошить? Не отключай FM-1 во время записи. [y/N] ").strip().lower() != "y":
        dev.link.close()
        sys.exit("отменено")

    prog = Progress()
    try:
        up.install(dev, image, product, prog)
    except RuntimeError as e:
        prog.end()
        sys.exit(f"ошибка: {e}")


def info():
    up = Updater()
    dev = up.find()
    if not dev:
        sys.exit("FM-1 не найден.")
    mode = "режим обновления (обновление не закончено)" if dev.id.loader else "работает"
    print(f"{dev.id.text}  [{mode}]  порт: {dev.name}")
    dev.link.close()


def main():
    ap = argparse.ArgumentParser(description="Прошивка FM-1 (.fwsc) по USB-MIDI (Felucca)")
    ap.add_argument("package", nargs="?", help="файл .fwsc")
    ap.add_argument("--port", help="часть имени MIDI-порта")
    ap.add_argument("--yes", action="store_true", help="без подтверждения")
    ap.add_argument("--info", action="store_true", help="показать подключённый FM-1")
    args = ap.parse_args()

    if args.info:
        info()
        return
    if not args.package:
        ap.error("укажи файл .fwsc или --info")
    install(args.package, args.port, args.yes)


if __name__ == "__main__":
    main()
