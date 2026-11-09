# P14 – Basisfunktion Chat-Bot (Plan)

Plan des Projektleiter-Agenten vom 2026-11-09. Grundlage:
- Bauplan P14, Punkt 3 in `docs/04-offene-punkte.md` (Umfang, Streamer.bot-Import)
- `docs/03-funktionen-entwurf.md`, Abschnitt «Chat-Bot»: «Zuerst: Twitch-Anmeldung, Chat ansehen, Nachrichten senden, Commands verwalten und ausführen (Name, Befehl, Berechtigung, Cooldown, Antwort), Protokoll»
- `docs/01-festlegungen.md` («Module», YouTube offen)
- Regeln 4, 8, 9, 12 und 14 (`CLAUDE.md`)
- Plan P13 (`docs/archiv/prompts/P13-twitch.md`: Ereignisfluss, Twitch-Anmeldung, Reiter «Chat-Bot»)
- Plan P12 (`docs/archiv/prompts/P12-ton.md`: Wunsch des Nutzers, später Töne vom Chat-Bot über weitere USB-Soundkarten auszugeben, Ausgänge im Dock «Steuerung»)

**Voraussetzung:** P13 ist gebaut und abgenommen, und der Nutzer hat sich angemeldet (P13 N1, N2). Ausführung in zwei Sitzungen des Agenten (bei Frage 1 B: drei), danach Handtest.

## Entscheide des Nutzers (bisher)

- Module sind fest eingebaut. Jedes Modul nutzt den Ereignisfluss, es gibt keine Plugins (Regel 14).
- Zugangsdaten stehen nie im Klartext (Regel 12). Das Muster ist F1 mit `systemd-creds --user`.
- Die Reihenfolge ist P12 Ton, P13 Twitch, P14 Chat-Bot (2026-11-09).
- Der Chat-Bot soll später Töne über weitere USB-Soundkarten ausgeben. Die Ausgänge liegen im Dock «Steuerung», je Zweck oder Modul ein eigener Ausgang (2026-11-09, Antwort 2 zu P12). In P14 gibt es noch keinen Ton, das Format ist aber dafür vorbereitet (3.2).
- `[OFFEN]`: Die Antworten des Nutzers auf die Fragen in P13 stehen nicht im Repo. Dieser Plan geht von den Empfehlungen A aus: Anmeldung mit Code, Kanal-Konto, alle Ereignisse, Liste «Ereignisse» im Reiter.

## Wichtigste Befunde

| Befund | Beleg |
|---|---|
| Senden: `POST https://api.twitch.tv/helix/chat/messages`. Pflichtfelder sind `broadcaster_id`, `sender_id` («must match the user ID in the user access token») und `message`, dazu wahlweise `reply_parent_message_id`. «The message is limited to a maximum of 500 characters.» | https://dev.twitch.tv/docs/api/reference/#send-chat-message |
| Mit User-Token reicht das Recht `user:write:chat`. Mit App-Token braucht es zusätzlich `user:bot` vom Absender und `channel:bot` vom Kanal (oder Moderator-Status) | dieselbe Seite |
| Die Antwort hat `message_id`, `is_sent` und `drop_reason` (`code`, `message`). Fehler: 400 (u. a. `for_source_only` mit User-Token), 401 (Recht fehlt), 403 («sender is not permitted»), 422 («message is too large»), 429 | dieselbe Seite |
| Das Abzeichen «Chat Bot» gibt es nur mit **App-Token** über die API, mit `channel:bot` oder Moderator-Status, und wenn «the chatbot’s user account is not the channel’s broadcaster». «chatbots that utilize User Access Tokens will not appear in chat with the Chat Bot Badge» | https://dev.twitch.tv/docs/chat/ |
| Grenzen: «20 Messages per 30 seconds», «100 messages per 30 seconds» für Broadcaster, Mod und VIP, «1 message per second per channel». Bei Überschreitung gilt: «Twitch ignores the bot messages for 1 hour». Laut derselben Seite hat die Send-API einen eigenen Bucket. Ob die Chat-Grenzen auch dort gelten, steht nicht ausdrücklich da | dieselbe Seite |
| Ein Echo eigener Nachrichten als `channel.chat.message` wird in der Doku nicht erwähnt. **Nicht geprüft** | https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/#channelchatmessage |
| Geteilter Chat: Für `source_broadcaster_user_id` gilt «Is null when the message happens in the same channel as the broadcaster». Mit User-Token gehen gesendete Nachrichten «to all channels in the shared chat session», `for_source_only` geht nur mit App-Token | https://dev.twitch.tv/docs/eventsub/eventsub-reference/#chat-message, https://dev.twitch.tv/docs/api/reference/#send-chat-message |
| `message_type`: `text`, `channel_points_highlighted`, `channel_points_sub_only`, `user_intro`, `power_ups_message_effect`, `power_ups_gigantified_emote` | https://dev.twitch.tv/docs/eventsub/eventsub-reference/#chat-message |
| Abzeichen: Belegt sind die Beispiele `moderator`, `subscriber`, `vip`, `bits`, `staff`. Für `broadcaster`, `founder` und `lead_moderator` gibt es keinen Beleg, ebenso nicht dafür, dass «Founder» das Abonnenten-Abzeichen ersetzt. **Nicht geprüft** | https://dev.twitch.tv/docs/chat/send-receive-messages/, https://dev.twitch.tv/docs/api/reference/#get-global-chat-badges |
| Ansagen: `POST …/helix/chat/announcements` mit `moderator:manage:announcements`, «One announcement may be sent every 2 seconds». Nicht in P14 | https://dev.twitch.tv/docs/api/reference/#send-chat-announcement |
| Streamer.bot-Befehle haben diese Felder: Name, Enabled, Group, Mode `Basic` oder `Regex`, Location `Start`, `Exact` oder `Anywhere`, mehrere Auslöser «one item per line», Case Sensitive, Ignore Bot Account, Persist Counter, Sources, Global Cooldown, User Cooldown, Permissions mit `Allow`/`Deny` und Gruppen. «The Broadcaster Account is always exempt from permission settings.» Die Antwort kommt immer über eine Action | https://docs.streamer.bot/guide/commands |
| Streamer.bot-Timer haben Interval, Random, Lines und Repeat. Das Bot-Konto ist «an optional second connection … can only send chat messages or whispers». Der Export ist «a UUEncoded string» | https://docs.streamer.bot/guide/core/timers, https://docs.streamer.bot/guide/platforms/twitch, https://docs.streamer.bot/guide/core/import-export |
| Der Kern läuft nur, solange ein Fenster offen ist (Beenden, Entscheid 1 in P07). Der Bot antwortet also nur bei offenem Studio | `docs/08-kern-schnittstelle.md:141-145` |
| Der Reiter «Chat-Bot» ist heute ein `QWidget` mit «Noch nicht vorhanden» im `QStackedWidget` der Module. P13 macht daraus die Liste «Ereignisse» | `src/app/mainwindow.cpp:182-191` |

