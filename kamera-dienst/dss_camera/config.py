"""Einstellungen des Dienstes aus dienst.ini (Plan 6: Port, erlaubte Adressen, Lauschadresse).

Beispiel:

    [dienst]
    port=8093
    lauschadressen=192.0.2.10,127.0.0.1
    erlaubte-adressen=192.0.2.20,127.0.0.1

Fehlt die Datei, gelten die Standardwerte (nur 127.0.0.1). Eine ungültige Datei
verhindert den Start mit einer klaren Meldung; sie wird nie überschrieben.
"""

import configparser
import ipaddress
import os
from dataclasses import dataclass, field
from pathlib import Path

SECTION = "dienst"
DEFAULT_PORT = 8093
# Wie der Overlay-Server (docs/08-kern-schnittstelle.md, kern.ini): unter 1024 nur mit
# besonderen Rechten, ab 32768 vergibt Linux Ports für ausgehende Verbindungen.
PORT_MIN = 1024
PORT_MAX = 32767
KEYS = ("port", "lauschadressen", "erlaubte-adressen")


class ConfigError(Exception):
    """dienst.ini ist ungültig; die Meldung ist für den Nutzer (Deutsch)."""


@dataclass
class Config:
    port: int = DEFAULT_PORT
    listen_addresses: list[str] = field(default_factory=lambda: ["127.0.0.1"])
    allowed_addresses: frozenset[str] = frozenset({"127.0.0.1"})
    path: Path | None = None
    file_found: bool = False


def default_path() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME") or str(Path.home() / ".config")
    return Path(base) / "dss-kamera" / "dienst.ini"


def _parse_addresses(key: str, text: str, listen: bool) -> list[str]:
    items = [part.strip() for part in text.split(",")]
    if not text.strip() or any(not item for item in items):
        raise ConfigError(f"«{key}» braucht eine oder mehrere IPv4-Adressen, getrennt mit Komma.")
    result = []
    for item in items:
        try:
            address = ipaddress.IPv4Address(item)
        except ValueError:
            raise ConfigError(f"«{item}» in «{key}» ist keine IPv4-Adresse.") from None
        # Plan 5, Schicht 1: nie auf allen Karten lauschen.
        if listen and (address.is_unspecified or address.is_multicast):
            raise ConfigError(f"«{item}» in «{key}» ist nicht erlaubt, nur Adressen dieses PCs.")
        if str(address) not in result:
            result.append(str(address))
    return result


def load(path: Path) -> Config:
    config = Config(path=path)
    if not path.exists():
        return config
    parser = configparser.ConfigParser(interpolation=None, default_section="\x00")
    try:
        with open(path, encoding="utf-8") as handle:
            parser.read_file(handle)
    except (OSError, UnicodeDecodeError, configparser.Error) as error:
        raise ConfigError(f"Datei nicht lesbar: {error}") from None
    config.file_found = True
    for section in parser.sections():
        if section != SECTION:
            raise ConfigError(f"Unbekannter Abschnitt «[{section}]», erlaubt ist nur «[{SECTION}]».")
    if not parser.has_section(SECTION):
        return config
    values = parser[SECTION]
    for key in values:
        if key not in KEYS:
            raise ConfigError(f"Unbekannter Wert «{key}», erlaubt sind {', '.join(KEYS)}.")
    if "port" in values:
        text = values["port"].strip()
        if not text.isdigit() or not PORT_MIN <= int(text) <= PORT_MAX:
            raise ConfigError(f"Port «{text}» ungültig, erlaubt sind {PORT_MIN} bis {PORT_MAX}.")
        config.port = int(text)
    if "lauschadressen" in values:
        config.listen_addresses = _parse_addresses("lauschadressen", values["lauschadressen"], True)
    if "erlaubte-adressen" in values:
        config.allowed_addresses = frozenset(
            _parse_addresses("erlaubte-adressen", values["erlaubte-adressen"], False))
    return config
