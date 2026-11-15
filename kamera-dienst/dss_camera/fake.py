"""Attrappe für Tests und Prüfungen ohne Geräte (`--attrappe`).

Simuliert eine OBSBOT Tiny 2, eine Logitech C922 und zwei Lampen. Alle Kennungen
und Namen sind erfunden.

Mit `--geraeteordner PFAD` kommen die Kameras aus der echten Suche (`devices.py`),
aber in einem Ordner statt `/dev` und `/sys` (Plan 2.2, «Attrappe mit Ordner statt
`/dev`»). Aufbau, wie ihn `FakeDeviceFolder` anlegt:

    PFAD/dev/videoN                         JSON: card, device_caps, controls
    PFAD/dev/v4l/by-id/usb-…-video-indexK   Verweis auf ../../videoN
    PFAD/sys/class/video4linux/videoN/device  Verweis auf die USB-Schnittstelle
    PFAD/sys/devices/usb1/<anschluss>/        idVendor, idProduct, serial, product

Ein- und Ausstecken: Verweis in `by-id` anlegen bzw. löschen.
"""

import errno
import json
import os
from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import asdict
from pathlib import Path

from . import v4l2
from .backend import Backend
from .devices import DeviceInfo, DeviceWatcher


class FakeBackend(Backend):
    def __init__(self, devices: DeviceWatcher | None = None) -> None:
        self.devices = devices
        self._cameras = [
            {"id": "attrappe-tiny2", "name": "Tiny 2 (Attrappe)", "model": "OBSBOT Tiny 2",
             "present": True, "state": "asleep", "ptz": True},
            {"id": "attrappe-c922", "name": "C922 (Attrappe)", "model": "Logitech C922 Pro Stream",
             "present": True, "state": "awake", "ptz": False},
        ]
        self._lights = [
            {"id": "attrappe-links", "name": "Licht links (Attrappe)", "reachable": True,
             "on": True, "brightness": 40, "temperature": 200},
            {"id": "attrappe-rechts", "name": "Licht rechts (Attrappe)", "reachable": True,
             "on": False, "brightness": 25, "temperature": 300},
        ]
        self._camera_presets = [{"id": "totale", "name": "Totale"}]
        self._light_presets = [{"id": "stream", "name": "Stream"}, {"id": "abend", "name": "Abend"}]

    def cameras(self) -> list[dict]:
        if self.devices is not None:
            return self.devices.status()
        return [dict(camera) for camera in self._cameras]

    def lights(self) -> list[dict]:
        return [dict(light) for light in self._lights]

    def camera_presets(self) -> list[dict]:
        return [dict(preset) for preset in self._camera_presets]

    def light_presets(self) -> list[dict]:
        return [dict(preset) for preset in self._light_presets]


