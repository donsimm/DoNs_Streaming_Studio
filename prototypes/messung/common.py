"""Gemeinsame Angaben der Messskripte T-01 und T-02 (Pfade, Befehle je Variante, Prozesse)."""
import os
import subprocess
import time

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROTO = os.path.join(ROOT, "prototypes")
MEDIA = os.path.join(PROTO, "media")
OUT = os.path.join(ROOT, "docs", "messungen", "P01-daten")
SHOTS = os.path.join(ROOT, "docs", "messungen", "P01-bilder")
CORE_PORT = 8765

A_BIN = os.path.join(PROTO, "a-cpp-qt", "build", "p01_a")
B_BIN = os.path.join(PROTO, "b-web", "host", "build", "p01_b_host")
C_PY = os.path.join(PROTO, "c-pyqt", "main.py")


def app_cmd(variant, mode, stats=None, duration=None):
    if variant == "A":
        cmd = [A_BIN, "--mode", mode]
    elif variant == "C":
        cmd = ["python3", C_PY, "--mode", mode]
    elif variant == "B":
        page = "t01.html" if mode == "t01" else f"overlay.html?duration={duration}"
        return [B_BIN, "--mode", mode, "--url", f"http://127.0.0.1:{CORE_PORT}/{page}"] + (
            ["--stats", stats] if stats else [])
    else:
        raise ValueError(variant)
    if mode == "t02":
        cmd += ["--media", MEDIA, "--stats", stats, "--duration", str(duration)]
    return cmd


def start_core():
    """Startet den Kern von Variante B und wartet auf CORE_READY."""
    p = subprocess.Popen(
        ["python3", os.path.join(PROTO, "b-web", "core", "core.py"), "--port", str(CORE_PORT),
         "--web", os.path.join(PROTO, "b-web", "web"), "--media", MEDIA],
        stdout=subprocess.PIPE, text=True, start_new_session=True)
    line = p.stdout.readline()
    if not line.startswith("CORE_READY"):
        raise RuntimeError(f"Kern nicht bereit: {line!r}")
    return p


def stop_core(p):
    p.terminate()
    p.wait(timeout=10)


def _stat_fields(pid):
    with open(f"/proc/{pid}/stat") as f:
        s = f.read()
    # Feld 2 (comm) steht in Klammern und kann Leerzeichen enthalten
    return s[s.rfind(")") + 2:].split()


def all_pids():
    return [int(d) for d in os.listdir("/proc") if d.isdigit()]


def session_members(sid):
    """Prozesse mit Sitzungs-ID sid (proc(5): Feld 6 = session)."""
    found = []
    for pid in all_pids():
        try:
            if int(_stat_fields(pid)[3]) == sid:
                found.append(pid)
        except (OSError, IndexError, ValueError):
            pass
    return found


def descendants(root):
    """root und alle Nachkommen (proc(5): Feld 4 = ppid)."""
    children = {}
    for pid in all_pids():
        try:
            children.setdefault(int(_stat_fields(pid)[1]), []).append(pid)
        except (OSError, IndexError, ValueError):
            pass
    out, todo = [], [root]
    while todo:
        p = todo.pop()
        out.append(p)
        todo.extend(children.get(p, []))
    return out


def pss_kib(pid):
    """PSS aus /proc/<pid>/smaps_rollup in KiB, None falls nicht lesbar."""
    try:
        with open(f"/proc/{pid}/smaps_rollup") as f:
            for line in f:
                if line.startswith("Pss:"):
                    return int(line.split()[1])
    except OSError:
        return None
    return None


def comm(pid):
    try:
        with open(f"/proc/{pid}/comm") as f:
            return f.read().strip()
    except OSError:
        return "?"


def now_iso():
    return time.strftime("%Y-%m-%d %H:%M:%S %z")
