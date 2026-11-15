# P14 – Chat-Bot als Aktionssystem mit Editor (Plan)

Plan des Projektleiter-Agenten vom 2026-11-15. Er ersetzt den Plan «Basisfunktion Chat-Bot» vom 2026-11-09 (Befehle mit fester Antwort): Der Nutzer hat am 2026-11-15 entschieden, dass P14 ein Aktionssystem wie Streamer.bot wird, mit Editor im Studio. Grundlage:
- Entscheide des Nutzers vom 2026-11-09 und 2026-11-15 (unten)
- Plan P13 (`docs/archiv/prompts/P13-twitch.md`, Stand 2026-11-15): Ereignisfluss, zwei Konten, Ereignisse mit Argumentnamen
- Plan und Bericht P12 (`docs/archiv/prompts/P12-ton.md`, `docs/archiv/berichte/P12-bericht.md`): Ton über PipeWire, Ausgang je Zweck
- `docs/01-festlegungen.md` («Module», «Oberfläche», «Kern als Dienst»), `docs/03-funktionen-entwurf.md` («Chat-Bot», E-17, TX-16), `docs/08-kern-schnittstelle.md` (Version 13)
- Recherchen von Lotse vom 2026-11-09 (nicht im Repo): «Chat-Bot als Aktionssystem» (Aufbau von Streamer.bot, Variante D) und «So nutzt der Nutzer Streamer.bot» (Auswertung der echten Daten, Abläufe in Abschnitt 5)
- Regeln 4, 7, 8, 9, 12, 13 und 14 (`CLAUDE.md`)

**Voraussetzung:** P13 ist gebaut und abgenommen, beide Konten sind angemeldet (P13 N1, N2). Für Abschnitt e (Editor) liegt der abgenommene «Entwurf UI Chat-Bot (folgt)» vor.

**Ausführung:** sechs Abschnitte (a bis f) mit je 2 bis 3 Commits, nach jedem Abschnitt eine Prüfung durch Lotse, am Ende Prüfstufe «Baustein» und Handtest.

## Entscheide des Nutzers

- **2026-11-09:** eigenes Bot-Konto (zweite Anmeldung mit Code); Follows kommen direkt von Twitch; Kanal ist Affiliate; alles lesen und schreiben, was Twitch erlaubt; Streamer.bot-Daten liegen auf dem Studio-PC und dürfen gelesen werden (ohne Zugangsdaten).
- **2026-11-15:**
  - P14 ist ein **Aktionssystem wie Streamer.bot** (Variante D der Recherche): Aktionen mit Schrittliste, Auslöser, Variablen. Modell wie Streamer.bot von Anfang an, der Umfang der Schritte wächst in Bausteinen.
  - **Editor im Studio schon in P14**, mit Grundschritten (Chat senden, Pause, Ton, Overlay-Element zeigen/verstecken, OBS-Quelle ein/aus, Variable/Zähler, Wenn/Dann). Der Nutzer legt Aktionen selbst an und ändert, kopiert und löscht auch die mitgelieferten.
  - **Mitgeliefert und veränderbar** für die ersten Streams: Info-Befehle, Zeitnachrichten (auch beim Streamstart), Begrüssung bei der ersten Nachricht, Shoutout (Befehl und Raid), First Chatters mit dauerhaften Zählern, Chat-Sounds, Alerts (Follow, Sub, Cheer, Raid), Chat im Studio lesen und schreiben.
  - Alerts, Begrüssung und Shoutout erscheinen als **Overlay im Studio**, nicht über vorbereitete OBS-Quellen.
  - **Kein Skript-Schritt.** Das Datenmodell lässt Platz dafür, gebaut wird er nicht. Sonderfunktionen kommen als feste, einstellbare Schritte.
  - Der Bot läuft nur, solange das Studio offen ist. Das reicht.
  - **Nicht für die ersten Streams:** Kanalpunkte-Effekte, Kamera-Fernbedienung, Kategorie per Befehl, Umfrage und Vorhersage, Moderations-Knöpfe.
  - **Später als feste Bausteine:** Countdown-Timer, Watchtime. **Vorerst nicht:** Subathon, YouTube Music, Golf, Hugs, Q&A.
  - Der Streamer.bot-Import ist ein eigener, späterer Baustein.

## Wichtigste Befunde

