"""Test kd_api: Kamera-Dienst als eigener Prozess auf 127.0.0.1 (P15 a1).

Prüft GET status, Fehlerformat, Absenderprüfung (403), Sperre gegen eine zweite
Instanz und die Meldung bei ungültiger dienst.ini. Jeder Dienst bekommt einen
eigenen Testordner (dienst.ini, XDG_RUNTIME_DIR) und einen freien Port.
"""

import json
import os
import random
import selectors
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
# Keine Zeitgrenze unter 5 s: Andere Agenten bauen parallel auf demselben Rechner.
START_TIMEOUT_S = 20
REQUEST_TIMEOUT_S = 10


def free_port() -> int:
    # Im Bereich, den dienst.ini erlaubt (1024 bis 32767).
    for _ in range(200):
        port = random.randint(20000, 32000)
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            try:
                probe.bind(("127.0.0.1", port))
            except OSError:
                continue
            return port
    raise RuntimeError("kein freier Port gefunden")


class Service:
    """Ein Dienst-Prozess mit eigenem Testordner."""

    def __init__(self, folder: Path, ini_text: str, fake: bool = True) -> None:
        self.folder = folder
        self.ini = folder / "dienst.ini"
        self.ini.write_text(ini_text, encoding="utf-8")
        self.runtime = folder / "laufzeit"
        self.runtime.mkdir(exist_ok=True)
        self.fake = fake
        self.stderr = ""
        self.process: subprocess.Popen | None = None

    def env(self) -> dict:
        env = dict(os.environ)
        env["XDG_RUNTIME_DIR"] = str(self.runtime)
        env["XDG_CONFIG_HOME"] = str(self.folder / "config")
        env["XDG_DATA_HOME"] = str(self.folder / "daten")
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        return env

    def command(self) -> list[str]:
        command = [sys.executable, "-m", "dss_camera", "--konfig", str(self.ini)]
        if self.fake:
            command.append("--attrappe")
        return command

    def run_to_end(self) -> subprocess.CompletedProcess:
        """Für Starts, die abbrechen sollen."""
        return subprocess.run(self.command(), cwd=SERVICE_DIR, env=self.env(), capture_output=True,
                              text=True, timeout=START_TIMEOUT_S)

    def start(self) -> None:
        self.process = subprocess.Popen(self.command(), cwd=SERVICE_DIR, env=self.env(),
                                        stderr=subprocess.PIPE, stdout=subprocess.DEVNULL, text=True)
        selector = selectors.DefaultSelector()
        selector.register(self.process.stderr, selectors.EVENT_READ)
        deadline = time.monotonic() + START_TIMEOUT_S
        try:
            while time.monotonic() < deadline:
                if not selector.select(timeout=deadline - time.monotonic()):
                    break
                line = self.process.stderr.readline()
                if not line:
                    break
                self.stderr += line
                if "bereit" in line:
                    return
        finally:
            selector.close()
        self.stop()
        raise AssertionError(f"Dienst nicht bereit, Ausgabe:\n{self.stderr}")

    def stop(self) -> int:
        if self.process is None:
            return 0
        if self.process.poll() is None:
            self.process.terminate()
        _, rest = self.process.communicate(timeout=START_TIMEOUT_S)
        self.stderr += rest or ""
        code = self.process.returncode
        self.process = None
        return code


def ini(port: int, allowed: str = "127.0.0.1") -> str:
    return f"[dienst]\nport={port}\nlauschadressen=127.0.0.1\nerlaubte-adressen={allowed}\n"


def request(port: int, path: str, method: str = "GET") -> tuple[int, dict]:
    req = urllib.request.Request(f"http://127.0.0.1:{port}{path}", method=method)
    try:
        with urllib.request.urlopen(req, timeout=REQUEST_TIMEOUT_S) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read().decode("utf-8"))


class ApiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.tmp = tempfile.TemporaryDirectory(prefix="kd_api-")
        cls.port = free_port()
        folder = Path(cls.tmp.name) / "haupt"
        folder.mkdir()
        cls.service = Service(folder, ini(cls.port))
        cls.service.start()

    @classmethod
    def tearDownClass(cls) -> None:
        code = cls.service.stop()
        cls.tmp.cleanup()
        assert code == 0, f"Dienst nicht sauber beendet ({code}):\n{cls.service.stderr}"

    def assert_error(self, body: dict, code: str) -> None:
        self.assertEqual(body.get("ok"), False)
        self.assertEqual(body.get("code"), code)
        self.assertIsInstance(body.get("message"), str)
        self.assertTrue(body["message"])

    def test_status(self) -> None:
        status, body = request(self.port, "/api/v1/status")
        self.assertEqual(status, 200)
        self.assertIs(body["ok"], True)
        self.assertIsInstance(body["service_version"], str)
        self.assertEqual(body["api_version"], 1)
        self.assertIs(body["fake"], True)
        cameras = {camera["model"]: camera for camera in body["cameras"]}
        self.assertEqual(set(cameras), {"OBSBOT Tiny 2", "Logitech C922 Pro Stream"})
        for camera in body["cameras"]:
            self.assertEqual(set(camera), {"id", "name", "model", "present", "state", "ptz"})
            self.assertIn(camera["state"], {"awake", "asleep", "privacy", "absent", "unknown"})
        self.assertIs(cameras["OBSBOT Tiny 2"]["ptz"], True)
        self.assertIs(cameras["Logitech C922 Pro Stream"]["ptz"], False)
        self.assertEqual(len(body["lights"]), 2)
        for light in body["lights"]:
            self.assertEqual(set(light), {"id", "name", "reachable", "on", "brightness", "temperature"})
        for key in ("camera_presets", "light_presets"):
            self.assertTrue(body[key])
            for preset in body[key]:
                self.assertEqual(set(preset), {"id", "name"})

    def test_unknown_path(self) -> None:
        for path, method in (("/api/v1/gibt-es-nicht", "GET"), ("/", "GET"),
                             ("/api/v2/status", "GET"), ("/api/v1/status", "DELETE")):
            with self.subTest(path=path, method=method):
                status, body = request(self.port, path, method)
                self.assertEqual(status, 404)
                self.assert_error(body, "not_found")

    def test_sender_not_allowed(self) -> None:
        folder = Path(self.tmp.name) / "fremd"
        folder.mkdir()
        port = free_port()
        # Erlaubt ist nur eine Adresse aus dem Dokumentationsnetz (RFC 5737), nicht 127.0.0.1.
        other = Service(folder, ini(port, allowed="192.0.2.20"))
        other.start()
        try:
            for method in ("GET", "PUT"):
                with self.subTest(method=method):
                    status, body = request(port, "/api/v1/status", method)
                    self.assertEqual(status, 403)
                    self.assert_error(body, "forbidden")
        finally:
            self.assertEqual(other.stop(), 0)
        self.assertIn("abgewiesen", other.stderr)
        self.assertIn("127.0.0.1", other.stderr)

    def test_second_instance(self) -> None:
        # Gleiches XDG_RUNTIME_DIR (gleiche Sperre), anderer Port: Die Sperre muss greifen.
        folder = Path(self.tmp.name) / "zweite"
        folder.mkdir()
        second = Service(folder, ini(free_port()))
        second.runtime = self.service.runtime
        result = second.run_to_end()
        self.assertEqual(result.returncode, 3, result.stderr)
        self.assertIn("läuft schon", result.stderr)
        # Der erste Dienst antwortet weiter.
        status, _ = request(self.port, "/api/v1/status")
        self.assertEqual(status, 200)

    def test_invalid_ini(self) -> None:
        cases = {
            "port-text": ("[dienst]\nport=abc\n", "Port «abc» ungültig"),
            "port-klein": ("[dienst]\nport=80\n", "Port «80» ungültig"),
            "adresse": ("[dienst]\nerlaubte-adressen=127.0.0.1,keine\n", "«keine» in «erlaubte-adressen»"),
            "alle-karten": ("[dienst]\nlauschadressen=0.0.0.0\n", "«0.0.0.0» in «lauschadressen»"),
            "leer": ("[dienst]\nerlaubte-adressen=\n", "«erlaubte-adressen» braucht"),
            "schluessel": ("[dienst]\nfarbe=rot\n", "Unbekannter Wert «farbe»"),
            "abschnitt": ("[andere]\nport=8093\n", "Unbekannter Abschnitt «[andere]»"),
            "kaputt": ("port=8093\n", "Datei nicht lesbar"),
        }
        for name, (text, expected) in cases.items():
            with self.subTest(case=name):
                folder = Path(self.tmp.name) / f"ini-{name}"
                folder.mkdir()
                result = Service(folder, text).run_to_end()
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn("dienst.ini ungültig", result.stderr)
                self.assertIn(expected, result.stderr)
                # Die Datei bleibt unverändert.
                self.assertEqual((folder / "dienst.ini").read_text(encoding="utf-8"), text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
