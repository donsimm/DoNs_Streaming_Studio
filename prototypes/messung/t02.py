#!/usr/bin/env python3
"""T-02: Overlay 1920x1080 einer Variante 60 s lang messen.

Ablauf: App starten (Laufzeit 75 s), 5 s Anlaufzeit, dann 60 s gleichzeitig
  LC_ALL=C mpstat 1 60                                   (CPU gesamt)
  LC_ALL=C pidstat -h -u -r -p <PIDs> 1 60               (CPU und RAM der Prozesse)
  nvidia-smi --query-gpu=... --format=csv -l 1           (GPU, zusätzlich)
und einmal PSS aus /proc/<pid>/smaps_rollup bei 30 s. Danach ein Bildschirmfoto mit
  ffmpeg -f x11grab -video_size 1920x1080 -i $DISPLAY+0,0 -frames:v 1
Bildrate, Einbrüche und Videobilder kommen aus den Zeitstempeln der App.

  python3 prototypes/messung/t02.py A|B|C

Quellen:
  pidstat(1), mpstat(1): https://sysstat.github.io/ und man pidstat / man mpstat (sysstat 12.7.5)
  x11grab: https://ffmpeg.org/ffmpeg-devices.html#x11grab
  nvidia-smi --query-gpu: nvidia-smi --help-query-gpu
"""
import argparse
import json
import os
import subprocess
import sys
import time

import common

DURATION_APP = 75
WARMUP = 5
WINDOW = 60
DROP_MS = 50.0  # Einbruch: Abstand zweier Bilder > 1,5 x 33,3 ms


def parse_pidstat(text):
    """Summiert %CPU und RSS (KiB) je Zeitpunkt über alle Prozesse."""
    header, per_t, per_cmd = None, {}, {}
    for line in text.splitlines():
        if line.startswith("#"):
            header = line[1:].split()
            continue
        parts = line.split()
        if not header or len(parts) != len(header):
            continue
        row = dict(zip(header, parts))
        t = row["Time"]
        s = per_t.setdefault(t, {"cpu": 0.0, "rss": 0})
        s["cpu"] += float(row["%CPU"])
        s["rss"] += int(row["RSS"])
        # je Befehl: Summe über gleichnamige Prozesse (z. B. mehrere QtWebEngineProcess) je Zeitpunkt
        c = per_cmd.setdefault(row["Command"], {}).setdefault(t, {"cpu": 0.0, "rss": 0})
        c["cpu"] += float(row["%CPU"])
        c["rss"] += int(row["RSS"])
    per_cmd = {k: {"cpu": [x["cpu"] for x in v.values()], "rss": [x["rss"] for x in v.values()]}
               for k, v in per_cmd.items()}
    return per_t, per_cmd


def parse_mpstat(text):
    vals = []
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 12 and parts[1] == "all" and not line.startswith("Average"):
            vals.append(100.0 - float(parts[-1]))
    return vals


def stat(vals):
    if not vals:
        return None
    return {"min": round(min(vals), 1), "mittel": round(sum(vals) / len(vals), 1), "max": round(max(vals), 1)}


