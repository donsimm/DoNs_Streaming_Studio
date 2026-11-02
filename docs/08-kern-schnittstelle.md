# 08 – Schnittstelle Kern–Fenster

Stand: 2026-11-02 (P07). Version der Schnittstelle: **1**. Massgebend ist der Code: Nachrichten in `src/common/protocol.h/.cpp`, Kern in `src/core/coreservice.cpp`, Fenster-Seite in `src/common/coreclient.cpp`. Tests: `tst_protocol` (alle Nachrichten lesen und schreiben, ohne Qt Gui) und `tst_kernverbindung` (Kern als eigener Prozess, Zusammenspiel).

In P07 ist die Schnittstelle **nur lesend**: Das Fenster kann Overlays ansehen, aber nichts ändern (Bearbeiten kommt mit P08). Der **Token** eines Overlays wird nie übertragen (kommt mit P10; CLAUDE.md Regel 12).

## Verbindung

| Punkt | Festlegung |
|---|---|
| Art | Unix-Socket (`QLocalServer` / `QLocalSocket`), Datenstrom (`SOCK_STREAM`), kein Netzwerk |
| Pfad | `$XDG_RUNTIME_DIR/dons-streaming-studio/kern.sock`, auf dem Studio-PC `/run/user/1000/dons-streaming-studio/kern.sock`. Anderer Pfad: `dss-core --verbindung PFAD`, `dss-app --verbindung PFAD` |
| Länge des Pfads | höchstens 107 Byte (`sun_path` hat 108 Byte mit abschliessender Null, unix(7)); sonst startet der Kern nicht und nennt den Grund |
| Zugriff | Ordner mit Rechten 0700, Socket mit `QLocalServer::UserAccessOption` (nur derselbe Benutzer). `XDG_RUNTIME_DIR` selbst hat 0700 |
| Mehrere Fenster | erlaubt; jedes bekommt alle Nachrichten an «alle» |
| Übrig gebliebener Socket | Findet der Kern beim Start eine Socket-Datei, verbindet er sich probehalber (500 ms). Antwortet dort ein Kern, beendet er sich mit Exit-Code 3; sonst entfernt er die Datei und öffnet sie neu |

Quellen: https://doc.qt.io/qt-6.8/qlocalserver.html (`SocketOption`, `removeServer`), https://doc.qt.io/qt-6.8/qlocalsocket.html, https://man7.org/linux/man-pages/man7/unix.7.html, https://doc.qt.io/qt-6.8/qstandardpaths.html (`RuntimeLocation`).

## Format

- Jede Nachricht ist **ein JSON-Objekt in einer Zeile** (UTF-8, kompakt), abgeschlossen mit `\n` (Byte 0x0A). Zeilenwechsel in Texten stehen im JSON als `\n` (zwei Zeichen), nie als Byte 0x0A.
- Jede Nachricht hat das Feld `type` (Text). Die übrigen Felder hängen vom Typ ab.
- Eine Zeile darf höchstens 16 MiB (16 777 216 Byte) lang sein. Eine längere Zeile beendet die Verbindung (Kern schickt vorher `error` mit `message_too_long`).
- **Pflichtfelder** müssen vorhanden sein und den richtigen Typ haben, sonst ist die Nachricht ungültig. **Unbekannte Felder** werden übergangen; so lassen sich später Felder ergänzen, ohne die Version zu erhöhen. Eine Änderung, die alte Gegenstellen falsch verstehen würden, erhöht die Version.
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
   | ---- quit_request ------------------> |   normales Schliessen des Fensters
   | <--- quit_reply {accepted} ---------- |
   | ---- trennt -------------------------X|
