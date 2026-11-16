"""HTTP/JSON-Schnittstelle unter /api/v1/ (Plan 4) mit Absenderprüfung (Plan 5, Schicht 2).

Ein `ThreadingHTTPServer` je Lauschadresse (Plan 5, Schicht 1), alle im selben Prozess.
Quelle: https://docs.python.org/3/library/http.server.html (nicht abgerufen).

Neue Anfragen kommen als Eintrag in `Api.router` dazu (`add(method, pattern, func)`);
Platzhalter wie `{cam}` im Muster werden als Argumente übergeben.
"""

import json
import logging
import re
import select
import socket
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote

from . import API_VERSION, SERVICE_VERSION
from .backend import Backend

API_PREFIX = f"/api/v{API_VERSION}/"
# Grösste Anfrage mit Inhalt; Presets und Werte sind wenige Kilobyte.
MAX_BODY_BYTES = 64 * 1024
# GET events: Warten auf das nächste Ereignis (danach prüfen, ob der Abonnent noch da ist),
# Kommentarzeile gegen stille Verbindungen, Zeitgrenze, wenn der Abonnent nicht liest.
STREAM_POLL_S = 0.5
KEEPALIVE_S = 15.0
STREAM_WRITE_TIMEOUT_S = 10.0

log = logging.getLogger("dss_camera.http")


