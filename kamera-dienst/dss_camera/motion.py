"""Bewegen: Dauerbewegung mit Totmann, Schritt, Mitte (Plan 2.3 Regeln 3–6, Plan 2.4;
Plan 4: POST cameras/{cam}/move, stop, step, home).

- Richtung in der Schnittstelle: `pan` +1 = Bildausschnitt nach rechts, `tilt` +1 = nach oben,
  `zoom` +1 = hinein. Ohne «Richtung umkehren» gilt das Vorzeichen von V4L2 (Pan positiv
  nach rechts, Tilt positiv nach oben); der Standard der Umkehr kommt aus dem Handtest N1.
- Dauerbewegung: alle `TICK_S` neue Zielposition (Richtung × Tempo × Zeit), begrenzt, auf
  den Schritt des Geräts gerundet; geschrieben wird nur, wenn sich der gerundete Wert ändert.
  Totmann: fehlt die Wiederholung `DEADMAN_S`, hält der Dienst an.
- Grenzen (Regel 3): je Kamera in `kameras.json` unter `motion` (Grad), sonst der Bereich des
  Geräts; bei der Tiny 2 Tilt unten Standard −66° (Schritt 0: Einschlafen bei −76°) und nie
  unter `TINY2_TILT_FLOOR_DEG`. Jeder Zielwert geht durch dieselbe Begrenzung (`goto` auch
  für Presets).
- KI aus vor Bewegung (Regel 4): Einstellung `ai_off_before_move`; der Befehl selbst ist
  gesperrt, bis er am Gerät bestätigt ist (`obsbot.AI_OFF_UNLOCKED`).
- Abbruch (Regel 6): Scheitert ein Schritt der Dauerbewegung (Kamera fehlt, Zeitüberschreitung,
  `EPROTO`, eingeschlafen), hält der Dienst an und sendet nichts nach. Wiederholungen der
  abgebrochenen Bewegung werden abgewiesen, bis `stop` kommt oder sie `DEADMAN_S` ausbleiben.
- Jeder Befehl an die Tiny 2 läuft in `ControlManager.movable` (Regeln 1 und 2). Die Sperre
  dort hält die Dauerbewegung nur je Schritt, nicht dauerhaft.

Quellen:
https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-camera.html
(V4L2_CID_PAN_ABSOLUTE, V4L2_CID_TILT_ABSOLUTE in Bogensekunden, Vorzeichen; ZOOM_ABSOLUTE)
"""

import logging
import math
import threading
import time
from collections.abc import Callable
from dataclasses import dataclass, field

from . import obsbot, v4l2
from .http_api import ApiError

log = logging.getLogger("dss_camera.motion")

TICK_S = 0.05
REPEAT_S = 0.2
DEADMAN_S = 0.5
# Verspätet sich ein Schritt (Sperre belegt), holt die Bewegung höchstens so viel nach.
MAX_DT_S = 2 * TICK_S
ARCSEC_PER_DEG = 3600
# Tempo 100: Pan und Tilt in Grad je Sekunde, Zoom über den ganzen Bereich in Sekunden.
# Annahmen; der Handtest N1 legt sie fest.
MAX_DEG_PER_S = 60.0
ZOOM_FULL_RANGE_S = 2.0
TINY2_TILT_MIN_DEG = -66
TINY2_TILT_FLOOR_DEG = -75

AXES = (("pan", v4l2.V4L2_CID_PAN_ABSOLUTE), ("tilt", v4l2.V4L2_CID_TILT_ABSOLUTE),
        ("zoom", v4l2.V4L2_CID_ZOOM_ABSOLUTE))

# Einstellungen je Kamera unter `motion` in kameras.json (Plan 4: PUT cameras/{cam}/config).
DEFAULTS: dict = {
    "pan_min": None, "pan_max": None, "tilt_min": None, "tilt_max": None,
    "invert_pan": False, "invert_tilt": False, "invert_zoom": False,
    "step_deg": 2, "zoom_step_percent": 10, "home_pan": 0, "home_tilt": 0,
    "speed": 50, "auto_wake": False, "ai_off_before_move": True,
}
_BOOLS = {"invert_pan", "invert_tilt", "invert_zoom", "auto_wake", "ai_off_before_move"}
_OPTIONAL = {"pan_min", "pan_max", "tilt_min", "tilt_max"}


