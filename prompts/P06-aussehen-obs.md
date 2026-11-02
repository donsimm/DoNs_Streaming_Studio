# P06 – Aussehen wie OBS Studio

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md`, Abschnitte «Aussehen des Fensters» und «Verbindungsanzeige im Kopfbereich», `docs/04-offene-punkte.md` Punkte 27 bis 32 und `docs/07-bauplan.md`), den Bericht `docs/berichte/P05-bericht.md` und sieh dir `docs/bilder/oberflaeche.png` und `docs/berichte/P05-bilder/fenster-1280x800.png` an.

## Ausgangslage

P05 hat das Fenster-Gerüst gebaut, mit eigenem dunklem Design, Systemschrift (DejaVu Sans) und Verbindungsknöpfen in zwei Farben. Der Nutzer will, dass das Fenster so ähnlich wie möglich wie OBS Studio aussieht (Schrift und Farben). Aufbau, Docks und Beschriftungen bleiben wie in P05. Nur das Fenster wird geändert; Modell, Kern und `dss-cli` bleiben unverändert.

## Auftrag

### 1. Design nach OBS «Yami»

- Vorlage ist das Standard-Design von OBS Studio, «Yami» (Variante «Default»), Datei `frontend/data/themes/Yami.obt` im Repo https://github.com/obsproject/obs-studio. Hole die Datei selbst (zum Beispiel flacher Klon nur dieses Ordners) in einen Ordner **ausserhalb** des Repos und nenne im Bericht den Commit, den du gelesen hast. Der Chat hat Commit `65c320b46e1af5c7a555161aea6182cdf7a8c524` gelesen; die Farbwerte stehen in `docs/01-festlegungen.md`.
- `Yami.obt` ist ein Qt-Stylesheet mit Variablen (`var(--…)`) und Berechnungen (`calc(…)`), die nur OBS auflöst. Übernimm daraus, was für unsere Bedienelemente nötig ist (Fenster, Menüs, Reiter, Knöpfe, Eingabe- und Zahlenfelder, Auswahllisten, Kontrollkästchen, Docks mit Titelleiste, Statuszeile, Bildlaufleisten), mit aufgelösten Werten. Die Werte stehen an einer Stelle (`src/app/theme.*` wie in P05), damit später ein helles Design (E-16) möglich ist.
- Übernommene Teile bekommen einen Kommentar mit Herkunft (Datei, Commit) und Lizenz (OBS Studio: GPL Version 2, Datei `COPYING`). Die Software bleibt privat (`docs/01-festlegungen.md`, Abschnitt «Ziel»).
- Leinwand-Umgebung und Lineale bekommen die Farben der OBS-Vorschau (`--bg_preview`).

### 2. Schrift Open Sans

- Open Sans als Schriftdatei im Repo unter `src/app/schriften/`, beim Start mit `QFontDatabase::addApplicationFont` geladen und als Schrift des Fensters gesetzt. Nicht auf eine installierte Schrift verlassen, kein Paket dafür installieren.
- Bezugsquelle: das offizielle Repo der Schrift bei Google Fonts. Prüfe die Lizenz an der Quelle (erwartet: SIL Open Font License) und lege die Lizenzdatei neben die Schriftdatei. Nenne Quelle (URL), Version und Lizenz im Bericht.
- Schriftgrösse wie in OBS: In `Yami.obt` ist `--font_base` = `1pt × obsFontScale`. Den Standardwert von `obsFontScale` setzt OBS im Quellcode, nicht in `Yami.obt` (vom Chat nicht geprüft). Suche ihn im Quellcode von OBS und nenne Datei und Zeile im Bericht.
- Prüfe danach, dass keine Beschriftung abgeschnitten wird (Bildschirmfoto in 1280x800 und in 1920x1080).

### 3. Pfeile der Zahlenfelder gut sichtbar (Punkt 30)

- Die Zahlenfelder in «Eigenschaften» und «Steuerung» haben schon Pfeilknöpfe, aber die Pfeile sind kaum zu sehen (Rückmeldung des Nutzers, Bildschirmfoto vom 2026-11-02). Sie sollen deutlich sichtbar sein, wie in OBS. Die Pfeile kommen als SVG aus `src/app/symbole/` (eigene einfache Pfeile oder die von OBS mit Herkunft und Lizenz wie oben).

### 4. Verbindungsknöpfe mit vier Zuständen (Punkt 27)

- Zustände und Farben nach `docs/01-festlegungen.md`, Abschnitt «Verbindungsanzeige im Kopfbereich»: grün = verbunden bzw. Ausgabe läuft, orange = Verbindungsversuch, rot = eingerichtet aber nicht verbunden, grau = nicht eingerichtet oder abgeschaltet. Farbwerte aus `Yami.obt`: `--green3` `#25A231`, `--yellow2` `#E5AF24`, `--red3` `#C01C37`, für grau ein Grauton aus `Yami.obt`.
- Der Zustand eines Knopfes ist eine Eigenschaft, die später der Kern setzt. In P06 gibt es noch keine Verbindungen, also stehen alle vier Knöpfe auf **grau**.
- Die Symbole (Kürzel «TW», «YT», «OBS», «NDI») sind einfarbig in Textfarbe; die Farbe des Zustands zeigt der Rahmen des Knopfes (Punkt 28: Symbole folgen so dem Design).
- Zur Prüfung: `dss-app --bildschirmfoto DATEI --zustaende-zeigen` setzt die vier Knöpfe auf die vier verschiedenen Zustände (Twitch grün, YouTube orange, OBS rot, NDI grau), nur für das Bildschirmfoto.

