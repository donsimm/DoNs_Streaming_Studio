"""Test kd_ereignisse: GET events (Server-Sent Events) und GET export (P15 c2).

- Im Prozess (Verteiler): Reihenfolge bei zwei Abonnenten, Stand beim Anmelden, Abmelden,
  ein Abonnent, der nicht liest, wird bei Überlauf abgemeldet und hält niemanden auf.
- Im Prozess über HTTP (Server ohne Geräte): Abmelden räumt auf, wenn die Verbindung zugeht;
  ein Abonnent, der den Socket nicht liest, hält weder andere Anfragen noch andere Abonnenten
  auf (Zeitgrenze) und wird nach der Schreib-Zeitgrenze abgemeldet.
- Über HTTP gegen den Dienst mit Attrappen-Ordner und Attrappen-Lampe: Ereignisse controls,
  lights, presets, status in Reihenfolge bei zwei Abonnenten; export mit Kamera-Einstellungen,
  Kamera- und Licht-Presets; ungültige Dateien werden im Export gemeldet.
"""

import http.client
import json
import socket
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SERVICE_DIR))

from dss_camera import http_api  # noqa: E402
from dss_camera.backend import Backend  # noqa: E402
from dss_camera.events import EventHub, Events  # noqa: E402
from dss_camera.fake import C922_BY_ID, C922_ID, TINY2_ID, FakeDeviceFolder, FakeLight  # noqa: E402
from test_api import free_port, ini  # noqa: E402
from test_werte import FolderService, send  # noqa: E402

# Grosszügig wegen paralleler Bauten; ohne Last kommen Ereignisse nach höchstens 0,2 s
# (Kamerasuche: 1 s).
EVENT_WAIT_S = 10
# Eine Anfrage neben einem Abonnenten, der nicht liest, braucht ohne Last Millisekunden.
REQUEST_LIMIT_S = 2.0


def parse(event: bytes) -> tuple[int, str, dict]:
    fields = dict(line.split(": ", 1) for line in event.decode("utf-8").strip().split("\n"))
    return int(fields["id"]), fields["event"], json.loads(fields["data"])


class HubTest(unittest.TestCase):
    def drain(self, subscriber) -> list[tuple[int, str, dict]]:
        result = []
        while (event := subscriber.get(0)) is not None:
            result.append(parse(event))
        return result

    def test_order_two_subscribers(self) -> None:
        hub = EventHub()
        first, second = hub.subscribe(), hub.subscribe()
        hub.update("lights", {"lights": [1]})
        hub.update("lights", {"lights": [1]})  # gleicher Stand: kein Ereignis
        hub.update("presets", {"camera_presets": [], "light_presets": []})
        hub.update("controls", {"camera": "c", "controls": []}, key=("controls", "c"), force=True)
        hub.update("controls", {"camera": "c", "controls": []}, key=("controls", "c"), force=True)
        hub.update("lights", {"lights": [2]})
        events = self.drain(first)
        self.assertEqual(events, self.drain(second))
        self.assertEqual([kind for _, kind, _ in events],
                         ["lights", "presets", "controls", "controls", "lights"])
        ids = [seq for seq, _, _ in events]
        self.assertEqual(ids, sorted(set(ids)))

    def test_initial_state_and_unsubscribe(self) -> None:
        hub = EventHub()
        hub.update("lights", {"lights": ["a"]})
        hub.update("status", {"cameras": ["c"]})
        hub.update("controls", {"camera": "c", "controls": []}, key=("controls", "c"), force=True)
        subscriber = hub.subscribe()
        # Stand beim Anmelden: status, presets (noch keiner), lights, mit ihrer Kennung
        # aufsteigend; nie controls.
        self.assertEqual(self.drain(subscriber),
                         [(1, "lights", {"lights": ["a"]}), (2, "status", {"cameras": ["c"]})])
        self.assertEqual(hub.count(), 1)
        hub.unsubscribe(subscriber)
        hub.unsubscribe(subscriber)  # zweimal schadet nicht
        self.assertEqual(hub.count(), 0)
        self.assertTrue(subscriber.closed)
        hub.update("lights", {"lights": ["b"]})
        self.assertIsNone(subscriber.get(0))

    def test_slow_subscriber_dropped(self) -> None:
        hub = EventHub(queue_limit=5)
        slow, fast = hub.subscribe(), hub.subscribe()
        received = []
        start = time.monotonic()
        for index in range(50):
            hub.update("lights", {"lights": [index]})
            received += self.drain(fast)
        self.assertLess(time.monotonic() - start, REQUEST_LIMIT_S)
        self.assertEqual([data["lights"][0] for _, _, data in received], list(range(50)))
        self.assertTrue(slow.closed)
        self.assertEqual(hub.count(), 1)


