# 08 – Schnittstelle Kern–Fenster

Stand: 2026-11-03 (P10-vor). Version der Schnittstelle: **5**. Massgebend ist der Code: Nachrichten in `src/common/protocol.h/.cpp`, Kern in `src/core/coreservice.cpp`, Fenster-Seite in `src/common/coreclient.cpp`, verzögertes Speichern in `src/common/savescheduler.h/.cpp`. Tests: `tst_protocol` und `tst_aenderungen` (alle Nachrichten lesen und schreiben, Abweisungen, Zusammenfassen, ohne Qt Gui), `tst_kernverbindung` und `tst_bearbeiten` (Kern als eigener Prozess, Zusammenspiel, zwei Fenster, Neustart), `tst_kerneinstellungen` (kern.ini ohne Kern), `tst_uebernehmen` (Übernehmen von Dateien im eigenen Thread).

Seit P08 kann das Fenster über die Schnittstelle **ändern** (Abschnitt «Änderungen»). Jede Änderung geht als Nachricht an den Kern; nur der Kern schreibt Dateien (K-03). Der **Token** eines Overlays wird nie übertragen, weder gelesen noch geändert (kommt mit P10; CLAUDE.md Regel 12).

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
   | <--- connections -------------------- |
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
   | ---- get_fonts ---------------------> |
   | <--- font_list ---------------------- |
   |                ...                    |
   | ---- get_settings ------------------> |   seit Version 5 (P10-vor)
   | <--- settings ----------------------- |
   | ---- update_settings {request_id 9} > |
   | <--- settings ----------------------- |   an alle Fenster, wenn geändert
   | <--- change_result {request_id 9} --- |   nur an dieses Fenster, zuletzt
   |                ...                    |
   | ---- quit_request ------------------> |   normales Schliessen des Fensters
   | <--- quit_reply {accepted} ---------- |
   | ---- trennt -------------------------X|
```

1. Beide Seiten schicken ihr `hello` sofort nach dem Verbinden, ohne auf die andere zu warten.
2. Das Fenster gilt als verbunden, sobald das `hello` des Kerns mit `protocol_version` = 5 da ist. Bei einer anderen Version trennt das Fenster und zeigt «Kern hat Schnittstelle Version X, das Fenster kennt Version 5».
3. Der Kern prüft das `hello` des Fensters. Bei einer anderen Version schickt er `error` mit `protocol_version` und trennt. Andere Nachrichten vor dem `hello` beantwortet er mit `error` `hello_missing` und übergeht sie.
4. Nach dem `hello` schickt der Kern `overlay_list`, `connections` und, falls schon gemessen, die letzte `stats`.
5. Danach schickt der Kern jede Sekunde `stats`, bei Änderungen `overlay_changed`, `overlay_removed`, `overlay_list` und (wenn eine neue Schriftdatei dazukam) `font_list` an **alle** begrüssten Verbindungen.
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
| `protocol_version` | ganze Zahl ab 1 | Version der Schnittstelle, heute `5` |
| `data_dir` | Text, nicht immer da | seit Version 3 nur vom Kern: Datenordner als absoluter Pfad. Das Fenster liest dort die Dateien der Quellen (`media/…`) für den Zeichner der Leinwand; es schreibt nie hinein (K-03) |

```json
{"data_dir":"/home/dominik/.local/share/dons-streaming-studio","program":"dss-core","protocol_version":5,"type":"hello","version":"0.1.0"}
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

Jedes Overlay steht genau einmal in der Liste; jede Kennung (Overlay und Ordner) kommt nur einmal vor. Sonst ist die Nachricht ungültig.

