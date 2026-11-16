"""V4L2: Strukturen und ioctl für Fähigkeiten und Werte einer Kamera (Plan 2.1).

Nummern und Strukturen nach `<linux/videodev2.h>` und `<linux/uvcvideo.h>`. Der Test
kd_v4l2 vergleicht sie mit dem Hilfsprogramm `tests/kamera_ioctl_werte.cpp`, das die
Kernel-Header des Studio-PCs ausgibt. Werte aus Schritt 0 (Plan, «Ergebnis Schritt 0»):
`VIDIOC_G_CTRL`, `VIDIOC_S_CTRL`, Pan, Tilt, Zoom und `UVCIOC_CTRL_QUERY`.

Quellen:
https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-querycap.html
https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-queryctrl.html
https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-g-ctrl.html
https://www.kernel.org/doc/html/latest/userspace-api/media/drivers/uvcvideo.html
"""

import ctypes
import errno
import fcntl
import struct
from dataclasses import dataclass, field

# Aufbau der ioctl-Nummern wie in <asm-generic/ioctl.h> (x86-64).
_IOC_NRBITS = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14
_IOC_NRSHIFT = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT = _IOC_SIZESHIFT + _IOC_SIZEBITS
_IOC_WRITE = 1
_IOC_READ = 2


def _ioc(direction: int, kind: str, number: int, size: int) -> int:
    return ((direction << _IOC_DIRSHIFT) | (ord(kind) << _IOC_TYPESHIFT)
            | (number << _IOC_NRSHIFT) | (size << _IOC_SIZESHIFT))


def _ior(kind: str, number: int, structure) -> int:
    return _ioc(_IOC_READ, kind, number, ctypes.sizeof(structure))


def _iowr(kind: str, number: int, structure) -> int:
    return _ioc(_IOC_READ | _IOC_WRITE, kind, number, ctypes.sizeof(structure))


class v4l2_capability(ctypes.Structure):
    _fields_ = [
        ("driver", ctypes.c_char * 16),
        ("card", ctypes.c_char * 32),
        ("bus_info", ctypes.c_char * 32),
        ("version", ctypes.c_uint32),
        ("capabilities", ctypes.c_uint32),
        ("device_caps", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 3),
    ]


class v4l2_queryctrl(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_uint32),
        ("type", ctypes.c_uint32),
        ("name", ctypes.c_char * 32),
        ("minimum", ctypes.c_int32),
        ("maximum", ctypes.c_int32),
        ("step", ctypes.c_int32),
        ("default_value", ctypes.c_int32),
        ("flags", ctypes.c_uint32),
        ("reserved", ctypes.c_uint32 * 2),
    ]


class _v4l2_querymenu_union(ctypes.Union):
    # Im Header `__attribute__((packed))` mit `__s64 value`: Der Wert steht hier als
    # 8 Bytes, damit die Union wie im Header nicht auf 8 Byte ausgerichtet wird
    # (ohne `_pack_`, das je nach Python-Version anders wirkt).
    _fields_ = [
        ("name", ctypes.c_char * 32),
        ("value", ctypes.c_uint8 * 8),
    ]


