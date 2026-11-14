"""Gemeinsame Teile der Ton-Skripte (scripts/tonpruefung.py, scripts/tonleistung.py): virtuelle Senke
über pw-loopback mit Sicherung «nichts an ein echtes Gerät», Aufnahme mit pw-cat, Erkennung der Piepe
in «Blitz und Piep» (tests/daten/ton/LIESMICH.md), nachgebildete Seite in OBS (…/events/<page>,
…/clock), Kern aus build/ mit eigenen Ordnern im Temp-Ordner, Aufräumen.

Quellen (nicht abgerufen, kein Webzugang; Aufrufe wie in prototypes/ton/pw-lauf.sh, dort erprobt):
https://docs.pipewire.org/page_man_pw-loopback_1.html, https://docs.pipewire.org/page_man_pw-cat_1.html
(--record, --target, --raw, «-» = stdout), https://docs.pipewire.org/page_man_pw-dump_1.html,
https://docs.pipewire.org/page_man_pw-cli_1.html (destroy),
https://docs.python.org/3/library/time.html#time.monotonic_ns (CLOCK_MONOTONIC unter Linux),
https://html.spec.whatwg.org/multipage/server-sent-events.html
"""
import array
import bisect
import glob
import http.client
import json
import os
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))
CORE_NODE = "dss_core_entwicklung_ton_video"  # src/core/main.cpp, Version in Arbeit
RATE = 48000
CHANNELS = 2
BEEP_PERIOD_NS = 2_000_000_000
THRESHOLD = 0.05    # Piep mit halber Vollaussteuerung (0,5): Schwelle −20 dB darunter, nach 0,02 ms
QUIET_S = 0.25      # Ruhe vor einem Einsatz
BLOCK = 240         # 5 ms
MAP_WINDOW = 5 * RATE
# Port des Overlay-Servers: wie scripts/startpruefung.py 32000 bis 32767 (nie 8080/8081, nicht der
# Bereich der Tests 24000 bis 31999), hier von oben gezählt
PORT_FIRST = 32767 - os.getpid() % 384


class Abbruch(Exception):
    pass


def now_ns():
    return time.monotonic_ns()


def sleep_until(t_ns):
    while True:
        d = t_ns - now_ns()
        if d <= 0:
            return
        time.sleep(min(d / 1e9, 0.05))


class Log:
    def __init__(self, path):
        self.f = open(path, "a", encoding="utf-8")

    def __call__(self, text):
        line = time.strftime("%F %T ") + text
        print(line, flush=True)
        self.f.write(line + "\n")
        self.f.flush()


def raise_on_signals():
    """SIGTERM und SIGHUP brechen ab wie SIGINT (Aufräumen im finally)."""
    def on_signal(sig, _frame):
        raise Abbruch("Signal %d" % sig)
    for sig in (signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, on_signal)


def ignore_signals():
    for sig in (signal.SIGTERM, signal.SIGHUP, signal.SIGINT):
        signal.signal(sig, signal.SIG_IGN)


def check_tools(build):
    """Pfade von dss-core und dss-cli; Abbruch, wenn etwas fehlt."""
    core_bin = os.path.join(build, "src", "core", "dss-core")
    cli_bin = os.path.join(build, "src", "cli", "dss-cli")
    for b in (core_bin, cli_bin):
        if not os.access(b, os.X_OK):
            raise Abbruch("%s fehlt (zuerst cmake --build build)" % b)
    for tool in ("pw-loopback", "pw-cat", "pw-dump", "pw-cli"):
        if not shutil.which(tool):
            raise Abbruch("%s fehlt" % tool)
    return core_bin, cli_bin


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


def own_nodes(nodes, prefix):
    return {i: p for i, p in nodes.items() if str(p.get("node.name", "")).startswith(prefix)}


def check_safe(prefix, default_before, recorder_pid=None):
    """Bricht ab, wenn die virtuellen Knoten mit etwas anderem verbunden sind als Kern → Senke und
    Quelle → Aufnahme, oder wenn sich Standardausgang oder -eingang geändert hat."""
    nodes, links, default = pw_state()
    own = own_nodes(nodes, prefix)
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