```

1. Beide Seiten schicken ihr `hello` sofort nach dem Verbinden, ohne auf die andere zu warten.
2. Das Fenster gilt als verbunden, sobald das `hello` des Kerns mit `protocol_version` = 1 da ist. Bei einer anderen Version trennt das Fenster und zeigt «Kern hat Schnittstelle Version X, das Fenster kennt Version 1».
3. Der Kern prüft das `hello` des Fensters. Bei einer anderen Version schickt er `error` mit `protocol_version` und trennt. Andere Nachrichten vor dem `hello` beantwortet er mit `error` `hello_missing` und übergeht sie.
4. Nach dem `hello` schickt der Kern `overlay_list`, `connections` und, falls schon gemessen, die letzte `stats`.
5. Danach schickt der Kern jede Sekunde `stats`, bei Änderungen `overlay_changed`, `overlay_removed` und `overlay_list` an **alle** begrüssten Verbindungen.
6. Unbekannte Typen: Der Kern antwortet mit `error` `unknown_type`. Das Fenster übergeht unbekannte Typen (Protokollzeile).
7. Verbindung getrennt: Der Kern vergisst die Verbindung. Das Fenster leert alle Daten vom Kern (Docks leer, Werte «–», Verbindungsknöpfe grau), zeigt «Kern-Dienst: nicht verbunden» und versucht es alle 2 s wieder.

### Änderungen (K-02)

Bei jeder Änderung schickt der Kern das **ganze** geänderte Overlay (`overlay_changed`) an alle Fenster, auch an solche, die es nie angefragt haben. In P07 ändert sich nur etwas, wenn der Kern den Datenordner neu lädt (SIGHUP, zum Beispiel `systemctl --user reload dss-core.service`): Dann vergleicht er jedes Overlay mit dem Stand vorher und schickt

- `overlay_changed` für jedes neue oder geänderte Overlay,
- `overlay_removed` für jedes Overlay, das es nicht mehr gibt,
- zum Schluss einmal `overlay_list`, wenn sich etwas geändert hat.

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
| `protocol_version` | ganze Zahl ab 1 | Version der Schnittstelle, heute `1` |

```json
{"program":"dss-core","protocol_version":1,"type":"hello","version":"0.1.0"}
```

### `overlay_list`

Liste aller geladenen Overlays, sortiert wie im Kern (nach Name ohne Gross-/Kleinschreibung, dann Adresse; Punkt 22).

| Feld | Typ | Inhalt |
|---|---|---|
| `overlays` | Liste | 0 oder mehr Einträge |
| `overlays[].id` | Text | Kennung |
| `overlays[].name` | Text | Name |
| `overlays[].address` | Text | Adresse der Browserquelle |
| `overlays[].width`, `overlays[].height` | ganze Zahl | Auflösung (1 bis 7680 bzw. 1 bis 4320) |
| `overlays[].source_count` | ganze Zahl ab 0 | Anzahl Quellen |

```json
{"overlays":[{"address":"ueberblick-start","height":1080,"id":"db7f47a9-dee3-4de5-b056-40b5b3507607","name":"Überblick Start","source_count":3,"width":1920}],"type":"overlay_list"}
```

### `overlay` und `overlay_changed`

Ein ganzes Overlay. `overlay` ist die Antwort auf `get_overlay`, `overlay_changed` geht bei einer Änderung an alle.

| Feld | Typ | Inhalt |
|---|---|---|
| `overlay` | Objekt | Overlay **im Format der Overlay-Datei** (`docs/06-overlay-format.md`), mit allen Feldern einschliesslich `format_version`; gelesen wird es genauso streng wie die Datei. **Ohne Token.** |

```json
{"overlay":{"address":"test-eins","format_version":1,"height":720,"id":"7be08f35-6885-4a95-9c32-939c45a688ea","name":"Test Eins","sources":[{"height":720,"id":"0c1d2b7e-8a43-4c1e-9d47-1b5e6f3a2c90","image":{"file":"media/3bb2f4e86362dd6d44bfa852d36109788d1854c38cded41ce74f3758cecb099e.png"},"kind":"image","name":"bild1.png","rotation":0,"visible":true,"width":1280,"x":0,"y":0}],"width":1280},"type":"overlay"}
```

### `overlay_removed`

| Feld | Typ | Inhalt |
|---|---|---|
| `id` | Text | Kennung des Overlays, das es nicht mehr gibt |

```json
{"id":"7be08f35-6885-4a95-9c32-939c45a688ea","type":"overlay_removed"}
```

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
| `hello` | `program` (Text, Fenster: `dss-app`), `version` (Text), `protocol_version` (ganze Zahl, `1`) | `overlay_list`, `connections`, `stats` |
| `get_overlay_list` | – | `overlay_list` |
| `get_overlay` | `id` (Text, Kennung) | `overlay` oder `error` `unknown_overlay` |
| `quit_request` | – | `quit_reply` |

```json
{"program":"dss-app","protocol_version":1,"type":"hello","version":"0.1.0"}
{"type":"get_overlay_list"}
{"id":"db7f47a9-dee3-4de5-b056-40b5b3507607","type":"get_overlay"}
{"type":"quit_request"}
```

## Fehlerfälle

| Code | Wann | Folge |
|---|---|---|
| `invalid_json` | Zeile ist kein JSON-Objekt oder hat kein Feld `type` (Text) | Zeile übergangen, Verbindung bleibt |
| `invalid_message` | Pflichtfeld fehlt oder hat falschen Typ (zum Beispiel `get_overlay` ohne gültige `id`) | Nachricht übergangen |
| `unknown_type` | unbekannter Wert in `type` | Nachricht übergangen |
| `hello_missing` | Anfrage vor dem `hello` | Anfrage übergangen |
| `protocol_version` | `hello` mit anderer Version | Kern trennt die Verbindung |
| `unknown_overlay` | `get_overlay` mit einer Kennung, die es nicht gibt | – |
| `message_too_long` | Zeile länger als 16 MiB | Kern trennt die Verbindung |

Auf der Fenster-Seite gibt es keine Fehlermeldungen an den Kern: Ungültige Nachrichten des Kerns werden im Protokoll gemeldet und übergangen; ein ungültiges `hello` oder eine zu lange Zeile trennt die Verbindung (neuer Versuch nach 2 s).

## Kern als Dienst

| Punkt | Festlegung |
|---|---|
| Einheit | `~/.config/systemd/user/dss-core.service`, angelegt von `scripts/dienst-einrichten.sh` (entfernen: `--entfernen`) |
| Start | nicht bei der Anmeldung (kein `[Install]`); das Fenster prüft beim Start, ob der Kern antwortet, und startet sonst `systemctl --user start dss-core.service` |
| Bereit | `Type=notify`: Der Kern meldet `READY=1`, sobald er Verbindungen annimmt; erst dann kehrt `systemctl start` zurück |
| Neustart | `Restart=on-failure`, `RestartSec=1s`: nach Exit-Code ≠ 0 oder Signal wie SIGKILL/SIGSEGV. Nicht nach Exit-Code 0, SIGTERM, SIGINT, SIGHUP, SIGPIPE oder `systemctl stop` |
| Kein Neustart bei | Exit-Code 3 (`RestartPreventExitStatus=3`): Datenordner gesperrt oder Verbindung belegt (anderer Kern läuft) |
| Neu laden | SIGHUP bzw. `systemctl --user reload dss-core.service` |
| Protokoll | Journal: `journalctl --user -u dss-core.service` |
| Sperre | `<Datenordner>/kern.lock` (`QLockFile`, `staleLockTime` 0). Solange der Kern läuft, ändert nur er die Overlay-Dateien; `dss-cli` zeigt dann nur an und bricht Änderungen mit Exit-Code 3 ab |

Quellen: https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html (`Type=`, `Restart=`, `RestartSec=`, `RestartPreventExitStatus=`, `NotifyAccess=`), https://www.freedesktop.org/software/systemd/man/latest/systemd.unit.html, https://www.freedesktop.org/software/systemd/man/latest/sd_notify.html (`READY=1`, `STOPPING=1`, `NOTIFY_SOCKET`, eigene Umsetzung ohne libsystemd), https://doc.qt.io/qt-6.8/qlockfile.html.

Exit-Codes von `dss-core`: 0 = beendet (auch nach SIGTERM/SIGINT oder `quit_request`), 1 = Fehler (zum Beispiel Datenordner nicht lesbar, Pfad der Verbindung zu lang), 3 = anderer Kern läuft. Ein falscher Aufruf (unbekannte Option) endet ebenfalls mit 1 (geprüft 2026-11-02: `build/src/core/dss-core --gibt-es-nicht; echo $?` → 1).
