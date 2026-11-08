# P11 – Bildvergleich der zwei Zeichner (Plan)

Plan des Projektleiter-Agenten vom 2026-11-08, Grundlage: Vorplan `~/.claude/lotse/berichte/dss-p11-plan-2026-11-07.md`. Ausgeführt in Teilen: P11a (Commits 1–3), Prüfung V durch Lotse, P11b (Commits 4–7).

## Entscheide des Nutzers

- 2026-11-08 (Panel, Vorplan): OBS ist massgebend; nur erzeugte Testdateien im Repo; HEVC und ProRes in Prüfung V nur notieren.
- 2026-11-08 (Sitzung): Lotse löscht die eigene Testquelle in «test 2» nach der Prüfung (Frage 1: A). Der automatische Test vergleicht mit Qt WebEngine und hält den Unterschied zu OBS fest, kein neues Paket (Frage 2: A). Die Messung gegen OBS reicht, der Nutzer sieht nicht selbst nach (Frage 3: A).

## Wichtigste neue Befunde

| Befund | Beleg |
|---|---|
| Der Editor rechnet **alle** Videos mit der Farbmatrix BT.601 (eingeschränkter Bereich). swscale nimmt ohne `sws_setColorspaceDetails` immer `SWS_CS_DEFAULT = 5` (BT.601) und liest die Farbangaben des Bildes nicht. | `src/media/videoplayer.cpp:280-284`; https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libswscale/swscale.h (`#define SWS_CS_DEFAULT 5`), https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libswscale/utils.c (`sws_init_single_context`) |
| Chromium bei H.264 ohne Farbangabe: unter 720 Pixel Höhe BT.601, ab 720 BT.709. Folge: Echte HD-Videos (mit und ohne Angabe) zeigt der Editor heute mit falscher Matrix. P10 hat nur mit 64x32 gemessen. | https://raw.githubusercontent.com/chromium/chromium/122.0.6261.171/media/ffmpeg/ffmpeg_common.cc (`natural_size.height() < 720 ? REC601() : REC709()`) |
| Bei VP9 gilt die Höhenregel nicht. Im FFmpeg-Pfad bleibt der Farbraum ohne Angabe undefiniert, im WebM-Parser gilt BT.709. Welcher Pfad bei `<video src>` greift, ist nicht belegt; das klärt die Messung. | gleiche Quelle; `media/formats/webm/webm_video_client.cc`, `media/filters/vpx_video_decoder.cc` (Tag 122.0.6261.171) |
| Qt WebEngine 6.8.2 enthält Chromium 122.0.6261.171. | https://wiki.qt.io/QtWebEngine/ChromiumVersions |
| WebM-Drehung: Chromium 122 übernimmt sie im WebM-Parser nicht (`kNoTransformation`), neuere Versionen schon, aber nur mit `ProjectionType`. Laut Quellcode schreibt FFmpeg 7.1 (`matroskaenc.c`) kein `ProjectionType`. Der FFmpeg-Leser macht aus `ProjectionPoseRoll` eine Display-Matrix. Die beiden Chromium-Versionen können sich also unterschiedlich verhalten. Den Bedingungen und Vorzeichen in FFmpeg habe ich nur in einer Zusammenfassung gelesen, nicht wörtlich. | `webm_video_client.cc`, `webm_projection_parser.cc` (122 und main); https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libavformat/matroskadec.c, `…/matroskaenc.c`; https://ffmpeg.org/ffmpeg.html (`-display_rotation`) |
| OBS 33.0 hat CEF von 127 auf 150 aktualisiert. Auf dem Streaming-PC läuft OBS 32.2.2 (rpm von Nobara). Welche CEF-Version es nutzt, ist `[OFFEN]`. | https://github.com/obsproject/obs-studio/releases/tag/33.0.0; `docs/02-umgebung.md:89` |
| `GetSourceScreenshot` liefert ein Bild einer Quelle in voller Auflösung (`imageFormat` ist Pflicht). Dass die Quelle dafür aktiv sein muss, steht nicht in der Dokumentation. | https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md |

