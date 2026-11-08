# 08 – Schnittstelle Kern–Fenster

Stand: 2026-11-07 (P10c). Version der Schnittstelle: **11**. Massgebend ist der Code: Nachrichten in `src/common/protocol.h/.cpp`, Kern in `src/core/coreservice.cpp`, Fenster-Seite in `src/common/coreclient.cpp`, verzögertes Speichern in `src/common/savescheduler.h/.cpp`, Overlay-Server in `src/server/`, Seite der Browserquelle in `web/`, Anbindung an OBS in `src/obs/`. Tests: `tst_protocol` und `tst_aenderungen` (alle Nachrichten lesen und schreiben, Abweisungen, Zusammenfassen, ohne Qt Gui), `tst_kernverbindung` und `tst_bearbeiten` (Kern als eigener Prozess, Zusammenspiel, zwei Fenster, Neustart), `tst_kerneinstellungen` (kern.ini ohne Kern), `tst_medienliste` (`medien.json` ohne Kern, seit P10b), `tst_medien` (Medienliste, Videoformate und `dss-cli` mit Kern als eigenem Prozess), `tst_uebernehmen` (Übernehmen von Dateien im eigenen Thread), `tst_overlayserver` (Overlay-Server ohne Fenster), `tst_browserquelle` (Seite in Qt WebEngine über die Adresse im Heimnetz, Stufe 1), `tst_obsprotokoll`, `tst_obsverbindung`, `tst_obspasswort`, `tst_obsquellen` (Anbindung an OBS gegen einen Testserver auf `127.0.0.1`, seit P10c).

Seit P08 kann das Fenster über die Schnittstelle **ändern** (Abschnitt «Änderungen»). Jede Änderung geht als Nachricht an den Kern; nur der Kern schreibt Dateien (K-03). Der **Token** eines Overlays steht seit P10 nur in der Adresse der Browserquelle (`browser_source`), und die bekommt nur die Verbindung, die danach fragt; geändert wird er mit `new_token`. In Overlays, Listen und im Protokoll steht er nie (CLAUDE.md Regel 12).

### Version 11 (P10c)

Neu ist die Anbindung an OBS (T-05, E-29): `obs_status` (Einzelheiten der Verbindung, nach `hello` und bei jeder Änderung an alle), `obs_sources` (Quellen in OBS je Overlay, ebenso), `obs_create_source` und `obs_add_to_scene`. `settings` trägt zusätzlich `obs_enabled`, `obs_host`, `obs_port` und `obs_password_set`, `update_settings` zusätzlich dieselben Werte und `obs_password` (Abschnitt «Einstellungen»). `connections.obs` zeigt seit P10c den echten Zustand. Neue Codes: `obs_not_connected`, `obs_name_taken`, `obs_source_missing`, `obs_timeout`. Ein Kern der Version 10 würde die neuen Anfragen mit `unknown_type` beantworten, und seinem `settings` fehlten die neuen Pflichtfelder. Darum stieg die Version.

### Version 10 (P10b)

Neu sind die Medienliste (E-28): `media_list` (Namen, Ordner, Grösse, Videoformat und Verwendung aller Dateien in `media/`, nach `hello` und nach jeder Änderung an alle), `get_media_list`, `import_progress` (Stand der laufenden Übernahmen, Punkt 73) und die Änderungen `add_media`, `rename_media`, `move_media`, `remove_media`, `create_media_folder`, `rename_media_folder`, `remove_media_folder`, `move_media_folder`, `set_media_folder_expanded` und `cancel_import`. `add_image_source` und `add_video_source` nehmen seit Version 10 auch einen Verweis `media/…` (aus dem Dock «Medien», nichts wird kopiert) und die Felder `center_x`, `center_y` und `insert_index` (Ablegen an der Mausstelle). Neue Codes: `canceled` und `in_use`. Ein Fenster der Version 9 kennt `media_list` nicht und übergeht sie; ein Kern der Version 9 würde `get_media_list` und die neuen Änderungen mit `unknown_type` beantworten, und das Dock «Medien» bliebe leer. Darum stieg die Version. Seit Version 10 trägt der Kern eine leere Adresse im Heimnetz innerhalb von 2 s ein (Punkt 85, Abschnitt «Einstellungen»); das ändert kein Feld.

### Version 9 (P10)

Neu ist `reset_settings` (ungültige `kern.ini` zurücksetzen, Punkt 77). Ein Kern der Version 8 würde sie mit `unknown_type` beantworten, und der Knopf «Auf Standardwerte zurücksetzen» im Dialog «Einstellungen» bliebe ohne Antwort. Seit Version 9 nimmt der Kern als Adresse im Heimnetz nur noch Adressen dieses PCs an (Punkt 78); das ändert kein Feld.

### Version 8 (P10)

Neu ist `server_status` (Zustand des Overlay-Servers, nach `hello` und bei jeder Änderung an alle). Das Fenster der Version 8 zeigt daraus «Port belegt», «Adresse fehlt» und «ungültige Adresse benutzt».

### Version 7 (P10)

Neu sind die Änderung `new_token` sowie `get_browser_source` und `browser_source` (Adresse der Browserquelle mit Token). Ein Kern der Version 6 würde sie mit `unknown_type` beantworten.

### Version 6 (P10)

`overlay` und `overlay_changed` tragen Overlays im Dateiformat 4 (Pflichtfeld `output`, E-24, `docs/06-overlay-format.md`), die ein Fenster der Version 5 als ungültig verwerfen würde; dazu das Feld `output` in `overlay_list` und die Änderung `set_overlay_output`.

### Version 5 (P10-vor)

Die Version stieg von 4 auf 5 wegen der neuen Nachrichten `get_settings`, `settings` und `update_settings` (Abschnitt «Einstellungen»). Ein Kern der Version 4 würde `get_settings` mit `unknown_type` beantworten, und das Fenster der Version 5 erwartet die Einstellungen für den Dialog «Werkzeuge → Einstellungen …». Die übrigen Nachrichten sind unverändert. Neu ist auch, dass `change_result` für Änderungen mit einer neuen Datei später kommen kann und andere Nachrichten in der Zwischenzeit beantwortet werden (Abschnitt «Übernehmen von Dateien»); das ändert kein Feld.

### Version 4 (P09b)

Die Version stieg von 3 auf 4, weil `overlay` und `overlay_changed` jetzt Overlays im Dateiformat 3 tragen (Pflichtfelder `flip_horizontal` und `flip_vertical` je Quelle, `docs/06-overlay-format.md`), die ein Fenster der Version 3 als ungültig verwerfen würde. Dazu kommen das Pflichtfeld `media_sizes` in `overlay` und `overlay_changed` (Originalgrösse der Dateien) und die Felder `changes.flip_horizontal` und `changes.flip_vertical` in `update_source` (ein Kern der Version 3 weist sie als unbekannt ab).

### Version 3 (P09)

Die Version stieg von 2 auf 3, weil `overlay` und `overlay_changed` jetzt Overlays im Dateiformat 2 tragen (Pflichtfeld `video.hold_at_end`, `docs/06-overlay-format.md`), die ein Fenster der Version 2 als ungültig verwerfen würde. Dazu kommen die Änderung `set_overlay_size` (ein Kern der Version 2 antwortet `unknown_type`), das Feld `changes.video.hold_at_end`, das Feld `data_dir` im `hello` des Kerns und das Feld `warning` in `change_result`.

### Version 2 (P08)

Die Version stieg von 1 auf 2, weil sich `overlay_list` so geändert hat, dass ein Fenster der Version 1 die Nachricht als ungültig verwerfen würde (Feld `entries` mit Ordnern statt `overlays`, Reihenfolge der Liste statt nach Name). Dazu kommen die Änderungen, `change_result`, `get_fonts` und `font_list`, die ein Kern der Version 1 mit `unknown_type` beantworten würde. Die Regel «unbekannte Felder werden übergangen» reicht dafür nicht, weil Pflichtfelder wegfallen. Fenster und Kern werden immer zusammen gebaut; eine abweichende Version meldet das `hello` (unten, Ablauf Punkte 2 und 3).

## Verbindung

| Punkt | Festlegung |
|---|---|
| Art | Unix-Socket (`QLocalServer` / `QLocalSocket`), Datenstrom (`SOCK_STREAM`), kein Netzwerk |
| Pfad | Version im Einsatz: `$XDG_RUNTIME_DIR/dons-streaming-studio/kern.sock`, auf dem Studio-PC `/run/user/1000/dons-streaming-studio/kern.sock`. Version in Arbeit (Bau aus dem Repo, seit P10a): `$XDG_RUNTIME_DIR/dons-streaming-studio-entwicklung/kern.sock`. Anderer Pfad: `dss-core --verbindung PFAD`, `dss-app --verbindung PFAD` |
| Länge des Pfads | höchstens 107 Byte (`sun_path` hat 108 Byte mit abschliessender Null, unix(7)); sonst startet der Kern nicht und nennt den Grund |
| Zugriff | Ordner mit Rechten 0700, Socket mit `QLocalServer::UserAccessOption` (nur derselbe Benutzer). `XDG_RUNTIME_DIR` selbst hat 0700 |
| Mehrere Fenster | erlaubt; jedes bekommt alle Nachrichten an «alle» |
| Übrig gebliebener Socket | Findet der Kern beim Start eine Socket-Datei, verbindet er sich probehalber (500 ms). Antwortet dort ein Kern, beendet er sich mit Exit-Code 3; sonst entfernt er die Datei und öffnet sie neu |

Quellen: https://doc.qt.io/qt-6.8/qlocalserver.html (`SocketOption`, `removeServer`), https://doc.qt.io/qt-6.8/qlocalsocket.html, https://man7.org/linux/man-pages/man7/unix.7.html, https://doc.qt.io/qt-6.8/qstandardpaths.html (`RuntimeLocation`).

## Format

- Jede Nachricht ist **ein JSON-Objekt in einer Zeile** (UTF-8, kompakt), abgeschlossen mit `\n` (Byte 0x0A). Zeilenwechsel in Texten stehen im JSON als `\n` (zwei Zeichen), nie als Byte 0x0A.
- Jede Nachricht hat das Feld `type` (Text). Die übrigen Felder hängen vom Typ ab.
- Eine Zeile darf höchstens 16 MiB (16 777 216 Byte) lang sein. Eine längere Zeile beendet die Verbindung (Kern schickt vorher `error` mit `message_too_long`).
- **Pflichtfelder** müssen vorhanden sein und den richtigen Typ haben, sonst ist die Nachricht ungültig. **Unbekannte Felder** werden übergangen; so lassen sich später Felder ergänzen, ohne die Version zu erhöhen. Eine Änderung, die alte Gegenstellen falsch verstehen würden, erhöht die Version. Ausnahme: Im Objekt `changes` von `update_source` ist ein unbekanntes Feld ein Fehler, damit kein Tippfehler still verloren geht.
- Kennungen (`id`) sind UUID in Kleinbuchstaben ohne Klammern, wie in der Overlay-Datei.
- Zahlen: JSON-Zahlen. «ganze Zahl» heisst ohne Nachkommastellen.

## Ablauf

