# DoNs Streaming Studio

Private Software für den Twitch-Stream von Dominik Simmen. Sie übernimmt alles, was nicht zum Haupt-OBS gehört. Hauptfokus ist der Editor: pro Overlay eine eigene Leinwand, die als Browserquelle (später auch per NDI) an OBS ausgegeben wird.

Status: Aufbau. Es gibt das Projektgerüst (P03): Kern `dss-core` und Fenster `dss-app` starten und beenden sich sauber, enthalten aber noch keine Funktion. Seit P04 gibt es das Overlay-Modell mit Speichern und Laden in `dss_common` und das Werkzeug `dss-cli` für den Handtest. Technik: C++20 mit Qt 6 (siehe `docs/01-festlegungen.md`).

## Inhalt

| Ordner / Datei | Inhalt |
|---|---|
| `docs/00-uebergabe.md` | Übergabe: Stand, Entscheide, offene Punkte, nächste Schritte (zuerst lesen) |
| `CLAUDE.md` | Regeln für Claude Code in diesem Projekt |
| `docs/01-festlegungen.md` | Alle bisherigen Entscheide, mit Stand und Herkunft |
| `docs/02-umgebung.md` | Zielsystem (Studio-PC) mit gemessenen Ist-Werten |
| `docs/03-funktionen-entwurf.md` | Funktionen je Modul (Editor priorisiert) |
| `docs/04-offene-punkte.md` | Offene Punkte und Reihenfolge der nächsten Schritte |
| `docs/05-quellen.md` | Quellen |
| `docs/06-overlay-format.md` | Format der Overlay-Datei und Aufbau des Datenordners (Vorlage für beide Zeichner) |
| `docs/berichte/` | Berichte der Bau-Prompts (ab P03) |
| `prompts/` | Aufträge an Claude Code, in Reihenfolge, je eine eigene Sitzung |
| `CMakeLists.txt` | Oberste Build-Datei (C++20, Qt 6), Version des Programms, Schalter |
| `src/common/` | Bibliothek `dss_common`: Logik und Daten für Kern und Fenster, nur Qt Core |
| `src/core/` | Kern `dss-core` (Hintergrundprogramm, ohne Fenster) |
| `src/app/` | Fenster `dss-app` |
| `src/cli/` | Werkzeug `dss-cli` ohne Fenster (Overlays anlegen und bearbeiten, Handtest) |
| `web/` | Platz für den Zeichner der Browserquelle (noch leer) |
| `tests/` | Tests mit Qt Test, ausgeführt über `ctest` |
| `scripts/` | Einrichtungsskript, Paketliste, Startprüfung |
| `prototypes/` | Messprototypen aus P01 und P02, nicht Teil des Builds |

## Bauen und starten

Auf dem Studio-PC (Debian 13, X11-Sitzung), im Ordner des Repos:

```
scripts/einrichten.sh                 # Pakete aus scripts/pakete.txt installieren und prüfen
cmake -S . -B build                   # einrichten (Schalter siehe unten)
cmake --build build -j8               # bauen
ctest --test-dir build                # Tests
build/src/core/dss-core               # Kern starten, beenden mit Ctrl+C (SIGINT) oder SIGTERM
build/src/app/dss-app                 # Fenster starten, beenden durch Schliessen des Fensters
build/src/core/dss-core --version     # Version ausgeben (ebenso dss-app --version)
build/src/cli/dss-cli --help          # Overlays ohne Fenster bearbeiten (Befehle siehe Hilfe)
python3 scripts/startpruefung.py      # je 20 Starts von dss-core und dss-app prüfen
```

Schalter beim Einrichten (`cmake -S . -B build -D<Schalter>=OFF`):

| Schalter | Standard | Wirkung |
|---|---|---|
| `DSS_BUILD_APP` | `ON` | Fenster `dss-app` bauen; aus: nur Kern (und Tests) |
| `DSS_BUILD_TESTS` | `ON` | Tests bauen und bei `ctest` anmelden |
| `DSS_BUILD_CLI` | `ON` | Werkzeug `dss-cli` bauen |

Der Kern wird immer gebaut. Die Version steht nur in `CMakeLists.txt` (`project(… VERSION …)`).

Die Festlegungen wurden in einem Claude-Dokument gesammelt: https://claude.ai/artifact/HpSYSw4X1Gx5SUPZAAhW13. Dieses Repo enthält dieselben Inhalte als Dateien, damit Claude Code sie ohne Umweg lesen kann.
