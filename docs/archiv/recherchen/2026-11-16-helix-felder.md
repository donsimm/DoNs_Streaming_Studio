# Bericht: Twitch Helix – Feldnamen und Regeln (Stand 2026-11-16)

Methode: öffentliche Doku über Abruf-Agenten gelesen, nichts angemeldet oder gesendet. Die Referenz-Übersicht `https://dev.twitch.tv/docs/api/reference/` enthält nur Links; die Details stehen auf Unterseiten:
- Chat: https://dev.twitch.tv/docs/api/reference/chat
- Users: https://dev.twitch.tv/docs/api/reference/users#get-users
- Channels: https://dev.twitch.tv/docs/api/reference/channels#get-channel-information

## 1. Chat-Nachricht senden – POST /helix/chat/messages
Quelle: https://dev.twitch.tv/docs/api/reference/chat (Abschnitt Send Chat Message)

| Im Code | Laut Doku | Stimmt? |
|---|---|---|
| Body `broadcaster_id` | String, Pflicht | ja |
| Body `sender_id` | String, Pflicht; muss zur User-ID im Token passen | ja |
| Body `message` | String, Pflicht, max. 500 Zeichen | ja |
| Body `reply_parent_message_id` | optional | ja |
| `data[0].message_id` | `data[].message_id` | ja |
| `data[0].is_sent` | `data[].is_sent` (Boolean) | ja |
| `data[0].drop_reason.code` | `drop_reason.code` (String); `drop_reason` ist `null`, wenn gesendet | ja, auf null prüfen |
| `data[0].drop_reason.message` | `drop_reason.message` | ja |

Zusätzlich dokumentiert: `for_source_only` (nur App-Token, mit User-Token HTTP 400), `pin` (braucht `moderator:manage:chat_messages`).
Codes für `drop_reason.code`: `msg_duplicate`, `user_banned`, `user_warned`, `user_timed_out`, `emote_only_mode`, `slow_mode`, `followers_only_mode`, `subs_only_mode`, `unique_mode`, `verified_account`, `channel_settings`, `automod_held`, `msg_rejected`, `msg_bad_characters`, `banned_email_alias`, `banned_phone_alias`, `verified_phone_number`, `channel_suspended`.

