# Kamera-Dienst

Kleiner Dienst für den Streaming-PC: steuert die Kameras (OBSBOT Tiny 2, Logitech C922) und die Lampen und bietet dafür eine HTTP/JSON-Schnittstelle unter `/api/v1/`. Das Studio, das Stream Deck und später Chat-Befehle nutzen sie. Plan: `docs/archiv/prompts/P15-kamera.md`. Ganze Schnittstelle mit Beispielen, Ereignissen (`GET events`), Export und Fehlercodes: `docs/09-kamera-dienst.md`.

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

Die Tiny 2 hat in `controls` zusätzlich die Gruppe «OBSBOT»: HDR und Bildwinkel (`obsbot_fov`: 0 weit, 1 mittel, 2 eng; setzbar, nur wenn die Kamera wach ist, werden gemerkt), KI-Verfolgung (`obsbot_ai`: 0 aus, 1 Oberkörper; setzbar nur bei wacher Kamera, wird nicht gemerkt und kommt nicht in Presets), Zustand und «Automatisch schlafen nach» (nur lesbar, `readonly: true`). Vor jeder Bewegung schaltet der Dienst die KI-Verfolgung aus (`ai_off_before_move`). Geräte ohne einen einstellbaren Wert (z. B. eine Aufnahmekarte) erscheinen nicht in `status`. `POST /api/v1/cameras/{cam}/wake` weckt die Kamera und wartet, bis sie wach ist (höchstens 10 s), `…/sleep` legt sie schlafen. Bewegungsbefehle gehen nur an die wache Kamera und frühestens 4,5 s nach dem Wecken. Der Zustand in `status` (`awake`, `asleep`, `privacy`) wird höchstens alle 5 s gelesen.

## Bewegen

- `POST /api/v1/cameras/{cam}/move` mit `{"pan": -1…1, "tilt": -1…1, "zoom": -1…1, "speed": 1…100}`: Dauerbewegung (`pan` +1 = Bild nach rechts, `tilt` +1 = nach oben, `zoom` +1 = hinein). Die Anfrage muss alle 200 ms wiederholt werden; fehlt sie 500 ms, hält der Dienst an.
- Tempo: Bei der Tiny 2 ist `speed` Grad pro Sekunde (1–100, Standard 10); sie fährt Schwenken und Neigen fliessend über ihre Geschwindigkeitssteuerung. Weil die Kamera ihre Lage nicht meldet, schätzt der Dienst sie aus Tempo × Zeit und setzt sie nach jedem Halt als Stellung (Abweichung etwa 1°). Bei der C922 bleibt `speed` 1–100 (100 = 60°/s, Standard 50), Zoom wie bisher.
- `…/stop` hält an, `…/step` mit `{"pan": -1/0/1, …}` macht einen Schritt (Standard 2°, Zoom 10 % des Bereichs), `…/home` fährt in die Mitte (Zoom bleibt). Schritte, Mitte und Presets fährt die Tiny 2 am Stück an (≈ 62°/s).
- Ist die Tiny 2 im Schlaf, kommt `409 camera_asleep`; mit `"wake": true` weckt der Dienst sie vorher.
- Jeder Zielwert bleibt in den Grenzen: Tiny 2 Neigen unten −66° (nie unter −75°, bei −76° schläft sie ein), C922 ±10°. Bei der Dauerbewegung hält die Tiny 2 kurz vor der Grenze an und fährt genau auf sie.
- Wird die Kamera während einer Dauerbewegung unerreichbar, bricht der Dienst ab und sendet nichts nach (ausser dem Halt der Tiny 2); Wiederholungen werden abgewiesen, bis `stop` kommt oder sie 500 ms ausbleiben.
- Beim Start des Dienstes und beim Einstecken hält der Dienst eine noch laufende Fahrt der wachen Tiny 2 an.
- Einstellungen je Kamera in `kameras.json` unter `motion`: Grenzen (`pan_min`, `pan_max`, `tilt_min`, `tilt_max` in Grad), `invert_pan`/`invert_tilt`/`invert_zoom`, `step_deg`, `zoom_step_percent`, `home_pan`, `home_tilt`, `speed`, `auto_wake`, `ai_off_before_move`.

