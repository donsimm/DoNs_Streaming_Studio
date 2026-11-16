"""Kamera-Presets in `kamera-presets.json` im Datenordner (Plan 3; Plan 4: camera-presets).

Aufbau (`format_version` 1):

    {"format_version": 1,
     "presets": [{"id": "totale", "name": "Totale", "camera": "<kennung>",
                  "pan": 0.0, "tilt": -10.0, "zoom": 0,
                  "controls": {"brightness": 128}}]}

- Preset = Position und Zoom; Bildwerte (`controls`) nur, wenn sie mitgespeichert wurden
  (Entscheid des Nutzers, Frage 2: Kästchen «Bildwerte mitspeichern», Standard aus).
  Pan und Tilt in Grad, Zoom im Wert des Geräts; `null` = Achse fehlt an der Kamera.
- Kennung (`id`) aus dem Namen wie die Adresse eines Overlays (`src/common/address.cpp`),
  bei gleichem Namen mit angehängter Zahl. Beim Umbenennen bleibt sie.
- Abruf: über `Mover.goto` (begrenzt wie jede Bewegung), aus dem Schlaf mit Wecken
  (`wake` Standard true), danach die Bildwerte über `ControlManager.put`.
- Geschrieben wird atomar (`store.write_atomic`). Eine ungültige Datei wird gemeldet und nie
  überschrieben; alle Anfragen an die Presets werden dann mit `409 file_error` abgewiesen.
"""

import json
import logging
import re
import threading
import unicodedata
from pathlib import Path

from .controls import POSITION, control_key
from .http_api import ApiError
from .motion import is_number
from .store import FORMAT_VERSION, write_atomic

FILE_NAME = "kamera-presets.json"
FALLBACK_ID = "preset"
_FIELDS = {"name", "camera", "pan", "tilt", "zoom", "controls"}

log = logging.getLogger("dss_camera.presets")


def id_from_name(name: str) -> str:
    """Kennung aus dem Namen, wie `dss::addressFromName` (Kleinbuchstaben, ae/oe/ue, «-»)."""
    text = unicodedata.normalize("NFC", name).lower()
    for char, replacement in (("ä", "ae"), ("ö", "oe"), ("ü", "ue"), ("ß", "ss"),
                              ("æ", "ae"), ("ø", "oe"), ("ł", "l")):
        text = text.replace(char, replacement)
    text = "".join(c for c in unicodedata.normalize("NFD", text) if unicodedata.category(c) != "Mn")
    return re.sub(r"[^a-z0-9]+", "-", text).strip("-") or FALLBACK_ID


def unique_id(base: str, taken: set[str]) -> str:
    if base not in taken:
        return base
    number = 2
    while f"{base}-{number}" in taken:
        number += 1
    return f"{base}-{number}"


def _check_preset(preset) -> str | None:
    if not isinstance(preset, dict):
        return "ist kein Objekt."
    unknown = sorted(set(preset) - _FIELDS - {"id"})
    if unknown:
        return f"unbekanntes Feld «{unknown[0]}»."
    if not isinstance(preset.get("id"), str) or id_from_name(preset["id"]) != preset["id"]:
        return "«id» ist keine gültige Kennung."
    for key in ("name", "camera"):
        if not isinstance(preset.get(key), str) or not preset[key].strip():
            return f"«{key}» fehlt oder ist leer."
    for key in ("pan", "tilt"):
        if preset.get(key) is not None and not is_number(preset[key]):
            return f"«{key}» muss eine Zahl (Grad) oder null sein."
    zoom = preset.get("zoom")
    if zoom is not None and (not isinstance(zoom, int) or isinstance(zoom, bool)):
        return "«zoom» muss eine ganze Zahl oder null sein."
    controls = preset.get("controls")
    if controls is not None:
        if not isinstance(controls, dict):
            return "«controls» ist kein Objekt."
        for key, value in controls.items():
            if not isinstance(value, (int, bool)):
                return f"Bildwert «{key}» ist keine Zahl."
    return None


def check(data, list_key: str = "presets", check_item=_check_preset, item: str = "Preset") -> str | None:
    """Fehlermeldung für den Nutzer, None wenn der Inhalt gültig ist."""
    if not isinstance(data, dict):
        return "Inhalt ist kein JSON-Objekt."
    if data.get("format_version") != FORMAT_VERSION:
        return f"«format_version» muss {FORMAT_VERSION} sein."
    entries = data.get(list_key, [])
    if not isinstance(entries, list):
        return f"«{list_key}» ist keine Liste."
    seen = set()
    for index, entry in enumerate(entries, 1):
        error = check_item(entry)
        if error is not None:
            return f"{item} {index}: {error}"
        if entry["id"] in seen:
            return f"Kennung «{entry['id']}» kommt mehrfach vor."
        seen.add(entry["id"])
    return None


