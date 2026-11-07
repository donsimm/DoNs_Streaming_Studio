# DoNs Streaming Studio

Private Software für den Twitch-Stream von Dominik Simmen. Sie übernimmt alles, was nicht zum Haupt-OBS gehört. Hauptfokus ist der Editor: pro Overlay eine eigene Leinwand, die als Browserquelle (später auch per NDI) an OBS ausgegeben wird.

Status: Aufbau. Es gibt das Projektgerüst (P03): Kern `dss-core` und Fenster `dss-app` starten und beenden sich sauber, enthalten aber noch keine Funktion. Seit P04 gibt es das Overlay-Modell mit Speichern und Laden in `dss_common` und das Werkzeug `dss-cli` für den Handtest. Seit P05 sieht das Fenster `dss-app` aus wie `docs/bilder/oberflaeche.png`, noch ohne Daten und ohne Verbindung zum Kern; seit P06 mit Farben und Schrift wie OBS Studio (Design «Yami», Open Sans). Seit P07 läuft der Kern als Benutzerdienst von systemd (`dss-core.service`, gestartet vom Fenster), hält die Overlays und schickt sie über einen Unix-Socket an das Fenster; das Fenster zeigt Overlays, Quellen und die Werte CPU, RAM und LAN an. Seit P08 lassen sich Overlays (mit Ordnern), Quellen und ihre Eigenschaften im Fenster bearbeiten. Seit P09 zeichnet die Leinwand Bild, Text, Video und WebM mit Alpha, und Quellen lassen sich dort mit der Maus wählen, verschieben, skalieren und drehen. Seit P09b lassen sich Quellen spiegeln und über «Bearbeiten» → «Transformieren» (auch per Rechtsklick) wie in OBS drehen, einpassen, zentrieren, zurücksetzen, kopieren und einfügen; Bilder erscheinen mit ihrer EXIF-Ausrichtung. Seit P10a gibt es die Version im Einsatz mit Installation, Umwandeln und Sicherung (Abschnitt «Einsatz»), seit P10-vor Einstellungen des Kerns und «Leinwand als Bild speichern», seit P10 die Browserquelle in OBS mit Overlay-Server und seit P10b das Dock «Medien». Aktueller Stand: `docs/HANDOFF.md`. Technik: C++20 mit Qt 6 (siehe `docs/01-festlegungen.md`).

## Inhalt

| Ordner / Datei | Inhalt |
|---|---|
| `CLAUDE.md` | Regeln für Claude Code in diesem Projekt (zuerst lesen) |
| `docs/HANDOFF.md` | Übergabe: aktueller Stand, nächster Schritt, Arbeitsweise |
| `docs/01-festlegungen.md` | Alle bisherigen Entscheide, mit Stand und Herkunft |
| `docs/02-umgebung.md` | Zielsystem (Studio-PC) mit gemessenen Ist-Werten |
| `docs/03-funktionen-entwurf.md` | Funktionen je Modul (Editor priorisiert) |
| `docs/04-offene-punkte.md` | Offene Punkte und Reihenfolge der nächsten Schritte |
| `docs/05-quellen.md` | Quellen |
| `docs/07-bauplan.md` | Reihenfolge der Bausteine (Prompts) |
| `docs/06-overlay-format.md` | Format der Overlay-Datei und Aufbau des Datenordners (Vorlage für beide Zeichner) |
| `docs/08-kern-schnittstelle.md` | Schnittstelle Fenster–Kern (Unix-Socket, JSON pro Zeile), Kern als Benutzerdienst |
| `docs/berichte/` | Berichte der Bau-Prompts (ab P03) |
| `prompts/` | Aufträge an Claude Code, in Reihenfolge, je eine eigene Sitzung |
| `CMakeLists.txt` | Oberste Build-Datei (C++20, Qt 6), Version des Programms, Schalter |
| `src/common/` | Bibliothek `dss_common`: Logik und Daten für Kern und Fenster, nur Qt Core |
| `src/core/` | Kern `dss-core` (Hintergrundprogramm, ohne Fenster) |
| `src/render/` | Bibliothek `dss_render` (seit P10-vor): Zeichner der Quellen und Bild eines Overlays in seiner Auflösung, Qt Gui ohne Qt Widgets |
| `src/app/` | Fenster `dss-app`; Farben und Schriftgrösse nur in `theme.cpp`, Symbole als SVG in `src/app/symbole/` (zur Laufzeit geladen, ersetzbar, einfarbig in Textfarbe gezeichnet), Schrift Open Sans in `src/app/schriften/` |
| `src/cli/` | Werkzeug `dss-cli` ohne Fenster (Overlays anlegen und bearbeiten, Handtest) |
| `web/` | Zeichner der Browserquelle (`overlay.html`, `overlay.js`) |
| `tests/` | Tests mit Qt Test, ausgeführt über `ctest` |
| `scripts/` | Einrichtungsskript, Paketliste, Startprüfung, Einrichten des Benutzerdienstes, Dienstprüfung |
| `prototypes/` | Messprototypen aus P01 und P02, nicht Teil des Builds; `prototypes/messung/media_erzeugen.sh` und `prototypes/media/` liefern die Testdateien der Tests |

