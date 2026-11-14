#!/usr/bin/env python3
"""Ton-Prüfung P12b b6 (Plan docs/archiv/prompts/P12-ton.md, Abschnitt «b6»): Ton der Videos durch
den ganzen Kern, unhörbar über eine virtuelle Senke.

Ablauf:
- Vorher: Reste «dss_tonpruefung…» in PipeWire entfernen, Standardausgang und -eingang merken.
- Virtuelle Senke mit pw-loopback: «dss_tonpruefung_ausgabe» (Audio/Sink) gibt alles an die Quelle
  «dss_tonpruefung_aufnahme» (Audio/Source) weiter. Kein echtes Gerät ist beteiligt; jede Verbindung
  von oder zu diesen Knoten ausser Kern → Senke und Quelle → Aufnahme bricht die Prüfung ab.
- Eigener Datenordner, eigener Ordner der Einstellungen und eigene Verbindung in einem temporären
  Ordner (nie ~/.local/share/dons-streaming-studio* oder ~/.config/dons-streaming-studio*).
  dss-cli legt dort ein Overlay mit tests/daten/ton/blitz-piep.mp4 an (Piep 1 kHz alle 2 s, 33,3 ms,
  tests/daten/ton/LIESMICH.md); kern.ini wählt die virtuelle Senke als Ausgang (Versatz 0).
- dss-core aus build/ (Version in Arbeit) mit diesem Ordner, eigenem Port und --einstellungen.
- Aufnahme mit pw-cat von der Quelle (roh, float, 48 kHz) über stdout. Jeder gelesene Block bekommt
  die Zeit des Eintreffens (CLOCK_MONOTONIC, wie steady_clock im Kern); die Zeit einer Probe ist der
  kleinste Wert «Eintreffen − Probenlage» der Blöcke in ±5 s (Messweg eingeschlossen, wie M1 und Z3).
- Seite in OBS nachgebildet wie tst_kernton: …/events/<page> offen, je Lauf «start» (Lage 0),
  jede Sekunde «frame», 0,5 s nach --dauer-s «stop». Die Uhr der Seite ist CLOCK_MONOTONIC dieses
  Skripts.
- Auswertung je Piep (ohne den Piep bei 0, der vor der ersten Meldung liegt): Abweichung = Einsatz in
  der Aufnahme − Zeitpunkt nach den Meldungen (Start der Seite + 2 s · k); zusätzlich ohne den
  Zyklus, den der Kern für das Gerät einrechnet (+ size/rate aus «Abruf: …»). Dazu die Zeilen des Kerns:
  «Ton: Lage …» (Abweichung, wie der Kern sie sieht), «Ton: Nachführen», «Abruf: delay …, size …»
  (gemeldete Verzögerung von PipeWire), «Abrufe ohne Puffer».
- Aufräumen (auch bei Abbruch und SIGINT/SIGTERM/SIGHUP): Aufnahme, Kern und pw-loopback beenden,
  Reste mit pw-cli destroy entfernen, prüfen: keine Knoten «dss_tonpruefung…», Standardausgang und
  -eingang wie vorher, temporärer Ordner gelöscht.

Bestanden (Interna P12b b6, Bericht P12):
- jeder Piep ab k = 1 genau einmal gefunden, kein zusätzlicher Einsatz, Länge 30 bis 36 ms;
- Streuung der Abweichung p5–p95 ≤ 15 ms (wie E4 in b1) und max − min ≤ 40 ms (Ziel H2);
- Kern: je Lauf «Ton: Start» und «Ton: Stopp … Seite inaktiv», 0 Abrufe ohne Puffer, jede Meldung
  mit 204 angenommen, Ende mit Exit-Code 0.

  python3 scripts/tonpruefung.py [--laeufe 20] [--dauer-s 12] [--build build] [--out DIR]

Exit-Code: 0 bestanden, 1 nicht bestanden, 2 Abbruch, 3 Aufräumen nicht vollständig.
Ergebnis in --out (Standard build/tonpruefung/<Datum-Zeit>): ergebnis.txt, einsaetze.csv, kern.log,
pw.log. Der Token des Overlays steht in keiner Datei ausserhalb des temporären Ordners.

Quellen (nicht abgerufen, kein Webzugang; Aufrufe wie in prototypes/ton/pw-lauf.sh, dort erprobt):
https://docs.pipewire.org/page_man_pw-loopback_1.html, https://docs.pipewire.org/page_man_pw-cat_1.html
(--record, --target, --raw, «-» = stdout), https://docs.pipewire.org/page_man_pw-dump_1.html,
https://docs.pipewire.org/page_man_pw-cli_1.html (destroy),
https://docs.python.org/3/library/time.html#time.monotonic_ns (CLOCK_MONOTONIC unter Linux),
https://html.spec.whatwg.org/multipage/server-sent-events.html
"""
import argparse
import array
import bisect
import glob
import http.client
import json
import os
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))
VIDEO = os.path.join(ROOT, "tests", "daten", "ton", "blitz-piep.mp4")
PREFIX = "dss_tonpruefung"
SINK = PREFIX + "_ausgabe"
SOURCE = PREFIX + "_aufnahme"
CORE_NODE = "dss_core_entwicklung_ton_video"  # src/core/main.cpp, Version in Arbeit
PAGE = "tonpruefung01"
RATE = 48000
CHANNELS = 2
BEEP_PERIOD_NS = 2_000_000_000
THRESHOLD = 0.05    # Piep mit halber Vollaussteuerung (0,5): Schwelle −20 dB darunter, nach 0,02 ms
QUIET_S = 0.25      # Ruhe vor einem Einsatz
BLOCK = 240         # 5 ms
MATCH_NS = 300_000_000
STOP_AFTER_NS = 500_000_000
MAP_WINDOW = 5 * RATE
BEEP_LEN_MS = (30.0, 36.0)
SPREAD_P5_P95_MS = 15.0
SPREAD_MAX_MIN_MS = 40.0
# Port des Overlay-Servers: wie scripts/startpruefung.py 32000 bis 32767 (nie 8080/8081, nicht der
# Bereich der Tests 24000 bis 31999), hier von oben gezählt
PORT_FIRST = 32767 - os.getpid() % 384


