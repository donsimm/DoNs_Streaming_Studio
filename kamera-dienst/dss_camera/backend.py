"""Stand von Kameras, Lampen und Presets, wie ihn die Schnittstelle zeigt (Plan 4).

`Backend` ist die Stelle, an der die Geräte (`devices.py`, später `camera.py`), die
Lampen (`lights.py`) und die Presets (`presets.py`) angeschlossen werden. Ohne Geräte
liefert es leere Listen; die Attrappe (`fake.py`) füllt sie mit erfundenen Geräten.
"""


class Backend:
    # Einträge je Kamera: id, name, model, present, state, ptz.
    # state: awake, asleep, privacy, absent, unknown.
    def cameras(self) -> list[dict]:
        return []

    # Einträge je Lampe: id, name, reachable, on, brightness, temperature.
    def lights(self) -> list[dict]:
        return []

    # Einträge je Preset: id, name.
    def camera_presets(self) -> list[dict]:
        return []

    def light_presets(self) -> list[dict]:
        return []


class DeviceBackend(Backend):
    """Kameras aus der Suche in `/dev/v4l/by-id` (`devices.py`); Lampen und Presets folgen."""

    def __init__(self, devices) -> None:
        self.devices = devices

    def cameras(self) -> list[dict]:
        return self.devices.status()
