# P15 – Kamerasteuerung: Kamera-Dienst und Dock «Kameras» mit Licht (Plan)

Plan des Projektleiter-Agenten vom 2026-11-15. Gebaut wird erst nach dem Go des Nutzers (Frage 4). Grundlage:
- Entscheide des Nutzers vom 2026-11-15 (unten; Lotse-Prozess «Idee Kamerasteuerung»)
- Recherche von Lotse «Kamera- und Lichtsteuerung unter Linux» vom 2026-11-15 mit Nachtrag «Probeläufe am Gerät» (nicht im Repo; der Nachtrag ist am Gerät belegt und hat Vorrang vor der Recherche)
- Pläne P13 (`docs/archiv/prompts/P13-twitch.md`, Ereignisfluss, Reiter «Chat-Bot» mit Unterreitern) und P14 (`docs/archiv/prompts/P14-chatbot.md`, Aktionssystem)
- `docs/01-festlegungen.md` («Module», «Oberfläche», «Anbindung an OBS», «Einsatz»), `docs/02-umgebung.md` (Streaming-PC, Firewall), `docs/08-kern-schnittstelle.md` (Version 13, «Einstellungen», «Anbindung an OBS»)
- Regeln 2 bis 5, 8, 9, 12 bis 14, 17, 18 (`CLAUDE.md`)

**Voraussetzung:** für Schritt 0 und die Abschnitte a bis c keine (nur der neue Ordner `kamera-dienst/`); für d bis f sind P13 und P14 gebaut und abgenommen.

**Ausführung:** Schritt 0 am Gerät durch Lotse, danach sechs Abschnitte (a bis f) mit je 2 bis 3 Commits, nach jedem Abschnitt eine Prüfung durch Lotse, am Ende Prüfstufe «Baustein» und Handtest.

## Entscheide des Nutzers (2026-11-15)

- Kameras am Streaming-PC über USB: OBSBOT Tiny 2 (`3564:fef8`) und Logitech C922 Pro Stream (`046d:085c`), beide UVC.
- Aufbau: kleiner Kamera-Dienst auf dem Streaming-PC (Werte setzen, Presets speichern) und Dock «Kameras» im Studio. Bedienung per Steuerkreuz statt grosser Regler. Später auch Stream Deck und Chat-Befehle. Netzregel: Dienst nur vom Studio-PC erreichbar.
- Umfang mindestens wie cameractrls, dazu funktionierendes Schwenken, Neigen und Zoomen (PTZ) der OBSBOT.
- Sonderfunktionen der OBSBOT (KI-Verfolgung, Schlafen/Wecken, Positionen) über das offen erforschte Protokoll (Vorlagen `cgevans/tiny2`, `OpenFoxes/Tiny4Linux`, beide EUPL-1.2), kein geschlossenes OBSBOT-SDK.
- Licht: die zwei Elgato Key Light Air (Ein/Aus, Helligkeit, Farbtemperatur) im Studio, dazu Licht-Presets (zum Beispiel «Stream», «Abend»: je Lampe Ein/Aus, Helligkeit, Farbtemperatur) anlegen, speichern, abrufen, auch per Stream Deck.
- Reihenfolge: direkt nach P14 (`docs/07-bauplan.md`).

## Antworten des Nutzers (2026-11-15, 21:55, in der Sitzung mit Lotse)

1. **Ort:** **B) Eigener Modul-Reiter «Kameras»** neben «Overlays» und «Chat-Bot», nicht das Dock aus Abschnitt 8. Abschnitt 8 und e werden vor Abschnitt e auf einen Reiter umgeplant (Unterteilung «Steuerung», «Bild», «Licht» im Reiter, Kopfzeile, Dialoge bleiben); `ui-consistency:design` vor dem Bau von e. Bis dahin gilt 8 nur für Inhalt, nicht für den Ort.
2. **Preset:** **A)** Position und Zoom, Kästchen «Bildwerte mitspeichern» (Standard aus).
3. **Schutz:** **A)** ohne Anmeldung, drei Schichten (Abschnitt 5).
4. **Go:** **A)** Schritt 0 und Abschnitte a bis c parallel zu P13/P14, wenn Platz ist, ohne P13/P14 zu verschieben; d bis f nach der Abnahme von P14. Rechte für Lotse am Streaming-PC wie in Frage 4 A (nie während eines Streams; bleibende Werte nur mit OK in der Sitzung). Grenze: Bau.

## Wichtigste Befunde

