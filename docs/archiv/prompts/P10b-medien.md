# P10b – Dock «Medien»

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/03-funktionen-entwurf.md` E-28, `docs/04-offene-punkte.md` Abschnitt «Wünsche nach P10a» und Punkte 73, 74, 81, 83, 85, `docs/06-overlay-format.md` Abschnitt «media/», `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`) und die Berichte `docs/berichte/P10-vor-bericht.md` und `docs/berichte/P10-bericht.md`.

## Ausgangslage

P10 ist abgenommen (2026-11-04): Die Browserquelle läuft in OBS auf dem Streaming-PC. Die Mediendateien liegen in `media/` im Datenordner und heissen nach dem SHA-256 ihres Inhalts; der ursprüngliche Dateiname ist bisher nirgends gespeichert. Jetzt kommt das Dock «Medien» (E-28, MUSS), dazu kleine Nacharbeiten aus P10-vor und P10.

## Teil A: Kern (Entscheide des Nutzers 2026-11-03 und 2026-11-04)

1. **Namen und Ordner:** neue Datei im Datenordner (zum Beispiel `medien.json`, Name und Aufbau wählen und in `docs/06-overlay-format.md` beschreiben). Nur der Kern schreibt sie, atomar.
   - Beim Übernehmen hält der Kern den **ursprünglichen Dateinamen** fest. Wird dieselbe Datei nochmals hinzugefügt, bleibt der bisherige Name.
   - **Bestehende Dateien ohne Namen** bekommen einen Ersatznamen aus Typ und Datum (zum Beispiel «Bild vom 2026-11-02»). Woher das Datum kommt, wählen und begründen.
   - **Umbenennen** ändert nur den Namen, nicht die Datei und nicht die Verweise der Overlays.
   - **Ordner** sind beliebig tief verschachtelt und nur eine Einteilung; die Dateien bleiben in `media/`, kein Verweis ändert sich. Ordner anlegen, umbenennen, löschen (Inhalt dabei wählen und begründen: zum Beispiel nur leere Ordner löschbar, oder Inhalt eine Ebene höher), Dateien und Ordner verschieben.
   - Liest der Kern eine Datei in `media/`, die in der neuen Datei fehlt (zum Beispiel mit `dss-cli` angelegt), nimmt er sie mit Ersatznamen auf; Einträge zu fehlenden Dateien fallen weg. Ungültige Datei: wie bei `liste.json` (`docs/06-overlay-format.md`).
   - Legt der Kern die neue Datei bei bestehenden Daten zum ersten Mal an, gilt das als Umwandeln im Sinne von P10a: `scripts/installieren.sh` sichert vorher den Datenordner (`dataformat`, `--format-pruefen`).
2. **Löschen:** nur Dateien, die keine Quelle nutzt. Der Kern kennt für jede Datei die Overlays und Quellen, die sie nutzen («verwendet in …»). Eine genutzte Datei weist er beim Löschen ab.
3. **Übernehmen mit Fortschritt (Punkt 73):** Eine Datei in Übernahme erscheint in der Liste mit Fortschritt in Prozent; der Nutzer kann sie abbrechen (nichts bleibt in `media/`). Hinzufügen ohne Quelle (Teil B 4) nutzt denselben Thread wie bisher.
4. **Videoformate (Punkt 81):** Der Kern erkennt beim Übernehmen das Format. Kann die Browserquelle es nicht abspielen (nach dem Bericht P10: HEVC, MPEG-4 Part 2, ProRes; Liste an einer Stelle im Code, Quelle nennen), meldet er das, zum Beispiel «zaehler.mov (ProRes) wird in der Browserquelle nicht angezeigt. In H.264 oder VP9 umwandeln.» Kein automatisches Umwandeln.
5. **Adresse im Heimnetz (Punkt 85):** Ist sie in `kern.ini` leer, prüft der Kern alle 2 s die Standardroute und trägt die Adresse ohne Neustart ein, sobald sie da ist; der Server hört dann auch dort zu.
6. Nachrichten der Schnittstelle für alles oben; Version erhöhen; `docs/08-kern-schnittstelle.md` nachführen. `dss-cli`: Medien anzeigen (mit Name, Ordner, Typ, Grösse, «verwendet in»), umbenennen, Ordner anlegen, verschieben, löschen.

## Teil B: Dock «Medien» im Fenster (Entscheide des Nutzers)

1. **Ansicht:** Liste mit Spalten wie im Dateimanager: kleines Vorschaubild, Name, Typ, Grösse; Ordner auf- und zuklappbar; Sortieren per Klick auf den Spaltenkopf. Vorschaubild für Bilder, Videos (ein Bild daraus, wählen und begründen) und Schriften (zum Beispiel «Aa» in der Schrift). Dateien, die die Browserquelle nicht abspielt (Teil A 4), bekommen ein Warnzeichen mit Hinweis beim Darüberfahren. Ungenutzte Dateien sind erkennbar (auch Kopien aus Punkt 74).
2. **Lage:** Gibt es eine gespeicherte Anordnung (`fenster.ini`), erscheint «Medien» als dritter Reiter neben «Overlays» und «Quellen». Ohne gespeicherte Anordnung: eigenes Dock in der unteren Reihe zwischen «Quellen» und «Eigenschaften». Lösbar, verschiebbar, über das Menü «Docks» ein- und ausblendbar wie die anderen Docks.
3. **Umbenennen** (zum Beispiel Doppelklick oder F2, wie in der Overlay-Liste), **Ordner anlegen**, **Ziehen** von Dateien und Ordnern zwischen Ordnern.
4. **Hinzufügen:** Knopf «Hinzufügen» unten im Dock, Dateiauswahl für mehrere Dateien; die Dateien kommen in den gewählten Ordner, ohne dass eine Quelle entsteht.
5. **Löschen:** Knopf unten im Dock; bei genutzten Dateien ausgegraut, mit «verwendet in …» (Overlays, beim Darüberfahren oder in einer Zeile unter der Liste).
6. **Ziehen ins Overlay:** Datei aus dem Dock auf die Leinwand ziehen → neue Quelle an der Stelle der Maus in Originalgrösse; in die Liste des Docks «Quellen» → neue Quelle an der abgelegten Stelle der Reihenfolge, in der Mitte der Leinwand. Es wird nichts kopiert. Ziehen aus dem Dateimanager: nicht Teil des Auftrags.
7. Schalter beim Bau für das Dock.

## Teil C: Tests (Punkt 83)

- Alle Testkerne, auch `tst_kernverbindung::settingsResetInvalid`, `tst_fensterbearbeiten::settingsDialogReset` und `scripts/startpruefung.py`, nutzen eigene Ports, unabhängig davon, ob `dss-core-entwicklung.service` läuft. Prüfen mit laufendem Dienst.
- Ohne Fenster: neue Datei der Namen und Ordner (lesen, schreiben, ungültig, fehlende und neue Dateien, Ersatznamen), «verwendet in», Löschen abgewiesen, Abbrechen beim Übernehmen, Erkennen der Videoformate (Testdateien mit `ffmpeg` erzeugen), Punkt 85 mit einer Liste der lokalen Adressen im Test, Umwandeln mit Sicherung durch `installieren.sh` (nur mit Testoptionen, Regel 18).
- Mit Fenster: Dock in beiden Lagen (mit und ohne gespeicherte Anordnung), Umbenennen, Ordner, Ziehen zwischen Ordnern, Ziehen auf die Leinwand und in die Liste «Quellen» (Lage und Grösse der neuen Quelle prüfen), Hinzufügen mehrerer Dateien, Fortschritt und Abbrechen, Löschen ausgegraut bei genutzter Datei.
- Mit einer **Kopie der echten Daten**: Ersatznamen für die bestehenden Dateien, nichts geht verloren.
- `tst_medien` und `tst_fensterbearbeiten` je 50 Mal; Startprüfung `--runs 20`; `ctest` auch in einem frischen Klon.

## Nicht Teil dieses Auftrags

- Ziehen aus dem Dateimanager, automatisches Umwandeln von Videos
- Anbindung an OBS (P10c), Bildvergleich (P11), Ton (P12)
- Installation in den echten Ort (macht der Nutzer im Handtest)
- Neue Pakete nur nach Rückfrage beim Nutzer, mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar; Release-Bau ohne Warnungen.
2. `ctest --test-dir build` besteht, auch in einem frischen Klon; `tst_medien` und `tst_fensterbearbeiten` je 50 Mal.
3. Startprüfung 20 von 20 je Fall sauber, auch mit laufendem `dss-core-entwicklung.service`.
4. Echte Daten und Installation unverändert (Prüfsummen vorher und nachher), nichts installiert (Regel 18).
5. Dokumente nachgeführt.
6. Handtest durch den Nutzer (Regel 10): höchstens 5 Prüfungen, **jede ein eigener Abschnitt**, Befehle beginnen mit `cd ~/DoNs_Streaming_Studio &&`. Inhalt: Dock ansehen und bedienen (Ansicht, Sortieren, Umbenennen, Ordner, Ziehen zwischen Ordnern); Hinzufügen mehrerer Dateien mit Fortschritt und Abbrechen, Hinweis bei einem Video, das die Browserquelle nicht abspielt; Ziehen auf die Leinwand und in die Liste «Quellen», Löschen einer ungenutzten und Versuch bei einer genutzten Datei; **echte Installation** mit `scripts/installieren.sh` (Sicherung, Ersatznamen für die bestehenden Dateien, Dock als Reiter neben «Overlays» und «Quellen»); Aufräumen der Entwicklungsdaten.

## Bericht

Schreibe den Bericht nach `docs/berichte/P10b-bericht.md` nach dem Muster der bisherigen Berichte und Regel 15. Trage erledigte Punkte in `docs/04-offene-punkte.md` ein (E-28, Punkte 73, 74, 81, 83, 85) und setze P10b in `docs/07-bauplan.md` unter «Erledigt» («ausgeführt, Handtest offen»).

Ein Commit pro Funktion (Regel 8), dazu Tests und Bericht. Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt und nichts aus `~/.config`, `~/.local/share`, `~/.local/opt` oder `~/Bilder` im Repo gelandet ist. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus (Regel 17).