```json
{"entries":[{"expanded":true,"id":"70fcf543-5e55-4028-ace3-1ddfed919ce5","kind":"folder","name":"Szenen","overlays":[{"address":"ueberblick-start","height":1080,"id":"9adf79a8-1420-4d18-94b1-d780afba49d8","name":"Überblick Start","source_count":0,"width":1920}]}],"list_error":null,"type":"overlay_list"}
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
| `message` | Text oder `null` | bei `ok: false` die Meldung auf Deutsch; das Fenster zeigt sie in der Statuszeile. Sonst `null` |
| `warning` | Text oder `null` | seit Version 3: bei `ok: true` ein Hinweis, den das Fenster in der Statuszeile zeigt, zum Beispiel «Grösse von «bild.png» nicht lesbar (…), die Quelle bekommt die Grösse des Overlays 1920x1080.» Sonst `null` |

```json
{"code":null,"id":"9adf79a8-1420-4d18-94b1-d780afba49d8","message":null,"ok":true,"request_id":1,"type":"change_result","warning":null}
{"code":"rejected","id":null,"message":"Drehung 3600 ungültig, erlaubt sind 0 bis 3599 Zehntelgrad.","ok":false,"request_id":6,"type":"change_result","warning":null}
{"code":"invalid_message","id":null,"message":"Feld changes.breite ist unbekannt","ok":false,"request_id":7,"type":"change_result","warning":null}
```

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

Zustand der vier Verbindungen im Kopfbereich. In P07 immer `off` (grau), weil es noch keine Verbindungen gibt. Werte wie die Eigenschaft `state` der Knöpfe: `off` (grau), `error` (rot), `connecting` (orange), `connected` (grün), siehe `docs/01-festlegungen.md`, «Verbindungsanzeige im Kopfbereich».

| Feld | Typ | Inhalt |
|---|---|---|
| `twitch`, `youtube`, `obs`, `ndi` | Text | Zustand, alle vier Pflicht |

```json
{"ndi":"off","obs":"off","twitch":"off","type":"connections","youtube":"off"}
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
| `hello` | `program` (Text, Fenster: `dss-app`, Werkzeug: `dss-cli`), `version` (Text), `protocol_version` (ganze Zahl, `5`) | `overlay_list`, `connections`, `stats` |
| `get_overlay_list` | – | `overlay_list` |
| `get_overlay` | `id` (Text, Kennung) | `overlay` oder `error` `unknown_overlay` |
| `get_fonts` | – | `font_list` |
| `quit_request` | – | `quit_reply` |
| `get_settings` | – (seit Version 5) | `settings` |
| `update_settings` | siehe Abschnitt «Einstellungen» (seit Version 5) | `settings` an alle (wenn geändert), dann `change_result` |
| Änderungen | siehe Abschnitt «Änderungen» | Nachrichten an alle, dann `change_result` |

```json
{"program":"dss-app","protocol_version":5,"type":"hello","version":"0.1.0"}
{"type":"get_fonts"}
{"type":"get_overlay_list"}
{"id":"db7f47a9-dee3-4de5-b056-40b5b3507607","type":"get_overlay"}
{"type":"quit_request"}
{"type":"get_settings"}
```

## Änderungen (P08)

Jede Änderung ist eine eigene Nachricht mit dem Feld `request_id` (ganze Zahl 0 bis 2^53, vom Fenster gewählt, kommt in `change_result` zurück). Der Kern prüft sie mit dem Modell (dieselben Prüfungen wie beim Lesen der Overlay-Datei, `docs/06-overlay-format.md`), übernimmt sie und schickt den neuen Zustand an alle Fenster (Abschnitt «Verteilen von Änderungen»), danach `change_result` an das Fenster, das sie geschickt hat. Eine abgewiesene Änderung ändert nichts; der Kern schreibt sie als Warnung ins Protokoll.

Das Fenster übernimmt den Zustand **nur** aus `overlay_changed`, `overlay_removed` und `overlay_list`, nie aus der eigenen Änderung. So sehen alle Fenster immer dasselbe.

