# P09b – Nacharbeiten Editor

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md` Abschnitt «Zeichnen im Editor», `docs/03-funktionen-entwurf.md` E-25 und E-26, `docs/04-offene-punkte.md` Abschnitte «Wünsche aus dem Handtest P09» und «Entschieden nach P09», `docs/06-overlay-format.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`) und den Bericht `docs/berichte/P09-bericht.md`.

## Ausgangslage

P09 hat den Zeichner des Editors und das Bedienen mit der Maus gebaut und ist abgenommen. Aus dem Handtest und den offenen Punkten kommen sieben Nacharbeiten, die vor der Browserquelle (P10) erledigt werden, damit der zweite Zeichner in P10 gleich alles kann, was der Editor kann (K-07).

## Entscheide des Nutzers (2026-11-03)

1. **Einrasten beim Drehen** mit Umschalttaste in **1°-Schritten** (statt 15°). Ohne Umschalttaste Zehntelgrad, kein Einrasten.
2. **Leistungsanzeige** (CPU, RAM, LAN) im Kopfbereich: **feste Breite 600 Pixel** für alle drei zusammen (200 je Anzeige), rechts im Kopfbereich. Wäre das mehr als ein Drittel der Fensterbreite, wird sie auf ein Drittel verkleinert. Der Platz zwischen den Verbindungsknöpfen und der Anzeige bleibt leer.
3. **Seitenverhältnis an der Ecke** nach der **Originalgrösse der Datei**, wie OBS. Bei Text (keine Originalgrösse) nach der Box beim Beginn des Ziehens.
4. **Spiegeln:** Ziehen über die Gegenseite hinaus spiegelt die Quelle, wie OBS. Neue Felder «waagrecht gespiegelt» und «senkrecht gespiegelt» je Quelle in Modell, Datei, Schnittstelle und `dss-cli`. Breite und Höhe bleiben positiv.
5. **E-25 Transformieren wie OBS**, per Rechtsklick auf die Quelle (Leinwand und Dock «Quellen») und im Menü «Bearbeiten»:
   - Transformation kopieren / einfügen (Lage, Grösse, Drehung, Spiegeln; Einfügen auch in eine Quelle eines anderen Overlays)
   - Transformation zurücksetzen (Lage 0/0, Originalgrösse, Drehung 0°, nicht gespiegelt; bei Text bleibt die Grösse)
   - 90° im Uhrzeigersinn, 90° gegen den Uhrzeigersinn, 180° drehen
   - Waagrecht spiegeln, senkrecht spiegeln
   - Auf Leinwand einpassen (so gross wie möglich, Seitenverhältnis bleibt), auf Leinwand strecken (genau so gross wie die Leinwand)
   - Zentrieren, senkrecht zentrieren, waagrecht zentrieren
   - Zuschneiden ist **nicht** enthalten.
6. **E-26 Dock «Eigenschaften» in Bereiche geteilt:** oben **Name**; Bereich **«Transformation»** (X, Y, Breite, Höhe, Drehung, waagrecht und senkrecht gespiegelt); Bereich nach Art (**«Text»**, **«Video»**, **«Bild»**) mit den übrigen Eigenschaften. Kleine Titel, feine Trennlinien im Stil des Kopfbereichs.
7. **EXIF-Ausrichtung** von Bildern beachten, im Zeichner des Editors und im Kern (Originalgrösse), wie der Browser (Standard `image-orientation: from-image`).

## Auftrag

- Prüfe das Verhalten von OBS zu 3, 4 und 5 im Quellcode von OBS (Menü «Transform», Funktionen dazu) und nenne Datei und Zeilen im Bericht. Wie OBS ist das Ziel; wo OBS bei gedrehten Quellen anders rechnet als einfach (zum Beispiel Einpassen, Zentrieren, Zurücksetzen einer gedrehten Quelle), übernimm das Verhalten von OBS und beschreibe es im Bericht.
- Kopierte Transformation ist Zustand des Fensters (Zwischenablage des Fensters), nicht des Kerns. Einfügen und alle anderen Befehle schicken die Änderung wie bisher an den Kern.
- Ob `format_version` der Overlay-Datei und die Version der Schnittstelle steigen, entscheidest du wie in P09 und begründest es. Bestehende Dateien müssen weiter laden (ohne Spiegeln = nicht gespiegelt).
- Regel 9: Alle Rechnungen (Einrasten, Seitenverhältnis, Spiegeln beim Ziehen, alle Befehle aus 5, EXIF-Ausrichtung) in `dss_common`, ohne Qt Gui getestet.
- `docs/06-overlay-format.md` und `docs/08-kern-schnittstelle.md` nachführen. In `docs/01-festlegungen.md` (Abschnitt «Konzept der zwei Zeichner» oder «Zeichnen im Editor») festhalten, dass der zweite Zeichner (P10) Spiegeln und EXIF-Ausrichtung genauso umsetzen muss.

## Tests

- Ohne Qt Gui: alle Rechnungen aus 1, 3, 4, 5, 7, auch mit gedrehten und gespiegelten Quellen; neue Felder in Modell, Datei, Schnittstelle; alte Dateien laden.
- EXIF: Testbilder mit den Ausrichtungen 1 bis 8 selbst erzeugen (zum Beispiel mit `ffmpeg` oder einem kleinen Skript), Grösse und Zeichnung prüfen.
- Bildschirmfotos: Kopfbereich in 1920 und 1280 Pixel Fensterbreite (Leistungsanzeige), Dock «Eigenschaften» mit Text-, Video- und Bildquelle, Leinwand mit gespiegelten Quellen.
- Startprüfung `scripts/startpruefung.py --runs 20` weiter 20 von 20; `ctest` auch in einem frischen Klon.

## Nicht Teil dieses Auftrags

- Browserquelle und alles aus P10, Zuschneiden, Rückgängig (E-22), Einrasten an Kanten und Raster (E-10)
- Neue Pakete: voraussichtlich keine. Falls doch, nenne sie dem Nutzer mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar.
2. `ctest --test-dir build` besteht, auch in einem frischen Klon.
3. Startprüfung 20 von 20 je Fall sauber.
4. Dokumente nachgeführt.
5. Handtest durch den Nutzer (Regel 10): höchstens 4 Prüfungen mit eigenen Testdaten wie in P09 (`handtest.sh`), **jede Prüfung ein eigener Abschnitt**, Befehle beginnen mit `cd ~/DoNs_Streaming_Studio &&`. Das Handtest-Fenster soll die eigenen Symbole des Nutzers aus `~/.config/dons-streaming-studio/symbole/` anzeigen (nur lesen, nichts dorthin schreiben). Inhalt: Leistungsanzeige und Bereiche im Dock «Eigenschaften» ansehen; Drehen mit Umschalttaste und Spiegeln durch Ziehen; die Befehle aus E-25 an zwei Quellen und über zwei Overlays; Aufräumen.

## Bericht

Schreibe den Bericht nach `docs/berichte/P09b-bericht.md` nach dem Muster der bisherigen Berichte und Regel 15. Trage erledigte Punkte in `docs/04-offene-punkte.md` ein und setze P09b in `docs/07-bauplan.md` unter «Erledigt» («ausgeführt, Handtest offen»).

Ein Commit pro Entscheid oder sinnvoller Gruppe, dazu Tests und Bericht. Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt und nichts aus `~/.config` oder `~/.local/share` im Repo gelandet ist. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus (Regel 17).
