"""Ereignisse für `GET events` als Server-Sent Events (Plan 4).

Arten, jede mit dem ganzen neuen Stand ihres Teils:

- `status`   `{"cameras": […]}` wie in GET status (erschienen, verschwunden, wach, Schlaf)
- `controls` `{"camera": "<kennung>", "controls": […]}` wie GET controls; nach geänderten
  Bildwerten, nach Bewegen (stop, Totmann, step, home), nach dem Abruf eines Presets und
  nach dem Neusetzen gemerkter Werte
- `presets`  `{"camera_presets": […], "light_presets": […]}` mit allen Feldern
- `lights`   `{"lights": […]}` wie in GET status (Schalten, Nachführen, Lampen verwalten)
- `config`   `{"camera": "<kennung>", "config": {…}, "ranges": {…}, "limits": {…}}` wie
  GET cameras/{cam}/config; nach jedem PUT config (ein neuer Name kommt zudem mit `status`)

`status`, `presets` und `lights` vergleicht ein eigener Faden alle `INTERVAL_S` mit dem
letzten Stand; schnelle Folgen von Änderungen können so zu einem Ereignis zusammenfallen.
`controls` meldet `ControlManager.on_change`; gelesen wird im selben Faden, nie in der Anfrage.

Jeder Abonnent hat eine eigene Warteschlange mit höchstens `QUEUE_LIMIT` Ereignissen.
Läuft sie über, wird er abgemeldet: Ein Abonnent, der nicht liest, hält den Dienst nie auf.
Format: https://html.spec.whatwg.org/multipage/server-sent-events.html (nicht abgerufen).
"""

import json
import logging
import threading
from collections import deque
from collections.abc import Callable

from .http_api import ApiError

INTERVAL_S = 0.2
QUEUE_LIMIT = 200
# Reihenfolge der Arten beim Anmelden (aktueller Stand vor allen Änderungen).
INITIAL_KINDS = ("status", "presets", "lights")

log = logging.getLogger("dss_camera.events")


class Subscriber:
    def __init__(self, limit: int) -> None:
        self._limit = limit
        self._queue: deque[bytes] = deque()
        self._cond = threading.Condition()
        # Abgemeldet (Überlauf, Ende des Dienstes oder Verbindung zu); der Faden der Anfrage endet.
        self.closed = False

    def put(self, event: bytes) -> bool:
        """Hängt ein Ereignis an; False bei Überlauf (dann nichts angehängt)."""
        with self._cond:
            if len(self._queue) >= self._limit:
                return False
            self._queue.append(event)
            self._cond.notify()
            return True

    def get(self, timeout: float) -> bytes | None:
        """Nächstes Ereignis oder None nach `timeout` bzw. beim Schliessen."""
        with self._cond:
            if not self._queue and not self.closed:
                self._cond.wait(timeout)
            return self._queue.popleft() if self._queue else None

    def close(self) -> None:
        with self._cond:
            self.closed = True
            self._cond.notify_all()


def encode(seq: int, kind: str, data: dict) -> bytes:
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":"))
    return f"id: {seq}\nevent: {kind}\ndata: {payload}\n\n".encode("utf-8")


