"""OBSBOT Tiny 2: Statusblock, Wecken, Schlafen, HDR (Plan 2.3 Regeln 1 und 2, «Ergebnis Schritt 0»).

- Statusblock: `GET_CUR` auf Selektor 0x06 (60 Byte). Byte 0x09 = Zustand (1 läuft,
  3 Schlaf, 4 Privatsphäre), Byte 0x06 = HDR, Bytes 0x0A–0x0B = Auto-Schlaf in Sekunden
  (Schritt 0: 120; Reihenfolge der Bytes ist eine Annahme, little-endian wie die Frames).
- Wecken und Schlafen über das Frame-Protokoll auf Selektor 0x02: Befehl 0xA0C2 mit
  Nutzlast u32, 0 = wecken, 1 = schlafen.
- Nach dem Wecken meldet die Kamera nach ~1,5 s Zustand 1, fährt dann aber noch in die
  Grundstellung und verwirft Bewegungsbefehle still (Schritt 0, Zeile 7). Darum wartet der
  Dienst danach noch `AFTER_WAKE_S`, bevor ein Bewegungsbefehl ans Gerät geht.
- HDR: Befehl `01 01 00` (aus) / `01 01 01` (an) auf Selektor 0x06, Status 0x06 folgt.
- Gesperrt, weil in Schritt 0 nicht bestätigt (nicht angeboten): Bildwinkel (`04 01 xx`)
  und KI-Modus «keine» (`16 02 00 00`). Auto-Schlaf wird nur gelesen, nie geschrieben.

Die Funktionen hier arbeiten auf einem geöffneten Gerät (`xu_get`, `xu_set`); Sperre und
Suche der Kamera liegen bei `controls.ControlManager`.

Quellen:
https://github.com/cgevans/tiny2 (src/frame.rs: Aufbau der Frames und CRC; Befehl 0xA0C2)
https://reveng.sourceforge.io/crc-catalogue/16.htm (CRC-16/USB: Poly 0x8005 gespiegelt, Start 0xFFFF, Ausgang xor 0xFFFF)
"""

import logging
import struct
import time
from collections.abc import Callable

from .http_api import ApiError
from .uvc_xu import SELECTOR_COMMAND, SELECTOR_FRAME, XU_LENGTH

log = logging.getLogger("dss_camera.obsbot")

MODEL = "OBSBOT Tiny 2"

CMD_SLEEP_WAKE = 0xA0C2
PAYLOAD_WAKE = 0
PAYLOAD_SLEEP = 1

STATUS_HDR = 0x06
STATUS_RUN = 0x09
STATUS_AUTO_SLEEP = 0x0A

RUN_AWAKE = 1
RUN_ASLEEP = 3
RUN_PRIVACY = 4
STATES = {RUN_AWAKE: "awake", RUN_ASLEEP: "asleep", RUN_PRIVACY: "privacy"}

# Schritt 0: Zustand 1 nach 1,51–1,61 s; höchstens 10 s warten (Plan 2.3 Regel 2).
WAKE_TIMEOUT_S = 10.0
# Schritt 0: mit 2,5 s Pause kein Schwenk, mit 4 s sichtbar; Prüfung b misst genauer.
AFTER_WAKE_S = 4.5
# Schritt 0: Zustand 3 nach ~0,1 s, einmal 1,2 s.
SLEEP_TIMEOUT_S = 10.0
HDR_READBACK_S = 1.0
POLL_S = 0.1
# Zustand für GET status höchstens alle 5 s lesen (Suche läuft jede Sekunde).
STATE_INTERVAL_S = 5.0

KEY_HDR = "obsbot_hdr"
KEY_STATE = "obsbot_state"
KEY_AUTO_SLEEP = "obsbot_auto_sleep"
KEYS = (KEY_HDR, KEY_STATE, KEY_AUTO_SLEEP)
GROUP = "OBSBOT"
STATE_LABELS = {RUN_AWAKE: "Wach", RUN_ASLEEP: "Schlaf", RUN_PRIVACY: "Privatsphäre"}


def is_tiny2(model: str) -> bool:
    return model == MODEL


