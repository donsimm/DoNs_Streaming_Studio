# P07 – Kern als Dienst, Verbindung Fenster–Kern

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md`, `docs/04-offene-punkte.md` Abschnitte «Entschieden vor P06» und «Entschieden nach P06», `docs/06-overlay-format.md`, `docs/07-bauplan.md`), die Berichte `docs/berichte/P04-bericht.md` bis `P06-bericht.md`, und sieh dir `docs/bilder/oberflaeche.png` an.

## Ausgangslage

Das Modell (P04, P05) und das Fenster mit Aussehen wie OBS (P05, P06) stehen. Der Kern `dss-core` startet bisher nur, schreibt eine Zeile und wartet. P07 macht ihn zum Dienst, der die Overlays hält, verbindet das Fenster mit ihm und zeigt echte Daten im Fenster an. Bearbeiten im Fenster kommt erst mit P08.

## Entscheide des Nutzers für diesen Baustein (2026-11-02)

1. Der Kern startet **automatisch bei der Anmeldung** des Nutzers und **startet nach einem Absturz von selbst neu**.
2. Solange der Kern läuft, ändert **nur der Kern** die Overlay-Dateien. `dss-cli` darf dann nur anzeigen; Änderungen mit `dss-cli` gehen nur bei angehaltenem Kern.
3. Die LAN-Anzeige zeigt **automatisch die Netzwerkverbindung, über die der Studio-PC ins Netz geht** (Standardroute).
4. Eigene Symbole des Nutzers liegen in `~/.config/dons-streaming-studio/symbole/` (siehe Teil A).
5. Punkt 33 wird hier behoben (siehe Teil B).

## Auftrag

### Teil A: Eigene Symbole ausserhalb des Repos

- Das Fenster sucht jede Symboldatei zuerst in `~/.config/dons-streaming-studio/symbole/` (gleicher Ordner wie `fenster.ini` aus P05, über dieselbe Funktion bestimmt) und nimmt nur, wenn dort keine Datei mit diesem Namen liegt, die Datei aus `src/app/symbole/`. Einfärbung wie in P06.
- Auf dem Studio-PC liegen dort schon `twitch.svg`, `youtube.svg`, `obs.svg`, `ndi.svg` des Nutzers. Verändere, kopiere oder committe diese Dateien **nicht**; nutze für Tests einen eigenen Ordner.
- Liegt ein eigenes `ndi.svg` vor, zeigt «Hilfe → Über» den Hinweis «NDI® is a registered trademark of Vizrt NDI AB» (offener Punkt aus «Entschieden vor P06»; prüfe den genauen Wortlaut auf der offiziellen NDI-Seite und nenne die Quelle).

### Teil B: Test unabhängig von erzeugten Testdateien (Punkt 33)

- `tst_overlaystore::formatDocExample` darf in einem frischen Klon nicht fehlschlagen, nur weil `prototypes/messung/media_erzeugen.sh` Dateien mit anderer Prüfsumme erzeugt. Wähle eine Lösung, begründe sie im Bericht und prüfe sie in einem frischen Klon in einem Ordner ausserhalb des Repos.

### Teil C: Kern als Dienst

- Benutzerdienst von systemd (`systemctl --user`), keine Systemdienste, kein `sudo`. Einheit startet mit der Anmeldung und nach Absturz neu (prüfe die passenden Einstellungen in der Dokumentation von systemd und nenne die Quelle). Protokoll über das Journal.
- `scripts/dienst-einrichten.sh` richtet den Dienst ein und startet ihn; mit `--entfernen` hält er ihn an und entfernt ihn. Mehrfach ausführbar ohne Schaden. Der Dienst startet das gebaute `dss-core` aus dem Repo-Ordner (Pfad im Skript ermittelt, nicht fest eingetragen).
- `dss-core` lädt beim Start alle Overlays aus dem Datenordner (Modell aus P04) und hält sie. Er ist der einzige, der schreibt (Entscheid 2). Er nimmt dazu eine Sperre im Datenordner (zum Beispiel `QLockFile`). `dss-cli` prüft die Sperre: Anzeige-Befehle gehen immer, Änderungs-Befehle brechen bei laufendem Kern mit einer klaren Meldung auf Deutsch ab (mit Hinweis, wie man den Kern anhält).
- `dss-core` bekommt die Optionen `--datenordner PFAD` und `--verbindung PFAD` (siehe Teil D), damit Tests nie die echten Daten berühren.

### Teil D: Verbindung Fenster–Kern

- Lokale Verbindung über einen Unix-Socket (zum Beispiel `QLocalServer`/`QLocalSocket`) im Laufzeitordner des Benutzers (`XDG_RUNTIME_DIR`), nur für den Benutzer zugänglich. Kein Netzwerk.
- Nachrichten als JSON, eine pro Zeile, mit Versionsnummer der Schnittstelle in der Begrüssung. Beschreibe die Schnittstelle vollständig in `docs/08-kern-schnittstelle.md`: alle Nachrichten mit Feldern, Ablauf, Fehlerfälle, Beispiele.
- Inhalt in P07 (nur lesen):
  - Begrüssung mit Name, Version des Kerns, Version der Schnittstelle.
  - Liste der Overlays (Kennung, Name, Adresse, Auflösung, Anzahl Quellen) und ein ganzes Overlay auf Anfrage.
  - Bei jeder Änderung schickt der Kern das ganze geänderte Overlay an alle verbundenen Fenster (K-02). In P07 ändert sich nur etwas, wenn der Kern neu lädt; die Nachricht muss trotzdem schon da und getestet sein.
  - Leistungswerte einmal pro Sekunde (Teil E).
  - Zustand der vier Verbindungen (Twitch, YouTube, OBS, NDI). In P07 immer «nicht eingerichtet» (grau).
- **Der Token eines Overlays wird in P07 nicht übertragen** (kommt mit P10, Regel 12).
- Mehrere Fenster gleichzeitig dürfen verbunden sein.

### Teil E: Leistungswerte im Kern

- CPU-Auslastung gesamt in Prozent, RAM belegt und gesamt, LAN empfangen und gesendet in kbit/s für die Netzwerkkarte der Standardroute (Entscheid 3). Quellen unter Linux zum Beispiel `/proc/stat`, `/proc/meminfo`, `/proc/net/route`, `/proc/net/dev`; prüfe Felder und Bedeutung in proc(5) und nenne die Quelle.
- Gibt es keine Standardroute (zum Beispiel Kabel gezogen), zeigt die Anzeige «–» statt eines falschen Werts.

### Teil F: Fenster zeigt die Daten

- Das Fenster verbindet sich beim Start mit dem Kern und bei Verlust alle paar Sekunden neu. Es startet auch ohne laufenden Kern.
- Statuszeile: «Kern-Dienst: verbunden» grün bzw. «Kern-Dienst: nicht verbunden» rot (Farben aus dem Design von P06).
- Kopfbereich: CPU, RAM, LAN mit Wert und Verlauf der letzten 60 Sekunden wie auf `docs/bilder/oberflaeche.png`. Verbindungsknöpfe mit dem Zustand vom Kern.
- Dock «Overlays»: Liste der Overlays. Auswahl eines Overlays zeigt seine Quellen im Dock «Quellen» (mit Augen-Symbol für sichtbar/ausgeblendet wie auf dem Bild) und setzt die Leinwand auf seine Auflösung (Lineale, Einpassen). Quellen werden noch **nicht** gezeichnet (P09).
- Dock «Eigenschaften»: zeigt Name, X, Y, Breite, Höhe, Drehung der gewählten Quelle, ausgegraut (Bearbeiten kommt mit P08).
- Dock «Steuerung»: unverändert ausgegraut.
- Ohne Verbindung zum Kern sind die Docks leer.
- Regel 9 gilt: Die Auswahl ist Zustand des Fensters; alles andere kommt vom Kern. Umwandeln der Nachrichten in Daten gehört in `dss_common` und wird dort ohne Qt Gui getestet.

## Tests

- Ohne Qt Gui: Lesen und Schreiben aller Nachrichten der Schnittstelle, Leistungswerte aus Beispieldateien im Format von `/proc` (feste Testdaten), Sperre (zweiter Kern startet nicht, `dss-cli` ändert nicht bei laufendem Kern), Symbolsuche mit eigenem Ordner.
- Zusammenspiel: Kern mit eigenem Datenordner und eigener Verbindung starten, Testverbindung anmelden, Liste und Overlay lesen, Leistungswerte empfangen, Kern beenden und neu starten, Verbindung wieder aufbauen.
- Startprüfung `scripts/startpruefung.py --runs 20` weiter 20 von 20 in allen drei Fällen (mit eigenem Datenordner und eigener Verbindung für `dss-core`).
- Dienst: einrichten, Neustart nach `kill -9` des Kerns prüfen (Zeit bis zum Neustart messen), entfernen. Danach den Dienst für den Nutzer **eingerichtet und laufend** lassen.

## Nicht Teil dieses Auftrags

- Bearbeiten im Fenster, Rückfrage beim Umbenennen (P08)
- Zeichnen der Quellen (P09), Webserver und Browserquelle, Token übertragen (P10)
- Echte Verbindungen zu Twitch, YouTube, OBS, NDI
- Neue Pakete: voraussichtlich keine nötig. Falls doch, nenne sie dem Nutzer mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar.
2. `ctest --test-dir build` besteht, auch in einem frischen Klon (Teil B).
3. Startprüfung 20 von 20 je Fall sauber.
4. Dienst startet nach Absturz neu (gemessene Zeit im Bericht) und läuft am Ende für den Nutzer.
5. `docs/08-kern-schnittstelle.md` vollständig.
6. Handtest durch den Nutzer (Regel 10): höchstens 5 Schritte mit fertigen Befehlen, darunter: nach Neuanmeldung läuft der Kern; ein Overlay mit `dss-cli` bei angehaltenem Kern anlegen, Kern starten, im Fenster ansehen (Docks, Leinwand-Auflösung); Kern beenden, Statuszeile wird rot und nach dem Neustart wieder grün; Werte CPU, RAM, LAN plausibel; eigene Symbole erscheinen. Nach dem Handtest entfernt ein letzter Schritt das Test-Overlay wieder (Kern anhalten, löschen, Kern starten).

## Bericht

Schreibe den Bericht nach `docs/berichte/P07-bericht.md` nach dem Muster der bisherigen Berichte und Regel 15. Trage erledigte Punkte in `docs/04-offene-punkte.md` ein und setze P07 in `docs/07-bauplan.md` unter «Erledigt» («ausgeführt, Handtest offen»). Ergänze `docs/08-kern-schnittstelle.md` in der Tabelle im `README.md`.

Ein Commit pro Teil (A bis F, Tests, Bericht). Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt und keine Dateien aus `~/.config` im Repo gelandet sind. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus (Regel 17).
