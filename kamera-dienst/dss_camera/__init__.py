"""Kamera-Dienst des DoNs Streaming Studio (P15).

Läuft auf dem Streaming-PC, steuert die Kameras über V4L2/UVC und die Lampen
und bietet dafür eine HTTP/JSON-Schnittstelle (Plan P15, Abschnitt 4).
Nur Standardbibliothek, Python >= 3.11.
"""

SERVICE_VERSION = "0.1.0"
# Version der Schnittstelle unter /api/v1/; Änderungen nur mit /api/v2/ (Plan 4).
API_VERSION = 1