def destroy_leftovers(log, prefix):
    nodes, _, _ = pw_state()
    for i, p in own_nodes(nodes, prefix).items():
        log("Rest %s (Knoten %d): pw-cli destroy" % (p.get("node.name"), i))
        subprocess.run(["pw-cli", "destroy", str(i)], capture_output=True, timeout=10)


def start_loopback(log, prefix, out, what):
    """Virtuelle Senke «<prefix>_ausgabe» (Audio/Sink), die alles an die Quelle «<prefix>_aufnahme»
    (Audio/Source) weitergibt; kein echtes Gerät. what: Name im Beschrieb der Knoten."""
    sink, source = prefix + "_ausgabe", prefix + "_aufnahme"
    pwlog = open(os.path.join(out, "pw-loopback.log"), "w")
    loop = subprocess.Popen(
        ["pw-loopback", "--channels", "2", "--name", prefix,
         "--capture-props=media.class=Audio/Sink node.name=%s node.description=\"%s Ausgabe "
         "(stumm)\" audio.position=[FL,FR]" % (sink, what),
         "--playback-props=media.class=Audio/Source node.name=%s node.description=\"%s "
         "Aufnahme\" audio.position=[FL,FR]" % (source, what)],
        stdout=pwlog, stderr=subprocess.STDOUT)
    return loop


def check_loopback(log, loop, prefix, default_before):
    time.sleep(2)
    if loop.poll() is not None:
        raise Abbruch("pw-loopback läuft nicht (Exit-Code %s)" % loop.returncode)
    own = check_safe(prefix, default_before)
    names = sorted(str(p.get("node.name")) for p in own.values())
    if prefix + "_ausgabe" not in names or prefix + "_aufnahme" not in names:
        raise Abbruch("Knoten fehlen: %s" % names)
    log("pw-loopback läuft (PID %d), Knoten %s" % (loop.pid, ", ".join(names)))


def cleanup(log, prefix, default_before, procs, tmp):
    """Nach dem Beenden aller Prozesse: Reste entfernen und nachprüfen. procs: (Popen oder None, Name).
    Gibt die Abweichungen zurück (leer = sauber)."""
    clean = []
    try:
        destroy_leftovers(log, prefix)
        time.sleep(0.5)
        nodes, _, default = pw_state()
        rest = own_nodes(nodes, prefix)
        if rest:
            clean.append("noch %d Knoten %s…" % (len(rest), prefix))
        if default_before is not None and default != default_before:
            clean.append("Standard ist %s, vorher %s" % (default, default_before))
    except Exception as e:  # Aufräumen läuft weiter
        clean.append("Prüfung nach dem Aufräumen: %s" % e)
    for p, what in procs:
        if p is not None and p.poll() is None:
            clean.append("%s läuft noch (PID %d)" % (what, p.pid))
    if tmp:
        shutil.rmtree(tmp, ignore_errors=True)
        if os.path.exists(tmp):
            clean.append("temporärer Ordner bleibt: %s" % tmp)
    return clean


# ---------- Eigene Ordner und Overlay

