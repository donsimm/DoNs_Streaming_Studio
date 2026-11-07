# P10a – Nacharbeiten Editor und Einsatz

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md` Abschnitte «Kern als Dienst», «Zeichnen im Editor», «Einsatz (Version im Stream)», `docs/04-offene-punkte.md` Abschnitte «Wünsche aus dem Handtest P09b» und «Entschieden nach P09b», Punkte 41, 42, 56 und 66, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`) und die Berichte `docs/berichte/P07-bericht.md`, `P09-bericht.md`, `P09b-bericht.md`.

## Ausgangslage

Der Editor ist bis P09b abgenommen. Vor der Browserquelle (P10) kommen zwei Teile: kleine Nacharbeiten am Editor (Teil A) und die Trennung zwischen der Version **in Arbeit** (Repo) und der Version **im Einsatz** (installiert, später im Stream) (Teil B).

## Teil A: Nacharbeiten Editor (Entscheide des Nutzers 2026-11-03)

1. **Verbindungsknöpfe** waagrecht in der Mitte des Kopfbereichs, mittig zwischen den Reitern «Overlays / Chat-Bot» und der Leistungsanzeige. **Logos** darin etwa zwei Drittel der heutigen Grösse, ohne eigenen Rand ums Logo; der Rahmen des Knopfes bleibt die einzige Umrandung. Nenne die alte und die neue Grösse in Pixeln.
2. **Knöpfe im Bereich «Transformation»** des Docks «Eigenschaften»: drei kleine Symbolknöpfe in der Titelzeile, am **rechten Rand** des Docks: Kopieren, Einfügen, Zurücksetzen. Hinweis beim Darüberfahren; «Einfügen» ausgegraut, solange nichts kopiert ist; Wirkung wie die Befehle aus E-25. Symbole als SVG unter `src/app/symbole/`, selbst gezeichnet, einfarbig wie die übrigen.
3. **Tastenkürzel genau wie OBS** (Punkt 60): Strg+Umschalt+C / Strg+Umschalt+V (Transformation kopieren / einfügen), Strg+R (zurücksetzen), Strg+F (auf Leinwand einpassen), Strg+S (auf Leinwand strecken), Strg+D (zentrieren). Im Menü neben den Einträgen anzeigen. Sie wirken auf die gewählte Quelle, wenn der Fokus auf der Leinwand oder im Dock «Quellen» liegt; in Textfeldern nicht.
4. **Ruckeln bei Videos klären (Punkt 66):** Zuerst messen, ob bei Videos mit Zeitstempeln in ganzen Millisekunden (zum Beispiel `alpha.webm`) Bilder ungleichmässig kommen (je Takt: neues Bild, kein neues Bild, mehrere). Ergebnis mit Zahlen im Bericht. Dann den Zeitpunkt mit einer kleinen Toleranz rechnen (Vorschlag 1 ms, Wahl begründen) und erneut messen. `tst_medien` muss danach **50 Mal** hintereinander bestehen (`ctest --repeat until-fail:50 -R tst_medien`), ebenso `tst_fensterbearbeiten`.
5. **Drehung aus den Metadaten von Videos (Punkt 65):** Der Editor beachtet die Drehung eines Videos aus seinen Metadaten (zum Beispiel Display-Matrix in MP4, über FFmpeg), wie die EXIF-Ausrichtung bei Bildern. Auch die Originalgrösse einer neuen Videoquelle folgt daraus. Testvideos mit 90°, 180°, 270° selbst erzeugen. In `docs/01-festlegungen.md` festhalten, dass auch der zweite Zeichner (P10) das umsetzen muss und P10 mit OBS prüft, ob die Browserquelle es tut.

## Teil B: Einsatz (Entscheide des Nutzers 2026-11-03)

1. **Installieren nur durch den Nutzer:** `scripts/installieren.sh` baut im Release-Modus in einen eigenen Build-Ordner und installiert nach `~/.local/opt/dons-streaming-studio/`. **Claude Code führt das Skript nie gegen diesen Ort aus**; zum Prüfen nur mit einem anderen Zielordner (Option des Skripts) und eigenen Testdaten.
2. Das Skript ist mehrfach ausführbar, meldet die installierte Version (Version aus CMake und Commit) und hält den Kern im Einsatz vorher an und danach nicht wieder an (er startet mit dem nächsten Fenster). Läuft gerade ein Fenster der installierten Version, bricht es mit einer Meldung ab.
3. **Getrennte Daten:**
   - Installierte Version: echte Daten `~/.local/share/dons-streaming-studio`, Einheit `dss-core.service`, die bisherige Verbindung, Fenster-Einstellungen `~/.config/dons-streaming-studio/fenster.ini`.
   - Version aus dem Repo (`build/`): eigener Entwicklungsordner für Daten, eigene Einheit, eigene Verbindung und eigene Fenster-Einstellungen (Namen und Orte wählst du, zum Beispiel mit Zusatz «-entwicklung»). Beide Versionen laufen gleichzeitig ohne sich zu stören. Woran ein Programm erkennt, welche Version es ist, legst du fest und begründest es (zum Beispiel Schalter beim Bau, nicht der Ort zur Laufzeit).
   - Die **eigenen Symbole** in `~/.config/dons-streaming-studio/symbole/` nutzen beide Versionen (nur lesen).
   - Das Fenster zeigt deutlich, welche Version läuft (zum Beispiel Fenstertitel «DoNs Streaming Studio – Entwicklung» für die Version aus dem Repo).
