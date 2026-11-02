# 03 – Funktionen je Modul (Entwurf)

Stand: 2026-11-02. Dieser Entwurf fasst zusammen, was der Nutzer genannt hat (du), was auf dem Bild der Oberfläche zu sehen ist (Bild) und was im alten Repo `donstv_Studio` stand (altes Repo, Funktionsliste `docs/01-funktionen.md`). Die Prioritäten des Editors sind entschieden (2026-11-02). Alles andere ist zu bestätigen und noch nicht priorisiert.

## Editor (Hauptfokus)

Prioritäten entschieden (2026-11-02, vom Nutzer bestätigt). MUSS: gehört in die erste lauffähige Version. SOLL: direkt danach. KANN: später oder abhängig von einem anderen Modul. Herkunft der Funktionen: du, Bild, altes Repo (siehe oben).

| Nr. | Funktion | Stufe | Grund |
|---|---|---|---|
| E-01 | Overlays anlegen, umbenennen, kopieren, löschen; Auflösung pro Overlay | MUSS | Grundlage für alles |
| E-02 | Overlays speichern und beim Start wieder laden | MUSS | Ohne geht alles bei jedem Neustart verloren |
| E-03 | Quelle Text | MUSS | Vom Nutzer genannt. Umfang: Tabelle TX-01 bis TX-16 unten |
| E-04 | Quelle Bild | MUSS | Vom Nutzer genannt |
| E-05 | Quelle Video und WebM mit Alpha | MUSS | Vom Nutzer genannt; Dekodierweg in P02 (M1) gemessen |
| E-06 | Quelle GIF | SOLL | Bild und Video zuerst |
| E-07 | Quellen hinzufügen, entfernen, Reihenfolge ändern, ein- und ausblenden | MUSS | Bild der Oberfläche |
| E-08 | Verschieben, skalieren, drehen mit der Maus und per Zahleneingabe (X, Y, Breite, Höhe, Drehung), ganze Pixel | MUSS | Bild der Oberfläche, CLAUDE.md Regel 11 |
| E-09 | Lineale, Zoom «Einpassen», Anzeige der Zeigerposition | MUSS | Bild der Oberfläche |
| E-10 | Einrasten, Raster, Hilfslinien, Ausrichten | SOLL | Bearbeiten geht auch ohne |
| E-11 | Schriften als Dateien: Be Vietnam Pro als Standard, eigene Schriftdateien | MUSS | Beide Zeichner laden dieselbe Datei. Systemschriften des Studio-PCs fehlen in OBS auf dem Streaming-PC (Einschätzung, nicht gemessen) |
| E-12 | Ausgabe als Browserquelle mit kopierbarer URL (mit Token), Aktualisierung in OBS ohne Neuladen | MUSS | Hauptweg der Ausgabe; Prüfung in OBS wartet auf T-04 |
| E-13 | Vorschau mit einstellbarer Bildrate und Verkleinerung | SOLL | Leinwand funktioniert auch mit fester Bildrate |
| E-14 | Animationen pro Quelle (Schweben, Atmen, Schreibmaschine) | SOLL | Grösstes Risiko für Abweichungen zwischen den Zeichnern, erst nach dem Konzept der zwei Zeichner |
| E-15 | Konfiguration exportieren und importieren | SOLL | Speichern (E-02) reicht für den Anfang |
| E-16 | Helles Design (dunkel ist Standard) | KANN | Keine Funktion für den Stream |
| E-17 | Spezialquellen: Chat-Box, Alert-Box, Ereignisliste, Zielbalken | KANN | Brauchen die Twitch-Anbindung, kommen mit dem Chat-Bot |
| E-18 | Quellen an die aktive OBS-Szene koppeln | KANN | Braucht obs-websocket, wartet auf T-05 |
| E-19 | NDI, höchstens 2 Ausgänge | KANN | Bereits als «später» entschieden, wartet auf T-03 und das NDI-SDK |

