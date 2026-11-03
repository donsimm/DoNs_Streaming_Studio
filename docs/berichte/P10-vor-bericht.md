# P10-vor – Bericht Vorarbeiten Browserquelle

Stand: 2026-11-03. Alle Messwerte stammen vom Studio-PC (Debian 13, Kernel 6.12.113+deb13-amd64, Xfce/xfwm4 unter X11, `DISPLAY=:0.0`, Qt 6.8.2 (`libqt6core6t64` 6.8.2+dfsg-9+deb13u2), systemd 257, Samsung SSD 840 EVO, `/home` ext4 auf `/dev/sda6`, `/tmp` tmpfs). Kein Xvfb und kein offscreen: Fenstertests liefen in der aktiven X11-Sitzung (Sitzung 1 auf seat0, `Active=yes`, `LockedHint=no` vor jedem Wiederholungslauf geprüft).

Auftrag: `prompts/P10-vor-vorarbeiten-browserquelle.md`. Teil A: Kern (Übernehmen im eigenen Thread, Aufräumen beim Start, Einstellungen `kern.ini`). Teil B: Fenster (Dialog «Einstellungen», Bild der Leinwand). Teil C: Einsatz und Tests (Punkte 69, 70, 72, Tabelle der Orte). Kein Webserver.

**Claude Code hat nicht nach `~/.local/opt/dons-streaming-studio/` installiert** und die echten Daten `~/.local/share/dons-streaming-studio` nicht verändert: Prüfsummen (`sha256sum` aller Dateien) vor den Tests (2026-11-03 20:34:59) und danach (2026-11-03 22:27:17) gleich, 14 Dateien in `~/.local/share/dons-streaming-studio`, 66 Dateien in `~/.local/opt/dons-streaming-studio`. `~/.config/dons-streaming-studio/fenster.ini` zuletzt geändert 2026-11-03 18:39:52 (vor dieser Sitzung), dort keine `kern.ini`. **Es wurde kein Paket installiert.** Kein Test hat nach `~/.config/dons-streaming-studio*` oder `~/Bilder` geschrieben (Tests mit eigenem `XDG_CONFIG_HOME` und eigenem Ordner für Bilder; `~/.config/dons-streaming-studio-entwicklung` und `~/Bilder/DoNs Streaming Studio` gibt es nach allen Tests nicht).

## Kurzfassung

| Abnahme | Ergebnis |
|---|---|
| Bau ohne Warnungen mit `-Wall -Wextra`, jeder Schalter einzeln aus, Release | ja, 0 Warnungen: Arbeitsordner (Debug), frischer Klon Release, alle 13 Schalter einzeln aus, `DSS_INSTALLED_EDITION=ON` (`docs/berichte/P10-vor-daten/schalter-ergebnis.txt`). Der Bau mit `DSS_BUILD_CLI=OFF` scheiterte zuerst (Fehler von mir in `tests/CMakeLists.txt`), behoben in `319856f`, danach 0 Warnungen und `tst_kernverbindung` 17 von 17. Mit den drei neuen Schaltern aus lief auch `ctest` (siehe «Bau») |
| `ctest`, auch im frischen Klon | Frischer Klon von `f475bb6` (Debug, 2026-11-03 22:44 bis 22:48): Bau Exit 0, 0 Warnungen, `ctest --output-on-failure` **35 von 35** (115,5 s). Release im frischen Klon von `a7ff729`: 34 von 35 (siehe «Bau») |
| `tst_medien` und `tst_fensterbearbeiten` je 50 Mal | `tst_medien` 50 von 50. `tst_fensterbearbeiten` zuerst 48 von 50; beide Fehler im neuen Test für 1 GiB (Fehler im Test, siehe «Punkt 72»), nach der Korrektur `f475bb6` (2026-11-03 22:22 bis 22:39): **50 von 50** |
| Startprüfung `--runs 20` | **20 von 20** in allen 4 Fällen (`core-sigterm`, `core-sigint`, `app-sigterm`, `app-kern-sigterm`), Exit-Code 0, Fenster sichtbar, keine Reste, keine unerwarteten Zeilen, neue Zeile «Einstellungen: » erwartet und vorhanden |
| Dokumente | nachgeführt: `docs/01-festlegungen.md` (Tabelle der Orte: `kern.ini`, Port, Bilder), `docs/04-offene-punkte.md` (67 bis 70 und 72 erledigt, neu 73 bis 77), `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md` (Version 5, «Übernehmen von Dateien», «Einstellungen», Watchdog), `README.md` (Ordner `src/render/`, Schalter, `kern.ini`, Optionen) |
| Handtest | offen, 3 Prüfungen und Aufräumen (Abschnitt «Handtest») |

## Teil A – Kern

### 1 – Übernehmen von Dateien im eigenen Thread (Punkt 67)

Umsetzung (`src/common/mediaimport.h/.cpp`, `src/core/coreservice.cpp`, Commit `67261ad`):

- `importMediaFile` rechnet SHA-256 und kopiert stückweise zu 1 MiB mit `QSaveFile`; beim Kopieren wird der Inhalt ein zweites Mal gehasht (Datei während des Übernehmens geändert → abgewiesen). Zwischen den Stücken prüft es ein Abbruchzeichen.
- `MediaImporter` (QThreadPool, **höchstens 2 Threads**: eine kleine Datei muss nicht hinter einer grossen warten; mehr Threads teilen sich nur dieselbe Platte) meldet das Ergebnis in den Thread des Kerns zurück. Erst dann prüft und ändert der Kern das Overlay in der Hauptschleife, verteilt es und schickt `change_result`. **Nur der Kern schreibt; Overlays und `liste.json` ändern sich nur in der Hauptschleife.**
- Vor dem Übernehmen prüft der Kern Overlay und Quelle, damit eine Änderung an einem fehlenden Overlay nicht erst nach Minuten scheitert.

