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

Mit `--attrappe --geraeteordner PFAD` sucht der Dienst die Kameras wie echt, aber im Ordner `PFAD` statt in `/dev` und `/sys` (Aufbau in `dss_camera/fake.py`). Ein- und Ausstecken: Verweis in `PFAD/dev/v4l/by-id` anlegen oder löschen.

## Kameras

Der Dienst sucht die Kameras jede Sekunde in `/dev/v4l/by-id` (Meta-Knoten zählen nicht). Kennung: `<vid>:<pid>:<seriennummer>` des USB-Geräts, ohne Seriennummer `<vid>:<pid>`. Eine ausgesteckte Kamera bleibt in `status` mit `present: false` und `state: "absent"`. Nach dem Einstecken wartet der Dienst 3 s, bevor er Werte schreibt.

## Bildwerte

`GET /api/v1/cameras/{cam}/controls` listet alle Werte der Kamera, `PUT` mit `{"<key>": Wert}` setzt sie (zuerst die Automatik-Schalter, dann die Werte) und meldet die zurückgelesenen Werte; `null` setzt den Standard des Geräts. Pan, Tilt und Zoom stehen in der Liste, gesetzt werden sie über Bewegen. Verstellte Werte merkt sich der Dienst in `~/.local/share/dss-kamera/kameras.json` und setzt sie beim Erscheinen der Kamera und beim Start neu. Eine ungültige `kameras.json` wird gemeldet und nicht überschrieben; Änderungen werden dann abgewiesen. In der Attrappe gibt es Bildwerte nur mit `--geraeteordner`.

## OBSBOT Tiny 2

Die Tiny 2 hat in `controls` zusätzlich die Gruppe «OBSBOT»: HDR (setzbar, nur wenn die Kamera wach ist, wird gemerkt), Zustand und «Automatisch schlafen nach» (nur lesbar, `readonly: true`). Bildwinkel und KI-Modus bietet der Dienst nicht an (nicht bestätigt). `POST /api/v1/cameras/{cam}/wake` weckt die Kamera und wartet, bis sie wach ist (höchstens 10 s), `…/sleep` legt sie schlafen. Bewegungsbefehle gehen nur an die wache Kamera und frühestens 4,5 s nach dem Wecken. Der Zustand in `status` (`awake`, `asleep`, `privacy`) wird höchstens alle 5 s gelesen.

## Installation

Folgt mit Abschnitt c (`installieren.sh`, `system-einrichten.sh`).
