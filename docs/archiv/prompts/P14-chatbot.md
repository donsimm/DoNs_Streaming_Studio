# P14 – Chat-Bot als Aktionssystem mit Editor (Plan)

Plan des Projektleiter-Agenten vom 2026-11-15, am selben Tag nachgeführt (Zuschauer-Daten aus Streamer.bot, Entscheide zur Oberfläche nach dem Entwurf, «alles anpassbar», streamfähiger erster Teil). Er ersetzt den Plan «Basisfunktion Chat-Bot» vom 2026-11-09. Grundlage:
- Entscheide des Nutzers vom 2026-11-09 und 2026-11-15 (unten)
- Plan P13 (`docs/archiv/prompts/P13-twitch.md`, Stand 2026-11-15): Ereignisfluss, zwei Konten, Ereignisse mit Argumentnamen, Unterreiter «Live» mit «Ereignisse»
- Entwurf der Oberfläche: Branch `lotse/entwurf-chatbot`, Ordner `entwuerfe/chatbot/` (Seiten 1 bis 5 und `teil-editor.html`)
- Plan und Bericht P12 (`docs/archiv/prompts/P12-ton.md`, `docs/archiv/berichte/P12-bericht.md`): Ton über PipeWire, Ausgang je Zweck
- `docs/01-festlegungen.md` («Module», «Oberfläche», «Kern als Dienst»), `docs/03-funktionen-entwurf.md` («Chat-Bot», E-14, E-17, TX-16), `docs/08-kern-schnittstelle.md` (Version 13)
- Recherchen von Lotse vom 2026-11-09 (nicht im Repo): «Chat-Bot als Aktionssystem» (Aufbau von Streamer.bot, Variante D) und «So nutzt der Nutzer Streamer.bot» (Auswertung der echten Daten, Abläufe in Abschnitt 5)
- Regeln 4, 5, 7, 8, 9, 12, 13, 14 und 18 (`CLAUDE.md`)

**Aufteilung:** **P14-1 «streamfähig»** (alles, was der erste Stream braucht) und **P14-2 «Komfort»** (nach dem ersten Stream). Die Abschnitte sind nach Abhängigkeit geordnet; mehrere lassen sich neben P13 bauen (Abschnitt 10).

## Entscheide des Nutzers

- **2026-11-09:** eigenes Bot-Konto (zweite Anmeldung mit Code); Follows kommen direkt von Twitch; Kanal ist Affiliate; alles lesen und schreiben, was Twitch erlaubt; die Streamer.bot-Daten liegen auf dem Studio-PC und dürfen gelesen werden (ohne Zugangsdaten).
- **2026-11-15, Umfang:**
  - P14 ist ein **Aktionssystem wie Streamer.bot** (Variante D der Recherche): Aktionen mit Schrittliste, Auslöser, Variablen. Modell wie Streamer.bot von Anfang an, der Umfang der Schritte wächst in Bausteinen.
  - **Editor im Studio schon in P14**, mit Grundschritten (Chat senden, Pause, Ton, Overlay-Element zeigen/verstecken, OBS-Quelle ein/aus, Variable/Zähler, Wenn/Dann). Der Nutzer legt Aktionen selbst an und ändert, kopiert und löscht auch die mitgelieferten.
  - **Mitgeliefert und veränderbar** für die ersten Streams: Info-Befehle, Zeitnachrichten (auch beim Streamstart), Begrüssung bei der ersten Nachricht, Shoutout (Befehl und Raid), First Chatters mit dauerhaften Zählern, Chat-Sounds, Alerts (Follow, Sub, Cheer, Raid), Chat im Studio lesen und schreiben.
  - Alerts, Begrüssung und Shoutout erscheinen als **Overlay im Studio**, nicht über vorbereitete OBS-Quellen.
  - **Kein Skript-Schritt.** Das Datenmodell lässt Platz dafür, gebaut wird er nicht.
  - Der Bot läuft nur, solange das Studio offen ist. Das reicht.
  - **Nicht für die ersten Streams:** Kanalpunkte-Effekte, Kamera-Fernbedienung, Kategorie per Befehl, Umfrage und Vorhersage, Moderations-Knöpfe. **Später als feste Bausteine:** Countdown-Timer, Watchtime. **Vorerst nicht:** Subathon, YouTube Music, Golf, Hugs, Q&A. Der Import der Aktionen aus Streamer.bot ist ein eigener, späterer Baustein.
- **2026-11-15, Zuschauer-Daten:** **alles übernehmen, was da ist**, vor dem ersten Stream, als eigener Abschnitt getrennt vom späteren Import der Aktionen; Zuordnung über die Twitch-Benutzer-ID; auch Werte ohne gebaute Funktion (Hugs, Watchtime) aufbewahren, sichtbar und änderbar; nur im Datenordner des Studios, nie im Repo, in Berichten oder in Tests. Nie `auth.db` und `auth-backup.db`.
- **2026-11-15, Oberfläche** (nach dem Entwurf `lotse/entwurf-chatbot`, Commit 89d47f0):
  - Editor als **Unterreiter** im Reiter «Chat-Bot», nicht als eigenes Fenster.
  - Die Docks des Overlay-Editors sind im Reiter «Chat-Bot» ausgeblendet.
  - Der Editor **speichert sofort**.
  - Begriff **«Aktionen»**.
  - **Alles anpassbar:** Die Grafiken im Entwurf sind nur Beispiele. Das Aussehen von Alerts, Begrüssung und Shoutout gestaltet der Nutzer im Overlay-Editor; Schritte verweisen auf beliebige Overlays und Elemente; das mitgelieferte Overlay ist nur ein neutraler Startpunkt.

## Wichtigste Befunde