| Fall | Verhalten | Test (`tst_uebernehmen`, 26 von 26, 2026-11-03) |
|---|---|---|
| Zwei Übernahmen gleichzeitig, verschiedene Dateien | laufen parallel | `respondsWhileImporting` (2 × 256 MiB) |
| Dieselbe Datei zweimal gleichzeitig | die zweite wartet auf die erste (Sperre je Ziel), findet die Kopie vor; eine Kopie in `media/`, Protokoll «war schon vorhanden» | `sameFileTwiceInParallel`, `respondsWhileImporting` |
| Overlay oder Quelle während des Übernehmens gelöscht | `change_result` `unknown_overlay` bzw. `unknown_source`; die Kopie bleibt in `media/` (Punkt 74) | `removedWhileImporting` |
| SIGTERM beim Hashen bzw. beim Kopieren | Exit-Code 0, «2 Übernahmen abgebrochen», `media/` leer (keine Datei unter dem endgültigen Namen), Zeit bis zum Ende gemessen: 5 ms bzw. 15 ms (Grenze im Test 2000 ms) | `sigtermWhileImporting` |
| Abbrechen aller Threads | 3 bis 8 ms | `cancelAllStopsThreads`, `cancelLeavesNothing` |
| Kern antwortet während des Übernehmens | 256 MiB in 3,56 s, 35 Anfragen `get_overlay_list`, Antwort min 0 / Median 0 / max 25 ms; 3 `stats` | `respondsWhileImporting` |

**Watchdog (Entscheid mit Begründung):** Die zusätzliche Meldung `WATCHDOG=1` jede Sekunde beim Übernehmen (P10a) **entfällt** mit dem Thread. Die Hauptschleife blockiert beim Übernehmen nicht mehr und meldet weiter alle 5 s; eine Meldung aus dem Thread würde eine hängende Hauptschleife verdecken, also genau den Fall, für den der Watchdog da ist. Nur mit `DSS_CORE_IMPORT_THREAD=OFF` bleibt die Meldung wie in P10a. Prüfung mit 4 GiB als Dienst (`docs/berichte/P10-vor-daten/watchdog-gross.sh`, Ergebnis `watchdog-ergebnis.txt`, 2026-11-03 21:13 bis 21:14):

| Wert | Ergebnis |
|---|---|
| Einheit | Testeinheit `dss-core-watchdogtest.service`, `WatchdogUSec=10s`, eigener Datenordner, Verbindung und `kern.ini` |
| Datei | 4 294 967 296 Byte aus `/dev/urandom`, auf ext4 |
| Dauer bis `change_result` | 58,5 s (Protokoll des Kerns: 57,6 s) |
| `get_overlay_list` während des Übernehmens | 117 Antworten, min 0,2 / Median 0,2 / max 0,8 ms |
| Neustart | keiner: PID vorher und nachher gleich (334255), `NRestarts=0`, auch 12 s nach dem Ende |

Testeinheit und Arbeitsordner sind danach entfernt.

**Fenster:** Hinweis in der Statuszeile (`importStatus`, zwischen Zoom und Zustand des Kerns): «Übernehme «a.png» …», bei mehreren «Übernehme 2 Dateien («a.png», «b.mp4») …» (`importStatusText` in `src/common/windowdata.cpp`, `pendingImports` in `CoreClient`). Mehr (Fortschritt, Abbrechen) ist `[OFFEN]`, Punkt 73.

**Fenster bedienbar während 1 GiB** (`tst_fensterbearbeiten::usableWhileImportingLargeFile`, Fenster und Kern wie in `dss-app`): Datei 1 GiB (Zufallszahlen, fester Startwert) im Bauordner auf ext4, Kopie des Kerns im Datenordner des Tests (tmpfs). Während des Übernehmens fragt eine zweite Verbindung alle 100 ms `get_overlay_list` an, und der Test drückt jede fünfte Runde die Pfeiltaste im Feld X; gemessen wird bis zur Ankunft beim Kern. In 48 bestandenen Läufen (2026-11-03 21:44 bis 22:02):

| Wert | min bis max über 48 Läufe (Median) |
|---|---|
| Dauer des Übernehmens | 13 453 bis 13 786 ms (13 554) |
| Antworten des Kerns je Lauf | 119 bis 122; Median je Lauf 0 ms; grösster Wert je Lauf 89 bis 224 ms (175,5) |
| Pfeiltaste bis zum Kern, grösster Wert je Lauf (24 bis 25 Änderungen) | 53 bis 64 ms (55) |
| grösste Lücke der Ereignisschleife des Fensters (Takt 10 ms) | 18 bis 26 ms (20) |

Die Datei liegt nach dem ersten Lauf im Seitencache; der erste Lauf überhaupt (21:12, Datei eben geschrieben) brauchte 13 748 ms, max. Antwort 122 ms. Gegenprobe: Mit `DSS_CORE_IMPORT_THREAD=OFF` scheitern `tst_uebernehmen` und `tst_fensterbearbeiten` (Kern antwortet während des Übernehmens nicht, `schalter-ergebnis.txt`).

### 2 – Aufräumen beim Start (Punkt 68)

Der Kern löscht nach dem Nehmen der Sperre in `media/` nur reguläre Dateien (keine Verweise, keine Ordner) mit dem Namen `<64 Hex-Zeichen>.<png|jpg|jpeg|webm|mp4|ttf|otf>.<6 Buchstaben A–Z, a–z>` und schreibt «Aufräumen media/: N vorläufige Dateien von QSaveFile gelöscht (B Byte)», dazu je Datei eine Zeile (`removeSaveFileLeftovers`, `src/core/main.cpp`). Herleitung des Musters (Quellen unten): `QSaveFile` nutzt `QTemporaryFileEngine` mit dem endgültigen Namen als Vorlage; enthält die Vorlage weniger als 6 «X», hängt `QTemporaryFile` «.XXXXXX» an und ersetzt die X durch Buchstaben A–Z und a–z. Auf Linux versucht es zuerst eine Datei ohne Namen.

Gemessen am 2026-11-03 20:29 (kleines Programm mit `QSaveFile`, Abbruch mit `_exit(9)` vor `commit()`, auf tmpfs; ebenso auf ext4 `/home`): `/proc/self/fd` zeigt `…/#118098 (deleted)`, im Ordner steht nichts, nach dem Abbruch bleibt nichts liegen. **Reste mit Namen entstehen auf diesem PC also nicht**; der Rückfall mit Namen (wenn O_TMPFILE fehlt) liess sich hier nicht erzeugen und ist **nicht geprüft**. Geprüft ist das Löschen mit von Hand angelegten Dateien: `tst_uebernehmen::leftoverPattern` (Muster, auch Fälle, die nicht passen), `removeLeftoversOnlyMatching` (2 gelöscht, 1234 Byte; Ordner und Verweis mit passendem Namen bleiben), `coreCleansAtStart` (Kern löscht 1 Datei, 4096 Byte, danach 0).

