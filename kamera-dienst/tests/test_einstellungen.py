"""Test kd_einstellungen: Einstellungen je Kamera (P15 c4, Plan 2.3 Regeln 3 und 4, 2.4, 4).

- Im Prozess mit ersetzbarer Uhr: GET/PUT config setzen und lesen, Grenzen wirken sofort auf
  move (auch eine laufende Dauerbewegung), step und Presets, Tilt der Tiny 2 unter −75°
  abgewiesen, Reihenfolge der Grenzen, Richtung umkehren, eigene Mitte für home, ungültige
  Werte mit Bereich, ungültige Datei bleibt unverändert, ausgesteckte Kamera, Ereignis-Hook.
- Über HTTP gegen den Dienst mit Attrappen-Ordner: PUT/GET config, Ereignisse `config` und
  `status` (neuer Name), Neustart behält die Werte.
"""

import functools
import json
import sys
import tempfile
import unittest
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SERVICE_DIR))

from dss_camera import motion, v4l2  # noqa: E402
from dss_camera.controls import ControlManager  # noqa: E402
from dss_camera.fake import (C922_ID, TINY2_BY_ID, TINY2_ID, FakeDeviceFolder,  # noqa: E402
                             fake_watcher, open_fake_device)
from dss_camera.http_api import ApiError  # noqa: E402
from dss_camera.motion import Mover  # noqa: E402
from dss_camera.presets import CameraPresets, PresetStore  # noqa: E402
from dss_camera.store import CameraStore  # noqa: E402
from test_api import free_port, ini  # noqa: E402
from test_ereignisse import SseReader  # noqa: E402
from test_obsbot import FakeTime  # noqa: E402
from test_werte import FolderService, send  # noqa: E402

NODE = "video0"
PAN = v4l2.V4L2_CID_PAN_ABSOLUTE
TILT = v4l2.V4L2_CID_TILT_ABSOLUTE
DEG = 3600


class ConfigTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_einstellungen-")
        self.root = Path(self.tmp.name)
        self.data_dir = self.root / "daten"
        self.folder = FakeDeviceFolder(self.root / "geraete")
        self.folder.add_standard_cameras()
        self.time = FakeTime()
        self.watcher = fake_watcher(self.folder.root, clock=self.time)
        self.store = CameraStore(self.data_dir)
        self.manager = ControlManager(self.watcher, self.store,
                                      functools.partial(open_fake_device, clock=self.time),
                                      self.time.sleep, self.time)
        self.watcher.scan()
        self.time.now += 3.0  # Wartezeit nach dem Erscheinen vorbei
        self.mover = Mover(self.manager, self.store, self.time)
        self.events: list[tuple[str, dict]] = []
        self.mover.on_config = lambda camera_id, config: self.events.append((camera_id, config))

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def writes(self, control: int) -> list[int]:
        return [entry[1] for entry in self.folder.write_log(NODE) if entry[0] == control]

    def put(self, body: dict, camera_id: str = TINY2_ID) -> dict:
        return self.mover.put_config(camera_id, body)

    def assert_invalid(self, body: dict, text: str, camera_id: str = TINY2_ID) -> None:
        with self.assertRaises(ApiError) as context:
            self.put(body, camera_id)
        self.assertEqual((context.exception.status, context.exception.code), (400, "invalid"))
        self.assertIn(text, context.exception.message)

    def hold(self, body: dict, seconds: float) -> None:
        """Dauerbewegung mit Wiederholung alle 200 ms und `tick` alle 50 ms."""
        self.mover.move(TINY2_ID, body)
        for index in range(1, round(seconds / motion.TICK_S) + 1):
            self.time.now += motion.TICK_S
            if index % round(motion.REPEAT_S / motion.TICK_S) == 0:
                self.mover.move(TINY2_ID, body)
            self.mover.tick()

    # --- Setzen und lesen --------------------------------------------------------------

    def test_defaults_ranges_and_limits(self) -> None:
        answer = self.mover.get_config(TINY2_ID)
        # Tiny 2: Tempo in Grad je Sekunde, Standard 10 (b4).
        self.assertEqual(answer["config"], {"name": "OBSBOT Tiny 2", **motion.DEFAULTS, "speed": 10})
        self.assertEqual(self.mover.get_config(C922_ID)["config"]["speed"], 50)
        self.assertEqual(answer["ranges"]["tilt_min"], [-75, 90])
        self.assertEqual(answer["ranges"]["pan_max"], [-130, 130])
        self.assertEqual(answer["ranges"]["step_deg"], [1, 90])
        self.assertEqual(answer["limits"], {"pan": [-130, 130], "tilt": [-66, 90]})
        c922 = self.mover.get_config(C922_ID)
        self.assertEqual(c922["ranges"]["pan_min"], [-10, 10])
        self.assertEqual(c922["limits"], {"pan": [-10, 10], "tilt": [-10, 10]})

    def test_set_read_and_reload(self) -> None:
        answer = self.put({"name": "  Kamera oben ", "tilt_min": -50, "pan_max": 60,
                           "invert_tilt": True, "step_deg": 5, "speed": 80,
                           "ai_off_before_move": False, "home_tilt": -10})
        config = answer["config"]
        self.assertEqual((config["name"], config["tilt_min"], config["pan_max"], config["invert_tilt"],
                          config["step_deg"], config["speed"], config["ai_off_before_move"],
                          config["home_tilt"]), ("Kamera oben", -50, 60, True, 5, 80, False, -10))
        self.assertEqual(answer["limits"], {"pan": [-130, 60], "tilt": [-50, 90]})
        self.assertEqual(self.mover.get_config(TINY2_ID), answer)
        saved = json.loads((self.data_dir / "kameras.json").read_text(encoding="utf-8"))
        self.assertEqual(saved["cameras"][TINY2_ID]["name"], "Kamera oben")
        self.assertEqual(saved["cameras"][TINY2_ID]["motion"]["tilt_min"], -50)
        # Neu geladen (wie nach einem Neustart) gleich.
        reloaded = CameraStore(self.data_dir)
        self.assertEqual(reloaded.name(TINY2_ID), "Kamera oben")
        self.assertEqual(reloaded.motion(TINY2_ID)["step_deg"], 5)
        # null = Standard, beim Namen das Modell; andere Felder bleiben.
        answer = self.put({"name": None, "tilt_min": None})
        self.assertEqual((answer["config"]["name"], answer["config"]["tilt_min"],
                          answer["config"]["pan_max"]), ("OBSBOT Tiny 2", None, 60))
        saved = json.loads((self.data_dir / "kameras.json").read_text(encoding="utf-8"))
        self.assertNotIn("name", saved["cameras"][TINY2_ID])

    def test_event_hook(self) -> None:
        answer = self.put({"invert_pan": True})
        self.assertEqual(self.events, [(TINY2_ID, answer)])

    # --- Grenzen wirken sofort ---------------------------------------------------------

    def test_limits_apply_to_move_step_and_preset(self) -> None:
        self.put({"tilt_min": -40, "pan_max": 20})
        self.hold({"tilt": -1, "pan": 1, "speed": 100}, 3.0)
        self.assertEqual(min(self.writes(TILT)), -40 * DEG)
        self.assertEqual(max(self.writes(PAN)), 20 * DEG)
        self.mover.stop(TINY2_ID)
        self.assertEqual(self.mover.step(TINY2_ID, {"tilt": -1})["position"]["tilt"], -40)
        presets = CameraPresets(PresetStore(self.data_dir), self.mover, self.manager)
        presets.put("unten", {"name": "Unten", "camera": TINY2_ID, "pan": 90, "tilt": -70, "zoom": 0})
        self.put({"tilt_min": -30})
        position = presets.recall("unten", {})["position"]
        self.assertEqual((position["pan"], position["tilt"]), (20, -30))

    def test_running_motion_takes_new_limit(self) -> None:
        # Tiny 2 fährt über das Tempo (22,1 °/s): bis 15° kein absoluter Befehl.
        self.hold({"pan": 1, "speed": 20}, 0.4)
        self.assertEqual(self.writes(PAN), [])
        self.put({"pan_max": 15})
        self.hold({"pan": 1, "speed": 20}, 2.0)
        self.assertEqual(self.writes(PAN), [15 * DEG])
        self.assertEqual(self.writes(v4l2.V4L2_CID_PAN_SPEED)[-2:], [20, 0])

    def test_tilt_floor_and_order(self) -> None:
        self.assert_invalid({"tilt_min": -76}, "von -75 bis 90")
        self.assertFalse((self.data_dir / "kameras.json").exists())
        self.assertEqual(self.put({"tilt_min": -75})["limits"]["tilt"], [-75, 90])
        self.put({"tilt_min": None})
        # Ohne eigene Untergrenze gilt −66°: die Obergrenze muss darüber liegen.
        self.assert_invalid({"tilt_max": -70}, "«tilt_min» (-66°) muss kleiner sein als «tilt_max» (-70°)")
        self.assert_invalid({"pan_min": 30, "pan_max": 30}, "muss kleiner sein")
        self.assert_invalid({"pan_min": 11}, "von -10 bis 10", C922_ID)

    def test_invalid_values(self) -> None:
        self.assert_invalid({"unbekannt": 1}, "Unbekanntes Feld «unbekannt»")
        self.assert_invalid({"invert_pan": 1}, "true oder false")
        self.assert_invalid({"step_deg": 0.5}, "«step_deg» erwartet eine Zahl von 1 bis 90")
        self.assert_invalid({"speed": 0}, "von 1 bis 100")
        self.assert_invalid({"zoom_step_percent": "10"}, "von 1 bis 100")
        self.assert_invalid({"home_pan": 200}, "von -130 bis 130")
        self.assert_invalid({"name": " "}, "1 bis 40 Zeichen")
        self.assert_invalid({"name": "x" * 41}, "1 bis 40 Zeichen")
        # Eine ungültige Angabe weist die ganze Anfrage ab.
        self.assert_invalid({"speed": 70, "step_deg": 100}, "step_deg")
        self.assertEqual(self.mover.get_config(TINY2_ID)["config"]["speed"], 10)
        self.assertEqual(self.events, [])
        with self.assertRaises(ApiError) as context:
            self.mover.put_config("cccc:0003", {"speed": 70})
        self.assertEqual(context.exception.code, "not_found")

    def test_absent_camera_configurable(self) -> None:
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        self.assertEqual(self.put({"name": "Tiny weg", "tilt_min": -60})["limits"]["tilt"], [-60, 90])

    # --- Richtung und Mitte -------------------------------------------------------------

    def test_invert_direction(self) -> None:
        self.put({"invert_pan": True, "invert_tilt": True, "step_deg": 3})
        position = self.mover.step(TINY2_ID, {"pan": 1, "tilt": 1})["position"]
        self.assertEqual((position["pan"], position["tilt"]), (-3, -3))
        self.put({"invert_pan": None})
        position = self.mover.step(TINY2_ID, {"pan": 1})["position"]
        self.assertEqual(position["pan"], 0)

    def test_own_home(self) -> None:
        self.put({"home_pan": 12, "home_tilt": -20})
        self.mover.step(TINY2_ID, {"pan": 1})
        position = self.mover.home(TINY2_ID, {})["position"]
        self.assertEqual((position["pan"], position["tilt"]), (12, -20))
        # Die Mitte wird wie jedes Ziel begrenzt.
        self.put({"tilt_min": -10})
        self.assertEqual(self.mover.home(TINY2_ID, {})["position"]["tilt"], -10)

    # --- Ungültige Datei -----------------------------------------------------------------

    def test_invalid_file_unchanged(self) -> None:
        path = self.data_dir / "kameras.json"
        self.data_dir.mkdir(parents=True)
        content = b'{"format_version": 1, "cameras": {"x": {"name": 5}}}\n'
        path.write_bytes(content)
        self.mover = Mover(self.manager, CameraStore(self.data_dir), self.time)
        with self.assertRaises(ApiError) as context:
            self.mover.put_config(TINY2_ID, {"speed": 70})
        self.assertEqual((context.exception.status, context.exception.code), (409, "file_error"))
        self.assertIn("ungültig", context.exception.message)
        self.assertEqual(path.read_bytes(), content)


class ServiceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_einstellungen-dienst-")
        root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(root / "geraete")
        self.folder.add_standard_cameras()
        (root / "dienst").mkdir()
        self.port = free_port()
        self.service = FolderService(root / "dienst", ini(self.port), self.folder.root)
        self.path = f"/api/v1/cameras/{TINY2_ID.replace(':', '%3A')}/config"
        self.reader: SseReader | None = None

    def tearDown(self) -> None:
        if self.reader is not None:
            self.reader.close()
        if self.service.process is not None:
            self.service.stop()
        self.tmp.cleanup()

    def test_put_events_and_restart(self) -> None:
        self.service.start()
        self.reader = SseReader(self.port)
        self.reader.wait_for(lambda kind, data: kind == "status", "status beim Anmelden")
        status, body = send(self.port, self.path, "PUT", {"name": "Kamera oben", "tilt_min": -50})
        self.assertEqual(status, 200, body)
        self.assertIs(body["ok"], True)
        self.assertEqual(body["limits"]["tilt"], [-50, 90])
        _, _, event = self.reader.wait_for(lambda kind, data: kind == "config", "config")
        self.assertEqual(event["camera"], TINY2_ID)
        self.assertEqual(event["config"], body["config"])
        self.reader.wait_for(lambda kind, data: kind == "status" and any(
            camera["name"] == "Kamera oben" for camera in data["cameras"]), "status mit Name")
        status, body = send(self.port, self.path, "PUT", {"tilt_min": -80})
        self.assertEqual((status, body["code"]), (400, "invalid"), body)
        self.assertIn("-75 bis 90", body["message"])

        self.assertEqual(self.service.stop(), 0, self.service.stderr)
        self.reader.close()
        self.reader = None
        self.service.start()
        status, body = send(self.port, self.path)
        self.assertEqual(status, 200, body)
        self.assertEqual((body["config"]["name"], body["config"]["tilt_min"]), ("Kamera oben", -50))
        status, body = send(self.port, "/api/v1/status")
        names = {camera["id"]: camera["name"] for camera in body["cameras"]}
        self.assertEqual(names[TINY2_ID], "Kamera oben")


if __name__ == "__main__":
    unittest.main(verbosity=2)
