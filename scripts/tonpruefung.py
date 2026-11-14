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

Gemeinsame Teile (Senke, Aufnahme, Seite, Kern, Aufräumen) in scripts/tonwerkzeug.py, dort auch die
Quellen.
"""
import argparse
import os
import statistics
import sys
import time

from tonwerkzeug import (ROOT, RATE, BEEP_PERIOD_NS, Abbruch, Log, Page, check_safe, check_loopback,
                         check_tools, cleanup, destroy_leftovers, find_onsets, free_port, ignore_signals,
                         make_workdir, now_ns, number_after, pct, pw_state, raise_on_signals, sleep_until,
                         start_core, start_loopback, start_recorder, stop_proc)

VIDEO = os.path.join(ROOT, "tests", "daten", "ton", "blitz-piep.mp4")
PREFIX = "dss_tonpruefung"
SINK = PREFIX + "_ausgabe"
SOURCE = PREFIX + "_aufnahme"
PAGE = "tonpruefung01"
MATCH_NS = 300_000_000
STOP_AFTER_NS = 500_000_000
BEEP_LEN_MS = (30.0, 36.0)
SPREAD_P5_P95_MS = 15.0
SPREAD_MAX_MIN_MS = 40.0


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
    raise_on_signals()

    tmp = None
    loop = recorder_proc = None
    core = page = recorder = None
    default_before = None
    rc = 2
    result = []
    try:
        core_bin, cli_bin = check_tools(args.build)
        if not os.path.isfile(VIDEO):
            raise Abbruch("%s fehlt" % VIDEO)
        log("Tonprüfung: %d Läufe zu %d s, Ausgabe %s" % (args.laeufe, args.dauer_s, out))
        log("Last (1/5/15 min): %s" % " ".join(open("/proc/loadavg").read().split()[:3]))
        destroy_leftovers(log, PREFIX)
        _, _, default_before = pw_state()
        log("Vorher: %s" % default_before)

        # Eigene Ordner, nie die echten
        port = free_port()
        tmp, env, ini, data, overlay, base = make_workdir("dss-tonpruefung", cli_bin, "Tonpruefung",
                                                          [(VIDEO, False)], port, SINK)
        video = overlay["sources"][0]
        log("Overlay «%s» mit «%s» (%s), Lautstärke %s, stumm %s"
            % (overlay["name"], video["name"], video["video"]["file"], video["video"].get("volume"),
               video["video"].get("muted")))

        # Virtuelle Senke
        loop = start_loopback(log, PREFIX, out, "DSS Tonprüfung")
        check_loopback(log, loop, PREFIX, default_before)

        # Kern
        core = start_core(core_bin, data, tmp, ini, port, env, os.path.join(out, "kern.log"))
        if core.wait_for("Ton (ton-video): Zustand playing", 10) < 0:
            raise Abbruch("Ausgang des Kerns nicht «playing» (kern.log)")
        log("Kern läuft (PID %d), Port %d, Ausgang %s" % (core.proc.pid, port, SINK))

        # Aufnahme
        recorder_proc, recorder = start_recorder(log, SOURCE, out, PREFIX, default_before)

        page = Page(port, base, PAGE)
        source_id, file = video["id"], video["video"]["file"]
        time.sleep(1.0)  # Vorrat des Decoders, sobald die Seite verbunden ist
        runs = []
        line_pos = 0
        for run in range(1, args.laeufe + 1):
            check_safe(PREFIX, default_before, recorder_proc.pid)
            t0 = now_ns()
            page.send("start", run, source_id, file, 0.0, t0 / 1e6)
            for i in range(1, args.dauer_s + 1):
                sleep_until(t0 + i * 1_000_000_000)
                page.send("frame", run, source_id, file, float(i), (t0 + i * 1_000_000_000) / 1e6)
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
        ignore_signals()
        if page:
            page.close()
        stop_proc(recorder_proc)
        if core:
            core.stop()
        stop_proc(loop)
        time.sleep(1)
        clean = cleanup(log, PREFIX, default_before,
                        ((loop, "pw-loopback"), (recorder_proc, "pw-cat"), (core.proc if core else None, "dss-core")),
                        tmp)
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
