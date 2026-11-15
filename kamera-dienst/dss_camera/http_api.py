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

    def json(self):
        """Liest den Inhalt als JSON-Objekt; Fehler werden zu `400 invalid`."""
        length_text = self._handler.headers.get("Content-Length", "0")
        if not length_text.isdigit():
            raise ApiError(400, "invalid", "Content-Length ungültig.")
        length = int(length_text)
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
    def __init__(self, backend: Backend, fake: bool) -> None:
        self.backend = backend
        self.fake = fake
        self.router = Router()
        self.router.add("GET", "status", self.get_status)

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
