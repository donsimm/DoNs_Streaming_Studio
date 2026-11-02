#!/usr/bin/env python3
"""Startprüfung für dss-core und dss-app in der echten X11-Sitzung (CLAUDE.md Regel 6).

Vorlage: prototypes/messung/t01.py (nicht verändert, Hilfsfunktionen hierher übernommen).

Je Start:
- Dauer bis zum Start: Zeit vor Popen bis zur Protokollzeile «… gestartet, Qt …» (dss-core)
  bzw. «Fenster gezeichnet» (dss-app), gemessen im Skript mit CLOCK_MONOTONIC beim Lesen der Zeile.
- dss-app: zusätzlich, ob xwininfo ein sichtbares Fenster «DoNs Streaming Studio» findet.
  Seit P05 läuft dss-app mit eigenem XDG_CONFIG_HOME (ein temporärer Ordner je Fall), damit
  die Prüfung die Fenstereinstellungen des Nutzers nicht ändert. Ab dem zweiten Start lädt
  dss-app also die beim vorigen Beenden gespeicherte Anordnung.
- Beenden: nach 1 s Laufzeit SIGTERM (dss-core auch SIGINT). dss-app schliesst bei SIGTERM sein
  Fenster, also derselbe Weg wie beim Schliessen von Hand.
- Seit P07 nie mit echten Daten: dss-core läuft mit eigenem --datenordner und eigener
  --verbindung (temporäre Ordner, Socket in $XDG_RUNTIME_DIR). dss-app läuft mit einer eigenen
  --verbindung, an der kein Kern antwortet, und einer --einheit, die es nicht gibt: Das Fenster
  versucht die Einheit zu starten, das scheitert, und es startet trotzdem (Teil F).
  Fall «app-kern-sigterm»: vor jedem Start ein eigener dss-core; das Fenster verbindet sich, beim
  Schliessen bittet es den Kern, sich zu beenden. Sauber heisst dann zusätzlich: Der Kern endet
  mit Exit-Code 0 und meldet «Beenden erbeten von dss-app».
- Sauber: Exit-Code 0, Startzeile gelesen, «beendet, Exit-Code 0» im Protokoll, keine
  Zeitüberschreitung, keine unerwarteten Protokollzeilen und 2 s nach Ende kein Prozess der
  Sitzung mehr vorhanden (Sitzungs-ID aus /proc/<pid>/stat, proc(5)).

  python3 scripts/startpruefung.py [--runs 20] [--build build] [--out docs/berichte/P03-daten]
"""
import argparse
import json
import os
import selectors
import signal
import subprocess
import sys
import tempfile
import time

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))
WINDOW_TITLE = "DoNs Streaming Studio"
START_TIMEOUT_S = 10
EXIT_TIMEOUT_S = 10
RUNNING_S = 1.0

CORE = "src/core/dss-core"
UNIT_MISSING = "dss-startpruefung-gibt-es-nicht.service"
CORE_EXPECTED = [" gestartet, Qt ", " Overlays geladen", "Verbindung offen: "]

# Programm, Signal zum Beenden, Muster der Startzeile, erwartete Protokollzeilen
CASES = {
    "core-sigterm": (CORE, signal.SIGTERM, "Verbindung offen: ",
                     CORE_EXPECTED + ["SIGTERM erhalten, beende", "beendet, Exit-Code 0"]),
    "core-sigint": (CORE, signal.SIGINT, "Verbindung offen: ",
                    CORE_EXPECTED + ["SIGINT erhalten, beende", "beendet, Exit-Code 0"]),
    "app-sigterm": ("src/app/dss-app", signal.SIGTERM, "Fenster gezeichnet",
                    [" gestartet, Qt ", "Fenster gezeichnet", "SIGTERM erhalten, schliesse Fenster",
                     "beendet, Exit-Code 0"]),
    "app-kern-sigterm": ("src/app/dss-app", signal.SIGTERM, "Fenster gezeichnet",
                         [" gestartet, Qt ", "Fenster gezeichnet",
                          "SIGTERM erhalten, schliesse Fenster", "Kern beendet sich",
                          "beendet, Exit-Code 0"]),
}


