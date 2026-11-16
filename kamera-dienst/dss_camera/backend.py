"""Stand von Kameras, Lampen und Presets, wie ihn die Schnittstelle zeigt (Plan 4).

`Backend` ist die Stelle, an der die Geräte (`devices.py`, später `camera.py`), die
Lampen (`lights.py`) und die Presets (`presets.py`) angeschlossen werden. Ohne Geräte
liefert es leere Listen; die Attrappe (`fake.py`) füllt sie mit erfundenen Geräten.
"""


class Backend:
    # presets.PresetStore, lights.Lights und lights.LightPresetStore; gesetzt, sobald es
    # Geräte gibt (__main__).
    preset_store = None
    light_controller = None
    light_preset_store = None
    # store.CameraStore für die Anzeigenamen (PUT cameras/{cam}/config).
    camera_store = None

    # Einträge je Kamera: id, name, model, present, state, ptz.
    # state: awake, asleep, privacy, absent, unknown.
    def cameras(self) -> list[dict]:
        return []

    def named(self, cameras: list[dict]) -> list[dict]:
        """Anzeigename aus kameras.json statt des Modells, wo einer gesetzt ist."""
        if self.camera_store is None:
            return cameras
        return [{**camera, "name": self.camera_store.name(camera["id"]) or camera["name"]}
                for camera in cameras]

    # Einträge je Lampe: id, name, reachable, on, brightness, temperature.
    def lights(self) -> list[dict]:
        return [] if self.light_controller is None else self.light_controller.status()

    # Einträge je Preset: id, name.
    def camera_presets(self) -> list[dict]:
        return [] if self.preset_store is None else self.preset_store.summary()

    def light_presets(self) -> list[dict]:
        return [] if self.light_preset_store is None else self.light_preset_store.summary()


class DeviceBackend(Backend):
    """Kameras aus der Suche in `/dev/v4l/by-id` (`devices.py`), Lampen aus `licht.json`."""

    def __init__(self, devices) -> None:
        self.devices = devices

    def cameras(self) -> list[dict]:
        return self.named(self.devices.status())
