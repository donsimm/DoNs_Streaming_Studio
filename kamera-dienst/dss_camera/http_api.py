"""HTTP/JSON-Schnittstelle unter /api/v1/ (Plan 4) mit Absenderprüfung (Plan 5, Schicht 2).

Ein `ThreadingHTTPServer` je Lauschadresse (Plan 5, Schicht 1), alle im selben Prozess.
Quelle: https://docs.python.org/3/library/http.server.html (nicht abgerufen).

Neue Anfragen kommen als Eintrag in `Api.router` dazu (`add(method, pattern, func)`);
Platzhalter wie `{cam}` im Muster werden als Argumente übergeben.
"""

import json
import logging
import re
import threading
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote

from . import API_VERSION, SERVICE_VERSION
from .backend import Backend

API_PREFIX = f"/api/v{API_VERSION}/"
# Grösste Anfrage mit Inhalt; Presets und Werte sind wenige Kilobyte.
MAX_BODY_BYTES = 64 * 1024

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
    def __init__(self, backend: Backend, fake: bool, controls=None, mover=None, presets=None) -> None:
        self.backend = backend
        self.fake = fake
        # controls.ControlManager, motion.Mover und presets.CameraPresets; fehlen ohne Geräte
        # (Attrappe ohne --geraeteordner).
        self.controls = controls
        self.mover = mover
        self.presets = presets
        self.router = Router()
        self.router.add("GET", "status", self.get_status)
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
