"""Attrappe für Tests und Prüfungen ohne Geräte (`--attrappe`).

Simuliert eine OBSBOT Tiny 2, eine Logitech C922 und zwei Lampen. Alle Kennungen
und Namen sind erfunden.
"""

from .backend import Backend


class FakeBackend(Backend):
    def __init__(self) -> None:
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
        return [dict(camera) for camera in self._cameras]

    def lights(self) -> list[dict]:
        return [dict(light) for light in self._lights]

    def camera_presets(self) -> list[dict]:
        return [dict(preset) for preset in self._camera_presets]

    def light_presets(self) -> list[dict]:
        return [dict(preset) for preset in self._light_presets]
