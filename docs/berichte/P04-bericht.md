# P04 – Bericht Overlay-Modell und Format der Overlay-Datei

Stand: 2026-11-02. Alle Messwerte stammen vom Studio-PC (Debian 13, Kernel 6.12.113+deb13-amd64, Xfce/xfwm4 unter X11, `DISPLAY=:0.0`, `XDG_SESSION_TYPE=x11`, umask `0002`).

Abnahme Punkt 5 (Handtest durch den Nutzer): **ausstehend**, Befehle im Abschnitt «Handtest».

## Kurzfassung

| Abnahme | Ergebnis |
|---|---|
| 1. Bau ohne Warnungen mit `-Wall -Wextra`, jeder Schalter einzeln abschaltbar | ja: 0 Warnungen im sauberen Neubau und mit `DSS_BUILD_APP=OFF`, `DSS_BUILD_TESTS=OFF`, `DSS_BUILD_CLI=OFF` sowie allen dreien aus |
| 2. `ctest --test-dir build` | 5 von 5 bestanden (`tst_version`, `tst_unixsignals`, `tst_overlaymodel` 54 Fälle, `media_vorbereiten`, `tst_overlaystore` 18 Fälle) |
| 3. Startprüfung `scripts/startpruefung.py --runs 20` | 20/20 sauber in allen drei Fällen (60 Starts) |
| 4. `docs/06-overlay-format.md` vollständig, Beispiel lädt (Test) | ja: Test `tst_overlaystore::formatDocExample` liest das Beispiel aus der Datei und lädt es ohne Fehler und ohne Warnung |
| 5. Handtest durch den Nutzer | ausstehend |

## Aufbau

| Datei | Inhalt |
|---|---|
| `src/common/overlay.h/.cpp` | Datenmodell (`Overlay`, `Source`, `TextProperties`), Grenzen der Werte, Prüfungen (`validate…`), Bearbeiten der Quellen (hinzufügen, entfernen, vor/zurück, ein-/ausblenden, Lage/Grösse/Drehung), Anzeige der Drehung |
| `src/common/address.h/.cpp` | Adresse aus dem Namen, eindeutige Adresse mit `-2`, `-3` … |
| `src/common/overlayjson.h/.cpp` | Overlay ↔ JSON, strenges Lesen mit Feldpfad in der Meldung, `format_version` 1 |
| `src/common/overlaystore.h/.cpp` | Datenordner: Laden beim Start, Overlays anlegen/umbenennen/kopieren/löschen, Quellen bearbeiten, Kopien unter `media/`, atomares Speichern mit `QSaveFile`, Standard-Datenordner |
| `src/common/tokenstore.h/.cpp` | `tokens.json` mit Rechten 0600, Token erzeugen/entfernen |
| `src/cli/main.cpp`, `src/cli/CMakeLists.txt` | `dss-cli`, Schalter `DSS_BUILD_CLI` |
| `tests/tst_overlaymodel.cpp` | Modell ohne Dateien |
| `tests/tst_overlaystore.cpp` | Datenordner, Medien, Token, Beispiel aus `docs/06-overlay-format.md` |
| `docs/06-overlay-format.md` | Format, Datenordner, Adressregeln, vollständiges Beispiel |

Alles in `dss_common` nutzt nur Qt Core (Regel 9). `ldd` zeigt für `dss-cli` nur `libQt6Core.so.6`, für beide neuen Tests nur `libQt6Test.so.6` und `libQt6Core.so.6`.

Jede Änderung im Modell wird zuerst an einer Kopie des Overlays ausgeführt und geprüft, dann gespeichert; erst wenn das Speichern gelingt, gilt sie im Speicher. Ungültige Werte geben eine Meldung zurück, nichts wird still korrigiert.

### Datenordner

