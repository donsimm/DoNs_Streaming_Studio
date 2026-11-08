# P11 – Bericht Bildvergleich der zwei Zeichner (K-08, Punkte 79 und 80)

Stand: 2026-11-08. Plan: `docs/archiv/prompts/P11-bildvergleich.md`. Ausgeführt in drei Teilen: P11a (Testdaten, Vergleichslogik, `tst_bildvergleich` ohne Grenze), Prüfung V durch Lotse gegen das echte OBS (OBS 32.2.2 auf Nobara 44, CEF/Chromium 146.0.7680.177, `BrowserHWAccel=true`; der Studio-PC hat Qt WebEngine mit Chromium 122), P11b (Videofarben, WebM-Drehung, Grenze, dieser Bericht). Stufe «Baustein». **Prüfung N (gegen das echte OBS nach P11b) steht aus.**

## Geändert

- **Testdaten und Vergleich (P11a):** `tests/daten/bildvergleich/` (`erzeugen.sh` mit ffmpeg, `LIESMICH.md`, Bilder und Videos), `tests/bildvergleich.{h,cpp}` (Kantenmaske, Masszahlen), `tst_bildvergleichlogik`, `tst_bildvergleich` (fünf Test-Overlays «Bild», «Text», «Videofarbe», «WebM», «Gesamt», dazu «Weitere Formate» nur zum Notieren; Weg über obs-websocket gegen den Testserver oder mit `DSS_BILDVERGLEICH_OBS` gegen das echte OBS), `tests/obstestserver.h` (`GetSourceScreenshot`, `RemoveInput`), `src/obs/obssecretstore.*` (Name der Zugangsdatei wählbar, für die Datei von Lotse).
- **Videofarben (Punkt 79):** `src/media/videocolor.{h,cpp}` (neu, Wahl von Matrix und Bereich), `src/media/videoplayer.{h,cpp}` (`sws_setColorspaceDetails`, Umrechnung in den Statistiken), `src/media/CMakeLists.txt`; Test `tst_videofarbe` (neu); `scripts/leistung.py --video`.
- **WebM-Drehung (Punkt 80):** `src/server/mediapatch.{h,cpp}` (EBML-Leser, `ProjectionPoseYaw` und `ProjectionPoseRoll` auf 0.0); Tests in `tst_overlayserver` (`webmRotationNeutralized`), Ausrichtung in `tst_bildvergleich`.
- **Bild von `GetSourceScreenshot`:** `tests/bildvergleich.{h,cpp}` (`fromObsScreenshot`, `toObsScreenshot`), `tst_bildvergleichlogik`, `tst_bildvergleich`; die Warnung «Entschlüsseln fehlgeschlagen» beim absichtlich falschen Namen ist nun erwartet (`QTest::ignoreMessage`).
- **Feste Grenze:** `tst_bildvergleich` (`kKindLimits`, `kKnown`, Slot `absichtlicheFehler`), `tst_browserquelle` (Toleranz der Videos getrennt für Editor und Seite).
- **Punkt 101 (Nachtrag):** `tst_bildvergleich` (Grenze Text, eigener Wert «Gesamt|Text über dem Video», `QEXPECT_FAIL` entfernt).
- **Dokumente:** `docs/01-festlegungen.md` («Farben von Videos»), `docs/04-offene-punkte.md` (79 und 80 ins Archiv, neue Punkte 96 bis 102), `docs/05-quellen.md`, `docs/archiv/04-erledigte-punkte.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md` (Overlay-Server, WebM).

## Befunde und Entscheide

### Videofarben (Punkt 79)

Der Editor rechnete jedes Video mit BT.601 (swscale ohne `sws_setColorspaceDetails`). Prüfung V: Bei SD und SMPTE170M lag der Editor schon 0.5 neben OBS, bei HD ohne Angabe und BT.709 2.5 (höchstens 31), beim vollen Bereich 2.9 (höchstens 32). Chromium wählt die Matrix in `AVStreamToVideoDecoderConfig` (122 und 146 gleich): Angabe der Datei, sonst `natural_size.height() < 720 ? REC601 : REC709`, ausser bei VP9 und AV1; voller Bereich nur mit `AVCOL_RANGE_JPEG`. Der Editor macht nun dasselbe (`chooseVideoColor`). Grundfarben werden nicht umgerechnet: OBS tut es bei SMPTE170M nicht (Editor ohne Umrechnung 0.5 neben OBS), Qt WebEngine 122 schon (deshalb 4.5 bis 4.7 zwischen Seite und Editor bei SD, als bekannter Wert festgehalten).

