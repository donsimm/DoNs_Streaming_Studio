"""Test kd_v4l2: V4L2-Nummern und Kamera-Suche (P15 a2).

Aufruf: test_v4l2.py PFAD_ZU_kamera_ioctl_werte

- Nummern, Grössen und Offsets in v4l2.py gegen die Kernel-Header (Hilfsprogramm).
- Suche über by-id im Attrappen-Ordner: zwei Kameras, Meta-Knoten übersprungen,
  Kennung aus sysfs, Ein- und Ausstecken, Wartezeit nach dem Erscheinen (ersetzbare Uhr).
- Der Dienst als Prozess mit --geraeteordner: present/absent in GET status.
"""

import ctypes
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SERVICE_DIR))

from dss_camera import v4l2  # noqa: E402
from dss_camera.devices import SETTLE_S  # noqa: E402
from dss_camera.fake import (C922_BY_ID, C922_ID, TINY2_BY_ID, TINY2_ID,  # noqa: E402
                             FakeDeviceFolder, fake_watcher)
from test_api import Service, free_port, ini, request  # noqa: E402

HELPER: Path | None = None
# Suche jede Sekunde; grosszügig, weil andere Agenten parallel bauen.
STATUS_WAIT_S = 15


class FakeClock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


class IoctlValuesTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        output = subprocess.run([str(HELPER)], capture_output=True, text=True, check=True).stdout
        cls.values = {}
        for line in output.splitlines():
            name, value = line.split()
            cls.values[name] = int(value)

    def test_numbers_and_constants(self) -> None:
        names = [name for name in self.values if "." not in name]
        self.assertGreaterEqual(len(names), 20)
        for name in names:
            with self.subTest(name=name):
                self.assertEqual(getattr(v4l2, name), self.values[name], hex(self.values[name]))

    def test_step0_values(self) -> None:
        # Am Gerät in Schritt 0 verwendet (Plan, «Ergebnis Schritt 0», 2026-11-15).
        self.assertEqual(v4l2.VIDIOC_G_CTRL, 0xC008561B)
        self.assertEqual(v4l2.VIDIOC_S_CTRL, 0xC008561C)
        self.assertEqual(v4l2.UVCIOC_CTRL_QUERY, 0xC0107521)
        self.assertEqual(v4l2.V4L2_CID_PAN_ABSOLUTE, 0x9A0908)
        self.assertEqual(v4l2.V4L2_CID_TILT_ABSOLUTE, 0x9A0909)
        self.assertEqual(v4l2.V4L2_CID_ZOOM_ABSOLUTE, 0x9A090D)

    def test_structures(self) -> None:
        for name, (structure, fields) in v4l2.STRUCTURES.items():
            with self.subTest(structure=name):
                self.assertEqual(ctypes.sizeof(structure), self.values[f"sizeof.{name}"])
                for field_name in fields:
                    self.assertEqual(getattr(structure, field_name).offset,
                                     self.values[f"offsetof.{name}.{field_name}"], field_name)


class WatcherTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_v4l2-")
        self.root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(self.root)
        self.folder.add_standard_cameras()
        self.clock = FakeClock()
        self.watcher = fake_watcher(self.root, clock=self.clock)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def by_id(self) -> dict:
        return {camera.id: camera for camera in self.watcher.cameras()}

    def test_two_cameras_meta_skipped(self) -> None:
        self.watcher.scan()
        cameras = self.by_id()
        # Je Kamera ein Eintrag, die Meta-Knoten (index1) zählen nicht.
        self.assertEqual(set(cameras), {TINY2_ID, C922_ID})
        self.assertEqual(cameras[TINY2_ID].node.name, "video0")
        self.assertEqual(cameras[C922_ID].node.name, "video2")
        self.assertEqual(cameras[TINY2_ID].model, "OBSBOT Tiny 2")
        self.assertEqual(cameras[C922_ID].model, "C922 Pro Stream Webcam")
        for camera in cameras.values():
            self.assertTrue(camera.present)
            self.assertEqual(camera.state, "awake")
        self.assertTrue(cameras[TINY2_ID].ptz)
        self.assertFalse(cameras[C922_ID].ptz)
        # Werte und Bereiche aus der Abfrage, mit Menü.
        controls = {control.name: control for control in cameras[C922_ID].controls}
        self.assertEqual(controls["Power Line Frequency"].menu, {0: "Disabled", 1: "50 Hz", 2: "60 Hz"})
        self.assertEqual((controls["Brightness"].minimum, controls["Brightness"].maximum), (0, 255))

    def test_meta_node_on_index0(self) -> None:
        # Liegt der Meta-Knoten auf index0, gilt der Bildknoten auf index1.
        video = v4l2.V4L2_CAP_VIDEO_CAPTURE
        meta = v4l2.V4L2_CAP_META_CAPTURE
        self.folder.add_usb("2-1", "cccc", "0003", "Kamera drei", serial="SERIE0003")
        self.folder.add_node("video4", "2-1", "Kamera drei", meta)
        self.folder.add_node("video5", "2-1", "Kamera drei", video)
        self.folder.plug("usb-Hersteller_Kamera_drei_SERIE0003-video-index0", "video4")
        self.folder.plug("usb-Hersteller_Kamera_drei_SERIE0003-video-index1", "video5")
        self.watcher.scan()
        self.assertEqual(self.by_id()["cccc:0003:SERIE0003"].node.name, "video5")

    def test_unplug_and_plug(self) -> None:
        self.watcher.scan()
        self.folder.unplug(C922_BY_ID)
        self.watcher.scan()
        cameras = self.by_id()
        self.assertFalse(cameras[C922_ID].present)
        self.assertEqual(cameras[C922_ID].state, "absent")
        self.assertIsNone(self.watcher.settle_remaining(C922_ID))
        self.assertFalse(self.watcher.writable(C922_ID))
        self.assertTrue(cameras[TINY2_ID].present)
        # Wieder einstecken: gleiche Kennung, wieder da.
        self.clock.now += 10
        self.folder.plug(C922_BY_ID, "video2")
        self.watcher.scan()
        camera = self.by_id()[C922_ID]
        self.assertTrue(camera.present)
        self.assertEqual(camera.state, "awake")
        self.assertEqual(camera.appeared_at, self.clock.now)

    def test_other_port_same_id(self) -> None:
        # Umstecken an einen anderen Anschluss: Kennung bleibt (kein Anschluss in der Kennung).
        self.watcher.scan()
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        self.folder.add_usb("3-4", "aaaa", "0001", "OBSBOT Tiny 2")
        self.folder.add_node("video6", "3-4", "OBSBOT Tiny 2: OBSBOT Tiny 2", v4l2.V4L2_CAP_VIDEO_CAPTURE)
        self.folder.plug(TINY2_BY_ID, "video6")
        self.watcher.scan()
        cameras = self.by_id()
        self.assertEqual(set(cameras), {TINY2_ID, C922_ID})
        self.assertTrue(cameras[TINY2_ID].present)
        self.assertEqual(cameras[TINY2_ID].node.name, "video6")

    def test_settle_time(self) -> None:
        self.watcher.scan()
        start = self.clock.now
        self.assertEqual(self.watcher.settle_remaining(TINY2_ID), SETTLE_S)
        self.assertEqual(SETTLE_S, 3.0)
        self.clock.now = start + 2.9
        self.watcher.scan()  # weitere Prüfungen setzen die Wartezeit nicht zurück
        self.assertFalse(self.watcher.writable(TINY2_ID))
        self.assertAlmostEqual(self.watcher.settle_remaining(TINY2_ID), 0.1)
        self.clock.now = start + 3.0
        self.assertTrue(self.watcher.writable(TINY2_ID))
        # Nach dem Wiedereinstecken beginnt die Wartezeit neu.
        self.folder.unplug(TINY2_BY_ID)
        self.watcher.scan()
        self.folder.plug(TINY2_BY_ID, "video0")
        self.clock.now = start + 20
        self.watcher.scan()
        self.assertFalse(self.watcher.writable(TINY2_ID))
        self.clock.now = start + 23
        self.assertTrue(self.watcher.writable(TINY2_ID))

    def test_probe_failure_unknown(self) -> None:
        self.folder.add_node("video0", "1-2", "OBSBOT Tiny 2", v4l2.V4L2_CAP_VIDEO_CAPTURE,
                             failure="EPROTO")
        self.watcher.scan()
        camera = self.by_id()[TINY2_ID]
        self.assertTrue(camera.present)
        self.assertEqual(camera.state, "unknown")
        # Die nächste Prüfung fragt erneut ab.
        self.folder.add_node("video0", "1-2", "OBSBOT Tiny 2", v4l2.V4L2_CAP_VIDEO_CAPTURE)
        self.watcher.scan()
        self.assertEqual(self.by_id()[TINY2_ID].state, "awake")

    def test_two_identical_without_serial(self) -> None:
        self.folder.add_usb("3-1", "aaaa", "0001", "OBSBOT Tiny 2")
        self.folder.add_node("video7", "3-1", "OBSBOT Tiny 2", v4l2.V4L2_CAP_VIDEO_CAPTURE)
        # udev hängt bei gleichem Namen keinen zweiten Verweis an; hier ein erfundener Name.
        self.folder.plug("usb-Remo_Tech_Co.__Ltd._OBSBOT_Tiny_2_b-video-index0", "video7")
        self.watcher.scan()
        self.assertEqual(set(self.by_id()), {TINY2_ID, C922_ID, f"{TINY2_ID}@3-1"})

    def test_missing_folder(self) -> None:
        watcher = fake_watcher(self.root / "gibt-es-nicht", clock=self.clock)
        watcher.scan()
        self.assertEqual(watcher.cameras(), [])