```
Fenster                                 Kern
   | ---- verbindet ---------------------> |
   | <--- hello (Kern) ------------------- |   sofort nach dem Verbinden
   | ---- hello (Fenster) ---------------> |   sofort nach dem Verbinden
   | <--- overlay_list ------------------- |   Antwort auf hello
   | <--- media_list --------------------- |   seit Version 10 (P10b)
   | <--- import_progress ---------------- |   nur wenn gerade etwas übernommen wird
   | <--- connections -------------------- |
   | <--- server_status ------------------ |   seit Version 8 (P10)
   | <--- obs_status --------------------- |   seit Version 11 (P10c)
   | <--- obs_sources -------------------- |   seit Version 11 (P10c)
   | <--- stats (letzte Messung) --------- |
   | <--- stats ------------------------- |   einmal pro Sekunde
   | ---- get_overlay {id} --------------> |
   | <--- overlay {overlay} -------------- |
   |                ...                    |
   | <--- overlay_changed {overlay} ------ |   bei jeder Änderung, an alle Fenster
   | <--- overlay_removed {id} ----------- |   an alle Fenster
   | <--- overlay_list ------------------- |   nach Änderungen, an alle Fenster
   |                ...                    |
   | ---- update_source {request_id 7} --> |   Änderung (P08)
   | <--- overlay_changed {overlay} ------ |   an alle Fenster, auch an dieses
   | <--- change_result {request_id 7} --- |   nur an dieses Fenster, zuletzt
   |                ...                    |
   | ---- add_media {request_id 11} -----> |   seit Version 10 (P10b)
   | <--- import_progress ---------------- |   an alle, höchstens alle 200 ms
   | <--- media_list --------------------- |   an alle, wenn fertig
   | <--- change_result {request_id 11} -- |   nur an dieses Fenster
   | <--- import_progress {imports []} --- |   an alle, keine Übernahme mehr
   |                ...                    |
   | ---- get_fonts ---------------------> |
   | <--- font_list ---------------------- |
   |                ...                    |
   | ---- get_settings ------------------> |   seit Version 5 (P10-vor)
   | <--- settings ----------------------- |
   | ---- update_settings {request_id 9} > |
   | <--- settings ----------------------- |   an alle Fenster, wenn geändert
   | <--- change_result {request_id 9} --- |   nur an dieses Fenster, zuletzt
   |                ...                    |
   | ---- get_browser_source {id} -------> |   seit Version 7 (P10)
   | <--- browser_source {url} ----------- |   nur an dieses Fenster, auch später von selbst
   |                ...                    |
   | ---- obs_create_source {request_id 12}> |   seit Version 11 (P10c)
   | <--- obs_sources -------------------- |   an alle
   | <--- change_result {request_id 12} -- |   nur an dieses Fenster, Antwort kann dauern
   |                ...                    |
   | ---- quit_request ------------------> |   normales Schliessen des Fensters
   | <--- quit_reply {accepted} ---------- |
   | ---- trennt -------------------------X|
```

1. Beide Seiten schicken ihr `hello` sofort nach dem Verbinden, ohne auf die andere zu warten.
2. Das Fenster gilt als verbunden, sobald das `hello` des Kerns mit `protocol_version` = 11 da ist. Bei einer anderen Version trennt das Fenster und zeigt «Kern hat Schnittstelle Version X, das Fenster kennt Version 11».
3. Der Kern prüft das `hello` des Fensters. Bei einer anderen Version schickt er `error` mit `protocol_version` und trennt. Andere Nachrichten vor dem `hello` beantwortet er mit `error` `hello_missing` und übergeht sie.
4. Nach dem `hello` schickt der Kern `overlay_list`, `media_list` (seit Version 10; hat sie sich seit der letzten geändert, zum Beispiel weil eine Datei in `media/` fehlt, an alle), `import_progress` (nur während einer Übernahme), `connections`, `server_status`, `obs_status`, `obs_sources` (beide seit Version 11) und, falls schon gemessen, die letzte `stats`.
5. Danach schickt der Kern jede Sekunde `stats`, bei Änderungen `overlay_changed`, `overlay_removed`, `overlay_list`, `media_list` (wenn sich die Medienliste geändert hat, auch durch eine Änderung an einem Overlay: «verwendet in»), `import_progress` und (wenn eine neue Schriftdatei dazukam) `font_list` an **alle** begrüssten Verbindungen.
6. Unbekannte Typen: Der Kern antwortet mit `error` `unknown_type`. Das Fenster übergeht unbekannte Typen (Protokollzeile).
7. Verbindung getrennt: Der Kern vergisst die Verbindung. Das Fenster leert alle Daten vom Kern (Docks leer, Werte «–», Verbindungsknöpfe grau), zeigt «Kern-Dienst: nicht verbunden» und versucht es alle 2 s wieder.

### Verteilen von Änderungen (K-02)

Bei jeder Änderung schickt der Kern das **ganze** geänderte Overlay (`overlay_changed`) an alle Fenster, auch an solche, die es nie angefragt haben. Eine Änderung entsteht durch eine Änderungsnachricht eines Fensters (Abschnitt «Änderungen») oder wenn der Kern den Datenordner neu lädt (SIGHUP, zum Beispiel `systemctl --user reload dss-core.service`). In beiden Fällen vergleicht er jedes Overlay und die Liste mit dem Stand vorher und schickt in dieser Reihenfolge

- `overlay_changed` für jedes neue oder geänderte Overlay,
- `overlay_removed` für jedes Overlay, das es nicht mehr gibt,
- einmal `overlay_list`, wenn sich die Liste geändert hat (Reihenfolge, Ordner, Name, Adresse, Auflösung oder Anzahl Quellen eines Overlays).

Vor dem Neuladen speichert der Kern noch nicht gespeicherte Änderungen (Abschnitt «Speichern»). Waren die Schriften schon geladen, lädt er sie danach neu und schickt `font_list` an alle.

### Beenden (Entscheid 1 in P07)

- **Normales Schliessen** des Fensters (Datei → Beenden, Schliessknopf, SIGTERM/SIGINT an `dss-app`): Das Fenster schickt `quit_request`, wartet höchstens 1 s auf `quit_reply` und trennt. Der Kern zählt die **anderen** begrüssten Verbindungen mit `program` = `dss-app`. Sind es 0, antwortet er `accepted: true` und beendet sich mit Exit-Code 0 (systemd startet ihn dann nicht neu). Sonst antwortet er `accepted: false` und läuft weiter. Verbindungen anderer Programme (Tests, später Werkzeuge) zählen nicht.
- **Absturz des Fensters** (`kill -9`): Die Verbindung bricht ab, der Kern läuft weiter.
- **Absturz des Kerns**: systemd startet ihn nach 1 s neu (`Restart=on-failure`, `RestartSec=1s`); die Fenster verbinden sich von selbst wieder.

## Nachrichten Kern → Fenster

### `hello`

| Feld | Typ | Inhalt |
|---|---|---|
| `type` | Text | `hello` |
| `program` | Text | `dss-core` |
| `version` | Text | Version des Kerns, zum Beispiel `0.1.0` (aus `CMakeLists.txt`) |
| `protocol_version` | ganze Zahl ab 1 | Version der Schnittstelle, heute `11` |
| `data_dir` | Text, nicht immer da | seit Version 3 nur vom Kern: Datenordner als absoluter Pfad. Das Fenster liest dort die Dateien der Quellen (`media/…`) für den Zeichner der Leinwand; es schreibt nie hinein (K-03) |

```json
{"data_dir":"/home/dominik/.local/share/dons-streaming-studio","program":"dss-core","protocol_version":11,"type":"hello","version":"0.1.0"}
```

### `overlay_list`

Liste aller geladenen Overlays **in der Reihenfolge der Liste** mit Ordnern (eine Ebene), wie sie in `<Datenordner>/liste.json` steht (`docs/06-overlay-format.md`, Abschnitt «Liste»). Geändert in Version 2 (vorher flache Liste nach Name).

| Feld | Typ | Inhalt |
|---|---|---|
| `entries` | Liste | Einträge der obersten Ebene, 0 oder mehr |
| `entries[].kind` | Text | `overlay` oder `folder` |
| `entries[].overlay` | Objekt | bei `overlay`: Eintrag eines Overlays (unten) |
| `entries[].id` | Text | bei `folder`: Kennung des Ordners |
| `entries[].name` | Text | bei `folder`: Name des Ordners |
| `entries[].expanded` | true/false | bei `folder`: aufgeklappt |
| `entries[].overlays` | Liste | bei `folder`: Einträge der Overlays im Ordner, 0 oder mehr |
| `list_error` | Text oder `null` | Meldung, wenn `liste.json` nicht gelesen werden konnte; die Liste steht dann nach Name, Änderungen an Reihenfolge und Ordnern werden abgewiesen (`rejected`), bis die Datei von Hand repariert ist. Anlegen, Kopieren und Löschen von Overlays gehen weiter |

Eintrag eines Overlays:

| Feld | Typ | Inhalt |
|---|---|---|
| `id` | Text | Kennung |
| `name` | Text | Name |
| `address` | Text | Adresse der Browserquelle |
| `width`, `height` | ganze Zahl | Auflösung (1 bis 7680 bzw. 1 bis 4320) |
| `source_count` | ganze Zahl ab 0 | Anzahl Quellen |
| `output` | true/false | seit Version 6 (P10, E-24): Ausgabe ein (Browserquelle zeigt das Overlay) oder aus (leer, durchsichtig) |

Jedes Overlay steht genau einmal in der Liste; jede Kennung (Overlay und Ordner) kommt nur einmal vor. Sonst ist die Nachricht ungültig.

```json
{"entries":[{"expanded":true,"id":"70fcf543-5e55-4028-ace3-1ddfed919ce5","kind":"folder","name":"Szenen","overlays":[{"address":"ueberblick-start","height":1080,"id":"9adf79a8-1420-4d18-94b1-d780afba49d8","name":"Überblick Start","output":true,"source_count":0,"width":1920}]}],"list_error":null,"type":"overlay_list"}
```

### `overlay` und `overlay_changed`

Ein ganzes Overlay. `overlay` ist die Antwort auf `get_overlay`, `overlay_changed` geht bei einer Änderung an alle.

| Feld | Typ | Inhalt |
|---|---|---|
| `overlay` | Objekt | Overlay **im Format der Overlay-Datei** (`docs/06-overlay-format.md`), mit allen Feldern einschliesslich `format_version`; gelesen wird es genauso streng wie die Datei. **Ohne Token.** |
| `media_sizes` | Objekt | seit Version 4 (P09b), Pflicht, darf leer sein: Originalgrösse der Bild- und Videodateien des Overlays, Schlüssel = Verweis `media/…`, Wert = `{"width": …, "height": …}` (ganze Pixel ab 1). Bild: so, wie es angezeigt wird (mit EXIF-Ausrichtung, 5 bis 8 vertauscht), Video: Grösse des ersten Bildes. Fehlt eine Datei darin, ist ihre Grösse nicht lesbar (oder der Kern ist ohne `DSS_MEDIA_SIZE` gebaut). Das Fenster braucht sie für das Seitenverhältnis an den Ecken, «Transformation zurücksetzen» und «Auf Leinwand einpassen» (P09b, Entscheide 3 und 5). Der Kern liest jede Datei einmal (die Dateien in `media/` sind nach Inhalt benannt und ändern sich nicht) |

```json
{"media_sizes":{"media/3bb2f4e86362dd6d44bfa852d36109788d1854c38cded41ce74f3758cecb099e.png":{"height":256,"width":256}},"overlay":{"address":"test-eins","format_version":3,"height":720,"id":"7be08f35-6885-4a95-9c32-939c45a688ea","name":"Test Eins","sources":[{"flip_horizontal":true,"flip_vertical":false,"height":720,"id":"0c1d2b7e-8a43-4c1e-9d47-1b5e6f3a2c90","image":{"file":"media/3bb2f4e86362dd6d44bfa852d36109788d1854c38cded41ce74f3758cecb099e.png"},"kind":"image","name":"bild1.png","rotation":0,"visible":true,"width":1280,"x":0,"y":0}],"width":1280},"type":"overlay"}
```

