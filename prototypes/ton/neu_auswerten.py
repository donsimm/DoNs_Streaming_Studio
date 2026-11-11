#!/usr/bin/env python3
"""P12a Z0: Klickläufe von M1 aus den CSV-Dateien neu auswerten, Zuordnung nach der Zeit.

Die alte Auswertung (klickmessung.cpp:333-348) ordnet einen Einsatz in der Aufnahme dem Klick
round((Lage − Lage des ersten Einsatzes) / 24000) zu. Verliert die Aufnahme mehr als 12 000 Proben,
landet der Einsatz beim vorigen Klick (Befund 2 im Messbericht P12a). Dieses Skript ordnet nach der
Zeit: Ein Einsatz gehört zu dem Klick, dessen Sendezeit plus Bezugsverzögerung am nächsten liegt.

Fenster: Bezugsverzögerung ± 250 ms. Die Bezugsverzögerung ist der Median der rohen Verzögerung
der ersten 10 Klicks (alte Zuordnung; dort ist der aufgelaufene Probenverlust weit unter 12 000
Proben, das Skript prüft das). 250 ms ist der halbe Klickabstand: Breiter darf das Fenster nicht
sein, sonst passt ein Einsatz zu zwei Klicks; schmaler muss es nicht sein, weil die rohe
Verzögerung in Ruhe nur 5 bis 105 ms streut. Wie weit die Einsätze vom Rand bleiben, steht in der
Ausgabe («Abstand zur Bezugsverzögerung»).

Einsätze, die die alte Auswertung als «fremd» verworfen hat, stehen nicht in der CSV, nur ihre
Anzahl in der Zusammenfassung. Die Klicks dazu bleiben hier ohne Einsatz; das Skript prüft, ob sie
genau an den Stellen liegen, an denen die alte Zuordnung um einen Klick verrutscht.

  python3 prototypes/ton/neu_auswerten.py <Datenordner> [<Datenordner> ...]

Schreibt je Ordner m1-last-<puffer>-neu.txt für die Läufe unter Last und m1-gegenprobe-neu.txt
(alle Klickläufe: neue gegen alte Kennzahlen). Nur Standardbibliothek.
"""
import argparse
import bisect
import csv
import math
import re
import sys
from pathlib import Path

RATE = 48000
CLICK_SPACING = 24000  # Proben, 500 ms (nachtlauf.sh: Abstand 500 ms)
CLICK_SPACING_MS = CLICK_SPACING * 1000 / RATE
WINDOW_HALF_MS = CLICK_SPACING_MS / 2
REFERENCE_CLICKS = 10
BIN_S = 60
JUMP_SAMPLES = 2  # wie klickmessung.cpp:374
# CSV hat 3 Nachkommastellen je Zeit; neu gerechnete Differenzen weichen deshalb um bis zu 0,001 ms ab
TOLERANCE_MS = 0.0015


def quantiles(values):
    """Wie kennzahlen() in auswertung.cpp: sortiert, linear zwischen Nachbarn."""
    v = sorted(values)
    if not v:
        return None

    def q(p):
        i = p * (len(v) - 1)
        a = math.floor(i)
        b = min(a + 1, len(v) - 1)
        return v[a] + (v[b] - v[a]) * (i - a)

    return {"n": len(v), "min": v[0], "p5": q(0.05), "median": q(0.5), "p95": q(0.95), "max": v[-1]}


def stats_text(k, unit="ms"):
    if k is None:
        return "n=0"
    return (f"n={k['n']} min {k['min']:.3f} p5 {k['p5']:.3f} Median {k['median']:.3f} "
            f"p95 {k['p95']:.3f} max {k['max']:.3f} {unit} (Spanne {k['max'] - k['min']:.3f} {unit}, "
            f"p5–p95 {k['p95'] - k['p5']:.3f} {unit})")


