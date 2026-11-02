# P03 – Bericht Projektgerüst und Einrichtungsskript

Stand: 2026-11-02. Alle Messwerte stammen vom Studio-PC (Debian 13, Kernel 6.12.113+deb13-amd64, Xfce/xfwm4 unter X11, `DISPLAY=:0.0`, `XDG_SESSION_TYPE=x11`). Kein Xvfb, keine offscreen-Plattform von Qt.

Abnahme Punkt 5 (Handtest: der Nutzer startet `dss-app`, sieht das Fenster und schliesst es): **[OFFEN]**, steht aus.

## Kurzfassung

| Abnahme | Ergebnis |
|---|---|
| 1. `scripts/einrichten.sh` zweimal ohne Fehler, zweiter Lauf installiert nichts | ja: beide Läufe Exit-Code 0, beide installieren nichts (alle Pakete waren schon da, siehe Annahme 1) |
| 2. Bau ohne Warnungen mit `-Wall -Wextra`; mit `-DDSS_BUILD_APP=OFF` nur der Kern | ja: 0 Warnungen; ohne Fenster werden `dss_common`, `dss-core` und die Tests gebaut, kein `dss-app` |
| 3. `ctest --test-dir build` | 2 von 2 Tests bestanden |
| 4. Startprüfung, 20 von 20 je Programm | `dss-core` SIGTERM 20/20, `dss-core` SIGINT 20/20, `dss-app` 20/20 |
| 5. Handtest durch den Nutzer | **[OFFEN]** |

## Aufbau

| Ordner / Datei | Inhalt |
|---|---|
| `CMakeLists.txt` | C++20, Qt 6.8 (`find_package(Qt6 6.8 …)`), Version `0.1.0` in `project(… VERSION …)`, Schalter `DSS_BUILD_APP` und `DSS_BUILD_TESTS` (beide Standard `ON`), Schnittstellen-Ziel `dss_warnings` mit `-Wall -Wextra` für alle eigenen Ziele. Ohne Angabe ist der Build-Typ `Debug`. |
| `src/common/` | Statische Bibliothek `dss_common`, nur `Qt6::Core`: `version.h/.cpp` (Version aus CMake über `DSS_VERSION`), `logging.h/.cpp` (Format des Protokolls, Startzeile), `unixsignals.h/.cpp` (SIGINT/SIGTERM in die Ereignisschleife) |
| `src/core/` | `dss-core`: `QCoreApplication`, Startzeile ins Protokoll, läuft bis SIGINT/SIGTERM, Exit-Code 0. `--version`, `--help` |
| `src/app/` | `dss-app`: leeres `QMainWindow` 1280x720, Titel «DoNs Streaming Studio». `--version` und `--help` werden vor `QApplication` ausgewertet und gehen daher auch ohne Fenstersystem. Meldet einmal «Fenster gezeichnet» nach dem ersten Zeichnen (wie die Prototypen von P01). SIGINT/SIGTERM schliessen das Fenster. |
| `web/LIESMICH.md` | ein Satz zum Zweck |
| `tests/` | `tst_version`, `tst_unixsignals` (Qt Test, `QTEST_GUILESS_MAIN`) |
| `scripts/` | `einrichten.sh`, `pakete.txt`, `startpruefung.py` |

Abweichungen vom Vorschlag im Prompt:

- `dss_common` enthält neben der Versionsangabe auch das Protokollformat und die Signalbehandlung. Begründung: Beide Programme brauchen dasselbe sauberes Beenden bei SIGINT/SIGTERM und dieselbe Protokollzeile (Name, Version, Qt-Version); so steht der Code nur einmal da und ist ohne Qt Gui testbar (Regel 9).
- Ein zweiter Test `tst_unixsignals` prüft die Signalbehandlung, weil das saubere Beenden Teil der Abnahme ist.
- `.gitignore` hatte schon `prototypes/**/build/`; neu dazu `/build/`.

