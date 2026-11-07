#!/usr/bin/env python3
"""Dienstprüfung für dss-core als Benutzerdienst von systemd (P07, Tests) in der echten X11-Sitzung.

Nie mit echten Daten und nie mit der echten Einheit: eine eigene Einheit (Standard
dss-core-pruefung.service) mit eigenem --datenordner (Testdaten wie im Handtest, mit dss-cli
angelegt) und eigener --verbindung (Socket in $XDG_RUNTIME_DIR). dss-app läuft mit eigenem
XDG_CONFIG_HOME, damit fenster.ini des Nutzers unberührt bleibt. Seit P10b (Punkt 83) hat der Kern
eine eigene kern.ini (--einstellungen) mit eigenem Port (32000 bis 32767 nach der PID, wie
scripts/startpruefung.py); vorher nutzte er die kern.ini der Version in Arbeit und damit Port 8081.

Ablauf:
 1. scripts/dienst-einrichten.sh zweimal: «geschrieben», dann «unverändert»; Zustand «static»
    (kein Start bei der Anmeldung) und «inactive».
 2. dss-app starten: Das Fenster startet die Einheit und verbindet sich (Zeit gemessen).
 3. --kills mal kill -9 des Kerns: Zeit bis systemd ihn neu gestartet hat (neue MainPID, Zustand
    «active») und bis das Fenster wieder verbunden ist.
 4. kill -9 des Fensters: Der Kern läuft weiter (gleiche MainPID nach 3 s).
 5. Zwei Fenster: Das erste wird normal geschlossen (SIGTERM, derselbe Weg wie von Hand), der Kern
    läuft weiter; das zweite wird geschlossen, der Kern beendet sich, kein Neustart (nach 3 s
    noch «inactive», Result «success», keine neue Startzeile im Journal).
 6. scripts/dienst-einrichten.sh --entfernen zweimal: «entfernt», dann «nicht vorhanden»; systemd
    kennt die Einheit danach nicht mehr.

Zeiten: time.monotonic() im Skript; die Abfrage von systemctl alle 20 ms, das Journal alle 50 ms.
Eigenschaften der Einheit (MainPID, ActiveState, NRestarts, UnitFileState, Result):
https://www.freedesktop.org/software/systemd/man/latest/org.freedesktop.systemd1.html
Startbegrenzung (Vorgabe DefaultStartLimitIntervalSec=10s, DefaultStartLimitBurst=5):
https://www.freedesktop.org/software/systemd/man/latest/systemd-system.conf.html
Zwischen zwei kill -9 wartet das Skript deshalb mindestens 3 s.

  python3 scripts/dienstpruefung.py [--kills 5] [--build build] [--out build/dienstpruefung]
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))
SETUP = os.path.join(ROOT, "scripts", "dienst-einrichten.sh")
MEDIA = os.path.join(ROOT, "prototypes", "media")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"


class Failed(Exception):
    pass


def log(text):
    print(text, flush=True)


def check(condition, text):
    if not condition:
        raise Failed(text)
    log("  ok: " + text)


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def unit_props(unit):
    out = run(["systemctl", "--user", "show", unit,
               "-p", "MainPID,ActiveState,SubState,NRestarts,UnitFileState,Result"]).stdout
    return dict(line.split("=", 1) for line in out.splitlines() if "=" in line)


def wait_for(predicate, timeout_s, step_s=0.02):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        time.sleep(step_s)
    return predicate()


class Journal:
    """Protokoll der Einheit seit Beginn der Prüfung (journalctl --user -u, nur Text)."""

    def __init__(self, unit):
        self.unit = unit
        self.since = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(time.time() - 1))

    def lines(self):
        out = run(["journalctl", "--user", "-u", self.unit, "--since", self.since,
                   "-o", "cat", "--no-pager"]).stdout
        return out.splitlines()

    def count(self, text):
        return sum(1 for line in self.lines() if text in line)

    def wait_count(self, text, n, timeout_s):
        return wait_for(lambda: self.count(text) >= n, timeout_s, 0.05)


class App:
    """dss-app mit eigener Verbindung, Einheit und eigenem XDG_CONFIG_HOME."""

    def __init__(self, binary, socket, unit, config_dir, log_path):
        env = dict(os.environ, XDG_CONFIG_HOME=config_dir)
        self.log_path = log_path
        self.log_file = open(log_path, "w")
        self.proc = subprocess.Popen([binary, "--verbindung", socket, "--einheit", unit],
                                     stdout=self.log_file, stderr=subprocess.STDOUT, env=env)

    def text(self):
        with open(self.log_path) as f:
            return f.read()

    def finish(self, sig, timeout_s=10):
        self.proc.send_signal(sig)
        try:
            rc = self.proc.wait(timeout_s)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            rc = self.proc.wait()
        self.log_file.close()
        return rc


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kills", type=int, default=5)
    ap.add_argument("--build", default="build")
    ap.add_argument("--unit", default="dss-core-pruefung.service")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    build = os.path.join(ROOT, args.build)
    cli = os.path.join(build, "src/cli/dss-cli")
    app_bin = os.path.join(build, "src/app/dss-app")
    unit = args.unit
    if unit in ("dss-core.service", "dss-core-entwicklung.service"):
        sys.exit("FEHLER: die Einheiten der beiden Versionen werden hier nicht geprüft")
    if not os.environ.get("DISPLAY"):
        sys.exit("FEHLER: DISPLAY fehlt, die Prüfung braucht die X11-Sitzung")

    work = tempfile.mkdtemp(prefix="dss-dienstpruefung-")
    run_dir = tempfile.mkdtemp(prefix="dss-", dir=os.environ["XDG_RUNTIME_DIR"])
    data = os.path.join(work, "daten")
    socket = os.path.join(run_dir, "kern.sock")
    config = os.path.join(work, "config")
    os.makedirs(config)
    # P10b, Punkt 83: eigene kern.ini mit eigenem Port; die Adresse im Heimnetz trägt der Kern ein.
    settings = os.path.join(work, "kern.ini")
    with open(settings, "w", encoding="utf-8") as f:
        f.write("[overlay-server]\nport=%d\n" % (32000 + os.getpid() % 768))
    result = {"datum": time.strftime("%Y-%m-%d %H:%M:%S %z"), "einheit": unit,
              "datenordner": data, "verbindung": socket, "kills": []}
    apps = []
    ok = False
    try:
        log("Testdaten (wie im Handtest) mit dss-cli in " + data)
        for cmd in (["overlay-neu", "Überblick Start", "1920", "1080"],
                    ["overlay-neu", "Chat Ecke", "800", "600"],
                    ["quelle-bild", "ueberblick-start", os.path.join(MEDIA, "bild1.png")],
                    ["quelle-video", "ueberblick-start", os.path.join(MEDIA, "alpha.webm"),
                     "--wiederholen"],
                    ["quelle-text", "ueberblick-start", "Willkommen", FONT]):
            r = run([cli, "--datenordner", data] + cmd)
            check(r.returncode == 0, "dss-cli " + " ".join(cmd[:2]))

        log("1. Einrichten (zweimal)")
        setup = [SETUP, "--einheit", unit, "--datenordner", data, "--verbindung", socket,
                 "--einstellungen", settings]
        r1, r2 = run(setup), run(setup)
        check(r1.returncode == 0 and "geschrieben:" in r1.stdout, "erstes Einrichten schreibt die Einheit")
        check(r2.returncode == 0 and "unverändert:" in r2.stdout, "zweites Einrichten ändert nichts")
        verify = run(["systemd-analyze", "--user", "verify", unit])
        check(verify.returncode == 0, "systemd-analyze --user verify " + unit)
        p = unit_props(unit)
        check(p.get("UnitFileState") == "static", "Zustand static (kein Start bei der Anmeldung)")
        check(p.get("ActiveState") == "inactive", "Einheit läuft nicht")
        journal = Journal(unit)

        log("2. Fenster startet den Kern")
        t0 = time.monotonic()
        apps.append(App(app_bin, socket, unit, config, os.path.join(work, "app1.log")))
        check(wait_for(lambda: unit_props(unit).get("ActiveState") == "active", 10),
              "Einheit active nach Start des Fensters")
        t_active = time.monotonic() - t0
        check(journal.wait_count("Verbunden: dss-app", 1, 10), "Fenster verbunden")
        t_connected = time.monotonic() - t0
        result["start_durch_fenster_s"] = {"active": round(t_active, 3),
                                           "verbunden": round(t_connected, 3)}
        log(f"  Zeit bis active {t_active:.3f} s, bis verbunden {t_connected:.3f} s")

        log(f"3. kill -9 des Kerns ({args.kills} mal)")
        connected = journal.count("Verbunden: dss-app")
        for i in range(args.kills):
            p = unit_props(unit)
            pid = int(p["MainPID"])
            restarts = int(p["NRestarts"])
            check(pid > 0, f"Kern läuft (PID {pid})")
            t0 = time.monotonic()
            os.kill(pid, signal.SIGKILL)

            def restarted():
                q = unit_props(unit)
                return q.get("ActiveState") == "active" and int(q.get("MainPID", "0")) not in (0, pid)
            check(wait_for(restarted, 10), f"Kern neu gestartet ({i + 1})")
            t_restart = time.monotonic() - t0
            check(journal.wait_count("Verbunden: dss-app", connected + 1, 10),
                  f"Fenster wieder verbunden ({i + 1})")
            t_reconnect = time.monotonic() - t0
            connected += 1
            new = unit_props(unit)
            check(int(new["NRestarts"]) == restarts + 1, f"NRestarts {restarts} → {new['NRestarts']}")
            result["kills"].append({"alte_pid": pid, "neue_pid": int(new["MainPID"]),
                                    "neustart_s": round(t_restart, 3),
                                    "wieder_verbunden_s": round(t_reconnect, 3)})
            log(f"  Neustart nach {t_restart:.3f} s, Fenster wieder verbunden nach {t_reconnect:.3f} s")
            time.sleep(max(0.0, 3.0 - (time.monotonic() - t0)))
        check(apps[0].proc.poll() is None, "Fenster läuft noch")

        log("4. kill -9 des Fensters: Kern läuft weiter")
        pid = int(unit_props(unit)["MainPID"])
        gone = journal.count("Verbindung getrennt: dss-app")
        apps[0].proc.kill()
        apps[0].proc.wait()
        apps[0].log_file.close()
        check(journal.wait_count("Verbindung getrennt: dss-app", gone + 1, 5), "Kern bemerkt Trennung")
        time.sleep(3)
        p = unit_props(unit)
        check(p["ActiveState"] == "active" and int(p["MainPID"]) == pid,
              f"Kern läuft nach 3 s weiter (PID {pid})")

        log("5. Zwei Fenster, normal schliessen")
        apps.append(App(app_bin, socket, unit, config, os.path.join(work, "app2.log")))
        apps.append(App(app_bin, socket, unit, config, os.path.join(work, "app3.log")))
        check(journal.wait_count("Verbunden: dss-app", connected + 2, 10), "zwei Fenster verbunden")
        # «Verbunden» schreibt der Kern beim hello des Fensters; das hello des Kerns kann beim
        # Fenster dann noch ungelesen sein. Kam SIGTERM davor, schloss das Fenster ohne Bitte
        # zum Beenden (P10b: 55 ms nach «Verbunden», 1 von 1 Lauf). Deshalb warten, bis beide
        # Fenster gezeichnet sind, und dann 1 s.
        check(wait_for(lambda: all("Fenster gezeichnet" in a.text() for a in apps[1:]), 10),
              "beide Fenster gezeichnet")
        time.sleep(1)
        rc = apps[1].finish(signal.SIGTERM)
        check(rc == 0, "erstes Fenster endet mit Exit-Code 0")
        check("Kern läuft weiter: 1 andere Fenster verbunden" in apps[1].text(),
              "erstes Fenster: «Kern läuft weiter: 1 andere Fenster verbunden»")
        time.sleep(1)
        p = unit_props(unit)
        check(p["ActiveState"] == "active" and int(p["MainPID"]) == pid, "Kern läuft weiter")
        starts = journal.count(" gestartet, Qt ")
        t0 = time.monotonic()
        rc = apps[2].finish(signal.SIGTERM)
        check(rc == 0, "zweites Fenster endet mit Exit-Code 0")
        check("Kern beendet sich" in apps[2].text(), "zweites Fenster: «Kern beendet sich»")
        check(wait_for(lambda: unit_props(unit).get("ActiveState") == "inactive", 10),
              "Einheit inactive")
        result["ende_nach_schliessen_s"] = round(time.monotonic() - t0, 3)
        time.sleep(3)
        p = unit_props(unit)
        # NRestarts setzt systemd beim Übergang nach «inactive» zurück; deshalb zählt die
        # Startzeile des Kerns im Journal.
        check(p["ActiveState"] == "inactive" and p.get("Result") == "success"
              and journal.count(" gestartet, Qt ") == starts,
              f"kein Neustart nach 3 s (Result {p.get('Result')}, keine neue Startzeile)")
        check(journal.count("beendet, Exit-Code 0") >= 1, "Kern meldet «beendet, Exit-Code 0»")
        ok = True
    except Failed as e:
        log("FEHLER: " + str(e))
    finally:
        for a in apps:
            if a.proc.poll() is None:
                a.finish(signal.SIGTERM)
        log("6. Entfernen (zweimal)")
        r1 = run([SETUP, "--entfernen", "--einheit", unit])
        r2 = run([SETUP, "--entfernen", "--einheit", unit])
        try:
            check(r1.returncode == 0 and "entfernt:" in r1.stdout, "erstes Entfernen entfernt die Einheit")
            check(r2.returncode == 0 and "nicht vorhanden:" in r2.stdout, "zweites Entfernen: nichts zu tun")
            check(unit_props(unit).get("UnitFileState", "") == "", "systemd kennt die Einheit nicht mehr")
        except Failed as e:
            log("FEHLER: " + str(e))
            ok = False
        result["journal"] = journal.lines() if "journal" in locals() else []
        result["fenster"] = {os.path.basename(a.log_path): a.text().splitlines() for a in apps}
        shutil.rmtree(work, ignore_errors=True)
        shutil.rmtree(run_dir, ignore_errors=True)

    if result["kills"]:
        times = [k["neustart_s"] for k in result["kills"]]
        recon = [k["wieder_verbunden_s"] for k in result["kills"]]
        result["neustart_s"] = {"min": min(times), "max": max(times)}
        result["wieder_verbunden_s"] = {"min": min(recon), "max": max(recon)}
        log(f"Neustart nach kill -9: {min(times):.3f} bis {max(times):.3f} s; "
            f"Fenster wieder verbunden: {min(recon):.3f} bis {max(recon):.3f} s")
    result["ergebnis"] = "bestanden" if ok else "nicht bestanden"
    log("Ergebnis: " + result["ergebnis"])
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        with open(os.path.join(args.out, "dienstpruefung.json"), "w") as f:
            json.dump(result, f, ensure_ascii=False, indent=1)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
