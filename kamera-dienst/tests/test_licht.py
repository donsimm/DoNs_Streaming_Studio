"""Test kd_licht: Lampen und Licht-Presets gegen Attrappen-Lampen auf 127.0.0.1 (P15 c1).

- Im Prozess mit ersetzbarer Uhr und kurzer Zeitgrenze: Ein/Aus (auch «toggle», einzeln und
  für alle), Helligkeit, Farbtemperatur, Begrenzung auf 3–100 und 143–344, stiller Fehler
  ausserhalb des Bereichs wird durch Zurücklesen erkannt, eine Lampe antwortet nicht (die
  andere wird trotzdem gesetzt, Meldung), `400` bei falschem `Content-Length` (die Attrappe
  erkennt ihn, der Dienst schickt ihn nie), Nachführen alle 5 s, Licht-Presets (anlegen aus
  dem Stand, abrufen, `null` = nicht ändern, Kennungen), Lampen verwalten, ungültige Dateien.
- Über HTTP gegen den Dienst mit Attrappen-Ordner: status, lights, lights/all, light-presets.
"""

import http.client
import json
import sys
import tempfile
import time
import unittest
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SERVICE_DIR))

from dss_camera.fake import FakeDeviceFolder, FakeLight  # noqa: E402
from dss_camera.http_api import ApiError  # noqa: E402
from dss_camera.lights import (ElgatoClient, LightPresets, LightPresetStore, Lights,  # noqa: E402
                               LightStore)
from test_api import free_port, ini  # noqa: E402
from test_obsbot import FakeTime  # noqa: E402
from test_werte import FolderService, send  # noqa: E402

# Kurze Zeitgrenze im Prozess; die Attrappe hängt deutlich länger.
TIMEOUT_S = 0.3
HANG_S = 1.0


class LightTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_licht-")
        self.data_dir = Path(self.tmp.name) / "daten"
        self.left = FakeLight(on=False, brightness=20, temperature=200)
        self.right = FakeLight(on=True, brightness=50, temperature=300)
        self.time = FakeTime()
        self.lights = self.make_lights()
        self.lights.create({"name": "Links", "host": self.left.host})
        self.lights.create({"name": "Rechts", "host": self.right.host})
        self.presets = self.make_presets()

    def tearDown(self) -> None:
        self.left.close()
        self.right.close()
        self.tmp.cleanup()

    def make_lights(self) -> Lights:
        return Lights(LightStore(self.data_dir), ElgatoClient(timeout_s=TIMEOUT_S), self.time)

    def make_presets(self) -> LightPresets:
        return LightPresets(LightPresetStore(self.data_dir), self.lights)

    def assert_api_error(self, status: int, code: str, func, *args) -> ApiError:
        with self.assertRaises(ApiError) as context:
            func(*args)
        self.assertEqual((context.exception.status, context.exception.code), (status, code),
                         context.exception.message)
        return context.exception

    def assert_clean(self) -> None:
        self.assertEqual((self.left.bad_requests, self.right.bad_requests), (0, 0))

    def status(self) -> dict:
        return {light["id"]: light for light in self.lights.status()}

    # --- Schalten ----------------------------------------------------------------------------

    def test_on_brightness_temperature(self) -> None:
        result = self.lights.set("links", {"on": True})
        self.assertEqual(result, {"lights": [{"id": "links", "name": "Links", "host": self.left.host,
                                              "reachable": True, "on": True, "brightness": 20,
                                              "temperature": 200}],
                                  "errors": []})
        self.assertEqual(self.left.state["on"], 1)
        self.assertEqual(self.left.puts, [{"on": 1}])
        self.lights.set("links", {"brightness": 70})
        self.lights.set("links", {"temperature": 250, "on": False})
        self.assertEqual(self.left.state, {"on": 0, "brightness": 70, "temperature": 250})
        self.assertEqual(self.right.puts, [])

        # «toggle» einzeln: frisch gelesen, dann umgeschaltet.
        self.left.state["on"] = 1  # z. B. von der Elgato-App geschaltet
        self.assertIs(self.lights.set("links", {"on": "toggle"})["lights"][0]["on"], False)
        # «toggle» für alle: rechts ist an → alle aus; danach alle an.
        result = self.lights.set("all", {"on": "toggle"})
        self.assertEqual([(light["id"], light["on"]) for light in result["lights"]],
                         [("links", False), ("rechts", False)])
        self.lights.set("all", {"on": "toggle", "brightness": 40})
        self.assertEqual((self.left.state["on"], self.right.state["on"]), (1, 1))
        self.assertEqual((self.left.state["brightness"], self.right.state["brightness"]), (40, 40))
        self.assert_clean()

    def test_clamp(self) -> None:
        for sent, expected in ((0, 3), (2, 3), (500, 100), (55, 55)):
            with self.subTest(brightness=sent):
                result = self.lights.set("links", {"brightness": sent})
                self.assertEqual(self.left.puts[-1], {"brightness": expected})
                self.assertEqual((result["lights"][0]["brightness"], result["errors"]), (expected, []))
        for sent, expected in ((100, 143), (1000, 344), (250, 250)):
            with self.subTest(temperature=sent):
                self.lights.set("all", {"temperature": sent})
                self.assertEqual(self.left.puts[-1], {"temperature": expected})
                self.assertEqual((self.left.state["temperature"], self.right.state["temperature"]),
                                 (expected, expected))
        count = len(self.left.puts)
        for body in ({}, {"brightness": "hell"}, {"brightness": 1.5}, {"temperature": True},
                     {"on": "ja"}, {"on": 1}, {"farbe": 3}):
            with self.subTest(body=body):
                self.assert_api_error(400, "invalid", self.lights.set, "links", body)
        self.assert_api_error(404, "not_found", self.lights.set, "mitte", {"on": True})
        self.assertEqual(len(self.left.puts), count)
        self.assert_clean()

    def test_silent_error_detected(self) -> None:
        # Annahme für den Test: eine Lampe mit kleinerem Bereich; sie nimmt 320 Mired mit 200 OK
        # an und lässt den Wert stehen (stiller Fehler).
        self.left.ranges["temperature"] = (143, 300)
        result = self.lights.set("links", {"temperature": 320, "brightness": 60})
        self.assertEqual(self.left.puts[-1], {"temperature": 320, "brightness": 60})
        self.assertEqual([(e["light"], e["code"]) for e in result["errors"]], [("links", "not_applied")])
        self.assertIn("temperature gesendet 320, gelesen 200", result["errors"][0]["message"])
        # Angezeigt wird der gelesene Stand, nicht der gesendete.
        self.assertEqual((result["lights"][0]["temperature"], result["lights"][0]["brightness"]), (200, 60))
        self.assertEqual(self.status()["links"]["temperature"], 200)
        self.assert_clean()

    def test_one_light_times_out(self) -> None:
        self.right.hang_s = HANG_S
        start = time.monotonic()
        result = self.lights.set("all", {"on": True, "brightness": 60})
        elapsed = time.monotonic() - start
        self.assertEqual(self.left.state, {"on": 1, "brightness": 60, "temperature": 200})
        self.assertEqual([(e["light"], e["code"]) for e in result["errors"]], [("rechts", "light_unreachable")])
        self.assertIn("Lampe «Rechts» antwortet nicht (Zeitüberschreitung)", result["errors"][0]["message"])
        # Zeitgrenze und ein neuer Versuch: höchstens 2 × 0,3 s für die hängende Lampe (mit Spielraum).
        self.assertLess(elapsed, HANG_S + 1.0)
        status = self.status()
        self.assertEqual((status["links"]["reachable"], status["rechts"]["reachable"]), (True, False))
        self.assertEqual(len(self.right.puts), 2)  # erster Versuch und Wiederholung

        # Eine einzelne Lampe ohne Antwort: 502; «toggle» liest zuerst und meldet ebenfalls.
        error = self.assert_api_error(502, "light_unreachable", self.lights.set, "rechts", {"on": False})
        self.assertIn("Rechts", error.message)
        result = self.lights.set("all", {"on": "toggle"})
        self.assertEqual([light["id"] for light in result["lights"]], ["links"])
        self.assertEqual((self.left.state["on"], result["errors"][0]["code"]), (0, "light_unreachable"))
        self.left.hang_s = HANG_S
        self.assert_api_error(502, "light_unreachable", self.lights.set, "all", {"on": True})
        self.assert_clean()

    def test_content_length(self) -> None:
        body = json.dumps({"numberOfLights": 1, "lights": [{"brightness": 33}]}).encode("utf-8")

        def raw(length: str | None) -> int:
            host, port = self.left.host.split(":")
            connection = http.client.HTTPConnection(host, int(port), timeout=5)
            try:
                connection.putrequest("PUT", "/elgato/lights")
                if length is not None:
                    connection.putheader("Content-Length", length)
                connection.endheaders()
                connection.send(body)
                return connection.getresponse().status
            finally:
                connection.close()

        # Die Attrappe weist wie die Lampe falsche Längen ab …
        for length in (str(len(body) - 3), str(len(body) + 5), None):
            with self.subTest(length=length):
                self.assertEqual(raw(length), 400)
        self.assertEqual(self.left.bad_requests, 3)
        self.assertEqual(self.left.state["brightness"], 20)
        self.assertEqual(raw(str(len(body))), 200)
        self.assertEqual(self.left.state["brightness"], 33)
        # … der Dienst schickt nie eine: Setzen, Presets und Abfragen ohne einen einzigen 400.
        self.left.bad_requests = 0
        self.lights.set("all", {"on": True, "brightness": 77, "temperature": 180})
        self.presets.create({"name": "Stream"})
        self.presets.recall("stream", {})
        self.lights.poll()
        self.assert_clean()
        self.assertEqual(self.left.state, {"on": 1, "brightness": 77, "temperature": 180})

    # --- Verbindung prüfen (P15 e4) ----------------------------------------------------------

    def test_check(self) -> None:
        # Gespeicherte Lampe: gelesen, nicht geschaltet, Stand im Dienst unverändert.
        self.assertEqual(self.lights.check({"host": self.left.host}),
                         {"reachable": True, "on": False, "brightness": 20, "temperature": 200})
        self.assertEqual((self.left.gets, self.left.puts), (1, []))
        self.assertIs(self.status()["links"]["reachable"], False)
        # Noch nicht gespeicherte Adresse (Leerzeichen am Rand zählen nicht).
        other = FakeLight(on=True, brightness=70, temperature=180)
        try:
            self.assertEqual(self.lights.check({"host": f" {other.host} "}),
                             {"reachable": True, "on": True, "brightness": 70, "temperature": 180})
            self.assertEqual(other.puts, [])
            # Lampe hängt: nicht erreichbar mit Grund, nach Zeitgrenze und einem neuen Versuch.
            other.hang_s = HANG_S
            start = time.monotonic()
            result = self.lights.check({"host": other.host})
            self.assertLess(time.monotonic() - start, HANG_S + 1.0)
            self.assertEqual(result, {"reachable": False, "message": f"Lampe unter «{other.host}» antwortet "
                                                                     "nicht (Zeitüberschreitung)."})
            other.hang_s = 0.0
        finally:
            other.close()
        # Niemand unter der Adresse (Port 1 auf 127.0.0.1, kein Netz).
        result = self.lights.check({"host": "127.0.0.1:1"})
        self.assertIs(result["reachable"], False)
        self.assertTrue(result["message"].startswith("Lampe unter «127.0.0.1:1» antwortet nicht ("))
        for body in ({}, {"host": ""}, {"host": "http://192.0.2.23/"}, {"host": 5},
                     {"host": "192.0.2.23", "name": "A"}):
            with self.subTest(body=body):
                self.assert_api_error(400, "invalid", self.lights.check, body)
        self.assertEqual([light["id"] for light in self.lights.status()], ["links", "rechts"])
        self.assert_clean()

    # --- Nachführen --------------------------------------------------------------------------

    def test_poll_every_5s(self) -> None:
        self.assertEqual(self.status()["links"], {"id": "links", "name": "Links", "host": self.left.host,
                                                 "reachable": False, "on": None, "brightness": None,
                                                 "temperature": None})
        self.assertTrue(self.lights.tick())
        self.assertEqual((self.left.gets, self.right.gets), (1, 1))
        self.assertEqual(self.status()["rechts"]["brightness"], 50)

        # Die Elgato-App schaltet links am Dienst vorbei.
        self.left.state.update({"on": 1, "brightness": 80})
        self.time.now += 4.9
        self.assertFalse(self.lights.tick())
        self.assertEqual((self.left.gets, self.status()["links"]["on"]), (1, False))
        self.time.now += 0.1
        self.assertTrue(self.lights.tick())
        self.assertEqual((self.status()["links"]["on"], self.status()["links"]["brightness"]), (True, 80))

        # Lampe antwortet nicht mehr: «reachable» false, letzter Stand bleibt sichtbar.
        self.right.hang_s = HANG_S
        self.time.now += 5.0
        self.assertTrue(self.lights.tick())
        self.assertEqual((self.status()["rechts"]["reachable"], self.status()["rechts"]["on"]), (False, True))
        self.right.hang_s = 0.0
        self.time.now += 5.0
        self.lights.tick()
        self.assertIs(self.status()["rechts"]["reachable"], True)

    # --- Licht-Presets -----------------------------------------------------------------------

    def test_presets(self) -> None:
        self.lights.set("links", {"on": True, "brightness": 45, "temperature": 210})
        result = self.presets.create({"name": "Stream"})
        preset = result["preset"]
        self.assertEqual(preset, {"id": "stream", "name": "Stream", "lights": {
            "links": {"on": True, "brightness": 45, "temperature": 210},
            "rechts": {"on": True, "brightness": 50, "temperature": 300}}})
        self.assertEqual(result["errors"], [])
        saved = json.loads((self.data_dir / "licht-presets.json").read_text(encoding="utf-8"))
        self.assertEqual(saved, {"format_version": 1, "presets": [preset]})

        self.lights.set("all", {"on": False, "brightness": 10, "temperature": 300})
        result = self.presets.recall("stream", {})
        self.assertEqual(result["errors"], [])
        self.assertEqual(self.left.state, {"on": 1, "brightness": 45, "temperature": 210})
        self.assertEqual(self.right.state, {"on": 1, "brightness": 50, "temperature": 300})

        # «Abend» von Hand: Werte ausserhalb werden beim Abruf begrenzt, `null` = nicht ändern,
        # eine entfernte Lampe wird gemeldet.
        puts = len(self.right.puts)
        self.presets.put("abend", {"name": "Abend", "lights": {
            "links": {"brightness": 500, "temperature": 100}, "rechts": None, "weg": {"on": False}}})
        result = self.presets.recall("abend", {})
        self.assertEqual(self.left.puts[-1], {"brightness": 100, "temperature": 143})
        self.assertEqual(len(self.right.puts), puts)
        self.assertEqual([(e["light"], e["code"]) for e in result["errors"]], [("weg", "not_found")])
        self.assertEqual([light["id"] for light in result["lights"]], ["links"])

        # Eine Lampe antwortet nicht: die andere wird gesetzt, beim Speichern bleibt sie `null`.
        self.right.hang_s = HANG_S
        self.lights.set("links", {"brightness": 5})
        result = self.presets.recall("stream", {})
        self.assertEqual(self.left.state["brightness"], 45)
        self.assertEqual([(e["light"], e["code"]) for e in result["errors"]], [("rechts", "light_unreachable")])
        result = self.presets.save_current("abend", {})
        self.assertEqual(result["preset"]["lights"], {"links": {"on": True, "brightness": 45, "temperature": 210},
                                                     "rechts": None})
        self.assertEqual(result["errors"][0]["code"], "light_unreachable")
        self.right.hang_s = 0.0

        # Kennungen, Umbenennen, Löschen wie bei den Kamera-Presets.
        self.assertEqual(self.presets.create({"name": "Stream!"})["preset"]["id"], "stream-2")
        renamed = self.presets.put("stream", {"name": "Live"})["preset"]
        self.assertEqual((renamed["id"], renamed["name"]), ("stream", "Live"))
        self.assertEqual(self.presets.store.summary(), [{"id": "stream", "name": "Live"},
                                                        {"id": "abend", "name": "Abend"},
                                                        {"id": "stream-2", "name": "Stream!"}])
        self.assertEqual(self.presets.delete("stream-2"), {})
        self.assert_api_error(404, "not_found", self.presets.recall, "stream-2", {})
        for preset_id, body in (("Gross", {"name": "A", "lights": {}}), ("neu", {"name": "A"}),
                                ("neu", {"name": "A", "lights": {"links": {"on": "toggle"}}}),
                                ("neu", {"name": "A", "lights": {"links": {}}}),
                                ("neu", {"name": "A", "lights": {"links": {"brightness": 1.5}}})):
            with self.subTest(preset_id=preset_id, body=body):
                self.assert_api_error(400, "invalid", self.presets.put, preset_id, body)
        self.assert_api_error(400, "invalid", self.presets.create, {"name": " "})
        self.assert_api_error(400, "invalid", self.presets.recall, "stream", {"wake": True})
        # Neu geladen aus der Datei: derselbe Stand.
        self.assertEqual(self.make_presets().get("stream"), self.presets.get("stream"))
        self.assert_clean()

    # --- Lampen verwalten, ungültige Dateien -------------------------------------------------

    def test_manage_lights(self) -> None:
        saved = json.loads((self.data_dir / "licht.json").read_text(encoding="utf-8"))
        self.assertEqual(saved, {"format_version": 1, "lights": [
            {"id": "links", "name": "Links", "host": self.left.host},
            {"id": "rechts", "name": "Rechts", "host": self.right.host}]})
        # «all» ist reserviert (PUT lights/all). Port 1 auf 127.0.0.1: keine Lampe, kein Netz.
        self.assertEqual(self.lights.create({"name": "All", "host": "127.0.0.1:1"})["light"]["id"], "all-2")
        self.assertEqual(self.lights.create({"name": "Links", "host": "127.0.0.1:1"})["light"]["id"],
                         "links-2")
        for body in ({"name": "A"}, {"name": "A", "host": "http://192.0.2.23/"},
                     {"name": "A", "host": "192.0.2.23:99999"}, {"name": "A", "host": "x", "id": "a"}):
            with self.subTest(body=body):
                self.assert_api_error(400, "invalid", self.lights.create, body)

        self.lights.tick()
        renamed = self.lights.config("links", {"name": "Key Light links"})["light"]
        self.assertEqual((renamed["id"], renamed["host"]), ("links", self.left.host))
        self.assertIs(self.status()["links"]["reachable"], True)
        # Neue Adresse: Stand vergessen bis zur nächsten Abfrage.
        self.lights.config("links", {"host": self.right.host})
        self.assertIs(self.status()["links"]["reachable"], False)
        self.assert_api_error(400, "invalid", self.lights.config, "links", {"host": "a b"})
        self.assert_api_error(404, "not_found", self.lights.config, "mitte", {"name": "Mitte"})
        self.assertEqual(self.lights.delete("all-2"), {})
        self.assert_api_error(404, "not_found", self.lights.delete, "all-2")
        self.assertEqual([light["id"] for light in self.lights.status()], ["links", "rechts", "links-2"])

    def test_invalid_files(self) -> None:
        for name, content in (("licht.json", b"{kaputt"),
                              ("licht.json", json.dumps({"format_version": 1, "lights": [
                                  {"id": "links", "name": "Links", "host": "192.0.2.21/x"}]}).encode()),
                              ("licht-presets.json", json.dumps({"format_version": 2, "presets": []}).encode()),
                              ("licht-presets.json", json.dumps({"format_version": 1, "presets": [
                                  {"id": "stream", "name": "Stream", "lights": {"links": {"on": 1}}}]}).encode())):
            with self.subTest(name=name, content=content):
                path = self.data_dir / name
                original = path.read_bytes() if path.exists() else None
                path.write_bytes(content)
                with self.assertLogs("dss_camera.presets", "ERROR"):
                    self.lights = self.make_lights()
                    self.presets = self.make_presets()
                if name == "licht.json":
                    self.assertEqual(self.lights.status(), [])
                    calls = ((self.lights.set, "all", {"on": True}), (self.lights.set, "links", {"on": True}),
                             (self.lights.create, {"name": "Neu", "host": "192.0.2.21"}),
                             (self.lights.delete, "links"), (self.presets.create, {"name": "Neu"}))
                else:
                    self.assertEqual(self.presets.store.summary(), [])
                    calls = ((self.presets.create, {"name": "Neu"}), (self.presets.recall, "stream", {}),
                             (self.presets.put, "stream", {"name": "Live"}))
                for func, *args in calls:
                    error = self.assert_api_error(409, "file_error", func, *args)
                    self.assertIn(name, error.message)
                self.assertEqual(path.read_bytes(), content)
                if original is None:
                    path.unlink()
                else:
                    path.write_bytes(original)
        self.assertEqual((self.left.puts, self.right.puts), ([], []))


class ServiceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_licht-dienst-")
        root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(root / "geraete")
        self.folder.add_standard_cameras()
        (root / "dienst").mkdir()
        self.left = FakeLight(on=False, brightness=20, temperature=200)
        self.right = FakeLight(on=True, brightness=50, temperature=300)
        data_dir = root / "dienst" / "daten" / "dss-kamera"
        data_dir.mkdir(parents=True)
        (data_dir / "licht.json").write_text(json.dumps({"format_version": 1, "lights": [
            {"id": "links", "name": "Links", "host": self.left.host},
            {"id": "rechts", "name": "Rechts", "host": self.right.host}]}), encoding="utf-8")
        self.port = free_port()
        self.service = FolderService(root / "dienst", ini(self.port), self.folder.root)

    def tearDown(self) -> None:
        if self.service.process is not None:
            self.service.stop()
        self.left.close()
        self.right.close()
        self.tmp.cleanup()

    def request(self, path: str, method: str = "GET", body=None) -> tuple[int, dict]:
        return send(self.port, f"/api/v1/{path}", method, body)

    def test_lights_over_http(self) -> None:
        self.service.start()
        # Die erste Abfrage läuft beim Start.
        deadline = time.monotonic() + 10
        while True:
            status, body = self.request("status")
            if all(light["reachable"] for light in body["lights"]) or time.monotonic() > deadline:
                break
            time.sleep(0.1)
        self.assertEqual(body["lights"], [
            {"id": "links", "name": "Links", "host": self.left.host, "reachable": True, "on": False,
             "brightness": 20, "temperature": 200},
            {"id": "rechts", "name": "Rechts", "host": self.right.host, "reachable": True, "on": True,
             "brightness": 50, "temperature": 300}])
        self.assertEqual(body["light_presets"], [])

        status, body = self.request("lights/all", "PUT", {"on": "toggle"})
        self.assertEqual((status, [light["on"] for light in body["lights"]], body["errors"]),
                         (200, [False, False], []))
        status, body = self.request("lights/links", "PUT", {"on": True, "brightness": 0, "temperature": 400})
        self.assertEqual((status, body["lights"][0]["brightness"], body["lights"][0]["temperature"]),
                         (200, 3, 344))
        status, body = self.request("lights/links", "PUT", {"on": "an"})
        self.assertEqual((status, body["code"]), (400, "invalid"))
        status, body = self.request("lights/mitte", "PUT", {"on": True})
        self.assertEqual((status, body["code"]), (404, "not_found"))

        status, body = self.request("light-presets", "POST", {"name": "Stream"})
        self.assertEqual((status, body["preset"]["id"]), (200, "stream"))
        status, body = self.request("lights/all", "PUT", {"on": False, "brightness": 30})
        status, body = self.request("light-presets/stream/recall", "POST")
        self.assertEqual((status, body["errors"]), (200, []))
        self.assertEqual(self.left.state, {"on": 1, "brightness": 3, "temperature": 344})
        status, body = self.request("status")
        self.assertEqual(body["light_presets"], [{"id": "stream", "name": "Stream"}])

        # Lampe anlegen, umbenennen, entfernen.
        status, body = self.request("lights", "POST", {"name": "Mitte", "host": "127.0.0.1:1"})
        self.assertEqual((status, body["light"]["id"]), (200, "mitte"))
        status, body = self.request("lights/mitte/config", "PUT", {"name": "Oben"})
        self.assertEqual((status, body["light"]["name"]), (200, "Oben"))
        status, body = self.request("lights/mitte", "DELETE")
        self.assertEqual((status, body), (200, {"ok": True}))

        # Rechts antwortet nicht: links wird gesetzt, Meldung; rechts allein ergibt 502.
        self.right.hang_s = 1.5
        status, body = self.request("lights/all", "PUT", {"brightness": 66})
        self.assertEqual((status, [e["code"] for e in body["errors"]]), (200, ["light_unreachable"]))
        self.assertEqual(self.left.state["brightness"], 66)
        status, body = self.request("lights/rechts", "PUT", {"brightness": 66})
        self.assertEqual((status, body["code"]), (502, "light_unreachable"))
        # Verbindung prüfen (P15 e4): ohne Schalten, auch hängend mit 200 und «reachable» false.
        puts = len(self.left.puts)
        status, body = self.request("lights/check", "POST", {"host": self.left.host})
        self.assertEqual((status, body["reachable"], body["brightness"]), (200, True, 66))
        self.assertEqual(len(self.left.puts), puts)
        status, body = self.request("lights/check", "POST", {"host": self.right.host})
        self.assertEqual((status, body["reachable"]), (200, False))
        self.assertIn("antwortet nicht", body["message"])
        status, body = self.request("lights/check", "POST", {"host": "a b"})
        self.assertEqual((status, body["code"]), (400, "invalid"))
        self.right.hang_s = 0.0
        self.assertEqual((self.left.bad_requests, self.right.bad_requests), (0, 0))
        self.assertEqual(self.service.stop(), 0, self.service.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