Protokoll: stderr, Format `%{time yyyy-MM-dd hh:mm:ss.zzz} %{type} %{message}`. Beispiel aus Lauf 1 der Startprüfung von `dss-app`:

```
2026-11-02 13:39:09.137 info dss-app 0.1.0 gestartet, Qt 6.8.2
2026-11-02 13:39:09.148 info Fenster gezeichnet
2026-11-02 13:39:10.153 info SIGTERM erhalten, schliesse Fenster
2026-11-02 13:39:10.154 info beendet, Exit-Code 0
```

Die Qt-Version in der Startzeile ist die Laufzeitversion (`qVersion()`).

## 1. Einrichtungsskript

`scripts/einrichten.sh [Paketliste]` liest `scripts/pakete.txt`, installiert fehlende Pakete mit `sudo apt install Name=Version` (ohne `-y`, der Nutzer bestätigt) und prüft danach jedes Paket mit `dpkg-query` auf die erwartete Version sowie `cmake`, `g++`, `make` auf Vorhandensein. Exit-Code 0 = alles da, 1 = Paket fehlt oder Version weicht ab (Zeile `ABWEICHUNG` bzw. `FEHLT` und Hinweis am Schluss), 2 = Paketliste fehlt oder ist fehlerhaft. Kein `apt update`, nichts wird entfernt.

Ermittlung der Versionen (2026-11-02):

```
$ LANG=C apt-cache policy build-essential cmake qt6-base-dev libavformat-dev libavcodec-dev libavutil-dev libswscale-dev | grep -E '^[a-z]|Installed|Candidate'
build-essential:  Installed: 12.12                 Candidate: 12.12
cmake:            Installed: 3.31.6-2              Candidate: 3.31.6-2
qt6-base-dev:     Installed: 6.8.2+dfsg-9+deb13u2  Candidate: 6.8.2+dfsg-9+deb13u2
libavformat-dev:  Installed: 7:7.1.5-0+deb13u1     Candidate: 7:7.1.5-0+deb13u1
libavcodec-dev:   Installed: 7:7.1.5-0+deb13u1     Candidate: 7:7.1.5-0+deb13u1
libavutil-dev:    Installed: 7:7.1.5-0+deb13u1     Candidate: 7:7.1.5-0+deb13u1
libswscale-dev:   Installed: 7:7.1.5-0+deb13u1     Candidate: 7:7.1.5-0+deb13u1
(Ausgabe zusammengefasst, je Paket eine Zeile)
$ LANG=C dpkg-query -W -f='${Package} ${Version} ${Status}\n' … qt6-base-dev-tools libqt6test6 pkg-config
libqt6test6 6.8.2+dfsg-9+deb13u2 install ok installed
qt6-base-dev-tools 6.8.2+dfsg-9+deb13u2 install ok installed
pkg-config  unknown ok not-installed        # bereitgestellt durch pkgconf 1.8.1-4
$ dpkg -L qt6-base-dev | grep Qt6TestConfig.cmake
/usr/lib/x86_64-linux-gnu/cmake/Qt6Test/Qt6TestConfig.cmake
```

Qt Test ist also schon in `qt6-base-dev` enthalten. **Es wurde kein neues Paket installiert.**

Zwei Läufe hintereinander (Abnahme 1):

```
$ scripts/einrichten.sh
Alle Pakete aus /home/dominik/DoNs_Streaming_Studio/scripts/pakete.txt sind installiert, es wird nichts installiert.

Prüfung:
  ok          build-essential      12.12
  ok          cmake                3.31.6-2
  ok          qt6-base-dev         6.8.2+dfsg-9+deb13u2
  ok          libavformat-dev      7:7.1.5-0+deb13u1
  ok          libavcodec-dev       7:7.1.5-0+deb13u1
  ok          libavutil-dev        7:7.1.5-0+deb13u1
  ok          libswscale-dev       7:7.1.5-0+deb13u1

Alles da.
Exit-Code: 0
(zweiter Lauf: gleiche Ausgabe, Exit-Code 0)
```

