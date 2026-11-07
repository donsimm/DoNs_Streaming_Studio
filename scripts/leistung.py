#!/usr/bin/env python3
"""Leistung des Zeichners im Editor (P09), gemessen wie T-02 (P01/P02).

Overlay 1920x1080 mit 10 Quellen: alpha.webm (WebM-Alpha) und video.webm, beide «Wiederholen»,
bild1.png bis bild5.png (eines gedreht), drei Textquellen (eine gedreht). Eigener Datenordner,
eigener dss-core, eigene Verbindung und eigenes XDG_CONFIG_HOME; die Daten des Nutzers werden
nicht berührt. dss-app wählt das Overlay und schreibt 75 s lang Zeitstempel
(--leinwand-messung); nach 5 s Anlaufzeit laufen 60 s lang dieselben Befehle wie in T-02:
  LC_ALL=C mpstat 1 60
  LC_ALL=C pidstat -h -u -r -p <dss-app>,<dss-core> 1 60
  nvidia-smi --query-gpu=timestamp,utilization.gpu,memory.used --format=csv,noheader,nounits -l 1
und einmal PSS aus /proc/<pid>/smaps_rollup bei 30 s. Bildrate, Einbrüche (> 50 ms) und Videobilder
werden mit derselben Rechnung wie T-02 ausgewertet (prototypes/messung/t02.py: rate, stat,
parse_pidstat, parse_mpstat).

  python3 scripts/leistung.py [--build build] [--out build/leistung]

Quellen: pidstat(1), mpstat(1): https://sysstat.github.io/ (sysstat 12.7.5),
nvidia-smi --help-query-gpu.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.realpath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT, "prototypes", "messung"))
import common  # noqa: E402  (descendants, pss_kib)
import t02  # noqa: E402  (rate, stat, parse_pidstat, parse_mpstat)

MEDIA = os.path.join(ROOT, "prototypes", "media")
FONT = os.path.join(ROOT, "src", "common", "schriften", "be-vietnam-pro", "BeVietnamPro-Regular.ttf")
DURATION_APP = 75
WARMUP = 5
WINDOW = 60


def cli(build, data, *args):
    cmd = [os.path.join(build, "src", "cli", "dss-cli"), "--datenordner", data, *args]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    return cmd


def prepare(build, data):
    """Overlay mit 10 Quellen; gibt die dss-cli-Befehle zurück."""
    a = "messung-p09"
    cmds = [cli(build, data, "overlay-neu", "Messung P09", "1920", "1080")]
    cmds.append(cli(build, data, "quelle-video", a, os.path.join(MEDIA, "video.webm"), "--wiederholen"))
    cmds.append(cli(build, data, "quelle-video", a, os.path.join(MEDIA, "alpha.webm"), "--wiederholen"))
    for i in range(1, 6):
        cmds.append(cli(build, data, "quelle-bild", a, os.path.join(MEDIA, f"bild{i}.png")))
    for text in ("Willkommen im Stream", "Zweite Zeile\nmit Umbruch", "DoNs Streaming Studio"):
        cmds.append(cli(build, data, "quelle-text", a, text, FONT))
    # Lage: Nr. in der Zeichenreihenfolge (1 = hinten)
    geometry = [
        (1, 0, 0, 960, 540, 0),          # video.webm
        (2, 1200, 80, 512, 512, 0),      # alpha.webm über …
        (3, 1250, 120, 400, 400, 0),     # … bild1 liegt darüber (Reihenfolge wie hinzugefügt)
        (4, 100, 600, 256, 256, 150),    # gedreht 15°
        (5, 400, 600, 256, 256, 0),
        (6, 700, 600, 256, 256, 0),
        (7, 1000, 600, 256, 256, 0),
        (8, 100, 900, 900, 120, 25),     # Text gedreht 2.5°
        (9, 1100, 700, 700, 200, 0),
        (10, 1300, 950, 600, 100, 0),
    ]
    for n, x, y, w, h, r in geometry:
        cmds.append(cli(build, data, "quelle-geometrie", a, str(n), str(x), str(y), str(w), str(h),
                        f"{r / 10:.1f}"))
    return cmds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=os.path.join(ROOT, "build"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "leistung"))
    args = ap.parse_args()
    build = os.path.realpath(args.build)
    os.makedirs(args.out, exist_ok=True)

    work = tempfile.mkdtemp(prefix="dss-p09-leistung-")
    run = tempfile.mkdtemp(prefix="dss-p09-", dir=os.environ["XDG_RUNTIME_DIR"])
    data = os.path.join(work, "daten")
    cfg = os.path.join(work, "cfg")
    sock = os.path.join(run, "kern.sock")
    stats_path = os.path.join(work, "zeitstempel.json")
    cli_cmds = prepare(build, data)

    core_cmd = [os.path.join(build, "src", "core", "dss-core"), "--datenordner", data, "--verbindung", sock]
    core = subprocess.Popen(core_cmd, stderr=subprocess.PIPE, text=True)
    time.sleep(0.5)
    app_cmd = [os.path.join(build, "src", "app", "dss-app"), "--verbindung", sock,
               "--einheit", "dss-nicht-vorhanden.service",
               "--leinwand-messung", stats_path, "--messdauer", str(DURATION_APP)]
    env_app = dict(os.environ, XDG_CONFIG_HOME=cfg)
    started = time.strftime("%Y-%m-%d %H:%M:%S")
    app = subprocess.Popen(app_cmd, stderr=subprocess.PIPE, text=True, env=env_app)
    try:
        time.sleep(WARMUP)
        pids = [app.pid, core.pid]
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
                                "--format=csv,noheader,nounits", "-l", "1"], stdout=subprocess.PIPE, text=True)
        time.sleep(WINDOW / 2)
        pss = {p: common.pss_kib(p) for p in pids}
        outputs = {k: r.communicate()[0] for k, r in runners.items()}
        we = ws + WINDOW * 1000
        gpu.terminate()
        gpu_out = gpu.communicate()[0]
        app_err = app.communicate(timeout=DURATION_APP + 30)[1]
        core_err = core.communicate(timeout=15)[1]  # beendet sich mit dem einzigen Fenster
    finally:
        for p in (app, core):
            if p.poll() is None:
                p.kill()

    for k, text in outputs.items():
        with open(os.path.join(args.out, f"{k}.txt"), "w") as f:
            f.write(text)
    with open(os.path.join(args.out, "nvidia-smi.csv"), "w") as f:
        f.write(gpu_out)
    with open(os.path.join(args.out, "app-stderr.txt"), "w") as f:
        f.write(app_err)
    with open(os.path.join(args.out, "kern-stderr.txt"), "w") as f:
        f.write(core_err)

    per_t, per_cmd = t02.parse_pidstat(outputs["pidstat"])
    gpu_rows = [l.split(",") for l in gpu_out.splitlines() if l.count(",") == 2]
    result = {
        "started": started,
        "dss_cli": [" ".join(c[c.index("--datenordner") + 2:]) for c in cli_cmds],
        "core_command": " ".join(core_cmd),
        "app_command": " ".join(app_cmd),
        "commands": {**{k: " ".join(c) for k, c in cmds.items()},
                     "nvidia-smi": "nvidia-smi --query-gpu=timestamp,utilization.gpu,memory.used "
                                   "--format=csv,noheader,nounits -l 1"},
        "app_exit_code": app.returncode,
        "core_exit_code": core.returncode,
        "processes": {str(p): n for p, n in procs.items()},
        "cpu_gesamt_prozent": t02.stat(t02.parse_mpstat(outputs["mpstat"])),
        "cpu_prozesse_prozent_eines_kerns": t02.stat([s["cpu"] for s in per_t.values()]),
        "rss_prozesse_mib": t02.stat([s["rss"] / 1024 for s in per_t.values()]),
        "pss_prozesse_mib_bei_30s": round(sum(x for x in pss.values() if x) / 1024, 1),
        "je_befehl": {c: {"cpu_mittel": round(sum(d["cpu"]) / len(d["cpu"]), 1),
                          "rss_max_mib": round(max(d["rss"]) / 1024, 1)} for c, d in per_cmd.items()},
        "gpu_auslastung_prozent": t02.stat([float(r[1]) for r in gpu_rows]),
        "gpu_speicher_mib": t02.stat([float(r[2]) for r in gpu_rows]),
    }
    if os.path.exists(stats_path):
        with open(stats_path) as f:
            s = json.load(f)
        result["bildrate_leinwand"] = t02.rate(s["frames"], ws, we)
        result["video_bilder"] = t02.rate(s["video_frames"], ws, we)
        result["alpha_bilder"] = t02.rate(s["alpha_frames"], ws, we)
        result["app_info"] = s.get("info", {})
    else:
        result["bildrate_leinwand"] = "keine Zeitstempel geschrieben"
    with open(os.path.join(args.out, "ergebnis.json"), "w") as f:
        json.dump(result, f, indent=1, ensure_ascii=False)
    short = dict(result)
    for k in ("bildrate_leinwand", "video_bilder", "alpha_bilder"):
        if isinstance(short.get(k), dict):
            short[k] = {kk: vv for kk, vv in short[k].items() if kk != "verlauf_pro_s"}
    short.pop("dss_cli")
    print(json.dumps(short, indent=1, ensure_ascii=False))
    shutil.rmtree(work)
    shutil.rmtree(run)
    return 0 if app.returncode == 0 and core.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