| Befund | Beleg |
|---|---|
| Tiny 2 (Firmware `bcdDevice` 4.09): Pan −468000…468000, Tilt −324000…324000, Schritt 3600 (= 1°, also ±130° / ±90°); Zoom 0–100; Pan-Speed −160…160, Tilt-Speed −120…120 (Minimum = −Maximum, der Kernel-Fehler beim Minimum zeigt sich hier nicht) | Nachtrag «Probeläufe am Gerät» (Lotse, 15.11., Python-ioctl über SSH) |
| **Pan positiv = Kopf dreht nach links** (aus Sicht des Nutzers) | Nachtrag |
| **Schlaf:** Ausgangslage Tilt −84° (Kopf unten), LED aus. `S_CTRL` ohne offenen Videostrom: nur im Cache, keine Bewegung. Mit offenem Videostrom: `SET_CUR` → −110 (Zeitüberschreitung), nach ~20 s Abmeldung vom USB und Neustart der Kamera (11 s), danach Grundstellung (Pan 1°, Tilt −6°) | Nachtrag |
| Wach: Pan +10° und zurück ohne Videostrom geht sofort und sichtbar | Nachtrag |
| USB-Autosuspend: `runtime_status` der Tiny 2 war «suspended» | Nachtrag |
| C922: Pan/Tilt ±10° (digital), Zoom 100–500, Fokus 0–250 Schritt 5, **Netzfrequenz steht auf 60 Hz** (für die Schweiz 50 Hz) | Nachtrag |
| Key Light Air ×2: Firmware 1.0.3 (Build 222), `hardwareBoardType` 200, HTTP auf Port 9123 (`/elgato/accessory-info`, `/elgato/lights`); **kein** MK.2-Protokoll | Nachtrag |
| Am Streaming-PC fehlt `v4l2-ctl`; ioctl aus Python geht ohne Zusatzpaket | Nachtrag |
| Erweiterungseinheit (XU) der Tiny 2: Unit 2, Selektor `0x6` (Befehle und 60-Byte-Statusblock `[Tag, Länge, Wert…]`), Selektor `0x2` (Frame-Protokoll mit `crc16_usb`). Statusblock: Byte `0x09` Gerätestatus (1 läuft, 3 Schlaf, 4 Privatsphäre), `0x06` HDR, `0x11` Bildwinkel, `0x18`/`0x1c` KI-Modus. Beispiele: HDR an `01 01 01`, Bildwinkel `04 01 00/01/02`, KI «Oberkörper» `16 02 02 01`. Nach dem Wecken wartet `tiny2` 400 ms | Recherche 1.2 und 1.4 (Quellcode `tiny2`; an anderer Firmware, teils KI-geschrieben, **am Gerät nicht geprüft**) |
| Im Schlaf werden laut `tiny2` die meisten Befehle ignoriert, Lesen geht | Recherche 1.4 |
| `UVCIOC_CTRL_QUERY` braucht laut Kernel-Doku keine Root-Rechte; `UVCIOC_CTRL_MAP` braucht Root und ist veraltet | Recherche 1.2, Frage 4 (https://docs.kernel.org/userspace-api/media/drivers/uvcvideo.html) |
| Frame-Befehle haben in `tiny2` keine Sperre; ein zweites Programm (cameractrls, `t4l`) darf nicht gleichzeitig XU-Befehle senden | Recherche 1.5 |
| Einzelbericht (Tiny 2 Lite): `EPROTO`-Abbruch vom USB, wenn ein Videostrom kurz nach dem Einstecken startet oder Strom-Wechsel und XU-Schreiben zusammenfallen | Recherche 1.5 (Issue #7 von `tiny2`) |
| Gelesener Pan/Tilt-Wert kommt laut Kommentar in `tiny2` aus dem Treiber-Cache und ist nach KI-Verfolgung veraltet | Recherche 1.1 |
| OBS schreibt beim Start keine Kamerawerte; nur beim Verstellen im Eigenschaftenfenster der Quelle | Recherche Frage 4 (Quellcode OBS `plugins/linux-v4l2`) |
| Key Light: `brightness` 3–100, `temperature` 143–344 Mired; Werte ausserhalb ergeben `200 OK` ohne Wirkung (stiller Fehler); fester `Content-Length` führt zu `400` | Recherche Frage 5 (apihandyman.io) |
| C922 hat zwei Videoknoten, nur einer nimmt Werte an; Knoten mit `V4L2_CAP_META_CAPTURE` ausschliessen, Kamera über `/dev/v4l/by-id/` finden | Recherche Frage 3 |
| Firewall des Streaming-PCs: Zone `FedoraWorkstation` auf der Kabelkarte, **Ports 1025–65535 TCP/UDP offen**. Ein Dienst auf einem hohen Port ist also ohne eigene Regel im ganzen Heimnetz erreichbar | `docs/02-umgebung.md`, «Streaming-PC» |
| Kopfbereich: vier feste Verbindungsknöpfe (Twitch, YouTube, OBS, NDI); Docks entstehen in `src/app/docks.cpp` mit `dock()`, `section()` (Titel mit Linie, Knöpfe in der Titelzeile), `listWithButtons()` (Leiste `dockButtons`), `symbolButton()`, Zahlenfelder mit Pfeilen, Regler mit Wertanzeige fester Breite (Lautstärke) | `src/app/headerbar.cpp:185-209`, `src/app/docks.cpp:29-141`, `:326-342` |
| Symbole: `nach-oben.svg`, `nach-unten.svg`, `zuruecksetzen.svg`, `hinzufuegen.svg`, `entfernen.svg` vorhanden; Pfeile nach links und rechts fehlen | `src/app/symbole/` |
| `tests/CMakeLists.txt` sucht `python3` schon (`find_program(DSS_PYTHON3 python3 REQUIRED)`) | `tests/CMakeLists.txt:112` |

## 1. Übersicht

```
Studio-PC                                             Streaming-PC
┌──────────────────────────┐                          ┌────────────────────────────────┐
│ Fenster: Dock «Kameras»  │                          │ Kamera-Dienst (Python,         │
│        │ Socket (08)     │   HTTP/JSON, Port <port> │ Benutzerdienst dss-kamera)     │
│ Kern: CameraService ─────┼─────────────────────────►│  ├─ V4L2 + XU → Tiny 2, C922   │
│  (P14: Schritte          │◄──── Ereignisse (SSE) ───┤  ├─ HTTP → Key Light ×2        │
│   «Kamera-Preset»,       │                          │  └─ Dateien: Presets, Werte    │
│   «Licht-Preset»)        │                          └────────────────────────────────┘
│ Stream-Deck-Dienst ──────┼──── HTTP/JSON (später, eigenes Projekt) ──┘
└──────────────────────────┘
```

- **Einziger Weg zu Kameras und Lampen ist der Dienst.** Studio, Stream Deck und Aktionssystem sprechen dieselbe Schnittstelle (4.).
- Im Studio spricht **der Kern** mit dem Dienst, nicht das Fenster (Regel 9 und `docs/01-festlegungen.md`, «Oberfläche»: Verbindungen betreibt der Kern; P14 ruft Presets aus dem Kern ab).

## 2. Kamera-Dienst

### 2.1 Ort, Sprache, Aufbau

- **Ort:** Unterordner `kamera-dienst/` im Studio-Repo (Interna-Entscheid, Begründung unten). Darin `dss_camera/` (Python-Paket), `tests/`, `installieren.sh`, `system-einrichten.sh`, `LIESMICH.md`. Die Doku der Schnittstelle kommt nach `docs/09-kamera-dienst.md`.
- **Sprache:** Python, nur Standardbibliothek (`fcntl`, `ctypes`, `struct`, `http.server`, `urllib.request`, `json`, `threading`, `configparser`). Kein Paket auf dem Streaming-PC zu installieren. Ziel Python ≥ 3.11; die Version auf Nobara 44 prüft Schritt 0. Begründung: wenige ioctl-Aufrufe, kleiner Netzdienst; cameractrls zeigt denselben Weg in Python (Recherche, Frage 4).
- **Ein Prozess, ein Arbeitsfaden je Kamera:** Alle Zugriffe auf eine Kamera laufen nacheinander über eine Warteschlange (XU hat keine Sperre). HTTP-Anfragen legen Aufträge in die Warteschlange und warten höchstens 3 s auf das Ergebnis.
- **Nur eine Instanz:** Der Dienst nimmt beim Start eine Sperre (`flock` auf `$XDG_RUNTIME_DIR/dss-kamera.lock`). Eine zweite Instanz (zum Beispiel eine Testinstanz von Lotse) startet nicht, solange der installierte Dienst läuft.
- **Module** (Namen vorläufig): `v4l2.py` (Strukturen und ioctl `VIDIOC_QUERYCAP`, `VIDIOC_QUERYCTRL`, `VIDIOC_QUERYMENU`, `VIDIOC_G_CTRL`, `VIDIOC_S_CTRL`), `uvc_xu.py` (`UVCIOC_CTRL_QUERY`), `obsbot.py` (Statusblock lesen, nur am Gerät geprüfte Befehle), `devices.py` (Suche und Ein-/Ausstecken), `camera.py` (Warteschlange, Schutzregeln, gemerkte Werte), `motion.py` (Bewegen mit Totmann), `lights.py` (Key Light), `presets.py`, `http_api.py`, `events.py` (Ereignisse), `fake.py` (Attrappen für Tests), `__main__.py`.
- **Lizenz:** Bytefolgen und Selektoren werden aus `tiny2` **gelesen und selbst neu geschrieben**, kein Code kopiert (Recherche, Frage 2). Jede Bytefolge im Code trägt einen Kommentar mit Quelle und «am Gerät geprüft am …» (Schritt 0). Nur geprüfte Befehle sind im Dienst freigeschaltet.

### 2.2 Geräte finden

- Kameras über `/dev/v4l/by-id/usb-…-video-index0`, Knoten mit `V4L2_CAP_META_CAPTURE` werden übersprungen (`VIDIOC_QUERYCAP`, `device_caps`).
- **Kennung einer Kamera:** `<vid>:<pid>:<seriennummer>` aus dem Namen in `by-id`; Anzeigename im Dock frei wählbar. Keine Seriennummern im Repo (Testdaten erfunden).
- **Ein- und Ausstecken:** Der Dienst prüft `by-id` jede Sekunde (ohne Zusatzpaket wie `pyudev`). Verschwindet eine Kamera, wird sie «fehlt»; laufende Bewegungen enden, Aufträge in der Warteschlange enden mit `camera_absent`.
- **Werte und Bereiche** liest der Dienst beim Erscheinen per `VIDIOC_QUERYCTRL` (min, max, Schritt, Standard, Menüeinträge, Flags), nicht fest im Code.

### 2.3 Schutzregeln für die Tiny 2 (Nachtrag am Gerät)

1. **Vor jedem Bewegungsbefehl** (Pan, Tilt, Zoom, Preset, Mitte) liest der Dienst den Gerätestatus (XU, Statusblock Byte `0x09`). Nur bei 1 («läuft») geht der Befehl ans Gerät.
2. **Im Schlaf (3) oder in Privatsphäre (4)** geht kein Bewegungsbefehl ans Gerät. Ohne `wake: true` antwortet der Dienst `camera_asleep`; mit `wake: true` (Standard beim Abrufen eines Presets) weckt er, wartet, bis der Status 1 ist (höchstens 10 s), dann noch die in Schritt 0 gemessene Zeit (Startwert 400 ms aus `tiny2`), und führt dann aus.
3. **Neigen nach unten begrenzt:** Grenze je Kamera einstellbar (Standard aus Schritt 0: Schwelle zum Einschlafen plus 10°). Der Dienst begrenzt jeden Zielwert, auch aus Presets und von aussen. Ebenso Grenzen für Pan und Tilt oben.
4. **KI-Verfolgung vor Bewegung aus** (einstellbar, Standard an): Vor manueller Bewegung und Preset setzt der Dienst den KI-Modus auf «keine Verfolgung».
5. **Totmann:** Eine Dauerbewegung (`move`) hält nur, solange alle 200 ms eine Wiederholung kommt. Fehlt sie 500 ms, hält der Dienst an (Fenster abgestürzt, Netz weg).
6. **Wird die Kamera während eines Auftrags unerreichbar** (Zeitüberschreitung, `EPROTO`), bricht der Dienst ab, sendet nichts nach und wartet auf das Wiedererscheinen.
7. **Nach dem Einstecken** wartet der Dienst 3 s, bevor er Werte schreibt (Einzelbericht `EPROTO` beim Einstecken); Schritt 0 und der Handtest prüfen, ob das reicht.
8. **USB-Autosuspend:** Schritt 0 misst, ob ein Lesezugriff auf die schlafende, vom USB «suspended» Kamera zuverlässig und schnell geht. Wenn nicht, schaltet `system-einrichten.sh` Autosuspend für die zwei Kameras per udev-Regel ab (`power/control` = `on`).

Die C922 hat keine XU-Befehle und keinen Schlaf; für sie gelten nur 3 (digitale Grenzen), 5 und 6.

### 2.4 Bewegen

- **Dauerbewegung** (Steuerkreuz gehalten): Der Dienst rechnet alle 50 ms eine neue Zielposition (Richtung × Tempo × Zeit), begrenzt sie (2.3, Regel 3) und schreibt `pan_absolute`/`tilt_absolute`. So halten die Grenzen sicher. Die Speed-Werte (`pan_speed`, `tilt_speed`) nutzt der Dienst nur, wenn Schritt 0 zeigt, dass absolute Schritte ruckeln.
- **Schritt** (Klick): Zielposition ± Schrittweite (Standard 2°, einstellbar).
- **Zoom:** gleich, über `zoom_absolute`.
- **Mitte:** Pan 0, Tilt 0 (oder eigene «Mitte» je Kamera), Zoom unverändert.
- **Richtung:** Pfeil links bewegt den Bildausschnitt nach links, wie wenn man die Kamera selbst schwenkt. Pan positiv dreht laut Nachtrag den Kopf nach links (aus Sicht des Nutzers, also nach rechts aus Sicht der Kamera). Je Achse «Richtung umkehren» in den Einstellungen der Kamera; der Standard wird im Handtest N1 festgelegt.
- **Aktuelle Position:** Der Dienst führt die zuletzt gesendete Position. Ob `G_CTRL` nach KI-Verfolgung die echte Position liefert, klärt Schritt 0; sonst `[OFFEN]` (2.3 Regel 4 vermeidet den Fall weitgehend).

### 2.5 Bildwerte und Werte über Neustarts

- Alle V4L2-Werte der Kamera (Umfang wie cameractrls): Helligkeit, Kontrast, Sättigung, Schärfe, Verstärkung, Gamma, Farbton, Gegenlicht, Netzfrequenz, Belichtung (Modus, Zeit, dynamische Bildrate), Fokus (automatisch, Wert), Weissabgleich (automatisch, Temperatur), Zoom, Pan, Tilt. Liste und Namen bestätigt Schritt 0 an beiden Kameras.
- **Reihenfolge beim Setzen:** zuerst Schalter für Automatik (Belichtungsmodus, Autofokus, automatischer Weissabgleich), dann die Werte. Werte mit `V4L2_CTRL_FLAG_INACTIVE` (bei Automatik) meldet der Dienst als «inaktiv»; das Dock zeigt sie ausgegraut.
- **Gemerkte Werte:** Was der Nutzer im Dock verstellt, merkt sich der Dienst je Kamera (`kameras.json`) und setzt es beim Erscheinen der Kamera und beim Start des Dienstes neu (zum Beispiel Netzfrequenz der C922 auf 50 Hz). Werte, die der Nutzer nie verstellt hat, fasst der Dienst nicht an. «Zurücksetzen» setzt den Standard des Geräts und vergisst den Wert.
- Nach jedem Setzen liest der Dienst zurück und meldet den echten Wert (das Gerät rundet auf den Schritt).
- **Sonderfunktionen der Tiny 2** als zusätzliche Werte im selben Format (Gruppe «OBSBOT»): KI-Verfolgung (Modus, Tempo), HDR, Bildwinkel, Spiegeln, Auto-Schlaf (Zeit, 0 = nie). Jede erst, wenn ihre Bytefolge in Schritt 0 oder Abschnitt b am Gerät geprüft ist; die übrigen bleiben im Code als «nicht freigeschaltet» und erscheinen nicht.
- **Nicht in P15:** Gesten (kein Beleg unter Linux), Presets im Speicher der Kamera (Opcodes unbestätigt, Recherche 1.3), Mikrofon und LED der Kamera.

### 2.6 Licht

- Lampen mit Name und Adresse in `licht.json`, im Dock anlegbar, umbenennbar, entfernbar (Dialog «Lampen …»). Keine mDNS-Suche in P15 (kein Paket in der Standardbibliothek); Adressen am Router reservieren.
- Ansprechen über `GET`/`PUT http://<lampe>:9123/elgato/lights`, Zeitgrenze 1 s, ein neuer Versuch. Kein eigener `Content-Length` von Hand (Stolperstein `400`).
- **Werte vor dem Senden begrenzen:** Helligkeit 3–100, Temperatur 143–344 Mired (stiller Fehler sonst). Das Dock zeigt Kelvin (`1 000 000 / Mired`, gerundet auf 50 K), gespeichert wird Mired.
- **Zustand nachführen:** Der Dienst fragt jede Lampe alle 5 s ab. So stimmt die Anzeige auch, wenn die heutige Stream-Deck-Taste `elgato_licht` oder die Elgato-App die Lampe direkt schaltet.

## 3. Presets: gespeichert im Dienst

**Entscheid (Interna, Begründung):** Kamera- und Licht-Presets liegen **im Dienst** auf dem Streaming-PC, nicht im Studio.
- Stream Deck und später Chat-Befehle rufen sie direkt beim Dienst ab, auch wenn das Studio geschlossen ist (der Bot läuft nur mit offenem Studio, das Stream Deck immer).
- Eine Stelle für alle Bedienwege; keine Abgleiche zwischen zwei Speicherorten.
- Der Dienst braucht die gemerkten Werte (2.5) ohnehin selbst, um sie beim Einstecken neu zu setzen.
- Gegenargument: Die Daten liegen auf dem anderen PC und sind nicht in der Sicherung des Studios. Abhilfe: `installieren.sh` sichert den Datenordner des Dienstes vor jeder Aktualisierung (6.), und `GET /api/v1/export` liefert alles als eine Datei.

**Dateien** im Datenordner `~/.local/share/dss-kamera/` (atomar geschrieben: temporäre Datei, dann `os.replace`), je mit `format_version` 1:

| Datei | Inhalt |
|---|---|
| `kameras.json` | je Kamera: Anzeigename, Grenzen (Pan, Tilt oben/unten), Richtung umkehren, Tempo, Schrittweite, automatisch wecken, KI vor Bewegung aus, gemerkte Werte |
| `kamera-presets.json` | Liste: `id`, `name`, `camera`, `pan`, `tilt`, `zoom` (in Einheiten des Geräts), wahlweise `ai_mode`, wahlweise `controls` (Bildwerte, Frage 2) |
| `licht.json` | Lampen: `id`, `name`, `host` |
| `licht-presets.json` | Liste: `id`, `name`, je Lampe `on`, `brightness`, `temperature` oder `null` (= diese Lampe nicht ändern) |

- **Kennung eines Presets** (`id`): aus dem Namen gebildet wie die Adresse eines Overlays (Kleinbuchstaben, Umlaute als ae/oe/ue, übrige Zeichen als «-», bei gleichem Namen eine Zahl angehängt), zum Beispiel «Totale» → `totale`. Beim Umbenennen bleibt sie (wie die Adresse eines Overlays, `docs/01-festlegungen.md`), damit Tasten am Stream Deck und Schritte in Aktionen weiter gehen.
- Eine ungültige Datei wird gemeldet und nicht überschrieben (Muster `liste.json`); Änderungen an ihr werden abgewiesen.

## 4. Schnittstelle des Dienstes (HTTP/JSON, Version 1)

Grundadresse `http://<streaming-pc>:<port>/api/v1/` (Port-Vorschlag 8093, frei laut Schritt 0). JSON in UTF-8. Bezeichner englisch (Regel 13), Meldungen deutsch. Ganze Doku mit Beispielen kommt in `docs/09-kamera-dienst.md` (Abschnitt c); die Schnittstelle ist ab dann stabil, neue Felder kommen nur dazu, Änderungen nur mit `/api/v2/`.

| Anfrage | Zweck |
|---|---|
| `GET status` | Version des Dienstes und der Schnittstelle; Kameras (`id`, `name`, `model`, `present`, `state`: `awake`/`asleep`/`privacy`/`absent`/`unknown`, `ptz`); Lampen (`id`, `name`, `reachable`, `on`, `brightness`, `temperature`); Listen der Presets (`id`, `name`) |
| `GET cameras/{cam}/controls` | alle Werte mit `key`, `label`, `group`, `type` (`int`, `bool`, `menu`), `min`, `max`, `step`, `default`, `value`, `menu`, `inactive`, `remembered` |
| `PUT cameras/{cam}/controls` | `{"<key>": Wert, …}` setzen, merken, zurückgelesene Werte in der Antwort; `{"<key>": null}` = Standard des Geräts, vergessen |
| `PUT cameras/{cam}/config` | Name, Grenzen, Richtung, Tempo, Schrittweite, automatisch wecken, KI vor Bewegung aus |
| `POST cameras/{cam}/move` | `{"pan": -1…1, "tilt": -1…1, "zoom": -1…1, "speed": 1…100}` Dauerbewegung mit Totmann (2.3, Regel 5) |
| `POST cameras/{cam}/stop` | anhalten |
| `POST cameras/{cam}/step` | `{"pan": -1/0/1, "tilt": …, "zoom": …}` ein Schritt |
| `POST cameras/{cam}/home` | Mitte |
| `POST cameras/{cam}/wake`, `…/sleep` | wecken, schlafen (nur Tiny 2) |
| `GET`, `PUT`, `DELETE camera-presets/{id}` | Preset lesen, anlegen/ändern, löschen |
| `POST camera-presets/{id}/save-current` | aktuelle Position (und bei Bedarf Bildwerte) ins Preset übernehmen |
| `POST camera-presets/{id}/recall` | abrufen; `{"wake": true}` ist Standard |
| `PUT lights/{light}` | `{"on": true/false/"toggle", "brightness": 3…100, "temperature": 143…344}`, jedes Feld wahlweise; `lights/all` für alle Lampen |
| `PUT lights/{light}/config`, `POST lights`, `DELETE lights/{light}` | Lampe umbenennen, Adresse ändern, anlegen, entfernen |
| `GET`, `PUT`, `DELETE light-presets/{id}`, `POST light-presets/{id}/save-current`, `POST light-presets/{id}/recall` | wie Kamera-Presets |
| `GET events` | Ereignisse als Server-Sent Events: `status`, `controls`, `presets`, `lights` (je ganzer neuer Stand des Teils) |
| `GET export` | alle Dateien des Datenordners als ein JSON (Sicherung) |

- **Antworten:** `200` mit `{"ok": true, …}`; sonst `{"ok": false, "code": "…", "message": "…"}` mit `400 invalid`, `403 forbidden`, `404 not_found`, `409 camera_absent`, `409 camera_asleep`, `409 file_error`, `502 light_unreachable`, `504 timeout`.
- **Beispiele für das Stream Deck** (nur Schnittstelle; der Umbau des Stream-Deck-Projekts ist ein eigener Auftrag):
  - `POST /api/v1/light-presets/stream/recall`
  - `PUT /api/v1/lights/all` mit `{"on": "toggle"}` (ersetzt später `elgato_licht`)
  - `POST /api/v1/camera-presets/totale/recall`

## 5. Sicherheit (Frage 3)

Empfehlung ohne Anmeldung, dafür drei Schichten:
1. **Bindung:** Der Dienst lauscht nur auf IPv4, auf der Adresse des Streaming-PCs im Heimnetz und `127.0.0.1` (wie der Overlay-Server, `docs/01-festlegungen.md`).
2. **Absender prüfen:** `dienst.ini` nennt die erlaubten Adressen (Standard: `<studio-pc>` und `127.0.0.1`). Andere bekommen `403` und einen Eintrag ins Protokoll. Die Liste lässt sich nicht über die Schnittstelle ändern, nur in der Datei.
3. **Firewall:** Die Zone des Streaming-PCs öffnet heute 1025–65535 für alle (`docs/02-umgebung.md`). `system-einrichten.sh` (sudo, startet der Nutzer) fügt eine Regel hinzu, die den Port des Dienstes nur für `<studio-pc>` öffnet und für alle anderen sperrt. Die genaue firewalld-Regel (Reihenfolge von Regeln mit `reject` gegenüber dem offenen Portbereich) prüft Abschnitt c gegen die firewalld-Doku (Regel 4) und am Gerät von einem dritten Gerät aus (N7).

Keine Zugangsdaten im Spiel; im Protokoll stehen keine Adressen anderer Absender ausser bei `403`.

## 6. Installation und Aktualisierung (Streaming-PC)

| | Ort |
|---|---|
| Programm | `~/.local/opt/dss-kamera/` |
| Daten | `~/.local/share/dss-kamera/` |
| Einstellungen | `~/.config/dss-kamera/dienst.ini` (Port, erlaubte Adressen, Lauschadresse) |
| Einheit | `~/.config/systemd/user/dss-kamera.service` (`Restart=on-failure`, `WantedBy=default.target`, startet mit der Anmeldung) |
| Sicherung vor Aktualisierung | `~/.local/share/dss-kamera-sicherungen/<JJJJ-MM-TT_hh-mm-ss>/` |
| Protokoll | Journal (`journalctl --user -u dss-kamera`) |

- **`kamera-dienst/installieren.sh <streaming-pc>`** startet der Nutzer auf dem Studio-PC, eine Zeile mit vollem Pfad (`~/DoNs_Streaming_Studio/kamera-dienst/installieren.sh <streaming-pc>`). Es kopiert über SSH (`tar` über `ssh`, kein `rsync` nötig), sichert vorher die Daten, schreibt die Einheit, startet den Dienst neu und prüft `GET /api/v1/status`. Dasselbe Skript aktualisiert. Testoptionen wie `scripts/installieren.sh`: `--ziel ORDNER`, `--ohne-einheit` (für die Prüfung durch Lotse).
- **`system-einrichten.sh`** (sudo) startet der Nutzer einmal auf dem Streaming-PC: `sudo ~/.local/opt/dss-kamera/system-einrichten.sh`. Es zeigt zuerst, was es tut: Firewall-Regel (5.), bei Bedarf udev-Regel gegen Autosuspend (2.3, Regel 8). Mehrfach aufrufbar, `--entfernen` macht es rückgängig.
- **Claude Code installiert nie** in `~/.local/opt/dss-kamera/` und ändert `~/.local/share/dss-kamera/` nicht (wie Regel 18). Prüfungen am Gerät laufen mit einer Testinstanz (13., Frage 4).
- **cameractrls:** Der Wiederherstellungsdienst von cameractrls (`cameractrlsd`) und der Dienst dürfen nicht gegeneinander Werte setzen. Schritt 0 prüft, ob er läuft; `LIESMICH.md` sagt, ihn abzuschalten. cameractrls selbst bleibt als Notlösung, aber nie gleichzeitig mit XU-Befehlen.

## 7. Studio: Kern und Schnittstelle

- **Bibliothek `src/camera/`** (`dss_camera`, Qt Core und Qt Network, kein Qt Gui, kein `protocol.h`): `CameraServiceClient` (Anfragen, Ereignisstrom mit Wiederverbinden nach 2 s, Zustand `off`/`connecting`/`connected`/`error`), angebunden an einer Stelle in `CoreService` (wie `setObsConnection`). Schalter `DSS_CORE_CAMERA` (Standard ON); Begründung: steuert Geräte an einem anderen PC, riskanter Teil nach Regel 8.
- **`kern.ini`**, neuer Abschnitt:
  ```ini
  [kamera-dienst]
  aktiv=nein
  adresse=
  port=8093
  ```
  Dialog «Einstellungen», neuer Bereich «Kamera-Dienst» (Aktiv, Adresse, Port) im Stil des Bereichs «OBS».
- **Schnittstelle Fenster–Kern** (Version: Endversion von P14 + 1):
  - Kern → Fenster: `camera_service` (Zustand der Verbindung, Meldung, letzter `status` des Dienstes) nach `hello` und bei jeder Änderung an alle; `camera_event` (Ereignisse des Dienstes weitergereicht).
  - Fenster → Kern: `camera_call` (`request_id`, `method`, `path`, `body`) → Antwort `camera_result` (`request_id`, `status`, `body`). Der Kern reicht nur Pfade unter `/api/v1/` aus einer festen Liste weiter.
  - Begründung (Interna): Die Schnittstelle des Dienstes ist an einer Stelle beschrieben (`docs/09-kamera-dienst.md`); kommt dort etwas dazu, bleibt die Schnittstelle Fenster–Kern gleich. Prüfen tut der Dienst.
- **Ereignisfluss (Regel 14):** Der Kern gibt `camera.state_changed` und `light.changed` auf den Fluss aus P13. Auslöser daraus baut P15 nicht.
- **`dss-cli`:** `kamera-status`, `kamera-preset ID`, `licht-preset ID`, `kamera-aufruf METHODE PFAD [JSON]` (Werkzeuge für Lotse).
- **Zwei Versionen** (Einsatz und Arbeit) dürfen beide verbunden sein; beide bekommen die Ereignisse. Die Stumm-Regel aus P14 (3.4) gilt für die neuen Schritte (9.).

## 8. Dock «Kameras» (Frage 1)

Aussehen «Yami», Bausteine aus `src/app/docks.cpp`. Vor Abschnitt e führt der Agent `ui-consistency:finding-patterns` durch und hält die Checkliste im Bericht fest; Abschnitt e endet mit `ui-consistency:verifying` durch einen getrennten Agenten.

| Ort | Inhalt |
|---|---|
| Kopfzeile des Docks | Auswahl der Kamera (Name und Zustand: «wach», «schläft», «fehlt»), Zustand des Dienstes («verbunden», «nicht erreichbar: …»; Platz für zwei Zeilen von Anfang an wie die Zeile «OBS»), Knopf «Wecken»/«Schlafen» (feste Breite), Knopf «Einrichten …» |
| Unterreiter | «Steuerung», «Bild», «Licht», im Stil der Unterreiter aus P13 |
| «Steuerung», Bereich «Bewegen» | **Steuerkreuz** 3 × 3: vier Pfeile, vier Schrägen, in der Mitte «Mitte». Klick = ein Schritt, Halten = fahren bis Loslassen (Fenster wiederholt alle 200 ms). Pfeiltasten der Tastatur, wenn das Kreuz den Fokus hat. Rechts daneben Zoom «−» und «+» (gleich: Klick, Halten), Anzeige Zoom in %, Zahlenfelder «Tempo» (%) und «Schritt» (°) mit Pfeilen. Ausgegraut, solange die Kamera schläft oder fehlt; Hinweis «Kamera schläft – Wecken» |
| «Steuerung», Bereich «Positionen» | Liste der Kamera-Presets der gewählten Kamera, darunter Leiste mit «Abrufen» (auch Doppelklick), «Aktuelle Position speichern …» (Name), «Überschreiben», «Umbenennen», «Löschen» (mit Rückfrage) |
| «Steuerung», Bereich «KI-Verfolgung» | Modus (Auswahl: keine, Normal, Oberkörper, Nahaufnahme, …, nur die geprüften), Tempo; bei der C922 ausgegraut mit Tooltip «Diese Kamera hat das nicht» |
| «Bild» | Bereiche «Bild», «Belichtung», «Fokus», «Weissabgleich», «Weitere», «OBSBOT» mit Bildlaufleiste wie das Dock «Eigenschaften». Je Wert eine Zeile: Regler mit Wertanzeige fester Breite (wie «Lautstärke»), Auswahl bei Menüs, Kästchen bei Ein/Aus. Gemerkte Werte mit Marke, Kontextmenü «Zurücksetzen». In der Titelzeile jedes Bereichs «Zurücksetzen» (Symbol `zuruecksetzen.svg`) |
| «Licht» | Je Lampe ein Bereich mit Name als Titel: «Ein», Regler «Helligkeit» 3–100 %, Regler «Farbtemperatur» 2900–7000 K. Darüber «Alle ein», «Alle aus». Bereich «Licht-Presets»: Liste mit Leiste «Abrufen», «Aktuelles Licht speichern …», «Überschreiben», «Umbenennen», «Löschen»; Knopf «Lampen …» |
| Dialog «Kamera einrichten» | Name, Grenzen Pan und Tilt in °, Richtung umkehren je Achse, Tempo, Schrittweite, «Presets wecken die Kamera», «KI-Verfolgung vor dem Bewegen aus», Liste der gemerkten Werte mit «Vergessen» |
| Dialog «Lampen» | Liste der Lampen mit Name und Adresse, «Hinzufügen», «Entfernen», «Verbindung prüfen» |
| Dialog «Einstellungen», Bereich «Kamera-Dienst» | Aktiv, Adresse, Port (7.) |
| Menü «Docks» | Eintrag «Kameras»; das Dock steht in beiden Modul-Reitern zur Verfügung (wie das Dock «Chat» aus P14) |

- **Regel 9:** Die Zeilen im Unterreiter «Bild» sind fest für die bekannten V4L2-Werte (Liste im Code, deutsche Namen); was eine Kamera nicht hat, ist ausgegraut. Bereiche und Menüeinträge kommen vom Dienst. So ändert ein Ereignis nur Werte, nie die Anordnung. Die Bereiche der Lampen werden nur nach dem Dialog «Lampen» neu aufgebaut.
- **Regler senden gebündelt:** höchstens alle 100 ms den letzten Wert (Logik in `dss_common`, getestet ohne Fenster).
- **Symbole:** `nach-links.svg`, `nach-rechts.svg` und vier Schrägen neu (gedrehte Fassungen von `nach-oben.svg`, kein Markenlogo); ohne Datei steht der Ersatztext («←» usw.) da, wie bei `symbolButton()`.
- Die Logik der Ansicht (Zustände, ausgegraute Teile, Kelvin/Mired, Bündeln, Wiederholen beim Halten) liegt in `dss_common` (`cameraview.{h,cpp}`) und ist ohne Fenster getestet (Regel 9).

## 9. Aktionssystem (P14) und Stream Deck

- **Neue Schritte in P14** (Abschnitt f), Format wie P14 2.4:

  | Typ | Felder | Verhalten |
  |---|---|---|
  | `camera_preset` | `preset` (Kennung), `wake` (Standard an), `wait` (bis die Bewegung fertig ist) | ruft das Preset über den Kern ab; Fehler (Dienst weg, Kamera fehlt) im Verlauf, der Lauf geht weiter |
  | `light_preset` | `preset` | ruft das Licht-Preset ab |

- Im Probelauf (P14 3.6) laufen beide **nicht** echt («Probelauf: würde Preset «Totale» abrufen»), weil sie etwas Sichtbares im Raum ändern.
- Stumm-Regel (P14 3.4): Läuft der Kern der Version im Einsatz, führt der Kern der Version in Arbeit die Schritte nicht aus.
- Kamera-Fernbedienung per Chat bleibt «später» (Entscheid 2026-11-15); sie baut auf diesen Schritten auf.
- **Stream Deck:** Plan nur der Schnittstelle (4.). Nach Abschnitt c kann das Stream-Deck-Projekt in einem eigenen Auftrag umgestellt werden; das hängt nicht vom Studio ab.

## 10. Schritt 0 am Gerät (Lotse, vor Abschnitt a)

Rechte nach Frage 4. Nur bei geschlossenem OBS bzw. ausserhalb eines Streams (vorher Prozess und Ausgabe prüfen), installierter Dienst gibt es noch nicht, cameractrls geschlossen. Python-Skripte nur im Scratchpad von Lotse, über SSH gestartet. `dmesg -w` läuft mit. Ergebnisse mit Datum und Befehl in den Bericht (Regel 3).

**Konstanten zuerst bestätigen** (Studio-PC, Header lesen): `UVC_SET_CUR` 0x01, `UVC_GET_CUR` 0x81, `UVC_GET_LEN` 0x85, `UVC_GET_INFO` 0x86 aus `/usr/include/linux/usb/video.h`; `struct uvc_xu_control_query` und `UVCIOC_CTRL_QUERY` aus `/usr/include/linux/uvcvideo.h`. Die Werte oben sind aus der UVC-Spezifikation übernommen und **nicht geprüft**.

**Reihenfolge der Bytefolgen (Tiny 2, Unit 2):**

| Nr. | Was | Anfrage | Erwartet | Abbruch, wenn |
|---|---|---|---|---|
| 1 | Länge Statusblock | Selektor `0x06`, `GET_LEN` | 2 Byte, Wert 60 (`3c 00`) | anderer Wert: Halt, nichts schreiben |
| 2 | Fähigkeiten | Selektor `0x06`, `GET_INFO` | Bits «GET» und «SET» gesetzt | – |
| 3 | Status wach | Selektor `0x06`, `GET_CUR`, 60 Byte, Kamera wach (LED an) | Byte `0x09` = 1; Bytes `0x06`, `0x11`, `0x18`, `0x1c`, `0x24` notieren | Byte `0x09` ≠ 1 |
| 4 | Status im Schlaf | wie 3, Kamera schläft (Kopf unten, LED aus; nur lesen, kein Bewegungsbefehl) | Byte `0x09` = 3; Zeit der Antwort notieren | Fehler oder > 1 s: Autosuspend prüfen (Nr. 11) |
| 5 | Länge Frame-Protokoll | Selektor `0x02`, `GET_LEN` | Wert notieren | – |
| 6 | **Wecken** | Bytefolge für «Wecken» aus `tiny2` (`src/lib.rs`, nur lesen, Folge notieren, nicht Code kopieren), `SET_CUR` auf dem dort genannten Selektor, Kamera schläft, **kein offener Videostrom** | danach alle 100 ms `GET_CUR` (Nr. 3), bis Byte `0x09` = 1 (höchstens 10 s); Zeit notieren | nach 10 s nicht 1, oder Abmeldung im `dmesg` |
| 7 | Erster Befehl nach dem Wecken | 400 ms nach Status 1: Pan +2° und zurück (`S_CTRL`) | Bewegung sichtbar, kein Eintrag im `dmesg`. Dann mit 0, 100, 200 ms wiederholen: Ab wann geht der erste Befehl verloren? | Neustart der Kamera |
| 8 | **Schlafen** | Bytefolge «Schlafen» aus `tiny2`, `SET_CUR` | Byte `0x09` = 3, LED aus | – |
| 9 | Wiederholen | 6 bis 8 fünfmal | immer gleich | ein Ausreisser: Halt |
| 10 | Harmloses Schreiben | HDR an `01 01 01` (auf 60 Byte mit Nullen), Status lesen (`0x06` = 1), HDR aus `01 01 00`; Bildwinkel `04 01 01` und zurück auf den Wert aus Nr. 3 | Bild ändert sich, Status folgt | Fehler |
| 11 | Autosuspend | `power/control` und `runtime_status` der Kamera in `/sys/bus/usb/devices/…` lesen; Nr. 4 mit «suspended» zehnmal, Zeiten notieren | zuverlässig, < 300 ms | sonst udev-Regel in `system-einrichten.sh` |
| 12 | KI-Modus | Bytefolge «keine Verfolgung» aus `tiny2` (Tag `0x16`), Status `0x18`/`0x1c` lesen; dann Verfolgung an, Kopf bewegen lassen, aus, `G_CTRL` Pan/Tilt mit dem Bild vergleichen | Status folgt; zeigt, ob der gelesene Wert nach Verfolgung stimmt (2.4) | – |
| 13 | Auto-Schlaf | Tag `0x0B` im Statusblock suchen (Vergleich vor/nach Ändern in einer zweiten Stufe, **nur mit OK des Nutzers** schreiben) | Lage im Statusblock bekannt | – |
| 14 | Schwelle zum Einschlafen | wach, Videostrom **zu**: Tilt von −60° in 2°-Schritten nach unten, nach jedem Schritt Status lesen; **sofort anhalten**, sobald `0x09` ≠ 1 | Winkel der Schwelle; Standard der Tilt-Grenze = Schwelle + 10° | – |
| 15 | Mit Videostrom | OBS mit Kameraquelle in «test 2» offen (kein Stream, keine Aufnahme): Nr. 3, 10 und ein Pan-Schritt; dann Kameraquelle aus- und einschalten, währenddessen Nr. 3 lesen | kein `EPROTO` im `dmesg` | `EPROTO`: Regel 7 in 2.3 verschärfen, Meldung an Lotse |
| 16 | Dauerbewegung | wach: Pan in 1°-Schritten alle 50 ms und alle 100 ms über 20°; dann `pan_speed` ±40 für 1 s | Bild in OBS ruckelfrei? Welcher Weg ist ruhiger? | – |

**Weiter am Streaming-PC (nur lesen):** `python3 --version`; Namen in `/dev/v4l/by-id/`; Rechte auf `/dev/video*` (`getfacl`, Benutzer ohne Gruppe `video`?); freier Port (`ss -ltn`); `firewall-cmd --get-active-zones` und, falls ohne Root lesbar, `--list-all`; läuft `cameractrlsd`?; Liste aller V4L2-Werte beider Kameras mit Bereichen (wie Probelauf 1 der Recherche); C922: Netzfrequenz 50 Hz setzen (**mit OK des Nutzers**), Umstecken durch den Nutzer, danach lesen (fällt sie zurück?); `exposure_absolute` mit `exposure_auto` = 1 schreibbar?

Weicht etwas ab, das eine Schutzregel trägt (Nr. 1, 3, 4, 6, 8, 14): Halt und Meldung an Lotse; der Plan wird angepasst, bevor Abschnitt b beginnt.

## 11. Bau-Abschnitte

Jeder Abschnitt endet mit Bau ohne Warnungen und `ctest` (Stufe «Klein») durch den Agenten und einer Prüfung durch Lotse. Dauer: Sitzungen des Agenten à 4 bis 5 h.

| Abschnitt | Hängt ab von | Fasst das Studio an |
|---|---|---|
| Schritt 0 | – | nein |
| a, b, c (Dienst) | Schritt 0 | nein (nur `kamera-dienst/`, eine Zeile `add_subdirectory` in `CMakeLists.txt`) |
| d (Kern) | c, **P14 abgenommen** | ja: Kern, Schnittstelle, `kern.ini`, Dialog «Einstellungen» |
| e (Dock) | d | ja: Fenster |
| f (Schritte, Doku) | e, P14 | ja: Aktionssystem |

**a, b und c laufen unabhängig von P13/P14** und können nach dem Go parallel dazu laufen (Frage 4), ohne P13/P14 zu verschieben. d bis f kommen nach P14, weil sie dieselben Stellen ändern (Version der Schnittstelle, `kern.ini`, Dialog «Einstellungen», Anordnung der Docks je Reiter).

### a – Dienst: Grundgerüst und V4L2 (3 Commits, ~1 Sitzung)

1. Paket `kamera-dienst/dss_camera/`: Start, `dienst.ini`, Sperre, HTTP-Server mit Bindung und Absenderprüfung (5.), `GET status`, Fehlerformat; Attrappen-Modus `--attrappe` (Kameras und Lampen simuliert). Tests `kamera-dienst/tests/test_api.py` (unittest), in `ctest` als `kd_api`.
2. `v4l2.py` und `devices.py`: Strukturen, ioctl, Suche über `by-id`, Meta-Knoten überspringen, Ein-/Ausstecken (Attrappe mit Ordner statt `/dev`). Prüfung der ioctl-Nummern und Strukturgrössen gegen die Kernel-Header des Studio-PCs mit einem kleinen C++-Hilfsprogramm (`tests/kamera_ioctl_werte.cpp`), Test `kd_v4l2`.
3. `GET`/`PUT controls`, Reihenfolge Automatik → Werte, Zurücklesen, gemerkte Werte in `kameras.json`, neu setzen beim Erscheinen. Attrappe der C922 mit den Bereichen aus dem Nachtrag (Netzfrequenz fällt beim Umstecken auf 60 Hz). Test `kd_werte`.

**Prüfung a:** Stufe «Klein». Lotse: Bau, `ctest`; Testinstanz am Streaming-PC auf `127.0.0.1` mit `--ohne-einheit` (Frage 4): `GET status` und `GET controls` beider Kameras, nur lesen.

### b – Dienst: OBSBOT, Schutzregeln, Bewegen, Kamera-Presets (3 Commits, ~1,5 Sitzungen)

1. `uvc_xu.py`, `obsbot.py`: Statusblock lesen, Wecken, Schlafen, die in Schritt 0 geprüften Sonderfunktionen als Werte der Gruppe «OBSBOT». Attrappe der Tiny 2: Status, Schlaf durch Neigen unter die Schwelle, **ein Bewegungsbefehl im Schlaf mit offenem Strom zählt als «Neustart»** (der Test verlangt null Neustarts), Verzögerung nach dem Wecken mit verlorenen Befehlen. Test `kd_obsbot`.
2. Schutzregeln (2.3), `move`/`stop`/`step`/`home` mit Totmann und Grenzen (2.4), ersetzbare Uhr. Test `kd_bewegen` (Grenzen halten auch bei Dauerbewegung und Presets; Totmann hält nach 500 ms; Wecken vor Preset; KI aus vor Bewegung; Kamera verschwindet während der Bewegung).
3. Kamera-Presets (3.), `save-current`, `recall`, Kennungen, ungültige Datei. Test `kd_presets`.

**Prüfung b:** Stufe «Klein», dazu 20 Wiederholungen von `kd_bewegen` (Takt). Lotse: Testinstanz am Gerät: Wecken/Schlafen fünfmal, Preset aus dem Schlaf, Dauerbewegung nach unten hält an der Grenze, `dmesg` ohne Abmeldung.

### c – Dienst: Licht, Ereignisse, Installation (3 Commits, ~1 Sitzung)

1. `lights.py`, Licht-Presets, Begrenzung, Nachführen alle 5 s. Attrappe einer Lampe als HTTP-Server (stiller Fehler ausserhalb des Bereichs, Zeitüberschreitung, `400` bei falschem `Content-Length`). Test `kd_licht`.
2. `GET events` (SSE), `GET export`; Doku `docs/09-kamera-dienst.md` (Schnittstelle mit Beispielen, Fehlercodes, Beispiele für das Stream Deck). Test `kd_ereignisse`.
3. `installieren.sh` (mit Testoptionen und Sicherung), `system-einrichten.sh` (firewalld-Regel nach Doku geprüft, Quelle im Kommentar; udev-Regel nur, wenn Schritt 0 sie verlangt; `--entfernen`), Einheit, `LIESMICH.md`. Test `kd_installieren` (installiert mit `--ziel` in einen Ordner unter `build/` auf dem Studio-PC, ohne Einheit).

**Prüfung c:** Stufe «Klein». Lotse: Installation mit Testoptionen in einen Testordner am Streaming-PC, Licht über die Testinstanz (L2). Danach ist der Dienst ohne Studio nutzbar (Installation durch den Nutzer, N7).

### d – Studio: Kern-Anbindung (3 Commits, ~1,5 Sitzungen; nach P14)

1. `src/camera/` (`dss_camera`): `CameraServiceClient`, Ereignisstrom, Wiederverbinden, Zustand. Test `tst_kameradienst` gegen den echten Python-Dienst im Attrappen-Modus (als eigener Prozess auf `127.0.0.1`, freier Port).
2. Kern: `[kamera-dienst]` in `kern.ini`, `settings`/`update_settings`, `camera_service`, `camera_event`, `camera_call`/`camera_result` mit Liste erlaubter Pfade, Ereignisfluss, Schalter `DSS_CORE_CAMERA`, `dss-cli`. Tests `tst_protocol`, `tst_kerneinstellungen`, neu `tst_kernkamera` (Kern als Prozess gegen den Dienst in der Attrappe: Preset über den Socket abrufen, Dienst beenden → Zustand `error`, neu starten → `connected`).
3. Dialog «Einstellungen», Bereich «Kamera-Dienst». Test `tst_fensterbearbeiten` (Bereich vorhanden, Werte gehen an den Kern).

**Prüfung d:** Stufe «Klein», 20 Wiederholungen von `tst_kernkamera`. Lotse: Bau, `ctest`, `dss-cli kamera-status` gegen die Testinstanz am Streaming-PC (Adresse nur auf `127.0.0.1` dort → per SSH-Tunnel; oder gegen den installierten Dienst, wenn N7 erledigt ist).

### e – Studio: Dock «Kameras» (3 Commits, ~2 Sitzungen)

0. `ui-consistency:finding-patterns`, Checkliste im Bericht.
1. Ansichtslogik `src/common/cameraview.{h,cpp}`: Zustände, ausgegraute Teile, Halten mit Wiederholung alle 200 ms und Stopp beim Loslassen, Bündeln der Regler, Kelvin/Mired, feste Liste der V4L2-Werte mit deutschen Namen. Test `tst_kameraansicht` ohne Fenster.
2. Dock mit Kopfzeile, Unterreiter «Steuerung» (Steuerkreuz, Zoom, Positionen, KI-Verfolgung) und «Bild», Dialog «Kamera einrichten», Symbole, Menü «Docks». Test neu `tst_fensterkameras` (gegen den Dienst in der Attrappe).
3. Unterreiter «Licht», Dialog «Lampen». Test `tst_fensterkameras`.

**Prüfung e:** Stufe «Klein», `ui-consistency:verifying` durch einen getrennten Agenten. Lotse: Bildschirmfotos des Docks in allen Unterreitern und Zuständen («verbunden», «nicht erreichbar», «schläft», C922 gewählt) und der Dialoge (Scratchpad).

### f – Aktionssystem und Dokumente (2 Commits, ~1 Sitzung)

1. Schritte `camera_preset`, `light_preset` in `dss_actions` mit Dialog «Schritt bearbeiten» (Auswahl aus den Presets des Dienstes), Probelauf, Stumm-Regel. Tests `tst_aktionsablauf`, `tst_kernaktionen`.
2. Dokumente: `01` (Kamerasteuerung, Entscheide), `02` (Streaming-PC: Dienst, Port, Firewall-Regel, Python-Version), `03` (neuer Abschnitt «Kameras und Licht»), `04` (neue Punkte), `05` (Quellen), `06` (Dateien des Dienstes), `08` (Version, `camera_*`, `[kamera-dienst]`), `09` nachgeführt; Bericht `docs/archiv/berichte/P15-bericht.md`.

**Prüfung f:** Prüfstufe «Baustein» (12.).

## 12. Prüfstufe «Baustein» (Ordner `build/pruefung-p15/`)

- `cmake --build build -j8`
- `ctest --test-dir build` mit aktiver X11-Sitzung, 0 übersprungen (darin die `kd_*`-Tests des Dienstes)
- frischer Klon (Bau und `ctest`)
- `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p15/start/startpruefung`
- `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p15/dienst`
- **50 Wiederholungen** (Takt, Threads, Netz): `kd_bewegen`, `kd_obsbot`, `kd_ereignisse`, `tst_kameradienst`, `tst_kernkamera`, `tst_fensterkameras`. Ohne Last daneben, nie zwei Fenstertests gleichzeitig.
- Last am Dienst (Attrappe, Studio-PC): 10 Anfragen je Sekunde über 60 s gemischt (`move`, `controls`, `recall`, Licht), dazu Ein-/Ausstecken alle 5 s. Erwartet: keine Antwort über 3 s, null «Neustarts» der Attrappe, Totmann hält jedes Mal. CPU des Dienstes als Messwert in den Bericht.

## 13. Handtest (Regel 10)

**Prüft Lotse** (Rechte nach Frage 4; nie während eines Streams, vorher Stream und Aufnahme prüfen; Testinstanz nur, wenn der installierte Dienst gestoppt ist):

**L1 Dienst an den Kameras.**
- Was: über die Schnittstelle (per SSH auf `127.0.0.1`): `status`, `controls` beider Kameras; Wecken/Schlafen zehnmal; Preset aus dem Schlaf (weckt, fährt, kein Neustart); Dauerbewegung nach unten 10 s → hält an der Grenze; Totmann: Bewegung starten, Verbindung trennen → steht nach höchstens 0,5 s (Position zurücklesen); `dmesg` ohne Abmeldung.
- Warum: echtes Gerät mit Schlaf, den die Attrappe nur nachbildet.

**L2 Licht.**
- Was: Licht-Presets «Test hell», «Test warm» anlegen, je zehnmal abrufen, Antwortzeiten messen; Werte ausserhalb des Bereichs werden begrenzt; danach Presets löschen und den Zustand vor dem Test wiederherstellen.
- Warum: echte Lampen im Netz.

**L3 Absender und Firewall.**
- Was: vom Studio-PC `GET status` (geht); vom Streaming-PC über dessen Heimnetz-Adresse (`403`); `firewall-cmd --list-all` bzw. Regeln lesen.
- Warum: Netzregel. Ein drittes Gerät hat nur der Nutzer (N7).

**L4 Studio.**
- Was: Kern und Fenster aus `build/` (eigenes `XDG_CONFIG_HOME`) gegen den installierten Dienst; Bildschirmfotos des Docks (alle Unterreiter, Zustände) im Scratchpad; Preset per `dss-cli kamera-preset`; Bild der Kameraquelle in «test 2» über obs-websocket holen (vorher/nachher). Testquelle danach löschen, Szene zurück auf «Szene».
- Warum: echter Weg Fenster → Kern → Dienst → Kamera, Bild in OBS.

**L5 Neustart.**
- **Eigener Schritt:** `systemctl --user restart dss-kamera` (Streaming-PC).
- Was: Presets, Lampen, gemerkte Werte unverändert; Studio verbindet sich von selbst neu.
- Warum: Speichern und Wiederverbinden im echten Betrieb.

**Prüft der Nutzer:**

**N1 Steuerkreuz und Zoom.**
- Was: Klick und Halten in alle Richtungen, Zoom, Tempo, Schritt; Richtung der Pfeile passt (sonst «Richtung umkehren»); Positionen anlegen, abrufen, umbenennen, löschen; Bild in OBS ansehen.
- Warum: Bedienung und Gefühl.

**N2 Bildwerte.**
- Was: C922 Netzfrequenz 50 Hz (Flackern weg?), Belichtung, Fokus, Weissabgleich; Tiny 2 HDR, Bildwinkel, KI-Verfolgung; Bild in OBS beurteilen.
- Warum: Aussehen im Stream.

**N3 Umstecken.**
- **Eigener Schritt:** beide Kameras bei laufendem OBS (ohne Stream) ab- und wieder anstecken.
- Was: Nach wenigen Sekunden stimmen die gemerkten Werte wieder (C922 50 Hz); Lotse liest das Protokoll des Dienstes und `dmesg`.
- Warum: Nur der Nutzer kann umstecken.

**N4 Schlafen und Wecken.**
- Was: «Schlafen» und «Wecken» im Dock, LED und Bild in OBS; Preset aus dem Schlaf.
- Warum: LED und Kopf sieht nur der Nutzer.

**N5 Licht.**
- Was: eigene Licht-Presets «Stream» und «Abend» anlegen und abrufen; Farbtemperatur und Helligkeit ansehen.
- Warum: Licht im Raum.

**N6 Aktionssystem.**
- Was: eine Aktion mit «Kamera-Preset» und «Licht-Preset» anlegen, echt auslösen (Test-Knopf mit «echt ausführen»).
- Warum: Bedienung des Editors.

**N7 Installation und Netzregel.**
- **Eigene Schritte:** `installieren.sh <streaming-pc>` auf dem Studio-PC; `sudo ~/.local/opt/dss-kamera/system-einrichten.sh` auf dem Streaming-PC.
- Was: Dienst läuft nach Anmeldung von selbst; von einem dritten Gerät im Heimnetz (Handy, Browser auf `http://<streaming-pc>:<port>/api/v1/status`) keine Antwort.
- Warum: sudo gibt nur der Nutzer ein; drittes Gerät.

Stream Deck: nicht Teil dieses Handtests (eigener Auftrag im Stream-Deck-Projekt nach Abschnitt c). Was Lotse oder der Agent schon geprüft haben, wiederholt der Nutzer nicht.

## 14. Pakete (Regel 5)

- **Streaming-PC:** kein neues Paket. Python 3 aus Nobara (Version in Schritt 0), nur Standardbibliothek. Kein `v4l2-ctl` nötig.
- **Studio-PC:** kein neues Paket. Qt Network ist da; `python3` 3.13.5 für die Tests des Dienstes ist da. Die Kernel-Header (`linux-libc-dev`) für das C++-Hilfsprogramm in a2: ob installiert, prüft Abschnitt a (sie kommen mit `build-essential` mit, nicht geprüft).

## 15. Schätzung

| Teil | Agent | Lotse | Nutzer |
|---|---|---|---|
| Schritt 0 | – | ~1,5 h | ~15 min (OK für Schreiben, Umstecken der C922, LED ansehen) |
| a Grundgerüst, V4L2 | ~1 Sitzung | ~15 min | – |
| b OBSBOT, Bewegen, Presets | ~1,5 Sitzungen | ~30 min | – |
| c Licht, Ereignisse, Installation | ~1 Sitzung | ~20 min | N7 (~10 min) |
| d Kern | ~1,5 Sitzungen | ~15 min | – |
| e Dock | ~2 Sitzungen | ~30 min | – |
| f Schritte, Dokumente | ~1 Sitzung | – | – |
| Prüfstufe «Baustein» | ~2 h, 50er-Läufe nachts | – | – |
| Handtest | – | ~1,5 h (L1–L5) | ~45 min (N1–N6) |
| **Summe** | **~8 Sitzungen à 4–5 h** | ~5 h | ~1 h 10 min |

## 16. Fragen an den Nutzer (gebündelt)

1. **Wo du Kameras und Licht bedienst** (sichtbar).
   - **A) Empfohlen:** ein Dock «Kameras» mit den Unterreitern «Steuerung», «Bild», «Licht» (Abschnitt 8), in beiden Modul-Reitern über «Docks» einblendbar. Begründung: Während des Streams bist du im Reiter «Chat-Bot» (Live); ein Dock steht dort neben dem Chat.
   - B) Eigener Modul-Reiter «Kameras» neben «Overlays» und «Chat-Bot». Gegenargument zu A: mehr Platz für die vielen Bildwerte; dafür nicht gleichzeitig mit Chat oder Editor sichtbar.
   - C) Wie A, aber Licht als eigenes Dock «Licht». Gegenargument zu A: Licht und Kamera einzeln anordnen; dafür ein Dock mehr unten.
