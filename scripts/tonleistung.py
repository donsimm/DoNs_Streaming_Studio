#!/usr/bin/env python3
"""Leistung des Kerns mit Ton der Videos (P12b, Prüfstufe «Baustein», Plan docs/archiv/prompts/P12-ton.md):
CPU-Mehrlast des Kerns durch den Ton von 3 Videos und Aussetzer über eine lange Dauer, unhörbar über
eine virtuelle Senke. Ziel aus dem Plan: höchstens 3 Prozentpunkte eines Kerns mehr CPU, 0 Aussetzer
in 30 min.

Ablauf (Senke, Aufnahme, Seite, Kern und Aufräumen aus scripts/tonwerkzeug.py, wie tonpruefung.py):
- Virtuelle Senke «dss_tonleistung_ausgabe» → Quelle «dss_tonleistung_aufnahme» (pw-loopback), läuft
  in beiden Durchgängen; Sicherung «nichts an ein echtes Gerät» beim Start und jede Minute.
- Eigener Temp-Ordner, Overlay mit 3 Videos «Wiederholen» (tests/daten/ton/: blitz-piep.mp4 AAC
  48 kHz, blitz-piep.webm Opus, blitz-piep-44100.mp4 AAC 44,1 kHz; je 60 s, Piep alle 2 s).
- Zwei Durchgänge mit demselben Overlay, je ein eigener dss-core aus build/:
    A: kern.ini «ausgang=» leer («Kein Ausgang»): Der Kern nimmt die Meldungen an, legt die Stimmen
       an (Decoder mit Vorrat), mischt aber nichts und gibt nichts aus.
    B: Ausgang auf die virtuelle Senke: Dekodieren, Mischen, Ausgabe über PipeWire.
- Seite in OBS nachgebildet: …/events/<page> offen; die Videos starten um 0,6 s versetzt (ihre Piepe
  liegen getrennt in der Aufnahme); je Video jede Sekunde «frame», nach 60 s und der Pause beim
  Wiederholen aus M2 (58,6 ms) «loop» (Durchgang + 1, Lage 0), wie web/overlay.js beim Wiederholen.
  Am Ende «stop».
- CPU: nach 10 s Anlauf --dauer-min lang dieselben Befehle wie T-02 (scripts/leistung.py), nur für
  den Kern:
    LC_ALL=C pidstat -h -u -r -p <dss-core> 1 <s>
    LC_ALL=C mpstat 1 <s>
  Mehrlast = Mittel %CPU B − Mittel %CPU A (Prozentpunkte eines Kerns).
- Aussetzer: aus dem Protokoll des Kerns (Abrufe ohne Puffer, abgebrochene Zyklen, Sprünge nach dem
  Start laut «Ton: Stopp», «3 s ohne Meldung») und aus der Aufnahme (pw-cat): jeder Piep ab k = 1
  genau einmal, kein zusätzlicher Einsatz, Länge 30 bis 36 ms. In A darf die Aufnahme keinen
  Einsatz enthalten. Lücken in stillen Teilen sieht die Aufnahme nicht (Leere des Rings zählt der
  Mischer, das Protokoll nennt sie nicht).

Bestanden: Mehrlast ≤ 3,0 Prozentpunkte; B ohne Aussetzer (alle Punkte oben = 0, alle Piepe da);
A ohne Einsatz; in beiden: 3 Starts, 3 Stopps «Seite inaktiv», jede Meldung mit 204, Kern Exit-Code 0.

  python3 scripts/leistung.py --ton [--dauer-min 30] [--build build] [--out DIR]
  (gleichwertig: python3 scripts/tonleistung.py [--dauer-min 30] …)

Exit-Code: 0 bestanden, 1 nicht bestanden, 2 Abbruch, 3 Aufräumen nicht vollständig.
Ergebnis in --out (Standard build/tonleistung/<Datum-Zeit>): ergebnis.txt, ergebnis.json, pw.log,
je Durchgang A/ und B/: kern.log, pidstat.txt, mpstat.txt, einsaetze.csv. Keine Fenster, kein dss-app.

Quellen: pidstat(1), mpstat(1): https://sysstat.github.io/ (sysstat 12.7.5); PipeWire und SSE in
scripts/tonwerkzeug.py.
"""
import argparse
import bisect
import json
import os
import statistics
import subprocess
import sys
import time

