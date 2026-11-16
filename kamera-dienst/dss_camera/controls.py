"""Bildwerte lesen, setzen und merken (Plan 2.5; Plan 4: GET/PUT cameras/{cam}/controls).

- Schlüssel (`key`) wie bei `v4l2-ctl`: Name des Treibers klein, alles ausser Buchstaben
  und Ziffern als «_» (z. B. «Power Line Frequency» → `power_line_frequency`).
- Reihenfolge beim Setzen: zuerst die Automatik-Schalter (Belichtungsmodus, Autofokus,
  automatischer Weissabgleich), dann die Werte. Ein Wert, der danach inaktiv ist
  (`V4L2_CTRL_FLAG_INACTIVE`), wird nicht geschrieben und abgewiesen.
- Nach jedem Setzen liest der Dienst zurück (das Gerät rundet auf den Schritt) und
  merkt sich den zurückgelesenen Wert in `kameras.json`; `null` setzt den Standard des
  Geräts und vergisst den Wert.
- Gemerkte Werte setzt der Dienst neu, sobald eine Kamera erscheint (auch beim Start),
  nach der Wartezeit aus Plan 2.3 Regel 7. Nie verstellte Werte fasst er nicht an.
- Pan, Tilt und Zoom erscheinen in der Liste, gesetzt werden sie nur über Bewegen
  (Abschnitt b), damit die Schutzregeln (Plan 2.3) immer gelten.
- Die Tiny 2 hat zusätzlich die Gruppe «OBSBOT» (`obsbot.py`): HDR schreibbar (nur wach),
  Zustand und Auto-Schlaf nur lesbar (`readonly`). Wecken, Schlafen und die Prüfung vor
  Bewegungsbefehlen (`movable`) laufen unter derselben Sperre wie die Bildwerte.

Quellen:
https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-queryctrl.html
https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-g-ctrl.html
https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-camera.html
https://git.linuxtv.org/v4l-utils.git/tree/utils/v4l2-ctl/v4l2-ctl-common.cpp (name2var, nicht abgerufen)
"""

import logging
import os
import re
import threading
import time
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from pathlib import Path

from . import obsbot, uvc_xu, v4l2
from .devices import Camera, DeviceWatcher
from .http_api import ApiError
from .store import CameraStore

log = logging.getLogger("dss_camera.controls")

AUTO_SWITCHES = (v4l2.V4L2_CID_EXPOSURE_AUTO, v4l2.V4L2_CID_FOCUS_AUTO,
                 v4l2.V4L2_CID_AUTO_WHITE_BALANCE)
POSITION = (v4l2.V4L2_CID_PAN_ABSOLUTE, v4l2.V4L2_CID_TILT_ABSOLUTE, v4l2.V4L2_CID_ZOOM_ABSOLUTE)

TYPES = {v4l2.V4L2_CTRL_TYPE_INTEGER: "int", v4l2.V4L2_CTRL_TYPE_BOOLEAN: "bool",
         v4l2.V4L2_CTRL_TYPE_MENU: "menu", v4l2.V4L2_CTRL_TYPE_INTEGER_MENU: "menu"}

