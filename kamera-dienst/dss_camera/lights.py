"""Elgato Key Light: Lampen, Schalten, Licht-Presets, Nachführen (Plan 2.6, 3, 4: lights, light-presets).

Lampe ansprechen (Plan 2.6; Schritt 0: Key Light Air, Firmware 1.0.3, kein MK.2-Protokoll):

    GET  http://<host>:9123/elgato/lights
         → {"numberOfLights": 1, "lights": [{"on": 1, "brightness": 20, "temperature": 213}]}
    PUT  http://<host>:9123/elgato/lights   mit demselben Aufbau, jedes Feld wahlweise

- Zeitgrenze 1 s, ein neuer Versuch. `Content-Length` setzt `urllib` aus dem Inhalt selbst;
  ein fester Wert von Hand führt bei der Lampe zu `400` (Recherche Frage 5).
- Helligkeit 3–100, Temperatur 143–344 Mired. Werte ausserhalb nimmt die Lampe mit `200 OK`
  ohne Wirkung an (stiller Fehler); darum begrenzt der Dienst vor dem Senden und liest nach
  jedem Setzen zurück. Weicht der gelesene Wert ab, meldet er `not_applied`.
- Der Dienst fragt jede Lampe alle 5 s ab (`Lights.tick`), damit der Stand auch stimmt, wenn
  die Elgato-App oder die Stream-Deck-Taste die Lampe am Dienst vorbei schaltet.

Dateien im Datenordner (Plan 3), Aufbau und Kennungen wie die Kamera-Presets (`presets.py`):

    licht.json          {"format_version": 1, "lights": [{"id": "links", "name": "Links",
                                                          "host": "192.0.2.21"}]}
    licht-presets.json  {"format_version": 1, "presets": [{"id": "stream", "name": "Stream",
                         "lights": {"links": {"on": true, "brightness": 40, "temperature": 200},
                                    "rechts": null}}]}

`host` ist eine Adresse oder ein Name, wahlweise mit `:port` (Standard 9123). Im Licht-Preset
heisst `null` «diese Lampe nicht ändern»; fehlende Felder einer Lampe bleiben ebenfalls.

Quellen:
- https://apihandyman.io/hacking-elgato-key-light-with-postman/ (Anfragen, Bereiche, stiller
  Fehler; Plan, Recherche Frage 5)
- https://docs.python.org/3/library/urllib.request.html (Request mit `data`; nicht abgerufen)
"""

import json
import logging
import re
import threading
import time
import urllib.error
import urllib.request
from collections.abc import Callable

from .http_api import ApiError
from .presets import PresetStore, _fields, id_from_name

PORT = 9123
PATH = "/elgato/lights"
TIMEOUT_S = 1.0
ATTEMPTS = 2  # ein neuer Versuch
POLL_S = 5.0
BRIGHTNESS_MIN, BRIGHTNESS_MAX = 3, 100
TEMPERATURE_MIN, TEMPERATURE_MAX = 143, 344
ALL = "all"
VALUE_KEYS = ("on", "brightness", "temperature")

_HOST = re.compile(r"^[A-Za-z0-9.-]+(:(\d{1,5}))?$")

log = logging.getLogger("dss_camera.lights")


def clamp(key: str, value: int) -> int:
    if key == "brightness":
        return max(BRIGHTNESS_MIN, min(BRIGHTNESS_MAX, value))
    return max(TEMPERATURE_MIN, min(TEMPERATURE_MAX, value))


def _is_int(value) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _check_host(host) -> str | None:
    match = _HOST.match(host) if isinstance(host, str) else None
    if match is None or (match.group(2) is not None and not 1 <= int(match.group(2)) <= 65535):
        return "«host» muss eine Adresse oder ein Name sein, wahlweise mit «:port»."
    return None


def _check_light(light) -> str | None:
    if not isinstance(light, dict):
        return "ist kein Objekt."
    unknown = sorted(set(light) - {"id", "name", "host"})
    if unknown:
        return f"unbekanntes Feld «{unknown[0]}»."
    if not isinstance(light.get("id"), str) or id_from_name(light["id"]) != light["id"]:
        return "«id» ist keine gültige Kennung."
    if not isinstance(light.get("name"), str) or not light["name"].strip():
        return "«name» fehlt oder ist leer."
    return _check_host(light.get("host"))


def check_values(values, allow_toggle: bool) -> str | None:
    """Werte einer Lampe: `on` (true/false, mit allow_toggle auch "toggle"), `brightness`,
    `temperature` (ganze Zahlen, begrenzt wird beim Senden)."""
    if not isinstance(values, dict):
        return "Werte sind kein Objekt."
    unknown = sorted(set(values) - set(VALUE_KEYS))
    if unknown:
        return f"unbekanntes Feld «{unknown[0]}»."
    if not values:
        return "keine Werte («on», «brightness», «temperature»)."
    if "on" in values and not (isinstance(values["on"], bool) or (allow_toggle and values["on"] == "toggle")):
        return "«on» erwartet true, false" + (" oder \"toggle\"." if allow_toggle else ".")
    for key in ("brightness", "temperature"):
        if key in values and not _is_int(values[key]):
            return f"«{key}» muss eine ganze Zahl sein."
    return None


