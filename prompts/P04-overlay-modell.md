# P04 – Overlay-Modell und Format der Overlay-Datei

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md`, Abschnitte «Editor» und «Konzept der zwei Zeichner», und `docs/03-funktionen-entwurf.md`), den Bericht `docs/berichte/P03-bericht.md` und sieh dir das Bild `docs/bilder/oberflaeche.png` an.

## Ausgangslage

P03 hat das Gerüst gebaut: `dss_common` (nur Qt Core), `dss-core`, `dss-app`, Tests mit Qt Test, `scripts/einrichten.sh`. P04 baut das Datenmodell der Overlays und das Format der Overlay-Datei (K-01). Es ist reine Logik ohne Oberfläche (Regel 9). Kern, Fenster und Zeichner nutzen es in späteren Bausteinen.

## Entscheide des Nutzers für diesen Baustein (2026-11-02)

1. **Dateien als Kopie:** Fügt der Nutzer ein Bild, Video oder eine Schriftdatei hinzu, legt die Software eine Kopie im eigenen Datenordner an. Das Overlay verweist nur auf die Kopie (K-03).
2. **Drehung** wird als ganze Zahl in Zehntelgrad gespeichert, Bereich 0 bis 3599 (Anzeige im Bild: «0.0 °»).
3. **Adresse aus dem Namen:** Jedes Overlay hat eine Adresse für die Browserquelle, gebildet aus dem Namen: Kleinbuchstaben, `ä`→`ae`, `ö`→`oe`, `ü`→`ue`, andere Akzente ohne Akzent (`é`→`e`), alle anderen Zeichen ausser `a-z` und `0-9` werden `-`, mehrere `-` hintereinander werden eines, `-` am Anfang und Ende fällt weg. Leeres Ergebnis wird `overlay`. Beispiel: «Überblick Start» → `ueberblick-start`.
4. **Adresse eindeutig:** Ist die Adresse schon vergeben, hängt die Software `-2`, `-3` usw. an.
5. **Umbenennen:** Die Adresse ändert sich nur, wenn der Aufrufer es ausdrücklich verlangt (später fragt das Fenster nach, Standard «nein»). Ohne diese Angabe bleibt die Adresse gleich.

## Auftrag

### 1. Datenmodell in `dss_common`

Nur Qt Core. Ein Overlay hat:

| Feld | Inhalt |
|---|---|
| Kennung | UUID, intern, ändert sich nie. Bestimmt den Dateinamen. |
| Name | frei wählbar, nicht leer |
| Adresse | aus dem Namen gebildet (Entscheid 3 bis 5) |
| Breite, Höhe | ganze Pixel, 1 bis 7680 bzw. 1 bis 4320 |
| Quellen | Liste in Zeichenreihenfolge, das erste Element liegt hinten |

Eine Quelle hat:

| Feld | Inhalt |
|---|---|
| Kennung | UUID, ändert sich nie |
| Art | `text`, `image`, `video` (Video umfasst WebM mit Alpha) |
| Name | Anzeigename in der Liste «Quellen» |
| X, Y | ganze Pixel, dürfen negativ sein (Quelle teilweise ausserhalb der Leinwand) |
| Breite, Höhe | ganze Pixel, mindestens 1 |
| Drehung | Zehntelgrad, 0 bis 3599, um die Mitte der Quelle |
| Sichtbar | ja/nein |

Eigenschaften je Art, nur der MUSS-Umfang:

- `text` (TX-01 bis TX-05): Inhalt (mehrzeilig, Zeilenwechsel als `\n`), Schriftdatei (Verweis auf eine Datei im Datenordner), Gewicht (100 bis 900), kursiv ja/nein, Schriftgrösse in ganzen Pixeln, Textfarbe mit Deckkraft als `#RRGGBBAA`, Ausrichtung waagrecht `left`/`center`/`right`.
- `image`: Datei (Verweis).
- `video`: Datei (Verweis). Weitere Eigenschaften (Wiederholen, Ton) sind nicht entschieden: als `[OFFEN]` in den Bericht.