## 1. Umfang (Frage 1, Empfehlung A)

**P14 (A):** genau der Teil «Zuerst» aus `03-funktionen-entwurf.md`.

1. **Chat ansehen:** Chat-Nachrichten und Ereignisse von P13 in einer Liste.
2. **Nachrichten senden:** aus dem Studio in den eigenen Chat.
3. **Befehle mit festen Antworten:**
   - Name, ein oder mehrere Auslöser, Antworttext mit Platzhaltern;
   - wer den Befehl auslösen darf;
   - Abklingzeit für den ganzen Kanal und je Person;
   - wahlweise als Antwort auf die auslösende Nachricht.
4. **Protokoll:** ausgeführte und abgewiesene Befehle mit Grund, Sendefehler.

**Danach (P14b, nur bei Frage 1 B schon in P14):** Zeitnachrichten und Dank für Ereignisse (Abschnitt 6).

**Nicht in P14:**
- Streamer.bot-Import: eigener Baustein. Das Format in 3.2 übernimmt dafür die Felder von Streamer.bot.
- YouTube: neuer offener Punkt.
- Töne: brauchen mehrere Ausgänge aus P12.
- Zähler, Regex, Variablen, Actions und Queues: kommen später als Streamer.bot-Funktionen.
- Ansagen (`announcements`) und Flüstern.

**Begründung für A:**
- Es ist die kleinste Einheit, die läuft und geprüft ist.
- Zeitnachrichten und Dank nutzen denselben Sendeweg und dieselben Platzhalter und kommen ohne Umbau dazu.

**Gegenargument:** Der Dank für Ereignisse kostet wenig mehr, weil P13 die Ereignisse schon liefert. Deshalb gibt es Variante B.

## 2. Schreiben in den Chat

### 2.1 Konto (Frage 2, Empfehlung A: Kanal-Konto)

| Variante | Anmeldung | Grenze | Aussehen im Chat | Aufwand |
|---|---|---|---|---|
| **A Kanal-Konto** | die Anmeldung aus P13, zusätzlich mit dem Recht `user:write:chat`, also einmal neu anmelden | 100 pro 30 s (Broadcaster) | Name des Kanals, kein Bot-Abzeichen | klein |
| B eigenes Bot-Konto mit Code | zweite Anmeldung mit Code (DCF) im Bot-Konto, nur `user:write:chat`, Datei `twitch-bot-token.cred` | 20 pro 30 s, als Mod 100 | Name des Bot-Kontos, kein Abzeichen («User Access Tokens will not appear … with the Chat Bot Badge») | +½ Sitzung |
| C Bot-Konto mit Abzeichen | App-Token aus Client-ID und **Client Secret** (verschlüsselt wie F1); `user:bot` vom Bot, `channel:bot` vom Kanal | wie B | Bot-Abzeichen | +1 Sitzung. Ob die App aus P13 («Public») dafür als «Confidential» neu registriert werden muss, ist **nicht geprüft** |