`tst_videofarbe` prüft die Regel (23 Fälle) und alle zehn Farbvideos der Testdaten über `VideoPlayer` gegen ein Modell (Kodieren mit der Matrix aus `erzeugen.sh`, Runden, Dekodieren mit der erwarteten Matrix): höchstens 3 daneben, Toleranz 4; die jeweils andere Matrix läge 32 bis 39 daneben.

### WebM-Drehung (Punkt 80)

Prüfung V: OBS–Seite 7 im Mittel, OBS–Editor 95 bis 125. In den Bildern von Prüfung V standen die Viertel bei OBS und der Seite gleich, beim Editor anders: Qt WebEngine (Chromium 122) und OBS (Chromium 146) drehen WebM mit Drehangabe selbst, und die Seite drehte nach `media[…].orientation` ein zweites Mal (90° und 270° um 180° falsch, 180° gar nicht gedreht). Grund: Chromium liest die Display-Matrix aus `codecpar->coded_side_data` von FFmpeg, ohne auf den Container zu schauen; FFmpeg erzeugt sie bei Matroska aus `ProjectionPoseYaw`/`ProjectionPoseRoll`.

Entscheid (Interna, Plan Punkt 80 «bei Abweichung neutralisieren»): Der Server setzt wie bei MP4 Yaw und Roll jeder Videospur in den gelieferten Bytes auf 0.0, nur bei Projektion «rechteckig» (Typ 0 oder fehlend), mit gleicher Länge; die Seite dreht wie der Editor. So hängt das Ergebnis nicht von der Chromium-Version ab (wie bei JPEG, PNG und MP4, `docs/01-festlegungen.md`, «Video in der Browserquelle»). Die Alternative, die Drehung in der Seite bei WebM wegzulassen, hätte von der Chromium-Version abgehangen. Nach dem Commit: Ausrichtung von Editor, Seite und Testserver gleich; Seite–Editor bei `drehung-*.webm` 6.9 im Mittel, das ist die bekannte Farbe von VP9 ohne Angabe in Qt WebEngine.

### Alpha im Bild von OBS

Prüfung V: Bei PNG und WebM mit Alpha lag OBS 13 im Mittel neben beiden Zeichnern (höchstens 64 bis 77), auch dort, wo Seite und Editor gleich sind. Das passt auf doppeltes Vormultiplizieren: obs-browser zeichnet die Quelle vormultipliziert (`GS_BLEND_ONE`, `GS_BLEND_INVSRCALPHA`), obs-websocket 5.7.4 kopiert die Bytes in `TakeSourceScreenshot` unverändert in ein `QImage::Format_RGBA8888` (nicht vormultipliziert) und schickt sie als PNG; der Test las sie als nicht vormultipliziert und multiplizierte ein zweites Mal. Vorhergesagt aus den Testdaten: Mittel 13.2, grösste Abweichung 255/4 = 64. Der Test liest das Bild nun als vormultipliziert (`bv::fromObsScreenshot`) und zählt Pixel mit Farbe grösser als Alpha als Gegenprobe; am Zeichner ändert sich nichts. Bestätigt wird das erst in Prüfung N (**nicht geprüft** gegen das echte OBS).

### Text, HEVC, ProRes (nur festgehalten)

- Text, Prüfung V: OBS–Seite 0.6 bis 2.4, OBS–Editor 1.2 bis 6.5; am grössten Urbanist 800 und Be Vietnam Pro klein. Seite–Editor in den 30 Läufen siehe «Grenze».
- HEVC: spielen weder Qt WebEngine noch OBS ab (beide leer, OBS–Seite 0). ProRes gibt es nur in MOV, das Studio nimmt MOV nicht an (Punkt 88).

## Grenze (K-08)

