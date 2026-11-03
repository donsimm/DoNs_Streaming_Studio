#!/usr/bin/env python3
"""Erzeugt die Testbilder mit EXIF-Ausrichtung 1 bis 8 (P09b, Entscheid 7).

Jedes Bild zeigt nach der Ausrichtung dasselbe Bild, 64x32 Pixel in vier Vierteln:
links oben rot, rechts oben grün, links unten blau, rechts unten gelb. Gespeichert ist es so,
dass erst die Ausrichtung es richtig zeigt (bei 5 bis 8 als 32x64).

- orientierung-N.png: PNG mit Chunk eXIf vor IDAT (TIFF «MM»), nur Python-Standardbibliothek
- orientierung-N.jpg: JPEG aus ffmpeg, danach ein APP1-Segment «Exif» 00 00 nach SOI
  (TIFF «II» bei geraden, «MM» bei ungeraden Werten, damit beide Byte-Reihenfolgen vorkommen)
- ohne-exif.jpg: dasselbe Bild ohne APP1 (Ausrichtung 1)

Aufruf: python3 tests/daten/exif/erzeugen.py   (schreibt in den Ordner dieses Skripts)
Quellen: EXIF 2.32 https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf
(Tag 0x0112, Abschnitte 4.5.4, 4.6.2, 4.6.4); PNG https://www.w3.org/TR/png-3/#eXIf
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib

WIDTH, HEIGHT = 64, 32
COLORS = {(0, 0): (255, 0, 0), (1, 0): (0, 255, 0), (0, 1): (0, 0, 255), (1, 1): (255, 255, 0)}


def shown(x, y):
    """Farbe des angezeigten Bildes an Pixel (x, y)."""
    return COLORS[(x * 2 // WIDTH, y * 2 // HEIGHT)]


def oriented(x, y, w, h, orientation):
    """Pixel (x, y) des gespeicherten Bildes (w x h) → Pixel im angezeigten Bild
    (wie dss::orientedPoint, hier mit Pixelmitten, also w - 1 - x)."""
    return {
        1: (x, y), 2: (w - 1 - x, y), 3: (w - 1 - x, h - 1 - y), 4: (x, h - 1 - y),
        5: (y, x), 6: (h - 1 - y, x), 7: (h - 1 - y, w - 1 - x), 8: (y, w - 1 - x),
    }[orientation]


def stored_pixels(orientation):
    w, h = (HEIGHT, WIDTH) if orientation >= 5 else (WIDTH, HEIGHT)
    rows = []
    for y in range(h):
        rows.append([shown(*oriented(x, y, w, h, orientation)) for x in range(w)])
    return w, h, rows


def tiff(orientation, little):
    e = '<' if little else '>'
    head = (b'II' if little else b'MM') + struct.pack(e + 'HI', 42, 8)
    entry = struct.pack(e + 'HHIH', 0x0112, 3, 1, orientation) + b'\0\0'
    return head + struct.pack(e + 'H', 1) + entry + struct.pack(e + 'I', 0)


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))


def write_png(path, orientation):
    w, h, rows = stored_pixels(orientation)
    raw = b''.join(b'\0' + bytes(c for px in row for c in px) for row in rows)
    data = (b'\x89PNG\r\n\x1a\n'
            + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + chunk(b'eXIf', tiff(orientation, little=False))
            + chunk(b'IDAT', zlib.compress(raw, 9))
            + chunk(b'IEND', b''))
    with open(path, 'wb') as f:
        f.write(data)


def write_jpeg(path, orientation, exif=True):
    w, h, rows = stored_pixels(orientation)
    with tempfile.TemporaryDirectory() as tmp:
        ppm = os.path.join(tmp, 'bild.ppm')
        with open(ppm, 'wb') as f:
            f.write(b'P6\n%d %d\n255\n' % (w, h))
            f.write(bytes(c for row in rows for px in row for c in px))
        jpg = os.path.join(tmp, 'bild.jpg')
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', ppm, '-q:v', '2',
                        '-pix_fmt', 'yuvj444p', jpg], check=True)
        with open(jpg, 'rb') as f:
            data = f.read()
    assert data[:2] == b'\xff\xd8'
    if exif:
        payload = b'Exif\0\0' + tiff(orientation, little=orientation % 2 == 0)
        data = data[:2] + b'\xff\xe1' + struct.pack('>H', len(payload) + 2) + payload + data[2:]
    with open(path, 'wb') as f:
        f.write(data)


def main():
    out = os.path.dirname(os.path.abspath(__file__))
    for n in range(1, 9):
        write_png(os.path.join(out, 'orientierung-%d.png' % n), n)
        write_jpeg(os.path.join(out, 'orientierung-%d.jpg' % n), n)
    write_jpeg(os.path.join(out, 'ohne-exif.jpg'), 1, exif=False)
    return 0


if __name__ == '__main__':
    sys.exit(main())