| Befund | Beleg |
|---|---|
| Streamer.bot hat fünf Teile: Action mit geordneten Sub-Actions, Trigger an der Action, Commands und Timed Actions als eigene Objekte (über einen Trigger an eine Action gebunden), Queues (blockierend oder nicht, «Default» nicht blockierend), Argumente `%arg%` je Lauf und globale Variablen (dauerhaft oder flüchtig, auch je Benutzer) | https://docs.streamer.bot/guide/core/actions, https://docs.streamer.bot/guide/triggers, https://docs.streamer.bot/guide/commands, https://docs.streamer.bot/guide/core/timers, https://docs.streamer.bot/guide/variables |
| Gespeicherte Felder einer Action: `id`, `queue`, `enabled`, `excludeFromHistory`, `excludeFromPending`, `name`, `group`, `alwaysRun`, `randomAction`, `concurrent`, `triggers[]`, `subActions[]`. Sub-Actions tragen `id`, `weight`, `type` (Nummer), `parentId`, `enabled`, `index` und die Felder des Typs | Recherche (Dritte: https://raw.githubusercontent.com/PedroZamecki/streamerz/main/src/model.rs) |
| In den Daten des Nutzers (Fassung 24) sind Sub-Actions **verschachtelt** gespeichert: Gruppen und Zweige von If/Else haben ein eigenes `subActions`, jedes Element trägt zusätzlich `parentId`. Tiefe bis 3 | Recherche «So nutzt der Nutzer Streamer.bot», Abschnitte 0 und 3.1 |
| Nutzung beim Nutzer: 163 Actions, 150 Trigger (93 davon Befehle), 912 Sub-Actions. Am häufigsten: OBS-Quelle ein/aus 193, Kommentar 136, Pause 91, Argument setzen 50, Chat senden 44 (fast immer als Bot), Variable lesen/setzen 42, Ton 40 (Lautstärke 0,5 bis 1,5), Run Action 21, If/Else 18, Benutzerinfo 22. Befehle nur «Basic/Start», 17 mit Abklingzeit Kanal, 2 je Person, 44 an Gruppen gebunden. `~global~` und `$math()$` kommen nicht vor | dieselbe Recherche, Abschnitte 0, 3, 4 |
| C# kommt in 32 Actions vor, fast nur Pakete von Dritten (Subathon, Countdown, YouTube Music, Watchtime, Hugs, Golf, Q&A, Quick Bar) | dieselbe Recherche, Abschnitt 6 |
| Der Kern hat einen Mischer je Ausgang und einen Ausgang je Zweck (`kAudioPurposes`, heute nur `video` mit `[ton-video]`). Jede Stimme folgt der Uhr einer Seite (`PageClock`); eine Stimme ohne Seite (Ton sofort abspielen) gibt es nicht | `src/common/coresettings.h:38-72`, `src/audio/mixer.h:11-41` |
| Der Decoder liest mit libavcodec und wandelt in float, 48 kHz, Stereo. Ob er Dateien ohne Bildspur (mp3, wav, ogg) öffnet, ist **nicht geprüft** | `src/media/audiodecoder.h:14-27` |
| Die Mediathek nimmt nur Bilder (`png`, `jpg`, `jpeg`), Videos (`webm`, `mp4`) und Schriften (`ttf`, `otf`) | `src/common/mediaimport.cpp:76-80` |
| `dss_obs` kennt heute `GetVersion`, `GetSceneList`, `GetInputList`, `GetInputSettings`, `SetInputSettings`, `CreateInput`, `CreateSceneItem`, `GetSourceActive`. Für «Quelle ein/aus» fehlen `GetSceneItemList`, `GetSceneItemId`, `GetSceneItemEnabled`, `SetSceneItemEnabled` | `src/obs/obssources.cpp`, `src/obs/obsconnection.cpp`; https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md |
| Quellen eines Overlays kennen Sichtbarkeit, Text und Datei; jede Änderung schreibt die Overlay-Datei (K-02). Ein «nur jetzt»-Zustand, der nicht gespeichert wird, fehlt | `src/common/overlay.h:41-97`, `docs/08-kern-schnittstelle.md` («Verteilen von Änderungen») |
| Der Reiter «Chat-Bot» ist ein `QWidget` mit «Noch nicht vorhanden» im `QStackedWidget` der Module. Die Docks hängen am Hauptfenster und bleiben beim Wechsel des Reiters sichtbar | `src/app/mainwindow.cpp:175-192`, `src/app/headerbar.cpp:183-192` |
| Senden: `POST https://api.twitch.tv/helix/chat/messages`, Pflicht `broadcaster_id`, `sender_id` (Konto des Tokens), `message` (höchstens 500 Zeichen), wahlweise `reply_parent_message_id`; Antwort mit `is_sent` und `drop_reason` | https://dev.twitch.tv/docs/api/reference/#send-chat-message |
| Grenzen: 20 Nachrichten je 30 s, 100 für Kanal, Moderatoren und VIPs; bei Überschreitung «Twitch ignores the bot messages for 1 hour» | https://dev.twitch.tv/docs/chat/ |
| Ansage: `POST …/helix/chat/announcements` mit `moderator:manage:announcements`, «One announcement may be sent every 2 seconds» | https://dev.twitch.tv/docs/api/reference/#send-chat-announcement |
| Shoutout: `POST …/helix/chat/shoutouts` mit `moderator:manage:shoutouts`. Grenzen (einmal je 2 min, dasselbe Ziel einmal je 60 min, nur im laufenden Stream) **nicht geprüft**, Schritt 0 | https://dev.twitch.tv/docs/api/reference/#send-a-shoutout (2026-11-15 nicht gelesen) |
| Benutzerinfo: `GET …/helix/users?login=` liefert unter anderem `profile_image_url`; die letzte Kategorie liefert `GET …/helix/channels?broadcaster_id=`. Felder und Rechte **nicht geprüft**, Schritt 0 | https://dev.twitch.tv/docs/api/reference/#get-users, https://dev.twitch.tv/docs/api/reference/#get-channel-information |

## 1. Begriffe

Oberfläche auf Deutsch (Regel 13), der Name aus Streamer.bot steht im Tooltip (Frage 1). Im Code englische Bezeichner.

| Oberfläche | Streamer.bot | Bedeutung |
|---|---|---|
| Aktion | Action | benannter Ablauf aus Schritten |
| Schritt | Sub-Action | ein Baustein einer Aktion, zum Beispiel «Chat senden» |
| Auslöser | Trigger | was eine Aktion startet: Befehl, Twitch-Ereignis, Zeitnachricht, Test |
| Befehl | Command | Chat-Befehl wie `!discord`, mit Rechten und Abklingzeit |
| Zeitnachricht | Timed Action | Auslöser nach Zeit, wahlweise erst nach einigen Chat-Nachrichten |
| Warteschlange | Queue | Aktionen derselben Warteschlange laufen nacheinander, wenn sie «blockierend» ist |
| Argument | Argument | Wert, der nur während eines Laufs gilt, zum Beispiel `%user%` |
| Variable | Global | Wert über Läufe hinweg, flüchtig oder dauerhaft, für alle oder je Zuschauer |
| Lauf | – | eine Ausführung einer Aktion |
| Verlauf | Action History | Liste der letzten Läufe mit Ergebnis je Schritt |

## 2. Datenmodell

### 2.1 Grundsätze

- Felder und Aufbau wie Streamer.bot (Fassung 24 aus den Daten des Nutzers), Namen im Format in `snake_case`. Die Tabelle zur Typnummer von Streamer.bot steht im Code (`steptypes.h`, `triggertypes.h`). So übernimmt der spätere Import Bekanntes ohne Umbau.
- Schritte sind verschachtelt gespeichert wie in Fassung 24 (eigene Liste `steps` bei Gruppen und Zweigen). `parentId` braucht das Studio nicht; der Import liest es und baut die Verschachtelung.
- Kennungen sind UUIDs. Beim Import bleiben die GUIDs von Streamer.bot erhalten (erneuter Import möglich).
- Unbekannte Felder eines Objekts bleiben beim Lesen und Schreiben erhalten (`extra`), damit nichts verloren geht, wenn eine ältere Version die Datei schreibt.

### 2.2 Datei `<Datenordner>/aktionen.json` (neu, `format_version` 1)

```json
{
  "format_version": 1,
  "enabled": false,
  "queues": [ { "id": "UUID", "name": "Alerts", "blocking": true } ],
  "groups": [ { "id": "UUID", "name": "Stammgäste", "users": [ { "user_id": "…", "login": "…" } ] } ],
  "actions": [
    {
      "id": "UUID", "name": "Begrüssung", "group": "Begrüssung", "enabled": true,
      "queue": "UUID oder null", "exclude_from_history": false,
      "random_action": false, "concurrent": false, "always_run": false,
      "triggers": [ { "id": "UUID", "type": "twitch.first_message", "enabled": true } ],
      "steps": [
        { "id": "UUID", "type": "user_info", "enabled": true, "login": "%userName%" },
        { "id": "UUID", "type": "chat_send", "enabled": true, "text": "Willkommen, %user%!", "use_bot": true, "fallback": true, "reply": false },
        { "id": "UUID", "type": "overlay_source", "enabled": true, "overlay": "UUID", "source": "UUID",
          "mode": "show", "text": null, "image": "%targetUserProfileImageUrl%", "duration_ms": 8000, "wait": true }
      ]
    }
  ],
  "commands": [
    { "id": "UUID", "name": "Discord", "enabled": true, "group": "Info", "commands": ["!discord", "!dc"],
      "mode": "basic", "location": "start", "case_sensitive": false,
      "ignore_bot_account": true, "ignore_internal": true, "sources": ["twitch_message"],
      "global_cooldown_s": 5, "user_cooldown_s": 0, "persist_counter": false, "persist_user_counter": false,
      "permissions": { "grant": "allow", "roles": [], "groups": [], "users": [] } }
  ],
  "timers": [
    { "id": "UUID", "name": "Info alle 20 min", "enabled": true, "repeat": true,
      "interval_s": 1200, "random": false, "random_min_s": 0, "random_max_s": 0, "lines": 5, "only_live": true }
  ]
}
```

- `enabled` ist der Hauptschalter «Chat-Bot aktiv», beim ersten Start **aus**.
- `queue: null` und die UUID aus lauter Nullen heissen beide «Standard» (nicht blockierend), wie bei Streamer.bot.
- Keine Zugangsdaten in der Datei.

### 2.3 Aktion

| Feld | Streamer.bot | In P14 |
|---|---|---|
| `id`, `name`, `group`, `enabled` | gleich | ja; `group` nur zum Ordnen in der Liste |
| `queue` | `queue` | ja |
| `exclude_from_history` | `excludeFromHistory` | ja (Lauf nicht im Verlauf) |
| `random_action` | `randomAction` | Feld gespeichert, Ablauf: nur einen zufälligen Schritt nach `weight` (kommt bei dem Nutzer nicht vor; gebaut, weil klein) |
| `concurrent` | `concurrent` | Feld gespeichert, nicht ausgeführt (kommt nicht vor). Der Editor zeigt es nicht; ein Lauf mit `concurrent: true` meldet im Verlauf «gleichzeitig nicht unterstützt, nacheinander ausgeführt» |
| `always_run` | `alwaysRun` | ja (läuft auch bei angehaltener Warteschlange) |
| `triggers[]`, `steps[]` | `triggers`, `subActions` | ja |

### 2.4 Schritte

Gemeinsame Felder: `id`, `type`, `enabled`, `weight` (Zufall), bei Gruppen und Zweigen `steps`. Platzhalter (2.9) gelten in allen Textfeldern.

| Typ | Streamer.bot (Nr.) | Felder | In P14 |
|---|---|---|---|
| `comment` | Kommentar (1009) | `text`, `color` | ja, tut nichts |
| `group` | Gruppe (99900) | `name`, `random`, `steps` | ja |
| `if` | If/Else (120) mit Zweigen `if_true` (99901) und `if_false` (99902) | `input` (Text mit Platzhaltern), `operation`, `value`, `auto_type`; genau zwei Kinder `if_true` und `if_false` mit je `steps` | ja; Vergleiche: gleich, ungleich, gleich ohne Gross-/Kleinschreibung, ungleich ohne, enthält, kleiner, grösser, leer, existiert nicht. Mit `auto_type` werden Zahlen als Zahlen verglichen. Regex später |
| `break` | Break (124) | – | ja, beendet den Lauf ohne Fehler |
| `delay` | Delay (1002) | `value_ms`, `max_value_ms`, `random` | ja, «Pause», höchstens 1 h |
| `run_action` | Run Action (4) | `action_id`, `immediately` (Streamer.bot schreibt `runImmedately`) | ja; «sofort» läuft im selben Lauf mit denselben Argumenten, sonst als eigener Lauf in der Warteschlange der Ziel-Aktion |
| `set_argument` | Set Argument (123) | `name`, `value`, `auto_type` | ja |
| `global_get` | Global (Get) (121) | `name`, `scope` (`global`, `user`), `user` (`trigger` = auslösende Person, `target` = Ziel aus «Benutzerinfo»), `persisted`, `destination` (Argument), `default` | ja |
| `global_set` | Global (Set) (122) | `name`, `scope`, `user`, `persisted`, `operation` (`set`, `increment`, `decrement`, `delete`), `value` | ja, «Variable setzen» bzw. «Zähler» |
| `chat_send` | Send Message to Channel (10) | `text`, `use_bot`, `fallback` (ohne Bot als Kanal senden), `reply` (Antwort auf `%msgId%`) | ja (4.1) |
| `user_info` | Get User Info for Target (50) | `login` (Text, ein «@» am Anfang wird entfernt) | ja (4.8) |
| `play_sound` | Play Sound (1) | `file` (Verweis `media/…`), `volume` (0 bis 200 %), `wait` (bis zum Ende warten) | ja (4.3) |
| `overlay_source` | – (eigener Schritt; bei Streamer.bot OBS 30, 38, 39) | `overlay`, `source`, `mode` (`show`, `hide`, `toggle`), `text`, `image`, `duration_ms` (0 = bleibt), `wait` | ja (4.4) |
| `obs_source_visible` | OBS Set Source Visibility (30) | `scene_name`, `source_name`, `state` (`on`, `off`, `toggle`) | ja (4.5) |
| `twitch_shoutout` | Nummer in Schritt 0 | `login` | ja (4.9) |
| `twitch_announce` | Twitch Announcement (23) | `text`, `color`, `use_bot` | ja (4.9) |
| `script` | Execute C# Code (99999) | `language`, `code` (Platzhalter für später) | **nein:** wird gelesen und gespeichert, im Editor grau «Skript-Schritt nicht verfügbar», nie ausgeführt, nicht anlegbar |
| `unsupported` | jeder andere Typ | `sb_type` (Nummer), `original` (unverändertes JSON) | **nein:** für den Import, immer aus, im Editor «nicht unterstützt: <Typ>» |

Später als eigene Schritte (nicht P14): Kategorie setzen (16), Umfrage (561), Vorhersage (568), Timeout (20), Clip (18), OBS Szene setzen (34), Filter (35), Text (39), Countdown, Watchtime.

### 2.5 Auslöser

Gemeinsame Felder: `id`, `type`, `enabled`. Die Argumente je Ereignis kommen aus P13, Abschnitt 1.4.

| Typ | Streamer.bot (Nr.) | Filter | Zusätzliche Argumente |
|---|---|---|---|
| `command` | Command Triggered (401) | `command_id` | `command`, `commandId`, `commandName`, `rawInput`, `input0` … `input9`, `msgId`, `counter`, `userCounter` |
| `twitch.chat_message` | Chat Message (133) | – | `message`, `msgId` |
| `twitch.first_message` | First Words (120) | – | `message`, `msgId`, `firstEver` |
| `twitch.follow` | Follow (101) | – | – |
| `twitch.subscribe` | Subscription (103) | `tiers[]` | `tier` |
| `twitch.resubscribe` | Resubscription (104) | `tiers[]`, `min_months` | Monate (Namen aus P13 1.4) |
| `twitch.gift_sub` | Gift Subscription (105) | `tiers[]` | Beschenkter |
| `twitch.gift_bomb` | Gift Bomb (106) | `tiers[]`, `min`, `max` (Anzahl) | Anzahl |
| `twitch.cheer` | Cheer (102) | `min`, `max` (Bits; nur `max` = genauer Wert) | `bits`, `message` |
| `twitch.raid` | Raid (107) | `min`, `max` (Zuschauer) | `viewers` |
| `twitch.stream_online` | Stream Online (Nr. Schritt 0) | `skip_at_start` (nicht auslösen, wenn das Studio erst während des Streams startet) | – |
| `twitch.stream_offline` | Stream Offline (155) | – | – |
| `timer` | Timed Action (701) | `timer_id` | `timerName` |
| `test` | Test (702) | `variables` (Argumente für den Test) | die eingetragenen |

- Überall gilt: Bereich «von–bis» schliesst beide Enden ein, wie bei Streamer.bot.
- Allgemeine Argumente in jedem Lauf: `actionName`, `actionId`, `runId`, `triggerType`, `date`, `time`, `unixtime`, `broadcastUser`, `broadcastUserName`, `broadcastUserId`, `isLive`.
- Später (nicht P14): Kanalpunkte (112), OBS-Ereignisse (14004, 14005), Anwesende Zuschauer (124), Abklingzeit eines Befehls (402).

### 2.6 Befehle

| Feld | Streamer.bot | In P14 |
|---|---|---|
| `commands[]` | Commands («one item per line») | ja, ein Befehl je Zeile; über alle aktiven Befehle eindeutig (Warnung beim Speichern) |
| `mode` | Mode | `basic`; `regex` wird gespeichert, aber nicht ausgeführt (Warnung) |
| `location` | Location | `start` (erstes Wort gleich dem Befehl, `!d` trifft nicht `!discord`), `exact` (ganze Nachricht), `anywhere` (Wort irgendwo) |
| `case_sensitive` | Case Sensitive | ja, Standard aus |
| `ignore_bot_account` | Ignore Bot Account | ja, Standard an |
| `ignore_internal` | Ignore Internal Messages | ja, Standard an: Nachrichten, die das Studio selbst gesendet hat (Dock «Chat», Schritte), lösen nichts aus |
| `sources` | Sources | `twitch_message`; weitere (YouTube) später |
| `global_cooldown_s`, `user_cooldown_s` | Global/User Cooldown | ja |
| `persist_counter`, `persist_user_counter` | Persist Counter | ja: Zähler `counter`, `userCounter` überstehen einen Neustart (in `variablen.json`) |
| `permissions` | Permissions | `grant` (`allow` oder `deny`), `roles` (`subscriber`, `vip`, `moderator`), `groups` (eigene Gruppen), `users` (`user_id`). Leere Liste bei `allow` = alle |

- **Der Kanal darf immer und hat keine Abklingzeit** («The Broadcaster Account is always exempt», Streamer.bot).
- Abgewiesene Befehle (Rechte, Abklingzeit, aus) erscheinen im Verlauf mit Grund, ohne Antwort im Chat.
- Ein Befehl löst die Aktionen aus, die einen Auslöser `command` mit seiner `command_id` haben. Der Editor legt beim Anlegen eines Befehls aus einer Aktion heraus beides in einem Schritt an.

### 2.7 Zeitnachrichten

- Felder wie Streamer.bot: `name`, `enabled`, `repeat` (sonst einmal, danach aus), `interval_s` (mindestens 60), `random` mit `random_min_s`/`random_max_s`, `lines` (so viele Chat-Nachrichten müssen seit dem letzten Mal gekommen sein; beide Bedingungen gelten).
- Eigene Erweiterung `only_live` (Standard an): zählt nur, solange `twitch_status.live` gilt (P13 2.5). Streamer.bot hat das Feld nicht; der Import setzt es auf aus.
- «Beim Streamstart» ist kein Feld der Zeitnachricht, sondern ein zweiter Auslöser `twitch.stream_online` an derselben Aktion, wie in den Daten des Nutzers («BOT Vorstellung»).

### 2.8 Warteschlangen

- `name`, `blocking`. Zur Laufzeit dazu: angehalten, wartende Läufe, laufender Lauf.
- «Standard» ist nicht blockierend und nicht löschbar.
- Blockierend: Läufe derselben Warteschlange laufen nacheinander in der Reihenfolge ihres Eintreffens. Das gilt auch für Läufe, die `run_action` ohne «sofort» anstösst.
- Anhalten hält neue Läufe zurück (ausser `always_run`); Fortsetzen startet sie. «Leeren» verwirft die wartenden mit Eintrag im Verlauf.

### 2.9 Argumente, Variablen, Platzhalter

- **Argumente** gelten nur im Lauf. Quellen: Auslöser (2.5), allgemeine (2.5), Schritte (`set_argument`, `global_get`, `user_info`).
- **Variablen:**
  - `scope` `global` oder `user` (je Zuschauer über `user_id`, mit `login` zur Anzeige).
  - flüchtig (nur im Speicher, weg beim Neustart des Kerns) oder dauerhaft.
  - Dauerhafte stehen in `<Datenordner>/variablen.json` (`format_version` 1, `global` und `users`), geschrieben verzögert über `SaveScheduler`. Werte sind Text oder Zahl.
  - Zähler der Befehle (`persist_counter`) liegen in derselben Datei.
- **Platzhalter** wie Streamer.bot:
  - `%name%` setzt das Argument ein. Ein unbekanntes bleibt wörtlich stehen, wie bei Streamer.bot.
  - `~name~` setzt eine dauerhafte globale Variable ein.
  - Nicht in P14 (kommt bei dem Nutzer nicht vor): Formate `%name:format%`, `$math()$`, `$length()$`.
- **Daten von Zuschauern** (`variablen.json`, `groups.users`, `twitch-sitzung.json` aus P13) bleiben lokal im Datenordner, nie im Protokoll, nie im Repo. Testdaten nutzen erfundene Namen.

### 2.10 Datei-Regeln

- Fehlt `aktionen.json`, legt der Kern die mitgelieferten Aktionen an (Abschnitt 7).
- Eine ungültige Datei wird gemeldet und nicht überschrieben (Muster `liste.json`). Der Bot bleibt dann aus, Änderungen werden abgewiesen.
- Verweise prüft der Kern beim Laden und beim Speichern: Warteschlange, Befehl → Aktion, `run_action`-Ziel, Overlay und Quelle, Mediendatei. Ein fehlender Verweis ist eine Warnung im Editor, der Schritt meldet ihn im Lauf.
- `dss-core --format-pruefen` kennt beide Dateien. `format_version` > 1 heisst neueres Format (Exit-Code 5).
- Speichern verzögert über `SaveScheduler`, wie bei den Overlays.

## 3. Ausführung

### 3.1 Lauf

- Bibliothek `src/actions/` (`dss_actions`): Qt Core, kein Qt Gui, kein Netz, kein `protocol.h`; bindet `dss_events` ein (Regel 9 und 14).
- Ein Lauf ist eine Zustandsmaschine im Hauptthread des Kerns, ohne blockierende Threads. Schritte mit Wartezeit (Pause, Senden, Ton mit «warten», Overlay mit «warten», Benutzerinfo, OBS) geben die Kontrolle ab und machen bei ihrer Antwort weiter.
- Schritte nach aussen gehen über Schnittstellen (`ChatSender`, `TwitchApi`, `ObsControl`, `SoundPlayer`, `OverlayLive`), die der Kern mit den Diensten verbindet und die Tests ersetzen. Die Uhr lässt sich im Test ersetzen.
- **Fehler eines Schritts** (Twitch lehnt ab, OBS-Quelle fehlt, kein Tonausgang): Eintrag im Verlauf, der Lauf geht weiter, wie bei Streamer.bot. Nur `break` und die Grenzen (3.3) beenden ihn.
- Ein abgeschalteter Schritt wird übersprungen, eine abgeschaltete Gruppe mit allem darin.

### 3.2 Reihenfolge

- Ein Ereignis startet alle aktiven Aktionen mit passendem Auslöser, in der Reihenfolge der Liste.
- Jede Aktion läuft in ihrer Warteschlange (2.8). Streamer.bot dokumentiert die Reihenfolge bei gleichzeitigen Auslösern nicht; das Studio nimmt die Reihenfolge des Eintreffens.

### 3.3 Grenzen

| Grenze | Wert | Begründung |
|---|---|---|
| Tiefe von `run_action` (sofort) | 8 | Schleifen zwischen Aktionen enden sicher |
| Schritte je Lauf | 2000 | dito |
| gleichzeitige Läufe | 32 | Ansturm (Follow-Welle) bremst den Kern nicht |
| wartende Läufe je Warteschlange | 50, ältere zuerst verworfen | Alerts stauen nicht ins Endlose |
| Dauer eines Laufs | 15 min | hängende Schritte enden |

Jede Grenze erscheint im Verlauf mit Grund.

### 3.4 Nur eine Version handelt

- Läuft der Kern der Version im Einsatz, startet der Kern der Version in Arbeit keine Läufe aus Twitch-Ereignissen, Befehlen und Zeitnachrichten. Geprüft am Socket wie bei «übrig gebliebener Socket».
- Zustand «stumm: Version im Einsatz läuft». Probeläufe mit dem Test-Knopf (3.6) gehen weiter.
- Gilt für alles nach aussen und für den Ton (sonst doppelte Antworten, doppelte Töne).

### 3.5 Verlauf

- Der Kern hält die letzten 200 Läufe im Speicher: Zeit, Aktion, Auslöser, Ergebnis (`ok`, `break`, `error`, `rejected`, `muted`, `dropped`, `limit`), Dauer und je Schritt Ergebnis und Meldung. Nicht gespeichert.
- Auf dem Fluss: `actions.run_started` und `actions.run_finished` (Kennungen, Name der Aktion, Auslöser, Ergebnis, Grund). Das Fenster bestellt sie über `subscribe_events` (P13 1.5) und holt Einzelheiten mit `action_run_details`.
- Ins Journal: Name der Aktion, Ergebnis, Fehler. Keine Chat-Texte, keine Namen von Zuschauern (wie P13).

### 3.6 Test-Knopf (Frage 3)

- «Test …» bei einer Aktion öffnet den Dialog «Test» mit den Argumenten des gewählten Auslösers, vorbelegt mit Beispielwerten (erfundene Namen).
- **Probelauf** (Standard): Schritte nach Twitch und OBS werden nicht ausgeführt; der Verlauf zeigt «Probelauf: würde senden …». Overlay, Ton, Pause und Variablen laufen echt, flüchtige Variablen in einem eigenen Satz, dauerhafte nur lesend.
- **«Echt ausführen»** (Kästchen): wie ein echter Auslöser.
- Im Unterreiter «Ereignisse» startet «Als Test ausführen …» eine Aktion mit den Argumenten eines echten Ereignisses.

## 4. Grundschritte im Einzelnen

### 4.1 Chat senden

- **Konto:** `use_bot` an = Bot-Konto (P13 2.1). Ist der Bot nicht angemeldet und `fallback` an, sendet der Kanal, sonst Fehler im Verlauf. Wie Streamer.bot (`useBot`, `fallback`).
- **Antwort:** `reply` setzt `reply_parent_message_id` auf `%msgId%` des Auslösers.
- **Grenze im Kern je Konto:** höchstens 1 Nachricht je Sekunde und 20 je 30 s (gleitend), Warteschlange von 10, was älter als 30 s ist, wird verworfen und im Verlauf genannt. Sicher für jede Rolle; bei Überschreitung sperrt Twitch den Bot 1 h.
- **Länge:** nach dem Einsetzen der Platzhalter auf 500 Zeichen gekürzt, nie mitten in einem Zeichen (`QTextBoundaryFinder`). Wie Twitch zählt: Schritt 0, bis dahin UTF-16-Einheiten.
- **Text am Anfang:** Texte, die mit `/` oder `.` beginnen, weist der Editor ab («Twitch-Befehle sind nicht möglich»). Ob die API sie ausführen würde: nicht geprüft.
- **Geteilter Chat:** Auslöser kommen nur aus dem eigenen Kanal (`source_broadcaster_id` gleich `null`).
- **Keine Schleifen:** Eigene Nachrichten erkennt der Kern an der `message_id` aus der Antwort (letzte 100) und zusätzlich an Absender und gleichem Text innerhalb von 10 s; sie zählen als «intern» (`ignore_internal`). Nachrichten des Bot-Kontos tragen `is_bot_account` (P13).
- **Ergebnis** auf dem Fluss: `twitch.chat_sent` (`message_id`, `account`, `requested_by`) bzw. `twitch.chat_dropped` (`code`, `message`).

### 4.2 Pause

`value_ms`, mit `random` eine Zufallszeit zwischen `value_ms` und `max_value_ms`. Über `QTimer` im Hauptthread, kein Schlafen.

### 4.3 Ton abspielen

- `file` aus der Mediathek, `volume` 0 bis 200 % (die Daten des Nutzers nutzen 50 bis 150 %), `wait` (Standard an: der Lauf wartet bis zum Ende, wie «Finish playing before continuing»).
- Mehrere Töne gleichzeitig (höchstens 8), ein neunter verdrängt den ältesten mit Eintrag im Verlauf.
- Im Dialog des Schritts «Anhören» über denselben Ausgang.

**Ausgang:** Ein eigener Zweck `aktionen` neben `video` (P12 b3, Entscheid 2026-11-09: «je Zweck oder Modul ein eigener Ausgang»):
- `kAudioPurposes` bekommt `{"aktionen", "ton-aktionen"}`: Abschnitt `[ton-aktionen]` in `kern.ini`, `audio_routes.aktionen` in `settings` und `update_settings`, `audio_outputs.routes.aktionen`.
- Dock «Steuerung»: neue Zeile «Ton Chat-Bot» unter «Ton Videos», mit Auswahl des Ausgangs und Zustand (ohne Versatz, den braucht nur der Ton zum Bild).
- Standard: kein Ausgang gewählt. Der Schritt meldet dann «kein Ausgang für Ton Chat-Bot gewählt».

**Was fehlt für den Ton (Bau in Abschnitt c):**
1. Medienart «Ton» in der Mediathek: Endungen `mp3`, `wav`, `ogg`, `m4a` (wie Play Sound bei Streamer.bot), Dock «Medien» zeigt Ton mit Symbol und Länge.
2. Decoder für Dateien ohne Bildspur prüfen und falls nötig anpassen (`AudioDecoder`, libavcodec). Testdateien wie in P12a (`tests/daten/ton/LIESMICH.md`).
3. Freie Stimme im Mischer: startet beim nächsten Abruf ohne Uhr einer Seite, Lautstärke, meldet ihr Ende zurück; gleiche Regeln für den Echtzeit-Thread wie P12 (keine Sperre, kein Speicher, kein Systemaufruf in `render()`). Werte über 100 % werden auf ±1,0 begrenzt.
4. Zweiter `pw_stream` für den Zweck `aktionen`. Er ist nur offen, solange ein Ausgang gewählt ist. Mehrlast messen (P12: 2,75 Prozentpunkte eines Kerns für den Ton der Videos, Grenze 3).
5. Ton von **Videos** in einem Overlay, das eine Aktion einblendet (zum Beispiel ein Alert-Video), kommt weiter über «Ton Videos» (P12) und nur, wenn die Browserquelle in OBS im Programm ist. Das bleibt so.
6. Mit `DSS_CORE_AUDIO=OFF` meldet der Schritt «ohne Ton gebaut».

### 4.4 Overlay-Quelle zeigen oder verstecken (Frage 2)

- **Laufzustand:** Eine Aktion ändert eine Quelle nur «jetzt»: Sichtbarkeit, Text, Bild. Der Kern hält das im Speicher je Overlay und Quelle, schreibt es **nie in die Overlay-Datei** und vergisst es beim Neustart. So bleibt die Datei sauber, und nach einem Absturz ist nichts halb eingeblendet.
- **Beide Zeichner zeigen ihn (K-07):**
  - Seite: neues Ereignis `event: live` auf `…/events` mit den geänderten Feldern je Quelle; `web/overlay.js` legt sie über das Overlay.
  - Editor: neue Nachricht `overlay_live` (Kern → Fenster); die Leinwand zeichnet den Laufzustand mit. Dock «Quellen» markiert betroffene Quellen mit «live» (Tooltip: Aktion), Kontextmenü «Laufzustand zurücksetzen».
- **Felder des Schritts:** `overlay`, `source` (Auswahl aus den Overlays des Studios), `mode` (`show`, `hide`, `toggle`), `text` (nur Textquellen, mit Platzhaltern), `image` (nur Bildquellen: Adresse eines Profilbilds als Argument, zum Beispiel `%targetUserProfileImageUrl%`, oder eine Datei der Mediathek), `duration_ms` (danach fällt der ganze Laufzustand der Quelle weg, die Quelle zeigt wieder den Stand des Editors), `wait` (Lauf wartet die Dauer ab).
- **Profilbilder:** Der Kern lädt das Bild selbst (HTTPS, nur vom Bildserver von Twitch, Name in Schritt 0, höchstens 2 MiB, nur PNG oder JPEG), legt es unter `<Datenordner>/live-bilder/<SHA-256>.<endung>` ab (höchstens 200 Dateien, älteste zuerst gelöscht, beim Start geleert) und liefert es unter `/o/<adresse>/<token>/live/<SHA-256>.<endung>` nur an Overlays, deren Laufzustand es benutzt. OBS lädt also nichts von Twitch, und Editor und Seite zeigen dieselbe Datei.
- Ein Video, das per Laufzustand eingeblendet wird, beginnt von vorne (wie heute beim Einblenden).
- **Mitgeliefertes Overlay «Chat-Bot»** (Abschnitt 7) hat die Quellen für Alerts, Begrüssung und Shoutout. Der Nutzer gestaltet es im Editor wie jedes Overlay und legt es einmal als Browserquelle in OBS an (E-29).

### 4.5 OBS-Quelle ein/aus

- `scene_name`, `source_name`, `state` (`on`, `off`, `toggle`). Namen wie bei Streamer.bot, damit der Import ohne Umsetzen geht.
- `dss_obs` bekommt `GetSceneItemList`, `GetSceneItemId`, `GetSceneItemEnabled` und `SetSceneItemEnabled`. Der Editor holt die Szenen und Quellen für die Auswahl über `obs_scene_items` (auf Anfrage).
- Ohne Verbindung zu OBS oder bei fehlender Quelle: Fehler im Verlauf, der Lauf geht weiter.

### 4.6 Variable und Zähler

`global_set` mit `set`, `increment`, `decrement`, `delete`; `global_get` kopiert in ein Argument. Je Zuschauer über `scope: user` und `user` (`trigger` oder `target`). Im Editor heissen die Schritte «Variable setzen», «Zähler ändern» und «Variable lesen».

### 4.7 Wenn/Dann, Abbruch, Gruppe

- «Wenn» mit Vergleich (2.4), Zweige «Dann» und «Sonst», beliebig tief.
- «Abbruch» beendet den Lauf (Ergebnis `break`).
- «Gruppe» fasst Schritte zusammen (Ein- und Ausschalten im Ganzen, Ordnung), wahlweise «zufällig einen» nach `weight`.

### 4.8 Benutzerinfo holen

- `login` mit Platzhaltern (zum Beispiel `%input0%` oder `%userName%`), «@» am Anfang wird entfernt.
- Setzt `targetUser`, `targetUserName`, `targetUserId`, `targetUserProfileImageUrl` (belegt in den Daten des Nutzers), dazu `targetDescription` und `game` (letzte Kategorie des Ziels, `%game%` belegt). Namen ausser den belegten: Schritt 0.
- Unbekannter Name: Argument `targetUserFound` = `false`, kein Fehler. So kann ein «Wenn» danach reagieren.

### 4.9 Shoutout und Ansage

- `twitch_shoutout`: `POST /helix/chat/shoutouts` mit dem Token des Kanals. Lehnt Twitch ab (Grenze, nicht live), steht der Grund im Verlauf, der Lauf geht weiter.
- `twitch_announce`: Ansage mit Farbe, als Bot (wenn Moderator) oder Kanal; Grenze einmal je 2 s im Kern.

### 4.10 Aktion ausführen

`run_action` mit «sofort» (im selben Lauf, Argumente gehen hin und zurück) oder eigenständig (eigener Lauf in der Warteschlange des Ziels). Grenze der Tiefe 3.3.

## 5. Kern und Schnittstelle

- **Kern:** `ActionService` in `CoreService` verbindet Fluss, Auslöser, Läufe und die Dienste (Twitch, OBS, Ton, Overlays). Schalter `DSS_CORE_ACTIONS` (Standard ON). Begründung: Der Bot handelt selbständig in einem öffentlichen Chat und in OBS, riskanter Teil nach Regel 8. OFF: kein Aktionssystem, der Reiter zeigt nur Chat und Ereignisse.
- **Kern an Fenster** (nach `hello` und bei jeder Änderung an alle):
  - `actions_config`: ganzer Inhalt von `aktionen.json` und `file_error`;
  - `actions_status`: `state` (`off`, `running`, `muted`, `error`), `message`, Warteschlangen mit Länge und Zustand, Konten (`channel`, `bot`) mit «kann senden»;
  - `variables` (auf Bestellung, wie Ereignisse): Liste der Variablen, ohne Werte flüchtiger Testläufe.
  - `overlay_live` (4.4) an alle.
- **Fenster an Kern**, jeweils mit `request_id` und Antwort im Muster von `change_result`:
  - `actions_set_enabled`;
  - `action_put`, `action_remove`, `action_copy` (neue Kennungen für Aktion, Auslöser und Schritte);
  - `command_put`, `command_remove`, `timer_put`, `timer_remove`, `queue_put`, `queue_remove`, `queue_control` (`pause`, `resume`, `clear`), `group_put`, `group_remove`;
  - `variable_set`, `variable_remove`;
  - `action_test` (`action_id`, `arguments`, `dry_run`), `action_run_details` (`run_id`);
  - `chat_send` (`text`, `account`, `reply_to`) für das Dock «Chat»;
  - `overlay_live_reset` (`overlay`, `source`);
  - `obs_scene_items` (Szenen und Quellen in OBS für die Auswahl).
- **Ganze Objekte statt Einzelfelder:** Der Editor schickt bei jeder Änderung die ganze Aktion (`action_put`). Aktionen sind klein; so gibt es keine Teil-Änderungen mit Konflikten.
- **Version der Schnittstelle:** 15 mit Abschnitt b; jeder weitere Abschnitt, der Pflichtfelder ändert, +1. Der Bericht nennt die Endversion.
- **`dss-cli`:** `aktionen` (Liste), `aktion-setzen DATEI.json`, `aktion-loeschen ID`, `aktion-test ID [--echt] [ARG=WERT …]`, `verlauf [--folgen]`, `variablen`, `chat-senden [--bot] TEXT` (nur im Handtest mit Erlaubnis aus Frage 4). Werkzeuge für Lotse.

## 6. Bedienorte im Studio

Der «Entwurf UI Chat-Bot (folgt)» gleicht sich an diese Orte an. Aussehen «Yami», Open Sans. Vor Abschnitt e führt der Agent `ui-consistency:finding-patterns` durch und hält die Checkliste im Bericht fest.

| Ort | Art | Inhalt |
|---|---|---|
| Kopfbereich | Knopf (P13) | Twitch-Knopf: Farbe nach dem Kanal, Tooltip mit Kanal und Bot. Klick öffnet Einstellungen, Bereich «Twitch» |
| Kopfbereich | Modul-Reiter | «Chat-Bot» (gibt es schon) |
| Dialog «Einstellungen», Bereich «Twitch» | Dialog (P13) | Client-ID, Konten «Kanal» und «Bot», Anmelden/Abmelden, Rechte, Abos |
| Reiter «Chat-Bot», Kopfzeile | Teil des Reiters | Schalter «Chat-Bot aktiv», Zustand («läuft», «aus», «stumm: Version im Einsatz läuft», «Twitch nicht verbunden», «Bot nicht angemeldet: sendet als Kanal») |
| Reiter «Chat-Bot», Unterreiter «Aktionen» | Teil des Reiters | links Liste der Aktionen nach Gruppen (aufklappbar) mit Suchfeld, Kästchen «aktiv» je Zeile, Knöpfe «Neu», «Kopieren», «Löschen», «Mitgelieferte wiederherstellen …»; rechts oben Felder der Aktion (Name, Gruppe, Warteschlange, aktiv) und «Test …»; rechts Mitte «Auslöser» (Liste, «Hinzufügen …», «Bearbeiten …», «Entfernen», aktiv); rechts unten «Schritte» als Baum mit Einrückung für Gruppe, Dann und Sonst, Knöpfe «Schritt hinzufügen» (Menü nach Art), «Bearbeiten …», «Hoch», «Runter», «Kopieren», «Löschen», Ziehen mit der Maus, Kästchen «aktiv» je Schritt (abgeschaltete grau) |
| Unterreiter «Befehle» | Teil des Reiters | Tabelle: aktiv, Befehle, Aktion, Wer darf, Abklingzeit Kanal und je Person; «Neu», «Bearbeiten …», «Löschen», «Benutzergruppen …» |
| Unterreiter «Zeitnachrichten» | Teil des Reiters | Tabelle: aktiv, Name, Abstand, Chat-Zeilen, nur im Stream, Aktion; «Neu», «Bearbeiten …», «Löschen» |
| Unterreiter «Warteschlangen» | Teil des Reiters | Tabelle: Name, blockierend, wartend, läuft, angehalten; «Neu», «Löschen», «Anhalten»/«Fortsetzen», «Leeren» |
| Unterreiter «Variablen» | Teil des Reiters | zwei Tabellen «Für alle» (Name, Wert, dauerhaft) und «Je Zuschauer» (Zuschauer, Name, Wert), Suchfeld, «Ändern …», «Löschen» |
| Unterreiter «Verlauf» | Teil des Reiters | Läufe (Zeit, Aktion, Auslöser, Ergebnis, Dauer), aufklappbar mit Schritten und Meldungen, Filter «nur Fehler» |
| Unterreiter «Ereignisse» | Teil des Reiters | Liste aus P13 (Zeit, Art, Name, Text), Kontextmenü «Als Test ausführen …» |
| Dock «Chat» (neu) | Dock | Nachrichten (Zeit, Name in Chatfarbe, Text; Ereignisse als graue Zeilen, Nachrichten des Bots erkennbar), Eingabe mit Zähler bis 500, Auswahl «Senden als: Bot / Kanal», «Senden», Kontextmenü «Antworten». Standard rechts im Reiter «Chat-Bot»; über «Docks → Chat» auch im Reiter «Overlays» |
| Editor-Docks im Reiter «Chat-Bot» | – | Overlays, Quellen, Medien, Eigenschaften und Steuerung werden im Reiter «Chat-Bot» ausgeblendet und beim Zurückwechseln wiederhergestellt; Anordnung je Reiter in `fenster.ini` (Frage 1) |
| Dialog «Schritt bearbeiten» | Dialog | je Art eigener Inhalt; Liste der Platzhalter des gewählten Auslösers zum Einfügen; bei Ton «Anhören»; bei Overlay Auswahl Overlay und Quelle; bei OBS Auswahl Szene und Quelle aus OBS |
| Dialog «Auslöser» | Dialog | Art (Befehl, Twitch-Ereignis, Zeitnachricht, Test) und Filter (Stufen, von–bis) |
| Dialog «Befehl bearbeiten» | Dialog | Name, Befehle (eine Zeile je Befehl), Ort, Gross-/Kleinschreibung, Bot übergehen, Wer darf (Alle, Abonnenten, VIPs, Moderatoren, Gruppen, einzelne Zuschauer; erlauben oder verbieten), Abklingzeiten, Zähler dauerhaft, Aktion |
| Dialog «Zeitnachricht bearbeiten» | Dialog | Name, Abstand, Zufall von–bis, Chat-Zeilen, wiederholen, nur im Stream, Aktion |
| Dialog «Benutzergruppen» | Dialog | Gruppen mit Zuschauern (Login eingeben, der Kern prüft ihn bei Twitch) |
| Dialog «Test» | Dialog | Argumente des Auslösers, Kästchen «echt ausführen» (Standard aus) |
| Dock «Steuerung» (Reiter «Overlays») | Dock | neue Zeile «Ton Chat-Bot»: Ausgang, Zustand |
| Dock «Quellen» (Reiter «Overlays») | Dock | Marke «live» bei Quellen mit Laufzustand, Kontextmenü «Laufzustand zurücksetzen» |
| Dock «Medien» | Dock | Tondateien mit Symbol und Länge; «Hinzufügen» nimmt `mp3`, `wav`, `ogg`, `m4a` |
| Dock «Overlays» | Dock | mitgeliefertes Overlay «Chat-Bot» im Ordner «Chat-Bot» |

- Die Logik der Ansichten liegt in `dss_common` und ist ohne Fenster getestet (Regel 9). Keine Layout-Änderungen in Ereignis-Behandlern.

## 7. Mitgelieferte Aktionen

Der Kern legt sie an, wenn `aktionen.json` fehlt, aus der Vorlage `src/actions/vorlage/aktionen.json` (im Programm eingebaut). «Mitgelieferte wiederherstellen …» legt fehlende wieder an (erkannt an der Kennung der Vorlage), ändert nichts an vorhandenen. Texte sind allgemein und ohne echte Namen; der Nutzer passt sie an.

| Aktion | Auslöser | Schritte (Kurzform) | Warteschlange | Beim Anlegen |
|---|---|---|---|---|
| Info: `!befehle`, `!discord`, `!regeln`, `!specs` | je ein Befehl, Abklingzeit Kanal 10 s | Chat als Bot | Standard | `!befehle` aktiv; die anderen aus, bis der Nutzer seinen Text einträgt (Inhalt unbekannt) |
| Zeitnachricht «Info» | Zeitnachricht alle 20 min, 5 Zeilen, nur im Stream | Chat als Bot («Alle Befehle: !befehle») | Standard | aktiv |
| Vorstellung beim Streamstart | `twitch.stream_online` (nicht beim Start des Studios im laufenden Stream) | Pause 60 s → Chat als Bot | Standard | aktiv |
| Begrüssung | `twitch.first_message` | Wenn `%role%` = 4 oder `%isBotAccount%` → Abbruch; Benutzerinfo `%userName%`; Chat als Bot «Willkommen, %user%!»; Overlay «Chat-Bot»: «Begrüssung Bild» mit Profilbild und «Begrüssung Text» mit Name, 8 s, warten | «Meldungen» (blockierend) | aktiv |
| Shoutout ausführen | keiner (wird aufgerufen) | Benutzerinfo `%shoutoutLogin%`; Wenn nicht gefunden → Chat «Unbekannt» + Abbruch; Shoutout; Chat als Bot «Schaut bei %targetUser% vorbei, zuletzt: %game%»; Overlay «Shoutout Bild»/«Shoutout Text» 10 s, warten | «Meldungen» | aktiv |
| Shoutout per Befehl | Befehl `!so`, nur Moderatoren | Argument `shoutoutLogin` = `%input0%` → Aktion «Shoutout ausführen» (sofort) | Standard | aktiv |
| Shoutout bei Raid | `twitch.raid` (ab 1 Zuschauer) | Argument `shoutoutLogin` = `%userName%` → «Shoutout ausführen» (sofort) | Standard | aktiv |
| First Chatters | `twitch.first_message` | Wenn Kanal oder Bot → Abbruch; Zähler `firstChatterPlace` (dauerhaft, global) +1; Wenn > 3 → Abbruch; je Platz Zähler `firsts`/`seconds`/`thirds` des Zuschauers (dauerhaft) +1, lesen, Chat «%user% ist heute Nummer 1 (schon %firsts% Mal)» | «Erste Chatter» (blockierend) | aktiv |
| First Chatters zurücksetzen | `twitch.stream_offline` | `firstChatterPlace` = 0 | «Erste Chatter» | aktiv |
| Chat-Sound (Vorlage) | Befehl `!sound`, Abklingzeit Kanal 30 s | Ton abspielen (keine Datei gewählt) | Standard | aus; der Nutzer kopiert sie je Ton und wählt Datei und Befehl |
| Alert Follow | `twitch.follow` | Benutzerinfo `%userName%`; Overlay «Alert Bild» (Profilbild), «Alert Text» «%user% folgt jetzt!», 6 s, warten; Ton (Schritt aus, bis eine Datei gewählt ist) | «Alerts» (blockierend) | aktiv |
| Alert Sub | `twitch.subscribe`, `twitch.resubscribe` | wie Follow, Text «%user% hat abonniert!» | «Alerts» | aktiv |
| Alert Geschenk | `twitch.gift_bomb` | wie Follow, Text mit Anzahl | «Alerts» | aktiv |
| Alert Cheer | `twitch.cheer` (ab 1 Bit) | wie Follow, Text «%user%: %bits% Bits» | «Alerts» | aktiv |
| Alert Raid | `twitch.raid` (ab 1 Zuschauer) | wie Follow, Text «Raid von %user% mit %viewers% Zuschauern» | «Alerts» | aktiv |

- **Chat im Studio lesen und schreiben** ist keine Aktion, sondern das Dock «Chat» (6).
- **Overlay «Chat-Bot»** (1920 × 1080, Ordner «Chat-Bot»): Quellen «Alert Bild», «Alert Text», «Begrüssung Bild», «Begrüssung Text», «Shoutout Bild», «Shoutout Text», alle unsichtbar. Bildquellen nutzen ein neutrales Platzhalterbild (vom Agenten erzeugt, kein Markenlogo), Textquellen die mitgelieferte Schrift. Lage und Gestaltung sind vorläufig; sie folgen dem «Entwurf UI Chat-Bot (folgt)» und der Nutzer ändert sie im Editor.
- Argumentnamen wie `%bits%`, `%viewers%`, `%isBotAccount%` sind vorläufig, bis Schritt 0 von P13 sie bestätigt.
- Der Hauptschalter «Chat-Bot aktiv» ist beim ersten Start aus.

## 8. Bau-Abschnitte

Jeder Abschnitt endet mit Bau ohne Warnungen und `ctest` (Stufe «Klein») durch den Agenten und einer Prüfung durch Lotse (Bau, `ctest`, Rückmeldung lesen). Dauer: Sitzungen des Agenten à 4 bis 5 h.

**Schritt 0 (Agent, nur lesen, zu Beginn von a):**
- Quellen unten lesen und bestätigen: Shoutout (Grenzen, nur live), Benutzerinfo und Kanalinfo (Felder, Rechte), Bildserver der Profilbilder, Zählung der 500 Zeichen, `/` am Anfang, Echo eigener Nachrichten, Typnummern von Shoutout und Stream Online bei Streamer.bot, Argumentnamen (zusammen mit P13 Schritt 0).
- Weicht etwas ab, das einen Grundschritt trägt: Halt und Meldung an Lotse.

### a – Kern und Modell (3 Commits, ~1 Sitzung)

1. `src/actions/`: Modell (2.2 bis 2.8), `aktionen.json` lesen und schreiben, unbekannte Felder (`extra`), `script` und `unsupported` bewahren, Typtabellen mit Nummern von Streamer.bot. Test `tst_aktionsformat` mit `tests/daten/aktionen/` (gültig, ungültig, neueres Format, unbekannte Typen und Felder bleiben erhalten, LIESMICH).
2. Platzhalter und Variablen: `%arg%`, `~global~`, `variablen.json`, je Zuschauer, Zähler. Tests `tst_platzhalter` (unbekannte bleiben stehen, Emoji, Kürzen an der Zeichengrenze) und `tst_variablen`.
3. Verweise prüfen (2.10), `dss-core --format-pruefen` für beide Dateien. Test in `tst_aktionsformat` und `tst_formatpruefen` (falls vorhanden, sonst im bestehenden Test der Option).

**Prüfung a:** Stufe «Klein». Lotse: Bau, `ctest`.

### b – Ausführung (3 Commits, ~1,5 Sitzungen)

1. Ablauf (3.1 bis 3.3): Lauf als Zustandsmaschine, Gruppen, Wenn/Dann, Abbruch, Pause, Variablen, Argument setzen, `run_action` sofort und eigenständig, Zufall, Warteschlangen, Grenzen; Schnittstellen nach aussen mit Ersatz im Test, ersetzbare Uhr. Test `tst_aktionsablauf` ohne Netz.
2. Auslöser: Befehle (Ort, Gross-/Kleinschreibung, Rechte mit Rollen, Gruppen und Zuschauern, Kanal ausgenommen, Abklingzeiten, Zähler, Bot und intern übergehen), Ereignisse mit Filtern, Zeitnachrichten (Abstand, Zufall, Zeilen, nur im Stream), Test. Test `tst_ausloeser`.
3. Kern: `ActionService`, Schnittstelle 15 (5.), Verlauf (3.5), Stumm-Regel (3.4), `dss-cli`, Schalter `DSS_CORE_ACTIONS`, Anlegen einer leeren Datei (Vorlage folgt in f). Tests `tst_protocol`, `tst_kernverbindung`, neu `tst_kernaktionen` (Kern als eigener Prozess gegen den Twitch-Testserver aus P13: Befehl → Lauf → Verlauf; Änderung über den Socket gespeichert, übersteht Neustart; ungültige Datei nicht überschrieben; stumm bei zweitem Socket «Einsatz»; **kein Testtoken und kein Chat-Text im Protokoll**).

**Prüfung b:** Stufe «Klein», dazu 20 Wiederholungen von `tst_aktionsablauf` und `tst_kernaktionen` (Zeitgrenzen). Lotse: Bau, `ctest`, `dss-cli aktion-test` mit Probelauf.

### c – Schritte nach aussen (3 Commits, ~1,5 Sitzungen)

1. Twitch: `TwitchApi` auf `helixclient` (Chat senden je Konto mit Grenzen, Antwort, Kürzen, Schleifenschutz; Benutzerinfo; Kanalinfo; Shoutout; Ansage), Ereignisse `twitch.chat_sent`/`twitch.chat_dropped`; Schritte `chat_send`, `user_info`, `twitch_shoutout`, `twitch_announce`. Testserver erweitert (`/helix/chat/messages` mit `is_sent`, `drop_reason`, 401, 403, 422, 429; Echo vor oder nach der Antwort; `/helix/users`, `/helix/channels`, `/helix/chat/shoutouts`, `/helix/chat/announcements`). Test `tst_twitchschreiben`.
2. OBS: neue Anfragen in `dss_obs` (4.5), Schritt `obs_source_visible`, `obs_scene_items`. Tests `tst_obsquellen` erweitert (Testserver), `tst_kernaktionen`.
3. Ton: Medienart «Ton», Zweck `aktionen`, freie Stimme, zweiter Strom, Schritt `play_sound`, Zeile «Ton Chat-Bot». Tests `tst_medienliste`, `tst_tondecoder` (Tondateien), `tst_tonmischer` (freie Stimme, 8 gleichzeitig, Begrenzung), `tst_kernton` (Ersatzausgang).

**Prüfung c:** Stufe «Klein», `scripts/tonpruefung.py` (unverändert bestanden), Mehrlast mit `scripts/tonleistung.py` mit gewähltem Ausgang «Ton Chat-Bot» (Messwert in den Bericht). Lotse: Bau, `ctest`.

### d – Overlays (2 Commits, ~1 Sitzung)

1. Laufzustand im Kern (4.4), Profilbilder (Laden, Ablage, Grenzen), Overlay-Server (`event: live`, `…/live/…`), `web/overlay.js`, Leinwand des Editors, Nachricht `overlay_live`. Tests `tst_overlayserver`, `tst_browserquelle` (Laufzustand sichtbar in Qt WebEngine), `tst_bildvergleich` (neuer Fall «Laufzustand» mit Text und Bild, beide Zeichner innerhalb der Grenze).
2. Schritt `overlay_source`, Marke «live» und «Laufzustand zurücksetzen» im Dock «Quellen», `overlay_live_reset`. Tests `tst_kernaktionen`, `tst_fensterbearbeiten`.

**Prüfung d:** Stufe «Klein». Lotse: Bau, `ctest`, Bildschirmfoto der Leinwand mit Laufzustand (Scratchpad).

### e – Editor (3 Commits, ~2 Sitzungen; erst nach Abnahme des «Entwurf UI Chat-Bot (folgt)»)

0. `ui-consistency:finding-patterns`, Checkliste im Bericht.
1. Ansichtslogik in `dss_common` (`actionsview.{h,cpp}`): Baum der Schritte, Hoch/Runter/Ziehen über Ebenen, Kopieren mit neuen Kennungen, Prüfen vor dem Senden, Platzhalter je Auslöser, Zusammenfassung eines Schritts als eine Zeile. Test `tst_aktionsansicht` ohne Fenster.
2. Reiter «Chat-Bot» mit Kopfzeile und Unterreitern «Aktionen», «Befehle», «Zeitnachrichten», «Warteschlangen», Dialoge (6.), Anordnung der Docks je Reiter. Test neu `tst_fensterchatbot`.
3. Dock «Chat», Unterreiter «Variablen», «Verlauf», «Ereignisse» mit «Als Test ausführen …». Test `tst_fensterchatbot`.

**Prüfung e:** Stufe «Klein», `ui-consistency:verifying` durch einen getrennten Agenten. Lotse: Bau, `ctest`, Bildschirmfotos aller Unterreiter und Dialoge (Scratchpad).

### f – Mitgelieferte Aktionen (2 Commits, ~1 Sitzung)

1. Vorlage (7.), Overlay «Chat-Bot» mit Platzhalterbild, Anlegen beim ersten Start, «Mitgelieferte wiederherstellen …». Test neu `tst_mitgeliefert`: jede mitgelieferte Aktion läuft mit Test-Auslöser gegen den Testserver und ergibt die erwarteten Chat-Texte, Laufzustände, Zähler (First Chatters über drei Zuschauer und Zurücksetzen), Ablehnungen (Kanal, Bot, Rechte bei `!so`).
2. Dokumente: `01` (Chat-Bot, Entscheide), `03` («Chat-Bot»), `04` (Punkt 3, neue Punkte), `05`, `06` (`aktionen.json`, `variablen.json`, Medienart Ton, `[ton-aktionen]`, `live-bilder/`), `08` (Version, Abschnitte «Aktionssystem», «Laufzustand», Overlay-Server `live`); Bericht `docs/archiv/berichte/P14-bericht.md`.

**Prüfung f:** Prüfstufe «Baustein» (9.).

## 9. Prüfstufe «Baustein» (Ordner `build/pruefung-p14/`)

- `cmake --build build -j8`
- `ctest --test-dir build` mit aktiver X11-Sitzung, 0 übersprungen
- frischer Klon (Bau und `ctest`)
- `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p14/start/startpruefung`
- `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p14/dienst`
- **50 Wiederholungen** (Zeitgrenzen, Netz, Ton): `tst_aktionsablauf`, `tst_ausloeser`, `tst_kernaktionen`, `tst_twitchschreiben`, `tst_tonmischer`, `tst_kernton`, `tst_mitgeliefert`. Ohne Last daneben, nie zwei Fenstertests gleichzeitig.
- Last: Der Testserver schickt 60 s lang 50 Chat-Nachrichten je Sekunde, davon 20 % Befehle, dazu 5 Follows je Sekunde.
  - Erwartet: Grenze hält (höchstens 20 Nachrichten je 30 s und Konto), Warteschlange «Alerts» verwirft über 50 mit Eintrag im Verlauf, Antwort auf `get_overlay_list` höchstens 25 ms.
  - CPU des Kerns als Messwert in den Bericht.
- `scripts/tonpruefung.py` (20 Läufe) und `scripts/tonleistung.py` mit beiden Zwecken.

## 10. Handtest (Regel 10)

**Prüft Lotse** (Kern und Fenster aus `build/`, eigenes `XDG_CONFIG_HOME`, installiertes Studio geschlossen, nie während eines Streams; Rechte nach Frage 4):

**L1 Bildschirmfotos.**
- Was: Reiter «Chat-Bot» in den Zuständen «aus», «läuft», «stumm» und «Bot nicht angemeldet»; alle Unterreiter, alle Dialoge, Dock «Chat», Zeile «Ton Chat-Bot», Marke «live». Ablage nur im Scratchpad.
- Warum: Aussehen.

**L2 Chat über den echten Weg.**
- Was: Testaktion `!dsstest` anlegen (`dss-cli aktion-setzen`): Chat als Bot «DSS-Test ok, %user%», dann als Kanal mit Antwort. Der Nutzer tippt den Befehl (N2). Lotse liest `dss-cli verlauf --folgen`. Erwartet: je eine `twitch.chat_sent` innerhalb von 2 s, genau eine Antwort je Konto, kein Echo löst etwas aus. Danach Testaktion löschen.
- Warum: echter Weg über Helix und TLS, Echo und Schleifenschutz (Doku schweigt).

**L3 OBS-Quelle ein/aus.**
- Was: In der Szene «test 2» eine eigene Testquelle anlegen, Testaktion schaltet sie ein, aus und um; Zustand über obs-websocket lesen; Testquelle danach löschen.
- Warum: echtes OBS, das `ctest` nicht hat.

**L4 Overlay in OBS.**
- Was: Overlay «Chat-Bot» als Browserquelle in «test 2» anlegen; «Begrüssung» per Test-Knopf (Probelauf) auslösen; Bild der Quelle holen: Profilbild und Name sichtbar, nach 8 s weg. Neustart des Kerns während der Anzeige: danach nichts eingeblendet. Quelle danach löschen, Szene zurück auf «Szene».
- Warum: zweiter Zeichner in OBS mit Laufzustand und Profilbild.

**L5 Ton-Weg.**
- Was: «Ton Chat-Bot» auf einen Ausgang gestellt (mit dem Nutzer abgesprochen), Testaktion mit Ton: Zustand `playing`, Protokoll ohne Fehler, Mehrlast im Leerlauf.
- Warum: echter PipeWire-Ausgang. Ob es gut klingt, prüft der Nutzer (N4).

**L6 Neustart.**
- **Eigener Schritt:** `systemctl --user restart dss-core-entwicklung.service`.
- Was: Aktionen, Befehle, dauerhafte Variablen und Zähler unverändert, flüchtige leer, Laufzustand leer.
- Warum: Speichern im echten Dienst.

**Prüft der Nutzer:**

**N1 Bedienung des Editors.**
- Was: eine eigene Aktion anlegen, Schritte hinzufügen, verschachteln (Wenn/Dann), ordnen, einen Schritt abschalten, Auslöser anhängen, Aktion kopieren und löschen, Test-Knopf (Probelauf und echt).
- Warum: Bedienung.

**N2 Mitgelieferte Aktionen anpassen.**
- Was: Texte von `!discord` und `!regeln` eintragen und einschalten; im eigenen Chat `!dsstest` (für L2) und `!befehle` tippen; Antwort des Bots im Twitch-Chat ansehen (Name, Antwort-Faden).
- Warum: Konten, Aussehen im Twitch-Chat.

**N3 Begrüssung, First Chatters, Shoutout.**
- Was: mit einem zweiten Zuschauer-Konto (falls vorhanden) eine erste Nachricht schreiben; sonst über «Als Test ausführen …». `!so <Kanal>` tippen.
- Warum: echte Ereignisse lassen sich nur über Konten auslösen. Ohne zweites Konto: «nicht geprüft» für den echten Weg.

**N4 Ton hören.**
- Was: Tondateien aus der alten Sammlung in die Mediathek holen, die Vorlage «Chat-Sound» kopieren, Datei und Befehl wählen, im Chat auslösen; Lautstärke 50 % und 150 % vergleichen.
- Warum: Hören kann nur der Nutzer.

**N5 Aussehen der Meldungen.**
- Was: Overlay «Chat-Bot» im Editor nach eigenem Geschmack gestalten, als Browserquelle in OBS anlegen; Alerts, Begrüssung und Shoutout per Test im OBS-Bild ansehen.
- Warum: Aussehen im Stream.

**N6 Erster Stream.**
- Was: echte Follows, Subs, Raids, Cheers, Zeitnachrichten, Vorstellung beim Streamstart, Zurücksetzen der First Chatters bei Stream offline. Lotse liest danach den Verlauf und das Protokoll.
- Warum: echte Ereignisse.

**N7 Ausschalten.**
- **Eigener Schritt:** «Chat-Bot aktiv» ausschalten, Befehl tippen. Erwartet: keine Antwort, im Verlauf «Bot aus».
- Warum: Bedienung.

Was Lotse oder der Agent schon geprüft haben, wiederholt der Nutzer nicht.

## 11. Später und nicht vorgesehen

- **Später als feste Bausteine (vorgemerkt):**
  - **Countdown-Timer:** eigene Schritte «Countdown starten/anhalten/fortsetzen/abbrechen/ändern» und eine Textquelle mit der Restzeit; ersetzt das C#-Paket «Timer». Braucht P14 (Schritte, Laufzustand).
  - **Watchtime:** Zeit je Zuschauer zählen (`moderator:read:chatters`, in P13 schon geholt), Variable je Zuschauer, Befehl zum Anzeigen. Braucht P14 (Variablen je Zuschauer).
- **Später (Entscheid 2026-11-15, nicht für die ersten Streams):** Kanalpunkte-Effekte (Auslöser Kanalpunkte, Belohnungen neu anlegen), Kamera-Fernbedienung (nach dem Baustein Kamerasteuerung), Kategorie per Befehl, Umfrage und Vorhersage, Moderations-Knöpfe, weitere OBS-Schritte (Szene, Filter, Text), OBS-Ereignisse als Auslöser.
- **Eigener Baustein:** Streamer.bot-Import (liest Export und `data/`, übernimmt bekannte Typen, bewahrt Unbekanntes als `unsupported`).
- **Vorerst nicht:** Subathon, YouTube Music, Golf, Hugs, Q&A, Skript-Schritt.
- **Offen ohne Termin:** Abzeichen «Chat Bot» (P13 Frage 2), YouTube, Auslösen von aussen (Stream Deck über den Overlay-Server).

## 12. Pakete (Regel 5)

Kein neues Paket. Benutzt werden die Pakete aus P13 (Qt Network, WebSockets, OpenSSL-Backend), aus P12 (libpipewire-0.3, libavcodec, libswresample) und Qt Core (`QTextBoundaryFinder`). Ob libavcodec die Decoder für mp3, Vorbis und AAC enthält, prüft Abschnitt c (Debian-Pakete der FFmpeg-Bibliotheken, Version in den Bericht).

## 13. Schätzung

| Teil | Agent | Lotse | Nutzer |
|---|---|---|---|
| a Kern und Modell | ~1 Sitzung | ~15 min | – |
| b Ausführung | ~1,5 Sitzungen | ~20 min | – |
| c Schritte nach aussen | ~1,5 Sitzungen | ~20 min | – |
| d Overlays | ~1 Sitzung | ~20 min | – |
| e Editor | ~2 Sitzungen | ~30 min | Entwurf UI vorher abnehmen |
| f Mitgelieferte Aktionen, Dokumente | ~1 Sitzung | – | – |
| Prüfstufe «Baustein» | ~2 h, 50er-Läufe nachts | – | – |
| Handtest | – | ~2 h (L1–L6) | ~1 h (N1–N5, N7), dazu N6 im ersten Stream |
| **Summe** | **~8 Sitzungen à 4–5 h** | ~4 h | ~1 h und ein Stream |

## 14. Fragen an den Nutzer (gebündelt)

1. **Aufbau im Studio** (sichtbar; Grundlage für den «Entwurf UI Chat-Bot (folgt)»).
   - **A) Empfohlen:** wie Abschnitt 6: Reiter «Chat-Bot» mit Unterreitern (Aktionen, Befehle, Zeitnachrichten, Warteschlangen, Variablen, Verlauf, Ereignisse); Schritte, Auslöser und Befehle in Dialogen wie bei Streamer.bot; Dock «Chat» in beiden Reitern; die fünf Editor-Docks im Reiter «Chat-Bot» ausgeblendet. Begriffe deutsch («Aktion», «Schritt», «Auslöser», «Warteschlange»), der Name aus Streamer.bot im Tooltip.
   - B) Wie A, aber Editor-Docks bleiben sichtbar. Gegenargument zu A: Das Umschalten der Docks je Reiter ist neu und kostet etwas Bau; B gibt dem Editor der Aktionen weniger Platz.
   - C) Wie A, aber Begriffe englisch wie Streamer.bot («Action», «Sub-Action», «Trigger»). Gegenargument: Regel 13 (Oberfläche deutsch).