Gemessen mit `scripts/wiederholen.sh build tst_bildvergleich 30 <Ordner>`, 2026-11-08 19:38 bis 19:51, 30 von 30 bestanden. Jede Masszahl war in allen 30 Läufen gleich (`build/tests/bildvergleich/zusammenfassung.tsv`, kleinster = grösster Wert). Grenze = grösster Wert × 1.25, mindestens Mittel + 0.1, grösste Abweichung + 2, Kanten + 1 Prozentpunkt, aufgerundet auf 0.1, ganze Zahl bzw. ganze Prozent. Geprüft wird Seite (Qt WebEngine) gegen Editor; jede der drei Masszahlen muss unter der Grenze bleiben.

| Grenze | Mittel | grösste ausserhalb Kanten | über 32 an Kanten | Grundlage (grösster Wert) |
|---|---|---|---|---|
| Bild (je Bereich) | 1.5 | 4 | 13 % | 1.124 verkleinert, 2, 10.00 % gedreht 12.5° |
| Text (je Bereich) | 1.9 | 2 | 21 % | 1.466 «Text rechts» (Gesamt), 0, 16.73 % Be Vietnam Pro gedreht (seit Punkt 101; vorher 2.0, 4, 21 % aus «Text über dem Video») |
| Video (je Bereich) | 1.6 | 6 | 7 % | 1.250 `bildwechsel.mp4`, 4, 5.37 % Video BT.709 1280x720 (Gesamt) |

Bekannte Werte je Fall (`kKnown`), gleiche Reserve:

| Overlay, Bereich | gemessen (Mittel, grösste, Kanten) | Grenze | Grund |
|---|---|---|---|
| Videofarbe, `h264-640x360-ohne.mp4` | 4.746, 70, 24.95 % | 6.0, 88, 32 % | Qt WebEngine 122 rechnet die Grundfarben um, OBS nicht |
| Videofarbe, `h264-1280x718-ohne.mp4` | 4.538, 70, 21.24 % | 5.7, 88, 27 % | ebenso |
| Videofarbe, `h264-1920x1080-bt601.mp4` | 4.541, 70, 24.99 % | 5.7, 88, 32 % | ebenso (SMPTE170M) |
| Videofarbe, `vp9-640x360-ohne.webm` | 3.732, 40, 11.86 % | 4.7, 50, 15 % | VP9 ohne Angabe, Qt WebEngine anders als OBS |
| Videofarbe, `vp9-1920x1080-ohne.webm` | 3.154, 32, 0.00 % | 4.0, 40, 1 % | ebenso |
| WebM, `drehung-90/180/270.webm` | 6.94/7.01/6.93, 39, 36.30/38.71/37.62 % | 8.7/8.8/8.7, 49, 46/49/48 % | ebenso (VP9 ohne Angabe) |
| WebM, `alpha.webm` 1:1 und vergrössert; Gesamt, «WebM mit Alpha» | 4.27/4.25, 38/39, 22.76/21.24 % | 5.4, 48/49, 29/27 % | ebenso |
| Text, Urbanist 800 Mitte | 6.193, 0, 27.86 % | 7.8, 2, 35 % | Punkt 96 |
| Text, Be Vietnam Pro klein | 6.009, 0, 27.41 % | 7.6, 2, 35 % | Punkt 96 |
| Gesamt, «Text über dem Video» | 1.534, 2, 7.11 % | 2.0, 4, 9 % | Text über einem Video; eigener Wert, damit die Grenze Text nicht steigt (Punkt 101, Entscheid des Nutzers 2026-11-08) |
| ganzes Overlay Bild / Text / Videofarbe / WebM / Gesamt | 0.108/1.422/1.607/1.295/0.618, 2/0/70/39/39, 1.34/15.93/7.34/22.60/4.73 % | 0.3/1.8/2.1/1.7/0.8, 4/2/88/49/49, 3/20/10/29/6 % | enthält die Fälle oben |

Nicht geprüft: «Weitere Formate» (HEVC, nur notiert) und die Zusammenfassungen «alle (Art)».

Absichtliche Fehler (Slot `absichtlicheFehler`, Editorbild absichtlich falsch gegen dieselbe Seite), Lauf 2026-11-08 nach Commit 6:

| Fehler | Bereich | Mittel, grösste, Kanten | Vielfaches der Grenze |
|---|---|---|---|
| 1 Pixel verschoben | Bild, vergrössert | 3.346, 5, 17.36 % | 2.2 |
| 1° gedreht | Bild, gedreht 12.5° | 4.638, 8, 22.39 % | 3.1 |
| falsch gespiegelt | Bild, gespiegelt waagrecht | 157.161, 255, 100 % | 104.8 |
| falsche EXIF-Angabe (6 als 8) | Bild, JPEG mit EXIF 1–8 | 10.805, 255, 11.11 % | 63.8 |
| Alpha um 8 daneben | Bild, PNG mit Alpha 1:1 | 4.422, 8, 0 % | 2.9 |
| BT.601 statt BT.709 | Videofarbe, `h264-1920x1080-bt709.mp4` | 3.215, 32, 0 % | 5.3 |
| falsches Videobild | Videofarbe, `bildwechsel.mp4` | 96.000, 129, 100 % | 60.0 |
| falsches Schriftgewicht (Urbanist 300 statt 400) | Text, Urbanist 300 Mitte | 3.919, 0, 28.89 % | 1.96 (Grenze 2.0); seit Punkt 101 **2.06** (Grenze 1.9) |
| Grundlinie 1 Pixel daneben | Text, DejaVu links | 5.062, 0, 24.63 % | 2.5; seit Punkt 101 2.66 |

«Falsches Schriftgewicht» lag mit der Grenze Text 2.0 über der Grenze, erreichte aber das verlangte Doppelte nicht (`QEXPECT_FAIL`, Punkt 101). Entscheid des Nutzers (2026-11-08): «Text über dem Video» bekommt einen eigenen bekannten Wert, die Grenze wird nicht aufgeweicht. Seither ist die Grenze Text 1.9 (aus den übrigen Textbereichen, gleiche Reserve), der Fehler liegt beim 2.06-fachen und `QEXPECT_FAIL` ist entfernt (Nachtrag Punkt 101, unten).

### Nachtrag Punkt 101 (2026-11-08)

Neue Werte aus denselben 30 Läufen (`build/tests/bildvergleich/zusammenfassung.tsv`, Seite–Editor, jede Masszahl in allen Läufen gleich), Reserve wie oben:

- Grenze Text: Mittel 1.466 × 1.25 = 1.83 → **1.9**; grösste ausserhalb Kanten 0 → 0 + 2 = **2** (vorher 4, weil nur «Text über dem Video» 2 hatte); Kanten 16.73 % × 1.25 = 20.9 → **21 %**.
- Eigener Wert «Gesamt|Text über dem Video»: 1.534 × 1.25 = 1.92 → **2.0**; 2 + 2 = **4**; 7.11 % × 1.25 = 8.9 → **9 %**.
- Absichtliche Fehler Text: «falsches Schriftgewicht» 3.919 / 1.9 = **2.06-fach**, «Grundlinie 1 Pixel daneben» 5.062 / 1.9 = 2.66-fach (im Protokoll gerundet 2.1 und 2.7).

Geprüft am 2026-11-08 auf dem Studio-PC, X11-Sitzung aktiv, daneben keine Bauten oder Tests (`pgrep -af "cmake|ctest|unittest|wiederholen"`), ohne `DSS_BILDVERGLEICH_OBS`:

| Prüfung | Befehl | Ergebnis |
|---|---|---|
| Bau | `cmake --build build -j8` | Exit 0, keine Warnung |
| Einzeln | `ctest --test-dir build -R "^tst_bildvergleich$" -V` | bestanden, 10 passed, kein XFAIL |
| Grenze | `scripts/wiederholen.sh build tst_bildvergleich 30 <Ordner>`, Ende 22:58:28 | 30 von 30, jede Masszahl in allen Läufen gleich und wie in P11b |
| 50 Wiederholungen | `scripts/wiederholen.sh build tst_bildvergleich 50 <Ordner>`, Ende 23:23:43 | 50 von 50, Masszahlen unverändert |
| Tests, voll | `ctest --test-dir build` | 45 von 45, 188 s, kein Test übersprungen |

Prüfung N gegen das echte OBS wurde nicht wiederholt: Geändert sind nur Grenzen im Test (Seite–Editor), nicht Zeichner, Seite oder Weg zu OBS; die Werte mit OBS werden ohne Grenze nur festgehalten.

