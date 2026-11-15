# Kamera-Dienst

Kleiner Dienst für den Streaming-PC: steuert die Kameras (OBSBOT Tiny 2, Logitech C922) und die Lampen und bietet dafür eine HTTP/JSON-Schnittstelle unter `/api/v1/`. Das Studio, das Stream Deck und später Chat-Befehle nutzen sie. Plan: `docs/archiv/prompts/P15-kamera.md`.

Python ab 3.11, nur Standardbibliothek; auf dem Streaming-PC ist nichts zu installieren.

## Start

```
cd kamera-dienst
python3 -m dss_camera
```

- Einstellungen: `~/.config/dss-kamera/dienst.ini` (anderer Pfad: `--konfig PFAD`). Fehlt die Datei, lauscht der Dienst nur auf `127.0.0.1`, Port 8093, und nimmt nur Anfragen von `127.0.0.1` an.

  ```ini
  [dienst]
  port=8093
  lauschadressen=<streaming-pc>,127.0.0.1
  erlaubte-adressen=<studio-pc>,127.0.0.1
  ```

  Anfragen von anderen Adressen bekommen `403` und einen Eintrag im Protokoll. Die Liste lässt sich nur in der Datei ändern.
- Es läuft immer nur ein Dienst (Sperre `$XDG_RUNTIME_DIR/dss-kamera.lock`). Ein zweiter Start bricht mit «Der Kamera-Dienst läuft schon» ab.
- Protokoll auf stderr (unter systemd im Journal).
- Exit-Codes: 0 beendet, 1 `dienst.ini` ungültig, 2 falsche Optionen, 3 läuft schon, 4 Lauschadresse oder Port nicht nutzbar.

Prüfen: `curl http://127.0.0.1:8093/api/v1/status`

## Attrappe

`python3 -m dss_camera --attrappe` simuliert eine Tiny 2, eine C922 und zwei Lampen (erfundene Kennungen), ohne ein Gerät anzufassen. Für Tests (`ctest -R '^kd_'`) und Prüfungen ohne Kameras.

## Installation

Folgt mit Abschnitt c (`installieren.sh`, `system-einrichten.sh`).