### `overlay_removed`

| Feld | Typ | Inhalt |
|---|---|---|
| `id` | Text | Kennung des Overlays, das es nicht mehr gibt |

```json
{"id":"7be08f35-6885-4a95-9c32-939c45a688ea","type":"overlay_removed"}
```

### `change_result`

Antwort auf eine Änderung, **nur** an das Fenster, das sie geschickt hat, und immer **nach** den Nachrichten an alle, die die Änderung auslöst. Kommt `change_result`, hat das Fenster den neuen Zustand also schon.

| Feld | Typ | Inhalt |
|---|---|---|
| `request_id` | ganze Zahl | `request_id` der Änderung |
| `ok` | true/false | `true`: übernommen |
| `id` | Text oder `null` | Kennung des neu angelegten Overlays, Ordners oder der neuen Quelle (`create_overlay`, `copy_overlay`, `create_folder`, `add_*_source`), sonst `null` |
| `code` | Text oder `null` | bei `ok: false` der Fehlercode (`invalid_message`, `unknown_overlay`, `unknown_source`, `unknown_folder`, `rejected`), sonst `null` |
| `message` | Text oder `null` | bei `ok: false` die Meldung auf Deutsch; das Fenster zeigt sie in der Statuszeile (Antworten auf `update_settings` und `reset_settings` seit P10 nur im Dialog «Einstellungen», Punkt 76). Sonst `null` |
| `warning` | Text oder `null` | seit Version 3: bei `ok: true` ein Hinweis, den das Fenster in der Statuszeile zeigt, zum Beispiel «Grösse von «bild.png» nicht lesbar (…), die Quelle bekommt die Grösse des Overlays 1920x1080.» Sonst `null` |

```json
{"code":null,"id":"9adf79a8-1420-4d18-94b1-d780afba49d8","message":null,"ok":true,"request_id":1,"type":"change_result","warning":null}
{"code":"rejected","id":null,"message":"Drehung 3600 ungültig, erlaubt sind 0 bis 3599 Zehntelgrad.","ok":false,"request_id":6,"type":"change_result","warning":null}
{"code":"invalid_message","id":null,"message":"Feld changes.breite ist unbekannt","ok":false,"request_id":7,"type":"change_result","warning":null}
```

### `browser_source` (seit Version 7, P10)

Adresse der Browserquelle des Overlays, nach dem das Fenster mit `get_browser_source` gefragt hat, **nur an diese Verbindung**. Der Kern schickt sie danach von selbst neu, wenn sie sich ändert (neuer Token, neuer Name mit neuer Adresse, neuer Port, neue Adresse im Heimnetz, Overlay gelöscht), auch nach dem Wiederverbinden (der `CoreClient` fragt nach `hello` erneut). Genau eines von `url` und `problem` ist gesetzt.

| Feld | Typ | Inhalt |
|---|---|---|
| `overlay_id` | Text | Kennung des Overlays |
| `url` | Text oder `null` | `http://<Adresse im Heimnetz>:<Port>/o/<adresse>/<token>/` (Abschnitt «Overlay-Server») |
| `problem` | Text oder `null` | warum es keine Adresse gibt: «Kein Overlay mit dieser Kennung.», «Das Overlay hat keinen Token. «Neuer Token» erzeugt einen.», «Der Kern hat keine Einstellungen.», «Keine Adresse im Heimnetz eingestellt (Dialog «Einstellungen»).» |

```json
{"overlay_id":"58de1c10-461d-43b7-bbeb-80532319b618","problem":null,"type":"browser_source","url":"http://192.168.1.9:24901/o/test-szene/<token>/"}
```

(aufgezeichnet am 2026-11-04 mit einem Kern mit eigenem Datenordner und Port 24901; Token hier ersetzt.)

### `server_status` (seit Version 8, P10)

Zustand des Overlay-Servers, nach dem `hello` und bei jeder Änderung an **alle** begrüssten Verbindungen.

| Feld | Typ | Inhalt |
|---|---|---|
| `enabled` | true/false | `false`, wenn der Kern ohne Server gebaut ist (`DSS_CORE_OVERLAY_SERVER=OFF`) |
| `listening` | Liste von Text | wo der Server lauscht, zum Beispiel `127.0.0.1:8081` |
| `problems` | Liste von Text | Meldungen auf Deutsch: Port belegt (mit dem Programm, Abschnitt «Overlay-Server»), Adresse im Heimnetz fehlt oder ist gerade nicht vorhanden, Lauschen fehlgeschlagen |
| `invalid_use` | Text oder `null` | Hinweis, dass eine Browserquelle eine ungültige Adresse benutzt (30 s lang nach der letzten solchen Anfrage), ohne Token: «Eine Browserquelle (192.168.1.116) benutzt eine ungültige Adresse (/o/szene/…). In OBS die Adresse aus «Kopieren» im Dock «Steuerung» eintragen.» |
| `pages` | Objekt | verbundene Seiten je Overlay: Schlüssel Kennung, Wert Anzahl (nur Overlays mit mindestens einer) |

```json
{"enabled":true,"invalid_use":null,"listening":["127.0.0.1:24901","192.168.1.9:24901"],"pages":{},"problems":[],"type":"server_status"}
```

### `media_list` (seit Version 10, P10b)

Medienliste (E-28): alle Dateien in `media/` mit Namen und Ordner aus `medien.json` (`docs/06-overlay-format.md`, «Medienliste»), nach `hello`, als Antwort auf `get_media_list` und nach jeder Änderung, die sie ändert, an **alle**. Der Kern vergleicht mit der zuletzt geschickten und schickt sie nur, wenn sie anders ist. Code: `src/common/medialibrary.h/.cpp`, `CoreService::mediaListData`.

| Feld | Typ | Inhalt |
|---|---|---|
| `folders` | Liste | Ordner in der Reihenfolge des Anlegens (das Dock sortiert selbst) |
| `folders[].id` | Text | Kennung (UUID) |
| `folders[].name` | Text | Name, nicht leer |
| `folders[].parent` | Text oder `null` | Elternordner; `null` = oberste Ebene. Ordner sind beliebig tief |
| `folders[].expanded` | true/false | im Dock aufgeklappt (gespeichert) |
| `files` | Liste | Dateien in der Reihenfolge des Übernehmens |
| `files[].file` | Text | Verweis `media/<SHA-256>.<Endung>` |
| `files[].name` | Text | Name im Dock: ursprünglicher Dateiname oder Ersatzname «Bild vom 2026-11-02» (Datei ohne Namen, zum Beispiel aus P10) |
| `files[].folder` | Text oder `null` | Ordner; `null` = oberste Ebene |
| `files[].kind` | Text | `image`, `video` oder `font` (aus der Endung) |
| `files[].bytes` | Zahl | Grösse der Datei in Byte |
| `files[].codec` | Text oder `null` | nur Video: Name des Formats in FFmpeg (`vp9`, `h264`, `hevc`, `mpeg4`, `prores`, …); `null` bei Bild, Schrift oder nicht lesbar |
| `files[].browser_problem` | Text oder `null` | Hinweis, wenn die Browserquelle das Video nicht abspielt (HEVC, MPEG-4 Part 2, ProRes; Punkt 81, `src/common/videoformat.h`): «zaehler.mov (ProRes) wird in der Browserquelle nicht angezeigt. In H.264 oder VP9 umwandeln.» |
| `files[].used_in` | Liste | Verwendungen: `overlay_id`, `overlay_name`, `source_id`, `source_name` (Bild- und Videoquellen über die Datei, Textquellen über ihre Schrift). Leer = ungenutzt, darf gelöscht werden |
| `error` | Text oder `null` | `medien.json` ungültig: Meldung; die Dateien stehen trotzdem in der Liste (mit Ersatznamen), Umbenennen, Verschieben und Ordner werden abgewiesen (`rejected`), bis die Datei repariert ist. Sonst `null` |

```json
{"error":null,"files":[{"browser_problem":null,"bytes":19761,"codec":null,"file":"media/3bb2f4e86362dd6d44bfa852d36109788d1854c38cded41ce74f3758cecb099e.png","folder":null,"kind":"image","name":"Bild vom 2026-11-02","used_in":[{"overlay_id":"6f845017-9f86-4b8b-bc92-7fa1801c3165","overlay_name":"Neues Overlay","source_id":"7a296900-e03e-4e28-ab2b-f8231933b9ae","source_name":"bild1.png"}]},{"browser_problem":null,"bytes":2017015,"codec":"vp9","file":"media/09445b269e30ac5f3eddf936bc7a19a960ea82dbcd9ebfcc17ff5acace4c7bdf.webm","folder":null,"kind":"video","name":"Video vom 2026-11-03","used_in":[]}],"folders":[{"expanded":true,"id":"a7f7b219-21e0-4964-9737-9ee50c64416e","name":"Logos","parent":null}],"type":"media_list"}
```

(aufgezeichnet am 2026-11-04 mit einem Kern auf einer Kopie der echten Daten, gekürzt auf 2 von 10 Dateien)

### `import_progress` (seit Version 10, P10b)

Stand aller laufenden Übernahmen (Punkt 73, Abschnitt «Übernehmen von Dateien»), an **alle**: beim Beginn, danach höchstens alle 200 ms, solange sich ein Wert ändert, und einmal mit leerer Liste, wenn die letzte fertig oder abgebrochen ist. Eine neue Verbindung bekommt sie nach `hello`, wenn gerade etwas übernommen wird.

| Feld | Typ | Inhalt |
|---|---|---|
| `imports` | Liste | laufende Übernahmen, nach `import_id`; abgebrochene fehlen sofort |
| `imports[].import_id` | ganze Zahl ab 1 | Nummer für `cancel_import`, vom Kern vergeben |
| `imports[].name` | Text | ursprünglicher Dateiname |
| `imports[].folder` | Text oder `null` | Ordner der Medienliste (nur `add_media`); `null` = oberste Ebene oder Übernahme für eine Quelle |
| `imports[].percent` | ganze Zahl 0 bis 100 | kopierter Anteil |

```json
{"imports":[{"folder":null,"import_id":2,"name":"gross-1024mib.png","percent":37}],"type":"import_progress"}
{"imports":[],"type":"import_progress"}
```

(aufgezeichnet am 2026-11-04 mit einer Datei von 1 GiB; zwischen den beiden Zeilen kamen die Werte 0 bis 99 in Schritten von 1 bis 2)

### `font_list`

Auswahl der Schriften für Textquellen (P08, Teil F), Antwort auf `get_fonts`. Der Kern lädt die Liste beim ersten `get_fonts` (ohne zu blockieren: `fc-list`, dann `fc-query` für die mitgelieferte Be Vietnam Pro und die Schriftdateien im Datenordner; auf dem Studio-PC 13 ms) und behält sie. Kommt durch eine Änderung eine neue Schriftdatei in den Datenordner, schickt er `font_list` an **alle**.

| Feld | Typ | Inhalt |
|---|---|---|
| `faces` | Liste | Schnitte, sortiert: Mitgelieferte zuerst, dann nach Familie (ohne Gross-/Kleinschreibung), Gewicht, kursiv |
| `faces[].family` | Text | Familie, zum Beispiel `DejaVu Sans` |
| `faces[].style` | Text | Name des Schnitts laut Datei, zum Beispiel `Bold Oblique` |
| `faces[].weight` | ganze Zahl 100 bis 900 | Gewicht wie in der Overlay-Datei |
| `faces[].italic` | true/false | kursiv oder schräg |
| `faces[].file` | Text | `builtin` und `system`: absoluter Pfad auf dem Studio-PC; `media`: Verweis `media/<SHA-256>.<Endung>` |
| `faces[].origin` | Text | `builtin` (Be Vietnam Pro im Repo), `system` (installiert), `media` (Kopie im Datenordner) |
| `error` | Text oder `null` | Meldung, wenn ein Teil fehlt (zum Beispiel `fc-list` nicht ausführbar); die übrigen Schnitte sind trotzdem da |

