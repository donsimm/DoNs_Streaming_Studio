"""Test kd_last: Lastprobe am Dienst mit Attrappen-Ordner (P15, Plan 12, Prüfstufe «Baustein»).

Nur mit `DSS_LASTPROBE=1` (ctest-Eintrag `kd_last`, nur `ctest -C Lastprobe`); ohne die
Variable besteht der Testfall sofort, ohne `skipTest`, damit der normale `ctest` 0 übersprungen
meldet (wie `tst_kerntwitch` `chatLoad`, P13).

Ablauf: Dienst als eigener Prozess mit Tiny 2, C922 und zwei Attrappen-Lampen. 60 s lang alle
100 ms eine Anfrage in eigenem Faden (Antwortzeit vom Senden bis zum Lesen der Antwort), reihum:
1. `move` der Tiny 2 (Pan und Tilt, Tempo 50) in den ersten 2,5 s jeder 5 s, sonst `GET status`;
2. Bildwerte: abwechselnd `PUT controls` der C922 (Helligkeit) und `GET controls` der Tiny 2;
3. Positionsabruf: abwechselnd Kamera-Preset der Tiny 2 und der C922 (`recall`);
4. Licht: reihum `PUT lights/links` (Helligkeit), `PUT lights/all` (umschalten), Licht-Preset.
Alle 5 s (ab 6 s) Ein- oder Ausstecken, abwechselnd Tiny 2 und C922; die Tiny 2 wird dabei
während einer Dauerbewegung ausgesteckt.

Prüft:
- jede Anfrage beantwortet (JSON), kein Status 5xx;
- keine Antwort über 3 s. Ausnahme nach Plan 2.3 Regel 7: Anfragen an eine Kamera, die vor dem
  Ende der Wartezeit nach dem Einstecken (Abfrage alle POLL_INTERVAL_S + SETTLE_S) gesendet
  wurden, warten absichtlich; für sie gilt höchstens 3 s nach dem Ende dieser Wartezeit;
- null «Neustarts» der Tiny-2-Attrappe;
- Totmann: Während die Tiny 2 eingesteckt ist und keine `move`-Anfrage läuft, steht ihr Tempo
  spätestens DEADMAN_S + 0,35 s nach der letzten Antwort auf `move` auf 0 (Abfrage alle 50 ms);
  der Dienst meldet mindestens einmal «(Totmann)»;
- der Dienst endet mit Exit-Code 0.

Ausgabe: Anzahl je Art und Status, Antwortzeiten (Median, p95, max) je Art, CPU des Dienstes in %
eines Kerns (Mittel, Median, p95, max; `utime + stime` aus `/proc/<pid>/stat`, proc(5)).
"""

import os
import statistics
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

SERVICE_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SERVICE_DIR))

from dss_camera import motion, v4l2  # noqa: E402
from dss_camera.devices import POLL_INTERVAL_S, SETTLE_S  # noqa: E402
from dss_camera.fake import (C922_BY_ID, C922_ID, TINY2_BY_ID, TINY2_ID,  # noqa: E402
                             FakeDeviceFolder, FakeLight)
from test_api import free_port, ini  # noqa: E402
from test_werte import FolderService, send  # noqa: E402

DURATION_S = 60.0
INTERVAL_S = 0.1
EVENT_FIRST_S = 6.0
EVENT_EVERY_S = 5.0
MOVE_WINDOW_S = 2.5
LIMIT_S = 3.0
DEADMAN_MARGIN_S = 0.35
NODE = "video0"
C922_NODE = "video2"
SPEEDS = (v4l2.V4L2_CID_PAN_SPEED, v4l2.V4L2_CID_TILT_SPEED)
CAMERA_PATH = {TINY2_ID: "/api/v1/cameras/" + TINY2_ID.replace(":", "%3A"),
               C922_ID: "/api/v1/cameras/" + C922_ID.replace(":", "%3A")}
BY_ID = {TINY2_ID: (TINY2_BY_ID, NODE), C922_ID: (C922_BY_ID, C922_NODE)}


def percentile(values: list[float], share: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(round(share * (len(ordered) - 1))))]


def cpu_seconds(pid: int) -> float:
    # proc(5): Feld 14 utime, 15 stime in Takten; nach dem Namen in Klammern gezählt.
    with open(f"/proc/{pid}/stat", encoding="ascii") as f:
        fields = f.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


