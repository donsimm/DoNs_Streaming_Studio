# Kamera-Dienst: Schnittstelle (Version 1)

Der Kamera-Dienst läuft auf dem Streaming-PC und steuert die Kameras (OBSBOT Tiny 2, Logitech C922) und die Lampen (Elgato Key Light). Studio, Stream Deck und später Chat-Befehle nutzen dieselbe HTTP/JSON-Schnittstelle. Plan und Entscheide: `docs/archiv/prompts/P15-kamera.md`; Start, Attrappe und Einstellungen: `kamera-dienst/LIESMICH.md`.

Ab dieser Fassung ist die Schnittstelle stabil: Neue Felder kommen nur dazu, Änderungen an bestehenden Feldern nur mit `/api/v2/`.

## Grundlagen

- Grundadresse `http://<streaming-pc>:8093/api/v1/`, auf dem Streaming-PC selbst `http://127.0.0.1:8093/api/v1/`. Port und Adressen stehen in `dienst.ini`.
- Inhalt und Antworten sind JSON in UTF-8. Bezeichner englisch, Meldungen deutsch.
- Erfolg: `200` mit `{"ok": true, …}`. Fehler: `{"ok": false, "code": "…", "message": "…"}` mit einem Code aus «Fehlercodes».
- Kameras haben die Kennung `<vid>:<pid>:<seriennummer>` (ohne Seriennummer `<vid>:<pid>`). Der Doppelpunkt darf in der Adresse stehen oder als `%3A` kommen.
- Presets und Lampen haben eine Kennung aus dem Namen (klein, ohne Umlaute und Leerzeichen, bei gleichem Namen mit angehängter Zahl). Beim Umbenennen bleibt die Kennung.

## Sicherheit

Keine Anmeldung, dafür drei Schichten (Plan 5):

1. **Bindung:** Der Dienst lauscht nur auf IPv4, auf den Adressen aus `lauschadressen` (Adresse des Streaming-PCs im Heimnetz und `127.0.0.1`).
2. **Absender:** Nur Adressen aus `erlaubte-adressen` (Standard: `<studio-pc>` und `127.0.0.1`) bekommen eine Antwort; andere bekommen `403 forbidden` und einen Eintrag im Protokoll. Die Liste lässt sich nur in `dienst.ini` ändern, nicht über die Schnittstelle.
3. **Firewall:** `sudo ~/.local/opt/dss-kamera/system-einrichten.sh <studio-pc>` legt in firewalld dauerhaft die reiche Regel `rule family="ipv4" source address="<studio-pc>" port port="8093" protocol="tcp" accept` an (wie die Regel für obs-websocket auf 4455) und lädt neu. Andere Absender trifft das Ziel der Zone; sie öffnet den Bereich 1025–65535 nicht mehr (Schritt 0). `--entfernen` nimmt die Regel wieder heraus, `--trocken` zeigt nur die Befehle. Installation, Entfernen und Rückweg: `kamera-dienst/LIESMICH.md`.

Es gibt keine Zugangsdaten. Im Protokoll stehen Adressen anderer Absender nur bei `403`.

## Anfragen

| Anfrage | Zweck |
|---|---|
| `GET status` | Versionen, Kameras, Lampen, Listen der Presets (`id`, `name`) |
| `GET cameras/{cam}/controls` | alle Bildwerte der Kamera |
| `PUT cameras/{cam}/controls` | Bildwerte setzen und merken; `null` = Standard des Geräts, vergessen |
| `GET`, `PUT cameras/{cam}/config` | Einstellungen der Kamera lesen, ändern (Name, Grenzen, Richtung, Tempo, Schrittweite, Mitte, Wecken, KI aus) |
| `POST cameras/{cam}/move` | Dauerbewegung (Totmann: alle 200 ms wiederholen) |
| `POST cameras/{cam}/stop` | anhalten |
| `POST cameras/{cam}/step` | ein Schritt |
| `POST cameras/{cam}/home` | Mitte (Zoom bleibt) |
| `POST cameras/{cam}/wake`, `…/sleep` | wecken, schlafen (nur Tiny 2) |
| `POST camera-presets` | neues Kamera-Preset aus der aktuellen Position |
| `GET`, `PUT`, `DELETE camera-presets/{id}` | Kamera-Preset lesen, anlegen oder ändern, löschen |
| `POST camera-presets/{id}/save-current` | aktuelle Position (und wahlweise Bildwerte) ins Preset |
| `POST camera-presets/{id}/recall` | Preset abrufen (weckt die Tiny 2, Standard) |
| `PUT lights/{light}`, `PUT lights/all` | Lampe oder alle schalten, Helligkeit, Farbtemperatur |
| `POST lights`, `PUT lights/{light}/config`, `DELETE lights/{light}` | Lampe anlegen, umbenennen oder Adresse ändern, entfernen |
| `POST light-presets` | neues Licht-Preset aus dem Stand aller Lampen |
| `GET`, `PUT`, `DELETE light-presets/{id}` | Licht-Preset lesen, anlegen oder ändern, löschen |
| `POST light-presets/{id}/save-current`, `…/recall` | Stand übernehmen, abrufen |
| `GET events` | Ereignisse als Server-Sent Events |
| `GET export` | alle Dateien des Datenordners als ein JSON |

