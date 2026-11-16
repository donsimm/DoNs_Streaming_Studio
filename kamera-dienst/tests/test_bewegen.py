"""Test kd_bewegen: Bewegen mit Schutzregeln gegen die Attrappe (P15 b2, b4).

- Im Prozess mit ersetzbarer Uhr (`tick` von Hand alle 50 ms, Wiederholung alle 200 ms):
  Tiny 2 fährt über das Tempo (keine Zielpositionen, Lage nach dem Halt = Tempo × Zeit und
  als Treiberwert gesetzt, `save-current` speichert sie), Grenzen bei Dauerbewegung (Tiny 2
  nie unter −66°, schläft nie ein, keine «Neustarts»; eigene Grenzen, Untergrenze −75°;
  C922 ±10° mit Zielpositionen wie bisher), Totmann nach 500 ms mit Tempo 0, stop, Tempo 0
  beim Erscheinen, Wecken vor der Bewegung mit Wartezeit, KI-Befehl nicht gesendet
  (gesperrt), Abbruch beim Ausstecken und bei EPROTO ohne Nachsenden, step, home, Richtung
  umkehren, ungültige Anfragen.
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
                             FakeDevice, FakeDeviceFolder, fake_watcher, open_fake_device)
from dss_camera.presets import CameraPresets, PresetStore  # noqa: E402
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
PAN_SPEED = v4l2.V4L2_CID_PAN_SPEED
TILT_SPEED = v4l2.V4L2_CID_TILT_SPEED
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
        self.watcher.scan()
        # Beim Erscheinen nur Tempo 0 an die Tiny 2 (test_halt_on_appearance); danach zählt
        # das Protokoll ab hier.
        self.appear_log = self.folder.write_log(NODE)
        self.mover = Mover(self.manager, self.store, self.time)

    def tearDown(self) -> None:
        obsbot.AI_OFF_UNLOCKED = False
        self.tmp.cleanup()

    def tiny2(self) -> dict:
        return self.folder.tiny2(NODE)

    def log(self, node: str = NODE) -> list[tuple]:
        """Geschriebene Werte seit setUp."""
        entries = self.folder.write_log(node)
        return entries[len(self.appear_log):] if node == NODE else entries

    def writes(self, control: int, node: str = NODE) -> list[int]:
        return [entry[1] for entry in self.log(node) if entry[0] == control]

    def real(self) -> dict[str, float]:
        """Echte Lage der Tiny-2-Attrappe in Grad (am Gerät nicht lesbar)."""
        return FakeDevice(self.folder.dev / NODE, self.time).position()

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

    # --- Tiny 2 über das Tempo ---------------------------------------------------------

    def test_speed_curve(self) -> None:
        # Nachmessung 2026-11-16: auf einem Messpunkt genau, dazwischen linear, über dem letzten
        # Punkt mit der Steigung des letzten Abschnitts, negativ mit gleichem Betrag.
        pan, tilt = obsbot.PAN_SPEED_CURVE, obsbot.TILT_SPEED_CURVE
        self.assertAlmostEqual(obsbot.speed_deg_per_s(pan, 10), 10.87)
        self.assertAlmostEqual(obsbot.speed_deg_per_s(tilt, 5), 4.58)
        self.assertAlmostEqual(obsbot.speed_deg_per_s(pan, 15), (10.87 + 22.78) / 2)
        self.assertAlmostEqual(obsbot.speed_deg_per_s(tilt, 15), (9.96 + 20.5) / 2)
        self.assertAlmostEqual(obsbot.speed_deg_per_s(pan, 100), 84.8 + (84.8 - 42.6) / 2)
        self.assertAlmostEqual(obsbot.speed_deg_per_s(tilt, 30), 20.5 + (20.5 - 9.96))
        self.assertAlmostEqual(obsbot.speed_deg_per_s(pan, -20), -22.78)
        self.assertAlmostEqual(obsbot.speed_deg_per_s(tilt, -7), -(4.58 + (9.96 - 4.58) * 2 / 5))
        self.assertEqual(obsbot.speed_deg_per_s(pan, 0), 0.0)

    def test_tiny2_moves_by_speed(self) -> None:
        # Standardtempo 10 °/s; Tilt halb so schnell. Während der Fahrt keine Zielpositionen.
        self.hold({"pan": 1, "tilt": 0.5}, 2.0)
        self.assertEqual(self.log(), [(PAN_SPEED, 10), (TILT_SPEED, 5)])
        self.assertNotIn(PAN, self.folder.values(NODE))  # G_CTRL: nur der letzte gesetzte Wert
        self.mover.stop(TINY2_ID)
        # Halt: Tempo 0, dann Treiberwert absolut auf die geschätzte Lage (Grad gerundet).
        # Lage = Tempo × Zeit ab dem ersten Schritt (50 ms) nach Messkurve (Pan 10, Tilt 5).
        pan_rate = obsbot.speed_deg_per_s(obsbot.PAN_SPEED_CURVE, 10)
        tilt_rate = obsbot.speed_deg_per_s(obsbot.TILT_SPEED_CURVE, 5)
        pan, tilt = round(pan_rate * 1.95), round(tilt_rate * 1.95)
        real = self.real()
        self.assertAlmostEqual(real["pan"], pan_rate * 1.95, delta=0.01)
        self.assertAlmostEqual(real["tilt"], tilt_rate * 1.95, delta=0.01)
        self.assertEqual(self.log()[2:], [(PAN_SPEED, 0), (PAN, pan * DEG), (TILT_SPEED, 0), (TILT, tilt * DEG)])
        self.time.now += 1.0
        self.assertEqual(self.real(), {"pan": float(pan), "tilt": float(tilt)})
        # Ein Schritt geht von der geschätzten Lage aus, save-current speichert sie.
        self.assertEqual(self.mover.step(TINY2_ID, {"pan": 1})["position"]["pan"], pan + 2.0)
        presets = CameraPresets(PresetStore(self.root / "daten"), self.mover, self.manager)
        preset_id = presets.create({"name": "Oben", "camera": TINY2_ID})["preset"]["id"]
        self.hold({"tilt": -1, "speed": 20}, 1.0)
        self.mover.stop(TINY2_ID)
        saved = presets.save_current(preset_id, {})["preset"]
        tilt_20 = obsbot.speed_deg_per_s(obsbot.TILT_SPEED_CURVE, 20)
        self.assertEqual((saved["pan"], saved["tilt"]), (pan + 2.0, round(tilt - tilt_20 * 0.95)))
        # Tempo über 100 gibt es nicht; die Richtung skaliert es (1 °/s mindestens).
        self.hold({"pan": -0.01, "speed": 40}, 0.2)
        self.assertEqual(self.writes(PAN_SPEED)[-1], -1)

    def test_tilt_limit_holds_while_moving_down(self) -> None:
        self.folder.update_tiny2(NODE, stream_open=True)
        self.hold({"tilt": -1, "speed": 100}, 10.0)
        # Vor der Grenze Tempo 0 und absolut genau auf −66°, danach nichts mehr.
        self.assertEqual(self.writes(TILT_SPEED), [-100, 0])
        self.assertEqual(self.writes(TILT), [-66 * DEG])
        self.assertEqual(self.log()[-2:], [(TILT_SPEED, 0), (TILT, -66 * DEG)])
        self.assertEqual(self.real()["tilt"], -66.0)
        state = self.tiny2()
        self.assertEqual((state["run"], state["restarts"], state["moves_asleep"], state["lost_moves"]),
                         (obsbot.RUN_AWAKE, 0, 0, 0))
        self.assertNotIn("verworfen", [entry[-1] for entry in self.log()])
        # Weiter nach unten drücken bewegt nichts; nach oben fährt sie wieder.
        self.hold({"tilt": -1, "speed": 100}, 1.0)
        self.assertEqual(self.writes(TILT_SPEED), [-100, 0])
        self.hold({"tilt": 1, "speed": 10}, 0.5)
        self.assertEqual(self.writes(TILT_SPEED)[-1], 10)

    def test_tilt_limit_at_low_speed(self) -> None:
        # Langsam an die Grenze: der Halt kommt erst kurz davor.
        self.store.update_motion(TINY2_ID, {"tilt_min": -3})
        self.hold({"tilt": -1, "speed": 10}, 1.0)
        self.assertEqual(self.writes(TILT_SPEED), [-10, 0])
        self.assertEqual(self.writes(TILT), [-3 * DEG])
        self.assertEqual(self.real()["tilt"], -3.0)

    def test_own_limits_and_floor(self) -> None:
        self.store.update_motion(TINY2_ID, {"tilt_min": -90, "tilt_max": 20, "pan_min": -30, "pan_max": 45})
        self.hold({"tilt": -1, "pan": -1, "speed": 100}, 3.0)
        # Eigene Grenze unter −75° wird auf −75° angehoben (Einschlafen bei −76°).
        self.assertEqual(self.writes(TILT), [-75 * DEG])
        self.assertEqual(self.writes(PAN), [-30 * DEG])
        self.assertEqual(self.real(), {"pan": -30.0, "tilt": -75.0})
        self.hold({"tilt": 1, "pan": 1, "speed": 100}, 3.0)
        self.assertEqual(self.writes(TILT), [-75 * DEG, 20 * DEG])
        self.assertEqual(self.writes(PAN), [-30 * DEG, 45 * DEG])
        self.assertEqual(self.real(), {"pan": 45.0, "tilt": 20.0})
        self.assertEqual((self.tiny2()["run"], self.tiny2()["restarts"]), (obsbot.RUN_AWAKE, 0))

    def test_pan_limits_of_device(self) -> None:
        # Ohne eigene Grenzen gilt der Bereich des Geräts (±130°, Anschlag): Halt davor.
        self.hold({"pan": 1, "speed": 100}, 3.0)
        self.assertEqual(self.writes(PAN), [130 * DEG])
        self.assertEqual(self.real()["pan"], 130.0)
        self.hold({"pan": -1, "speed": 100}, 4.0)
        self.assertEqual(self.writes(PAN), [130 * DEG, -130 * DEG])
        self.assertEqual(self.writes(PAN_SPEED), [100, 0, -100, 0])

    def test_halt_on_appearance(self) -> None:
        # Beim Start (setUp) und bei jedem Erscheinen einmal Tempo 0, nur bei wacher Kamera.
        self.assertEqual(self.appear_log, [(PAN_SPEED, 0), (TILT_SPEED, 0)])
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        self.folder.plug(TINY2_BY_ID, NODE)
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP, stream_open=True)
        self.watcher.scan()
        self.time.now += 3.0
        self.watcher.scan()
        self.assertEqual(self.log(), [])
        self.assertEqual(self.tiny2()["restarts"], 0)
        self.manager.wake(TINY2_ID)
        self.time.now += 5.0
        self.watcher.scan()
        self.assertEqual(self.log(), [(PAN_SPEED, 0), (TILT_SPEED, 0)])
        self.watcher.scan()
        self.assertEqual(len(self.log()), 2)
        self.assertEqual((self.tiny2()["restarts"], self.tiny2()["moves_asleep"]), (0, 0))

    def test_c922_digital_limits(self) -> None:
        self.hold({"pan": 1, "tilt": -1, "zoom": 1, "speed": 100}, 3.0, C922_ID)
        values = self.folder.values(C922_NODE)
        self.assertEqual((values[PAN], values[TILT], values[ZOOM]), (10 * DEG, -10 * DEG, 500))
        self.assertTrue(all(abs(value) <= 10 * DEG for value in self.writes(PAN, C922_NODE)))
        # Zielpositionen je Schritt wie bisher (60 °/s, 3° je Schritt), kein Tempo.
        self.assertEqual(self.writes(PAN, C922_NODE)[:3], [3 * DEG, 6 * DEG, 9 * DEG])

    def test_axis_rounding(self) -> None:
        # C922 (Zielpositionen wie bisher), langsam (Tempo 1 = 0,6°/s): geschrieben wird nur,
        # wenn sich das 1°-Raster ändert.
        self.hold({"pan": 1, "speed": 1}, 4.0, C922_ID)
        self.assertEqual(self.writes(PAN, C922_NODE), [DEG, 2 * DEG])

    # --- Totmann und stop --------------------------------------------------------------

    def test_deadman_stops_after_500_ms(self) -> None:
        self.hold({"pan": 1, "speed": 50}, 1.0)
        self.assertEqual(self.writes(PAN_SPEED), [50])
        last_repeat = self.time.now
        stopped_at = None
        for _ in range(40):
            self.run_for(motion.TICK_S)
            if stopped_at is None and self.writes(PAN_SPEED)[-1] == 0:
                stopped_at = self.time.now - last_repeat
        self.assertLessEqual(stopped_at, motion.DEADMAN_S + motion.TICK_S + 1e-6)
        self.assertGreater(stopped_at, motion.DEADMAN_S - motion.TICK_S)
        # Halt mit Tempo 0, Treiberwert auf die Lage; danach steht die Kamera dort.
        self.assertEqual(self.log()[-2:][0], (PAN_SPEED, 0))
        self.assertEqual(self.log()[-1][0], PAN)
        self.assertEqual(self.real()["pan"], self.folder.values(NODE)[PAN] / DEG)
        # Danach beginnt eine neue Bewegung mit der nächsten Anfrage.
        self.hold({"pan": -1, "speed": 50}, 0.4)
        self.assertEqual(self.writes(PAN_SPEED)[-1], -50)

    def test_stop(self) -> None:
        self.hold({"pan": 1, "speed": 100}, 0.5)
        self.assertEqual(self.mover.stop(TINY2_ID), {"moving": False})
        self.assertEqual(self.log()[-2:], [(PAN_SPEED, 0), (PAN, round(self.real()["pan"]) * DEG)])
        count = len(self.log())
        self.run_for(1.0)
        self.assertEqual(len(self.log()), count)
        # move mit Richtung 0 hält ebenfalls an.
        self.hold({"pan": 1, "speed": 100}, 0.3)
        self.assertEqual(self.mover.move(TINY2_ID, {"pan": 0}), {"moving": False})
        self.assertEqual(self.writes(PAN_SPEED)[-1], 0)
        count = len(self.log())
        self.run_for(0.3)
        self.assertEqual(len(self.log()), count)
        # Eine Achse loslassen hält nur sie an, die andere fährt weiter.
        self.hold({"pan": 1, "tilt": 1, "speed": 10}, 0.3)
        self.hold({"tilt": 1, "speed": 10}, 0.3)
        self.assertEqual(self.writes(PAN_SPEED)[-1], 0)
        self.assertEqual(self.writes(TILT_SPEED), [10])
        self.mover.stop(TINY2_ID)
        # stop gelingt auch ohne Kamera.
        self.hold({"pan": 1, "speed": 100}, 0.3)
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        self.assertEqual(self.mover.stop(TINY2_ID), {"moving": False})

    # --- Wecken, KI ----------------------------------------------------------------------

    def test_wake_before_move(self) -> None:
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP, stream_open=True)
        self.assert_api_error(409, "camera_asleep", self.mover.move, TINY2_ID, {"tilt": 1})
        self.assert_api_error(409, "camera_asleep", self.mover.step, TINY2_ID, {"tilt": 1})
        self.run_for(0.5)
        self.assertEqual(self.log(), [])

        start = self.time.now
        self.hold({"tilt": 1, "speed": 100, "wake": True}, 0.5)
        state = self.tiny2()
        frames = [data for selector, data in state["xu_protokoll"] if selector == uvc_xu.SELECTOR_FRAME]
        self.assertEqual(len(frames), 1)
        # Zustand 1 nach 1,5 s, dann 4,5 s Wartezeit vor dem ersten Befehl (sonst lost_moves).
        self.assertGreaterEqual(self.time.now - start, TINY2_WAKE_S + obsbot.AFTER_WAKE_S)
        self.assertEqual(self.writes(TILT_SPEED), [100])
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
        count = len(self.log())
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        # Auch ohne neuen Befehl merkt der nächste Schritt, dass die Kamera fehlt.
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
        self.assertEqual(len(self.log()), count)
        # Erst nach stop gilt eine neue Bewegung; stop sendet nur Tempo 0 (Lage unsicher).
        self.mover.stop(TINY2_ID)
        self.assertEqual(self.log()[count:], [(PAN_SPEED, 0)])
        self.hold(body, 0.3)
        self.assertEqual(self.writes(PAN_SPEED)[-1], 100)

    def test_abort_on_eproto(self) -> None:
        body = {"tilt": -1, "speed": 20}
        self.hold(body, 0.3)
        count = len(self.log())
        for failure in ("EPROTO", "ETIMEDOUT"):
            with self.subTest(failure=failure):
                self.folder.fail(NODE, failure)
                self.time.now += motion.TICK_S
                self.mover.tick()
                self.folder.fail(NODE, None)
                self.assert_api_error(409, "camera_absent", self.run_for, motion.REPEAT_S, TINY2_ID, body)
                self.assertEqual(len(self.log()), count)
                # Ohne Wiederholung (Taste losgelassen) ist der Abbruch nach 500 ms vergessen;
                # dann geht Tempo 0 noch ans Gerät, weil der Halt beim Abbruch misslang.
                self.run_for(1.0)
                self.assertEqual(self.log()[count:], [(TILT_SPEED, 0)])
                self.hold(body, 0.3)
                self.assertEqual(self.writes(TILT_SPEED)[-1], -20)
                count = len(self.log())
        self.assertEqual(self.tiny2()["run"], obsbot.RUN_AWAKE)

    def test_abort_when_asleep(self) -> None:
        # Schläft die Kamera während der Fahrt ein, bricht der nächste Schritt ab (Zustand je
        # Schritt geprüft); im Schlaf geht auch Tempo 0 nicht ans Gerät (Plan 2.3).
        self.hold({"pan": 1, "speed": 10}, 0.5)
        self.folder.update_tiny2(NODE, run=obsbot.RUN_ASLEEP)
        self.time.now += motion.TICK_S
        self.mover.tick()
        self.assert_api_error(409, "camera_absent", self.mover.move, TINY2_ID, {"pan": 1})
        self.assertEqual(self.writes(PAN_SPEED), [10])
        self.mover.stop(TINY2_ID)
        self.assertEqual(self.writes(PAN_SPEED), [10])
        self.assertEqual(self.tiny2()["moves_asleep"], 0)

    # --- Schritt, Mitte, Richtung ------------------------------------------------------

    def test_step(self) -> None:
        result = self.mover.step(TINY2_ID, {"pan": 1})
        self.assertEqual(result["position"], {"pan": 2.0, "tilt": 0.0, "zoom": 0})
        self.mover.step(TINY2_ID, {"pan": -1, "tilt": -1, "zoom": 1})
        self.assertEqual(self.log(), [(PAN, 2 * DEG), (PAN, 0), (TILT, -2 * DEG), (ZOOM, 10)])
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
        self.assertEqual(self.log(), [])
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
        # Tilt nach unten mit Tempo 100 (≈ 98 °/s) während 1,6 s: hält an der Grenze −66°.
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
        # Treiberwert nach dem Halt = geschätzte Lage (Tempo 50 ≈ 53 °/s).
        deg_per_s = obsbot.speed_deg_per_s(obsbot.PAN_SPEED_CURVE, 50)
        self.assertGreater(pan, deg_per_s * (motion.DEADMAN_S - 0.1) * DEG)
        self.assertLessEqual(pan, deg_per_s * (motion.DEADMAN_S + 0.25) * DEG)
        self.assertEqual(self.folder.values(NODE)[v4l2.V4L2_CID_PAN_SPEED], 0)
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