`tst_browserquelle`: Toleranz der Videofarben für den Editor 80 → 4 (gemessen 2), für die Seite weiter 80 (gemessen 64).

## Geprüft

Alle Läufe am 2026-11-08 auf dem Studio-PC mit aktiver X11-Sitzung. Stufe «Baustein». Daneben lief die installierte Version des Nutzers (`dss-app`, `dss-core`) und ein anderer Agent von Lotse (`claude -p`, Webansicht); ein Testprozess von ihm lief bei keinem Start eines Laufs (`pgrep -af "cmake|ctest|unittest"`).

| Prüfung | Befehl | Ergebnis |
|---|---|---|
| Bau | `cmake --build build -j8` | Exit 0, keine Warnung (nach jedem Commit) |
| Kleine Tests nach Commit 4 | `ctest -R` für `tst_medien`, `tst_videofarbe`, `tst_leinwand`, `tst_leinwandbild`, `tst_browserquelle`, `media_vorbereiten` | 6 von 6 |
| Leistung vor und nach Commit 4 | `python3 scripts/leistung.py --video --out build/pruefung-p11/leistung-vor` bzw. `…-nach`, zwei Videos 1080p30, 60 s | siehe unten |
| Grenze | `scripts/wiederholen.sh build tst_bildvergleich 30 <Ordner>`, Ende 19:50:55 | 30 von 30, alle Masszahlen in jedem Lauf gleich |
| Tests nach Commit 6 | `ctest -R "^(tst_bildvergleich\|tst_browserquelle)$" -V` | bestanden, ein XFAIL (Punkt 101) |
| Tests, voll | `ctest --test-dir build` (dreimal, Ende 19:57, 20:01, 20:14) | Lauf 1 und 2: 44 von 45, `tst_kernverbindung` gescheitert (Punkt 102); Lauf 3: **45 von 45**, 188 s. Kein Test übersprungen |
| `tst_kernverbindung` | einzeln und `ctest -I 1,17` | bestanden |
| Frischer Klon | `git clone --branch lotse/p11`, `cmake -S -B`, `cmake --build -j8`, `ctest` | Stand nach Commit 6, ohne diesen Bericht. Bau ohne Warnung; 45 von 45, 207 s |
| Startprüfung | `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p11/start/startpruefung` | je 20 von 20 sauber: `app-sigterm`, `app-kern-sigterm`, `core-sigterm`, `core-sigint` |
| Dienstprüfung | `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p11/dienst` | bestanden. Neustart nach `kill -9` 1.090 bis 1.135 s, Fenster wieder verbunden 1.886 bis 2.935 s |
| 50 Wiederholungen | `scripts/wiederholen.sh build <Test> 50 <Ordner>`, nacheinander 20:14 bis 21:12, nichts daneben gebaut | je **50 von 50**: `tst_bildvergleich` (Ende 20:37:55, Masszahlen wie in den 30 Läufen; XFAIL im Protokoll des letzten Laufs, ein XPASS hätte einen Lauf scheitern lassen), `tst_browserquelle` (20:44:46), `tst_medien` (21:05:01), `tst_overlayserver` (21:10:35), `tst_leinwand` (21:10:59), `tst_leinwandbild` (21:12:47) |

**Leistung** (`scripts/leistung.py --video`, Overlay 1920x1080 mit `h264-1920x1080-bt709.mp4` und `h264-1920x1080-bt601.mp4` je 960x540, 60 s):

| Stand | Beginn | `dss-app`, % eines Kerns (Mittel) | Bilder/s | Einbrüche über 50 ms | grösster Abstand | GPU-Auslastung (Mittel) |
|---|---|---|---|---|---|---|
| vor Commit 4 | 19:22:25 | 91.0 | 30.0 | 0 | 42.4 ms | 16.4 % |
| nach Commit 4 | 19:27:21 | 90.8 | 30.0 | 0 | 43.7 ms | 23.4 % |

Die Vorgabe (30 Bilder/s ohne Einbrüche, höchstens 5 Prozentpunkte eines Kerns mehr) ist erfüllt. Die GPU-Auslastung gilt für die ganze Karte (auch die installierte Version und die Anzeige); woher die 7 Prozentpunkte kommen, ist nicht geprüft (swscale rechnet auf der CPU).