Für alle Varianten gilt:
- Gelesen wird der Chat weiter über die Anmeldung des Kanals aus P13.
- `TwitchChat::send` nimmt das Token des sendenden Kontos.

### 2.2 Senden (`src/twitch/twitchchat.{h,cpp}`)

- **Aufruf:**
  - `send(text, replyToMessageId)` geht über `helixclient` aus P13 (401: einmal erneuern, 429: `Ratelimit-Reset` abwarten).
  - Das Ergebnis kommt als Ereignis auf den Fluss, wie in P13 1.1 festgelegt.
- **Eigene Grenze im Kern**, sicher für jede Rolle:
  - höchstens 1 Nachricht pro Sekunde und 20 pro 30 s (gleitend);
  - Warteschlange mit höchstens 10 Einträgen; was älter als 30 s ist, wird verworfen und ins Protokoll geschrieben.
  - Begründung: Bei Überschreitung ignoriert Twitch den Bot 1 h lang.
- **Länge:**
  - Die Antwort wird nach dem Einsetzen der Platzhalter auf 500 Zeichen gekürzt, nie mitten in einem Zeichen. Gekürzt wird mit `QTextBoundaryFinder`, Qt Core.
  - Wie Twitch die Zeichen zählt, ist **nicht geprüft** (Schritt 0). Bis dahin gilt die vorsichtige Zählung in UTF-16-Einheiten.
- **Text am Anfang:** Antworten, die mit `/` oder `.` beginnen, weist der Editor ab («Twitch-Befehle sind nicht möglich»). Ob die API sie ausführen würde, ist **nicht geprüft**.
- **Geteilter Chat:** Mit User-Token gehen Antworten an alle Kanäle der Sitzung. Der Bot reagiert nur auf Nachrichten mit `source_broadcaster_user_id` gleich `null` (eigener Kanal).

### 2.3 Keine Schleifen

- Eigene Nachrichten werden erkannt, egal ob das Echo vor oder nach der HTTP-Antwort kommt:
  - an der `message_id` aus der Antwort (die letzten 100);
  - zusätzlich an Absender und gleichem Text innerhalb von 10 s.
  - So kann eine Antwort, die mit «!» beginnt, keinen Befehl auslösen.
- Bei Variante B und C löst das Bot-Konto nie einen Befehl aus.
- **Nur eine Version antwortet:**
  - Der Kern der Version in Arbeit schickt nichts, solange der Kern der Version im Einsatz läuft, geprüft am Socket wie bei «übrig gebliebener Socket».
  - Zustand: «stumm: Version im Einsatz läuft». Sonst kämen Antworten doppelt.

### 2.4 Rechte

- `user:write:chat` kommt zu den nötigen Rechten aus P13 2.4. Fehlt es, zeigt der Kern wie in P13: «Rechte fehlen: Chat schreiben. Neu anmelden.»
- Lesen und Ereignisse laufen bis dahin weiter.

## 3. Bibliothek, Format, Kern

### 3.1 `src/chatbot/` (`dss_chatbot`)

Qt Core, kein Qt Gui, kein Netz, kein `protocol.h`. Die Bibliothek bindet `dss_events` ein (Regel 9 und 14).

| Datei | Inhalt |
|---|---|
| `chatbotconfig.{h,cpp}` | Format lesen und schreiben, streng wie `liste.json` |
| `commandmatcher.{h,cpp}` | Auslöser finden: **Anfang** = erstes Wort gleich dem Auslöser (`!d` trifft nicht `!discord`), **Genau** = ganze Nachricht; Gross- und Kleinschreibung egal |
| `chatroles.{h,cpp}` | Rolle aus `badges[]`: `broadcaster` bzw. `user_id` = Kanal, `moderator`, `vip`, `subscriber`; `founder` und `lead_moderator` nach Schritt 0 |
| `placeholders.{h,cpp}` | Platzhalter einsetzen, kürzen auf 500 |
| `cooldowns.{h,cpp}`, `sendqueue.{h,cpp}` | Abklingzeiten, Grenze und Warteschlange, mit einer Uhr, die sich im Test ersetzen lässt |
| `chatbot.{h,cpp}` | abonniert `twitch.chat_message`, sendet über die Schnittstelle `ChatSender`, veröffentlicht `chatbot.*` |

Der Kern verbindet `ChatSender` mit `TwitchChat`. Die Tests nutzen einen Ersatz.

### 3.2 Speicherformat `<Datenordner>/chatbot.json` (neu, `format_version` 1)