## Bauen und starten

Der Bau aus dem Repo ist die **Version in Arbeit** («DoNs Streaming Studio – Entwicklung»). Sie hat eigene Daten (`~/.local/share/dons-streaming-studio-entwicklung`), eine eigene Einheit (`dss-core-entwicklung.service`), eine eigene Verbindung und eigene Fenster-Einstellungen und läuft neben der installierten Version, ohne deren Daten zu berühren. Die Version für den Stream installiert man mit `scripts/installieren.sh` (Abschnitt «Einsatz»).

Auf dem Studio-PC (Debian 13, X11-Sitzung), im Ordner des Repos:

```
scripts/einrichten.sh                 # Pakete aus scripts/pakete.txt installieren und prüfen
cmake -S . -B build                   # einrichten (Schalter siehe unten)
cmake --build build -j8               # bauen
ctest --test-dir build                # Tests
scripts/dienst-einrichten.sh          # Kern als Benutzerdienst dss-core-entwicklung.service einrichten (startet nicht bei der Anmeldung)
build/src/app/dss-app                 # Fenster starten; startet den Dienst, wenn der Kern nicht läuft
systemctl --user stop dss-core-entwicklung.service    # Kern anhalten (zum Beispiel vor Änderungen mit dss-cli)
journalctl --user -u dss-core-entwicklung.service     # Protokoll des Kerns
scripts/dienst-einrichten.sh --entfernen  # Dienst anhalten und entfernen
build/src/core/dss-core               # Kern ohne Dienst starten, beenden mit Ctrl+C (SIGINT) oder SIGTERM
build/src/app/dss-app --bildschirmfoto bild.png   # Fenster 1280x800 als Bild speichern und beenden
build/src/app/dss-app --bildschirmfoto bild.png --fenstergroesse 1920x1080   # in anderer Grösse
build/src/app/dss-app --bildschirmfoto bild.png --zustaende-zeigen   # Verbindungsknöpfe in allen vier Zuständen
build/src/core/dss-core --version     # Version ausgeben (ebenso dss-app --version)
build/src/cli/dss-cli --help          # Overlays ohne Fenster bearbeiten (Befehle siehe Hilfe)
python3 scripts/startpruefung.py      # je 20 Starts von dss-core und dss-app prüfen
python3 scripts/dienstpruefung.py     # Dienst mit eigener Testeinheit und Testdaten prüfen
```

Optionen für Tests und Prüfungen (echte Daten bleiben unberührt): `dss-core --datenordner PFAD --verbindung PFAD --einstellungen PFAD`, `dss-app --verbindung PFAD --einheit NAME.service`, `dss-cli --datenordner PFAD`, `dss-cli --verbindung PFAD einstellungen`. Schliesst man das letzte Fenster normal, beendet sich der Kern; stürzt das Fenster ab, läuft er weiter. Solange der Kern läuft, zeigt `dss-cli` nur an (`overlay-liste`, `quelle-liste`) und bricht Änderungen ab. Eigene Symbole (zum Beispiel `ndi.svg`) legt man nach `~/.config/dons-streaming-studio/symbole/`; sie haben Vorrang vor `src/app/symbole/` und gelten für beide Versionen.

Schalter beim Einrichten (`cmake -S . -B build -D<Schalter>=OFF`):