Antworten auf die Fragen:
- **Scopes:** User-Token: `user:write:chat`. App-Token: `user:write:chat` + `user:bot` (Sender) + `channel:bot` (Kanalinhaber), vorab autorisiert. Was `user:bot`/`channel:bot` genau bedeuten, ist auf den gelesenen Seiten nicht definiert (nicht belegt).
- **Höchstlänge:** 500 Zeichen. Zu lang → 422 «The message is too large».
- **Erfolg:** 200 (nicht 204). Weitere: 400, 401 (sender_id ≠ Token), 403 (Sender darf nicht schreiben), 422, 429.
- **Rate-Limits** (https://dev.twitch.tv/docs/chat/, «Twitch Chat Rate Limits»): normales Konto 20 Nachrichten/30 s; Broadcaster/Moderator/VIP 100/30 s; verifizierter Bot 7500/30 s; überall 1 Nachricht/s/Kanal. Bei Überschreitung «Twitch ignores the bot messages for 1 hour». Dass diese Zahlen für den Helix-Endpunkt gelten, steht dort nicht ausdrücklich. Punktewert/Bucket des Endpunkts: nicht belegt (https://dev.twitch.tv/docs/api/guide/).
- **«Chat Bot»-Abzeichen** (https://dev.twitch.tv/docs/chat/): nur mit Send-Chat-Message-API, **App-Token**, `channel:bot` vom Kanalinhaber oder Moderatorstatus, und Bot ≠ Kanalinhaber. Mit User-Token kein Abzeichen. Ein eigenes Rate-Limit für das Abzeichen: nicht belegt.

## 2. Ansage – POST /helix/chat/announcements
Quelle: https://dev.twitch.tv/docs/api/reference/chat (Abschnitt Send Chat Announcement)

| Im Code | Laut Doku | Stimmt? |
|---|---|---|
| Query `broadcaster_id` | Pflicht | ja |
| Query `moderator_id` | Pflicht; muss zur User-ID im Token passen (Broadcaster-ID, wenn er selbst sendet) | ja |
| Body `message` | Pflicht, nicht leer | ja |
| Body `color` | optional | ja |

Antworten:
- **color:** `blue`, `green`, `orange`, `purple`, `primary` (Standard).
- **Scope:** User-Token `moderator:manage:announcements`. App-Token: dieser Scope + `user:bot` (moderator_id) + `channel:bot` (broadcaster_id).
- **Höchstlänge:** 500 Zeichen; längere werden **abgeschnitten**, nicht abgelehnt.
- **Rate-Limit:** ja, eigenes: «One announcement may be sent every 2 seconds.» (429: Chatraum hat das Fenster überschritten, egal wer sendet).
- **Erfolg:** 204 No Content (Twitch hat angenommen; kann danach noch verworfen werden, z. B. AutoMod).
- **Fehler:** 400 (message fehlt/leer/«failed review», ungültige Farbe, `for_source_only` mit User-Token); 401 (moderator_id ≠ Token, Scope fehlt, Broadcaster hat `channel:bot` nicht erteilt); **403** «The user in moderator_id does not have permission to send announcements in the broadcaster's chat room» (Vermutung 403 bestätigt); 429.

## 3. Shoutout – POST /helix/chat/shoutouts
Quelle: https://dev.twitch.tv/docs/api/reference/chat#send-a-shoutout

| Im Code | Laut Doku | Stimmt? |
|---|---|---|
| Query `from_broadcaster_id` | Pflicht | ja |
| Query `to_broadcaster_id` | Pflicht | ja |
| Query `moderator_id` | Pflicht; muss zum Token passen (Broadcaster oder dessen Moderator) | ja |
| kein Body | «Nicht dokumentiert»; Beispiel ohne Body | ja |

Antworten:
- **Scope:** User-Token `moderator:manage:shoutouts`. App-Token: dieser Scope + `user:bot` (moderator_id) + `channel:bot` (from_broadcaster_id).
- **Erfolg:** 204 No Content.
- **Abklingzeiten:** global «once every 2 minutes»; gleicher Zielkanal «once every 60 minutes».
- **Live-Pflicht:** 400 «The broadcaster is not streaming live.» Ob der Absender oder der Zielkanal gemeint ist, sagt der Text nicht. Ob der Zielkanal live sein muss: nicht belegt. Zuschauerzahl-Bedingung: nicht belegt.
- **400:** fehlende/ungültige IDs; Selbst-Shoutout nicht erlaubt; Absender nicht live.
- **401:** moderator_id ≠ Token, Token fehlt/ungültig, Scope fehlt, falsche Client-Id.
- **403:** moderator_id ist kein Moderator des Kanals; oder «The broadcaster may not send the specified broadcaster a Shoutout» (ohne Grund genannt).
- **429:** zu viele Shoutouts im Fenster, bzw. zu viele an denselben Kanal.
- Die Abklingzeiten liefert laut Seite EventSub `channel.shoutout.create`; diese Seite wurde nicht gelesen.

## 4. Benutzerinfo
Quellen: https://dev.twitch.tv/docs/api/reference/users#get-users, https://dev.twitch.tv/docs/api/reference/channels#get-channel-information

| Im Code | Laut Doku | Stimmt? |
|---|---|---|
| `GET /helix/users?login=…` | `id`/`login`, wiederholbar, zusammen max. 100 | ja |
| users `id` | «An ID that identifies the user.» | ja |
| users `login` | «The user's login name.» | ja |
| users `display_name` | «The user's display name.» | ja |
| users `description` | «The user's description of their channel.» Kein Deprecation-Hinweis | ja |
| users `profile_image_url` | «A URL to the user's profile image.» | ja |
| `GET /helix/channels?broadcaster_id=…` | `broadcaster_id`, Pflicht, bis 100 | ja |
| channels `game_name` | Name des Spiels, leerer String, wenn nie gespielt; kein Deprecation-Hinweis | ja |

Antworten:
- **Token users:** User- **oder** App-Token reichen. Ohne `id`/`login` liefert der Aufruf den Benutzer des User-Tokens.
- **Token channels:** User- oder App-Token, keine Scopes genannt.
- **`description`:** vorhanden, nicht veraltet.
- Veraltet in users: `view_count` («not valid»). `email` nur mit `user:read:email` für den Token-Benutzer.
- Ob `profile_image_url` über die Zeit stabil ist: nicht belegt.

## 5. Cheer: Streamer.bot und EventSub
- **EventSub `channel.cheer`** (https://dev.twitch.tv/docs/eventsub/reference/channel#channel-cheer; alte URL `.../eventsub-reference/` leitet um): Feld `event.is_anonymous`, Boolean, «Whether the user cheered anonymously or not.» Scope `bits:read`. Weitere Felder: `user_id`, `user_login`, `user_name` (**null**, wenn `is_anonymous` true), `broadcaster_user_*`, `message`, `bits`.
- **Streamer.bot** Trigger «Cheer» (https://docs.streamer.bot/api/triggers/twitch/chat/cheer): Die Seite zeigt nur Variablengruppen (General, Broadcaster, Chat, User). Aus den Quelldateien (https://github.com/Streamerbot/docs/tree/main/streamerbot/3.api/.variables, Dateien `General.md`, `twitch/TwitchChat.md`, `twitch/TwitchUser.md`, `twitch/TwitchBroadcaster.md`; Raw-Basis `https://raw.githubusercontent.com/Streamerbot/docs/main/streamerbot/3.api/.variables/`):
  - Cheer-relevant: `bits` (number, «The amount of bits donated»), `message`/`messageStripped`, `messageCheermotesStripped`, `user`, `userName`, `userId`.
  - **Kein Argument für Anonymität dokumentiert** (weder `anonymous` noch `isAnonymous` noch ähnlich). Ob es zur Laufzeit eines gibt, ist aus der Doku nicht ersichtlich → **nicht belegt**.
  - Empfehlung, ohne Beleg: Argumentliste im Streamer.bot-Aktionsprotokoll bei einem echten anonymen Cheer prüfen; ersatzweise `userId`/`userName` auf leer oder auf einen Platzhalter testen (Annahme, nicht belegt, abgeleitet aus EventSub `user_name = null`).

## Zusammenfassung der Abweichungen vom Code
- Alle Feld-, Parameter- und Pfadnamen der Punkte 1–4 stimmen.
- Erfolgscodes: Chat senden = **200**; Ansage und Shoutout = **204**.
- `drop_reason` ist `null`, wenn gesendet: vor Zugriff auf `.code` prüfen.
- Nachrichten über 500 Zeichen: senden → 422, Ansage → stilles Abschneiden.
- Für das «Chat Bot»-Abzeichen ist ein App-Token nötig; mit User-Token (üblich bei privatem Programm) gibt es keines.
- Streamer.bot: Anonymitäts-Argument beim Cheer nicht belegt.