## 1. Umfang

**Gehört zu P11:**
- K-08 als feste Prüfung in `ctest`: Test-Overlays, beide Zeichner, Masszahl, gemessene Grenze, absichtliche Fehler.
- Punkt 79 mit OBS als Massstab (Vorgabe des Nutzers).
- Punkt 80.
- Prüfung V in OBS, ausgeführt von Lotse; HEVC und ProRes werden dabei nur notiert (Vorgabe).

**Gehört nicht zu P11:**
- Ton (P12), neue Eigenschaften, Änderungen an `web/overlay.js` ohne gemessenen Fehler.
- Die Punkte 81, 87 und 88.
- Einstellungen in OBS ändern.
- Die Meldung «Sicherung weicht ab» aus der Übergabe.

**Teilung wie im Vorplan:**
- **P11a:** Testdaten und Messgerät, Ausgangswerte.
- **Prüfung V durch Lotse.**
- **P11b:** Punkte 79 und 80, Grenze.

Grund für die Teilung: Der bauende Agent hat keine Rechte für OBS.

## 2. Zweiter Zeichner ohne sichtbares Fenster

- **Vorhanden** (gelesen in `/var/lib/dpkg/status`):
  - Qt WebEngine `libqt6webenginecore6` 6.8.2+dfsg-4 (Chromium 122)
  - `qt6-webengine-dev` 6.8.2+dfsg-4, `libqt6gui6` 6.8.2+dfsg-9+deb13u2
  - FFmpeg 7:7.1.5-0+deb13u1 (`ffmpeg`, `libavcodec61`, `libswscale8`, Entwicklungspakete), `libvpx9` 1.15.0-2.1+deb13u1
  - `/usr/bin/ffmpeg`, `/usr/bin/ffprobe`
- **Nicht installiert:** `chromium`, `chromium-headless-shell`, ImageMagick, Xvfb.
- **Vorschlag, ohne Installation:** Qt WebEngine wie in `tests/tst_browserquelle.cpp:387-395` (Abgriff mit `grabFramebuffer`, Zeile 215).
  - Das Fenster liegt ausserhalb des Bildschirms und nimmt keinen Fokus.
  - Laut P02 rendert Qt WebEngine dort weiter (`docs/04-offene-punkte.md:36`).
  - Regel 6 (echtes X11) bleibt erfüllt.
- **Alternative, nur falls nötig:** `chromium-headless-shell` 156.0.8078.73-1~deb13u1 aus trixie-security (gelesen in `/var/lib/apt/lists/…trixie-security_main_binary-amd64_Packages`).
  - Nachteil: Die Version entspricht weder Qt WebEngine noch OBS.
  - Bringt nur etwas, wenn sie OBS nachweislich näher ist (siehe Frage 2).

## 3. Test-Overlays und Daten (Regel 7, nur erzeugte Dateien)

Neuer Ordner `tests/daten/bildvergleich/` mit `erzeugen.sh` (ffmpeg lavfi) und `LIESMICH.md`, die Dateien eingefroren im Repo. Wiederverwendet werden `tests/daten/exif/`, `tests/daten/video-drehung/`, Urbanist, Be Vietnam Pro und DejaVu. `prototypes/media/alpha.webm` entsteht nur zur Laufzeit (`tests/CMakeLists.txt:37-38`), deshalb kommt ein eigenes WebM mit Alpha in den neuen Ordner.

