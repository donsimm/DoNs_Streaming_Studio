# P05 – Nachträge zum Overlay-Modell und Fenster-Gerüst

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md` und `docs/06-overlay-format.md`), die Berichte `docs/berichte/P03-bericht.md` und `docs/berichte/P04-bericht.md`, und sieh dir das Bild `docs/bilder/oberflaeche.png` genau an.

## Ausgangslage

P03 hat das Gerüst gebaut, P04 das Overlay-Modell mit `dss-cli`. Nach P04 hat der Nutzer offene Punkte entschieden (`docs/01-festlegungen.md`, Abschnitt «Overlay-Daten»; `docs/04-offene-punkte.md`, Abschnitt «Entschieden nach P04»). P05 setzt zuerst diese Entscheide im Modell um (Teil A) und baut dann das Fenster als Gerüst nach dem Bild, noch ohne Daten (Teil B).

Neu seit P04: Regel 10 in `CLAUDE.md` (Handtest nur, wo Claude Code nicht selbst prüfen kann) und `.claude/settings.json` (du darfst `git pull --rebase origin main` und `git push origin main` ohne Rückfrage ausführen; Push per SSH).

## Teil A: Nachträge zum Overlay-Modell

1. **Adresse:** «ß» wird «ss», «æ» wird «ae», «ø» wird «oe», «ł» wird «l» (gross wie klein). Beispiel: «Straße» → `strasse`. Tests ergänzen.
2. **Video wiederholen:** Die Quelle `video` bekommt ein Feld für Wiederholen am Ende, Standard **nicht wiederholen**. In `dss-cli` als Option von `quelle-video` (zum Beispiel `--wiederholen`) und in `quelle-liste` sichtbar.
3. **Format:** `format_version` bleibt `1`, wenn es auf dem Studio-PC noch keine echten Overlay-Dateien gibt. Prüfe das (`ls ~/.local/share/dons-streaming-studio`) und nenne das Ergebnis im Bericht. Gibt es dort Dateien, frag den Nutzer, bevor du weitermachst.
4. `docs/06-overlay-format.md` und das Beispiel darin nachführen; der Test `formatDocExample` muss weiter bestehen.

Nicht in P05: Ton abspielen (E-20, E-21). Das Modell speichert nichts zum Ton; die Browserquelle wird später immer stumm sein.

## Teil B: Fenster-Gerüst

`dss-app` soll aussehen wie `docs/bilder/oberflaeche.png`, aber noch ohne Daten und ohne Verbindung zum Kern. Regel 9 gilt: keine Logik im Fenster, keine Layout-Änderungen in Ereignis-Behandlern.

| Bereich | Inhalt in P05 |
|---|---|
| Menüleiste | Datei, Bearbeiten, Ansicht, Docks, Werkzeuge, Hilfe. Funktionierende Einträge: Datei → Beenden; Docks → die vier Docks ein- und ausblenden; Hilfe → Über (Name, Version aus `dss_common`, Qt-Version). Alle anderen Einträge fehlen noch oder sind ausgegraut. |
| Kopfbereich | Modul-Reiter «Overlays» und «Chat-Bot» (Chat-Bot zeigt eine leere Seite mit «Noch nicht vorhanden»). Vier Verbindungsknöpfe Twitch, YouTube, OBS, NDI im Zustand «nicht verbunden». Drei Anzeigen CPU, RAM, LAN mit Titel und Verlaufsfläche, Wert «–». |
| Leinwand | Fläche in der Mitte mit Linealen oben und links in Pixeln der Overlay-Auflösung, Schachbrettmuster für Transparenz, dunkler Rand ausserhalb der Leinwand. Ohne Overlay zeigt sie eine leere Leinwand 1920x1080 als Platzhalter. Zoom «Einpassen» (Leinwand passt ganz in die Fläche, auch beim Ändern der Fenstergrösse). Anzeige der Zeigerposition unten links «X: … Y: …» in Pixeln der Leinwand, «–» ausserhalb. |
| Docks unten | Overlays, Quellen, Eigenschaften, Steuerung, mit denselben Bedienelementen und Beschriftungen wie auf dem Bild, alle leer bzw. ausgegraut. Docks lassen sich lösen, schliessen und über das Menü «Docks» wieder öffnen. |
| Statuszeile | links «Bereit», rechts Auswahl «Einpassen» und «Kern-Dienst: nicht verbunden». |
| Design | dunkel wie auf dem Bild. Farben an einer Stelle festlegen (für das spätere helle Design, E-16). |
| Fenster | Grösse, Lage und Anordnung der Docks werden beim Beenden gespeichert und beim Start wiederhergestellt. |

**Symbole:** Für Twitch, YouTube, OBS und NDI keine Markenlogos nachzeichnen. Verwende vorerst eine neutrale Beschriftung (zum Beispiel «TW», «YT», «OBS», «NDI»). Die Symbole werden aus Dateien unter `src/app/symbole/` geladen, damit der Nutzer sie später selbst ersetzen kann; nenne im Bericht Dateinamen und Format.

**Bildschirmfoto:** `dss-app --bildschirmfoto DATEI` startet das Fenster in 1280x800, wartet bis zum ersten Zeichnen, speichert ein Bild des Fensters (zum Beispiel mit `QWidget::grab`) und beendet sich mit Exit-Code 0. Lege das Ergebnis unter `docs/berichte/P05-bilder/` ab und vergleiche es selbst mit `docs/bilder/oberflaeche.png`. Nenne im Bericht jede Abweichung, die du siehst.

## Tests

- Teil A: Qt Test wie in P04, ohne Qt Gui.
- Teil B: Startprüfung `scripts/startpruefung.py --runs 20` weiter 20 von 20 sauber. Ein Test, der `dss-app --bildschirmfoto` in der X11-Sitzung ausführt und prüft, dass ein Bild in 1280x800 entsteht.
- Fensterlogik gibt es keine; zu testen ist nur, was ohne Bildschirm prüfbar ist.

## Nicht Teil dieses Auftrags

- Verbindung Fenster–Kern, echte Werte für CPU, RAM, LAN und Verbindungen
- Overlays und Quellen in den Docks anzeigen oder bearbeiten, Zeichnen von Quellen auf der Leinwand
- Rückfrage beim Umbenennen, Ton, Browserquelle, Webserver
- Helles Design (E-16), Einrasten, Raster, Hilfslinien (E-10)
- Neue Pakete: voraussichtlich keine nötig. Falls doch, nenne sie dem Nutzer mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar.
2. `ctest --test-dir build` besteht.
3. Startprüfung 20 von 20 je Fall sauber.
4. Bildschirmfoto liegt im Bericht, Abweichungen zum Bild des Nutzers sind aufgelistet.
5. Handtest durch den Nutzer (Regel 10, Aussehen und Bedienung): Schreibe im Bericht höchstens 5 kurze Schritte mit fertigen Befehlen, zum Beispiel Fenster starten, Grösse ändern (Leinwand passt sich an), Zeiger über die Leinwand bewegen (Position stimmt), ein Dock schliessen und über das Menü wieder öffnen, beenden und neu starten (Anordnung bleibt).

## Bericht

Schreibe den Bericht nach `docs/berichte/P05-bericht.md` nach dem Muster von `docs/berichte/P04-bericht.md` und Regel 15.

Ein Commit pro Teil (Teil A: Adresse, Video; Teil B: Menü und Kopfbereich, Leinwand, Docks und Statuszeile, Bildschirmfoto und Tests; Bericht). Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus.