```json
{"error":null,"faces":[{"family":"Be Vietnam Pro","file":"/home/dominik/DoNs_Streaming_Studio/src/common/schriften/be-vietnam-pro/BeVietnamPro-Thin.ttf","italic":false,"origin":"builtin","style":"Thin","weight":100},{"family":"DejaVu Sans","file":"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf","italic":false,"origin":"system","style":"Bold","weight":700},{"family":"DejaVu Sans","file":"media/a4c5bc453ca281d90ea079e596da7ae0dfeb5777497c29ec254e76d97ff6f890.ttf","italic":false,"origin":"media","style":"Bold","weight":700}],"type":"font_list"}
```

(gekürzt: auf dem Studio-PC 133 Schnitte am 2026-11-03)

### `stats`

Leistungswerte des Studio-PCs, einmal pro Sekunde (Teil E). `null` heisst «nicht bekannt»; das Fenster zeigt dann «–».

| Feld | Typ | Inhalt |
|---|---|---|
| `cpu_percent` | Zahl 0 bis 100 oder `null` | CPU-Auslastung aller Kerne seit der letzten Messung. `null` bei der ersten Messung |
| `ram_used_kib` | ganze Zahl oder `null` | belegt = `MemTotal` − `MemAvailable`, in KiB |
| `ram_total_kib` | ganze Zahl oder `null` | `MemTotal` in KiB |
| `lan` | Objekt oder `null` | `null` ohne Standardroute (zum Beispiel Kabel gezogen), bei der ersten Messung und wenn die Karte der Standardroute gewechselt hat |
| `lan.interface` | Text | Netzwerkkarte der Standardroute, zum Beispiel `enp3s0` |
| `lan.rx_kbit_s` | Zahl ab 0 | empfangen, kbit/s (1 kbit = 1000 bit) |
| `lan.tx_kbit_s` | Zahl ab 0 | gesendet, kbit/s |

```json
{"cpu_percent":2.5,"lan":{"interface":"enp3s0","rx_kbit_s":1.11,"tx_kbit_s":0.42},"ram_total_kib":32809248,"ram_used_kib":3692468,"type":"stats"}
{"cpu_percent":null,"lan":null,"ram_total_kib":32809248,"ram_used_kib":3694676,"type":"stats"}
```

Herkunft der Werte (proc(5)):

| Wert | Datei | Rechnung |
|---|---|---|
| CPU | `/proc/stat`, Zeile `cpu` | belegt = user + nice + system + irq + softirq + steal; gesamt = belegt + idle + iowait; Auslastung = Δbelegt / Δgesamt × 100. `guest` und `guest_nice` werden nicht gezählt, weil der Kernel sie schon in `user` bzw. `nice` einrechnet (Annahme, in proc_stat(5) nicht beschrieben) |
| RAM | `/proc/meminfo` | `MemTotal`, `MemAvailable` (Einheit kB = KiB) |
| Karte der Standardroute | `/proc/net/route` | Zeile mit `Destination` = `00000000`, `Mask` = `00000000` und Flag `RTF_UP` (0x0001); bei mehreren die kleinste `Metric` |
| LAN | `/proc/net/dev` | erste Zahl nach `Name:` = empfangene Bytes, neunte Zahl = gesendete Bytes; kbit/s = ΔBytes × 8 / 1000 / Δt |

Quellen: https://man7.org/linux/man-pages/man5/proc_stat.5.html, https://man7.org/linux/man-pages/man5/proc_meminfo.5.html, https://man7.org/linux/man-pages/man5/proc_pid_net.5.html (`/proc/net` ist seit Linux 2.6.25 ein Verweis auf `/proc/self/net`), `/usr/include/linux/route.h` (Paket `linux-libc-dev` 6.12.113-1) für `RTF_UP`.

### `connections`

Zustand der vier Verbindungen im Kopfbereich. Seit P10c zeigt `obs` den echten Zustand (`error` auch während der Wiederholungen, `connecting` nur beim ersten Versuch nach dem Start oder einer Änderung der Einstellungen, Entscheid F9); die anderen drei bleiben `off`, weil es sie noch nicht gibt. Werte wie die Eigenschaft `state` der Knöpfe: `off` (grau), `error` (rot), `connecting` (orange), `connected` (grün), siehe `docs/01-festlegungen.md`, «Verbindungsanzeige im Kopfbereich».

| Feld | Typ | Inhalt |
|---|---|---|
| `twitch`, `youtube`, `obs`, `ndi` | Text | Zustand, alle vier Pflicht |

```json
{"ndi":"off","obs":"off","twitch":"off","type":"connections","youtube":"off"}
```

### `obs_status` (seit Version 11, P10c)

Einzelheiten der Verbindung zu OBS. Die Farbe des Knopfes steht weiter in `connections`. Geht nach `hello` an die neue Verbindung und bei jeder Änderung an alle.

| Feld | Typ | Inhalt |
|---|---|---|
| `message` | Text oder `null` | Meldung zum Zustand, zum Beispiel «OBS hat das Passwort abgelehnt. Im Dialog «Einstellungen» neu eingeben.» |
| `obs_version`, `websocket_version` | Text oder `null` | Versionen, sobald verbunden |
| `current_scene` | Objekt (`uuid`, `name`) oder `null` | aktuelle Szene (`CurrentProgramSceneChanged`), nur verbunden |
| `scenes` | Liste von Objekten (`uuid`, `name`) | alle Szenen, nur verbunden, sonst leer |

```json
{"current_scene":{"name":"Live","uuid":"scene-live"},"message":null,"obs_version":"33.0.1","scenes":[{"name":"Live","uuid":"scene-live"},{"name":"Pause","uuid":"scene-pause"}],"type":"obs_status","websocket_version":"5.7.6"}
```

Das Passwort steht in keiner Nachricht an das Fenster, nur `obs_password_set` in `settings`.

### `obs_sources` (seit Version 11, P10c)

Stand der Quellen in OBS. Geht nach `hello` an die neue Verbindung und bei jeder Änderung an alle. Die Liste enthält nur Overlays mit Zuordnung; ein Overlay ohne Eintrag hat keine Quelle in OBS. Die Adresse mit Token steht nicht darin.

| Feld | Typ | Inhalt |
|---|---|---|
| `sources[].overlay_id` | Text | Kennung des Overlays |
| `sources[].state` | Text | `present` (Quelle da, oder zuletzt da und OBS gerade nicht verbunden), `missing` (in OBS nicht mehr gefunden, Zuordnung bleibt, Entscheid F7), `none` |
| `sources[].input_name` | Text | Name der Quelle in OBS, zuletzt bekannt |

```json
{"sources":[{"input_name":"Handtest P10c","overlay_id":"db7f47a9-dee3-4de5-b056-40b5b3507607","state":"present"}],"type":"obs_sources"}
```

### `quit_reply`

| Feld | Typ | Inhalt |
|---|---|---|
| `accepted` | true/false | `true`: Der Kern beendet sich jetzt |
| `other_windows` | ganze Zahl ab 0 | Anzahl anderer verbundener Fenster (`program` = `dss-app`) |

```json
{"accepted":false,"other_windows":1,"type":"quit_reply"}
```

### `error`

| Feld | Typ | Inhalt |
|---|---|---|
| `code` | Text | Fehlercode, siehe unten |
| `message` | Text | Beschreibung auf Deutsch |
| `request` | Text oder `null` | Typ der Anfrage, auf die sich der Fehler bezieht; `null`, wenn keiner bekannt ist |

```json
{"code":"unknown_overlay","message":"Kein Overlay mit der Kennung 00000000-0000-4000-8000-000000000000","request":"get_overlay","type":"error"}
```

## Nachrichten Fenster → Kern

| Typ | Felder | Antwort des Kerns |
|---|---|---|
| `hello` | `program` (Text, Fenster: `dss-app`, Werkzeug: `dss-cli`), `version` (Text), `protocol_version` (ganze Zahl, `11`) | `overlay_list`, `media_list`, `import_progress` (nur während einer Übernahme), `connections`, `server_status`, `obs_status`, `obs_sources`, `stats` |
| `get_overlay_list` | – | `overlay_list` |
| `get_overlay` | `id` (Text, Kennung) | `overlay` oder `error` `unknown_overlay` |
| `get_fonts` | – | `font_list` |
| `get_media_list` | – (seit Version 10) | `media_list` |
| `quit_request` | – | `quit_reply` |
| `get_settings` | – (seit Version 5) | `settings` |
| `update_settings` | siehe Abschnitt «Einstellungen» (seit Version 5) | `settings` an alle (wenn geändert), dann `change_result` |
| `reset_settings` | `request_id` (seit Version 9, Abschnitt «Einstellungen») | `settings` an alle, dann `change_result` |
| `get_browser_source` | `overlay_id` (Text oder `null` = keine Adresse mehr schicken; seit Version 7) | `browser_source` an diese Verbindung, danach bei jeder Änderung von selbst |
| `obs_create_source` | `request_id`, `overlay_id`, `scene_uuid`, `name` (seit Version 11) | legt eine Browserquelle in OBS an und fügt sie in die Szene ein: `obs_sources` an alle, dann `change_result`. Die Antwort kommt später, OBS antwortet über das Netz |
| `obs_add_to_scene` | `request_id`, `overlay_id`, `scene_uuid` (seit Version 11) | fügt die vorhandene Quelle in eine weitere Szene ein, dann `change_result` |
| Änderungen | siehe Abschnitt «Änderungen» | Nachrichten an alle, dann `change_result` |

```json
{"program":"dss-app","protocol_version":11,"type":"hello","version":"0.1.0"}
{"type":"get_fonts"}
{"type":"get_overlay_list"}
{"id":"db7f47a9-dee3-4de5-b056-40b5b3507607","type":"get_overlay"}
{"type":"quit_request"}
{"type":"get_settings"}
{"type":"get_media_list"}
{"type":"get_browser_source","overlay_id":"58de1c10-461d-43b7-bbeb-80532319b618"}
```

## Änderungen (P08)

Jede Änderung ist eine eigene Nachricht mit dem Feld `request_id` (ganze Zahl 0 bis 2^53, vom Fenster gewählt, kommt in `change_result` zurück). Der Kern prüft sie mit dem Modell (dieselben Prüfungen wie beim Lesen der Overlay-Datei, `docs/06-overlay-format.md`), übernimmt sie und schickt den neuen Zustand an alle Fenster (Abschnitt «Verteilen von Änderungen»), danach `change_result` an das Fenster, das sie geschickt hat. Eine abgewiesene Änderung ändert nichts; der Kern schreibt sie als Warnung ins Protokoll.

Das Fenster übernimmt den Zustand **nur** aus `overlay_changed`, `overlay_removed` und `overlay_list`, nie aus der eigenen Änderung. So sehen alle Fenster immer dasselbe.

Kennungen: `overlay_id`, `source_id`, `folder_id` sind Text (UUID). `folder_id` meint bei den Änderungen der Overlay-Liste einen Ordner aus `liste.json`, bei den Änderungen der Medien (seit Version 10) einen Ordner aus `medien.json`. `file` ist ein absoluter Pfad auf dem Studio-PC (Fenster und Kern laufen auf demselben Rechner); bei `set_text_font`, seit Version 10 auch bei `add_image_source` und `add_video_source`, ein Verweis `media/…` auf eine Kopie im Datenordner (dann wird nichts kopiert); bei `rename_media`, `move_media` und `remove_media` immer ein Verweis. Der Kern kopiert jede Datei nach `media/<SHA-256>.<Endung>`; gleicher Inhalt gibt eine Kopie (K-03).