2. **Was ein Kamera-Preset speichert** (bedienbar).
   - **A) Empfohlen:** Position und Zoom; beim Speichern ein Kästchen «Bildwerte mitspeichern» (Standard aus), dann auch Belichtung, Weissabgleich usw. So reicht ein Preset für «Totale», und für «Abendlicht» geht beides.
   - B) Nur Position und Zoom. Gegenargument zu A: einfacher (etwa ¼ Sitzung weniger), aber Bildwerte nur von Hand.
   - C) Immer alles. Gegenargument: Abrufen einer Position verstellt dann auch das Bild, das du gerade angepasst hast.
3. **Schutz des Dienstes** (Sicherheit).
   - **A) Empfohlen:** ohne Anmeldung. Der Dienst hört nur im Heimnetz (IPv4) und antwortet nur dem Studio-PC und sich selbst; dazu eine Firewall-Regel, die du einmal mit sudo setzt (Abschnitt 5).
   - B) Zusätzlich ein Schlüssel (Token). Gegenargument zu A: Schutz auch gegen ein Gerät, das die Adresse des Studio-PCs vortäuscht; dafür muss der Schlüssel verschlüsselt auf beiden PCs liegen (Regel 12) und auch das Stream-Deck-Projekt ihn kennen.
4. **Go und Rechte für Lotse.**
   - **A) Empfohlen:** Go für Schritt 0 und die Abschnitte a bis c (nur der Dienst) parallel zu P13/P14, wenn Lotse Platz hat, ohne P13/P14 zu verschieben; d bis f nach der Abnahme von P14. Lotse darf am Streaming-PC (nie während eines Streams, OBS vorher prüfen): die Kameras bewegen, wecken und schlafen legen, XU-Befehle aus Schritt 0 senden, eine Testinstanz des Dienstes auf `127.0.0.1` starten und beenden, den installierten Dienst dafür mit `systemctl --user stop/start dss-kamera` anhalten, Licht für Tests schalten (danach wie vorher). Werte, die bleiben (Netzfrequenz, Auto-Schlaf), nur mit deinem OK in der Sitzung. Das wird in `CLAUDE.md`, «Umgebung», eingetragen.
   - B) Go, aber alles erst nach P14. Gegenargument zu A: P13/P14 bekommen die ganze Aufmerksamkeit; dafür kommt das Stream Deck später an Kamera- und Licht-Presets.
   - C) Änderungen am Plan, bitte nennen.