In den Beispielen steht `D=http://127.0.0.1:8093/api/v1` und `CAM` für die Kennung einer Kamera.

### Status

```sh
curl "$D/status"
```

```json
{"ok": true, "service_version": "…", "api_version": 1, "fake": false,
 "cameras": [{"id": "…", "name": "OBSBOT Tiny 2", "model": "OBSBOT Tiny 2", "present": true,
              "state": "asleep", "ptz": true}],
 "lights": [{"id": "links", "name": "Links", "reachable": true, "on": true,
             "brightness": 40, "temperature": 200}],
 "camera_presets": [{"id": "totale", "name": "Totale"}],
 "light_presets": [{"id": "stream", "name": "Stream"}]}
```

- `state`: `awake`, `asleep`, `privacy` (nur Tiny 2), `absent` (ausgesteckt, `present: false`), `unknown`. Den Zustand der Tiny 2 liest der Dienst höchstens alle 5 s.
- `cameras` enthält nur Geräte mit mindestens einem einstellbaren Wert (V4L2-Wert, der nicht nur lesbar ist, oder die Gruppe «OBSBOT»). Andere Bildgeräte, z. B. eine Aufnahmekarte, fehlen; der Dienst meldet sie einmal im Protokoll («… ohne einstellbare Werte, ausgeblendet»).
- Die Listen der Presets enthalten nur `id` und `name`. Den ganzen Inhalt gibt es einzeln (`GET camera-presets/{id}`), für alle zusammen im Ereignis `presets` und in `GET export`. Eine Anfrage `GET camera-presets` für die ganze Liste gibt es nicht.

### Bildwerte

```sh
curl "$D/cameras/$CAM/controls"
curl -X PUT "$D/cameras/$CAM/controls" -d '{"white_balance_automatic": false, "white_balance_temperature": 4500}'
curl -X PUT "$D/cameras/$CAM/controls" -d '{"brightness": null}'
```

- Jeder Wert hat `key`, `label`, `group`, `type` (`int`, `bool`, `menu`), `min`, `max`, `step`, `default`, `value`, `menu`, `inactive`, `remembered`, `readonly` (`true` bei nur lesbaren Werten).
- `PUT` setzt zuerst die Automatik-Schalter, dann die Werte, und antwortet mit den zurückgelesenen Werten. Verstellte Werte merkt sich der Dienst und setzt sie beim Erscheinen der Kamera und beim Start neu.
- Ein falscher Wert ändert nichts (`400 invalid`). Ein Wert, der wegen eingeschalteter Automatik inaktiv ist, wird nicht gesetzt (`400 invalid`, die übrigen sind gesetzt).
- Pan, Tilt und Zoom stehen in der Liste, gesetzt werden sie nur über Bewegen.
- Tiny 2: Gruppe «OBSBOT» mit HDR (`obsbot_hdr`) und Bildwinkel (`obsbot_fov`, Typ `menu`: 0 «Weit», 1 «Mittel», 2 «Eng», Standard «Weit»), beide nur bei wacher Kamera (sonst `409 camera_asleep`) und gemerkt, dazu Zustand und «Automatisch schlafen nach» (nur lesbar). Die Zuordnung der Stufen zu weit, mittel, eng ist am Gerät bestätigt (2026-11-16).

### Bewegen

```sh
curl -X POST "$D/cameras/$CAM/move" -d '{"pan": 1, "tilt": 0, "zoom": 0, "speed": 50}'
curl -X POST "$D/cameras/$CAM/stop"
curl -X POST "$D/cameras/$CAM/step" -d '{"tilt": -1}'
curl -X POST "$D/cameras/$CAM/home"
curl -X POST "$D/cameras/$CAM/wake"
```