# Anzeigenamen; unbekannte Werte behalten den Namen des Treibers.
LABELS = {
    v4l2.V4L2_CID_BRIGHTNESS: "Helligkeit",
    v4l2.V4L2_CID_CONTRAST: "Kontrast",
    v4l2.V4L2_CID_SATURATION: "Sättigung",
    v4l2.V4L2_CID_HUE: "Farbton",
    v4l2.V4L2_CID_AUTO_WHITE_BALANCE: "Weissabgleich automatisch",
    v4l2.V4L2_CID_GAMMA: "Gamma",
    v4l2.V4L2_CID_GAIN: "Verstärkung",
    v4l2.V4L2_CID_POWER_LINE_FREQUENCY: "Netzfrequenz",
    v4l2.V4L2_CID_WHITE_BALANCE_TEMPERATURE: "Farbtemperatur",
    v4l2.V4L2_CID_SHARPNESS: "Schärfe",
    v4l2.V4L2_CID_BACKLIGHT_COMPENSATION: "Gegenlicht",
    v4l2.V4L2_CID_EXPOSURE_AUTO: "Belichtung",
    v4l2.V4L2_CID_EXPOSURE_ABSOLUTE: "Belichtungszeit",
    v4l2.V4L2_CID_EXPOSURE_AUTO_PRIORITY: "Dynamische Bildrate",
    v4l2.V4L2_CID_PAN_ABSOLUTE: "Schwenken",
    v4l2.V4L2_CID_TILT_ABSOLUTE: "Neigen",
    v4l2.V4L2_CID_FOCUS_ABSOLUTE: "Fokus",
    v4l2.V4L2_CID_FOCUS_AUTO: "Autofokus",
    v4l2.V4L2_CID_ZOOM_ABSOLUTE: "Zoom",
}
MENU_LABELS = {
    v4l2.V4L2_CID_POWER_LINE_FREQUENCY: {0: "Aus", 1: "50 Hz", 2: "60 Hz", 3: "Automatisch"},
    v4l2.V4L2_CID_EXPOSURE_AUTO: {0: "Automatisch", 1: "Manuell", 2: "Zeit fest", 3: "Blende fest"},
}
GROUPS = {v4l2.V4L2_CTRL_CLASS_USER: "Bild", v4l2.V4L2_CTRL_CLASS_CAMERA: "Kamera"}


def control_key(name: str) -> str:
    return re.sub(r"[^0-9a-z]+", "_", name.lower()).strip("_")


def supported(control: v4l2.Control) -> bool:
    return control.type in TYPES and not control.flags & v4l2.V4L2_CTRL_FLAG_DISABLED


def label(control: v4l2.Control) -> str:
    return LABELS.get(control.id, control.name)


def to_api(control: v4l2.Control, raw: int) -> int | bool:
    return bool(raw) if control.type == v4l2.V4L2_CTRL_TYPE_BOOLEAN else raw


def to_raw(control: v4l2.Control, value) -> int:
    """Prüft einen Wert aus der Anfrage; Fehler als `400 invalid` mit Bereich."""
    name = label(control)
    if control.type == v4l2.V4L2_CTRL_TYPE_BOOLEAN:
        if not isinstance(value, bool):
            raise ApiError(400, "invalid", f"«{name}» erwartet true oder false.")
        return int(value)
    if not isinstance(value, int) or isinstance(value, bool):
        raise ApiError(400, "invalid", f"«{name}» erwartet eine ganze Zahl.")
    if TYPES[control.type] == "menu":
        if value not in control.menu:
            choices = ", ".join(str(index) for index in sorted(control.menu))
            raise ApiError(400, "invalid", f"«{name}»: {value} ist keine Auswahl (erlaubt: {choices}).")
    elif not control.minimum <= value <= control.maximum:
        raise ApiError(400, "invalid",
                       f"«{name}»: {value} liegt ausserhalb von {control.minimum} bis {control.maximum}.")
    return value


def describe(control: v4l2.Control, raw: int, remembered: bool) -> dict:
    kind = TYPES[control.type]
    menu = None
    if kind == "menu":
        names = MENU_LABELS.get(control.id, {})
        menu = [{"value": index, "label": names.get(index, str(entry))}
                for index, entry in sorted(control.menu.items())]
    return {"key": control_key(control.name), "label": label(control),
            "group": GROUPS.get(control.id & 0xFFFF0000, "Weitere"), "type": kind,
            "min": control.minimum, "max": control.maximum, "step": control.step,
            "default": to_api(control, control.default), "value": to_api(control, raw),
            "menu": menu, "inactive": bool(control.flags & v4l2.V4L2_CTRL_FLAG_INACTIVE),
            "remembered": remembered, "readonly": False}


def auto_first(item) -> bool:
    return item[0].id not in AUTO_SWITCHES


class V4l2Device:
    """Werte eines geöffneten Knotens; dieselben Methoden hat die Attrappe (`fake.FakeDevice`)."""

    def __init__(self, fd: int) -> None:
        self._fd = fd

    def controls(self) -> list[v4l2.Control]:
        return v4l2.query_controls(self._fd)

    def flags(self, control_id: int) -> int:
        return v4l2.query_flags(self._fd, control_id)

    def get(self, control_id: int) -> int:
        return v4l2.get_ctrl(self._fd, control_id)

    def set(self, control_id: int, value: int) -> None:
        v4l2.set_ctrl(self._fd, control_id, value)

    def xu_get(self, selector: int, size: int) -> bytes:
        return uvc_xu.get_cur(self._fd, selector, size)

    def xu_set(self, selector: int, data: bytes) -> None:
        uvc_xu.set_cur(self._fd, selector, data)