| Befund | Beleg |
|---|---|
| Streamer.bot hat fünf Teile: Action mit geordneten Sub-Actions, Trigger an der Action, Commands und Timed Actions als eigene Objekte, Queues (blockierend oder nicht, «Default» nicht blockierend), Argumente `%arg%` je Lauf und globale Variablen (dauerhaft oder flüchtig, auch je Benutzer) | https://docs.streamer.bot/guide/core/actions, https://docs.streamer.bot/guide/triggers, https://docs.streamer.bot/guide/commands, https://docs.streamer.bot/guide/core/timers, https://docs.streamer.bot/guide/variables |
| Gespeicherte Felder einer Action: `id`, `queue`, `enabled`, `excludeFromHistory`, `excludeFromPending`, `name`, `group`, `alwaysRun`, `randomAction`, `concurrent`, `triggers[]`, `subActions[]`. Sub-Actions tragen `id`, `weight`, `type` (Nummer), `parentId`, `enabled`, `index` und die Felder des Typs | Recherche (Dritte: https://raw.githubusercontent.com/PedroZamecki/streamerz/main/src/model.rs) |
| In den Daten des Nutzers (Fassung 24) sind Sub-Actions **verschachtelt** gespeichert, jedes Element trägt zusätzlich `parentId`. Tiefe bis 3 | Recherche «So nutzt der Nutzer Streamer.bot», Abschnitte 0 und 3.1 |
| Nutzung beim Nutzer: 163 Actions, 150 Trigger (93 davon Befehle), 912 Sub-Actions; am häufigsten OBS-Quelle ein/aus, Kommentar, Pause, Argument setzen, Chat senden (fast immer als Bot), Variable lesen/setzen, Ton (Lautstärke 0,5 bis 1,5), Run Action, If/Else, Benutzerinfo. Befehle nur «Basic/Start». `~global~` und `$math()$` kommen nicht vor | dieselbe Recherche, Abschnitte 0, 3, 4 |
| Zuschauer-Daten von Streamer.bot liegen in `globals.db`, `firstwords.db`, `twitch_data.db`, `commands.db`, `kick_data.db`, `youtube_data.db`, `pronouns.db` (Format **LiteDB**, Kopf «** This is a LiteDB file **», nicht SQLite) und in `quotes.dat`, `users.dat` (Format nicht geprüft). Laut Lotse: Variablen je Zuschauer, darunter `watchtime` bei rund 800, `firsts`/`seconds`/`thirds` bei 40 bis 70, `hugsReceived`/`hugsGiven` bei einigen Dutzend Zuschauern, dazu Einstellungen von Paketen | Auftrag von Lotse vom 2026-11-15 (Ordner nicht gelesen) |
| Streamer.bot nutzt LiteDB 5.0.21. LiteDB ist eine .NET-Bibliothek (MIT); Version 5 schreibt Änderungen zuerst in eine Log-Datei neben der Datenbank (`<name>-log.db`), die erst beim Checkpoint in die Hauptdatei kommt. **Nicht geprüft**, Schritt 0 | https://streamer.bot/changelogs (Recherche), https://github.com/litedb-org/LiteDB (2026-11-15 nicht gelesen) |
| Der Kern hat einen Mischer je Ausgang und einen Ausgang je Zweck (`kAudioPurposes`, heute nur `video` mit `[ton-video]`). Jede Stimme folgt der Uhr einer Seite (`PageClock`); eine Stimme ohne Seite gibt es nicht | `src/common/coresettings.h:38-72`, `src/audio/mixer.h:11-41` |
| Der Decoder liest mit libavcodec und wandelt in float, 48 kHz, Stereo. Ob er Dateien ohne Bildspur öffnet, ist **nicht geprüft** | `src/media/audiodecoder.h:14-27` |
| Die Mediathek nimmt nur Bilder, Videos und Schriften | `src/common/mediaimport.cpp:76-80` |
| `dss_obs` kann Quellen nicht ein- und ausschalten (`GetSceneItemList`, `GetSceneItemId`, `GetSceneItemEnabled`, `SetSceneItemEnabled` fehlen) | `src/obs/obssources.cpp`, `src/obs/obsconnection.cpp`; https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md |
| Quellen eines Overlays sind eine flache Liste mit Sichtbarkeit, Text und Datei; jede Änderung schreibt die Overlay-Datei (K-02). Gruppen von Quellen, Übergänge und ein «nur jetzt»-Zustand fehlen. Animationen sind E-14 (SOLL) | `src/common/overlay.h:41-97`, `docs/08-kern-schnittstelle.md`, `docs/03-funktionen-entwurf.md:24` |
| Der Reiter «Chat-Bot» ist ein `QWidget` im `QStackedWidget` der Module; Docks hängen am Hauptfenster | `src/app/mainwindow.cpp:175-192` |
| Senden: `POST https://api.twitch.tv/helix/chat/messages`, Pflicht `broadcaster_id`, `sender_id` (Konto des Tokens), `message` (höchstens 500 Zeichen), wahlweise `reply_parent_message_id`; Antwort mit `is_sent` und `drop_reason` | https://dev.twitch.tv/docs/api/reference/#send-chat-message |
| Grenzen: 20 Nachrichten je 30 s, 100 für Kanal, Moderatoren und VIPs; bei Überschreitung «Twitch ignores the bot messages for 1 hour» | https://dev.twitch.tv/docs/chat/ |
| Ansage: `POST …/helix/chat/announcements` mit `moderator:manage:announcements`, «One announcement may be sent every 2 seconds» | https://dev.twitch.tv/docs/api/reference/#send-chat-announcement |
| Shoutout: `POST …/helix/chat/shoutouts` mit `moderator:manage:shoutouts`. Grenzen (einmal je 2 min, dasselbe Ziel einmal je 60 min, nur im laufenden Stream) **nicht geprüft**, Schritt 0 | https://dev.twitch.tv/docs/api/reference/#send-a-shoutout (nicht gelesen) |
| Benutzerinfo: `GET …/helix/users?login=` liefert unter anderem `profile_image_url`; die letzte Kategorie liefert `GET …/helix/channels`. **Nicht geprüft**, Schritt 0 | https://dev.twitch.tv/docs/api/reference/#get-users, https://dev.twitch.tv/docs/api/reference/#get-channel-information |

## 1. Begriffe

Oberfläche auf Deutsch (Regel 13, Entscheid «Aktionen»), der Name aus Streamer.bot im Tooltip. Im Code englische Bezeichner.

| Oberfläche | Streamer.bot | Bedeutung |
|---|---|---|
| Aktion | Action | benannter Ablauf aus Schritten |
| Schritt | Sub-Action | ein Baustein einer Aktion, zum Beispiel «Chat senden» |
| Auslöser | Trigger | was eine Aktion startet: Befehl, Twitch-Ereignis, Zeitnachricht, Test |
| Befehl | Command | Chat-Befehl wie `!discord`, mit Rechten und Abklingzeit |
| Zeitnachricht | Timed Action | Auslöser nach Zeit, wahlweise erst nach einigen Chat-Nachrichten |
| Warteschlange | Queue | Aktionen derselben Warteschlange laufen nacheinander, wenn sie «nacheinander» (blockierend) ist |
| Argument | Argument | Wert, der nur während eines Laufs gilt, zum Beispiel `%user%` |
| Variable | Global | Wert über Läufe hinweg, flüchtig oder dauerhaft, für alle oder je Zuschauer |
| Element | – | eine Quelle eines Overlays (Text, Bild, Video), auf die ein Schritt zeigt |
| Lauf | – | eine Ausführung einer Aktion |

## 2. Datenmodell

### 2.1 Grundsätze

- Felder und Aufbau wie Streamer.bot (Fassung 24 aus den Daten des Nutzers), Namen im Format in `snake_case`. Die Tabelle zur Typnummer von Streamer.bot steht im Code (`steptypes.h`, `triggertypes.h`). So übernimmt der spätere Import Bekanntes ohne Umbau.
- Schritte sind verschachtelt gespeichert wie in Fassung 24 (eigene Liste `steps` bei Gruppen und Zweigen). `parentId` braucht das Studio nicht; der Import liest es.
- Kennungen sind UUIDs. Beim Import bleiben die GUIDs von Streamer.bot erhalten (erneuter Import möglich, Zähler der Befehle finden ihren Befehl, 9.3).
- Unbekannte Felder eines Objekts bleiben beim Lesen und Schreiben erhalten (`extra`).
- **Nichts am Aussehen steht im Modell:** Schritte verweisen nur auf Overlays und Elemente (Kennungen); Lage, Grösse, Schrift, Farben, Bilder und Videos bleiben im Overlay (Abschnitt 8).

### 2.2 Datei `<Datenordner>/aktionen.json` (neu, `format_version` 1)

```json
{
  "format_version": 1,
  "enabled": false,
  "queues": [ { "id": "UUID", "name": "Alerts", "blocking": true } ],
  "groups": [ { "id": "UUID", "name": "Stammgäste", "users": [ { "user_id": "…" } ] } ],
  "actions": [
    {
      "id": "UUID", "name": "Begrüssung", "group": "Begrüssung", "enabled": true,
      "queue": "UUID oder null", "exclude_from_history": false,
      "random_action": false, "concurrent": false, "always_run": false,
      "template_id": "UUID der Vorlage oder null",
      "triggers": [ { "id": "UUID", "type": "twitch.first_message", "enabled": true } ],
      "steps": [
        { "id": "UUID", "type": "user_info", "enabled": true, "login": "%userName%" },
        { "id": "UUID", "type": "chat_send", "enabled": true, "text": "Willkommen, %user%!", "use_bot": true, "fallback": true, "reply": false },
        { "id": "UUID", "type": "overlay_source", "enabled": true, "overlay": "UUID", "mode": "show",
          "elements": [ { "source": "UUID", "text": null, "image": "%targetUserProfileImageUrl%" },
                        { "source": "UUID", "text": null, "image": null } ],
          "duration_ms": 8000, "wait": true, "transition_in": "none", "transition_out": "none", "transition_ms": 0 }
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
- `template_id` kennzeichnet mitgelieferte Aktionen («Zurücksetzen», Abschnitt 7).
- Keine Zugangsdaten und keine Daten von Zuschauern ausser den Kennungen in `groups` und `permissions.users`.

### 2.3 Aktion

| Feld | Streamer.bot | In P14 |
|---|---|---|
| `id`, `name`, `group`, `enabled` | gleich | ja; `group` nur zum Ordnen |
| `queue` | `queue` | ja |
| `exclude_from_history` | `excludeFromHistory` | ja (Lauf nicht in «Läufe der Aktionen») |
| `random_action` | `randomAction` | ja: nur ein zufälliger Schritt nach `weight` |
| `concurrent` | `concurrent` | gespeichert, nicht ausgeführt (kommt nicht vor); Meldung im Lauf «gleichzeitig nicht unterstützt, nacheinander ausgeführt» |
| `always_run` | `alwaysRun` | ja (läuft auch bei angehaltener Warteschlange) |
| `triggers[]`, `steps[]` | `triggers`, `subActions` | ja |

### 2.4 Schritte

Gemeinsame Felder: `id`, `type`, `enabled`, `weight` (Zufall), bei Gruppen und Zweigen `steps`. Platzhalter (2.9) gelten in allen Textfeldern. Spalte «Teil»: in welchem Teil der Schritt gebaut wird.

| Typ | Streamer.bot (Nr.) | Felder | Teil |
|---|---|---|---|
| `comment` | Kommentar (1009) | `text`, `color` | P14-1, tut nichts |
| `group` | Gruppe (99900) | `name`, `random`, `steps` | P14-1 |
| `if` | If/Else (120) mit Zweigen `if_true` (99901) und `if_false` (99902) | `input`, `operation`, `value`, `auto_type`; genau zwei Kinder `if_true` und `if_false` mit je `steps` | P14-1; Vergleiche gleich, ungleich, gleich/ungleich ohne Gross-/Kleinschreibung, enthält, kleiner, grösser, leer, existiert nicht; Regex später |
| `break` | Break (124) | – | P14-1, beendet den Lauf ohne Fehler |
| `delay` | Delay (1002) | `value_ms`, `max_value_ms`, `random` | P14-1, «Pause», höchstens 1 h |
| `run_action` | Run Action (4) | `action_id`, `immediately` (Streamer.bot schreibt `runImmedately`) | P14-1 |
| `set_argument` | Set Argument (123) | `name`, `value`, `auto_type` | P14-1 |
| `global_get` | Global (Get) (121) | `name`, `scope` (`global`, `user`), `user` (`trigger` oder `target`), `persisted`, `destination`, `default` | P14-1 |
| `global_set` | Global (Set) (122) | `name`, `scope`, `user`, `persisted`, `operation` (`set`, `increment`, `decrement`, `delete`), `value` | P14-1 |
| `chat_send` | Send Message to Channel (10) | `text`, `use_bot`, `fallback`, `reply` | P14-1 (4.1) |
| `user_info` | Get User Info for Target (50) | `login` | P14-1 (4.8) |
| `twitch_shoutout` | Nummer in Schritt 0 | `login` | P14-1 (4.9) |
| `twitch_announce` | Twitch Announcement (23) | `text`, `color`, `use_bot` | P14-1 (4.9) |
| `play_sound` | Play Sound (1) | `file`, `volume` (0 bis 200 %), `wait` | P14-1 (4.3) |
| `overlay_source` | – (eigener Schritt; bei Streamer.bot OBS 30, 38, 39) | `overlay`, `mode` (`show`, `hide`, `toggle`), `elements[]` (`source`, `text`, `image`), `duration_ms`, `wait`, `transition_in`, `transition_out` (`none`, `fade`), `transition_ms` | P14-1 ohne Übergänge; `fade` in P14-2 (4.4) |
| `obs_source_visible` | OBS Set Source Visibility (30) | `scene_name`, `source_name`, `state` (`on`, `off`, `toggle`) | P14-2 (4.5) |
| `script` | Execute C# Code (99999) | `language`, `code` | **nie gebaut:** gelesen und gespeichert, im Editor grau «Skript-Schritt nicht verfügbar», nie ausgeführt, nicht anlegbar |
| `unsupported` | jeder andere Typ | `sb_type`, `original` | **nie ausgeführt:** für den Import, immer aus, im Editor «nicht unterstützt: <Typ>» |

Später als eigene Schritte (nicht P14): Kategorie setzen (16), Umfrage (561), Vorhersage (568), Timeout (20), Clip (18), OBS Szene setzen (34), Filter (35), Text (39), Countdown, Watchtime, Zitate.

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
| `twitch.stream_online` | Stream Online (Nr. Schritt 0) | `skip_at_start` | – |
| `twitch.stream_offline` | Stream Offline (155) | – | – |
| `timer` | Timed Action (701) | `timer_id` | `timerName` |
| `test` | Test (702) | `variables` | die eingetragenen |

- Bereich «von–bis» schliesst beide Enden ein, wie bei Streamer.bot.
- Allgemeine Argumente in jedem Lauf: `actionName`, `actionId`, `runId`, `triggerType`, `date`, `time`, `unixtime`, `broadcastUser`, `broadcastUserName`, `broadcastUserId`, `isLive`.
- Später (nicht P14): Kanalpunkte (112), OBS-Ereignisse (14004, 14005), Anwesende Zuschauer (124), Abklingzeit eines Befehls (402).

### 2.6 Befehle

| Feld | Streamer.bot | In P14 |
|---|---|---|
| `commands[]` | Commands («one item per line») | ja, ein Befehl je Zeile; über alle aktiven Befehle eindeutig (Warnung) |
| `mode` | Mode | `basic`; `regex` gespeichert, nicht ausgeführt (Warnung) |
| `location` | Location | `start` (erstes Wort gleich dem Befehl, `!d` trifft nicht `!discord`), `exact`, `anywhere` |
| `case_sensitive` | Case Sensitive | ja, Standard aus |
| `ignore_bot_account` | Ignore Bot Account | ja, Standard an |
| `ignore_internal` | Ignore Internal Messages | ja, Standard an: Nachrichten, die das Studio selbst gesendet hat, lösen nichts aus |
| `sources` | Sources | `twitch_message`; weitere später |
| `global_cooldown_s`, `user_cooldown_s` | Global/User Cooldown | ja |
| `persist_counter`, `persist_user_counter` | Persist Counter | ja: Zähler `counter`, `userCounter` in `variablen.json` |
| `permissions` | Permissions | `grant` (`allow`, `deny`), `roles` (`subscriber`, `vip`, `moderator`), `groups`, `users` (`user_id`). Leere Liste bei `allow` = alle. Im Editor von P14-1: Rollen und Gruppen; «verbieten» und einzelne Zuschauer in P14-2 (im Modell und in der Ausführung schon in P14-1, damit importierte Rechte wirken) |

- **Der Kanal darf immer und hat keine Abklingzeit** (wie Streamer.bot).
- Abgewiesene Befehle erscheinen in «Läufe der Aktionen» mit Grund, ohne Antwort im Chat.
- Ein Befehl löst die Aktionen aus, die einen Auslöser `command` mit seiner `command_id` haben. «Ausführen: Aktion» im Bereich des Befehls legt diesen Auslöser an oder ändert ihn (wie im Entwurf, Seite 3).

### 2.7 Zeitnachrichten

- Felder wie Streamer.bot: `name`, `enabled`, `repeat`, `interval_s` (mindestens 60), `random` mit `random_min_s`/`random_max_s`, `lines`.
- Eigene Erweiterung `only_live` (Standard an). Streamer.bot hat das Feld nicht; der Import setzt es auf aus.
- «Beim Streamstart» ist ein zweiter Auslöser `twitch.stream_online` an derselben Aktion.

### 2.8 Warteschlangen

- `name`, `blocking`. Zur Laufzeit dazu: angehalten, wartende Läufe, laufender Lauf.
- «Standard» ist nicht blockierend und nicht löschbar.
- Blockierend: Läufe laufen nacheinander in der Reihenfolge ihres Eintreffens.
- Anhalten hält neue Läufe zurück (ausser `always_run`), Fortsetzen startet sie, «Leeren» verwirft die wartenden (P14-2 in der Oberfläche; in P14-1 über `dss-cli`).

### 2.9 Argumente, Variablen, Platzhalter

- **Argumente** gelten nur im Lauf. Quellen: Auslöser, allgemeine, Schritte (`set_argument`, `global_get`, `user_info`).
- **Variablen:**
  - **Beliebige Namen**, Werte Text oder Zahl. Der Name aus Streamer.bot bleibt beim Import unverändert (`watchtime`, `firsts`, `hugsReceived`), damit spätere Bausteine und importierte Aktionen sie finden.
  - `scope` `global` oder `user`; Variablen je Zuschauer sind über die **Twitch-Benutzer-ID** zugeordnet, nie über den Namen.
  - flüchtig (nur im Speicher) oder dauerhaft.
  - Je Wert merkt sich der Kern `origin` (`studio` oder `streamerbot`) und `changed_at`. So erkennt ein erneuter Import, was im Studio seither geändert wurde (9.4).
- **Datei** `<Datenordner>/variablen.json` (`format_version` 1): `global` (Name → Wert), `users` (Twitch-ID → Name → Wert), `command_counters` (Befehlskennung → `counter` und je Twitch-ID `user_counter`). Geschrieben verzögert über `SaveScheduler`.
- **Platzhalter** wie Streamer.bot:
  - `%name%` setzt das Argument ein; ein unbekanntes bleibt wörtlich stehen.
  - `~name~` setzt eine dauerhafte globale Variable ein.
  - Nicht in P14 (kommt bei dem Nutzer nicht vor): Formate `%name:format%`, `$math()$`, `$length()$`.

### 2.10 Weitere Dateien mit Zuschauer-Daten

| Datei | Inhalt | Woher |
|---|---|---|
| `<Datenordner>/zuschauer.json` | je Twitch-ID: `login`, Anzeigename und weitere Felder, wie Streamer.bot sie hat (Name der Felder unverändert) | Import aus `twitch_data.db` (9.3); im Betrieb nachgeführt aus Chat-Nachrichten und «Benutzerinfo» (nur Login und Anzeigename) |
| `<Datenordner>/zitate.json` | Zitate mit den Feldern aus `quotes.dat` | Import; ein Befehl für Zitate kommt später |
| `<Datenordner>/streamerbot-archiv.json` | alles aus den übrigen Dateien, das sich keinem Twitch-Zuschauer als einfacher Wert zuordnen lässt (Kick, YouTube, Einstellungen von Paketen, die keine einfachen Werte sind), je Datei und Sammlung unverändert als JSON | Import; nicht sichtbar, aufbewahrt für spätere Bausteine |

- **Datenschutz:** Diese Dateien, `variablen.json` und `twitch-sitzung.json` (P13) liegen nur im Datenordner des Studios. Nie im Repo, nie im Protokoll, nie in Berichten, nie in Tests (Tests nutzen erfundene Daten; Ausnahme von Regel 7 nach Entscheid des Nutzers vom 2026-11-15).
- Die Sicherung der Installation (`scripts/installieren.sh`) nimmt sie wie die übrigen Dateien des Datenordners mit.

### 2.11 Datei-Regeln

- Fehlt `aktionen.json`, legt der Kern die mitgelieferten Aktionen an (Abschnitt 7).
- Eine ungültige Datei wird gemeldet und nicht überschrieben (Muster `liste.json`). Der Bot bleibt dann aus, Änderungen werden abgewiesen.
- Verweise prüft der Kern beim Laden und Speichern (Warteschlange, Befehl → Aktion, `run_action`-Ziel, Overlay und Element, Mediendatei). Ein fehlender Verweis ist eine Warnung im Editor; der Schritt meldet ihn im Lauf.
- `dss-core --format-pruefen` kennt alle neuen Dateien. `format_version` > 1 heisst neueres Format (Exit-Code 5).

## 3. Ausführung

### 3.1 Lauf

- Bibliothek `src/actions/` (`dss_actions`): Qt Core, kein Qt Gui, kein Netz, kein `protocol.h`; bindet `dss_events` ein (Regel 9 und 14).
- Ein Lauf ist eine Zustandsmaschine im Hauptthread des Kerns. Schritte mit Wartezeit geben die Kontrolle ab und machen bei ihrer Antwort weiter.
- Schritte nach aussen gehen über Schnittstellen (`ChatSender`, `TwitchApi`, `ObsControl`, `SoundPlayer`, `OverlayLive`), die der Kern verbindet und die Tests ersetzen. Die Uhr lässt sich im Test ersetzen.
- Ein Lauf arbeitet mit dem Stand der Aktion bei seinem Beginn. Eine Änderung im Editor (speichert sofort) gilt ab dem nächsten Lauf.
- **Fehler eines Schritts:** Eintrag im Lauf, der Lauf geht weiter (wie Streamer.bot). Nur `break` und die Grenzen beenden ihn.
- Ein abgeschalteter Schritt wird übersprungen, eine abgeschaltete Gruppe mit allem darin.

### 3.2 Reihenfolge

Ein Ereignis startet alle aktiven Aktionen mit passendem Auslöser in der Reihenfolge der Liste; jede läuft in ihrer Warteschlange. Bei gleichzeitigen Auslösern gilt die Reihenfolge des Eintreffens (bei Streamer.bot nicht dokumentiert).

### 3.3 Grenzen

| Grenze | Wert | Begründung |
|---|---|---|
| Tiefe von `run_action` (sofort) | 8 | Schleifen zwischen Aktionen enden sicher |
| Schritte je Lauf | 2000 | dito |
| gleichzeitige Läufe | 32 | Ansturm bremst den Kern nicht |
| wartende Läufe je Warteschlange | 50, ältere zuerst verworfen | Alerts stauen nicht ins Endlose |
| Dauer eines Laufs | 15 min | hängende Schritte enden |

### 3.4 Nur eine Version handelt

- Läuft der Kern der Version im Einsatz, startet der Kern der Version in Arbeit keine Läufe aus Twitch-Ereignissen, Befehlen und Zeitnachrichten (geprüft am Socket). Zustand «stumm: Version im Einsatz läuft». Probeläufe gehen weiter.
- Gilt für alles nach aussen und für den Ton.

### 3.5 Läufe der Aktionen (Protokoll)

- Der Kern hält die letzten 200 Läufe im Speicher: Zeit, Aktion, Auslöser, Zustand (`waiting` mit Warteschlange, `running` mit Schritt x von y, `ok`, `break`, `error`, `rejected` mit Grund, `muted`, `dropped`, `limit`, `cancelled`), Dauer, je Schritt Ergebnis und Meldung. Nicht gespeichert.
- Auf dem Fluss: `actions.run_started`, `actions.run_changed`, `actions.run_finished`. Das Fenster bestellt sie über `subscribe_events` (P13 1.5) und holt Einzelheiten mit `action_run_details`. «Lauf abbrechen» über `action_run_cancel`.
- Ins Journal: Name der Aktion, Ergebnis, Fehler. Keine Chat-Texte, keine Namen von Zuschauern.

### 3.6 Testen (Frage 3)

- «▶ Testen» in der Knopfleiste der Schritte und im Bereich eines Befehls, mit Testwerten (erfundener Zuschauer, im Entwurf «Testwerte: Benutzer»). In P14-2 zusätzlich alle Argumente des Auslösers bearbeitbar.
- **Probelauf** (Standard): Schritte nach Twitch und OBS werden nicht ausgeführt; der Lauf zeigt «Probelauf: würde senden …». Overlay, Ton, Pause laufen echt, Variablen in einem eigenen flüchtigen Satz.
- **«echt ausführen»** (Kästchen): wie ein echter Auslöser.
- «Nochmals auslösen» im Bereich «Ereignisse» (P14-2) startet die Aktionen eines echten Ereignisses noch einmal, echt.

## 4. Grundschritte im Einzelnen

### 4.1 Chat senden

- **Konto:** `use_bot` an = Bot-Konto (P13 2.1). Ist der Bot nicht angemeldet und `fallback` an, sendet der Kanal, sonst Fehler im Lauf.
- **Antwort:** `reply` setzt `reply_parent_message_id` auf `%msgId%`.
- **Grenze im Kern je Konto:** höchstens 1 Nachricht je Sekunde und 20 je 30 s, Warteschlange von 10, was älter als 30 s ist, wird verworfen und im Lauf genannt.
- **Länge:** nach dem Einsetzen auf 500 Zeichen gekürzt, nie mitten in einem Zeichen (`QTextBoundaryFinder`). Wie Twitch zählt: Schritt 0, bis dahin UTF-16-Einheiten.
- **Text am Anfang:** Texte mit `/` oder `.` am Anfang weist der Editor ab («Twitch-Befehle sind nicht möglich»). Nicht geprüft, ob die API sie ausführen würde.
- **Geteilter Chat:** Auslöser nur aus dem eigenen Kanal.
- **Keine Schleifen:** Eigene Nachrichten erkennt der Kern an der `message_id` aus der Antwort (letzte 100) und an Absender und gleichem Text innerhalb von 10 s; sie gelten als «intern».
- **Ergebnis** auf dem Fluss: `twitch.chat_sent` bzw. `twitch.chat_dropped`.

### 4.2 Pause

`value_ms`, mit `random` eine Zufallszeit zwischen `value_ms` und `max_value_ms`. Über `QTimer`, kein Schlafen.

### 4.3 Ton abspielen

- `file` aus der Mediathek, `volume` 0 bis 200 %, `wait` (Standard an). Höchstens 8 Töne gleichzeitig. Im Bereich des Schritts «Anhören» über denselben Ausgang.
- **Abbruch:** Entscheid Nutzer 2026-11-16: Abbruch beendet die Töne des Laufs, sofort und mit und ohne `wait` (Schritt «Abbruch», «Lauf abbrechen», Zeit- und Schrittgrenze, Grenze der Ebenen). Beim normalen Ende nach dem letzten Schritt (auch mit Fehler in einem Schritt) spielen Töne ohne `wait` zu Ende.
- **Ausgang:** eigener Zweck `aktionen` neben `video` (Entscheid 2026-11-09: «je Zweck oder Modul ein eigener Ausgang»): `{"aktionen", "ton-aktionen"}` in `kAudioPurposes`, Abschnitt `[ton-aktionen]` in `kern.ini`, `audio_routes.aktionen`, Zeile «Ton Chat-Bot» im Dock «Steuerung» (Ausgang und Zustand, ohne Versatz). Standard: kein Ausgang gewählt; der Schritt meldet das.
- **Was fehlt (Abschnitt t):**
  1. Medienart «Ton» in der Mediathek: `mp3`, `wav`, `ogg`, `m4a`; Dock «Medien» zeigt Symbol und Länge.
  2. Decoder für Dateien ohne Bildspur prüfen und falls nötig anpassen.
  3. Freie Stimme im Mischer: Start beim nächsten Abruf ohne Uhr einer Seite, Lautstärke, Ende wird gemeldet; Regeln des Echtzeit-Threads wie P12. Über 100 % auf ±1,0 begrenzt.
  4. Zweiter `pw_stream`, nur offen, solange ein Ausgang gewählt ist. Mehrlast messen (P12: 2,75 Prozentpunkte eines Kerns, Grenze 3).
  5. Ton von Videos in einem eingeblendeten Element kommt weiter über «Ton Videos» (P12), nur wenn die Browserquelle in OBS im Programm ist.
  6. Mit `DSS_CORE_AUDIO=OFF` meldet der Schritt «ohne Ton gebaut».

### 4.4 Overlay-Element zeigen oder verstecken (Fragen 1 und 2)

- **Beliebige Overlays und Elemente:** Der Schritt wählt ein Overlay des Studios und darin ein oder mehrere Elemente (Quellen). Mehrere Elemente werden gemeinsam gezeigt und versteckt (zum Beispiel Rahmen, Profilbild und Name einer Begrüssung). Gruppen von Quellen im Overlay-Editor gibt es heute nicht (Frage 2).
- **Laufzustand:** Eine Aktion ändert ein Element nur «jetzt»: Sichtbarkeit, Text, Bild. Der Kern hält das im Speicher, schreibt es **nie in die Overlay-Datei** und vergisst es beim Neustart.
- **Inhalt je Element:**
  - **Text:** Ein Textelement darf in seinem eigenen Text (im Overlay-Editor gestaltet) Platzhalter enthalten, zum Beispiel «Neuer Follower: %user%». Zeigt eine Aktion das Element, setzt der Kern die Argumente des Laufs ein. So bleiben Wortlaut und Aussehen im Overlay. Wahlweise ersetzt `text` im Schritt den ganzen Text. Im Editor ohne Laufzustand steht der Text wörtlich da. (Ein Teil von TX-16.)
  - **Bild:** `image` ist eine Adresse aus einem Argument (Profilbild, `%targetUserProfileImageUrl%`) oder eine Datei der Mediathek; leer = Bild des Elements.
- **Dauer** `duration_ms` (danach fällt der ganze Laufzustand der Elemente weg), `wait` (Lauf wartet die Dauer ab).
- **Übergänge** `transition_in`/`transition_out` (`none`, `fade`) und `transition_ms`: in P14-1 nur `none`; weiches Ein- und Ausblenden über die Deckkraft in P14-2, in beiden Zeichnern gleich (K-07; Vorlage für E-14).
- **Beide Zeichner zeigen den Laufzustand (K-07):**
  - Seite: `event: live` auf `…/events` mit den geänderten Feldern je Element; `web/overlay.js` legt sie über das Overlay.
  - Editor: Nachricht `overlay_live`; die Leinwand zeichnet ihn mit. Dock «Quellen»: Marke «live». Dock «Eigenschaften»: Bereich «Chat-Bot» (4.4a).
- **Profilbilder:** Der Kern lädt das Bild selbst (HTTPS, nur vom Bildserver von Twitch, Name in Schritt 0, höchstens 2 MiB, PNG oder JPEG), legt es unter `<Datenordner>/live-bilder/<SHA-256>.<endung>` ab (höchstens 200, älteste zuerst gelöscht, beim Start geleert) und liefert es unter `/o/<adresse>/<token>/live/<SHA-256>.<endung>` nur an Overlays, deren Laufzustand es benutzt. Das Element zeigt das Bild in seinem Kasten wie jedes Bild (Lage und Grösse aus dem Editor).
- Ein Video, das eingeblendet wird, beginnt von vorne.

**4.4a Bereich «Chat-Bot» im Dock «Eigenschaften»** (Entwurf Seite 5): «Jetzt» (live sichtbar, Restzeit) und «Ausblenden» (Laufzustand zurücksetzen) in P14-1; «Gezeigt von» (Aktionen, die das Element benutzen) und «Testweise zeigen» in P14-2.

### 4.5 OBS-Quelle ein/aus (P14-2)

- `scene_name`, `source_name`, `state` (`on`, `off`, `toggle`). Namen wie bei Streamer.bot.
- `dss_obs` bekommt `GetSceneItemList`, `GetSceneItemId`, `GetSceneItemEnabled`, `SetSceneItemEnabled`; der Editor holt Szenen und Quellen über `obs_scene_items`.
- Keine mitgelieferte Aktion braucht ihn; darum erst nach dem ersten Stream.

### 4.6 Variable und Zähler

`global_set` mit `set`, `increment`, `decrement`, `delete`; `global_get` kopiert in ein Argument. Je Zuschauer über `scope: user` und `user` (`trigger` oder `target`). Im Editor «Variable / Zähler» (ein Schritt mit Auswahl Setzen, Erhöhen, Verringern, Löschen, Lesen).

### 4.7 Wenn/Dann, Abbruch, Gruppe

«Wenn» mit Vergleich, Zweige «Dann» und «Sonst», beliebig tief; «Abbruch» beendet den Lauf; «Gruppe» fasst Schritte zusammen, wahlweise «zufällig einen».

### 4.8 Benutzerinfo holen

- `login` mit Platzhaltern, «@» am Anfang wird entfernt.
- Setzt `targetUser`, `targetUserName`, `targetUserId`, `targetUserProfileImageUrl` (belegt in den Daten des Nutzers), dazu `targetDescription` und `game` (`%game%` belegt). Weitere Namen: Schritt 0. Der Entwurf nennt `%userProfileImageUrl%` und `%isFollowing%`; ob Streamer.bot diese für die auslösende Person setzt, klärt Schritt 0.
- Unbekannter Name: `targetUserFound` = `false`, kein Fehler.
- Nebenbei führt der Kern `zuschauer.json` nach (Login, Anzeigename).

### 4.9 Shoutout und Ansage

- `twitch_shoutout` mit dem Token des Kanals; eine Ablehnung (Grenze, nicht live) steht im Lauf, der Lauf geht weiter.
- `twitch_announce` als Bot (wenn Moderator) oder Kanal; einmal je 2 s im Kern.

### 4.10 Aktion ausführen

`run_action` sofort (im selben Lauf, Argumente hin und zurück) oder eigenständig (eigener Lauf in der Warteschlange des Ziels).

## 5. Kern und Schnittstelle

- **Kern:** `ActionService` in `CoreService`. Schalter `DSS_CORE_ACTIONS` (Standard ON), weil der Bot selbständig öffentlich handelt (Regel 8). OFF: Reiter zeigt nur «Live» ohne Läufe.
- **Kern an Fenster** (nach `hello` und bei Änderung an alle): `actions_config` (ganzer Inhalt von `aktionen.json`, `file_error`), `actions_status` (`state`, `message`, Warteschlangen, Konten «kann senden»), `overlay_live`. Auf Bestellung: `variables` (Variablen mit Herkunft), `viewers` (Login und Anzeigename zu Twitch-IDs, nur die angezeigten), `quotes`.
- **Fenster an Kern**, mit `request_id` und Antwort im Muster von `change_result`:
  - `actions_set_enabled`; `action_put`, `action_remove`, `action_copy`, `action_reset` (mitgelieferte zurücksetzen); `command_put`, `command_remove`; `timer_put`, `timer_remove`; `queue_put`, `queue_remove`, `queue_control`; `group_put`, `group_remove`;
  - `variable_set`, `variable_remove`, `quote_put`, `quote_remove`;
  - `action_test` (`action_id`, `arguments`, `dry_run`), `action_run_details`, `action_run_cancel`, `event_replay` (P14-2);
  - `chat_send` (`text`, `account`, `reply_to`);
  - `overlay_live_reset`; `obs_scene_items` (P14-2);
  - `viewer_import` (`path`, `dry_run`, Abschnitt 9).
- **Speichert sofort:** Kästchen und Auswahllisten senden bei jeder Änderung; Textfelder beim Verlassen oder mit Enter. Der Editor schickt die ganze Aktion (`action_put`); die Statuszeile zeigt «Gespeichert: <Aktion>», eine Abweisung steht am Feld. Es gibt keinen Knopf «Speichern». Rückgängig gibt es in P14 nicht (Risiko 16; «Zurücksetzen» für mitgelieferte).
- **Version der Schnittstelle:** 15 mit Abschnitt b3; jeder weitere Abschnitt, der Pflichtfelder ändert, +1. Bauen Abschnitte parallel (10.2), vergibt Lotse die Nummern beim Zusammenführen.
- **`dss-cli`:** `aktionen`, `aktion-setzen DATEI.json`, `aktion-loeschen ID`, `aktion-test ID [--echt] [ARG=WERT …]`, `laeufe [--folgen]`, `warteschlange NAME anhalten|fortsetzen|leeren`, `variablen [--zaehlen]` (nur Anzahlen je Name), `zuschauer-import ORDNER [--endgueltig]`, `chat-senden [--bot] TEXT` (nur im Handtest mit Erlaubnis aus Frage 5).

## 6. Bedienorte im Studio (nach dem Entwurf)

Grundlage: `entwuerfe/chatbot/` auf `lotse/entwurf-chatbot` und die Entscheide vom 2026-11-15. Aussehen «Yami», Open Sans. Vor Abschnitt e führt der Agent `ui-consistency:finding-patterns` durch und hält die Checkliste im Bericht fest. Spalte «Teil»: P14-1 oder P14-2.

| Ort | Inhalt | Teil |
|---|---|---|
| Kopfbereich | Modul-Reiter «Chat-Bot» (gibt es), Twitch-Knopf (P13) | P13 |
| Reiter «Chat-Bot», Kopfzeile | links Unterreiter im Stil der Modul-Reiter: «Live», «Aktionen», «Befehle», «Zeitnachrichten», «Variablen»; rechts «Chat-Bot aktiv» und Zustand («läuft · liest als … · sendet als …», «aus», «stumm: Version im Einsatz läuft», «Bot nicht angemeldet: sendet als Kanal») | P13 (Gerüst), P14-1 |
| Docks des Overlay-Editors | im Reiter «Chat-Bot» ausgeblendet, beim Zurückwechseln wiederhergestellt; Anordnung je Reiter in `fenster.ini` | P13 |
| Unterreiter «Live» | drei Bereiche nebeneinander (Teiler verschiebbar): **Chat** (Liste mit Zeit, Abzeichen «Bot»/«Kanal»/«Mod», Name in Chatfarbe, Text; Ereignisse und ausgelöste Aktionen als graue Zeilen; Eingabe mit Auswahl «als Bot / als Kanal», Zähler bis 500, «Senden», Enter); **Ereignisse** (aus P13; «Chat-Nachrichten ausblenden»); **Läufe der Aktionen** (Zeit, Aktion, Auslöser, Ergebnis mit «wartet: Warteschlange …», «läuft, Schritt x von y», Abweisungen; «Im Editor öffnen», «Lauf abbrechen», «Nur Fehler») | P14-1 |
| «Live», Ergänzungen | «Nochmals auslösen» bei Ereignissen; Kontextmenü «Antworten» im Chat; Bereiche lösbar wie Docks | P14-2 |
| Unterreiter «Aktionen», links | Baum Gruppen → Aktionen mit Kästchen «aktiv», Suchfeld; Knopfleiste «Neu», «Neue Gruppe», «Kopieren», «Löschen», rechts «Zurücksetzen» (nur mitgelieferte) | P14-1 |
| «Aktionen», Mitte | Bereich **Aktion** (Name, Gruppe, Warteschlange mit Eintrag «Warteschlangen verwalten …», Aktiv, Hinweis «mitgeliefert, geändert»); Bereich **Auslöser** (Liste mit Kästchen, «+», «−»); Bereich **Schritte** (Baum mit Einrückung für Gruppe, Dann, Sonst; Kästchen je Schritt, abgeschaltet rot durchgestrichen; eine Zeile je Schritt mit Art und Kurzinhalt); Knopfleiste «Schritt hinzufügen» (Menü nach Art), «Entfernen», «Hoch», «Runter»; rechts «Testwerte: Benutzer» und «▶ Testen» mit «echt ausführen» | P14-1 |
| «Aktionen», Mitte, Ergänzungen | Ziehen mit dem Griff ⋮⋮, «Kopieren» und «Einfügen» von Schritten, Testwerte für alle Argumente | P14-2 |
| «Aktionen», rechts | **Einstellungen des gewählten Schritts oder Auslösers** in Bereichen wie das Dock «Eigenschaften» (kein Dialog). Beim Overlay-Schritt: Overlay, Elemente (Liste mit «+»), Inhalt je Element, Dauer, «Warten, bis ausgeblendet». Beim Ton: Datei, Lautstärke, «Anhören» | P14-1 |
| «Aktionen», rechts, Ergänzungen | Bereich «Platzhalter» (Doppelklick fügt ein), Bereich «Vorschau» (das gewählte Overlay mit den Testwerten, gezeichnet vom Zeichner des Editors), Einblenden/Ausblenden «weich» mit Dauer | P14-2 |
| Unterreiter «Befehle» | Suchfeld, Filter Gruppe; Tabelle «Aktiv», «Auslöser», «Gruppe», «Wer darf», «Abkl. Kanal», «Abkl. Person», «Aktion», «Benutzt»; Knopfleiste «Neu», «Kopieren», «Löschen»; rechts Bereich des gewählten Befehls (Befehl, Wer darf mit Alle/Abonnenten/VIPs/Moderatoren und eigenen Gruppen, «Nachrichten des Bots ignorieren», Abklingzeit, Ausführen mit «Öffnen»), «▶ Testen als …» | P14-1 |
| «Befehle», Ergänzungen | «verbieten» statt «erlauben», einzelne Zuschauer, Dialog «Benutzergruppen» (Gruppen bearbeiten) | P14-2 |
| Unterreiter «Zeitnachrichten» | Tabelle (Aktiv, Name, Abstand, Chat-Zeilen, nur im Stream, Aktion); rechts Bereich des Gewählten wie bei «Befehle» | P14-1 |
| Unterreiter «Variablen» | Bereiche «Für alle» (Name, Wert, dauerhaft, Herkunft) und «Je Zuschauer» (Zuschauer mit Anzeigename aus `zuschauer.json`, Variable, Wert, Herkunft «Streamer.bot»/«Studio»); Suchfeld, Filter nach Variablenname; Wert in der Zelle ändern, «Löschen». Zeigt auch übernommene Werte ohne Funktion (`watchtime`, `hugsReceived` …) | P14-1 |
| «Variablen», Ergänzungen | Bereich «Zitate» (ansehen, ändern, löschen) | P14-2 |
| Dialog «Warteschlangen» | Liste (Name, nacheinander), «Neu», «Löschen»; «Anhalten», «Fortsetzen», «Leeren» | P14-1 (anlegen, löschen), P14-2 (steuern) |
| Reiter «Overlays», Dock «Quellen» | Marke «live» bei Elementen mit Laufzustand | P14-1 |
| Dock «Eigenschaften» | Bereich «Chat-Bot» (4.4a) | P14-1 / P14-2 |
| Dock «Steuerung» | Zeile «Ton Chat-Bot» | P14-1 |
| Dock «Medien» | Tondateien | P14-1 |
| Dock «Overlays» | mitgeliefertes Overlay «Chat-Bot» | P14-1 |
| Statuszeile | «Gespeichert: <Aktion>» | P14-1 |

**Abweichungen vom Entwurf und vom ersten Plan, mit Grund:**
- Kein eigenes Dock «Chat» mehr: Der Chat ist ein Bereich von «Live» wie im Entwurf. Im Reiter «Overlays» gibt es keinen Chat (wäre ein späteres Dock).
- Kein Unterreiter «Warteschlangen»: Anlegen über die Auswahl im Bereich «Aktion» («Warteschlangen verwalten …»), Zustand in «Läufe der Aktionen». So bleibt es bei den fünf Unterreitern des Entwurfs.
- Kein Unterreiter «Verlauf» und «Ereignisse»: beides in «Live» («Läufe der Aktionen», «Ereignisse»).
- Einstellungen von Schritten, Auslösern, Befehlen und Zeitnachrichten rechts im Bereich wie im Entwurf, keine Dialoge (der erste Plan hatte Dialoge).
- Die Bereiche in «Live» und im Editor tragen im Entwurf Knöpfe zum Lösen und Schliessen wie Docks. In P14-1 sind es feste Bereiche mit Teilern (echte Docks innerhalb eines Reiters bräuchten ein zweites Hauptfenster); Lösen kommt mit P14-2.
- «Element (Gruppe)» und Quellen als Gruppen (Entwurf Seite 5): Gruppen von Quellen gibt es im Overlay-Editor nicht; der Schritt nimmt stattdessen mehrere Elemente (Frage 2).
- Einblenden/Ausblenden «weich»: in P14-2 (Frage 1).

- Die Logik der Ansichten liegt in `dss_common` und ist ohne Fenster getestet (Regel 9). Keine Layout-Änderungen in Ereignis-Behandlern.

## 7. Mitgelieferte Aktionen und Overlay

Der Kern legt sie an, wenn `aktionen.json` fehlt, aus der eingebauten Vorlage `src/actions/vorlage/aktionen.json`. Jede trägt `template_id`. «Zurücksetzen» stellt eine mitgelieferte auf die Vorlage zurück (nach Rückfrage); gelöschte legt «Zurücksetzen» im Menü der Liste wieder an. Texte sind allgemein und ohne echte Namen.

| Aktion | Auslöser | Schritte (Kurzform) | Warteschlange | Beim Anlegen |
|---|---|---|---|---|
| Info: `!befehle`, `!discord`, `!regeln`, `!specs` | je ein Befehl, Abklingzeit Kanal 10 s | Chat als Bot | Standard | `!befehle` aktiv; die anderen aus, bis der Nutzer seinen Text einträgt |
| Zeitnachricht «Info» | Zeitnachricht alle 20 min, 5 Zeilen, nur im Stream | Chat als Bot | Standard | aktiv |
| Vorstellung beim Streamstart | `twitch.stream_online` (nicht beim Start des Studios im laufenden Stream) | Pause 60 s → Chat als Bot | Standard | aktiv |
| Begrüssung | `twitch.first_message` | Wenn Kanal oder Bot → Abbruch; Benutzerinfo `%userName%`; Chat als Bot; Overlay-Element «Begrüssung» (Profilbild aus Argument, Name über Platzhalter im Element), 8 s, warten | «Meldungen» (nacheinander) | aktiv |
| Shoutout ausführen | keiner (wird aufgerufen) | Benutzerinfo `%shoutoutLogin%`; wenn nicht gefunden → Chat + Abbruch; Shoutout; Chat als Bot; Overlay-Element «Shoutout», 10 s, warten | «Meldungen» | aktiv |
| Shoutout per Befehl | Befehl `!so`, nur Moderatoren | `shoutoutLogin` = `%input0%` → «Shoutout ausführen» (sofort) | Standard | aktiv |
| Shoutout bei Raid | `twitch.raid` (ab 1 Zuschauer) | `shoutoutLogin` = `%userName%` → «Shoutout ausführen» (sofort) | Standard | aktiv |
| First Chatters | `twitch.first_message` | Wenn Kanal oder Bot → Abbruch; `firstChatterPlace` (dauerhaft, global) +1; wenn > 3 → Abbruch; je Platz `firsts`/`seconds`/`thirds` des Zuschauers (dauerhaft) +1, lesen, Chat | «Erste Chatter» (nacheinander) | aktiv; nutzt dieselben Namen wie Streamer.bot, die übernommenen Zähler zählen also weiter |
| First Chatters zurücksetzen | `twitch.stream_offline` | `firstChatterPlace` = 0 | «Erste Chatter» | aktiv |
| Chat-Sound (Vorlage) | Befehl `!sound`, Abklingzeit Kanal 30 s | Ton abspielen (keine Datei gewählt) | Standard | aus; der Nutzer kopiert sie je Ton |
| Alert Follow, Sub (Sub und Resub), Geschenk (Bündel), Cheer (ab 1 Bit), Raid (ab 1 Zuschauer) | je das Ereignis | Benutzerinfo; Overlay-Element «Alert …» (Text über Platzhalter im Element), 6 s, warten; Ton (Schritt aus, bis eine Datei gewählt ist) | «Alerts» (nacheinander) | aktiv |

**Mitgeliefertes Overlay «Chat-Bot»** (1920 × 1080, Ordner «Chat-Bot»), neutraler Startpunkt:
- Elemente «Alert Follow», «Alert Sub», «Alert Geschenk», «Alert Cheer», «Alert Raid» (je ein Text, zum Beispiel «Neuer Follower: %user%»), «Begrüssung Bild», «Begrüssung Name» («Willkommen, %user%»), «Shoutout Bild», «Shoutout Name»; alle unsichtbar.
- Gestaltung absichtlich schlicht: weisse Schrift aus der mitgelieferten Schrift, keine Rahmen, keine Farbflächen, ein neutrales Platzhalterbild (vom Agenten erzeugt, kein Markenlogo). Die Grafiken des Entwurfs werden **nicht** nachgebaut.
- Der Nutzer gestaltet es im Overlay-Editor um, ersetzt es durch eigene Overlays oder löscht es. Die mitgelieferten Aktionen zeigen dann «Element fehlt», bis er im Schritt seine eigenen Elemente wählt.
- Argumentnamen wie `%bits%`, `%viewers%` sind vorläufig bis Schritt 0 von P13.
- Der Hauptschalter «Chat-Bot aktiv» ist beim ersten Start aus.

## 8. Prüfung «alles anpassbar»

| Was | Wo der Nutzer es ändert | Fest eingebaut? |
|---|---|---|
| Lage, Grösse, Drehung, Schrift, Farben, Bilder, Videos der Meldungen | Overlay-Editor, beliebiges Overlay | nein |
| Wortlaut im Overlay | Text des Elements im Overlay-Editor (mit Platzhaltern) oder `text` im Schritt | nein |
| Welche Elemente eine Aktion zeigt | Schritt «Overlay-Element zeigen», jedes Overlay, jedes Element | nein |
| Dauer, Übergang, Reihenfolge, Warteschlange | Schritt, Aktion | nein (Übergang «weich» ab P14-2) |
| Texte im Chat, Konto (Bot/Kanal), Antwort | Schritt «Chat senden» | nein |
| Töne, Lautstärke, Ausgang | Schritt «Ton abspielen», Dock «Steuerung» | nein |
| Abläufe selbst | Schritte der Aktion; mitgelieferte änderbar, kopierbar, löschbar, zurücksetzbar | nein |
| Befehle, Rechte, Abklingzeiten, Zeitnachrichten | Unterreiter «Befehle», «Zeitnachrichten» | nein |
| Variablen und Zähler | Unterreiter «Variablen» | nein |
| Welche Felder ein Schritt hat; Grenzen 3.3; Sendegrenze 4.1; Bildserver der Profilbilder | Code | ja, mit Grund (Schutz vor Sperre durch Twitch, Schleifen, fremden Adressen) |

Das Aussehen hängt an keiner Stelle am mitgelieferten Overlay; der Agent prüft das in Abschnitt f mit einem Test, der die mitgelieferten Aktionen auf ein zweites, anders gestaltetes Overlay umstellt.

## 9. Import Zuschauer-Daten aus Streamer.bot (Abschnitt z, vor dem ersten Stream)

### 9.1 Ziel und Grenzen

- Übernimmt **alles, was in den Datendateien ist**, ausser Zugangsdaten. Getrennt vom späteren Import der Aktionen.
- Wiederholbar: Probelauf (zeigt, was geschähe, schreibt nichts), dann endgültig; ein zweiter endgültiger Lauf ist möglich (9.4).
- Bericht nur mit Anzahlen und Namen von Variablen (keine Namen von Zuschauern, keine Werte).
- Nie gelesen: `auth.db`, `auth-backup.db`. Der Export und der Import öffnen nur eine Kopie, nie das Original.

### 9.2 Lesen der LiteDB-Dateien (Frage 4)

**Empfehlung A: einmaliger Export mit einem kleinen .NET-Werkzeug.**
- Werkzeug `scripts/streamerbot-export/` (C#, eine Datei und ein Projekt, Bibliothek LiteDB 5.0.21 von NuGet, MIT, dieselbe Version wie Streamer.bot). Nicht Teil des Studios: kein CMake, kein `ctest`, das Studio bleibt ohne .NET.
- Ablauf (Lotse):
  1. Kopie des Ordners `data/` ohne `auth*.db` in einen Ordner unter dem Datenordner der Version in Arbeit (`…/dons-streaming-studio-entwicklung/import/streamerbot-<datum>/quelle/`), **mit** den Log-Dateien `*-log.db`, falls vorhanden (Befund LiteDB).
  2. `--struktur`: listet je Datei Sammlungen, Feldnamen, Typen und Anzahlen, ohne Werte (Schritt 0, Ergebnis im Bericht).
  3. `--export`: schreibt je Sammlung eine JSON-Datei nach `…/export/`, Datenbank nur lesend geöffnet (`ReadOnly`).
  4. `quotes.dat` und `users.dat`: Format in Schritt 0 bestimmen (JSON-Text oder LiteDB) und gleich behandeln.
- Paket: .NET SDK (Regel 5). Bezug in Schritt 0: Debian-Paket, falls vorhanden, sonst Microsoft-Paketquelle; Version im Bericht. `sudo apt install` gibt der Nutzer ein. Nach dem endgültigen Import kann es wieder weg.
- **Gegenargument:** neues, grosses Paket für eine einmalige Aufgabe, dazu ein Abruf von NuGet aus dem Netz.

**B: eigener Leser in C++** (LiteDB 5: Seiten zu 8 KiB, BSON-Dokumente über verkettete Datenblöcke, Log-Datei; Vorlage der Quelltext von LiteDB, MIT).
- Vorteil: kein fremdes Paket, im Studio wiederholbar.
- Nachteil: etwa 1 bis 1,5 Sitzungen mehr, Format nicht dokumentiert, Risiko bei Log-Datei, Verschlüsselung (ob Streamer.bot ein Passwort setzt, ist nicht geprüft) und Versionsunterschieden. Der Leser wird nur ein Mal gebraucht.

### 9.3 Zuordnung (Feldnamen nach Schritt 0)

| Quelle | Ziel im Studio | Sichtbar |
|---|---|---|
| `globals.db`, dauerhafte Variablen je Zuschauer | `variablen.json`, `users[<Twitch-ID>][<Name>]`, Name unverändert, `origin: streamerbot` | «Variablen», «Je Zuschauer» |
| `globals.db`, dauerhafte globale Variablen (auch Einstellungen von Paketen) | `variablen.json`, `global`; was kein einfacher Wert ist, ins Archiv | «Variablen», «Für alle» |
| `twitch_data.db` | `zuschauer.json` (Twitch-ID, Login, Anzeigename, weitere Felder unverändert) | Namen in «Variablen» |
| `users.dat` (Benutzergruppen) | `aktionen.json`, `groups` (Mitglieder als Twitch-ID) | «Befehle», «Wer darf» |
| `commands.db` (Zähler der Befehle) | `variablen.json`, `command_counters` unter der GUID des Befehls aus Streamer.bot | Spalte «Benutzt», sobald der Befehl mit derselben GUID importiert ist (späterer Baustein) |
| `quotes.dat` | `zitate.json` | P14-2: Bereich «Zitate» |
| `firstwords.db`, `pronouns.db` | einfache Werte je Twitch-Zuschauer als Variablen mit Vorsilbe der Datei (`firstwords.…`, `pronouns.…`), Rest ins Archiv | «Variablen» |
| `kick_data.db`, `youtube_data.db` | Archiv (keine Twitch-IDs) | nein |

- **Zuordnung über die Twitch-ID:** Hat eine Quelle nur den Login, sucht der Import die ID in `twitch_data.db`; findet er keine, fragt er (nach P13, mit dem Token des Kanals) `GET /helix/users` in Bündeln. Was ohne ID bleibt, kommt ins Archiv; der Bericht nennt die Anzahl.
- `watchtime`, `hugsReceived`, `hugsGiven` und andere Werte ohne Funktion bleiben Variablen mit ihrem Namen; die späteren Bausteine Watchtime usw. lesen sie. Die Einheit von `watchtime` ist `[OFFEN]` bis zum Baustein.
- Die Zähler von First Chatters (`firsts`, `seconds`, `thirds`) nutzt die mitgelieferte Aktion «First Chatters» sofort weiter.

### 9.4 Ablauf im Studio

- `viewer_import` (Kern) bzw. `dss-cli zuschauer-import ORDNER [--endgueltig]` liest die JSON-Dateien aus `…/export/`. Nur der Kern schreibt (K-03).
- **Probelauf** (Standard): Ergebnis mit Anzahlen je Ziel, Namen der Variablen mit Anzahl Zuschauer, Zuschauer ohne ID, Werte, die der Import überschreiben würde, darunter solche, die im Studio seit dem letzten Import geändert wurden (`origin: studio` oder `changed_at` neuer).
- **Endgültig:** zuerst Sicherung der betroffenen Dateien (`…/import/sicherung-<zeit>/`), dann schreiben. Werte aus Streamer.bot ersetzen gleichnamige; Werte, die im Studio seit dem letzten Import geändert wurden, bleiben, ausser mit `--ueberschreiben`. Nur im Studio vorhandene bleiben immer.
- **Echte Daten (Regel 18):** Probeläufe und Prüfungen macht Lotse mit dem Datenordner der Version in Arbeit. Den endgültigen Import in die Version im Einsatz startet der **Nutzer** selbst mit einem Skript, Aufruf aus jedem Ordner:
  ```
  ~/DoNs_Streaming_Studio/scripts/zuschauer-import.sh
  ```
  Es nimmt den Export, zeigt zuerst den Probelauf und fragt dann «Endgültig übernehmen? (j/n)». Voraussetzung: P14-1 ist installiert, das Studio im Einsatz läuft.
- Der Export bleibt im Datenordner, bis der Nutzer ihn löscht (für Wiederholungen); das Skript nennt den Ordner.

### 9.5 Tests

`tst_zuschauerimport` mit **erfundenen** Exportdaten in `tests/daten/streamerbot-export/` (gleiche Sammlungen und Feldnamen wie nach Schritt 0, ausgedachte Namen und Werte): Zuordnung über ID und über Login, fehlende ID ins Archiv, Probelauf schreibt nichts, Sicherung, zweiter Import mit im Studio geänderten Werten, `--ueberschreiben`, kein Name und kein Wert im Protokoll.

## 10. Bau-Abschnitte, Reihenfolge und parallele Arbeit

### 10.1 Abschnitte

Dauer in Sitzungen des Agenten à 4 bis 5 h. Jeder Abschnitt endet mit Bau ohne Warnungen und `ctest` (Stufe «Klein») und einer Prüfung durch Lotse (Bau, `ctest`, Rückmeldung lesen).

| Abschnitt | Inhalt | Commits | Dauer | Braucht | Unabhängig von P13? |
|---|---|---|---|---|---|
| **P14-1 «streamfähig»** | | | | | |
| a Modell | `src/actions/`: Modell 2.2 bis 2.8, `aktionen.json`, `extra`, `script`/`unsupported`, Typtabellen; Platzhalter, `variablen.json` mit Herkunft, `zuschauer.json`, `zitate.json`, Archiv; Verweise, `--format-pruefen` | 3 | 1 | – | **ja**, sofort |
| t Ton | Medienart Ton, Decoder ohne Bildspur, freie Stimme, Zweck `aktionen`, zweiter Strom, Zeile «Ton Chat-Bot» (ohne Schritt) | 2 | 0,75 | P12 | **ja**, sofort |
| d Overlays | Laufzustand im Kern, Platzhalter im Text eines Elements, Profilbilder, `event: live`, `…/live/…`, `web/overlay.js`, Leinwand, `overlay_live`, Marke «live», Bereich «Chat-Bot» (Jetzt, Ausblenden); über `dss-cli` testbar (ohne Schritt) | 2 | 1 | – | **ja**, sofort |
| z0 Export (Lotse) | Schritt 0 der Daten (`--struktur`), Export; Werkzeug schreibt der Agent von z1 vorab als eigenen Commit oder Lotse selbst | 1 | ~1 h Lotse, ~0,25 Agent | .NET SDK (Nutzer) | **ja**, sofort |
| b Ausführung | b1 Ablauf, b2 Auslöser (ohne Netz), b3 Kern (`ActionService`, Schnittstelle 15, Läufe, Stumm-Regel, `dss-cli`, Schalter) | 3 | 1,5 | a; b3 braucht P13 | b1, b2 ja; b3 nein |
| z1 Import | `viewer_import`, Zuordnung, Probelauf, Sicherung, `scripts/zuschauer-import.sh`, `tst_zuschauerimport` | 2 | 1 | a, z0; Auflösen per Login über Twitch nach P13 | **ja** (ohne Auflösen über Twitch) |
| c Schritte nach aussen | c1 Twitch (`TwitchApi`, Chat, Benutzerinfo, Shoutout, Ansage, Testserver); c2 Schritte «Ton abspielen» und «Overlay-Element zeigen» an t und d | 2 | 1 | P13, b, t, d | nein |
| e Editor | e1 Ansichtslogik in `dss_common` (nur a nötig); e2 «Live» (Chat, Läufe) und «Aktionen» mit Einstellungen rechts; e3 «Befehle», «Zeitnachrichten», «Variablen», Dialog «Warteschlangen» | 3 | 1,75 | e1: a; e2, e3: b3 | e1 ja |
| f Mitgeliefert | Vorlage, Overlay «Chat-Bot», «Zurücksetzen», `tst_mitgeliefert` (auch mit zweitem Overlay, Abschnitt 8); Dokumente, Bericht | 2 | 0,75 | c, e | nein |
| **Summe P14-1** | | | **~8,5** | | |
| **P14-2 «Komfort»** (nach dem ersten Stream) | | | | | |
| o OBS | Schritt `obs_source_visible`, neue Anfragen in `dss_obs`, Auswahl aus OBS | 1 | 0,5 | b3 | – |
| k Komfort | Ziehen, Kopieren/Einfügen von Schritten, Platzhalter-Liste, Vorschau, alle Testwerte, «Nochmals auslösen», «Antworten», Bereiche lösbar, «verbieten» und einzelne Zuschauer, Benutzergruppen, Warteschlangen steuern, «Gezeigt von», «Testweise zeigen», «Zitate», Übergang «weich» | 3 | 1,5 | P14-1 | – |
| **Summe P14-2** | | | **~2** | | |

Gegenüber der ersten Schätzung (8 Sitzungen) kommen der Zuschauer-Import (~1,25), «Live» und «Variablen» mit Herkunft und die Ergänzungen aus dem Entwurf (~0,75) dazu.

### 10.2 Schnellster Weg zum ersten Stream

Ohne Editor kann der Nutzer nicht streamen: Er muss mindestens Texte, Töne und Elemente der mitgelieferten Aktionen anpassen. Der Editor ist darum in P14-1, nur seine Bequemlichkeiten sind in P14-2. Die Zeit bis zum Stream lässt sich vor allem durch **paralleles Bauen** kürzen:

| Spur | Jetzt (neben P13, ~3,5 Sitzungen) | Danach |
|---|---|---|
| Agent 1 | P13 | b3 → c → e2, e3 → f |
| Agent 2 | a → b1, b2 → e1 | (frei für P14-2 oder Kamerasteuerung) |
| Agent 3 | t → d → z1 (ohne Auflösen über Twitch) | – |
| Lotse | z0 (Struktur und Export, sobald .NET da ist) | Prüfungen, Handtest |

- **Zeit bis «streamfähig»:** rund 3,5 Sitzungen (P13 und die Spuren 2 und 3 nebeneinander) plus rund 4 Sitzungen nacheinander (b3, c, e2/e3, f) = **etwa 7,5 Sitzungen statt etwa 12** ganz nacheinander (P13 + P14-1).
- Danach: Installation von P14-1 durch den Nutzer, endgültiger Zuschauer-Import (Skript), Handtest, erster Stream. P14-2 und die Kamerasteuerung können dann parallel laufen.
- **Regeln für paralleles Arbeiten** (Lotse stimmt ab):
  - jede Spur in einem eigenen Worktree und Branch; zusammengeführt wird über Lotse;
  - Bauen parallel ist erlaubt, aber `ctest` mit Fenstertests, Wiederholungsläufe, Start- und Dienstprüfung nur **eine Spur zur Zeit** und ohne Bau daneben (`CLAUDE.md`, Prüfstufen);
  - Berührungspunkte: `src/core/coreservice.cpp`, `src/common/protocol.h` (Version der Schnittstelle), `docs/08`. Spuren 2 und 3 halten ihre Kern-Anbindung klein; Lotse vergibt die Versionsnummern beim Zusammenführen;
  - Spur 3 baut auf `lotse/p12` (Ton aus P12).

### 10.3 Schritt 0 (Agent, nur lesen, je zu Beginn des betroffenen Abschnitts)

- c1: Shoutout (Grenzen, nur live), Benutzerinfo und Kanalinfo (Felder, Rechte), Bildserver der Profilbilder, Zählung der 500 Zeichen, `/` am Anfang, Echo eigener Nachrichten.
- a: Typnummern von Shoutout und Stream Online bei Streamer.bot, Argumentnamen (mit P13 Schritt 0), `%userProfileImageUrl%` und `%isFollowing%`.
- z0: Struktur der Dateien, Format von `quotes.dat` und `users.dat`, Log-Dateien, Verschlüsselung, Bezug des .NET SDK.
- Weicht etwas ab, das einen Grundschritt oder den Import trägt: Halt und Meldung an Lotse.

### 10.4 Tests je Abschnitt

| Abschnitt | Tests |
|---|---|
| a | `tst_aktionsformat` (mit `tests/daten/aktionen/`), `tst_platzhalter`, `tst_variablen` |
| t | `tst_medienliste`, `tst_tondecoder` (Tondateien), `tst_tonmischer` (freie Stimme, 8 gleichzeitig, Begrenzung), `tst_kernton`; `scripts/tonpruefung.py`, Mehrlast mit `scripts/tonleistung.py` |
| d | `tst_overlayserver`, `tst_browserquelle` (Laufzustand in Qt WebEngine), `tst_bildvergleich` (neuer Fall «Laufzustand» mit Text, Platzhalter und Bild), `tst_fensterbearbeiten` (Marke «live») |
| z1 | `tst_zuschauerimport` (9.5) |
| b | `tst_aktionsablauf`, `tst_ausloeser`, `tst_protocol`, `tst_kernverbindung`, neu `tst_kernaktionen` (Kern gegen den Twitch-Testserver; Änderung übersteht Neustart; ungültige Datei nicht überschrieben; stumm bei zweitem Socket; **kein Testtoken und kein Chat-Text im Protokoll**); 20 Wiederholungen von `tst_aktionsablauf` und `tst_kernaktionen` |
| c | `tst_twitchschreiben` (Testserver mit `/helix/chat/messages`, `/helix/users`, `/helix/channels`, `/helix/chat/shoutouts`, `/helix/chat/announcements`), `tst_kernaktionen` |
| e | `tst_aktionsansicht` (ohne Fenster), neu `tst_fensterchatbot`; `ui-consistency:verifying` durch einen getrennten Agenten |
| f | `tst_mitgeliefert` |
| o | `tst_obsquellen` erweitert, `tst_kernaktionen` |
| k | `tst_aktionsansicht`, `tst_fensterchatbot`, `tst_bildvergleich` (Übergang «weich» zu festen Zeitpunkten) |

## 11. Prüfstufe «Baustein» (je am Ende von P14-1 und P14-2, Ordner `build/pruefung-p14/`)

- `cmake --build build -j8`; `ctest --test-dir build` mit aktiver X11-Sitzung, 0 übersprungen; frischer Klon (Bau und `ctest`)
- `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p14/start/startpruefung`
- `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p14/dienst`
- **50 Wiederholungen:** `tst_aktionsablauf`, `tst_ausloeser`, `tst_kernaktionen`, `tst_twitchschreiben`, `tst_tonmischer`, `tst_kernton`, `tst_mitgeliefert`. Ohne Last daneben, nie zwei Fenstertests gleichzeitig.
- Last: 60 s lang 50 Chat-Nachrichten je Sekunde (20 % Befehle) und 5 Follows je Sekunde. Erwartet: Sendegrenze hält, «Alerts» verwirft über 50 mit Eintrag, Antwort auf `get_overlay_list` höchstens 25 ms; CPU des Kerns im Bericht.
- `scripts/tonpruefung.py` (20 Läufe), `scripts/tonleistung.py` mit beiden Zwecken.
- Stufe «Vor der Installation» vor P14-1 (Release-Bau, `scripts/installieren-pruefen.sh`), weil danach der Nutzer installiert und importiert.

## 12. Handtest (Regel 10)

**Prüft Lotse** (Kern und Fenster aus `build/`, eigenes `XDG_CONFIG_HOME`, installiertes Studio geschlossen, nie während eines Streams; Rechte nach Frage 5):

**L1 Bildschirmfotos.**
- Was: Reiter «Chat-Bot» in den Zuständen «aus», «läuft», «stumm», «Bot nicht angemeldet»; alle fünf Unterreiter mit Bereich rechts, Dialog «Warteschlangen», Zeile «Ton Chat-Bot», Marke «live», Bereich «Chat-Bot» im Dock «Eigenschaften». Vergleich mit dem Entwurf. Ablage nur im Scratchpad.
- Warum: Aussehen.

**L2 Chat über den echten Weg.**
- Was: Testaktion `!dsstest` (`dss-cli aktion-setzen`): Chat als Bot, dann als Kanal mit Antwort. Der Nutzer tippt den Befehl (N2). `dss-cli laeufe --folgen`: je eine `twitch.chat_sent` innerhalb von 2 s, genau eine Antwort je Konto, kein Echo löst etwas aus. Danach Testaktion löschen.
- Warum: echter Weg über Helix und TLS, Echo und Schleifenschutz.

**L3 Overlay in OBS.**
- Was: Overlay «Chat-Bot» als Browserquelle in «test 2»; «Begrüssung» per Probelauf; Bild der Quelle holen: Profilbild und Name sichtbar, nach 8 s weg. Neustart des Kerns während der Anzeige: danach nichts eingeblendet. Quelle danach löschen, Szene zurück auf «Szene».
- Warum: zweiter Zeichner in OBS mit Laufzustand, Platzhalter im Element und Profilbild.

**L4 Ton-Weg.**
- Was: «Ton Chat-Bot» auf einen mit dem Nutzer abgesprochenen Ausgang, Testaktion mit Ton: Zustand `playing`, Protokoll ohne Fehler, Mehrlast im Leerlauf.
- Warum: echter PipeWire-Ausgang.

**L5 Zuschauer-Import, Probelauf und Probe-Import.**
- Was: Export (z0) in den Datenordner der Version in Arbeit, `dss-cli zuschauer-import` als Probelauf, dann endgültig in der Version in Arbeit, zweiter Lauf. Vergleich der Anzahlen mit `--struktur`. Bericht nur mit Anzahlen.
- Warum: echte Datenmenge (rund 800 Zuschauer), die Tests mit erfundenen Daten nicht haben.

**L6 Neustart.**
- **Eigener Schritt:** `systemctl --user restart dss-core-entwicklung.service`.
- Was: Aktionen, Befehle, dauerhafte und übernommene Variablen unverändert, flüchtige und Laufzustand leer.
- Warum: Speichern im echten Dienst.

**L7 OBS-Quelle ein/aus (P14-2).**
- Was: In «test 2» eine eigene Testquelle anlegen, Testaktion schaltet sie ein, aus und um; danach löschen.
- Warum: echtes OBS.

**Prüft der Nutzer:**

**N1 Bedienung des Editors.**
- Was: eigene Aktion anlegen, Schritte hinzufügen, verschachteln (Wenn/Dann), ordnen, einen Schritt abschalten, Auslöser anhängen, kopieren, löschen; prüfen, dass alles sofort gespeichert ist (Studio schliessen und öffnen); «▶ Testen» als Probelauf und echt.
- Warum: Bedienung.

**N2 Mitgelieferte anpassen.**
- Was: Texte von `!discord` und `!regeln` eintragen und einschalten; `!dsstest` (für L2) und `!befehle` im eigenen Chat tippen; Antwort im Twitch-Chat ansehen.
- Warum: Konten, Aussehen im Twitch-Chat.

**N3 Eigenes Aussehen.**
- Was: Ein eigenes Overlay (oder das mitgelieferte umgestaltet) mit Alert und Begrüssung im Overlay-Editor gestalten, in den Schritten «Alert Follow» und «Begrüssung» die eigenen Elemente wählen, in OBS über «▶ Testen» ansehen.
- Warum: «alles anpassbar» kann nur der Nutzer mit seinem Overlay beurteilen.

**N4 Ton hören.**
- Was: Tondateien aus der alten Sammlung in die Mediathek holen, «Chat-Sound (Vorlage)» kopieren, Datei und Befehl wählen, im Chat auslösen; Lautstärke 50 % und 150 % vergleichen.
- Warum: Hören.

**N5 Zuschauer-Daten übernehmen.**
- **Eigener Schritt:** nach der Installation von P14-1 `~/DoNs_Streaming_Studio/scripts/zuschauer-import.sh` ausführen, Probelauf lesen, «j».
- Was: im Unterreiter «Variablen» einige bekannte Werte ansehen (zum Beispiel die eigenen First-Chatter-Zähler und die von zwei Zuschauern, die der Nutzer kennt).
- Warum: echte Daten (Regel 18); ob die Werte stimmen, weiss nur der Nutzer.

**N6 Begrüssung, First Chatters, Shoutout.**
- Was: mit einem zweiten Zuschauer-Konto (falls vorhanden) eine erste Nachricht schreiben; sonst «▶ Testen». `!so <Kanal>` tippen.
- Warum: echte Ereignisse. Ohne zweites Konto: «nicht geprüft» für den echten Weg.

**N7 Erster Stream.**
- Was: echte Follows, Subs, Raids, Cheers, Zeitnachrichten, Vorstellung beim Streamstart, First Chatters zählen mit den übernommenen Zählern weiter, Zurücksetzen bei Stream offline. Lotse liest danach Läufe und Protokoll.
- Warum: echte Ereignisse.

**N8 Ausschalten.**
- **Eigener Schritt:** «Chat-Bot aktiv» ausschalten, Befehl tippen. Erwartet: keine Antwort, in «Läufe der Aktionen» «Bot aus».
- Warum: Bedienung.

Was Lotse oder der Agent schon geprüft haben, wiederholt der Nutzer nicht.

## 13. Später und nicht vorgesehen

- **Später als feste Bausteine (vorgemerkt):**
  - **Countdown-Timer:** Schritte «Countdown starten/anhalten/fortsetzen/abbrechen/ändern» und ein Textelement mit der Restzeit; ersetzt das C#-Paket «Timer».
  - **Watchtime:** Zeit je Zuschauer zählen (`moderator:read:chatters`, in P13 schon geholt), auf der übernommenen Variablen `watchtime` aufbauend, Befehl zum Anzeigen.
- **Später:** Kanalpunkte-Effekte, Kamera-Fernbedienung (nach der Kamerasteuerung), Kategorie per Befehl, Umfrage und Vorhersage, Moderations-Knöpfe, weitere OBS-Schritte, OBS-Ereignisse als Auslöser, Zitat-Befehle (Daten schon übernommen), Gruppen von Elementen im Overlay-Editor (falls Frage 2 B nicht gewählt), Rückgängig im Editor der Aktionen, Chat als Dock im Reiter «Overlays».
- **Eigener Baustein:** Import der Aktionen aus Streamer.bot (liest Export und `data/`, übernimmt bekannte Typen, bewahrt Unbekanntes als `unsupported`; Zähler der Befehle sind schon da).
- **Vorerst nicht:** Subathon, YouTube Music, Golf, Hugs (Daten bleiben als Variablen erhalten), Q&A, Skript-Schritt.
- **Offen ohne Termin:** YouTube, Auslösen von aussen (Stream Deck). Das Abzeichen «Chat Bot» kommt mit P13 (Entscheid 2026-11-15); Schritt «Chat senden als Bot» nutzt dafür das App-Token des Kerns.

## 14. Pakete (Regel 5)

- **Studio:** kein neues Paket. Benutzt werden die Pakete aus P13 (Qt Network, WebSockets, OpenSSL-Backend), aus P12 (libpipewire-0.3, libavcodec, libswresample) und Qt Core (`QTextBoundaryFinder`). Ob libavcodec die Decoder für mp3, Vorbis und AAC enthält, prüft Abschnitt t (Version in den Bericht).
- **Nur für den Export (z0, bei Frage 4 A):** .NET SDK (Bezug und Version in Schritt 0, Installation durch den Nutzer) und LiteDB 5.0.21 von NuGet (MIT). Nicht im Bau des Studios.

## 15. Schätzung

| Teil | Agent | Lotse | Nutzer |
|---|---|---|---|
| P14-1 | ~8,5 Sitzungen Arbeit; mit drei Spuren neben P13 etwa 7,5 Sitzungen Wartezeit bis «streamfähig» einschliesslich P13 | Prüfungen je Abschnitt (~2 h), z0 (~1 h), Handtest L1–L6 (~2,5 h) | .NET SDK installieren (~5 min), Installation, N1–N6, N8 (~1,5 h), N7 im ersten Stream |
| P14-2 | ~2 Sitzungen | ~1 h, L7 | ~20 min |

## 16. Fragen an den Nutzer: beantwortet 2026-11-15

1. Overlays: **A**, nur «jetzt» (Laufzustand), weich ein- und ausblenden erst in P14-2.
2. Mehrteilige Meldungen: **A**, der Schritt nimmt mehrere Elemente; keine Gruppen im Overlay-Editor vor dem ersten Stream.
3. Test-Knopf: **A**, Probelauf als Standard, Kästchen «echt ausführen».
4. Zuschauer-Daten: **A**, einmaliger Export mit .NET-Werkzeug (Nutzer installiert das .NET SDK).
5. Go: **A**, P14-1 mit bis zu drei Agenten parallel neben P13; Handtest-Rechte für Lotse wie beschrieben (eingetragen in `CLAUDE.md`, «Umgebung»).

## 17. Risiken und `[OFFEN]`

| Risiko oder offene Frage | Gegenmassnahme oder Stand |
|---|---|
| Struktur und Feldnamen der Zuschauer-Daten unbekannt | z0 (`--struktur`) vor z1; Zuordnung 9.3 wird danach bestätigt |
| LiteDB-Log-Datei nicht kopiert: neuere Werte fehlen | Kopie mit `*-log.db`; Lesen mit der Original-Bibliothek (Frage 4 A) |
| Datenbank verschlüsselt (Passwort) | z0; dann Halt und Meldung an Lotse |
| Einheit von `watchtime` | `[OFFEN]` bis zum Baustein Watchtime; Wert bleibt unverändert |
| Zuschauer ohne Twitch-ID | Auflösen über Twitch, Rest ins Archiv, Anzahl im Bericht |
| Paralleles Bauen: Konflikte in `coreservice.cpp`, `protocol.h`; Last bei Prüfungen | eigene Worktrees, kleine Kern-Anbindung je Spur, Prüfungen nacheinander (10.2) |
| Editor speichert sofort, kein Rückgängig | «Zurücksetzen» für mitgelieferte, Sicherung bei der Installation; Rückgängig später |
| Argumentnamen und Typnummern von Streamer.bot nur teilweise belegt | Schritt 0; vorläufige Namen als `[OFFEN]` |
| Echo eigener Nachrichten nicht dokumentiert | doppelter Schutz (4.1), L2 |
| Shoutout: Grenzen und «nur live» nicht geprüft | Schritt 0; Ablehnung steht im Lauf |
| Decoder öffnet Tondateien ohne Bildspur womöglich nicht | Abschnitt t |
| Zweiter `pw_stream` erhöht die Mehrlast über die Grenze aus P12 | nur offen bei gewähltem Ausgang; Messung in t |
| Profilbild-Server von Twitch nicht geprüft | Schritt 0; ohne Bild bleibt das Bild des Elements |
| Zwei Versionen handeln doppelt | Stumm-Regel 3.4 |
| Geteilter Chat | nur auf den eigenen Kanal reagieren |

## Annahmen und Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| `dss_actions` ohne Gui und ohne Netz, Dienste über Schnittstellen | Regel 9 und 14; testbar ohne Twitch, OBS und Gerät |
| Lauf als Zustandsmaschine im Hauptthread; Lauf mit dem Stand bei seinem Beginn | keine Sperren; sofortiges Speichern stört laufende Abläufe nicht |
| Schritte verschachtelt gespeichert, unbekannte Felder und Typen bleiben | wie Fassung 24; Import ohne Umbau |
| Platzhalter `%arg%`, `~global~` wie Streamer.bot | eine Schreibweise; importierte Texte ohne Umschrift |
| Platzhalter im Text eines Elements | Wortlaut und Aussehen bleiben im Overlay-Editor («alles anpassbar») |
| Schritt mit mehreren Elementen statt Gruppen im Overlay | kein Umbau des Overlay-Formats vor dem ersten Stream (Frage 2) |
| Variablen mit beliebigem Namen, Zuordnung über Twitch-ID, Herkunft je Wert | Entscheid 2026-11-15; Umbenennen eines Zuschauers trennt nichts; erneuter Import erkennt Änderungen im Studio |
| Namen aus Streamer.bot unverändert | mitgelieferte und später importierte Aktionen finden die übernommenen Werte |
| Archiv für Daten ohne Twitch-ID oder ohne einfachen Wert | «alles übernehmen», ohne das Modell für Kick und YouTube jetzt zu bauen |
| Endgültiger Import in die echten Daten durch den Nutzer per Skript | Regel 18 |
| Tests nur mit erfundenen Daten | Entscheid 2026-11-15 (Datenschutz) geht Regel 7 vor |
| Export-Werkzeug ausserhalb des Baus | das Studio bleibt ohne .NET |
| OBS-Schritt in P14-2 | keine mitgelieferte Aktion braucht ihn; früher streamfähig |
| Bereiche statt Docks in «Live» und im Editor (P14-1) | echte Docks im Reiter bräuchten ein zweites Hauptfenster |
| Kein Unterreiter «Warteschlangen» | fünf Unterreiter wie im Entwurf; Zustand steht in «Läufe der Aktionen» |
| Eigene Sendegrenze 1/s und 20/30 s je Konto | Sperre 1 h bei Überschreitung |
| Ton über eigenen Zweck `aktionen`, Strom nur bei gewähltem Ausgang | Entscheid 2026-11-09; Mehrlast nur bei Nutzung |
| Profilbilder lädt der Kern | beide Zeichner zeigen dieselbe Datei (K-07); OBS lädt nichts aus dem Netz |
| Mitgeliefertes Overlay schlicht, ohne Grafiken des Entwurfs | Entscheid «alles anpassbar», neutraler Startpunkt |
| Hauptschalter «Chat-Bot aktiv» Standard aus | kein ungewollter Bot nach dem Update |
| Neuer Schalter `DSS_CORE_ACTIONS` | handelt selbständig öffentlich, Regel 8 |

## Quellen

- **Lokal gelesen (2026-11-15):**
  - `CLAUDE.md`, `.claude/HANDOFF.md`, Entscheide des Nutzers vom 2026-11-15 (nachgeführt)
  - `docs/01-festlegungen.md`, `docs/03-funktionen-entwurf.md`, `docs/04-offene-punkte.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`
  - `docs/archiv/prompts/P13-twitch.md`
  - Entwurf `entwuerfe/chatbot/` auf `lotse/entwurf-chatbot` (Commit 89d47f0): `index.html`, `1-gesamtansicht.html`, `2a-editor-im-reiter.html`, `teil-editor.html`, `3-befehle.html`, `5-overlay-vorschau.html`
  - `src/app/mainwindow.cpp:111-226`, `src/app/headerbar.cpp:160-200`, `src/app/settingsdialog.h`, `src/app/docks.h`, `src/common/overlay.h`, `src/common/coresettings.h`, `src/common/mediaimport.cpp:70-95`, `src/common/protocol.h:51`, `src/audio/mixer.h`, `src/media/audiodecoder.h`, `src/obs/obssources.cpp`, `src/obs/obsconnection.cpp` (Namen der Anfragen)
  - Recherchen von Lotse (2026-11-09, nicht im Repo): «Chat-Bot als Aktionssystem», «So nutzt der Nutzer Streamer.bot»
  - Nicht gelesen: der Ordner `data/` von Streamer.bot (Angaben aus dem Auftrag von Lotse)
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
  - https://docs.streamer.bot/guide/core/backup
  - https://docs.streamer.bot/api/sub-actions
  - https://docs.streamer.bot/api/sub-actions/core/logic/if-else
  - https://docs.streamer.bot/api/sub-actions/core/actions/run-action
  - https://docs.streamer.bot/api/sub-actions/core/globals/global-set
  - https://docs.streamer.bot/api/sub-actions/core/sounds/play-sound
  - https://docs.streamer.bot/api/triggers/core/commands/command-triggered
  - https://docs.streamer.bot/guide/platforms/twitch
  - https://streamer.bot/changelogs (LiteDB 5.0.21)
  - https://raw.githubusercontent.com/PedroZamecki/streamerz/main/src/model.rs (Dritte, MIT)
  - https://raw.githubusercontent.com/PedroZamecki/streamerz/main/src/types.rs (Dritte, MIT)
  - https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md (für P10c gelesen)
- **Nicht gelesen (Schritt 0):**
  - https://github.com/litedb-org/LiteDB (Dateiformat, Log-Datei, `ReadOnly`, Verschlüsselung)
  - https://www.nuget.org/packages/LiteDB/5.0.21
  - Bezug des .NET SDK für Debian 13 (https://learn.microsoft.com/dotnet/core/install/linux-debian)
  - https://dev.twitch.tv/docs/api/reference/#send-a-shoutout
  - https://dev.twitch.tv/docs/api/reference/#get-users
  - https://dev.twitch.tv/docs/api/reference/#get-channel-information
  - Abschnitte `GetSceneItemList`, `GetSceneItemId`, `GetSceneItemEnabled`, `SetSceneItemEnabled` in der Doku von obs-websocket
  - Seiten der Twitch-Auslöser und der Sub-Action «Get User Info for Target» unter https://docs.streamer.bot/api/
  - https://doc.qt.io/qt-6.8/qtextboundaryfinder.html
- **Am 2026-11-15 nicht im Web geprüft:** Aussagen zu Twitch, Streamer.bot und LiteDB stützen sich auf die Lesungen vom 2026-11-09 und den Auftrag von Lotse. Schritt 0 bestätigt sie gegen die aktuelle Doku (Regel 4).