class Abbruch(Exception):
    pass


def now_ns():
    return time.monotonic_ns()


class Log:
    def __init__(self, path):
        self.f = open(path, "a", encoding="utf-8")

    def __call__(self, text):
        line = time.strftime("%F %T ") + text
        print(line, flush=True)
        self.f.write(line + "\n")
        self.f.flush()


# ---------- PipeWire

def pw_state():
    """Knoten (id → props), Verbindungen (aus, ein) und Standardausgang/-eingang aus pw-dump."""
    r = subprocess.run(["pw-dump"], capture_output=True, text=True, timeout=30)
    if r.returncode != 0:
        raise Abbruch("pw-dump: Exit-Code %d" % r.returncode)
    nodes, links, default = {}, [], {}
    for o in json.loads(r.stdout or "[]"):
        t = o.get("type", "")
        info = o.get("info") or {}
        if t == "PipeWire:Interface:Node":
            nodes[o["id"]] = info.get("props") or {}
        elif t == "PipeWire:Interface:Link":
            links.append((info.get("output-node-id"), info.get("input-node-id")))
        elif t == "PipeWire:Interface:Metadata" and (o.get("props") or {}).get("metadata.name") == "default":
            for e in o.get("metadata") or []:
                if e.get("key") in ("default.audio.sink", "default.audio.source"):
                    v = e.get("value")
                    default[e["key"]] = v.get("name") if isinstance(v, dict) else v
    return nodes, links, default


def own_nodes(nodes):
    return {i: p for i, p in nodes.items() if str(p.get("node.name", "")).startswith(PREFIX)}


