# P03 – Projektgerüst und Einrichtungsskript

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` und die Berichte `docs/messungen/P01-bericht.md` und `docs/messungen/P02-bericht.md`.

## Ausgangslage

Technik ist entschieden: C++ mit Qt 6, eigene Leinwand, Kern (Hintergrundprogramm) und Fenster getrennt, beide in C++ (`docs/01-festlegungen.md`). Die Prioritäten des Editors (E-01 bis E-19, TX-01 bis TX-16) und das Konzept der zwei Zeichner (K-01 bis K-08) stehen fest. Dies ist der erste Baustein der eigentlichen Software. Er enthält noch keine Funktion, sondern das Gerüst, auf dem die nächsten Bausteine aufbauen.

## Auftrag

### 1. Einrichtungsskript

- `scripts/einrichten.sh` installiert alle Pakete, die zum Bauen und Testen nötig sind, und prüft danach, ob alles da ist.
- Die Liste der Pakete mit Versionen steht in `scripts/pakete.txt` (eine Zeile pro Paket, Name und erwartete Version). Ausgangspunkt sind die Pakete, die P01 und P02 bereits installiert haben und die das Gerüst braucht: `build-essential`, `cmake`, `qt6-base-dev`, `libavformat-dev`, `libavcodec-dev`, `libavutil-dev`, `libswscale-dev`. Prüfe mit `apt-cache policy` und `dpkg-query`, welche Versionen auf dem Studio-PC stehen, und trage diese ein.
- Nicht in die Liste: `qt6-webengine-dev`, `qt6-multimedia-dev`, PyQt6. Diese Wege werden nicht weiterverfolgt. Sie werden **nicht** deinstalliert.
- Das Skript darf mehrfach laufen, ohne Schaden anzurichten. Es installiert über `sudo apt install`, der Nutzer bestätigt. Weicht eine installierte Version von `pakete.txt` ab, meldet das Skript es deutlich und bricht nicht still ab.
- Fehlt ein Paket, das du für das Gerüst brauchst (zum Beispiel für Qt Test), nenne es dem Nutzer mit Begründung, **bevor** du es installierst.

### 2. Projektgerüst

Aufbau (Vorschlag, Abweichungen im Bericht begründen):

| Ordner | Inhalt |
|---|---|
| `CMakeLists.txt` | Oberste Build-Datei, C++20, Qt 6 |
| `src/common/` | Bibliothek `dss_common` für Logik und Daten, die Kern und Fenster teilen. Nur Qt Core, kein Qt Gui, kein Qt Widgets (`CLAUDE.md` Regel 9). In P03 nur ein Beispiel: Versionsangabe des Programms. |
| `src/core/` | Programm `dss-core` (Kern), ohne Fenster |
| `src/app/` | Programm `dss-app` (Fenster) |
| `web/` | Platz für den Zeichner der Browserquelle, in P03 nur eine Datei `LIESMICH.md` mit einem Satz zum Zweck |
| `tests/` | Tests mit Qt Test, ausgeführt über `ctest` |
| `scripts/` | Einrichtungsskript, Paketliste, Startprüfung |

- `dss-core` startet ohne Fenster, schreibt eine Zeile ins Protokoll (Name, Version, Qt-Version), läuft weiter und beendet sich sauber mit Exit-Code 0 bei `SIGINT` und `SIGTERM`. Mit `--version` gibt es die Version aus und endet.
- `dss-app` öffnet ein leeres Hauptfenster mit dem Titel «DoNs Streaming Studio» und beendet sich sauber beim Schliessen. Mit `--version` gibt es die Version aus und endet. Noch keine Docks, keine Leinwand, keine Verbindung zum Kern.
- Die Version steht an einer Stelle (CMake) und kommt in beide Programme über `dss_common`.
- Schalter in CMake (Regel 8): `DSS_BUILD_APP` (Fenster bauen, Standard an) und `DSS_BUILD_TESTS` (Tests bauen, Standard an). Der Kern ist immer an.
- Build-Ordner `build/` steht in `.gitignore`.
- Die Prototypen unter `prototypes/` bleiben unverändert und gehören nicht zum Build.

### 3. Tests

- Ein Test in `tests/` prüft die Versionsangabe aus `dss_common`, ohne Qt Gui.
- `scripts/startpruefung.py` startet `dss-core` und `dss-app` je mindestens 20 Mal in der echten X11-Sitzung (Regel 6): Exit-Code, Dauer bis zum Start, sauberes Beenden (`dss-core` per `SIGTERM`, `dss-app` per Schliessen des Fensters oder `SIGTERM`; nenne, welchen Weg du nimmst und warum). Du darfst `prototypes/messung/t01.py` als Vorlage nehmen, nicht verändern.

### 4. Dokumentation

- `README.md`: Abschnitt «Bauen und starten» mit den Befehlen, Tabelle «Inhalt» um die neuen Ordner ergänzen, Satz «Es gibt noch keinen Programmcode» anpassen.
- `docs/02-umgebung.md`: installierte Versionen von `cmake` und den neuen Paketen nachtragen.

## Nicht Teil dieses Auftrags

- Overlay-Modell und Format der Overlay-Datei (K-01), kommt mit P04
- Verbindung zwischen Kern und Fenster, Webserver, Browserquelle, Zeichner
- Oberfläche (Docks, Leinwand, Kopfbereich), Twitch, OBS, NDI
- Chromium für den Bildvergleich (K-08), kommt mit dem Zeichner der Browserquelle

## Abnahme

1. `scripts/einrichten.sh` läuft auf dem Studio-PC zweimal hintereinander ohne Fehler; der zweite Lauf installiert nichts.
2. `cmake -S . -B build && cmake --build build` baut ohne Warnungen mit `-Wall -Wextra`. Mit `-DDSS_BUILD_APP=OFF` baut nur der Kern.
3. `ctest --test-dir build` besteht.
4. Startprüfung: 20 von 20 Starts je Programm sauber, Tabelle im Bericht.
5. Der Nutzer hat `dss-app` selbst gestartet, das Fenster gesehen und geschlossen (Handtest, Regel 10).

## Bericht

Schreibe den Bericht nach `docs/berichte/P03-bericht.md` (Ordner neu anlegen) nach dem Muster von `docs/messungen/P01-bericht.md` und Regel 15: geänderte Dateien, alle Befehle mit Ausgabe (gekürzt, wo sehr lang), Testergebnisse, installierte Versionen, nicht Erledigtes als `[OFFEN]`, getroffene Annahmen, Quellen (nur wirklich abgerufene, übrige als «nicht abgerufen»).

Ein Commit pro Teil (Einrichtungsskript, Gerüst, Tests, Dokumentation, Bericht). Pushe am Ende auf `main` (der Nutzer erteilt diesen Auftrag hiermit; für den Push braucht der Nutzer sein GitHub-Zugangstoken).
