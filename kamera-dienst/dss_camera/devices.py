"""Kameras finden, Ein- und Ausstecken erkennen (Plan 2.2, 2.3 Regel 6 und 7).

Der Dienst prüft `/dev/v4l/by-id` jede Sekunde (ohne Zusatzpaket wie `pyudev`).
Knoten mit `V4L2_CAP_META_CAPTURE` werden übersprungen; je USB-Gerät zählt der
erste Bildknoten. Werte und Bereiche liest der Dienst beim Erscheinen per
`VIDIOC_QUERYCTRL`, nicht fest im Code. Geräte ohne einen einstellbaren Wert (z. B. eine
Aufnahmekarte) fehlen in GET status (`Camera.hidden`, P15 b7).

Kennung einer Kamera: `<vid>:<pid>:<seriennummer>` aus sysfs (`idVendor`,
`idProduct`, `serial` des USB-Geräts), ohne Seriennummer nur `<vid>:<pid>`. Die
Namen in `by-id` enthalten keine vid:pid, und bei der Tiny 2 fehlt die Seriennummer;
der USB-Anschluss steckt nicht in der Kennung, damit sie beim Umstecken an einen
anderen Anschluss gleich bleibt. Nur wenn zwei gleiche Kameras ohne Seriennummer
gleichzeitig stecken, bekommt die zweite den Anschluss angehängt (`@1-2`).

Quellen:
https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-querycap.html
https://www.kernel.org/doc/Documentation/ABI/stable/sysfs-bus-usb
https://github.com/systemd/systemd/blob/main/rules.d/60-persistent-v4l.rules
"""

import logging
import os
import re
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

from . import obsbot, v4l2

log = logging.getLogger("dss_camera.devices")

BY_ID_DIR = Path("/dev/v4l/by-id")
SYSFS_DIR = Path("/sys/class/video4linux")
POLL_INTERVAL_S = 1.0
# Plan 2.3 Regel 7: nach dem Einstecken 3 s warten, bevor Werte geschrieben werden.
SETTLE_S = 3.0

_BY_ID_PATTERN = re.compile(r"^usb-.+-video-index(\d+)$")
# Typen, die GET controls anbietet (controls.TYPES).
VALUE_TYPES = (v4l2.V4L2_CTRL_TYPE_INTEGER, v4l2.V4L2_CTRL_TYPE_BOOLEAN,
               v4l2.V4L2_CTRL_TYPE_MENU, v4l2.V4L2_CTRL_TYPE_INTEGER_MENU)


@dataclass
class DeviceInfo:
    card: str
    device_caps: int
    controls: list[v4l2.Control] = field(default_factory=list)