class PresetStore:
    """Liste mit Kennungen in einer JSON-Datei; Unterklassen ändern Datei, Liste und Meldungen
    (`lights.LightStore` für licht.json, `lights.LightPresetStore` für licht-presets.json)."""

    FILE_NAME = FILE_NAME
    LIST_KEY = "presets"
    ITEM = "Preset"
    UNKNOWN = "Unbekanntes Kamera-Preset"
    LOCKED = "Kamera-Presets sind gesperrt"
    # Kennungen, die nie vergeben werden (z. B. «all» bei den Lampen).
    RESERVED: frozenset[str] = frozenset()
    check_item = staticmethod(_check_preset)

    def __init__(self, data_dir: Path) -> None:
        self.path = data_dir / self.FILE_NAME
        self._lock = threading.Lock()
        self._data: dict = {"format_version": FORMAT_VERSION, self.LIST_KEY: []}
        # Meldung, wenn die Datei ungültig ist; dann wird nichts gespeichert.
        self.error: str | None = None
        self._load()

    def _load(self) -> None:
        try:
            text = self.path.read_text(encoding="utf-8")
        except FileNotFoundError:
            return
        except (OSError, UnicodeDecodeError) as error:
            self.error = f"nicht lesbar: {error}"
        else:
            try:
                data = json.loads(text)
            except json.JSONDecodeError as error:
                self.error = f"kein gültiges JSON: {error}"
            else:
                self.error = check(data, self.LIST_KEY, self.check_item, self.ITEM)
                if self.error is None:
                    data.setdefault(self.LIST_KEY, [])
                    self._data = data
        if self.error is not None:
            log.error("%s ungültig, wird nicht überschrieben, Änderungen werden abgewiesen: %s",
                      self.path, self.error)

    def ensure_valid(self) -> None:
        if self.error is not None:
            raise ApiError(409, "file_error", f"{self.path} ist ungültig ({self.error}); "
                                              f"{self.LOCKED}.")

    def _find(self, preset_id: str) -> dict | None:
        return next((p for p in self._data[self.LIST_KEY] if p["id"] == preset_id), None)

    def _write(self) -> None:
        try:
            write_atomic(self.path, self._data)
        except OSError as error:
            log.error("%s nicht gespeichert: %s", self.path, error)
            raise ApiError(409, "file_error",
                           f"{self.ITEM} nicht gespeichert: {error.strerror or error}.") from None

    def summary(self) -> list[dict]:
        """Für GET status: `id`, `name` (leer bei ungültiger Datei)."""
        with self._lock:
            return [{"id": p["id"], "name": p["name"]} for p in self._data[self.LIST_KEY]]

    def entries(self) -> list[dict]:
        """Alle Einträge als Kopie (leer bei ungültiger Datei)."""
        with self._lock:
            return json.loads(json.dumps(self._data[self.LIST_KEY]))

    def get(self, preset_id: str) -> dict:
        self.ensure_valid()
        with self._lock:
            preset = self._find(preset_id)
            if preset is None:
                raise ApiError(404, "not_found", f"{self.UNKNOWN} «{preset_id}».")
            return json.loads(json.dumps(preset))

    def create(self, fields: dict) -> dict:
        """Neues Preset; die Kennung wird aus dem Namen gebildet."""
        self.ensure_valid()
        with self._lock:
            taken = {p["id"] for p in self._data[self.LIST_KEY]} | self.RESERVED
            preset = {"id": unique_id(id_from_name(fields["name"]), taken), **fields}
            self._store(preset)
            self._data[self.LIST_KEY].append(preset)
            self._write()
            return dict(preset)

    def update(self, preset_id: str, changes: dict, create: bool = False) -> dict:
        """Felder ändern (`controls: null` = ohne Bildwerte); mit `create` auch neu anlegen."""
        self.ensure_valid()
        with self._lock:
            old = self._find(preset_id)
            if old is None and not create:
                raise ApiError(404, "not_found", f"{self.UNKNOWN} «{preset_id}».")
            if old is None and preset_id in self.RESERVED:
                raise ApiError(400, "invalid", f"Die Kennung «{preset_id}» ist reserviert.")
            preset = {**(old or {"id": preset_id}), **changes}
            if preset.get("controls") is None:
                preset.pop("controls", None)
            self._store(preset)
            if old is None:
                self._data[self.LIST_KEY].append(preset)
            else:
                old.clear()
                old.update(preset)
            self._write()
            return dict(preset)

    def _store(self, preset: dict) -> None:
        error = self.check_item(preset)
        if error is not None:
            raise ApiError(400, "invalid", f"{self.ITEM} {error}")

    def delete(self, preset_id: str) -> None:
        self.ensure_valid()
        with self._lock:
            preset = self._find(preset_id)
            if preset is None:
                raise ApiError(404, "not_found", f"{self.UNKNOWN} «{preset_id}».")
            self._data[self.LIST_KEY].remove(preset)
            self._write()