class SseReader:
    """Liest GET events in einem eigenen Faden und sammelt die Ereignisse."""

    def __init__(self, port: int) -> None:
        self.connection = http.client.HTTPConnection("127.0.0.1", port, timeout=EVENT_WAIT_S)
        self.connection.request("GET", "/api/v1/events")
        # Die Antwort hält den Socket offen, auch nach connection.close(); zum Schliessen gemerkt.
        self.sock = self.connection.sock
        self.response = self.connection.getresponse()
        self.events: list[tuple[int, str, dict]] = []
        self.raw = b""
        self._cond = threading.Condition()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self) -> None:
        block = b""
        try:
            while line := self.response.readline():
                block += line
                if line != b"\n":
                    continue
                if block.startswith(b"id: "):
                    with self._cond:
                        self.events.append(parse(block))
                        self._cond.notify_all()
                block = b""
        except OSError:
            pass

    def wait_for(self, check, what: str) -> tuple[int, str, dict]:
        """Wartet auf das erste neue Ereignis, für das `check(kind, data)` zutrifft."""
        deadline = time.monotonic() + EVENT_WAIT_S
        with self._cond:
            while True:
                for event in self.events:
                    if check(event[1], event[2]):
                        return event
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise AssertionError(f"Kein Ereignis «{what}»; erhalten: {self.events}")
                self._cond.wait(remaining)

    def until(self, seq: int) -> list[tuple[int, str, dict]]:
        with self._cond:
            return [event for event in self.events if event[0] <= seq]

    def close(self) -> None:
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()
        self.connection.close()
        self._thread.join(timeout=EVENT_WAIT_S)