from tonwerkzeug import (ROOT, RATE, BEEP_PERIOD_NS, Abbruch, Log, Page, check_safe, check_loopback,
                         check_tools, cleanup, destroy_leftovers, find_onsets, free_port, ignore_signals,
                         make_workdir, now_ns, number_after, pct, pw_state, raise_on_signals, sleep_until,
                         start_core, start_loopback, start_recorder, stop_proc, write_ini)

sys.path.insert(0, os.path.join(ROOT, "prototypes", "messung"))
import t02  # noqa: E402  (stat, parse_pidstat, parse_mpstat)

TON = os.path.join(ROOT, "tests", "daten", "ton")
VIDEOS = [os.path.join(TON, n) for n in ("blitz-piep.mp4", "blitz-piep.webm", "blitz-piep-44100.mp4")]
PREFIX = "dss_tonleistung"
SINK = PREFIX + "_ausgabe"
SOURCE = PREFIX + "_aufnahme"
OFFSET_NS = 600_000_000      # Versatz der Videos untereinander
WARMUP_S = 10
SAFE_EVERY_S = 60
VIDEO_S = 60                 # Länge der Testvideos
# Pause des Videos beim Wiederholen in der Seite: Median aus M2, wie Mixer::kLoopGapStartNs
# (src/audio/mixer.h). Ohne Pause misst der Mischer beim ersten Wiederholen 58,6 ms Abweichung und
# springt (Probelauf 2026-11-14), was eine echte Seite nicht tut.
LOOP_GAP_NS = 58_600_000
PERIOD_NS = VIDEO_S * 1_000_000_000 + LOOP_GAP_NS
MATCH_NS = 250_000_000
BEEP_LEN_MS = (30.0, 36.0)
LIMIT_PP = 3.0


