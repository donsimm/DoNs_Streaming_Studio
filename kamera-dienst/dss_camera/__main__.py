"""Start des Kamera-Dienstes: python3 -m dss_camera [--konfig PFAD] [--attrappe [--geraeteordner PFAD]].

Exit-Codes: 0 sauber beendet, 1 dienst.ini ungültig, 2 falsche Optionen,
3 Sperre (läuft schon), 4 Lauschadresse oder Port nicht nutzbar.
"""

import argparse
import logging
import os
import signal
import sys
import threading
from pathlib import Path

from . import SERVICE_VERSION, config, lock
from .backend import DeviceBackend
from .devices import DeviceWatcher
from .fake import FakeBackend, fake_watcher
from .http_api import Api, Servers

EXIT_CONFIG = 1
EXIT_LOCK = 3
EXIT_LISTEN = 4

log = logging.getLogger("dss_camera")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="dss_camera", description="Kamera-Dienst des DoNs Streaming Studio")
    parser.add_argument("--konfig", type=Path, default=None,
                        help="Pfad der dienst.ini (Standard: $XDG_CONFIG_HOME/dss-kamera/dienst.ini)")
    parser.add_argument("--attrappe", action="store_true",
                        help="erfundene Kameras und Lampen statt Geräten (für Tests)")
    parser.add_argument("--geraeteordner", type=Path, default=None,
                        help="nur mit --attrappe: Kameras aus diesem Ordner statt /dev und /sys (für Tests)")
    args = parser.parse_args(argv)
    if args.geraeteordner is not None and not args.attrappe:
        parser.error("--geraeteordner geht nur mit --attrappe")

    # Protokoll nach stderr, unter systemd im Journal (Plan 6).
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s", stream=sys.stderr)

    path = args.konfig or config.default_path()
    try:
        settings = config.load(path)
    except config.ConfigError as error:
        log.error("dienst.ini ungültig (%s): %s", path, error)
        return EXIT_CONFIG
    if not settings.file_found:
        log.info("%s fehlt, es gelten die Standardwerte.", path)

    try:
        lock_fd = lock.acquire()
    except lock.LockError as error:
        log.error("%s", error)
        return EXIT_LOCK

    devices = None
    if args.attrappe:
        if args.geraeteordner is not None:
            devices = fake_watcher(args.geraeteordner)
        backend = FakeBackend(devices)
    else:
        devices = DeviceWatcher()
        backend = DeviceBackend(devices)
    api = Api(backend, fake=args.attrappe)
    try:
        servers = Servers(api, settings.listen_addresses, settings.port, settings.allowed_addresses)
    except OSError as error:
        log.error("Lauschen auf %s Port %d nicht möglich: %s.",
                  ", ".join(settings.listen_addresses), settings.port, error.strerror)
        os.close(lock_fd)
        return EXIT_LISTEN
    if devices is not None:
        devices.start()

    stop = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    servers.start()
    log.info("Kamera-Dienst %s bereit%s: %s, Port %d.", SERVICE_VERSION,
             " (Attrappe)" if args.attrappe else "",
             ", ".join(settings.listen_addresses), settings.port)
    # Kurze Wartezeit statt wait() ohne Grenze, damit die Signal-Behandlung sicher drankommt.
    while not stop.wait(0.5):
        pass
    servers.close()
    if devices is not None:
        devices.close()
    os.close(lock_fd)
    log.info("Kamera-Dienst beendet.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