Gegenproben mit veränderten Kopien der Liste im Scratch-Ordner (nicht im Repo):

```
$ scripts/einrichten.sh <Kopie mit «cmake 3.99.0-1»>
  ABWEICHUNG  cmake                erwartet 3.99.0-1, installiert 3.31.6-2
ACHTUNG: 1 Abweichung(en). Bitte prüfen und scripts/pakete.txt nur mit Begründung anpassen.
Exit-Code: 1
$ scripts/einrichten.sh <Datei mit Zeile «cmake» ohne Version>
FEHLER: Zeile in … muss «Name Version» sein: cmake
Exit-Code: 2
```

Nicht geprüft: der Installationsweg (fehlendes Paket), weil alle Pakete installiert waren. `shellcheck` ist nicht installiert; geprüft wurde nur die Syntax mit `bash -n`.

## 2. Bau

```
$ rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j8 2>&1 | grep -c -i -E 'warning|error'
0
$ touch src/core/main.cpp && cmake --build build --verbose 2>&1 | grep -c -- '-Wall -Wextra'
1                                  # Flags kommen beim Übersetzen an
$ ldd build/src/core/dss-core | grep -i qt
libQt6Core.so.6 => /lib/x86_64-linux-gnu/libQt6Core.so.6
$ ldd build/tests/tst_version | grep -i qt
libQt6Test.so.6 …
libQt6Core.so.6 …                  # kein Qt Gui
```

Schalter (je frischer Build-Ordner im Scratch-Ordner, `grep 'Built target' | grep -v autogen`):

```
== -DDSS_BUILD_APP=OFF
Built target dss_common / dss-core / tst_version / tst_unixsignals
== -DDSS_BUILD_APP=OFF -DDSS_BUILD_TESTS=OFF
Built target dss_common / dss-core
```

In beiden Fällen 0 Warnungen. Mit `DSS_BUILD_APP=OFF` sucht CMake auch Qt Widgets nicht.

Korrektur beim ersten Bau: Die Variable `signals` kollidierte mit dem Qt-Makro `signals` (Fehler «expected unqualified-id before ‹public›»), umbenannt in `signalWatcher`.

Handprüfung (2026-11-02, 13:36):

```
$ build/src/core/dss-core --version          → dss-core 0.1.0, rc=0
$ build/src/app/dss-app --version            → dss-app 0.1.0, rc=0
$ env -u DISPLAY build/src/app/dss-app --version → dss-app 0.1.0, rc=0
$ dss-core & kill -TERM                       → «SIGTERM erhalten, beende», rc=0
$ dss-core & kill -INT                        → «SIGINT erhalten, beende», rc=0
$ dss-app & xwininfo -name "DoNs Streaming Studio"
xwininfo: Window id: 0x1400007 "DoNs Streaming Studio"   Width: 1280  Height: 720  Map State: IsViewable
$ kill -TERM                                  → «SIGTERM erhalten, schliesse Fenster», rc=0
```

## 3. Tests

```
$ ctest --test-dir build
1/2 Test #1: tst_version ......................   Passed    0.00 sec
2/2 Test #2: tst_unixsignals ..................   Passed    0.00 sec
100% tests passed, 0 tests failed out of 2
```

- `tst_version`: Version gleich `PROJECT_VERSION` aus CMake (der Test bekommt sie getrennt als `DSS_EXPECTED_VERSION`), Form `Zahl.Zahl.Zahl`, `versionLine("dss-core")` = `dss-core 0.1.0`.
- `tst_unixsignals`: `raise(SIGINT)` und `raise(SIGTERM)` kommen je genau einmal als `terminationRequested` mit der richtigen Nummer an; ein zweites Objekt im selben Prozess ist ungültig.

### Startprüfung