| Typ | Felder | Wirkung | `id` in `change_result` |
|---|---|---|---|
| `create_overlay` | `name`, `width`, `height` (ganze Zahlen) | neues Overlay ohne Quellen, Adresse aus dem Namen, eigener Token; ans Ende der Liste | Overlay |
| `set_overlay_size` | `overlay_id`, `width`, `height` (ganze Zahlen, 1 bis 7680 bzw. 4320) | seit Version 3 (P09): neue Auflösung; die Quellen behalten ihre Pixelwerte | – |
| `rename_overlay` | `overlay_id`, `name`, `change_address` (true/false) | neuer Name; bei `true` auch neue Adresse aus dem Namen (eindeutig gemacht) | – |
| `copy_overlay` | `overlay_id` | Kopie mit neuen Kennungen, Name «<Name> Kopie», eigener Adresse und neuem Token; direkt nach dem Original in der Liste | Overlay |
| `remove_overlay` | `overlay_id` | löscht das Overlay (Datei und Eintrag in der Liste) | – |
| `move_overlay` | `overlay_id`, `folder_id` (Text oder `null` = oberste Ebene), `index` (ganze Zahl ab 0) | setzt das Overlay an die Stelle `index` im Ziel; `index` zählt nach dem Herausnehmen | – |
| `create_folder` | `name` | neuer Ordner (aufgeklappt, leer) am Ende der Liste | Ordner |
| `rename_folder` | `folder_id`, `name` | neuer Name | – |
| `remove_folder` | `folder_id` | löscht den Ordner; seine Overlays kommen an seiner Stelle auf die oberste Ebene | – |
| `move_folder` | `folder_id`, `index` | setzt den Ordner an die Stelle `index` der obersten Ebene | – |
| `set_folder_expanded` | `folder_id`, `expanded` (true/false) | auf- oder zugeklappt (wird gespeichert) | – |
| `add_image_source` | `overlay_id`, `file`; seit Version 10 wahlweise `center_x`, `center_y` (ganze Zahlen, beide oder keines) und `insert_index` (ganze Zahl ab 0), je fehlend oder `null` | neue Bildquelle in der Grösse der Datei (seit P09; nicht lesbar: Grösse des Overlays und `warning`). Ohne die neuen Felder zuoberst an Lage 0/0. Mit `center_x`/`center_y` liegt ihre Mitte dort (Pixel des Overlays; `x` = `center_x` − Breite/2, ganzzahlig abgerundet), mit `insert_index` steht sie an dieser Stelle der Zeichenreihenfolge (wie `move_source`). Name der Quelle: ursprünglicher Dateiname, bei einem Verweis der Name in der Medienliste. Die Fenster bekommen ein `overlay_changed` mit dem Endstand. Gelingt Lage oder Stelle nicht, bleibt die Quelle trotzdem, und `warning` nennt den Grund («Quelle an Lage 0/0 hinzugefügt: …») | Quelle |
| `add_video_source` | wie `add_image_source` | neue Videoquelle wie oben (Grösse des ersten Bildes), «Wiederholen» und «Am Ende anhalten» aus. Seit Version 10 bei HEVC, MPEG-4 Part 2 und ProRes mit `warning` (Text wie `browser_problem`) | Quelle |
| `add_text_source` | `overlay_id` | neue Textquelle wie oben, Inhalt «Text», Schrift Be Vietnam Pro Regular | Quelle |
| `remove_source` | `overlay_id`, `source_id` | entfernt die Quelle (die Kopie im Datenordner bleibt) | – |
| `move_source` | `overlay_id`, `source_id`, `index` | Stelle in der Zeichenreihenfolge, 0 = ganz hinten | – |
| `update_source` | `overlay_id`, `source_id`, `changes` (Objekt, unten) | ändert die angegebenen Eigenschaften, alle zusammen oder keine | – |
| `set_text_font` | `overlay_id`, `source_id`, `file`, `weight` (100 bis 900 oder `null`), `italic` (true/false oder `null`) | Schriftdatei, Gewicht und kursiv zusammen; `null` = aus der Datei lesen (`fc-query`, für «Datei wählen …») | – |
| `replace_image_file` | `overlay_id`, `source_id`, `file` | neues Bild für eine Bildquelle; Lage und Grösse bleiben | – |
| `set_overlay_output` | `overlay_id`, `output` (true/false) | seit Version 6 (P10, E-24): Ausgabe ein/aus, gespeichert in der Overlay-Datei (`output`); aus = Browserquelle leer und durchsichtig, bleibt verbunden; der Editor bleibt bearbeitbar | – |
| `new_token` | `overlay_id` | seit Version 7 (P10, Teil A 8): neuer Token; die alte Adresse gilt sofort nicht mehr (offene Seiten bekommen `gone` und laden die leere Seite), die neue kommt mit `browser_source`. Protokoll ohne Token: «Neuer Token für Overlay «Name» (adresse): die alte Adresse gilt nicht mehr» | – |
| `add_media` | `file` (Pfad), `folder_id` (Text oder `null`) | seit Version 10: übernimmt die Datei in die Medienliste (Ordner der Medienliste), ohne Quelle. Name = ursprünglicher Dateiname. Ist der Inhalt schon da, bleiben Name und Ordner, `warning` «x.png ist schon in der Medienliste (als «Logo»), Name und Ordner bleiben.». Erlaubt: Endungen der Bilder, Videos und Schriften (`docs/06-overlay-format.md`, «media/»); bei HEVC, MPEG-4 Part 2, ProRes `warning` wie `browser_problem` | – |
| `rename_media` | `file` (Verweis), `name` | neuer Name in der Medienliste, nicht leer; die Datei und die Namen der Quellen bleiben | – |
| `move_media` | `file`, `folder_id` (Text oder `null`) | in einen anderen Ordner der Medienliste | – |
| `remove_media` | `file` | löscht die Datei in `media/` und ihren Eintrag; nur wenn keine Quelle sie nutzt, sonst `in_use` mit «verwendet in» | – |
| `create_media_folder` | `name`, `parent_id` (Text oder `null`) | neuer Ordner, aufgeklappt | Ordner |
| `rename_media_folder` | `folder_id`, `name` | neuer Name | – |
| `remove_media_folder` | `folder_id` | löscht den Ordner; Dateien und Unterordner kommen eine Ebene höher (in den Elternordner), keine Datei wird gelöscht | – |
| `move_media_folder` | `folder_id`, `parent_id` (Text oder `null`) | in einen anderen Ordner; nicht in sich selbst oder einen eigenen Unterordner (`rejected`) | – |
| `set_media_folder_expanded` | `folder_id`, `expanded` (true/false) | auf- oder zugeklappt (gespeichert) | – |
| `cancel_import` | `import_id` (aus `import_progress`) | bricht die Übernahme ab; deren Änderung bekommt `change_result` `canceled`. Unbekannte oder fertige Nummer: `rejected` «Keine laufende Übernahme mit der Nummer 1 (schon fertig?).» | – |

Objekt `changes` (nur angegebene Felder ändern sich, mindestens eines; unbekannte Felder sind ein Fehler):

| Feld | Typ | Inhalt |
|---|---|---|
| `name` | Text | Name der Quelle, nicht leer |
| `x`, `y` | ganze Zahl | Lage in Pixeln der Overlay-Auflösung, auch negativ |
| `width`, `height` | ganze Zahl | Grösse in Pixeln, ab 1 |
| `rotation` | ganze Zahl | Drehung in Zehntelgrad, 0 bis 3599 |
| `flip_horizontal`, `flip_vertical` | true/false | seit Version 4 (P09b): Inhalt waagrecht bzw. senkrecht gespiegelt |
| `visible` | true/false | sichtbar |
| `video.loop` | true/false | nur Video: wiederholen |
| `video.hold_at_end` | true/false | nur Video, seit Version 3: am Ende anhalten |
| `text.content` | Text | nur Text: Inhalt, mehrzeilig |
| `text.font_size` | ganze Zahl | nur Text: Schriftgrösse in Pixeln, ab 1 |
| `text.color` | Text | nur Text: `#RRGGBBAA` |
| `text.align` | Text | nur Text: `left`, `center`, `right` |

Grenzen und Meldungen kommen aus dem Modell (`docs/06-overlay-format.md`). Eine Eigenschaft, die es bei der Art der Quelle nicht gibt, wird abgewiesen («Wiederholen gibt es nur bei Quellen der Art Video.»).

```json
{"type":"create_overlay","request_id":1,"name":"Überblick Start","width":1920,"height":1080}
{"type":"create_folder","request_id":2,"name":"Szenen"}
{"type":"move_overlay","request_id":3,"overlay_id":"9adf79a8-1420-4d18-94b1-d780afba49d8","folder_id":"70fcf543-5e55-4028-ace3-1ddfed919ce5","index":0}
{"type":"add_text_source","request_id":4,"overlay_id":"9adf79a8-1420-4d18-94b1-d780afba49d8"}
{"type":"update_source","request_id":5,"overlay_id":"9adf79a8-1420-4d18-94b1-d780afba49d8","source_id":"c520a200-5230-438f-8a5e-72685bc804aa","changes":{"x":40,"y":900,"text":{"content":"Willkommen","font_size":72}}}
{"type":"set_overlay_size","request_id":9,"overlay_id":"9adf79a8-1420-4d18-94b1-d780afba49d8","width":1280,"height":720}
{"type":"update_source","request_id":10,"overlay_id":"9adf79a8-1420-4d18-94b1-d780afba49d8","source_id":"b814d820-5d53-42a8-9683-ef3419cf4d7e","changes":{"video":{"hold_at_end":true}}}
{"type":"set_overlay_output","request_id":1,"overlay_id":"58de1c10-461d-43b7-bbeb-80532319b618","output":false}
{"type":"new_token","request_id":3,"overlay_id":"58de1c10-461d-43b7-bbeb-80532319b618"}
{"type":"set_text_font","request_id":8,"overlay_id":"9adf79a8-1420-4d18-94b1-d780afba49d8","source_id":"c520a200-5230-438f-8a5e-72685bc804aa","file":"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf","weight":700,"italic":false}
```

Seit Version 10 (aufgezeichnet am 2026-11-04 mit einem Kern auf einer Kopie der echten Daten, Pfade gekürzt; Antwort jeweils darunter):