2. **Wie Aktionen Overlays ändern** (sichtbar).
   - **A) Empfohlen:** nur «jetzt» (Laufzustand): nicht in der Datei, nach einem Neustart wieder der Stand des Editors; im Editor sichtbar mit Marke «live». Mitgeliefert wird ein Overlay «Chat-Bot» mit Quellen für Alerts, Begrüssung und Shoutout, das du im Editor gestaltest und einmal in OBS anlegst.
   - B) Aktionen ändern die Overlay-Datei wie eine Änderung im Editor. Gegenargument zu A: einfacher (etwa ½ Sitzung weniger), aber jeder Alert schreibt die Datei zweimal, und nach einem Absturz bleibt eine Meldung eingeblendet.
3. **Test-Knopf** (bedienbar).
   - **A) Empfohlen:** Probelauf als Standard: nichts geht an Twitch oder OBS, Overlay und Ton laufen echt, der Verlauf zeigt, was gesendet würde; Kästchen «echt ausführen». Schützt vor Testnachrichten im Chat während eines Streams.
   - B) Immer echt, wie bei Streamer.bot. Gegenargument zu A: Ein Probelauf zeigt keine Fehler von Twitch (fehlende Rechte, Grenzen); dafür gibt es das Kästchen.
4. **Go für P14 und Rechte im Handtest.**
   - **A) Empfohlen:** Go für die Abschnitte a bis d gleich nach P13; Abschnitt e erst, wenn du den «Entwurf UI Chat-Bot (folgt)» abgenommen hast; dann f. Lotse darf im Handtest, nie während eines Streams, Testnachrichten «DSS-Test» als Bot und als Kanal in deinen Chat senden, Testaktionen anlegen und löschen und eine Testquelle in «test 2» schalten. Das wird in `CLAUDE.md`, «Umgebung», eingetragen.
   - B) Go, aber Lotse sendet nichts selbst. Dann bleibt L2 «nicht geprüft» und du tippst alles selbst. Gegenargument zu A: Testnachrichten erscheinen in deinem öffentlichen Chat-Verlauf.
   - C) Änderungen am Plan, bitte nennen.