def _check_light_preset(preset) -> str | None:
    if not isinstance(preset, dict):
        return "ist kein Objekt."
    unknown = sorted(set(preset) - {"id", "name", "lights"})
    if unknown:
        return f"unbekanntes Feld «{unknown[0]}»."
    if not isinstance(preset.get("id"), str) or id_from_name(preset["id"]) != preset["id"]:
        return "«id» ist keine gültige Kennung."
    if not isinstance(preset.get("name"), str) or not preset["name"].strip():
        return "«name» fehlt oder ist leer."
    lights = preset.get("lights")
    if not isinstance(lights, dict):
        return "«lights» fehlt oder ist kein Objekt."
    for light_id, values in lights.items():
        if values is not None:
            error = check_values(values, allow_toggle=False)
            if error is not None:
                return f"Lampe «{light_id}»: {error}"
    return None


class LightStore(PresetStore):
    FILE_NAME = "licht.json"
    LIST_KEY = "lights"
    ITEM = "Lampe"
    UNKNOWN = "Unbekannte Lampe"
    LOCKED = "Lampen sind gesperrt"
    RESERVED = frozenset({ALL})  # PUT lights/all gilt allen Lampen
    check_item = staticmethod(_check_light)


class LightPresetStore(PresetStore):
    FILE_NAME = "licht-presets.json"
    UNKNOWN = "Unbekanntes Licht-Preset"
    LOCKED = "Licht-Presets sind gesperrt"
    check_item = staticmethod(_check_light_preset)


class LightUnreachable(Exception):
    pass


class ElgatoClient:
    """GET/PUT an /elgato/lights mit Zeitgrenze und einem neuen Versuch."""

    def __init__(self, timeout_s: float = TIMEOUT_S, attempts: int = ATTEMPTS) -> None:
        self.timeout_s = timeout_s
        self.attempts = attempts

    @staticmethod
    def _url(host: str) -> str:
        return f"http://{host}{PATH}" if ":" in host else f"http://{host}:{PORT}{PATH}"

    @staticmethod
    def _parse(payload) -> dict:
        try:
            light = payload["lights"][0]
            values = {"on": bool(light["on"]), "brightness": int(light["brightness"]),
                      "temperature": int(light["temperature"])}
        except (KeyError, IndexError, TypeError, ValueError):
            raise ValueError("unerwartete Antwort") from None
        return values

    def _request(self, host: str, method: str, body: dict | None = None) -> dict:
        # Kein Content-Length von Hand: urllib setzt ihn aus `data` (Plan 2.6, Stolperstein 400).
        data = None if body is None else json.dumps(body).encode("utf-8")
        reason = ""
        for _ in range(self.attempts):
            request = urllib.request.Request(self._url(host), data=data, method=method,
                                             headers={"Content-Type": "application/json"})
            try:
                with urllib.request.urlopen(request, timeout=self.timeout_s) as response:
                    return self._parse(json.loads(response.read().decode("utf-8")))
            except urllib.error.HTTPError as error:
                reason = f"HTTP {error.code}"
            except (OSError, ValueError) as error:
                # URLError, Zeitüberschreitung, Verbindung abgelehnt, kein JSON.
                reason = "Zeitüberschreitung" if "timed out" in str(error) else str(error)
        raise LightUnreachable(reason)

    def get(self, host: str) -> dict:
        return self._request(host, "GET")

    def put(self, host: str, values: dict) -> dict:
        light = {key: int(value) if key == "on" else value for key, value in values.items()}
        return self._request(host, "PUT", {"numberOfLights": 1, "lights": [light]})