@contextmanager
def open_v4l2(node: Path) -> Iterator[V4l2Device]:
    fd = os.open(node, os.O_RDWR | os.O_NONBLOCK | os.O_CLOEXEC)
    try:
        yield V4l2Device(fd)
    finally:
        os.close(fd)


class ControlManager:
    def __init__(self, watcher: DeviceWatcher, store: CameraStore,
                 open_device: Callable = open_v4l2,
                 sleep: Callable[[float], None] = time.sleep,
                 clock: Callable[[], float] = time.monotonic) -> None:
        self._watcher = watcher
        self._store = store
        self._open = open_device
        self._sleep = sleep
        self._clock = clock
        # Ein Gerätezugriff zur Zeit (Anfragen, Wecken, Bewegen, Neusetzen beim Erscheinen).
        self._lock = threading.Lock()
        # Kamera → Zeitpunkt des Erscheinens, für den die gemerkten Werte gesetzt sind.
        self._restored: dict[str, float] = {}
        self.obsbot = obsbot.Obsbot(clock, sleep)
        # Gerufen mit der Kennung, wenn sich Bildwerte oder Position geändert haben
        # (Ereignis «controls», events.py); muss sofort zurückkehren.
        self.on_change: Callable[[str], None] = lambda camera_id: None

    def _camera(self, camera_id: str) -> Camera:
        for camera in self._watcher.cameras():
            if camera.id == camera_id:
                if not camera.present or camera.node is None:
                    raise ApiError(409, "camera_absent", f"Kamera «{camera.model}» ist nicht eingesteckt.")
                return camera
        raise ApiError(404, "not_found", f"Unbekannte Kamera «{camera_id}».")

    def camera(self, camera_id: str) -> Camera:
        """Steckende Kamera (Kopie); sonst `404 not_found` oder `409 camera_absent`."""
        return self._camera(camera_id)

    def known(self, camera_id: str) -> Camera:
        """Je gesehene Kamera (Kopie), auch ausgesteckt, mit den zuletzt gelesenen Werten;
        sonst `404 not_found`. Für Einstellungen, die kein Gerät brauchen."""
        for camera in self._watcher.cameras():
            if camera.id == camera_id:
                return camera
        raise ApiError(404, "not_found", f"Unbekannte Kamera «{camera_id}».")

    def _tiny2(self, camera_id: str) -> Camera:
        camera = self._camera(camera_id)
        if not obsbot.is_tiny2(camera.model):
            raise ApiError(400, "invalid", f"«{camera.model}» kann nicht schlafen (nur {obsbot.MODEL}).")
        return camera

    def _block(self, device, camera: Camera) -> bytes:
        """Statusblock der Tiny 2 lesen und den Zustand für GET status nachführen."""
        block = self.obsbot.read_block(device, camera.id)
        self._watcher.set_state(camera.id, obsbot.state_name(obsbot.run_state(block)))
        return block

    def _read(self, device, camera: Camera) -> list[dict]:
        remembered = self._store.remembered(camera.id)
        result = []
        for control in device.controls():
            if supported(control):
                key = control_key(control.name)
                result.append(describe(control, device.get(control.id), key in remembered))
        if obsbot.is_tiny2(camera.model):
            result += obsbot.describe(self._block(device, camera), remembered)
        return result

    @staticmethod
    def _unreachable(camera: Camera, error: OSError) -> ApiError:
        return ApiError(409, "camera_absent",
                        f"Kamera «{camera.model}» nicht erreichbar: {error.strerror or error}.")

    def get(self, camera_id: str) -> list[dict]:
        camera = self._camera(camera_id)
        try:
            with self._lock, self._open(camera.node) as device:
                return self._read(device, camera)
        except OSError as error:
            raise self._unreachable(camera, error) from None

    def put(self, camera_id: str, changes: dict) -> list[dict]:
        if self._store.error is not None:
            raise ApiError(409, "file_error", f"{self._store.path} ist ungültig ({self._store.error}); "
                                              "Änderungen werden abgewiesen.")
        camera = self._camera(camera_id)
        # Plan 2.3 Regel 7: kurz nach dem Einstecken erst warten.
        remaining = self._watcher.settle_remaining(camera_id)
        if remaining is None:
            raise ApiError(409, "camera_absent", f"Kamera «{camera.model}» ist nicht eingesteckt.")
        if remaining > 0:
            self._sleep(remaining)
        remember: dict[str, int | bool | None] = {}
        rejected: list[str] = []
        try:
            with self._lock, self._open(camera.node) as device:
                controls = {control_key(c.name): c for c in device.controls() if supported(c)}
                # Erst alles prüfen, dann schreiben: Ein falscher Wert ändert nichts.
                plan = []
                special = []
                tiny2 = obsbot.is_tiny2(camera.model)
                for key, value in changes.items():
                    if tiny2 and key in obsbot.KEYS:
                        special.append((key, obsbot.to_raw(key, value), value is None))
                        continue
                    control = controls.get(key)
                    if control is None:
                        raise ApiError(400, "invalid", f"Unbekannter Wert «{key}».")
                    if control.id in POSITION:
                        raise ApiError(400, "invalid",
                                       f"«{label(control)}» wird über Bewegen gesteuert, nicht über die Bildwerte.")
                    plan.append((control, None if value is None else to_raw(control, value)))
                if special and obsbot.run_state(self._block(device, camera)) != obsbot.RUN_AWAKE:
                    raise ApiError(409, "camera_asleep", "HDR nur bei wacher Kamera (erst wecken).")
                try:
                    for key, raw, forget in special:
                        value = self.obsbot.set_hdr(device, camera_id, raw)
                        remember[key] = None if forget else value
                    for control, raw in sorted(plan, key=auto_first):
                        key = control_key(control.name)
                        inactive = (control.id not in AUTO_SWITCHES
                                    and device.flags(control.id) & v4l2.V4L2_CTRL_FLAG_INACTIVE)
                        if raw is None:
                            if not inactive:
                                device.set(control.id, control.default)
                            remember[key] = None
                        elif inactive:
                            rejected.append(label(control))
                        else:
                            device.set(control.id, raw)
                            remember[key] = to_api(control, device.get(control.id))
                finally:
                    if remember:
                        self.on_change(camera_id)
                    # Auch bei einem Abbruch: Was geschrieben ist, bleibt gemerkt.
                    self._save(camera_id, remember)
                result = self._read(device, camera) if not rejected else []
        except OSError as error:
            raise self._unreachable(camera, error) from None
        if rejected:
            names = ", ".join(f"«{name}»" for name in rejected)
            raise ApiError(400, "invalid", f"Inaktiv (Automatik ist an), nicht gesetzt: {names}. "
                                           "Die übrigen Werte sind gesetzt.")
        return result

    def _save(self, camera_id: str, remember: dict) -> None:
        if not remember:
            return
        try:
            self._store.update_controls(camera_id, remember)
        except OSError as error:
            log.error("%s nicht gespeichert: %s", self._store.path, error)
            raise ApiError(409, "file_error",
                           f"Werte gesetzt, aber nicht gespeichert: {error.strerror or error}.") from None

    def wake(self, camera_id: str) -> str:
        """POST cameras/{cam}/wake: weckt die Tiny 2 und wartet auf Zustand 1 (Plan 2.3 Regel 2)."""
        camera = self._tiny2(camera_id)
        try:
            with self._lock, self._open(camera.node) as device:
                try:
                    self.obsbot.wake(device, camera_id)
                finally:
                    self._block(device, camera)
        except OSError as error:
            raise self._unreachable(camera, error) from None
        return "awake"

    def send_to_sleep(self, camera_id: str) -> str:
        """POST cameras/{cam}/sleep."""
        camera = self._tiny2(camera_id)
        try:
            with self._lock, self._open(camera.node) as device:
                try:
                    self.obsbot.send_to_sleep(device, camera_id)
                finally:
                    self._block(device, camera)
        except OSError as error:
            raise self._unreachable(camera, error) from None
        return "asleep"

    @contextmanager
    def movable(self, camera_id: str, wake: bool) -> Iterator:
        """Geöffnetes Gerät für Bewegungsbefehle unter der Sperre (Plan 2.3 Regeln 1 und 2).

        Bei der Tiny 2 nur, wenn sie wach ist und die Wartezeit nach dem Wecken vorbei ist;
        sonst `409 camera_asleep` (ohne `wake`) oder Wecken und Warten (mit `wake`).
        Kurz nach dem Einstecken wartet es erst (Regel 7). Für alle Kameras mit Pan, Tilt, Zoom.
        """
        camera = self._camera(camera_id)
        remaining = self._watcher.settle_remaining(camera_id)
        if remaining:
            self._sleep(remaining)
        try:
            with self._lock, self._open(camera.node) as device:
                if obsbot.is_tiny2(camera.model):
                    try:
                        self.obsbot.ensure_ready(device, camera_id, wake)
                    finally:
                        self._block(device, camera)
                yield device
        except OSError as error:
            raise self._unreachable(camera, error) from None

    def _refresh_states(self) -> None:
        """Zustand der Tiny 2 für GET status lesen (höchstens alle STATE_INTERVAL_S)."""
        for camera in self._watcher.cameras():
            if not obsbot.is_tiny2(camera.model):
                continue
            if not camera.present or camera.node is None or camera.error is not None:
                self.obsbot.forget(camera.id)
                continue
            # Eine laufende Anfrage (z. B. Wecken) hat Vorrang; die Suche wartet nicht.
            if not self.obsbot.due(camera.id) or not self._lock.acquire(blocking=False):
                continue
            try:
                with self._open(camera.node) as device:
                    self._block(device, camera)
            except OSError as error:
                log.warning("Zustand von %s nicht lesbar: %s.", camera.id, error.strerror or error)
            finally:
                self._lock.release()

    def restore_pending(self) -> None:
        """Gemerkte Werte neu setzen: je Erscheinen einmal, nach der Wartezeit (Plan 2.5)."""
        self._refresh_states()
        for camera in self._watcher.cameras():
            if (not camera.present or camera.state != "awake" or camera.appeared_at is None
                    or self._restored.get(camera.id) == camera.appeared_at
                    or not self._watcher.writable(camera.id)):
                continue
            self._restored[camera.id] = camera.appeared_at
            remembered = self._store.remembered(camera.id)
            if remembered:
                self._restore(camera, remembered)

    def _restore(self, camera: Camera, remembered: dict) -> None:
        done = []
        try:
            with self._lock, self._open(camera.node) as device:
                controls = {control_key(c.name): c for c in device.controls() if supported(c)}
                plan = []
                for key, value in remembered.items():
                    if key == obsbot.KEY_HDR and obsbot.is_tiny2(camera.model):
                        # Nur bei wacher Kamera (restore_pending prüft den Zustand).
                        done.append(f"{key}={int(self.obsbot.set_hdr(device, camera.id, int(bool(value))))}")
                        continue
                    control = controls.get(key)
                    if control is None or control.id in POSITION:
                        log.warning("Gemerkter Wert «%s» passt nicht zu %s, übersprungen.", key, camera.id)
                        continue
                    try:
                        plan.append((control, to_raw(control, value)))
                    except ApiError as error:
                        log.warning("Gemerkter Wert für %s übersprungen: %s", camera.id, error.message)
                for control, raw in sorted(plan, key=auto_first):
                    key = control_key(control.name)
                    if (control.id not in AUTO_SWITCHES
                            and device.flags(control.id) & v4l2.V4L2_CTRL_FLAG_INACTIVE):
                        log.info("Gemerkter Wert «%s» für %s inaktiv, nicht gesetzt.", key, camera.id)
                        continue
                    device.set(control.id, raw)
                    done.append(f"{key}={device.get(control.id)}")
        except OSError as error:
            # Plan 2.3 Regel 6: nichts nachsenden, beim nächsten Erscheinen neu.
            log.warning("Gemerkte Werte für %s nicht gesetzt: %s.", camera.id, error.strerror or error)
            return
        if done:
            self.on_change(camera.id)
        log.info("Gemerkte Werte gesetzt für %s: %s.", camera.id, ", ".join(done) or "keine")