```json
{
  "format_version": 1,
  "enabled": false,
  "commands": [
    {
      "id": "UUID",
      "name": "Discord",
      "enabled": true,
      "triggers": ["!discord", "!dc"],
      "match": "start",
      "allowed": ["everyone"],
      "cooldown_channel_s": 5,
      "cooldown_user_s": 0,
      "actions": [ { "type": "chat", "text": "Hier gehts zum Discord, {name}: …", "reply": false } ]
    }
  ]
}
```

- **Felder:**
  - `match`: `start` oder `exact`.
  - `allowed`: `everyone` oder eine Auswahl aus `subscriber`, `vip`, `moderator`. Der Kanal darf immer und hat keine Abklingzeit, wie bei Streamer.bot.
  - Auslöser sind ein Wort ohne Leerzeichen und über alle Befehle eindeutig.
- **`actions`:** Version 1 erlaubt genau eine Aktion `chat`. Später kommen `sound` (mit Ausgang aus dem Dock «Steuerung», P12) und Streamer.bot-Actions dazu, ohne Umbau des Aufbaus.
- **Platzhalter:**
  - `{name}`: Anzeigename;
  - `{eingabe}`: Text nach dem Auslöser;
  - `{ziel}`: erstes Wort der Eingabe ohne «@», sonst `{name}`;
  - `{kanal}`;
  - `{{` und `}}` für geschweifte Klammern.
  - Unbekannte Platzhalter: Warnung beim Speichern, der Text bleibt stehen.
- **Datei:**
  - Fehlt die Datei, gibt es keine Befehle und keine Umwandlung.
  - Eine ungültige Datei wird gemeldet und nicht überschrieben (Muster `liste.json`). Der Bot bleibt dann aus, Änderungen werden abgewiesen.
  - `dss-core --format-pruefen` kennt die Datei. `format_version` > 1 heisst neueres Format (Exit-Code 5).
- **Speichern:** verzögert über `SaveScheduler`, wie bei den Overlays.
- **Keine Zugangsdaten in der Datei.**

### 3.3 Ereignisse (Typenliste `src/events/eventtypes.h`)

| Typ | `data` |
|---|---|
| `twitch.chat_message` (erweitert) | zusätzlich `source_broadcaster_id` (oder `null`) und `message_type` |
| `twitch.chat_sent` (neu) | `message_id`, `text`, `reply_to`, `requested_by` (`chatbot` oder `window`) |
| `twitch.chat_dropped` (neu) | `text`, `code` (Twitch `drop_reason.code`, `http_429`, `queue_full`, `too_old`, `not_authorized`), `message` |
| `chatbot.command_run` (neu) | `command_id`, `command_name`, `user_login`, `user_name`, `trigger_message_id`, `result` (`sent`, `cooldown`, `not_allowed`, `disabled`, `bot_off`, `muted`) |

- Für den Journal-Eintrag gilt wie in P13: keine Chat-Texte und keine Namen von Zuschauern, nur Befehlsname, Ergebnis und Fehler.
- Das sichtbare Protokoll im Reiter entsteht aus diesen Ereignissen (Verlauf 200 aus P13).

### 3.4 Kern und Schnittstelle (Version +1)

- **Kern an Fenster:**
  - `chatbot_config` mit dem ganzen Inhalt von `chatbot.json` und `file_error`, nach `hello` und bei jeder Änderung an alle;
  - `chatbot_status` mit `state` (`off`, `running`, `muted`, `error`), `message`, `sender_login`, `queue_length`.
- **Fenster an Kern**, jeweils mit `request_id` und Antwort im Muster von `change_result`:
  - `chatbot_set_enabled`;
  - `chatbot_put_command` (anlegen oder ändern, ganzes Objekt);
  - `chatbot_remove_command`;
  - `twitch_send_chat` (`text`).
- **Ereignisse:** wie in P13 über `subscribe_events`.
- **`dss-cli`:**
  - `chatbot-befehle` listet die Befehle;
  - `chatbot-befehl-setzen DATEI.json` und `chatbot-befehl-loeschen ID`, Werkzeug für Lotse;
  - `chat-senden TEXT`, nur im Handtest mit Erlaubnis aus Frage 4.
- **Schalter `DSS_CORE_CHATBOT`** (Standard ON): Bei OFF gibt es keinen Bot, der Reiter zeigt nur den Chat. Begründung: Der Bot schreibt selbständig in einen öffentlichen Chat, das ist ein riskanter Teil nach Regel 8.

## 4. Bedienung im Reiter «Chat-Bot» (Frage 3, Empfehlung A)

- **Oben:**
  - Schalter «Chat-Bot aktiv», beim ersten Start **aus**;
  - Zustandstext «läuft als <Konto>», «stumm: Version im Einsatz läuft» oder «Rechte fehlen: Chat schreiben».
- **Links «Chat»:**
  - Liste mit Zeit, Name in Chatfarbe und Text, höchstens 200 Einträge, nach unten mitlaufend. Ereignisse wie Follow und Sub als graue Zeile, eigene Bot-Antworten erkennbar.
  - Darunter ein Eingabefeld mit Zähler bis 500 und «Senden».
  - Nachrichten aus dem Studio lösen keine Befehle aus (Annahme).
