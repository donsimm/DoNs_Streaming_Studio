# Testdaten: Datenordner mit Overlay-Dateien im Format 1 bis 5 (P10a, P10, P12b)

Für `tst_datenformat` und die Prüfung von `scripts/installieren.sh` (Umwandeln mit Sicherung).
Angelegt am 2026-11-03 mit `dss-cli` (Format 3), danach von Hand zurückgestuft nach
`docs/06-overlay-format.md`, Abschnitt «Versionen»:

- `format-eins` (Bild und Video): Format 1, ohne `hold_at_end`, `flip_horizontal`, `flip_vertical`
- `format-zwei` (Video, «Am Ende anhalten»): Format 2, ohne `flip_horizontal`, `flip_vertical`
- `format-drei` (Text und gespiegeltes, gedrehtes Bild): Format 3
- `format-vier` (wie `format-drei`, «Ausgabe aus»): Format 4 mit `output` = `false`, angelegt
  am 2026-11-03 (P10) als Kopie von `format-drei` mit neuen Kennungen und eigenem Test-Token
- `format-fuenf` (Video wie `format-zwei`, wiederholt, Lautstärke 35 %, stumm): Format 5 mit
  `video.volume` und `video.muted`, von Hand angelegt am 2026-11-14 (P12b b5) mit neuen Kennungen
  und eigenem Test-Token

Bild und Videos kommen aus `tests/daten/exif` und `tests/daten/video-drehung`. Die Schriftdatei
der Textquelle fehlt absichtlich (das Programm meldet das als Warnung). `tokens.json` enthält nur
Token dieser Testdaten, keine echten.
