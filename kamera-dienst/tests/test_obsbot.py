"""Test kd_obsbot: OBSBOT Tiny 2 gegen die Attrappe (P15 b1).

Aufruf: test_obsbot.py PFAD_ZU_kamera_ioctl_werte

- UVC-Anfragen in uvc_xu.py gegen <linux/usb/video.h> (Hilfsprogramm).
- CRC-16/USB und Aufbau eines Frames (Musterframe seq 0xA5, Befehl 0xA0C2, Nutzlast 0).
- Im Prozess mit ersetzbarer Uhr: Statusblock, Zustand in GET status, Wecken mit Wartezeit
  (Bewegung erst 4,5 s nach Zustand 1), Schlafen, Schlaf durch Neigen, kein «Neustart»
  (Bewegung im Schlaf bei offenem Strom), HDR lesen/setzen/merken, gesperrte Funktionen.
- Über HTTP gegen den Dienst mit Attrappen-Ordner: wake, sleep, GET/PUT controls.
"""

import functools
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SERVICE_DIR))

from dss_camera import obsbot, uvc_xu, v4l2  # noqa: E402
from dss_camera.controls import ControlManager  # noqa: E402
from dss_camera.fake import (C922_ID, TINY2_BY_ID, TINY2_ID, TINY2_SLEEP_TILT,  # noqa: E402
                             FakeDeviceFolder, fake_watcher, open_fake_device)
from dss_camera.http_api import ApiError  # noqa: E402
from dss_camera.store import CameraStore  # noqa: E402
from test_api import free_port, ini  # noqa: E402
from test_werte import FolderService, send  # noqa: E402

HELPER: Path | None = None
NODE = "video0"
PAN = v4l2.V4L2_CID_PAN_ABSOLUTE
TILT = v4l2.V4L2_CID_TILT_ABSOLUTE


class FakeTime:
    """Uhr und Warten: `sleep` stellt die Uhr vor."""

    def __init__(self) -> None:
        self.now = 1000.0
        self.slept = 0.0

    def __call__(self) -> float:
        return self.now

    def sleep(self, seconds: float) -> None:
        self.slept += seconds
        self.now += seconds


class HelperTest(unittest.TestCase):
    def test_uvc_requests(self) -> None:
        output = subprocess.run([str(HELPER)], capture_output=True, text=True, check=True).stdout
        values = {}
        for line in output.splitlines():
            name, value = line.split()
            if name.startswith("uvc_xu."):
                values[name.removeprefix("uvc_xu.")] = int(value)
        self.assertEqual(set(values), {"UVC_SET_CUR", "UVC_GET_CUR", "UVC_GET_LEN", "UVC_GET_INFO"})
        for name, value in values.items():
            self.assertEqual(getattr(uvc_xu, name), value, name)
        # Schritt 0: GET_CUR 0x81, SET_CUR 0x01, GET_LEN 0x85, GET_INFO 0x86; Länge 60, Einheit 2.
        self.assertEqual((uvc_xu.UVC_GET_CUR, uvc_xu.UVC_SET_CUR), (0x81, 0x01))
        self.assertEqual((uvc_xu.XU_UNIT, uvc_xu.XU_LENGTH), (2, 60))
        self.assertEqual((uvc_xu.SELECTOR_COMMAND, uvc_xu.SELECTOR_FRAME), (0x06, 0x02))