class EventHub:
    def __init__(self, queue_limit: int = QUEUE_LIMIT) -> None:
        self._queue_limit = queue_limit
        self._lock = threading.Lock()
        self._seq = 0
        self._subscribers: list[Subscriber] = []
        # Kennung und letzter Stand je Teil («status», «presets», «lights», («controls», Kamera)).
        self._last: dict = {}
        self._closed = False

    def count(self) -> int:
        with self._lock:
            return len(self._subscribers)

    def subscribe(self) -> Subscriber:
        """Neuer Abonnent; er bekommt zuerst den aktuellen Stand von status, presets, lights."""
        subscriber = Subscriber(self._queue_limit)
        with self._lock:
            if self._closed:
                raise ApiError(404, "not_found", "Der Kamera-Dienst wird beendet.")
            # Mit der Kennung, unter der der Stand verteilt wurde, aufsteigend.
            initial = sorted((self._last[kind][0], kind) for kind in INITIAL_KINDS if kind in self._last)
            for seq, kind in initial:
                subscriber.put(encode(seq, kind, self._last[kind][1]))
            self._subscribers.append(subscriber)
            count = len(self._subscribers)
        log.info("Ereignisse: Abonnent angemeldet, %d aktiv.", count)
        return subscriber

    def unsubscribe(self, subscriber: Subscriber) -> None:
        with self._lock:
            if subscriber not in self._subscribers:
                return
            self._subscribers.remove(subscriber)
            count = len(self._subscribers)
        subscriber.close()
        log.info("Ereignisse: Abonnent abgemeldet, %d aktiv.", count)

    def update(self, kind: str, data: dict, key=None, force: bool = False) -> bool:
        """Verteilt `data`, wenn es sich vom letzten Stand des Teils unterscheidet (oder `force`)."""
        key = kind if key is None else key
        dropped = []
        with self._lock:
            if not force and key in self._last and self._last[key][1] == data:
                return False
            self._seq += 1
            self._last[key] = (self._seq, data)
            event = encode(self._seq, kind, data)
            for subscriber in self._subscribers:
                if not subscriber.put(event):
                    dropped.append(subscriber)
            for subscriber in dropped:
                self._subscribers.remove(subscriber)
            count = len(self._subscribers)
        for subscriber in dropped:
            subscriber.close()
            log.warning("Ereignisse: Abonnent liest nicht (mehr als %d Ereignisse offen), "
                        "abgemeldet, %d aktiv.", self._queue_limit, count)
        return True

    def close(self) -> None:
        with self._lock:
            self._closed = True
            subscribers, self._subscribers = self._subscribers, []
        for subscriber in subscribers:
            subscriber.close()


class Events:
    """Verteiler und Faden, der Kameras, Presets und Lampen vergleicht."""

    def __init__(self, backend, read_controls: Callable[[str], list[dict]] | None = None,
                 interval_s: float = INTERVAL_S, queue_limit: int = QUEUE_LIMIT) -> None:
        self.hub = EventHub(queue_limit)
        self._backend = backend
        self._read_controls = read_controls
        self._interval_s = interval_s
        self._pending: set[str] = set()
        self._pending_lock = threading.Lock()
        self._wake = threading.Event()
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None

    def controls_changed(self, camera_id: str) -> None:
        """Merkt eine Kamera zum Lesen vor; blockiert nie (aus Anfragen und dem Faden «bewegen»)."""
        with self._pending_lock:
            self._pending.add(camera_id)
        self._wake.set()

    def config_changed(self, camera_id: str, config: dict) -> None:
        """Neue Einstellungen einer Kamera; blockiert nie (aus der Anfrage PUT config)."""
        self.hub.update("config", {"camera": camera_id, **config}, key=("config", camera_id),
                        force=True)

    def _presets(self) -> dict:
        backend = self._backend
        return {"camera_presets": (backend.preset_store.entries() if backend.preset_store is not None
                                   else backend.camera_presets()),
                "light_presets": (backend.light_preset_store.entries()
                                  if backend.light_preset_store is not None
                                  else backend.light_presets())}

    def poll(self) -> None:
        """Ein Vergleich aller Teile; läuft alle INTERVAL_S im eigenen Faden."""
        self.hub.update("status", {"cameras": self._backend.cameras()})
        self.hub.update("presets", self._presets())
        self.hub.update("lights", {"lights": self._backend.lights()})
        with self._pending_lock:
            pending, self._pending = sorted(self._pending), set()
        for camera_id in pending:
            if self._read_controls is None:
                continue
            try:
                controls = self._read_controls(camera_id)
            except ApiError as error:
                log.debug("Bildwerte von %s für Ereignis nicht lesbar: %s", camera_id, error.message)
                continue
            # Immer senden, auch bei gleichem Stand: Jede gemeldete Änderung gibt ein Ereignis.
            self.hub.update("controls", {"camera": camera_id, "controls": controls},
                            key=("controls", camera_id), force=True)

    def start(self) -> None:
        self.poll()

        def run() -> None:
            while not self._stop.is_set():
                self._wake.wait(self._interval_s)
                self._wake.clear()
                if self._stop.is_set():
                    break
                try:
                    self.poll()
                except Exception:
                    log.exception("Fehler bei den Ereignissen")

        self._thread = threading.Thread(target=run, name="ereignisse", daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        self._wake.set()
        self.hub.close()
        if self._thread is not None:
            self._thread.join()