def _stat_fields(pid):
    with open(f"/proc/{pid}/stat") as f:
        s = f.read()
    # Feld 2 (comm) steht in Klammern und kann Leerzeichen enthalten
    return s[s.rfind(")") + 2:].split()


def session_members(sid):
    """Prozesse mit Sitzungs-ID sid (proc(5): Feld 6 = session)."""
    found = []
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            if int(_stat_fields(int(d))[3]) == sid:
                found.append(int(d))
        except (OSError, IndexError, ValueError):
            pass
    return found


def comm(pid):
    try:
        with open(f"/proc/{pid}/comm") as f:
            return f.read().strip()
    except OSError:
        return "?"


def window_visible():
    """xwininfo(1): sucht das Fenster nach Titel, True wenn «Map State: IsViewable»."""
    r = subprocess.run(["xwininfo", "-name", WINDOW_TITLE], capture_output=True, text=True)
    return r.returncode == 0 and "IsViewable" in r.stdout


def read_until(proc, pattern, timeout_s, lines):
    """Liest stderr zeilenweise, bis pattern vorkommt. Gibt CLOCK_MONOTONIC-ns der Zeile zurück."""
    sel = selectors.DefaultSelector()
    sel.register(proc.stderr, selectors.EVENT_READ)
    deadline = time.monotonic() + timeout_s
    try:
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not sel.select(remaining):
                return None
            line = proc.stderr.readline()
            if not line:
                return None
            t = time.monotonic_ns()
            lines.append(line.rstrip("\n"))
            if pattern in line:
                return t
    finally:
        sel.close()


def start_core(build, data_dir, socket_path, env):
    """Eigener Kern für den Fall app-kern-sigterm; wartet, bis er Verbindungen annimmt."""
    core = subprocess.Popen([os.path.join(build, CORE), "--datenordner", data_dir,
                             "--verbindung", socket_path], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, start_new_session=True, env=env)
    core_lines = []
    ready = read_until(core, "Verbindung offen: ", START_TIMEOUT_S, core_lines)
    return core, core_lines, ready is not None


