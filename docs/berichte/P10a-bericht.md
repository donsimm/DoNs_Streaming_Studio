# P10a – Bericht Nacharbeiten Editor und Einsatz

Stand: 2026-11-03. Alle Messwerte stammen vom Studio-PC (Debian 13, Kernel 6.12.113+deb13-amd64, Xfce/xfwm4 unter X11, `DISPLAY=:0.0`, Qt 6.8.2, FFmpeg 7.1.5, systemd 257.13-1~deb13u1, NVIDIA 550.163.01). Kein Xvfb und kein offscreen: Fenstertests liefen in der aktiven X11-Sitzung (vor den Läufen `loginctl show-session … -p LockedHint` → `LockedHint=no`).

Auftrag: `prompts/P10a-nacharbeiten-einsatz.md`. Teil A: Nacharbeiten am Editor (Verbindungsknöpfe, Knöpfe bei «Transformation», Tastenkürzel, Punkt 66, Punkt 65). Teil B: Trennung der Version in Arbeit (Repo) und im Einsatz (installiert).

**Claude Code hat nicht nach `~/.local/opt/dons-streaming-studio/` installiert** und die echten Daten `~/.local/share/dons-streaming-studio` nicht verändert (Prüfsummen aller 11 Dateien vor und nach den Tests gleich, 2026-11-03 16:31). Die echte Einheit `~/.config/systemd/user/dss-core.service` ist unverändert (zeigt weiter auf `build/src/core/dss-core`, siehe Punkt 71). **Es wurde kein Paket installiert.**

## Kurzfassung

| Abnahme | Ergebnis |
|---|---|
| Bau ohne Warnungen mit `-Wall -Wextra`, jeder Schalter einzeln abschaltbar, Release-Bau | ja: 0 Warnungen im Arbeitsordner (Debug), im frischen Klon, im Release-Bau, mit jedem der 10 Schalter einzeln aus, mit `DSS_INSTALLED_EDITION=ON` und in den Zwischenständen `f4ae362`, `647a275`, `12245b3` (`docs/berichte/P10a-daten/schalter-ergebnis.txt`). Die Bauten mit abgeschaltetem Schalter sind nur gebaut, ihre Tests nicht ausgeführt. Neue Schalter: `DSS_VIDEO_ROTATION`, `DSS_CORE_WATCHDOG`, `DSS_INSTALLED_EDITION` |
| `ctest`, auch im frischen Klon | 32 von 32 im Arbeitsordner und im frischen Klon von `acdace6` (2026-11-03 18:19 bzw. 18:22, Bau im Klon ohne Warnungen) |
| `tst_medien` und `tst_fensterbearbeiten` je 50 Mal | je 50 von 50 (`--repeat until-fail:50`, 2026-11-03 17:52 bis 18:16), nach einer Korrektur am Test `tst_fensterbearbeiten` (der erste Lauf scheiterte in Wiederholung 40, siehe «Tests») |
| Startprüfung `--runs 20` | 20 von 20 in allen vier Fällen (2026-11-03 18:16 bis 18:18) |
| Dokumente | nachgeführt: `docs/01-festlegungen.md` (Video-Drehung, zweiter Zeichner; Tabelle der Orte je Version, Sicherung, Watchdog), `docs/04-offene-punkte.md` (41, 56, 60, 65, 66 erledigt; neu 67 bis 71), `docs/06-overlay-format.md` (Umwandeln beim Installieren), `docs/07-bauplan.md` (P10a unter «Erledigt»), `docs/08-kern-schnittstelle.md` (Verbindung und Einheit je Version, Watchdog), `README.md` (Abschnitt «Einsatz», Schalter), `CLAUDE.md` (Regel 18) |
| Handtest | offen, 5 Prüfungen (unten) |

## Teil A

### 1 – Verbindungsknöpfe mittig, Logos kleiner

Die vier Verbindungsknöpfe stehen waagrecht in der Mitte zwischen dem rechten Ende der Reiter «Overlays / Chat-Bot» und dem linken Rand der Leistungsanzeige (`headerCenteredX` in `src/common/windowsize.cpp`, Rest der Teilung rechts). Logos: **vorher 28×28, jetzt 19×19 Pixel** (≈ ⅔), ohne eigenen Rand; der Knopf bleibt 46×40 Pixel mit dem Rahmen in der Farbe des Zustands. Der feste Abstand von 70 Pixel vor den Knöpfen ist entfernt. Commit `da7c927`. Geprüft in `tst_windowsize::headerCentered` (Rechnung) und `tst_fensterbearbeiten::headerGaugesWidth` (Abstand links und rechts gleich bis auf 1 Pixel, Symbol 19×19).

### 2 – Knöpfe Kopieren, Einfügen, Zurücksetzen bei «Transformation»

Drei Symbolknöpfe am rechten Rand der Titelzeile des Bereichs «Transformation» im Dock «Eigenschaften». Symbole: vorhandenes `kopieren.svg`, neu `src/app/symbole/einfuegen.svg` (Klemmbrett) und `zuruecksetzen.svg` (Pfeil gegen den Uhrzeigersinn), selbst gezeichnet, einfarbig wie die übrigen (Maske, eingefärbt mit der Textfarbe). Hinweis beim Darüberfahren mit Kürzel, zum Beispiel «Transformation kopieren (Strg+Umschalt+C)». «Einfügen» ist ausgegraut, solange nichts kopiert ist. Die Knöpfe lösen dieselben Aktionen aus wie das Menü (E-25). Ohne `DSS_APP_TRANSFORM` sind sie verborgen. Commit `89311bc`.

### 3 – Tastenkürzel wie OBS