def make_workdir(prefix, cli_bin, overlay_name, videos, port, output):
    """Temp-Ordner mit Daten-, Konfig- und Laufzeitordner (nie die echten), kern.ini mit Port und
    Ausgang (leer = kein Ausgang), Overlay 1280x720 mit den Videos über dss-cli. videos: Liste von
    (Datei, Wiederholen). Gibt tmp, env, ini, overlay (JSON) und den Basispfad der Seite zurück."""
    tmp = tempfile.mkdtemp(prefix=prefix + "-")
    data = os.path.join(tmp, "daten")
    config_home = os.path.join(tmp, "konfig")
    settings_dir = os.path.join(config_home, "dons-streaming-studio-entwicklung")
    os.makedirs(settings_dir)
    os.makedirs(data)
    env = dict(os.environ, XDG_CONFIG_HOME=config_home, XDG_DATA_HOME=os.path.join(tmp, "share"))
    ini = os.path.join(settings_dir, "kern.ini")
    write_ini(ini, port, output)
    cmds = [["overlay-neu", overlay_name, "1280", "720"]]
    address = None
    for file, loop in videos:
        cmds.append(["quelle-video", None, file] + (["--wiederholen"] if loop else []))
    for cmd in cmds:
        if cmd[1] is None:
            cmd[1] = address
        r = subprocess.run([cli_bin, "--datenordner", data] + cmd, env=env, capture_output=True, text=True,
                           timeout=30)
        if r.returncode != 0:
            raise Abbruch("dss-cli %s: %s" % (cmd[0], r.stderr.strip()))
        if cmd[0] == "overlay-neu":
            files = glob.glob(os.path.join(data, "overlays", "*.json"))
            if len(files) != 1:
                raise Abbruch("Overlay-Datei fehlt")
            address = json.load(open(files[0], encoding="utf-8"))["address"]
    files = glob.glob(os.path.join(data, "overlays", "*.json"))
    overlay = json.load(open(files[0], encoding="utf-8"))
    token = json.load(open(os.path.join(data, "tokens.json"), encoding="utf-8"))["tokens"][overlay["id"]]
    base = "/o/%s/%s/" % (overlay["address"], token)
    return tmp, env, ini, data, overlay, base


def write_ini(ini, port, output):
    with open(ini, "w", encoding="utf-8") as f:
        f.write("[overlay-server]\nport=%d\nadresse-im-heimnetz=\n\n[ton-video]\nausgang=%s\nversatz_ms=0\n"
                % (port, output))


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


def start_recorder(log, source, out, prefix, default_before):
    proc = subprocess.Popen(
        ["pw-cat", "--record", "--target", source, "--rate", str(RATE), "--channels", str(CHANNELS),
         "--format", "f32", "--raw", "-"],
        stdout=subprocess.PIPE, stderr=open(os.path.join(out, "pw-cat.log"), "w"))
    recorder = Recorder(proc)
    time.sleep(1.5)
    if proc.poll() is not None or not recorder.frames:
        raise Abbruch("pw-cat nimmt nicht auf (pw-cat.log)")
    check_safe(prefix, default_before, proc.pid)
    log("Aufnahme läuft (pw-cat, PID %d)" % proc.pid)
    return proc, recorder


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

    def __init__(self, port, base, page_id):
        self.port, self.base, self.page_id = port, base, page_id
        self.seq = 0
        self.codes = []
        self.events = socket.create_connection(("127.0.0.1", port), timeout=5)
        self.events.sendall(("GET %sevents/%s HTTP/1.1\r\nHost: tonpruefung\r\nAccept: text/event-stream\r\n\r\n"
                             % (base, page_id)).encode())
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

    def send(self, event, run, source=None, file=None, media_s=None, display_ms=None, loop=0, duration_s=60.0):
        self.seq += 1
        m = {"v": 1, "page": self.page_id, "seq": self.seq, "event": event, "run": run, "now_ms": now_ns() / 1e6}
        if event != "stop":
            m.update({"source": source, "file": file, "loop": loop, "media_s": media_s,
                      "display_ms": display_ms, "duration_s": duration_s})
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


def start_core(core_bin, data, tmp, ini, port, env, log_path):
    """dss-core aus build/ mit den eigenen Ordnern; wartet, bis der Overlay-Server lauscht."""
    core = Core([core_bin, "--datenordner", data, "--verbindung", os.path.join(tmp, "kern.sock"),
                 "--einstellungen", ini, "--standardport", str(port)], env, log_path)
    if core.wait_for("Overlay-Server lauscht auf 127.0.0.1:%d" % port, 10) < 0:
        raise Abbruch("Overlay-Server startet nicht (%s)" % log_path)
    return core


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
