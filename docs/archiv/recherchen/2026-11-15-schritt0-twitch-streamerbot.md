# Bericht: Twitch EventSub/Helix und Streamer.bot (Schritt 0 für P13 und P14)

Stand der Doku: 15.11.2026. Gelesen wurden nur öffentliche Seiten. Es wurde nichts angemeldet, heruntergeladen oder ausgeführt. Die Seiten wurden von Leseagenten abgerufen. Wo nur eine Tool-Zusammenfassung vorlag und kein Wortlaut, steht «Zusammenfassung».

## Abweichungen und Lücken (zuerst)

| Punkt | Befund |
|---|---|
| Quellen A | `eventsub-subscription-types` und `eventsub-reference` sind umgezogen nach `https://dev.twitch.tv/docs/eventsub/reference/` (Unterseiten `/chat`, `/channel`, `/channel-points`, `/stream`). Anker wie `#channelchatmessage` existieren nicht mehr. Richtig ist z. B. `/reference/chat#channel-chat-message`. |
| 10f | «3 Verbindungen» gilt je Token (Tupel Client-ID + Nutzer-ID), nicht je Client-ID oder je Nutzer allein. `max_total_cost`: Fliesstext sagt 10, JSON-Beispiele zeigen 10000. Die Doku widerspricht sich. Den Wert zur Laufzeit aus der API-Antwort lesen. |
| 10e | Revocation-Status laut WebSocket-Referenz nur `authorization_revoked`, `user_removed`, `version_removed`. Die längere Liste (`moderator_removed`, `chat_user_banned`, `websocket_disconnected` …) steht bei den Abo-Status der Verwaltungsseite. |
| 12 | Beim Device-Code-Tausch steht in der Doku kein `client_secret` (weder Parameterliste noch cURL). Ob vertrauliche Apps es mitsenden müssen, ist **nicht belegbar**. Refresh-Token: einmal verwendbar; bei vertraulichen Apps ohne Ablaufzeit, bei öffentlichen 30 Tage. Die Doku widerspricht sich, ob ab Erstellung (`refresh-tokens`) oder ab Inaktivität (`getting-tokens-oauth`). |
| 14 | Seite `chat/chatbot-badge` liefert 404. Das Abzeichen «Chat Bot» ist damit **nicht belegbar**. Die Bedingungen für das Senden mit App-Token stehen aber (siehe 14). |
| 16 | Kategorien der Developer-Konsole: **nicht belegbar** (Seite nennt keine Liste). Client-Typ-Begriffe stehen nicht auf der Registrierungsseite. |
| 11 | «Offline = leeres `data`» steht nicht ausdrücklich, folgt nur sinngemäss aus «Returns only the streams of those users that are broadcasting». |
| 19 | `tier` heisst bei Subs-Triggern **«tier 1»** (String, auch `prime`), nicht «1000». Die Zahl 1000/2000/3000 gibt es als `subscriptionTier` (number) in den User-Variablen. `monthsSubscribed` ist eine User-Variable (Chat/Cheer/Follow). Beim Resub heisst es `cumulative`/`monthStreak`, beim Gift Sub `cumulativeMonths`. Gift Bomb: `gifts`/`totalGifts`. `isHighlighted` heisst `isHighlight`. |
| 20 | Typnummern **nicht dokumentiert** (siehe 20). |
| 21 | `userProfileImageUrl` und `isFollowing` gibt es **nicht**. Es gibt `targetUserProfileImageUrl` (Sub-Action) und kein Follow-Flag. |
| A3 | Bestätigt: `channel.subscribe` nennt bei Geschenken **keinen** Schenkenden. Für Schenkende `channel.subscription.gift` (nullable bei anonym) oder `channel.chat.notification` nutzen. |

## A. Twitch EventSub

Basis: https://dev.twitch.tv/docs/eventsub/reference/ (und Unterseiten).