- **Rechts Unterreiter «Befehle»:**
  - Tabelle mit Spalten «Aktiv», «Auslöser», «Antwort» (gekürzt) und «Wer darf».
  - Darunter der Bereich zum Bearbeiten:
    - «Name»;
    - «Auslöser» (eine Zeile je Auslöser);
    - «Nur genau dieser Text»;
    - «Antwort» mit Liste der Platzhalter und Zähler;
    - «Als Antwort auf die Nachricht»;
    - «Wer darf»: Kästchen «Alle», «Abonnenten», «VIPs», «Moderatoren»;
    - «Abklingzeit Kanal (s)», «Abklingzeit je Person (s)»;
    - Knöpfe «Neu», «Speichern», «Löschen».
  - Konflikt bei Auslösern und ungültige Platzhalter werden beim Speichern gemeldet.
- **Rechts Unterreiter «Protokoll»:** Zeit, Befehl, Person, Ergebnis mit Grund, dazu Sendefehler.
- **Aussehen und Regeln:**
  - Aussehen wie der Rest («Yami», Open Sans).
  - Die Logik der Ansicht liegt in `dss_common` und ist ohne Fenster getestet (Regel 9).
  - Vor dem Commit «Fenster» führt der Agent die Prüfung der Oberflächenmuster durch (`ui-consistency:finding-patterns`) und hält die Checkliste im Bericht fest.

## 5. Commits und Prüfung

**Schritt 0 (Agent, nur lesen):**
- Quellen unten lesen. Bestätigen:
  - Senden mit User-Token des Kanals;
  - Echo eigener Nachrichten;
  - Zählung der 500 Zeichen;
  - `/` am Anfang;
  - `set_id` für Broadcaster, Founder und Lead Moderator;
  - bei Frage 1 B zusätzlich `GET /helix/streams` (Recht, Antwort bei offline).
- Die Streamer.bot-Daten am Ort aus Frage 1 nur lesen und die Felder mit 3.2 abgleichen.
- Weicht etwas ab, das das Senden trägt: Halt und Meldung an Lotse.

**Commits (Regel 8):**
1. `twitchchat`, `user:write:chat`, Typen aus 3.3, Testserver erweitert:
   - `/helix/chat/messages` hält den Inhalt fest und antwortet mit `is_sent`, `drop_reason`, 401, 403, 422 oder 429 mit `Ratelimit-Reset`;
   - Echo wahlweise vor oder nach der HTTP-Antwort.
   - Test `tst_twitchchat`.
2. `chatbotconfig` mit `tests/daten/chatbot/` (gültig, ungültig, neueres Format, LIESMICH). Test `tst_chatbotformat`.
3. Logik des Bots (3.1). Test `tst_chatbotlogik` ohne Netz, mit ersetzter Uhr:
   - Anfang und Genau;
   - Rollen aus Abzeichen;
   - Kanal ausgenommen;
   - Abklingzeiten;
   - Platzhalter einschliesslich Emoji und Kürzen an der Zeichengrenze;
   - Schleife in beiden Reihenfolgen;
   - geteilter Chat;
   - Grenze 1/s und 20/30 s, Warteschlange voll bzw. zu alt;
   - Schalter aus.
4. Kern:
   - Bot in `CoreService`, `chatbot.json`, Schnittstelle +1, `dss-cli`, Schutz vor doppelten Antworten (2.3), `--format-pruefen`, Schalter `DSS_CORE_CHATBOT`;
   - Tests `tst_protocol`, `tst_kernverbindung`, neu `tst_kernchatbot` mit dem Kern als eigenem Prozess gegen den Testserver:
     - Befehl über den Chat ergibt einen POST mit richtigem Inhalt;
     - Änderung über den Socket ist gespeichert und überlebt einen Neustart;
     - ungültige Datei wird nicht überschrieben;
     - stumm, wenn ein zweiter Socket «Einsatz» antwortet;
     - **kein Testtoken und kein Chat-Text im Protokoll**.
5. (nur bei Frage 2 B oder C) zweites Konto. Test in `tst_twitchanmeldung` bzw. `tst_kernchatbot`.
6. Fenster nach Abschnitt 4. Test `tst_fensterbearbeiten` (oder neu `tst_fensterchatbot`).
7. Dokumente:
   - `01` (Chat-Bot, Entscheide), `03`, `04` (Punkt 3 teilweise ins Archiv; neue Punkte: YouTube, Import, Bot ohne Fenster), `05`, `06` (`chatbot.json`), `08`;
   - Bericht `docs/archiv/berichte/P14-bericht.md`.

