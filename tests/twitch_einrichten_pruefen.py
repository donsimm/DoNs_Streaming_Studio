#!/usr/bin/env python3
"""Prüft scripts/twitch-einrichten.sh (P13, Commit 9) ohne Kontakt zu Twitch und ohne Browser.

Aufruf: twitch_einrichten_pruefen.py SKRIPT DSS_CORE DSS_CLI

- bash -n auf das Skript;
- ohne laufenden Kern: Meldung «Zuerst das Studio öffnen.», Exit-Code 1;
- mit Testkern (eigener Datenordner, eigene Verbindung, eigenes XDG_CONFIG_HOME): gültige
  Client-ID wird eingetragen und mit «dss-cli einstellungen» zurückgelesen;
- ungültige Client-IDs werden abgewiesen, die eingetragene bleibt.

Das Skript läuft immer mit --ohne-browser und --client-id (nie xdg-open, nie eine Eingabe).
"""
import os
import re
import subprocess
import sys
import tempfile
import time

failures = []


def check(condition, text):
    print(("ok: " if condition else "FEHLER: ") + text)
    if not condition:
        failures.append(text)


def main():
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    script, core, cli = sys.argv[1:]

    result = subprocess.run(["bash", "-n", script], capture_output=True, text=True)
    check(result.returncode == 0, "bash -n " + result.stderr.strip())

    with tempfile.TemporaryDirectory(prefix="tst_twitcheinrichten.") as tmp:
        env = dict(os.environ)
        env["XDG_CONFIG_HOME"] = os.path.join(tmp, "config")
        env["XDG_DATA_HOME"] = os.path.join(tmp, "data")
        env["XDG_RUNTIME_DIR"] = os.path.join(tmp, "run")
        for d in ("config", "data", "run", "daten"):
            os.makedirs(os.path.join(tmp, d), mode=0o700)
        socket = os.path.join(tmp, "kern.sock")
        cli_args = [cli, "--verbindung", socket]

        def run_script(client_id):
            return subprocess.run([script, "--ohne-browser", "--cli", cli, "--verbindung", socket,
                                   "--client-id", client_id],
                                  env=env, capture_output=True, text=True, stdin=subprocess.DEVNULL,
                                  timeout=30)

        def client_id_from_cli():
            r = subprocess.run(cli_args + ["einstellungen"], env=env, capture_output=True, text=True,
                               timeout=30)
            m = re.search(r"^Twitch-Client-ID +(\S+)", r.stdout, re.MULTILINE)
            return r, (m.group(1) if m else None)

        # Ohne Kern
        r = run_script("abc123")
        check(r.returncode == 1 and "Zuerst das Studio öffnen." in r.stderr,
              "ohne Kern: «Zuerst das Studio öffnen.», Exit-Code %d" % r.returncode)
        check("Das ist kein Passwort und kein Token." in r.stdout, "Hinweis zur Client-ID")
        check("Das Secret ist ein Passwort. Nicht in den Chat und nicht in eine Datei kopieren."
              in r.stdout, "Hinweis zum Secret")

        # Testkern mit eigenem Port (wie nextTestPort in tests/prozesshilfe.h, nie 8080 oder 8081)
        port = 24000 + (os.getpid() % 2000) * 4
        log_path = os.path.join(tmp, "kern.log")
        with open(log_path, "w") as log:
            proc = subprocess.Popen([core, "--datenordner", os.path.join(tmp, "daten"), "--verbindung",
                                     socket, "--standardport", str(port)],
                                    env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            ready = False
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and proc.poll() is None:
                r, _ = client_id_from_cli()
                if r.returncode == 0 and re.search(r"^Quelle +Kern$", r.stdout, re.MULTILINE):
                    ready = True
                    break
                time.sleep(0.1)
            check(ready, "Testkern bereit")
            if ready:
                r = run_script("  0123456789abcdefghijklmnopqrst ")
                check(r.returncode == 0, "gültige Client-ID: Exit-Code %d %s" % (r.returncode, r.stderr.strip()))
                check("Eingetragen: 0123456789abcdefghijklmnopqrst" in r.stdout, "Ausgabe «Eingetragen»")
                check("/mod <Botname>" in r.stdout, "nächste Schritte genannt")
                _, value = client_id_from_cli()
                check(value == "0123456789abcdefghijklmnopqrst", "zurückgelesen: %s" % value)

                for bad in ("", "ABCdef123", "abc-123", "abc def", "a" * 65):
                    r = run_script(bad)
                    check(r.returncode == 1 and "Die Client-ID ist ungültig" in r.stderr,
                          "ungültig abgewiesen: %r, Exit-Code %d" % (bad, r.returncode))
                _, value = client_id_from_cli()
                check(value == "0123456789abcdefghijklmnopqrst", "nach ungültigen unverändert: %s" % value)

                r = run_script("a" * 64)
                check(r.returncode == 0, "64 Zeichen erlaubt: Exit-Code %d" % r.returncode)
        finally:
            proc.terminate()
            try:
                code = proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                code = proc.wait()
            check(code == 0, "Testkern sauber beendet, Exit-Code %d" % code)
            if failures:
                with open(log_path) as log:
                    print("Protokoll des Kerns:\n" + log.read())

    print("%d Fehler" % len(failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