| Schalter | Standard | Wirkung |
|---|---|---|
| `DSS_BUILD_APP` | `ON` | Fenster `dss-app` bauen; aus: nur Kern (und Tests) |
| `DSS_BUILD_TESTS` | `ON` | Tests bauen und bei `ctest` anmelden |
| `DSS_BUILD_CLI` | `ON` | Werkzeug `dss-cli` bauen |
| `DSS_APP_EDITING` | `ON` | Bearbeiten im Fenster (P08); aus: Fenster zeigt nur an wie in P07 |
| `DSS_MEDIA_SIZE` | `ON` | Kern und `dss-cli` lesen die Grösse neuer Bild- und Videoquellen aus der Datei (P09, FFmpeg); aus: Grösse des Overlays wie in P08 |
| `DSS_APP_RENDERER` | `ON` | Leinwand zeichnet die Quellen (P09); aus: leere Leinwand wie in P08 |
| `DSS_APP_CANVAS_EDIT` | `ON` | Quellen auf der Leinwand mit Maus und Pfeiltasten bearbeiten (P09); aus: Leinwand zeigt nur an |
| `DSS_APP_TRANSFORM` | `ON` | Befehle «Transformieren» im Menü «Bearbeiten» und per Rechtsklick (P09b); aus: kein Menü «Transformieren» |
| `DSS_APP_SETTINGS_DIALOG` | `ON` | Dialog «Werkzeuge → Einstellungen …» für Port und Adresse des Kerns (P10-vor); aus: Menü «Werkzeuge» ausgegraut, Einstellungen nur mit `dss-cli` |
| `DSS_APP_CANVAS_IMAGE` | `ON` | «Datei → Leinwand als Bild speichern» (Strg+Umschalt+B) als PNG in `~/Bilder/DoNs Streaming Studio/` (P10-vor, braucht `DSS_APP_RENDERER`); aus: kein Menüeintrag |
| `DSS_VIDEO_ROTATION` | `ON` | Drehung aus den Metadaten von Videos (Display-Matrix) in Kern und Zeichner (P10a); aus: Videos wie gespeichert |
| `DSS_CORE_IMPORT_THREAD` | `ON` | Kern übernimmt neue Dateien (SHA-256, Kopie) in eigenen Threads und antwortet dabei weiter (P10-vor); aus: in der Hauptschleife wie in P10a |
| `DSS_CORE_WATCHDOG` | `ON` | Kern meldet sich beim Watchdog von systemd (P10a); aus: ohne Meldung, dann `scripts/dienst-einrichten.sh --watchdog 0` |
| `DSS_CORE_OVERLAY_SERVER` | `ON` | Overlay-Server im Kern für die Browserquelle in OBS (P10, `docs/08-kern-schnittstelle.md`, «Overlay-Server»); aus: Kern ohne Server, das Fenster meldet «ohne Overlay-Server gebaut» |
| `DSS_APP_BROWSER_SOURCE` | `ON` | Dock «Steuerung» mit Adresse der Browserquelle, «Kopieren», «Neuer Token», «Ausgabe: …», Kästchen «Ausgabe ein/aus» in der Overlay-Liste und Meldungen des Servers (P10); aus: die Felder im Dock «Steuerung» bleiben ausgegraut wie in P10-vor, keine Kästchen, keine Meldungen des Servers |
| `DSS_APP_MEDIA_DOCK` | `ON` | Dock «Medien» (P10b, E-28): Mediendateien mit Vorschau, Namen, Ordnern, Hinzufügen, Löschen und Ziehen auf Leinwand und Quellen; aus: kein Dock, Kern und `dss-cli` führen die Medienliste trotzdem |
| `DSS_INSTALLED_EDITION` | `OFF` | Version im Einsatz bauen (nur `scripts/installieren.sh`, P10a): echte Daten, `dss-core.service`, Laufzeit-Dateien aus dem Installationsordner |