class v4l2_querymenu(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [
        ("id", ctypes.c_uint32),
        ("index", ctypes.c_uint32),
        ("u", _v4l2_querymenu_union),
        ("reserved", ctypes.c_uint32),
    ]


class v4l2_control(ctypes.Structure):
    _fields_ = [
        ("id", ctypes.c_uint32),
        ("value", ctypes.c_int32),
    ]


class uvc_xu_control_query(ctypes.Structure):
    # Für uvc_xu.py (Abschnitt b); hier nur, damit kd_v4l2 alle Nummern an einer Stelle prüft.
    _fields_ = [
        ("unit", ctypes.c_uint8),
        ("selector", ctypes.c_uint8),
        ("query", ctypes.c_uint8),
        ("size", ctypes.c_uint16),
        ("data", ctypes.c_void_p),
    ]


VIDIOC_QUERYCAP = _ior("V", 0, v4l2_capability)
VIDIOC_G_CTRL = _iowr("V", 27, v4l2_control)
VIDIOC_S_CTRL = _iowr("V", 28, v4l2_control)
VIDIOC_QUERYCTRL = _iowr("V", 36, v4l2_queryctrl)
VIDIOC_QUERYMENU = _iowr("V", 37, v4l2_querymenu)
UVCIOC_CTRL_QUERY = _iowr("u", 0x21, uvc_xu_control_query)

V4L2_CAP_VIDEO_CAPTURE = 0x00000001
V4L2_CAP_META_CAPTURE = 0x00800000
V4L2_CAP_DEVICE_CAPS = 0x80000000

V4L2_CTRL_FLAG_DISABLED = 0x0001
V4L2_CTRL_FLAG_INACTIVE = 0x0010
V4L2_CTRL_FLAG_NEXT_CTRL = 0x80000000

V4L2_CTRL_TYPE_INTEGER = 1
V4L2_CTRL_TYPE_BOOLEAN = 2
V4L2_CTRL_TYPE_MENU = 3
V4L2_CTRL_TYPE_BUTTON = 4
V4L2_CTRL_TYPE_CTRL_CLASS = 6
V4L2_CTRL_TYPE_INTEGER_MENU = 9

V4L2_CID_BRIGHTNESS = 0x00980900
V4L2_CID_CONTRAST = 0x00980901
V4L2_CID_SATURATION = 0x00980902
V4L2_CID_HUE = 0x00980903
V4L2_CID_AUTO_WHITE_BALANCE = 0x0098090C
V4L2_CID_GAMMA = 0x00980910
V4L2_CID_GAIN = 0x00980913
V4L2_CID_POWER_LINE_FREQUENCY = 0x00980918
V4L2_CID_WHITE_BALANCE_TEMPERATURE = 0x0098091A
V4L2_CID_SHARPNESS = 0x0098091B
V4L2_CID_BACKLIGHT_COMPENSATION = 0x0098091C
V4L2_CID_EXPOSURE_AUTO = 0x009A0901
V4L2_CID_EXPOSURE_ABSOLUTE = 0x009A0902
V4L2_CID_EXPOSURE_AUTO_PRIORITY = 0x009A0903
V4L2_CID_PAN_ABSOLUTE = 0x009A0908
V4L2_CID_TILT_ABSOLUTE = 0x009A0909
V4L2_CID_PAN_SPEED = 0x009A0920
V4L2_CID_TILT_SPEED = 0x009A0921
V4L2_CID_FOCUS_ABSOLUTE = 0x009A090A
V4L2_CID_FOCUS_AUTO = 0x009A090C
V4L2_CID_ZOOM_ABSOLUTE = 0x009A090D
V4L2_CTRL_CLASS_USER = 0x00980000
V4L2_CTRL_CLASS_CAMERA = 0x009A0000

# Für den Vergleich im Test kd_v4l2: Name wie im Header → (Struktur, Felder).
STRUCTURES = {
    "v4l2_capability": (v4l2_capability,
                        ("driver", "card", "bus_info", "version", "capabilities", "device_caps")),
    "v4l2_queryctrl": (v4l2_queryctrl,
                       ("id", "type", "name", "minimum", "maximum", "step", "default_value", "flags")),
    "v4l2_querymenu": (v4l2_querymenu, ("id", "index", "name", "value")),
    "v4l2_control": (v4l2_control, ("id", "value")),
    "uvc_xu_control_query": (uvc_xu_control_query, ("unit", "selector", "query", "size", "data")),
}


@dataclass
class Capability:
    driver: str
    card: str
    bus_info: str
    device_caps: int


@dataclass
class Control:
    """Ein Wert der Kamera mit Bereich, wie ihn `VIDIOC_QUERYCTRL` meldet."""
    id: int
    type: int
    name: str
    minimum: int
    maximum: int
    step: int
    default: int
    flags: int
    # Menüeinträge: Index → Name (MENU) bzw. Zahl (INTEGER_MENU); Lücken fehlen.
    menu: dict[int, str | int] = field(default_factory=dict)


def _text(raw: bytes) -> str:
    return raw.decode("utf-8", errors="replace")


def query_cap(fd: int) -> Capability:
    cap = v4l2_capability()
    fcntl.ioctl(fd, VIDIOC_QUERYCAP, cap, True)
    # Ohne V4L2_CAP_DEVICE_CAPS ist device_caps leer; dann gelten die Fähigkeiten des Geräts.
    device_caps = cap.device_caps if cap.capabilities & V4L2_CAP_DEVICE_CAPS else cap.capabilities
    return Capability(_text(cap.driver), _text(cap.card), _text(cap.bus_info), device_caps)


def query_menu(fd: int, control_id: int, minimum: int, maximum: int,
               integer_menu: bool) -> dict[int, str | int]:
    entries: dict[int, str | int] = {}
    for index in range(max(minimum, 0), maximum + 1):
        item = v4l2_querymenu(id=control_id, index=index)
        try:
            fcntl.ioctl(fd, VIDIOC_QUERYMENU, item, True)
        except OSError as error:
            # EINVAL: Lücke im Menü (laut Doku erlaubt).
            if error.errno == errno.EINVAL:
                continue
            raise
        if integer_menu:
            entries[index] = struct.unpack("<q", bytes(item.value))[0]
        else:
            entries[index] = _text(item.name)
    return entries


def query_controls(fd: int) -> list[Control]:
    """Alle Werte der Kamera mit `V4L2_CTRL_FLAG_NEXT_CTRL`, ohne Klassen-Überschriften."""
    controls = []
    next_id = 0
    while True:
        query = v4l2_queryctrl(id=next_id | V4L2_CTRL_FLAG_NEXT_CTRL)
        try:
            fcntl.ioctl(fd, VIDIOC_QUERYCTRL, query, True)
        except OSError as error:
            if error.errno == errno.EINVAL:
                return controls
            raise
        next_id = query.id
        if query.type == V4L2_CTRL_TYPE_CTRL_CLASS:
            continue
        control = Control(query.id, query.type, _text(query.name), query.minimum, query.maximum,
                          query.step, query.default_value, query.flags)
        if query.type in (V4L2_CTRL_TYPE_MENU, V4L2_CTRL_TYPE_INTEGER_MENU):
            control.menu = query_menu(fd, query.id, query.minimum, query.maximum,
                                      query.type == V4L2_CTRL_TYPE_INTEGER_MENU)
        controls.append(control)


def query_flags(fd: int, control_id: int) -> int:
    """Aktuelle Flags eines Werts (z. B. `V4L2_CTRL_FLAG_INACTIVE` nach dem Setzen der Automatik)."""
    query = v4l2_queryctrl(id=control_id)
    fcntl.ioctl(fd, VIDIOC_QUERYCTRL, query, True)
    return query.flags


def get_ctrl(fd: int, control_id: int) -> int:
    control = v4l2_control(id=control_id)
    fcntl.ioctl(fd, VIDIOC_G_CTRL, control, True)
    return control.value


def set_ctrl(fd: int, control_id: int, value: int) -> int:
    """Schreibt einen Wert; zurück kommt der Wert, den der Treiber eingetragen hat."""
    control = v4l2_control(id=control_id, value=value)
    fcntl.ioctl(fd, VIDIOC_S_CTRL, control, True)
    return control.value