| Overlay | Inhalt |
|---|---|
| Bild | EXIF 1–8 (JPEG, PNG), PNG mit Alpha, vergrössert und verkleinert, gespiegelt, um 12.5° gedreht, überlagert |
| Text | DejaVu, Be Vietnam Pro, Urbanist in zwei Gewichten, alle Ausrichtungen, Farbe mit Alpha, gedreht |
| Videofarbe | deckende Farbfelder: Grundfarben, 75-%-Balken, Graustufen, Hauttöne. H.264 in 640x360, 1280x719, 1280x720 und 1920x1080, je ohne Angabe, mit BT.709, mit BT.601 (smpte170m) und mit vollem Bereich. VP9 in WebM ohne Angabe und mit BT.709 |
| WebM | VP9 mit Drehangabe (`-display_rotation` 90/180/270), dazu eine Datei mit Alpha |
| Gesamt | 1920x1080, gemischt wie im Einsatz |

- **Festes Einzelbild (K-05):** Alle Bilder eines Farbvideos sind gleich. Zusätzlich gibt es ein Video mit unterschiedlichen Bildern und «Am Ende anhalten», das ein falsches Bild aufdeckt.
- **Overlays:** Sie entstehen zur Laufzeit über `OverlayStore`, wie in `tst_browserquelle`.

## 4. Vergleich und Grenze

- **Masszahlen je Bereich und Quellenart:** vormultipliziert, Kanäle 0–255.
  - Mittel
  - grösste Abweichung ausserhalb der Kanten
  - Anteil der Pixel über 32 an den Kanten
- **Kantenmaske:** Helligkeits- oder Alphasprung in einem der beiden Bilder, um 1 Pixel erweitert. Die Logik kommt aus `tst_browserquelle.cpp:80-107` in `tests/bildvergleich.{h,cpp}`.
- **Grenze festlegen:**
  1. Erst messen, wenn die Punkte 79 und 80 umgesetzt sind.
  2. 30 Läufe ohne Last daneben.
  3. Grenze = grösster Wert plus Reserve, aufgerundet.
  4. Jeder absichtliche Fehler muss mindestens das Doppelte der Grenze erreichen. Absichtliche Fehler: 1 Pixel verschoben, 1° gedreht, falsch gespiegelt, falsche EXIF-Angabe, Alpha um 8 daneben, BT.601 statt BT.709, falsches Videobild, falsches Schriftgewicht, Grundlinie 1 Pixel daneben.
  5. 50 neue Läufe bestätigen die Grenze.
  6. Reicht der Abstand nicht, wird `[OFFEN]` gemeldet; die Grenze wird nicht zurechtgebogen.
- **Bei Überschreitung:** Der Test scheitert mit Bereich und Zahlen und speichert Seite, Editor und Differenzbild unter `build/`. Nach K-07 kommt eine neue Eigenschaft dann nicht dazu. Eine neue Grenze gibt es nur mit Commit und Begründung im Bericht.
- **OBS als Massstab:** `ctest` vergleicht den Editor mit Qt WebEngine. Weicht OBS in der Prüfung V von Qt WebEngine ab, folgt der Editor OBS. Den gemessenen Unterschied zu Qt WebEngine hält der Test dann je Fall als bekannten Wert fest. Prüfung V wird bei K-07-Eigenschaften und nach einem OBS-Update wiederholt.

## 5. Punkte 79 und 80

**Punkt 79, Ist-Zustand:**
- `videoplayer.cpp:280-284`: nur `sws_getCachedContext`/`sws_scale`, also immer BT.601, und die Grundfarben werden nicht umgerechnet.
- `src/render/mediapreview.cpp:61` nutzt `VideoPlayer`, die Vorschaubilder sind also gleich betroffen.