**Nicht geprüft:**

- Alles gegen das echte OBS nach P11b (Prüfung N, Lotse).
- AV1, nicht quadratische Pixel, Grundfarben ohne Matrix (Punkte 98 bis 100).
- 50 Wiederholungen anderer Tests und `schalter-lauf`: laut Plan nicht verlangt. Stufe «Vor der Installation»: nicht verlangt.

Prüfsummen der echten Daten wurden nicht erhoben. Die Prüfungen liefen mit `build/` und eigenen Ordnern; `scripts/installieren.sh` wurde nicht aufgerufen.

## Handtest

### Prüfung N – prüft Lotse

Grund (Regel 10): Nur das echte OBS auf dem Streaming-PC zeigt, ob die Browserquelle nach P11b wie der Editor aussieht; die Rechte dafür hat nur Lotse («Umgebung», Handtest-Rechte ab dem Baustein nach P10c). Nicht während eines Streams; die Szene «test 2» muss aktiv sein.

Vorher: OBS läuft, kein Stream; Szene «test 2» aktiv machen (danach zurück). Aufruf auf dem Studio-PC, Zugangsdatei von Lotse (Name `obs-websocket`, Port 4455 ist Standard):

```
cd ~/DoNs_Streaming_Studio && DSS_BILDVERGLEICH_OBS="192.168.1.116;test 2;$HOME/.config/lotse/obs-websocket.cred;obs-websocket" ctest --test-dir build -R '^tst_bildvergleich$' -V --output-log build/pruefung-p11/pruefung-n.log
```

Der Test legt die Quelle «DSS Bildvergleich» an und löscht sie am Ende wieder. Erwartung:

1. Der Test besteht. Die Grenze gilt nur Seite–Editor; die Werte mit OBS stehen in `build/tests/bildvergleich/werte.tsv` (Zeilen «OBS–Editor», «OBS–Seite»), die Bilder unter `build/tests/bildvergleich/*-obs.png`.
2. Videofarben: OBS–Editor im Mittel unter 2 in allen Bereichen des Overlays «Videofarbe» und bei «Video BT.709 1280x720» in «Gesamt». In Prüfung V lagen HD ohne Angabe und BT.709 bei 2.5, voller Bereich bei 2.9; SD, SMPTE170M und VP9 ohne Angabe bei 0.5, die bleiben so.
3. WebM-Drehung: Protokoll «Ausrichtung drehung-90/180/270.webm: Editor rot grün blau gelb | Seite rot grün blau gelb | OBS rot grün blau gelb». OBS–Editor bei `drehung-*.webm` im Mittel um 0.5 wie bei VP9 ohne Angabe (in Prüfung V 95 bis 125).
4. Alpha: OBS–Editor bei «PNG mit Alpha 1:1», «PNG mit Alpha vergrössert», `alpha.webm` und «WebM mit Alpha» deutlich unter 13 (Prüfung V), in der Grössenordnung von OBS–Seite. Die Zeile «Bild von OBS vormultipliziert gelesen, Pixel mit Farbe grösser als Alpha: …» nennt je Overlay die Zahl; erwartet sind bei «Bild» und «Text» 0 oder wenige (nur Rundung). In Overlays mit WebM mit Alpha hat schon der Abgriff von Qt WebEngine viele solche Pixel («Gesamt» 48641, Punkt 97); dort ist die Zahl nur festzuhalten. Sind es bei «Bild» viele, stimmt die Erklärung nicht: `[OFFEN]`, nichts am Zeichner ändern.
5. Text, HEVC: nur festhalten (Prüfung V: OBS–Editor bis 6.5; HEVC leer).

### Prüft der Nutzer

Kein Handtest nötig. Grund: Die sichtbare Änderung (Farben von HD-Videos im Editor) misst Prüfung N gegen OBS in Zahlen; Bedienung ändert sich nicht.

## Nicht erledigt, `[OFFEN]`