Befehl: `python3 scripts/startpruefung.py --runs 20` (Beginn 2026-11-02 13:38:08 +0100, Exit-Code 0). Vorlage: `prototypes/messung/t01.py` (unverändert; die Hilfsfunktionen für Sitzungsprozesse sind ins neue Skript übernommen).

Gemessen je Start:
- **bis Start:** Zeit vor `Popen` bis zum Lesen der Protokollzeile «… gestartet, Qt …» (`dss-core`) bzw. «Fenster gezeichnet» (`dss-app`), CLOCK_MONOTONIC im Skript.
- **Fenster sichtbar** (nur `dss-app`): `xwininfo -name "DoNs Streaming Studio"` meldet `IsViewable`, 1 s nach dem Start.
- **Beenden:** 1 s nach dem Start Signal senden, Zeit bis Prozessende.
- **sauber:** Exit-Code 0, keine Zeitüberschreitung (Start 10 s, Ende 10 s), alle erwarteten Protokollzeilen da (einschliesslich «beendet, Exit-Code 0»), keine anderen Zeilen auf stderr, nichts auf stdout, Fenster sichtbar (bei `dss-app`), 2 s nach Ende kein Prozess der Sitzung mehr.

**Weg zum Beenden von `dss-app`: SIGTERM.** `dss-app` schliesst bei SIGTERM sein Hauptfenster (`QWidget::close()`); das Programm endet dann über «letztes Fenster geschlossen», also über denselben Weg wie beim Schliessen von Hand. Den Schliessknopf des Fenstermanagers automatisch zu drücken bräuchte ein Werkzeug wie `xdotool` oder `wmctrl`; beide sind nicht installiert und wurden nach Regel 5 nicht installiert. Diesen Weg deckt der Handtest ab.

Zusätzlich zum Prompt: `dss-core` auch 20-mal mit SIGINT, weil der Prompt für den Kern beide Signale verlangt.

| Fall | sauber | bis Start min / Median / max (ms) | Beenden min / Median / max (ms) |
|---|---|---|---|
| `dss-core`, SIGTERM | 20/20 | 3,6 / 5,3 / 5,9 | 0,7 / 0,9 / 0,9 |
| `dss-core`, SIGINT | 20/20 | 4,3 / 5,6 / 6,2 | 0,7 / 0,9 / 1,9 |
| `dss-app`, SIGTERM | 20/20 | 87,0 / 99,7 / 105,6 | 5,1 / 7,1 / 9,3 |

Bei allen 60 Starts: Exit-Code 0, keine Restprozesse, 0 unerwartete Zeilen; bei `dss-app` war das Fenster in 20 von 20 Starts sichtbar.

<details><summary>dss-core, SIGTERM</summary>

| Start | Exit-Code | bis Start (ms) | Beenden (ms) | sauber |
|---|---|---|---|---|
| 1 | 0 | 3.9 | 0.7 | ja |
| 2 | 0 | 5.3 | 0.9 | ja |
| 3 | 0 | 3.6 | 0.8 | ja |
| 4 | 0 | 5.3 | 0.9 | ja |
| 5 | 0 | 4.9 | 0.7 | ja |
| 6 | 0 | 5.6 | 0.9 | ja |
| 7 | 0 | 5.0 | 0.7 | ja |
| 8 | 0 | 5.1 | 0.9 | ja |
| 9 | 0 | 5.5 | 0.9 | ja |
| 10 | 0 | 4.0 | 0.9 | ja |
| 11 | 0 | 5.2 | 0.8 | ja |
| 12 | 0 | 4.2 | 0.9 | ja |
| 13 | 0 | 5.9 | 0.8 | ja |
| 14 | 0 | 4.4 | 0.7 | ja |
| 15 | 0 | 5.6 | 0.9 | ja |
| 16 | 0 | 5.0 | 0.9 | ja |
| 17 | 0 | 5.7 | 0.9 | ja |
| 18 | 0 | 5.3 | 0.8 | ja |
| 19 | 0 | 5.7 | 0.9 | ja |
| 20 | 0 | 5.8 | 0.9 | ja |