Das Fenster speichert Grösse, Lage und Anordnung der Docks beim Beenden in `~/.config/dons-streaming-studio-entwicklung/fenster.ini` (Version in Arbeit) bzw. `~/.config/dons-streaming-studio/fenster.ini` (Version im Einsatz, jeweils `$XDG_CONFIG_HOME/…`). Löschen der Datei stellt die Standardanordnung her. Das Dock «Medien» (seit P10b) steht in der Standardanordnung zwischen «Quellen» und «Eigenschaften»; mit einer `fenster.ini` von vorher kommt es als dritter Reiter neben «Overlays» und «Quellen». Lösen, verschieben und ein- oder ausblenden lässt es sich wie die anderen Docks (Menü «Docks»).

Namen und Ordner der Mediendateien stehen in `medien.json` im Datenordner (seit P10b, `docs/06-overlay-format.md`, «Medienliste»); nur der Kern schreibt sie. Anzeigen mit `dss-cli medien-liste` (geht immer), ändern im Dock «Medien» oder mit `dss-cli medium-umbenennen`, `medium-verschieben`, `medium-loeschen`, `medien-ordner-anlegen`, `medien-ordner-umbenennen`, `medien-ordner-verschieben`, `medien-ordner-loeschen` über den laufenden Kern (`dss-cli --help`).

Der Kern hält seine Einstellungen (Port des Overlay-Servers, Adresse im Heimnetz, seit P10-vor) in `kern.ini` im selben Ordner; nur der Kern schreibt sie. Anzeigen und ändern: im Fenster unter «Werkzeuge → Einstellungen …» oder mit `dss-cli einstellungen`, `dss-cli einstellung-port PORT`, `dss-cli einstellung-adresse ADRESSE` (Aufbau und gültige Werte: `docs/08-kern-schnittstelle.md`, «Einstellungen»).

## Einsatz

Die **Version im Einsatz** (für den Stream) liegt in `~/.local/opt/dons-streaming-studio/` und nutzt die echten Daten `~/.local/share/dons-streaming-studio`. Nur der Nutzer installiert sie, nach der Abnahme eines Bausteins.

**Installieren** (darf mehrfach laufen, das Fenster der installierten Version muss geschlossen sein):

```
cd ~/DoNs_Streaming_Studio && scripts/installieren.sh
```

Das Skript baut im Release-Modus in `build-einsatz/`, hält den Kern `dss-core.service` an (er startet mit dem nächsten Fenster wieder), wandelt die echten Dateien bei Bedarf in das neue Format um und installiert. Vor dem Umwandeln sichert es den ganzen Datenordner nach `~/.local/share/dons-streaming-studio-sicherungen/<JJJJ-MM-TT_hh-mm-ss>/` und nennt den Ort. Scheitert das Umwandeln, bleiben die Dateien unverändert und es installiert nichts. Am Ende meldet es die Version, zum Beispiel «dss-app 0.1.0 (Einsatz, Commit 85ac702)».

**Starten:** Anwendungsmenü von Xfce → «DoNs Streaming Studio» (Eintrag `~/.local/share/applications/dons-streaming-studio.desktop`). Oder `~/.local/opt/dons-streaming-studio/bin/dss-app`. Das Fenster startet den Kern `dss-core.service`, wenn er nicht läuft. Protokoll: `journalctl --user -u dss-core.service`.

**Sicherung zurückspielen** (zum Beispiel nach einem Umwandeln, das nicht passt):

```
cd ~/DoNs_Streaming_Studio && systemctl --user stop dss-core.service
ls ~/.local/share/dons-streaming-studio-sicherungen/          # Sicherung wählen
mv ~/.local/share/dons-streaming-studio ~/.local/share/dons-streaming-studio-ersetzt
cp -a ~/.local/share/dons-streaming-studio-sicherungen/<JJJJ-MM-TT_hh-mm-ss> ~/.local/share/dons-streaming-studio
```

Danach eine Version starten, die dieses Format liest (die Version vor der Installation, zum Beispiel mit `git checkout <Commit>` und `scripts/installieren.sh`); eine neuere Version würde beim nächsten Installieren wieder umwandeln. Den Ordner `…-ersetzt` erst löschen, wenn alles stimmt.

Der Kern wird immer gebaut. Die Version steht nur in `CMakeLists.txt` (`project(… VERSION …)`).

Die Festlegungen wurden anfangs in einem Claude-Dokument gesammelt: https://claude.ai/artifact/HpSYSw4X1Gx5SUPZAAhW13. Es wird seit P06 nicht mehr nachgeführt; massgebend ist dieses Repo.
