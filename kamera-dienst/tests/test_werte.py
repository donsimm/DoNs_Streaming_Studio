"""Test kd_werte: Bildwerte lesen, setzen und merken (P15 a3).

Im Prozess mit ersetzbarer Uhr: Reihenfolge Automatik → Wert, inaktive Werte,
Zurücklesen, `null` vergisst, Neusetzen nach Umstecken und Neustart erst nach der
Wartezeit, ungültige kameras.json. Über HTTP gegen den Dienst mit Attrappen-Ordner:
GET/PUT, gemerkter Wert übersteht einen Neustart des Dienstes, ungültige Datei.
"""

import json
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SERVICE_DIR))

from dss_camera import v4l2  # noqa: E402
from dss_camera.controls import ControlManager  # noqa: E402
from dss_camera.fake import (C922_BY_ID, C922_ID, TINY2_ID, FakeDeviceFolder,  # noqa: E402
                             fake_watcher, open_fake_device)
from dss_camera.http_api import ApiError  # noqa: E402
from dss_camera.store import CameraStore  # noqa: E402
from test_api import REQUEST_TIMEOUT_S, Service, free_port, ini  # noqa: E402

C922_NODE = "video2"
TINY2_NODE = "video0"
PLF = v4l2.V4L2_CID_POWER_LINE_FREQUENCY
# Neusetzen nach der Wartezeit (3 s) bei der nächsten Suche; grosszügig wegen paralleler Bauten.
RESTORE_WAIT_S = 15


class FakeClock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


class ManagerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_werte-")
        self.root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(self.root / "geraete")
        self.folder.add_standard_cameras()
        self.data_dir = self.root / "daten"
        self.clock = FakeClock()
        self.sleeps: list[float] = []
        self.manager = self.new_manager()
        self.clock.now += 3.0  # Wartezeit nach dem Erscheinen vorbei

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def new_manager(self) -> ControlManager:
        self.watcher = fake_watcher(self.folder.root, clock=self.clock)
        self.store = CameraStore(self.data_dir)
        manager = ControlManager(self.watcher, self.store, open_fake_device, self.sleeps.append)
        self.watcher.add_listener(manager.restore_pending)
        self.watcher.scan()
        return manager

    def controls(self, camera_id: str = C922_ID) -> dict:
        return {item["key"]: item for item in self.manager.get(camera_id)}

    def assert_api_error(self, status: int, code: str, func, *args) -> ApiError:
        with self.assertRaises(ApiError) as context:
            func(*args)
        self.assertEqual((context.exception.status, context.exception.code), (status, code),
                         context.exception.message)
        return context.exception

    def test_get_c922(self) -> None:
        controls = self.controls()
        self.assertEqual(set(controls), {
            "brightness", "white_balance_automatic", "power_line_frequency",
            "white_balance_temperature", "auto_exposure", "exposure_time_absolute",
            "pan_absolute", "tilt_absolute", "focus_absolute", "focus_automatic_continuous",
            "zoom_absolute"})
        for item in controls.values():
            self.assertEqual(set(item), {"key", "label", "group", "type", "min", "max", "step",
                                         "default", "value", "menu", "inactive", "remembered",
                                         "readonly"})
            self.assertIs(item["readonly"], False)
            self.assertIs(item["remembered"], False)
        plf = controls["power_line_frequency"]
        self.assertEqual((plf["type"], plf["value"], plf["default"], plf["label"], plf["group"]),
                         ("menu", 2, 2, "Netzfrequenz", "Bild"))
        self.assertEqual(plf["menu"], [{"value": 0, "label": "Aus"}, {"value": 1, "label": "50 Hz"},
                                       {"value": 2, "label": "60 Hz"}])
        self.assertEqual([entry["value"] for entry in controls["auto_exposure"]["menu"]], [1, 3])
        pan = controls["pan_absolute"]
        self.assertEqual((pan["min"], pan["max"], pan["step"]), (-36000, 36000, 3600))
        self.assertEqual((controls["zoom_absolute"]["min"], controls["zoom_absolute"]["max"]), (100, 500))
        self.assertEqual(controls["focus_absolute"]["step"], 5)
        self.assertEqual(controls["focus_automatic_continuous"]["type"], "bool")
        self.assertIs(controls["focus_automatic_continuous"]["value"], True)
        # Automatik an: Werte dazu inaktiv.
        for key in ("exposure_time_absolute", "focus_absolute", "white_balance_temperature"):
            self.assertIs(controls[key]["inactive"], True, key)
        self.assertIs(controls["brightness"]["inactive"], False)
        self.assertEqual(self.folder.write_log(C922_NODE), [])

    def test_get_tiny2(self) -> None:
        controls = self.controls(TINY2_ID)
        self.assertEqual((controls["pan_absolute"]["min"], controls["pan_absolute"]["max"]), (-468000, 468000))
        self.assertEqual((controls["tilt_absolute"]["min"], controls["tilt_absolute"]["step"]), (-324000, 3600))
        self.assertEqual((controls["zoom_absolute"]["min"], controls["zoom_absolute"]["max"]), (0, 100))

    def test_order_auto_first(self) -> None:
        # Wert vor dem Schalter in der Anfrage: geschrieben wird der Schalter zuerst.
        result = {item["key"]: item for item in self.manager.put(C922_ID, {
            "exposure_time_absolute": 500, "white_balance_temperature": 3300,
            "auto_exposure": 1, "white_balance_automatic": False})}
        log = self.folder.write_log(C922_NODE)
        self.assertEqual(log, [(v4l2.V4L2_CID_EXPOSURE_AUTO, 1), (v4l2.V4L2_CID_AUTO_WHITE_BALANCE, 0),
                               (v4l2.V4L2_CID_EXPOSURE_ABSOLUTE, 500),
                               (v4l2.V4L2_CID_WHITE_BALANCE_TEMPERATURE, 3300)])
        self.assertEqual(result["exposure_time_absolute"]["value"], 500)
        self.assertIs(result["exposure_time_absolute"]["inactive"], False)
        self.assertIs(result["white_balance_automatic"]["value"], False)
        self.assertIs(result["auto_exposure"]["remembered"], True)
        self.assertEqual(self.store.remembered(C922_ID), {
            "auto_exposure": 1, "white_balance_automatic": False,
            "exposure_time_absolute": 500, "white_balance_temperature": 3300})

    def test_inactive_rejected(self) -> None:
        error = self.assert_api_error(400, "invalid", self.manager.put, C922_ID,
                                      {"exposure_time_absolute": 500, "brightness": 140})
        self.assertIn("Belichtungszeit", error.message)
        # Helligkeit ist gesetzt und gemerkt, die inaktive Belichtungszeit nicht.
        self.assertEqual(self.folder.write_log(C922_NODE), [(v4l2.V4L2_CID_BRIGHTNESS, 140)])
        self.assertEqual(self.store.remembered(C922_ID), {"brightness": 140})

    def test_readback_rounds_to_step(self) -> None:
        result = {item["key"]: item for item in self.manager.put(C922_ID, {
            "focus_automatic_continuous": False, "focus_absolute": 12})}
        self.assertEqual(result["focus_absolute"]["value"], 10)
        self.assertEqual(self.store.remembered(C922_ID)["focus_absolute"], 10)

    def test_invalid_requests_change_nothing(self) -> None:
        cases = [
            ({"gibt_es_nicht": 1}, "gibt_es_nicht"),
            ({"brightness": 300}, "0 bis 255"),
            ({"brightness": 1.5}, "ganze Zahl"),
            ({"brightness": True}, "ganze Zahl"),
            ({"focus_automatic_continuous": 0}, "true oder false"),
            ({"power_line_frequency": 5}, "0, 1, 2"),
            ({"auto_exposure": 2}, "1, 3"),
            ({"pan_absolute": 3600}, "Bewegen"),
            ({"zoom_absolute": 200}, "Bewegen"),
            # Ein gültiger Wert vor einem ungültigen wird auch nicht geschrieben.
            ({"brightness": 100, "contrast": 1}, "contrast"),
        ]
        for changes, text in cases:
            with self.subTest(changes=changes):
                error = self.assert_api_error(400, "invalid", self.manager.put, C922_ID, changes)
                self.assertIn(text, error.message)
        self.assertEqual(self.folder.write_log(C922_NODE), [])
        self.assertFalse((self.data_dir / "kameras.json").exists())

    def test_unknown_and_absent(self) -> None:
        self.assert_api_error(404, "not_found", self.manager.get, "gibt-es-nicht")
        self.folder.unplug(C922_BY_ID)
        self.watcher.scan()
        self.assert_api_error(409, "camera_absent", self.manager.get, C922_ID)
        self.assert_api_error(409, "camera_absent", self.manager.put, C922_ID, {"brightness": 1})

    def test_null_resets_and_forgets(self) -> None:
        self.manager.put(C922_ID, {"power_line_frequency": 1, "brightness": 90})
        result = {item["key"]: item for item in self.manager.put(C922_ID, {"power_line_frequency": None})}
        self.assertEqual(result["power_line_frequency"]["value"], 2)
        self.assertIs(result["power_line_frequency"]["remembered"], False)
        self.assertIs(result["brightness"]["remembered"], True)
        data = json.loads((self.data_dir / "kameras.json").read_text(encoding="utf-8"))
        self.assertEqual(data, {"format_version": 1,
                                "cameras": {C922_ID: {"controls": {"brightness": 90}}}})

    def test_put_waits_for_settle_time(self) -> None:
        self.folder.unplug(C922_BY_ID)
        self.watcher.scan()
        self.folder.plug(C922_BY_ID, C922_NODE)
        self.clock.now += 10
        self.watcher.scan()
        self.clock.now += 1.0
        self.manager.put(C922_ID, {"brightness": 100})
        self.assertEqual(len(self.sleeps), 1)
        self.assertAlmostEqual(self.sleeps[0], 2.0)

    def test_restore_after_replug(self) -> None:
        self.manager.put(C922_ID, {"power_line_frequency": 1})
        self.assertEqual(self.folder.values(C922_NODE), {PLF: 1})
        self.folder.unplug(C922_BY_ID)
        self.watcher.scan()
        # Umstecken: Die C922 fällt auf 60 Hz zurück.
        self.folder.plug(C922_BY_ID, C922_NODE)
        self.assertEqual(self.folder.values(C922_NODE), {})
        before = len(self.folder.write_log(C922_NODE))
        self.clock.now += 10
        self.watcher.scan()
        start = self.clock.now
        self.clock.now = start + 2.9
        self.watcher.scan()
        self.assertEqual(self.folder.values(C922_NODE), {}, "vor der Wartezeit geschrieben")
        self.clock.now = start + 3.0
        self.watcher.scan()
        # Nur der gemerkte Wert, nie verstellte bleiben unberührt.
        self.assertEqual(self.folder.values(C922_NODE), {PLF: 1})
        self.assertEqual(self.folder.write_log(C922_NODE)[before:], [(PLF, 1)])
        # Weitere Suchen setzen nicht nochmals.
        self.clock.now += 5
        self.watcher.scan()
        self.assertEqual(len(self.folder.write_log(C922_NODE)), before + 1)
        self.assertEqual(self.folder.values(TINY2_NODE), {})

    def test_restore_on_restart_auto_first(self) -> None:
        self.manager.put(C922_ID, {"auto_exposure": 1, "exposure_time_absolute": 700})
        self.folder.unplug(C922_BY_ID)
        self.folder.plug(C922_BY_ID, C922_NODE)  # Standardwerte, Protokoll bleibt
        before = len(self.folder.write_log(C922_NODE))
        self.manager = self.new_manager()
        self.assertEqual(len(self.folder.write_log(C922_NODE)), before)
        self.clock.now += 3.0
        self.watcher.scan()
        self.assertEqual(self.folder.write_log(C922_NODE)[before:],
                         [(v4l2.V4L2_CID_EXPOSURE_AUTO, 1), (v4l2.V4L2_CID_EXPOSURE_ABSOLUTE, 700)])

    def test_invalid_file_untouched(self) -> None:
        self.data_dir.mkdir()
        path = self.data_dir / "kameras.json"
        cases = [b'{"format_version": 1, "cameras": {', b'{"format_version": 2, "cameras": {}}',
                 b'{"format_version": 1, "cameras": {"x": {"controls": {"brightness": "hell"}}}}',
                 b"\xff\xfe"]
        for content in cases:
            with self.subTest(content=content):
                path.write_bytes(content)
                self.manager = self.new_manager()
                self.assertIsNotNone(self.store.error)
                self.assert_api_error(409, "file_error", self.manager.put, C922_ID, {"brightness": 10})
                self.assertEqual(len(self.controls()), 11)  # Lesen geht weiter
                self.clock.now += 3.0
                self.watcher.scan()
                self.assertEqual(path.read_bytes(), content)
        self.assertEqual(self.folder.write_log(C922_NODE), [])