</details>

<details><summary>dss-core, SIGINT</summary>

| Start | Exit-Code | bis Start (ms) | Beenden (ms) | sauber |
|---|---|---|---|---|
| 1 | 0 | 5.3 | 0.9 | ja |
| 2 | 0 | 5.6 | 0.9 | ja |
| 3 | 0 | 4.9 | 0.9 | ja |
| 4 | 0 | 5.7 | 0.8 | ja |
| 5 | 0 | 5.2 | 0.9 | ja |
| 6 | 0 | 5.6 | 1.9 | ja |
| 7 | 0 | 5.6 | 1.9 | ja |
| 8 | 0 | 5.7 | 1.9 | ja |
| 9 | 0 | 6.0 | 0.9 | ja |
| 10 | 0 | 5.7 | 0.8 | ja |
| 11 | 0 | 5.6 | 0.9 | ja |
| 12 | 0 | 4.3 | 0.9 | ja |
| 13 | 0 | 5.6 | 0.9 | ja |
| 14 | 0 | 4.8 | 0.8 | ja |
| 15 | 0 | 6.0 | 1.0 | ja |
| 16 | 0 | 6.2 | 0.8 | ja |
| 17 | 0 | 5.2 | 0.9 | ja |
| 18 | 0 | 4.9 | 0.7 | ja |
| 19 | 0 | 5.6 | 0.9 | ja |
| 20 | 0 | 4.9 | 0.9 | ja |

</details>

<details><summary>dss-app, SIGTERM</summary>

| Start | Exit-Code | bis Start (ms) | Beenden (ms) | Fenster sichtbar | Restprozesse | unerwartete Zeilen | sauber |
|---|---|---|---|---|---|---|---|
| 1 | 0 | 87.0 | 6.5 | ja | keine | 0 | ja |
| 2 | 0 | 105.6 | 5.9 | ja | keine | 0 | ja |
| 3 | 0 | 100.6 | 7.0 | ja | keine | 0 | ja |
| 4 | 0 | 100.9 | 6.9 | ja | keine | 0 | ja |
| 5 | 0 | 97.0 | 8.3 | ja | keine | 0 | ja |
| 6 | 0 | 95.0 | 7.6 | ja | keine | 0 | ja |
| 7 | 0 | 99.7 | 5.1 | ja | keine | 0 | ja |
| 8 | 0 | 101.7 | 7.0 | ja | keine | 0 | ja |
| 9 | 0 | 98.4 | 5.7 | ja | keine | 0 | ja |
| 10 | 0 | 95.5 | 7.0 | ja | keine | 0 | ja |
| 11 | 0 | 99.7 | 8.7 | ja | keine | 0 | ja |
| 12 | 0 | 96.4 | 5.5 | ja | keine | 0 | ja |
| 13 | 0 | 100.1 | 7.6 | ja | keine | 0 | ja |
| 14 | 0 | 101.9 | 7.4 | ja | keine | 0 | ja |
| 15 | 0 | 100.5 | 9.3 | ja | keine | 0 | ja |
| 16 | 0 | 94.6 | 7.6 | ja | keine | 0 | ja |
| 17 | 0 | 98.2 | 8.0 | ja | keine | 0 | ja |
| 18 | 0 | 97.8 | 6.7 | ja | keine | 0 | ja |
| 19 | 0 | 101.7 | 7.1 | ja | keine | 0 | ja |
| 20 | 0 | 99.4 | 8.0 | ja | keine | 0 | ja |

</details>

Rohdaten (mit vollem Protokoll je Start): `docs/berichte/P03-daten/startpruefung-{core-sigterm,core-sigint,app-sigterm}.json` und `.md`.

