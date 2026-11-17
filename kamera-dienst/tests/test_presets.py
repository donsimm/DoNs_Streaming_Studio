"""Test kd_presets: Kamera-Presets gegen die Attrappe (P15 b3).

- Im Prozess mit ersetzbarer Uhr: Speichern aus der aktuellen Position, Abruf (auch aus dem
  Schlaf mit Wecken und Wartezeit, ohne verlorene Befehle und ohne «Neustarts»), Grenzen
  gelten auch für Presets (Tilt unter −66° wird begrenzt), Bildwerte wahlweise, Kennungen aus
  dem Namen, Umbenennen behält die Kennung, Löschen, ungültige Datei bleibt Byte für Byte
  gleich und Änderungen werden abgewiesen.
- Über HTTP gegen den Dienst mit Attrappen-Ordner: anlegen, status, lesen, recall,
  save-current, umbenennen, löschen, ungültige Datei nach dem Neustart.
"""

import functools
import json
import sys
import tempfile
import unittest
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SERVICE_DIR))

from dss_camera import obsbot, presets, uvc_xu, v4l2  # noqa: E402
from dss_camera.controls import ControlManager  # noqa: E402
from dss_camera.fake import (C922_ID, TINY2_ID, TINY2_WAKE_S,  # noqa: E402
                             FakeDeviceFolder, fake_watcher, open_fake_device)
from dss_camera.http_api import ApiError  # noqa: E402
from dss_camera.motion import Mover  # noqa: E402
from dss_camera.presets import CameraPresets, PresetStore  # noqa: E402
from dss_camera.store import CameraStore  # noqa: E402
from test_api import free_port, ini  # noqa: E402
from test_obsbot import FakeTime  # noqa: E402
from test_werte import FolderService, send  # noqa: E402

NODE = "video0"
C922_NODE = "video2"
PAN = v4l2.V4L2_CID_PAN_ABSOLUTE
TILT = v4l2.V4L2_CID_TILT_ABSOLUTE
ZOOM = v4l2.V4L2_CID_ZOOM_ABSOLUTE
BRIGHTNESS = v4l2.V4L2_CID_BRIGHTNESS
DEG = 3600


class PresetTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_presets-")
        self.root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(self.root / "geraete")
        self.folder.add_standard_cameras()
        self.time = FakeTime()
        self.watcher = fake_watcher(self.folder.root, clock=self.time)
        self.data_dir = self.root / "daten"
        self.store = CameraStore(self.data_dir)
        self.manager = ControlManager(self.watcher, self.store,
                                      functools.partial(open_fake_device, clock=self.time),
                                      self.time.sleep, self.time)
        self.watcher.add_listener(self.manager.restore_pending)
        self.watcher.scan()
        self.time.now += 3.0  # Wartezeit nach dem Erscheinen vorbei
        self.mover = Mover(self.manager, self.store, self.time)
        self.presets = self.make_presets()

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def make_presets(self) -> CameraPresets:
        return CameraPresets(PresetStore(self.data_dir), self.mover, self.manager)

    def tiny2(self) -> dict:
        return self.folder.tiny2(NODE)

    def saved(self) -> dict:
        return json.loads((self.data_dir / presets.FILE_NAME).read_text(encoding="utf-8"))

    def assert_api_error(self, status: int, code: str, func, *args) -> ApiError:
        with self.assertRaises(ApiError) as context:
            func(*args)
        self.assertEqual((context.exception.status, context.exception.code), (status, code),
                         context.exception.message)
        return context.exception

    # --- Speichern und Abruf -------------------------------------------------------------

    def test_save_and_recall(self) -> None:
        self.mover.goto(TINY2_ID, 20.0, -10.0, 30, False)
        preset = self.presets.create({"name": "Totale", "camera": TINY2_ID})["preset"]
        self.assertEqual(preset, {"id": "totale", "name": "Totale", "camera": TINY2_ID,
                                  "pan": 20.0, "tilt": -10.0, "zoom": 30})
        self.assertEqual(self.saved(), {"format_version": 1, "presets": [preset]})

        self.mover.home(TINY2_ID, {})
        self.mover.goto(TINY2_ID, None, None, 0, False)
        result = self.presets.recall("totale", {})
        self.assertEqual(result, {"position": {"pan": 20.0, "tilt": -10.0, "zoom": 30}})
        values = self.folder.values(NODE)
        self.assertEqual((values[PAN], values[TILT], values[ZOOM]), (20 * DEG, -10 * DEG, 30))

        # save-current übernimmt die neue Position, Name und Kennung bleiben.
        self.mover.goto(TINY2_ID, -5.0, 3.0, 0, False)
        preset = self.presets.save_current("totale", {})["preset"]
        self.assertEqual((preset["id"], preset["name"], preset["pan"], preset["tilt"], preset["zoom"]),
                         ("totale", "Totale", -5.0, 3.0, 0))
        self.assertEqual(self.presets.get("totale"), {"preset": preset})
        # Neu geladen aus der Datei: derselbe Stand.
        self.assertEqual(self.make_presets().get("totale"), {"preset": preset})
        state = self.tiny2()
        self.assertEqual((state["run"], state["restarts"], state["lost_moves"]), (obsbot.RUN_AWAKE, 0, 0))

    def test_recall_from_sleep(self) -> None:
        self.mover.goto(TINY2_ID, 15.0, 5.0, 20, False)
        self.presets.create({"name": "Nah", "camera": TINY2_ID})
        self.mover.home(TINY2_ID, {})
        self.manager.send_to_sleep(TINY2_ID)
        self.folder.update_tiny2(NODE, stream_open=True)
        count = len(self.folder.write_log(NODE))

        # Ohne Wecken: abgewiesen, kein Befehl ans Gerät; save-current weckt nie.
        self.assert_api_error(409, "camera_asleep", self.presets.recall, "nah", {"wake": False})
        self.assert_api_error(409, "camera_asleep", self.presets.save_current, "nah", {})
        self.assertEqual(len(self.folder.write_log(NODE)), count)

        # Standard: wecken, Wartezeit abwarten, dann fahren.
        def frames() -> int:
            return sum(1 for selector, _ in self.tiny2()["xu_protokoll"] if selector == uvc_xu.SELECTOR_FRAME)

        before = frames()
        start = self.time.now
        result = self.presets.recall("nah", {})
        self.assertEqual(result["position"], {"pan": 15.0, "tilt": 5.0, "zoom": 20})
        self.assertGreaterEqual(self.time.now - start, TINY2_WAKE_S + obsbot.AFTER_WAKE_S)
        self.assertEqual(frames() - before, 1)  # ein Weckbefehl
        state = self.tiny2()
        self.assertEqual((state["run"], state["lost_moves"], state["restarts"], state["moves_asleep"]),
                         (obsbot.RUN_AWAKE, 0, 0, 0))
        values = self.folder.values(NODE)
        self.assertEqual((values[PAN], values[TILT], values[ZOOM]), (15 * DEG, 5 * DEG, 20))

    def test_limits_apply_to_presets(self) -> None:
        # Von Hand angelegtes Preset unter der Grenze: Abruf hält bei −66°.
        preset = self.presets.put("boden", {"name": "Boden", "camera": TINY2_ID,
                                            "pan": 500.0, "tilt": -80.0, "zoom": 1000})["preset"]
        self.assertEqual(preset["id"], "boden")
        result = self.presets.recall("boden", {})
        self.assertEqual(result["position"], {"pan": 130.0, "tilt": -66.0, "zoom": 100})
        tilts = [value for control, value, *_ in self.folder.write_log(NODE) if control == TILT]
        self.assertEqual(min(tilts), -66 * DEG)
        # Eigene Grenzen gelten ebenfalls.
        self.store.update_motion(TINY2_ID, {"pan_max": 45})
        self.assertEqual(self.presets.recall("boden", {})["position"]["pan"], 45.0)
        self.assertEqual((self.tiny2()["run"], self.tiny2()["restarts"]), (obsbot.RUN_AWAKE, 0))

    def test_controls_optional(self) -> None:
        self.manager.put(C922_ID, {"brightness": 200, "power_line_frequency": 1})
        self.mover.goto(C922_ID, 5.0, -3.0, 200, False)
        without = self.presets.create({"name": "Pult", "camera": C922_ID})["preset"]
        self.assertNotIn("controls", without)
        preset = self.presets.create({"name": "Pult hell", "camera": C922_ID,
                                      "with_controls": True})["preset"]
        self.assertEqual((preset["id"], preset["pan"], preset["tilt"], preset["zoom"]),
                         ("pult-hell", 5.0, -3.0, 200))
        controls = preset["controls"]
        self.assertEqual((controls["brightness"], controls["power_line_frequency"]), (200, 1))
        # Ohne Position (über pan/tilt/zoom) und ohne inaktive Werte (Automatik an).
        for key in ("pan_absolute", "tilt_absolute", "zoom_absolute", "white_balance_temperature",
                    "exposure_time_absolute", "focus_absolute"):
            self.assertNotIn(key, controls)

        self.manager.put(C922_ID, {"brightness": 50})
        self.mover.home(C922_ID, {})
        self.presets.recall("pult", {})
        self.assertEqual(self.folder.values(C922_NODE)[BRIGHTNESS], 50)
        result = self.presets.recall("pult-hell", {})
        self.assertEqual(result["position"], {"pan": 5.0, "tilt": -3.0, "zoom": 200})
        self.assertEqual({item["key"]: item["value"] for item in result["controls"]}["brightness"], 200)
        self.assertEqual(self.folder.values(C922_NODE)[BRIGHTNESS], 200)

        # save-current ohne Kästchen entfernt die Bildwerte; mit Kästchen kommen sie wieder.
        self.assertNotIn("controls", self.presets.save_current("pult-hell", {})["preset"])
        self.assertIn("controls", self.presets.save_current("pult-hell", {"with_controls": True})["preset"])
        # Tiny 2: HDR und Bildwinkel kommen mit, die nur lesbaren Werte und die KI-Verfolgung
        # (ein Zustand, P15 e1b) nicht.
        tiny = self.presets.create({"name": "Tiny", "camera": TINY2_ID, "with_controls": True})["preset"]
        self.assertEqual(set(tiny["controls"]), {obsbot.KEY_HDR, obsbot.KEY_FOV})

    # --- Kennungen, Umbenennen, Löschen ------------------------------------------------

    def test_ids(self) -> None:
        for name, expected in (("Totale", "totale"), ("Schräg über Tür", "schraeg-ueber-tuer"),
                               ("Groß Bild", "gross-bild"), ("Café  Nähe!", "cafe-naehe"),
                               ("  ---  ", "preset"), ("Kamera 2", "kamera-2")):
            with self.subTest(name=name):
                self.assertEqual(presets.id_from_name(name), expected)

        first = self.presets.create({"name": "Totale", "camera": TINY2_ID})["preset"]
        second = self.presets.create({"name": "totale!", "camera": TINY2_ID})["preset"]
        third = self.presets.create({"name": "Totale", "camera": TINY2_ID})["preset"]
        self.assertEqual([first["id"], second["id"], third["id"]], ["totale", "totale-2", "totale-3"])

        # Umbenennen behält die Kennung (Stream Deck, Aktionen).
        renamed = self.presets.put("totale", {"name": "Weit"})["preset"]
        self.assertEqual((renamed["id"], renamed["name"], renamed["pan"]), ("totale", "Weit", first["pan"]))
        self.assertEqual(self.presets.store.summary(),
                         [{"id": "totale", "name": "Weit"}, {"id": "totale-2", "name": "totale!"},
                          {"id": "totale-3", "name": "Totale"}])
        self.assertEqual(self.presets.delete("totale-2"), {})
        self.assert_api_error(404, "not_found", self.presets.get, "totale-2")
        self.assert_api_error(404, "not_found", self.presets.recall, "totale-2", {})
        self.assert_api_error(404, "not_found", self.presets.delete, "totale-2")
        self.assertEqual([p["id"] for p in self.saved()["presets"]], ["totale", "totale-3"])

    def test_invalid_requests(self) -> None:
        count = len(self.folder.write_log(NODE))
        for body in ({"camera": TINY2_ID}, {"name": " ", "camera": TINY2_ID}, {"name": "A"},
                     {"name": "A", "camera": TINY2_ID, "with_controls": 1},
                     {"name": "A", "camera": TINY2_ID, "pan": 3}):
            with self.subTest(body=body):
                self.assert_api_error(400, "invalid", self.presets.create, body)
        self.assert_api_error(404, "not_found", self.presets.create, {"name": "A", "camera": "gibt-es-nicht"})
        for preset_id, body in (("Gross Bild", {"name": "A", "camera": TINY2_ID}),
                                ("neu", {"name": "A"}),
                                ("neu", {"name": "A", "camera": TINY2_ID, "tilt": "unten"}),
                                ("neu", {"name": "A", "camera": TINY2_ID, "zoom": 1.5}),
                                ("neu", {"name": "A", "camera": TINY2_ID, "controls": {"x": "hell"}}),
                                ("neu", {"name": "A", "camera": TINY2_ID, "id": "anders"})):
            with self.subTest(preset_id=preset_id, body=body):
                self.assert_api_error(400, "invalid", self.presets.put, preset_id, body)
        self.presets.put("neu", {"name": "Neu", "camera": TINY2_ID, "pan": 1, "tilt": None, "zoom": None})
        self.assert_api_error(400, "invalid", self.presets.recall, "neu", {"wake": "ja"})
        self.assert_api_error(400, "invalid", self.presets.save_current, "neu", {"camera": C922_ID})
        self.assertEqual(len(self.folder.write_log(NODE)), count)
        self.assertEqual([p["id"] for p in self.saved()["presets"]], ["neu"])

    # --- Ungültige Datei -----------------------------------------------------------------

    def test_invalid_file(self) -> None:
        path = self.data_dir / presets.FILE_NAME
        self.data_dir.mkdir(parents=True, exist_ok=True)
        valid = {"id": "totale", "name": "Totale", "camera": TINY2_ID, "pan": 0, "tilt": 0, "zoom": 0}
        for content in (b"{kaputt", b"\xff\xfe",
                        json.dumps({"format_version": 2, "presets": []}).encode(),
                        json.dumps({"format_version": 1, "presets": [valid, valid]}).encode(),
                        json.dumps({"format_version": 1, "presets": [{**valid, "id": "Totale"}]}).encode(),
                        json.dumps({"format_version": 1, "presets": [{**valid, "tilt": "unten"}]}).encode()):
            with self.subTest(content=content):
                path.write_bytes(content)
                with self.assertLogs("dss_camera.presets", "ERROR"):
                    handler = self.make_presets()
                self.assertIsNotNone(handler.store.error)
                self.assertEqual(handler.store.summary(), [])
                calls = ((handler.create, {"name": "Neu", "camera": TINY2_ID}),
                         (handler.put, "totale", {"name": "Weit"}),
                         (handler.get, "totale"), (handler.delete, "totale"),
                         (handler.save_current, "totale", {}), (handler.recall, "totale", {}))
                for func, *args in calls:
                    error = self.assert_api_error(409, "file_error", func, *args)
                    self.assertIn(presets.FILE_NAME, error.message)
                self.assertEqual(path.read_bytes(), content)
        self.assertEqual(self.folder.write_log(NODE), [])


class ServiceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_presets-dienst-")
        root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(root / "geraete")
        self.folder.add_standard_cameras()
        (root / "dienst").mkdir()
        self.port = free_port()
        self.service = FolderService(root / "dienst", ini(self.port), self.folder.root)
        self.data_file = root / "dienst" / "daten" / "dss-kamera" / presets.FILE_NAME
        self.camera = f"/api/v1/cameras/{TINY2_ID.replace(':', '%3A')}"

    def tearDown(self) -> None:
        if self.service.process is not None:
            self.service.stop()
        self.tmp.cleanup()

    def request(self, path: str, method: str = "GET", body=None) -> tuple[int, dict]:
        return send(self.port, f"/api/v1/{path}", method, body)

    def test_presets_over_http(self) -> None:
        self.service.start()
        # Die erste Anfrage wartet die 3 s nach dem Erscheinen ab.
        status, body = send(self.port, f"{self.camera}/step", "POST", {"pan": 1, "tilt": 1})
        self.assertEqual(status, 200, body)
        status, body = self.request("camera-presets", "POST", {"name": "Totale", "camera": TINY2_ID})
        self.assertEqual(status, 200, body)
        self.assertEqual(body["preset"], {"id": "totale", "name": "Totale", "camera": TINY2_ID,
                                          "pan": 2.0, "tilt": 2.0, "zoom": 0})
        status, body = self.request("status")
        self.assertEqual(body["camera_presets"], [{"id": "totale", "name": "Totale"}])
        status, body = self.request("camera-presets/totale")
        self.assertEqual((status, body["preset"]["pan"]), (200, 2.0))

        status, body = send(self.port, f"{self.camera}/home", "POST")
        self.assertEqual(status, 200, body)
        status, body = self.request("camera-presets/totale/recall", "POST")
        self.assertEqual((status, body), (200, {"ok": True, "position": {"pan": 2.0, "tilt": 2.0, "zoom": 0}}))
        self.assertEqual(self.folder.values(NODE)[PAN], 2 * DEG)

        status, body = send(self.port, f"{self.camera}/step", "POST", {"pan": 1})
        status, body = self.request("camera-presets/totale/save-current", "POST")
        self.assertEqual((status, body["preset"]["pan"]), (200, 4.0))
        status, body = self.request("camera-presets/totale", "PUT", {"name": "Weit"})
        self.assertEqual((status, body["preset"]["id"], body["preset"]["name"]), (200, "totale", "Weit"))
        status, body = self.request("camera-presets/totale/recall", "POST", {"wake": "ja"})
        self.assertEqual((status, body["code"]), (400, "invalid"))
        status, body = self.request("camera-presets/totale", "DELETE")
        self.assertEqual((status, body), (200, {"ok": True}))
        status, body = self.request("camera-presets/totale")
        self.assertEqual((status, body["code"]), (404, "not_found"))
        self.assertEqual(self.service.stop(), 0, self.service.stderr)
        state = self.folder.tiny2(NODE)
        self.assertEqual((state["run"], state["restarts"]), (obsbot.RUN_AWAKE, 0))

    def test_invalid_file_after_restart(self) -> None:
        content = b'{"format_version": 1, "presets": [{"id": "totale"'
        self.data_file.parent.mkdir(parents=True, exist_ok=True)
        self.data_file.write_bytes(content)
        self.service.start()
        status, body = self.request("status")
        self.assertEqual((status, body["camera_presets"]), (200, []))
        for path, method, payload in (("camera-presets", "POST", {"name": "Neu", "camera": TINY2_ID}),
                                      ("camera-presets/totale", "PUT", {"name": "Weit"}),
                                      ("camera-presets/totale", "DELETE", None),
                                      ("camera-presets/totale/recall", "POST", None)):
            with self.subTest(path=path, method=method):
                status, body = self.request(path, method, payload)
                self.assertEqual((status, body["code"]), (409, "file_error"))
        self.assertEqual(self.service.stop(), 0, self.service.stderr)
        self.assertIn(f"{presets.FILE_NAME} ungültig", self.service.stderr)
        self.assertEqual(self.data_file.read_bytes(), content)


if __name__ == "__main__":
    unittest.main(verbosity=2)