def check_safe(log, default_before, recorder_pid=None):
    """Bricht ab, wenn die virtuellen Knoten mit etwas anderem verbunden sind als Kern → Senke und
    Quelle → Aufnahme, oder wenn sich Standardausgang oder -eingang geändert hat."""
    nodes, links, default = pw_state()
    own = own_nodes(nodes)
    name = lambda i: str(nodes.get(i, {}).get("node.name", "?"))
    for a, b in links:
        if a in own and b not in own:
            p = nodes.get(b, {})
            pid = p.get("application.process.id")
            # Aufnahme: Knoten von pw-cat mit der PID der Aufnahme; trägt der Knoten keine PID, ein
            # Aufnahmestrom namens pw-cat (Stream/Input/Audio, spielt nichts ab)
            recording = recorder_pid is not None and (
                str(pid) == str(recorder_pid)
                or (pid is None and name(b) == "pw-cat" and str(p.get("media.class")) == "Stream/Input/Audio"))
            if not recording:
                raise Abbruch("Verbindung %s → %s (nicht die Aufnahme; media.class %s, application.process.id %s)"
                              % (name(a), name(b), p.get("media.class"), pid))
        if b in own and a not in own and name(a) != CORE_NODE:
            raise Abbruch("Verbindung %s → %s (nicht der Kern)" % (name(a), name(b)))
    if default != default_before:
        raise Abbruch("Standard geändert: vorher %s, jetzt %s" % (default_before, default))
    return own


def destroy_leftovers(log):
    nodes, _, _ = pw_state()
    for i, p in own_nodes(nodes).items():
        log("Rest %s (Knoten %d): pw-cli destroy" % (p.get("node.name"), i))
        subprocess.run(["pw-cli", "destroy", str(i)], capture_output=True, timeout=10)


# ---------- Aufnahme