class ServerTest(unittest.TestCase):
    """Server im Prozess ohne Geräte; Stand der Teile kommt direkt in den Verteiler."""

    def setUp(self) -> None:
        self.port = free_port()
        self.events = Events(Backend())
        api = http_api.Api(Backend(), fake=True, events=self.events)
        self.servers = http_api.Servers(api, ["127.0.0.1"], self.port, frozenset({"127.0.0.1"}))
        self.servers.start()
        # Ohne den Faden der Ereignisse: Der Test setzt den Stand selbst.
        self.events.hub.update("status", {"cameras": []})
        self.write_timeout = http_api.STREAM_WRITE_TIMEOUT_S

    def tearDown(self) -> None:
        http_api.STREAM_WRITE_TIMEOUT_S = self.write_timeout
        self.events.close()
        self.servers.close()

    def wait_count(self, count: int) -> None:
        deadline = time.monotonic() + EVENT_WAIT_S
        while self.events.hub.count() != count and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertEqual(self.events.hub.count(), count)

    def test_unsubscribe_on_close(self) -> None:
        first, second = SseReader(self.port), SseReader(self.port)
        self.assertEqual(first.response.status, 200)
        self.assertEqual(first.response.getheader("Content-Type"), "text/event-stream; charset=utf-8")
        first.wait_for(lambda kind, data: kind == "status", "status beim Anmelden")
        self.wait_count(2)
        second.close()
        # Ohne weiteres Ereignis: Der Dienst merkt das Schliessen selbst.
        self.wait_count(1)
        self.events.hub.update("lights", {"lights": ["neu"]})
        first.wait_for(lambda kind, data: data == {"lights": ["neu"]}, "lights")
        first.close()
        self.wait_count(0)

    def test_slow_reader_blocks_nothing(self) -> None:
        http_api.STREAM_WRITE_TIMEOUT_S = 1.0
        # Abonnent, der nie liest, mit kleinem Empfangspuffer.
        slow = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
        slow.connect(("127.0.0.1", self.port))
        slow.sendall(b"GET /api/v1/events HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
        fast = SseReader(self.port)
        try:
            self.wait_count(2)
            # Rund 8 MB: mehr, als in die Puffer des langsamen Abonnenten passt.
            big = "x" * 200_000
            for index in range(40):
                self.events.hub.update("lights", {"lights": [index, big]})
                start = time.monotonic()
                status, body = send(self.port, "/api/v1/status")
                self.assertEqual(status, 200, body)
                self.assertLess(time.monotonic() - start, REQUEST_LIMIT_S)
            last = fast.wait_for(lambda kind, data: kind == "lights" and data["lights"][0] == 39,
                                 "letztes lights")
            numbers = [data["lights"][0] for _, kind, data in fast.until(last[0]) if kind == "lights"]
            self.assertEqual(numbers[-40:], list(range(40)))
            # Der langsame wird nach der Schreib-Zeitgrenze abgemeldet, der schnelle bleibt.
            self.wait_count(1)
        finally:
            slow.close()
            fast.close()


class ServiceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_ereignisse-")
        root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(root / "geraete")
        self.folder.add_standard_cameras()
        (root / "dienst").mkdir()
        self.port = free_port()
        self.service = FolderService(root / "dienst", ini(self.port), self.folder.root)
        self.data_dir = root / "dienst" / "daten" / "dss-kamera"
        self.light = FakeLight(on=False, brightness=20, temperature=200)
        self.readers: list[SseReader] = []

    def tearDown(self) -> None:
        for reader in self.readers:
            reader.close()
        if self.service.process is not None:
            self.service.stop()
        self.light.close()
        self.tmp.cleanup()

    def stop(self) -> None:
        self.assertEqual(self.service.stop(), 0, self.service.stderr)

    def request(self, path: str, method: str = "GET", body=None) -> dict:
        status, answer = send(self.port, path, method, body)
        self.assertEqual(status, 200, answer)
        return answer

    def test_events_in_order_and_export(self) -> None:
        self.service.start()
        self.readers = [SseReader(self.port), SseReader(self.port)]
        for reader in self.readers:
            for kind in ("status", "presets", "lights"):
                reader.wait_for(lambda k, d, kind=kind: k == kind, f"{kind} beim Anmelden")
        first = self.readers[0]

        # Bildwert setzen → controls der C922 mit neuem Wert.
        cam = C922_ID.replace(":", "%3A")
        self.request(f"/api/v1/cameras/{cam}/controls", "PUT", {"power_line_frequency": 1})
        controls = first.wait_for(
            lambda k, d: k == "controls" and d["camera"] == C922_ID and any(
                item["key"] == "power_line_frequency" and item["value"] == 1 for item in d["controls"]),
            "controls C922")
        # Lampe anlegen → lights mit der neuen Lampe (erreichbar nach dem Nachführen).
        self.request("/api/v1/lights", "POST", {"name": "Links", "host": self.light.host})
        lights = first.wait_for(
            lambda k, d: k == "lights" and [(x["id"], x["reachable"]) for x in d["lights"]]
            == [("links", True)], "lights links")
        # Licht- und Kamera-Preset → presets mit ganzem Inhalt.
        self.request("/api/v1/light-presets", "POST", {"name": "Stream"})
        self.request("/api/v1/camera-presets/totale", "PUT",
                     {"name": "Totale", "camera": TINY2_ID, "pan": 0.0, "tilt": -10.0, "zoom": 0})
        presets = first.wait_for(
            lambda k, d: k == "presets" and [p["id"] for p in d["light_presets"]] == ["stream"]
            and [p["id"] for p in d["camera_presets"]] == ["totale"], "presets")
        self.assertEqual(presets[2]["light_presets"][0]["lights"],
                         {"links": {"on": False, "brightness": 20, "temperature": 200}})
        self.assertEqual(presets[2]["camera_presets"][0]["tilt"], -10.0)
        # Kamera ausstecken → status mit verschwundener C922.
        self.folder.unplug(C922_BY_ID)
        status = first.wait_for(
            lambda k, d: k == "status" and any(c["id"] == C922_ID and not c["present"]
                                               for c in d["cameras"]), "status ausgesteckt")
        self.assertLess(controls[0], lights[0])
        self.assertLess(lights[0], presets[0])
        self.assertLess(presets[0], status[0])

        # Beide Abonnenten haben dieselben Ereignisse in derselben Reihenfolge (ab dem ersten
        # Ereignis nach dem Anmelden beider).
        self.readers[1].wait_for(lambda k, d: k == "status" and any(
            c["id"] == C922_ID and not c["present"] for c in d["cameras"]), "status beim zweiten")
        events = [reader.until(status[0]) for reader in self.readers]
        events = [[event for event in items if event[0] >= controls[0]] for items in events]
        self.assertEqual(events[0], events[1])
        self.assertGreaterEqual(len(events[0]), 4)
        ids = [event[0] for event in events[0]]
        self.assertEqual(ids, sorted(set(ids)))

        # Export: alle Dateien des Datenordners.
        answer = self.request("/api/v1/export")
        self.assertEqual(answer["errors"], [])
        files = answer["files"]
        self.assertEqual(sorted(files), ["kamera-presets.json", "kameras.json", "licht-presets.json",
                                         "licht.json"])
        self.assertEqual(files["kameras.json"]["cameras"][C922_ID]["controls"],
                         {"power_line_frequency": 1})
        self.assertEqual([p["id"] for p in files["kamera-presets.json"]["presets"]], ["totale"])
        self.assertEqual(files["licht.json"]["lights"],
                         [{"id": "links", "name": "Links", "host": self.light.host}])
        self.assertEqual([p["id"] for p in files["licht-presets.json"]["presets"]], ["stream"])
        for reader in self.readers:
            reader.close()
        self.readers = []
        self.stop()
        self.assertIn("Ereignisse: Abonnent angemeldet, 2 aktiv.", self.service.stderr)

    def test_export_reports_invalid_files(self) -> None:
        self.data_dir.mkdir(parents=True)
        broken = b'{"format_version": 1, "presets": ['
        (self.data_dir / "licht-presets.json").write_bytes(broken)
        wrong = {"format_version": 2, "presets": []}
        (self.data_dir / "kamera-presets.json").write_text(json.dumps(wrong), encoding="utf-8")
        valid = {"format_version": 1, "cameras": {}}
        (self.data_dir / "kameras.json").write_text(json.dumps(valid), encoding="utf-8")
        self.service.start()
        answer = self.request("/api/v1/export")
        self.assertEqual(answer["files"], {"kamera-presets.json": wrong, "kameras.json": valid,
                                           "licht-presets.json": None})
        errors = {error["file"]: error["message"] for error in answer["errors"]}
        self.assertEqual(sorted(errors), ["kamera-presets.json", "licht-presets.json"])
        self.assertIn("format_version", errors["kamera-presets.json"])
        self.assertIn("kein gültiges JSON", errors["licht-presets.json"])
        self.stop()
        # Der Export ändert nichts an den Dateien.
        self.assertEqual((self.data_dir / "licht-presets.json").read_bytes(), broken)


if __name__ == "__main__":
    unittest.main(verbosity=2)