## 15. Risiken und `[OFFEN]`

| Risiko oder offene Frage | Gegenmassnahme oder Stand |
|---|---|
| Argumentnamen von Streamer.bot nur teilweise belegt | P13 Schritt 0; vorläufige Namen als `[OFFEN]`, der Import gleicht ab |
| Typnummern von Shoutout und Stream Online bei Streamer.bot unbekannt | Schritt 0; sonst `null` in der Tabelle, der Import ergänzt |
| Echo eigener Nachrichten nicht dokumentiert | doppelter Schutz (4.1), L2 |
| Zählung der 500 Zeichen, `/` am Anfang | Schritt 0, vorsichtige Annahmen (4.1) |
| Shoutout: Grenzen und «nur live» nicht geprüft | Schritt 0; Ablehnung steht im Verlauf, der Lauf geht weiter |
| Decoder öffnet Tondateien ohne Bildspur womöglich nicht | Abschnitt c, Test mit eigenen Dateien |
| Zweiter `pw_stream` erhöht die Mehrlast über die Grenze aus P12 | nur offen, wenn ein Ausgang gewählt ist; Messung in c und in der Prüfstufe |
| Profilbild-Server von Twitch (Name, Formate) nicht geprüft | Schritt 0; ohne Bild bleibt das Platzhalterbild |
| Laufzustand ist im Editor sichtbar, steht aber nicht in der Datei | Marke «live», «Laufzustand zurücksetzen», Frage 2 |
| Zwei Versionen handeln doppelt | Stumm-Regel 3.4, Test in `tst_kernaktionen` |
| Bot nur bei offenem Studio | vom Nutzer so entschieden (2026-11-15) |
| Geteilter Chat: Nachrichten mit User-Token gehen an alle Kanäle der Sitzung | nur auf den eigenen Kanal reagieren; Abhilfe erst mit App-Token (Abzeichen) |
| `concurrent` wird nicht ausgeführt | kommt in den Daten des Nutzers nicht vor; Meldung im Verlauf |
| Umfang gross (8 Sitzungen) | Abschnitte einzeln lauffähig und geprüft; e wartet auf den Entwurf, a bis d nicht |