class Lights:
    """Lampen aus licht.json: Stand, Schalten (auch `all`), Nachführen alle 5 s."""

    def __init__(self, store: LightStore, client: ElgatoClient | None = None,
                 clock: Callable[[], float] = time.monotonic, poll_s: float = POLL_S) -> None:
        self.store = store
        self._client = client or ElgatoClient()
        self._clock = clock
        self._poll_s = poll_s
        self._next_poll: float | None = None
        self._lock = threading.Lock()
        # Je Lampe: zuletzt gelesener Stand und eine Sperre, damit Anfragen nacheinander gehen.
        self._state: dict[str, dict] = {}
        self._lamp_locks: dict[str, threading.Lock] = {}
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    # --- Stand -----------------------------------------------------------------------------

    def _lamp_lock(self, light_id: str) -> threading.Lock:
        with self._lock:
            return self._lamp_locks.setdefault(light_id, threading.Lock())

    def _record(self, light_id: str, values: dict | None) -> None:
        with self._lock:
            if values is None:
                self._state.setdefault(light_id, {})["reachable"] = False
            else:
                self._state[light_id] = {"reachable": True, **values}

    def _entry(self, light: dict) -> dict:
        with self._lock:
            state = self._state.get(light["id"], {})
        return {"id": light["id"], "name": light["name"], "reachable": state.get("reachable", False),
                **{key: state.get(key) for key in VALUE_KEYS}}

    def status(self) -> list[dict]:
        """Für GET status: `id`, `name`, `reachable`, `on`, `brightness`, `temperature`."""
        return [self._entry(light) for light in self.store.entries()]

    def read(self, light: dict) -> dict:
        """Liest den Stand der Lampe; bei Fehler `LightUnreachable` und `reachable` false."""
        with self._lamp_lock(light["id"]):
            try:
                values = self._client.get(light["host"])
            except LightUnreachable:
                self._record(light["id"], None)
                raise
            self._record(light["id"], values)
            return values

    # --- Nachführen ------------------------------------------------------------------------

    def poll(self) -> None:
        lights = self.store.entries()
        for light in lights:
            try:
                self.read(light)
            except LightUnreachable as error:
                log.debug("Lampe «%s» antwortet nicht: %s", light["id"], error)
        known = {light["id"] for light in lights}
        with self._lock:
            for light_id in set(self._state) - known:
                del self._state[light_id]

    def tick(self) -> bool:
        """Fragt alle Lampen ab, wenn 5 s seit der letzten Abfrage um sind; True wenn abgefragt."""
        now = self._clock()
        if self._next_poll is not None and now < self._next_poll:
            return False
        self._next_poll = now + self._poll_s
        self.poll()
        return True

    def start(self) -> None:
        def run() -> None:
            self.tick()
            while not self._stop.wait(0.2):
                self.tick()

        self._thread = threading.Thread(target=run, name="licht", daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2 * TIMEOUT_S * ATTEMPTS + 1)

    # --- Setzen ----------------------------------------------------------------------------

    def _apply(self, light: dict, wanted: dict) -> dict:
        """Senden, zurücklesen; Fehler als `{"light", "code", "message"}`, sonst None."""
        name = light["name"]
        with self._lamp_lock(light["id"]):
            try:
                self._client.put(light["host"], wanted)
                values = self._client.get(light["host"])
            except LightUnreachable as error:
                self._record(light["id"], None)
                log.warning("Lampe «%s» antwortet nicht: %s", light["id"], error)
                return {"light": light["id"], "code": "light_unreachable",
                        "message": f"Lampe «{name}» antwortet nicht ({error})."}
            self._record(light["id"], values)
        wrong = [key for key in wanted if values[key] != wanted[key]]
        if wrong:
            details = ", ".join(f"{key} gesendet {json.dumps(wanted[key])}, gelesen {json.dumps(values[key])}"
                                for key in wrong)
            log.warning("Lampe «%s» hat Werte nicht übernommen: %s", light["id"], details)
            return {"light": light["id"], "code": "not_applied",
                    "message": f"Lampe «{name}» hat Werte nicht übernommen ({details})."}
        return None

    def apply_many(self, targets: list[tuple[dict, dict]], errors: list[dict] | None = None) -> dict:
        """Setzt je Lampe die Werte (begrenzt); eine Lampe ohne Antwort hält die anderen nicht auf.

        Antwort `{"lights": [...], "errors": [...]}`. Antwortet keine der Lampen (bei einer
        einzelnen Lampe also diese), wird daraus `502 light_unreachable`.
        """
        errors = list(errors or [])
        unreachable = []
        for light, values in targets:
            wanted = {key: clamp(key, value) if key != "on" else value for key, value in values.items()}
            error = self._apply(light, wanted)
            if error is not None:
                errors.append(error)
                if error["code"] == "light_unreachable":
                    unreachable.append(error)
        if targets and len(unreachable) == len(targets):
            raise ApiError(502, "light_unreachable", " ".join(e["message"] for e in unreachable))
        return {"lights": [self._entry(light) for light, _ in targets], "errors": errors}

    def set(self, light_id: str, body: dict) -> dict:
        """PUT lights/{light} und lights/all: `on` (auch "toggle"), `brightness`, `temperature`."""
        error = check_values(body, allow_toggle=True)
        if error is not None:
            raise ApiError(400, "invalid", error[0].upper() + error[1:])
        if light_id == ALL:
            self.store.ensure_valid()
            lights = self.store.entries()
        else:
            lights = [self.store.get(light_id)]
        errors: list[dict] = []
        if body.get("on") == "toggle":
            # Frisch gelesen; bei «all» gehen alle aus, wenn eine an ist, sonst alle an.
            current = []
            for light in lights:
                try:
                    current.append((light, self.read(light)["on"]))
                except LightUnreachable as reason:
                    errors.append({"light": light["id"], "code": "light_unreachable",
                                   "message": f"Lampe «{light['name']}» antwortet nicht ({reason})."})
            if lights and not current:
                raise ApiError(502, "light_unreachable", " ".join(e["message"] for e in errors))
            switch_on = not any(on for _, on in current)
            lights = [light for light, _ in current]
            body = {**body, "on": switch_on}
        return self.apply_many([(light, body) for light in lights], errors)

    # --- Lampen verwalten (licht.json) -----------------------------------------------------

    def create(self, body: dict) -> dict:
        _fields(body, {"name", "host"})
        for key in ("name", "host"):
            if not isinstance(body.get(key), str) or not body[key].strip():
                raise ApiError(400, "invalid", f"«{key}» fehlt oder ist leer.")
        light = self.store.create({"name": body["name"].strip(), "host": body["host"].strip()})
        return {"light": light}

    def config(self, light_id: str, body: dict) -> dict:
        """PUT lights/{light}/config: umbenennen (Kennung bleibt) oder Adresse ändern."""
        _fields(body, {"name", "host"})
        changes = {key: value.strip() if isinstance(value, str) else value for key, value in body.items()}
        old = self.store.get(light_id)
        light = self.store.update(light_id, changes)
        if light["host"] != old["host"]:
            with self._lock:
                self._state.pop(light_id, None)
        return {"light": light}

    def delete(self, light_id: str) -> dict:
        self.store.delete(light_id)
        with self._lock:
            self._state.pop(light_id, None)
        return {}


