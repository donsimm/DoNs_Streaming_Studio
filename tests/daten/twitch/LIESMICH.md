# Testnachrichten von Twitch (P13)

Nachrichten von EventSub über WebSocket und eine Antwort der API, für `tst_twitchprotokoll` (Commit 4) und später den Testserver `tests/twitchtestserver.h`.

**Nachgebaut nach Doku.** Beim Bau gab es keinen Webzugang. Die Dateien folgen den Feldern im Bericht zu Schritt 0 (`docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md`, gelesen am 15.11.2026) und dem Plan `docs/archiv/prompts/P13-twitch.md`, Abschnitt 1.3. Sie sind keine Mitschnitte echter Nachrichten. Ein Vergleich mit echten Nachrichten steht aus (Handtest L2, `[OFFEN]`).

Nur Platzhalter: Kanal `testkanal` (100000001), Bot `testbot` (100000002), Zuschauer 1000001xx, raidender Kanal 100000201. Keine echten Namen, Konten oder Tokens.

## Aufbau

Jede Nachricht hat `metadata` (`message_id`, `message_type`, `message_timestamp`, bei Benachrichtigungen `subscription_type` und `subscription_version`) und `payload`. Quelle: https://dev.twitch.tv/docs/eventsub/websocket-reference/ und https://dev.twitch.tv/docs/eventsub/handling-websocket-events/

## Dateien

| Datei | Inhalt | Quelle |
|---|---|---|
| `session_welcome.json` | Welcome, Keepalive 10 s | https://dev.twitch.tv/docs/eventsub/websocket-reference/ |
| `session_keepalive.json` | Keepalive, leeres `payload` | https://dev.twitch.tv/docs/eventsub/websocket-reference/ |
| `session_reconnect.json` | Reconnect mit `reconnect_url` | https://dev.twitch.tv/docs/eventsub/websocket-reference/ |
| `revocation.json` | Abo entzogen, `authorization_revoked` | https://dev.twitch.tv/docs/eventsub/websocket-reference/ |
| `chat_message.json` | Zuschauer mit Abo-Abzeichen, Text und Emote | https://dev.twitch.tv/docs/eventsub/reference/chat#channel-chat-message |
| `chat_ohne_emote.json` | nur Text, ohne Abzeichen, leere Farbe | dieselbe |
| `chat_antwort.json` | Antwort (`reply`) mit Erwähnung | dieselbe |
| `chat_moderator.json` | Moderator mit Abo | dieselbe |
| `chat_vip.json` | VIP | dieselbe |
| `chat_bot.json` | das Bot-Konto schreibt (Moderator) | dieselbe |
| `chat_kanal.json` | der Kanal schreibt (Abzeichen `broadcaster`) | dieselbe |
| `chat_user_intro.json` | erste Nachricht überhaupt (`user_intro`) | dieselbe |
| `chat_cheer.json` | Nachricht mit Cheer (`cheer.bits`) | dieselbe |
| `chat_hervorgehoben.json` | hervorgehoben mit Kanalpunkten | dieselbe |
| `follow.json` | Follow (Version 2) | https://dev.twitch.tv/docs/eventsub/reference/channel#channel-follow |
| `subscribe.json` | Abo ohne Geschenk, Stufe 1 | https://dev.twitch.tv/docs/eventsub/reference/channel#channel-subscribe |
| `subscribe_geschenk.json` | geschenktes Abo (`is_gift`), Stufe 2 | dieselbe |
| `resubscribe.json` | Abo verlängert mit Nachricht, Stufe 3, Serie nicht geteilt | https://dev.twitch.tv/docs/eventsub/reference/channel#channel-subscription-message |
| `gift_bomb.json` | Geschenkbündel (5) | https://dev.twitch.tv/docs/eventsub/reference/channel#channel-subscription-gift |
| `gift_bomb_anonym.json` | Geschenkbündel anonym (10) | dieselbe |
| `cheer.json` | Cheer | https://dev.twitch.tv/docs/eventsub/reference/channel#channel-cheer |
| `cheer_anonym.json` | Cheer anonym | dieselbe |
| `raid.json` | Raid auf den Kanal | https://dev.twitch.tv/docs/eventsub/reference/channel#channel-raid |
| `reward_redemption.json` | Kanalpunkte eingelöst | https://dev.twitch.tv/docs/eventsub/reference/channel-points#channel-channel-points-custom-reward-redemption-add |
| `stream_online.json` | Stream gestartet | https://dev.twitch.tv/docs/eventsub/reference/stream#stream-online |
| `stream_offline.json` | Stream beendet | https://dev.twitch.tv/docs/eventsub/reference/stream#stream-offline |
| `streams_live.json` | Antwort von `GET /helix/streams`, Stream läuft | https://dev.twitch.tv/docs/api/reference/streams#get-streams |
| `streams_offline.json` | dieselbe Antwort ohne Stream (leeres `data`, laut Doku nur sinngemäss) | dieselbe |