def run_pass(log, name, output, ctx, minutes):
    """Ein Durchgang mit eigenem Kern; gibt die Rohwerte für die Auswertung zurück."""
    out = os.path.join(ctx["out"], name)
    os.makedirs(out, exist_ok=True)
    write_ini(ctx["ini"], ctx["port"], output)
    p = {"name": name, "output": output or "(kein Ausgang)"}
    core = recorder_proc = recorder = page = None
    runners = {}
    try:
        core = start_core(ctx["core_bin"], ctx["data"], ctx["tmp"], ctx["ini"], ctx["port"], ctx["env"],
                          os.path.join(out, "kern.log"))
        ctx["core"] = core
        ctx["procs"].append((core.proc, "dss-core"))
        if output:
            if core.wait_for("Ton (ton-video): Zustand playing", 10) < 0:
                raise Abbruch("Durchgang %s: Ausgang des Kerns nicht «playing» (kern.log)" % name)
            state = "playing"
        else:
            # Ohne Ausgang meldet der Kern «no_target» oder gar keinen Wechsel; nie «playing»
            time.sleep(2)
            with core.lock:
                states = [l[l.find("Zustand"):] for l in core.lines if "Ton (ton-video): Zustand" in l]
            if any("playing" in s or "connecting" in s for s in states):
                raise Abbruch("Durchgang %s: Kern ohne Ausgang meldet %s" % (name, states))
            state = ", ".join(states) or "keine Zeile «Zustand»"
        log("Durchgang %s: Kern läuft (PID %d), Port %d, Ausgang %s, %s"
            % (name, core.proc.pid, ctx["port"], p["output"], state))
        p["with_output"] = bool(output)

        recorder_proc, recorder = start_recorder(log, SOURCE, out, PREFIX, ctx["default_before"])
        ctx["recorder_proc"] = recorder_proc
        ctx["procs"].append((recorder_proc, "pw-cat"))
        page =Page(ctx["port"], ctx["base"], "tonleistung" + name.lower())
        ctx["page"] = page
        time.sleep(1.0)  # Vorrat der Decoder, sobald die Seite verbunden ist

        duration_s = int(round(minutes * 60))
        end_s = WARMUP_S + duration_s + 1
        t0 = now_ns() + 200_000_000
        starts = [t0 + n * OFFSET_NS for n in range(len(ctx["videos"]))]
        # Zeitplan: (Zeit, Art, Video, Lage in s, Durchgang); Durchgang n beginnt bei
        # Start + n · (Länge + Pause beim Wiederholen)
        plan = []
        for v, ts in enumerate(starts):
            n = 0
            while ts + n * PERIOD_NS <= ts + end_s * 1_000_000_000:
                for m in range(VIDEO_S):
                    t = ts + n * PERIOD_NS + m * 1_000_000_000
                    if t > ts + end_s * 1_000_000_000:
                        break
                    kind = "start" if n == 0 and m == 0 else "loop" if m == 0 else "frame"
                    plan.append((t, kind, v, m, n))
                n += 1
        for s in range(SAFE_EVERY_S, end_s, SAFE_EVERY_S):
            plan.append((t0 + s * 1_000_000_000 + 300_000_000, "safe", None, s, 0))
        plan.append((t0 + WARMUP_S * 1_000_000_000, "measure", None, 0, 0))
        plan.sort(key=lambda e: e[0])
        env = dict(os.environ, LC_ALL="C")
        cmds = {"pidstat": ["pidstat", "-h", "-u", "-r", "-p", str(core.proc.pid), "1", str(duration_s)],
                "mpstat": ["mpstat", "1", str(duration_s)]}
        last_log = t0
        for t, kind, v, s, n in plan:
            sleep_until(t)
            if kind == "measure":
                # Ausgabe direkt in Dateien: 1800 Zeilen passen nicht in den Puffer einer Pipe, die erst
                # am Ende gelesen wird (pidstat bliebe beim Schreiben hängen)
                runners = {k: subprocess.Popen(c, stdout=open(os.path.join(out, "%s.txt" % k), "w"), env=env)
                           for k, c in cmds.items()}
                p["measure_start"] = time.strftime("%F %T")
                log("Durchgang %s: Messung %d s beginnt" % (name, duration_s))
                continue
            if kind == "safe":
                check_safe(PREFIX, ctx["default_before"], recorder_proc.pid)
                if core.proc.poll() is not None:
                    raise Abbruch("Durchgang %s: Kern beendet (Exit-Code %s)" % (name, core.proc.returncode))
                if t - last_log >= 300_000_000_000:
                    log("Durchgang %s: %d s, Meldungen %d" % (name, s, len(page.codes)))
                    last_log = t
                continue
            src, file = ctx["videos"][v]
            page.send(kind, 1, src, file, float(s), t / 1e6, n, float(VIDEO_S))
        for k, r in runners.items():
            try:
                r.wait(60)
            except subprocess.TimeoutExpired:
                raise Abbruch("Durchgang %s: %s endet nicht" % (name, k))
        outputs = {}
        for k in runners:
            with open(os.path.join(out, "%s.txt" % k)) as f:
                outputs[k] = f.read()
        runners = {}
        sleep_until(starts[-1] + end_s * 1_000_000_000 + 500_000_000)
        page.send("stop", 1)
        stop_at = now_ns()
        n_stop = 0
        pos = 0
        while n_stop < len(ctx["videos"]):
            i = core.wait_for("Seite inaktiv (gespielt", 3, pos)
            if i < 0:
                break
            n_stop += 1
            pos = i + 1
        time.sleep(0.5)
        stop_proc(recorder_proc)
        recorder.thread.join(10)
        page.close()
        ctx["page"] = None
        core_rc = core.stop()
        ctx["core"] = None
        with core.lock:
            lines = list(core.lines)
        p.update({"commands": {k: " ".join(c) for k, c in cmds.items()}, "outputs": outputs, "lines": lines,
                  "core_rc": core_rc, "codes": list(page.codes), "starts": starts, "stop_at": stop_at,
                  "recorder": recorder, "out": out})
        return p
    finally:
        for r in runners.values():
            stop_proc(r)
        if page:
            page.close()
            ctx["page"] = None
        stop_proc(recorder_proc)
        ctx["recorder_proc"] = None
        if core:
            core.stop()
            ctx["core"] = None