- `move`: `pan`, `tilt`, `zoom` je −1 bis 1 (`pan` +1 = Bild nach rechts, `tilt` +1 = nach oben, `zoom` +1 = hinein), `speed` 1 bis 100. Die Anfrage muss alle 200 ms wiederholt werden; fehlt sie 500 ms, hält der Dienst an (Totmann).
- Tempo bei der Tiny 2: `speed` ist Grad pro Sekunde (Standard 10), die Richtung skaliert es (`"pan": 0.5, "speed": 10` = 5°/s). Schwenken und Neigen fahren fliessend über die Geschwindigkeitssteuerung der Kamera. Sie meldet ihre Lage nicht; der Dienst schätzt sie aus Tempo × Zeit (Abweichung am Gerät etwa 1°) und setzt sie nach jedem Halt als Stellung, sodass `controls`, `step` und `save-current` von dort ausgehen. Vor einer Grenze hält sie an und fährt genau auf die Grenze. `step`, `home` und Presets fährt sie am Stück an (≈ 62°/s).
- Tempo bei der C922: `speed` 1 bis 100 (100 = 60°/s, Zoom 2 s über den ganzen Bereich), Standard 50.
- `step`: je Achse −1, 0 oder 1; Standard 2°, Zoom 10 % des Bereichs. Antwort mit `position` (Pan, Tilt in Grad, Zoom im Wert des Geräts).
- Jede Bewegung bleibt in den Grenzen (Tiny 2 Neigen unten −66°, nie unter −75°).
- Schläft die Tiny 2, kommt `409 camera_asleep`; mit `"wake": true` (oder `auto_wake` in den Einstellungen) weckt der Dienst sie und wartet, bis sie bereit ist.
- Wird die Kamera während einer Dauerbewegung unerreichbar, bricht der Dienst ab und sendet nichts nach ausser dem Halt der Tiny 2 (`409 camera_absent`, bis `stop` kommt).
- Beim Start des Dienstes und beim Einstecken hält der Dienst eine noch laufende Fahrt der wachen Tiny 2 an.

### Einstellungen je Kamera

```sh
curl "$D/cameras/$CAM/config"
curl -X PUT "$D/cameras/$CAM/config" -d '{"name": "Kamera oben", "tilt_min": -50, "invert_pan": true}'
curl -X PUT "$D/cameras/$CAM/config" -d '{"tilt_min": null}'
```

```json
{"ok": true,
 "config": {"name": "Kamera oben", "pan_min": null, "pan_max": null, "tilt_min": -50, "tilt_max": null,
            "invert_pan": true, "invert_tilt": false, "invert_zoom": false, "step_deg": 2,
            "zoom_step_percent": 10, "home_pan": 0, "home_tilt": 0, "speed": 10,
            "auto_wake": false, "ai_off_before_move": true},
 "ranges": {"speed": [1, 100], "pan_min": [-130, 130], "pan_max": [-130, 130], "home_pan": [-130, 130],
            "tilt_min": [-75, 90], "tilt_max": [-75, 90], "home_tilt": [-75, 90],
            "step_deg": [1, 90], "zoom_step_percent": [1, 100]},
 "limits": {"pan": [-130, 130], "tilt": [-50, 90]}}
```

| Feld | Bedeutung | Standard |
|---|---|---|
| `name` | Anzeigename (1 bis 40 Zeichen), erscheint als `name` in `GET status` | Modell |
| `pan_min`, `pan_max`, `tilt_min`, `tilt_max` | Grenzen in Grad | Bereich des Geräts; Tiny 2 `tilt_min` −66° |
| `invert_pan`, `invert_tilt`, `invert_zoom` | Richtung umkehren | `false` |
| `step_deg`, `zoom_step_percent` | Schrittweite für `step` (Grad, Prozent des Zoombereichs) | 2, 10 |
| `home_pan`, `home_tilt` | eigene Mitte für `home` in Grad | 0, 0 |
| `speed` | Tempo von `move` ohne `speed` (Tiny 2 in Grad pro Sekunde) | Tiny 2: 10, sonst 50 |
| `auto_wake` | Tiny 2 bei Bewegen ohne `wake` wecken | `false` |
| `ai_off_before_move` | KI-Verfolgung vor Bewegung aus (Tiny 2, einmal je Bewegung, siehe Einschränkungen) | `true` |