class FrameTest(unittest.TestCase):
    def test_crc16_usb_check_value(self) -> None:
        # Prüfwert aus dem CRC-Katalog (reveng, CRC-16/USB): «123456789» → 0xB4C8.
        self.assertEqual(obsbot.crc16_usb(b"123456789"), 0xB4C8)
        self.assertEqual(obsbot.crc16_usb(b""), 0x0000)

    def test_sample_frame(self) -> None:
        frame = obsbot.build_frame(0xA5, 0xA0C2, struct.pack("<I", 0))
        self.assertEqual(len(frame), 60)
        # CRC über den Kopf mit noch leerem CRC-Feld (Offset 6), dann über Länge, leeres
        # CRC-Feld (Offset 14) und Nutzlast.
        head = bytes([0xAA, 0x25, 0xA5, 0x00, 0x0C, 0x00, 0, 0, 0x0A, 0x02, 0xC2, 0xA0])
        crc_head = obsbot.crc16_usb(head)
        crc_body = obsbot.crc16_usb(bytes([0x04, 0x00, 0, 0]) + bytes(4))
        expected = bytearray(head + bytes([0x04, 0x00, 0, 0]) + bytes(44))
        struct.pack_into("<H", expected, 6, crc_head)
        struct.pack_into("<H", expected, 14, crc_body)
        self.assertEqual(frame, bytes(expected))
        self.assertNotEqual(crc_head, 0)
        # Sequenz läuft je Frame weiter und bricht nach 0xFFFF um.
        self.assertEqual(obsbot.build_frame(0x1FFFF, 0xA0C2, b"")[2:4], b"\xff\xff")


class ManagerTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_obsbot-")
        self.root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(self.root / "geraete")
        self.folder.add_standard_cameras()
        self.time = FakeTime()
        self.watcher = fake_watcher(self.folder.root, clock=self.time)
        self.store = CameraStore(self.root / "daten")
        self.manager = ControlManager(self.watcher, self.store,
                                      functools.partial(open_fake_device, clock=self.time),
                                      self.time.sleep, self.time)
        self.watcher.add_listener(self.manager.restore_pending)
        self.watcher.scan()
        self.time.now += 3.0  # Wartezeit nach dem Erscheinen vorbei

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def tiny2(self) -> dict:
        return self.folder.tiny2(NODE)

    def state(self, camera_id: str = TINY2_ID) -> str:
        return next(item["state"] for item in self.watcher.status() if item["id"] == camera_id)

    def controls(self, camera_id: str = TINY2_ID) -> dict:
        return {item["key"]: item for item in self.manager.get(camera_id)}

    def assert_api_error(self, status: int, code: str, func, *args) -> ApiError:
        with self.assertRaises(ApiError) as context:
            func(*args)
        self.assertEqual((context.exception.status, context.exception.code), (status, code),
                         context.exception.message)
        return context.exception

    def move(self, value: int, wake: bool, control: int = PAN) -> float:
        """Bewegungsbefehl wie Abschnitt b2; Rückgabe: Zeitpunkt des Befehls."""
        with self.manager.movable(TINY2_ID, wake) as device:
            device.set(control, value)
            return self.time.now

    def frames(self) -> list[str]:
        return [data for selector, data in self.tiny2()["xu_protokoll"] if selector == uvc_xu.SELECTOR_FRAME]

    def test_status_block(self) -> None:
        controls = self.controls()
        obsbot_items = {key: item for key, item in controls.items() if item["group"] == "OBSBOT"}
        self.assertEqual(set(obsbot_items), {"obsbot_hdr", "obsbot_state", "obsbot_auto_sleep"})
        self.assertEqual((controls["obsbot_hdr"]["value"], controls["obsbot_hdr"]["readonly"]), (False, False))
        self.assertEqual((controls["obsbot_state"]["value"], controls["obsbot_state"]["readonly"]), (1, True))
        self.assertEqual([entry["label"] for entry in controls["obsbot_state"]["menu"]],
                         ["Wach", "Schlaf", "Privatsphäre"])
        self.assertEqual((controls["obsbot_auto_sleep"]["value"], controls["obsbot_auto_sleep"]["readonly"]),
                         (120, True))
        for item in obsbot_items.values():
            self.assertEqual(set(item), set(controls["pan_absolute"]))
        self.assertEqual(self.state(), "awake")
        # Die C922 hat keine Gruppe «OBSBOT».
        self.assertFalse(any(key.startswith("obsbot_") for key in self.controls(C922_ID)))

        # Zustand von aussen geändert: GET controls und die Suche (alle 5 s) führen ihn nach.
        self.folder.update_tiny2(NODE, run=obsbot.RUN_PRIVACY)
        self.assertEqual(self.controls()["obsbot_state"]["value"], 4)
        self.assertEqual(self.state(), "privacy")
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP)
        self.watcher.scan()
        self.assertEqual(self.state(), "privacy")  # noch keine 5 s
        self.time.now += obsbot.STATE_INTERVAL_S
        self.watcher.scan()
        self.assertEqual(self.state(), "asleep")
        self.assertEqual(self.tiny2()["xu_protokoll"], [])

    def test_wake_then_wait_before_move(self) -> None:
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP, stream_open=True)
        start = self.time.now
        moved_at = self.move(36000, wake=True)
        state = self.tiny2()
        self.assertEqual(len(self.frames()), 1)
        self.assertEqual(state["run"], obsbot.RUN_AWAKE)
        # Zustand 1 nach 1,5 s (Abfrage alle 0,1 s), dann 4,5 s Wartezeit.
        self.assertGreaterEqual(moved_at - state["awake_since"], obsbot.AFTER_WAKE_S - 1e-6)
        self.assertLess(moved_at - start, 1.5 + 2 * obsbot.POLL_S + obsbot.AFTER_WAKE_S)
        self.assertEqual((state["lost_moves"], state["restarts"], state["moves_asleep"]), (0, 0, 0))
        self.assertEqual(self.folder.write_log(NODE), [(PAN, 36000)])
        self.assertEqual(self.state(), "awake")

        # Danach ohne Wartezeit.
        before = self.time.slept
        self.move(0, wake=False)
        self.assertEqual(self.time.slept, before)

    def test_explicit_wake_then_move_waits_rest(self) -> None:
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP)
        self.assertEqual(self.manager.wake(TINY2_ID), "awake")
        self.assertEqual(self.state(), "awake")
        awake_since = self.tiny2()["awake_since"]
        self.time.now += 1.0
        moved_at = self.move(7200, wake=False)
        self.assertGreaterEqual(moved_at - awake_since, obsbot.AFTER_WAKE_S - 1e-6)
        self.assertEqual(self.tiny2()["lost_moves"], 0)
        # Ein zweites Wecken bei wacher Kamera schickt keinen Frame.
        self.manager.wake(TINY2_ID)
        self.assertEqual(len(self.frames()), 1)

    def test_fake_drops_moves_while_homing(self) -> None:
        # Gegenprobe: Ohne die Wartezeit verwirft die Attrappe den Befehl wie das Gerät.
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP)
        self.manager.wake(TINY2_ID)
        with open_fake_device(self.folder.dev / NODE, self.time) as device:
            device.set(PAN, 36000)
        self.assertEqual(self.tiny2()["lost_moves"], 1)
        self.assertEqual(self.folder.write_log(NODE), [(PAN, 36000, "verworfen")])

    def test_wake_timeout(self) -> None:
        # Kamera wacht nicht auf: nach 10 s 504, keine Bewegung.
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP, wake_at=self.time.now + 100)
        self.assert_api_error(504, "timeout", self.move, 3600, True)
        self.assertGreaterEqual(self.time.slept, obsbot.WAKE_TIMEOUT_S - 1e-6)
        self.assertEqual(self.folder.write_log(NODE), [])
        self.assertEqual(self.state(), "asleep")

    def test_asleep_without_wake(self) -> None:
        for run, state in ((obsbot.RUN_ASLEEP, "asleep"), (obsbot.RUN_PRIVACY, "privacy")):
            with self.subTest(state=state):
                self.folder.update_tiny2(NODE, run=run, stream_open=True)
                self.assert_api_error(409, "camera_asleep", self.move, 3600, False)
                self.assertEqual(self.state(), state)
        self.assertEqual(self.folder.write_log(NODE), [])
        self.assertEqual(self.frames(), [])
        self.assertEqual(self.tiny2()["restarts"], 0)

    def test_sleep(self) -> None:
        self.assertEqual(self.manager.send_to_sleep(TINY2_ID), "asleep")
        self.assertEqual(self.tiny2()["run"], obsbot.RUN_ASLEEP)
        self.assertEqual(self.state(), "asleep")
        self.assertEqual(len(self.frames()), 1)
        sleep_frame = bytes.fromhex(self.frames()[0]).ljust(60, b"\0")
        self.assertEqual(struct.unpack_from("<H", sleep_frame, 10)[0], obsbot.CMD_SLEEP_WAKE)
        self.assertEqual(struct.unpack_from("<I", sleep_frame, 16)[0], obsbot.PAYLOAD_SLEEP)
        self.assertEqual(self.controls()["obsbot_state"]["value"], 3)

    def test_sleep_by_tilt(self) -> None:
        self.folder.update_tiny2(NODE, stream_open=True)
        self.move(-74 * 3600, wake=False, control=TILT)
        self.assertEqual(self.tiny2()["run"], obsbot.RUN_AWAKE)
        self.move(TINY2_SLEEP_TILT, wake=False, control=TILT)
        self.assertEqual(self.tiny2()["run"], obsbot.RUN_ASLEEP)
        # Der nächste Befehl geht nicht in die schlafende Kamera (Regel 1), auch nicht aus der Suche.
        self.assert_api_error(409, "camera_asleep", self.move, 0, False, TILT)
        self.time.now += obsbot.STATE_INTERVAL_S
        self.watcher.scan()
        self.assertEqual(self.state(), "asleep")
        self.move(0, wake=True, control=TILT)
        state = self.tiny2()
        self.assertEqual((state["run"], state["restarts"], state["lost_moves"]), (obsbot.RUN_AWAKE, 0, 0))

    def test_no_restart_with_stream_open(self) -> None:
        self.folder.update_tiny2(NODE, stream_open=True)
        for _ in range(3):
            self.manager.send_to_sleep(TINY2_ID)
            self.move(3600, wake=True)
            self.assertRaises(ApiError, self.manager.put, TINY2_ID, {"pan_absolute": 0})
        state = self.tiny2()
        self.assertEqual((state["restarts"], state["lost_moves"], state["moves_asleep"]), (0, 0, 0))
        self.assertEqual(len(self.frames()), 6)

        # Gegenprobe: Ein Befehl direkt an die schlafende Kamera zählt als Neustart.
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP)
        with open_fake_device(self.folder.dev / NODE, self.time) as device:
            device.set(PAN, 0)
        self.assertEqual(self.tiny2()["restarts"], 1)

    def test_hdr(self) -> None:
        result = {item["key"]: item for item in self.manager.put(TINY2_ID, {"obsbot_hdr": True})}
        self.assertEqual((result["obsbot_hdr"]["value"], result["obsbot_hdr"]["remembered"]), (True, True))
        self.assertEqual(self.tiny2()["xu_protokoll"], [[uvc_xu.SELECTOR_COMMAND, "010101"]])
        self.assertEqual(self.store.remembered(TINY2_ID), {"obsbot_hdr": True})
        self.assertEqual(self.folder.values(NODE), {})

        # Neu einstecken: gemerktes HDR wird nach der Wartezeit wieder gesetzt.
        self.folder.update_tiny2(NODE, hdr=0)
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        self.folder.plug(TINY2_BY_ID, NODE)
        self.watcher.scan()
        self.assertEqual(self.tiny2()["hdr"], 0)
        self.time.now += 3.0
        self.watcher.scan()
        self.assertEqual(self.tiny2()["hdr"], 1)

        # Im Schlaf abgewiesen, ohne Befehl.
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP, xu_protokoll=[])
        self.assert_api_error(409, "camera_asleep", self.manager.put, TINY2_ID, {"obsbot_hdr": False})
        self.assertEqual(self.tiny2()["xu_protokoll"], [])

        # null: aus und vergessen.
        self.folder.update_tiny2(NODE, run=obsbot.RUN_AWAKE)
        result = {item["key"]: item for item in self.manager.put(TINY2_ID, {"obsbot_hdr": None})}
        self.assertEqual((result["obsbot_hdr"]["value"], result["obsbot_hdr"]["remembered"]), (False, False))
        self.assertEqual(self.store.remembered(TINY2_ID), {})
        error = self.assert_api_error(400, "invalid", self.manager.put, TINY2_ID, {"obsbot_hdr": 1})
        self.assertIn("true oder false", error.message)

    def test_locked_functions(self) -> None:
        keys = set(self.controls())
        self.assertFalse({key for key in keys if "fov" in key or "ai" in key or "view" in key
                          or "tracking" in key} - {"obsbot_state"})
        for key in ("obsbot_state", "obsbot_auto_sleep"):
            with self.subTest(key=key):
                error = self.assert_api_error(400, "invalid", self.manager.put, TINY2_ID, {key: 60})
                self.assertIn("nur lesbar", error.message)
        for key in ("obsbot_fov", "obsbot_ai_mode", "obsbot_tracking"):
            with self.subTest(key=key):
                self.assert_api_error(400, "invalid", self.manager.put, TINY2_ID, {key: 0})
        self.assertEqual(self.tiny2()["xu_protokoll"], [])
        self.assertEqual(self.tiny2()["auto_sleep"], 120)
        # Wecken und Schlafen nur für die Tiny 2.
        self.assert_api_error(400, "invalid", self.manager.wake, C922_ID)
        self.assert_api_error(400, "invalid", self.manager.send_to_sleep, C922_ID)


class ServiceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_obsbot-dienst-")
        root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(root / "geraete")
        self.folder.add_standard_cameras()
        (root / "dienst").mkdir()
        self.port = free_port()
        self.service = FolderService(root / "dienst", ini(self.port), self.folder.root)
        self.base = f"/api/v1/cameras/{TINY2_ID.replace(':', '%3A')}"

    def tearDown(self) -> None:
        if self.service.process is not None:
            self.service.stop()
        self.tmp.cleanup()

    def state(self) -> str:
        _, body = send(self.port, "/api/v1/status")
        return next(item["state"] for item in body["cameras"] if item["id"] == TINY2_ID)

    def test_wake_sleep_hdr(self) -> None:
        self.service.start()
        status, body = send(self.port, f"{self.base}/sleep", "POST", {})
        self.assertEqual((status, body.get("state")), (200, "asleep"), body)
        self.assertEqual(self.state(), "asleep")
        status, body = send(self.port, f"{self.base}/controls", "PUT", {"obsbot_hdr": True})
        self.assertEqual((status, body["code"]), (409, "camera_asleep"))

        # Wecken in Echtzeit: Attrappe meldet Zustand 1 nach 1,5 s.
        status, body = send(self.port, f"{self.base}/wake", "POST", {})
        self.assertEqual((status, body.get("state")), (200, "awake"), body)
        self.assertEqual(self.state(), "awake")
        status, body = send(self.port, f"{self.base}/controls", "PUT", {"obsbot_hdr": True})
        self.assertEqual(status, 200, body)
        controls = {item["key"]: item for item in body["controls"]}
        self.assertIs(controls["obsbot_hdr"]["value"], True)
        self.assertEqual(controls["obsbot_state"]["value"], 1)

        status, body = send(self.port, f"/api/v1/cameras/{C922_ID.replace(':', '%3A')}/wake", "POST", {})
        self.assertEqual((status, body["code"]), (400, "invalid"))
        self.assertEqual(self.service.stop(), 0, self.service.stderr)
        self.assertEqual(self.folder.tiny2(NODE)["restarts"], 0)


if __name__ == "__main__":
    HELPER = Path(sys.argv.pop(1))
    unittest.main(verbosity=2)