Ohne `--datenordner` nimmt `dss::defaultDataDir()` den Ordner aus `QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)`. Laut Qt-6.8-Dokumentation ist das unter Linux `~/.local/share/<APPNAME>` (Quelle: https://doc.qt.io/qt-6.8/qstandardpaths.html, abgerufen 2026-11-02; `<APPNAME>` ist dort «the organization name, the application name, or both»). Weil Kern, Fenster und `dss-cli` verschiedene Programmnamen haben, setzt `defaultDataDir()` den Programmnamen nur für diese Abfrage auf `dons-streaming-studio` (kein Organisationsname gesetzt). Ergebnis auf dem Studio-PC: **`/home/dominik/.local/share/dons-streaming-studio`**.

Geprüft (2026-11-02):

```
$ XDG_DATA_HOME=<Scratch>/xdg build/src/cli/dss-cli overlay-liste
Keine Overlays vorhanden.
$ find <Scratch>/xdg
<Scratch>/xdg/dons-streaming-studio
<Scratch>/xdg/dons-streaming-studio/media
<Scratch>/xdg/dons-streaming-studio/overlays
$ ls -d ~/.local/share/dons-streaming-studio
ls: Zugriff auf '/home/dominik/.local/share/dons-streaming-studio' nicht möglich: Datei oder Verzeichnis nicht gefunden
```

Dasselbe prüft `tst_overlaystore::defaultDataDir` (mit `XDG_DATA_HOME` auf den Testordner). Der echte Datenordner im Home wurde in dieser Sitzung nicht angelegt.

### Token

32 Byte aus `QRandomGenerator::system()` (laut Qt-Dokumentation unter Linux die Zufallsquelle des Betriebssystems, `/dev/urandom` bzw. `getrandom()`, «cryptographically safe»; https://doc.qt.io/qt-6.8/qrandomgenerator.html), Base64url ohne `=`, 43 Zeichen. `tokens.json` wird über `QSaveFile` geschrieben; direkt nach dem Öffnen und vor dem Schreiben setzt das Programm die Rechte 0600. Kein Token erscheint in Meldungen, Protokoll oder Ausgabe von `dss-cli`.

Verhalten beim Laden: fehlt `tokens.json`, gibt es noch keine Token; hat ein Overlay keinen Token, wird einer erzeugt und als Warnung gemeldet; andere Rechte als 0600 werden gemeldet und auf 0600 gesetzt; eine kaputte `tokens.json` lässt das Laden scheitern und wird nicht überschrieben.

## Ausgeführte Befehle und Ergebnisse

### Vorbereitung

```
$ git pull
Bereits aktuell.
$ fc-list | grep -i vietnam
(keine Ausgabe – Be Vietnam Pro ist nicht installiert)
$ fc-list | grep -i dejavu | head -3
/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf: DejaVu Serif:style=Bold
/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf: DejaVu Sans Mono:style=Book
/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf: DejaVu Sans:style=Book
$ dpkg -S /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf ; dpkg-query -W fonts-dejavu-core
fonts-dejavu-core: /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
fonts-dejavu-core	2.37-8
$ ls prototypes/media
alpha.webm  bild1.png  bild2.png  bild3.png  bild4.png  bild5.png  video.webm
$ sha256sum prototypes/media/bild1.png prototypes/media/alpha.webm /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
3bb2f4e86362dd6d44bfa852d36109788d1854c38cded41ce74f3758cecb099e  prototypes/media/bild1.png
09445b269e30ac5f3eddf936bc7a19a960ea82dbcd9ebfcc17ff5acace4c7bdf  prototypes/media/alpha.webm
57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e  /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf
$ dpkg-query -W ffmpeg
ffmpeg	7:7.1.5-0+deb13u1
$ command -v strace
(nicht installiert)
```

Die Testdateien unter `prototypes/media` stammen aus `prototypes/messung/media_erzeugen.sh` (P01/P02, nicht im Repo). Der ctest-Fall `media_vorbereiten` ruft das Skript auf, wenn `alpha.webm` fehlt; hier waren die Dateien vorhanden, das Skript lief also nicht.

**Es wurde kein neues Paket installiert.**

### Bau (Abnahme 1)

```
$ date '+%F %T %z'
2026-11-02 14:59:56 +0100
$ rm -rf build && cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j8 > build.log 2>&1 ; echo rc=$?
rc=0
$ grep -c -i -E 'warning|error' build.log
0
$ touch src/common/overlaystore.cpp && cmake --build build --verbose 2>&1 | grep -c -- '-Wall -Wextra'
1
```

Schalter, je frischer Build-Ordner im Scratch-Ordner (`grep 'Built target' | grep -v autogen`):

| Schalter | rc | Warnungen | gebaut |
|---|---|---|---|
| `-DDSS_BUILD_APP=OFF` | 0 | 0 | dss_common, dss-core, dss-cli, tst_version, tst_unixsignals, tst_overlaymodel, tst_overlaystore |
| `-DDSS_BUILD_TESTS=OFF` | 0 | 0 | dss_common, dss-core, dss-cli, dss-app |
| `-DDSS_BUILD_CLI=OFF` | 0 | 0 | dss_common, dss-core, dss-app, tst_version, tst_unixsignals, tst_overlaymodel, tst_overlaystore |
| alle drei `OFF` | 0 | 0 | dss_common, dss-core |

Korrektur beim Bau: keine Fehler beim Übersetzen. Beim ersten Testlauf schlugen zwei eigene Erwartungen fehl (falscher erwarteter Feldname im Fall «falsches Eigenschaftsobjekt»; im Fall `formatDocExample` wurde der Token schon beim ersten Laden erzeugt, nicht beim zweiten); korrigiert wurden die Tests, nicht der Code.

### Tests (Abnahme 2)

```
$ ctest --test-dir build
1/5 Test #1: tst_version ......................   Passed    0.00 sec
2/5 Test #2: tst_unixsignals ..................   Passed    0.00 sec
3/5 Test #3: tst_overlaymodel .................   Passed    0.01 sec
4/5 Test #5: media_vorbereiten ................   Passed    0.00 sec
5/5 Test #4: tst_overlaystore .................   Passed    0.13 sec
100% tests passed, 0 tests failed out of 5
$ build/tests/tst_overlaymodel | tail -2 | head -1
Totals: 54 passed, 0 failed, 0 skipped, 0 blacklisted, 2ms
$ build/tests/tst_overlaystore | tail -2 | head -1
Totals: 18 passed, 0 failed, 0 skipped, 0 blacklisted, 128ms
```

Abdeckung der im Prompt verlangten Fälle:

| Verlangt | Test |
|---|---|
| Speichern und Laden ergibt dasselbe Overlay | `tst_overlaystore::saveAndLoadGiveSameOverlay` (Bild, Video, WebM mit Alpha, Text, Geometrie, ausgeblendet, Reihenfolge; Vergleich mit `operator==`), `tst_overlaymodel::jsonRoundTrip` |
| Adresse aus Namen: Umlaute, Akzente, Sonderzeichen, nur Sonderzeichen, gleicher Name zweimal | `tst_overlaymodel::addressFromName` (12 Fälle, auch zerlegter Umlaut, Emoji, `ß`), `uniqueAddress`, `tst_overlaystore::addressFromNameTwice` |
| Umbenennen mit und ohne neue Adresse | `renameWithoutAddress`, `renameWithAddress` (auch nach erneutem Laden) |
| Kopieren: neue Kennungen, neuer Token | `copyOverlay` (Overlay- und alle Quellkennungen neu, Name «… Kopie», eigene Adresse, Token verschieden, keine neuen Medienkopien, zweite Kopie `-2`) |
| Löschen: Datei und Token weg | `removeOverlay` |
| Gleiche Mediendatei zweimal → eine Kopie | `sameMediaTwiceGivesOneCopy` (auch unter anderem Dateinamen und Endung in Grossbuchstaben; Name = SHA-256 des Inhalts) |
| Ungültige Werte abgewiesen | `tst_overlaymodel::invalidValuesRejected` (18 Fälle), `jsonRejected` (16 Fälle, u. a. unbekannte Art, Drehung 3600, Kommazahl, unbekanntes Feld), `tst_overlaystore::invalidValuesRejected` (Datei bleibt byte-gleich) |
| Kaputte Datei | `brokenFileIsReportedAndKept` (Meldung mit Dateiname, andere Overlays geladen, Datei nach weiteren Änderungen byte-gleich; falscher Dateiname) |
| Höhere `format_version` | `higherFormatVersionRejected`, `jsonRejected(format_version 2)` |
| Fehlende Mediendatei | `missingMediaReportedPerSource` (eine Meldung je Quelle, Overlay geladen) |
| Dateirechte von `tokens.json` | `tokenFile` (0600 trotz umask 0002, nach erneutem Schreiben, falsche Rechte werden gemeldet und gesetzt), `tokenMissingOrBroken`, `generatedTokensDiffer` (1000 Token, alle verschieden) |
| Beispiel aus `docs/06-overlay-format.md` | `formatDocExample` |

Testdateien (Regel 7): `prototypes/media/bild1.png`, `bild2.png`, `video.webm`, `alpha.webm` und `/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` – dieselben wie im Handtest. Die Pfade stehen als CMake-Cache-Variablen `DSS_TEST_MEDIA_DIR` und `DSS_TEST_FONT` in `tests/CMakeLists.txt`.

### Startprüfung (Abnahme 3)

```
$ python3 scripts/startpruefung.py --runs 20 --out docs/berichte/P04-daten
(Beginn 2026-11-02 15:01:09 +0100)
```

| Fall | sauber | bis Start min / Median / max (ms) | Beenden min / Median / max (ms) |
|---|---|---|---|
| `dss-core`, SIGTERM | 20/20 | 3,7 / 5,2 / 6,1 | 0,6 / 0,9 / 4,7 |
| `dss-core`, SIGINT | 20/20 | 3,9 / 5,4 / 6,9 | 0,7 / 0,9 / 1,9 |
| `dss-app`, SIGTERM | 20/20 | 92,2 / 97,9 / 102,0 | 5,5 / 8,1 / 10,0 |

Bei allen 60 Starts Exit-Code 0, keine Restprozesse, keine unerwarteten Zeilen; bei `dss-app` war das Fenster 20 von 20 Mal sichtbar. Rohdaten: `docs/berichte/P04-daten/`. Den Exit-Code des Skripts selbst habe ich nicht getrennt festgehalten (die Ausgabe lief über `tail`); laut Skript (`return 0 if all_ok else 1`) ist er bei 60 sauberen Starts 0.

### Probelauf von `dss-cli` und Gegenproben

Ausgeführt in einem Scratch-Datenordner mit denselben Befehlen wie im Handtest; die Ausgaben stehen unten als erwartete Ausgabe. Zusätzlich:

```
$ dss-cli --datenordner … quelle-geometrie pause 1 0 0 640 360 360 ; echo $?
Fehler: Drehung «360» ungültig, erlaubt sind 0 bis 359.9 Grad mit höchstens einer Nachkommastelle.
1
$ dss-cli --datenordner … overlay-neu "" 1920 1080 ; echo $?
Fehler: Der Name des Overlays darf nicht leer sein.
1
$ dss-cli --datenordner … overlay-neu x 0 10 ; echo $?
Fehler: Breite 0 ungültig, erlaubt sind 1 bis 7680 Pixel.
1
$ dss-cli ; echo $?
Fehler: Befehl fehlt.
(Hilfe)
2
$ stat -c '%a' …/tokens.json
600
```

Nicht geprüft: ob die temporäre Datei von `QSaveFile` beim Schreiben von `tokens.json` die Rechte 0600 schon vor dem ersten Byte hat. Der Code setzt sie vor dem Schreiben; ein Nachweis bräuchte `strace` (nicht installiert, nach Regel 5 nicht installiert). Geprüft ist nur die fertige Datei.

## Handtest

Für den Nutzer: im Terminal der Xfce-Sitzung, im Ordner des Repos, nach `git pull` und `cmake --build build -j8`. Der Handtest nutzt einen eigenen Datenordner `~/dss-handtest`, nicht den echten Datenordner. Der Ordner darf vorher nicht bestehen. Jeder Schritt ist ein Befehl; danach jeweils `echo $?` → erwartet `0` (ausser Schritt 15).

| Nr | Befehl | Erwartete Ausgabe |
|---|---|---|
| 1 | `build/src/cli/dss-cli --datenordner ~/dss-handtest overlay-liste` | `Keine Overlays vorhanden.` |
| 2 | `build/src/cli/dss-cli --datenordner ~/dss-handtest overlay-neu "Überblick Start" 1920 1080` | `Overlay «Überblick Start» angelegt: Adresse ueberblick-start, 1920x1080` |
| 3 | `build/src/cli/dss-cli --datenordner ~/dss-handtest quelle-bild ueberblick-start prototypes/media/bild1.png` | `Quelle 1 «bild1.png» (image) hinzugefügt, Datei media/3bb2f4e8…099e.png` (64 Zeichen SHA-256) |
| 4 | `build/src/cli/dss-cli --datenordner ~/dss-handtest quelle-text ueberblick-start "Willkommen" /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` | `Quelle 2 «Text: Willkommen» (text) hinzugefügt, Datei media/57f73e11…c84e.ttf` |
| 5 | `build/src/cli/dss-cli --datenordner ~/dss-handtest quelle-geometrie ueberblick-start 1 -100 200 640 360 12.5` | `Quelle 1: X -100, Y 200, Breite 640, Höhe 360, Drehung 12.5 °` |
| 6 | `build/src/cli/dss-cli --datenordner ~/dss-handtest quelle-geometrie ueberblick-start 2 400 900 800 140 0` | `Quelle 2: X 400, Y 900, Breite 800, Höhe 140, Drehung 0.0 °` |
| 7 | `build/src/cli/dss-cli --datenordner ~/dss-handtest quelle-liste ueberblick-start` | Tabelle mit `1 image ja -100 200 640 360 12.5 ° bild1.png` und `2 text ja 400 900 800 140 0.0 ° Text: Willkommen` |
| 8 | `build/src/cli/dss-cli --datenordner ~/dss-handtest overlay-umbenennen ueberblick-start "Pause"` | `Overlay umbenannt in «Pause», Adresse ueberblick-start (unverändert)` |
| 9 | `build/src/cli/dss-cli --datenordner ~/dss-handtest overlay-liste` | `Pause  ueberblick-start  1920x1080  2` |
| 10 | `build/src/cli/dss-cli --datenordner ~/dss-handtest overlay-umbenennen ueberblick-start "Pause" --adresse-mitaendern` | `Overlay umbenannt in «Pause», Adresse pause (vorher ueberblick-start)` |
| 11 | `build/src/cli/dss-cli --datenordner ~/dss-handtest overlay-kopieren pause` | `Kopie «Pause Kopie» angelegt: Adresse pause-kopie, 2 Quelle(n)` |
| 12 | `build/src/cli/dss-cli --datenordner ~/dss-handtest overlay-liste` | zwei Zeilen: `Pause  pause  1920x1080  2` und `Pause Kopie  pause-kopie  1920x1080  2` |
| 13 | `build/src/cli/dss-cli --datenordner ~/dss-handtest overlay-loeschen pause-kopie` | `Overlay «Pause Kopie» (Adresse pause-kopie) gelöscht` |
| 14 | `build/src/cli/dss-cli --datenordner ~/dss-handtest overlay-liste` | nur noch `Pause  pause  1920x1080  2` |
| 15 | `build/src/cli/dss-cli --datenordner ~/dss-handtest quelle-geometrie pause 1 0 0 640 360 360` | `Fehler: Drehung «360» ungültig, …`; `echo $?` → `1` |
| 16 | `cat ~/dss-handtest/overlays/*.json` | ein JSON-Objekt (siehe unten), `"address": "pause"`, `"format_version": 1`, zwei Quellen, Bild mit `"rotation": 125`, `"x": -100`; kein Feld `token` |
| 17 | `ls -l ~/dss-handtest ~/dss-handtest/media` | `tokens.json` mit `-rw-------`, in `media/` genau zwei Dateien (`.png`, `.ttf`) |
| 18 | `rm -r ~/dss-handtest` | (keine Ausgabe; räumt den Handtest-Ordner weg) |

Erwartete Ausgabe von Schritt 16 (Kennungen sind bei jedem Lauf anders; Felder alphabetisch):

```json
{
    "address": "pause",
    "format_version": 1,
    "height": 1080,
    "id": "4b488ad1-1178-4ee8-975e-561db63d60c5",
    "name": "Pause",
    "sources": [
        {
            "height": 360,
            "id": "15b130e7-71a1-4e38-b5df-567eff67b75d",
            "image": {
                "file": "media/3bb2f4e86362dd6d44bfa852d36109788d1854c38cded41ce74f3758cecb099e.png"
            },
            "kind": "image",
            "name": "bild1.png",
            "rotation": 125,
            "visible": true,
            "width": 640,
            "x": -100,
            "y": 200
        },
        {
            "height": 140,
            "id": "ad9f664f-6516-42cd-967c-a90fa8c28161",
            "kind": "text",
            "name": "Text: Willkommen",
            "rotation": 0,
            "text": {
                "align": "left",
                "color": "#FFFFFFFF",
                "content": "Willkommen",
                "font_file": "media/57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e.ttf",
                "font_size": 48,
                "italic": false,
                "weight": 400
            },
            "visible": true,
            "width": 800,
            "x": 400,
            "y": 900
        }
    ],
    "width": 1920
}
```

Die SHA-256-Werte hängen vom Inhalt der Dateien ab; `bild1.png` wird von `media_erzeugen.sh` erzeugt und kann sich bei einer Neuerzeugung ändern.

## Geänderte und neue Dateien

| Datei | Art | Commit |
|---|---|---|
| `src/common/overlay.h/.cpp`, `src/common/address.h/.cpp` | neu | 3cca7de |
| `src/common/CMakeLists.txt` | Dateien ergänzt | 3cca7de, 00a9744, 57eebd8 |
| `src/common/overlayjson.h/.cpp`, `src/common/overlaystore.h/.cpp` | neu | 00a9744 |
| `src/common/tokenstore.h/.cpp`; `overlaystore.h/.cpp` angepasst | neu / geändert | 57eebd8 |
| `src/cli/main.cpp`, `src/cli/CMakeLists.txt`; `CMakeLists.txt` (Schalter `DSS_BUILD_CLI`) | neu / geändert | f8f6bc3 |
| `tests/tst_overlaymodel.cpp`, `tests/tst_overlaystore.cpp`, `tests/CMakeLists.txt` | neu / geändert | ff1ef86 |
| `docs/06-overlay-format.md`; Test `formatDocExample` | neu / geändert | 1634d11 |
| `docs/berichte/P04-bericht.md`, `docs/berichte/P04-daten/…` | neu | Bericht-Commit |
| `README.md` (Status, `dss-cli`, Schalter, `docs/06`), `docs/00-uebergabe.md` (Stand P04), `docs/04-offene-punkte.md` (Punkte 17 bis 25), `docs/05-quellen.md` (Qt-Quellen) | geändert | Bericht-Commit |

`prototypes/`, `src/core/`, `src/app/` und `scripts/` sind unverändert.

## Nicht erledigt

- **[OFFEN]** Handtest durch den Nutzer (Abnahme 5).
- **[OFFEN]** Eigenschaften der Quelle Video (Wiederholen, Ton) sind nicht entschieden; `video` hat nur `file` (Auftrag; `docs/04-offene-punkte.md` Punkt 17).
- **[OFFEN]** Drehrichtung ist nicht festgelegt (Punkt 18). Das Format speichert nur den Winkel; beide Zeichner müssen dieselbe Richtung verwenden.
- **[OFFEN]** `ß` wird nach der Regel wörtlich zu `-` («Straße» → `stra-e`), ebenso Buchstaben ohne Unicode-Zerlegung (`ł`, `æ`, `ø`). Ob `ß` → `ss` gewünscht ist, entscheidet der Nutzer (Punkt 19).
- **[OFFEN]** Be Vietnam Pro ist auf dem Studio-PC nicht installiert; Tests und Handtest nutzen DejaVu Sans (`fonts-dejavu-core` 2.37-8). Laut Prompt «kommt Be Vietnam Pro mit dem Zeichner» (Punkt 23).
- Nicht geprüft: Rechte der temporären Datei von `tokens.json` vor dem ersten Byte (kein `strace`; Punkt 25).
- Nicht Teil des Auftrags: Verteilung an die Zeichner (K-02), Webserver, Browserquelle, Verbindung Kern–Fenster, Zeilenumbruch, SOLL/KANN-Eigenschaften, Oberfläche, Löschen ungenutzter Kopien unter `media/`. `dss-cli` hat nur die im Prompt genannten Befehle; Entfernen, Reihenfolge und Ein-/Ausblenden von Quellen gibt es im Modell (getestet), aber nicht in `dss-cli`.

## Annahmen

1. **Programmname für den Datenordner** `dons-streaming-studio` (im Prompt nicht vorgegeben). Kein Organisationsname, damit der Pfad `~/.local/share/dons-streaming-studio` ist.
2. **Neue Quelle**: liegt zuoberst, an Lage 0/0, in der Grösse des Overlays (die natürliche Grösse von Bild/Video zu lesen bräuchte einen Dekoder; Punkt 20).
3. **Name einer neuen Quelle**: bei Bild und Video der ursprüngliche Dateiname (Prompt), bei Text «Text: <erste Zeile>» (höchstens 40 Zeichen; nach dem Bild der Oberfläche), bei leerem Text «Text».
4. **Standardwerte Text**: Gewicht 400, nicht kursiv, 48 px, `#FFFFFFFF`, `left` (im Prompt nicht vorgegeben).
5. **Erlaubte Endungen**: Bild `png`/`jpg`/`jpeg`, Video `webm`/`mp4`, Schrift `ttf`/`otf`; geprüft wird nur die Endung (Punkt 21). GIF fehlt, weil E-06 SOLL ist.
6. **Name der Quelle** darf nicht leer sein (wie beim Overlay; im Prompt nicht ausdrücklich).
7. **Strenges Lesen**: fehlende und unbekannte Felder machen eine Datei ungültig («nicht geraten»). Eine spätere Formatversion muss das über `format_version` lösen.
8. **Farbe**: `#RRGGBBAA` mit Gross- oder Kleinbuchstaben; gespeichert wie eingegeben.
9. **Gewicht** 100 bis 900 in Schritten von 1 (nicht nur Hunderter), wie im Prompt als Bereich angegeben.
10. **Adresse beim Laden**: Ist eine Adresse schon von einer früher geladenen Datei belegt (Reihenfolge: Dateiname), gilt die spätere Datei als kaputt und wird nicht geladen.
11. **Overlay ohne Token** beim Laden: neuer Token, Meldung als Warnung. Ein Overlay ohne Token wäre sonst über die Browserquelle nicht erreichbar.
12. **Falsche Rechte von `tokens.json`** beim Laden: werden auf 0600 gesetzt und gemeldet.
13. **Reihenfolge der Overlays**: nach Name ohne Gross-/Kleinschreibung, dann Adresse; nicht gespeichert (Punkt 22).
14. **`dss-cli`**: DREHUNG in Grad mit höchstens einer Nachkommastelle (`12.5`), wie die Anzeige «0.0 °»; NR ist die Nummer aus `quelle-liste` (1 = ganz hinten). Exit-Code 2 bei falschem Aufruf. Kaputte Dateien und Warnungen beim Laden gehen nach stderr, der Befehl läuft trotzdem.
15. **Token-Länge** 256 Bit (Prompt: mindestens 128).
16. **K-01 «Nummer pro Quelle»**: als Kennung (`id`) und Lage in der Liste `sources` umgesetzt; eine eigene Nummer gibt es nicht.

## Quellen

Abgerufen in dieser Sitzung (2026-11-02):

- Qt 6.8, QStandardPaths (Tabelle «Linux and UNIX»: `AppDataLocation` = `~/.local/share/<APPNAME>`, `/usr/local/share/<APPNAME>`, `/usr/share/<APPNAME>`; erster Eintrag ist der schreibbare): https://doc.qt.io/qt-6.8/qstandardpaths.html
- Qt 6.8, QSaveFile (`commit()` benennt die temporäre Datei um; bei Fehler bleibt die alte Datei, die temporäre wird gelöscht; `directWriteFallback` ohne Atomarität, hier nicht eingeschaltet): https://doc.qt.io/qt-6.8/qsavefile.html
- Qt 6.8, QRandomGenerator (`system()` nutzt die Zufallsquelle des Betriebssystems, unter Linux kryptografisch sicher; `fillRange`): https://doc.qt.io/qt-6.8/qrandomgenerator.html

Nicht abgerufen (aus dem Wissensstand, im Code oder hier als Verweis genannt):

- Qt 6.8, `QFileDevice::setPermissions`: https://doc.qt.io/qt-6.8/qfiledevice.html#setPermissions
- Qt 6.8, `QString::normalized` (NFC/NFD): https://doc.qt.io/qt-6.8/qstring.html#normalized
- Qt 6.8, QJsonDocument/QJsonObject (Schlüssel alphabetisch sortiert): https://doc.qt.io/qt-6.8/qjsonobject.html
- Qt 6.8, QCryptographicHash: https://doc.qt.io/qt-6.8/qcryptographichash.html
- CMake 3.31, CTest-Fixtures (`FIXTURES_SETUP`/`FIXTURES_REQUIRED`): https://cmake.org/cmake/help/v3.31/prop_test/FIXTURES_REQUIRED.html
- Unicode Standard Annex #15, Normalisierung: https://unicode.org/reports/tr15/
- XDG Base Directory Specification (`XDG_DATA_HOME`): https://specifications.freedesktop.org/basedir-spec/latest/
- Debian-Pakete: Ausgaben von `dpkg-query` und `dpkg -S` auf diesem Gerät
