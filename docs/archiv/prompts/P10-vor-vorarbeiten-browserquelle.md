# P10-vor – Vorarbeiten für die Browserquelle

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md` Abschnitte «Kern als Dienst» und «Einsatz (Version im Stream)», `docs/02-umgebung.md` Abschnitt «Netzwerk», `docs/04-offene-punkte.md` Punkte 67 bis 72 und Abschnitte «Entschieden nach P10a» und «Entschieden für P10», `docs/06-overlay-format.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`) und die Berichte `docs/berichte/P09b-bericht.md` und `docs/berichte/P10a-bericht.md`.

## Ausgangslage

P10a ist abgenommen (2026-11-03), einschliesslich der ersten echten Installation durch den Nutzer. Seit dem 2026-11-03 besteht das Netzwerk zum Streaming-PC (`docs/02-umgebung.md`, «Netzwerk»). Die Browserquelle (P10) wurde auf Entscheid des Nutzers in zwei Aufträge geteilt: zuerst diese Vorarbeiten, danach P10 mit Webserver und zweitem Zeichner. **In diesem Auftrag gibt es noch keinen Webserver.**

## Teil A: Kern (Entscheide des Nutzers 2026-11-03)

1. **Übernehmen von Dateien in einem eigenen Thread (Punkt 67):** Kopie und SHA-256 einer neuen Datei laufen nicht mehr in der Hauptschleife des Kerns. Während des Übernehmens beantwortet der Kern weiter alle Nachrichten, die Leistungswerte laufen weiter, und das Fenster bleibt bedienbar. Nur der Kern schreibt Dateien; Änderungen an Overlays und `liste.json` bleiben in der Hauptschleife. Regle und prüfe diese Fälle:
   - zwei Übernahmen gleichzeitig (auch dieselbe Datei zweimal);
   - das Overlay oder die Quelle wird während des Übernehmens gelöscht;
   - der Kern wird während des Übernehmens beendet (SIGTERM): sauberes Ende ohne Rest unter dem endgültigen Namen, Zeit bis zum Ende messen;
   - Watchdog (Punkt 41): Entscheide und begründe, ob die zusätzliche Meldung beim Übernehmen (aus P10a) bleibt oder entfällt, und prüfe mit einer Datei von 4 GiB, dass kein Neustart erfolgt.
   Wie das Fenster ein laufendes Übernehmen zeigt, ist nicht entschieden. Mindestens ein Hinweis in der Statusleiste; alles darüber hinaus als `[OFFEN]` in den Bericht.
2. **Aufräumen beim Start (Punkt 68):** Der Kern löscht beim Start, nachdem er die Sperre hält, die vorläufigen Dateien von `QSaveFile` in `media/` (Muster aus der Qt-Dokumentation belegen, Quelle nennen). Nur diese Dateien, nichts anderes. Protokollzeile mit Anzahl und Grösse.
3. **Einstellungen des Kerns:** Datei `kern.ini` im Ordner der Einstellungen der jeweiligen Version (`~/.config/dons-streaming-studio/` im Einsatz, `~/.config/dons-streaming-studio-entwicklung/` in Arbeit). Nur der Kern schreibt sie, atomar. Inhalt in diesem Auftrag:
   - **Port** des späteren Overlay-Servers: Standard **8080** im Einsatz, **8081** in Arbeit. Gültigen Bereich festlegen und begründen.
   - **Adresse im Heimnetz** (nur IPv4): Fehlt sie, füllt der Kern sie beim Start mit der IPv4-Adresse der Netzwerkkarte, über die die Standardroute läuft (auf dem Studio-PC heute `192.168.1.9` auf `enp3s0`), und schreibt sie fest. Danach ermittelt er sie nicht neu. Gibt es beim ersten Start keine Standardroute mit IPv4, bleibt der Wert leer, mit Protokollzeile. Wie der Kern die Standardroute liest, festlegen und Quelle nennen.
   - Neue Nachrichten der Schnittstelle zum Lesen und Ändern der Einstellungen; Version der Schnittstelle erhöhen; `docs/08-kern-schnittstelle.md` nachführen. Ungültige Werte weist der Kern mit Meldung ab.
   - `dss-cli`: Einstellungen anzeigen und ändern (für Tests und als Notweg).
4. Den Server selbst, die Meldung bei belegtem Port und das Warten auf eine fehlende Adresse baut P10, nicht dieser Auftrag.

## Teil B: Fenster (Entscheide des Nutzers 2026-11-03)

1. **Dialog «Werkzeuge → Einstellungen …»** mit Port und Adresse im Heimnetz. Er liest die Werte vom Kern und schickt Änderungen als Nachricht an den Kern (keine Logik im Fenster, Regel 9). Meldungen des Kerns (ungültiger Wert) erscheinen im Dialog. Hinweis im Dialog, dass Port und Adresse erst mit der Browserquelle (P10) wirken.
2. **Bildschirmfoto der Leinwand:** Menüeintrag «Datei → Leinwand als Bild speichern» mit Tastenkürzel. Wähle ein Kürzel, das mit keinem bestehenden Kürzel des Fensters und keinem Kürzel von Xfce auf dem Studio-PC zusammenstösst (prüfen und nennen; die Taste «Druck» belegt Xfce in der Regel selbst). Das Bild:
   - PNG in genau der Auflösung des gewählten Overlays, mit Durchsichtigkeit;
   - nur das Overlay, gezeichnet vom Zeichner des Editors, **ohne** Schachbrett, Auswahlrahmen, Lineale, Hilfslinien und Zoom; Videos mit dem Bild, das die Leinwand gerade zeigt;
   - Ablage in `~/Bilder/DoNs Streaming Studio/` (Ordner bei Bedarf anlegen; ob `~/Bilder` aus `xdg-user-dir PICTURES` kommt, prüfen und so umsetzen), Name `<adresse>_<JJJJ-MM-TT_hh-mm-ss>.png`;
   - Meldung in der Statusleiste mit dem Dateinamen; ohne gewähltes Overlay ausgegraut.
   Das Zeichnen ins Bild gehört in die Logik ohne Fenster, damit P11 sie später nutzen kann.
3. Neue grössere Teile per Schalter abschaltbar (Regel 8).

## Teil C: Einsatz und Tests (Entscheide des Nutzers 2026-11-03)

1. **Programmordner leeren (Punkt 69):** `scripts/installieren.sh` leert `<Ziel>/bin/` und `<Ziel>/share/dons-streaming-studio/` erst nach erfolgreichem Bau und Prüfen des Formats, direkt vor `cmake --install`. Die Daten und `version.txt` bleiben unberührt. Prüfen mit einer veralteten Datei im Testziel, die danach fehlen muss, und mit einem absichtlich scheiternden Bau, nach dem nichts gelöscht ist. **Nur mit den Testoptionen** (Regel 18).
2. **Zurückschreiben beim Umwandeln (Punkt 70):** `convertDataFormat` bekommt eine Testmöglichkeit, die nur im Test greift (zum Beispiel «scheitere beim Schreiben der n-ten Datei»). Sie darf im Programm keine Wirkung haben. Test: Nach dem Fehler sind alle Dateien Byte für Byte wie vorher.
3. **`tst_fensterbearbeiten` 50 Mal (Punkt 72):** Scheitert er wieder, suche die Ursache, bevor du den Auftrag abschliesst, und beschreibe sie mit Messwerten.
4. Tabelle der Orte je Version in `docs/01-festlegungen.md` («Einsatz») um `kern.ini` und Port ergänzen.

## Tests

- Ohne Qt Gui: `kern.ini` (Standardwerte je Version, ungültige Werte, atomares Schreiben, Adresse aus der Standardroute mit Testdaten), Aufräumen beim Start, Übernehmen im Thread (alle Fälle aus Teil A 1), Testmöglichkeit beim Umwandeln, Zeichnen des Bildschirmfotos (Grösse, Durchsichtigkeit, keine Hilfsmittel des Editors; Vergleich mit dem Zeichner; Testbilder aus `tests/daten/exif/` und `tests/daten/video-drehung/`).
- Mit Fenster: Dialog liest und ändert die Werte über den Kern; Kürzel und Menüeintrag des Bildschirmfotos; das Fenster bleibt während des Übernehmens einer Datei von 1 GiB bedienbar (Antwortzeit des Kerns messen).
- `tst_medien` und `tst_fensterbearbeiten` je 50 Mal.
- Startprüfung `scripts/startpruefung.py --runs 20` weiter 20 von 20; `ctest` auch in einem frischen Klon.

## Nicht Teil dieses Auftrags

- Webserver, zweiter Zeichner, Token in der Adresse, Schalter «Ausgabe ein/aus», Adresse kopieren, Meldung bei belegtem Port (P10)
- Dock «Medien» (P10b)
- Installation in den echten Ort `~/.local/opt/dons-streaming-studio/` (macht der Nutzer; nach diesem Auftrag **nicht** nötig, die nächste echte Installation folgt mit P10)
- Neue Pakete: voraussichtlich keine. Falls doch, nenne sie dem Nutzer mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar; Release-Bau ohne Warnungen.
2. `ctest --test-dir build` besteht, auch in einem frischen Klon; `tst_medien` und `tst_fensterbearbeiten` je 50 Mal.
3. Startprüfung 20 von 20 je Fall sauber.
4. Echte Daten unverändert (Prüfsummen vorher und nachher), nichts nach `~/.local/opt/dons-streaming-studio/` installiert (Regel 18).
5. Dokumente nachgeführt.
6. Handtest durch den Nutzer (Regel 10): höchstens 3 Prüfungen, **jede ein eigener Abschnitt**, Befehle beginnen mit `cd ~/DoNs_Streaming_Studio &&`, nur mit der Version aus dem Repo und dem Entwicklungsordner. Inhalt: Dialog «Einstellungen» ansehen und bedienen; Bildschirmfoto speichern und im Bildbetrachter mit der Leinwand vergleichen (Durchsichtigkeit, keine Hilfsmittel); eine grosse Datei hinzufügen und dabei das Fenster bedienen. Am Ende Aufräumen der Entwicklungsdaten.

## Bericht

Schreibe den Bericht nach `docs/berichte/P10-vor-bericht.md` nach dem Muster der bisherigen Berichte und Regel 15. Trage erledigte Punkte in `docs/04-offene-punkte.md` ein und setze P10-vor in `docs/07-bauplan.md` unter «Erledigt» («ausgeführt, Handtest offen»).

Ein Commit pro Funktion, dazu Tests und Bericht. Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt und nichts aus `~/.config`, `~/.local/share`, `~/.local/opt` oder `~/Bilder` im Repo gelandet ist. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus (Regel 17).