class FolderService(Service):
    def __init__(self, folder: Path, ini_text: str, devices: Path) -> None:
        super().__init__(folder, ini_text)
        self.devices = devices

    def command(self) -> list[str]:
        return super().command() + ["--geraeteordner", str(self.devices)]


def send(port: int, path: str, method: str = "GET", body=None) -> tuple[int, dict]:
    data = None if body is None else json.dumps(body).encode("utf-8")
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", data=data, method=method,
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=REQUEST_TIMEOUT_S) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode("utf-8"))


class ServiceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_werte-dienst-")
        root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(root / "geraete")
        self.folder.add_standard_cameras()
        (root / "dienst").mkdir()
        self.port = free_port()
        self.service = FolderService(root / "dienst", ini(self.port), self.folder.root)
        self.data_file = root / "dienst" / "daten" / "dss-kamera" / "kameras.json"
        self.path = f"/api/v1/cameras/{C922_ID.replace(':', '%3A')}/controls"

    def tearDown(self) -> None:
        if self.service.process is not None:
            self.service.stop()
        self.tmp.cleanup()

    def stop(self) -> None:
        self.assertEqual(self.service.stop(), 0, self.service.stderr)

    def test_get_put_and_restart(self) -> None:
        self.service.start()
        status, body = send(self.port, self.path)
        self.assertEqual(status, 200, body)
        controls = {item["key"]: item for item in body["controls"]}
        self.assertEqual(controls["power_line_frequency"]["value"], 2)

        status, body = send(self.port, self.path, "PUT", {"power_line_frequency": 1})
        self.assertEqual(status, 200, body)
        controls = {item["key"]: item for item in body["controls"]}
        self.assertEqual(controls["power_line_frequency"]["value"], 1)
        self.assertIs(controls["power_line_frequency"]["remembered"], True)
        saved = json.loads(self.data_file.read_text(encoding="utf-8"))
        self.assertEqual(saved["cameras"][C922_ID]["controls"], {"power_line_frequency": 1})

        status, body = send(self.port, self.path, "PUT", {"brightness": 999})
        self.assertEqual((status, body["code"]), (400, "invalid"))
        self.assertIn("0 bis 255", body["message"])
        status, body = send(self.port, "/api/v1/cameras/gibt-es-nicht/controls")
        self.assertEqual((status, body["code"]), (404, "not_found"))
        self.stop()

        # Neustart mit umgesteckter Kamera (60 Hz): Der Dienst setzt 50 Hz neu.
        self.folder.unplug(C922_BY_ID)
        self.folder.plug(C922_BY_ID, C922_NODE)
        self.assertEqual(self.folder.values(C922_NODE), {})
        self.service.start()
        deadline = time.monotonic() + RESTORE_WAIT_S
        while self.folder.values(C922_NODE).get(PLF) != 1 and time.monotonic() < deadline:
            time.sleep(0.2)
        self.assertEqual(self.folder.values(C922_NODE), {PLF: 1})
        status, body = send(self.port, self.path)
        controls = {item["key"]: item for item in body["controls"]}
        self.assertEqual(controls["power_line_frequency"]["value"], 1)
        self.stop()
        self.assertIn(f"Gemerkte Werte gesetzt für {C922_ID}: power_line_frequency=1.",
                      self.service.stderr)

    def test_invalid_file(self) -> None:
        self.data_file.parent.mkdir(parents=True)
        content = b'{"format_version": 1, "cameras": [}'
        self.data_file.write_bytes(content)
        self.service.start()
        status, body = send(self.port, self.path, "PUT", {"brightness": 10})
        self.assertEqual((status, body["code"]), (409, "file_error"))
        status, _ = send(self.port, self.path)
        self.assertEqual(status, 200)
        self.stop()
        self.assertEqual(self.data_file.read_bytes(), content)
        self.assertIn("ungültig, wird nicht überschrieben", self.service.stderr)
        self.assertEqual(self.folder.write_log(C922_NODE), [])

    def test_fake_without_folder(self) -> None:
        service = Service(self.service.folder, ini(self.port))
        service.start()
        try:
            status, body = send(self.port, "/api/v1/cameras/attrappe-c922/controls")
            self.assertEqual((status, body["code"]), (404, "not_found"))
        finally:
            self.assertEqual(service.stop(), 0, service.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