### Quelle Text (E-03)

Umfang entschieden (2026-11-02, vom Nutzer bestätigt). Stufen wie beim Editor. Alle Werte in ganzen Pixeln der Overlay-Auflösung (CLAUDE.md Regel 11). Animationen gehören zu E-14.

| Nr. | Eigenschaft | Stufe | Grund |
|---|---|---|---|
| TX-01 | Inhalt mehrzeilig, Zeilenwechsel von Hand (Enter) | MUSS | Grundfunktion |
| TX-02 | Schriftdatei und Schnitt (Gewicht, kursiv) | MUSS | Baut auf E-11 auf |
| TX-03 | Schriftgrösse in Pixeln | MUSS | Grundfunktion |
| TX-04 | Textfarbe mit Deckkraft | MUSS | Grundfunktion |
| TX-05 | Ausrichtung waagrecht: links, Mitte, rechts | MUSS | Grundfunktion |
| TX-06 | Ausrichtung senkrecht in der Box: oben, Mitte, unten | SOLL | Nützlich bei fester Boxgrösse |
| TX-07 | Zeilenabstand und Zeichenabstand | SOLL | Feinarbeit |
| TX-08 | Umriss (Farbe, Breite) | SOLL | Lesbarkeit im Stream |
| TX-09 | Schatten (Farbe, Versatz X/Y, Unschärfe) | SOLL | Lesbarkeit im Stream |
| TX-10 | Hintergrundfläche hinter dem Text (Farbe, Innenabstand, runde Ecken) | SOLL | Ersetzt eine separate Bildquelle |
| TX-11 | Automatischer Zeilenumbruch an der Breite der Box | SOLL | Rechnet nur der Kern, die Zeichner brechen nie selbst um (K-04) |
| TX-12 | Verhalten bei zu langem Text: abschneiden oder Schrift verkleinern | KANN | Erst nach TX-11 sinnvoll |
| TX-13 | Farbverlauf als Füllung | KANN | Gestaltung |
| TX-14 | Unterschiedliche Formate in einem Text (einzelnes Wort fett oder farbig) | KANN | Aufwendig in beiden Zeichnern |
| TX-15 | Umwandlung in Gross- oder Kleinbuchstaben | KANN | Gestaltung |
| TX-16 | Platzhalter im Text, zum Beispiel letzter Follower | KANN | Braucht die Twitch-Anbindung, kommt mit dem Chat-Bot |

## Kopfbereich und Allgemeines

- Modul-Reiter, Verbindungsanzeige für Twitch, YouTube, OBS und NDI, Leistungsanzeige für CPU, RAM und LAN (Bild).
- Einstellungen: dunkles und helles Design (altes Repo).
- Konfiguration speichern, laden, exportieren, importieren (altes Repo).

## Chat-Bot (nochmals zu prüfen)

- Zuerst: Twitch-Anmeldung, Chat ansehen, Nachrichten senden, Commands verwalten und ausführen (Name, Befehl, Berechtigung, Cooldown, Antwort), Protokoll (altes Repo).
- Danach: Actions, Queues, Variablen, Timer, Trigger (Streamer.bot-Stil) und Import einer früheren Streamer.bot-Installation (altes Repo; der Nutzer hatte den Import als wichtig bezeichnet).
- Chat-Spiele mit Anzeige im Overlay (du). Ablauf: Chat-Ereignis, Spiellogik, Anzeige per Browserquelle.

## Spätere Module (nur Ideen)

- Live-Modul: Funktionen für die Zeit, in der der Nutzer live ist. Inhalt wird später geklärt.
- Zuschauer-Minispiele, zum Beispiel «Pflanze giessen» und «Katze streicheln» (altes Repo).
- Musikplayer: Audio über die Standard-Audiokarte und ein externes Mischpult. Die Audiokarte ist in der Software wählbar. Audio per NDI ist als Option vorgesehen.