### 3 – Einstellungen in `kern.ini`

Commit `fcbd377`. Datei `<Ordner der Einstellungen der Version>/kern.ini` (`~/.config/dons-streaming-studio/` bzw. `~/.config/dons-streaming-studio-entwicklung/`), Abschnitt `[overlay-server]`, Angaben `port` und `adresse-im-heimnetz`. Nur der Kern schreibt sie, atomar mit `QSaveFile` (`src/common/coresettings.h/.cpp`). Aufbau und Nachrichten: `docs/08-kern-schnittstelle.md`, «Einstellungen».

- **Port:** Standard 8080 im Einsatz, 8081 in Arbeit. Gültig **1024 bis 32767**. Begründung: Unter 1024 braucht ein Port besondere Rechte (`/proc/sys/net/ipv4/ip_unprivileged_port_start` = 1024); ab 32768 vergibt Linux die Ports für ausgehende Verbindungen (`/proc/sys/net/ipv4/ip_local_port_range` = `32768 60999`), ein Port dort kann beim Start des Servers zufällig belegt sein. Gemessen am 2026-11-03 mit `cat` der beiden Dateien (`sysctl` ist nicht installiert). Quelle: ip(7).
- **Adresse im Heimnetz:** Fehlt sie beim Start, trägt der Kern einmal die IPv4-Adresse der Netzwerkkarte der Standardroute ein und schreibt die Datei; danach ermittelt er sie nie neu (auch nicht beim Neustart, geprüft in `tst_kernverbindung::reconnectAfterKillAndRestart`). Standardroute: die erste Zeile in `/proc/net/route` mit Destination und Mask `00000000` und Flag `RTF_UP` (dieselbe Funktion `defaultRouteInterface` wie die LAN-Anzeige); Adresse: die erste IPv4-Adresse dieser Karte ausser 127.x.x.x und 169.254.x.x (`QNetworkInterface::interfaceFromName`). Heute ergibt das **192.168.1.9 auf enp3s0** (`tst_kerneinstellungen::detectOnThisMachine`, 2026-11-03). Ohne IPv4-Standardroute bleibt der Wert leer, Warnung «Einstellungen: keine Standardroute mit IPv4, Adresse im Heimnetz bleibt leer»; neuer Versuch beim nächsten Start.
- **Gültige Adresse:** vier Zahlen 0 bis 255 ohne führende Nullen (strenger als `QHostAddress`, das auch «1» oder «192.168.1» annimmt); nicht 0.0.0.0, 127.x.x.x, ab 224.0.0.0. Leer erlaubt.
- **Ungültige Datei:** Standardwerte gelten, Warnung im Protokoll, Datei bleibt unverändert, Änderungen werden abgewiesen, bis sie von Hand repariert ist (wie `liste.json`).
- **Schnittstelle Version 5:** neue Nachrichten `get_settings`, `settings` (an alle nach jeder Änderung), `update_settings` (Antwort `change_result`; ungültige Werte mit `rejected` und Meldung, zum Beispiel «Port 80 ungültig, erlaubt sind 1024 bis 32767.»).
- **dss-cli:** `einstellungen` (fragt den laufenden Kern; läuft er nicht, liest es `kern.ini` nur), `einstellung-port PORT`, `einstellung-adresse ADRESSE` (nur über den laufenden Kern, sonst Exit-Code 4), Option `--verbindung`. Blockierende Verbindung `CoreSession` in `src/common/coresession.h`. `dss-core --einstellungen PFAD` und `scripts/dienst-einrichten.sh --einstellungen PFAD` für Tests.
- Der Kern wendet Port und Adresse noch nicht an (kein Server, P10).

Tests: `tst_kerneinstellungen` (48 von 48, ohne Qt Gui): Standardwerte je Version, Ports 0 bis 65535 an den Grenzen, 16 Adressen, 9 ungültige Dateien, Adresse aus der Standardroute mit Testdaten (`tests/daten/proc/heimnetz/net/route`, Kopie von `/proc/net/route` vom 2026-11-03 20:52:31, enp3s0; `messung1` mit enx2aea2d88f6c1 und link-local, das übersprungen wird; `ohne-route`), Karte ohne IPv4, einmal ermitteln, ungültige Datei bleibt unverändert, atomares Schreiben (neue Inode nach dem Schreiben; im schreibgeschützten Ordner scheitert die Änderung, alte Datei ganz, keine vorläufige Datei). `tst_protocol::settingsMessages` (Nachrichten lesen und schreiben, ungültige Fälle). `tst_kernverbindung::settingsReadAndChange` und `invalidSettingsFile` (Kern als Prozess mit eigenem `XDG_CONFIG_HOME`: `kern.ini` angelegt mit 8081 und 192.168.1.9, 5 ungültige Änderungen abgewiesen und Datei unverändert, gültige Änderung an eine zweite Verbindung verteilt, `dss-cli` lesen, ändern, abgewiesen, ohne Kern Exit 4; ungültige `kern.ini` bleibt Byte für Byte).

## Teil B – Fenster

### 1 – Dialog «Werkzeuge → Einstellungen …»

Commit `fac3600`. `src/app/settingsdialog.h/.cpp`, nicht blockierend (`open()`). Der Dialog fragt beim Öffnen `get_settings` an, stellt das Zahlenfeld auf den Bereich aus der Nachricht (1024 bis 32767), schickt bei «OK» nur geänderte Werte als `update_settings` (`settingsUpdateFromForm` in `src/common/windowdata.cpp`, getestet in `tst_windowdata::settingsForm`) und schliesst erst nach `change_result` `ok`. Eine Meldung des Kerns erscheint im Dialog (Platz für drei Zeilen von Anfang an, damit das Setzen des Textes das Layout nicht ändert, Regel 9). Hinweis im Dialog: «Port und Adresse gelten erst mit der Browserquelle (P10). …». Ohne Verbindung ist der Menüeintrag ausgegraut; im offenen Dialog steht dann «Keine Verbindung zum Kern …». Schalter `DSS_APP_SETTINGS_DIALOG`. Geprüft in `tst_fensterbearbeiten::settingsDialog` (Menü, Werte vom Kern, ungültige Adresse mit Meldung des Kerns und gleicher Höhe der Meldung, gültige Änderung kommt bei der zweiten Verbindung an, Abbrechen ändert nichts).