class Recorder:
    """Liest pw-cat --raw von stdout, linker Kanal; je Block Eintreffen und Probenlage danach."""

    def __init__(self, proc):
        self.proc = proc
        self.samples = array.array("f")
        self.arrival = []   # ns
        self.frames = []    # Probenlage am Ende des Blocks
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        fd = self.proc.stdout.fileno()
        rest = b""
        frame_bytes = 4 * CHANNELS
        while True:
            data = os.read(fd, 1 << 16)
            t = now_ns()
            if not data:
                break
            data = rest + data
            n = len(data) // frame_bytes * frame_bytes
            rest = data[n:]
            a = array.array("f")
            a.frombytes(data[:n])
            self.samples.extend(a[0::CHANNELS])
            self.arrival.append(t)
            self.frames.append(len(self.samples))

    def time_ns(self, n):
        """Zeit der Probe n: kleinster Wert «Eintreffen − Lage» der Blöcke in ±5 s."""
        lo = bisect.bisect_left(self.frames, n - MAP_WINDOW)
        hi = bisect.bisect_right(self.frames, n + MAP_WINDOW)
        if lo >= hi:
            return None
        t0 = min(self.arrival[i] - self.frames[i] * 1_000_000_000 // RATE for i in range(lo, hi))
        return t0 + n * 1_000_000_000 // RATE


def find_onsets(s):
    """Einsätze: erste Probe über der Schwelle nach QUIET_S Ruhe; Länge bis 20 ms Ruhe, Spitze."""
    peaks = []
    for i in range(0, len(s) - BLOCK + 1, BLOCK):
        b = s[i:i + BLOCK]
        peaks.append(max(max(b), -min(b)))
    quiet_blocks = int(QUIET_S * RATE / BLOCK)
    onsets = []
    j = quiet_blocks
    while j < len(peaks):
        if peaks[j] > THRESHOLD and all(p <= THRESHOLD for p in peaks[j - quiet_blocks:j]):
            start = j * BLOCK
            while abs(s[start]) <= THRESHOLD:
                start += 1
            end = start
            k = j
            while k < len(peaks) and any(p > THRESHOLD for p in peaks[k:k + 4]):
                k += 1
            for m in range(min(len(s), k * BLOCK + BLOCK) - 1, start - 1, -1):
                if abs(s[m]) > THRESHOLD:
                    end = m
                    break
            peak = max(peaks[j:k + 1])
            onsets.append((start, (end - start) * 1000.0 / RATE, peak))
            j = k + 1
        else:
            j += 1
    return onsets


# ---------- Seite

class Page:
    """Seite in OBS, nachgebildet: …/events/<page> offen, Meldungen an …/clock (Format
    src/server/clockreport.h, docs/08-kern-schnittstelle.md)."""

    def __init__(self, port, base, source, file):
        self.port, self.base, self.source, self.file = port, base, source, file
        self.seq = 0
        self.codes = []
        self.events = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.events.sendall(("GET %sevents/%s HTTP/1.1\r\nHost: tonpruefung\r\nAccept: text/event-stream\r\n\r\n"
                             % (base, PAGE)).encode())
        head = self.events.recv(4096).decode("utf-8", "replace")
        if " 200 " not in head.split("\r\n", 1)[0]:
            raise Abbruch("events/<page>: %s" % head.split("\r\n", 1)[0])
        self.events.settimeout(None)
        threading.Thread(target=self.drain, daemon=True).start()
        self.clock = http.client.HTTPConnection("127.0.0.1", port, timeout=5)

    def drain(self):
        try:
            while self.events.recv(4096):
                pass
        except OSError:
            pass

    def send(self, event, run, media_s=None, display_ms=None):
        self.seq += 1
        m = {"v": 1, "page": PAGE, "seq": self.seq, "event": event, "run": run, "now_ms": now_ns() / 1e6}
        if event != "stop":
            m.update({"source": self.source, "file": self.file, "loop": 0, "media_s": media_s,
                      "display_ms": display_ms, "duration_s": 60.0})
        self.clock.request("POST", self.base + "clock", body=json.dumps(m),
                           headers={"Content-Type": "text/plain"})
        r = self.clock.getresponse()
        r.read()
        self.codes.append(r.status)

    def close(self):
        self.clock.close()
        try:
            self.events.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.events.close()


def sleep_until(t_ns):
    while True:
        d = t_ns - now_ns()
        if d <= 0:
            return
        time.sleep(min(d / 1e9, 0.05))


# ---------- Kern

class Core:
    def __init__(self, cmd, env, log_path):
        self.proc = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        self.lines = []
        self.lock = threading.Lock()
        self.file = open(log_path, "w", encoding="utf-8")
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for raw in self.proc.stderr:
            line = raw.decode("utf-8", "replace").rstrip("\n")
            with self.lock:
                self.lines.append(line)
            self.file.write(line + "\n")
            self.file.flush()

    def wait_for(self, text, timeout_s, start=0):
        """Index der ersten Zeile ab start, die text enthält (text: Zeichenkette oder Liste, alle
        Teile nötig); -1 nach timeout_s oder wenn der Kern endet."""
        parts = [text] if isinstance(text, str) else text
        end = time.monotonic() + timeout_s
        while time.monotonic() < end:
            with self.lock:
                for i in range(start, len(self.lines)):
                    if all(p in self.lines[i] for p in parts):
                        return i
            if self.proc.poll() is not None:
                break
            time.sleep(0.02)
        return -1

    def stop(self, timeout_s=10):
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try:
                self.proc.wait(timeout_s)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        time.sleep(0.2)
        return self.proc.returncode


def stop_proc(p, timeout_s=5):
    if p is None or p.poll() is not None:
        return
    p.send_signal(signal.SIGTERM)
    try:
        p.wait(timeout_s)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()


def free_port():
    for port in range(PORT_FIRST, 31999, -1):
        with socket.socket() as s:
            try:
                s.bind(("127.0.0.1", port))
                return port
            except OSError:
                continue
    raise Abbruch("kein freier Port 32000 bis 32767")


def number_after(line, word):
    i = line.find(word)
    if i < 0:
        return None
    t = line[i + len(word):].strip().split()[0].rstrip(",")
    try:
        return float(t)
    except ValueError:
        return None


def pct(values, p):
    v = sorted(values)
    k = (len(v) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


# ---------- Ablauf

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--laeufe", type=int, default=20)
    ap.add_argument("--dauer-s", type=int, default=12, help="Dauer eines Laufs in s (gewertet die Piepe bei 2, 4, … bis --dauer-s)")
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("--out", default=None)
    args = ap.parse_args()
    if args.laeufe < 1 or args.dauer_s < 4 or args.dauer_s > 58:
        ap.error("--laeufe ≥ 1, --dauer-s 4 bis 58")

    out = args.out or os.path.join(ROOT, "build", "tonpruefung", time.strftime("%Y-%m-%d-%H%M%S"))
    os.makedirs(out, exist_ok=True)
    log = Log(os.path.join(out, "pw.log"))

    def on_signal(sig, _frame):
        raise Abbruch("Signal %d" % sig)
    for sig in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, on_signal)

    core_bin = os.path.join(args.build, "src", "core", "dss-core")
    cli_bin = os.path.join(args.build, "src", "cli", "dss-cli")
    tmp = None
    loop = recorder_proc = None
    core = page = recorder = None
    default_before = None
    rc = 2
    result = []
    try:
        for b in (core_bin, cli_bin):
            if not os.access(b, os.X_OK):
                raise Abbruch("%s fehlt (zuerst cmake --build build)" % b)
        for tool in ("pw-loopback", "pw-cat", "pw-dump", "pw-cli"):
            if not shutil.which(tool):
                raise Abbruch("%s fehlt" % tool)
        if not os.path.isfile(VIDEO):
            raise Abbruch("%s fehlt" % VIDEO)
        log("Tonprüfung: %d Läufe zu %d s, Ausgabe %s" % (args.laeufe, args.dauer_s, out))
        log("Last (1/5/15 min): %s" % " ".join(open("/proc/loadavg").read().split()[:3]))
        destroy_leftovers(log)
        _, _, default_before = pw_state()
        log("Vorher: %s" % default_before)

        # Eigene Ordner, nie die echten
        tmp = tempfile.mkdtemp(prefix="dss-tonpruefung-")
        data = os.path.join(tmp, "daten")
        config_home = os.path.join(tmp, "konfig")
        settings_dir = os.path.join(config_home, "dons-streaming-studio-entwicklung")
        os.makedirs(settings_dir)
        os.makedirs(data)
        env = dict(os.environ, XDG_CONFIG_HOME=config_home, XDG_DATA_HOME=os.path.join(tmp, "share"))
        port = free_port()
        ini = os.path.join(settings_dir, "kern.ini")
        with open(ini, "w", encoding="utf-8") as f:
            f.write("[overlay-server]\nport=%d\nadresse-im-heimnetz=\n\n[ton-video]\nausgang=%s\nversatz_ms=0\n"
                    % (port, SINK))
        for cmd in (["overlay-neu", "Tonpruefung", "1280", "720"],
                    ["quelle-video", "tonpruefung", VIDEO]):
            r = subprocess.run([cli_bin, "--datenordner", data] + cmd, env=env, capture_output=True, text=True,
                               timeout=30)
            if r.returncode != 0:
                raise Abbruch("dss-cli %s: %s" % (cmd[0], r.stderr.strip()))
        files = glob.glob(os.path.join(data, "overlays", "*.json"))
        if len(files) != 1:
            raise Abbruch("Overlay-Datei fehlt")
        overlay = json.load(open(files[0], encoding="utf-8"))
        video = overlay["sources"][0]
        token = json.load(open(os.path.join(data, "tokens.json"), encoding="utf-8"))["tokens"][overlay["id"]]
        base = "/o/%s/%s/" % (overlay["address"], token)
        log("Overlay «%s» mit «%s» (%s), Lautstärke %s, stumm %s"
            % (overlay["name"], video["name"], video["video"]["file"], video["video"].get("volume"),
               video["video"].get("muted")))

        # Virtuelle Senke
        pwlog = open(os.path.join(out, "pw-loopback.log"), "w")
        loop = subprocess.Popen(
            ["pw-loopback", "--channels", "2", "--name", PREFIX,
             "--capture-props=media.class=Audio/Sink node.name=%s node.description=\"DSS Tonprüfung Ausgabe "
             "(stumm)\" audio.position=[FL,FR]" % SINK,
             "--playback-props=media.class=Audio/Source node.name=%s node.description=\"DSS Tonprüfung "
             "Aufnahme\" audio.position=[FL,FR]" % SOURCE],
            stdout=pwlog, stderr=subprocess.STDOUT)
        time.sleep(2)
        if loop.poll() is not None:
            raise Abbruch("pw-loopback läuft nicht (Exit-Code %s)" % loop.returncode)
        own = check_safe(log, default_before)
        names = sorted(str(p.get("node.name")) for p in own.values())
        if SINK not in names or SOURCE not in names:
            raise Abbruch("Knoten fehlen: %s" % names)
        log("pw-loopback läuft (PID %d), Knoten %s" % (loop.pid, ", ".join(names)))

        # Kern
        core = Core([core_bin, "--datenordner", data, "--verbindung", os.path.join(tmp, "kern.sock"),
                     "--einstellungen", ini, "--standardport", str(port)], env, os.path.join(out, "kern.log"))
        if core.wait_for("Overlay-Server lauscht auf 127.0.0.1:%d" % port, 10) < 0:
            raise Abbruch("Overlay-Server startet nicht (kern.log)")
        if core.wait_for("Ton (ton-video): Zustand playing", 10) < 0:
            raise Abbruch("Ausgang des Kerns nicht «playing» (kern.log)")
        log("Kern läuft (PID %d), Port %d, Ausgang %s" % (core.proc.pid, port, SINK))

        # Aufnahme
        recorder_proc = subprocess.Popen(
            ["pw-cat", "--record", "--target", SOURCE, "--rate", str(RATE), "--channels", str(CHANNELS),
             "--format", "f32", "--raw", "-"],
            stdout=subprocess.PIPE, stderr=open(os.path.join(out, "pw-cat.log"), "w"))
        recorder = Recorder(recorder_proc)
        time.sleep(1.5)
        if recorder_proc.poll() is not None or not recorder.frames:
            raise Abbruch("pw-cat nimmt nicht auf (pw-cat.log)")
        check_safe(log, default_before, recorder_proc.pid)
        log("Aufnahme läuft (pw-cat, PID %d)" % recorder_proc.pid)

        page = Page(port, base, video["id"], video["video"]["file"])
        time.sleep(1.0)  # Vorrat des Decoders, sobald die Seite verbunden ist
        runs = []
        line_pos = 0
        for run in range(1, args.laeufe + 1):
            check_safe(log, default_before, recorder_proc.pid)
            t0 = now_ns()
            page.send("start", run, 0.0, t0 / 1e6)
            for i in range(1, args.dauer_s + 1):
                sleep_until(t0 + i * 1_000_000_000)
                page.send("frame", run, float(i), (t0 + i * 1_000_000_000) / 1e6)
            # Stopp 0,5 s nach der letzten vollen Sekunde: Der Kern schreibt etwa 40 ms voraus, ein
            # Piep genau beim Stopp wäre je nach Laufzeit ganz, halb oder gar nicht zu hören.
            sleep_until(t0 + args.dauer_s * 1_000_000_000 + STOP_AFTER_NS)
            page.send("stop", run)
            started = core.wait_for(["Ton: Start ", "(Seite %s, Lauf %d)" % (PAGE, run)], 2, line_pos)
            stopped = core.wait_for("Seite inaktiv (gespielt", 3, max(started, line_pos))
            line_pos = max(line_pos, started, stopped)
            runs.append({"run": run, "t0": t0, "started": started >= 0, "stopped": stopped >= 0,
                         "stop_line": core.lines[stopped] if stopped >= 0 else ""})
            log("Lauf %d/%d: Start %s, Stopp %s" % (run, args.laeufe, "ja" if started >= 0 else "NEIN",
                                                   "ja" if stopped >= 0 else "NEIN"))
            time.sleep(2.0)
        time.sleep(0.5)

        # Ende: Aufnahme, Seite, Kern
        stop_proc(recorder_proc)
        recorder.thread.join(10)
        page.close()
        core_rc = core.stop()
        with core.lock:
            core_lines = list(core.lines)
        core_clean = core_rc == 0 and any("beendet, Exit-Code 0" in l for l in core_lines)

        # ---------- Auswertung
        onsets = find_onsets(recorder.samples)
        expected = []
        for r in runs:
            for k in range(0, args.dauer_s // 2 + 1):
                expected.append((r["run"], k, r["t0"] + k * BEEP_PERIOD_NS))
        found = {}
        extra = []
        for start, length, peak in onsets:
            t = recorder.time_ns(start)
            best = min(expected, key=lambda e: abs(e[2] - t)) if t is not None else None
            if best is None or abs(best[2] - t) > MATCH_NS or (best[0], best[1]) in found:
                extra.append((start, t, length, peak))
                continue
            found[(best[0], best[1])] = ((t - best[2]) / 1e6, length, peak, start)
        rows = []
        missing = []
        start_beeps = []
        for run, k, t in expected:
            f = found.get((run, k))
            if k == 0:
                start_beeps.append(f)
                continue
            if f is None:
                missing.append((run, k))
            else:
                rows.append((run, k, (t - runs[0]["t0"]) / 1e9) + f)
        with open(os.path.join(out, "einsaetze.csv"), "w") as f:
            f.write("lauf,piep,zeit_s,abweichung_ms,laenge_ms,spitze\n")
            for run, k, ts, dev, length, peak, _ in rows:
                f.write("%d,%d,%.3f,%.3f,%.2f,%.3f\n" % (run, k, ts, dev, length, peak))

        devs = [r[3] for r in rows]
        lengths = [r[4] for r in rows]
        lag = [l for l in core_lines if "Ton: Lage " in l]
        lag_devs = [v for v in (number_after(l, "Abweichung") for l in lag) if v is not None]
        follow = [l for l in core_lines if "Ton: Nachführen" in l]
        empty = [l for l in core_lines if "Abrufe ohne Puffer" in l]
        aborted = [l for l in core_lines if "abgebrochene Zyklen" in l]
        delay_lines = [l[l.find("Abruf:"):] for l in core_lines if "Abruf: delay" in l]
        stop_jumps = [number_after(r["stop_line"], "Sprünge") for r in runs]
        stop_soft = [number_after(r["stop_line"], "weich") for r in runs]
        codes_bad = [c for c in page.codes if c != 204]

        res = result
        res.append("Tonprüfung P12b b6, %s" % time.strftime("%F %T"))
        res.append("Aufruf: python3 scripts/tonpruefung.py --laeufe %d --dauer-s %d" % (args.laeufe, args.dauer_s))
        res.append("Läufe: %d zu %d s, Meldungen %d (nicht 204: %d)" % (len(runs), args.dauer_s, len(page.codes),
                                                                       len(codes_bad)))
        res.append("Aufnahme: %.1f s, %d Blöcke" % (len(recorder.samples) / RATE, len(recorder.frames)))
        res.append("Piepe ab k = 1: erwartet %d, gefunden %d, fehlen %d, zusätzliche Einsätze %d"
                   % (len(rows) + len(missing), len(rows), len(missing), len(extra)))
        if missing:
            res.append("  fehlen: %s" % ", ".join("Lauf %d Piep %d" % m for m in missing))
        for start, t, length, peak in extra:
            res.append("  zusätzlich: Probe %d, Länge %.1f ms, Spitze %.3f" % (start, length, peak))
        sb = [s for s in start_beeps if s]
        res.append("Piep bei 0 (vor der ersten Meldung, nicht gewertet): in %d von %d Läufen gehört%s"
                   % (len(sb), len(start_beeps),
                      ", Abweichung %.1f bis %.1f ms" % (min(s[0] for s in sb), max(s[0] for s in sb)) if sb else ""))
        ok = not missing and not extra and not codes_bad
        if devs:
            p5, p50, p95 = pct(devs, 5), pct(devs, 50), pct(devs, 95)
            spread = max(devs) - min(devs)
            res.append("Abweichung zu den Meldungen (Einsatz − Start der Seite − 2 s · k, Messweg eingeschlossen):")
            res.append("  Mittel %.2f ms, Median %.2f ms, p5 %.2f, p95 %.2f, min %.2f, max %.2f"
                       % (statistics.fmean(devs), p50, p5, p95, min(devs), max(devs)))
            res.append("  Streuung p5–p95 %.2f ms (Grenze %.0f), max − min %.2f ms (Grenze %.0f)"
                       % (p95 - p5, SPREAD_P5_P95_MS, spread, SPREAD_MAX_MIN_MS))
            means = [statistics.fmean([r[3] for r in rows if r[0] == run["run"]] or [0]) for run in runs]
            res.append("  Mittel je Lauf: min %.2f, max %.2f ms" % (min(means), max(means)))
            # Gemeldete Verzögerung: Der Kern rechnet den hörbaren Zeitpunkt mit einem Zyklus (size)
            # nach dem Abruf (Lesart «Rest + Quantum», b1/b2). Die virtuelle Senke gibt die Proben
            # im selben Zyklus an die Aufnahme weiter; ohne diesen Zyklus bleibt der Messweg.
            sizes = {number_after(l, "size") for l in delay_lines} - {None}
            if len(sizes) == 1:
                size = sizes.pop()
                res.append("  Abweichung + size/rate (size %d, %.2f ms): Median %.2f ms, p5 %.2f, p95 %.2f"
                           % (size, size * 1000 / RATE, p50 + size * 1000 / RATE, p5 + size * 1000 / RATE,
                              p95 + size * 1000 / RATE))
            else:
                res.append("  size wechselte (%s), Abweichung + size/rate nicht gerechnet" % sorted(sizes))
            if len(rows) > 2:
                slope = statistics.linear_regression([r[2] for r in rows], devs).slope * 60
                res.append("  Gang über alle Läufe: %.3f ms/min" % slope)
            res.append("  Länge der Piepe %.2f bis %.2f ms (Grenze %.0f bis %.0f), Spitze %.3f bis %.3f"
                       % (min(lengths), max(lengths), BEEP_LEN_MS[0], BEEP_LEN_MS[1],
                          min(r[5] for r in rows), max(r[5] for r in rows)))
            ok = ok and p95 - p5 <= SPREAD_P5_P95_MS and spread <= SPREAD_MAX_MIN_MS
            ok = ok and all(BEEP_LEN_MS[0] <= l <= BEEP_LEN_MS[1] for l in lengths)
        else:
            ok = False
        res.append("Kern (kern.log):")
        res.append("  Start in %d von %d Läufen, Stopp «Seite inaktiv» in %d"
                   % (sum(r["started"] for r in runs), len(runs), sum(r["stopped"] for r in runs)))
        res.append("  «Ton: Lage»: %d Zeilen, Abweichung %s" % (
            len(lag), "%.2f bis %.2f ms" % (min(lag_devs), max(lag_devs)) if lag_devs else "–"))
        res.append("  «Ton: Nachführen»: %d Zeilen; Stopp: Sprünge %s, weich %s"
                   % (len(follow), sum(v or 0 for v in stop_jumps), sum(v or 0 for v in stop_soft)))
        res.append("  Abrufe ohne Puffer: %d Zeilen; abgebrochene Zyklen: %d Zeilen" % (len(empty), len(aborted)))
        res.append("  gemeldete Verzögerung von PipeWire: %s" % ("; ".join(sorted(set(delay_lines))) or "–"))
        res.append("  Ende: Exit-Code %s, «beendet, Exit-Code 0» %s" % (core_rc, "ja" if core_clean else "NEIN"))
        ok = ok and core_clean and not empty and all(r["started"] and r["stopped"] for r in runs)
        rc = 0 if ok else 1
        res.append("Ergebnis: %s" % ("bestanden" if ok else "NICHT bestanden"))
    except (Abbruch, KeyboardInterrupt) as e:
        log("ABBRUCH: %s" % (str(e) or "SIGINT"))
        result.append("ABBRUCH: %s" % (str(e) or "SIGINT"))
        rc = 2
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGHUP, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        if page:
            page.close()
        stop_proc(recorder_proc)
        if core:
            core.stop()
        stop_proc(loop)
        time.sleep(1)
        clean = []
        try:
            destroy_leftovers(log)
            time.sleep(0.5)
            nodes, _, default = pw_state()
            rest = own_nodes(nodes)
            if rest:
                clean.append("noch %d Knoten %s…" % (len(rest), PREFIX))
            if default_before is not None and default != default_before:
                clean.append("Standard ist %s, vorher %s" % (default, default_before))
        except Exception as e:  # Aufräumen läuft weiter
            clean.append("Prüfung nach dem Aufräumen: %s" % e)
        for p, what in ((loop, "pw-loopback"), (recorder_proc, "pw-cat"), (core.proc if core else None, "dss-core")):
            if p is not None and p.poll() is None:
                clean.append("%s läuft noch (PID %d)" % (what, p.pid))
        if tmp:
            shutil.rmtree(tmp, ignore_errors=True)
            if os.path.exists(tmp):
                clean.append("temporärer Ordner bleibt: %s" % tmp)
        if clean:
            result.append("Aufräumen: ABWEICHUNG: %s" % "; ".join(clean))
            rc = 3
        else:
            result.append("Aufräumen geprüft: keine Knoten %s…, pw-loopback, pw-cat und Kern beendet, Standard "
                          "unverändert, temporärer Ordner gelöscht" % PREFIX)
        with open(os.path.join(out, "ergebnis.txt"), "w") as f:
            f.write("\n".join(result) + "\n")
        print("\n".join(result))
        print("Ergebnis: %s" % os.path.join(out, "ergebnis.txt"))
    return rc


if __name__ == "__main__":
    sys.exit(main())
