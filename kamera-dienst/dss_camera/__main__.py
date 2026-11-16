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
from .controls import ControlManager, open_v4l2
from .devices import DeviceWatcher
from .events import Events
from .export import export
from .fake import FakeBackend, fake_watcher, open_fake_device
from .http_api import Api, Servers
from .lights import LightPresets, LightPresetStore, Lights, LightStore
from .motion import Mover
from .presets import CameraPresets, PresetStore
from .store import CameraStore, default_data_dir

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
    open_device = open_v4l2
    if args.attrappe:
        if args.geraeteordner is not None:
            devices = fake_watcher(args.geraeteordner)
            open_device = open_fake_device
        backend = FakeBackend(devices)
    else:
        devices = DeviceWatcher()
        backend = DeviceBackend(devices)
    manager = None
    mover = None
    presets = None
    lights = None
    light_presets = None
    if devices is not None:
        # Gemerkte Werte in $XDG_DATA_HOME/dss-kamera/kameras.json (Plan 3).
        store = CameraStore(default_data_dir())
        manager = ControlManager(devices, store, open_device)
        devices.add_listener(manager.restore_pending)
        mover = Mover(manager, store)
        backend.camera_store = store
        backend.preset_store = PresetStore(default_data_dir())
        presets = CameraPresets(backend.preset_store, mover, manager)
        # Lampen aus licht.json, Licht-Presets aus licht-presets.json (Plan 2.6, 3).
        lights = Lights(LightStore(default_data_dir()))
        backend.light_controller = lights
        backend.light_preset_store = LightPresetStore(default_data_dir())
        light_presets = LightPresets(backend.light_preset_store, lights)
    # GET events (SSE) und GET export (Plan 4).
    events = Events(backend, None if manager is None else manager.get)
    if manager is not None:
        manager.on_change = events.controls_changed
        mover.on_config = events.config_changed
    data_dir = default_data_dir()
    api = Api(backend, fake=args.attrappe, controls=manager, mover=mover, presets=presets,
              lights=lights, light_presets=light_presets, events=events,
              export=lambda: export(data_dir))
    try:
        servers = Servers(api, settings.listen_addresses, settings.port, settings.allowed_addresses)
    except OSError as error:
        log.error("Lauschen auf %s Port %d nicht möglich: %s.",
                  ", ".join(settings.listen_addresses), settings.port, error.strerror)
        os.close(lock_fd)
        return EXIT_LISTEN
    if devices is not None:
        devices.start()
        mover.start()
        lights.start()
    events.start()

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
    # Erst die Ereignisse: Offene GET events enden, dann die Server.
    events.close()
    servers.close()
    if devices is not None:
        lights.close()
        mover.close()
        devices.close()
    os.close(lock_fd)
    log.info("Kamera-Dienst beendet.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