## Annahmen und Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| Eigene Bibliothek `dss_actions` ohne Gui und ohne Netz, Dienste über Schnittstellen | Regel 9 und 14; Abläufe ohne Twitch, OBS und Gerät testbar |
| Lauf als Zustandsmaschine im Hauptthread | keine Sperren zwischen Läufen; Pausen über Timer; Vorschlag der Recherche |
| Schritte verschachtelt gespeichert, ohne `parentId` | wie Fassung 24 beim Nutzer; Import ohne Umbau |
| Unbekannte Felder und Typen bleiben erhalten | Import und ältere Versionen verlieren nichts |
| `script` nur als gespeicherter Platzhalter | Entscheid 2026-11-15: kein Skript-Schritt, Platz im Modell |
| Platzhalter `%arg%` und `~global~` wie Streamer.bot, unbekannte bleiben stehen | eine Schreibweise; importierte Texte ohne Umschrift |
| `$math()$` und Formate nicht in P14 | kommen in den Daten des Nutzers nicht vor |
| Fehler eines Schritts beenden den Lauf nicht | wie Streamer.bot; ein fehlendes Bild verhindert die Chat-Begrüssung nicht |
| Grenzen 3.3 | Schleifen und Ansturm enden sicher; Werte gross genug für die Abläufe des Nutzers |
| Eigene Sendegrenze 1/s und 20/30 s je Konto | Sperre 1 h bei Überschreitung; sicher für jede Rolle |
| Ton über eigenen Zweck `aktionen`, Strom nur bei gewähltem Ausgang | Entscheid 2026-11-09 (Ausgang je Zweck); Mehrlast nur bei Nutzung |
| Lautstärke bis 200 %, begrenzt auf ±1,0 | Daten des Nutzers nutzen bis 150 % |
| Profilbilder lädt der Kern, Overlay-Server liefert sie | beide Zeichner zeigen dieselbe Datei (K-07); OBS lädt nichts aus dem Netz |
| Ganze Aktion je Änderung (`action_put`) | einfache Schnittstelle, keine Teilkonflikte |
| Verlauf 200 Läufe, nur im Speicher | genug für Anzeige und Prüfung; keine Daten von Zuschauern auf der Platte |
| Hauptschalter «Chat-Bot aktiv» in `aktionen.json`, Standard aus | geht mit der Installation mit; kein ungewollter Bot nach dem Update |
| Mitgelieferte Info-Befehle mit unbekanntem Inhalt aus | kein Platzhaltertext im echten Chat |
| Shoutout als eine aufgerufene Aktion für Befehl und Raid | ein Ablauf, zwei Auslöser; zeigt `run_action` |
| First Chatters mit dauerhaftem Platz-Zähler | ein Neustart des Kerns während des Streams zählt richtig weiter |
| Neuer Schalter `DSS_CORE_ACTIONS` | handelt selbständig öffentlich, riskanter Teil nach Regel 8 |
| Dialoge statt Bereich im Reiter für Schritte | jede Art hat andere Felder; wie Streamer.bot, dem Nutzer vertraut |