Ungültige Werte (zum Beispiel Drehung 3600, Breite 0, leerer Name, unbekannte Art) weist das Modell mit einer klaren Fehlermeldung ab. Nichts wird still korrigiert.

### 2. Bearbeiten

Funktionen im Modell (E-01, E-07, Datenteil von E-08):

- Overlay anlegen (Name, Breite, Höhe), umbenennen (mit oder ohne neue Adresse), kopieren (neue Kennungen für Overlay und alle Quellen, Name «<Name> Kopie», eigene Adresse, eigener Token), löschen.
- Quelle hinzufügen, entfernen, eine Stufe nach vorne oder hinten, ein- und ausblenden, Lage, Grösse und Drehung setzen.

### 3. Speichern und Laden (E-02, K-01)

- Datenordner: der Ordner, den `QStandardPaths::AppDataLocation` liefert. Prüfe in der Qt-6.8-Dokumentation, welcher Pfad das unter Linux ist, und nenne ihn im Bericht mit Quelle. Für Tests und Handtest muss ein anderer Datenordner wählbar sein.
- Je Overlay eine Datei `overlays/<Kennung>.json`. Feldnamen im JSON auf Englisch (Regel 13: Bezeichner Englisch), Versionsfeld `format_version` mit Wert `1`.
- Dateien des Nutzers liegen als Kopie unter `media/<SHA-256 des Inhalts>.<Endung>`. Dieselbe Datei zweimal hinzugefügt ergibt nur eine Kopie. Der ursprüngliche Dateiname wird als Name der Quelle vorgeschlagen. Ungenutzte Kopien werden in P04 nicht gelöscht.
- Speichern atomar (zum Beispiel `QSaveFile`), damit ein Absturz beim Speichern keine halbe Datei hinterlässt.
- Laden aller Overlays beim Start. Eine kaputte Datei erzeugt eine Fehlermeldung mit Dateiname, die übrigen Overlays werden trotzdem geladen, die kaputte Datei wird nicht überschrieben. Eine Datei mit höherer `format_version` als bekannt wird abgewiesen, nicht geraten.
- Fehlt eine Kopie unter `media/`, auf die ein Overlay verweist, meldet das Laden es pro Quelle; das Overlay wird trotzdem geladen.

### 4. Token pro Overlay

- Jedes Overlay hat einen Token für die Browserquelle (entschieden in `docs/01-festlegungen.md`, «Betrieb im Netz»). Erzeugt mit einem kryptografisch sicheren Zufallsgenerator (zum Beispiel `QRandomGenerator::system()`), mindestens 128 Bit.
- Der Token steht **nicht** in der Overlay-Datei, sondern in einer eigenen Datei `tokens.json` im Datenordner, Dateirechte `0600`. Er erscheint nie im Protokoll (Regel 12). Grund: Overlay-Dateien sollen sich weitergeben oder exportieren lassen, ohne den Token mitzunehmen.
- Kopieren erzeugt einen neuen Token, Löschen entfernt ihn.

### 5. Werkzeug für den Handtest: `dss-cli`

Ein kleines Programm ohne Fenster, Schalter `DSS_BUILD_CLI` (Standard an, Regel 8). Befehle auf Deutsch (Regel 13), Option `--datenordner PFAD`:

```
dss-cli overlay-liste
dss-cli overlay-neu NAME BREITE HOEHE
dss-cli overlay-umbenennen ADRESSE NEUER_NAME [--adresse-mitaendern]
dss-cli overlay-kopieren ADRESSE
dss-cli overlay-loeschen ADRESSE
dss-cli quelle-liste ADRESSE
dss-cli quelle-bild ADRESSE DATEI
dss-cli quelle-video ADRESSE DATEI
dss-cli quelle-text ADRESSE TEXT SCHRIFTDATEI
dss-cli quelle-geometrie ADRESSE NR X Y BREITE HOEHE DREHUNG
```