class DrainedService(FolderService):
    """Wie FolderService, liest stderr nach dem Start in einem Faden weiter (Pipe läuft nie voll)."""

    def start(self) -> None:
        super().start()
        self._lines: list[str] = []
        self._reader = threading.Thread(target=self._drain, args=(self.process.stderr,), daemon=True)
        self._reader.start()

    def _drain(self, stream) -> None:
        for line in stream:
            self._lines.append(line)

    def stop(self) -> int:
        if self.process is None:
            return 0
        if self.process.poll() is None:
            self.process.terminate()
        code = self.process.wait(timeout=20)
        # Scheitert der Start, ruft FolderService.start() stop() vor dem Lesefaden auf.
        reader = getattr(self, "_reader", None)
        if reader is None:
            self.stderr += self.process.stderr.read()
        else:
            reader.join(timeout=5)
            self.stderr += "".join(self._lines)
        self.process = None
        return code


class LoadTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = tempfile.TemporaryDirectory(prefix="kd_last-")
        root = Path(self.tmp.name)
        self.folder = FakeDeviceFolder(root / "geraete")
        self.folder.add_standard_cameras()
        (root / "dienst").mkdir()
        self.left = FakeLight(on=False, brightness=20, temperature=200)
        self.right = FakeLight(on=True, brightness=50, temperature=300)
        data_dir = root / "dienst" / "daten" / "dss-kamera"
        data_dir.mkdir(parents=True)
        (data_dir / "licht.json").write_text(
            '{"format_version": 1, "lights": ['
            f'{{"id": "links", "name": "Links", "host": "{self.left.host}"}}, '
            f'{{"id": "rechts", "name": "Rechts", "host": "{self.right.host}"}}]}}', encoding="utf-8")
        self.port = free_port()
        self.service = DrainedService(root / "dienst", ini(self.port), self.folder.root)

    def tearDown(self) -> None:
        if self.service.process is not None:
            self.service.stop()
        self.left.close()
        self.right.close()
        self.tmp.cleanup()

    def request(self, path: str, method: str = "GET", body=None) -> tuple[int, dict]:
        return send(self.port, path, method, body)

    def prepare(self) -> tuple[str, str, str]:
        """Presets anlegen; wartet die Wartezeit nach dem Erscheinen und die Lampen ab."""
        status, body = self.request(CAMERA_PATH[TINY2_ID] + "/step", "POST", {"pan": 1})
        self.assertEqual(status, 200, body)
        status, body = self.request("/api/v1/camera-presets", "POST", {"name": "Links", "camera": TINY2_ID})
        self.assertEqual(status, 200, body)
        tiny2_preset = body["preset"]["id"]
        status, body = self.request(CAMERA_PATH[TINY2_ID] + "/home", "POST")
        self.assertEqual(status, 200, body)
        status, body = self.request("/api/v1/camera-presets", "POST", {"name": "Mitte", "camera": C922_ID})
        self.assertEqual(status, 200, body)
        c922_preset = body["preset"]["id"]
        deadline = time.monotonic() + 10
        while True:
            status, body = self.request("/api/v1/status")
            if all(light["reachable"] for light in body["lights"]) or time.monotonic() > deadline:
                break
            time.sleep(0.1)
        status, body = self.request("/api/v1/light-presets", "POST", {"name": "Hell"})
        self.assertEqual(status, 200, body)
        return tiny2_preset, c922_preset, body["preset"]["id"]

    def test_load(self) -> None:
        if os.environ.get("DSS_LASTPROBE") != "1":
            return  # nur mit ctest -C Lastprobe (Eintrag kd_last)
        self.service.start()
        pid = self.service.process.pid
        tiny2_preset, c922_preset, light_preset = self.prepare()

        lock = threading.Lock()
        results: list[dict] = []
        plugged = {TINY2_ID: True, C922_ID: True}
        settle_until = {TINY2_ID: 0.0, C922_ID: 0.0}
        moves_open = [0]
        last_move_end = [0.0]
        deadman_violations: list[str] = []
        cpu: list[float] = []
        stop = threading.Event()

        def plan(i: int) -> tuple[str, str | None, str, str, object]:
            """Art, Kamera, Methode, Pfad, Inhalt für Anfrage i."""
            t = i * INTERVAL_S
            slot, turn = i % 4, i // 4
            if slot == 0:
                if t % EVENT_EVERY_S < MOVE_WINDOW_S:
                    d = 1 if int(t // EVENT_EVERY_S) % 2 == 0 else -1
                    return ("move", TINY2_ID, "POST", CAMERA_PATH[TINY2_ID] + "/move",
                            {"pan": d, "tilt": -d, "speed": 50})
                return ("status", None, "GET", "/api/v1/status", None)
            if slot == 1:
                if turn % 2 == 0:
                    return ("controls", C922_ID, "PUT", CAMERA_PATH[C922_ID] + "/controls",
                            {"brightness": 100 + turn % 50})
                return ("controls", TINY2_ID, "GET", CAMERA_PATH[TINY2_ID] + "/controls", None)
            if slot == 2:
                if turn % 2 == 0:
                    return ("recall", TINY2_ID, "POST", f"/api/v1/camera-presets/{tiny2_preset}/recall", None)
                return ("recall", C922_ID, "POST", f"/api/v1/camera-presets/{c922_preset}/recall", None)
            kind = turn % 3
            if kind == 0:
                return ("light", None, "PUT", "/api/v1/lights/links", {"brightness": 10 + turn % 80})
            if kind == 1:
                return ("light", None, "PUT", "/api/v1/lights/all", {"on": "toggle"})
            return ("light", None, "POST", f"/api/v1/light-presets/{light_preset}/recall", None)

        def one(i: int, start: float) -> None:
            kind, camera, method, path, body = plan(i)
            with lock:
                limit_end = settle_until[camera] if camera else 0.0
                if kind == "move":
                    moves_open[0] += 1
            sent = time.monotonic()
            status, error = None, None
            try:
                status, _ = send(self.port, path, method, body)
            except Exception as exc:  # Zeitüberschreitung, Verbindung abgebrochen, kein JSON
                error = f"{type(exc).__name__}: {exc}"
            done = time.monotonic()
            # Plan 2.3 Regel 7: vor dem Ende der Wartezeit gesendet → 3 s ab deren Ende.
            limit = LIMIT_S + max(0.0, limit_end - sent)
            with lock:
                if kind == "move":
                    moves_open[0] -= 1
                    if status == 200:
                        last_move_end[0] = max(last_move_end[0], done)
                results.append({"i": i, "t": sent - start, "kind": kind, "camera": camera,
                                "status": status, "error": error, "s": done - sent, "limit": limit})

        def unplug_or_plug(k: int, start: float) -> None:
            camera = TINY2_ID if (k // 2) % 2 == 0 else C922_ID
            by_id, node = BY_ID[camera]
            with lock:
                if k % 2 == 0:
                    plugged[camera] = False
                    self.folder.unplug(by_id)
                else:
                    self.folder.plug(by_id, node)
                    settle_until[camera] = time.monotonic() + POLL_INTERVAL_S + SETTLE_S
                    plugged[camera] = True
            events.append((round(time.monotonic() - start, 2), camera, "aus" if k % 2 == 0 else "ein"))

        def watch_deadman() -> None:
            while not stop.is_set():
                with lock:
                    check = plugged[TINY2_ID] and moves_open[0] == 0
                    since = time.monotonic() - last_move_end[0]
                if check and since > motion.DEADMAN_S + DEADMAN_MARGIN_S:
                    try:
                        values = self.folder.values(NODE)
                    except (OSError, ValueError) as exc:
                        deadman_violations.append(f"Knoten nicht lesbar: {exc}")
                        values = {}
                    moving = {key: values.get(key, 0) for key in SPEEDS if values.get(key, 0)}
                    # Erneut prüfen: Ein- oder Ausstecken während des Lesens zählt nicht.
                    with lock:
                        still = plugged[TINY2_ID] and moves_open[0] == 0
                    if moving and still:
                        deadman_violations.append(f"{since:.2f} s nach move: Tempo {moving}")
                stop.wait(0.05)

        def watch_cpu() -> None:
            last_cpu, last_wall = cpu_seconds(pid), time.monotonic()
            while not stop.wait(1.0):
                now_cpu, now_wall = cpu_seconds(pid), time.monotonic()
                cpu.append(100.0 * (now_cpu - last_cpu) / (now_wall - last_wall))
                last_cpu, last_wall = now_cpu, now_wall

        events: list[tuple] = []
        watchers = [threading.Thread(target=watch_deadman, daemon=True),
                    threading.Thread(target=watch_cpu, daemon=True)]
        for watcher in watchers:
            watcher.start()
        start = time.monotonic()
        threads: list[threading.Thread] = []
        count = int(round(DURATION_S / INTERVAL_S))
        next_event = 0
        for i in range(count):
            due = start + i * INTERVAL_S
            while True:
                event_due = start + EVENT_FIRST_S + next_event * EVENT_EVERY_S
                if event_due < start + DURATION_S and event_due <= due:
                    time.sleep(max(0.0, event_due - time.monotonic()))
                    unplug_or_plug(next_event, start)
                    next_event += 1
                    continue
                break
            time.sleep(max(0.0, due - time.monotonic()))
            thread = threading.Thread(target=one, args=(i, start), daemon=True)
            thread.start()
            threads.append(thread)
        for thread in threads:
            thread.join(timeout=30)
        stop.set()
        for watcher in watchers:
            watcher.join(timeout=5)
        # Ausgesteckte Kamera wieder einstecken, dann sauber beenden.
        for camera, state in plugged.items():
            if not state:
                self.folder.plug(*BY_ID[camera])
        code = self.service.stop()
        state = self.folder.tiny2(NODE)

        # Ausgabe
        kinds = sorted({r["kind"] for r in results})
        print(f"Anfragen: {len(results)} in {DURATION_S:.0f} s, Ein-/Ausstecken: {events}")
        for kind in kinds:
            rows = [r for r in results if r["kind"] == kind]
            times = [r["s"] * 1000 for r in rows]
            statuses: dict = {}
            for r in rows:
                statuses[r["status"] or r["error"]] = statuses.get(r["status"] or r["error"], 0) + 1
            print(f"  {kind}: {len(rows)}, Status {statuses}, Antwort ms Median "
                  f"{statistics.median(times):.1f} / p95 {percentile(times, 0.95):.1f} / max {max(times):.1f}")
        waited = [r for r in results if r["limit"] > LIMIT_S]
        normal = [r for r in results if r["limit"] <= LIMIT_S]
        print(f"  ohne Wartezeit nach dem Einstecken: {len(normal)}, max "
              f"{max(r['s'] for r in normal) * 1000:.1f} ms")
        if waited:
            print(f"  in der Wartezeit nach dem Einstecken gesendet: {len(waited)}, max "
                  f"{max(r['s'] for r in waited) * 1000:.1f} ms, max nach Ende der Wartezeit "
                  f"{max(r['s'] - (r['limit'] - LIMIT_S) for r in waited) * 1000:.1f} ms")
        deadman_lines = [line for line in self.service.stderr.splitlines() if "(Totmann)" in line]
        print(f"  Totmann gemeldet: {len(deadman_lines)}, Verletzungen: {deadman_violations}")
        print(f"  Tiny 2: restarts {state['restarts']}, lost_moves {state['lost_moves']}, "
              f"moves_asleep {state['moves_asleep']}, run {state['run']}")
        print(f"  CPU des Dienstes in % eines Kerns: Mittel {statistics.mean(cpu):.1f}, Median "
              f"{statistics.median(cpu):.1f}, p95 {percentile(cpu, 0.95):.1f}, max {max(cpu):.1f} "
              f"({len(cpu)} Sekunden: {[round(v, 1) for v in cpu]})")
        print(f"  Lampen: {len(self.left.puts)} + {len(self.right.puts)} PUT, Fehler 400: "
              f"{self.left.bad_requests + self.right.bad_requests}")

        self.assertEqual(code, 0, self.service.stderr[-4000:])
        self.assertGreaterEqual(len(results), count)
        self.assertEqual([r for r in results if r["error"]], [])
        self.assertEqual([r for r in results if r["status"] >= 500], [])
        too_slow = [r for r in results if r["s"] > r["limit"]]
        self.assertEqual(too_slow, [])
        self.assertEqual(state["restarts"], 0)
        self.assertEqual(deadman_violations, [])
        self.assertGreaterEqual(len(deadman_lines), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
