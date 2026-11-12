#!/usr/bin/env python3
"""P12b b1: Z3 aus den CSV-Dateien neu auswerten (E1 in zwei Lesarten, fehlende Klicks).

Entscheid des Nutzers 2026-11-12: E1 neu auswerten, P behalten, Klick 197 klären. Die alte
Auswertung (pwklick.cpp:957-996) vergleicht den Median des Rests (Einsatz − erwartete Zeit aus
pw_time) von 5 Klicks vor gegen 5 Klicks nach jeder Marke. Dieses Skript rechnet für dieselben
Klicks zwei weitere Lesarten:
  (a) Lage in Proben: versatz_proben (Lage Aufnahme − Lage Ausgabe), in ms umgerechnet;
  (b) Rest + Quantum: rest_ms + size/Rate (size = Zyklusgrösse des Abrufs, der den Klick schrieb).
Grenze je Wechsel wie E1: Betrag der Änderung ≤ 10 ms. E2 bis E4 werden nicht neu gerechnet, nur
aus den Zusammenfassungen übernommen.

Fehlende Klicks: Lage in der Aufnahme (Lage Ausgabe + Median des Versatzes), Abruf der Ausgabe
und der Aufnahme an dieser Lage (z3c-aufrufe.csv), fremde Einsätze in der Nähe, Lage zum nächsten
Wechsel der Zyklusgrösse.

  python3 prototypes/ton/z3_neu_auswerten.py <Datenordner> [<Datenordner> ...]

Schreibt je Ordner z3-neu.txt. Nur Standardbibliothek.
"""
import argparse
import bisect
import csv
import re
import statistics
import sys
from pathlib import Path

RATE = 48000
CLICK_SPACING = 24000  # Proben, 500 ms (pw-lauf.sh: Abstand 500 ms)
E1_LIMIT_MS = 10.0
N_SIDE = 5  # Klicks je Seite einer Marke, wie pwklick.cpp:965-969
# CSV hat 3 Nachkommastellen je Zeit; der Median von 5 Werten ist ein Wert der CSV
TOLERANCE_MS = 0.0015


def ms(samples):
    return samples * 1000 / RATE


def opt_float(s):
    return float(s) if s != "" else None


def load_pw_csv(path):
    """CSV von pw-messen (pwklick.cpp:707-711): Klicks, fremde Einsätze, Zustände."""
    clicks, foreign, states = [], [], []
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row["art"] == "klick":
                if row["rate"] != f"1/{RATE}":
                    raise SystemExit(f"{path}: Rate {row['rate']} statt 1/{RATE}")
                clicks.append({
                    "k": int(row["k"]),
                    "pos": int(row["lage"]),
                    "sent_ms": float(row["geschrieben_ms"]),
                    "index": int(row["index"]),
                    "size": int(row["size"]),
                    "expected_ms": opt_float(row["erwartet_ms"]),
                    "rec_pos": int(row["aufnahme_lage"]) if row["aufnahme_lage"] else None,
                    "rec_ms": opt_float(row["aufnahme_ms"]),
                    "rest": opt_float(row["rest_ms"]),
                    "raw": opt_float(row["roh_ms"]),
                    "offset": int(row["versatz_proben"]) if row["versatz_proben"] else None,
                    "epoch": int(row["epoch_ms"]),
                })
            elif row["art"] == "fremd":
                foreign.append({"rec_pos": int(row["aufnahme_lage"]), "rec_ms": float(row["aufnahme_ms"])})
            elif row["art"] == "zustand":
                states.append((float(row["geschrieben_ms"]), row["text"].strip()))
    clicks.sort(key=lambda c: c["k"])
    return clicks, foreign, states


def load_q_csv(path):
    """CSV von ton-messen (klickmessung.cpp), Gegenlauf Q."""
    clicks = []
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row["art"] != "klick":
                continue
            found = row["aufnahme_ms"] != ""
            clicks.append({
                "k": int(row["k"]),
                "pos": int(row["lage"]),
                "sent_ms": float(row["geschrieben_ms"]),
                "offset": int(row["versatz_proben"]) if found else None,
                "wo": float(row["verzoegerung_ohne_puffer_ms"]) if found else None,
                "epoch": int(row["epoch_ms"]),
            })
    clicks.sort(key=lambda c: c["k"])
    return clicks