- **Prüfung N** gegen das echte OBS steht aus (Lotse). Erst sie bestätigt die Videofarben nach P11b, die WebM-Ausrichtung in OBS und die Erklärung zu Alpha.
- Punkt 96: Urbanist 800 und Be Vietnam Pro klein liegen im Editor neben beiden Browsern (Seite–Editor 6.19 bzw. 6.01 im Mittel; Prüfung V: OBS–Editor bis 6.5, OBS–Seite 0.6 bis 2.4). Ursache nicht untersucht; festgehalten als bekannte Werte.
- Punkt 101: erledigt (2026-11-08), siehe «Nachtrag Punkt 101».
- Punkt 102 (nicht im Auftrag): `tst_kernverbindung::settingsReadAndChange` scheiterte in zwei von drei vollen `ctest`-Läufen (Zeile 454), sonst bestanden. Vermutete Ursache im Test (wartet auf den Anfang der Protokollzeile, prüft sofort das Ende), nicht geprüft. P11 ändert weder den Test noch `coresettings`.
- Punkt 97: Der Abgriff von Qt WebEngine enthält im Overlay «Gesamt» 48641 Pixel mit Farbe grösser als Alpha (ungültig vormultipliziert), vermutlich im WebM mit Alpha. Ob das an Qt WebEngine 122 oder am Abgriff liegt und ob OBS dasselbe zeigt, ist nicht geprüft.
- Punkt 98: AV1 ist nicht gemessen (keine Testdatei). Die Regel «AV1 ohne Angabe wie BT.601» stammt nur aus dem Quelltext von Chromium.
- Punkt 99: Videos mit nicht quadratischen Pixeln: Chromium wählt die Matrix nach `natural_size`, der Editor nach der Höhe des Bildes. Nicht geprüft.
- Punkt 100: Videos mit Angabe der Grundfarben oder der Übertragung, aber ohne Matrix: Der Editor nimmt die Höhenregel; ob Chromium in diesem Fall gleich wählt, ist nicht geprüft.
- Rest aus Punkt 81: HEVC spielt OBS 32.2.2 nicht ab (Prüfung V); MPEG-4 Part 2 ist weiterhin nicht geprüft, ProRes in MP4 ebenso.

## Annahmen, Interna-Entscheide

- **Höhenregel nach der Höhe des Bildes:** Chromium nimmt `natural_size` (mit dem Seitenverhältnis der Pixel), der Editor die Höhe des dekodierten Bildes (`AVFrame::height`). Bei quadratischen Pixeln ist das gleich (alle Testdateien); sonst Punkt 99.
- **Keine Umrechnung der Grundfarben:** OBS rechnet bei SMPTE170M nicht um (Editor ohne Umrechnung 0.5 neben OBS, Prüfung V); Qt WebEngine 122 schon. OBS ist massgebend.
- **`sws_setColorspaceDetails` bei jedem Bild:** Die Funktion kehrt bei unveränderten Werten früh zurück; so gilt eine Änderung mitten im Video (neue Angabe im Bild) sofort. Leistung siehe «Geprüft».
- **WebM neutralisieren statt die Seite ändern:** siehe «WebM-Drehung». Nur Yaw und Roll (4 oder 8 Byte, nicht null) bei Projektion «rechteckig» (Typ 0 oder fehlend); Pitch bleibt, weil FFmpeg mit Pitch ungleich 0 keine Matrix erzeugt. Gesucht wird nur bis zum ersten Cluster, Tracks höchstens 16 MiB; bei unbekannter Grösse oder Fehler bleibt die Datei unverändert.
- **Bild von `GetSourceScreenshot`:** als vormultipliziert gelesen, Farben grösser als Alpha auf Alpha begrenzt und gezählt (Gegenprobe). Der Testserver sendet die Seite auf dieselbe Art, damit `ctest` den Weg prüft.
- **Commit «Bild von GetSourceScreenshot …, erwartete Warnung»:** zwei kleine Punkte in derselben Testdatei `tst_bildvergleich` (Weg über obs-websocket), deshalb ein Commit (Regel 8).
- **Form der Grenze:** eine Grenze je Quellenart (Bild, Text, Video) für alle Bereiche ohne eigenen Eintrag; bekannte Werte je Fall (`kKnown`) für die Fälle, in denen Qt WebEngine anders rechnet als OBS, für Urbanist 800 und Be Vietnam Pro klein und für «ganzes Overlay» je Overlay. Reserve: × 1.25 mit Mindestabstand (Mittel + 0.1, grösste Abweichung + 2, Kanten + 1 Prozentpunkt), damit keine Grenze 0 ist. Geprüft wird jede der drei Masszahlen. Die Zusammenfassungen «alle (Art)» werden nicht geprüft (sie sind aus den Bereichen gerechnet).
- **`QEXPECT_FAIL` für «falsches Schriftgewicht»:** Ein dauerhaft roter Test hätte die 50 Wiederholungen und jeden `ctest`-Lauf unbrauchbar gemacht; die Grenze anzupassen verbietet der Plan. So bleibt der Fehlbetrag sichtbar (XFAIL im Protokoll), der Test prüft weiter, dass der Fehler über der Grenze liegt, und scheitert, sobald er das Doppelte erreicht (XPASS) oder unter die Grenze fällt. Mit Punkt 101 entfernt; der Fehler gilt nun wie alle anderen (mindestens das Doppelte).
- **Punkt 101, grösste Abweichung ausserhalb der Kanten:** Die Grenze Text sinkt dabei von 4 auf 2, weil alle Textbereiche ausser «Text über dem Video» dort 0 hatten (gleiche Reserve wie bei allen Grenzen, «+ 2»). Die Grenze ist damit strenger, nicht weicher.
- **Absichtliche Fehler:** erzeugt aus den Bildern des Laufs (Editor neu gezeichnet mit geänderter Quelle oder Pixel im Kasten ersetzt), verglichen mit der Seite. Massgebend ist das grösste Verhältnis der drei Masszahlen zur Grenze des Bereichs.
- **`tst_browserquelle`:** Toleranz der Videos für Editor und Seite getrennt. Für die Seite bleibt 80 (Qt WebEngine 122 rechnet die Grundfarben um, OBS nicht).
- **`scripts/leistung.py --video`:** zwei Videos 1080p30 (BT.709 und SMPTE170M) je 960x540 im Overlay 1920x1080, damit beide Matrizen gleichzeitig laufen.