def load_csv(path):
    clicks = []
    states_out = []
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row["art"] == "klick":
                c = {
                    "k": int(row["k"]),
                    "pos": int(row["lage"]),
                    "sent_ms": float(row["geschrieben_ms"]),
                    "waiting_ms": float(row["wartend_ms"]),
                    "rec_pos": None,
                    "rec_ms": None,
                    "old_delay": None,
                    "old_delay_wo": None,
                }
                if row["aufnahme_ms"]:
                    c["rec_pos"] = int(row["aufnahme_lage"])
                    c["rec_ms"] = float(row["aufnahme_ms"])
                    c["old_delay"] = float(row["verzoegerung_ms"])
                    c["old_delay_wo"] = float(row["verzoegerung_ohne_puffer_ms"])
                clicks.append(c)
            elif row["art"] == "zustand_ausgang":
                # Zustand und Fehler stehen in der letzten Spalte (klickmessung.cpp:394-396)
                state, error = row["epoch_ms"].split(" ", 1)
                states_out.append((float(row["geschrieben_ms"]), state, error))
    clicks.sort(key=lambda c: c["k"])
    if [c["k"] for c in clicks] != list(range(len(clicks))):
        raise SystemExit(f"{path}: Klicknummern nicht lückenlos")
    return clicks, states_out


def load_summary(path):
    text = path.read_text(encoding="utf-8")
    num = r"(-?[\d.]+)"
    pat = (rf"n={num} min {num} p5 {num} Median {num} p95 {num} max {num}")
    raw = re.search(r"Verzögerung Schreiben → Aufnahme: " + pat, text)
    wo = re.search(r"ohne geschätzten Pufferinhalt: " + pat, text)
    names = ["n", "min", "p5", "median", "p95", "max"]

    def to_dict(m):
        if not m:
            return None
        d = {n: float(x) for n, x in zip(names, m.groups())}
        d["n"] = int(d["n"])
        return d

    foreign = re.search(r"fremde Einsätze (\d+)", text)
    missing = re.search(r"fehlend (\d+)", text)
    duration = re.search(r"Dauer (\d+) s", text)
    return {
        "raw": to_dict(raw),
        "wo": to_dict(wo),
        "foreign": int(foreign.group(1)) if foreign else None,
        "missing": int(missing.group(1)) if missing else None,
        "duration_s": int(duration.group(1)) if duration else None,
    }


def assign_by_time(clicks):
    """Ordnet die Einsätze aus der CSV nach der Zeit zu. Gibt Zuordnung und Prüfwerte zurück."""
    onsets = [(c["rec_ms"], c["rec_pos"], c["k"]) for c in clicks if c["rec_ms"] is not None]
    onsets.sort()
    ref_set = [c for c in clicks[:REFERENCE_CLICKS] if c["rec_ms"] is not None]
    if not ref_set:
        raise SystemExit("keine Einsätze unter den ersten Klicks")
    # Alte Zuordnung ist am Anfang richtig, solange der Versatz um weniger als 12 000 Proben wandert
    offset0 = clicks[0]["rec_pos"] - clicks[0]["pos"] if clicks[0]["rec_pos"] is not None else None
    ref_drift = max(abs((c["rec_pos"] - c["pos"]) - offset0) for c in ref_set) if offset0 is not None else None
    ref_ms = quantiles([c["rec_ms"] - c["sent_ms"] for c in ref_set])["median"]

    sent = [c["sent_ms"] for c in clicks]
    expected = [s + ref_ms for s in sent]
    result = {}  # k -> (rec_ms, rec_pos, old_k)
    outside = []
    double = []
    deviations = []
    for rec_ms, rec_pos, old_k in onsets:
        i = bisect.bisect_left(expected, rec_ms)
        cand = [j for j in (i - 1, i) if 0 <= j < len(expected)]
        k = min(cand, key=lambda j: abs(rec_ms - expected[j]))
        dev = rec_ms - expected[k]
        if abs(dev) >= WINDOW_HALF_MS:
            outside.append((rec_ms, old_k, dev))
            continue
        if k in result:
            double.append((k, rec_ms, old_k))
            continue
        result[k] = (rec_ms, rec_pos, old_k)
        deviations.append(dev)
    return {
        "ref_ms": ref_ms,
        "ref_drift": ref_drift,
        "assigned": result,
        "outside": outside,
        "double": double,
        "deviations": deviations,
    }