## Installation

| | Ort auf dem Streaming-PC |
|---|---|
| Programm | `~/.local/opt/dss-kamera/` |
| Daten | `~/.local/share/dss-kamera/` |
| Einstellungen | `~/.config/dss-kamera/dienst.ini` |
| Einheit | `~/.config/systemd/user/dss-kamera.service` (startet mit der Anmeldung, Neustart bei Fehler) |
| Sicherungen | `~/.local/share/dss-kamera-sicherungen/<JJJJ-MM-TT_hh-mm-ss>/` (`programm/`, `daten/`) |
| Protokoll | `journalctl --user -u dss-kamera` |

1. **Installieren oder aktualisieren** (auf dem Studio-PC, eine Zeile):

   ```
   ~/DoNs_Streaming_Studio/kamera-dienst/installieren.sh <streaming-pc>
   ```

   Kopiert den Dienst über `ssh` auf den Streaming-PC, sichert vorher Programm und Daten (nur wenn schon eine Version installiert ist), schreibt die Einheit, startet den Dienst neu und prüft `GET /api/v1/status`. Daten und `dienst.ini` bleiben unverändert.
2. **Einstellungen** (einmal, auf dem Streaming-PC): `~/.config/dss-kamera/dienst.ini` wie oben unter «Start» anlegen, danach `systemctl --user restart dss-kamera`. Ohne die Datei ist der Dienst nur auf dem Streaming-PC selbst erreichbar.
3. **Firewall** (einmal, auf dem Streaming-PC, `sudo` gibt der Nutzer ein):

   ```
   sudo ~/.local/opt/dss-kamera/system-einrichten.sh <studio-pc>
   ```

   Zeigt zuerst den Befehl und öffnet dann den Port 8093/tcp dauerhaft nur für `<studio-pc>` (reiche Regel in firewalld wie für obs-websocket). Die Zone von firewalld wählt das Skript selbst: die Zone der Schnittstelle, über die `<studio-pc>` erreicht wird (`ip route get`); hat diese keine Zone, die einzige aktive Zone, sonst bricht es mit Meldung ab. Anderer Port: `--port N`, andere Zone: `--zone NAME` (hat Vorrang), nur anzeigen: `--trocken`. Eine udev-Regel braucht es nicht.
4. **cameractrls:** Läuft der Wiederherstellungsdienst `cameractrlsd`, abschalten; er und der Kamera-Dienst würden sonst gegeneinander Werte setzen. cameractrls selbst nie gleichzeitig mit dem Dienst nutzen.

**Entfernen** (auf dem Streaming-PC):

```
sudo ~/.local/opt/dss-kamera/system-einrichten.sh --entfernen <studio-pc>
systemctl --user disable --now dss-kamera
rm ~/.config/systemd/user/dss-kamera.service
systemctl --user daemon-reload
rm -r ~/.local/opt/dss-kamera
```

Daten, `dienst.ini` und Sicherungen bleiben liegen; wer sie nicht mehr braucht, löscht `~/.local/share/dss-kamera`, `~/.config/dss-kamera` und `~/.local/share/dss-kamera-sicherungen` von Hand.

**Rückweg** zur vorigen Version (auf dem Streaming-PC, `<zeit>` = Ordner der Sicherung):

```
systemctl --user stop dss-kamera
rm -r ~/.local/opt/dss-kamera
cp -a ~/.local/share/dss-kamera-sicherungen/<zeit>/programm ~/.local/opt/dss-kamera
systemctl --user start dss-kamera
```

Die Daten nur zurückholen, wenn die neue Version sie verdorben hat: vorher `~/.local/share/dss-kamera` umbenennen, dann `…/<zeit>/daten` dorthin kopieren. Welche Version installiert ist, steht in `~/.local/opt/dss-kamera/version.txt`.

**Nur zum Prüfen** (Claude Code und Lotse, nie gegen die echte Installation): `installieren.sh --lokal --ziel ORDNER --datenordner ORDNER --ohne-einheit` auf diesem PC ohne `ssh`, oder mit `<streaming-pc>` statt `--lokal` in einen Testordner auf dem Streaming-PC. Die drei Optionen gehen nur zusammen.