**Prüfstufe «Baustein»** (Ordner `build/pruefung-p14/`):
- `cmake --build build -j8`
- `ctest --test-dir build` mit aktiver X11-Sitzung, 0 übersprungen
- frischer Klon (Bau und `ctest`)
- `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p14/start/startpruefung`
- `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p14/dienst`
- **50 Wiederholungen** (Zeitgrenzen und Netz): `tst_twitchchat`, `tst_kernchatbot`, `tst_kernverbindung`. Ohne Last daneben, nie zwei Fenstertests gleichzeitig.
- Last:
  - Der Testserver schickt 60 s lang 50 Nachrichten pro Sekunde, davon 20 % Befehle.
  - Erwartet: Grenze hält (höchstens 20 POST pro 30 s), Warteschlange verwirft mit Protokoll, Antwort auf `get_overlay_list` höchstens 25 ms.
  - CPU des Kerns als Messwert in den Bericht.

## 6. P14b – Zeitnachrichten und Dank (nur bei Frage 1 B in P14, sonst eigener Baustein)

**Zeitnachrichten:**
- Felder: Name, Text, «alle N Minuten» (mindestens 5), «mindestens M Chat-Nachrichten seit der letzten» (Standard 5), «nur im Stream» (Standard ja).
- Zwischen zwei Zeitnachrichten liegen mindestens 60 s.
- Den Zustand «im Stream» liefern `twitch.stream_online` und `twitch.stream_offline`, beim Start dazu `GET /helix/streams` (Schritt 0).

**Dank für Ereignisse:**
- Je Typ (Follow, Sub, Resub, Geschenk, Raid, Cheer, Kanalpunkte) ein Text mit Platzhaltern:
  - `{name}`, `{monate}`, `{stufe}`, `{anzahl}`, `{bits}`, `{zuschauer}`, `{belohnung}`, `{eingabe}`.
- Eigene Abklingzeit: Follow standardmässig 10 s, Überzählige werden mit Protokoll verworfen. Raid ab einer Mindestzahl Zuschauer.
- Einzelne Abos aus einem Geschenk (`is_gift`) bekommen keinen eigenen Dank, nur `twitch.gift`.

**Format:** `timers` und `reactions` in `chatbot.json`. Werden sie erst nach P14 gebaut, steigt `format_version` auf 2 und die Umwandlung legt leere Listen an.

**Weitere Unterreiter** «Zeitnachrichten» und «Dank».

**Tests:** in `tst_chatbotlogik` und `tst_kernchatbot`.

**Aufwand:** etwa +1 Sitzung.

## 7. Handtest (Regel 10)

**Prüft Lotse** (Kern und Fenster aus `build/`, eigenes `XDG_CONFIG_HOME`, installiertes Studio geschlossen):

**L1 Bildschirmfotos.**
- Was: Reiter «Chat-Bot» in den Zuständen «aus», «läuft», «Rechte fehlen» und «stumm», jeweils mit «Chat», «Befehle» (mit Fehlermeldung) und «Protokoll». Ablage nur im Scratchpad.
- Warum: Aussehen.

**L2 Echter Weg.**
- Was:
  - Lotse legt mit `dss-cli chatbot-befehl-setzen` den Testbefehl `!dsstest` an, Antwort «DSS-Test ok, {name}».
  - Der Nutzer tippt ihn in N2. Lotse liest `dss-cli ereignisse --folgen`.
  - Erwartet: `twitch.chat_sent` mit `is_sent` innerhalb von 2 s, genau eine Antwort, kein `twitch.chat_dropped`.
  - Danach löscht Lotse den Testbefehl.
- Warum: echter Weg über Helix und TLS, den `ctest` nicht hat.

**L3 Senden durch Lotse** (nur mit Frage 4 A, nie während eines Streams).
- Was: `dss-cli chat-senden "DSS-Test"`. Erwartet: `twitch.chat_sent`. Ein Echo löst nichts aus.
- Warum: Echo und Schleifenschutz mit echtem Twitch (Doku schweigt).

**L4 Neustart.**
- **Eigener Schritt:** `systemctl --user restart dss-core-entwicklung.service`.
- Was: Erwartet werden Befehle und der Schalter «aktiv» unverändert.
- Warum: Speichern im echten Dienst.

**Prüft der Nutzer:**

**N1 Neu anmelden.**
- Was: Twitch-Knopf, «Anmelden …», das Recht «Chat schreiben» bestätigen. Bei Frage 2 B oder C zusätzlich das Bot-Konto anmelden und im eigenen Chat `/mod <Botname>` eingeben.
- Warum: Konto.

**N2 Befehl im echten Chat.**
- Was: `!dsstest` im eigenen Chat tippen und die Antwort im Twitch-Chat ansehen (Name, Abzeichen, Antwort-Faden). Wenn möglich mit einem zweiten Konto ohne Mod-Rechte einen Befehl «nur Moderatoren» testen, sonst «nicht geprüft».
- Warum: Nur der Nutzer hat die Konten. Aussehen im Twitch-Chat.