def is_number(value) -> bool:
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def check_settings(settings) -> str | None:
    """Fehlermeldung für `motion` einer Kamera, None wenn gültig (für store.py)."""
    if not isinstance(settings, dict):
        return "ist kein Objekt."
    for key, value in settings.items():
        if key not in DEFAULTS:
            return f"unbekannte Einstellung «{key}»."
        if key in _BOOLS:
            if not isinstance(value, bool):
                return f"«{key}» muss true oder false sein."
        elif not (is_number(value) or (key in _OPTIONAL and value is None)):
            return f"«{key}» muss eine Zahl sein."
    return None


@dataclass
class Axis:
    control_id: int
    lo: int
    hi: int
    step: int
    span: int
    rate: float  # Rohwert je Sekunde bei Tempo 100
    sign: int

    def clamp(self, value: float) -> float:
        return min(self.hi, max(self.lo, value))

    def raw(self, value: float) -> int:
        """Begrenzt und auf das Raster des Geräts gerundet (lo liegt auf dem Raster)."""
        snapped = self.lo + round((self.clamp(value) - self.lo) / self.step) * self.step
        return min(self.hi, max(self.lo, snapped))


def make_axis(control: v4l2.Control, lo: float, hi: float, rate: float, invert: bool) -> Axis:
    step = max(1, control.step)
    lo = control.minimum + math.ceil((max(lo, control.minimum) - control.minimum) / step) * step
    hi = control.minimum + math.floor((min(hi, control.maximum) - control.minimum) / step) * step
    return Axis(control.id, lo, max(lo, hi), step, control.maximum - control.minimum, rate,
                -1 if invert else 1)


def axes(model: str, controls: list[v4l2.Control], settings: dict) -> dict[str, Axis]:
    """Achsen mit Grenzen (Plan 2.3 Regel 3); die Grenzen liegen immer im Bereich des Geräts."""
    by_id = {control.id: control for control in controls}
    result = {}
    for name, control_id in AXES:
        control = by_id.get(control_id)
        if control is None:
            continue
        if name == "zoom":
            result[name] = make_axis(control, control.minimum, control.maximum,
                                     (control.maximum - control.minimum) / ZOOM_FULL_RANGE_S,
                                     settings["invert_zoom"])
            continue
        lo_deg, hi_deg = settings[f"{name}_min"], settings[f"{name}_max"]
        if name == "tilt" and obsbot.is_tiny2(model):
            lo_deg = max(TINY2_TILT_FLOOR_DEG, TINY2_TILT_MIN_DEG if lo_deg is None else lo_deg)
        lo = control.minimum if lo_deg is None else lo_deg * ARCSEC_PER_DEG
        hi = control.maximum if hi_deg is None else hi_deg * ARCSEC_PER_DEG
        result[name] = make_axis(control, lo, hi, MAX_DEG_PER_S * ARCSEC_PER_DEG,
                                 settings[f"invert_{name}"])
    return result


def position(found: dict[str, Axis], values: dict[str, float]) -> dict:
    """Position für die Antwort: Pan und Tilt in Grad, Zoom im Wert des Geräts."""
    result = {}
    for name in found:
        raw = values[name]
        result[name] = raw if name == "zoom" else raw / ARCSEC_PER_DEG
    return result


@dataclass
class Motion:
    direction: dict[str, float]
    speed: float
    last_request: float
    running: bool = False
    aborted: bool = False
    axes: dict[str, Axis] = field(default_factory=dict)
    position: dict[str, float] = field(default_factory=dict)
    sent: dict[str, int] = field(default_factory=dict)
    last_tick: float = 0.0

    def advance(self, dt: float) -> list[tuple[int, int]]:
        """Neue Zielwerte für dt Sekunden; nur Achsen, deren gerundeter Wert sich ändert."""
        writes = []
        for name, axis in self.axes.items():
            direction = self.direction.get(name, 0.0)
            if direction == 0:
                continue
            self.position[name] = axis.clamp(
                self.position[name] + axis.sign * direction * axis.rate * self.speed / 100 * dt)
            raw = axis.raw(self.position[name])
            if raw != self.sent[name]:
                self.sent[name] = raw
                writes.append((axis.control_id, raw))
        return writes


def _fields(body: dict, allowed: set[str]) -> None:
    unknown = sorted(set(body) - allowed)
    if unknown:
        raise ApiError(400, "invalid", f"Unbekanntes Feld «{unknown[0]}».")


def _wake(body: dict, settings: dict) -> bool:
    value = body.get("wake", settings["auto_wake"])
    if not isinstance(value, bool):
        raise ApiError(400, "invalid", "«wake» erwartet true oder false.")
    return value


