# P08 – Bearbeiten im Fenster

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md` mit den Abschnitten «Overlay-Daten», «Overlay-Liste», «Kern als Dienst», `docs/03-funktionen-entwurf.md`, `docs/04-offene-punkte.md`, `docs/06-overlay-format.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`), den Bericht `docs/berichte/P07-bericht.md` und sieh dir `docs/bilder/oberflaeche.png` an.

## Ausgangslage

Seit P07 hält der Kern die Overlays, und das Fenster zeigt sie über eine **nur lesende** Schnittstelle an. P08 macht die Docks «Overlays», «Quellen» und «Eigenschaften» bedienbar. Jede Änderung geht als Nachricht an den Kern; nur der Kern ändert Dateien (Entscheid aus P07). Quellen werden noch **nicht** auf der Leinwand gezeichnet (P09), und das Dock «Steuerung» bleibt ausgegraut (P10).

## Entscheide des Nutzers für diesen Baustein (2026-11-02)

1. **Löschen** eines Overlays, eines Ordners oder einer Quelle **mit Rückfrage**.
2. **Änderungen wirken sofort:** gespeichert und an alle Fenster verteilt, sobald ein Wert geändert ist, wie in OBS. Kein Knopf «Übernehmen».
3. **Rückgängig und Wiederholen** kommt nicht in P08 (neue Funktion E-22, SOLL, später).
4. **Be Vietnam Pro** als Datei im Repo, Standardschrift für neue Textquellen (E-11).
5. **Schriftauswahl:** alle installierten Schriften des Studio-PCs, dazu Be Vietnam Pro und eigene Schriftdateien. Beim Wählen wird die passende Datei (Schrift und Schnitt) wie ein Bild in den Datenordner kopiert (K-03), damit die Browserquelle auf dem Streaming-PC dieselbe Datei nutzt.
6. **Overlay-Liste:** per Maus frei verschiebbar, Reihenfolge gespeichert; Ordner mit **einer** Ebene (keine Unterordner); Adresse unabhängig vom Ordner; Reihenfolge und Ordner in einer eigenen Datei des Kerns, nicht in den Overlay-Dateien.
7. **Umbenennen** eines Overlays fragt, ob die Adresse mitgeändert werden soll, Standard «nein» (entschieden vor P04).
8. **Quellen-Dock:** vorderste Quelle oben, wie in OBS (Punkt 40).

## Auftrag

### Teil A: Schnittstelle zum Ändern

- Erweitere die Schnittstelle (`src/common/protocol.*`) um Nachrichten vom Fenster an den Kern für alle Änderungen in Teil C bis E. Der Kern prüft jede Änderung mit dem Modell aus P04 und antwortet mit Erfolg oder einem Fehler mit Meldung auf Deutsch; das Fenster zeigt Fehler in der Statuszeile. Bei Erfolg schickt der Kern das Geänderte an **alle** Fenster (K-02), auch an das, das die Änderung geschickt hat; das Fenster übernimmt den Zustand nur aus dieser Nachricht.
- Entscheide, ob die Version der Schnittstelle steigt, und begründe es im Bericht. Führe `docs/08-kern-schnittstelle.md` vollständig nach.
- Häufige Änderungen (zum Beispiel Zahl mit gedrückter Pfeiltaste) dürfen den Kern nicht überlasten und die Datei nicht bei jedem Schritt schreiben. Wähle einen Weg (zum Beispiel Zusammenfassen innerhalb kurzer Zeit) und begründe ihn; die Änderung muss trotzdem spätestens nach kurzer Zeit gespeichert sein.
- Der Token wird weiterhin **nicht** übertragen (P10).

### Teil B: Reihenfolge und Ordner der Overlay-Liste

- Neue Datei des Kerns im Datenordner (zum Beispiel `liste.json`), atomar gespeichert wie die Overlay-Dateien. Inhalt: Reihenfolge der Einträge oben auf der Liste, Ordner mit Name, Reihenfolge der Overlays je Ordner, ob ein Ordner aufgeklappt ist. Format mit `format_version` und Beschreibung in `docs/06-overlay-format.md`.
- Overlays, die in der Datei fehlen (zum Beispiel mit `dss-cli` bei angehaltenem Kern angelegt), kommen ans Ende der obersten Ebene; Einträge zu fehlenden Overlays werden entfernt. Eine kaputte Datei wird gemeldet und nicht überschrieben; die Liste fällt dann auf «nach Name» zurück.
- `dss-cli overlay-liste` zeigt die Overlays in derselben Reihenfolge, mit Ordnername.

### Teil C: Dock «Overlays»

- Liste mit Ordnern (aufklappbar) und Overlays. Ziehen mit der Maus: Overlays umsortieren, in Ordner hinein und heraus; Ordner umsortieren. Ordner in Ordner ist nicht möglich.
- Knöpfe unten wie auf dem Bild (hinzufügen, entfernen, kopieren) und ein Kontextmenü (rechte Maustaste): Overlay anlegen (Name, Breite, Höhe; Vorschlag 1920x1080), umbenennen (mit Rückfrage zur Adresse, Entscheid 7), kopieren, löschen (Entscheid 1), Ordner anlegen, Ordner umbenennen, Ordner löschen (Rückfrage; die Overlays darin kommen auf die oberste Ebene, sie werden nicht gelöscht).
- Ein neues oder kopiertes Overlay wird gewählt.

### Teil D: Dock «Quellen»

- Knöpfe wie auf dem Bild: hinzufügen (Auswahl Bild, Video, Text), entfernen (Entscheid 1), nach vorne, nach hinten; Augen-Symbol schaltet sichtbar und ausgeblendet. Ziehen mit der Maus ändert die Reihenfolge. Vorderste Quelle oben (Entscheid 8).
- Bild und Video: Dateiauswahl; der Kern kopiert die Datei in den Datenordner (wie in P04). Text: neue Quelle mit Inhalt «Text» und Be Vietnam Pro.
- Doppelklick oder Kontextmenü: Quelle umbenennen.

### Teil E: Dock «Eigenschaften»

- Für die gewählte Quelle bedienbar: Name, X, Y, Breite, Höhe (ganze Pixel), Drehung (0.0 bis 359.9 Grad, eine Nachkommastelle, gespeichert in Zehntelgrad). Ungültige Werte lassen sich nicht eingeben.
- Text (TX-01 bis TX-05): Inhalt mehrzeilig, Schrift (Teil F), Gewicht/Schnitt, kursiv, Schriftgrösse in Pixeln, Textfarbe mit Deckkraft (Farbwahl), Ausrichtung links/Mitte/rechts.
- Video: «Wiederholen» ein und aus (Punkt 31).
- Bild: Datei ersetzen.
- Die Knöpfe zum Ausrichten unten im Dock bleiben ausgegraut (E-10, SOLL).

### Teil F: Schriften

- Be Vietnam Pro als Datei unter `src/app/schriften/` oder einem passenderen Ort, den Kern und Fenster finden (begründen). Bezug aus dem offiziellen Repo der Schrift bei Google Fonts; Lizenz an der Quelle prüfen (erwartet: SIL Open Font License), Lizenzdatei daneben, Quelle, Version und Lizenz im Bericht.
- Auswahl (Entscheid 5): Liste aller installierten Schriften des Studio-PCs mit ihren Schnitten, dazu Be Vietnam Pro und «Datei wählen …» für eigene Dateien. Zum gewählten Namen und Schnitt braucht es die **Datei**; Qt liefert dafür keinen Pfad. Finde einen Weg (zum Beispiel fontconfig) und begründe ihn. Neue Pakete nur nach Rückfrage beim Nutzer (Regel 5).
- Die gewählte Datei kopiert der Kern in den Datenordner (gleiche Prüfsumme → eine Kopie). Gibt es zu einem Schnitt keine eigene Datei (zum Beispiel variable Schriften mit allen Gewichten in einer Datei), wird das im Bericht als Annahme festgehalten und `[OFFEN]` markiert, wenn es nicht sicher geht.
- Lizenz von Systemschriften: nicht prüfen; die Software bleibt privat. Im Bericht als Hinweis vermerken.

## Tests

- Ohne Qt Gui: alle neuen Nachrichten lesen und schreiben; Kern prüft und lehnt ungültige Änderungen ab; Datei der Liste (lesen, schreiben, fehlende und überzählige Einträge, kaputte Datei); Zusammenfassen häufiger Änderungen; Auflösung Schriftname und Schnitt → Datei.
- Zusammenspiel mit dem Kern als eigener Prozess (wie `tst_kernverbindung`): jede Art von Änderung einmal, zwei Fenster gleichzeitig verbunden bekommen dieselbe Änderung, Kern neu gestartet → alles wie vorher.
- Startprüfung `scripts/startpruefung.py --runs 20` weiter 20 von 20 in allen vier Fällen.
- Bildschirmfoto mit einem Test-Overlay mit Ordner, je einer Quelle Bild, Text, Video und gewählter Textquelle in «Eigenschaften».

## Nicht Teil dieses Auftrags

- Zeichnen der Quellen, Bedienen mit der Maus auf der Leinwand (P09)
- Dock «Steuerung», Browserquelle, Token (P10)
- Rückgängig und Wiederholen (E-22), Ausrichten, Einrasten, Raster (E-10), SOLL- und KANN-Eigenschaften der Quelle Text (TX-06 bis TX-16)
- Löschen ungenutzter Kopien im Datenordner (Punkt 24)

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar.
2. `ctest --test-dir build` besteht, auch in einem frischen Klon.
3. Startprüfung 20 von 20 je Fall sauber.
4. `docs/06-overlay-format.md` und `docs/08-kern-schnittstelle.md` nachgeführt.
5. Handtest durch den Nutzer (Regel 10, Bedienung): höchstens 5 Schritte mit fertigen Befehlen, darunter: Overlay und Ordner anlegen und per Maus verschieben; Bild, Text und Video hinzufügen, Reihenfolge ändern, eine Quelle ausblenden; Zahlen und Texteigenschaften ändern, eine Systemschrift wählen; Umbenennen mit Rückfrage; Löschen mit Rückfrage; Fenster schliessen und öffnen → alles ist noch da. Am Ende ein Schritt zum Aufräumen.

## Bericht

Schreibe den Bericht nach `docs/berichte/P08-bericht.md` nach dem Muster der bisherigen Berichte und Regel 15. Trage erledigte Punkte in `docs/04-offene-punkte.md` ein, ergänze E-22 «Rückgängig und Wiederholen» (SOLL) in `docs/03-funktionen-entwurf.md` und setze P08 in `docs/07-bauplan.md` unter «Erledigt» («ausgeführt, Handtest offen»).

Ein Commit pro Teil (A bis F, Tests, Bericht). Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt und nichts aus `~/.config` oder `~/.local/share` im Repo gelandet ist. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus (Regel 17).
