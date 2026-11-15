"""Nur eine Instanz des Dienstes (Plan 2.1): flock auf $XDG_RUNTIME_DIR/dss-kamera.lock.

Die Sperre gilt, solange der Prozess lebt; der Kernel gibt sie beim Ende frei,
auch nach einem Absturz. Quelle: flock(2), https://man7.org/linux/man-pages/man2/flock.2.html
(nicht abgerufen).
"""

import fcntl
import os
from pathlib import Path

LOCK_NAME = "dss-kamera.lock"


class LockError(Exception):
    """Sperre nicht möglich; die Meldung ist für den Nutzer (Deutsch)."""


def lock_path() -> Path:
    runtime_dir = os.environ.get("XDG_RUNTIME_DIR")
    if not runtime_dir:
        raise LockError("XDG_RUNTIME_DIR ist nicht gesetzt, die Sperre ist nicht möglich.")
    return Path(runtime_dir) / LOCK_NAME


def acquire() -> int:
    """Nimmt die Sperre und gibt den offenen Dateideskriptor zurück (bleibt offen)."""
    path = lock_path()
    try:
        fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o600)
    except OSError as error:
        raise LockError(f"Sperrdatei {path} nicht nutzbar: {error.strerror}.") from None
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        os.close(fd)
        raise LockError(f"Der Kamera-Dienst läuft schon (Sperre {path}).") from None
    return fd
