# Testdaten: Export der Zuschauer-Daten aus Streamer.bot (P14 z1a)

Erfundene Daten im Format von `scripts/streamerbot-export/` (`--export`), mit den Sammlungen,
Feldnamen und Typen aus `docs/archiv/recherchen/2026-11-15-z0-streamerbot-struktur.txt`. Alle Namen,
IDs und Werte sind ausgedacht (`testzuschauer1`, `100000001`); keine echten Daten (Plan 2.10,
Ausnahme von Regel 7 nach Entscheid des Nutzers vom 2026-11-15).

Fälle für `tst_zuschauerimport`:

- `globals/globals.json`: globale Variablen, einfach (Zahl, Text, grosse Zahl) und nicht einfach
  (wahr/falsch, JSON-Text, ohne Wert) → Archiv.
- `globals/user_globals.json`: Variablen je Zuschauer über die ID, über den Login (Feld `userLogin`,
  nicht in der Struktur, ID aus `twitch_data/subscriptionHistory.json`), Login ohne ID → Archiv,
  doppelter Eintrag (der neuere gilt), andere Plattform → Archiv.
- `commands/commandUsage.json`: Zähler der Befehle über alle und je Zuschauer.
- `firstwords/last_update.json`: unbekannte Felder (`userId`, `count`) → Variable `firstwords.count`.
- `pronouns/pronouns.json`, `kick_data/settings.json`, `twitch_data/*`, `neue_datei/` → Archiv.
- Leere Sammlungen (`[]`).
- `users.json` (`users.dat`): Zuschauer, doppelter Zuschauer, andere Plattform, Benutzergruppen
  (eine ohne GUID → Archiv).
- `quotes.json` (`quotes.dat`): Zitate, mit UTF-8-Kennung (BOM) wie das Original.
- `auth/`: kein JSON; der Import darf es nie lesen.
- Diese Datei: kein JSON, wird übersprungen.