- `PUT` mit beliebig vielen Feldern; `null` setzt den Standard (beim Namen das Modell). Ist ein Feld ungültig, ändert sich nichts (`400 invalid`, die Meldung nennt den Bereich).
- `ranges`: erlaubte Bereiche aus dem Gerät, in Grad. Bei der Tiny 2 geht `tilt_min` nie unter −75° (sie schläft bei −76° ein). Die untere Grenze muss unter der oberen liegen, mit den Standards für `null`. Felder zu Achsen, die die Kamera nicht hat, fehlen in `ranges` und werden abgewiesen.
- `limits`: wirksame Grenzen in Grad. Sie gelten sofort für jedes Ziel (`move`, auch eine laufende Dauerbewegung, `step`, `home`, Presets); die eigene Mitte wird ebenso begrenzt.
- Geht auch bei ausgesteckter Kamera (mit den zuletzt gelesenen Bereichen). Eine Kamera, die der Dienst nie gesehen hat: `404 not_found`. Ist `kameras.json` ungültig: `409 file_error`, die Datei bleibt unverändert.
- Nach jedem `PUT` kommt das Ereignis `config`; ein neuer Name kommt zudem mit `status`.

### Kamera-Presets

```sh
curl -X POST "$D/camera-presets" -d '{"name": "Totale", "camera": "'"$CAM"'", "with_controls": false}'
curl "$D/camera-presets/totale"
curl -X PUT "$D/camera-presets/totale" -d '{"name": "Totale breit"}'
curl -X POST "$D/camera-presets/totale/save-current" -d '{"with_controls": true}'
curl -X POST "$D/camera-presets/totale/recall"
curl -X DELETE "$D/camera-presets/totale"
```

- Ein Preset ist `{"id", "name", "camera", "pan", "tilt", "zoom"}` und wahlweise `controls` (Bildwerte, nur mit `with_controls: true`). `null` bei einer Achse heisst «fehlt an der Kamera».
- `recall` fährt die Position begrenzt an, weckt die Tiny 2 (`{"wake": false}` schaltet das ab) und setzt danach die Bildwerte.
- `save-current` weckt die Tiny 2 nicht; im Schlaf ist die Position die Schlafstellung.

### Lampen

```sh
curl -X PUT "$D/lights/links" -d '{"on": true, "brightness": 40, "temperature": 200}'
curl -X PUT "$D/lights/all" -d '{"on": "toggle"}'
curl -X POST "$D/lights" -d '{"name": "Links", "host": "<lampe>"}'
curl -X PUT "$D/lights/links/config" -d '{"host": "<lampe>:9123"}'
curl -X DELETE "$D/lights/links"
```

- `on`: `true`, `false` oder `"toggle"`; `brightness` 3–100, `temperature` 143–344 (Mired). Jedes Feld ist wahlweise. Werte ausserhalb begrenzt der Dienst vor dem Senden.
- `"toggle"` liest die Lampen frisch. Bei `all`: Ist eine Lampe an, gehen alle aus, sonst gehen alle an.
- Antwort `{"lights": […], "errors": […]}`. Nach jedem Setzen liest der Dienst zurück; weicht ein Wert ab, steht in `errors` ein Eintrag mit `not_applied`. Eine Lampe ohne Antwort steht mit `light_unreachable` in `errors`, die anderen werden trotzdem gesetzt. Antwortet keine der angesprochenen Lampen, kommt `502 light_unreachable`.
- `all` ist reserviert und nie die Kennung einer Lampe. `host` ist Adresse oder Name, wahlweise mit `:port` (Standard 9123).
- Der Dienst fragt alle Lampen alle 5 s ab; so stimmt der Stand auch, wenn die Elgato-App die Lampe schaltet. Eine Lampe ohne Antwort kostet pro Anfrage rund 2 s (1 s Zeitgrenze, ein neuer Versuch).

### Licht-Presets

```sh
curl -X POST "$D/light-presets" -d '{"name": "Stream"}'
curl -X POST "$D/light-presets/stream/recall"
```

- Ein Licht-Preset ist `{"id", "name", "lights": {"<lampe>": {"on", "brightness", "temperature"} | null}}`. `null` und fehlende Felder heissen «nicht ändern».
- `POST` und `save-current` übernehmen den frisch gelesenen Stand aller Lampen; eine Lampe ohne Antwort kommt als `null` ins Preset (Meldung in `errors`).
- `recall` antwortet wie `PUT lights/…`; eine Lampe, die es nicht mehr gibt, steht mit `not_found` in `errors`.

