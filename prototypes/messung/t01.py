#!/usr/bin/env python3
"""T-01: n Starts einer Variante in der echten X11-Sitzung.

Je Start: Exit-Code, Dauer bis zum ersten Bild (Zeit vor Popen bis FIRST_FRAME_NS der App,
beides CLOCK_MONOTONIC), Gesamtdauer, sauberes Beenden (keine Restprozesse in der Sitzung
des Prozesses 2 s nach Ende, keine Zeitüberschreitung).

  python3 prototypes/messung/t01.py A|B|C [--runs 20]
"""
import argparse
import json
import os
import subprocess
import sys
import time

import common


def one_run(variant, idx, errdir):
    cmd = common.app_cmd(variant, "t01")
    t0 = time.monotonic_ns()
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                         start_new_session=True)
    timeout = False
    try:
        out, err = p.communicate(timeout=30)
    except subprocess.TimeoutExpired:
        timeout = True
        p.kill()
        out, err = p.communicate()
    t_end = time.monotonic_ns()

    first = None
    for line in out.splitlines():
        if line.startswith("FIRST_FRAME_NS "):
            first = int(line.split()[1])

    # Restprozesse der Sitzung (z. B. QtWebEngineProcess) bis 2 s nach Ende
    leftovers = common.session_members(p.pid)
    deadline = time.monotonic() + 2
    while leftovers and time.monotonic() < deadline:
        time.sleep(0.1)
        leftovers = common.session_members(p.pid)
    leftover_names = [common.comm(x) for x in leftovers]
    for pid in leftovers:
        try:
            os.kill(pid, 9)
        except OSError:
            pass

    err_lines = [l for l in err.splitlines() if l.strip()]
    if err_lines:
        with open(os.path.join(errdir, f"stderr-{idx:02d}.txt"), "w") as f:
            f.write(err)
    return {
        "run": idx,
        "exit_code": p.returncode,
        "first_frame_ms": round((first - t0) / 1e6, 1) if first else None,
        "total_ms": round((t_end - t0) / 1e6, 1),
        "timeout": timeout,
        "leftover_processes": leftover_names,
        "stderr_lines": len(err_lines),
        "clean": (p.returncode == 0 and not timeout and not leftovers and first is not None),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("variant", choices=["A", "B", "C"])
    ap.add_argument("--runs", type=int, default=20)
    args = ap.parse_args()

    outdir = os.path.join(common.OUT, f"t01-{args.variant}")
    os.makedirs(outdir, exist_ok=True)
    core = common.start_core() if args.variant == "B" else None
    started = common.now_iso()
    results = []
    try:
        for i in range(1, args.runs + 1):
            r = one_run(args.variant, i, outdir)
            results.append(r)
            print(json.dumps(r, ensure_ascii=False), flush=True)
            time.sleep(1)
    finally:
        if core:
            common.stop_core(core)

    ff = [r["first_frame_ms"] for r in results if r["first_frame_ms"] is not None]
    summary = {
        "variant": args.variant,
        "started": started,
        "command": common.app_cmd(args.variant, "t01"),
        "runs": len(results),
        "clean": sum(r["clean"] for r in results),
        "first_frame_ms_min": min(ff) if ff else None,
        "first_frame_ms_median": sorted(ff)[len(ff) // 2] if ff else None,
        "first_frame_ms_max": max(ff) if ff else None,
    }
    with open(os.path.join(outdir, "ergebnis.json"), "w") as f:
        json.dump({"summary": summary, "runs": results}, f, indent=1, ensure_ascii=False)

    lines = ["| Start | Exit-Code | erstes Bild (ms) | Gesamtdauer (ms) | Restprozesse | stderr-Zeilen | sauber |",
             "|---|---|---|---|---|---|---|"]
    for r in results:
        lines.append(f"| {r['run']} | {r['exit_code']} | {r['first_frame_ms']} | {r['total_ms']} | "
                     f"{', '.join(r['leftover_processes']) or 'keine'} | {r['stderr_lines']} | "
                     f"{'ja' if r['clean'] else 'nein'} |")
    with open(os.path.join(outdir, "tabelle.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(json.dumps(summary, ensure_ascii=False))
    return 0 if summary["clean"] == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