def crc16_usb(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc ^ 0xFFFF


def build_frame(seq: int, cmd: int, payload: bytes) -> bytes:
    """Frame nach cgevans/tiny2 `frame.rs`; von Lotse in Schritt 0 am Gerät geprüft."""
    frame = bytearray(XU_LENGTH)
    frame[0], frame[1] = 0xAA, 0x25
    struct.pack_into("<HH", frame, 2, seq & 0xFFFF, 0x0C)
    frame[8], frame[9] = 0x0A, 0x02
    struct.pack_into("<HH", frame, 10, cmd, len(payload))
    frame[16:16 + len(payload)] = payload
    struct.pack_into("<H", frame, 6, crc16_usb(frame[0:12]))
    struct.pack_into("<H", frame, 14, crc16_usb(frame[12:16 + len(payload)]))
    return bytes(frame)


def command(*data: int) -> bytes:
    """Kurzbefehl für Selektor 0x06, mit Nullen auf 60 Byte aufgefüllt."""
    return bytes(data).ljust(XU_LENGTH, b"\0")


def run_state(block: bytes) -> int:
    return block[STATUS_RUN]


def state_name(run: int) -> str:
    return STATES.get(run, "unknown")


def describe(block: bytes, remembered: dict) -> list[dict]:
    """Werte der Gruppe «OBSBOT» im Format von GET controls (Plan 4)."""
    run = run_state(block)
    menu = [{"value": value, "label": text} for value, text in STATE_LABELS.items()]
    if run not in STATE_LABELS:
        menu.append({"value": run, "label": f"Unbekannt ({run})"})
    auto_sleep = struct.unpack_from("<H", block, STATUS_AUTO_SLEEP)[0]
    common = {"group": GROUP, "step": 1, "inactive": False}
    # HDR-Standard des Geräts ist nicht bekannt; «aus» ist eine Annahme.
    return [
        {**common, "key": KEY_HDR, "label": "HDR", "type": "bool", "min": 0, "max": 1,
         "default": False, "value": bool(block[STATUS_HDR]), "menu": None,
         "remembered": KEY_HDR in remembered, "readonly": False},
        {**common, "key": KEY_STATE, "label": "Zustand", "type": "menu", "min": 0, "max": 255,
         "default": RUN_AWAKE, "value": run, "menu": menu, "remembered": False, "readonly": True},
        {**common, "key": KEY_AUTO_SLEEP, "label": "Automatisch schlafen nach (s)", "type": "int",
         "min": 0, "max": 65535, "default": auto_sleep, "value": auto_sleep, "menu": None,
         "remembered": False, "readonly": True},
    ]


def to_raw(key: str, value) -> int:
    """Prüft einen Wert aus PUT controls; nur HDR ist schreibbar."""
    if key != KEY_HDR:
        raise ApiError(400, "invalid", f"«{key}» ist nur lesbar.")
    if value is None:
        return 0
    if not isinstance(value, bool):
        raise ApiError(400, "invalid", "«HDR» erwartet true oder false.")
    return int(value)


class Obsbot:
    """Ablauf mit der Tiny 2 auf einem geöffneten Gerät; Uhr und Warten sind ersetzbar."""

    def __init__(self, clock: Callable[[], float] = time.monotonic,
                 sleep: Callable[[float], None] = time.sleep) -> None:
        self._clock = clock
        self._sleep = sleep
        self._seq = 0
        # Kamera → frühester Zeitpunkt für Bewegungsbefehle nach dem Wecken.
        self._ready_at: dict[str, float] = {}
        self._last_run: dict[str, int] = {}
        self._last_read: dict[str, float] = {}

    def read_block(self, device, camera_id: str) -> bytes:
        block = device.xu_get(SELECTOR_COMMAND, XU_LENGTH)
        self.note(camera_id, run_state(block))
        return block

    def note(self, camera_id: str, run: int) -> None:
        """Zustand merken; ist die Kamera ohne den Dienst aufgewacht, gilt die Wartezeit ab jetzt."""
        now = self._clock()
        previous = self._last_run.get(camera_id)
        if run == RUN_AWAKE and previous is not None and previous != RUN_AWAKE:
            self._ready_at.setdefault(camera_id, now + AFTER_WAKE_S)
        elif run != RUN_AWAKE:
            self._ready_at.pop(camera_id, None)
        self._last_run[camera_id] = run
        self._last_read[camera_id] = now

    def due(self, camera_id: str) -> bool:
        last = self._last_read.get(camera_id)
        return last is None or self._clock() - last >= STATE_INTERVAL_S

    def forget(self, camera_id: str) -> None:
        """Beim Ausstecken: Zustand und Wartezeit neu lesen, sobald die Kamera wieder da ist."""
        for table in (self._ready_at, self._last_run, self._last_read):
            table.pop(camera_id, None)

    def send_frame(self, device, cmd: int, payload: bytes) -> None:
        self._seq = (self._seq + 1) & 0xFFFF
        device.xu_set(SELECTOR_FRAME, build_frame(self._seq, cmd, payload))

    def _wait_for(self, device, camera_id: str, run: int, timeout_s: float) -> bool:
        deadline = self._clock() + timeout_s
        while True:
            if run_state(self.read_block(device, camera_id)) == run:
                return True
            if self._clock() >= deadline:
                return False
            self._sleep(POLL_S)

    def wake(self, device, camera_id: str) -> None:
        """Weckt und wartet auf Zustand 1; Bewegungsbefehle erst nach `AFTER_WAKE_S` (`wait_ready`)."""
        if run_state(self.read_block(device, camera_id)) == RUN_AWAKE:
            return
        self.send_frame(device, CMD_SLEEP_WAKE, struct.pack("<I", PAYLOAD_WAKE))
        if not self._wait_for(device, camera_id, RUN_AWAKE, WAKE_TIMEOUT_S):
            raise ApiError(504, "timeout", f"Kamera nach {WAKE_TIMEOUT_S:.0f} s nicht wach.")
        self._ready_at[camera_id] = self._clock() + AFTER_WAKE_S
        log.info("%s geweckt.", camera_id)

    def send_to_sleep(self, device, camera_id: str) -> None:
        self.send_frame(device, CMD_SLEEP_WAKE, struct.pack("<I", PAYLOAD_SLEEP))
        if not self._wait_for(device, camera_id, RUN_ASLEEP, SLEEP_TIMEOUT_S):
            raise ApiError(504, "timeout", f"Kamera nach {SLEEP_TIMEOUT_S:.0f} s nicht im Schlaf.")
        log.info("%s schlafen gelegt.", camera_id)

    def wait_ready(self, camera_id: str) -> None:
        remaining = self._ready_at.get(camera_id, 0.0) - self._clock()
        if remaining > 0:
            self._sleep(remaining)
        self._ready_at.pop(camera_id, None)

    def ensure_ready(self, device, camera_id: str, wake: bool) -> None:
        """Plan 2.3 Regeln 1 und 2: vor jedem Bewegungsbefehl Zustand lesen, bei Bedarf wecken."""
        run = run_state(self.read_block(device, camera_id))
        if run in (RUN_ASLEEP, RUN_PRIVACY):
            if not wake:
                what = "schläft" if run == RUN_ASLEEP else "ist in Privatsphäre"
                raise ApiError(409, "camera_asleep", f"Kamera {what}; Bewegung nur mit Wecken.")
            self.wake(device, camera_id)
        elif run != RUN_AWAKE:
            raise ApiError(409, "camera_asleep", f"Zustand der Kamera unbekannt ({run}), keine Bewegung.")
        self.wait_ready(camera_id)

    def set_hdr(self, device, camera_id: str, raw: int) -> bool:
        """Setzt HDR und liest zurück (Status 0x06); nur im Zustand 1."""
        device.xu_set(SELECTOR_COMMAND, command(0x01, 0x01, raw))
        deadline = self._clock() + HDR_READBACK_S
        while True:
            value = self.read_block(device, camera_id)[STATUS_HDR]
            if value == raw or self._clock() >= deadline:
                return bool(value)
            self._sleep(POLL_S)