def _bool(body: dict, key: str, default: bool) -> bool:
    value = body.get(key, default)
    if not isinstance(value, bool):
        raise ApiError(400, "invalid", f"«{key}» erwartet true oder false.")
    return value


def _fields(body: dict, allowed: set[str]) -> None:
    unknown = sorted(set(body) - allowed)
    if unknown:
        raise ApiError(400, "invalid", f"Unbekanntes Feld «{unknown[0]}».")


class CameraPresets:
    """Anfragen an camera-presets (Plan 4) mit Datei, Bewegen und Bildwerten."""

    def __init__(self, store: PresetStore, mover, manager) -> None:
        self.store = store
        self._mover = mover
        self._manager = manager

    def _current(self, camera_id: str, with_controls: bool) -> dict:
        """Aktuelle Position und auf Wunsch die Bildwerte der Kamera."""
        fields = {"pan": None, "tilt": None, "zoom": None, **self._mover.current(camera_id)}
        if with_controls:
            camera = self._manager.camera(camera_id)
            position = {control_key(c.name) for c in camera.controls if c.id in POSITION}
            # Nur schreibbare, aktive Werte; Position kommt aus pan/tilt/zoom.
            fields["controls"] = {item["key"]: item["value"] for item in self._manager.get(camera_id)
                                  if not item["readonly"] and not item["inactive"]
                                  and item["key"] not in position}
        else:
            fields["controls"] = None
        return fields

    def get(self, preset_id: str) -> dict:
        return {"preset": self.store.get(preset_id)}

    def create(self, body: dict) -> dict:
        """POST camera-presets: neues Preset aus der aktuellen Position (Kennung aus dem Namen)."""
        _fields(body, {"name", "camera", "with_controls"})
        with_controls = _bool(body, "with_controls", False)
        for key in ("name", "camera"):
            if not isinstance(body.get(key), str) or not body[key].strip():
                raise ApiError(400, "invalid", f"«{key}» fehlt oder ist leer.")
        self.store.ensure_valid()
        fields = {"name": body["name"].strip(), "camera": body["camera"],
                  **self._current(body["camera"], with_controls)}
        if fields["controls"] is None:
            del fields["controls"]
        return {"preset": self.store.create(fields)}

    def put(self, preset_id: str, body: dict) -> dict:
        """PUT camera-presets/{id}: ändern (Umbenennen behält die Kennung) oder neu anlegen."""
        _fields(body, _FIELDS)
        changes = dict(body)
        if isinstance(changes.get("name"), str):
            changes["name"] = changes["name"].strip()
        return {"preset": self.store.update(preset_id, changes, create=True)}

    def delete(self, preset_id: str) -> dict:
        self.store.delete(preset_id)
        return {}

    def save_current(self, preset_id: str, body: dict) -> dict:
        """POST …/save-current: aktuelle Position, mit `with_controls` auch die Bildwerte.

        Die Tiny 2 wird dafür nicht geweckt: Im Schlaf ist die Position die Schlafstellung.
        """
        _fields(body, {"with_controls"})
        with_controls = _bool(body, "with_controls", False)
        camera_id = self.store.get(preset_id)["camera"]
        return {"preset": self.store.update(preset_id, self._current(camera_id, with_controls))}

    def recall(self, preset_id: str, body: dict) -> dict:
        """POST …/recall: Position begrenzt anfahren, aus dem Schlaf mit Wecken; dann Bildwerte."""
        _fields(body, {"wake"})
        wake = _bool(body, "wake", True)
        preset = self.store.get(preset_id)
        result = self._mover.goto(preset["camera"], preset.get("pan"), preset.get("tilt"),
                                  preset.get("zoom"), wake)
        if preset.get("controls"):
            result["controls"] = self._manager.put(preset["camera"], preset["controls"])
        log.info("Kamera-Preset «%s» abgerufen: %s.", preset_id, result["position"])
        return result
