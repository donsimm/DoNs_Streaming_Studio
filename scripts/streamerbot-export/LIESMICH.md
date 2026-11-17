# streamerbot-export

Einmaliges Werkzeug für P14 (Plan, Abschnitt 9.2, Empfehlung A): liest die LiteDB-Dateien aus dem Ordner `data/` von Streamer.bot und schreibt sie als JSON, damit der Import im Studio sie ohne .NET lesen kann. Nicht Teil des Studios: kein CMake, kein `ctest`.

## Nur auf einer Kopie

- Immer auf einer Kopie von `data/` arbeiten, **ohne `auth*.db`** (Zugangsdaten). Das Werkzeug öffnet `auth*.db` nie, auch wenn sie in der Kopie liegt.
- Die Log-Dateien `*-log.db` mitkopieren: Sie enthalten die neuesten Änderungen.
- Die Datenbanken werden nur lesend geöffnet (`ReadOnly`). Das Werkzeug prüft vorher und nachher mit SHA-256, dass sich nichts verändert hat.

## Bauen

```
cd scripts/streamerbot-export
~/.dotnet/dotnet build --no-restore
```

## Aufruf

```
~/.dotnet/dotnet run --no-build -- --struktur QUELLORDNER
~/.dotnet/dotnet run --no-build -- --export QUELLORDNER ZIELORDNER
```

- `--struktur`: je Datei die Sammlungen, je Sammlung die Anzahl Dokumente und die Felder mit BSON-Typen (verschachtelt mit `.`, Arrays mit `[]`) und der Anzahl Dokumente, die das Feld haben. Keine Werte.
- `--export`: je Sammlung `ZIELORDNER/<datei>/<sammlung>.json` (Array der Dokumente). Datum als ISO-8601 in UTC, ObjectId und Guid als Text, Binärdaten als Base64. Bestehende Dateien werden überschrieben.
- Gelesen werden `*.db` (ausser `*-backup.db`, `*-log.db`, `*-tmp.db`, `auth*.db`) sowie `quotes.dat` und `users.dat`. Das Format erkennt das Werkzeug selbst: LiteDB wie oben, JSON-Text wird unverändert als `ZIELORDNER/<name>.json` kopiert, sonst «Format unbekannt» mit den ersten 16 Bytes als Hex.
- Rückgabewert 0 bei Erfolg, 1 bei Fehlern. Fehler werden je Datei gemeldet, die übrigen Dateien trotzdem verarbeitet.
