#!/usr/bin/env python3
"""Führt «dss-app --bildschirmfoto DATEI» in der X11-Sitzung aus und prüft das Ergebnis
(Auftrag P05, erweitert in P06):

1. ohne weitere Optionen: Exit-Code 0, PNG mit 1280x800 Pixeln, «Bildschirmfoto gespeichert»
   im Protokoll, keine Konfiguration geschrieben; alle vier Verbindungsknöpfe grau
   (Rahmen in --grey1, keine Pixel in Grün, Orange oder Rot der Zustände).
2. mit --zustaende-zeigen: wie 1, dazu Rahmen in allen vier Farben (Twitch grün, YouTube
   orange, OBS rot, NDI grau).
3. mit --fenstergroesse 1920x1080: PNG mit 1920x1080 Pixeln.
4. --zustaende-zeigen ohne --bildschirmfoto: falscher Aufruf, Exit-Code 2.

PNG lesen nur mit der Standardbibliothek (zlib): Signatur, IHDR, IDAT, Filter 0 bis 4.
https://www.w3.org/TR/png-3/#11IHDR, https://www.w3.org/TR/png-3/#9Filters

  python3 bildschirmfoto_pruefen.py PFAD/dss-app AUSGABEORDNER

Exit-Code: 0 = bestanden, 1 = nicht bestanden, 77 = keine X11-Sitzung (ctest: übersprungen).
"""
import os
import struct
import subprocess
import sys
import tempfile
import zlib

SKIP = 77

# Farben der Zustände aus src/app/theme.cpp (Yami.obt: --green3, --yellow2, --red3, --grey1).
STATE_COLORS = {
    "grün": (0x25, 0xA2, 0x31),
    "orange": (0xE5, 0xAF, 0x24),
    "rot": (0xC0, 0x1C, 0x37),
    "grau": (0x5B, 0x62, 0x73),
}
# Ein Rahmen von 2 px um einen Knopf von 46x40 hat gut 300 Pixel in voller Farbe.
MIN_PIXELS_PER_BUTTON = 100


def read_png(path):
    """Gibt (Breite, Höhe, Zählung der RGB-Farben) zurück. Nur 8 Bit, RGB oder RGBA,
    ohne Zeilensprung (so speichert Qt das Bild von QWidget::grab)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("keine PNG-Datei")
    pos, idat, ihdr = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    width, height, depth, color_type, _, _, interlace = ihdr
    if depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise ValueError(f"PNG-Art nicht unterstützt: Tiefe {depth}, Farbtyp {color_type}")
    bpp = 3 if color_type == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * bpp
    counts = {}
    prev = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        ftype = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 0xFF
            elif ftype == 2:
                line[i] = (line[i] + b) & 0xFF
            elif ftype == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        for x in range(0, stride, bpp):
            rgb = (line[x], line[x + 1], line[x + 2])
            counts[rgb] = counts.get(rgb, 0) + 1
        prev = line
    return width, height, counts


def run_app(app, args):
    # Eigener Konfigurationsordner: Der Test darf die Einstellungen des Nutzers nicht lesen
    # oder schreiben (--bildschirmfoto tut das ohnehin nicht).
    with tempfile.TemporaryDirectory() as cfg:
        env = dict(os.environ, XDG_CONFIG_HOME=cfg)
        r = subprocess.run([app] + args, env=env, capture_output=True, text=True, timeout=30)
        cfg_files = os.listdir(cfg)
    print(f"$ dss-app {' '.join(args)}  → Exit-Code {r.returncode}")
    print(r.stderr, end="")
    return r, cfg_files


def check_screenshot(app, out_dir, name, extra, size, expect_colors, errors):
    path = os.path.join(out_dir, name)
    if os.path.exists(path):
        os.remove(path)
    r, cfg_files = run_app(app, ["--bildschirmfoto", path] + extra)
    label = name
    if r.returncode != 0:
        errors.append(f"{label}: Exit-Code {r.returncode}, erwartet 0")
    if "Bildschirmfoto gespeichert" not in r.stderr:
        errors.append(f"{label}: Zeile «Bildschirmfoto gespeichert» fehlt")
    if cfg_files:
        errors.append(f"{label}: Konfiguration geschrieben, obwohl --bildschirmfoto: {cfg_files}")
    if not os.path.exists(path):
        errors.append(f"{label}: Datei fehlt: {path}")
        return
    try:
        width, height, counts = read_png(path)
    except ValueError as e:
        errors.append(f"{label}: {e}")
        return
    print(f"Bild {label}: {width}x{height}, {os.path.getsize(path)} Byte")
    if (width, height) != size:
        errors.append(f"{label}: Grösse {width}x{height}, erwartet {size[0]}x{size[1]}")
    if expect_colors is None:
        return
    for color_name, rgb in STATE_COLORS.items():
        n = counts.get(rgb, 0)
        print(f"  Pixel in {color_name} #{rgb[0]:02X}{rgb[1]:02X}{rgb[2]:02X}: {n}")
        expected = expect_colors.get(color_name, 0)
        if expected == 0 and n != 0:
            errors.append(f"{label}: {n} Pixel in {color_name}, erwartet keine")
        if expected > 0 and n < expected * MIN_PIXELS_PER_BUTTON:
            errors.append(f"{label}: nur {n} Pixel in {color_name}, erwartet mindestens "
                          f"{expected * MIN_PIXELS_PER_BUTTON}")


def main():
    app, out_dir = sys.argv[1], sys.argv[2]
    if os.environ.get("XDG_SESSION_TYPE") != "x11" or not os.environ.get("DISPLAY"):
        print("übersprungen: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)")
        return SKIP
    os.makedirs(out_dir, exist_ok=True)
    errors = []
    # Werte: Anzahl Knöpfe je Farbe.
    check_screenshot(app, out_dir, "bildschirmfoto.png", [], (1280, 800), {"grau": 4}, errors)
    check_screenshot(app, out_dir, "zustaende.png", ["--zustaende-zeigen"], (1280, 800),
                     {"grün": 1, "orange": 1, "rot": 1, "grau": 1}, errors)
    check_screenshot(app, out_dir, "fenster-1920x1080.png", ["--fenstergroesse", "1920x1080"],
                     (1920, 1080), None, errors)
    r, _ = run_app(app, ["--zustaende-zeigen"])
    if r.returncode != 2:
        errors.append(f"--zustaende-zeigen ohne --bildschirmfoto: Exit-Code {r.returncode}, "
                      "erwartet 2")
    for e in errors:
        print("FEHLER:", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