### 2 – «Datei → Leinwand als Bild speichern»

Commit `3105aee`.

- **Kürzel Strg+Umschalt+B.** Geprüft am 2026-11-03 21:04: `xfconf-query -c xfce4-keyboard-shortcuts -l -v` enthält kein Kürzel mit B (belegt sind unter anderem Print, Alt+Print, Shift+Print, Super+P/E/R, viele Strg+Alt, Strg+Umschalt+Escape, Strg+Umschalt+Alt+Pfeile); im Fenster sind Strg+Q, Strg+Umschalt+C/V, Strg+R/F/S/D belegt (`grep setShortcut src/app`). Im Test wirkt das Kürzel mit Fokus auf der Leinwand.
- **Bild:** PNG in der Auflösung des Overlays (1920×1080 im Test), `QImage::Format_ARGB32_Premultiplied`, durchsichtig gefüllt, gezeichnet vom Zeichner der Leinwand mit Massstab 1 und Ecke (0,0). Schachbrett, Auswahlrahmen, Lineale und Zoom zeichnet nur `CanvasWidget`, nicht der Zeichner; sie können also nicht ins Bild kommen. Videos: das Bild, das der Zeichner gerade hält, also das der Leinwand. Gespeichert atomar mit `QSaveFile`.
- **Ablage:** `QStandardPaths::PicturesLocation` + `/DoNs Streaming Studio`, Ordner bei Bedarf angelegt. Qt liest dafür `XDG_PICTURES_DIR` aus `$XDG_CONFIG_HOME/user-dirs.dirs` (sonst `~/.config/user-dirs.dirs`), dieselbe Datei wie `xdg-user-dir`, und nimmt `~/Pictures`, wenn dort nichts steht (Quelle: `qstandardpaths_unix.cpp`, Tag v6.8.2). Gemessen am 2026-11-03: `xdg-user-dir PICTURES` → `/home/dominik/Bilder` (aus `~/.config/user-dirs.dirs`: `XDG_PICTURES_DIR="$HOME/Bilder"`, Paket xdg-user-dirs 0.18-2); ein Testprogramm mit `QStandardPaths::writableLocation(PicturesLocation)` → `/home/dominik/Bilder`, mit `XDG_CONFIG_HOME=/nonexistent` → `/home/dominik/Pictures`.
- **Name:** `<adresse>_<JJJJ-MM-TT_hh-mm-ss>.png` (Ortszeit). Zweites Bild in derselben Sekunde: `…-2.png` (Annahme, damit nichts überschrieben wird).
- Meldung in der Statuszeile «Leinwand gespeichert: <Pfad>»; ohne gewähltes Overlay (zum Beispiel Ordner gewählt) ausgegraut, das Kürzel tut dann nichts.
- **Zeichnen ohne Fenster für P11:** Der Zeichner (`SourceRenderer`) liegt jetzt in der neuen Bibliothek `dss_render` (`src/render/`, Qt Gui ohne Qt Widgets), dazu `renderOverlayImage` und `saveOverlayImage` (`src/render/overlayimage.h`); Name und Ordner in `src/common/canvasimage.h` (nur Qt Core). Das Fenster ruft nur `saveOverlayImage` auf.
- Schalter `DSS_APP_CANVAS_IMAGE` (braucht `DSS_APP_RENDERER`).

Tests: `tst_leinwandbild` (8 von 8; `QGuiApplication` ohne Fenster in der X11-Sitzung, gelinkt nur Qt Core, Gui, Network, DBus, Test, geprüft mit `ldd`): Grösse 640×360 und Format; **jedes Pixel ausserhalb der Quellen hat Alpha 0** (also kein Schachbrett, Rahmen, Lineal); halbdurchsichtiges Rot bleibt bei Alpha 128 ± 1; Text gezeichnet; 16 EXIF-Bilder (`tests/daten/exif/`, JPEG und PNG) und 4 gedrehte Videos (`tests/daten/video-drehung/`) mit den Farben rot, grün, blau, gelb an der richtigen Stelle; Vergleich mit dem Zeichner (an anderer Stelle einer grösseren Fläche gezeichnet, Pixel für Pixel gleich); PNG zurückgelesen (Signatur, Alpha, Pixel gleich bis auf das Video, das inzwischen weiterlief); zweites Bild `-2`; Fehlermeldung bei unmöglichem Ordner; Ordner aus `user-dirs.dirs` gleich `xdg-user-dir`. `tst_fensterbearbeiten::canvasImage`: Menüeintrag in «Datei», Kürzel, ausgegraut mit gewähltem Ordner, Datei mit Namensmuster, Meldung mit dem Namen, Bild 1920×1080 mit Alpha. Beispielbild aus dem Test: `docs/berichte/P10-vor-daten/leinwandbild.png` (Bildbetrachter mit weissem Grund zeigen den weissen Text «Willkommen» nicht; auf dunklem Grund zusammengesetzt mit `ffmpeg … overlay` ist er da, und der dunkle Grund scheint durch das halbdurchsichtige Rot).

**Abweichung vom Wortlaut:** Der Auftrag nennt das Zeichnen unter «Tests ohne Qt Gui». Zeichnen braucht `QPainter`, `QImage` und die Schriften von Qt Gui; ohne Qt Gui geht es nicht. Umgesetzt ist «ohne Qt Widgets und ohne Fenster» (Punkt 75, zu bestätigen).

### 3 – Schalter

Neu: `DSS_CORE_IMPORT_THREAD`, `DSS_APP_SETTINGS_DIALOG`, `DSS_APP_CANVAS_IMAGE` (alle Standard ON, `README.md`). Die Einstellungen des Kerns (`kern.ini`) haben keinen Schalter: Ohne sie fehlen dem Dialog und P10 die Werte; der Kern ändert ausser dem Anlegen der Datei nichts am Verhalten.

## Teil C – Einsatz und Tests

### 1 – Programmordner leeren (Punkt 69)