## Quellen

- **Lokal gelesen (2026-11-15):**
  - `CLAUDE.md`, `.claude/HANDOFF.md`
  - `docs/01-festlegungen.md` («Module», «Oberfläche», «Verbindungsanzeige», «Kern als Dienst»), `docs/03-funktionen-entwurf.md`, `docs/04-offene-punkte.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md` (Versionen, «audio_outputs», «Anbindung an OBS», «Overlay-Server», «Ton der Videos»)
  - `docs/archiv/prompts/P13-twitch.md`, Plan P14 vom 2026-11-09
  - `src/app/mainwindow.cpp:111-226`, `src/app/headerbar.cpp:160-200`, `src/app/settingsdialog.h`, `src/app/docks.h`, `src/common/overlay.h`, `src/common/coresettings.h`, `src/common/mediaimport.cpp:70-95`, `src/common/protocol.h:51`, `src/audio/mixer.h`, `src/media/audiodecoder.h`, `src/obs/obssources.cpp`, `src/obs/obsconnection.cpp` (Namen der Anfragen)
  - Recherchen von Lotse (2026-11-09, nicht im Repo): «Chat-Bot als Aktionssystem», «So nutzt der Nutzer Streamer.bot»; Entscheide des Nutzers vom 2026-11-15
- **Web, gelesen am 2026-11-09 (Plan P14 alt und Recherchen):**
  - https://dev.twitch.tv/docs/chat/
  - https://dev.twitch.tv/docs/chat/authenticating/
  - https://dev.twitch.tv/docs/chat/send-receive-messages/
  - https://dev.twitch.tv/docs/api/reference/#send-chat-message
  - https://dev.twitch.tv/docs/api/reference/#send-chat-announcement
  - https://dev.twitch.tv/docs/eventsub/eventsub-reference/#chat-message
  - https://docs.streamer.bot/guide/core/actions
  - https://docs.streamer.bot/guide/triggers
  - https://docs.streamer.bot/guide/commands
  - https://docs.streamer.bot/guide/core/timers
  - https://docs.streamer.bot/guide/variables
  - https://docs.streamer.bot/api/sub-actions
  - https://docs.streamer.bot/api/sub-actions/core/logic/if-else
  - https://docs.streamer.bot/api/sub-actions/core/actions/run-action
  - https://docs.streamer.bot/api/sub-actions/core/globals/global-set
  - https://docs.streamer.bot/api/sub-actions/core/sounds/play-sound
  - https://docs.streamer.bot/api/triggers/core/commands/command-triggered
  - https://docs.streamer.bot/guide/platforms/twitch
  - https://raw.githubusercontent.com/PedroZamecki/streamerz/main/src/model.rs (Dritte, MIT)
  - https://raw.githubusercontent.com/PedroZamecki/streamerz/main/src/types.rs (Dritte, MIT)
  - https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md (für P10c gelesen)
- **Nicht gelesen (Schritt 0):**
  - https://dev.twitch.tv/docs/api/reference/#send-a-shoutout
  - https://dev.twitch.tv/docs/api/reference/#get-users
  - https://dev.twitch.tv/docs/api/reference/#get-channel-information
  - Abschnitte `GetSceneItemList`, `GetSceneItemId`, `GetSceneItemEnabled`, `SetSceneItemEnabled` in https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md
  - Seiten der Twitch-Auslöser und der Sub-Action «Get User Info for Target» unter https://docs.streamer.bot/api/
  - https://doc.qt.io/qt-6.8/qtextboundaryfinder.html
- **Am 2026-11-15 nicht im Web geprüft:** Aussagen zu Twitch und Streamer.bot stützen sich auf die Lesungen vom 2026-11-09. Schritt 0 bestätigt sie gegen die aktuelle Doku (Regel 4).