### 1. `channel.chat.message` v1 – bestätigt
Quelle: https://dev.twitch.tv/docs/eventsub/reference/chat#channel-chat-message
- Condition: `broadcaster_user_id`, `user_id` («The User ID to read chat as»).
- Felder: `message_id`, `chatter_user_id/_login/_name`, `message.text`, `color` (kann leer sein), `badges[]` (`set_id`, `id`, `info` = bei Subscriber-Abzeichen die Monatszahl), `cheer` (nullable, `bits`), `reply` (nullable: `parent_message_id`, `parent_message_body`, `parent_user_id/_name/_login`, `thread_message_id`, `thread_user_id/_name/_login`), `channel_points_custom_reward_id` (nullable).
- `message.fragments[].type`: `text`, `cheermote` (prefix, bits, tier), `emote` (`id`, `emote_set_id`, `owner_id`, `format[]` = `animated`/`static`), `mention` (user_id/_name/_login), `gif` (id, url).
- `message_type`: `text`, `channel_points_highlighted`, `channel_points_sub_only`, `user_intro`, `power_ups_message_effect`, `power_ups_gigantified_emote`. `user_intro` ist bestätigt.
- Shared Chat: `source_broadcaster_user_id/_name/_login`, `source_message_id`, `source_badges`, `is_source_only` (alle nullable, null bei Nachricht im eigenen Kanal des Streamers).
- Recht: User-Token mit `user:read:chat`. Mit App-Token (nur Webhook): `user:read:chat` und `user:bot` für `user_id` plus `channel:bot` für `broadcaster_user_id`. WebSocket braucht User-Token (https://dev.twitch.tv/docs/chat/authenticating/).
- Nicht belegbar: welcher Nutzer das Token besitzen muss (die Regel «Token-Nutzer = `user_id`» steht in den gelesenen Texten nicht).

### 2. `channel.follow` v2 – bestätigt
Quelle: https://dev.twitch.tv/docs/eventsub/reference/channel#channel-follow
- Condition `broadcaster_user_id`, `moderator_user_id` (der Broadcaster selbst, wenn Autorisierung vom Broadcaster kommt). Recht `moderator:read:followers`.
- Felder: `user_id/_login/_name`, `broadcaster_user_*`, `followed_at` (RFC3339). Nicht nullable.

### 3. `channel.subscribe` v1 – bestätigt (Zusammenfassung)
Quelle: https://dev.twitch.tv/docs/eventsub/reference/channel#channel-subscribe
- `tier` = `1000`/`2000`/`3000`, `is_gift` (bool). Recht `channel:read:subscriptions`. Enthält keine Resubs.
- Schenkende Person: kein Feld. Annahme «nein» bestätigt.

### 4. `channel.subscription.message` v1 – bestätigt (Zusammenfassung)
Quelle: https://dev.twitch.tv/docs/eventsub/reference/channel#channel-subscription-message
- `message.text`, `message.emotes[]` (begin, end, id), `cumulative_months`, `streak_months` (nullable: null, wenn nicht geteilt), `duration_months`, `tier`. Recht `channel:read:subscriptions`.

### 5. `channel.subscription.gift` v1 – bestätigt (Zusammenfassung)
Quelle: https://dev.twitch.tv/docs/eventsub/reference/channel#channel-subscription-gift
- `total`, `tier`, `cumulative_total` (nullable: null bei anonym oder nicht geteilt), `is_anonymous`. `user_id/_login/_name` sind nullable (null bei anonym). Kein Empfänger im Event. Recht `channel:read:subscriptions`.

### 6. `channel.cheer` v1 – bestätigt
Quelle: https://dev.twitch.tv/docs/eventsub/reference/channel#channel-cheer
- `is_anonymous`, `user_id/_login/_name` (null, wenn anonym), `message`, `bits`. Recht `bits:read`.

### 7. `channel.raid` v1 – bestätigt
Quelle: https://dev.twitch.tv/docs/eventsub/reference/channel#channel-raid
- Condition: `from_broadcaster_user_id` **oder** `to_broadcaster_user_id`, nicht beide. Felder: `from_broadcaster_user_*`, `to_broadcaster_user_*`, `viewers`. «No authorization required.»

### 8. `channel.channel_points_custom_reward_redemption.add` v1 – bestätigt
Quelle: https://dev.twitch.tv/docs/eventsub/reference/channel-points#channel-channel-points-custom-reward-redemption-add
- Condition `broadcaster_user_id`, optional `reward_id`. Felder `id`, `user_input` (leer, wenn nicht angegeben), `status` (`unknown`, `unfulfilled`, `fulfilled`, `canceled`; Standard `unfulfilled`), `reward.id/title/cost/prompt`, `redeemed_at`. Recht `channel:read:redemptions` oder `channel:manage:redemptions`.

### 9. `stream.online` / `stream.offline` v1 – bestätigt
Quelle: https://dev.twitch.tv/docs/eventsub/reference/stream#stream-online
- Beide: «No authorization required.»
- `stream.online`: `id`, `type` (`live`, `playlist`, `watch_party`, `premiere`, `rerun`), `started_at`. `stream.offline`: nur `id` und `broadcaster_user_*`.

### Zusätzlich
- `channel.chat.notification` v1 existiert (https://dev.twitch.tv/docs/eventsub/reference/chat#channel-chat-notification), Recht `user:read:chat`, `notice_type`: `sub`, `resub`, `sub_gift`, `community_sub_gift`, `gift_paid_upgrade`, `prime_paid_upgrade`, `raid`, `unraid`, `pay_it_forward`, `announcement`, `bits_badge_tier`, `charity_donation`, `watch_streak`, `modiversary`, `gifted_drops_summary`, `shared_chat_*`, `unknown`. Dass es `channel.subscribe` & Co. ablöst, ist **nicht belegbar**.
- `channel.shoutout.create` / `.receive` v1 existieren, Recht `moderator:read:shoutouts` oder `moderator:manage:shoutouts`, Condition `broadcaster_user_id` + `moderator_user_id` (`create` wörtlich, `receive` Zusammenfassung). Quelle: https://dev.twitch.tv/docs/eventsub/reference/channel#channel-shoutout-create

### 10. WebSocket
Quellen: https://dev.twitch.tv/docs/eventsub/handling-websocket-events/ , https://dev.twitch.tv/docs/eventsub/websocket-reference/ , https://dev.twitch.tv/docs/eventsub/manage-subscriptions/
- URL `wss://eventsub.wss.twitch.tv/ws`, Parameter `keepalive_timeout_seconds`: ganze Zahlen 10 bis 600, ungültige Zahlen werden gerundet. Ein ausdrücklicher Standardwert ist **nicht belegbar** (Welcome-Beispiel zeigt 10). Der Timer wird durch jede Notification/Keepalive zurückgesetzt, Ping/Pong nicht.
- Frist nach `session_welcome`: **bestätigt**, 10 s («unless otherwise specified»), sonst Close-Code 4003.
- `session_reconnect`: bestätigt. `payload.session.reconnect_url` unverändert verwenden. 30 s Frist. Die alte Verbindung liefert Events, bis die neue verbunden ist und ihr Welcome kam; erst dann die alte schliessen. Abos wandern automatisch mit. Zählt nicht zum Verbindungslimit. Bei echtem Verbindungsverlust gibt es kein Replay, Abos müssen neu erstellt werden.
- Close-Codes: 4000 Internal server error, 4001 Client sent inbound traffic (nur Pong erlaubt), 4002 Client failed ping-pong, 4003 Connection unused, 4004 Reconnect grace time expired, 4005 Network timeout, 4006 Network error, 4007 Invalid reconnect.
- Nachrichtentypen: `session_welcome`, `session_keepalive`, `notification`, `session_reconnect`, `revocation`.
- `revocation` (`payload.subscription.status`): `authorization_revoked`, `user_removed`, `version_removed`. Kommt einmal, danach keine Nachrichten mehr für diesen Nutzer und Abo-Typ.
- Grenzen: 3 Verbindungen je Token (Client-ID + Nutzer-ID), 300 aktive Abos je Verbindung (deaktivierte zählen nicht), `max_total_cost` widersprüchlich (10 vs. 10000, siehe oben). Abos, die eine Nutzer-Autorisierung verlangen, kosten 0. Abos ohne Autorisierungspflicht (z. B. `stream.online`) kosten laut Beispiel je 1, wenn der Nutzer nicht autorisiert hat. Höchstens 3 Abos mit gleichem Typ und gleicher Condition.
- WebSocket-Abos: nur User-Token, App-Token schlägt fehl.
- Duplikate: Zustellung «at least once», gleiche `message_id` bei Wiederholung. Empfehlung: Zeitstempel nicht älter als 10 Minuten. Aufbewahrungsdauer der IDs: nicht belegbar.

### 11. `GET /helix/streams` – bestätigt, ein Detail nicht belegbar
Quelle: https://dev.twitch.tv/docs/api/reference/streams#get-streams
- User- oder App-Token, **kein** Scope. `user_id` bis 100 mal. Liefert nur Sendende. Offline = leeres `data`: sinngemäss, nicht ausdrücklich. Felder `type` (immer `live`), `started_at`, `viewer_count`, `id`, `user_*`, `game_*`, `title`, `tags`, `language`, `thumbnail_url`.

## B. Anmeldung und Rechte

### 12. Device Code Flow
Quellen: https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/ , https://dev.twitch.tv/docs/authentication/refresh-tokens/
- Gilt für öffentliche **und** vertrauliche Apps («allows for public and confidential client types»). Empfehlung der Seite: vertraulich nur auf sicheren Geräten, auf offenen Plattformen öffentlich.
- `client_secret` beim Tausch: nicht in der Parameterliste, nicht belegbar als Pflicht.
- Beim Refresh kommen neues Access- und neues Refresh-Token. Alte sind einmalig verwendbar. Access-Token ca. 4 Stunden.
- Refresh-Token öffentlich: 30 Tage (Quellen uneins: ab Erstellung vs. Inaktivität). Vertraulich: keine Ablaufzeit; ungültig bei Passwortänderung oder Trennung der App.
- Device-Code-Antwort: `expires_in` im Beispiel 1800 s, `interval` 5.

### 13. Client Credentials
Quelle: https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/
- `POST https://id.twitch.tv/oauth2/token`, `client_id`, `client_secret`, `grant_type=client_credentials`. Antwort `access_token`, `expires_in`, `token_type`. Kein Refresh-Token («You cannot refresh app access tokens»). Keine Scopes. Beispielwert `expires_in` ca. 58 Tage, eine feste Gültigkeit steht nicht im Text.

### 14. Senden mit App-Token – bestätigt (Abzeichen nicht belegbar)
Quellen: https://dev.twitch.tv/docs/chat/irc-migration/ , https://dev.twitch.tv/docs/chat/send-receive-messages/
- Wörtlich: «If using an App Access Token, the token will also require the scope user:bot from the chatting user, and either moderator status in the channel or channel:bot scope from the broadcaster.» Mindestens `user:write:chat` vom Sendenden.
- Parameter `broadcaster_id`, `sender_id`, `message`, `reply_parent_message_id`. Antwort `is_sent`, `drop_reason` (`code`, `message`, z. B. `automod_held`). 500 Zeichen: nur aus Zusammenfassung von https://dev.twitch.tv/docs/api/reference/chat.
- Abzeichen «Chat Bot»: Seite `chat/chatbot-badge` 404, **nicht belegbar**. Auf mehreren Chat-Seiten steht ein Hinweis, dass Prüfungen zur Chatbot-Verifizierung «temporarily paused» sind. Ratenlimits: **nicht belegbar** (verwiesen auf https://dev.twitch.tv/docs/chat/#rate-limits, nicht gelesen).

### 15. Scope-Liste (80 Namen)
Quelle: https://dev.twitch.tv/docs/authentication/scopes/ . Kein Scope als veraltet markiert. `whispers` gibt es nicht, nur `user:manage:whispers` und `user:read:whispers`.

Alle 13 geprüften Scopes sind vorhanden: `user:read:chat`, `user:write:chat`, `user:bot`, `channel:bot`, `moderator:read:followers`, `channel:read:subscriptions`, `bits:read`, `channel:read:redemptions`, `moderator:manage:shoutouts`, `moderator:manage:announcements`, `channel:read:stream_key`, `channel:manage:moderators`, `channel:manage:extensions`.

Vollständig:
- analytics:read:extensions, analytics:read:games
- bits:read
- channel:bot, channel:edit:commercial, channel:manage:ads, channel:manage:broadcast, channel:manage:clips, channel:manage:extensions, channel:manage:guest_star, channel:manage:moderators, channel:manage:polls, channel:manage:predictions, channel:manage:raids, channel:manage:redemptions, channel:manage:schedule, channel:manage:videos, channel:manage:vips, channel:moderate
- channel:read:ads, channel:read:charity, channel:read:editors, channel:read:goals, channel:read:guest_star, channel:read:hype_train, channel:read:polls, channel:read:predictions, channel:read:redemptions, channel:read:stream_key, channel:read:subscriptions, channel:read:vips
- clips:edit, editor:manage:clips
- moderation:read
- moderator:manage:announcements, :automod, :automod_settings, :banned_users, :blocked_terms, :chat_messages, :chat_settings, :guest_star, :shield_mode, :shoutouts, :suspicious_users, :unban_requests, :warnings
- moderator:read:automod_settings, :banned_users, :blocked_terms, :chat_messages, :chat_settings, :chatters, :followers, :guest_star, :moderators, :shield_mode, :shoutouts, :suspicious_users, :unban_requests, :vips, :warnings
- user:bot, user:edit, user:edit:broadcast, user:manage:blocked_users, user:manage:chat_color, user:manage:whispers, user:read:blocked_users, user:read:broadcast, user:read:chat, user:read:email, user:read:emotes, user:read:follows, user:read:moderated_channels, user:read:subscriptions, user:read:whispers, user:write:chat
- IRC: chat:edit, chat:read

### 16. App-Registrierung
Quelle: https://dev.twitch.tv/docs/authentication/register-app/
- Kategorien und Client-Typ-Wahl: **nicht belegbar**. Die Seite sagt nur «Select a Category». Zwei Faktoren (2FA) sind Pflicht, der Name muss eindeutig sein, ein neues Secret macht das alte ungültig. Redirect-URL: nur «callback URL that your app uses»; im Beispiel `http://localhost:3000`.

### 17. Shoutout und Ansage – bestätigt
Quelle: https://dev.twitch.tv/docs/api/reference/chat
- Shoutout: Recht `moderator:manage:shoutouts`; alle drei IDs (`from_broadcaster_id`, `to_broadcaster_id`, `moderator_id`) als Query-Parameter. Grenzen: alle 2 Minuten ein Shoutout, dasselbe Ziel alle 60 Minuten. Nur wenn der Kanal live ist (sonst 400 «not streaming live»). Eigener Shoutout nicht erlaubt. Zuschauerzahl-Grenze: nicht belegbar. Mit App-Token zusätzlich `user:bot` (moderator_id) und `channel:bot` (from_broadcaster_id).
- Ansage: Recht `moderator:manage:announcements`, `color` im Body (`blue`, `green`, `orange`, `purple`, `primary`, Standard `primary`). `moderator_id` muss moderieren dürfen oder der Broadcaster sein, sonst 403. Eine Ansage alle 2 s, über 500 Zeichen wird gekürzt. Ansagen gehen nicht über Send Chat Message.

### 18. IRC-Migration (für Bots wichtig)
Quelle: https://dev.twitch.tv/docs/chat/irc-migration/
- Senden über Helix (`user:write:chat`), Empfangen über EventSub `channel.chat.message` (`user:read:chat`). Mit App-Token zusätzlich `user:bot` und (Moderator oder `channel:bot`).
- Nachricht als `message.text` und `fragments`. Emotes kommen im Text, Fragmente vom Typ `emote`. Bits als `cheer`, Replies mit `parent_message_id`/`thread_message_id`; beim Senden `reply_parent_message_id`, der @-Name wird automatisch gesetzt.
- Abzeichen je Nachricht lesen, nicht speichern. ROOMSTATE ersetzt Get Chat Settings, Chatterliste über Get Chatters. Bann oder Timeout beendet das Abo (Status `chat_user_banned`).
- Ratenlimits und Duplikate: **nicht belegbar** auf dieser Seite.

## C. Streamer.bot

Quelle der Variablengruppen (vom gerenderten HTML eingeklappt): öffentliches Doku-Repository `Streamerbot/docs`, Dateien unter https://github.com/Streamerbot/docs/tree/main/streamerbot/3.api/.variables (`General.md`, `twitch/TwitchBroadcaster.md`, `twitch/TwitchChat.md`, `twitch/TwitchUser.md`). Trigger-Seiten: https://docs.streamer.bot/api/triggers/twitch/ .

### 19. Argumentnamen
Gruppenzuordnung: Chat Message = Chat + Broadcaster + User. Cheer = Broadcaster + Chat + User. Follow = Broadcaster + User (**ohne** Chat-Gruppe, also kein `msgId`, `bits`, `message`).

| Name | Befund |
|---|---|
| `user`, `userName`, `userId`, `userType` | bestätigt (User-Gruppe): Anzeigename, Login, ID, `twitch` |
| `msgId`, `message`, `messageStripped`, `bits`, `color`, `emotes` (C#-Liste), `isReply`, `firstMessage`, `rawInput`, `input#` | bestätigt (Chat-Gruppe, Chat Message und Cheer) |
| `isHighlighted` | weicht ab: heisst `isHighlight` |
| `role` | bestätigt: number, 1=Viewer, 2=VIP, 3=Moderator, 4=Broadcaster |
| `isSubscribed`, `isVip`, `isModerator` | bestätigt (boolean) |
| `subscriptionTier` | number 1000/2000/3000 (nur wenn `isSubscribed` wahr) |
| `monthsSubscribed` | bestätigt als User-Variable (number), nicht bei Sub-Triggern als eigener Name |
| `broadcastUser`, `broadcastUserName`, `broadcastUserId` | bestätigt (Broadcaster-Gruppe); zusätzlich `broadcastIsAffiliate`, `broadcastIsPartner` |
| `tier` (Sub-Trigger) | weicht ab von «1000»: String `prime`, `tier 1`, `tier 2`, `tier 3` |
| Resub | `cumulative`, `monthStreak`, `streakShared`, `isMultiMonth`, `multiMonthDuration`, `multiMonthTenure` |
| Gift Sub | `anonymous`, `cumulativeMonths`, `fromGiftBomb`, `monthsGifted`, `random`, `recipientId`, `recipientUser`, `recipientUserName`, `subBombCount`, `systemMessage`, `totalSubsGifted`, `totalSubsGiftedShared` |
| Gift Bomb | `anonymous`, `bonusGifts`, `gifts` (Anzahl), `gift.recipientId#`/`gift.recipientUser#`/`gift.recipientUserName#`, `systemMessage`, `totalGifts`, `totalGiftsShared` |
| Cheer | keine eigenen Variablen; `bits` und `message` aus Chat-Gruppe. `anonymous`: nicht belegbar |
| Raid | `viewers` (number); Rest aus den gemeinsamen Gruppen (bei Raid-Seite nicht einzeln geprüft) |
| First Words | `firstMessage` (bool); Parameter User Name |
| Stream Online | `game`, `gameId`, `startedAt`, `tag#`, `tagCount`, `tags`, `tagsDelimited` |
| Stream Offline | `endedAt` |
| Command Triggered (Core) | `command`, `commandId`, `commandName`, `commandSource`, `commandType`, `counter`, `input#`, `inputEscaped#`, `inputUrlEncoded#`, `msgId`, `rawInput`, `rawInputEscaped`, `rawInputUrlEncoded`, `userCounter`, bei Regex `match.count`, `match[i]` |

Hinweise: Beispielwerte in der Doku sind nicht immer echt (z. B. `broadcastUserId` hat dieselbe GUID wie `msgId`). Die Gruppen der Trigger Sub/Resub/Gift/Raid/First Words/Stream Online/Offline wurden nicht einzeln aus den Quelldateien geprüft; `user`, `userId` usw. dort sind aus der Gruppenstruktur ableitbar, aber nicht bei jedem Trigger belegt. Bei Gift Sub ist `user` der Schenkende, der Empfänger heisst `recipientUser*`. Auch das ist aus den Namen abgeleitet.

### 20. Typnummern – nicht belegbar
- Doku nennt keine Zahlen. `EventType` (C#) ist ein Enum nur mit Namen: https://docs.streamer.bot/api/csharp/enums/event-type — Namen `EventType.TwitchShoutoutCreated`, `TwitchShoutoutReceived`, `TwitchStreamOnline`. In Skripten gibt `%__source%` den Namen an.
- WebSocket-Events heissen `Twitch.ShoutoutReceived`, `Twitch.StreamOnline` usw. (https://docs.streamer.bot/api/websocket/events).
- Nur ein Hinweis, kein Beleg: Die GitHub-Edit-Links der Doku-Dateien heissen `154.stream-online.json` und `156.shoutout-received.json`. Das sind Dateinamen, keine dokumentierten IDs. Nicht als Typnummern verwenden, ohne sie in der laufenden Version zu prüfen.

### 21. `%userProfileImageUrl%` und `%isFollowing%` – weicht ab
- Nicht in den vier Variablengruppen (Quelldateien durchsucht).
- Sub-Action «Get User Info for Target» (https://docs.streamer.bot/api/sub-actions/twitch/user/get-user-info-for-target) setzt `targetUserProfileImageUrl` (300×300 PNG) und `targetUserProfileImageUrlEscaped`, ausserdem `targetIsAffiliate`, `targetIsModerator`, `targetIsPartner`, `targetIsSubscribed`, `targetIsVip`. Kein Follow-Flag.
- Nicht gelesen: «Get Follow Age Info for Target» (`/api/sub-actions/twitch/followers/get-follow-age-info-for-target`). Sie könnte Follow-Variablen setzen, das ist unbelegt.
- Namen «Get Extended User Info» und «Get User Profile Image» gibt es im Sub-Action-Index nicht.
- WebSocket-Payload von `Twitch.ShoutoutReceived` und `Twitch.StreamOnline` enthält nur `id`, `login`, `name`, `type` des Nutzers.

## Nicht geprüft / Restlücken
- Wortlaut von `channel.subscribe`, `.subscription.message`, `.subscription.gift`, `.shoutout.receive` nur als Zusammenfassung. Markdown-Fassung wäre `https://dev.twitch.tv/docs/eventsub/reference/channel.md`.
- Ratenlimits fürs Senden (`https://dev.twitch.tv/docs/chat/#rate-limits`) nicht gelesen.
- Welcher Nutzer das Token für `channel.chat.message` über WebSocket besitzen muss: nicht belegt.
- Streamer.bot: Trigger-Variablen für Sub-Triggern, Raid usw. nur über die Gruppenstruktur, nicht Datei für Datei.