Commit `dec3587`. `scripts/installieren.sh` prüft das Ziel (nicht leer, nicht `/`, absolut) und löscht `<Ziel>/bin/` und `<Ziel>/share/dons-streaming-studio/` direkt vor `cmake --install`, also nach erfolgreichem Bau und Prüfen bzw. Umwandeln des Formats. Daten und `version.txt` (liegt in `<Ziel>/`) bleiben. Geprüft **nur mit den Testoptionen** (`docs/berichte/P10-vor-daten/installieren-pruefen.sh`, Ergebnis `installieren-ergebnis.txt`, 2026-11-03 20:35 bis 20:36; Ziel, Datenordner, Einheit `dss-core-installtest.service`, Menü- und Symbolordner im Scratch-Ordner, Daten aus `tests/daten/formate/`):

| Lauf | Ergebnis |
|---|---|
| 1 – erste Installation | Exit 0, 2 Dateien umgewandelt, Sicherung angelegt |
| 2 – mit `bin/veraltet-programm` und `share/…/symbole/veraltet.svg` | Exit 0, beide Dateien weg, Daten unverändert |
| 3 – Klon mit `#error` in `src/core/main.cpp` | Exit 1 «Bau gescheitert, nichts installiert», Ziel und Daten unverändert |

Testeinheit danach entfernt.

### 2 – Zurückschreiben beim Umwandeln (Punkt 70)

Commit `fad744a`. `dss::testing::setConvertFailWriteAt(n)` (`src/common/dataformattesting.h`, nur `tst_datenformat` bindet die Datei ein; im Programm bleibt der Wert 0 und die Prüfung `g_failWriteAt == i + 1` greift nie). `tst_datenformat::failedWriteRestoresFiles` mit «erste Datei» und «zweite Datei»: Nach dem Fehler sind alle Dateien Byte für Byte wie vorher; bei der zweiten Datei hat die erste eine neue Inode (geschrieben und zurückgeschrieben), danach gelingt das Umwandeln ohne Testfehler. Gegenprobe: Ohne das Zurückschreiben scheitert die Zeile «zweite Datei». 11 von 11.

### 3 – `tst_fensterbearbeiten` 50 Mal (Punkt 72)

Aufruf `docs/berichte/P10-vor-daten/wiederholen.sh build tst_fensterbearbeiten 50 <Ordner>` (je Lauf `ctest -R '^tst_fensterbearbeiten$' -V`, also mit derselben Umgebung wie `ctest`; vor jedem Lauf Prüfung der X11-Sitzung; bei einem Fehler wird die ganze Ausgabe gespeichert).

- **Erster Durchgang** (Stand `319856f`, 2026-11-03 21:44 bis 22:02): 48 von 50. `headerGaugesWidth` (der Test aus Punkt 72) bestand alle 50 Male. Beide Fehler (Läufe 9 und 24) im neuen Test `usableWhileImportingLargeFile`, Zeile 1346: «Actual (sourceAtRow(0).x): 0, Expected (before + 1): 1». Ursache, aus dem Ablauf hergeleitet: Der Test las die bearbeitete Quelle als «vorderste Zeile». Die neue Quelle aus dem Übernehmen kommt ganz nach vorne. Der Kern schickt das geänderte Overlay an alle, dann `change_result` an das Fenster; die zweite Verbindung des Tests kann die neue Quelle schon haben, während das Fenster noch auf `change_result` wartet (`pendingImports()` noch nicht leer). Dann ist `sourceAtRow(0)` die neue Quelle mit X 0, die Pfeiltaste ändert aber die gewählte, bisherige Quelle. Der Wert 0 passt dazu (neue Quelle bei 0,0). Nachprüfung mit vorübergehender Ausgabe im Test (nicht eingecheckt, 5 Läufe, 2026-11-03 22:40 bis 22:42): Die bearbeitete Quelle hat X 1500 zu Beginn und 1523 bis 1524 am Ende; der Wert 0 kann also nicht von ihr stammen, sondern nur von der neuen Quelle. In diesen 5 Läufen trat der Fall selbst nicht auf (Zeile 0 war vor jeder Pfeiltaste die bearbeitete Quelle; `wiederholen-ergebnis.txt`). Fehler im Test, nicht im Programm. Korrektur `f475bb6`: Der Test folgt der Quelle nach Kennung.
- **Zweiter Durchgang** nach der Korrektur (Stand `f475bb6`, 2026-11-03 22:22 bis 22:39): **50 von 50**, auch `headerGaugesWidth` und `usableWhileImportingLargeFile`. Damit ist Punkt 72 erledigt: `headerGaugesWidth` 100 von 100; die Ursache des einen Fehlers aus P10a bleibt ungeklärt (trat nicht wieder auf).

### 4 – Tabelle der Orte je Version

`docs/01-festlegungen.md`, «Einsatz»: neue Zeilen «Einstellungen des Kerns» (`kern.ini` je Version), «Port des Overlay-Servers» (8080 / 8081), «Bilder der Leinwand» (`~/Bilder/DoNs Streaming Studio/` für beide).

## Ausgeführte Befehle und Ergebnisse

### Bau

```
cmake -S . -B build && cmake --build build -j8                          # Debug, 0 Warnungen
docs/berichte/P10-vor-daten/schalter-lauf.sh <Scratch>/schalter         # frischer Klon von a7ff729, 21:22 bis 21:47
```

Ergebnis (`schalter-ergebnis.txt`): Release Exit 0, 0 Warnungen, `ctest` 34 von 35 (`tst_fensterbearbeiten` fehlgeschlagen; die Ausgabe war mit dem Bauordner gelöscht, siehe unten); `DSS_BUILD_APP`, `DSS_BUILD_TESTS`, `DSS_MEDIA_SIZE`, `DSS_APP_RENDERER`, `DSS_APP_CANVAS_EDIT`, `DSS_VIDEO_ROTATION`, `DSS_CORE_WATCHDOG`, `DSS_APP_EDITING`, `DSS_APP_TRANSFORM` je aus: Exit 0, 0 Warnungen (nur gebaut); `DSS_BUILD_CLI=OFF`: Exit 1 beim Konfigurieren (`tst_kernverbindung` brauchte `dss-cli`), behoben in `319856f`, danach Exit 0 und 0 Warnungen; `DSS_CORE_IMPORT_THREAD=OFF`: 0 Warnungen, `ctest` 33 von 35, `tst_uebernehmen` und `tst_fensterbearbeiten` scheitern wie erwartet (Gegenprobe, Kern antwortet beim Übernehmen nicht); `DSS_APP_SETTINGS_DIALOG=OFF` und `DSS_APP_CANVAS_IMAGE=OFF`: 0 Warnungen, `ctest` 35 von 35; `DSS_INSTALLED_EDITION=ON`: Exit 0, 0 Warnungen.

