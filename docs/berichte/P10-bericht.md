# P10 – Bericht Browserquelle

Auftrag: `prompts/P10-browserquelle.md`. Ausgeführt am 2026-11-03/04 von Claude Code auf dem Studio-PC (Debian 13, X11, `DISPLAY=:0.0`). Stand: ausgeführt, Handtest offen.

Alle Messwerte stammen von diesem Gerät, mit Datum und Befehl. Was nur in OBS auf dem Streaming-PC prüfbar ist, ist «nicht geprüft» und steht im Handtest. Tokens sind in diesem Bericht durch `<token>` ersetzt.

## Kurzfassung

- **Overlay-Server im Kern** (`src/server/`, Schalter `DSS_CORE_OVERLAY_SERVER`): eigener HTTP/1.1-Server auf `QTcpServer`, nur IPv4 auf `127.0.0.1` und der Adresse im Heimnetz (`192.168.1.9`), Port aus `kern.ini`. Adresse `http://192.168.1.9:<Port>/o/<adresse>/<token>/`. Änderungen gehen als Server-Sent Events an die Seite, ohne Neuladen. Den Token vergleicht der Server mit gleichbleibender Laufzeit, ins Protokoll schreibt er ihn nie. Eine alte oder unbekannte Adresse bekommt eine leere, durchsichtige Seite, ins Protokoll kommt «unbekannte Adresse», und das Fenster zeigt einen Hinweis. Ist der Port belegt, nennt die Meldung das Programm. Fehlt die Adresse, lauscht der Server nur auf `127.0.0.1` und kommt von selbst auf die Adresse, sobald sie da ist.
- **Seite als zweiter Zeichner** (`web/overlay.html`, `web/overlay.js`): durchsichtig, gleiche Lage, Grösse, Reihenfolge, Spiegelung und Drehung wie der Editor. EXIF und Video-Drehung wendet die Seite selbst an, mit der Angabe des Kerns. Der Kern liefert die Dateien dafür mit neutraler Ausrichtung aus, damit das Ergebnis nicht von der Chromium-Version abhängt. Text kommt aus derselben Schriftdatei mit den Zeilenmassen wie Qt. Videos sind stumm und beginnen immer von vorne. Was die Seite nicht abspielen kann, bleibt leer und wird dem Kern gemeldet.
- **Ausgabe ein/aus** (E-24): ein Kästchen links neben jedem Overlay, gespeichert in der Overlay-Datei (Format 4).
- **Neuer Token** mit Rückfrage im Fenster und in `dss-cli`.
- **Dock «Steuerung»:** Adresse, «Kopieren», «Neuer Token», «Ausgabe: …». Meldungen des Servers erscheinen fett in der Warnfarbe im Dock und in der Statuszeile (Schalter `DSS_APP_BROWSER_SOURCE`).
- **Teil D:** Punkte 76, 77 und 78 sind umgesetzt. Die Schnittstelle Kern–Fenster steht auf **Version 9**.
- **Stufe 1 der Prüfung** (`tst_browserquelle`, Qt WebEngine 6.8.2 / Chromium 122 über `http://192.168.1.9:<Port>/`) besteht. Bilder, Spiegeln und Drehung weichen im Mittel um 0.02 bis 0.06 vom Editor ab, Text um 1.3 bis 1.9. Bei Videos ist die Abweichung grösser: Chromium rechnet die Farben um (Mittel 10.4, höchstens 95; `[OFFEN]`, Punkt 79). Eine Änderung braucht bis zur Seite im Median 21 bis 22 ms.
- **Videoformate in Chromium (Qt WebEngine 6.8.2):** H.264, VP9 (WebM und MP4), VP9 mit Alpha und AV1 laufen. HEVC, MPEG-4 Part 2 und ProRes laufen nicht (`[OFFEN]`, Punkt 81).

## Teil A – Overlay-Server im Kern

### Technik (Teil A 1 und 6)

Gewählt habe ich einen **eigenen kleinen HTTP/1.1-Server auf `QTcpServer`** mit **Server-Sent Events** für Änderungen. Begründung:

- Qt Network ist schon eingebunden, es braucht kein neues Paket (Regel 5). Qt HttpServer und Qt WebSockets sind nicht installiert: `dpkg -l | grep -iE "httpserver|websockets"` lieferte am 2026-11-04 nichts. Installiert sind nur die Pakete von Qt WebEngine 6.8.2+dfsg-4 (`dpkg -l | grep -i webengine`, 2026-11-04).
- Die Seite braucht nur Daten vom Kern zur Seite. Server-Sent Events (`EventSource`) sind dafür genau gemacht: Die Verbindung baut sich von selbst wieder auf (`retry: 1000`), und es braucht keinen eigenen WebSocket-Rahmen (https://html.spec.whatwg.org/multipage/server-sent-events.html).
- Der Server ist klein: GET, HEAD und POST, `Range`, Keep-Alive, Senden in Stücken. Geprüft wird er ohne Fenster mit `tst_overlayserver`.

Nur IPv4: je ein `QTcpServer` auf `127.0.0.1` und auf der Adresse im Heimnetz, nie `QHostAddress::Any`, weil das auch IPv6 annimmt (https://doc.qt.io/qt-6.8/qtcpserver.html#listen, https://doc.qt.io/qt-6.8/qhostaddress.html).

Gemessen am 2026-11-04 00:52 mit einem Testkern auf Port 24901 (eigener Datenordner und eigene `kern.ini`):

```
ss -ltnH "sport = :24901"     → 192.168.1.9:24901 und 127.0.0.1:24901 (LISTEN)
ss -ltn6H "sport = :24901"    → leer
curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:24901/     → 200
curl -s -o /dev/null -w "%{http_code}" http://192.168.1.9:24901/   → 200
curl http://[::1]:24901/                                           → Verbindung abgelehnt
```

Protokoll des Kerns dabei (Auszug): «Overlay-Server lauscht auf 127.0.0.1:24901», «Overlay-Server lauscht auf 192.168.1.9:24901», «Browserquelle: unbekannte Adresse /o/test-szene/… von 127.0.0.1», «SIGTERM erhalten, beende», «beendet, Exit-Code 0».

Pfade, Antworten, Ereignisse und Meldungen stehen in `docs/08-kern-schnittstelle.md`, Abschnitt «Overlay-Server».

### Port belegt (Teil A 2)

Kann der Server nicht lauschen, sucht der Kern das belegende Programm über `/proc/net/tcp` und `/proc/net/tcp6` (Inode des Sockets im Zustand LISTEN) und `/proc/<pid>/fd` sowie `/proc/<pid>/exe`. Die Meldung lautet zum Beispiel: «Port 8081 ist belegt, vermutlich von DoNs Streaming Studio in Arbeit (/home/dominik/DoNs_Streaming_Studio/build/src/core/dss-core) (Prozess …). Die Browserquelle ist nicht erreichbar. Anderen Port im Dialog «Einstellungen» wählen oder das Programm beenden.» Sie steht in `server_status` und im Protokoll. Alle 2 s versucht der Kern es neu. Geprüft wird das mit `tst_overlayserver` (nachgebautes `/proc` und echter belegter Port) und `tst_fensterbearbeiten::browserSource` (Meldung im Fenster).

### Adresse fehlt (Teil A 3)

Ist die Adresse leer oder gerade auf keiner Netzwerkkarte, lauscht der Server nur auf `127.0.0.1` und meldet das. Alle 2 s prüft er, ob die Adresse da ist, und lauscht dann von selbst auch dort. Geprüft wird das mit `tst_overlayserver`, wo eine Liste der lokalen Adressen im Test die Netzwerkkarte ersetzt. Ein echtes Ausstecken des Kabels habe ich nicht gemacht (nicht geprüft, das Netz des Nutzers sollte nicht unterbrochen werden).

### Token (Teil A 4)

Der Vergleich läuft über `constantTimeEquals` (`src/server/httpmessage.cpp`): Alle Bytes werden mit `volatile` verglichen, nach dem Vorbild von `sodium_memcmp` (https://libsodium.gitbook.io/doc/helpers#constant-time-test-for-equality). Fehlt ein Overlay, vergleicht der Server gegen einen Platzhalter mit 43 Zeichen. Ins Protokoll kommt nur `/o/<adresse>/…`. `tst_overlayserver` und `tst_browserquelle` (`cleanupTestCase`) prüfen, dass kein Token im Protokoll steht.

Beim Nachführen der Doku am 2026-11-04 fiel auf: Der Vergleich wurde übersprungen, wenn es kein Overlay mit der Adresse gab. Die Laufzeit verriet also, ob eine Adresse existiert, aber nichts über den Token. Das ist korrigiert, der Vergleich läuft jetzt immer (eigener Commit).

### Alte oder unbekannte Adresse (Teil A 5)

Eine alte oder unbekannte Adresse bekommt eine leere, durchsichtige Seite, die sich alle 10 s neu lädt; `…/events` bekommt `gone`. Ins Protokoll kommt höchstens einmal pro Minute je Gegenstelle und Adresse «Browserquelle: unbekannte Adresse /o/<adresse>/… von <IPv4>». Das Fenster zeigt 30 s lang den Hinweis, zum Beispiel in `tst_browserquelle` (2026-11-04): «Eine Browserquelle (192.168.1.9) benutzt eine ungültige Adresse (/o/pruefung-browserquelle/…). In OBS die Adresse aus «Kopieren» im Dock «Steuerung» eintragen.»

### Ausgabe ein/aus (Teil A 7, E-24)

Der Zustand wird **in der Overlay-Datei** gespeichert (Feld `output`, Format 4, umgewandelt nach den Regeln aus P10a, `docs/06-overlay-format.md`). Begründung: Der Zustand gehört zum Overlay, übersteht einen Neustart, und Editor, Kern und Seite haben denselben Stand. Eine eigene Datei bräuchte ein eigenes Format und ein eigenes Umwandeln. Die Änderung heisst `set_overlay_output`. Bei «aus» zeichnet die Seite nichts, bleibt aber verbunden.

### Neuer Token (Teil A 8)

Knopf im Dock «Steuerung» und `dss-cli neuer-token ADRESSE [--ja]`, beide mit der Rückfrage aus `src/common/browsersource.h` («Die alte Adresse funktioniert danach nicht mehr. In OBS muss die neue Adresse eingetragen werden. Fortfahren?»). Offene Seiten der alten Adresse bekommen `gone`.

## Teil B – Seite als zweiter Zeichner

### Ausrichtung (B 2)

Gemessen am 2026-11-03 mit einem eigenen Prüfprogramm (Qt WebEngine 6.8.2, `QtWebEngine/6.8.2 Chrome/122.0.6261.171`), mit `<img>` bzw. `<video>` ohne CSS-Ausrichtung:

| Datei | Chromium zeigt |
|---|---|
| JPEG mit EXIF 1 bis 8 | gedreht nach EXIF (alle richtig) |
| PNG mit `eXIf` 2 bis 8 | **nicht** gedreht (wie gespeichert) |
| H.264 in MP4 mit Display-Matrix 90/180/270 | gedreht (alle richtig) |
| VP9 in MP4 mit Display-Matrix 90 | **nicht** gedreht |
| VP9 in WebM | ohne Drehung (Testdatei ohne Angabe) |

Weil Chromium das je nach Format verschieden macht, setzt der Server die Angaben in den **ausgelieferten Bytes** auf «keine Drehung»: EXIF-Ausrichtung 1 (bei PNG mit neuer CRC) und Einheitsmatrix in `mvhd` und `tkhd`, bei gleicher Länge (`src/server/mediapatch.cpp`). Die Seite dreht dann selbst, nach `media[…].orientation` vom Kern. Das ist dieselbe Ausrichtung wie im Editor, in der Reihenfolge Ausrichtung → Strecken → Spiegeln → Drehung. Die Dateien im Datenordner bleiben unverändert. Quellen: EXIF 2.32 (https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf), PNG 3 (https://www.w3.org/TR/png-3/#eXIf, https://www.w3.org/TR/png-3/#samplecrc), FFmpeg n7.1 `mov.c` (https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libavformat/mov.c), CSS Transforms (https://drafts.csswg.org/css-transforms/#transform-rendering). Bei WebM/Matroska neutralisiert der Server nichts (`[OFFEN]`, Punkt 80).

### Text (B 2)

Text wird als SVG gezeichnet, mit derselben Schriftdatei (`FontFace`, https://drafts.csswg.org/css-font-loading/). Die Grundlinien kommen aus den Massen, die der Kern mitschickt (`units_per_em`, `ascent`, `descent`, `leading` aus `head`, `hhea` und `OS/2`, gerechnet wie Qt 6.8.2 `QFontEngine`; https://learn.microsoft.com/en-us/typography/opentype/spec/head, https://learn.microsoft.com/en-us/typography/opentype/spec/hhea, https://learn.microsoft.com/en-us/typography/opentype/spec/os2, https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/text/qfontengine.cpp?h=v6.8.2). Der Text ist am Kasten abgeschnitten und waagrecht ausgerichtet (https://www.w3.org/TR/SVG2/text.html).

### Video (B 2)

Videos sind stumm (`muted`) und beginnen von vorne: beim Einblenden, Laden, «Ausgabe ein» und bei einer anderen Datei. Das Ende ist wie im Editor (`loop`, `hold_at_end`, sonst leer; https://html.spec.whatwg.org/multipage/media.html).

### Wechsel des Overlays (B 3)

Im Editor beginnt ein Video nach dem Wechsel auf ein anderes Overlay und zurück **neu**, weil die Spieler neu angelegt werden. Neuer Slot `tst_leinwandbild::videoRestartsAfterOverlaySwitch`: Nach dem Zurückwechseln gilt `loops == 0`, und es sind weniger als 10 Bilder dekodiert. Festgelegt für beide Zeichner: Ein Video beginnt von vorne, wann immer seine Quelle wieder gezeigt wird. Der Wechsel im Editor wirkt nicht auf die Browserquelle. Festgehalten in `docs/01-festlegungen.md`, «Zeichnen im Editor». `tst_browserquelle::otherOverlayLeavesPageAlone` prüft, dass die Seite unverändert bleibt, wenn sich ein anderes Overlay ändert.

### Videoformate (B 4)

Gemessen am 2026-11-03/04 mit Qt WebEngine 6.8.2 (Chromium 122), Testvideos mit `ffmpeg` erzeugt, `<video muted autoplay>`:

| Format | Ergebnis |
|---|---|
| H.264 in MP4 | läuft (`canPlayType` «probably») |
| VP9 in WebM | läuft («probably») |
| VP9 in MP4 | läuft («probably») |
| VP9 mit Alpha in WebM (`prototypes/media/alpha.webm`) | läuft mit Durchsichtigkeit: Alpha im Mittel 53.13 (Editor 52.87), 1622 Pixel teilweise durchsichtig (Kasten 110x110, `tst_browserquelle::webmAlpha`) |
| AV1 in MP4 | läuft |
| HEVC (H.265) in MP4 | Fehler 4 «DEMUXER_ERROR_NO_SUPPORTED_STREAMS: FFmpegDemuxer: no supported streams», `canPlayType('video/mp4; codecs="hvc1"')` leer |
| MPEG-4 Part 2 in MP4 | ebenso Fehler 4, `canPlayType` für `mp4v.20.8` leer |
| ProRes in MOV | ebenso Fehler 4, `canPlayType` für `apcn` leer |

Nicht abspielbar: Die Quelle bleibt leer, und die Seite meldet zum Beispiel «Video nicht abspielbar: «name» (media/….mp4) (Fehler 4 …)» an `…/report`. Der Kern schreibt das ins Protokoll (`tst_browserquelle::unplayableVideo` mit MPEG-4 Part 2). `[OFFEN]`: was mit solchen Dateien geschehen soll (Punkt 81) und ob das Chromium der Browserquelle in OBS dieselben Formate kann (nicht geprüft).

Farben von Videos: H.264 ohne Farbangaben behandelt Chromium wie `smpte170m` (BT.601) und rechnet die Grundfarben um. Gemessen in den Vierteln der Drehvideos: Seite Rot 248/36/0, Grün 64/252/0, Blau 27/34/255. Mit der Kennung BT.709 waren es 255/24/0, 0/216/0, 0/14/255. Der Editor (swscale) rechnet nicht um. Deshalb weicht das Video deutlicher ab als Bilder (unten). `[OFFEN]`, Punkt 79.

## Teil C – Fenster

- Dock «Steuerung», Zeile 1: Feld «Browserquelle» (nur lesbar, Tooltip mit der ganzen Adresse), «Kopieren» (Zwischenablage), «Neuer Token», «Ausgabe: ein, 1 Browserquelle verbunden» bzw. «Ausgabe: aus (Browserquelle leer)». Ohne gewähltes Overlay ausgegraut, Platzhalter «Kein Overlay gewählt».
- Meldungen des Servers (Port belegt, Adresse fehlt, ungültige Adresse benutzt): im Dock unter der Adresse mit Zeilenumbruch und in der Statuszeile rechts (gekürzt auf 90 Zeichen, «(+N)» bei weiteren), fett in der Warnfarbe.
- Overlay-Liste: Kästchen «Ausgabe» links neben jedem Overlay (im Einzug, an der Stelle aus Punkt 48), Klick schaltet um.
- Texte und Aufbereitung ohne Qt Widgets in `src/common/windowdata.h/.cpp` (`tst_windowdata`), Bedienung in `tst_fensterbearbeiten::browserSource`: Kopieren, Rückfrage beim neuen Token, Kästchen, Port belegt.
- Bildschirmfoto des Fensters am 2026-11-04 mit einem Testkern (nur zur eigenen Kontrolle, nicht im Repo, weil es einen Test-Token zeigt): Dock und Kästchen ohne Überlappung. Ob es dem Nutzer gefällt, prüft der Handtest.

## Teil D – Nacharbeiten

- **Punkt 78:** `update_settings` nimmt nur Adressen an, die eine Netzwerkkarte dieses PCs gerade hat (`QNetworkInterface::allAddresses`, https://doc.qt.io/qt-6.8/qnetworkinterface.html). Aufgezeichnet am 2026-11-04 mit einem Testkern: `192.168.1.1` → `rejected`, «192.168.1.1 gehört nicht zu diesem PC. Adressen dieses PCs: 192.168.1.9.». Die bisherige Adresse darf bleiben, auch wenn sie gerade fehlt.
- **Punkt 76:** Antworten auf `update_settings` und `reset_settings` gehen im `CoreClient` über das eigene Signal `settingsUpdateFinished` nur an den Dialog, nicht in die Statuszeile.
- **Punkt 77:** Knopf «Auf Standardwerte zurücksetzen» (nur bei ungültiger `kern.ini` frei) und Nachricht `reset_settings`. Der Kern benennt die Datei in `kern.ini.ungueltig-<JJJJ-MM-TT_hh-mm-ss>` um, schreibt die Standardwerte, trägt die Adresse aus der Standardroute ein und meldet «Auf Standardwerte zurückgesetzt. Die ungültige Datei heisst jetzt …». Bei einer gültigen Datei: `rejected` «… ist gültig; nichts zurückgesetzt.».

## Stufe 1 der Prüfung (`tst_browserquelle`)

Aufbau: Ein Testkern mit eigenem Datenordner und eigenem Port (24000 + PID % 2000 · 4 + 2). Ein `QWebEngineView` mit durchsichtigem Hintergrund (https://doc.qt.io/qt-6.8/qwebenginepage.html#backgroundColor-prop, https://doc.qt.io/qt-6.8/qwebengineview.html) lädt `http://192.168.1.9:<Port>/o/…/<token>/`, also über die echte Netzwerkkarte. Das Bild mit Alpha kommt über `QQuickWidget::grabFramebuffer` (Weg aus P02, M2; https://doc.qt.io/qt-6.8/qquickwidget.html#grabFramebuffer). Verglichen wird mit `dss_render` (gleiches Overlay, gleiche Dateien). Test-Overlay 800x450: 8 JPEG und 8 PNG aus `tests/daten/exif/` (je 88x44), die 4 Drehvideos aus `tests/daten/video-drehung/`, Text DejaVu Sans 36 px zentriert (#FFE040), Text Be Vietnam Pro Bold 30 px rechtsbündig, ein gespiegeltes PNG, ein PNG um 12.5° gedreht, ein Video mit Drehung 90° in der Datei, das zusätzlich um 300° gedreht und senkrecht gespiegelt ist. Ohne X11 oder ohne Adresse im Heimnetz endet der Test mit 77 (übersprungen).

Abweichung Seite gegen Editor, je Kanal 0 bis 255 (Mittel / grösste / Anteil Pixel mit Abweichung > 32), Lauf vom 2026-11-04:

| Bereich | Mittel | grösste | > 32 |
|---|---|---|---|
| ganzes Bild | 1.26 | 179 | 6.27 % |
| JPEG (EXIF 1 bis 8) | 0.03 | 2 | 0 % |
| PNG (EXIF 1 bis 8) | 0.03 | 2 | 0 % |
| Videos (Drehung 0/90/180/270) | 10.38 | 95 | 67.26 % |
| Text DejaVu Sans | 1.85 | 179 | 3.02 % |
| Text Be Vietnam Pro | 1.31 | 127 | 2.32 % |
| PNG gespiegelt | 0.02 | 2 | 0 % |
| PNG um 12.5° gedreht | 0.06 | 59 | 0.01 % |
| Video gedreht und gespiegelt | 3.83 | 134 | 22.62 % |

Deckende Pixel des Textes: Seite 3863, Editor 3891. Die Abweichungen beim Text sitzen an den Kanten der Buchstaben (Kantenglättung). Bei den Videos kommen sie von der Farbumrechnung (oben). Die Richtung jedes Viertels prüft der Test über die nächstliegende Grundfarbe. Die feste Grenze kommt mit P11.

| Prüfung | Ergebnis (2026-11-04) |
|---|---|
| Änderung im Kern bis zur Seite (`window.dssState.applied`, abgefragt per `runJavaScript`) | Median 21 bis 22 ms, höchstens 23 ms |
| Änderung bis zum abgegriffenen Bild | Median 23 bis 39 ms, höchstens 56 ms |
| Ausgabe aus → Bild ganz durchsichtig; wieder ein → Videos bei ≤ 0.004 s | bestanden |
| Quelle aus- und wieder eingeblendet → Video bei 0.008 s | bestanden |
| Neuer Token: alte Adresse leer und durchsichtig, Hinweis im Fenster, neue Adresse zeigt das Overlay | bestanden |
| Kern beendet: Bild unverändert (grösste Abweichung 0); Kern neu gestartet: wieder verbunden nach 417 bis 490 ms | bestanden |
| Anderes Overlay geändert: Seite unverändert | bestanden |

## Last

`DSS_BROWSERQUELLE_LAST=1 build/tests/tst_browserquelle load`, 2026-11-04 00:26: Overlay 1920x1080 mit 10 Quellen, darunter 2 Videos 1920x1080 mit 30 fps (VP9 in WebM und H.264 in MP4, je 20 s, mit `ffmpeg` erzeugt), die übrigen Bilder und Texte. Gemessen über 30 s aus `/proc/<pid>/stat` und `/proc/<pid>/status` (https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html). Die Bilder der Videos zählt `getVideoPlaybackQuality` (https://developer.mozilla.org/en-US/docs/Web/API/HTMLVideoElement/getVideoPlaybackQuality).

| Prozess | CPU (eines Kerns) | RSS |
|---|---|---|
| `dss-core` | 0.2 % | 63 MiB |
| Testprogramm (`tst_browserquelle`, ohne Chromium) | 32.3 % | 427 MiB |
| alle `QtWebEngineProcess` | 101.4 % | 548 MiB |

Videos: 1049 bzw. 1046 Bilder gezeigt, davon 5 bzw. 4 verworfen. Der Kern trägt also fast nichts zur Last bei, sie liegt beim Zeichnen in Chromium. Wie sich das Chromium von OBS auf dem Streaming-PC verhält: nicht geprüft.

## Firewall

Geprüft am 2026-11-04 00:51, nichts geändert:

```
dpkg -l nftables ufw firewalld iptables     → nur nftables 1.1.3-1 installiert
systemctl is-active nftables; systemctl is-enabled nftables   → inactive, disabled
cat /etc/nftables.conf                      → Tabelle inet filter mit leeren Ketten input/forward/output, ohne Regel
lsmod | grep -E "nf_tables|ip_tables"       → nf_tables nicht geladen, ip_tables mit 0 Nutzern
```

`nft list ruleset` braucht root. Die **aktiven Regeln sind deshalb nicht geprüft**. Weil das Modul `nf_tables` nicht geladen ist, gibt es vermutlich keine (Annahme). Der Zugriff über `192.168.1.9` vom Studio-PC selbst ging (siehe oben). Ob der Streaming-PC durchkommt, zeigt erst der Handtest.

## Ausgeführte Befehle und Ergebnisse

### Bau

```
cmake -S . -B build && cmake --build build -j8                          # Debug mit -Wall -Wextra (dss_warnings), 0 Warnungen
docs/berichte/P10-daten/schalter-lauf.sh <Notizordner>/p10/schalter     # frischer Klon von 68d4d8c, 2026-11-04 01:32:23 bis 01:59:56
```

Ergebnis (`schalter-lauf.sh`, alle mit `-DCMAKE_CXX_FLAGS="-Wall -Wextra"`):

| Bau | Ergebnis |
|---|---|
| Release, frischer Klon | Exit 0, 0 Warnungen, `ctest` **37 von 37** |
| Debug, frischer Klon | Exit 0, 0 Warnungen, `ctest` **37 von 37** |
| `DSS_BUILD_APP`, `DSS_BUILD_TESTS`, `DSS_BUILD_CLI`, `DSS_MEDIA_SIZE`, `DSS_APP_RENDERER`, `DSS_APP_CANVAS_EDIT`, `DSS_VIDEO_ROTATION`, `DSS_CORE_WATCHDOG`, `DSS_APP_EDITING`, `DSS_APP_TRANSFORM`, `DSS_CORE_IMPORT_THREAD`, `DSS_APP_SETTINGS_DIALOG`, `DSS_APP_CANVAS_IMAGE` je aus | je Exit 0, 0 Warnungen (nur gebaut) |
| `DSS_APP_BROWSER_SOURCE=OFF` | Exit 0, 0 Warnungen, `ctest` 37 von 37 |
| `DSS_CORE_OVERLAY_SERVER=OFF` | Exit 0, 0 Warnungen, `ctest` 34 von 35: `tst_fensterbearbeiten::browserSource` erwartete auch ohne Server keine Meldung und prüfte «Port belegt». Den Test habe ich an den Schalter angepasst (`327dfc6`). Danach lief der Bau aus dem Arbeitsordner mit `DSS_CORE_OVERLAY_SERVER=OFF` (2026-11-04 02:03) mit 0 Warnungen und `ctest` **35 von 35**. `tst_overlayserver` und `tst_browserquelle` werden ohne Server nicht gebaut |
| `DSS_INSTALLED_EDITION=ON` | Exit 0, 0 Warnungen |

Nach `327dfc6`: `ctest --test-dir build -j4` 2026-11-04 02:04, **37 von 37**, 0 Warnungen.

### Installation (nur mit Testoptionen, Regel 18)

```
docs/berichte/P10-daten/installieren-pruefen.sh <Notizordner>/p10/inst 24950
```

2026-11-04 02:06:01 bis 02:06:52, Ergebnis in `docs/berichte/P10-daten/installieren-ergebnis.txt`. `scripts/installieren.sh` lief mit `--ziel`, `--datenordner` (Kopie von `tests/daten/formate`), `--einheit dss-core-installtest.service`, `--menue-ordner`, `--symbol-ordner`: Exit 0, Sicherung angelegt, 3 Dateien umgewandelt, danach alle 4 im Format 4. `share/dons-streaming-studio/web/` enthält `overlay.html` und `overlay.js` (gleich wie im Repo). Das installierte `dss-core` (eigene `kern.ini` mit Port 24950) lauschte auf `127.0.0.1:24950` und `192.168.1.9:24950`. Die Seite lieferte 200 (gleich `web/overlay.html`), `overlay.js` 200, `events` das Ereignis `overlay`, ein falscher Token die leere Seite. Kein Token im Protokoll, Exit 0. Die Testeinheit ist wieder entfernt. «Commit 327dfc6-geändert» heisst: Im Arbeitsordner lagen die Dokumente noch nicht committet. Der erste Versuch (02:04) scheiterte am Prüfskript selbst: Der Pfad des Sockets im Notizordner war mit 119 Byte zu lang (höchstens 107). Danach nutzte das Skript einen kurzen Pfad unter `/run/user/1000/`.

### Tests

| Lauf | Datum | Ergebnis |
|---|---|---|
| `ctest --test-dir build -j4`, vor Teil D | 2026-11-04 | 37 von 37 |
| `tst_medien` 50 Mal (`ctest --test-dir build -R '^tst_medien$'`), Stand `1516383` | 2026-11-04 00:51 bis 01:09 | **50 von 50** |
| `tst_fensterbearbeiten` 50 Mal, Stand `1516383` | 2026-11-04 01:09 bis 01:28 | **50 von 50** |
| Startprüfung `--runs 20`, Stand `1516383` mit `scripts/startpruefung.py` wie in `68d4d8c` | 2026-11-04 01:28 bis 01:31 | 20 von 20 in allen 4 Fällen |
| `ctest --test-dir build -j4` nach `bb28a6f` (Token immer vergleichen) | 2026-11-04 01:31 bis 01:32 | **37 von 37**, 0 Warnungen beim Bau |
| `DSS_BROWSERQUELLE_LAST=1 build/tests/tst_browserquelle load` | 2026-11-04 00:26 | bestanden (Werte unter «Last») |

Die 50 Läufe und die Startprüfung liefen mit dem Stand vor `bb28a6f`. Diese Änderung betrifft nur den Vergleich des Tokens im Server (zwei Zeilen), danach liefen `ctest` und die Klone unten.

### Startprüfung

```
cd ~/DoNs_Streaming_Studio && python3 scripts/startpruefung.py --runs 20 --out <Notizordner>/p10/laeufe/startpruefung
```

2026-11-04 01:28:50 bis 01:31:04, `DISPLAY=:0.0`, dss-app mit eigenem `XDG_CONFIG_HOME` je Fall, also auch mit eigener `kern.ini` (Port 8081 frei, `dss-core-entwicklung.service` lief nicht). Neu erwartet: «Overlay-Server lauscht auf». Ergebnisse in `docs/berichte/P10-daten/startpruefung-*.md` und `.json`.

| Fall | sauber | Start ms (min / Median / max) | Ende ms (min / Median / max) |
|---|---|---|---|
| `core-sigterm` | 20 von 20 | 36,8 / 46,9 / 47,5 | 3,6 / 4,0 / 4,4 |
| `core-sigint` | 20 von 20 | 37,8 / 45,9 / 47,6 | 3,6 / 4,0 / 4,4 |
| `app-sigterm` | 20 von 20 | 219,1 / 227,6 / 245,1 | 13,1 / 14,8 / 18,0 |
| `app-kern-sigterm` | 20 von 20 | 220,0 / 229,9 / 239,7 | 15,1 / 17,0 / 21,1 |

### Prüfsummen

```
cd ~ && find .local/opt/dons-streaming-studio .local/share/dons-streaming-studio -type f -print0 | sort -z | xargs -0 sha256sum
```

Vorher 2026-11-03 23:38:33, nachher 2026-11-04 02:07:09: **gleich**, 79 Dateien (66 in `~/.local/opt/dons-streaming-studio`, 13 in `~/.local/share/dons-streaming-studio`). `~/.config/dons-streaming-studio/fenster.ini` hat den Stand vom 2026-11-03 22:55:50, also von vor dieser Sitzung. Installiert habe ich nichts, auch kein Paket.

## Handtest

Grund (Regel 10): Das Aussehen in OBS auf dem Streaming-PC, das Verhalten des Chromium in OBS, das Bildschirmfoto von OBS und die echte Installation (Regel 18) kann nur der Nutzer prüfen. Was Stufe 1 auf dem Studio-PC schon geprüft hat (Pixel, Zeiten, Token, Wiederverbinden, Protokoll), muss der Nutzer nicht wiederholen. Die Prüfungen 1 bis 3 nutzen die Version aus dem Repo (Port 8081) und den Entwicklungsordner. Befehle auf dem Studio-PC laufen im Terminal der Xfce-Sitzung. Vorher alle Fenster von DoNs Streaming Studio schliessen.

### Prüfung 1 – Test-Overlay in OBS, Vergleich mit der Leinwand

```
cd ~/DoNs_Streaming_Studio && cmake --build build -j8 && mkdir -p build/handtest-p10 && ffmpeg -loglevel error -y -f lavfi -i testsrc=size=640x360:rate=30:duration=30 -c:v libx264 -pix_fmt yuv420p build/handtest-p10/zaehler.mp4 && build/src/cli/dss-cli overlay-neu "Handtest P10" 1920 1080 && build/src/cli/dss-cli quelle-bild handtest-p10 tests/daten/exif/orientierung-6.jpg && build/src/cli/dss-cli quelle-video handtest-p10 tests/daten/video-drehung/drehung-90.mp4 --wiederholen && build/src/cli/dss-cli quelle-text handtest-p10 "Willkommen" /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf && build/src/cli/dss-cli quelle-bild handtest-p10 tests/daten/exif/orientierung-1.png && build/src/cli/dss-cli quelle-video handtest-p10 build/handtest-p10/zaehler.mp4 --wiederholen && build/src/cli/dss-cli quelle-geometrie handtest-p10 1 40 40 640 320 0 && build/src/cli/dss-cli quelle-geometrie handtest-p10 2 720 40 640 320 0 && build/src/cli/dss-cli quelle-geometrie handtest-p10 3 40 420 1200 160 0 && build/src/cli/dss-cli quelle-geometrie handtest-p10 4 1400 60 320 160 12.5 --waagrecht-gespiegelt && build/src/cli/dss-cli quelle-geometrie handtest-p10 5 1240 640 640 360 0 && build/src/app/dss-app
```

Damit entsteht im Entwicklungsordner das Overlay «Handtest P10»: Bild mit EXIF 6, Video mit Drehung 90° in der Datei, Text, ein gespiegeltes und um 12.5° gedrehtes Bild und ein Video mit laufendem Zähler (`testsrc` von ffmpeg, https://ffmpeg.org/ffmpeg-filters.html). Danach startet das Fenster «DoNs Streaming Studio – Entwicklung». Läuft `dss-core-entwicklung.service` schon, verweigert `dss-cli` das Anlegen (Exit 3). Dann zuerst `cd ~/DoNs_Streaming_Studio && systemctl --user stop dss-core-entwicklung.service` und den Befehl nochmals ausführen.

1. Im Dock «Overlays» «Handtest P10» wählen. Erwartet im Dock «Steuerung»: Im Feld «Browserquelle» steht `http://192.168.1.9:8081/o/handtest-p10/…/` (die ganze Adresse zeigt der Tooltip), daneben «Ausgabe: ein, keine Browserquelle verbunden». In der Statuszeile steht keine rote Meldung. Links neben «Handtest P10» ist ein angekreuztes Kästchen.
2. «Kopieren» drücken. **Weg der Adresse zum Streaming-PC:** Die Zwischenablage gilt nur auf dem Studio-PC. Vorschlag über SSH (auf dem Studio-PC läuft `openssh-server`, geprüft am 2026-11-04; ob die Anmeldung vom Streaming-PC klappt, ist nicht geprüft). Auf dem Studio-PC:

```
cd ~/DoNs_Streaming_Studio && mousepad ~/browserquelle.txt
```

   Mit Strg+V einfügen, speichern, Mousepad schliessen. Dann **auf dem Streaming-PC** in einem Terminal (fragt beim ersten Mal nach dem Schlüssel des Studio-PCs, «yes», danach nach dem Passwort von `dominik` auf dem Studio-PC):

```
ssh dominik@192.168.1.9 cat browserquelle.txt
```

   Die angezeigte Zeile markieren und kopieren. Wer einen anderen Weg hat (Nachricht an sich selbst, abtippen), nimmt diesen. Prüfung 5 löscht die Datei wieder.
3. **Auf dem Streaming-PC in OBS:** in einer Szene unter «Quellen» «+» → «Browser», Name «Handtest P10», URL einfügen, Breite 1920, Höhe 1080, OK. Erwartet: OBS zeigt dasselbe wie die Leinwand im Editor. Links oben das Bild (rot und grün oben, blau und gelb unten), daneben das Video mit demselben Bild, darunter «Willkommen», rechts oben das gespiegelte und leicht gedrehte Bild (grün und rot oben, gelb und blau unten, gleich schräg wie im Editor), rechts unten der laufende Zähler. Wo keine Quelle ist, sieht man, was in der Szene darunter liegt (durchsichtig). Im Editor zeigt das Dock «Steuerung» jetzt «1 Browserquelle verbunden». Kleine Unterschiede der Farbe beim Video sind erwartet (Punkt 79).
4. **Auf dem Streaming-PC in OBS:** Rechtsklick auf die Quelle «Handtest P10» → «Bildschirmfoto (Quelle)». Laut Quellcode von OBS 30.2.3 landet das Bild im Aufnahmeordner (Einstellungen → Ausgabe → Aufnahmepfad), nicht geprüft. Im Editor Strg+Umschalt+B drücken (das Bild landet in `~/Bilder/DoNs Streaming Studio/`). Das Bild von OBS auf den Studio-PC holen, **auf dem Streaming-PC**:

```
scp "<Pfad des Bildschirmfotos von OBS>" dominik@192.168.1.9:Bilder/
```

   Dann auf dem Studio-PC beide Bilder ansehen:

```
cd ~/DoNs_Streaming_Studio && firefox "$(ls -t ~/Bilder/*.png | head -1)" "$(ls -t ~/Bilder/DoNs\ Streaming\ Studio/*.png | head -1)"
```

   Erwartet: Beide Bilder sind 1920 × 1080 (Titel in Firefox) und zeigen dieselben Quellen an denselben Stellen. Die Videos können ein anderes Bild zeigen. Bitte festhalten: **Behält das Bild von OBS die Durchsichtigkeit?** Durchsichtig heisst, man sieht dort den Hintergrund von Firefox wie beim Bild des Editors; nicht durchsichtig heisst schwarz oder eine andere feste Farbe. Laut Quellcode von OBS 30.2.3 erwartet: nicht durchsichtig. Dazu die Version von OBS notieren (OBS → «Hilfe» → «Über»).

Fenster und OBS offen lassen für Prüfung 2.

### Prüfung 2 – Änderung ohne Neuladen, Ausgabe aus und ein, Video von vorne

1. Im Editor «Willkommen» mit der Maus an eine andere Stelle ziehen. Erwartet in OBS: Der Text springt sofort (unter 1 s) an die neue Stelle, ohne dass die Seite neu lädt. Der Zähler läuft dabei ohne Sprung weiter.
2. Im Dock «Quellen» bei «zaehler.mp4» auf das Auge klicken (ausblenden), einige Sekunden warten, wieder einblenden. Erwartet in OBS: Der Zähler verschwindet und beginnt beim Einblenden bei 0.
3. Im Dock «Overlays» auf das Kästchen links neben «Handtest P10» klicken. Erwartet: Das Kästchen ist leer, im Dock «Steuerung» steht «Ausgabe: aus (Browserquelle leer), 1 Browserquelle verbunden». In OBS ist die Quelle ganz leer und durchsichtig. Der Editor zeigt das Overlay weiter und lässt sich bearbeiten.
4. Nochmals auf das Kästchen klicken. Erwartet: «Ausgabe: ein, 1 Browserquelle verbunden», in OBS ist alles wieder da, beide Videos beginnen von vorne (Zähler bei 0).

### Prüfung 3 – Neuer Token

1. Im Dock «Steuerung» «Neuer Token» drücken. Erwartet: Rückfrage «Die alte Adresse funktioniert danach nicht mehr. In OBS muss die neue Adresse eingetragen werden. Fortfahren?». «Ja» wählen.
2. Erwartet: In OBS wird die Quelle leer und durchsichtig, ohne Fehlertext. Im Editor erscheinen im Dock «Steuerung» und rechts in der Statuszeile in roter, fetter Schrift der Hinweis «Eine Browserquelle (192.168.1.116) benutzt eine ungültige Adresse (/o/handtest-p10/…). In OBS die Adresse aus «Kopieren» im Dock «Steuerung» eintragen.» Im Feld «Browserquelle» steht eine andere Adresse als vorher.
3. Die neue Adresse wie in Prüfung 1 Schritt 2 auf den Streaming-PC bringen (Mousepad: alten Inhalt ersetzen) und in OBS in den Eigenschaften der Quelle «Handtest P10» als URL eintragen. Erwartet: Das Overlay ist wieder da. Der Hinweis im Editor verschwindet spätestens 30 s nach der letzten Anfrage mit der alten Adresse.

Das Fenster schliessen.

### Prüfung 4 – Echte Installation und Port 8080

Nur der Nutzer installiert (Regel 18). Die echten Daten sind im Format 3. Das Skript sichert sie zuerst und wandelt sie dann in Format 4 um (Ausgabe bei allen Overlays «ein»). Das Fenster der installierten Version muss geschlossen sein.

```
cd ~/DoNs_Streaming_Studio && scripts/installieren.sh
```

Erwartet: am Ende «installiert» mit Version und Commit, vorher ein Hinweis auf die Sicherung unter `~/.local/share/dons-streaming-studio-sicherungen/<Datum_Zeit>/` und das Umwandeln. Dann «DoNs Streaming Studio» aus dem Anwendungsmenü starten (Titel ohne «Entwicklung»). Ein echtes Overlay wählen. Erwartet im Dock «Steuerung»: `http://192.168.1.9:8080/o/<adresse>/…/`, ohne rote Meldung (insbesondere nicht «Port 8080 ist belegt»). Die Adresse wie in Prüfung 1 auf den Streaming-PC bringen und **in OBS auf dem Streaming-PC** als Browserquelle eintragen (Breite und Höhe wie das Overlay). Erwartet: OBS zeigt dasselbe wie die Leinwand. Eine kleine Änderung im Editor erscheint in OBS ohne Neuladen. Diese Browserquelle bleibt für den Stream.

### Prüfung 5 – Aufräumen

**Auf dem Streaming-PC in OBS:** die Quelle «Handtest P10» löschen und das Bildschirmfoto aus Prüfung 1 löschen, falls nicht mehr gebraucht. Auf dem Studio-PC das Fenster der Entwicklung schliessen (falls noch offen), dann:

```
cd ~/DoNs_Streaming_Studio && systemctl --user stop dss-core-entwicklung.service; rm -rf ~/.local/share/dons-streaming-studio-entwicklung ~/.config/dons-streaming-studio-entwicklung build/handtest-p10 ~/browserquelle.txt && ls ~/.local/share/ ~/.config/ | grep dons-streaming-studio
```

Erwartet: Es bleiben `dons-streaming-studio` (echte Daten, in `~/.local/share/` und in `~/.config/`) und `dons-streaming-studio-sicherungen`. Die Bilder aus Prüfung 1 liegen in `~/Bilder/` und `~/Bilder/DoNs Streaming Studio/` und können gelöscht werden, wenn sie nicht mehr gebraucht werden.

## Commits

- `f5c5538` P10: Overlay-Format 4 mit Ausgabe ein/aus (E-24), Schnittstelle 6
- `0b2ff81` P10: Neuer Token und Adresse der Browserquelle, Schnittstelle 7
- `9316097` P10: Grundlagen des zweiten Zeichners: Lage der EXIF-Angabe, Zeilenmasse wie Qt
- `48dbb21` P10: Seite der Browserquelle als zweiter Zeichner (Teil B)
- `8f28fd1` P10: Overlay-Server im Kern (Teil A), Schnittstelle 8
- `253a1e5` P10: Stufe 1 der Prüfung mit Qt WebEngine, Video beim Wechsel des Overlays
- `1fa87ad` P10: Browserquelle im Fenster (Teil C)
- `345e8af` P10: Nacharbeiten zu den Einstellungen (Punkte 76 bis 78), Schnittstelle 9
- `9958676` P10: Token immer vergleichen, auch ohne Overlay zur Adresse
- `1a33c89` P10: Startprüfung erwartet «Overlay-Server lauscht auf» im Protokoll des Kerns
- `ada65f1` P10: tst_fensterbearbeiten besteht auch mit DSS_CORE_OVERLAY_SERVER=OFF
- `033bb32` P10: Bericht, Dokumente nachgeführt, Messdaten
- (dieser Commit) P10: Bericht, Kennungen der Commits nach dem Rebase

Beim `git pull --rebase origin main` lag auf dem Server `6c047a8` («P10 gestartet», nur `docs/00-uebergabe.md` und `docs/07-bauplan.md`). Die Commits oben sind darauf neu aufgesetzt. Den Konflikt in `docs/07-bauplan.md` habe ich so gelöst: Die Zeile P10 unter «Geplant» fällt weg, weil P10 jetzt unter «Erledigt» steht. Die übrigen Teile dieses Berichts und die Messdaten nennen die Kennungen vor dem Rebase: `f84ba9f` = `f5c5538`, `a07fd5b` = `0b2ff81`, `50d5dbc` = `9316097`, `98e8f04` = `48dbb21`, `a0b5057` = `8f28fd1`, `88e2cf3` = `253a1e5`, `d07789a` = `1fa87ad`, `1516383` = `345e8af`, `bb28a6f` = `9958676`, `68d4d8c` = `1a33c89`, `327dfc6` = `ada65f1`. Der Code ist derselbe (`git diff 327dfc6 ada65f1 -- . ':!docs'` leer).

## Geänderte und neue Dateien

- Neu: `src/server/` (`CMakeLists.txt`, `httpmessage.h/.cpp`, `mediapatch.h/.cpp`, `overlayserver.h/.cpp`, `portowner.h/.cpp`), `src/common/browsersource.h/.cpp`, `web/overlay.html`, `web/overlay.js`, `tests/tst_overlayserver.cpp`, `tests/tst_browserquelle.cpp`, `tests/daten/formate/` (Overlay im Format 3 mit Token zum Umwandeln), `docs/berichte/P10-bericht.md`, `docs/berichte/P10-daten/` (`schalter-lauf.sh`, `schalter-ergebnis.txt`, `installieren-pruefen.sh`, `installieren-ergebnis.txt`, `startpruefung-*.md` und `.json`).
- Geändert: `CMakeLists.txt`, `README.md`, `scripts/startpruefung.py`, `src/app/` (`CMakeLists.txt`, `docks.h/.cpp`, `dockviews.h/.cpp`, `mainwindow.h/.cpp`, `mainwindowedit.cpp`, `settingsdialog.h/.cpp`, `theme.cpp`), `src/cli/main.cpp`, `src/common/` (`CMakeLists.txt`, `coreclient.h/.cpp`, `coresettings.h/.cpp`, `imageorientation.h/.cpp`, `overlay.h`, `overlayjson.h/.cpp`, `overlaystore.h/.cpp`, `protocol.h/.cpp`, `sourcegeometry.h/.cpp`, `windowdata.h/.cpp`), `src/core/` (`CMakeLists.txt`, `coreservice.h/.cpp`, `main.cpp`), `src/media/mediainfo.h/.cpp`, `tests/` (`CMakeLists.txt`, `prozesshilfe.h`, `tst_aenderungen.cpp`, `tst_bearbeiten.cpp`, `tst_datenformat.cpp`, `tst_fensterbearbeiten.cpp`, `tst_imageorientation.cpp`, `tst_kerneinstellungen.cpp`, `tst_kernverbindung.cpp`, `tst_leinwandbild.cpp`, `tst_overlaymodel.cpp`, `tst_overlaystore.cpp`, `tst_protocol.cpp`, `tst_sourcegeometry.cpp`, `tst_sperre.cpp`, `tst_windowdata.cpp`), `docs/01-festlegungen.md`, `docs/04-offene-punkte.md`, `docs/06-overlay-format.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`.
- Ausserhalb des Repos (nur Tests, wieder entfernt): Testkerne mit Datenordnern und `kern.ini` in temporären Ordnern und im Notizordner der Sitzung, Sockets unter `/run/user/1000/`. Die echten Daten, `~/.local/opt` und `~/.config/dons-streaming-studio*` habe ich nicht berührt (Prüfsummen unten).

## Nicht erledigt

- Handtest (OBS auf dem Streaming-PC, echte Installation).
- Aktive Regeln der Firewall (`nft list ruleset` braucht root).
- Echtes Ausstecken des Kabels für «Adresse fehlt»; geprüft nur mit einer Liste der lokalen Adressen im Test.
- Drehung von WebM-Videos (Punkt 80), Farben von Videos (Punkt 79), nicht abspielbare Formate (Punkt 81).

## Annahmen

- Ausgabe ein/aus wird in der Overlay-Datei gespeichert (Begründung in Teil A 7).
- Eine leere Seite lädt sich alle 10 s neu, damit OBS nach dem Eintragen der richtigen Adresse ohne Zutun lädt. Den Abstand von 10 s habe ich gewählt.
- Den Hinweis «ungültige Adresse benutzt» zeigt das Fenster 30 s lang, ins Protokoll kommt höchstens einmal pro Minute je Gegenstelle. Beide Werte habe ich gewählt.
- Meldungen der Seite (`report`): höchstens 20 pro Minute zu je 300 Zeichen, gewählt gegen ein volles Protokoll.
- `overlay.html` und `overlay.js` gehen mit `no-cache` hinaus, Medien mit `immutable`, weil ihre Namen nach dem Inhalt gebildet werden.
- Teil D ist ein Commit (Punkt 82).
- Firewall: Ohne geladenes `nf_tables` gibt es vermutlich keine aktiven Regeln (nicht geprüft).

## `[OFFEN]`

- Punkt 79: Chromium rechnet die Farben von Videos ohne Farbangaben um, der Editor nicht (Abweichung im Mittel 10.4, höchstens 95).
- Punkt 80: Drehung von WebM/Matroska nicht neutralisiert und nicht geprüft.
- Punkt 81: HEVC, MPEG-4 Part 2 und ProRes laufen in Chromium (Qt WebEngine 6.8.2) nicht. Unklar ist, was mit solchen Dateien geschehen soll und ob das Chromium von OBS sie kann.
- Punkt 83: Nach dem Zurücksetzen lauschen Testkerne und die Startprüfung auf dem Standardport 8081. Läuft `dss-core-entwicklung.service`, meldet die Startprüfung «nicht sauber».
- OBS speichert ein «Bildschirmfoto (Quelle)» laut Quellcode von OBS 30.2.3 als `QImage::Format_RGBX8888`, also **ohne** Alpha. Das gilt nicht für HDR, das als JXR mit `GS_RGBA16F` gespeichert wird. Quelle: https://sources.debian.org/src/obs-studio/30.2.3%2Bdfsg-3/UI/window-basic-main-screenshot.cpp. Welche Version von OBS auf dem Streaming-PC läuft und wie sie sich verhält, prüft der Handtest.
- Wie die Adresse mit Token zum Streaming-PC kommt: Die Zwischenablage teilen die beiden PCs nicht. Vorschlag im Handtest über SSH. Dass `openssh-server` auf dem Studio-PC läuft, ist geprüft (aktiv, Port 22, 2026-11-04). Ob sich der Streaming-PC anmelden kann, ist nicht geprüft.

## Quellen

- HTTP und Server-Sent Events: https://www.rfc-editor.org/rfc/rfc9110, https://www.rfc-editor.org/rfc/rfc9112, https://html.spec.whatwg.org/multipage/server-sent-events.html, https://html.spec.whatwg.org/multipage/media.html
- Qt 6.8: https://doc.qt.io/qt-6.8/qtcpserver.html#listen, https://doc.qt.io/qt-6.8/qhostaddress.html, https://doc.qt.io/qt-6.8/qnetworkinterface.html, https://doc.qt.io/qt-6.8/qwebenginepage.html#backgroundColor-prop, https://doc.qt.io/qt-6.8/qwebengineview.html, https://doc.qt.io/qt-6.8/qquickwidget.html#grabFramebuffer, https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/text/qfontengine.cpp?h=v6.8.2
- Vergleich mit gleichbleibender Laufzeit: https://libsodium.gitbook.io/doc/helpers#constant-time-test-for-equality
- Linux: https://docs.kernel.org/networking/proc_net_tcp.html, https://man7.org/linux/man-pages/man5/proc_pid_fd.5.html, https://man7.org/linux/man-pages/man5/proc_pid_exe.5.html, https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html
- Bilder und Videos: https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf, https://www.w3.org/TR/png-3/#eXIf, https://www.w3.org/TR/png-3/#samplecrc, https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libavformat/mov.c, https://ffmpeg.org/ffmpeg-filters.html (`testsrc`, Testvideos)
- Web: https://drafts.csswg.org/css-transforms/#transform-rendering, https://drafts.csswg.org/css-font-loading/, https://www.w3.org/TR/SVG2/text.html, https://developer.mozilla.org/en-US/docs/Web/API/HTMLVideoElement/getVideoPlaybackQuality
- Schriften: https://learn.microsoft.com/en-us/typography/opentype/spec/head, https://learn.microsoft.com/en-us/typography/opentype/spec/hhea, https://learn.microsoft.com/en-us/typography/opentype/spec/os2
- OBS (Bildschirmfoto einer Quelle): https://sources.debian.org/src/obs-studio/30.2.3%2Bdfsg-3/UI/window-basic-main-screenshot.cpp; Ablage im Aufnahmeordner laut https://www.digitalcitizen.life/how-to-take-a-screenshot-using-obs-studio-on-windows/ (Drittquelle, deckt sich mit dem Quellcode: Pfad aus `SimpleOutput/FilePath` bzw. `AdvOut/RecFilePath`)