def one_run(build, case, idx, env=None, data_dir=None, socket_path=None):
    rel, sig, ready_pattern, expected = CASES[case]
    cmd = [os.path.join(build, rel)]
    if rel == CORE:
        cmd += ["--datenordner", data_dir, "--verbindung", socket_path]
    else:
        cmd += ["--verbindung", socket_path, "--einheit", UNIT_MISSING]
    core = None
    core_ok = True
    core_lines = []
    if case == "app-kern-sigterm":
        core, core_lines, core_ok = start_core(build, data_dir, socket_path, env)
    lines = []
    t0 = time.monotonic_ns()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         start_new_session=True, env=env)
    t_ready = read_until(p, ready_pattern, START_TIMEOUT_S, lines)
    visible = None
    if t_ready is not None:
        time.sleep(RUNNING_S)
        if case.startswith("app"):
            visible = window_visible()
    t_signal = time.monotonic_ns()
    if p.poll() is None:
        p.send_signal(sig)
    timeout = False
    try:
        out, err = p.communicate(timeout=EXIT_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        timeout = True
        p.kill()
        out, err = p.communicate()
    t_end = time.monotonic_ns()
    lines += [l for l in err.splitlines()]
    lines = [l for l in lines if l.strip()]

    leftovers = session_members(p.pid)
    deadline = time.monotonic() + 2
    while leftovers and time.monotonic() < deadline:
        time.sleep(0.1)
        leftovers = session_members(p.pid)
    leftover_names = [comm(x) for x in leftovers]
    for pid in leftovers:
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass

    core_exit = None
    if core is not None:
        try:
            _, core_err = core.communicate(timeout=EXIT_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            core.kill()
            _, core_err = core.communicate()
            core_ok = False
        core_lines += [l for l in core_err.splitlines() if l.strip()]
        core_exit = core.returncode
        core_ok = (core_ok and core_exit == 0
                   and any("Beenden erbeten von dss-app" in l for l in core_lines))

    unexpected = [l for l in lines if not any(e in l for e in expected)]
    missing = [e for e in expected if not any(e in l for l in lines)]
    clean = (p.returncode == 0 and not timeout and t_ready is not None and not leftovers
             and not unexpected and not missing and visible is not False and not out.strip()
             and core_ok)
    return {
        "run": idx,
        "exit_code": p.returncode,
        "start_ms": round((t_ready - t0) / 1e6, 1) if t_ready else None,
        "exit_ms": round((t_end - t_signal) / 1e6, 1),
        "window_visible": visible,
        "timeout": timeout,
        "leftover_processes": leftover_names,
        "unexpected_lines": unexpected,
        "missing_lines": missing,
        "stdout": out.strip(),
        "log": lines,
        "core_exit_code": core_exit,
        "core_log": core_lines,
        "clean": clean,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=20)
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("--out", default=os.path.join(ROOT, "docs", "berichte", "P03-daten"))
    ap.add_argument("--cases", nargs="+", choices=list(CASES), default=list(CASES))
    args = ap.parse_args()

    if os.environ.get("XDG_SESSION_TYPE") != "x11" or not os.environ.get("DISPLAY"):
        print("FEHLER: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)", file=sys.stderr)
        return 2

    os.makedirs(args.out, exist_ok=True)
    all_ok = True
    for case in args.cases:
        started = time.strftime("%Y-%m-%d %H:%M:%S %z")
        results = []
        config_dir = tempfile.TemporaryDirectory(prefix="dss-startpruefung-")
        data_dir = tempfile.TemporaryDirectory(prefix="dss-startpruefung-daten-")
        # Kurzer Pfad: Unix-Sockets fassen höchstens 107 Byte (unix(7)).
        run_dir = tempfile.TemporaryDirectory(prefix="dss-sp-",
                                              dir=os.environ.get("XDG_RUNTIME_DIR"))
        socket_path = os.path.join(run_dir.name, "kern.sock")
        env = dict(os.environ, XDG_CONFIG_HOME=config_dir.name)
        for i in range(1, args.runs + 1):
            r = one_run(args.build, case, i, env, data_dir.name, socket_path)
            results.append(r)
            print(json.dumps({k: v for k, v in r.items() if k not in ("log", "core_log")},
                             ensure_ascii=False),
                  flush=True)
            time.sleep(0.5)
        config_saved = os.path.exists(
            os.path.join(config_dir.name, "dons-streaming-studio", "fenster.ini"))
        config_dir.cleanup()
        data_dir.cleanup()
        run_dir.cleanup()

        st = sorted(r["start_ms"] for r in results if r["start_ms"] is not None)
        ex = sorted(r["exit_ms"] for r in results)
        summary = {
            "case": case,
            "started": started,
            "command": os.path.join(args.build, CASES[case][0]),
            "with_core": case == "app-kern-sigterm",
            "signal": CASES[case][1].name,
            "display": os.environ.get("DISPLAY"),
            "config_saved": config_saved if case.startswith("app") else None,
            "runs": len(results),
            "clean": sum(r["clean"] for r in results),
            "start_ms_min": st[0] if st else None,
            "start_ms_median": st[len(st) // 2] if st else None,
            "start_ms_max": st[-1] if st else None,
            "exit_ms_min": ex[0],
            "exit_ms_median": ex[len(ex) // 2],
            "exit_ms_max": ex[-1],
        }
        all_ok &= summary["clean"] == len(results)
        with open(os.path.join(args.out, f"startpruefung-{case}.json"), "w") as f:
            json.dump({"summary": summary, "runs": results}, f, indent=1, ensure_ascii=False)

        lines = ["| Start | Exit-Code | bis Start (ms) | Beenden (ms) | Fenster sichtbar | "
                 "Restprozesse | unerwartete Zeilen | sauber |",
                 "|---|---|---|---|---|---|---|---|"]
        for r in results:
            vis = {None: "–", True: "ja", False: "nein"}[r["window_visible"]]
            lines.append(f"| {r['run']} | {r['exit_code']} | {r['start_ms']} | {r['exit_ms']} | "
                         f"{vis} | {', '.join(r['leftover_processes']) or 'keine'} | "
                         f"{len(r['unexpected_lines'])} | {'ja' if r['clean'] else 'nein'} |")
        with open(os.path.join(args.out, f"startpruefung-{case}.md"), "w") as f:
            f.write("\n".join(lines) + "\n")
        print(json.dumps(summary, ensure_ascii=False), flush=True)
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