class LightPresets:
    """Anfragen an light-presets (Plan 4), wie die Kamera-Presets."""

    def __init__(self, store: LightPresetStore, lights: Lights) -> None:
        self.store = store
        self._lights = lights

    def _current(self) -> tuple[dict, list[dict]]:
        """Frisch gelesener Stand aller Lampen; ohne Antwort `null` (= nicht ändern)."""
        self._lights.store.ensure_valid()
        values: dict[str, dict | None] = {}
        errors = []
        for light in self._lights.store.entries():
            try:
                values[light["id"]] = self._lights.read(light)
            except LightUnreachable as reason:
                values[light["id"]] = None
                errors.append({"light": light["id"], "code": "light_unreachable",
                               "message": f"Lampe «{light['name']}» antwortet nicht ({reason}); "
                                          "im Preset bleibt sie unverändert."})
        return values, errors

    def get(self, preset_id: str) -> dict:
        return {"preset": self.store.get(preset_id)}

    def create(self, body: dict) -> dict:
        """POST light-presets: neues Preset aus dem Stand aller Lampen."""
        _fields(body, {"name"})
        if not isinstance(body.get("name"), str) or not body["name"].strip():
            raise ApiError(400, "invalid", "«name» fehlt oder ist leer.")
        self.store.ensure_valid()
        values, errors = self._current()
        return {"preset": self.store.create({"name": body["name"].strip(), "lights": values}),
                "errors": errors}

    def put(self, preset_id: str, body: dict) -> dict:
        _fields(body, {"name", "lights"})
        changes = dict(body)
        if isinstance(changes.get("name"), str):
            changes["name"] = changes["name"].strip()
        return {"preset": self.store.update(preset_id, changes, create=True)}

    def delete(self, preset_id: str) -> dict:
        self.store.delete(preset_id)
        return {}

    def save_current(self, preset_id: str, body: dict) -> dict:
        _fields(body, set())
        self.store.get(preset_id)
        values, errors = self._current()
        return {"preset": self.store.update(preset_id, {"lights": values}), "errors": errors}

    def recall(self, preset_id: str, body: dict) -> dict:
        """POST …/recall: Werte je Lampe begrenzt setzen; `null` und fehlende Lampen bleiben."""
        _fields(body, set())
        preset = self.store.get(preset_id)
        self._lights.store.ensure_valid()
        lights = {light["id"]: light for light in self._lights.store.entries()}
        targets, errors = [], []
        for light_id, values in preset["lights"].items():
            if values is None:
                continue
            if light_id not in lights:
                errors.append({"light": light_id, "code": "not_found",
                               "message": f"Lampe «{light_id}» gibt es nicht mehr."})
                continue
            targets.append((lights[light_id], values))
        result = self._lights.apply_many(targets, errors)
        log.info("Licht-Preset «%s» abgerufen (%d Lampen, %d Meldungen).",
                 preset_id, len(result["lights"]), len(result["errors"]))
        return result