class ApiError(Exception):
    """Fehlerantwort `{"ok": false, "code", "message"}` (Plan 4)."""

    def __init__(self, status: int, code: str, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message


class Request:
    def __init__(self, handler: BaseHTTPRequestHandler, method: str, path: str) -> None:
        self.method = method
        self.path = path
        self._handler = handler

    def json(self, optional: bool = False):
        """Liest den Inhalt als JSON-Objekt; Fehler werden zu `400 invalid`.

        Mit `optional` gilt eine Anfrage ohne Inhalt als `{}` (z. B. POST stop).
        """
        length_text = self._handler.headers.get("Content-Length", "0")
        if not length_text.isdigit():
            raise ApiError(400, "invalid", "Content-Length ungültig.")
        length = int(length_text)
        if optional and length == 0:
            return {}
        if length > MAX_BODY_BYTES:
            raise ApiError(400, "invalid", f"Anfrage zu gross, höchstens {MAX_BODY_BYTES} Byte.")
        try:
            data = json.loads(self._handler.rfile.read(length).decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise ApiError(400, "invalid", "Inhalt ist kein gültiges JSON.") from None
        if not isinstance(data, dict):
            raise ApiError(400, "invalid", "Inhalt muss ein JSON-Objekt sein.")
        return data


class Stream:
    """Antwort von GET events: Der Handler schreibt die Ereignisse des Abonnenten, bis er geht."""

    def __init__(self, hub, subscriber) -> None:
        self.hub = hub
        self.subscriber = subscriber


class Router:
    def __init__(self) -> None:
        self._routes: list[tuple[str, re.Pattern, object]] = []

    def add(self, method: str, pattern: str, func) -> None:
        # «cameras/{cam}/controls» → ^cameras/(?P<cam>[^/]+)/controls$
        regex = re.sub(r"\\\{(\w+)\\\}", r"(?P<\1>[^/]+)", re.escape(pattern))
        self._routes.append((method, re.compile(f"^{regex}$"), func))

    def resolve(self, method: str, path: str):
        for route_method, regex, func in self._routes:
            match = regex.match(path)
            if match and route_method == method:
                return func, match.groupdict()
        return None, None


class Api:
    def __init__(self, backend: Backend, fake: bool, controls=None, mover=None, presets=None,
                 lights=None, light_presets=None, events=None, export=None) -> None:
        self.backend = backend
        self.fake = fake
        # controls.ControlManager, motion.Mover, presets.CameraPresets, lights.Lights und
        # lights.LightPresets; fehlen ohne Geräte (Attrappe ohne --geraeteordner).
        self.controls = controls
        self.mover = mover
        self.presets = presets
        self.lights = lights
        self.light_presets = light_presets
        # events.Events und export.export (mit dem Datenordner); hier nicht importiert,
        # weil beide Module ApiError aus diesem Modul brauchen.
        self.events = events
        self.export = export
        self.router = Router()
        self.router.add("GET", "status", self.get_status)
        self.router.add("GET", "events", self.get_events)
        self.router.add("GET", "export", self.get_export)
        self.router.add("GET", "cameras/{cam}/controls", self.get_controls)
        self.router.add("PUT", "cameras/{cam}/controls", self.put_controls)
        self.router.add("POST", "cameras/{cam}/wake", self.post_wake)
        self.router.add("POST", "cameras/{cam}/sleep", self.post_sleep)
        self.router.add("POST", "cameras/{cam}/move", self.post_move)
        self.router.add("POST", "cameras/{cam}/stop", self.post_stop)
        self.router.add("POST", "cameras/{cam}/step", self.post_step)
        self.router.add("POST", "cameras/{cam}/home", self.post_home)
        self.router.add("POST", "camera-presets", self.post_camera_preset)
        self.router.add("GET", "camera-presets/{preset}", self.get_camera_preset)
        self.router.add("PUT", "camera-presets/{preset}", self.put_camera_preset)
        self.router.add("DELETE", "camera-presets/{preset}", self.delete_camera_preset)
        self.router.add("POST", "camera-presets/{preset}/save-current", self.post_save_current)
        self.router.add("POST", "camera-presets/{preset}/recall", self.post_recall)
        self.router.add("PUT", "lights/{light}", self.put_light)
        self.router.add("POST", "lights", self.post_light)
        self.router.add("PUT", "lights/{light}/config", self.put_light_config)
        self.router.add("DELETE", "lights/{light}", self.delete_light)
        self.router.add("POST", "light-presets", self.post_light_preset)
        self.router.add("GET", "light-presets/{preset}", self.get_light_preset)
        self.router.add("PUT", "light-presets/{preset}", self.put_light_preset)
        self.router.add("DELETE", "light-presets/{preset}", self.delete_light_preset)
        self.router.add("POST", "light-presets/{preset}/save-current", self.post_light_save_current)
        self.router.add("POST", "light-presets/{preset}/recall", self.post_light_recall)

    def get_status(self, request: Request) -> dict:
        return {
            "service_version": SERVICE_VERSION,
            "api_version": API_VERSION,
            "fake": self.fake,
            "cameras": self.backend.cameras(),
            "lights": self.backend.lights(),
            "camera_presets": self.backend.camera_presets(),
            "light_presets": self.backend.light_presets(),
        }

    def get_events(self, request: Request) -> Stream:
        if self.events is None:
            raise ApiError(404, "not_found", "Ereignisse gibt es in diesem Lauf nicht.")
        return Stream(self.events.hub, self.events.hub.subscribe())

    def get_export(self, request: Request) -> dict:
        if self.export is None:
            raise ApiError(404, "not_found", "Export gibt es in diesem Lauf nicht.")
        return self.export()

    def _control_manager(self):
        if self.controls is None:
            raise ApiError(404, "not_found",
                           "Bildwerte gibt es nur mit Geräten (Attrappe: mit --geraeteordner).")
        return self.controls

    def get_controls(self, request: Request, cam: str) -> dict:
        return {"controls": self._control_manager().get(cam)}

    def put_controls(self, request: Request, cam: str) -> dict:
        manager = self._control_manager()
        return {"controls": manager.put(cam, request.json())}

    def post_wake(self, request: Request, cam: str) -> dict:
        return {"state": self._control_manager().wake(cam)}

    def post_sleep(self, request: Request, cam: str) -> dict:
        return {"state": self._control_manager().send_to_sleep(cam)}

    def _mover(self):
        if self.mover is None:
            raise ApiError(404, "not_found",
                           "Bewegen gibt es nur mit Geräten (Attrappe: mit --geraeteordner).")
        return self.mover

    def post_move(self, request: Request, cam: str) -> dict:
        return self._mover().move(cam, request.json())

    def post_stop(self, request: Request, cam: str) -> dict:
        mover = self._mover()
        request.json(optional=True)
        return mover.stop(cam)

    def post_step(self, request: Request, cam: str) -> dict:
        return self._mover().step(cam, request.json())

    def post_home(self, request: Request, cam: str) -> dict:
        return self._mover().home(cam, request.json(optional=True))

    def _presets(self):
        if self.presets is None:
            raise ApiError(404, "not_found",
                           "Kamera-Presets gibt es nur mit Geräten (Attrappe: mit --geraeteordner).")
        return self.presets

    def post_camera_preset(self, request: Request) -> dict:
        return self._presets().create(request.json())

    def get_camera_preset(self, request: Request, preset: str) -> dict:
        return self._presets().get(preset)

    def put_camera_preset(self, request: Request, preset: str) -> dict:
        return self._presets().put(preset, request.json())

    def delete_camera_preset(self, request: Request, preset: str) -> dict:
        return self._presets().delete(preset)

    def post_save_current(self, request: Request, preset: str) -> dict:
        return self._presets().save_current(preset, request.json(optional=True))

    def post_recall(self, request: Request, preset: str) -> dict:
        return self._presets().recall(preset, request.json(optional=True))

    def _lights(self):
        if self.lights is None:
            raise ApiError(404, "not_found",
                           "Lampen gibt es nur mit Geräten (Attrappe: mit --geraeteordner).")
        return self.lights

    def put_light(self, request: Request, light: str) -> dict:
        return self._lights().set(light, request.json())

    def post_light(self, request: Request) -> dict:
        return self._lights().create(request.json())

    def put_light_config(self, request: Request, light: str) -> dict:
        return self._lights().config(light, request.json())

    def delete_light(self, request: Request, light: str) -> dict:
        return self._lights().delete(light)

    def _light_presets(self):
        if self.light_presets is None:
            raise ApiError(404, "not_found",
                           "Licht-Presets gibt es nur mit Geräten (Attrappe: mit --geraeteordner).")
        return self.light_presets

    def post_light_preset(self, request: Request) -> dict:
        return self._light_presets().create(request.json())

    def get_light_preset(self, request: Request, preset: str) -> dict:
        return self._light_presets().get(preset)

    def put_light_preset(self, request: Request, preset: str) -> dict:
        return self._light_presets().put(preset, request.json())

    def delete_light_preset(self, request: Request, preset: str) -> dict:
        return self._light_presets().delete(preset)

    def post_light_save_current(self, request: Request, preset: str) -> dict:
        return self._light_presets().save_current(preset, request.json(optional=True))

    def post_light_recall(self, request: Request, preset: str) -> dict:
        return self._light_presets().recall(preset, request.json(optional=True))


def make_handler(api: Api, allowed_addresses: frozenset[str]):
    class Handler(BaseHTTPRequestHandler):
        server_version = f"dss-kamera/{SERVICE_VERSION}"

        def do_GET(self) -> None:
            self._dispatch("GET")

        def do_PUT(self) -> None:
            self._dispatch("PUT")

        def do_POST(self) -> None:
            self._dispatch("POST")

        def do_DELETE(self) -> None:
            self._dispatch("DELETE")

        def log_message(self, format: str, *args) -> None:
            # Kein Zugriffsprotokoll: Adressen anderer Absender nur bei 403 (Plan 5).
            pass

        def _dispatch(self, method: str) -> None:
            sender = self.client_address[0]
            path = self.path.split("?", 1)[0]
            try:
                if sender not in allowed_addresses:
                    log.warning("Anfrage von %s abgewiesen (nicht in erlaubte-adressen): %s %s",
                                sender, method, path)
                    raise ApiError(403, "forbidden", "Dieser Absender ist nicht erlaubt.")
                func, args = (None, None)
                if path.startswith(API_PREFIX):
                    func, args = api.router.resolve(method, path[len(API_PREFIX):])
                if func is None:
                    raise ApiError(404, "not_found", f"Unbekannte Anfrage: {method} {path}")
                # Kennungen enthalten «:», das Absender auch als %3A schicken.
                args = {name: unquote(value) for name, value in args.items()}
                body = func(Request(self, method, path), **args)
                if isinstance(body, Stream):
                    self._stream(body)
                    return
                self._send(200, {"ok": True, **body})
            except ApiError as error:
                self._send(error.status, {"ok": False, "code": error.code, "message": error.message})
            except Exception:
                log.exception("Fehler bei %s %s", method, path)
                self._send(500, {"ok": False, "code": "internal",
                                 "message": "Interner Fehler im Kamera-Dienst, siehe Protokoll."})

        def _send(self, status: int, body: dict) -> None:
            data = json.dumps(body, ensure_ascii=False).encode("utf-8")
            self.send_response(status, HTTPStatus(status).phrase)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _peer_closed(self) -> bool:
            """True, wenn der Abonnent die Verbindung geschlossen hat (ohne zu schreiben)."""
            try:
                readable, _, _ = select.select([self.connection], [], [], 0)
                return bool(readable) and self.connection.recv(1, socket.MSG_PEEK) == b""
            except OSError:
                return True

        def _stream(self, stream: Stream) -> None:
            """GET events: Server-Sent Events bis der Abonnent geht, abgemeldet oder zu langsam ist.

            Quelle: https://html.spec.whatwg.org/multipage/server-sent-events.html (nicht abgerufen).
            """
            subscriber = stream.subscriber
            self.close_connection = True
            try:
                self.send_response(200, "OK")
                self.send_header("Content-Type", "text/event-stream; charset=utf-8")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Connection", "close")
                self.end_headers()
                # Ein Abonnent, der nicht liest, hält diesen Faden höchstens so lange auf.
                self.connection.settimeout(STREAM_WRITE_TIMEOUT_S)
                self.wfile.write(b"retry: 2000\n\n")
                idle = 0.0
                while not subscriber.closed:
                    event = subscriber.get(STREAM_POLL_S)
                    if event is not None:
                        self.wfile.write(event)
                        idle = 0.0
                        continue
                    if self._peer_closed():
                        break
                    idle += STREAM_POLL_S
                    if idle >= KEEPALIVE_S:
                        self.wfile.write(b": \n\n")
                        idle = 0.0
            except OSError as error:
                log.info("Ereignisse: Verbindung beendet (%s).", error.strerror or type(error).__name__)
            finally:
                stream.hub.unsubscribe(subscriber)

    return Handler


class Servers:
    """Ein Server je Lauschadresse, jeder in einem eigenen Faden."""

    def __init__(self, api: Api, listen_addresses: list[str], port: int,
                 allowed_addresses: frozenset[str]) -> None:
        handler = make_handler(api, allowed_addresses)
        self._servers: list[ThreadingHTTPServer] = []
        try:
            for address in listen_addresses:
                server = ThreadingHTTPServer((address, port), handler)
                server.daemon_threads = True
                self._servers.append(server)
        except OSError:
            self.close()
            raise
        self._threads: list[threading.Thread] = []

    def start(self) -> None:
        for server in self._servers:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            self._threads.append(thread)

    def close(self) -> None:
        for server in self._servers:
            if self._threads:
                server.shutdown()
            server.server_close()
        self._servers = []