Gegenprobe des Skripts: Ein Ersatzprogramm (Shell-Skript, schreibt die Startzeile und wartet mit `sleep`) endet bei SIGTERM mit Exit-Code −15 und wird als `"clean": false` gemeldet (fehlende Zeilen «SIGTERM erhalten, beende», «beendet, Exit-Code 0»).

Ein Probelauf mit 2 Starts je Fall (13:37) lief vor der Messung ebenfalls sauber.

## Geänderte und neue Dateien

| Datei | Art | Commit |
|---|---|---|
| `scripts/einrichten.sh`, `scripts/pakete.txt` | neu | 9f52cbd |
| `CMakeLists.txt` | neu | bce87a3, Schalter `DSS_BUILD_TESTS` in 8b01491 |
| `src/common/{CMakeLists.txt,version.h,version.cpp,logging.h,logging.cpp,unixsignals.h,unixsignals.cpp}` | neu | bce87a3 |
| `src/core/{CMakeLists.txt,main.cpp}` | neu | bce87a3 |
| `src/app/{CMakeLists.txt,main.cpp,mainwindow.h,mainwindow.cpp}` | neu | bce87a3 |
| `web/LIESMICH.md` | neu | bce87a3 |
| `.gitignore` | `/build/` ergänzt | bce87a3 |
| `tests/{CMakeLists.txt,tst_version.cpp,tst_unixsignals.cpp}`, `scripts/startpruefung.py` | neu | 8b01491 |
| `README.md` | Status-Satz, Tabelle «Inhalt», Abschnitt «Bauen und starten» | f932ecc |
| `docs/02-umgebung.md` | Abschnitt «Installierte Pakete zum Bauen und Testen», Satz zu nicht installierten Programmen | f932ecc |
| `docs/04-offene-punkte.md` | Punkte 14 bis 16 | f932ecc |
| `docs/berichte/P03-bericht.md`, `docs/berichte/P03-daten/…` | neu | Bericht-Commit |

`prototypes/` ist unverändert und nicht Teil des Builds.

## Nicht erledigt

- **[OFFEN]** Handtest durch den Nutzer (Abnahme 5): `build/src/app/dss-app` starten, Fenster «DoNs Streaming Studio» ansehen, über den Schliessknopf schliessen; im Terminal sollte «beendet, Exit-Code 0» stehen und `echo $?` 0 zeigen.
- **[OFFEN]** Schliessen über den Fenstermanager ist nicht automatisch geprüft (kein `xdotool`/`wmctrl`), siehe `docs/04-offene-punkte.md` Punkt 14.
- **[OFFEN]** Installationsweg von `einrichten.sh` nicht geprüft (Punkt 15).
- **[OFFEN]** `docs/00-uebergabe.md` sagt noch «Es gibt noch keinen Programmcode» (Punkt 16; nicht Teil des Auftrags).
- Die Qt-Anleitung zu Unix-Signalen deaktiviert den `QSocketNotifier` während des Lesens und aktiviert ihn danach wieder; `UnixSignalWatcher` liest ohne diesen Schritt genau ein Byte je Aktivierung. Nicht als Fehler beobachtet, aber eine Abweichung von der Anleitung.
- Nicht Teil des Auftrags: Overlay-Modell, Verbindung Kern–Fenster, Webserver, Zeichner, Oberfläche, Twitch, OBS, NDI, Chromium.

## Annahmen

1. Abnahme 1 «der zweite Lauf installiert nichts»: Auf dem Studio-PC waren alle Pakete aus P01/P02 schon installiert, also installierte auch der erste Lauf nichts.
2. Versionsnummer `0.1.0` (im Prompt nicht vorgegeben).
3. Fenstergrösse 1280x720 beim Start (im Prompt nicht vorgegeben).
4. Build-Typ `Debug`, wenn keiner angegeben ist (im Prompt nicht vorgegeben).
5. «Mit `-DDSS_BUILD_APP=OFF` baut nur der Kern» verstehe ich als «ohne Fenster»; die Tests werden mitgebaut, solange `DSS_BUILD_TESTS` an ist. Mit beiden Schaltern aus entstehen nur `dss_common` und `dss-core`.
6. Die Protokollzeilen gehen nach stderr (Standard von Qt ohne eigenen Handler); eine Protokolldatei gibt es noch nicht.
7. `x11-utils` (`xwininfo`) und `pkgconf` sind vorhanden, stehen aber nicht in `pakete.txt`, weil der Prompt die Liste vorgibt; `xwininfo` braucht nur die Startprüfung.