def probe_device(node: Path) -> DeviceInfo:
    """Fähigkeiten und Werte eines Knotens; öffnet ihn nur kurz, ohne Videostrom."""
    fd = os.open(node, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        cap = v4l2.query_cap(fd)
        info = DeviceInfo(cap.card, cap.device_caps)
        if not cap.device_caps & v4l2.V4L2_CAP_META_CAPTURE:
            info.controls = v4l2.query_controls(fd)
        return info
    finally:
        os.close(fd)


def adjustable(control: v4l2.Control) -> bool:
    """Einstellbarer Wert im Sinn von controls.supported: bekannter Typ, weder abgeschaltet
    noch nur lesbar."""
    return (control.type in VALUE_TYPES
            and not control.flags & (v4l2.V4L2_CTRL_FLAG_DISABLED | v4l2.V4L2_CTRL_FLAG_READ_ONLY))


@dataclass
class Camera:
    id: str
    model: str
    node: Path | None = None
    usb_dir: Path | None = None
    present: bool = False
    # awake, asleep, privacy, absent, unknown; Schlaf und Privatsphäre nur Tiny 2 (obsbot.py).
    state: str = "unknown"
    controls: list[v4l2.Control] = field(default_factory=list)
    appeared_at: float | None = None
    error: str | None = None

    @property
    def ptz(self) -> bool:
        ids = {control.id for control in self.controls}
        return v4l2.V4L2_CID_PAN_ABSOLUTE in ids or v4l2.V4L2_CID_TILT_ABSOLUTE in ids

    @property
    def hidden(self) -> bool:
        """Abgefragt und ohne einen einstellbaren Wert (z. B. eine Aufnahmekarte): nicht in
        GET status, auch ausgesteckt nicht. Nicht abfragbare Geräte bleiben sichtbar (Fehler).
        Die Tiny 2 hat immer die Gruppe «OBSBOT» (obsbot.py)."""
        return (self.error is None and not obsbot.is_tiny2(self.model)
                and not any(map(adjustable, self.controls)))

    def status(self) -> dict:
        # Anzeigename: das Modell; einen eigenen setzt Backend.named (kameras.json).
        return {"id": self.id, "name": self.model, "model": self.model, "present": self.present,
                "state": self.state, "ptz": self.ptz}


def _read_attr(folder: Path, name: str) -> str | None:
    try:
        return (folder / name).read_text(encoding="utf-8", errors="replace").strip() or None
    except OSError:
        return None


class DeviceWatcher:
    """Stand aller je gesehenen Kameras; `scan()` prüft einmal, `start()` jede Sekunde.

    Pfade, Abfrage und Uhr sind ersetzbar: Die Attrappe nutzt einen Ordner statt
    `/dev` und `/sys` und liest die Fähigkeiten aus Dateien (`fake.py`).
    """

    def __init__(self, by_id_dir: Path = BY_ID_DIR, sysfs_dir: Path = SYSFS_DIR,
                 probe: Callable[[Path], DeviceInfo] = probe_device,
                 clock: Callable[[], float] = time.monotonic,
                 interval_s: float = POLL_INTERVAL_S, settle_s: float = SETTLE_S) -> None:
        self._by_id_dir = by_id_dir
        self._sysfs_dir = sysfs_dir
        self._probe = probe
        self._clock = clock
        self._interval_s = interval_s
        self._settle_s = settle_s
        self._lock = threading.Lock()
        self._cameras: dict[str, Camera] = {}
        # Abfrage je Knoten, solange er steckt: Eine steckende Kamera wird nicht jede
        # Sekunde neu geöffnet. Fehlgeschlagene Abfragen werden beim nächsten Mal wiederholt.
        self._probed: dict[Path, DeviceInfo] = {}
        self._failed: set[Path] = set()
        # Ausgeblendete Geräte, schon im Protokoll gemeldet (einmal je Gerät).
        self._hidden_noted: set[str] = set()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        # Nach jeder Suche aufgerufen (Neusetzen gemerkter Werte, controls.py).
        self._listeners: list[Callable[[], None]] = []

    def add_listener(self, listener: Callable[[], None]) -> None:
        self._listeners.append(listener)

    def _candidates(self) -> list[tuple[int, Path]]:
        try:
            entries = list(self._by_id_dir.iterdir())
        except OSError:
            return []
        result = []
        for entry in entries:
            match = _BY_ID_PATTERN.match(entry.name)
            if match and entry.exists():
                result.append((int(match.group(1)), entry))
        return sorted(result, key=lambda item: (item[0], item[1].name))

    def _usb_dir(self, node: Path) -> Path | None:
        # videoN/device zeigt auf die USB-Schnittstelle (z. B. 1-2:1.0), darüber liegt das Gerät.
        interface = self._sysfs_dir / node.name / "device"
        try:
            return interface.resolve(strict=True).parent
        except OSError:
            return None

    def _info(self, node: Path) -> DeviceInfo | str:
        info = self._probed.get(node)
        if info is not None:
            return info
        try:
            info = self._probe(node)
        except OSError as error:
            if node not in self._failed:
                log.warning("Kamera %s nicht abfragbar: %s.", node, error.strerror or error)
            self._failed.add(node)
            return error.strerror or str(error)
        self._failed.discard(node)
        self._probed[node] = info
        return info

    def scan(self) -> None:
        found: dict[str, tuple[Path, Path, DeviceInfo | str, str]] = {}
        seen_usb: set[Path] = set()
        seen_nodes: set[Path] = set()
        for _, entry in self._candidates():
            node = entry.resolve()
            usb_dir = self._usb_dir(node)
            if usb_dir is None or usb_dir in seen_usb:
                continue
            seen_nodes.add(node)
            info = self._info(node)
            if isinstance(info, DeviceInfo) and info.device_caps & v4l2.V4L2_CAP_META_CAPTURE:
                continue
            vendor = _read_attr(usb_dir, "idVendor")
            product_id = _read_attr(usb_dir, "idProduct")
            if vendor is None or product_id is None:
                continue
            seen_usb.add(usb_dir)
            serial = _read_attr(usb_dir, "serial")
            camera_id = f"{vendor}:{product_id}:{serial}" if serial else f"{vendor}:{product_id}"
            if camera_id in found:
                camera_id = f"{camera_id}@{usb_dir.name}"
            model = (_read_attr(usb_dir, "product")
                     or (info.card if isinstance(info, DeviceInfo) else entry.name))
            found[camera_id] = (node, usb_dir, info, model)

        # Abfragen ausgesteckter Knoten vergessen: Beim Wiedereinstecken wird neu gelesen.
        for node in list(self._probed):
            if node not in seen_nodes:
                del self._probed[node]
        self._failed &= seen_nodes

        now = self._clock()
        with self._lock:
            for camera_id, (node, usb_dir, info, model) in found.items():
                camera = self._cameras.get(camera_id)
                if camera is None:
                    camera = self._cameras[camera_id] = Camera(camera_id, model)
                appeared = not camera.present or camera.node != node
                if appeared:
                    camera.appeared_at = now
                    log.info("Kamera erschienen: %s (%s, %s).", camera_id, model, node)
                camera.present = True
                camera.node = node
                camera.usb_dir = usb_dir
                camera.model = model
                if isinstance(info, DeviceInfo):
                    # Schlaf und Privatsphäre setzt set_state (Statusblock der Tiny 2).
                    if appeared or camera.state not in ("asleep", "privacy"):
                        camera.state = "awake"
                    camera.controls = info.controls
                    camera.error = None
                else:
                    camera.state = "unknown"
                    camera.error = info
                if camera.hidden and camera_id not in self._hidden_noted:
                    self._hidden_noted.add(camera_id)
                    log.info("Gerät %s (%s) ohne einstellbare Werte, ausgeblendet.", camera_id, model)
            for camera_id, camera in self._cameras.items():
                if camera_id not in found and camera.present:
                    camera.present = False
                    camera.state = "absent"
                    camera.node = None
                    camera.appeared_at = None
                    log.info("Kamera fehlt: %s (%s).", camera_id, camera.model)
        for listener in self._listeners:
            listener()

    def cameras(self) -> list[Camera]:
        with self._lock:
            return [Camera(**vars(camera)) for camera in self._cameras.values()]

    def status(self) -> list[dict]:
        with self._lock:
            return [camera.status() for camera in self._cameras.values() if not camera.hidden]

    def set_state(self, camera_id: str, state: str) -> None:
        """Zustand aus dem Gerät (awake, asleep, privacy, unknown); nur für steckende Kameras."""
        with self._lock:
            camera = self._cameras.get(camera_id)
            if camera is not None and camera.present and camera.state != state:
                log.info("Kamera %s: Zustand %s.", camera_id, state)
                camera.state = state

    def settle_remaining(self, camera_id: str) -> float | None:
        """Sekunden bis zum ersten Schreiben (Regel 7); None, wenn die Kamera fehlt."""
        with self._lock:
            camera = self._cameras.get(camera_id)
            if camera is None or not camera.present or camera.appeared_at is None:
                return None
            return max(0.0, camera.appeared_at + self._settle_s - self._clock())

    def writable(self, camera_id: str) -> bool:
        return self.settle_remaining(camera_id) == 0.0

    def start(self) -> None:
        """Erste Prüfung sofort, danach jede Sekunde in einem eigenen Faden."""
        self.scan()
        self._thread = threading.Thread(target=self._run, name="kamera-suche", daemon=True)
        self._thread.start()

    def _run(self) -> None:
        while not self._stop.wait(self._interval_s):
            try:
                self.scan()
            except Exception:  # Suche darf den Dienst nie beenden.
                log.exception("Fehler bei der Kamera-Suche.")

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join()
            self._thread = None