**N3 Bedienung.**
- Was:
  - einen eigenen Befehl anlegen, ändern und löschen;
  - eine Nachricht aus dem Studio senden;
  - **Eigener Schritt:** «Chat-Bot aktiv» ausschalten, Befehl tippen. Erwartet: keine Antwort, im Protokoll «Bot aus».
- Warum: Bedienung.

Was Lotse oder der Agent schon geprüft haben, wiederholt der Nutzer nicht.

## 8. Pakete (Regel 5)

Kein neues Paket. Benutzt werden die Pakete aus P13 (Qt Network, WebSockets, OpenSSL-Backend) und Qt Core (`QTextBoundaryFinder`).

## 9. Schätzung

| Teil | Agent | Lotse | Nutzer |
|---|---|---|---|
| P14 (A) | 2 Sitzungen à ~4–5 h (Commits 1–4, 5–7), 50er-Läufe nachts ~1,5 h | ~45 min (L1–L4) | ~15 min (N1–N3) |
| zusätzlich bei Frage 1 B | +1 Sitzung | +15 min | +10 min |
| zusätzlich bei Frage 2 B bzw. C | +½ bzw. +1 Sitzung | – | +10 min (Bot-Konto anlegen, anmelden, Mod) |

## 10. Fragen an den Nutzer (gebündelt, höchstens 4)

1. **Umfang von P14** (bedienbar).
   - **A) Empfohlen:** Chat ansehen und schreiben, Befehle mit festen Antworten (Platzhalter `{name}`, `{eingabe}`, `{ziel}`, `{kanal}`; wer darf; Abklingzeit Kanal und je Person), Protokoll. Zeitnachrichten und Dank für Ereignisse folgen als P14b.
   - B) Wie A, dazu Zeitnachrichten und Dank für Follow, Sub, Raid, Cheer und Kanalpunkte (+1 Sitzung).
   - C) Nur Befehle, ohne Chat-Ansicht und ohne Senden aus dem Studio.
   - Für alle gilt: Der Bot antwortet nur, solange das Studio offen ist. Ein Bot ohne Fenster wäre ein eigener Punkt. Streamer.bot-Import und YouTube kommen später.
   - Dazu: Wo liegen die Daten der alten Streamer.bot-Installation? Der Agent liest sie nur, um die Felder abzugleichen.
2. **Mit welchem Konto schreibt der Bot?** (sichtbar im Chat)
   - **A) Kanal-Konto (empfohlen):** einmal neu anmelden, kein zweites Konto, kein Secret, höhere Grenze. Antworten stehen unter deinem Kanalnamen, ohne Bot-Abzeichen.
   - B) Eigenes Bot-Konto, angemeldet mit Code: Antworten unter dem Botnamen, ohne Abzeichen. Du legst das Konto an und machst es zum Mod (+½ Sitzung).
   - C) Bot-Konto mit Bot-Abzeichen: braucht ein Client Secret, das verschlüsselt gespeichert wird, eventuell eine neue Registrierung der App (nicht geprüft), +1 Sitzung.
3. **Reiter «Chat-Bot»** (sichtbar).
   - **A) Empfohlen:** oben «Chat-Bot aktiv» (beim ersten Start aus) mit Zustand; links Chat mit Eingabe und «Senden»; rechts Unterreiter «Befehle» (Tabelle und Bereich zum Bearbeiten, «Wer darf» als Kästchen Alle, Abonnenten, VIPs, Moderatoren; Kanal darf immer) und «Protokoll».
   - B) Wie A, aber Befehle in einem eigenen Dialog statt im Reiter.
   - C) Änderungen bitte nennen.
4. **Go für P14.**
   - **A) Go wie geplant (empfohlen).** Dazu darf Lotse im Handtest Testnachrichten («DSS-Test») in deinen Chat senden und einen Testbefehl `!dsstest` anlegen und wieder löschen, nie während eines Streams. Das wird in `CLAUDE.md`, «Umgebung», eingetragen.
   - B) Go, aber Lotse sendet nichts selbst. Dann bleibt L3 «nicht geprüft».
   - C) Erst P12 und P13 ganz abschliessen, dann entscheiden.

## 11. Risiken und `[OFFEN]`

| Risiko oder offene Frage | Gegenmassnahme oder Stand |
|---|---|
| Antworten des Nutzers zu P13 nicht im Repo | Plan geht von den Empfehlungen A aus. Lotse gleicht vor dem Auftrag ab |
| Echo eigener Nachrichten nicht dokumentiert | doppelter Schutz (2.3), L3 |
| Zählung der 500 Zeichen, `/` am Anfang, `set_id` für Broadcaster, Founder und Lead Moderator nicht belegt | Schritt 0, vorsichtige Annahmen (2.2, 3.1) |
| Ob die Chat-Grenzen auch für die Send-API gelten («separate bucket») | eigene Grenze 20/30 s und 1/s, sicher für jede Rolle |
| Zwei Versionen antworten doppelt | stumm, solange der Kern im Einsatz läuft (2.3), Test in `tst_kernchatbot` |
| Bot nur bei offenem Studio | Frage 1, neuer offener Punkt |
| Geteilter Chat: Antworten gehen an alle Kanäle der Sitzung (User-Token) | nur auf den eigenen Kanal reagieren. Abhilfe wäre nur Variante C mit `for_source_only` |
| App für Variante C eventuell neu als «Confidential» zu registrieren | nur bei Frage 2 C, Schritt 0 |
| Founder ohne Abonnenten-Abzeichen könnte «nur Abonnenten» verfehlen | Schritt 0. Dann gilt `founder` auch als Abonnent |
| Streamer.bot-Daten nicht gelesen | Frage 1, Schritt 0 nur lesend |