def underruns(states_out):
    """Unterläufe mit Dauer des Zustands «leer» bis zum nächsten «aktiv»."""
    out = []
    for i, (t, state, error) in enumerate(states_out):
        if error != "Unterlauf":
            continue
        end = next((t2 for t2, s2, _ in states_out[i + 1:] if s2 == "aktiv"), None)
        out.append((t, None if end is None else end - t))
    return out


def evaluate(csv_path):
    clicks, states_out = load_csv(csv_path)
    summary = load_summary(csv_path.with_name(csv_path.stem + "-zusammenfassung.txt"))
    a = assign_by_time(clicks)
    rows = []
    for c in clicks:
        hit = a["assigned"].get(c["k"])
        if hit is None:
            continue
        rec_ms, rec_pos, old_k = hit
        raw = rec_ms - c["sent_ms"]
        rows.append({
            "k": c["k"],
            "old_k": old_k,
            "sent_ms": c["sent_ms"],
            "raw": raw,
            "wo": raw - c["waiting_ms"],
            "offset": rec_pos - c["pos"],
        })
    missing = [c for c in clicks if c["k"] not in a["assigned"]]

    # Stellen, an denen die alte Zuordnung um einen weiteren Klick verrutscht: Dort hat die alte
    # Auswertung genau einen Einsatz als «fremd» verworfen (Klick davor war schon belegt).
    shift_steps = []
    prev_shift = 0
    for r in rows:
        shift = r["k"] - r["old_k"]
        if shift != prev_shift:
            shift_steps.append((r["k"], prev_shift, shift))
            prev_shift = shift
    dropped_at = {k - 1 for k, before, after in shift_steps if after == before + 1}

    duration_ms = (summary["duration_s"] or 0) * 1000
    missing_info = []
    for c in missing:
        if c["k"] in dropped_at:
            kind = "Einsatz von der alten Auswertung als «fremd» verworfen (Zeit nicht in der CSV)"
        elif c["sent_ms"] > duration_ms - CLICK_SPACING_MS:
            kind = "kurz vor dem Stopp geschrieben (stop() leert den Puffer)"
        else:
            kind = "kein Einsatz (Lücke)"
        missing_info.append((c["k"], c["sent_ms"], kind))

    jumps = []
    for prev, cur in zip(rows, rows[1:]):
        d = cur["offset"] - prev["offset"]
        if abs(d) > JUMP_SAMPLES:
            jumps.append((cur["k"], d))

    return {
        "path": csv_path,
        "clicks": clicks,
        "summary": summary,
        "assign": a,
        "rows": rows,
        "missing": missing_info,
        "shift_steps": shift_steps,
        "jumps": jumps,
        "underruns": underruns(states_out),
    }