```json
{"type":"create_media_folder","request_id":1,"name":"Logos","parent_id":null}
{"code":null,"id":"a7f7b219-21e0-4964-9737-9ee50c64416e","message":null,"ok":true,"request_id":1,"type":"change_result","warning":null}
{"type":"add_media","request_id":2,"file":"/…/09445b269e30ac5f3eddf936bc7a19a960ea82dbcd9ebfcc17ff5acace4c7bdf.webm","folder_id":"a7f7b219-21e0-4964-9737-9ee50c64416e"}
{"code":null,"id":null,"message":null,"ok":true,"request_id":2,"type":"change_result","warning":"09445b269e30ac5f3eddf936bc7a19a960ea82dbcd9ebfcc17ff5acace4c7bdf.webm ist schon in der Medienliste (als «Video vom 2026-11-03»), Name und Ordner bleiben."}
{"type":"remove_media","request_id":5,"file":"media/3bb2f4e86362dd6d44bfa852d36109788d1854c38cded41ce74f3758cecb099e.png"}
{"code":"in_use","id":null,"message":"«Bild vom 2026-11-02» lässt sich nicht löschen, verwendet in Overlay «Neues Overlay» (Quelle «bild1.png»).","ok":false,"request_id":5,"type":"change_result","warning":null}
{"type":"add_image_source","request_id":6,"overlay_id":"6f845017-9f86-4b8b-bc92-7fa1801c3165","file":"media/3bb2f4e86362dd6d44bfa852d36109788d1854c38cded41ce74f3758cecb099e.png","center_x":960,"center_y":540,"insert_index":0}
{"code":null,"id":"0ef8a20d-cd56-46a8-9a2d-45f83ef63920","message":null,"ok":true,"request_id":6,"type":"change_result","warning":null}
{"type":"add_media","request_id":7,"file":"/…/gross-1024mib.png","folder_id":null}
{"type":"cancel_import","request_id":8,"import_id":1}
{"code":null,"id":null,"message":null,"ok":true,"request_id":8,"type":"change_result","warning":null}
{"code":"canceled","id":null,"message":"Übernehmen von gross-1024mib.png abgebrochen.","ok":false,"request_id":7,"type":"change_result","warning":null}
```

Die neue Quelle aus `request_id` 6 (256x256) liegt danach an 832/412 mit dem Namen «Bild vom 2026-11-02» und ist die erste der Zeichenreihenfolge (`overlay_changed` vor `change_result`).

(aufgezeichnet am 2026-11-03 mit einem Kern mit eigenem Datenordner; die Antworten stehen oben unter `change_result`. Die Zeilen mit `request_id` 9 und 10 sind in P09 von Hand ergänzt, Form wie `tst_aenderungen` sie prüft. `set_overlay_output` und `new_token` aufgezeichnet am 2026-11-04, Antwort jeweils `change_result` `ok: true`.)

Ungültige Änderung (Pflichtfeld fehlt, falscher Typ): Hat sie eine gültige `request_id`, antwortet der Kern mit `change_result` `ok: false`, `code: invalid_message`; sonst mit `error` `invalid_message`.

### Übernehmen von Dateien (P10-vor, Punkt 67)

Änderungen mit einer neuen Datei (`add_image_source`, `add_video_source`, `replace_image_file`, `add_text_source` mit Schrift, `set_text_font` mit einem Pfad statt `media/…`) prüft der Kern zuerst (Overlay und Quelle vorhanden). Dann rechnet er SHA-256 und kopiert die Datei in einem **eigenen Thread** (höchstens 2 gleichzeitig, `MediaImporter` in `src/common/mediaimport.h/.cpp`), stückweise zu 1 MiB, mit `QSaveFile`. Die Hauptschleife beantwortet in der Zwischenzeit alle Nachrichten und schickt `stats` weiter. Erst wenn die Kopie fertig ist, ändert der Kern das Overlay (in der Hauptschleife, wie jede Änderung), verteilt es und schickt `change_result`. Gemessen in `tst_uebernehmen` (2026-11-03, 256 MiB): Antwortzeit auf `get_overlay_list` während des Übernehmens höchstens 25 ms.

- Nur der Kern schreibt Dateien; Änderungen an Overlays und `liste.json` bleiben in der Hauptschleife.
- Zwei Änderungen mit derselben Datei gleichzeitig: Die zweite wartet auf die erste und findet dann die Kopie vor (eine Kopie, Protokoll «war schon vorhanden»).
- Wird das Overlay oder die Quelle während des Übernehmens gelöscht, antwortet der Kern mit `change_result` `unknown_overlay` bzw. `unknown_source`; die Kopie bleibt in `media/` (gleicher Inhalt wird später wieder verwendet).
- Beim Beenden (SIGTERM, SIGINT, `quit_request`) bricht der Kern laufende Übernahmen ab; die vorläufige Datei verschwindet (`QSaveFile::cancelWriting`), unter dem endgültigen Namen bleibt nichts. Protokoll: «N Übernahmen abgebrochen (… ms)».
- Seit Version 10 (P10b) auch `add_media`. Jede Übernahme bekommt eine Nummer und erscheint in `import_progress` (Punkt 73). `cancel_import` bricht sie ab (`MediaImporter::cancel`): Die vorläufige Datei verschwindet; war die Kopie schon fertig und neu, löscht der Kern sie, sobald keine Übernahme mehr läuft und keine Quelle sie nutzt. In `media/` bleibt nichts zurück (geprüft in `tst_uebernehmen` und `tst_fensterbearbeiten`). Die Änderung bekommt `change_result` `canceled`, `cancel_import` selbst `ok: true`.
- Nach jeder Übernahme trägt der Kern die Datei mit ihrem ursprünglichen Namen in `medien.json` ein (auch bei `add_image_source` usw.); war der Inhalt schon da, bleiben Name und Ordner.
- Schalter `DSS_CORE_IMPORT_THREAD` (Standard ON). OFF: wie bis P10a in der Hauptschleife, ohne `import_progress` und ohne Abbrechen.

Vorläufige Dateien, die nach einem Absturz übrig bleiben könnten (`<SHA-256>.<Endung>.XXXXXX`), löscht der Kern beim Start nach dem Nehmen der Sperre (Protokoll «Aufräumen media/: N vorläufige Dateien von QSaveFile gelöscht (… Byte)»). Muster und Quellen in `src/common/mediaimport.h`.

### Häufige Änderungen zusammenfassen

Hält der Nutzer eine Pfeiltaste in einem Zahlenfeld gedrückt, entstehen viele Änderungen pro Sekunde. Zwei Stufen verhindern, dass der Kern überlastet wird oder die Datei bei jedem Schritt schreibt:

1. **Fenster** (`CoreClient::sendChange` mit Schlüssel, zum Beispiel Feld und Quelle; beim Ziehen auf der Leinwand seit P09 ein `update_source` mit `x`, `y`, `width`, `height`, `rotation` (seit P09b auch `flip_horizontal`, `flip_vertical`; ebenso die Befehle «Transformieren») unter dem Schlüssel «<Quelle>/geometry», Auflösung unter «<Overlay>/size»): Je Schlüssel ist höchstens eine Änderung unterwegs. Eine neue Änderung, bevor `change_result` da ist, wird zurückgehalten und ersetzt eine schon zurückgehaltene; nach der Antwort geht die letzte los. Gemessen in `tst_bearbeiten` (2026-11-03): 200 Schritte → 3 Änderungen an den Kern, der letzte Wert kommt an.
2. **Kern** (Abschnitt «Speichern»): Jede Änderung gilt sofort im Speicher und geht sofort an alle Fenster; die Datei schreibt er später.

### Speichern

| Punkt | Festlegung |
|---|---|
| Overlay-Dateien | nach 300 ms ohne weitere Änderung, spätestens 1000 ms nach der ersten noch nicht gespeicherten Änderung (`SaveScheduler`); atomar (`QSaveFile`) |
| Sofort geschrieben | Anlegen, Kopieren, Löschen eines Overlays, `liste.json`, `medien.json` (seit P10b), Kopien im Datenordner |
| Beim Beenden | vor dem Ende (`quit_request`, SIGTERM, SIGINT) und vor dem Neuladen (SIGHUP) speichert der Kern alles Offene |
| `kill -9` / Absturz | höchstens etwa die Änderungen der letzten Sekunde gehen verloren |
| Fehler | Kann eine Datei nicht geschrieben werden, schreibt der Kern das ins Protokoll (kritisch), schickt `error` `save_failed` an alle und versucht es mit der nächsten Änderung und beim Beenden wieder. Im Speicher bleibt die Änderung gültig |

## Einstellungen (P10-vor)

Einstellungen des Kerns stehen in `kern.ini` im Ordner der Einstellungen der Version: `~/.config/dons-streaming-studio/kern.ini` (Version im Einsatz), `~/.config/dons-streaming-studio-entwicklung/kern.ini` (Version in Arbeit); anderer Pfad: `dss-core --einstellungen PFAD`. Nur der Kern schreibt die Datei, atomar (`QSaveFile`). Code: `src/common/coresettings.h/.cpp`. Seit P10 wirken beide Werte sofort: Der Overlay-Server startet mit den neuen Werten neu, und die Adresse der Browserquelle ändert sich (`browser_source`).

```ini
[overlay-server]
port=8081
adresse-im-heimnetz=192.168.1.9

[obs]
aktiv=nein
adresse=
port=4455
```