Kennungen: `overlay_id`, `source_id`, `folder_id` sind Text (UUID). `file` ist ein absoluter Pfad auf dem Studio-PC (Fenster und Kern laufen auf demselben Rechner); bei `set_text_font` auch ein Verweis `media/…` auf eine Kopie im Datenordner. Der Kern kopiert jede Datei nach `media/<SHA-256>.<Endung>`; gleicher Inhalt gibt eine Kopie (K-03).

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
| `add_image_source` | `overlay_id`, `file` | neue Bildquelle zuoberst, Lage 0/0 in der Grösse der Datei (seit P09; nicht lesbar: Grösse des Overlays und `warning`) | Quelle |
| `add_video_source` | `overlay_id`, `file` | neue Videoquelle wie oben (Grösse des ersten Bildes), «Wiederholen» und «Am Ende anhalten» aus | Quelle |
| `add_text_source` | `overlay_id` | neue Textquelle wie oben, Inhalt «Text», Schrift Be Vietnam Pro Regular | Quelle |
| `remove_source` | `overlay_id`, `source_id` | entfernt die Quelle (die Kopie im Datenordner bleibt) | – |
| `move_source` | `overlay_id`, `source_id`, `index` | Stelle in der Zeichenreihenfolge, 0 = ganz hinten | – |
| `update_source` | `overlay_id`, `source_id`, `changes` (Objekt, unten) | ändert die angegebenen Eigenschaften, alle zusammen oder keine | – |
| `set_text_font` | `overlay_id`, `source_id`, `file`, `weight` (100 bis 900 oder `null`), `italic` (true/false oder `null`) | Schriftdatei, Gewicht und kursiv zusammen; `null` = aus der Datei lesen (`fc-query`, für «Datei wählen …») | – |
| `replace_image_file` | `overlay_id`, `source_id`, `file` | neues Bild für eine Bildquelle; Lage und Grösse bleiben | – |

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
{"type":"set_text_font","request_id":8,"overlay_id":"9adf79a8-1420-4d18-94b1-d780afba49d8","source_id":"c520a200-5230-438f-8a5e-72685bc804aa","file":"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf","weight":700,"italic":false}
```

(aufgezeichnet am 2026-11-03 mit einem Kern mit eigenem Datenordner; die Antworten stehen oben unter `change_result`. Die Zeilen mit `request_id` 9 und 10 sind in P09 von Hand ergänzt, Form wie `tst_aenderungen` sie prüft.)

Ungültige Änderung (Pflichtfeld fehlt, falscher Typ): Hat sie eine gültige `request_id`, antwortet der Kern mit `change_result` `ok: false`, `code: invalid_message`; sonst mit `error` `invalid_message`.

### Übernehmen von Dateien (P10-vor, Punkt 67)

Änderungen mit einer neuen Datei (`add_image_source`, `add_video_source`, `replace_image_file`, `add_text_source` mit Schrift, `set_text_font` mit einem Pfad statt `media/…`) prüft der Kern zuerst (Overlay und Quelle vorhanden). Dann rechnet er SHA-256 und kopiert die Datei in einem **eigenen Thread** (höchstens 2 gleichzeitig, `MediaImporter` in `src/common/mediaimport.h/.cpp`), stückweise zu 1 MiB, mit `QSaveFile`. Die Hauptschleife beantwortet in der Zwischenzeit alle Nachrichten und schickt `stats` weiter. Erst wenn die Kopie fertig ist, ändert der Kern das Overlay (in der Hauptschleife, wie jede Änderung), verteilt es und schickt `change_result`. Gemessen in `tst_uebernehmen` (2026-11-03, 256 MiB): Antwortzeit auf `get_overlay_list` während des Übernehmens höchstens 25 ms.

- Nur der Kern schreibt Dateien; Änderungen an Overlays und `liste.json` bleiben in der Hauptschleife.
- Zwei Änderungen mit derselben Datei gleichzeitig: Die zweite wartet auf die erste und findet dann die Kopie vor (eine Kopie, Protokoll «war schon vorhanden»).
- Wird das Overlay oder die Quelle während des Übernehmens gelöscht, antwortet der Kern mit `change_result` `unknown_overlay` bzw. `unknown_source`; die Kopie bleibt in `media/` (gleicher Inhalt wird später wieder verwendet).
- Beim Beenden (SIGTERM, SIGINT, `quit_request`) bricht der Kern laufende Übernahmen ab; die vorläufige Datei verschwindet (`QSaveFile::cancelWriting`), unter dem endgültigen Namen bleibt nichts. Protokoll: «N Übernahmen abgebrochen (… ms)».
- Schalter `DSS_CORE_IMPORT_THREAD` (Standard ON). OFF: wie bis P10a in der Hauptschleife.

Vorläufige Dateien, die nach einem Absturz übrig bleiben könnten (`<SHA-256>.<Endung>.XXXXXX`), löscht der Kern beim Start nach dem Nehmen der Sperre (Protokoll «Aufräumen media/: N vorläufige Dateien von QSaveFile gelöscht (… Byte)»). Muster und Quellen in `src/common/mediaimport.h`.

### Häufige Änderungen zusammenfassen

Hält der Nutzer eine Pfeiltaste in einem Zahlenfeld gedrückt, entstehen viele Änderungen pro Sekunde. Zwei Stufen verhindern, dass der Kern überlastet wird oder die Datei bei jedem Schritt schreibt:

1. **Fenster** (`CoreClient::sendChange` mit Schlüssel, zum Beispiel Feld und Quelle; beim Ziehen auf der Leinwand seit P09 ein `update_source` mit `x`, `y`, `width`, `height`, `rotation` (seit P09b auch `flip_horizontal`, `flip_vertical`; ebenso die Befehle «Transformieren») unter dem Schlüssel «<Quelle>/geometry», Auflösung unter «<Overlay>/size»): Je Schlüssel ist höchstens eine Änderung unterwegs. Eine neue Änderung, bevor `change_result` da ist, wird zurückgehalten und ersetzt eine schon zurückgehaltene; nach der Antwort geht die letzte los. Gemessen in `tst_bearbeiten` (2026-11-03): 200 Schritte → 3 Änderungen an den Kern, der letzte Wert kommt an.
2. **Kern** (Abschnitt «Speichern»): Jede Änderung gilt sofort im Speicher und geht sofort an alle Fenster; die Datei schreibt er später.

### Speichern

| Punkt | Festlegung |
|---|---|
| Overlay-Dateien | nach 300 ms ohne weitere Änderung, spätestens 1000 ms nach der ersten noch nicht gespeicherten Änderung (`SaveScheduler`); atomar (`QSaveFile`) |
| Sofort geschrieben | Anlegen, Kopieren, Löschen eines Overlays, `liste.json`, Kopien im Datenordner |
| Beim Beenden | vor dem Ende (`quit_request`, SIGTERM, SIGINT) und vor dem Neuladen (SIGHUP) speichert der Kern alles Offene |
| `kill -9` / Absturz | höchstens etwa die Änderungen der letzten Sekunde gehen verloren |
| Fehler | Kann eine Datei nicht geschrieben werden, schreibt der Kern das ins Protokoll (kritisch), schickt `error` `save_failed` an alle und versucht es mit der nächsten Änderung und beim Beenden wieder. Im Speicher bleibt die Änderung gültig |

## Einstellungen (P10-vor)

Einstellungen des Kerns stehen in `kern.ini` im Ordner der Einstellungen der Version: `~/.config/dons-streaming-studio/kern.ini` (Version im Einsatz), `~/.config/dons-streaming-studio-entwicklung/kern.ini` (Version in Arbeit); anderer Pfad: `dss-core --einstellungen PFAD`. Nur der Kern schreibt die Datei, atomar (`QSaveFile`). Code: `src/common/coresettings.h/.cpp`. Beide Werte wirken erst mit dem Overlay-Server (P10).

```ini
[overlay-server]
port=8081
adresse-im-heimnetz=192.168.1.9
```

| Wert | Festlegung |
|---|---|
| `port` | Port des Overlay-Servers. Standard 8080 im Einsatz, 8081 in Arbeit (beide Versionen können gleichzeitig laufen). Gültig **1024 bis 32767**: Ports unter 1024 darf unter Linux nur ein Prozess mit besonderen Rechten öffnen (`/proc/sys/net/ipv4/ip_unprivileged_port_start` = 1024); ab 32768 vergibt Linux Ports für ausgehende Verbindungen (`/proc/sys/net/ipv4/ip_local_port_range` = 32768 60999), ein Port dort kann beim Start des Servers zufällig belegt sein. Werte gemessen auf dem Studio-PC am 2026-11-03 mit `cat`. Quelle: ip(7), https://man7.org/linux/man-pages/man7/ip.7.html |
| `adresse-im-heimnetz` | IPv4-Adresse des Studio-PCs, vier Zahlen 0 bis 255 ohne führende Nullen; nicht 0.0.0.0, 127.x.x.x und ab 224.0.0.0. Leer erlaubt. Fehlt sie beim Start, trägt der Kern einmal die Adresse der Netzwerkkarte der Standardroute ein und schreibt die Datei; danach ermittelt er sie nie neu. Standardroute: die erste Zeile in `/proc/net/route` mit `Destination` und `Mask` 00000000 und gesetztem Flag `RTF_UP` (0x0001, `/usr/include/linux/route.h`) (wie die LAN-Anzeige, `defaultRouteInterface` in `src/common/systemstats.h`; Quelle: proc_pid_net(5), https://man7.org/linux/man-pages/man5/proc_pid_net.5.html, und route(8)), Adresse: die erste IPv4-Adresse dieser Karte, die nicht 127.x.x.x und nicht 169.254.x.x ist (`QNetworkInterface::interfaceFromName`, https://doc.qt.io/qt-6.8/qnetworkinterface.html). Ohne IPv4-Standardroute bleibt der Wert leer (Warnung im Protokoll), neuer Versuch beim nächsten Start |

Ist die Datei ungültig (unbekannte Angabe oder Abschnitt, doppelte Angabe, ungültiger Wert), gelten die Standardwerte, der Kern schreibt eine Warnung ins Protokoll, überschreibt die Datei nicht und weist `update_settings` ab, bis sie von Hand repariert oder gelöscht ist.

### `settings` (Kern → Fenster)

Antwort auf `get_settings` und nach jeder Änderung an **alle** begrüssten Verbindungen.

| Feld | Typ | Bedeutung |
|---|---|---|
| `port` | ganze Zahl | Port |
| `home_address` | Text | Adresse im Heimnetz, leer = nicht gesetzt |
| `port_min`, `port_max` | ganze Zahlen | gültiger Bereich (1024, 32767); das Fenster stellt sein Zahlenfeld danach ein |
| `file` | Text | Pfad von `kern.ini` |
| `settings_error` | Text oder `null` | Meldung, wenn die Datei ungültig ist |

```json
{"file":"/home/dominik/.config/dons-streaming-studio-entwicklung/kern.ini","home_address":"192.168.1.9","port":8081,"port_max":32767,"port_min":1024,"settings_error":null,"type":"settings"}
```

### `update_settings` (Fenster → Kern)

Felder: `request_id` (wie bei Änderungen), dazu mindestens eines von `port` (ganze Zahl) und `home_address` (Text, leer erlaubt). Nur angegebene Werte ändern sich. Der Kern prüft alle Werte; ist einer ungültig, ändert sich nichts und `change_result` hat `ok: false`, `code: rejected` und die Meldung, zum Beispiel «Port 80 ungültig, erlaubt sind 1024 bis 32767.». Sonst schreibt er die Datei, schickt `settings` an alle (auch an den Absender) und dann `change_result` `ok: true`. Ohne Wert oder mit falschem Typ: `change_result` `invalid_message` (bzw. `error`, wenn `request_id` fehlt).

```json
{"type":"update_settings","request_id":9,"port":8090}
{"type":"update_settings","request_id":10,"home_address":"192.168.1.9"}
```

`dss-cli einstellungen`, `einstellung-port PORT`, `einstellung-adresse ADRESSE` nutzen dieselben Nachrichten (`hello` mit `program` `dss-cli`, `src/common/coresession.h`). Läuft der Kern nicht, zeigt `einstellungen` die Datei an; ändern geht dann nicht (Exit-Code 4).

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
| `unknown_folder` | Änderung mit einem Ordner, den es nicht gibt (nur in `change_result`) | – |
| `rejected` | Änderung vom Modell oder beim Kopieren der Datei abgewiesen oder ungültige Einstellung (`update_settings`), zum Beispiel ungültiger Wert, Datei nicht lesbar, `liste.json` kaputt (nur in `change_result`) | nichts geändert |
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