`overlay-liste` zeigt Name, Adresse, Auflösung und Anzahl Quellen, nicht den Token. Exit-Code 0 bei Erfolg, sonst ungleich 0 mit Meldung auf Deutsch.

### 6. Dokumentation des Formats

`docs/06-overlay-format.md`: alle Felder mit Typ, Bereich und Bedeutung, ein vollständiges Beispiel mit je einer Quelle `text`, `image`, `video`, Aufbau des Datenordners, Regeln für die Adresse. Diese Datei ist die Vorlage für beide Zeichner (K-01).

## Tests

- Qt Test, ohne Qt Gui, eigener temporärer Datenordner je Test.
- Mindestens: Speichern und Laden ergibt dasselbe Overlay; Adresse aus Namen (Umlaute, Akzente, Sonderzeichen, nur Sonderzeichen, gleicher Name zweimal); Umbenennen mit und ohne neue Adresse; Kopieren (neue Kennungen, neuer Token); Löschen (Datei und Token weg); gleiche Mediendatei zweimal → eine Kopie; ungültige Werte abgewiesen; kaputte Datei; höhere `format_version`; fehlende Mediendatei; Dateirechte von `tokens.json`.
- Testdateien: mit `prototypes/messung/media_erzeugen.sh` erzeugen (Bild, Video, WebM mit Alpha). Als Schriftdatei eine auf dem Studio-PC vorhandene Datei (prüfe mit `fc-list`, welche; Be Vietnam Pro kommt mit dem Zeichner). Regel 7: Die Tests nutzen dieselben Dateien wie der Handtest.

## Nicht Teil dieses Auftrags

- Verteilung an die Zeichner (K-02), Webserver, Browserquelle, Verbindung Kern–Fenster
- Zeilenumbruch im Kern (K-04, TX-11), alle SOLL- und KANN-Eigenschaften
- Zeichner, Oberfläche, Rückfrage beim Umbenennen im Fenster
- Löschen ungenutzter Kopien unter `media/`
- Neue Pakete: voraussichtlich keine nötig. Falls doch, nenne sie dem Nutzer mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar.
2. `ctest --test-dir build` besteht.
3. Die Startprüfung aus P03 (`scripts/startpruefung.py --runs 20`) läuft weiterhin 20 von 20 sauber.
4. `docs/06-overlay-format.md` ist vollständig, das Beispiel darin lässt sich mit dem Modell laden (als Test).
5. Handtest durch den Nutzer: Schreibe im Bericht eine Liste mit fertigen Befehlen (ein Befehl pro Schritt, mit erwarteter Ausgabe): Overlay «Überblick Start» 1920x1080 anlegen, Bild und Text hinzufügen, Geometrie setzen, umbenennen ohne und mit neuer Adresse, kopieren, löschen, Liste prüfen, eine Overlay-Datei mit `cat` ansehen. Der Nutzer führt sie aus.

## Bericht

Schreibe den Bericht nach `docs/berichte/P04-bericht.md` nach dem Muster von `docs/berichte/P03-bericht.md` und Regel 15: geänderte Dateien, alle Befehle mit Ausgabe (gekürzt, wo sehr lang), Testergebnisse, nicht Erledigtes als `[OFFEN]`, getroffene Annahmen, Quellen (nur wirklich abgerufene, übrige als «nicht abgerufen»).

Ein Commit pro Teil (Modell, Speichern und Laden, Token, `dss-cli`, Tests, Formatdokument, Bericht). Prüfe vor dem Push mit `git status`, dass nichts Gewolltes fehlt. Pushe am Ende auf `main` (der Nutzer erteilt diesen Auftrag hiermit; für den Push braucht der Nutzer sein GitHub-Token).