## Ausgeführte Befehle (Auswahl, chronologisch)

```
git pull                                            # Bereits aktuell.
LANG=C dpkg-query -W … ; LANG=C apt-cache policy …  # Versionen, siehe oben
dpkg -L qt6-base-dev | grep Qt6Test                 # Qt Test vorhanden
command -v xdotool wmctrl xwininfo pkg-config       # xdotool, wmctrl fehlen
bash -n scripts/einrichten.sh                       # Syntax ok
scripts/einrichten.sh (2×), Gegenproben             # siehe Abschnitt 1
cmake -S . -B build && cmake --build build -j8      # 0 Warnungen
build/src/{core/dss-core,app/dss-app} --version, Handprüfung mit kill -TERM/-INT, xwininfo
ctest --test-dir build                              # 2/2
python3 scripts/startpruefung.py --runs 2 --out <Scratch>   # Probelauf
python3 scripts/startpruefung.py --runs 1 --cases core-sigterm --build <Ersatzprogramm>   # Gegenprobe
rm -rf build && cmake … && cmake --build … && ctest …        # sauberer Neubau
python3 scripts/startpruefung.py --runs 20          # Messung, Exit-Code 0
cmake -S . -B <Scratch> -DDSS_BUILD_APP=OFF [-DDSS_BUILD_TESTS=OFF] && cmake --build …
```

## Quellen

Abgerufen in dieser Sitzung (2026-11-02):

- Qt 6.8, Calling Qt Functions From Unix Signal Handlers (Socket-Paar, `QSocketNotifier`, `sigaction` mit `SA_RESTART`): https://doc.qt.io/qt-6.8/unix-signals.html
- Qt 6.8, `qt_standard_project_setup` (seit Qt 6.3, schaltet AUTOMOC ein): https://doc.qt.io/qt-6.8/qt-standard-project-setup.html
- Qt 6.8, `qSetMessagePattern` (Platzhalter `%{time …}`, `%{type}`, `%{message}`; ohne eigenen Handler Ausgabe nach stderr; `QT_MESSAGE_PATTERN` hat Vorrang): https://doc.qt.io/qt-6.8/qtlogging.html
- Qt 6.8, QTest (`QTEST_GUILESS_MAIN` «instantiates a QCoreApplication object»): https://doc.qt.io/qt-6.8/qtest.html

Nicht abgerufen (aus dem Wissensstand, im Code oder hier als Verweis genannt):

- Qt Test Übersicht: https://doc.qt.io/qt-6.8/qttest-index.html
- signal-safety(7): https://man7.org/linux/man-pages/man7/signal-safety.7.html
- socketpair(2), sigaction(2): https://man7.org/linux/man-pages/man2/socketpair.2.html, https://man7.org/linux/man-pages/man2/sigaction.2.html
- proc(5) (Sitzungs-ID in `/proc/<pid>/stat`): https://man7.org/linux/man-pages/man5/proc.5.html
- dpkg-query(1) (`${db:Status-Status}`): https://manpages.debian.org/trixie/dpkg/dpkg-query.1.en.html
- xwininfo(1): https://manpages.debian.org/trixie/x11-utils/xwininfo.1.en.html
- Python `selectors`, `subprocess`: https://docs.python.org/3/library/selectors.html, https://docs.python.org/3/library/subprocess.html
- Debian-Pakete: Ausgaben von `apt-cache policy` und `dpkg-query` auf diesem Gerät