## Quellen

- Chromium `media/ffmpeg/ffmpeg_common.cc` (`AVStreamToVideoDecoderConfig`: Farbraum, Höhenregel, Display-Matrix): https://raw.githubusercontent.com/chromium/chromium/146.0.7680.177/media/ffmpeg/ffmpeg_common.cc, https://raw.githubusercontent.com/chromium/chromium/122.0.6261.171/media/ffmpeg/ffmpeg_common.cc
- FFmpeg 7.1: https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libswscale/swscale.h, https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libswscale/utils.c (`sws_setColorspaceDetails`, `sws_getCoefficients`), https://ffmpeg.org/doxygen/7.1/structAVFrame.html, https://ffmpeg.org/doxygen/7.1/pixfmt_8h.html (`AVColorSpace`, `AVColorRange`), https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libavformat/matroska.h, https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libavformat/matroskadec.c, https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libavformat/matroskaenc.c
- ITU-T H.273 https://www.itu.int/rec/T-REC-H.273, ITU-R BT.601 https://www.itu.int/rec/R-REC-BT.601, ITU-R BT.709 https://www.itu.int/rec/R-REC-BT.709
- EBML RFC 8794 https://www.rfc-editor.org/rfc/rfc8794; Matroska RFC 9559 https://www.rfc-editor.org/rfc/rfc9559 (Projection, ProjectionType, ProjectionPoseYaw, ProjectionPoseRoll)
- obs-websocket 5.7.4 (`TakeSourceScreenshot`): https://raw.githubusercontent.com/obsproject/obs-websocket/5.7.4/src/requesthandler/RequestHandler_Sources.cpp; Protokoll https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md
- obs-browser (`BrowserSource::Render`, `GS_BLEND_ONE`, `GS_BLEND_INVSRCALPHA`): https://raw.githubusercontent.com/obsproject/obs-browser/master/obs-browser-source.cpp
- Qt 6.8: https://doc.qt.io/qt-6.8/qimage.html#reinterpretAsFormat, https://doc.qt.io/qt-6.8/qimage.html#Format-enum, https://doc.qt.io/qt-6.8/qimage.html#mirrored, https://doc.qt.io/qt-6.8/qtest.html#ignoreMessage, https://doc.qt.io/qt-6.8/qtest.html#QEXPECT_FAIL