def rate(times, ws, we):
    """Bilder pro Sekunde (60 Eimer), Abstände, Einbrüche im Fenster [ws, we)."""
    t = sorted(x for x in times if ws <= x < we)
    buckets = [0] * int(round((we - ws) / 1000))
    for x in t:
        i = int((x - ws) // 1000)
        if 0 <= i < len(buckets):
            buckets[i] += 1
    gaps = [b - a for a, b in zip(t, t[1:])]
    return {
        "bilder": len(t),
        "bilder_pro_s": round(len(t) / ((we - ws) / 1000), 2),
        "pro_s": stat(buckets),
        "sekunden_unter_28": sum(1 for b in buckets if b < 28),
        "abstand_max_ms": round(max(gaps), 1) if gaps else None,
        "einbrueche_ueber_50ms": sum(1 for g in gaps if g > DROP_MS),
        "verlauf_pro_s": buckets,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("variant", choices=["A", "B", "C"])
    args = ap.parse_args()
    v = args.variant

    outdir = os.path.join(common.OUT, f"t02-{v}")
    os.makedirs(outdir, exist_ok=True)
    os.makedirs(common.SHOTS, exist_ok=True)
    stats_path = os.path.join(outdir, "app-zeitstempel.json")
    if os.path.exists(stats_path):
        os.remove(stats_path)

    core = common.start_core() if v == "B" else None
    cmd = common.app_cmd(v, "t02", stats=stats_path, duration=DURATION_APP)
    started = common.now_iso()
    app = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                           start_new_session=True)
    try:
        time.sleep(WARMUP)
        pids = common.descendants(app.pid)
        if core:
            pids.append(core.pid)
        procs = {p: common.comm(p) for p in pids}
        pid_arg = ",".join(str(p) for p in pids)
        env = dict(os.environ, LC_ALL="C")
        cmds = {
            "mpstat": ["mpstat", "1", str(WINDOW)],
            "pidstat": ["pidstat", "-h", "-u", "-r", "-p", pid_arg, "1", str(WINDOW)],
        }
        ws = time.time() * 1000
        runners = {k: subprocess.Popen(c, stdout=subprocess.PIPE, text=True, env=env) for k, c in cmds.items()}
        gpu = subprocess.Popen(["nvidia-smi", "--query-gpu=timestamp,utilization.gpu,memory.used",
                                "--format=csv,noheader,nounits", "-l", "1"],
                               stdout=subprocess.PIPE, text=True)
        time.sleep(WINDOW / 2)
        pss = {p: common.pss_kib(p) for p in pids}
        outputs = {k: r.communicate()[0] for k, r in runners.items()}
        we = ws + WINDOW * 1000
        gpu.terminate()
        gpu_out = gpu.communicate()[0]

        time.sleep(1)
        shot = os.path.join(common.SHOTS, f"t02-{v}.png")
        shot_cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "x11grab",
                    "-video_size", "1920x1080", "-i", os.environ.get("DISPLAY", ":0") + "+0,0",
                    "-frames:v", "1", shot]
        shot_rc = subprocess.run(shot_cmd).returncode

        out, err = app.communicate(timeout=DURATION_APP + 30)
    finally:
        if app.poll() is None:
            app.kill()
        if core:
            common.stop_core(core)

    for k, text in outputs.items():
        with open(os.path.join(outdir, f"{k}.txt"), "w") as f:
            f.write(text)
    with open(os.path.join(outdir, "nvidia-smi.csv"), "w") as f:
        f.write(gpu_out)
    with open(os.path.join(outdir, "app-stderr.txt"), "w") as f:
        f.write(err)

    per_t, per_cmd = parse_pidstat(outputs["pidstat"])
    gpu_util = [float(l.split(",")[1]) for l in gpu_out.splitlines() if l.count(",") == 2]
    gpu_mem = [float(l.split(",")[2]) for l in gpu_out.splitlines() if l.count(",") == 2]
    result = {
        "variant": v,
        "started": started,
        "app_command": cmd,
        "commands": {**cmds, "nvidia-smi": "nvidia-smi --query-gpu=timestamp,utilization.gpu,memory.used "
                     "--format=csv,noheader,nounits -l 1", "screenshot": " ".join(shot_cmd)},
        "app_exit_code": app.returncode,
        "processes": {str(p): n for p, n in procs.items()},
        "cpu_gesamt_prozent": stat(parse_mpstat(outputs["mpstat"])),
        "cpu_prozesse_prozent_eines_kerns": stat([s["cpu"] for s in per_t.values()]),
        "rss_prozesse_mib": stat([s["rss"] / 1024 for s in per_t.values()]),
        "pss_prozesse_mib_bei_30s": round(sum(x for x in pss.values() if x) / 1024, 1),
        "je_befehl": {c: {"cpu_mittel": round(sum(d["cpu"]) / len(d["cpu"]), 1),
                          "rss_max_mib": round(max(d["rss"]) / 1024, 1)} for c, d in per_cmd.items()},
        "gpu_auslastung_prozent": stat(gpu_util),
        "gpu_speicher_mib": stat(gpu_mem),
        "bildschirmfoto": os.path.relpath(shot, common.ROOT) if shot_rc == 0 else f"Fehler {shot_rc}",
    }
    if os.path.exists(stats_path):
        with open(stats_path) as f:
            s = json.load(f)
        result["bildrate"] = rate(s["frames"], ws, we)
        result["video_bilder"] = rate(s["video_frames"], ws, we)
        result["alpha_bilder"] = rate(s["alpha_frames"], ws, we)
        if "raf" in s:
            result["raf_bildwiederholung"] = rate(s["raf"], ws, we)
        result["app_info"] = s.get("info", {})
    else:
        result["bildrate"] = "keine Zeitstempel geschrieben"

    with open(os.path.join(outdir, "ergebnis.json"), "w") as f:
        json.dump(result, f, indent=1, ensure_ascii=False)
    short = {k: val for k, val in result.items()}
    for k in ("bildrate", "video_bilder", "alpha_bilder", "raf_bildwiederholung"):
        if isinstance(short.get(k), dict):
            short[k] = {kk: vv for kk, vv in short[k].items() if kk != "verlauf_pro_s"}
    print(json.dumps(short, indent=1, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