| Wert | Festlegung |
|---|---|
| `port` | Port des Overlay-Servers. Standard 8080 im Einsatz, 8081 in Arbeit (beide Versionen können gleichzeitig laufen). Gültig **1024 bis 32767**: Ports unter 1024 darf unter Linux nur ein Prozess mit besonderen Rechten öffnen (`/proc/sys/net/ipv4/ip_unprivileged_port_start` = 1024); ab 32768 vergibt Linux Ports für ausgehende Verbindungen (`/proc/sys/net/ipv4/ip_local_port_range` = 32768 60999), ein Port dort kann beim Start des Servers zufällig belegt sein. Werte gemessen auf dem Studio-PC am 2026-11-03 mit `cat`. Quelle: ip(7), https://man7.org/linux/man-pages/man7/ip.7.html |
| `adresse-im-heimnetz` | IPv4-Adresse des Studio-PCs, vier Zahlen 0 bis 255 ohne führende Nullen; nicht 0.0.0.0, 127.x.x.x und ab 224.0.0.0. Leer erlaubt. Ist sie leer (beim Start oder später, zum Beispiel ohne Kabel beim Start), prüft der Kern seit P10b (Punkt 85) alle 2 s die Standardroute und trägt, sobald es eine gibt, die Adresse ihrer Netzwerkkarte ein, schreibt die Datei, schickt `settings` an alle und startet den Overlay-Server mit der Adresse neu (`HomeAddressWatcher` in `src/common/coresettings.h`); ein Neustart ist nicht nötig. Eine eingetragene Adresse ermittelt er nie neu. Bei ungültiger Datei trägt er nichts ein. Standardroute: die erste Zeile in `/proc/net/route` mit `Destination` und `Mask` 00000000 und gesetztem Flag `RTF_UP` (0x0001, `/usr/include/linux/route.h`) (wie die LAN-Anzeige, `defaultRouteInterface` in `src/common/systemstats.h`; Quelle: proc_pid_net(5), https://man7.org/linux/man-pages/man5/proc_pid_net.5.html, und route(8)), Adresse: die erste IPv4-Adresse dieser Karte, die nicht 127.x.x.x und nicht 169.254.x.x ist (`QNetworkInterface::interfaceFromName`, https://doc.qt.io/qt-6.8/qnetworkinterface.html). Ohne IPv4-Standardroute bleibt der Wert leer (Warnung im Protokoll), neuer Versuch nach 2 s. Seit P10 (Punkt 78) nimmt `update_settings` nur eine Adresse an, die eine Netzwerkkarte dieses PCs gerade hat (`QNetworkInterface::allAddresses`, IPv4 ohne 127.x.x.x), sonst «192.168.1.1 gehört nicht zu diesem PC. Adressen dieses PCs: 192.168.1.9.»; die bisherige Adresse darf bleiben, auch wenn sie gerade fehlt (Kabel) |
| `[obs] aktiv` | `ja` oder `nein`, Standard `nein`. «Mit OBS verbinden» im Dialog; braucht eine Adresse (P10c) |
| `[obs] adresse` | Adresse von obs-websocket auf dem Streaming-PC: Buchstaben, Ziffern, `.` und `-`, leer erlaubt. Nicht auf Adressen dieses PCs beschränkt, die Tests nutzen `127.0.0.1` |
| `[obs] port` | Port von obs-websocket, 1 bis 65535, Standard 4455 |

Das Passwort von OBS steht nicht in `kern.ini` (Regel 12), sondern verschlüsselt in `obs-passwort.cred` neben ihr, Rechte 0600, je Version getrennt (`src/obs/obssecretstore.h`, `systemd-creds --user`, Entscheid F1). Der Kern ruft `systemd-creds` über `QProcess` mit stdin und stdout auf, das Passwort steht nie in der Befehlszeile. Quelle: https://www.freedesktop.org/software/systemd/man/latest/systemd-creds.html. Grenze: Andere Programme desselben Benutzers könnten es ebenfalls entschlüsseln, und der Host-Schlüssel liegt auf derselben Platte.

Ist die Datei ungültig (unbekannte Angabe oder Abschnitt, doppelte Angabe, ungültiger Wert), gelten die Standardwerte, der Kern schreibt eine Warnung ins Protokoll, überschreibt die Datei nicht und weist `update_settings` ab, bis sie von Hand repariert oder mit `reset_settings` zurückgesetzt ist (unten).

### `settings` (Kern → Fenster)

Antwort auf `get_settings` und nach jeder Änderung an **alle** begrüssten Verbindungen.

| Feld | Typ | Bedeutung |
|---|---|---|
| `port` | ganze Zahl | Port |
| `home_address` | Text | Adresse im Heimnetz, leer = nicht gesetzt |
| `port_min`, `port_max` | ganze Zahlen | gültiger Bereich (1024, 32767); das Fenster stellt sein Zahlenfeld danach ein |
| `file` | Text | Pfad von `kern.ini` |
| `settings_error` | Text oder `null` | Meldung, wenn die Datei ungültig ist |
| `obs_enabled`, `obs_host`, `obs_port` | wahr/falsch, Text, ganze Zahl | Abschnitt `[obs]`, seit Version 11 |
| `obs_password_set` | wahr/falsch | Ein Passwort ist gespeichert. Der Wert selbst geht nie an ein Fenster (Regel 12), seit Version 11 |

```json
{"file":"/home/dominik/.config/dons-streaming-studio-entwicklung/kern.ini","home_address":"192.168.1.9","obs_enabled":false,"obs_host":"","obs_password_set":false,"obs_port":4455,"port":8081,"port_max":32767,"port_min":1024,"settings_error":null,"type":"settings"}
```

### `update_settings` (Fenster → Kern)

Felder: `request_id` (wie bei Änderungen), dazu mindestens eines von `port` (ganze Zahl), `home_address` (Text, leer erlaubt) und seit Version 11 `obs_enabled`, `obs_host`, `obs_port` und `obs_password` (Text: fehlt = behalten, leer = löschen). Nur angegebene Werte ändern sich. Steht `obs_enabled` darin, versucht der Kern die Verbindung zu OBS auch bei gleichen Werten neu: Damit wirkt «OK» nach einem Abbruch durch OBS. Das Passwort wird zuerst verschlüsselt, danach erst `kern.ini` geschrieben; `change_result` kann also später kommen. Der Kern prüft alle Werte; ist einer ungültig, ändert sich nichts und `change_result` hat `ok: false`, `code: rejected` und die Meldung, zum Beispiel «Port 80 ungültig, erlaubt sind 1024 bis 32767.». Sonst schreibt er die Datei, schickt `settings` an alle (auch an den Absender) und dann `change_result` `ok: true`. Ohne Wert oder mit falschem Typ: `change_result` `invalid_message` (bzw. `error`, wenn `request_id` fehlt).

```json
{"type":"update_settings","request_id":9,"port":8090}
{"type":"update_settings","request_id":10,"home_address":"192.168.1.9"}
```

### `reset_settings` (Fenster → Kern, seit Version 9)

Punkt 77 (Entscheid 2026-11-03): Knopf «Auf Standardwerte zurücksetzen» im Dialog «Einstellungen», nur bei ungültiger Datei frei. Feld: `request_id`. Der Kern benennt die ungültige Datei in `kern.ini.ungueltig-<JJJJ-MM-TT_hh-mm-ss>` um (Ortszeit, 24 Stunden), schreibt eine neue mit den Standardwerten, trägt die Adresse im Heimnetz wieder aus der Standardroute ein, schickt `settings` an alle und dann `change_result` `ok: true` mit `warning` «Auf Standardwerte zurückgesetzt. Die ungültige Datei heisst jetzt kern.ini.ungueltig-2026-11-04_00-53-07.». Abgewiesen (`rejected`): Datei gültig («… ist gültig; nichts zurückgesetzt.»), Name schon vergeben, Umbenennen oder Schreiben fehlgeschlagen (dann bleibt alles wie vorher).

```json
{"type":"reset_settings","request_id":4}
```

`dss-cli einstellungen`, `einstellung-port PORT`, `einstellung-adresse ADRESSE` nutzen dieselben Nachrichten (`hello` mit `program` `dss-cli`, `src/common/coresession.h`). Läuft der Kern nicht, zeigt `einstellungen` die Datei an; ändern geht dann nicht (Exit-Code 4). Das Passwort von OBS lässt sich über `dss-cli` nicht setzen.

## Anbindung an OBS (P10c, T-05, E-29)

Code: `src/obs/` (`dss_obs`, ohne Qt Gui, bindet `protocol.h` nicht ein). Protokoll obs-websocket 5.x, RPC 1, Unterprotokoll `obswebsocket.json`, über Qt WebSockets. Quelle: https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md. Geprüft in `ctest` gegen einen Testserver auf `127.0.0.1` (`tests/obstestserver.h`), gegen das echte OBS nur im Handtest.

- **Verbinden:** `ObsConnection` verbindet sich, wenn `[obs] aktiv=ja`. Nach einem Fehlschlag neuer Versuch nach 2 s, Grenze je Versuch 5 s, Ping alle 5 s, ohne Antwort nach 10 s gilt die Verbindung als tot. Schliesst OBS mit 4009 (Anmeldung), 4010 oder 4011 (Version), versucht es der Kern nicht mehr, bis die Einstellungen sich ändern oder «OK» gedrückt wird; Knopf rot, Meldung in `obs_status`. OBS beschäftigt (Status 207): dieselbe Anfrage höchstens fünfmal nach je 1 s.
- **Ereignisfluss (Regel 14):** nicht gebaut, Punkt 92. `ObsConnection` gibt nach aussen nur `stateChanged` und `currentSceneChanged` mit eigenen Werten; verbunden an einer Stelle, `CoreService::setObsConnection`.
- **Quellen:** `ObsSources` hält die Zuordnung in `obs-quellen.json` (`docs/06-overlay-format.md`). Beim Verbinden und bei Änderungen in OBS gleicht der Kern ab (`GetInputList` mit `browser_source`, `GetInputSettings` je Quelle): Name nachführen, Quelle mit genau der richtigen Adresse übernehmen (Entscheid F10), Adresse und Grösse bei Unterschied mit `SetInputSettings` nachführen, auch nachträglich nach einem Neustart von OBS (F11). Fehlt eine Quelle, bleibt die Zuordnung auf `missing`; nichts wird von selbst angelegt (F7). Während eines Wechsels der Szenensammlung zählt `InputRemoved` nicht. Die Lage in der Szene fasst das Studio nie an. Es löscht nichts in OBS; beim Löschen eines Overlays kommt eine Warnung (F8).
- **Token:** Die Adresse mit Token steht in der Szenensammlung von OBS im Klartext, wie beim Eintragen von Hand. Sie steht nicht in `obs-quellen.json`, nicht in `obs_sources` und nicht im Protokoll.

## Overlay-Server (P10)

Der Kern liefert jedem Overlay eine Webseite für die Browserquelle in OBS (E-12, zweiter Zeichner K-01 bis K-08). Code: `src/server/` (`dss_server`, Schalter `DSS_CORE_OVERLAY_SERVER`, Standard ON), Seite: `web/overlay.html`, `web/overlay.js`. Begründung der Technik im Bericht `docs/archiv/berichte/P10-bericht.md`.

| Punkt | Festlegung |
|---|---|
| Technik | eigener kleiner HTTP/1.1-Server auf `QTcpServer` (Qt Network ist schon da; Qt HttpServer und Qt WebSockets sind nicht installiert, Regel 5). Änderungen als Server-Sent Events (`text/event-stream`): Der Kern schickt nur zur Seite, `EventSource` verbindet sich von selbst neu (https://html.spec.whatwg.org/multipage/server-sent-events.html). HTTP/1.1 nach RFC 9110 und RFC 9112 (https://www.rfc-editor.org/rfc/rfc9110, https://www.rfc-editor.org/rfc/rfc9112) |
| Adressen | nur IPv4: ein `QTcpServer` je Adresse, auf `127.0.0.1` und der «Adresse im Heimnetz» aus `kern.ini`, nie `QHostAddress::Any` (das nimmt auch IPv6 an). Port aus `kern.ini`. Gemessen am 2026-11-04 mit `ss -ltnH "sport = :24901"`: `192.168.1.9:24901` und `127.0.0.1:24901`; `ss -ltn6H` leer |
| Neue Einstellungen | Port oder Adresse geändert: Server schliesst alle Verbindungen und lauscht neu |
| Port belegt | Meldung in `server_status.problems` und im Protokoll (Warnung), zum Beispiel «Port 8081 ist belegt, vermutlich von DoNs Streaming Studio in Arbeit (/home/dominik/DoNs_Streaming_Studio/build/src/core/dss-core) (Prozess 1234). Die Browserquelle ist nicht erreichbar. Anderen Port im Dialog «Einstellungen» wählen oder das Programm beenden.» Das Programm findet der Kern über `/proc/net/tcp` und `/proc/net/tcp6` (Zeilen im Zustand `0A` = LISTEN mit dem Port, Inode) und die Links `/proc/<pid>/fd/*` → `socket:[<inode>]`; heisst das Programm (`/proc/<pid>/exe`) `dss-core` und liegt unter `~/.local/opt/dons-streaming-studio/`, heisst es «im Einsatz», ein anderer `dss-core` «in Arbeit (Pfad)», sonst «von «Dateiname»»; ohne Zugriff (Prozess eines anderen Benutzers) «von einem Programm, das sich nicht feststellen lässt». Neuer Versuch alle 2 s. Quellen: https://docs.kernel.org/networking/proc_net_tcp.html, proc_pid_fd(5) https://man7.org/linux/man-pages/man5/proc_pid_fd.5.html, proc_pid_exe(5) https://man7.org/linux/man-pages/man5/proc_pid_exe.5.html |
| Adresse fehlt | Ist die Adresse im Heimnetz leer oder gerade auf keiner Karte (Kabel), lauscht der Server nur auf `127.0.0.1`, meldet das in `server_status.problems` und prüft alle 2 s, ob sie da ist; dann lauscht er von selbst auch dort (Protokoll «Overlay-Server: Adresse im Heimnetz … ist da») |

### Pfade

Alles liegt unter `/o/<adresse>/<token>/` (`<adresse>` aus der Overlay-Datei, `<token>` 43 Zeichen Base64url, `docs/06-overlay-format.md`). Code: `src/common/browsersource.h`.

| Pfad | Methode | Antwort |
|---|---|---|
| `/o/<adresse>/<token>` | GET | 302 nach `…/` (sonst gingen die relativen Verweise der Seite ins Leere) |
| `/o/<adresse>/<token>/` | GET, HEAD | `overlay.html` |
| `…/overlay.js` | GET, HEAD | Zeichner der Seite |
| `…/events` | GET | Server-Sent Events: zuerst `retry: 1000`, dann `event: overlay` mit `data: {"overlay": <Overlay im Dateiformat ohne Token>, "media": {"media/…": {"orientation": 1 bis 8}}, "fonts": {"media/….ttf": {"units_per_em", "ascent", "descent", "leading"}}}` bei jeder Änderung (immer das ganze Overlay, K-02), ein neues `overlay` nur, wenn sich die Daten geändert haben; alle 15 s eine Kommentarzeile `:` (tote Verbindungen erkennen) |
| `…/media/<sha256>.<endung>` | GET, HEAD | nur Dateien, die das Overlay benutzt; `Range` (206, 416), `Cache-Control: max-age=31536000, immutable` (Dateien heissen nach ihrem Inhalt). Bei JPEG/PNG steht die EXIF-Ausrichtung in den gelieferten Bytes auf 1 (PNG mit neuer CRC des Blocks `eXIf`), bei MP4/MOV die Matrix in `mvhd` und `tkhd` auf der Einheitsmatrix, bei WebM (Matroska) `ProjectionPoseYaw` und `ProjectionPoseRoll` jeder Videospur auf 0.0 (nur Projektion «rechteckig», gleiche Länge, seit P11, Punkt 80): Die Seite dreht selbst nach `media[…].orientation`, unabhängig davon, ob die Chromium-Version es selbst täte. Die Datei im Datenordner bleibt unverändert |
| `…/report` | POST | Meldung der Seite (zum Beispiel «Video nicht abspielbar: «name» (media/…) (Fehler 4 …)»), höchstens 20 pro Minute, 300 Zeichen, Steuerzeichen ersetzt; Protokoll «Browserquelle /o/<adresse>/… meldet: …»; Antwort 204 |
| `/` | GET | leere, durchsichtige Seite |
| alles andere | – | 404 ohne Inhalt; andere Methoden 405 |

`Cache-Control`: Medien wie oben, `overlay.html` und `overlay.js` `no-cache` (OBS fragt jedes Mal nach, ob sie neu sind), alles andere `no-store`. Verbindungen bleiben offen (Keep-Alive); grosse Dateien gehen in Stücken zu 256 KiB, ein neues Stück erst, wenn weniger als 512 KiB auf das Senden warten.

### Alte und unbekannte Adressen

Der Token wird mit gleichbleibender Laufzeit verglichen (`constantTimeEquals` in `src/server/httpmessage.cpp`, Vergleich aller Bytes mit `volatile`, wie `sodium_memcmp`, https://libsodium.gitbook.io/doc/helpers#constant-time-test-for-equality); auch ohne passendes Overlay wird gegen einen Platzhalter von 43 Zeichen verglichen. Falsche oder alte Adresse (Umbenennen mit neuer Adresse, neuer Token, Overlay gelöscht, falscher Token): Die Seite bekommt eine **leere, durchsichtige Seite** (keine Fehlerseite, sonst wäre in OBS ein Kasten mit Text im Stream), die sich alle 10 s neu lädt (`<meta http-equiv="refresh" content="10">`, damit OBS nach dem Eintragen der richtigen Adresse ohne Zutun lädt); `…/events` bekommt `event: gone` (die Seite lädt neu und ist dann leer), andere Pfade 404. Protokoll (Warnung) «Browserquelle: unbekannte Adresse /o/<adresse>/… von <IPv4>», ohne Token, höchstens einmal pro Minute je Gegenstelle und Adresse; im Fenster erscheint `invalid_use` 30 s lang.

### Seite (zweiter Zeichner)

`web/overlay.js` zeichnet aus dem Overlay dasselbe wie der Editor (K-07): Kästen als `div` mit `rotate(θ) scale(±1, ±1)` um die Mitte, darin Bild bzw. Video gestreckt mit der Ausrichtung aus `media` (zuerst spiegeln, dann drehen, bei 5 bis 8 Breite und Höhe getauscht), Text als SVG mit derselben Schriftdatei (`FontFace`), Grundlinien aus `fonts` wie Qt 6.8 (`fontMetricPixels`, `src/render/sourcegeometry.h`), abgeschnitten am Kasten, waagrecht ausgerichtet. Videos stumm, beginnen immer von vorne (Einblenden, Laden, Ausgabe ein, andere Datei), Ende wie im Editor (`loop`, `hold_at_end`, sonst leer). Ausgabe aus: nichts gezeichnet, Verbindung bleibt. Bricht die Verbindung ab, bleibt das letzte Bild stehen, und `EventSource` verbindet sich nach 1 s wieder. Für Prüfungen steht der Zustand in `window.dssState` (`applied`, `output`, `connected`, `reports`). Abweichungen zum Editor in Zahlen: Bericht P10.

## Fehlerfälle

| Code | Wann | Folge |
|---|---|---|
| `invalid_json` | Zeile ist kein JSON-Objekt oder hat kein Feld `type` (Text) | Zeile übergangen, Verbindung bleibt |
| `invalid_message` | Pflichtfeld fehlt oder hat falschen Typ (zum Beispiel `get_overlay` ohne gültige `id`) | Nachricht übergangen |
| `unknown_type` | unbekannter Wert in `type` | Nachricht übergangen |
| `hello_missing` | Anfrage vor dem `hello` | Anfrage übergangen |
| `protocol_version` | `hello` mit anderer Version | Kern trennt die Verbindung |
| `unknown_overlay` | `get_overlay` oder eine Änderung mit einer Kennung, die es nicht gibt | – |
| `unknown_source` | Änderung mit einer Quelle, die es im Overlay nicht gibt (nur in `change_result`) | – |
| `unknown_folder` | Änderung mit einem Ordner, den es nicht gibt, in `liste.json` bzw. seit Version 10 in `medien.json` (nur in `change_result`) | – |
| `in_use` | seit Version 10: `remove_media` mit einer Datei, die eine Quelle nutzt; Meldung mit «verwendet in» (nur in `change_result`) | nichts gelöscht |
| `canceled` | seit Version 10: Übernahme mit `cancel_import` abgebrochen (nur in `change_result` der abgebrochenen Änderung) | nichts geändert, nichts in `media/` |
| `rejected` | Änderung vom Modell oder beim Kopieren der Datei abgewiesen oder ungültige Einstellung (`update_settings`, `reset_settings`), zum Beispiel ungültiger Wert, Adresse nicht von diesem PC, Datei nicht lesbar, `liste.json` kaputt; seit Version 11 auch `obs_create_source` und `obs_add_to_scene`, die OBS oder der Kern abweist, zum Beispiel Szene nicht mehr da, Name leer, keine Adresse (nur in `change_result`) | nichts geändert |
| `obs_not_connected` | seit Version 11: `obs_create_source` oder `obs_add_to_scene` ohne Verbindung zu OBS oder Verbindung während der Anfrage verloren; im zweiten Fall sagt die Meldung, dass es sich nach dem Wiederverbinden zeigt (nur in `change_result`) | – |
| `obs_name_taken` | seit Version 11: Name in OBS schon vergeben (nur in `change_result`) | nichts angelegt |
| `obs_source_missing` | seit Version 11: `obs_add_to_scene`, die Quelle oder die Szene gibt es in OBS nicht mehr (nur in `change_result`) | – |
| `obs_timeout` | seit Version 11: OBS hat nicht rechtzeitig geantwortet (nur in `change_result`) | – |
| `save_failed` | verzögertes Speichern gescheitert (als `error` an alle, `request` `null`) | Änderung bleibt im Speicher, neuer Versuch |
| `message_too_long` | Zeile länger als 16 MiB | Kern trennt die Verbindung |

Auf der Fenster-Seite gibt es keine Fehlermeldungen an den Kern: Ungültige Nachrichten des Kerns werden im Protokoll gemeldet und übergangen; ein ungültiges `hello` oder eine zu lange Zeile trennt die Verbindung (neuer Versuch nach 2 s).

## Kern als Dienst

| Punkt | Festlegung |
|---|---|
| Einheit | `~/.config/systemd/user/dss-core.service` (Version im Einsatz, angelegt von `scripts/installieren.sh` über `scripts/dienst-einrichten.sh --einsatz`) bzw. `dss-core-entwicklung.service` (Version in Arbeit, `scripts/dienst-einrichten.sh`); entfernen: `--entfernen`. Unten steht `dss-core.service`, für die Version in Arbeit gilt dasselbe mit dem anderen Namen |
| Start | nicht bei der Anmeldung (kein `[Install]`); das Fenster prüft beim Start, ob der Kern antwortet, und startet sonst `systemctl --user start dss-core.service` |
| Watchdog | seit P10a: `WatchdogSec=10s`. Der Kern meldet `WATCHDOG=1` alle 5 s aus seiner Hauptschleife. Seit P10-vor (Übernehmen im eigenen Thread) keine zusätzliche Meldung beim Übernehmen mehr: Die Hauptschleife blockiert dabei nicht, und eine Meldung aus dem Thread würde eine hängende Hauptschleife verdecken. Nur mit `DSS_CORE_IMPORT_THREAD=OFF` meldet der Kern beim Übernehmen wie bis P10a zusätzlich jede Sekunde. Bleibt die Meldung aus, beendet systemd ihn (SIGABRT, nach `TimeoutAbortSec=5s` SIGKILL) und startet ihn neu (`Restart=on-failure` gilt auch für den Watchdog). Ohne Watchdog: `scripts/dienst-einrichten.sh --watchdog 0`, Schalter `DSS_CORE_WATCHDOG` |
| Bereit | `Type=notify`: Der Kern meldet `READY=1`, sobald er Verbindungen annimmt; erst dann kehrt `systemctl start` zurück |
| Neustart | `Restart=on-failure`, `RestartSec=1s`: nach Exit-Code ≠ 0 oder Signal wie SIGKILL/SIGSEGV. Nicht nach Exit-Code 0, SIGTERM, SIGINT, SIGHUP, SIGPIPE oder `systemctl stop` |
| Kein Neustart bei | Exit-Code 3 (`RestartPreventExitStatus=3`): Datenordner gesperrt oder Verbindung belegt (anderer Kern läuft) |
| Neu laden | SIGHUP bzw. `systemctl --user reload dss-core.service` |
| Protokoll | Journal: `journalctl --user -u dss-core.service` |
| Sperre | `<Datenordner>/kern.lock` (`QLockFile`, `staleLockTime` 0). Solange der Kern läuft, ändert nur er die Overlay-Dateien; `dss-cli` zeigt dann nur an und bricht Änderungen mit Exit-Code 3 ab |

Quellen: https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html (`Type=`, `Restart=`, `RestartSec=`, `RestartPreventExitStatus=`, `NotifyAccess=`), https://www.freedesktop.org/software/systemd/man/latest/systemd.unit.html, https://www.freedesktop.org/software/systemd/man/latest/sd_notify.html (`READY=1`, `STOPPING=1`, `NOTIFY_SOCKET`, eigene Umsetzung ohne libsystemd), https://www.freedesktop.org/software/systemd/man/latest/sd_watchdog_enabled.html (`WATCHDOG_USEC`, `WATCHDOG_PID`, Meldung alle halbe Zeit), https://www.freedesktop.org/software/systemd/man/latest/systemd.kill.html (SIGCONT nach dem Signal), https://doc.qt.io/qt-6.8/qlockfile.html.

Exit-Codes von `dss-core`: 0 = beendet (auch nach SIGTERM/SIGINT oder `quit_request`), 1 = Fehler (zum Beispiel Datenordner nicht lesbar, Pfad der Verbindung zu lang), 3 = anderer Kern läuft. Ein falscher Aufruf (unbekannte Option) endet ebenfalls mit 1 (geprüft 2026-11-02: `build/src/core/dss-core --gibt-es-nicht; echo $?` → 1).