**Punkt 79, Vorschlag:**
- Neue Logik `src/media/videocolor.{h,cpp}` ohne Qt Gui (Regel 9). Sie bestimmt aus den Angaben der Datei Matrix, Bereich, Grundfarben und Übertragungskurve.
- Fehlt eine Angabe, gilt die Regel des Massstabs: H.264 und andere wie Chromium (Höhe unter 720 → BT.601/SMPTE170M, sonst BT.709); VP9 nach der Messung in P11a.
- Im Decoder-Thread `sws_setColorspaceDetails` mit `sws_getCoefficients(...)`, Quellbereich aus der Datei, Ziel voller Bereich.
- Grundfarben nur umrechnen, wenn sie nicht BT.709 sind, mit Tabellen in ganzen Zahlen vor dem Vormultiplizieren.
  - Betrifft vor allem SD-Videos.
  - Die Übertragungskurve legen erst die Graustufen der Messung fest; mit Werten 0 und 1 allein ist sie nicht bestimmbar (Vorplan, Zeile 49).
- **Sichtbare Folge:** Fast alle HD-Videos sehen im Editor danach anders aus, nämlich wie in OBS. Das ist die beschlossene Richtung.

**Punkt 80, Ist-Zustand:**
- Editor und Kern lesen die Display-Matrix aus jedem Container (`src/media/avsupport.cpp:76-82`, `src/media/mediainfo.cpp:88-90`).
- Der Server neutralisiert die Drehung nur bei JPEG, PNG und MP4/MOV (`src/server/mediapatch.h:32-35`).
- Die Seite wendet die Ausrichtung des Kerns immer an (`web/overlay.js:104-113`, `:238`).

**Punkt 80, Vorschlag:**
- WebM mit Drehung im Editor, in Qt WebEngine und in OBS vergleichen.
- Weicht **einer** der beiden Browser ab (doppelt oder gar nicht gedreht), setzt der Server `ProjectionPoseRoll` in den gesendeten Bytes auf 0, bei gleicher Länge.
- Das deckt sich mit dem Entscheid vom 2026-11-04. Wegen der unterschiedlichen Chromium-Versionen ist eine Abweichung wahrscheinlich.

## 6. Commits und Prüfstufe «Baustein»

**Commits (Regel 8):**

P11a:
1. Testdaten
2. Vergleichslogik mit `tst_bildvergleichlogik` (ohne Fenster)
3. `tst_bildvergleich`, noch ohne feste Grenze. Mit `DSS_BILDVERGLEICH_OBS=…` (Adresse, Szene, Pfad und Name der Zugangsdatei) legt er die Browserquelle über obs-websocket an, holt `GetSourceScreenshot` und vergleicht mit derselben Logik. Ohne diese Variable wird der OBS-Teil übersprungen. Dazu `ObsSecretStore` mit wählbarem Namen der Zugangsdatei (heute fest `dss-obs-passwort`, `src/obs/obssecretstore.cpp:15`; die Datei von Lotse heisst `obs-websocket`), Test in `tst_obspasswort`.

→ Halt: Ausgangswerte an Lotse, dann Prüfung V.

P11b:

4. Videofarben mit `tst_videofarbe`; `scripts/leistung.py` bekommt `--video` für den ungünstigsten Fall
5. WebM-Drehung (Test, nur bei Abweichung `mediapatch` mit Test in `tst_overlayserver`)
6. Feste Grenze; die Toleranz 80 in `tst_browserquelle.cpp:279` nachführen
7. Dokumente und Bericht `docs/archiv/berichte/P11-bericht.md`

**Befehle** (wie im Bericht P10c, `docs/archiv/berichte/P10c-bericht.md:20-24`):
- `cmake --build build -j8`
- `ctest --test-dir build`: X11-Sitzung aktiv, **0 übersprungen**
- frischer Klon: `git clone --branch <branch>`, `cmake -S -B`, `cmake --build -j8`, `ctest`
- `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p11/start/startpruefung`
- `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p11/dienst`

**50 Wiederholungen** (Video und Decoder-Thread sind betroffen): `scripts/wiederholen.sh build <Test> 50 build/pruefung-p11/wdh` für
- `tst_bildvergleich`
- `tst_leinwand`
- `tst_leinwandbild`
- `tst_browserquelle`
- `tst_medien`
- dazu `tst_overlayserver`, falls Commit 5 den Server ändert