Nachprüfung des Fehlers im Release-Bau: neuer Klon, Release, `ctest -j4 --output-on-failure` 7 Mal hintereinander (2026-11-03 21:28 bis 21:41): **7 von 7 mit 35 von 35**. Die Ausgabe des einen Fehlers fehlt; vermutlich derselbe Fehler im Test wie unter «Punkt 72» (nicht geprüft).

### Tests

| Lauf | Datum | Ergebnis |
|---|---|---|
| `ctest` Arbeitsordner, nach A1/A2 | 2026-11-03 20:4x | 33 von 33 (79,5 s) |
| `ctest` Arbeitsordner, nach A3 | 2026-11-03 21:00 | 34 von 34 |
| `ctest` Arbeitsordner, nach B2 | 2026-11-03 21:10 | 35 von 35 |
| `tst_medien` 50 Mal, Stand `319856f` | 2026-11-03 22:02 bis 22:20 | **50 von 50** |
| `tst_fensterbearbeiten` 50 Mal | siehe Teil C 3 | |
| `tst_fensterbearbeiten` 50 Mal, Stand `f475bb6` | 2026-11-03 22:22 bis 22:39 | **50 von 50**; Übernehmen 1 GiB 13 457 bis 13 898 ms (Median 13 585), 119 bis 123 Antworten je Lauf (Median je Lauf 0 ms, grösster Wert 75 bis 224 ms), Pfeiltaste bis zum Kern max 52 bis 61 ms, Lücke der Ereignisschleife 19 bis 25 ms |
| Startprüfung `--runs 20` | 2026-11-03 22:42 bis 22:44 | 20 von 20 in allen 4 Fällen |
| `ctest` frischer Klon `f475bb6`, Debug | 2026-11-03 22:44 bis 22:48 | **35 von 35**, 0 Warnungen |
| Prüfsummen echte Daten und `~/.local/opt` | 2026-11-03 22:27 | gleich wie 20:34:59 |

### Startprüfung

```
cd ~/DoNs_Streaming_Studio && python3 scripts/startpruefung.py --runs 20 --out docs/berichte/P10-vor-daten
```

2026-11-03 22:42:26 bis 22:44:39, `DISPLAY=:0.0`, dss-app mit eigenem `XDG_CONFIG_HOME` je Fall (also auch eigene `kern.ini`). Ergebnisse `docs/berichte/P10-vor-daten/startpruefung-*.md` und `.json`.

| Fall | sauber | Start ms (min / Median / max) | Ende ms (min / Median / max) |
|---|---|---|---|
| `core-sigterm` | 20 von 20 | 35,8 / 43,2 / 46,3 | 2,5 / 3,8 / 4,6 |
| `core-sigint` | 20 von 20 | 36,7 / 44,0 / 46,6 | 3,5 / 3,8 / 4,9 |
| `app-sigterm` | 20 von 20 | 219,9 / 236,0 / 251,6 | 12,4 / 16,2 / 21,9 |
| `app-kern-sigterm` | 20 von 20 | 215,0 / 228,1 / 246,9 | 16,0 / 18,8 / 22,7 |

## Handtest

Grund (Regel 10): Aussehen und Bedienung des Dialogs, der Vergleich des gespeicherten Bildes mit der Leinwand durch das Auge und das Bedienen des Fensters während einer grossen Datei mit Maus und Dateidialog (GTK, nicht fernsteuerbar) kann nur der Nutzer prüfen. Was oben geprüft ist (Werte, Abweisungen, Datei `kern.ini`, Pixel des Bildes, Antwortzeiten, Watchdog), muss der Nutzer nicht wiederholen. Nur die Version aus dem Repo und der Entwicklungsordner; die echten Daten und die installierte Version bleiben unberührt. Befehle im Terminal der Xfce-Sitzung. Vorher alle Fenster von DoNs Streaming Studio schliessen.

### Prüfung 1 – Dialog «Einstellungen»

```
cd ~/DoNs_Streaming_Studio && cmake --build build -j8 && build/src/cli/dss-cli overlay-neu "Handtest P10-vor" 1920 1080 && build/src/cli/dss-cli quelle-bild handtest-p10-vor tests/daten/exif/orientierung-6.jpg && build/src/cli/dss-cli quelle-video handtest-p10-vor tests/daten/video-drehung/drehung-90.mp4 --wiederholen && build/src/cli/dss-cli quelle-text handtest-p10-vor "Willkommen" /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf && build/src/cli/dss-cli quelle-geometrie handtest-p10-vor 1 0 0 640 320 0 && build/src/cli/dss-cli quelle-geometrie handtest-p10-vor 2 700 0 640 320 0 && build/src/cli/dss-cli quelle-geometrie handtest-p10-vor 3 100 500 1200 200 0 && build/src/app/dss-app
```

Das legt im Entwicklungsordner ein Overlay mit Bild, Video und Text an und startet das Fenster «DoNs Streaming Studio – Entwicklung» (es startet `dss-core-entwicklung.service`; der Kern legt dabei `~/.config/dons-streaming-studio-entwicklung/kern.ini` an). Im Fenster:

1. «Werkzeuge → Einstellungen …» öffnen. Erwartet: Port 8081, Adresse 192.168.1.9, darunter der Pfad von `kern.ini` und der Hinweis, dass die Werte erst mit der Browserquelle (P10) gelten.
2. Adresse auf `192.168.1` ändern, «OK». Erwartet: Der Dialog bleibt offen, darunter rot bzw. als Text «Adresse «192.168.1» ungültig, erwartet ist eine IPv4-Adresse wie 192.168.1.9.»; der Dialog springt nicht in der Grösse.
3. Adresse wieder `192.168.1.9`, Port 8082, «OK». Erwartet: Dialog schliesst. Nochmals öffnen: Port 8082. Ins Zahlenfeld lassen sich nur 1024 bis 32767 eingeben.
4. Im Terminal (Fenster offen lassen):

