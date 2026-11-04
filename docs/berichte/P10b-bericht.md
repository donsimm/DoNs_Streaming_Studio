# P10b – Bericht Dock «Medien»

Auftrag: `prompts/P10b-medien.md`. Ausgeführt am 2026-11-04 von Claude Code auf dem Studio-PC (Debian 13, X11, `DISPLAY=:0.0`). Stand: ausgeführt, Handtest offen.

Alle Messwerte stammen von diesem Gerät, mit Datum und Befehl. Was nur der Nutzer prüfen kann (Aussehen und Bedienung im echten Fenster, echte Installation), ist «nicht geprüft» und steht im Handtest. Tokens kommen in diesem Bericht nicht vor.

## Kurzfassung

- **Medienliste im Kern** (E-28, Teil A): neue Datei `medien.json` im Datenordner mit Namen und Ordnern der Dateien in `media/`. Nur der Kern schreibt sie, atomar (`QSaveFile`). Format in `docs/06-overlay-format.md`, «Medienliste». Beim Übernehmen hält der Kern den ursprünglichen Dateinamen fest. Kommt derselbe Inhalt nochmals, bleiben Name und Ordner. Dateien ohne Eintrag bekommen einen Ersatznamen wie «Bild vom 2026-11-02». Ordner sind beliebig tief. Gelöscht werden nur ungenutzte Dateien, sonst kommt «verwendet in …». Das erste Anlegen bei bestehenden Daten gilt als Umwandeln: `scripts/installieren.sh` sichert vorher.
- **Fortschritt und Abbrechen** (Punkt 73): Jede Übernahme erscheint mit Prozent in allen Fenstern und lässt sich abbrechen; danach bleibt nichts in `media/`.
- **Videoformate** (Punkt 81): Der Kern liest das Format beim Übernehmen. Bei HEVC, MPEG-4 Part 2 und ProRes meldet er «… wird in der Browserquelle nicht angezeigt. In H.264 oder VP9 umwandeln.», und das Dock zeigt ein Warnzeichen.
- **Adresse im Heimnetz** (Punkt 85): Ist sie leer, prüft der Kern alle 2 s die Standardroute und trägt die Adresse ohne Neustart ein.
- **Schnittstelle Version 10** und neue Befehle in `dss-cli` (`medien-liste`, `medium-umbenennen`, `medium-verschieben`, `medium-loeschen`, `medien-ordner-…`).
- **Dock «Medien»** (Teil B, Schalter `DSS_APP_MEDIA_DOCK`):
  - Vorschau, Name, Typ, Grösse; Sortieren per Klick auf den Spaltenkopf
  - Umbenennen mit F2 oder Doppelklick, Ordner, Ziehen zwischen Ordnern
  - «Hinzufügen» mehrerer Dateien, «Löschen» nur ungenutzter Dateien
  - Ziehen auf die Leinwand und in die Liste «Quellen»
  - Lage: ohne `fenster.ini` zwischen «Quellen» und «Eigenschaften», mit gespeicherter Anordnung als dritter Reiter neben «Overlays» und «Quellen»
- **Eigene Ports für alle Testkerne** (Punkt 83), auch für die Startprüfung und die Dienstprüfung. `ctest`, die Startprüfung und die Dienstprüfung liefen mit laufendem `dss-core-entwicklung.service`.

## Teil A – Kern

### Namen und Ordner (A 1)

Datei `<Datenordner>/medien.json`, `format_version` 1 (Code `src/common/medialibrary.h/.cpp`, Beispiel und Regeln in `docs/06-overlay-format.md`, «Medienliste»; `tst_medienliste::formatDocExample` prüft, dass das Beispiel dort genau so geschrieben wird). Nur der Kern schreibt sie (`OverlayStore::setWritesMediaLibrary`, nur in `CoreService`); `dss-cli` liest sie nur.

