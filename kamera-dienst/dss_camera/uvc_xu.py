"""UVC-Erweiterungseinheit (Extension Unit) über `UVCIOC_CTRL_QUERY` (Plan 2.1, Abschnitt b).

Die OBSBOT Tiny 2 hat ihre Sonderfunktionen in der Einheit 2: Selektor 0x06 nimmt kurze
Befehle an und liefert den Statusblock, Selektor 0x02 nimmt Frames (`obsbot.py`). Beide
sind 60 Byte lang und können GET und SET (Plan, «Ergebnis Schritt 0», Zeilen 1, 2, 5).

Struktur und ioctl-Nummer stehen in `v4l2.py` (kd_v4l2 prüft sie), die Anfragen hier
(kd_obsbot prüft sie gegen <linux/usb/video.h>).

Quellen:
https://www.kernel.org/doc/html/latest/userspace-api/media/drivers/uvcvideo.html
https://www.usb.org/document-library/video-class-v15-document-set (UVC 1.5, Tabelle A-8, nicht abgerufen)
"""

import ctypes
import fcntl
import struct

from . import v4l2

UVC_SET_CUR = 0x01
UVC_GET_CUR = 0x81
UVC_GET_LEN = 0x85
UVC_GET_INFO = 0x86

# OBSBOT Tiny 2 (Schritt 0, 2026-11-15).
XU_UNIT = 2
SELECTOR_FRAME = 0x02
SELECTOR_COMMAND = 0x06
XU_LENGTH = 60


def query(fd: int, unit: int, selector: int, request: int, data: bytes | int) -> bytes:
    """Eine Anfrage; `data` ist der Inhalt (SET) oder die Länge der Antwort (GET)."""
    size = data if isinstance(data, int) else len(data)
    buffer = ctypes.create_string_buffer(data if isinstance(data, bytes) else b"", size)
    item = v4l2.uvc_xu_control_query(unit, selector, request, size,
                                     ctypes.cast(buffer, ctypes.c_void_p))
    fcntl.ioctl(fd, v4l2.UVCIOC_CTRL_QUERY, item, True)
    return buffer.raw


def get_cur(fd: int, selector: int, size: int = XU_LENGTH, unit: int = XU_UNIT) -> bytes:
    return query(fd, unit, selector, UVC_GET_CUR, size)


def set_cur(fd: int, selector: int, data: bytes, unit: int = XU_UNIT) -> None:
    query(fd, unit, selector, UVC_SET_CUR, data)


def get_len(fd: int, selector: int, unit: int = XU_UNIT) -> int:
    return struct.unpack("<H", query(fd, unit, selector, UVC_GET_LEN, 2))[0]


def get_info(fd: int, selector: int, unit: int = XU_UNIT) -> int:
    return query(fd, unit, selector, UVC_GET_INFO, 1)[0]