```
cd ~/DoNs_Streaming_Studio && build/src/cli/dss-cli einstellungen && build/src/cli/dss-cli einstellung-port 8081 && cat ~/.config/dons-streaming-studio-entwicklung/kern.ini
```

Erwartet: zuerst «Port 8082 …», «Quelle Kern»; dann «Port: 8081», in der Datei `port=8081` und `adresse-im-heimnetz=192.168.1.9`. Den Dialog nochmals öffnen: Port 8081.

Fenster offen lassen für Prüfung 2.

### Prüfung 2 – Bild der Leinwand

1. Im Dock «Overlays» «Handtest P10-vor» wählen. Ordner gibt es keinen; wer prüfen will, dass der Menüeintrag ohne Overlay ausgegraut ist: «Datei» öffnen, bevor ein Overlay gewählt ist.
2. Strg+Umschalt+B drücken (oder «Datei → Leinwand als Bild speichern»). Erwartet: unten in der Statuszeile «Leinwand gespeichert: /home/dominik/Bilder/DoNs Streaming Studio/handtest-p10-vor_<Datum_Zeit>.png». Keine Xfce-Aktion (kein Bildschirmfoto-Dialog, kein Fensterwechsel).
3. Bild ansehen:

```
cd ~/DoNs_Streaming_Studio && firefox "$(ls -t ~/Bilder/DoNs\ Streaming\ Studio/*.png | head -1)"
```

Erwartet: Firefox nennt im Titel «(PNG-Grafik, 1920 × 1080 Pixel)». Das Bild zeigt dasselbe wie die Leinwand: links oben Bild (rot, grün oben; blau, gelb unten), daneben das Video mit demselben Bild, darunter «Willkommen» in weiss. Wo keine Quelle ist, sieht man den Hintergrund von Firefox (durchsichtig), **kein** Schachbrett, **kein** Auswahlrahmen, **keine** Lineale, keine Zoomstufe. Das Video zeigt ein Standbild.

Fenster offen lassen für Prüfung 3.

### Prüfung 3 – Grosse Datei, dabei das Fenster bedienen

Die Datei `build/tests/gross-daten/gross-1024mib.png` (1 GiB Zufallszahlen aus dem Test, kein echtes Bild) liegt schon da.

1. Im Dock «Quellen» «+» → «Bild», im Dateidialog `~/DoNs_Streaming_Studio/build/tests/gross-daten/gross-1024mib.png` wählen.
2. Sofort danach, während etwa 14 s: Unten erscheint «Übernehme «gross-1024mib.png» …». In dieser Zeit eine Quelle auf der Leinwand anklicken und mit der Maus verschieben, im Dock «Eigenschaften» X mit der Pfeiltaste ändern, ein anderes Overlay wählen und zurück. Erwartet: Alles reagiert ohne Hänger; CPU, RAM und LAN oben laufen jede Sekunde weiter.
3. Danach: Der Hinweis verschwindet, im Dock «Quellen» steht die neue Quelle «gross-1024mib.png» zuoberst; die Statuszeile meldet, dass die Grösse der Datei nicht lesbar ist (Zufallszahlen sind kein PNG), die Quelle bleibt leer. Die neue Quelle löschen.

Fenster schliessen.

### Prüfung 4 – Aufräumen der Entwicklungsdaten

```
cd ~/DoNs_Streaming_Studio && systemctl --user stop dss-core-entwicklung.service; rm -rf ~/.local/share/dons-streaming-studio-entwicklung ~/.config/dons-streaming-studio-entwicklung build/tests/gross-daten && ls ~/.local/share/ ~/.config/ | grep dons-streaming-studio
```

Erwartet: Es bleiben `dons-streaming-studio` (echte Daten, in `~/.local/share/` und in `~/.config/`) und gegebenenfalls `dons-streaming-studio-sicherungen`. Die Bilder aus Prüfung 2 liegen in `~/Bilder/DoNs Streaming Studio/`; nicht mehr gebraucht: `rm -r ~/Bilder/DoNs\ Streaming\ Studio`.

## Commits

- `fad744a` P10-vor: Testmöglichkeit beim Umwandeln, Zurückschreiben geprüft (Punkt 70)
- `dec3587` P10-vor: installieren.sh leert die Programmordner vor dem Kopieren (Punkt 69)
- `67261ad` P10-vor: Übernehmen von Dateien im eigenen Thread, Aufräumen beim Start (Punkte 67, 68)
- `fcbd377` P10-vor: Einstellungen des Kerns in kern.ini, Schnittstelle Version 5, dss-cli
- `fac3600` P10-vor: Dialog Werkzeuge → Einstellungen (Port, Adresse im Heimnetz)
- `3105aee` P10-vor: Datei → Leinwand als Bild speichern (Strg+Umschalt+B)
- `a7ff729` P10-vor: Test Fenster bedienbar während des Übernehmens von 1 GiB
- `319856f` P10-vor: tst_kernverbindung ohne dss-cli baubar (DSS_BUILD_CLI=OFF)
- `f475bb6` P10-vor: Test 1 GiB folgt der bearbeiteten Quelle nach Kennung
- (dieser Commit) P10-vor: Bericht, Dokumente nachgeführt, Messdaten

## Geänderte und neue Dateien

