#!/usr/bin/env python3
"""
sysview_record.py - SystemView kaydini SystemView uygulamasi olmadan alir

SystemView uygulamasinin canli kaydi bu kurulumda (J-Link OB, S5'te ~95 KB/s)
"Stop"ta donuyor ve CPU'yu durdurulmus birakabiliyor. Bu arac SystemView'in
karta gonderdigi komutlari (START, GET_MODULE, STOP) J-Link Commander ile RTT
asagi tamponuna kendisi yazar, akisi J-Link RTT Logger ile dosyaya alir ve
SystemView'de File -> Load Recording ile acilabilen bir .SVDat uretir.

Akis:
  1. RTT Logger SystemView kanalini (1) okumaya baslar.
  2. STOP -> tampon bosalir -> START + GET_MODULE 0 (sistem bilgisi, gorev
     listesi ve BTN modul aciklamasi akisin basina yazilir).
  3. Kayit sirasinda J-Link'e baska oturum acilmaz (aciliris aninda logger
     okuyamaz, tampon dolar). Sure dolunca once logger durur, sonra STOP.
  4. Dosya START'in senkron dizisinden (10 x 0x00 + TRACE_START) kirpilir,
     cozulerek dogrulanir (dusen olay, sure, EXTI0 / BTN ACCEPT sayilari).

Kullanim (SystemView uygulamasi KAPALI olmali):
    python sysview_record.py A_S5
    python sysview_record.py B_S5 --sure 60

Cikti: analysis/systemview/<ad>.SVDat
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
import threading
import time
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAP_FILE = ROOT / "firmware/EWARM/Debug/List/presstrace_sched.map"
OUT_DIR = ROOT / "analysis/systemview"

DEVICE = "STM32F407VG"
SPEED_KHZ = 4000
SYSVIEW_CHANNEL = 1  # SystemView RTT kanali (0 = Terminal)
CPU_HZ = 168_000_000

CMD_START, CMD_STOP, CMD_GET_MODULE = 0x01, 0x02, 0x80
SYNC = bytes(10) + b"\x0a"  # 10 x NOP + TRACE_START

EXTI0_ISR = 22  # SystemView kesme numarasi (I#22 = EXTI0)
BTN_EVENT_OFFSET = 512  # app_trace modulunun ilk olayi (ACCEPT)
REPEAT_WINDOW_S = 0.030  # main.h BUTTON_REPEAT_WINDOW_US: bu sureden yakin kenarlar sicrama


# --------------------------------------------------------------------------
# J-Link
# --------------------------------------------------------------------------

def jlink_dir() -> Path:
    try:
        import winreg
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\SEGGER\J-Link") as k:
            p = Path(winreg.QueryValueEx(k, "InstallPath")[0])
            if (p / "JLink.exe").exists():
                return p
    except OSError:
        pass
    sys.exit("J-Link kurulumu bulunamadi (HKCU\\Software\\SEGGER\\J-Link\\InstallPath).")


def commander(jdir: Path, lines: list[str]) -> dict[int, int]:
    """J-Link Commander'i komut dosyasiyla calistirir; mem32 ciktisini
    adres -> 32 bit deger olarak dondurur (Commander satir basina 4 kelime yazar)."""
    with tempfile.NamedTemporaryFile("w", suffix=".jlink", delete=False) as f:
        f.write("\n".join(lines + ["qc"]) + "\n")
        script = f.name
    try:
        r = subprocess.run(
            [str(jdir / "JLink.exe"), "-device", DEVICE, "-if", "SWD", "-speed", str(SPEED_KHZ),
             "-autoconnect", "1", "-NoGui", "1", "-ExitOnError", "1", "-CommandFile", script],
            capture_output=True, text=True, timeout=30)
    finally:
        Path(script).unlink(missing_ok=True)
    words: dict[int, int] = {}
    for m in re.finditer(r"^([0-9A-F]{8}) = ((?:[0-9A-F]{8} ?)+)", r.stdout, re.M):
        for k, x in enumerate(m.group(2).split()):
            words[int(m.group(1), 16) + 4 * k] = int(x, 16)
    if not words and lines and lines[0].startswith("mem32"):
        sys.exit("J-Link Commander karti okuyamadi:\n" + r.stdout[-800:])
    return words


def map_symbol(name: str) -> int:
    m = re.search(rf"^{name}\s+0x([0-9a-f']+)", MAP_FILE.read_text(errors="replace"), re.M)
    if not m:
        sys.exit(f"{name} bulunamadi: {MAP_FILE} (USE_SYSVIEW 1 ile derlendi mi?)")
    return int(m.group(1).replace("'", ""), 16)


class DownChannel:
    """SystemView'in RTT asagi tamponu (aDown[1]): PC -> kart komutlari.

    Kayit akarken J-Link Commander oturumu acilmaz: baglanirken RTT Logger
    birkac yuz ms okuyamaz ve 16 KB tampon S5'te ~170 ms'de dolar. Bu yuzden
    adresler kayittan once bir kez okunur, yazma konumu burada izlenir."""

    def __init__(self, jdir: Path, cb: int, globals_addr: int) -> None:
        self.jdir = jdir
        self.drop_addr = globals_addr + 20  # SEGGER_SYSVIEW_CORE_CONTEXT.DropCount
        max_up = commander(jdir, [f"mem32 0x{cb + 0x10:08X}, 1"])[cb + 0x10]  # acID[16] sonrasi
        self.addr = cb + 0x18 + 24 * max_up + 24 * SYSVIEW_CHANNEL
        self.refresh()

    def refresh(self) -> int:
        """Tampon durumunu okur; kartin su ana kadar dusurdugu olay sayisini dondurur."""
        w = commander(self.jdir, [f"mem32 0x{self.addr:08X}, 6", f"mem32 0x{self.drop_addr:08X}, 1"])
        self.pbuf, self.size, self.wr, rd = (w[self.addr + 4 * k] for k in (1, 2, 3, 4))
        if self.size == 0 or (rd - self.wr - 1) % self.size < 3:
            sys.exit(f"Asagi tampon yok ya da dolu (size={self.size}, wr={self.wr}, rd={rd}).")
        return w[self.drop_addr]

    def send(self, data: bytes) -> None:
        """Yalnizca yazar (okuma yok): kart onceki komutlari tuketmis olmali."""
        cmds = [f"w1 0x{self.pbuf + (self.wr + i) % self.size:08X}, {b:02X}" for i, b in enumerate(data)]
        self.wr = (self.wr + len(data)) % self.size
        cmds.append(f"w4 0x{self.addr + 12:08X}, {self.wr:X}")  # WrOff en son
        commander(self.jdir, cmds)


# --------------------------------------------------------------------------
# Akis cozumu (hedef kaynagi SEGGER_SYSVIEW.c ile ayni bicim)
# --------------------------------------------------------------------------

SHORT = {1: "v", 2: "v", 3: "", 4: "v", 5: "", 6: "v", 7: "vv", 8: "v", 9: "vvs", 10: "",
         11: "", 12: "v", 13: "vv", 14: "s", 15: "v", 16: "v", 17: "", 18: "", 19: "v",
         20: "", 21: "vvvv", 22: "vvs"}


def decode(b: bytes):
    """(zaman, olay_id, argumanlar) listesi ve hata metni (yoksa None)."""
    def varint(i):
        v = shift = 0
        for _ in range(5):
            x = b[i]
            i += 1
            v |= (x & 0x7F) << shift
            if not x & 0x80:
                return v, i
            shift += 7
        raise ValueError(f"bozuk sayi @ {i}")

    i, t, out = 0, 0, []
    while i < len(b):
        p0 = i
        try:
            eid = b[i]
            i += 1
            if eid == 0:
                continue  # senkron / NOP: zaman damgasi yok
            if eid < 24:
                if eid not in SHORT:
                    return out, f"bilinmeyen olay {eid} @ {p0}"
                args = []
                for f in SHORT[eid]:
                    if f == "v":
                        v, i = varint(i)
                    else:
                        n = b[i]
                        v, i = b[i + 1:i + 1 + n].decode("latin-1"), i + 1 + n
                    args.append(v)
            else:
                eid, i = varint(p0)
                n, i = varint(i)
                args = [b[i:i + n]]
                i += n
            delta, i = varint(i)
            t += delta
            out.append((t, eid, args))
        except IndexError:
            break  # son olay yarim kalmis
        except ValueError as e:
            return out, str(e)
    return out, None


def report(evs, err, drops_before: int) -> None:
    if err:
        print(f"  UYARI: akis cozulemedi: {err}")
    if not evs:
        print("  Olay yok.")
        return
    dur = (evs[-1][0] - evs[0][0]) / CPU_HZ
    names = Counter(e[1] for e in evs)
    # OVERFLOW paketi kartin acilistan beri toplam dusurdugu olayi tasir.
    counts = [e[2][0] for e in evs if e[1] == 1]
    overflow = max(counts) - drops_before if counts else 0
    exti = [e for e in evs if e[1] == 2 and e[2][0] == EXTI0_ISR]
    accepted = names.get(BTN_EVENT_OFFSET, 0)
    print(f"  {len(evs)} olay, {dur:.2f} s, {len(evs) / max(dur, 1e-9):.0f} olay/s")
    print(f"  Dusen olay (OVERFLOW): {overflow}" + ("  <-- kayitta bosluk var" if overflow else ""))
    print(f"  EXTI0 kesmesi (her kenar, sicramalar dahil): {len(exti)}")

    # Firmware filtresi gibi grupla: 30 ms sessizlikle ayrilan kenarlar bir
    # grup; her fiziksel basis iki grup uretir (basma + birakma).
    groups, last = [], None
    for t, _, _ in exti:
        if last is None or (t - last) / CPU_HZ >= REPEAT_WINDOW_S:
            groups.append(t)
        last = t
    print(f"  Kenar grubu (basma + birakma): {len(groups)} -> beklenen basis ~{len(groups) // 2}")
    print(f"  BTN ACCEPT (kabul edilen basis): {accepted}")
    if len(groups) // 2 > accepted:
        print("  <-- filtrenin kabul etmedigi basis var")


# --------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ad", help="cikti adi, orn. A_S5")
    ap.add_argument("--sure", type=float, default=30.0, help="kayit suresi, s (varsayilan 30)")
    a = ap.parse_args()

    running = subprocess.run(["tasklist", "/FI", "IMAGENAME eq SystemView.exe"],
                             capture_output=True, text=True).stdout
    if "SystemView.exe" in running:
        sys.exit("SystemView uygulamasi acik: ayni RTT kanalini okur. Kapatip tekrar deneyin.")

    jdir, cb = jlink_dir(), map_symbol("_SEGGER_RTT")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    raw = OUT_DIR / f"{a.ad}.raw.bin"
    out = OUT_DIR / f"{a.ad}.SVDat"
    raw.unlink(missing_ok=True)

    print(f"J-Link: {jdir}\nRTT kontrol blogu: 0x{cb:08X}")
    dhcsr = commander(jdir, ["mem32 0xE000EDF0, 1"])[0xE000EDF0]
    if dhcsr & (1 << 17):  # S_HALT: hata ayiklayici durdurmus (or. coken SystemView oturumu)
        print("UYARI: CPU durdurulmus bulundu (hata ayiklayici). Devam ettiriliyor.")
        commander(jdir, ["g"])
    logger = subprocess.Popen(
        [str(jdir / "JLinkRTTLogger.exe"), "-Device", DEVICE, "-If", "SWD", "-Speed", str(SPEED_KHZ),
         "-RTTAddress", f"0x{cb:08X}", "-RTTChannel", str(SYSVIEW_CHANNEL), str(raw)],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    # Logger okumaya baslamadan START gonderilirse 16 KB tampon S5'te ~170 ms'de
    # dolar ve olaylar duser. "Getting RTT data" satiri beklenir; cikti arka
    # planda bosaltilir (ilerleme satirlari boruyu doldurup logger'i durdurmasin).
    ready = threading.Event()

    def drain() -> None:
        seen = b""
        while chunk := logger.stdout.read1(4096):
            if not ready.is_set():
                seen += chunk
                if b"Getting RTT data" in seen:
                    ready.set()

    threading.Thread(target=drain, daemon=True).start()
    try:
        if not ready.wait(timeout=15):
            sys.exit("RTT Logger karta baglanamadi (15 s). Kart bagli mi, baska bir J-Link programi acik mi?")
        down = DownChannel(jdir, cb, map_symbol("_SYSVIEW_Globals"))
        down.send(bytes([CMD_STOP]))  # onceki kayit varsa birak, tampon bosalsin
        time.sleep(1.0)
        drops_before = down.refresh()
        down.send(bytes([CMD_START, CMD_GET_MODULE, 0]))
        print(f"Kayit basladi ({a.sure:.0f} s). Simdi butona basabilirsiniz.")
        end = time.monotonic() + a.sure
        while (left := end - time.monotonic()) > 0:
            print(f"\r  kalan {left:5.1f} s", end="", flush=True)
            time.sleep(min(0.5, left))
        print()
    finally:
        logger.terminate()  # once logger: STOP oturumu kayit penceresini kesmesin
        logger.wait(timeout=10)
    down.refresh()
    down.send(bytes([CMD_STOP]))

    data = raw.read_bytes() if raw.exists() else b""
    start = data.rfind(SYNC)
    if start < 0:
        sys.exit(f"Kayitta START bulunamadi ({len(data)} bayt). Kart USE_SYSVIEW 1 ile mi derlendi?")
    out.write_bytes(data[start:])
    raw.unlink()
    print(f"Kaydedildi: {out} ({len(data) - start} bayt)")
    report(*decode(data[start + len(SYNC) - 1:]), drops_before)
    print("SystemView'de acmak icin: File -> Load Recording")


if __name__ == "__main__":
    main()
