"""Einstellungen je Kamera in `kameras.json` im Datenordner (Plan 3).

Aufbau (`format_version` 1):

    {"format_version": 1,
     "cameras": {"<kennung>": {"controls": {"power_line_frequency": 1}}}}

Unter `controls` stehen nur die Bildwerte, die der Nutzer verstellt hat (Plan 2.5).
Unter `motion` stehen die Einstellungen zum Bewegen (Grenzen, Richtung, Tempo,
Schrittweite, Mitte; `motion.DEFAULTS`). Weitere Felder je Kamera bleiben beim Speichern
erhalten. Geschrieben wird atomar (temporäre Datei, dann `os.replace`). Eine ungültige
Datei wird gemeldet und nie überschrieben; Änderungen werden dann abgewiesen.
"""

import json
import logging
import os
import tempfile
import threading
from pathlib import Path

from .motion import check_settings

FORMAT_VERSION = 1
FILE_NAME = "kameras.json"

log = logging.getLogger("dss_camera.store")


def default_data_dir() -> Path:
    base = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local" / "share")
    return Path(base) / "dss-kamera"


def write_atomic(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as handle:
            json.dump(data, handle, ensure_ascii=False, indent=2)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp, path)
    except BaseException:
        Path(temp).unlink(missing_ok=True)
        raise


def _check(data) -> str | None:
    """Fehlermeldung für den Nutzer, None wenn der Inhalt gültig ist."""
    if not isinstance(data, dict):
        return "Inhalt ist kein JSON-Objekt."
    if data.get("format_version") != FORMAT_VERSION:
        return f"«format_version» muss {FORMAT_VERSION} sein."
    cameras = data.get("cameras", {})
    if not isinstance(cameras, dict):
        return "«cameras» ist kein Objekt."
    for camera_id, entry in cameras.items():
        if not isinstance(entry, dict):
            return f"Eintrag der Kamera «{camera_id}» ist kein Objekt."
        controls = entry.get("controls", {})
        if not isinstance(controls, dict):
            return f"«controls» der Kamera «{camera_id}» ist kein Objekt."
        for key, value in controls.items():
            if not isinstance(value, (int, bool)):
                return f"Wert «{key}» der Kamera «{camera_id}» ist keine Zahl."
        if "motion" in entry:
            error = check_settings(entry["motion"])
            if error is not None:
                return f"«motion» der Kamera «{camera_id}»: {error}"
    return None


class CameraStore:
    def __init__(self, data_dir: Path) -> None:
        self.path = data_dir / FILE_NAME
        self._lock = threading.Lock()
        self._data: dict = {"format_version": FORMAT_VERSION, "cameras": {}}
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
                self.error = _check(data)
                if self.error is None:
                    data.setdefault("cameras", {})
                    self._data = data
        if self.error is not None:
            log.error("%s ungültig, wird nicht überschrieben, Änderungen werden abgewiesen: %s",
                      self.path, self.error)

    def remembered(self, camera_id: str) -> dict[str, int | bool]:
        with self._lock:
            entry = self._data["cameras"].get(camera_id, {})
            return dict(entry.get("controls", {}))

    def motion(self, camera_id: str) -> dict:
        """Einstellungen zum Bewegen (`motion.DEFAULTS`); nur die gesetzten."""
        with self._lock:
            entry = self._data["cameras"].get(camera_id, {})
            return dict(entry.get("motion", {}))

    def update_motion(self, camera_id: str, changes: dict) -> None:
        """Einstellungen zum Bewegen ändern (None = Standard) und speichern."""
        error = check_settings({key: value for key, value in changes.items() if value is not None})
        if self.error is not None or error is not None:
            raise ValueError(self.error or error)
        with self._lock:
            settings = self._data["cameras"].setdefault(camera_id, {}).setdefault("motion", {})
            for key, value in changes.items():
                if value is None:
                    settings.pop(key, None)
                else:
                    settings[key] = value
            write_atomic(self.path, self._data)

    def update_controls(self, camera_id: str, changes: dict[str, int | bool | None]) -> None:
        """Merkt Werte (None = vergessen) und speichert; nur bei gültiger Datei aufrufen."""
        if self.error is not None:
            raise RuntimeError("kameras.json ist ungültig")
        with self._lock:
            entry = self._data["cameras"].setdefault(camera_id, {})
            controls = entry.setdefault("controls", {})
            for key, value in changes.items():
                if value is None:
                    controls.pop(key, None)
                else:
                    controls[key] = value
            write_atomic(self.path, self._data)