- Neu: `src/common/mediaimport.h/.cpp`, `coresettings.h/.cpp`, `coresession.h/.cpp`, `canvasimage.h/.cpp`, `dataformattesting.h`; `src/render/` (`CMakeLists.txt`, `overlayimage.h/.cpp`; `sourcerenderer.h/.cpp` verschoben aus `src/app/`); `src/app/settingsdialog.h/.cpp`; `tests/tst_uebernehmen.cpp`, `tst_kerneinstellungen.cpp`, `tst_leinwandbild.cpp`, `tests/daten/proc/heimnetz/net/route`; `docs/berichte/P10-vor-bericht.md`, `docs/berichte/P10-vor-daten/` (`installieren-pruefen.sh`, `installieren-ergebnis.txt`, `watchdog-gross.sh`, `watchdog-gross.py`, `watchdog-ergebnis.txt`, `schalter-lauf.sh`, `schalter-ergebnis.txt`, `wiederholen.sh`, `wiederholen-ergebnis.txt`, `leinwandbild.png`, `startpruefung-*.md` und `.json`).
- Geändert: `CMakeLists.txt`, `README.md`, `scripts/installieren.sh`, `scripts/dienst-einrichten.sh`, `scripts/startpruefung.py`, `src/app/` (`CMakeLists.txt`, `mainwindow.h/.cpp`), `src/cli/main.cpp`, `src/common/` (`CMakeLists.txt`, `coreclient.h/.cpp`, `dataformat.cpp`, `overlaystore.h/.cpp`, `protocol.h/.cpp`, `windowdata.h/.cpp`), `src/core/` (`CMakeLists.txt`, `coreservice.h/.cpp`, `main.cpp`), `tests/` (`CMakeLists.txt`, `prozesshilfe.h`, `tst_datenformat.cpp`, `tst_fensterbearbeiten.cpp`, `tst_kernverbindung.cpp`, `tst_protocol.cpp`, `tst_windowdata.cpp`), `docs/01-festlegungen.md`, `docs/04-offene-punkte.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`.
- Ausserhalb des Repos (nur Tests, wieder entfernt): Testeinheiten `dss-core-watchdogtest.service` und `dss-core-installtest.service`, Sockets unter `/run/user/1000/`, Scratch-Ordner. Bleibt bis zum Aufräumen im Handtest: `build/tests/gross-daten/gross-1024mib.png` (1 GiB, im Bauordner, nicht im Repo).

## Nicht erledigt

- Handtest (Prüfungen 1 bis 3, Aufräumen).
- Webserver, Meldung bei belegtem Port, Warten auf eine fehlende Adresse (P10, nicht im Auftrag).
- Mehr Anzeige beim Übernehmen (Fortschritt, Abbrechen): Punkt 73.
- Der Rückfall von `QSaveFile` auf eine vorläufige Datei mit Namen ist nicht geprüft (entsteht auf diesem PC nicht).

## Annahmen

- Höchstens 2 Threads zum Übernehmen (Begründung oben).
- Eine Kopie, deren Änderung danach scheitert (Overlay gelöscht), bleibt in `media/` (Punkt 74); Löschen ungenutzter Dateien gehört zu P10b.
- Zweites Bild in derselben Sekunde bekommt `-2` statt das erste zu überschreiben.
- Der Ordner für Bilder ist für beide Versionen derselbe (`~/Bilder/DoNs Streaming Studio/`, wie im Auftrag).
- `dss-cli einstellungen` liest ohne laufenden Kern die Datei der Version (nicht eine mit `--einstellungen` gesetzte andere).
- Die Einstellungen haben keinen eigenen Schalter (Begründung unter «Schalter»).
- «Ohne Qt Gui» beim Zeichnen des Bildes umgesetzt als «ohne Qt Widgets» (Punkt 75).

## `[OFFEN]`

- Punkt 73 (mehr Anzeige beim Übernehmen als der Hinweis in der Statuszeile).
- Punkt 75 (Zeichnen des Bildes kann nicht ohne Qt Gui getestet werden; zu bestätigen).
- Punkt 74 (ungenutzte Kopien in `media/`, wenn die Änderung nach dem Übernehmen scheitert).
- Punkt 76 (abgewiesene Einstellung erscheint im Dialog und in der Statuszeile).
- Punkt 77 (ungültige `kern.ini` nur von Hand reparierbar; gleiche Ports für beide Versionen).
- Ursache des einen Fehlers von `tst_fensterbearbeiten` im Release-Bau (Klon `a7ff729`): Ausgabe fehlt, vermutlich der Fehler im Test aus «Punkt 72» (nicht geprüft); 7 weitere Release-Läufe und alle späteren Läufe ohne Fehler.
- Rückfall von `QSaveFile` auf eine vorläufige Datei mit Namen (nicht geprüft, siehe Teil A 2).

## Quellen

- Qt 6.8: https://doc.qt.io/qt-6.8/qsavefile.html, https://doc.qt.io/qt-6.8/qtemporaryfile.html («If the file name doesn't contain "XXXXXX", QTemporaryFile will append the generated part», «On Linux, QTemporaryFile will attempt to create unnamed temporary files»), https://doc.qt.io/qt-6.8/qthreadpool.html, https://doc.qt.io/qt-6.8/qmetaobject.html#invokeMethod, https://doc.qt.io/qt-6.8/qnetworkinterface.html, https://doc.qt.io/qt-6.8/qhostaddress.html, https://doc.qt.io/qt-6.8/qstandardpaths.html, https://doc.qt.io/qt-6.8/qimage.html, https://doc.qt.io/qt-6.8/qstatusbar.html#showMessage
- Qt-Quellcode, Tag v6.8.2: https://github.com/qt/qtbase/blob/v6.8.2/src/corelib/io/qtemporaryfile.cpp (`.XXXXXX` anhängen, Buchstaben A–Z und a–z, unbenannte Datei mit Rückfall), https://github.com/qt/qtbase/blob/v6.8.2/src/corelib/io/qsavefile.cpp (`QTemporaryFileEngine` mit dem endgültigen Namen), https://github.com/qt/qtbase/blob/v6.8.2/src/corelib/io/qstandardpaths_unix.cpp (`user-dirs.dirs`, `XDG_PICTURES_DIR`, Rückfall `~/Pictures`)
- Linux: ip(7) https://man7.org/linux/man-pages/man7/ip.7.html (`ip_local_port_range`, `ip_unprivileged_port_start`), proc_pid_net(5) https://man7.org/linux/man-pages/man5/proc_pid_net.5.html (`/proc/net/route`), `/usr/include/linux/route.h` (`RTF_UP`), open(2) https://man7.org/linux/man-pages/man2/open.2.html (`O_TMPFILE`)
- freedesktop: https://www.freedesktop.org/wiki/Software/xdg-user-dirs/
- systemd: https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html (`WatchdogSec=`), https://www.freedesktop.org/software/systemd/man/latest/sd_watchdog_enabled.html
- Xfce: Kürzel aus `xfconf-query -c xfce4-keyboard-shortcuts -l -v` auf dem Studio-PC (2026-11-03 21:04)
