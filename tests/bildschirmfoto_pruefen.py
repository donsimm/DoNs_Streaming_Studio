#!/usr/bin/env python3
"""Führt «dss-app --bildschirmfoto DATEI» in der X11-Sitzung aus und prüft das Ergebnis
(Auftrag P05): Exit-Code 0, PNG-Datei mit 1280x800 Pixeln, «Bildschirmfoto gespeichert»
im Protokoll.

Die Grösse steht im IHDR-Block direkt nach der PNG-Signatur (Breite, Höhe je 4 Byte,
Big Endian): https://www.w3.org/TR/png-3/#11IHDR

  python3 bildschirmfoto_pruefen.py PFAD/dss-app AUSGABEORDNER

Exit-Code: 0 = bestanden, 1 = nicht bestanden, 77 = keine X11-Sitzung (ctest: übersprungen).
"""
import os
import struct
import subprocess
import sys
import tempfile

EXPECTED = (1280, 800)
SKIP = 77


def main():
    app, out_dir = sys.argv[1], sys.argv[2]
    if os.environ.get("XDG_SESSION_TYPE") != "x11" or not os.environ.get("DISPLAY"):
        print("übersprungen: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)")
        return SKIP
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "bildschirmfoto.png")
    if os.path.exists(path):
        os.remove(path)
    # Eigener Konfigurationsordner: Der Test darf die Einstellungen des Nutzers nicht lesen
    # oder schreiben (--bildschirmfoto tut das ohnehin nicht).
    with tempfile.TemporaryDirectory() as cfg:
        env = dict(os.environ, XDG_CONFIG_HOME=cfg)
        r = subprocess.run([app, "--bildschirmfoto", path], env=env, capture_output=True,
                           text=True, timeout=30)
        cfg_files = os.listdir(cfg)
    print(r.stderr, end="")
    errors = []
    if r.returncode != 0:
        errors.append(f"Exit-Code {r.returncode}, erwartet 0")
    if "Bildschirmfoto gespeichert" not in r.stderr:
        errors.append("Zeile «Bildschirmfoto gespeichert» fehlt")
    if cfg_files:
        errors.append(f"Konfiguration geschrieben, obwohl --bildschirmfoto: {cfg_files}")
    if not os.path.exists(path):
        errors.append(f"Datei fehlt: {path}")
    else:
        with open(path, "rb") as f:
            head = f.read(24)
        if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
            errors.append("keine PNG-Datei")
        else:
            size = struct.unpack(">II", head[16:24])
            print(f"Bild: {size[0]}x{size[1]}, {os.path.getsize(path)} Byte")
            if size != EXPECTED:
                errors.append(f"Grösse {size[0]}x{size[1]}, erwartet "
                              f"{EXPECTED[0]}x{EXPECTED[1]}")
    for e in errors:
        print("FEHLER:", e)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
