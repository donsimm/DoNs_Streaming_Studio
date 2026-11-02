#!/usr/bin/env python3
"""Messprototyp Variante B, Kern: liefert Seiten und Testdateien per HTTP aus.

Platzhalter für den unabhängigen Kern (die Sprache des echten Kerns ist offen). Nur
Python-Standardbibliothek. Unterstützt HTTP-Range, damit Chromium Videos in Teilen laden
und in Schleife abspielen kann.

  python3 core.py --port 8765 --web ../web --media ../../media

Quellen:
  http.server:  https://docs.python.org/3/library/http.server.html
  Range (RFC 9110, Abschnitt 14): https://www.rfc-editor.org/rfc/rfc9110#section-14
"""
import argparse
import mimetypes
import os
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

mimetypes.add_type("video/webm", ".webm")
RANGE_RE = re.compile(r"bytes=(\d*)-(\d*)$")


def make_handler(web_dir, media_dir):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_args):
            pass

        def _resolve(self):
            path = self.path.split("?", 1)[0]
            if path.startswith("/media/"):
                base, rel = media_dir, path[len("/media/"):]
            else:
                base, rel = web_dir, path.lstrip("/") or "index.html"
            full = os.path.realpath(os.path.join(base, rel))
            if not full.startswith(os.path.realpath(base) + os.sep) or not os.path.isfile(full):
                return None
            return full

        def do_GET(self):
            full = self._resolve()
            if full is None:
                self.send_error(404)
                return
            size = os.path.getsize(full)
            start, end = 0, size - 1
            status = 200
            m = RANGE_RE.match(self.headers.get("Range", ""))
            if m and (m.group(1) or m.group(2)):
                if m.group(1):
                    start = int(m.group(1))
                    if m.group(2):
                        end = min(int(m.group(2)), size - 1)
                else:
                    start = max(0, size - int(m.group(2)))
                if start > end:
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{size}")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                    return
                status = 206
            length = end - start + 1
            self.send_response(status)
            self.send_header("Content-Type", mimetypes.guess_type(full)[0] or "application/octet-stream")
            self.send_header("Content-Length", str(length))
            self.send_header("Accept-Ranges", "bytes")
            self.send_header("Cache-Control", "no-store")
            if status == 206:
                self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
            self.end_headers()
            with open(full, "rb") as f:
                f.seek(start)
                remaining = length
                while remaining > 0:
                    chunk = f.read(min(65536, remaining))
                    if not chunk:
                        break
                    try:
                        self.wfile.write(chunk)
                    except (BrokenPipeError, ConnectionResetError):
                        return
                    remaining -= len(chunk)

    return Handler


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--web", required=True)
    ap.add_argument("--media", required=True)
    args = ap.parse_args()
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(args.web, args.media))
    print(f"CORE_READY {args.port}", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    srv.server_close()


if __name__ == "__main__":
    main()
