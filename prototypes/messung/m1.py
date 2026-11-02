#!/usr/bin/env python3
"""M1 (Prompt P02): t02.py unverändert ausführen, nur Programm und Ablageorte umlenken.

  python3 prototypes/messung/m1.py A-libvpx   neuer Prototyp prototypes/a-cpp-qt-libvpx
  python3 prototypes/messung/m1.py A          Wiederholung P01-Variante A (Vergleich, gleiche Sitzung)
  python3 prototypes/messung/m1.py B          Wiederholung P01-Variante B (Vergleich, gleiche Sitzung)
  python3 prototypes/messung/m1.py A-libvpx t01   20 Starts mit t01.py (ebenfalls unverändert)

Ablage: docs/messungen/P02-daten/m1-<NAME>/t02-<A|B>/ und docs/messungen/P02-bilder/m1-<NAME>/t02-<A|B>.png.
t01.py und t02.py rufen common.app_cmd und common.OUT/SHOTS erst zur Laufzeit ab; deshalb genügt es, diese
Werte hier vor dem Aufruf zu ersetzen.
"""
import os
import sys

import common
import t01
import t02

A_LIBVPX_BIN = os.path.join(common.PROTO, "a-cpp-qt-libvpx", "build", "p02_a_libvpx")


def main():
    script = t01 if sys.argv[2:] == ["t01"] else t02
    if len(sys.argv) not in (2, 3) or sys.argv[1] not in ("A-libvpx", "A", "B"):
        print(__doc__)
        return 2
    name = sys.argv[1]
    common.OUT = os.path.join(common.ROOT, "docs", "messungen", "P02-daten", f"m1-{name}")
    common.SHOTS = os.path.join(common.ROOT, "docs", "messungen", "P02-bilder", f"m1-{name}")
    if name == "A-libvpx":
        original = common.app_cmd

        def app_cmd(variant, mode, stats=None, duration=None):
            cmd = original(variant, mode, stats, duration)
            return [A_LIBVPX_BIN] + cmd[1:]
        common.app_cmd = app_cmd
    sys.argv = [script.__name__ + ".py", "B" if name == "B" else "A"]
    return script.main()


if __name__ == "__main__":
    sys.exit(main())