**Leistung:** `python3 scripts/leistung.py` vor und nach Commit 4, mit zwei Videos 1080p30 mit BT.709 und mit SMPTE170M. Ziel: 30 Bilder/s ohne Einbrüche, höchstens 5 Prozentpunkte eines Kerns mehr CPU.

## 7. Handtest (Regel 10)

**Prüfung V, prüft Lotse** (nach P11a, nicht während eines Streams):
- **Warum:** Nur dort zeigt sich, wie das Chromium in OBS rechnet; OBS ist massgebend.
- **Ablauf:**
  1. Vorher Prozess und Ausgabe von OBS prüfen.
  2. Per SSH lesend notieren: OBS-Version, CEF-Version, `BrowserHWAccel` in `~/.config/obs-studio/global.ini`.
  3. `tst_bildvergleich` mit `DSS_BILDVERGLEICH_OBS`: Die Browserquelle kommt in die Szene «test 2», die der Nutzer angelegt hat.
  4. Bilder der Quelle holen und Farben, WebM-Drehung, HEVC und ProRes vergleichen (nur notieren).
  5. Bleibt das Bild leer, die Szene wechseln (erlaubt).

**Prüfung N, prüft Lotse** (nach P11b):
- Prüfung V wiederholen.
- Ziel bei Videos: Abweichung vom Editor im Mittel unter 2.
- Danach ein Bildschirmfoto des Editors auf dem Studio-PC mit dem Overlay «Videofarbe».

**Prüft der Nutzer:** Kein Handtest nötig, ausser der Nutzer wählt bei Frage 3 «selbst ansehen».

## 8. Risiken und `[OFFEN]`

| Risiko oder offene Frage | Gegenmassnahme oder Stand |
|---|---|
| CEF-Version von OBS 32.2.2 (rpm von Nobara) | `[OFFEN]`, Lotse liest sie bei Prüfung V |
| Ob OBS eine Browserquelle ausserhalb der aktiven Szene neu zeichnet | `[OFFEN]`; sonst die Szene wechseln |
| Ob das Bild von `GetSourceScreenshot` Alpha hat | `[OFFEN]`; Farbfelder deckend, verglichen wird nur Deckendes |
| GPU- und Software-Weg rechnen in Chromium unterschiedlich | Weg im Bericht festhalten |
| Wie Chromium Grundfarben und Übertragungskurve bei SMPTE170M umrechnet | Modell aus dem Vorplan passt auf ±1; Mitteltöne nicht belegt, misst P11a |
| Mehr CPU-Last durch die Umrechnung der Grundfarben | Messung; über 5 Prozentpunkte `[OFFEN]` |
| Unzuverlässige Fenstertests | 30 und 50 Läufe, nie gleichzeitig |
| WebM mit `ProjectionType` von anderen Programmen | `[OFFEN]` (`mkvtoolnix` fehlt) |
| `ffprobe` der Testmedien und der echten Medien | nicht geprüft (Bash gesperrt) |

## Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| «Ohne Fenster» heisst bei Chromium: echtes X11-Fenster ausserhalb des Bildschirms | Regel 6, in P02 gemessen; nichts zu installieren |
| P11 geteilt in P11a und P11b | Prüfung V braucht Rechte, die nur Lotse hat |
| OBS-Vergleich im selben Test über eine Umgebungsvariable | Dieselbe Masszahl, Muster wie `DSS_BROWSERQUELLE_LAST` |
| WebM wird neutralisiert, wenn einer der beiden Browser abweicht | OBS ist massgebend, und die Chromium-Versionen verhalten sich verschieden |
| Kein neuer Schalter beim Bau, keine neue Bibliothek | Regel 5 und 8; die Änderung ist klein und gemessen, `libavfilter` wird nicht gebraucht |
| Festes Einzelbild über gleiche Bilder bzw. «Am Ende anhalten» | Kein Testcode im Programm nötig |