def evaluate(p, n_videos):
    """Kennzahlen eines Durchgangs."""
    per_t, per_cmd = t02.parse_pidstat(p["outputs"]["pidstat"])
    cpu = [s["cpu"] for s in per_t.values()]
    lines = p["lines"]
    stop_lines = [l for l in lines if "Seite inaktiv (gespielt" in l]
    lag_devs = [v for v in (number_after(l, "Abweichung") for l in lines if "Ton: Lage " in l) if v is not None]
    e = {
        "durchgang": p["name"],
        "ausgang": p["output"],
        "messung_ab": p.get("measure_start"),
        "befehle": p["commands"],
        "cpu_kern_prozent_eines_kerns": {**(t02.stat(cpu) or {}), "median": round(statistics.median(cpu), 1) if cpu else None,
                                         "p95": round(pct(cpu, 95), 1) if cpu else None, "sekunden": len(cpu),
                                         "mittel_genau": round(statistics.fmean(cpu), 3) if cpu else None},
        "rss_kern_mib": t02.stat([s["rss"] / 1024 for s in per_t.values()]),
        "cpu_gesamt_prozent": t02.stat(t02.parse_mpstat(p["outputs"]["mpstat"])),
        "meldungen": len(p["codes"]),
        "meldungen_nicht_204": sum(1 for c in p["codes"] if c != 204),
        "starts": sum(1 for l in lines if "Ton: Start " in l),
        "stopps_seite_inaktiv": len(stop_lines),
        "ohne_meldung_3s": sum(1 for l in lines if "3 s ohne Meldung" in l),
        "nachfuehren_zeilen": sum(1 for l in lines if "Ton: Nachführen" in l),
        "abrufe_ohne_puffer_zeilen": sum(1 for l in lines if "Abrufe ohne Puffer" in l),
        "abgebrochene_zyklen_zeilen": sum(1 for l in lines if "abgebrochene Zyklen" in l),
        "spruenge_nach_start": sum(number_after(l, "Sprünge") or 0 for l in stop_lines),
        "weich": sum(number_after(l, "weich") or 0 for l in stop_lines),
        "wiederholungen": [number_after(l, "Wiederholungen") for l in stop_lines],
        "gespielt_s": [number_after(l, "gespielt") for l in stop_lines],
        "lage_zeilen": len(lag_devs),
        "lage_abweichung_ms": {"min": min(lag_devs), "max": max(lag_devs)} if lag_devs else None,
        "abruf_zeilen": sorted({l[l.find("Abruf:"):] for l in lines if "Abruf: delay" in l}),
        "kern_exit_code": p["core_rc"],
        "kern_sauber": p["core_rc"] == 0 and any("beendet, Exit-Code 0" in l for l in lines),
    }
    # Aufnahme: erwartete Piepe je Video bei Start + n · (Länge + Pause) + 2 s · k bis 0,3 s vor dem
    # Stopp, ohne den ersten (vor der ersten Meldung); Nummer = 30 · n + k
    rec = p["recorder"]
    onsets = find_onsets(rec.samples)
    expected = []
    per_pass = VIDEO_S * 1_000_000_000 // BEEP_PERIOD_NS
    for v, ts in enumerate(p["starts"] if p["with_output"] else []):
        i = 1
        while True:
            t = ts + (i // per_pass) * PERIOD_NS + (i % per_pass) * BEEP_PERIOD_NS
            if t >= p["stop_at"] - 300_000_000:
                break
            expected.append((t, v, i))
            i += 1
    expected.sort()
    times = [x[0] for x in expected]
    found, extra, rows, edge = {}, [], [], 0
    t_first = min(p["starts"])
    for start, length, peak in onsets:
        t = rec.time_ns(start)
        if t is None:
            extra.append((None, length))
            continue
        i = bisect.bisect_left(times, t)
        best = min((j for j in (i - 1, i) if 0 <= j < len(times)), key=lambda j: abs(times[j] - t), default=None)
        if best is not None and abs(times[best] - t) <= MATCH_NS and best not in found:
            found[best] = True
            rows.append((expected[best][1], expected[best][2], (t - times[best]) / 1e6, length, peak))
        elif (any(abs(t - ts) <= MATCH_NS for ts in p["starts"]) or t < t_first - MATCH_NS
              or t >= p["stop_at"] - 300_000_000):
            # Piep bei 0 (vor der ersten Meldung) oder beim Stopp (der Kern schreibt etwa 40 ms
            # voraus, wie tonpruefung.py): nicht gewertet
            edge += 1
        else:
            extra.append(((t - t_first) / 1e9, length))
    with open(os.path.join(p["out"], "einsaetze.csv"), "w") as f:
        f.write("video,piep,abweichung_ms,laenge_ms,spitze\n")
        for row in sorted(rows):
            f.write("%d,%d,%.3f,%.2f,%.3f\n" % row)
    devs = [r[2] for r in rows]
    lengths = [r[3] for r in rows]
    e["aufnahme_s"] = round(len(rec.samples) / RATE, 1)
    e["einsaetze"] = len(onsets)
    e["piepe_erwartet"] = len(expected)
    e["piepe_gefunden"] = len(rows)
    e["piepe_fehlen"] = len(expected) - len(rows)
    e["einsaetze_zusaetzlich"] = len(extra)
    e["zusaetzlich_s_laenge_ms"] = [(round(t, 3) if t is not None else None, round(l, 2)) for t, l in extra[:20]]
    e["einsaetze_am_rand"] = edge
    if devs:
        e["abweichung_ms"] = {"median": round(statistics.median(devs), 2), "p5": round(pct(devs, 5), 2),
                              "p95": round(pct(devs, 95), 2), "min": round(min(devs), 2), "max": round(max(devs), 2)}
        e["laenge_ms"] = {"min": round(min(lengths), 2), "max": round(max(lengths), 2)}
        e["laenge_ausserhalb"] = sum(1 for l in lengths if not BEEP_LEN_MS[0] <= l <= BEEP_LEN_MS[1])
    e["grundbedingungen"] = (e["meldungen_nicht_204"] == 0 and e["starts"] == n_videos
                             and e["stopps_seite_inaktiv"] == n_videos and e["kern_sauber"])
    return e


def main(argv=None):
    ap = argparse.ArgumentParser(prog="leistung.py --ton", description=__doc__.split("\n\n")[0])
    ap.add_argument("--dauer-min", type=float, default=30.0, help="Messdauer je Durchgang in min (Vorgabe 30)")
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("--out", default=None)
    args = ap.parse_args(argv)
    if args.dauer_min < 1 or args.dauer_min > 120:
        ap.error("--dauer-min 1 bis 120")

    out = os.path.realpath(args.out or os.path.join(ROOT, "build", "tonleistung", time.strftime("%Y-%m-%d-%H%M%S")))
    os.makedirs(out, exist_ok=True)
    log = Log(os.path.join(out, "pw.log"))
    raise_on_signals()

    ctx = {"out": out, "core": None, "recorder_proc": None, "page": None, "default_before": None, "procs": []}
    tmp = loop = None
    rc = 2
    result = []
    summary = {}
    try:
        ctx["core_bin"], cli_bin = check_tools(os.path.realpath(args.build))
        for v in VIDEOS:
            if not os.path.isfile(v):
                raise Abbruch("%s fehlt" % v)
        log("Tonleistung: 2 Durchgänge (A ohne Ausgang, B mit Senke) zu %.1f min, Ausgabe %s" % (args.dauer_min, out))
        log("Last (1/5/15 min): %s" % " ".join(open("/proc/loadavg").read().split()[:3]))
        destroy_leftovers(log, PREFIX)
        _, _, ctx["default_before"] = pw_state()
        log("Vorher: %s" % ctx["default_before"])

        ctx["port"] = free_port()
        tmp, ctx["env"], ctx["ini"], ctx["data"], overlay, ctx["base"] = make_workdir(
            "dss-tonleistung", cli_bin, "Tonleistung", [(v, True) for v in VIDEOS], ctx["port"], "")
        ctx["tmp"] = tmp
        ctx["videos"] = [(s["id"], s["video"]["file"]) for s in overlay["sources"]]
        for s in overlay["sources"]:
            log("Quelle «%s» (%s), Wiederholen %s, Lautstärke %s, stumm %s"
                % (s["name"], s["video"]["file"], s["video"].get("loop"), s["video"].get("volume"),
                   s["video"].get("muted")))

        loop = start_loopback(log, PREFIX, out, "DSS Tonleistung")
        check_loopback(log, loop, PREFIX, ctx["default_before"])

        passes = [run_pass(log, "A", "", ctx, args.dauer_min)]
        time.sleep(3)
        passes.append(run_pass(log, "B", SINK, ctx, args.dauer_min))

        ev = [evaluate(p, len(VIDEOS)) for p in passes]
        a, b = ev
        extra_pp = b["cpu_kern_prozent_eines_kerns"]["mittel_genau"] - a["cpu_kern_prozent_eines_kerns"]["mittel_genau"]
        dropouts_b = (b["abrufe_ohne_puffer_zeilen"] + b["abgebrochene_zyklen_zeilen"] + b["spruenge_nach_start"]
                      + b["ohne_meldung_3s"] + b["piepe_fehlen"] + b["einsaetze_zusaetzlich"]
                      + b.get("laenge_ausserhalb", 0))
        ok_cpu = extra_pp <= LIMIT_PP
        ok_b = dropouts_b == 0 and b["piepe_gefunden"] > 0 and b["grundbedingungen"]
        ok_a = a["einsaetze"] == 0 and a["grundbedingungen"]
        ok = ok_cpu and ok_b and ok_a
        summary = {"aufruf": "python3 scripts/leistung.py --ton --dauer-min %g" % args.dauer_min,
                   "videos": [os.path.relpath(v, ROOT) for v in VIDEOS], "durchgaenge": ev,
                   "mehrlast_prozentpunkte": round(extra_pp, 2), "grenze_prozentpunkte": LIMIT_PP,
                   "aussetzer_b": dropouts_b, "bestanden": ok}
        with open(os.path.join(out, "ergebnis.json"), "w") as f:
            json.dump(summary, f, indent=1, ensure_ascii=False)

        res = result
        res.append("Tonleistung P12b, %s" % time.strftime("%F %T"))
        res.append("Aufruf: %s" % summary["aufruf"])
        res.append("Videos (Wiederholen, um 0,6 s versetzt): %s" % ", ".join(summary["videos"]))
        for e in ev:
            c = e["cpu_kern_prozent_eines_kerns"]
            res.append("Durchgang %s (%s), Messung ab %s, %d s:" % (e["durchgang"], e["ausgang"], e["messung_ab"], c["sekunden"]))
            res.append("  CPU dss-core %% eines Kerns: Mittel %.2f, Median %s, p95 %s, min %s, max %s; RSS max %s MiB; "
                       "CPU gesamt Mittel %s %%"
                       % (c["mittel_genau"], c["median"], c["p95"], c["min"], c["max"], e["rss_kern_mib"]["max"],
                          e["cpu_gesamt_prozent"]["mittel"]))
            res.append("  Meldungen %d (nicht 204: %d), Starts %d, Stopps «Seite inaktiv» %d, «3 s ohne Meldung» %d"
                       % (e["meldungen"], e["meldungen_nicht_204"], e["starts"], e["stopps_seite_inaktiv"],
                          e["ohne_meldung_3s"]))
            res.append("  Abrufe ohne Puffer %d, abgebrochene Zyklen %d, Sprünge nach dem Start %d, weich %d, "
                       "«Nachführen» %d Zeilen; Wiederholungen %s; gespielt %s s"
                       % (e["abrufe_ohne_puffer_zeilen"], e["abgebrochene_zyklen_zeilen"], e["spruenge_nach_start"],
                          e["weich"], e["nachfuehren_zeilen"], e["wiederholungen"], e["gespielt_s"]))
            res.append("  «Ton: Lage» %d Zeilen, Abweichung %s; %s"
                       % (e["lage_zeilen"], e["lage_abweichung_ms"], "; ".join(e["abruf_zeilen"]) or "keine Abruf-Zeile"))
            res.append("  Aufnahme %.1f s: Einsätze %d (am Start oder Stopp, nicht gewertet: %d), Piepe erwartet %d, "
                       "gefunden %d, fehlen %d, zusätzlich %d%s"
                       % (e["aufnahme_s"], e["einsaetze"], e["einsaetze_am_rand"], e["piepe_erwartet"],
                          e["piepe_gefunden"], e["piepe_fehlen"], e["einsaetze_zusaetzlich"],
                          ", Abweichung %s ms, Länge %s ms (ausserhalb %d)" % (e["abweichung_ms"], e["laenge_ms"],
                                                                              e["laenge_ausserhalb"])
                          if "abweichung_ms" in e else ""))
            if e["zusaetzlich_s_laenge_ms"]:
                res.append("  zusätzlich (s nach dem ersten Start, Länge ms): %s" % e["zusaetzlich_s_laenge_ms"])
            res.append("  Kern Exit-Code %s, sauber %s" % (e["kern_exit_code"], "ja" if e["kern_sauber"] else "NEIN"))
        res.append("Mehrlast B − A: %.2f Prozentpunkte eines Kerns (Grenze %.1f): %s"
                   % (extra_pp, LIMIT_PP, "ja" if ok_cpu else "NEIN"))
        res.append("Aussetzer in B: %d (%s); A ohne Ton: %s" % (dropouts_b, "ja" if ok_b else "NEIN",
                                                              "ja" if ok_a else "NEIN"))
        rc = 0 if ok else 1
        res.append("Ergebnis: %s" % ("bestanden" if ok else "NICHT bestanden"))
    except (Abbruch, KeyboardInterrupt) as e:
        log("ABBRUCH: %s" % (str(e) or "SIGINT"))
        result.append("ABBRUCH: %s" % (str(e) or "SIGINT"))
        rc = 2
    finally:
        ignore_signals()
        if ctx.get("page"):
            ctx["page"].close()
        stop_proc(ctx.get("recorder_proc"))
        core = ctx.get("core")
        if core:
            core.stop()
        stop_proc(loop)
        time.sleep(1)
        clean = cleanup(log, PREFIX, ctx["default_before"],
                        [(loop, "pw-loopback")] + ctx["procs"], tmp)
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
