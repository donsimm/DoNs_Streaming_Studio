#!/usr/bin/env python3
"""M2 (Prompt P02): Variante B mit Bildabgriff 60 s lang messen.

Gleicher Ablauf und gleiche Werkzeuge wie t02.py (Auswertung von dort übernommen, t02.py bleibt
unverändert): Kern und Abgriff-Programm starten (Laufzeit 75 s), 5 s Anlaufzeit, dann 60 s
  LC_ALL=C mpstat 1 60
  LC_ALL=C pidstat -h -u -r -p <PIDs> 1 60
  nvidia-smi --query-gpu=timestamp,utilization.gpu,memory.used --format=csv,noheader,nounits -l 1
und PSS bei 30 s. Danach ein Bildschirmfoto (zeigt, ob das Overlay sichtbar war) und bei 68 s je
Overlay ein Kontrollbild aus dem Speicher (PNG und UYVY).

  python3 prototypes/messung/m2.py NAME --count 1|2 --visibility visible|covered|minimized|offscreen
                                        [--grab framebuffer|widget]

Quellen: siehe t02.py.
"""
import argparse
import json
import os
import statistics
import subprocess
import sys
import time

import common
import t02

OUT = os.path.join(common.ROOT, "docs", "archiv", "messungen", "P02-daten")
SHOTS = os.path.join(common.ROOT, "docs", "archiv", "messungen", "P02-bilder")
BIN = os.path.join(common.PROTO, "b-web-capture", "host", "build", "p02_b_capture")