def _read_node(node: Path) -> dict:
    try:
        data = json.loads(node.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise OSError(errno.EIO, f"Attrappe nicht lesbar: {error}") from None
    if "fehler" in data:
        code = getattr(errno, data["fehler"])
        raise OSError(code, os.strerror(code))
    return data


def _controls(data: dict) -> list[v4l2.Control]:
    return [v4l2.Control(**{**control, "menu": {int(k): v for k, v in control.get("menu", {}).items()}})
            for control in data.get("controls", [])]


def probe_fake_device(node: Path) -> DeviceInfo:
    """Wie `devices.probe_device`, liest aber die JSON-Datei des Knotens.

    `"fehler": "EPROTO"` simuliert einen Knoten, der sich nicht abfragen lässt.
    """
    data = _read_node(node)
    return DeviceInfo(data["card"], data["device_caps"], _controls(data))


# Wert → (Automatik-Schalter, Stellungen, in denen der Wert inaktiv ist), wie uvcvideo.
_INACTIVE_WHEN = {
    v4l2.V4L2_CID_EXPOSURE_ABSOLUTE: (v4l2.V4L2_CID_EXPOSURE_AUTO, (0, 2, 3)),
    v4l2.V4L2_CID_FOCUS_ABSOLUTE: (v4l2.V4L2_CID_FOCUS_AUTO, (1,)),
    v4l2.V4L2_CID_WHITE_BALANCE_TEMPERATURE: (v4l2.V4L2_CID_AUTO_WHITE_BALANCE, (1,)),
}


class FakeDevice:
    """Werte eines Attrappen-Knotens wie `controls.V4l2Device` (G_CTRL/S_CTRL).

    Die Werte stehen unter `values` in der Knotendatei, jedes Schreiben zusätzlich
    unter `protokoll` (für die Prüfung der Reihenfolge). Schreiben auf einen inaktiven
    Wert scheitert mit `EACCES`; das Gerät rundet auf den Schritt.
    """

    def __init__(self, node: Path) -> None:
        self._node = node
        self._data = _read_node(node)
        self._controls = {control.id: control for control in _controls(self._data)}

    def _value(self, control_id: int) -> int:
        return self._data.get("values", {}).get(str(control_id), self._controls[control_id].default)

    def _inactive(self, control_id: int) -> bool:
        rule = _INACTIVE_WHEN.get(control_id)
        return rule is not None and rule[0] in self._controls and self._value(rule[0]) in rule[1]

    def controls(self) -> list[v4l2.Control]:
        result = []
        for control in self._controls.values():
            flags = control.flags | (v4l2.V4L2_CTRL_FLAG_INACTIVE if self._inactive(control.id) else 0)
            result.append(v4l2.Control(**{**asdict(control), "flags": flags}))
        return result

    def flags(self, control_id: int) -> int:
        if control_id not in self._controls:
            raise OSError(errno.EINVAL, os.strerror(errno.EINVAL))
        return next(c.flags for c in self.controls() if c.id == control_id)

    def get(self, control_id: int) -> int:
        if control_id not in self._controls:
            raise OSError(errno.EINVAL, os.strerror(errno.EINVAL))
        return self._value(control_id)

    def set(self, control_id: int, value: int) -> None:
        control = self._controls.get(control_id)
        if control is None:
            raise OSError(errno.EINVAL, os.strerror(errno.EINVAL))
        if self._inactive(control_id):
            raise OSError(errno.EACCES, os.strerror(errno.EACCES))
        if control.step > 1:
            value = control.minimum + round((value - control.minimum) / control.step) * control.step
        value = max(control.minimum, min(control.maximum, value))
        self._data.setdefault("values", {})[str(control_id)] = value
        self._data.setdefault("protokoll", []).append([control_id, value])
        self._node.write_text(json.dumps(self._data), encoding="utf-8")


@contextmanager
def open_fake_device(node: Path) -> Iterator[FakeDevice]:
    yield FakeDevice(node)


def fake_watcher(root: Path, **kwargs) -> DeviceWatcher:
    return DeviceWatcher(by_id_dir=root / "dev" / "v4l" / "by-id",
                         sysfs_dir=root / "sys" / "class" / "video4linux",
                         probe=probe_fake_device, **kwargs)


def control(control_id: int, kind: int, name: str, minimum: int, maximum: int, step: int,
            default: int, menu: dict | None = None) -> v4l2.Control:
    return v4l2.Control(control_id, kind, name, minimum, maximum, step, default, 0, menu or {})


# Erfundene Kennungen (keine echten Seriennummern). Bereiche aus dem Probelauf am Gerät
# (2026-11-15): Tiny 2 Pan/Tilt/Zoom; C922 Pan/Tilt digital ±10°, Zoom, Fokus, Netzfrequenz,
# Belichtung, Weissabgleich. Helligkeit und Belichtungszeit der C922 sind Annahmen.
_INT = v4l2.V4L2_CTRL_TYPE_INTEGER
_BOOL = v4l2.V4L2_CTRL_TYPE_BOOLEAN
_MENU = v4l2.V4L2_CTRL_TYPE_MENU
TINY2_CONTROLS = [
    control(v4l2.V4L2_CID_PAN_ABSOLUTE, v4l2.V4L2_CTRL_TYPE_INTEGER, "Pan, Absolute",
            -468000, 468000, 3600, 0),
    control(v4l2.V4L2_CID_TILT_ABSOLUTE, v4l2.V4L2_CTRL_TYPE_INTEGER, "Tilt, Absolute",
            -324000, 324000, 3600, 0),
    control(v4l2.V4L2_CID_ZOOM_ABSOLUTE, v4l2.V4L2_CTRL_TYPE_INTEGER, "Zoom, Absolute",
            0, 100, 1, 0),
]
C922_CONTROLS = [
    control(v4l2.V4L2_CID_BRIGHTNESS, _INT, "Brightness", 0, 255, 1, 128),
    control(v4l2.V4L2_CID_AUTO_WHITE_BALANCE, _BOOL, "White Balance, Automatic", 0, 1, 1, 1),
    control(v4l2.V4L2_CID_POWER_LINE_FREQUENCY, _MENU, "Power Line Frequency", 0, 2, 1, 2,
            {0: "Disabled", 1: "50 Hz", 2: "60 Hz"}),
    control(v4l2.V4L2_CID_WHITE_BALANCE_TEMPERATURE, _INT, "White Balance Temperature",
            2000, 6500, 1, 4000),
    control(v4l2.V4L2_CID_EXPOSURE_AUTO, _MENU, "Auto Exposure", 0, 3, 1, 3,
            {1: "Manual Mode", 3: "Aperture Priority Mode"}),
    control(v4l2.V4L2_CID_EXPOSURE_ABSOLUTE, _INT, "Exposure Time, Absolute", 3, 2047, 1, 250),
    control(v4l2.V4L2_CID_PAN_ABSOLUTE, _INT, "Pan, Absolute", -36000, 36000, 3600, 0),
    control(v4l2.V4L2_CID_TILT_ABSOLUTE, _INT, "Tilt, Absolute", -36000, 36000, 3600, 0),
    control(v4l2.V4L2_CID_FOCUS_ABSOLUTE, _INT, "Focus, Absolute", 0, 250, 5, 0),
    control(v4l2.V4L2_CID_FOCUS_AUTO, _BOOL, "Focus, Automatic Continuous", 0, 1, 1, 1),
    control(v4l2.V4L2_CID_ZOOM_ABSOLUTE, _INT, "Zoom, Absolute", 100, 500, 1, 100),
]


class FakeDeviceFolder:
    """Legt Kameras im Attrappen-Ordner an und steckt sie ein und aus."""

    def __init__(self, root: Path) -> None:
        self.root = root
        self.dev = root / "dev"
        self.by_id = self.dev / "v4l" / "by-id"
        self.sysfs = root / "sys" / "class" / "video4linux"
        self.usb = root / "sys" / "devices" / "usb1"
        for folder in (self.by_id, self.sysfs, self.usb):
            folder.mkdir(parents=True, exist_ok=True)

    def add_node(self, node: str, port: str, card: str, device_caps: int,
                 controls: list[v4l2.Control] | None = None, failure: str | None = None) -> None:
        data: dict = {"card": card, "device_caps": device_caps,
                      "controls": [asdict(item) for item in controls or []]}
        if failure:
            data["fehler"] = failure
        (self.dev / node).write_text(json.dumps(data), encoding="utf-8")
        interface = self.usb / port / f"{port}:1.0"
        interface.mkdir(parents=True, exist_ok=True)
        link = self.sysfs / node / "device"
        link.parent.mkdir(exist_ok=True)
        if not link.is_symlink():
            link.symlink_to(os.path.relpath(interface, link.parent))

    def add_usb(self, port: str, vendor: str, product_id: str, product: str,
                serial: str | None = None) -> None:
        folder = self.usb / port
        folder.mkdir(parents=True, exist_ok=True)
        values = {"idVendor": vendor, "idProduct": product_id, "product": product}
        if serial:
            values["serial"] = serial
        for name, value in values.items():
            (folder / name).write_text(value + "\n", encoding="utf-8")

    def plug(self, by_id_name: str, node: str) -> None:
        """Einstecken; die Werte fallen auf den Standard zurück (C922: Netzfrequenz 60 Hz)."""
        link = self.by_id / by_id_name
        if not link.is_symlink():
            path = self.dev / node
            data = json.loads(path.read_text(encoding="utf-8"))
            if data.pop("values", None) is not None:
                path.write_text(json.dumps(data), encoding="utf-8")
            link.symlink_to(os.path.relpath(path, self.by_id))

    def values(self, node: str) -> dict[int, int]:
        """Geschriebene Werte eines Knotens (ohne Standardwerte)."""
        data = json.loads((self.dev / node).read_text(encoding="utf-8"))
        return {int(key): value for key, value in data.get("values", {}).items()}

    def write_log(self, node: str) -> list[tuple[int, int]]:
        data = json.loads((self.dev / node).read_text(encoding="utf-8"))
        return [tuple(item) for item in data.get("protokoll", [])]

    def unplug(self, by_id_name: str) -> None:
        (self.by_id / by_id_name).unlink(missing_ok=True)

    def add_standard_cameras(self) -> None:
        """Eine Tiny 2 (ohne Seriennummer) und eine C922 mit Meta-Knoten, wie am Streaming-PC."""
        video = v4l2.V4L2_CAP_VIDEO_CAPTURE
        meta = v4l2.V4L2_CAP_META_CAPTURE
        self.add_usb("1-2", "aaaa", "0001", "OBSBOT Tiny 2")
        self.add_node("video0", "1-2", "OBSBOT Tiny 2: OBSBOT Tiny 2", video, TINY2_CONTROLS)
        self.add_node("video1", "1-2", "OBSBOT Tiny 2: OBSBOT Tiny 2", meta)
        self.add_usb("1-3", "bbbb", "0002", "C922 Pro Stream Webcam", serial="SERIE0001")
        self.add_node("video2", "1-3", "C922 Pro Stream Webcam", video, C922_CONTROLS)
        self.add_node("video3", "1-3", "C922 Pro Stream Webcam", meta)
        self.plug(TINY2_BY_ID, "video0")
        self.plug(TINY2_BY_ID.replace("index0", "index1"), "video1")
        self.plug(C922_BY_ID, "video2")
        self.plug(C922_BY_ID.replace("index0", "index1"), "video3")


TINY2_BY_ID = "usb-Remo_Tech_Co.__Ltd._OBSBOT_Tiny_2-video-index0"
C922_BY_ID = "usb-046d_C922_Pro_Stream_Webcam_SERIE0001-video-index0"
TINY2_ID = "aaaa:0001"
C922_ID = "bbbb:0002:SERIE0001"