def report_text(ev):
    a = ev["assign"]
    rows = ev["rows"]
    s = ev["summary"]
    lines = []
    lines.append(f"Z0: {ev['path'].name} neu ausgewertet, Zuordnung nach der Zeit "
                 f"(prototypes/ton/neu_auswerten.py)")
    lines.append(f"Bezugsverzögerung (Median roh, Klick 0–{REFERENCE_CLICKS - 1}): {a['ref_ms']:.3f} ms; "
                 f"Fenster ± {WINDOW_HALF_MS:.0f} ms; Versatz wandert dort um höchstens "
                 f"{a['ref_drift']} Proben (Grenze 12000)")
    lines.append(f"Abstand zur Bezugsverzögerung: {stats_text(quantiles(a['deviations']))}")
    lines.append(f"Klicks geschrieben {len(ev['clicks'])}, mit Einsatz {len(rows)}, ohne Einsatz "
                 f"{len(ev['missing'])}, doppelt {len(a['double'])}, Einsätze ausserhalb des Fensters "
                 f"{len(a['outside'])}")
    lines.append(f"Alte Auswertung: fehlend {s['missing']}, fremde Einsätze {s['foreign']}")
    dropped = sum(1 for m in ev["missing"] if "verworfen" in m[2])
    real_gaps = sum(1 for m in ev["missing"] if "Lücke" in m[2])
    lines.append(f"Klicks ohne Einsatz: {dropped} verworfen von der alten Auswertung, "
                 f"{len(ev['missing']) - dropped - real_gaps} kurz vor dem Stopp, {real_gaps} Lücken")
    if dropped != (s["foreign"] or 0):
        lines.append(f"  ACHTUNG: {dropped} Klicks an Verrutsch-Stellen, aber {s['foreign']} fremde Einsätze")
    for k, t, kind in ev["missing"]:
        lines.append(f"  Klick {k} (geschrieben bei {t / 1000:.3f} s): {kind}")
    lines.append(f"Verrutsch-Stellen der alten Zuordnung (neu k − alt k): " + (", ".join(
        f"ab Klick {k}: {b} → {f}" for k, b, f in ev["shift_steps"]) or "keine"))
    lines.append("")
    lines.append(f"Verzögerung Schreiben → Aufnahme: {stats_text(quantiles([r['raw'] for r in rows]))}")
    lines.append(f"  ohne geschätzten Pufferinhalt: {stats_text(quantiles([r['wo'] for r in rows]))}")
    if rows:
        loss = rows[0]["offset"] - rows[-1]["offset"]
        lines.append(f"Versatz (Lage Aufnahme − Lage Ausgabe): Klick {rows[0]['k']} {rows[0]['offset']}, "
                     f"Klick {rows[-1]['k']} {rows[-1]['offset']} Proben → Verlust {loss} Proben = "
                     f"{loss * 1000 / RATE:.1f} ms")
    big = [j for j in ev["jumps"] if abs(j[1]) > CLICK_SPACING / 2]
    lines.append(f"Sprünge der Lage (> {JUMP_SAMPLES} Proben): {len(ev['jumps'])}, davon > 12000 Proben: "
                 f"{len(big)}; grösster Betrag "
                 f"{max((abs(d) for _, d in ev['jumps']), default=0)} Proben")
    ur = ev["underruns"]
    lines.append(f"Aussetzer (Unterläufe QAudioSink): {len(ur)}" + (
        f", Dauer «leer» {stats_text(quantiles([d for _, d in ur if d is not None]))}" if ur else ""))
    for t, d in ur:
        before = [r["wo"] for r in rows if t - 3 * CLICK_SPACING_MS <= r["sent_ms"] < t]
        after = [r["wo"] for r in rows if t <= r["sent_ms"] < t + 3 * CLICK_SPACING_MS]
        mb = quantiles(before)["median"] if before else float("nan")
        ma = quantiles(after)["median"] if after else float("nan")
        lines.append(f"  bei {t / 1000:.3f} s, leer {d:.3f} ms; ohne Pufferinhalt 1,5 s davor / danach "
                     f"(Median): {mb:.1f} / {ma:.1f} ms")
    lines.append("")
    lines.append(f"Verlauf je {BIN_S} s (nach Sendezeit; ohne geschätzten Pufferinhalt):")
    lines.append("  Zeit       n   Median     p95   Spanne  p5–p95 | roh Median  p95 | Verlust Proben | "
                 "Unterläufe | ohne Einsatz")
    end_s = max(c["sent_ms"] for c in ev["clicks"]) / 1000
    # Verlust gegen den letzten Klick des vorigen Abschnitts, damit die Summe dem ganzen Lauf entspricht
    last_offset = rows[0]["offset"] if rows else 0
    for b in range(0, int(end_s // BIN_S) + 1):
        lo, hi = b * BIN_S * 1000, (b + 1) * BIN_S * 1000
        part = [r for r in rows if lo <= r["sent_ms"] < hi]
        if not part:
            continue
        kw = quantiles([r["wo"] for r in part])
        kr = quantiles([r["raw"] for r in part])
        loss = last_offset - part[-1]["offset"]
        last_offset = part[-1]["offset"]
        n_ur = sum(1 for t, _ in ur if lo <= t < hi)
        n_miss = sum(1 for _, t, _ in ev["missing"] if lo <= t < hi)
        lines.append(f"  {b * BIN_S:3d}–{(b + 1) * BIN_S:3d} s {kw['n']:4d} {kw['median']:8.2f} {kw['p95']:7.2f} "
                     f"{kw['max'] - kw['min']:8.2f} {kw['p95'] - kw['p5']:7.2f} | {kr['median']:10.2f} "
                     f"{kr['p95']:5.1f} | {loss:14d} | {n_ur:10d} | {n_miss:12d}")
    return "\n".join(lines) + "\n"


def compare(new, old):
    """Grösste Abweichung der Kennzahlen neu gegen alt (ms) und ob n gleich ist."""
    if new is None or old is None:
        return None, False
    diff = max(abs(new[x] - old[x]) for x in ("min", "p5", "median", "p95", "max"))
    return diff, new["n"] == old["n"]


def crosscheck_text(evs):
    lines = ["Z0 Gegenprobe: Zuordnung nach der Zeit gegen die alte Auswertung (Zusammenfassung)",
             f"Toleranz {TOLERANCE_MS} ms (CSV hat 3 Nachkommastellen je Zeit)", ""]
    ok_all = True
    for ev in evs:
        s = ev["summary"]
        rows = ev["rows"]
        moved = sum(1 for r in rows if r["k"] != r["old_k"])
        new_raw = quantiles([r["raw"] for r in rows])
        new_wo = quantiles([r["wo"] for r in rows])
        d_raw, n_raw = compare(new_raw, s["raw"])
        d_wo, n_wo = compare(new_wo, s["wo"])
        same = moved == 0 and n_raw and n_wo and d_raw <= TOLERANCE_MS and d_wo <= TOLERANCE_MS
        lines.append(f"{ev['path'].name}: Einsätze anders zugeordnet {moved}; ganzer Lauf: "
                     f"n {new_raw['n']}/{s['raw']['n']}, grösste Abweichung roh {d_raw:.4f} ms, "
                     f"ohne Pufferinhalt {d_wo:.4f} ms → {'gleich' if same else 'verschieden'}")
        if moved:
            first = min(r["old_k"] for r in rows if r["k"] != r["old_k"])
            # Vorher: alte Werte aus der CSV gegen neue Werte für dieselben Klicks
            old_pre = [c for c in ev["clicks"] if c["k"] < first and c["old_delay"] is not None]
            new_pre = [r for r in rows if r["k"] < first]
            d1, n1 = compare(quantiles([r["raw"] for r in new_pre]), quantiles([c["old_delay"] for c in old_pre]))
            d2, n2 = compare(quantiles([r["wo"] for r in new_pre]),
                             quantiles([c["old_delay_wo"] for c in old_pre]))
            pre_ok = n1 and n2 and d1 <= TOLERANCE_MS and d2 <= TOLERANCE_MS
            ok_all &= pre_ok
            lines.append(f"  vor dem ersten Verrutschen (Klick 0–{first - 1}): n {len(new_pre)}/{len(old_pre)}, "
                         f"grösste Abweichung roh {d1:.4f} ms, ohne Pufferinhalt {d2:.4f} ms → "
                         f"{'gleich' if pre_ok else 'verschieden'}")
        else:
            ok_all &= same
    lines.append("")
    lines.append("Ergebnis: " + ("Gegenprobe bestanden" if ok_all else "Gegenprobe NICHT bestanden"))
    return "\n".join(lines) + "\n", ok_all


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("ordner", nargs="+", type=Path)
    args = p.parse_args()
    ok = True
    for folder in args.ordner:
        files = sorted(folder.glob("m1-*.csv"))
        if not files:
            raise SystemExit(f"{folder}: keine m1-*.csv")
        evs = [evaluate(f) for f in files]
        for ev in evs:
            if ev["path"].stem.startswith("m1-last-"):
                text = report_text(ev)
                (folder / (ev["path"].stem + "-neu.txt")).write_text(text, encoding="utf-8")
                print(f"== {folder.name}/{ev['path'].stem}-neu.txt\n{text}")
        text, good = crosscheck_text(evs)
        (folder / "m1-gegenprobe-neu.txt").write_text(text, encoding="utf-8")
        print(f"== {folder.name}/m1-gegenprobe-neu.txt\n{text}")
        ok &= good
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