def ms_stat(vals):
    if not vals:
        return None
    s = sorted(vals)
    return {"median": round(statistics.median(s), 2), "mittel": round(sum(s) / len(s), 2),
            "p95": round(s[int(0.95 * (len(s) - 1))], 2), "max": round(s[-1], 2)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("name")
    ap.add_argument("--count", type=int, default=1)
    ap.add_argument("--visibility", default="visible")
    ap.add_argument("--grab", default="framebuffer")
    args = ap.parse_args()

    outdir = os.path.join(OUT, f"m2-{args.name}")
    os.makedirs(outdir, exist_ok=True)
    os.makedirs(SHOTS, exist_ok=True)
    stats_path = os.path.join(outdir, "app-zeitstempel.json")
    if os.path.exists(stats_path):
        os.remove(stats_path)
    frame_prefix = os.path.join(outdir, "abgriff")

    core = common.start_core()
    cmd = [BIN, "--url", f"http://127.0.0.1:{common.CORE_PORT}/overlay.html", "--count", str(args.count),
           "--visibility", args.visibility, "--grab", args.grab, "--duration", str(t02.DURATION_APP),
           "--stats", stats_path, "--save-frame", frame_prefix, "--save-at", "68"]
    started = common.now_iso()
    app = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                           start_new_session=True)
    try:
        time.sleep(t02.WARMUP)
        pids = common.descendants(app.pid) + [core.pid]
        procs = {p: common.comm(p) for p in pids}
        env = dict(os.environ, LC_ALL="C")
        cmds = {
            "mpstat": ["mpstat", "1", str(t02.WINDOW)],
            "pidstat": ["pidstat", "-h", "-u", "-r", "-p", ",".join(str(p) for p in pids), "1", str(t02.WINDOW)],
        }
        ws = time.time() * 1000
        runners = {k: subprocess.Popen(c, stdout=subprocess.PIPE, text=True, env=env) for k, c in cmds.items()}
        gpu = subprocess.Popen(["nvidia-smi", "--query-gpu=timestamp,utilization.gpu,memory.used",
                                "--format=csv,noheader,nounits", "-l", "1"], stdout=subprocess.PIPE, text=True)
        time.sleep(t02.WINDOW / 2)
        pss = {p: common.pss_kib(p) for p in pids}
        outputs = {k: r.communicate()[0] for k, r in runners.items()}
        we = ws + t02.WINDOW * 1000
        gpu.terminate()
        gpu_out = gpu.communicate()[0]

        time.sleep(1)
        shot = os.path.join(SHOTS, f"m2-{args.name}-bildschirm.png")
        shot_cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "x11grab",
                    "-video_size", "1920x1080", "-i", os.environ.get("DISPLAY", ":0") + "+0,0",
                    "-frames:v", "1", shot]
        shot_rc = subprocess.run(shot_cmd).returncode
        out, err = app.communicate(timeout=t02.DURATION_APP + 30)
    finally:
        if app.poll() is None:
            app.kill()
        common.stop_core(core)

    for k, text in outputs.items():
        with open(os.path.join(outdir, f"{k}.txt"), "w") as f:
            f.write(text)
    with open(os.path.join(outdir, "nvidia-smi.csv"), "w") as f:
        f.write(gpu_out)
    with open(os.path.join(outdir, "app-stderr.txt"), "w") as f:
        f.write(err)

    # Kontrollbilder: PNG ins Bilderverzeichnis, UYVY mit ffmpeg zurück nach PNG (prüft die Umwandlung)
    controls = []
    for n in range(1, args.count + 1):
        png, raw = f"{frame_prefix}-{n}.png", f"{frame_prefix}-{n}.uyvy"
        if os.path.exists(png):
            dst = os.path.join(SHOTS, f"m2-{args.name}-abgriff-{n}.png")
            os.replace(png, dst)
            controls.append(os.path.relpath(dst, common.ROOT))
        if os.path.exists(raw):
            dst = os.path.join(SHOTS, f"m2-{args.name}-uyvy-{n}.png")
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo",
                            "-pix_fmt", "uyvy422", "-video_size", "1920x1080", "-i", raw,
                            "-frames:v", "1", dst], check=False)
            os.remove(raw)
            controls.append(os.path.relpath(dst, common.ROOT))

    per_t, per_cmd = t02.parse_pidstat(outputs["pidstat"])
    gpu_rows = [l.split(",") for l in gpu_out.splitlines() if l.count(",") == 2]
    result = {
        "name": args.name,
        "started": started,
        "app_command": cmd,
        "commands": {**cmds, "nvidia-smi": "nvidia-smi --query-gpu=timestamp,utilization.gpu,memory.used "
                     "--format=csv,noheader,nounits -l 1", "screenshot": " ".join(shot_cmd)},
        "app_exit_code": app.returncode,
        "processes": {str(p): n for p, n in procs.items()},
        "cpu_gesamt_prozent": t02.stat(t02.parse_mpstat(outputs["mpstat"])),
        "cpu_prozesse_prozent_eines_kerns": t02.stat([s["cpu"] for s in per_t.values()]),
        "rss_prozesse_mib": t02.stat([s["rss"] / 1024 for s in per_t.values()]),
        "pss_prozesse_mib_bei_30s": round(sum(x for x in pss.values() if x) / 1024, 1),
        "je_befehl": {c: {"cpu_mittel": round(sum(d["cpu"]) / len(d["cpu"]), 1),
                          "rss_max_mib": round(max(d["rss"]) / 1024, 1)} for c, d in per_cmd.items()},
        "gpu_auslastung_prozent": t02.stat([float(r[1]) for r in gpu_rows]),
        "gpu_speicher_mib": t02.stat([float(r[2]) for r in gpu_rows]),
        "bildschirmfoto": os.path.relpath(shot, common.ROOT) if shot_rc == 0 else f"Fehler {shot_rc}",
        "kontrollbilder": controls,
    }
    if os.path.exists(stats_path):
        with open(stats_path) as f:
            s = json.load(f)
        result["app_info"] = s.get("info", {})
        result["overlays"] = []
        for o in s["overlays"]:
            in_win = [i for i, t in enumerate(o["grabs"]) if ws <= t < we]
            conv = o["convert"]
            done_in = [i for i, t in enumerate(conv["done_epoch_ms"]) if ws <= t < we]
            page = o.get("page", {})
            result["overlays"].append({
                "abgriffe": t02.rate(o["grabs"], ws, we),
                "neue_bilder": t02.rate(o["new_frames"], ws, we),
                "abgriff_ms": ms_stat([o["grab_ms"][i] for i in in_win]),
                "vergleich_ms": ms_stat([o["compare_ms"][i] for i in in_win]),
                "umwandlung_format_ms": ms_stat([conv["format_ms"][i] for i in done_in]),
                "umwandlung_uyvy_ms": ms_stat([conv["uyvy_ms"][i] for i in done_in]),
                "umwandlung_uyva_ms": ms_stat([conv["uyva_ms"][i] for i in done_in]),
                "umwandlungen_im_fenster": len(done_in),
                "verworfen_gesamt": conv["dropped"],
                "seite_bildrate": t02.rate(page.get("frames", []), ws, we),
                "seite_raf": t02.rate(page.get("raf", []), ws, we),
                "seite_video": t02.rate(page.get("video_frames", []), ws, we),
                "seite_alpha": t02.rate(page.get("alpha_frames", []), ws, we),
                "info": o["info"],
                "eingabeformat_umwandlung": conv["input_format"],
            })
    else:
        result["overlays"] = "keine Zeitstempel geschrieben"

    with open(os.path.join(outdir, "ergebnis.json"), "w") as f:
        json.dump(result, f, indent=1, ensure_ascii=False)

    def strip(x):
        if isinstance(x, dict):
            return {k: strip(v) for k, v in x.items() if k != "verlauf_pro_s"}
        if isinstance(x, list):
            return [strip(v) for v in x]
        return x
    print(json.dumps(strip(result), indent=1, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