### Ereignisse

```sh
curl -N "$D/events"
```

```
retry: 2000

id: 3
event: lights
data: {"lights":[{"id":"links","name":"Links","reachable":true,"on":true,"brightness":40,"temperature":200}]}

id: 7
event: controls
data: {"camera":"…","controls":[…]}
```

Server-Sent Events, jedes Ereignis mit dem ganzen neuen Stand seines Teils:

| `event` | `data` | kommt bei |
|---|---|---|
| `status` | `{"cameras": […]}` wie in `GET status` | Kamera erschienen oder verschwunden, wach, Schlaf, Privatsphäre, neuer Name |
| `config` | `{"camera": "<kennung>", "config": {…}, "ranges": {…}, "limits": {…}}` wie `GET …/config` | Einstellungen der Kamera geändert (`PUT …/config`) |
| `controls` | `{"camera": "<kennung>", "controls": […]}` wie `GET …/controls` | Bildwerte gesetzt, Ende einer Bewegung (`stop`, Totmann), `step`, `home`, Abruf eines Kamera-Presets, gemerkte Werte neu gesetzt |
| `presets` | `{"camera_presets": […], "light_presets": […]}` mit allen Feldern | Preset angelegt, geändert, gelöscht |
| `lights` | `{"lights": […]}` wie in `GET status` | Lampe geschaltet, Abfrage alle 5 s mit neuem Stand, Lampe angelegt, geändert, entfernt |

- Beim Anmelden kommt zuerst der aktuelle Stand von `status`, `presets` und `lights` (nicht `controls` und `config`; die liest der Abonnent mit `GET …/controls` und `GET …/config`).
- `id` steigt; alle Abonnenten sehen dieselben Ereignisse mit derselben `id` in derselben Reihenfolge.
- `status`, `presets` und `lights` vergleicht der Dienst alle 0,2 s mit dem letzten Stand. Schnelle Folgen von Änderungen können zu einem Ereignis zusammenfallen; das letzte Ereignis zeigt immer den neuesten Stand. `controls` und `config` kommen nach jeder gemeldeten Änderung, auch wenn die Werte gleich geblieben sind.
- Ohne Ereignis kommt alle 15 s eine Kommentarzeile (`: `).
- Mehrere Abonnenten sind möglich. Ein Abonnent, der nicht liest, hält weder andere Abonnenten noch andere Anfragen auf: Hat er mehr als 200 Ereignisse offen oder nimmt er 10 s lang nichts an, meldet der Dienst ihn ab und schliesst die Verbindung. Nach dem Schliessen verbindet der Abonnent neu und liest den Stand neu.

### Export

```sh
curl "$D/export" > kamera-dienst-sicherung.json
```

```json
{"ok": true, "format_version": 1, "data_dir": "…/dss-kamera",
 "files": {"kameras.json": {…}, "kamera-presets.json": {…}, "licht.json": {…},
           "licht-presets.json": {…}},
 "errors": []}
```

- Alle `*.json` des Datenordners, so wie sie auf der Platte stehen.
- Eine ungültige Datei steht unter `errors` mit `file` und `message`; unter `files` steht sie mit `null` (nicht lesbar oder kein JSON) oder mit ihrem Inhalt (Inhalt ungültig, z. B. falsche `format_version`). Der Export gelingt trotzdem.
- Zurückspielen geht vorerst von Hand: Dienst anhalten, Dateien aus `files` in den Datenordner schreiben, Dienst starten.

## Fehlercodes

| Status | `code` | Bedeutung |
|---|---|---|
| `400` | `invalid` | Anfrage oder Wert ungültig (Feld, Bereich, JSON, inaktiver Wert, Kamera ohne Bewegen) |
| `403` | `forbidden` | Absender nicht in `erlaubte-adressen` |
| `404` | `not_found` | unbekannte Anfrage, Kamera, Lampe oder Preset; in der Attrappe ohne Geräteordner auch Bildwerte, Bewegen, Presets und Lampen |
| `409` | `camera_absent` | Kamera ausgesteckt oder nicht erreichbar; Dauerbewegung abgebrochen |
| `409` | `camera_asleep` | Tiny 2 schläft (Bewegen ohne `wake`, HDR, Bildwinkel) |
| `409` | `file_error` | Datei im Datenordner ungültig (Änderungen gesperrt, Datei bleibt unverändert) oder nicht speicherbar |
| `502` | `light_unreachable` | keine der angesprochenen Lampen antwortet |
| `504` | `timeout` | Tiny 2 hat nicht rechtzeitig den Zustand gewechselt (Wecken, Schlafen) |
| `500` | `internal` | Fehler im Dienst, Einzelheiten im Protokoll |

