# P09 – Zeichner im Editor

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md` mit den Abschnitten «Konzept der zwei Zeichner», «Overlay-Daten», «Zeichnen im Editor», `docs/03-funktionen-entwurf.md`, `docs/04-offene-punkte.md` Abschnitte «Entschieden im Handtest P08» und «Entschieden nach P08», `docs/06-overlay-format.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`), die Berichte `docs/messungen/P02-bericht.md` (Dekodierweg M1) und `docs/berichte/P08-bericht.md`, und sieh dir `docs/bilder/oberflaeche.png` an.

## Ausgangslage

Seit P08 lassen sich Overlays und Quellen im Fenster bearbeiten; die Leinwand zeichnet die Quellen aber noch nicht. P09 baut den Zeichner des Editors (der zweite Zeichner, die Webseite für die Browserquelle, kommt mit P10) und das Bedienen mit der Maus auf der Leinwand. Vorab kommen kleine Nachträge aus dem Handtest P08.

## Entscheide des Nutzers für diesen Baustein (2026-11-03)

1. **Kopfbereich fest** über die ganze Fensterbreite; links oder rechts angedockte Docks beginnen darunter (E-23).
2. **Blaue Fläche** links neben einem gewählten Eintrag im Ordner entfernen (Punkt 48). Dort kommt mit P10 der Schalter «Ausgabe ein/aus» (E-24); lass die Fläche frei.
3. **Texte von Qt deutsch ohne «ß»:** `qtbase_de.qm` beim Bau kopieren, «ß» durch «ss» ersetzen, laden (Punkt 44).
4. **Variable Schriften:** Der Zeichner setzt bei einer variablen Datei die gewählte Stärke (Achse `wght`) (Punkt 45).
5. **Auflösung eines Overlays** nachträglich änderbar im Dock «Steuerung», zwei Felder Breite und Höhe in Pixeln. Quellen behalten ihre Pixelwerte (Punkt 47).
6. **Bild und Video füllen die Box** der Quelle und werden dafür gestreckt. Neue Bild- und Videoquellen bekommen die **Originalgrösse der Datei**.
7. **Ziehen an einer Ecke:** Seitenverhältnis bleibt, mit Umschalttaste frei. **An einer Kante:** nur Breite oder Höhe. Wie OBS.
8. **Drehgriff** oberhalb der gewählten Quelle wie OBS; mit Umschalttaste Einrasten in 15°-Schritten. Drehung um die Mitte, positiv im Uhrzeigersinn.
9. **Ende eines Videos:** ohne Einstellung wird die Quelle leer (durchsichtig). Neues Kästchen **«Am Ende anhalten»**: letztes Bild bleibt stehen. Mit «Wiederholen» beginnt es von vorne, «Am Ende anhalten» ist dann ausgegraut. Neues Feld in Modell und Overlay-Datei.
10. **Im Editor läuft ein Video ab dem Wählen des Overlays.**

Prüfe das Verhalten von OBS zu 7 und 8 in der Dokumentation oder im Quellcode von OBS und nenne die Quelle. Weicht OBS ab, halte dich an die Entscheide und nenne die Abweichung.

## Auftrag

### Teil A: Nachträge

- Entscheide 1, 2 und 3 im Fenster.
- Entscheid 5: neue Änderungsart in der Schnittstelle für die Auflösung, Felder im Dock «Steuerung» (die übrigen Elemente dort bleiben ausgegraut bis P10).
- Entscheid 9: Feld im Modell, in `dss-cli` (Option und Spalte in `quelle-liste`), in der Schnittstelle und im Dock «Eigenschaften». Ob `format_version` steigt, entscheidest du nach den Daten auf dem Studio-PC (`~/.local/share/dons-streaming-studio`) und begründest es; alte Dateien müssen weiter laden.
- Entscheid 6, zweiter Teil: Beim Hinzufügen liest der Kern die Grösse der Datei (Bild: Pixel; Video: Grösse des ersten Bildes). Kann er sie nicht lesen, nimmt er die Grösse des Overlays und meldet es.

### Teil B: Zeichner im Editor

- Die Leinwand zeichnet alle sichtbaren Quellen des gewählten Overlays in Zeichenreihenfolge, mit Lage, Grösse und Drehung aus dem Modell, in Pixeln der Overlay-Auflösung und dann auf die Grösse der Leinwand verkleinert (Einpassen). Ausgeblendete Quellen werden nicht gezeichnet.
- **Bild:** PNG und JPEG, gestreckt auf die Box (Entscheid 6).
- **Text** (TX-01 bis TX-05): aus der Schriftdatei im Datenordner, mit Schnitt, Kursiv, Grösse in Pixeln, Farbe mit Deckkraft, Ausrichtung waagrecht. Zeilen nur an den Zeilenwechseln des Inhalts (automatischer Umbruch ist TX-11, SOLL, und rechnet laut K-04 später der Kern). Senkrecht oben (TX-06 ist SOLL). Was mit Text geschieht, der über die Box hinausgeht, entscheidest du als Annahme und markierst es `[OFFEN]`.
- **Video und WebM mit Alpha:** eigener Dekodierweg über libavcodec wie in P02 (M1), `libvpx-vp9` für VP9 mit Alpha, sonst der Standard-Decoder. Dekodieren in eigenen Threads, nie im Thread der Oberfläche. Gestreckt auf die Box, Wiederholen und «Am Ende anhalten» nach Entscheid 9, Start nach Entscheid 10. Kein Ton (E-20 kommt mit P12).
- Bildrate der Leinwand: fest 30 Bilder pro Sekunde, solange ein Video läuft; sonst nur neu zeichnen, wenn sich etwas ändert (E-13 mit einstellbarer Bildrate ist SOLL).
- Regel 9: Rechnungen (Lage, Drehung, Treffer der Maus, Griffe, Einrasten, Seitenverhältnis) gehören in `dss_common` und werden ohne Qt Gui getestet. Das Zeichnen selbst ist im Fenster.

### Teil C: Bedienen mit der Maus

- Klick wählt die oberste sichtbare Quelle unter dem Zeiger, auch bei gedrehten Quellen; die Wahl ist dieselbe wie im Dock «Quellen» (beide Richtungen). Klick ins Leere hebt die Wahl auf.
- Gewählte Quelle: Rahmen mit Griffen an Ecken und Kanten und Drehgriff, gedreht wie die Quelle, gut sichtbar auf dunklem und hellem Inhalt.
- Ziehen: verschieben, skalieren (Entscheid 7), drehen (Entscheid 8). Ergebnis immer in ganzen Pixeln (Regel 11) und Zehntelgrad. Mindestgrösse 1 Pixel.
- Pfeiltasten verschieben die gewählte Quelle um 1 Pixel, mit Umschalttaste um 10 Pixel (wie OBS; prüfen und Quelle nennen).
- Während des Ziehens zeigt die Leinwand die Änderung sofort; an den Kern geht sie zusammengefasst wie in P08 (spätestens beim Loslassen). Das Dock «Eigenschaften» zeigt die Werte mit.
- Zeiger zeigt die passende Form (verschieben, skalieren je Richtung, drehen).

## Tests

- Ohne Qt Gui: Umrechnung Overlay → Leinwand, Treffer bei gedrehten und überlappenden Quellen, Griffe, Skalieren mit und ohne Seitenverhältnis, Drehen mit und ohne Einrasten, ganze Pixel, neues Feld «Am Ende anhalten» in Modell, Datei und Schnittstelle, Grösse einer neuen Bild- und Videoquelle.
- Zeichnen: Bildschirmfotos der Leinwand mit einem Test-Overlay (Bild, gedrehtes Bild, Text mit zwei Zeilen in einer Systemschrift, WebM mit Alpha über einem Bild, ausgeblendete Quelle). Prüfe den Alpha des Videos mit einer Zahl wie in P02.
- Video: Ende ohne Einstellung, mit «Am Ende anhalten», mit «Wiederholen».
- Leistung wie T-02 in P01/P02: Overlay 1920x1080 mit 10 Quellen, davon 2 Videos (eines WebM mit Alpha), 60 Sekunden, CPU, RAM, erreichte Bildrate, Einbrüche. Befehl und Werte im Bericht. Ziel: 30 Bilder pro Sekunde ohne Einbrüche über 50 ms.
- Startprüfung `scripts/startpruefung.py --runs 20` weiter 20 von 20 in allen Fällen.

## Nicht Teil dieses Auftrags

- Browserquelle, Webserver, zweiter Zeichner, Schalter «Ausgabe ein/aus» (P10)
- Bildvergleich der zwei Zeichner (P11), Ton (P12)
- Einrasten an Kanten, Raster, Hilfslinien, Ausrichten (E-10), Vorschau mit einstellbarer Bildrate (E-13), GIF (E-06), Animationen (E-14)
- Neue Pakete: voraussichtlich keine nötig (FFmpeg-Pakete seit P02). Falls doch, nenne sie dem Nutzer mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar.
2. `ctest --test-dir build` besteht, auch in einem frischen Klon.
3. Startprüfung 20 von 20 je Fall sauber.
4. Leistungsmessung im Bericht, Ziel erreicht oder Abweichung begründet.
5. `docs/06-overlay-format.md` und `docs/08-kern-schnittstelle.md` nachgeführt.
6. Handtest durch den Nutzer (Regel 10, Aussehen und Bedienung). Höchstens 5 Prüfungen; **jede Prüfung ist ein eigener, abgeschlossener Abschnitt** mit fertigen Befehlen, die mit `cd ~/DoNs_Streaming_Studio &&` beginnen. Keine Prüfung enthält das Schliessen des Fensters zusammen mit einer anderen Kontrolle. Inhalt: Kopfbereich fest beim Andocken links; Bild, Text und WebM mit Alpha erscheinen richtig; Quelle mit der Maus wählen, verschieben, an Ecke und Kante skalieren (mit und ohne Umschalttaste), drehen (mit und ohne Umschalttaste); Video mit «Wiederholen» und «Am Ende anhalten»; Auflösung im Dock «Steuerung» ändern. Am Ende eine eigene Prüfung zum Aufräumen.

## Bericht

Schreibe den Bericht nach `docs/berichte/P09-bericht.md` nach dem Muster der bisherigen Berichte und Regel 15. Trage erledigte Punkte in `docs/04-offene-punkte.md` ein und setze P09 in `docs/07-bauplan.md` unter «Erledigt» («ausgeführt, Handtest offen»).

Ein Commit pro Teil (A, B, C, Tests, Leistungsmessung, Bericht). Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt und nichts aus `~/.config` oder `~/.local/share` im Repo gelandet ist. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus (Regel 17).