## 17. Risiken und `[OFFEN]`

| Risiko oder offene Frage | Gegenmassnahme oder Stand |
|---|---|
| Bytefolgen für Wecken, Schlafen, KI-Modus nur aus `tiny2` (andere Firmware, teils KI-geschrieben) | Schritt 0 Nr. 6, 8, 12; nur geprüfte Befehle freigeschaltet |
| Bewegungsbefehl im Schlaf löst Neustart der Kamera aus | Schutzregeln 2.3, Test mit Attrappe (null Neustarts), L1 |
| Schwelle zum Einschlafen beim Neigen unbekannt (−84° war schon Schlaf) | Schritt 0 Nr. 14, Grenze einstellbar |
| Gelesene Position nach KI-Verfolgung womöglich veraltet | Schritt 0 Nr. 12; KI vor Bewegung aus; sonst `[OFFEN]` |
| `EPROTO` beim Einstecken oder bei XU während Strom-Wechsel (Einzelbericht, anderes Modell) | Schritt 0 Nr. 15, 3 s Wartezeit, N3 |
| USB-Autosuspend | Schritt 0 Nr. 11, udev-Regel nur bei Bedarf |
| Firewall-Regel: Reihenfolge gegenüber dem offenen Portbereich der Zone | gegen die firewalld-Doku prüfen (Abschnitt c), Absenderprüfung im Dienst als zweite Schicht, N7 |
| Konstanten `UVC_*` und Struktur `uvc_xu_control_query` aus dem Gedächtnis der UVC-Spezifikation | Schritt 0 gegen die Kernel-Header, Test `kd_v4l2` |
| Python-Version auf Nobara 44 unbekannt | Schritt 0; Code für ≥ 3.11 |
| Rechte auf `/dev/video*` ohne Gruppe `video` | Schritt 0 (`getfacl`); sonst Frage an den Nutzer |
| cameractrls oder `cameractrlsd` setzen gleichzeitig Werte | Schritt 0, `LIESMICH.md` |
| Stream-Deck-Taste `elgato_licht` schaltet Lampen am Dienst vorbei | Nachführen alle 5 s (2.6); Umstellung im Stream-Deck-Projekt |
| Presets nicht in der Sicherung des Studios | Sicherung durch `installieren.sh`, `GET export` |
| Umfang gross (~8 Sitzungen) | Abschnitte einzeln lauffähig; der Dienst ist nach c ohne Studio nutzbar |