4. **Umwandeln beim Installieren:** Liest die neue Version ein neueres Format als die echten Dateien haben, legt das Skript zuerst eine Sicherungskopie des ganzen Datenordners an (Ort und Name mit Datum und Uhrzeit nennen) und lässt dann die neue Version die Dateien umwandeln. Schlägt das fehl, bleiben die Dateien unverändert, und das Skript nennt die Sicherung.
5. **Menüeintrag** «DoNs Streaming Studio» im Anwendungsmenü von Xfce für die installierte Version (Desktop-Datei nach der freedesktop-Spezifikation, Quelle nennen), mit dem **Programm-Symbol des Nutzers**: Datei `assets/branding/logo-fernseher-2400x1800.png` aus dem alten Repo https://github.com/donsimm/donstv_Studio (Commit `f95ba54`, laut README dort eigenes Werk des Nutzers). Lege es unter `assets/branding/` in dieses Repo, schneide es auf ein Quadrat um den sichtbaren Fernseher zu (Ränder durchsichtig) und erzeuge die üblichen Grössen (16 bis 512 Pixel). Dasselbe Symbol als Fenstersymbol beider Versionen.
6. **Überwachung eines hängenden Kerns (Punkt 41):** Der Kern meldet sich regelmässig bei systemd (Watchdog nach sd_notify(3), Einstellung in der Einheit); hängt er, startet systemd ihn neu. Zeit wählen und begründen; prüfen mit einem absichtlich angehaltenen Kern (zum Beispiel `kill -STOP`).
7. `scripts/dienst-einrichten.sh` aus P07 passt du an beide Einheiten an oder ersetzt es; die Einheit der installierten Version startet das Programm aus `~/.local/opt/…`, nicht aus dem Repo.
8. `README.md`: Abschnitt «Einsatz» mit Installieren, Starten, Sicherung zurückspielen.

## Tests

- Ohne Qt Gui: Toleranz der Zeitpunkte (Punkt 66), Drehung aus Video-Metadaten, Unterscheidung der Versionen (Orte, Namen), Umwandeln mit Sicherung (Testdaten im Format 1 bis 3).
- Installieren in einen **Testordner** mit Testdaten: zweimal hintereinander, Umwandeln mit Sicherung, Abbruch bei laufendem Fenster.
- Beide Versionen gleichzeitig: Kern und Fenster der Testinstallation und der Version aus dem Repo laufen nebeneinander, jede sieht nur ihre Daten.
- Watchdog: angehaltener Kern wird neu gestartet (Zeit messen).
- Startprüfung `scripts/startpruefung.py --runs 20` weiter 20 von 20; `ctest` auch in einem frischen Klon.

## Nicht Teil dieses Auftrags

- Browserquelle, Webserver, zweiter Zeichner, Schalter «Ausgabe ein/aus» (P10)
- Installation in den echten Ort `~/.local/opt/dons-streaming-studio/` (macht der Nutzer)
- Neue Pakete: voraussichtlich keine. Falls doch, nenne sie dem Nutzer mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar; Release-Bau ohne Warnungen.
2. `ctest --test-dir build` besteht, auch in einem frischen Klon; `tst_medien` und `tst_fensterbearbeiten` je 50 Mal.
3. Startprüfung 20 von 20 je Fall sauber.
4. Dokumente nachgeführt.
5. Handtest durch den Nutzer (Regel 10): höchstens 5 Prüfungen, **jede ein eigener Abschnitt**, Befehle beginnen mit `cd ~/DoNs_Streaming_Studio &&`. Inhalt: Kopfbereich und Knöpfe bei «Transformation» ansehen und bedienen, Tastenkürzel; **erste echte Installation** mit `scripts/installieren.sh` (das ist die Prüfung, die nur der Nutzer machen darf), Start über das Anwendungsmenü mit Symbol, die bisherigen echten Overlays sind da; Version aus dem Repo gleichzeitig starten, sie zeigt «Entwicklung» und andere Daten; Aufräumen der Entwicklungsdaten. Die Sicherungskopie aus der ersten Installation bleibt liegen.

## Bericht

Schreibe den Bericht nach `docs/berichte/P10a-bericht.md` nach dem Muster der bisherigen Berichte und Regel 15. Trage erledigte Punkte in `docs/04-offene-punkte.md` ein und setze P10a in `docs/07-bauplan.md` unter «Erledigt» («ausgeführt, Handtest offen»). Ergänze `CLAUDE.md` um eine Regel: Claude Code installiert nie in `~/.local/opt/dons-streaming-studio/` und verändert die echten Daten `~/.local/share/dons-streaming-studio` nicht; es arbeitet mit der Version aus dem Repo und deren Entwicklungsordner.

Ein Commit pro Teil oder sinnvoller Gruppe, dazu Tests und Bericht. Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt und nichts aus `~/.config`, `~/.local/share` oder `~/.local/opt` im Repo gelandet ist. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus (Regel 17).