class Mover:
    def __init__(self, manager, store, clock: Callable[[], float] = time.monotonic) -> None:
        self._manager = manager
        self._store = store
        self._clock = clock
        # Kamera → laufende oder abgebrochene Dauerbewegung. Sperrfolge: erst die Sperre
        # des ControlManager (movable), dann diese; nie umgekehrt.
        self._lock = threading.Lock()
        self._motions: dict[str, Motion] = {}
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def settings(self, camera_id: str) -> dict:
        return {**DEFAULTS, **self._store.motion(camera_id)}

    def _ptz_camera(self, camera_id: str):
        camera = self._manager.camera(camera_id)
        if not any(control.id == control_id for control in camera.controls for _, control_id in AXES):
            raise ApiError(400, "invalid", f"«{camera.model}» lässt sich nicht bewegen.")
        return camera

    def _prepare(self, device, camera, settings: dict) -> tuple[dict[str, Axis], dict[str, int]]:
        """Im movable-Kontext: KI aus (Regel 4), Achsen, aktuelle Position aus dem Gerät."""
        if obsbot.is_tiny2(camera.model) and settings["ai_off_before_move"]:
            self._manager.obsbot.tracking_off(device, camera.id)
        found = axes(camera.model, camera.controls, settings)
        return found, {name: device.get(axis.control_id) for name, axis in found.items()}

    def _cancel(self, camera_id: str) -> "Motion | None":
        with self._lock:
            return self._motions.pop(camera_id, None)

    # --- Dauerbewegung ---------------------------------------------------------------

    def move(self, camera_id: str, body: dict) -> dict:
        """POST move: startet oder wiederholt eine Dauerbewegung (Totmann, Plan 2.3 Regel 5)."""
        _fields(body, {"pan", "tilt", "zoom", "speed", "wake"})
        settings = self.settings(camera_id)
        direction = {}
        for name, _ in AXES:
            value = body.get(name, 0)
            if not is_number(value) or not -1 <= value <= 1:
                raise ApiError(400, "invalid", f"«{name}» erwartet eine Zahl von -1 bis 1.")
            direction[name] = float(value)
        speed = body.get("speed", settings["speed"])
        if not is_number(speed) or not 1 <= speed <= 100:
            raise ApiError(400, "invalid", "«speed» erwartet eine Zahl von 1 bis 100.")
        wake = _wake(body, settings)
        if not any(direction.values()):
            self.stop(camera_id)
            return {"moving": False}
        now = self._clock()
        with self._lock:
            motion = self._motions.get(camera_id)
            if motion is not None:
                motion.last_request = now
                if motion.aborted:
                    raise ApiError(409, "camera_absent", "Bewegung abgebrochen, Kamera war nicht erreichbar. "
                                                         "Erst loslassen (stop), dann neu bewegen.")
                motion.direction, motion.speed = direction, float(speed)
                return {"moving": True}
            motion = self._motions[camera_id] = Motion(direction, float(speed), now)
        try:
            camera = self._ptz_camera(camera_id)
            with self._manager.movable(camera_id, wake) as device:
                found, current = self._prepare(device, camera, settings)
                with self._lock:
                    if self._motions.get(camera_id) is motion:
                        motion.axes = found
                        motion.position = {name: float(value) for name, value in current.items()}
                        motion.sent = dict(current)
                        # Nach dem Wecken zählt der Totmann ab jetzt.
                        motion.last_request = motion.last_tick = self._clock()
                        motion.running = True
        except BaseException:
            with self._lock:
                if self._motions.get(camera_id) is motion:
                    del self._motions[camera_id]
            raise
        log.info("Bewegung %s: %s, Tempo %g.", camera_id, direction, speed)
        return {"moving": True}

    def stop(self, camera_id: str) -> dict:
        """POST stop: hält an; nach `stop` kommt kein Bewegungsbefehl mehr ans Gerät.

        Gelingt auch ohne Kamera (Loslassen, nachdem sie ausgesteckt wurde).
        """
        if self._cancel(camera_id) is not None:
            # Position nach der Bewegung als Ereignis (events.py).
            self._manager.on_change(camera_id)
        return {"moving": False}

    def tick(self) -> None:
        """Ein Schritt aller Dauerbewegungen (Plan 2.4); läuft alle TICK_S im eigenen Faden."""
        with self._lock:
            current = list(self._motions.items())
        for camera_id, motion in current:
            with self._lock:
                if self._motions.get(camera_id) is not motion or not (motion.running or motion.aborted):
                    continue
                now = self._clock()
                if now - motion.last_request > DEADMAN_S:
                    del self._motions[camera_id]
                    if motion.running:
                        log.info("Bewegung %s angehalten: keine Wiederholung seit %.1f s (Totmann).",
                                 camera_id, DEADMAN_S)
                        self._manager.on_change(camera_id)
                    continue
                if motion.aborted:
                    continue
                writes = motion.advance(min(MAX_DT_S, now - motion.last_tick))
                motion.last_tick = now
            if writes:
                self._send(camera_id, motion, writes)

    def _send(self, camera_id: str, motion: Motion, writes: list[tuple[int, int]]) -> None:
        try:
            with self._manager.movable(camera_id, False) as device:
                with self._lock:
                    if self._motions.get(camera_id) is not motion:
                        return
                    for control_id, raw in writes:
                        device.set(control_id, raw)
        except ApiError as error:
            # Plan 2.3 Regel 6: anhalten, nichts nachsenden.
            with self._lock:
                if self._motions.get(camera_id) is motion:
                    motion.running, motion.aborted = False, True
            log.warning("Bewegung %s abgebrochen: %s", camera_id, error.message)

    # --- Schritt und Mitte -----------------------------------------------------------

    def step(self, camera_id: str, body: dict) -> dict:
        """POST step: Zielposition ± Schrittweite (Standard 2°, Zoom in Prozent des Bereichs)."""
        _fields(body, {"pan", "tilt", "zoom", "wake"})
        settings = self.settings(camera_id)
        direction = {}
        for name, _ in AXES:
            value = body.get(name, 0)
            if value not in (-1, 0, 1) or isinstance(value, bool):
                raise ApiError(400, "invalid", f"«{name}» erwartet -1, 0 oder 1.")
            direction[name] = value
        if not any(direction.values()):
            raise ApiError(400, "invalid", "Schritt ohne Richtung.")
        wake = _wake(body, settings)

        def target(found: dict[str, Axis], current: dict[str, int]) -> dict[str, float]:
            result = {}
            for name, axis in found.items():
                if direction[name]:
                    size = (max(axis.step, axis.span * settings["zoom_step_percent"] / 100)
                            if name == "zoom" else settings["step_deg"] * ARCSEC_PER_DEG)
                    result[name] = current[name] + axis.sign * direction[name] * size
            return result

        return self._goto(camera_id, settings, wake, target)

    def home(self, camera_id: str, body: dict) -> dict:
        """POST home: Pan 0, Tilt 0 oder eigene Mitte (`home_pan`, `home_tilt`), Zoom unverändert."""
        _fields(body, {"wake"})
        settings = self.settings(camera_id)
        return self.goto(camera_id, settings["home_pan"], settings["home_tilt"], None,
                         _wake(body, settings))

    def goto(self, camera_id: str, pan: float | None, tilt: float | None, zoom: float | None,
             wake: bool) -> dict:
        """Fährt eine Position an (Pan, Tilt in Grad, Zoom im Wert des Geräts; None = unverändert).

        Auch für Presets (Abschnitt b3): Jeder Zielwert wird begrenzt.
        """
        settings = self.settings(camera_id)
        wanted = {"pan": None if pan is None else pan * ARCSEC_PER_DEG,
                  "tilt": None if tilt is None else tilt * ARCSEC_PER_DEG, "zoom": zoom}
        return self._goto(camera_id, settings, wake,
                          lambda found, current: {name: value for name, value in wanted.items()
                                                  if value is not None and name in found})

    def current(self, camera_id: str) -> dict:
        """Aktuelle Position (wie `position`) für Presets; ohne Wecken und ohne KI-Befehl."""
        settings = self.settings(camera_id)
        camera = self._ptz_camera(camera_id)
        with self._manager.movable(camera_id, False) as device:
            found = axes(camera.model, camera.controls, settings)
            return position(found, {name: device.get(axis.control_id) for name, axis in found.items()})

    def _goto(self, camera_id: str, settings: dict, wake: bool, target) -> dict:
        camera = self._ptz_camera(camera_id)
        self._cancel(camera_id)
        with self._manager.movable(camera_id, wake) as device:
            found, current = self._prepare(device, camera, settings)
            values = dict(current)
            for name, value in target(found, current).items():
                values[name] = found[name].raw(value)
                if values[name] != current[name]:
                    device.set(found[name].control_id, values[name])
        self._manager.on_change(camera_id)
        return {"position": position(found, values)}

    # --- Faden -----------------------------------------------------------------------

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="bewegen", daemon=True)
        self._thread.start()

    def _run(self) -> None:
        while not self._stop.wait(TICK_S):
            try:
                self.tick()
            except Exception:
                log.exception("Fehler beim Bewegen")

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join()
        with self._lock:
            self._motions.clear()