### 5. Bildschirmfotos

- `docs/berichte/P06-bilder/fenster-1280x800.png`, `fenster-1920x1080.png`, `zustaende.png`.
- Vergleiche selbst mit dem P05-Bildschirmfoto und nenne im Bericht, was sich geändert hat und ob etwas abgeschnitten oder verrutscht ist.

## Tests

- Startprüfung `scripts/startpruefung.py --runs 20` weiter 20 von 20 sauber in allen drei Fällen.
- Der Test für `--bildschirmfoto` aus P05 besteht weiter; ergänze ihn um `--zustaende-zeigen`.
- Ein Test prüft ohne Fenster, dass die Schriftdatei geladen werden kann und «Open Sans» als Familienname liefert.

## Nicht Teil dieses Auftrags

- Verbindung zum Kern, echte Zustände der Verbindungen, echte Werte für CPU, RAM, LAN
- Helles Design (E-16), Werte der ausgegrauten Elemente (Punkt 32)
- Änderungen an Aufbau, Docks oder Beschriftungen
- Neue Pakete: voraussichtlich keine nötig. Falls doch, nenne sie dem Nutzer mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar.
2. `ctest --test-dir build` besteht.
3. Startprüfung 20 von 20 je Fall sauber.
4. Bildschirmfotos liegen im Bericht, Änderungen und Auffälligkeiten sind aufgelistet.
5. Handtest durch den Nutzer (Regel 10, Aussehen): höchstens 3 Schritte mit fertigen Befehlen, darunter der Vergleich mit OBS auf dem Streaming-PC (Farben, Schrift, Zahlenfelder). Diesen Vergleich kann nur der Nutzer machen, weil OBS nicht auf dem Studio-PC läuft.

## Bericht

Schreibe den Bericht nach `docs/berichte/P06-bericht.md` nach dem Muster von `docs/berichte/P05-bericht.md` und Regel 15. Trage erledigte Punkte (27, 28, 29, 30) in `docs/04-offene-punkte.md` ein und setze P06 in `docs/07-bauplan.md` unter «Erledigt» (ohne Abnahme durch den Nutzer: «ausgeführt, Handtest offen»).

Ein Commit pro Teil (Design, Schrift, Zahlenfelder, Verbindungsknöpfe, Bildschirmfotos und Tests, Bericht). Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus (Regel 17).