In `errors` von Lampen-Antworten stehen zusätzlich `light_unreachable`, `not_applied` (Lampe hat einen Wert nicht übernommen) und `not_found` (Lampe aus dem Preset gibt es nicht mehr).

## Beispiele für das Stream Deck

Nur die Schnittstelle; der Umbau des Stream-Deck-Projekts ist ein eigener Auftrag. Jede Taste ist eine einzelne Anfrage an `http://<streaming-pc>:8093`:

| Taste | Anfrage |
|---|---|
| Licht «Stream» | `POST /api/v1/light-presets/stream/recall` |
| Licht ein/aus | `PUT /api/v1/lights/all` mit `{"on": "toggle"}` (ersetzt später `elgato_licht`) |
| Kamera «Totale» | `POST /api/v1/camera-presets/totale/recall` |
| Kamera schlafen | `POST /api/v1/cameras/<kennung>/sleep` |

Dauerbewegungen (`move`) eignen sich nicht für eine einzelne Taste, weil sie alle 200 ms wiederholt werden müssen; dafür `step` verwenden.

## Einschränkungen

- **Tiny 2, KI-Modus:** Vor jeder Bewegung (Start, Schritt, Ziel, nicht je Takt) sendet der Dienst «KI-Verfolgung aus» (`16 02 00 00`, am Gerät bestätigt 2026-11-16, `ai_off_before_move`). Die Kamera braucht ≈ 5 s, bis die Verfolgung aus ist (Status 6, dann 0); der Dienst wartet nicht darauf. Ob sie Bewegungsbefehle während dieses Übergangs annimmt, ist nicht geprüft. Einschalten bietet der Dienst nicht an, `ai_mode` gibt es in `controls` nicht. Auto-Schlaf wird nur gelesen.
- **Richtung und Tempo:** Die Standards von «Richtung umkehren» und bei der C922 das Tempo 100 (60°/s, Zoom 2 s über den ganzen Bereich) sind Annahmen; der Handtest N1 legt sie fest.
- **Lage der Tiny 2:** geschätzt, nicht gemessen (die Kamera meldet sie nicht). Die Schätzung folgt der am Gerät gemessenen Kurve Tempo → Grad pro Sekunde (2026-11-16, über das OBS-Bild; Pan bis 80, Tilt bis 20 gemessen, darüber verlängert). Nach einem Abbruch (Kamera unerreichbar) ist die Lage unsicher, bis `home` oder ein Preset sie absolut setzt.

## Dateien im Datenordner

`~/.local/share/dss-kamera/` (`$XDG_DATA_HOME/dss-kamera/`), alle mit `format_version` 1, atomar geschrieben, bei ungültigem Inhalt nie überschrieben:

| Datei | Inhalt |
|---|---|
| `kameras.json` | je Kamera Anzeigename (`name`), gemerkte Bildwerte (`controls`) und Einstellungen zum Bewegen (`motion`) |
| `kamera-presets.json` | Kamera-Presets (`presets`) |
| `licht.json` | Lampen (`lights`: `id`, `name`, `host`) |
| `licht-presets.json` | Licht-Presets (`presets`) |

## Quellen

- Server-Sent Events: https://html.spec.whatwg.org/multipage/server-sent-events.html (nicht abgerufen)
- Python `http.server`: https://docs.python.org/3/library/http.server.html (nicht abgerufen)
- Elgato Key Light: https://apihandyman.io/hacking-elgato-key-light-with-postman/
- OBSBOT Tiny 2, Frames: https://github.com/cgevans/tiny2
- V4L2 Pan/Tilt absolut und Tempo: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-camera.html (nicht abgerufen)
- firewalld, reiche Regeln: https://firewalld.org/documentation/man-pages/firewalld.richlanguage.html und https://firewalld.org/documentation/man-pages/firewall-cmd.html (nicht abgerufen)
- systemd-Benutzereinheit: https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html (nicht abgerufen)