## Annahmen und Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| Code des Dienstes in `kamera-dienst/` im Studio-Repo, nicht in einem eigenen Repo | eine Version für Dienst und Studio, Schnittstelle und Dock ändern sich im selben Commit, gemeinsamer Bauplan und `ctest`. Gegenargument: andere Sprache und anderer PC im selben Repo; das trennt der Unterordner mit eigener Installation |
| Python mit Standardbibliothek, kein `aiohttp`, `FastAPI`, `pyudev`, `linuxpy` | kein Paket am Streaming-PC (keine Änderung am System, Regel 5); `linuxpy` wäre GPL-3.0 |
| Kein Code aus `tiny2`/`Tiny4Linux` kopiert, Bytefolgen selbst geschrieben und am Gerät geprüft | EUPL-1.2 (Recherche, Frage 2) und unsichere Herkunft der Folgen |
| Ein Arbeitsfaden je Kamera, Warteschlange | XU ohne Sperre; keine gleichzeitigen Zugriffe |
| Sperre gegen eine zweite Instanz | Testinstanz und installierter Dienst dürfen nicht beide XU senden |
| Dauerbewegung über absolute Schritte alle 50 ms | Grenzen halten sicher, auch gegen den Schlaf; Speed-Werte nur nach Schritt 0 |
| Totmann 500 ms, Wiederholung 200 ms | Bewegung endet, wenn Fenster oder Netz wegfallen |
| Nur verstellte Werte merken und neu setzen | der Dienst überschreibt nichts, was der Nutzer nicht gewählt hat |
| Presets im Dienst | Stream Deck ohne Studio, eine Stelle (Abschnitt 3) |
| Kennung eines Presets aus dem Namen, bleibt beim Umbenennen | wie die Adresse eines Overlays; Tasten und Aktionen gehen weiter |
| Kern spricht mit dem Dienst, das Fenster nicht direkt | Verbindungen betreibt der Kern; P14 braucht den Weg ohnehin |
| `camera_call` reicht Pfade weiter, statt jede Anfrage einzeln in `08` | Schnittstelle des Dienstes an einer Stelle beschrieben; Fenster–Kern bleibt bei neuen Funktionen gleich |
| Kein fünfter Verbindungsknopf im Kopfbereich | vier Knöpfe sind entschieden (2026-11-02); Zustand des Dienstes im Dock und im Dialog «Einstellungen» |
| Feste Zeilen für bekannte V4L2-Werte | Regel 9: Ereignisse ändern keine Anordnung |
| Mired gespeichert, Kelvin angezeigt | Gerät rechnet in Mired; Kelvin ist vertrauter |
| Lampen-Adressen fest eingetragen, kein mDNS | kein Paket nötig; Adressen am Router reservierbar |
| Neuer Schalter `DSS_CORE_CAMERA` | Geräte an einem anderen PC, riskanter Teil nach Regel 8 |
| Port-Vorschlag 8093 | frei laut Schritt 0, nicht 4455 (obs-websocket), nicht 8080/8081 (Overlay-Server) |
| Schritte «Kamera-Preset», «Licht-Preset» laufen im Probelauf nicht | ändern etwas im Raum, wie Schritte nach Twitch und OBS |