## Annahmen und Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| Eigene Bibliothek `dss_chatbot` ohne Netz, Senden über `ChatSender` | Regel 9 und 14; Logik ohne Twitch testbar |
| Ergebnis des Sendens als Ereignis | Vorgabe P13 1.1 (Handlungen über den Dienst, Ergebnis auf dem Fluss) |
| Eigene Grenze 1/s und 20/30 s mit Warteschlange von 10 | Bei Überschreitung 1 h Sperre; für eine Basisfunktion genug |
| «Anfang» heisst erstes Wort gleich dem Auslöser | `!d` darf `!discord` nicht treffen |
| Kanal ohne Rechteprüfung und ohne Abklingzeit | wie Streamer.bot; erleichtert späteren Import |
| Kästchen «Wer darf» statt Mindeststufe | VIP und Abonnent haben keine klare Rangfolge; entspricht Allow-Gruppen von Streamer.bot |
| `actions` als Liste mit einer Aktion `chat` | später `sound` (P12-Ausgänge) und Streamer.bot-Actions ohne Umbau |
| Schalter «aktiv» in `chatbot.json`, Standard aus | geht mit der Installation mit; kein ungewollter Bot nach dem Update |
| Nachrichten aus dem Studio lösen keine Befehle aus | einfache Schleifenregel; der Nutzer kann im Twitch-Chat tippen |
| Keine Chat-Texte und keine Namen von Zuschauern im Journal | wie P13 |
| Version in Arbeit stumm, solange die Version im Einsatz läuft | keine doppelten Antworten im Stream |
| Neuer Schalter `DSS_CORE_CHATBOT` | Bot schreibt selbständig öffentlich, riskanter Teil nach Regel 8 |
| Ansagen, Flüstern, Regex und Zähler nicht in P14 | Funktionsentwurf «Zuerst»; später mit den Streamer.bot-Funktionen |

## Quellen

- **Lokal gelesen (2026-11-09):**
  - `CLAUDE.md`, `.claude/HANDOFF.md`
  - `docs/01-festlegungen.md`, `docs/03-funktionen-entwurf.md`, `docs/04-offene-punkte.md`, `docs/06-overlay-format.md` (Überschriften), `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md` (Versionen, Ablauf, Beenden)
  - `docs/archiv/prompts/P12-ton.md`, `docs/archiv/prompts/P13-twitch.md`
  - `src/app/mainwindow.cpp:171-192`, `src/app/settingsdialog.h`, `src/app/docks.h`, `src/app/dockviews.h`, `CMakeLists.txt` (Schalter), `src/common/savescheduler.h`
  - `git log`, `git branch`
- **Web gelesen (2026-11-09):**
  - https://dev.twitch.tv/docs/chat/
  - https://dev.twitch.tv/docs/chat/send-receive-messages/
  - https://dev.twitch.tv/docs/api/reference/#send-chat-message
  - https://dev.twitch.tv/docs/api/reference/#send-chat-announcement
  - https://dev.twitch.tv/docs/api/reference/#get-global-chat-badges
  - https://dev.twitch.tv/docs/api/reference/#get-channel-chat-badges
  - https://dev.twitch.tv/docs/authentication/scopes/
  - https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/#channelchatmessage
  - https://dev.twitch.tv/docs/eventsub/eventsub-reference/#chat-message
  - https://docs.streamer.bot/guide/commands
  - https://docs.streamer.bot/guide/actions
  - https://docs.streamer.bot/guide/core/timers
  - https://docs.streamer.bot/guide/core/import-export
  - https://docs.streamer.bot/guide/core/backup
  - https://docs.streamer.bot/guide/platforms/twitch
  - https://docs.streamer.bot/guide/variables
- **Nicht lesbar oder nicht gelesen (Schritt 0):**
  - https://dev.twitch.tv/docs/api/reference/#get-streams (Abschnitt nicht erreichbar)
  - https://docs.streamer.bot/api/sub-actions
  - https://docs.streamer.bot/api/triggers/core/commands
  - https://doc.qt.io/qt-6.8/qtextboundaryfinder.html
  - altes Repo `donstv_Studio` und Streamer.bot-Daten (ausserhalb des Projektordners)