def load_calls(path):
    """Abrufe (pwklick.cpp): strom aus/ein, steady_ms, ticks, size, lage, frames, leer, kurz."""
    calls = {"aus": [], "ein": []}
    with open(path, newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            calls[row["strom"]].append({
                "t": float(row["steady_ms"]),
                "ticks": int(row["ticks"]) if row["ticks"] else None,
                "size": int(row["size"]) if row["size"] else None,
                "pos": int(row["lage"]),
                "frames": int(row["frames"]) if row["frames"] else 0,
                "empty": row["leer"] == "1",
                "short": row["kurz"] == "1",
            })
    return calls


def load_markers(path):
    out = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            epoch, text = line.split(" ", 1)
            out.append((int(epoch), text))
    return out


def summary_start_epoch(text):
    m = re.search(r"Beginn \(Wanduhr\) (\d+) ms", text)
    return int(m.group(1)) if m else None


def old_marker_values(text):
    """Alte Werte je Marke aus der Zusammenfassung (pwklick.cpp:983-992)."""
    out = []
    for m in re.finditer(r"Marke «([^»]*)» bei ([\d.]+) s.*?\n  Rest Median (-?[\d.]+) → (-?[\d.]+) ms, "
                         r"Änderung (-?[\d.]+) ms", text):
        out.append({"text": m.group(1), "rest_before": float(m.group(3)), "rest_after": float(m.group(4)),
                    "change": float(m.group(5))})
    return out


def split_at_marker(clicks, epoch, usable):
    """Wie pwklick.cpp:959-969: 5 letzte Klicks vor, 5 erste ab der Marke (Wanduhr)."""
    before, after = [], []
    for c in clicks:
        if not usable(c):
            continue
        if c["epoch"] < epoch:
            before.append(c)
        elif len(after) < N_SIDE:
            after.append(c)
    return before[-N_SIDE:], after


def med(clicks, f):
    return statistics.median(f(c) for c in clicks)


def verdict(change):
    return "erfüllt" if abs(change) <= E1_LIMIT_MS else "nicht erfüllt"


def size_switches(calls):
    """Abrufe, bei denen die Zyklusgrösse wechselt, mit Δticks zum vorigen Abruf."""
    out = []
    for prev, cur in zip(calls, calls[1:]):
        if cur["size"] != prev["size"] and not cur["empty"] and not prev["empty"]:
            out.append({"call": cur, "prev_size": prev["size"],
                        "dticks": None if cur["ticks"] is None or prev["ticks"] is None else cur["ticks"] - prev["ticks"]})
    return out


def call_at(calls, pos):
    """Index des Abrufs, dessen Proben die Lage pos enthalten, sonst None."""
    starts = [c["pos"] for c in calls]
    i = bisect.bisect_right(starts, pos) - 1
    if i >= 0 and calls[i]["pos"] <= pos < calls[i]["pos"] + calls[i]["frames"]:
        return i
    return None


def contiguous(calls, i, span=3):
    """Lage zusammenhängend und keine leeren Abrufe in i−span … i+span."""
    lo, hi = max(0, i - span), min(len(calls) - 1, i + span)
    gaps = [j for j in range(lo, hi) if calls[j]["pos"] + calls[j]["frames"] != calls[j + 1]["pos"]]
    empties = [j for j in range(lo, hi + 1) if calls[j]["empty"]]
    return gaps, empties


def e1_pw(folder, lines):
    clicks, foreign, states = load_pw_csv(folder / "z3c.csv")
    summary = (folder / "z3c-zusammenfassung.txt").read_text(encoding="utf-8")
    markers = load_markers(folder / "z3c-zeiten.txt")
    start_epoch = summary_start_epoch(summary)
    old = old_marker_values(summary)
    usable = lambda c: c["rec_ms"] is not None and c["expected_ms"] is not None  # noqa: E731

    lines.append("== E1 neu: Z3c (pw-messen), Median von 5 Klicks vor gegen 5 nach jeder Marke")
    lines.append("   (a) Lage = versatz_proben in ms; (b) Rest + Quantum = rest_ms + size/Rate; alt = rest_ms")
    lines.append("")
    results = []
    all_ok_old = True
    for idx, (epoch, text) in enumerate(markers):
        before, after = split_at_marker(clicks, epoch, usable)
        near = [c["k"] for c in clicks if abs(c["epoch"] - epoch) <= 1]
        if len(before) != N_SIDE or len(after) != N_SIDE:
            raise SystemExit(f"{folder}: Marke {text}: {len(before)} vor / {len(after)} nach")
        a_b, a_a = med(before, lambda c: ms(c["offset"])), med(after, lambda c: ms(c["offset"]))
        b_b = med(before, lambda c: c["rest"] + ms(c["size"]))
        b_a = med(after, lambda c: c["rest"] + ms(c["size"]))
        r_b, r_a = med(before, lambda c: c["rest"]), med(after, lambda c: c["rest"])
        size_b = sorted({c["size"] for c in before})
        size_a = sorted({c["size"] for c in after})
        t_s = (epoch - start_epoch) / 1000 if start_epoch else float("nan")
        lines.append(f"Marke «{text}» bei {t_s:.3f} s; Klicks vor {before[0]['k']}–{before[-1]['k']}, "
                     f"nach {after[0]['k']}–{after[-1]['k']}; size vor {size_b}, nach {size_a}")
        if near:
            lines.append(f"  ACHTUNG: Klick {near} liegt ≤ 1 ms an der Marke (Wanduhr in der CSV ganzzahlig)")
        lines.append(f"  (a) Lage:           {a_b:9.3f} → {a_a:9.3f} ms, Änderung {a_a - a_b:+8.3f} ms "
                     f"({before[-1]['offset']} → {after[0]['offset']} Proben) → {verdict(a_a - a_b)}")
        lines.append(f"  (b) Rest + Quantum: {b_b:9.3f} → {b_a:9.3f} ms, Änderung {b_a - b_b:+8.3f} ms "
                     f"→ {verdict(b_a - b_b)}")
        lines.append(f"  alt Rest:           {r_b:9.3f} → {r_a:9.3f} ms, Änderung {r_a - r_b:+8.3f} ms "
                     f"→ {verdict(r_a - r_b)}")
        if idx < len(old):
            o = old[idx]
            same = (abs(o["rest_before"] - r_b) <= TOLERANCE_MS and abs(o["rest_after"] - r_a) <= TOLERANCE_MS)
            all_ok_old &= same
            lines.append(f"  Gegenprobe alt (Zusammenfassung): {o['rest_before']:.3f} → {o['rest_after']:.3f} ms, "
                         f"Änderung {o['change']:+.3f} ms → {'gleich' if same else 'VERSCHIEDEN'}")
        else:
            all_ok_old = False
            lines.append("  Gegenprobe alt: Marke fehlt in der Zusammenfassung")
        results.append((text, a_a - a_b, b_a - b_b, r_a - r_b))
        lines.append("")
    ok_a = all(abs(x[1]) <= E1_LIMIT_MS for x in results)
    ok_b = all(abs(x[2]) <= E1_LIMIT_MS for x in results)
    ok_old = all(abs(x[3]) <= E1_LIMIT_MS for x in results)
    lines.append(f"E1 (a) Lage in Proben:   {'erfüllt' if ok_a else 'NICHT erfüllt'} "
                 f"(grösste Änderung {max(abs(x[1]) for x in results):.3f} ms)")
    lines.append(f"E1 (b) Rest + Quantum:   {'erfüllt' if ok_b else 'NICHT erfüllt'} "
                 f"(grösste Änderung {max(abs(x[2]) for x in results):.3f} ms)")
    lines.append(f"E1 alt (Rest):           {'erfüllt' if ok_old else 'NICHT erfüllt'} "
                 f"(grösste Änderung {max(abs(x[3]) for x in results):.3f} ms)")
    lines.append(f"Gegenprobe alte Rechnung gegen Zusammenfassung: {'bestanden' if all_ok_old else 'NICHT bestanden'}")
    found = [c for c in clicks if c["offset"] is not None]
    offs = sorted({c["offset"] for c in found})
    lines.append(f"Versatz aller gefundenen Klicks (n={len(found)}): {offs} Proben")
    lines.append("")
    return clicks, foreign, states, start_epoch, all_ok_old


def missing_clicks(folder, clicks, foreign, states, lines):
    calls = load_calls(folder / "z3c-aufrufe.csv")
    rec, out = calls["ein"], calls["aus"]
    found = [c for c in clicks if c["offset"] is not None]
    offset = int(statistics.median(c["offset"] for c in found))
    switches = size_switches(rec)
    pauses = [t for t, s in states if s.startswith("aufnahme streaming → paused")]
    rec_end = rec[-1]["pos"] + rec[-1]["frames"] if rec else 0

    lines.append("== Fehlende Klicks (Z3c)")
    lines.append(f"Lage in der Aufnahme = Lage Ausgabe + {offset} Proben (Median Versatz); "
                 f"Aufnahme endet bei Lage {rec_end}")
    lines.append("Wechsel der Zyklusgrösse (Aufnahme): " + ", ".join(
        f"{s['call']['t'] / 1000:.3f} s {s['prev_size']} → {s['call']['size']} (Δticks {s['dticks']}, "
        f"Lage {s['call']['pos']})" for s in switches))
    lines.append("Aufnahme «streaming → paused» bei: " + (", ".join(f"{t / 1000:.3f} s" for t in pauses) or "nie"))
    missing = [c for c in clicks if c["rec_ms"] is None]
    for c in missing:
        pos = c["pos"] + offset
        lines.append(f"Klick {c['k']}: geschrieben bei {c['sent_ms'] / 1000:.3f} s, Lage Ausgabe {c['pos']}, "
                     f"index {c['index']} im Abruf, size {c['size']}, erwartet hörbar "
                     f"{c['expected_ms'] / 1000:.3f} s" if c["expected_ms"] is not None else
                     f"Klick {c['k']}: geschrieben bei {c['sent_ms'] / 1000:.3f} s, ohne erwartete Zeit")
        if pos >= rec_end:
            lines.append(f"  Lage in der Aufnahme {pos} liegt nach dem Ende der Aufnahme ({rec_end}): "
                         f"nach Ende der Messdauer geschrieben, kein Verlust")
            continue
        io = call_at(out, c["pos"])
        if io is None:
            lines.append("  Ausgabe: kein Abruf enthält diese Lage")
        else:
            a = out[io]
            lines.append(f"  Ausgabe: Abruf bei {a['t'] / 1000:.3f} s, Lage {a['pos']}+{a['frames']}, "
                         f"leer {int(a['empty'])}, kurz {int(a['short'])}")
        ir = call_at(rec, pos)
        if ir is None:
            lines.append(f"  Aufnahme: kein Abruf enthält Lage {pos}")
        else:
            r = rec[ir]
            gaps, empties = contiguous(rec, ir)
            lines.append(f"  Aufnahme: Lage {pos} im Abruf bei {r['t'] / 1000:.3f} s, Lage {r['pos']}+{r['frames']} "
                         f"(size {r['size']}), {pos - r['pos']} Proben nach Beginn des Abrufs; Lage ±3 Abrufe "
                         f"{'zusammenhängend' if not gaps else 'mit Lücken ' + str(gaps)}, leere Abrufe "
                         f"{len(empties)}")
        near = [f for f in foreign if abs(f["rec_pos"] - pos) < CLICK_SPACING / 2]
        lines.append(f"  fremde Einsätze ± {CLICK_SPACING // 2} Proben um diese Lage: {len(near)} "
                     f"(fremde Einsätze im ganzen Lauf: {len(foreign)}, Lagen {[f['rec_pos'] for f in foreign]})")
        if switches:
            s = min(switches, key=lambda s: abs(pos - s["call"]["pos"]))
            d = pos - s["call"]["pos"]
            lines.append(f"  nächster Wechsel: {s['prev_size']} → {s['call']['size']} bei {s['call']['t'] / 1000:.3f} s "
                         f"(Δticks {s['dticks']}); Lage {d:+d} Proben = {ms(d):+.3f} ms zum ersten Abruf danach")
        if pauses:
            t = min(pauses, key=lambda t: abs(t - (c["expected_ms"] or c["sent_ms"])))
            if c["expected_ms"] is not None:
                arr = c["expected_ms"] + statistics.median(x["rest"] for x in found if x["size"] == c["size"])
                lines.append(f"  Ankunft in der Aufnahme bei gleichem Rest wie die übrigen Klicks mit size {c['size']}: "
                             f"{arr / 1000:.3f} s; Aufnahme «paused» bei {t / 1000:.3f} s ({arr - t:+.1f} ms)")
    lines.append("")
    lines.append("Klick, dessen Lage in der Aufnahme am nächsten am ersten Abruf nach jedem Wechsel liegt:")
    for s in switches:
        best = min(clicks, key=lambda c: abs(c["pos"] + offset - s["call"]["pos"]))
        d = best["pos"] + offset - s["call"]["pos"]
        lines.append(f"  {s['prev_size']} → {s['call']['size']} bei {s['call']['t'] / 1000:.3f} s (Δticks {s['dticks']}): "
                     f"Klick {best['k']}, Lage {d:+d} Proben = {ms(d):+.3f} ms, "
                     f"{'gefunden' if best['rec_ms'] is not None else 'FEHLT'}")
    lines.append("")


def e1_q(folder, lines):
    path = folder / "z3cq.csv"
    if not path.exists():
        lines.append("== Z3c-Q: z3cq.csv fehlt, nicht ausgewertet")
        lines.append("")
        return
    clicks = load_q_csv(path)
    markers = load_markers(folder / "z3cq-zeiten.txt")
    lines.append("== Z3c-Q zum Vergleich (ton-messen, Q): Lage in Proben und alte Grösse «ohne Pufferinhalt»")
    for epoch, text in markers:
        before, after = split_at_marker(clicks, epoch, lambda c: c["offset"] is not None)
        a_b, a_a = med(before, lambda c: ms(c["offset"])), med(after, lambda c: ms(c["offset"]))
        w_b, w_a = med(before, lambda c: c["wo"]), med(after, lambda c: c["wo"])
        lines.append(f"Marke «{text}»: Klicks vor {before[0]['k']}–{before[-1]['k']}, nach {after[0]['k']}–{after[-1]['k']}")
        lines.append(f"  Lage: {a_b:.3f} → {a_a:.3f} ms, Änderung {a_a - a_b:+.3f} ms → {verdict(a_a - a_b)}")
        lines.append(f"  ohne Pufferinhalt: {w_b:.3f} → {w_a:.3f} ms, Änderung {w_a - w_b:+.3f} ms")
    missing = [c["k"] for c in clicks if c["offset"] is None]
    lines.append(f"Klicks ohne Einsatz: {missing}")
    lines.append("")


def offsets_quiet(folder, lines):
    """Z3b, Z3d: keine Marken; nur prüfen, ob der Versatz in Proben fest bleibt."""
    lines.append("== Z3b, Z3d (ohne Marken): Versatz in Proben")
    for name in ("z3b", "z3d-1", "z3d-2"):
        path = folder / f"{name}.csv"
        if not path.exists():
            lines.append(f"{name}: CSV fehlt")
            continue
        clicks, _, _ = load_pw_csv(path)
        offs = sorted({c["offset"] for c in clicks if c["offset"] is not None})
        miss = [c["k"] for c in clicks if c["rec_ms"] is None]
        lines.append(f"{name}: Versatz {offs} Proben, Klicks ohne Einsatz {miss} (von {len(clicks)})")
    lines.append("")


def taken_over(folder, lines):
    lines.append("== E2 bis E4 übernommen (nicht neu gerechnet), aus den Zusammenfassungen")
    for name in ("z3b", "z3c", "z3d-1", "z3d-2"):
        path = folder / f"{name}-zusammenfassung.txt"
        if not path.exists():
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            if line.startswith("Unterläufe Ausgabe") or "p5–p95 des Rests" in line:
                lines.append(f"{name}: {line.strip()}")
    for name in ("z3a-bibliotheken.txt", "z3a-vergleich-m1a.txt"):
        path = folder / name
        if path.exists():
            body = [x.strip() for x in path.read_text(encoding="utf-8").splitlines() if x.strip()]
            lines.append(f"{name}: " + "; ".join(body))
    lines.append("")


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("ordner", nargs="+", type=Path)
    args = p.parse_args()
    ok = True
    for folder in args.ordner:
        lines = [f"Z3 neu ausgewertet: {folder.name} (prototypes/ton/z3_neu_auswerten.py)",
                 f"Rate {RATE}/s, Grenze E1 {E1_LIMIT_MS:.0f} ms je Wechsel", ""]
        clicks, foreign, states, _, good = e1_pw(folder, lines)
        ok &= good
        missing_clicks(folder, clicks, foreign, states, lines)
        e1_q(folder, lines)
        offsets_quiet(folder, lines)
        taken_over(folder, lines)
        text = "\n".join(lines)
        (folder / "z3-neu.txt").write_text(text, encoding="utf-8")
        print(f"== {folder.name}/z3-neu.txt\n{text}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