- **Ursprünglicher Name:** Nach jeder Übernahme (auch über `add_image_source`, `add_video_source`, `set_text_font`) trägt der Kern den Dateinamen ein. Ist der Inhalt schon da, bleiben Name und Ordner; bei `add_media` sagt `warning` das («x.webm ist schon in der Medienliste (als «Video vom 2026-11-03»), Name und Ordner bleiben.»).
- **Ersatzname, Wahl des Datums:** letzte Änderung der Datei in `media/` (lokale Zeit). Begründung: Der Kern schreibt die Datei beim Übernehmen und ändert sie danach nie (ihr Name ist ihr Inhalt), also ist es das Datum der Übernahme. `cp -a` (Sicherung in `installieren.sh`) behält es. Der Zeitpunkt des Anlegens (birth time) wäre gleich, gibt es aber nicht auf jedem Dateisystem (https://doc.qt.io/qt-6.8/qfileinfo.html#lastModified, https://www.gnu.org/software/coreutils/manual/html_node/cp-invocation.html). Gleiche Ersatznamen bekommen « (2)», « (3)» in der Reihenfolge des Datums.
- **Ordner löschen, Wahl:** Dateien und Unterordner kommen eine Ebene höher. Begründung: Es wird nie eine Datei gelöscht, und es tut dasselbe wie die Ordner der Overlay-Liste; «nur leere Ordner» wäre bei verschachtelten Ordnern umständlich.
- **Abgleich:** Beim Laden und nach jeder Übernahme gleicht der Kern mit `media/` ab: Einträge zu fehlenden Dateien fallen weg, neue Dateien kommen mit Ersatznamen auf die oberste Ebene. Andere Dateien in `media/` (vorläufige von `QSaveFile`, fremde Namen) gehören nicht dazu.
- **Ungültige Datei:** wie `liste.json`. Gemeldet, nicht überschrieben, Ersatznamen nur im Speicher; Umbenennen, Verschieben und Ordner werden abgewiesen, Übernehmen geht weiter.
- **Umwandeln:** Fehlt `medien.json` und liegen Dateien in `media/`, meldet `dss-core --format-pruefen` «älteres Format: medien.json (fehlt, N Datei(en) in media/ bekommen Ersatznamen)» mit Exit 4; `--umwandeln` schreibt sie (`src/common/dataformat.cpp`).

### Löschen (A 2)

`remove_media` löscht Datei und Eintrag nur, wenn keine Quelle die Datei nutzt (Bild- und Videoquellen über `file`, Textquellen über ihre Schrift). Sonst Code `in_use` mit «verwendet in» (aufgezeichnet: «Bild vom 2026-11-02» lässt sich nicht löschen, verwendet in Overlay «Neues Overlay» (Quelle «bild1.png»).). `media_list` nennt die Verwendungen jeder Datei (`used_in`).

### Fortschritt und Abbrechen (A 3, Punkt 73)

`MediaImporter` zählt die kopierten Byte; der Kern schickt `import_progress` an alle, höchstens alle 200 ms und einmal leer am Ende. `add_media` nutzt denselben Thread wie das Übernehmen für Quellen (höchstens 2 gleichzeitig). `cancel_import` bricht ab: Die vorläufige Datei verschwindet. War die Kopie schon fertig und neu, löscht der Kern sie, sobald keine Übernahme mehr läuft und keine Quelle sie nutzt (eine andere Übernahme könnte denselben Inhalt gerade gefunden haben). Geprüft in `tst_uebernehmen` (`cancelLeavesNothing`: Abbruch beim Kopieren; `cancelAllStopsThreads`; `progressAndCancel`: Fortschritt an alle, `canceled`, nichts in `media/`) und im Fenster (`tst_fensterbearbeiten::mediaAddProgressCancel`): danach dieselben Dateien in `media/` wie vorher.

### Videoformate (A 4, Punkt 81)

`readVideoCodec` (`src/media/mediainfo.cpp`, FFmpeg `avformat_find_stream_info`, Name über `avcodec_get_name`) liest das Format beim Übernehmen. Die Liste der Formate, die die Browserquelle nicht abspielt, steht nur in `src/common/videoformat.h` (HEVC, MPEG-4 Part 2, ProRes nach dem Bericht P10, Abschnitt «Videoformate»). Meldung als `warning` an das Fenster (Statuszeile) und als `browser_problem` in `media_list` (Warnzeichen im Dock). Geprüft in `tst_medien::videoCodec` und `coreFormatsAndPlacement` mit Dateien, die der Test mit `ffmpeg` erzeugt (`libx265`, `mpeg4`, `prores_ks`, dazu H.264 und VP9 ohne Hinweis). ProRes schreibt ffmpeg nicht in MP4; der Test nimmt ProRes in MOV, als `.mp4` benannt, weil `.mov` keine erlaubte Endung ist (Punkt 88).

### Adresse im Heimnetz (A 5, Punkt 85)

`HomeAddressWatcher` (`src/common/coresettings.h/.cpp`) ruft alle 2 s `CoreSettingsStore::fillMissingHomeAddress` auf: Bei gültiger Datei und leerer Adresse trägt er die Adresse der Standardroute ein, schreibt `kern.ini`, und der Kern schickt `settings` an alle und startet den Server neu (`settingsApplied`). Protokoll: «Einstellungen: Adresse im Heimnetz 192.168.1.9 eingetragen (Netzwerkkarte enp3s0 der Standardroute, ohne Neustart)». Geprüft ohne Netzwerk mit vorgegebener Route und Liste der lokalen Adressen (`tst_kerneinstellungen`: keine Route, dann Karte ohne IPv4, dann mit 192.168.1.9) und mit Kern (`tst_kernverbindung::settingsReadAndChange`: leere Adresse über `update_settings`, nach höchstens 2 s wieder eingetragen). Der echte Fall mit Kabel ist nicht geprüft.

### Schnittstelle und `dss-cli` (A 6)

Version 10, alle Nachrichten in `docs/08-kern-schnittstelle.md` (Beispiele aufgezeichnet am 2026-11-04 mit einem Kern auf einer Kopie der echten Daten). Neue Befehle in `dss-cli` (`dss-cli --help`): `medien-liste` (geht immer, liest den Datenordner) und die übrigen über den laufenden Kern (Exit 4 ohne Kern), Datei nach Nummer, Verweis oder eindeutigem Namen, Ordner als Pfad «Logos/Alt».

## Teil B – Dock «Medien»

- **Ansicht (B 1):** `QTreeWidget` mit Spalten Name, Typ, Grösse.
  - Vorschau 40x24 Pixel vor dem Namen, geladen in einem Thread-Pool (`src/render/mediapreview.h/.cpp`, höchstens 2 Threads, Zwischenspeicher): Bild mit EXIF-Ausrichtung, bei Schriften «Aa» in der Schrift (`QRawFont`).
  - Bei Videos das erste dekodierte Bild. Gewählt, weil eine neue Quelle auf der Leinwand mit diesem Bild beginnt und weil es ohne Suchen in der Datei kommt. Beginnt ein Video schwarz, ist auch die Vorschau schwarz.
  - Ordner stehen fett und immer vor den Dateien. Sortieren per Klick auf den Spaltenkopf, bei Gleichstand fest nach Verweis bzw. Kennung (`mediaRowBefore` in `src/common/windowedit.cpp`).
  - Warnzeichen in der Spalte «Typ» bei `browser_problem`; der Hinweis steht im Tooltip und in der Zeile unter der Liste.
  - Ungenutzte Dateien sind kursiv, mit «Ungenutzt» im Tooltip und in der Zeile unter der Liste. Eine Farbe ginge nicht, weil das Stylesheet die Farbe aller Zeilen festlegt. Die Hinweise am Spaltenkopf erklären Kursivschrift und Warnzeichen.
- **Lage (B 2):** Ohne `fenster.ini` liegt das Dock in der unteren Reihe zwischen «Quellen» und «Eigenschaften» (Breiten 200/200/300/330/400). Mit gespeicherter Anordnung ohne Eintrag `fenster/medien` kommt es als dritter Reiter zu «Overlays» und «Quellen» (`tabifyDockWidget`). Beim Speichern schreibt das Fenster `fenster/medien=true`, danach gilt die gespeicherte Lage des Docks. Es ist lösbar, verschiebbar und im Menü «Docks».
  - Geprüft in `tst_fensterbearbeiten::mediaDockSavedLayout`, einmal mit einer erzeugten Anordnung und einmal mit einer Kopie der echten `~/.config/dons-streaming-studio/fenster.ini` (nur gelesen).
  - Dabei aufgefallen: Die Reiter der Docks haben keinen Stil des Designs; der gewählte Reiter ist im Bild des Tests weiss mit hellem Text (Punkt 86, schon vor P10b).
- **Umbenennen, Ordner, Ziehen (B 3):** F2 und Doppelklick öffnen den Dialog «Datei umbenennen» bzw. «Ordner umbenennen». Der Knopf mit dem Ordnersymbol öffnet «Ordner anlegen …», der neue Ordner kommt in den gewählten Ordner. Ziehen von Dateien und Ordnern auf einen Ordner verschiebt hinein, auf eine Datei in deren Ordner, auf die leere Fläche auf die oberste Ebene (`mediaDropFolder`). Auf- und Zuklappen wird gespeichert (`set_media_folder_expanded`).
- **Hinzufügen (B 4):** «Dateien hinzufügen …» (Dateiauswahl für mehrere Dateien mit Filter Bilder, Videos, Schriften) schickt je Datei `add_media` mit dem gewählten Ordner. Es entsteht keine Quelle. Laufende Übernahmen stehen als Zeile «übernehme … 37 %» im Zielordner.
- **Löschen (B 5):** Ist eine genutzte Datei gewählt, ist «Löschen» ausgegraut. Tooltip und Zeile unter der Liste nennen dann «Verwendet in Overlay «…» (Quelle «…»)». Ungenutzte Dateien und Ordner werden nach einer Rückfrage gelöscht. Auf der Zeile einer Übernahme bricht «Löschen» ab.
- **Ziehen ins Overlay (B 6):** Bild- und Videodateien lassen sich auf die Leinwand ziehen. Die neue Quelle kommt in Originalgrösse, ihre Mitte an der Mausstelle (ganze Pixel des Overlays), zuoberst.
  - In die Liste «Quellen» gezogen, kommt sie an die abgelegte Stelle der Reihenfolge und in die Mitte der Leinwand.
  - Der Kern bekommt den Verweis `media/…` mit `center_x`/`center_y`/`insert_index` und kopiert nichts.
  - Geprüft mit echtem Ziehen der Maus über `QDrag` (`tst_fensterbearbeiten::mediaDragAndDrop`): Bild 256x256 an der Stelle 600/400 des Overlays → Lage 472/272 in 256x256; Video 960x540 in die Liste an die zweite Zeile → Lage 480/270, an dieser Stelle der Reihenfolge; `media/` unverändert.
- **Schalter (B 7):** `DSS_APP_MEDIA_DOCK` (Standard ON). Aus: kein Dock, keine Annahme von Ziehen auf Leinwand und Liste, `fenster.ini` ohne Eintrag.

Die Logik (Sortieren, Ziel beim Ablegen, Texte, Stelle der neuen Quelle) steht in `src/common/windowedit.h/.cpp` ohne Qt Widgets und ist dort getestet (`tst_windowedit`: `newSourceAtRow`, `mediaSorting`, `mediaDrop`, `mediaSelection`). Das Dock (`src/app/mainwindowmedia.cpp`, `dockviews.cpp`) füllt nur den Baum und schickt Nachrichten.

## Teil C – Tests

Neue und erweiterte Tests:

- Ohne Fenster:
  - `tst_medienliste` (neu, 27 Fälle): Lesen, Schreiben, ungültige Dateien, Abgleich, Ersatznamen, Ordner, Verwendungen, Beispiel aus `docs/06`
  - `tst_protocol` und `tst_aenderungen`: Nachrichten der Version 10
  - `tst_datenformat`: Umwandeln mit `medien.json`
  - `tst_uebernehmen`: Fortschritt, Abbrechen, nichts bleibt in `media/`
  - `tst_kerneinstellungen`: Punkt 85 mit Liste der lokalen Adressen
  - `tst_kernverbindung`: Punkte 83 und 85 mit Kern
  - `tst_medien`, 40 Fälle: `videoCodec`; `coreFormatsAndPlacement` mit Hinweis, `media_list`, Lage über `center`/`insert_index`, nichts kopiert; `cliMedia` mit den Befehlen von `dss-cli`
- Mit Fenster (`tst_fensterbearbeiten`, 28 bestanden, 1 übersprungen wie bisher):
  - `mediaDockDefaultLayout`, `mediaDockSavedLayout` (zwei Zeilen)
  - `mediaRenameFoldersRemove`, `mediaAddProgressCancel`, `mediaDragAndDrop`
  - `settingsDialogReset` mit eigenem Port
  - `sourceSelectionFollowsMark` (Umstellung der Auswahl, unten)
- **Kopie der echten Daten** (2026-11-04 09:42:39, Kopie von `~/.local/share/dons-streaming-studio` im Notizordner):
  - `dss-core --datenordner <Kopie> --format-pruefen`: «älteres Format: medien.json (fehlt, 10 Datei(en) in media/ bekommen Ersatznamen)», Exit 4
  - `--umwandeln`: Exit 0; danach `--format-pruefen`: «Format aktuell», Exit 0
  - `diff -r` gegen die unveränderte Kopie: nur `medien.json` dazu
  - `dss-cli --datenordner <Kopie> medien-liste`: 10 Dateien mit Ersatznamen, «verwendet in» bei den zwei genutzten (Bild in «Neues Overlay», Schrift der Quelle «Text: Herzlich Wilkommen»)
  - Dieselbe Prüfung lief über `installieren.sh` (unten)
- Die Beispiele in `docs/08` sind mit einem eigenen Kern (Port 32557/32558) auf einer weiteren Kopie aufgezeichnet; die Kopien sind gelöscht.

## Ausgeführte Befehle und Ergebnisse

### Bau

```
cmake --build build -j8                                                   # Debug mit -Wall -Wextra (dss_warnings)
docs/berichte/P10b-daten/schalter-lauf.sh <Notizordner>/schalter          # frischer Klon
```

Zwischenstände der Commits (je `git checkout-index` des Index in den Notizordner, `cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-Wall -Wextra"`, `cmake --build -j3`, mit `nice`):

| Stand | Bau | Tests |
|---|---|---|
| `749981a` (Punkte 83 und 85) | Exit 0, 0 Warnungen | `ctest -R` 9 Tests der Einstellungen, des Kerns und der Änderungen: 9 von 9; `tst_fensterbearbeiten settingsDialog settingsDialogReset`: bestanden |
| `6df2e9b` (Teil A) | Exit 0, 0 Warnungen | `ctest -E tst_fensterbearbeiten`: 37 von 37; `ctest -R tst_fensterbearbeiten` (2026-11-04 12:49:29, mit `XDG_CONFIG_HOME` im Bauordner): bestanden, 23.8 s |

Arbeitsordner `build/` (Debug, `-Wall -Wextra`) auf dem Endstand `559d974`, 2026-11-04 14:38: Exit 0, 0 Warnungen.

**Schalter-Lauf** im frischen Klon (`git clone` des Repos in den Notizordner; je Schalter `cmake -DCMAKE_CXX_FLAGS="-Wall -Wextra"`, `cmake --build -j8`; Release, Debug und `DSS_APP_MEDIA_DOCK=OFF` mit `ctest` ohne `-j`, mit eigenem `XDG_CONFIG_HOME` je Bau):

1. Erster Lauf, Klon `2e15fe9`, 2026-11-04 12:50:07 bis 13:23:25 (`docs/berichte/P10b-daten/schalter-ergebnis-2e15fe9.txt`): Release und Debug Exit 0, 0 Warnungen, `ctest` 38 von 38. Zwei Fehler:
   - `DSS_APP_MEDIA_DOCK=OFF`: Exit 2, «undefined reference to `MainWindow::connectMedia()'» (`src/app/mainwindow.cpp:352`). Der Aufruf stand nicht hinter `#if DSS_APP_MEDIA_DOCK`, die Funktion steht in `mainwindowmedia.cpp`, das ohne den Schalter nicht gebaut wird. Korrektur `8e8e83d`.
   - `DSS_MEDIA_SIZE=OFF`: 1 Warnung, «variable ‘at’ set but not used» (`tests/tst_fensterbearbeiten.cpp:2048`, `mediaDragAndDrop`). Korrektur `559d974`.
   - Alle übrigen 15 Bauten: Exit 0, 0 Warnungen.
2. Zweiter Lauf, Klon `559d974`, 2026-11-04 14:02:27 bis 14:37:52 (`docs/berichte/P10b-daten/schalter-ergebnis.txt`): alle 19 Bauten Exit 0, 0 Warnungen. `ctest` 38 von 38 bei Release, Debug und `DSS_APP_MEDIA_DOCK=OFF`.

Die Wiederholungsläufe, `ctest` im Arbeitsordner und die Startprüfung liefen mit `9641893`. Die Commits danach ändern am Bau mit den Vorgaben nichts: `2e15fe9` ändert nur `scripts/dienstpruefung.py`, `8e8e83d` setzt einen Aufruf hinter `#if DSS_APP_MEDIA_DOCK` (Vorgabe ON), `559d974` eine Zeile im Test hinter `#if DSS_MEDIA_SIZE` (Vorgabe ON). Mit den Vorgaben entsteht also derselbe Code; geprüft ist das mit dem zweiten Schalter-Lauf (Release und Debug, `ctest` 38 von 38), nicht mit neuen 50er-Läufen.

### Installation (nur mit Testoptionen, Regel 18)

```
docs/berichte/P10b-daten/installieren-pruefen.sh <Notizordner>/inst
```

2026-11-04 10:39:54 bis 10:41, Ergebnis in `docs/berichte/P10b-daten/installieren-ergebnis.txt`. `scripts/installieren.sh` lief mit `--ziel`, `--datenordner` (frische Kopie der echten Daten, 13 Dateien, ohne `medien.json`), `--einheit dss-core-installtest.service`, `--menue-ordner`, `--symbol-ordner`, `--bau-ordner` (alle im Notizordner). Ergebnis:

- Exit 0, «älteres Format: medien.json (fehlt, 10 Datei(en) in media/ bekommen Ersatznamen)».
- Sicherung unter `<Daten>-sicherungen/2026-11-04_10-40-57`, gleich den Daten vorher (`diff -r`).
- Danach kam nur `medien.json` dazu. `--format-pruefen` des installierten `dss-core`: «Format aktuell», Exit 0. `medien-liste`: dieselben 10 Ersatznamen wie oben.
- Die Testeinheit ist entfernt, die Kopien der Daten sind gelöscht.
- «Commit 4ebf22b-geändert» heisst: Die Dokumente lagen noch nicht committet im Arbeitsordner.

### Tests

| Lauf | Datum | Ergebnis |
|---|---|---|
| `ctest --test-dir build` (Arbeitsordner, Stand gleich `4ebf22b` ohne Dokumente, `dss-core-entwicklung.service` lief) | 2026-11-04 10:12:11, 127 s | 38 von 38 |
| `tst_medien` 50 Mal (`build/tests/tst_medien`, Stand vor dem `QSKIP` bei `DSS_MEDIA_SIZE=OFF`, siehe unten) | 2026-11-04 10:17:18 bis etwa 10:37:45 | **50 von 50**, je 24 bis 26 s |
| `tst_fensterbearbeiten` 50 Mal, erster Versuch | 2026-11-04 10:37:45 bis 10:41:36 | abgebrochen nach 8 Läufen, siehe unten |
| `tst_fensterbearbeiten` 50 Mal, zweiter Versuch, ohne andere Last, Stand `4ebf22b` | 2026-11-04 10:42:20 bis 11:03:57 | **44 von 50**, 6 Mal dieselbe Stelle, siehe «Wettlauf im Test» |
| `ctest --test-dir build` mit `df67eab` (= `5a76856`, Neubau, `dss-core-entwicklung.service` lief) | 2026-11-04 11:26:41 bis 11:28:47 | **38 von 38** |
| `tst_fensterbearbeiten` 50 Mal, dritter Versuch, Stand `df67eab` | 2026-11-04 11:29:04 bis etwa 11:50 | **45 von 50**, dieselbe Stelle |
| `tst_fensterbearbeiten` 50 Mal, vierter Versuch, Stand `9641893`, ohne andere Last | 2026-11-04 12:19:36 bis 12:41:59 | **50 von 50**, je 26.7 bis 27.1 s |
| `tst_medien` 50 Mal, Stand `df67eab` (neu gebaut; der Code ist gleich `5a76856`, der Commit danach `9641893` ändert nur `tst_fensterbearbeiten`) | 2026-11-04 etwa 11:50 bis 12:11:06 | **50 von 50**, je 24.3 bis 24.4 s |

Erster Versuch mit `tst_fensterbearbeiten`: Lauf 6 scheiterte in `usableWhileImportingLargeFile`. Beim Übernehmen von 1 GiB brauchte eine Antwort des Kerns 807 ms, die Grenze im Test ist 500 ms. Lauf 6 endete um 10:40:37, also während ich daneben `installieren.sh` baute (Release, `-j8`, 10:39:54 bis 10:40:57). Den Lauf habe ich deshalb abgebrochen und ohne andere Last neu gestartet. Daneben liefen danach nur Arbeiten an Texten. Unter solcher Last kann die Antwortzeit des Kerns beim Übernehmen also über 500 ms steigen; ohne Last gemessen in einem einzelnen Lauf danach (`XDG_CONFIG_HOME=$PWD/build/tests/fenster-config build/tests/tst_fensterbearbeiten usableWhileImportingLargeFile`, 2026-11-04 12:42:17): «Übernehmen 1024 MiB: 13691 ms; 121 Antworten des Kerns min 0 / Median 0 / max 215 ms; 24 Änderungen im Fenster max 56 ms; grösste Lücke der Ereignisschleife 19 ms». Die Läufe der 50er-Reihen geben diese Zeile nicht aus (die Protokolle bestandener Läufe werden gelöscht); nicht geprüft ist deshalb das Maximum über alle 50 Läufe.

Die ersten 50 Läufe von `tst_medien` nutzten einen Bau vor der letzten Änderung am Test: Der Fall `coreFormatsAndPlacement` überspringt sich jetzt bei `DSS_MEDIA_SIZE=OFF` (Definition aus `tests/CMakeLists.txt`). Darum liefen die 50 Läufe nach dem Neubau nochmals.

### Wettlauf im Test und Auswahl der Quelle (gefunden in den 50 Läufen)

Im zweiten Versuch scheiterte `usableWhileImportingLargeFile` 6 Mal an derselben Stelle. Die Pfeiltaste im Feld X änderte die geprüfte Quelle nicht (1500 statt 1501). Allein gestartet bestand der Fall 8 von 8 Mal.

1. **Erste Ausgabe** (vorübergehend im Test, nicht committet, 12 Läufe, 1 gescheitert): Im gescheiterten Lauf zeigte das Feld X den Wert 0 statt 1500, und keine Änderung ging an den Kern.
   - Daraus schloss ich falsch, im Fenster sei keine Quelle gewählt, weil `currentItemChanged` beim Markieren einer schon aktuellen Zeile nicht kommt.
   - Ich habe die Wahl auf `itemSelectionChanged` umgestellt (`5a76856`, ursprünglich `df67eab`). Danach scheiterte der dritte Versuch trotzdem 5 von 50 Mal an derselben Stelle.
2. **Zweite Ausgabe** (16 Läufe, 2 gescheitert): In beiden gescheiterten Läufen waren vor `selectSourceRow(0)` Zeile 0 markiert und «video.webm» mit x 480 im Feld. In den bestandenen Läufen war nichts markiert.

**Ursache:** `mediaDragAndDrop` löscht am Ende die beiden neuen Quellen über die zweite Verbindung und wartete nur auf deren `change_result`. Kam der nächste Fall schneller als `overlay_changed` beim Fenster, zeigte das Fenster noch «video.webm» als gewählte Quelle. Die Pfeiltaste galt dieser, schon gelöschten Quelle. Der Fehler lag also im Test, nicht im Fenster.

**Korrektur** (`9641893`): Der Fall wartet, bis auch das Fenster beide Quellen nicht mehr kennt.

**Die Umstellung in `5a76856` bleibt**, weil sie eine eigene Abweichung behebt: Ändert sich die Markierung, ohne dass die aktuelle Zeile wechselt, zeigten die Eigenschaften die Quelle der aktuellen Zeile statt der markierten (`src/app/mainwindow.cpp`, https://doc.qt.io/qt-6.8/qlistwidget.html#itemSelectionChanged).
- Der neue Fall `tst_fensterbearbeiten::sourceSelectionFollowsMark` markiert Zeile 0, während Zeile 1 aktuell bleibt.
- Mit der alten Verbindung scheitert er («alpha.webm» statt «Text: Willkommen»), mit der neuen besteht er (Gegenprobe 2026-11-04 um 11:25).
- Wie oft ein Nutzer diesen Zustand mit Maus oder Tastatur erreicht, ist nicht geprüft.

### Startprüfung

```
cd ~/DoNs_Streaming_Studio && python3 scripts/startpruefung.py --runs 20 --out <Notizordner>/start/startpruefung
```

2026-11-04 12:42:31 bis 12:44:45, Stand `9641893`, `DISPLAY=:0.0`, Exit 0. `dss-core-entwicklung.service` lief daneben (Port 8081). Die Kerne der Prüfung lauschten auf Port 32488 (32000 + PID % 768, Punkt 83), auf 127.0.0.1 und 192.168.1.9 (Protokoll in `startpruefung-core-sigterm.json`). Ergebnisse in `docs/berichte/P10b-daten/startpruefung-*.md` und `.json`.

| Fall | sauber | Start ms (min / Median / max) | Ende ms (min / Median / max) |
|---|---|---|---|
| `core-sigterm` | 20 von 20 | 36,0 / 43,8 / 48,0 | 3,8 / 4,0 / 6,0 |
| `core-sigint` | 20 von 20 | 36,4 / 43,0 / 46,4 | 3,6 / 3,9 / 4,3 |
| `app-sigterm` | 20 von 20 | 232,1 / 247,2 / 263,9 | 13,6 / 17,7 / 23,3 |
| `app-kern-sigterm` | 20 von 20 | 226,3 / 241,0 / 257,2 | 17,9 / 19,7 / 23,5 |

Alle Läufe: Exit-Code 0, Fenster sichtbar (Fälle mit `dss-app`), keine Restprozesse, keine unerwarteten und keine fehlenden Zeilen.

### Dienstprüfung

```
cd ~/DoNs_Streaming_Studio && python3 scripts/dienstpruefung.py --kills 5 --out <Notizordner>/dienstN
```

`dss-core-entwicklung.service` lief daneben. Die Prüfeinheit `dss-core-pruefung.service` nutzte eine eigene `kern.ini` mit Port 32505 (Journal: «Overlay-Server lauscht auf 127.0.0.1:32505» und «192.168.1.9:32505»). Alle Ausgaben in `docs/berichte/P10b-daten/dienstpruefung.txt`.

| Lauf | Stand | Beginn | Ergebnis |
|---|---|---|---|
| 0 | `9641893` | 2026-11-04 12:46:07 | **nicht bestanden** in Schritt 5: «erstes Fenster: «Kern läuft weiter: 1 andere Fenster verbunden»» |
| 1 | `2e15fe9` | 2026-11-04 12:47:48 | bestanden; Neustart nach `kill -9` 1.103 bis 1.150 s, Fenster wieder verbunden 1.623 bis 2.678 s |
| 2 | `2e15fe9` | 2026-11-04 12:48:13 | bestanden; 1.096 bis 1.120 s, 1.965 bis 3.122 s |
| 3 | `2e15fe9` | 2026-11-04 12:48:39 | bestanden; 1.094 bis 1.143 s, 1.934 bis 2.975 s |

Ursache von Lauf 0: Das erste der zwei Fenster bekam SIGTERM um 12:46:27.483, 55 ms nachdem der Kern «Verbunden» geschrieben hatte. Im Protokoll des Fensters fehlt jede Zeile zur Bitte zum Beenden, und der Kern meldet nur «Verbindung getrennt (noch 1)». Das Fenster hatte das hello des Kerns also noch nicht verarbeitet (`CoreClient::isConnected()` falsch) und schloss ohne Bitte (`MainWindow::closeEvent`). Das Verhalten des Fensters ist so gewollt; das Skript schickte das Signal zu früh. `2e15fe9` lässt das Skript warten, bis beide Fenster «Fenster gezeichnet» geschrieben haben, und dann 1 s. Schritt 5 des Skripts stammt aus P07 (`3357430` enthält dieselbe Prüfung); warum der Wettlauf erst jetzt auftrat, ist nicht geprüft (möglich: mehr Nachrichten nach dem hello, `media_list`).

### Prüfsummen

```
cd ~ && find .local/opt/dons-streaming-studio .local/share/dons-streaming-studio -type f -print0 | sort -z | xargs -0 sha256sum
```

Vorher 2026-11-04 09:12:49, 81 Dateien. Nachher 2026-11-04 14:38:34, 81 Dateien, mit `cmp` gleich wie vorher. In `~/.local/opt/dons-streaming-studio/` ist nichts installiert. Die echte `~/.config/dons-streaming-studio/fenster.ini` wurde zuletzt um 08:54:48 geändert, also vor Beginn dieser Sitzung (`ls -l --time-style=full-iso`). `dss-core-entwicklung.service` ist um 14:39 angehalten (`inactive`).

## Handtest

Grund (Regel 10): Aussehen und Bedienung des Docks im echten Fenster (Ziehen mit der Hand, Dateiauswahl, Lesbarkeit) und die echte Installation (Regel 18) kann nur der Nutzer prüfen. Was die Tests auf dem Studio-PC schon geprüft haben (Lage und Grösse neuer Quellen, nichts kopiert, nichts bleibt nach dem Abbrechen, Ersatznamen, Sicherung), muss der Nutzer nicht nachmessen. Die Prüfungen 1 bis 3 nutzen die Version aus dem Repo und den Entwicklungsordner. Befehle laufen im Terminal der Xfce-Sitzung. Vorher alle Fenster von DoNs Streaming Studio schliessen.

### Prüfung 1 – Dock ansehen und bedienen

```
cd ~/DoNs_Streaming_Studio && cmake --build build -j8 && systemctl --user stop dss-core-entwicklung.service; build/src/cli/dss-cli overlay-neu "Handtest P10b" 1920 1080 && build/src/cli/dss-cli quelle-bild handtest-p10b tests/daten/exif/orientierung-1.png && build/src/app/dss-app
```

Erwartet:

- Das Dock «Medien» liegt unten zwischen «Quellen» und «Eigenschaften» (die Version in Arbeit hat keine `fenster.ini`).
- Darin steht «Bild vom 2026-11-04» mit kleinem Vorschaubild, Typ «Bild», Grösse. Mit `dss-cli` angelegt, also mit Ersatznamen, nicht kursiv. Beim Darüberfahren: «Verwendet in Overlay «Handtest P10b» …».

Dann im Dock:

1. «Dateien hinzufügen …» (Plus): im Ordner `~/DoNs_Streaming_Studio/prototypes/media/` mit Strg+Klick `bild2.png`, `bild3.png` und `alpha.webm` wählen, «Öffnen». Erwartet: drei neue Zeilen mit diesen Namen, kursiv (ungenutzt), mit Vorschau.
2. «Ordner anlegen …» (Ordnersymbol): «Logos». Eine Datei mit der Maus auf «Logos» ziehen. Dann «Logos» wählen, nochmals «Ordner anlegen …»: «Alt». «Alt» liegt in «Logos».
3. Eine Datei wählen, F2: neuer Name. Doppelklick auf «Alt»: umbenennen.
4. Auf die Spaltenköpfe «Name», «Typ», «Grösse» klicken, je zweimal. Ordner bleiben oben.
5. Dock mit dem Knopf im Titel lösen und wieder andocken; im Menü «Docks» aus- und einblenden.

Zu beurteilen: Lesbarkeit, Vorschaubilder, Grösse der Zeilen, ob die Kursivschrift für «ungenutzt» genügt, ob Umbenennen per Dialog passt (Annahmen in Punkt 90).

### Prüfung 2 – Hinzufügen mit Fortschritt und Abbrechen, Hinweis zum Videoformat

Im selben Fenster lassen. In einem zweiten Terminal:

```
cd ~/DoNs_Streaming_Studio && mkdir -p build/handtest-p10b && ffmpeg -loglevel error -y -f lavfi -i testsrc=size=640x360:rate=30:duration=5 -c:v libx265 -pix_fmt yuv420p -x265-params log-level=error build/handtest-p10b/zaehler-hevc.mp4 && head -c 2G /dev/urandom > build/handtest-p10b/gross.png && ls -l build/handtest-p10b
```

Dann im Dock «Dateien hinzufügen …», in `~/DoNs_Streaming_Studio/build/handtest-p10b/` beide Dateien wählen. Erwartet:

- `zaehler-hevc.mp4` erscheint mit Warnzeichen in der Spalte «Typ». Die Statuszeile und der Tooltip sagen «zaehler-hevc.mp4 (HEVC) wird in der Browserquelle nicht angezeigt. In H.264 oder VP9 umwandeln.»
- `gross.png` steht als Zeile «übernehme … N %», die Prozent steigen. Diese Zeile wählen und «Löschen» drücken. Erwartet: Die Zeile verschwindet, die Statuszeile meldet «Übernehmen von gross.png abgebrochen.», es entsteht kein neuer Eintrag.

Das Fenster bleibt dabei bedienbar.

### Prüfung 3 – Ziehen auf Leinwand und Liste, Löschen

Overlay «Handtest P10b» im Dock «Overlays» wählen.

1. `bild2.png` aus dem Dock «Medien» auf die Leinwand ziehen und loslassen. Erwartet: eine neue Quelle in Originalgrösse, ihre Mitte an der Mausstelle; in «Medien» ist `bild2.png` nicht mehr kursiv.
2. `alpha.webm` in die Liste des Docks «Quellen» zwischen die beiden Quellen ziehen. Erwartet: neue Videoquelle an dieser Stelle der Liste, in der Mitte der Leinwand.
3. `bild3.png` wählen (ungenutzt) und «Löschen» drücken, die Rückfrage mit «Löschen» bestätigen. Erwartet: Die Zeile verschwindet.
4. `bild2.png` wählen (jetzt genutzt). Erwartet: «Löschen» ist ausgegraut; Tooltip und Zeile unter der Liste nennen «Verwendet in Overlay «Handtest P10b» (Quelle «bild2.png»)».

Fenster schliessen.

### Prüfung 4 – Echte Installation

Nur der Nutzer installiert (Regel 18). Die echten Daten haben noch kein `medien.json`. Das Skript sichert den Datenordner zuerst und legt die Datei dann mit Ersatznamen an. Das Fenster der installierten Version muss geschlossen sein.

```
cd ~/DoNs_Streaming_Studio && scripts/installieren.sh
```

Erwartet:

- Während des Laufs: «älteres Format: medien.json (fehlt, 10 Datei(en) in media/ bekommen Ersatznamen)», die Sicherung unter `~/.local/share/dons-streaming-studio-sicherungen/<Datum_Zeit>/` und «umgewandelt».
- Am Ende die Version mit Commit.

Dann «DoNs Streaming Studio» aus dem Anwendungsmenü starten. Erwartet:

- «Medien» ist ein dritter Reiter neben «Overlays» und «Quellen», wegen der gespeicherten Anordnung.
- Die 10 Dateien stehen mit Ersatznamen wie «Bild vom 2026-11-02», «Schrift vom 2026-11-03 (2)» usw. Zwei sind nicht kursiv (Bild in «Neues Overlay», Schrift der Textquelle «Herzlich Wilkommen»), die übrigen kursiv.
- Die Overlays sehen aus wie vorher, auch in OBS (die Browserquelle verbindet sich nach dem Neustart des Kerns von selbst wieder).
- Bitte auch ansehen, ob die Reiter lesbar sind (Punkt 86).

### Prüfung 5 – Aufräumen

Das Fenster der Entwicklung schliessen (falls noch offen), dann:

```
cd ~/DoNs_Streaming_Studio && systemctl --user stop dss-core-entwicklung.service; rm -rf ~/.local/share/dons-streaming-studio-entwicklung ~/.config/dons-streaming-studio-entwicklung build/handtest-p10b && ls ~/.local/share/ ~/.config/ | grep dons-streaming-studio
```

Erwartet: Es bleiben `dons-streaming-studio` (echte Daten, in `~/.local/share/` und in `~/.config/`) und `dons-streaming-studio-sicherungen`. Den Entwicklungsordner und `kern.ini` der Version in Arbeit hat diese Sitzung um 10:12 neu angelegt (Start von `dss-core-entwicklung.service` für die Prüfungen mit laufendem Dienst); sie enthalten nur Daten aus den Prüfungen 1 bis 3.

## Commits

- `749981a` P10b, Punkte 83 und 85: eigene Ports der Testkerne, Adresse im Heimnetz während des Laufs
- `6df2e9b` P10b, Teil A: Medienliste im Kern (E-28), Fortschritt und Abbrechen (73), Videoformate (81)
- `4ebf22b` P10b, Teil B: Dock «Medien» (E-28)
- `5a76856` P10b: Gewählte Quelle folgt der Markierung in der Liste «Quellen» (vor dem Berichtigen des Textes `df67eab`)
- `9641893` P10b: tst_fensterbearbeiten wartet nach mediaDragAndDrop auf das Fenster
- `2e15fe9` P10b: Dienstprüfung wartet in Schritt 5, bis beide Fenster gezeichnet sind
- `8e8e83d` P10b: Bau mit DSS_APP_MEDIA_DOCK=OFF verbindet das Dock «Medien» nicht
- `559d974` P10b: tst_fensterbearbeiten ohne Warnung bei DSS_MEDIA_SIZE=OFF
- (dieser Commit) P10b: Bericht, Dokumente nachgeführt, Messdaten

Begründung nach Regel 8:

- Die Punkte 83 und 85 stehen in einem Commit, weil beide dieselbe Stelle ändern: die Werte von `kern.ini` beim Start von `dss-core` und die Tests der Einstellungen in `tst_kernverbindung`. Keiner der beiden ändert die Schnittstelle.
- Teil A mit den Punkten 73 und 81 ist ein Commit: dieselbe Stelle (Übernehmen und Medienliste in `CoreService` und `OverlayStore`) und dieselbe Version 10 der Schnittstelle. Der Fortschritt läuft über dieselbe Nachricht wie die Medienliste, der Hinweis zum Format über `media_list` und `change_result`.
- Das Dock (Teil B) ist ein eigener Commit, ebenso die Umstellung der Auswahl (`5a76856`), die Korrektur im Test (`9641893`) die Korrektur in der Dienstprüfung (`2e15fe9`) und die beiden Korrekturen aus dem Schalter-Lauf (`8e8e83d` im Fenster, `559d974` im Test; verschiedene Dateien und Gründe, deshalb getrennt).

## Geänderte und neue Dateien

- Neu:
  - `src/common/medialibrary.h/.cpp`, `src/common/videoformat.h/.cpp`, `src/render/mediapreview.h/.cpp`
  - `src/app/mainwindowmedia.cpp`, `src/app/symbole/ordner-neu.svg`
  - `tests/tst_medienliste.cpp`
  - `docs/berichte/P10b-bericht.md`, `docs/berichte/P10b-daten/` (`installieren-pruefen.sh`, `installieren-ergebnis.txt`, `schalter-lauf.sh`, `schalter-ergebnis.txt`, `schalter-ergebnis-2e15fe9.txt`, `startpruefung-*.md` und `.json`, `dienstpruefung.txt`)
- Geändert:
  - `README.md`, `scripts/startpruefung.py`, `scripts/dienstpruefung.py`
  - `src/app/` (`CMakeLists.txt`, `canvaswidget.h/.cpp`, `docks.h/.cpp`, `dockviews.h/.cpp`, `mainwindow.h/.cpp`, `settingsdialog.cpp`, `theme.cpp`)
  - `src/cli/main.cpp`
  - `src/common/` (`CMakeLists.txt`, `coreclient.h/.cpp`, `coresession.h/.cpp`, `coresettings.h/.cpp`, `dataformat.h/.cpp`, `mediaimport.h/.cpp`, `overlaystore.h/.cpp`, `protocol.h/.cpp`, `windowdata.cpp`, `windowedit.h/.cpp`)
  - `src/core/` (`coreservice.h/.cpp`, `main.cpp`), `src/media/mediainfo.h/.cpp`, `src/render/CMakeLists.txt`
  - `tests/` (`CMakeLists.txt`, `prozesshilfe.h`, `tst_aenderungen.cpp`, `tst_datenformat.cpp`, `tst_fensterbearbeiten.cpp`, `tst_kerneinstellungen.cpp`, `tst_kernverbindung.cpp`, `tst_medien.cpp`, `tst_protocol.cpp`, `tst_uebernehmen.cpp`, `tst_windowdata.cpp`, `tst_windowedit.cpp`)
  - `docs/04-offene-punkte.md`, `docs/06-overlay-format.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`
- Ausserhalb des Repos:
  - Nur für Tests, wieder entfernt: Testkerne mit Datenordnern und `kern.ini` in temporären Ordnern und im Notizordner, Sockets unter `/run/user/1000/`, Testeinheiten `dss-core-installtest.service` und `dss-core-pruefung.service`.
  - `dss-core-entwicklung.service` habe ich um 10:12 gestartet (lief vorher nicht) und am Ende angehalten. Dabei legte der Kern `~/.local/share/dons-streaming-studio-entwicklung` (leer) und `~/.config/dons-streaming-studio-entwicklung/kern.ini` (Port 8081, Adresse 192.168.1.9) an.
  - Zwei verwaiste Testkerne von `tst_fensterbearbeiten` (gestartet 10:04 und 10:05, aus Läufen, die an einer Prüfzeile abstürzten) habe ich um 10:23 beendet und ihre Ordner in `/tmp` gelöscht (Punkt 87).
  - Die echten Daten, `~/.local/opt` und `~/.config/dons-streaming-studio` habe ich nicht verändert (Prüfsummen oben). Die echte `fenster.ini` hat der Test nur als Kopie gelesen.

## Nicht erledigt

- Handtest (Bedienung im echten Fenster, echte Installation).
- Echtes Ziehen des Kabels für Punkt 85; geprüft nur mit vorgegebener Route und Liste der Adressen im Test.
- Ob die Browserquelle in OBS dieselben Videoformate nicht kann (Punkt 81, weiterhin nicht geprüft).
- Die 50er-Läufe und die Startprüfung sind nicht mit dem Endstand `559d974` wiederholt, sondern mit `9641893`; die Commits danach ändern den Bau mit den Vorgaben nicht (siehe «Bau»).
- `docs/00-uebergabe.md` und `docs/01-festlegungen.md` («vier Docks unten») nicht geändert; beide pflegt der Chat (Projektplanung), der Auftrag nennt sie nicht.

## Annahmen

Siehe auch Punkt 90 in `docs/04-offene-punkte.md`:

- Beim Ablegen auf der Leinwand liegt die **Mitte** der neuen Quelle an der Mausstelle, damit die Quelle unter der Maus erscheint, auch wenn sie gross ist.
- F2 und Doppelklick öffnen einen Dialog wie bei Overlays und Quellen, kein Feld in der Zeile.
- Ungenutzte Dateien sind kursiv statt farbig (Stylesheet).
- Das Vorschaubild steht vor dem Namen in der Spalte «Name», nicht in einer eigenen Spalte. Grösse 40x24 Pixel (gewählt).
- Sortieren wird nicht gespeichert; Ordner immer zuerst.
- Ersatznamen mit dem Datum der letzten Änderung der Datei (Begründung oben).
- Ordner löschen: Inhalt eine Ebene höher (Begründung oben).
- Schriften lassen sich nicht auf Leinwand oder Liste ziehen (Punkt 89).
- Dienstprüfung, Schritt 5: «Fenster gezeichnet» und danach 1 s reichen, damit das Fenster das hello des Kerns verarbeitet hat (3 von 3 Läufen bestanden). Ein Zeichen des Fensters dafür im Protokoll gibt es nicht.
- Die Dienstprüfung (`scripts/dienstpruefung.py`) zählt zu den Testkernen aus Punkt 83 und bekommt eine eigene `kern.ini`. Der Auftrag nennt sie nicht ausdrücklich.

## `[OFFEN]`

- Punkt 86: Reiter der Docks ohne Stil (gewählter Reiter weiss, Text schwer lesbar); im echten Fenster nicht geprüft.
- Punkt 87: Testkerne laufen weiter, wenn ihr Test abstürzt.
- Punkt 88: `.mov` (und `.mkv`) sind keine erlaubten Endungen. Das Beispiel im Entscheid zu Punkt 81 («zaehler.mov (ProRes)») kann so nicht auftreten.
- Punkt 89: Ziehen einer Schrift.
- Punkt 90: Annahmen zu bestätigen.
- Unter starker Last (Bau mit 8 Threads daneben) stieg die Antwortzeit des Kerns beim Übernehmen von 1 GiB einmal auf 807 ms (Grenze im Test 500 ms). Ohne Last siehe «Tests».

## Quellen

- Qt 6.8:
  - https://doc.qt.io/qt-6.8/qmainwindow.html#restoreState
  - https://doc.qt.io/qt-6.8/qmainwindow.html#tabifyDockWidget
  - https://doc.qt.io/qt-6.8/qabstractitemview.html#DragDropMode-enum
  - https://doc.qt.io/qt-6.8/qdrag.html#exec
  - https://doc.qt.io/qt-6.8/qtreewidgetitem.html#operator-lt
  - https://doc.qt.io/qt-6.8/qtreeview.html#scrollTo
  - https://doc.qt.io/qt-6.8/qtest.html#mouseMove-1
  - https://doc.qt.io/qt-6.8/qsavefile.html
  - https://doc.qt.io/qt-6.8/qjsondocument.html
  - https://doc.qt.io/qt-6.8/qfileinfo.html#lastModified
  - https://doc.qt.io/qt-6.8/qimagereader.html
  - https://doc.qt.io/qt-6.8/qrawfont.html
  - https://doc.qt.io/qt-6.8/qpainter.html#drawGlyphRun
  - https://doc.qt.io/qt-6.8/qthreadpool.html
  - https://doc.qt.io/qt-6.8/qnetworkinterface.html
  - Quellcode 6.8.2, `QTreeWidget` (Sortieren, Ablegen): https://github.com/qt/qtbase/blob/v6.8.2/src/widgets/itemviews/qtreewidget.cpp
- FFmpeg 7.1:
  - https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html (`avformat_find_stream_info`)
  - https://ffmpeg.org/doxygen/7.1/group__lavc__core.html (`avcodec_get_name`)
  - https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libavcodec/codec_desc.c (Namen der Codecs)
  - https://ffmpeg.org/ffmpeg-filters.html (`testsrc`, Testvideos)
- Linux und Werkzeuge:
  - https://www.gnu.org/software/coreutils/manual/html_node/cp-invocation.html (`cp -a` behält Zeitstempel)
  - https://man7.org/linux/man-pages/man5/proc_pid_net.5.html (Standardroute)
  - https://man7.org/linux/man-pages/man7/ip.7.html (gültige Ports)
  - https://man7.org/linux/man-pages/man2/prctl.2.html (Punkt 87, nur Vorschlag)
- Videoformate in Chromium: Bericht P10, Abschnitt «Videoformate» (eigene Messung mit Qt WebEngine 6.8.2).
