"""`GET export`: alle Dateien des Datenordners als ein JSON (Plan 4, Sicherung).

    {"format_version": 1,
     "files": {"kameras.json": {…}, "kamera-presets.json": {…}, "licht.json": {…},
               "licht-presets.json": {…}},
     "errors": [{"file": "licht.json", "message": "…"}]}

Gelesen wird der Stand auf der Platte, nicht der im Speicher. Ungültige Dateien stehen mit
`null` unter `files` (nicht lesbar oder kein JSON) bzw. mit ihrem Inhalt (Inhalt ungültig)
und immer mit einer Meldung unter `errors`; der Export gelingt trotzdem.
"""

import json
from pathlib import Path

from . import store
from .http_api import ApiError
from .lights import LightPresetStore, LightStore
from .presets import PresetStore, check

FORMAT_VERSION = 1


def _list_check(cls):
    return lambda data: check(data, cls.LIST_KEY, cls.check_item, cls.ITEM)


# Prüfung je bekannter Datei; andere *.json kommen ungeprüft mit.
CHECKS = {
    store.FILE_NAME: store._check,
    PresetStore.FILE_NAME: _list_check(PresetStore),
    LightStore.FILE_NAME: _list_check(LightStore),
    LightPresetStore.FILE_NAME: _list_check(LightPresetStore),
}


def export(data_dir: Path) -> dict:
    files: dict = {}
    errors: list[dict] = []
    try:
        # Temporäre Dateien von write_atomic enden auf .tmp und fallen weg.
        paths = sorted(data_dir.glob("*.json")) if data_dir.is_dir() else []
    except OSError as error:
        raise ApiError(409, "file_error",
                       f"Datenordner {data_dir} nicht lesbar: {error.strerror or error}.") from None
    for path in paths:
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError) as error:
            files[path.name] = None
            errors.append({"file": path.name, "message": f"nicht lesbar: {error}"})
            continue
        except json.JSONDecodeError as error:
            files[path.name] = None
            errors.append({"file": path.name, "message": f"kein gültiges JSON: {error}"})
            continue
        files[path.name] = data
        checker = CHECKS.get(path.name)
        message = None if checker is None else checker(data)
        if message is not None:
            errors.append({"file": path.name, "message": message})
    return {"format_version": FORMAT_VERSION, "data_dir": str(data_dir), "files": files,
            "errors": errors}
