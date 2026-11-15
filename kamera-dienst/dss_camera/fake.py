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


def probe_fake_device(node: Path) -> DeviceInfo:
    """Wie `devices.probe_device`, liest aber die JSON-Datei des Knotens.

    `"fehler": "EPROTO"` simuliert einen Knoten, der sich nicht abfragen lässt.
    """
    try:
        data = json.loads(node.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise OSError(errno.EIO, f"Attrappe nicht lesbar: {error}") from None
    if "fehler" in data:
        code = getattr(errno, data["fehler"])
        raise OSError(code, os.strerror(code))
    controls = [v4l2.Control(**{**control, "menu": {int(k): v for k, v in control.get("menu", {}).items()}})
                for control in data.get("controls", [])]
    return DeviceInfo(data["card"], data["device_caps"], controls)


def fake_watcher(root: Path, **kwargs) -> DeviceWatcher:
    return DeviceWatcher(by_id_dir=root / "dev" / "v4l" / "by-id",
                         sysfs_dir=root / "sys" / "class" / "video4linux",
                         probe=probe_fake_device, **kwargs)


def control(control_id: int, kind: int, name: str, minimum: int, maximum: int, step: int,
            default: int, menu: dict | None = None) -> v4l2.Control:
    return v4l2.Control(control_id, kind, name, minimum, maximum, step, default, 0, menu or {})


# Erfundene Werte (keine echten Seriennummern). Bereiche nur als Beispiel; die echten
# Bereiche der C922 bringt a3.
TINY2_CONTROLS = [
    control(v4l2.V4L2_CID_PAN_ABSOLUTE, v4l2.V4L2_CTRL_TYPE_INTEGER, "Pan, Absolute",
            -468000, 468000, 3600, 0),
    control(v4l2.V4L2_CID_TILT_ABSOLUTE, v4l2.V4L2_CTRL_TYPE_INTEGER, "Tilt, Absolute",
            -324000, 324000, 3600, 0),
    control(v4l2.V4L2_CID_ZOOM_ABSOLUTE, v4l2.V4L2_CTRL_TYPE_INTEGER, "Zoom, Absolute",
            0, 100, 1, 0),
]
C922_CONTROLS = [
    control(0x00980900, v4l2.V4L2_CTRL_TYPE_INTEGER, "Brightness", 0, 255, 1, 128),
    control(0x00980918, v4l2.V4L2_CTRL_TYPE_MENU, "Power Line Frequency", 0, 2, 1, 2,
            {0: "Disabled", 1: "50 Hz", 2: "60 Hz"}),
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
        link = self.by_id / by_id_name
        if not link.is_symlink():
            link.symlink_to(os.path.relpath(self.dev / node, self.by_id))

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