## Quellen

- **Lokal gelesen (2026-11-15):**
  - `CLAUDE.md`, `.claude/HANDOFF.md`
  - `docs/01-festlegungen.md`, `docs/02-umgebung.md`, `docs/03-funktionen-entwurf.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md` (Kopf, «Einstellungen», «Anbindung an OBS», «Overlay-Server»)
  - `docs/archiv/prompts/P13-twitch.md` (Reiter, Unterreiter «Live»), `docs/archiv/prompts/P14-chatbot.md`
  - `src/app/docks.h`, `src/app/docks.cpp`, `src/app/headerbar.cpp:150-256`, `src/app/mainwindow.cpp:56-285`, `src/app/settingsdialog.cpp` (Bereich «OBS»), `src/app/symbole/`, `tests/CMakeLists.txt`
  - Entscheide des Nutzers vom 2026-11-15 und Recherche von Lotse «Kamera- und Lichtsteuerung unter Linux» mit Nachtrag «Probeläufe am Gerät» (Kopie im Arbeitsordner, nicht im Repo)
- **Aus der Recherche übernommen, am 2026-11-15 von Lotse gelesen, von mir nicht erneut abgerufen** (Regel 4: die Abschnitte b und c lesen die für sie tragenden Stellen erneut):
  - https://github.com/cgevans/tiny2 und https://raw.githubusercontent.com/cgevans/tiny2/main/src/lib.rs (XU, Tags, Statusblock, Wecken/Schlafen, 400 ms), https://github.com/cgevans/tiny2/issues (Issue #7)
  - https://github.com/OpenFoxes/Tiny4Linux, https://github.com/OpenFoxes/Tiny4Linux/blob/main/LICENSE.md (EUPL-1.2)
  - https://docs.kernel.org/userspace-api/media/drivers/uvcvideo.html (`UVCIOC_CTRL_QUERY`, `UVCIOC_CTRL_MAP`)
  - https://docs.kernel.org/6.3/userspace-api/media/v4l/control.html (`power_line_frequency`)
  - https://lkml.iu.edu/2511.2/02322.html, https://lkml.iu.edu/2512.1/00400.html (Minimum der Speed-Werte)
  - https://github.com/soyersoyer/cameractrls (Umfang, `cameractrlsd`, LGPL-3.0-or-later)
  - https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/linux-v4l2/v4l2-input.c, https://raw.githubusercontent.com/obsproject/obs-studio/master/plugins/linux-v4l2/v4l2-controls.c
  - https://apihandyman.io/hacking-elgato-key-light-with-postman/ (Key Light, Bereiche, stiller Fehler)
  - https://www.manualslib.com/answers/4463421/obsbot-tiny-2-lite-dedicated-software-functionality.html (Schlaf bei Kopf unten)
- **Nicht gelesen, vor dem Bau zu lesen:**
  - firewalld, Regeln mit Quelle und Reihenfolge: https://firewalld.org/documentation/man-pages/firewalld.richlanguage.html (Abschnitt c)
  - systemd-Benutzerdienste: https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html, https://www.freedesktop.org/software/systemd/man/latest/systemd.special.html (`default.target`, Abschnitt c)
  - Python `http.server`: https://docs.python.org/3/library/http.server.html (Abschnitt a)
  - Server-Sent Events: https://html.spec.whatwg.org/multipage/server-sent-events.html (wie P10; Abschnitt c)
  - V4L2 `VIDIOC_QUERYCTRL`: https://docs.kernel.org/userspace-api/media/v4l/vidioc-queryctrl.html (Abschnitt a)