Strg+Umschalt+C / Strg+Umschalt+V (Transformation kopieren / einfügen), Strg+R (zurücksetzen), Strg+F (auf Leinwand einpassen), Strg+S (auf Leinwand strecken), Strg+D (zentrieren). Sie stehen im Menü «Bearbeiten → Transformieren» neben den Einträgen. Die Aktionen haben `Qt::WidgetShortcut` und hängen an der Leinwand und an der Liste des Docks «Quellen»; in Textfeldern (zum Beispiel «Name», «X») wirken sie nicht (https://doc.qt.io/qt-6.8/qt.html#ShortcutContext-enum). Die Texte «Strg» und «Umschalt» kommen aus `qtbase_de.qm`. Geprüft in `tst_fensterbearbeiten::transformButtonsAndShortcuts`: Kürzel der Aktionen, Lage der Knöpfe, Hinweise, Klicks, Strg+D/S/F/C/R auf der Leinwand, Strg+Umschalt+V in der Liste, keine Wirkung in `propertyName` und `propertyX`.

### 4 – Ruckeln bei Videos (Punkt 66)

**Messung vorher** (Stand `eba92fa`, `alpha.webm`: Zeitbasis 1/1000, Zeitstempel 0, 33, 67, 100 ms …, 300 Bilder, 30 Bilder/s). Zwei Messprogramme gegen `libdss_media.a` (`docs/berichte/P10a-daten/takt.cpp`, `leinwand.cpp`; gebaut mit `g++ -std=c++20 -O2 -I src/media … build/src/media/libdss_media.a $(pkg-config --cflags --libs Qt6Core libavformat libavcodec libavutil libswscale) -fPIC`; aufgerufen mit `prototypes/media/alpha.webm`). Gezählt wird je Takt: genau ein neues Bild, kein neues, mehrere. Takt wie die Leinwand: geplante Zeitpunkte n/30 s, Abfrage 0 bis 6 ms später.

| Messung | Datum | Ergebnis |
|---|---|---|
| `takt` vorher: Takt ab dem ersten Bild mit Versatz −2…+2 ms (0,1 ms) und 3…33 ms, je 60 Takte | 2026-11-03 15:36:35 | ungleichmässig bei Versatz −0,3 bis +0,3 ms und 33,0 ms: je etwa 20 Takte ohne neues Bild und 20 mit zweien (25 von 72 Versätzen). Bei negativem Versatz fehlt einmal das erste Bild (Eigenheit des Programms, nicht gezählt als Ruckeln) |
| `leinwand` vorher: wie die Leinwand, jeder Lauf ein neuer Player, Takt ab Start + Versatz 0…33,3 ms (0,1 ms), 334 Läufe zu 45 Takten | 2026-11-03 15:39:55 | 3 von 334 Läufen ungleichmässig (Versatz 7,9, 8,7, 9,0 ms), 90 von 15 030 Takten; in diesen Läufen kam jedes dritte Bild einen Takt zu spät und das nächste doppelt (15 neu, 15 kein, 15 mehrere) |
| `takt` nur mit Toleranz 1 ms | 2026-11-03 15:53:59 | 16 von 72 Versätzen ungleichmässig, jetzt bei −1,3 bis −0,7 ms und 32,0 ms |
| `leinwand` nur mit Toleranz 1 ms | 2026-11-03 15:56:25 | 6 von 334 Läufen (5,6 bis 7,7 ms), 180 von 15 030 Takten |
| `leinwand` mit Toleranz und Anker (Endstand) | 2026-11-03 16:24:57 | **0 von 334 Läufen, 0 von 15 030 Takten** |

Ursache: Die Zeitstempel in ganzen Millisekunden weichen bis 1/3 ms vom Raster n/30 s ab. Liegt der Takt der Leinwand fast genau auf den Zeitpunkten der Bilder, wird jedes dritte Bild einen Takt zu spät fällig. Ob das geschieht, hing vom zufälligen Abstand zwischen Beginn des Decoders und Takt der Leinwand ab.

Umsetzung (Commit `a78098c`):
- **Toleranz 1 ms** (`VideoPlayer::kDueTolerance`): Ein Bild gilt als fällig, wenn sein Zeitpunkt höchstens 1 ms nach dem gefragten liegt. Wahl: Runden auf ganze Millisekunden verschiebt höchstens um 0,5 ms (bei 30 Bildern/s um 1/3 ms); 1 ms deckt das mit Reserve und ist klein gegen einen halben Takt (16,7 ms bei 30, 8,3 ms bei 60 Bildern/s), es zieht also nie ein Bild einen ganzen Takt vor.
- **Anker:** Die Messung zeigt, dass die Toleranz allein das schlechte Fenster nur verschiebt (6 statt 3 Läufe). Deshalb ist der Beginn eines Videos jetzt der Zeitpunkt der ersten Abfrage `frameAt`, die ein Bild vorfindet, statt der Zeit, zu der der Decoder das erste Bild fertig hat. Die Warteschlange speichert den Abstand zum ersten Bild. So liegen die Bilder auf dem Raster des Takts, und die Toleranz deckt die Rundung. Bis zur ersten Abfrage wartet der Decoder mit vollem Vorrat und verwirft nichts.
- Die Leinwand überspringt nach langer Beschäftigung (mehr als 4 Takte im Rückstand) die verpassten Takte und behält ihr Raster, statt es neu zu beginnen. Sonst wären laufende Videos nach dem Rückstand nicht mehr auf dem Raster ihres Ankers.

Tests: `tst_medien::dueTolerance` (Simulation mit den echten Zeitstempeln aus `ffprobe`: ohne Toleranz 199 von 299 Takten ungleichmässig, mit 1 ms 0; Grenze genau 1 ms), `tst_medien::anchorOnTick` (Echtzeit, Versatz 7,9, 8,7, 9,0 ms wie die schlechten Läufe vorher: je 45 von 45 Takten mit genau einem neuen Bild), `oneFramePerTick` und `endBehaviour` unverändert bestanden.

### 5 – Drehung aus den Metadaten von Videos (Punkt 65)

FFmpeg liefert die Drehung als Display-Matrix (`AV_PKT_DATA_DISPLAYMATRIX` in `codecpar->coded_side_data`, https://ffmpeg.org/doxygen/7.1/group__lavc__packet__side__data.html). Der Kern rechnet sie in eine EXIF-Ausrichtung 1 bis 8 um (`displayMatrixOrientation` in `src/media/mediainfo.cpp`), nach derselben Logik wie `ffmpeg` beim automatischen Drehen (`fftools/ffmpeg_filter.c`, `fftools/cmdutils.c` `get_rotation`, `libavutil/display.c` `av_display_rotation_get`, Stand n7.1): Winkel 90 → `transpose` (mit Spiegeln bei positivem `m[3]`), 180 → `hflip`/`vflip` je nach Vorzeichen, 270 → `transpose` (mit Spiegeln bei negativem `m[3]`), 0 mit negativem `m[4]` → senkrecht gespiegelt; andere Winkel → ungedreht.
- Die **Originalgrösse** einer neuen Videoquelle folgt der Drehung (`readMediaSize`, bei 90°/270° Breite und Höhe vertauscht).
- Der **Zeichner** dreht beim Zeichnen (`SourceRenderer::drawImage` mit `orientationSteps`), das Bild selbst bleibt, wie der Decoder es liefert.
- Schalter `DSS_VIDEO_ROTATION`.

Testvideos selbst erzeugt (`tests/daten/video-drehung/erzeugen.sh`): H.264, yuv420p, 64×32 angezeigt, aus `tests/daten/exif/orientierung-1.png`. Gespeichert gedreht (`transpose=cclock`, `hflip,vflip`, `transpose=clock`) mit `-display_rotation` 270, 180, 90. ffprobe meldet die Drehung −90, −180 und 90; das automatische Drehen von `ffmpeg` gibt bei allen vier das Testbild in der richtigen Lage.

Tests: `tst_medien::displayMatrix` (9 Matrizen, doppelter Massstab, nullptr), `videoRotation` (Grösse 64×32, Ausrichtung 1/6/3/8, Farben der vier Viertel gegen das automatische Drehen von ffmpeg, Abweichung höchstens 4, gegen die Sollfarbe höchstens 12), `storeVideoRotation` (Grösse neuer Quellen im Kern), `tst_leinwand::videoRotation` (alle vier Videos auf der Leinwand gleich, Abweichung höchstens 12). Gegenprobe: Mit `orientationSteps(1)` statt der Ausrichtung des Videos schlägt `tst_leinwand::videoRotation` fehl («Video 90° bei 16/8: #00ff00 statt #ff0000»). In `docs/01-festlegungen.md` steht, dass der zweite Zeichner (P10) das auch umsetzen muss und P10 es mit OBS und diesen Testvideos prüft. Commit `4b98726`.

## Teil B

### Unterscheidung der Versionen

Festlegung: **Schalter beim Bau** `DSS_INSTALLED_EDITION` (Standard aus = Version in Arbeit). Begründung: Ein Bau aus dem Repo nutzt so immer den Entwicklungsordner, auch wenn er von einem anderen Ort startet oder kopiert wird; er kann die echten Daten nicht treffen. Eine Erkennung nach dem Ort zur Laufzeit hätte genau dieses Risiko (ein verschobener oder verlinkter Bau). `scripts/installieren.sh` baut mit `DSS_INSTALLED_EDITION=ON`. Orte und Namen in `src/common/configpaths.cpp`, Tabelle in `docs/01-festlegungen.md`, Abschnitt «Einsatz»: Daten, Einstellungen und Verbindung mit dem Zusatz `-entwicklung`, Einheit `dss-core-entwicklung.service`, Fenstertitel «DoNs Streaming Studio – Entwicklung». Die eigenen Symbole liest jede Version aus `~/.config/dons-streaming-studio/symbole/`. Im Einsatz liegen Symbole, Schriften, Be Vietnam Pro, Übersetzung und Programm-Symbol unter `<Ziel>/share/dons-streaming-studio/` (Regeln für `cmake --install`, https://cmake.org/cmake/help/v3.31/module/GNUInstallDirs.html); die installierten Programme enthalten keinen Pfad ins Repo (`strings -el`). `--version` und die Startzeile nennen Version und Commit, zum Beispiel «dss-core 0.1.0 (Einsatz, Commit 0036f8f-geändert)» (Testinstallation aus einem Stand mit nicht committeten Änderungen). Commit `f4ae362`.

Tests ohne Qt Gui: `tst_configpaths::editions` (alle Orte und Namen beider Versionen), `tst_overlaystore::defaultDataDir` (beide Datenordner), `tst_version::versionLine`, `tst_sperre` (Hinweis nennt `dss-core-entwicklung.service`).

### Programm-Symbol

`assets/branding/logo-fernseher-2400x1800.png` aus https://github.com/donsimm/donstv_Studio, Commit `f95ba54` (sha256 `fa6c5d5548f2ea2f65ff74d7e7e6a704c381e344b7d47ca788e4f672ce14ac79`, 2400×1800 RGBA; laut README dort eigenes Werk des Nutzers). Sichtbarer Kasten (Alpha ab 1, `ffmpeg … -vf "alphaextract,bbox=min_val=1"`): x 388–2031, y 312–1562 (1644×1251). Quadrat 1644×1644 ab (388, 115), oben und unten durchsichtig, skaliert mit vormultipliziertem Alpha und Lanczos (`assets/branding/symbol-erzeugen.sh`). Grössen 16, 22, 24, 32, 48, 64, 128, 256, 512 wie die Ordner `apps` des Themas hicolor auf dem Studio-PC (`/usr/share/icons/hicolor/index.theme`; 36, 72, 96, 192 nicht erzeugt). Das Innere des Bildschirms ist wie im Original durchsichtig. Fenstersymbol beider Versionen über `QApplication::setWindowIcon` (geprüft: `_NET_WM_ICON` am Fenster gesetzt, `xprop`); `tst_symbols::appIconSizes` prüft alle Grössen, durchsichtige Ränder und Farbe.

### Umwandeln beim Installieren

Neues Modul `src/common/dataformat.*` (ohne Qt Gui) und `dss-core --format-pruefen` (Exit-Code 0 aktuell, 4 Umwandeln nötig, 5 neueres Format, 1 Fehler) bzw. `--umwandeln` (nimmt die Sperre; liest alle Dateien streng, schreibt erst danach, jede atomar mit `QSaveFile`; bei einem Fehler werden schon geschriebene Dateien zurückgeschrieben). Umgewandelt werden Overlay-Dateien im Format 1 und 2 genau so, wie das Programm sie beim nächsten Speichern schreibt. Unlesbare Dateien bleiben unverändert (wie beim Laden). Commit `647a275`.

Sicherung: `installieren.sh` kopiert vor dem Umwandeln den ganzen Datenordner mit `cp -a` nach `<Datenordner>-sicherungen/<JJJJ-MM-TT_hh-mm-ss>/`, also für die echten Daten `~/.local/share/dons-streaming-studio-sicherungen/<JJJJ-MM-TT_hh-mm-ss>/`, und vergleicht sie mit `diff -r -q`. Testdaten im Format 1, 2 und 3: `tests/daten/formate/` (angelegt mit `dss-cli`, zurückgestuft nach `docs/06-overlay-format.md`).

Tests `tst_datenformat`: findet Format 1 und 2; Umwandeln ändert nur diese zwei Dateien, das Programm liest danach dasselbe, ein zweites Umwandeln ändert nichts; neueres Format, gehaltene Sperre und fehlender Ordner ändern nichts; unlesbare Datei bleibt; die Optionen von `dss-core` mit Exit-Codes 4, 3, 0, 0, 5, 1.

Mit einer **Kopie der echten Daten** (Scratch-Ordner, danach gelöscht): `installieren.sh` mit Testoptionen fand `overlays/6f845017-….json (Format 2)`, legte die Sicherung an (identisch mit den echten Daten, `diff -r -q`) und wandelte um. `dss-cli overlay-liste` und `quelle-liste neues-overlay` zeigen aus Sicherung (Format 2) und Umwandlung (Format 3) dieselben Zeilen.

### Watchdog (Punkt 41)

Der Kern liest `WATCHDOG_USEC` und `WATCHDOG_PID` wie `sd_watchdog_enabled(3)` und meldet `WATCHDOG=1` alle halbe Zeit über einen `QTimer` in der Hauptschleife (https://www.freedesktop.org/software/systemd/man/latest/sd_watchdog_enabled.html). Hängt die Hauptschleife, bleibt die Meldung aus. Einheit: `WatchdogSec=10s`, `TimeoutAbortSec=5s` (https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html: der Watchdog wirkt ab dem Ende des Starts, bei Ablauf SIGABRT bzw. `WatchdogSignal=`, `Restart=on-failure` startet neu; `TimeoutAbortSec` begrenzt das Warten danach, sonst gilt `DefaultTimeoutAbortSec`, auf dem Studio-PC 1 min 30 s laut `systemctl --user show`). Commit `12245b3`.

Wahl der Zeit: Gemessen (2026-11-03 16:18, `dss-cli quelle-bild` mit 1 GiB bzw. 4 GiB grossen PNG-Dateien, derselbe Code wie im Kern, Datei im Seitencache): Übernehmen 16,0 s bzw. 64,0 s. Das ist die längste Arbeit in der Hauptschleife; ein Watchdog unter 64 s würde den Kern dabei beenden. Deshalb meldet sich der Kern beim Übernehmen nach jedem MiB (höchstens einmal je Sekunde) zusätzlich; Fortschritt heisst, er hängt nicht. Damit genügt eine kurze Zeit. **10 s:** im normalen Lauf ist die Hauptschleife nur Millisekunden beschäftigt, Laden beim Start fällt nicht darunter (der Watchdog beginnt nach `READY=1`), und ein hängender Kern ist nach höchstens etwa 11 s wieder da. 5 s für `TimeoutAbortSec` reichen, weil der Kern auf SIGABRT sofort endet (gemessen 2 bis 3 ms).

Prüfung mit einer Testeinheit `dss-core-watchdogtest.service` (eigene Daten, eigene Verbindung; `docs/berichte/P10a-daten/watchdog-pruefen.sh`, Ergebnis `watchdog-ergebnis.txt`), 2026-11-03 16:20:59:

| Lauf | Ergebnis |
|---|---|
| 25 s ohne Eingriff | dieselbe PID, kein Neustart |
| `kill -STOP`, 1 | neue PID nach 11,0 s |
| `kill -STOP`, 2 | neue PID nach 9,2 s |
| `kill -STOP`, 3 | neue PID nach 9,1 s |

Journal: «Watchdog timeout (limit 10s)!», «Killing process … with signal SIGABRT», «Main process exited, code=killed, status=6/ABRT» 2 bis 3 ms später, Neustart nach 1 s. Der angehaltene Prozess endet sofort, weil systemd mit dem Signal SIGCONT schickt (systemd.kill(5), `KillSignal=`, https://www.freedesktop.org/software/systemd/man/latest/systemd.kill.html). Kein Kernabbild: `LimitCORESoft=0` und `core_pattern` = `core` (geprüft mit `systemctl --user show`, `/proc/sys/kernel/core_pattern`).

Übernehmen einer 4-GiB-Datei über die Schnittstelle (`add_image_source`, `watchdog-gross.py`) bei laufendem Watchdog: Antwort nach 58,0 s, kein Neustart, kein «Watchdog timeout» im Journal. Gegenprobe ohne die Meldung beim Übernehmen: systemd beendete den Kern 10 s nach Beginn mitten im Übernehmen (SIGABRT). `tst_systemdnotify` prüft das Lesen von `WATCHDOG_USEC`/`WATCHDOG_PID` und die Meldung an einen eigenen Socket.

### `scripts/installieren.sh` und `scripts/dienst-einrichten.sh`

`installieren.sh` (Commit `7e88aa9`): Abbruch mit Exit-Code 3, wenn `/proc/*/exe` auf `<Ziel>/bin/dss-app` zeigt (auch « (deleted)», proc(5)); Bau im Release-Modus in `build-einsatz/` mit `DSS_INSTALLED_EDITION=ON`, `DSS_BUILD_TESTS=OFF`, `DSS_COMMIT=<git rev-parse --short HEAD>` (mit «-geändert» bei nicht committeten Änderungen); Kern anhalten; Format prüfen, sichern, umwandeln; `cmake --install`; `version.txt`; Einheit über `dienst-einrichten.sh --einsatz`; Programm-Symbol nach `~/.local/share/icons/hicolor/<N>x<N>/apps/dons-streaming-studio.png`; Menüeintrag `~/.local/share/applications/dons-streaming-studio.desktop` (`Type=Application`, `Version=1.5`, `Exec`, `Icon=dons-streaming-studio`, `Categories=AudioVideo;Video;`, `StartupWMClass=dss-app` nach `xprop` WM_CLASS «dss-app», «dss-app»; https://specifications.freedesktop.org/desktop-entry/latest/recognized-keys.html, https://specifications.freedesktop.org/menu/latest/category-registry.html: «Video» verlangt «AudioVideo»). `desktop-file-validate` ohne Meldung. Zum Prüfen gibt es fünf Optionen für Testorte (`--ziel`, `--datenordner`, `--einheit`, `--menue-ordner`, `--symbol-ordner`), die nur alle zusammen gelten; die Einheit darf dann nicht `dss-core.service` oder `dss-core-entwicklung.service` heissen. So kann ein Test nie den echten Kern anhalten oder die echten Daten umwandeln.

`dienst-einrichten.sh`: ohne Option `dss-core-entwicklung.service` mit `build/src/core/dss-core`; mit `--einsatz ZIEL` `dss-core.service` mit `ZIEL/bin/dss-core`; `--watchdog SEKUNDEN` (Standard 10, 0 = aus).

Prüfungen der Installation in einen Testordner (Scratch-Ordner, Einheit `dss-core-installtest.service`, Testdaten `tests/daten/formate`), 2026-11-03:

| Fall | Ergebnis |
|---|---|
| 1. Lauf (16:27:38) | Exit 0 nach 41 s. Format 1 und 2 gefunden, Sicherung `…/daten-sicherungen/2026-11-03_16-28-19` (gleich den Testdaten, `tokens.json` 0600), 2 Dateien umgewandelt, danach alle Format 3. Installiert `bin/` (dss-app, dss-core, dss-cli), `share/dons-streaming-studio/` (be-vietnam-pro, branding, schriften, symbole, uebersetzung), `version.txt`, 9 Symbole, Menüeintrag. 0 Warnungen im Release-Bau. Version «(Einsatz, Commit 0036f8f-geändert)» |
| 2. Lauf direkt danach (16:28:40) | Exit 0 nach 1 s, «Format aktuell», keine neue Sicherung |
| Fenster der Testinstallation läuft | «ABBRUCH: Das Fenster von DoNs Streaming Studio läuft (PID 179067).», Exit 3, Kern läuft unverändert weiter |
| Kern läuft über die Einheit, kein Fenster | «Kern angehalten: dss-core-installtest.service», danach `inactive`, nicht wieder gestartet |
| Umwandeln scheitert (älteres Format, ein von Hand gestarteter Kern hält die Sperre) | Sicherung angelegt und genannt, «Umwandeln gescheitert; die Dateien … sind unverändert», «Nichts installiert», Exit 1; Prüfsummen vorher und nachher gleich |
| Daten in neuerem Format (Format 9) | «… neueres Format, als diese Version lesen kann; nichts installiert», Exit 1, `bin/dss-app` unverändert |

### Beide Versionen gleichzeitig

Eigenes `HOME`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME` im Scratch-Ordner, `XDG_RUNTIME_DIR` unter `/run/user/1000` (der Socket-Pfad im Scratch-Ordner war mit 141 Byte zu lang; höchstens 107). Installierte Version (Testinstallation) mit der Kopie der echten Daten, Version aus `build/` mit einem eigenen Overlay «Nur Entwicklung». Beide Kerne und beide Fenster ohne Optionen gestartet (2026-11-03 16:30):
- Kern im Einsatz: Datenordner `…/.local/share/dons-streaming-studio`, 1 Overlay, Verbindung `…/dons-streaming-studio/kern.sock`.
- Kern in Arbeit: `…/.local/share/dons-streaming-studio-entwicklung`, 1 Overlay, `…/dons-streaming-studio-entwicklung/kern.sock`.
- Jeder Kern meldete «Verbunden: dss-app 0.1.0 (1 Verbindungen)». Fenster «DoNs Streaming Studio» und «DoNs Streaming Studio – Entwicklung» gleichzeitig sichtbar (`xwininfo -root -tree`).
- Bildschirmfotos über die Standardverbindung jeder Version: Die installierte zeigt nur «Neues Overlay» (die echten Daten mit `bild1.png` und Text), die aus dem Repo nur «Nur Entwicklung». Die Fotos zeigen echte Daten und sind deshalb nicht im Repo.
- Nach dem Schliessen: `fenster.ini` je in `.config/dons-streaming-studio/` und `.config/dons-streaming-studio-entwicklung/`; beide Kerne beendeten sich auf Bitte des letzten Fensters mit Exit-Code 0; kein Prozess blieb.

## Ausgeführte Befehle und Ergebnisse

### Bau

```
cmake -S . -B build && cmake --build build -j8          # Debug, 0 Warnungen
docs/berichte/P10a-daten/schalter-lauf.sh               # frischer Klon, Zwischenstände, Release, Schalter (17:41 fertig)
```

Ergebnis `schalter-ergebnis.txt`: alle 15 Bauten Exit 0 und 0 Warnungen (Zwischenstände `f4ae362`, `647a275`, `12245b3`; Release; `DSS_BUILD_APP`, `DSS_BUILD_TESTS`, `DSS_BUILD_CLI`, `DSS_MEDIA_SIZE`, `DSS_APP_RENDERER`, `DSS_APP_CANVAS_EDIT`, `DSS_VIDEO_ROTATION`, `DSS_CORE_WATCHDOG`, `DSS_APP_EDITING`, `DSS_APP_TRANSFORM` je aus; `DSS_INSTALLED_EDITION` ein). Der Teil-A-Commit `a78098c` (Punkt 66) wurde einzeln gebaut und `tst_medien` bestand dort (2 von 2 Tests); die übrigen Teil-A-Commits sind nicht einzeln gebaut.

### Tests

```
ctest --test-dir build --output-on-failure                                            # Arbeitsordner
git clone . <scratch>/klon && cmake -S . -B build && cmake --build build -j8 && ctest --test-dir build --output-on-failure   # frischer Klon
ctest --test-dir build --repeat until-fail:50 -R '^tst_fensterbearbeiten$' --output-on-failure
ctest --test-dir build --repeat until-fail:50 -R '^tst_medien$' --output-on-failure
```

| Lauf | Datum | Ergebnis |
|---|---|---|
| `ctest` Arbeitsordner, Stand vor `acdace6` | 2026-11-03 16:34 | 32 von 32 |
| `ctest` frischer Klon von `7e88aa9` | 2026-11-03 17:41 | `tst_datenformat` fehlgeschlagen: `tokens.json` hat im Klon die Rechte 0644 (git speichert nur das Ausführungsrecht), das Format prüft 0600. Behoben in `7c5c2c2` (der Test setzt 0600, wie `installieren.sh` es mit `cp -a` übernimmt) |
| `ctest` frischer Klon von `7c5c2c2` | 2026-11-03 17:44:20 | `tst_fensterbearbeiten` einmal fehlgeschlagen, `ctest --rerun-failed` 2 von 2 bestanden. Die Ausgabe ist durch den zweiten Lauf überschrieben; vermutlich dieselbe Ursache wie in der nächsten Zeile (nicht geprüft) |
| `tst_fensterbearbeiten` 50 Mal, Stand `7c5c2c2` | 2026-11-03 17:45:38 bis 17:50:00 | 39 bestanden, Wiederholung 40 fehlgeschlagen: `headerGaugesWidth`, «Actual (cpu->width() + ram->width() + lan->width()): 600, Expected (total): 426». Das Fenster hatte nach `resize(1280, 800)` die Breite 1280, die Anzeige noch die Breite für 1920. Vermutung, nicht geprüft: `resize()` setzt die Breite sofort, die Bestätigung des Fenstermanagers für die vorherige Grösse 1920 traf erst im folgenden `processEvents()` ein. Korrektur nur im Test (`acdace6`): warten, bis Fensterbreite und Summe der Anzeige zusammen zur Sollbreite passen, dann ohne Ereignisverarbeitung prüfen; die Meldung nennt bei einem Fehler beide Werte |
| `tst_fensterbearbeiten` 50 Mal, Stand `acdace6` | 2026-11-03 17:52:37 bis 17:58:04 | **50 von 50** |
| `tst_medien` 50 Mal, Stand `acdace6` | 2026-11-03 17:58:04 bis 18:16:14 | **50 von 50** (1089,6 s) |
| `ctest` Arbeitsordner, `acdace6` | 2026-11-03 18:19:19 bis 18:20:11 | **32 von 32** |
| `ctest` frischer Klon von `acdace6` | 2026-11-03 18:20:11 bis 18:22:29 | Bau 0 Warnungen, **32 von 32** |

Ohne Qt Gui (geprüft mit `ldd build/tests/<Test>`, nur `libQt6Core` und `libQt6Test`): `tst_configpaths`, `tst_version`, `tst_overlaystore`, `tst_datenformat`, `tst_systemdnotify`, `tst_sperre`, `tst_medien`. `tst_symbols` (Programm-Symbol) braucht Qt Gui und läuft nur mit dem Fenster (`DSS_BUILD_APP`).

### Startprüfung

```
python3 scripts/startpruefung.py --runs 20 --out docs/berichte/P10a-daten    # 2026-11-03 18:16:31 bis 18:18:44
```

Vorher `cat /sys/class/tty/tty0/active` → `tty7`, `loginctl show-session … -p Active -p LockedHint` → `Active=yes`, `LockedHint=no`. Bau aus `build/` (Version in Arbeit), Fenstertitel «DoNs Streaming Studio – Entwicklung».

| Fall | Beginn | sauber | bis Start min / Median / max (ms) | Beenden min / Median / max (ms) |
|---|---|---|---|---|
| `core-sigterm` | 18:16:31 | 20 von 20 | 36,3 / 44,3 / 47,7 | 3,6 / 3,9 / 5,5 |
| `core-sigint` | 18:17:02 | 20 von 20 | 36,7 / 43,6 / 47,5 | 3,5 / 4,0 / 6,1 |
| `app-sigterm` | 18:17:33 | 20 von 20 | 207,1 / 229,1 / 243,9 | 12,3 / 15,8 / 21,4 |
| `app-kern-sigterm` | 18:18:08 | 20 von 20 | 208,9 / 225,7 / 239,1 | 14,1 / 16,8 / 21,8 |

Alle Starts mit Exit-Code 0, Fenster sichtbar (bei `app-*`), keine Restprozesse, keine unerwarteten Zeilen, Kern bei `app-kern-sigterm` mit Exit-Code 0. Einzelwerte: `docs/berichte/P10a-daten/startpruefung-*.json` und `.md`.

## Handtest

Grund (Regel 10): Aussehen und Bedienung (Kopfbereich, Knöpfe, Kürzel), die erste echte Installation (darf nur der Nutzer, CLAUDE.md Regel 18), der Start über das Anwendungsmenü mit Symbol und das Fenster mit den echten Daten lassen sich hier nicht durch einen Menschen prüfen. Was oben geprüft ist (Lage in Pixeln, Wirkung der Kürzel, Umwandeln, Sicherung, Trennung der Daten, Watchdog), muss der Nutzer nicht wiederholen. Befehle im Terminal der Xfce-Sitzung. Vorher alle Fenster von DoNs Streaming Studio schliessen.

### Prüfung 1 – Kopfbereich, Knöpfe bei «Transformation», Tastenkürzel (Version aus dem Repo)

```
cd ~/DoNs_Streaming_Studio && cmake --build build -j8 && scripts/dienst-einrichten.sh && build/src/cli/dss-cli overlay-neu "Handtest P10a" 1920 1080 && build/src/cli/dss-cli quelle-bild handtest-p10a tests/daten/exif/orientierung-1.png && build/src/cli/dss-cli quelle-video handtest-p10a tests/daten/video-drehung/drehung-90.mp4 --wiederholen && build/src/cli/dss-cli quelle-geometrie handtest-p10a 1 0 0 640 320 0 && build/src/cli/dss-cli quelle-geometrie handtest-p10a 2 700 0 640 320 0 && build/src/app/dss-app
```

Das legt die Einheit `dss-core-entwicklung.service` und im Entwicklungsordner ein Overlay mit einem Bild und einem Video an. Erwartet:
- Titel «DoNs Streaming Studio – Entwicklung», in der Taskleiste das Fernseher-Symbol.
- Kopfbereich: die vier Verbindungsknöpfe mittig zwischen «Overlays / Chat-Bot» und CPU/RAM/LAN, Logos kleiner als bisher, ohne eigenen Rand ums Logo.
- «Handtest P10a» anklicken: Bild und Video sehen gleich aus (links oben rot, rechts oben grün, links unten blau, rechts unten gelb), obwohl das Video hochkant gespeichert ist.
- Eine Quelle auf der Leinwand anklicken. Im Dock «Eigenschaften» rechts in der Titelzeile von «Transformation» drei kleine Knöpfe; beim Darüberfahren «Transformation kopieren (Strg+Umschalt+C)», «Transformation einfügen (Strg+Umschalt+V)», «Transformation zurücksetzen (Strg+R)». «Einfügen» ausgegraut, bis kopiert wurde.
- Auf der Leinwand: Strg+D zentriert, Strg+F passt ein, Strg+S streckt, Strg+R setzt zurück, Strg+Umschalt+C und (andere Quelle gewählt) Strg+Umschalt+V übertragen Lage und Grösse. Dasselbe mit Fokus in der Liste des Docks «Quellen». Im Menü «Bearbeiten → Transformieren» stehen die Kürzel neben den Einträgen.
- Ins Feld «Name» klicken und Strg+D drücken: keine Wirkung auf die Quelle.

Fenster danach schliessen.

### Prüfung 2 – Erste echte Installation

```
cd ~/DoNs_Streaming_Studio && scripts/installieren.sh
```

Erwartet (etwa 1 Minute): «Baue DoNs Streaming Studio 0.1.0 (Commit …)», «älteres Format: overlays/6f845017-9f86-4b8b-bc92-7fa1801c3165.json (Format 2)», «Sicherung des Datenordners: /home/dominik/.local/share/dons-streaming-studio-sicherungen/<Datum_Uhrzeit>», «1 Datei umgewandelt», «Einheit: dss-core.service», «Menüeintrag: …/dons-streaming-studio.desktop», am Ende «dss-app 0.1.0 (Einsatz, Commit …)» und «dss-core 0.1.0 (Einsatz, Commit …)». Steht hinter dem Commit «-geändert», hat das Repo nicht committete Änderungen. Läuft noch ein Fenster der installierten Version (bei der ersten Installation nicht möglich), bricht es ab. Die Sicherung bleibt liegen.

### Prüfung 3 – Start über das Anwendungsmenü

Im Anwendungsmenü von Xfce «DoNs Streaming Studio» suchen (zum Beispiel unter «Multimedia») und starten. Erwartet:
- Im Menü und in der Taskleiste das Fernseher-Symbol.
- Titel «DoNs Streaming Studio» ohne «Entwicklung».
- Die bisherigen echten Overlays sind da («Neues Overlay» mit `bild1.png` und Text), Status unten «Kern-Dienst: verbunden».
- «Hilfe → Über» zeigt die Version.

Das Fenster offen lassen für Prüfung 4.

### Prüfung 4 – Beide Versionen gleichzeitig

```
cd ~/DoNs_Streaming_Studio && build/src/app/dss-app
```

Erwartet: Ein zweites Fenster «DoNs Streaming Studio – Entwicklung» mit nur «Handtest P10a»; das Fenster aus dem Menü zeigt weiter die echten Overlays. Eine kleine Änderung im Entwicklungsfenster (zum Beispiel Strg+D an einer Quelle) erscheint nicht im anderen. Danach beide Fenster schliessen.

### Prüfung 5 – Aufräumen der Entwicklungsdaten

```
cd ~/DoNs_Streaming_Studio && systemctl --user stop dss-core-entwicklung.service; rm -rf ~/.local/share/dons-streaming-studio-entwicklung ~/.config/dons-streaming-studio-entwicklung && ls ~/.local/share/ | grep dons-streaming-studio
```

Erwartet: Es bleiben `dons-streaming-studio` (echte Daten) und `dons-streaming-studio-sicherungen` (Sicherung aus Prüfung 2). Die Einheit `dss-core-entwicklung.service` bleibt für die weitere Arbeit eingerichtet (entfernen ginge mit `scripts/dienst-einrichten.sh --entfernen`).

## Commits

| Commit | Inhalt |
|---|---|
| `da7c927` | Teil A 1: Verbindungsknöpfe mittig, Logos 19 statt 28 Pixel |
| `89311bc` | Teil A 2 und 3: Knöpfe bei «Transformation», Tastenkürzel wie OBS |
| `a78098c` | Teil A 4: Toleranz 1 ms und Anker (Punkt 66) |
| `4b98726` | Teil A 5: Drehung aus den Video-Metadaten (Punkt 65), Festlegung für P10 |
| `f4ae362` | Teil B 3 und 5: Versionen getrennt, Programm-Symbol, Installationsregeln |
| `647a275` | Teil B 4: Datenformate prüfen und umwandeln |
| `12245b3` | Teil B 6: Watchdog |
| `7e88aa9` | Teil B 1, 2, 5, 7: `installieren.sh`, `dienst-einrichten.sh` |
| `7c5c2c2` | `tst_datenformat` im frischen Klon (Rechte von `tokens.json`) |
| `acdace6` | `tst_fensterbearbeiten::headerGaugesWidth` wartet, bis Fenster und Anzeige zur Sollbreite passen |
| `3c2d623` | Dokumente, Bericht, Messdaten, `CLAUDE.md` Regel 18 |

Die Kennungen hier gelten nach `git pull --rebase origin main` (auf origin lag `7dd5d10`, «Übergabe: Stand für den nächsten Chat», nur `docs/00-uebergabe.md`). Gemessen und getestet wurde auf denselben Ständen vor dem Rebase; der Code ist gleich. In der wörtlichen Ausgabe «Commit 0036f8f-geändert» und in `docs/berichte/P10a-daten/schalter-*` stehen die Kennungen vor dem Rebase: `9fcddc1` = `da7c927`, `81963d9` = `89311bc`, `895a4cf` = `a78098c`, `0036f8f` = `4b98726`, `0dbba12` = `f4ae362`, `0e447aa` = `647a275`, `439c979` = `12245b3`, `85ac702` = `7e88aa9`, `1a8aeca` = `7c5c2c2`, `0c0da23` = `acdace6`, `91d90e7` = `3c2d623`.

## Geänderte und neue Dateien

- Neu: `scripts/installieren.sh`, `src/common/dataformat.h/.cpp`, `src/app/symbole/einfuegen.svg`, `zuruecksetzen.svg`, `assets/branding/logo-fernseher-2400x1800.png`, `assets/branding/symbol-erzeugen.sh`, `assets/branding/symbol/dons-streaming-studio-{16,22,24,32,48,64,128,256,512}.png`, `tests/tst_datenformat.cpp`, `tests/tst_systemdnotify.cpp`, `tests/daten/video-drehung/` (Skript, 4 Videos), `tests/daten/formate/` (3 Overlays, Liste, Token, 3 Mediendateien, LIESMICH), `docs/berichte/P10a-bericht.md`, `docs/berichte/P10a-daten/`.
- Geändert: `CMakeLists.txt`, `.gitignore` (`/build-einsatz/`), `scripts/dienst-einrichten.sh`, `scripts/startpruefung.py` (Fenstertitel je Version), `scripts/dienstpruefung.py` (lehnt beide echten Einheiten ab), `src/app/` (`CMakeLists.txt`, `docks.*`, `headerbar.cpp`, `main.cpp`, `mainwindow.*`, `mainwindowedit.cpp`, `sourcerenderer.*`, `symbols.*`), `src/cli/` (`CMakeLists.txt`, `main.cpp`), `src/common/` (`CMakeLists.txt`, `configpaths.*`, `logging.cpp`, `overlaystore.*`, `systemdnotify.*`, `version.*`, `windowsize.*`), `src/core/` (`CMakeLists.txt`, `main.cpp`), `src/media/` (`CMakeLists.txt`, `avsupport.*`, `mediainfo.*`, `videoplayer.*`), `tests/` (`CMakeLists.txt`, `tst_configpaths.cpp`, `tst_fensterbearbeiten.cpp`, `tst_leinwand.cpp`, `tst_medien.cpp`, `tst_overlaystore.cpp`, `tst_sperre.cpp`, `tst_symbols.cpp`, `tst_version.cpp`, `tst_windowsize.cpp`), `README.md`, `CLAUDE.md`, `docs/01-festlegungen.md`, `docs/04-offene-punkte.md`, `docs/06-overlay-format.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`.
- Ausserhalb des Repos (nur Tests, wieder entfernt): Testeinheiten `dss-core-watchdogtest.service` und `dss-core-installtest.service` unter `~/.config/systemd/user/`, Ordner unter `/run/user/1000/`, Scratch-Ordner. Memory von Claude Code nachgeführt.

## Nicht erledigt

- Die erste echte Installation, der Start über das Menü und das Aussehen: Handtest des Nutzers.
- Prüfung mit OBS, ob die Browserquelle die Video-Drehung beachtet: P10 (Netzwerk fehlt noch).
- Neue offene Punkte 67 bis 71 in `docs/04-offene-punkte.md` (Übernehmen grosser Dateien in der Hauptschleife, Reste von `QSaveFile` nach Absturz, alte Dateien nach `cmake --install`, ungeprüftes Zurückschreiben beim Umwandeln, Übergang der Einheit `dss-core.service` bis zur ersten Installation).

## Annahmen

- Anker (Punkt 66): Der Auftrag nennt die Toleranz; die Messung zeigt, dass sie allein nicht reicht. Der Anker ist eine begründete Ergänzung. Nebenwirkung: Bis die Leinwand das erste Bild abholt, läuft ein Video nicht weiter (vorher lief es ab dem Dekodieren).
- Der Watchdog-Schutz beim Übernehmen grosser Dateien ist nicht im Auftrag, aber nötig, damit der Watchdog den Kern nicht bei gewöhnlicher Arbeit beendet (Gegenprobe oben).
- Sicherung neben dem Datenordner (`…-sicherungen/`), Datum und Uhrzeit im Namen mit `_` und `-` (ohne `:`), damit der Name in allen Werkzeugen ohne Anführungszeichen geht.
- Menü-Kategorien `AudioVideo;Video;`; Xfce zeigt den Eintrag damit unter «Multimedia» (nicht geprüft, Handtest Prüfung 3).
- Die Einheit `dss-core-entwicklung.service` legt nicht Claude Code an, sondern der Nutzer im Handtest (Prüfung 1), wie bisher bei `dss-core.service`.
- Bis zur ersten Installation startet die bisherige Einheit `dss-core.service` den Bau aus `build/`, der jetzt die Entwicklungsdaten nutzt (Punkt 71); die Installation schreibt die Einheit neu.

## `[OFFEN]`

- keine

## Quellen

- systemd: https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html (`WatchdogSec=`, `Restart=`, `TimeoutAbortSec=`), https://www.freedesktop.org/software/systemd/man/latest/sd_watchdog_enabled.html, https://www.freedesktop.org/software/systemd/man/latest/systemd.kill.html (SIGCONT), https://www.freedesktop.org/software/systemd/man/latest/sd_notify.html, https://www.freedesktop.org/software/systemd/man/latest/systemd.unit.html
- freedesktop: https://specifications.freedesktop.org/desktop-entry/latest/recognized-keys.html, https://specifications.freedesktop.org/menu/latest/category-registry.html, https://specifications.freedesktop.org/icon-theme/latest/
- FFmpeg 7.1: https://ffmpeg.org/doxygen/7.1/group__lavc__packet__side__data.html, `fftools/ffmpeg_filter.c`, `fftools/cmdutils.c`, `libavutil/display.c` (Tag n7.1, https://git.ffmpeg.org/gitweb/ffmpeg.git), https://ffmpeg.org/ffmpeg-filters.html#premultiply, https://ffmpeg.org/ffmpeg-filters.html#scale, https://ffmpeg.org/ffmpeg-filters.html#bbox
- Qt 6.8: https://doc.qt.io/qt-6.8/qt.html#ShortcutContext-enum, https://doc.qt.io/qt-6.8/qsavefile.html, https://doc.qt.io/qt-6.8/qstandardpaths.html, https://doc.qt.io/qt-6.8/qguiapplication.html#windowIcon-prop, https://doc.qt.io/qt-6.8/qguiapplication.html#desktopFileName-prop, https://doc.qt.io/qt-6.8/qpainter.html#coordinate-transformations
- CMake: https://cmake.org/cmake/help/v3.31/module/GNUInstallDirs.html, https://cmake.org/cmake/help/v3.31/manual/cmake.1.html#install-a-project
- OBS (Tastenkürzel): `frontend/forms/OBSBasic.ui`, Zeilen 1726–1785 (Bericht P09b, Punkt 60)
- Programm-Symbol: https://github.com/donsimm/donstv_Studio, Commit `f95ba54`, `assets/branding/logo-fernseher-2400x1800.png`
- Linux: proc(5) (`/proc/<pid>/exe`), unix(7) (Länge von `sun_path`)
