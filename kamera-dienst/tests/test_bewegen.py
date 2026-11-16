"""Test kd_bewegen: Bewegen mit Schutzregeln gegen die Attrappe (P15 b2).

- Im Prozess mit ersetzbarer Uhr (`tick` von Hand alle 50 ms, Wiederholung alle 200 ms):
  Grenzen bei Dauerbewegung (Tiny 2 nie unter −66°, schläft nie ein, keine «Neustarts»;
  eigene Grenzen, Untergrenze −75°; C922 ±10°), Totmann nach 500 ms, stop, Wecken vor der
  Bewegung mit Wartezeit, KI-Befehl nicht gesendet (gesperrt), Abbruch beim Ausstecken und
  bei EPROTO ohne Nachsenden, step, home, Richtung umkehren, ungültige Anfragen.
- Über HTTP gegen den Dienst mit Attrappen-Ordner in Echtzeit: move mit Grenze, Totmann,
  stop ohne Inhalt, step, home.
"""

import functools
import sys
import tempfile
import time
import unittest
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SERVICE_DIR))

from dss_camera import motion, obsbot, uvc_xu, v4l2  # noqa: E402
from dss_camera.controls import ControlManager  # noqa: E402
from dss_camera.fake import (C922_ID, TINY2_BY_ID, TINY2_ID, TINY2_WAKE_S,  # noqa: E402
                             FakeDeviceFolder, fake_watcher, open_fake_device)
from dss_camera.http_api import ApiError  # noqa: E402
from dss_camera.motion import Mover  # noqa: E402
from dss_camera.store import CameraStore  # noqa: E402
from test_api import free_port, ini  # noqa: E402
from test_obsbot import FakeTime  # noqa: E402
from test_werte import FolderService, send  # noqa: E402

NODE = "video0"
C922_NODE = "video2"
PAN = v4l2.V4L2_CID_PAN_ABSOLUTE
TILT = v4l2.V4L2_CID_TILT_ABSOLUTE
ZOOM = v4l2.V4L2_CID_ZOOM_ABSOLUTE
DEG = 3600
AI_OFF = bytes(obsbot.AI_OFF_COMMAND).rstrip(b"\0").hex()


class MoverTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_bewegen-")
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
        self.mover = Mover(self.manager, self.store, self.time)

    def tearDown(self) -> None:
        obsbot.AI_OFF_UNLOCKED = False
        self.tmp.cleanup()

    def tiny2(self) -> dict:
        return self.folder.tiny2(NODE)

    def writes(self, control: int, node: str = NODE) -> list[int]:
        return [entry[1] for entry in self.folder.write_log(node) if entry[0] == control]

    def assert_api_error(self, status: int, code: str, func, *args) -> ApiError:
        with self.assertRaises(ApiError) as context:
            func(*args)
        self.assertEqual((context.exception.status, context.exception.code), (status, code),
                         context.exception.message)
        return context.exception

    def run_for(self, seconds: float, camera_id: str = TINY2_ID, body: dict | None = None) -> None:
        """Uhr vorstellen und alle 50 ms `tick`; mit `body` alle 200 ms eine Wiederholung."""
        steps = round(seconds / motion.TICK_S)
        repeat_every = round(motion.REPEAT_S / motion.TICK_S)
        for index in range(1, steps + 1):
            self.time.now += motion.TICK_S
            if body is not None and index % repeat_every == 0:
                self.mover.move(camera_id, body)
            self.mover.tick()
            # Die Sperre der Bildwerte ist zwischen den Schritten frei (Lesen alle 5 s, controls).
            self.assertFalse(self.manager._lock.locked())

    def hold(self, body: dict, seconds: float, camera_id: str = TINY2_ID) -> None:
        self.assertEqual(self.mover.move(camera_id, body), {"moving": True})
        self.run_for(seconds, camera_id, body)

    # --- Grenzen -----------------------------------------------------------------------

    def test_tilt_limit_holds_while_moving_down(self) -> None:
        self.folder.update_tiny2(NODE, stream_open=True)
        self.hold({"tilt": -1, "speed": 100}, 10.0)
        tilts = self.writes(TILT)
        self.assertEqual(min(tilts), -66 * DEG)
        self.assertEqual(tilts[-1], -66 * DEG)
        # Auf die Grenze kommt genau ein Befehl, danach keiner mehr (gerundeter Wert gleich).
        self.assertEqual(tilts.count(-66 * DEG), 1)
        self.assertTrue(all(a > b for a, b in zip(tilts, tilts[1:])), tilts)
        state = self.tiny2()
        self.assertEqual((state["run"], state["restarts"], state["moves_asleep"], state["lost_moves"]),
                         (obsbot.RUN_AWAKE, 0, 0, 0))
        self.assertNotIn("verworfen", [entry[-1] for entry in self.folder.write_log(NODE)])

    def test_own_limits_and_floor(self) -> None:
        self.store.update_motion(TINY2_ID, {"tilt_min": -90, "tilt_max": 20, "pan_min": -30, "pan_max": 45})
        self.hold({"tilt": -1, "pan": -1, "speed": 100}, 3.0)
        # Eigene Grenze unter −75° wird auf −75° angehoben (Einschlafen bei −76°).
        self.assertEqual(min(self.writes(TILT)), -75 * DEG)
        self.assertEqual(min(self.writes(PAN)), -30 * DEG)
        self.hold({"tilt": 1, "pan": 1, "speed": 100}, 3.0)
        self.assertEqual(max(self.writes(TILT)), 20 * DEG)
        self.assertEqual(max(self.writes(PAN)), 45 * DEG)
        self.assertEqual((self.tiny2()["run"], self.tiny2()["restarts"]), (obsbot.RUN_AWAKE, 0))

    def test_c922_digital_limits(self) -> None:
        self.hold({"pan": 1, "tilt": -1, "zoom": 1, "speed": 100}, 3.0, C922_ID)
        values = self.folder.values(C922_NODE)
        self.assertEqual((values[PAN], values[TILT], values[ZOOM]), (10 * DEG, -10 * DEG, 500))
        self.assertTrue(all(abs(value) <= 10 * DEG for value in self.writes(PAN, C922_NODE)))

    def test_axis_rounding(self) -> None:
        # Langsam (Tempo 1 = 0,6°/s): geschrieben wird nur, wenn sich das 1°-Raster ändert.
        self.hold({"pan": 1, "speed": 1}, 4.0)
        self.assertEqual(self.writes(PAN), [DEG, 2 * DEG])

    # --- Totmann und stop --------------------------------------------------------------

    def test_deadman_stops_after_500_ms(self) -> None:
        self.hold({"pan": 1, "speed": 50}, 1.0)
        moving = len(self.writes(PAN))
        self.assertGreater(moving, 15)
        last_repeat = self.time.now
        counts = []
        for _ in range(40):
            self.run_for(motion.TICK_S)
            counts.append((self.time.now - last_repeat, len(self.writes(PAN))))
        stopped_at = min(elapsed for elapsed, count in counts if count == counts[-1][1])
        self.assertLessEqual(stopped_at, motion.DEADMAN_S + motion.TICK_S + 1e-6)
        self.assertGreater(stopped_at, motion.DEADMAN_S - motion.TICK_S)
        # Danach beginnt eine neue Bewegung mit der nächsten Anfrage.
        self.hold({"pan": -1, "speed": 50}, 0.4)
        self.assertLess(self.writes(PAN)[-1], self.writes(PAN)[-3])

    def test_stop(self) -> None:
        self.hold({"pan": 1, "speed": 100}, 0.5)
        count = len(self.folder.write_log(NODE))
        self.assertEqual(self.mover.stop(TINY2_ID), {"moving": False})
        self.run_for(1.0)
        self.assertEqual(len(self.folder.write_log(NODE)), count)
        # move mit Richtung 0 hält ebenfalls an.
        self.hold({"pan": 1, "speed": 100}, 0.3)
        self.assertEqual(self.mover.move(TINY2_ID, {"pan": 0}), {"moving": False})
        count = len(self.folder.write_log(NODE))
        self.run_for(0.3)
        self.assertEqual(len(self.folder.write_log(NODE)), count)
        # stop gelingt auch ohne Kamera.
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        self.assertEqual(self.mover.stop(TINY2_ID), {"moving": False})

    # --- Wecken, KI ----------------------------------------------------------------------

    def test_wake_before_move(self) -> None:
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP, stream_open=True)
        self.assert_api_error(409, "camera_asleep", self.mover.move, TINY2_ID, {"tilt": 1})
        self.assert_api_error(409, "camera_asleep", self.mover.step, TINY2_ID, {"tilt": 1})
        self.run_for(0.5)
        self.assertEqual(self.folder.write_log(NODE), [])

        start = self.time.now
        self.hold({"tilt": 1, "speed": 100, "wake": True}, 0.5)
        state = self.tiny2()
        frames = [data for selector, data in state["xu_protokoll"] if selector == uvc_xu.SELECTOR_FRAME]
        self.assertEqual(len(frames), 1)
        # Zustand 1 nach 1,5 s, dann 4,5 s Wartezeit vor dem ersten Befehl (sonst lost_moves).
        self.assertGreaterEqual(self.time.now - start, TINY2_WAKE_S + obsbot.AFTER_WAKE_S)
        self.assertGreater(len(self.writes(TILT)), 5)
        self.assertEqual((state["run"], state["lost_moves"], state["restarts"], state["moves_asleep"]),
                         (obsbot.RUN_AWAKE, 0, 0, 0))

        # Einstellung «automatisch wecken»: ohne «wake» in der Anfrage.
        self.mover.stop(TINY2_ID)
        self.manager.send_to_sleep(TINY2_ID)
        self.store.update_motion(TINY2_ID, {"auto_wake": True})
        self.assertIn("position", self.mover.step(TINY2_ID, {"pan": 1}))
        self.assertEqual(self.tiny2()["lost_moves"], 0)
        self.assertEqual(self.tiny2()["restarts"], 0)

    def test_ai_off_not_sent_while_locked(self) -> None:
        self.hold({"pan": 1}, 0.5)
        self.mover.step(TINY2_ID, {"tilt": 1})
        self.mover.home(TINY2_ID, {})
        self.assertNotIn([uvc_xu.SELECTOR_COMMAND, AI_OFF], self.tiny2()["xu_protokoll"])
        self.assertEqual(self.tiny2()["xu_protokoll"], [])

        # Gegenprobe: freigeschaltet geht der Befehl vor der Bewegung ans Gerät …
        obsbot.AI_OFF_UNLOCKED = True
        self.mover.stop(TINY2_ID)
        self.mover.step(TINY2_ID, {"pan": 1})
        self.assertEqual(self.tiny2()["xu_protokoll"], [[uvc_xu.SELECTOR_COMMAND, AI_OFF]])
        # … ausser die Einstellung ist aus.
        self.store.update_motion(TINY2_ID, {"ai_off_before_move": False})
        self.mover.step(TINY2_ID, {"pan": 1})
        self.assertEqual(len(self.tiny2()["xu_protokoll"]), 1)

    # --- Abbruch -----------------------------------------------------------------------

    def test_abort_when_unplugged(self) -> None:
        body = {"pan": 1, "speed": 100}
        self.hold(body, 0.5)
        count = len(self.folder.write_log(NODE))
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        self.time.now += motion.TICK_S
        self.mover.tick()
        # Wiederholungen der abgebrochenen Bewegung werden abgewiesen, auch nach dem Einstecken.
        for _ in range(3):
            self.assert_api_error(409, "camera_absent", self.run_for, motion.REPEAT_S, TINY2_ID, body)
        self.folder.plug(TINY2_BY_ID, NODE)
        self.watcher.scan()
        self.time.now += 3.0
        self.assert_api_error(409, "camera_absent", self.mover.move, TINY2_ID, body)
        for _ in range(5):
            self.assert_api_error(409, "camera_absent", self.run_for, motion.REPEAT_S, TINY2_ID, body)
        self.assertEqual(len(self.folder.write_log(NODE)), count)
        # Erst nach stop gilt eine neue Bewegung.
        self.mover.stop(TINY2_ID)
        self.hold(body, 0.3)
        self.assertGreater(len(self.folder.write_log(NODE)), count)

    def test_abort_on_eproto(self) -> None:
        body = {"tilt": -1, "speed": 100}
        self.hold(body, 0.3)
        count = len(self.folder.write_log(NODE))
        for failure in ("EPROTO", "ETIMEDOUT"):
            with self.subTest(failure=failure):
                self.folder.fail(NODE, failure)
                self.time.now += motion.TICK_S
                self.mover.tick()
                self.folder.fail(NODE, None)
                self.assert_api_error(409, "camera_absent", self.run_for, motion.REPEAT_S, TINY2_ID, body)
                self.assertEqual(len(self.folder.write_log(NODE)), count)
                # Ohne Wiederholung (Taste losgelassen) ist der Abbruch nach 500 ms vergessen.
                self.run_for(1.0)
                self.hold(body, 0.3)
                self.assertGreater(len(self.folder.write_log(NODE)), count)
                count = len(self.folder.write_log(NODE))

    # --- Schritt, Mitte, Richtung ------------------------------------------------------

    def test_step(self) -> None:
        result = self.mover.step(TINY2_ID, {"pan": 1})
        self.assertEqual(result["position"], {"pan": 2.0, "tilt": 0.0, "zoom": 0})
        self.mover.step(TINY2_ID, {"pan": -1, "tilt": -1, "zoom": 1})
        self.assertEqual(self.folder.write_log(NODE), [(PAN, 2 * DEG), (PAN, 0), (TILT, -2 * DEG), (ZOOM, 10)])
        self.store.update_motion(TINY2_ID, {"step_deg": 5, "zoom_step_percent": 25})
        result = self.mover.step(TINY2_ID, {"tilt": 1, "zoom": 1})
        self.assertEqual(result["position"], {"pan": 0.0, "tilt": 3.0, "zoom": 35})
        # Schritte über die Grenze bleiben an der Grenze.
        for _ in range(20):
            result = self.mover.step(TINY2_ID, {"tilt": -1})
        self.assertEqual(result["position"]["tilt"], -66.0)
        self.assertEqual(min(self.writes(TILT)), -66 * DEG)
        self.assertEqual(self.tiny2()["run"], obsbot.RUN_AWAKE)

    def test_home(self) -> None:
        self.hold({"pan": 1, "tilt": -1, "zoom": 1, "speed": 100}, 1.0)
        zoom = self.folder.values(NODE)[ZOOM]
        self.assertGreater(zoom, 0)
        result = self.mover.home(TINY2_ID, {})
        self.assertEqual(result["position"], {"pan": 0.0, "tilt": 0.0, "zoom": zoom})
        # home hält eine laufende Bewegung an.
        self.run_for(1.0)
        self.assertEqual((self.folder.values(NODE)[PAN], self.folder.values(NODE)[TILT]), (0, 0))
        # Eigene Mitte, auch sie wird begrenzt.
        self.store.update_motion(TINY2_ID, {"home_pan": 12, "home_tilt": -80})
        result = self.mover.home(TINY2_ID, {})
        self.assertEqual(result["position"], {"pan": 12.0, "tilt": -66.0, "zoom": zoom})
        # Von aussen (Presets, Abschnitt b3): goto begrenzt ebenfalls.
        result = self.mover.goto(TINY2_ID, 500.0, -100.0, 1000, False)
        self.assertEqual(result["position"], {"pan": 130.0, "tilt": -66.0, "zoom": 100})
        self.assertEqual((self.tiny2()["run"], self.tiny2()["restarts"]), (obsbot.RUN_AWAKE, 0))

    def test_invert_direction(self) -> None:
        self.hold({"pan": 1, "tilt": 1, "speed": 100}, 0.5)
        self.mover.stop(TINY2_ID)
        values = self.folder.values(NODE)
        self.assertGreater(values[PAN], 0)
        self.assertGreater(values[TILT], 0)
        self.mover.home(TINY2_ID, {})
        self.store.update_motion(TINY2_ID, {"invert_pan": True, "invert_tilt": True, "invert_zoom": True})
        self.hold({"pan": 1, "tilt": 1, "speed": 100}, 0.5)
        self.mover.stop(TINY2_ID)
        values = self.folder.values(NODE)
        self.assertLess(values[PAN], 0)
        self.assertLess(values[TILT], 0)
        result = self.mover.step(TINY2_ID, {"pan": 1, "zoom": -1})
        self.assertEqual(result["position"]["pan"], values[PAN] / DEG - 2)
        self.assertEqual(result["position"]["zoom"], 10)

    def test_invalid_requests(self) -> None:
        for body in ({"pan": 2}, {"pan": "links"}, {"pan": True}, {"tilt": 1, "speed": 0},
                     {"tilt": 1, "speed": 101}, {"tilt": 1, "wake": 1}, {"roll": 1}):
            with self.subTest(body=body):
                self.assert_api_error(400, "invalid", self.mover.move, TINY2_ID, body)
        for body in ({"pan": 0.5}, {}, {"pan": 1, "speed": 50}):
            with self.subTest(body=body):
                self.assert_api_error(400, "invalid", self.mover.step, TINY2_ID, body)
        self.assert_api_error(400, "invalid", self.mover.home, TINY2_ID, {"pan": 1})
        self.assert_api_error(404, "not_found", self.mover.move, "gibt-es-nicht", {"pan": 1})
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        self.assert_api_error(409, "camera_absent", self.mover.move, TINY2_ID, {"pan": 1})
        self.assert_api_error(409, "camera_absent", self.mover.home, TINY2_ID, {})
        self.assertEqual(self.folder.write_log(NODE), [])
        # Ungültige Einstellungen werden nicht gespeichert.
        self.assertRaises(ValueError, self.store.update_motion, TINY2_ID, {"tilt_min": "unten"})
        self.assertRaises(ValueError, self.store.update_motion, TINY2_ID, {"gibt_es_nicht": 1})


class ServiceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_bewegen-dienst-")
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

    def post(self, action: str, body=None) -> tuple[int, dict]:
        return send(self.port, f"{self.base}/{action}", "POST", body)

    def test_move_stop_step_home(self) -> None:
        self.service.start()
        # Tilt nach unten mit Tempo 100 (60°/s) während 1,6 s: hält an der Grenze −66°.
        # Die erste Anfrage wartet die 3 s nach dem Erscheinen ab.
        status, body = self.post("move", {"tilt": -1, "speed": 100})
        self.assertEqual(status, 200, body)
        end = time.monotonic() + 1.6
        while time.monotonic() < end:
            status, body = self.post("move", {"tilt": -1, "speed": 100})
            self.assertEqual((status, body), (200, {"ok": True, "moving": True}))
            time.sleep(motion.REPEAT_S)
        status, body = self.post("stop")
        self.assertEqual((status, body), (200, {"ok": True, "moving": False}))
        tilts = [entry[1] for entry in self.folder.write_log(NODE) if entry[0] == TILT]
        self.assertEqual((min(tilts), tilts[-1]), (-66 * DEG, -66 * DEG))

        # Totmann: eine einzelne Anfrage bewegt höchstens ~0,5 s.
        status, body = self.post("move", {"pan": 1, "speed": 50})
        self.assertEqual(status, 200, body)
        time.sleep(1.0)
        count = len(self.folder.write_log(NODE))
        pan = self.folder.values(NODE)[PAN]
        self.assertLessEqual(pan, 30 * (motion.DEADMAN_S + 0.25) * DEG)
        time.sleep(0.5)
        self.assertEqual(len(self.folder.write_log(NODE)), count)

        status, body = self.post("step", {"pan": 1})
        self.assertEqual(status, 200, body)
        self.assertEqual(body["position"]["pan"], pan / DEG + 2)
        status, body = self.post("home")
        self.assertEqual((status, body["position"]["pan"], body["position"]["tilt"]), (200, 0.0, 0.0))
        status, body = self.post("move", {"pan": 3})
        self.assertEqual((status, body["code"]), (400, "invalid"))
        self.assertEqual(self.service.stop(), 0, self.service.stderr)
        state = self.folder.tiny2(NODE)
        self.assertEqual((state["run"], state["restarts"]), (obsbot.RUN_AWAKE, 0))


if __name__ == "__main__":
    unittest.main(verbosity=2)