class FolderService(Service):
    def __init__(self, folder: Path, ini_text: str, devices: Path) -> None:
        super().__init__(folder, ini_text)
        self.devices = devices

    def command(self) -> list[str]:
        return super().command() + ["--geraeteordner", str(self.devices)]


class ServiceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_v4l2-dienst-")
        root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(root / "geraete")
        self.folder.add_standard_cameras()
        (root / "dienst").mkdir()
        self.port = free_port()
        self.service = FolderService(root / "dienst", ini(self.port), self.folder.root)

    def tearDown(self) -> None:
        self.tmp.cleanup()

    def cameras(self) -> dict:
        status, body = request(self.port, "/api/v1/status")
        self.assertEqual(status, 200)
        return {camera["id"]: camera for camera in body["cameras"]}

    def wait_for(self, camera_id: str, present: bool) -> dict:
        deadline = time.monotonic() + STATUS_WAIT_S
        while True:
            camera = self.cameras()[camera_id]
            if camera["present"] is present or time.monotonic() > deadline:
                return camera
            time.sleep(0.2)

    def test_plug_in_status(self) -> None:
        self.service.start()
        try:
            # Erste Suche läuft vor «bereit».
            cameras = self.cameras()
            self.assertEqual(set(cameras), {TINY2_ID, C922_ID})
            for camera in cameras.values():
                self.assertEqual(set(camera), {"id", "name", "model", "present", "state", "ptz"})
                self.assertIs(camera["present"], True)
                self.assertEqual(camera["state"], "awake")
            self.assertIs(cameras[TINY2_ID]["ptz"], True)

            self.folder.unplug(C922_BY_ID)
            camera = self.wait_for(C922_ID, False)
            self.assertIs(camera["present"], False)
            self.assertEqual(camera["state"], "absent")

            self.folder.plug(C922_BY_ID, "video2")
            camera = self.wait_for(C922_ID, True)
            self.assertIs(camera["present"], True)
            self.assertEqual(camera["state"], "awake")
        finally:
            self.assertEqual(self.service.stop(), 0, self.service.stderr)
        self.assertIn("Kamera fehlt: " + C922_ID, self.service.stderr)
        self.assertIn("Kamera erschienen: " + C922_ID, self.service.stderr)

    def test_folder_needs_fake(self) -> None:
        self.service.fake = False
        result = self.service.run_to_end()
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("--geraeteordner geht nur mit --attrappe", result.stderr)


if __name__ == "__main__":
    HELPER = Path(sys.argv.pop(1))
    unittest.main(verbosity=2)
