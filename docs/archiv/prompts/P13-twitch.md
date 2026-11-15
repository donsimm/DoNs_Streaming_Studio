# P13 – Twitch-Anbindung mit Ereignisfluss (Plan)

Plan des Projektleiter-Agenten vom 2026-11-09, angepasst am 2026-11-15 (zweimal: Aktionssystem, dann Entwurf der Oberfläche) an die Entscheide des Nutzers vom 2026-11-09 und 2026-11-15 (P14 wird ein Aktionssystem wie Streamer.bot, `docs/archiv/prompts/P14-chatbot.md`). Grundlage:
- Bauplan P13 und Punkt 92 (`docs/04-offene-punkte.md`)
- Regeln 4, 5, 8, 9, 12 und 14 (`CLAUDE.md`)
- `docs/01-festlegungen.md` («Module», F1)
- `docs/08-kern-schnittstelle.md` (Version 13)
- Bericht `~/.claude/lotse/berichte/archiv/dss-bauplan-pruefung.md` (Befunde 2, 3, 7)
- Recherchen von Lotse vom 2026-11-09 (nicht im Repo): «Chat-Bot als Aktionssystem» (Abschnitt 3.3, Rechte und Ereignisse) und «So nutzt der Nutzer Streamer.bot» (Argumentnamen in Abschnitt 3.2)

Ausführung in drei bis vier Sitzungen des Agenten, danach Handtest. P14 (Aktionssystem mit Editor) baut auf P13 auf.

## Entscheide des Nutzers

- Module sind fest eingebaut. Jedes Modul nutzt dieselbe Schnittstelle zum Ereignisfluss, es gibt keine Plugins (Regel 14, `01-festlegungen.md:22`).
- Zugangsdaten stehen nie im Klartext (Regel 12). Das Muster ist F1 (P10c): `systemd-creds --user`, ohne Nachfrage.
- Die Verbindungsknöpfe im Kopfbereich zeigen Twitch, YouTube, OBS und NDI mit vier Farben (`01-festlegungen.md:120`). YouTube gehört nicht zu P13.
- **2026-11-09** (Antworten auf die Fragen des ersten Plans):
  - Anmeldung mit Code im Browser (Gerätecode, englisch «Device Code Flow»), öffentliche App (nur Client-ID, kein Client Secret).
  - Der Kanal ist Affiliate.
  - Alles lesen **und schreiben**, was Twitch erlaubt.
  - Anzeige: Bereich «Twitch» im Dialog «Einstellungen», Liste «Ereignisse» im Reiter «Chat-Bot».
  - Der Chat-Bot schreibt mit einem **eigenen Bot-Konto** (zweite Anmeldung mit Code). Follows kommen als Ereignis direkt von Twitch.
  - Go für P13.
- **2026-11-15:**
  - Reihenfolge: P13 Twitch, dann P14 Chat-Bot, danach die Kamerasteuerung.
  - P14 ist ein Aktionssystem wie Streamer.bot. Die Ereignisse aus P13 sind direkt seine Auslöser.
  - Der Bot läuft nur, solange das Studio offen ist (Kern wie heute). Das reicht.
  - Oberfläche nach dem Entwurf (`lotse/entwurf-chatbot`): Reiter «Chat-Bot» mit Unterreitern, die Docks des Overlay-Editors sind dort ausgeblendet.
  - Zuschauer-Daten aus Streamer.bot werden übernommen (P14), zugeordnet über die Twitch-Benutzer-ID.
  - **Abzeichen «Chat Bot»: ja (Frage 2 B).** Die App wird vertraulich registriert (mit Client Secret); der Bot sendet mit App-Token und trägt das Abzeichen. Das Secret gibt der Nutzer direkt im Studio ein, abgelegt wie F1. Das ersetzt «öffentliche App, kein Secret» vom 2026-11-09.
  - Bot-Konto: das bisherige aus der Zeit mit Streamer.bot, es steht bereit (Frage 2, Zusatz).
  - Nicht für die ersten Streams: Kanalpunkte-Effekte, Kategorie per Befehl, Umfrage und Vorhersage, Moderations-Knöpfe. Die Rechte dafür holt P13 trotzdem schon (Entscheid «alles lesen und schreiben»), damit später keine neue Anmeldung nötig ist.

## Was sich gegenüber dem Plan vom 2026-11-09 ändert

| Teil | Vorher | Jetzt |
|---|---|---|
| Konten | nur das Kanal-Konto | zwei Anmeldungen: Kanal (lesen, Ereignisse, schreiben) und Bot (schreiben in den Chat), 2.1 |
| Rechte | nur lesend | lesen und schreiben, alles ausser einer kurzen Liste gefährlicher Rechte (2.4, Frage 1) |
| Ereignisse | 10 Typen, eigene Felder | dazu «erste Nachricht eines Zuschauers», Geschenk einzeln und als Bündel getrennt, Rollen und Bot-Kennzeichen in der Chat-Nachricht, Stream-Zustand beim Start; je Feld der Name des Arguments für P14 (1.3, 1.4) |
| Abzeichen «Chat Bot» | offen | entschieden 2026-11-15: mit Abzeichen, vertrauliche App mit Secret, Senden des Bots mit App-Token (2.4) |
| Fragen | Anmeldung, Ereignisse, Anzeige, Go | beantwortet; Abzeichen entschieden; offen nur noch Rechte und Go für den angepassten Plan |

## Wichtigste Befunde

| Befund | Beleg |
|---|---|
| Einen Ereignisfluss gibt es nicht. OBS gibt nur die Qt-Signale `stateChanged` und `currentSceneChanged` nach aussen, verbunden an einer Stelle | `src/obs/obsconnection.h:103-108`, `src/core/coreservice.cpp:495-498` |
| `dss_obs` bindet absichtlich weder `dss_common` noch `protocol.h` ein. Das Passwort liegt in `ObsSecretStore` (`systemd-creds` über `QProcess`, stdin und stdout, `QSaveFile` mit 0600) | `src/obs/CMakeLists.txt:1-3`, `src/obs/obssecretstore.h:13-31` |
| Twitch ist im Code nur als Knopf vorhanden (`connections.twitch` immer `off`). Der Reiter «Chat-Bot» zeigt «Noch nicht vorhanden» | `src/common/connectionstate.h:11`, `src/app/mainwindow.cpp:183-189` |
| TLS ist neu, denn OBS läuft über `ws://`. Vorhanden sind `libqopensslbackend.so`, `libssl3t64` 3.5.7-1~deb13u3 und `ca-certificates` 20250419+deb13u1, dazu Qt Network 6.8.2+dfsg-9+deb13u2 und `libqt6websockets6` 6.8.2-3 | `/usr/lib/x86_64-linux-gnu/qt6/plugins/tls/`, `/var/lib/dpkg/status` (gelesen 2026-11-09) |
| Qt NetworkAuth ist nicht installiert. `QOAuth2DeviceAuthorizationFlow` gibt es erst seit Qt 6.9, Debian 13 hat `qt6-networkauth-dev` 6.8.2-4. Der Gerätefluss besteht aus zwei POST-Aufrufen und wird mit `QNetworkAccessManager` selbst gebaut | https://doc.qt.io/qt-6/qoauth2deviceauthorizationflow.html, https://packages.debian.org/trixie/qt6-networkauth-dev |
| Gerätefluss: `POST https://id.twitch.tv/oauth2/device` liefert `device_code`, `user_code`, `verification_uri`, `expires_in` (Beispiel 1800) und `interval` (Beispiel 5). Danach wird `POST …/oauth2/token` mit `grant_type=urn:ietf:params:oauth:grant-type:device_code` abgefragt. Solange der Nutzer nicht bestätigt hat, kommt 400 `authorization_pending`. Ein Public Client braucht **kein Client Secret** | https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/ |
| Ein Access-Token gilt 4 h. Refresh-Tokens aus dem Gerätefluss sind «for one time use only». Beim Public Client verfallen sie nach 30 Tagen, wobei eine Seite «after they are generated» schreibt und die andere «inactive». Die Refresh-Seite sagt zudem, nur Tokens aus dem Autorisierungscode liessen sich ohne Zustimmung erneuern. **Das widerspricht der Seite zum Gerätefluss** | dieselbe Seite und https://dev.twitch.tv/docs/authentication/refresh-tokens/ |
| Pflicht: «validate the OAuth token when it starts and on an hourly basis thereafter», ausdrücklich auch für Chat-Bots (`GET https://id.twitch.tv/oauth2/validate`) | https://dev.twitch.tv/docs/authentication/validate-tokens/ |
| Für das Registrieren der App muss am Konto die Zwei-Faktor-Anmeldung aktiv sein. Die Felder sind Name (eindeutig), OAuth Redirect URLs und Category. Das Feld «Client Type» nennt die Seite nicht | https://dev.twitch.tv/docs/authentication/register-app/ |
| Für den Chat empfiehlt Twitch EventSub und die API: «The preferred method of viewing and sending chats on Twitch is through EventSub and Twitch API» | https://dev.twitch.tv/docs/chat/ |
| Das Abzeichen «Chat Bot» gibt es nur beim Senden mit einem **App-Token**, wenn der Bot `user:bot` und der Kanal `channel:bot` freigegeben hat (oder der Bot Moderator ist) und das Bot-Konto nicht das Kanal-Konto ist: «chatbots that utilize User Access Tokens will not appear in chat with the Chat Bot Badge». Ein App-Token entsteht nur mit Client Secret | https://dev.twitch.tv/docs/chat/, https://dev.twitch.tv/docs/chat/authenticating/, https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/ |
| Senden in den Chat: `POST https://api.twitch.tv/helix/chat/messages`, mit User-Token reicht `user:write:chat`; `sender_id` muss das Konto des Tokens sein. Für den Bot heisst das: eigenes Token des Bot-Kontos | https://dev.twitch.tv/docs/api/reference/#send-chat-message |
| Grenzen beim Schreiben: 20 Nachrichten je 30 s, 100 für Kanal, Moderatoren und VIPs; ein Bot als Moderator darf also mehr | https://dev.twitch.tv/docs/chat/ |
| EventSub über WebSocket (`wss://eventsub.wss.twitch.tv/ws`): Nach `session_welcome` bleiben 10 s für das erste Abo, sonst Close 4003. `keepalive_timeout_seconds` liegt zwischen 10 und 600. `session_reconnect` kommt 30 s vorher mit `reconnect_url`, die alte Verbindung wird erst nach dem Welcome auf der neuen geschlossen, die Abos bleiben. Verlorene Ereignisse werden nicht nachgeliefert. Close-Codes 4000 bis 4007 | https://dev.twitch.tv/docs/eventsub/handling-websocket-events/ |
| Ein Abo über WebSocket geht nur mit einem User-Token (`POST https://api.twitch.tv/helix/eventsub/subscriptions`). Je Token höchstens 3 Verbindungen und 300 Abos je Verbindung. Hat der Nutzer die App freigegeben, kostet ein Abo nichts | https://dev.twitch.tv/docs/eventsub/manage-subscriptions/ |
| `message_type` der Chat-Nachricht kennt `user_intro` (erste Nachricht eines Zuschauers im Kanal überhaupt). «Erste Nachricht in diesem Stream» (bei Streamer.bot «First Words») liefert Twitch nicht, das muss der Kern selbst feststellen | https://dev.twitch.tv/docs/eventsub/eventsub-reference/#chat-message |
| Streamer.bot nennt Argumente in Texten `%user%`, `%userName%`, `%targetUser%`, `%targetUserProfileImageUrl%`, `%broadcastUser%`, `%broadcastUserName%`, `%rawInput%`, `%input0%`, `%game%`, `%tier%`, `%role%`, `%isFollowing%`, `%followDate%` (so in den Daten des Nutzers). Die vollständigen Listen je Auslöser sind in der Doku eingeklappt | Recherche «So nutzt der Nutzer Streamer.bot», Abschnitt 3.2; https://docs.streamer.bot/api/triggers/core/commands/command-triggered |
| Twitch CLI hat einen eigenen Testserver (`twitch event websocket start-server`, [BETA]). Er ist ein Go-Programm und nicht in Debian | https://dev.twitch.tv/docs/cli/websocket-event-command/ |

## 1. Ereignisfluss im Kern (Regel 14, Punkt 92)

### 1.1 Aufbau

Neue Bibliothek `src/events/` (`dss_events`) mit Qt Core, ohne Qt Gui und ohne `protocol.h`. Sie ist klein und wird von allen Modulen genutzt.

```cpp
namespace dss::events {
struct Event {
    QUuid id;          // vom Kern vergeben
    QString type;      // «twitch.chat_message», «obs.scene_changed», …
    QString origin;    // «twitch», «obs», später «actions», …
    QDateTime time;    // UTC, Zeitpunkt laut Herkunft (Twitch: message_timestamp), sonst Empfang
    QString originId;  // Kennung bei der Herkunft (Twitch: message_id), leer wenn keine
    QJsonObject data;  // Felder je Typ, eigene Namen (1.3), nie Rohdaten oder Token
};
class EventBus : public QObject {   // lebt im Hauptthread des Kerns
public:
    void publish(Event event);       // unbekannter Typ: abgewiesen und Warnung im Protokoll
    Subscription subscribe(QObject *context, QStringList filters,
                           std::function<void(const Event &)> handler);
    QList<Event> history(const QStringList &filters) const;  // letzte 200, nur im Speicher
};
}
```

- **Abonnieren:**
  - Filter sind genaue Typen oder Vorsilben mit Punkt (`twitch.`). Eine leere Liste heisst «nichts».
  - `Subscription` meldet sich beim Zerstören ab. Wird das Kontextobjekt zerstört, endet das Abo ebenfalls.
- **Zustellen:**
  - Immer verzögert über das Kontextobjekt (`QMetaObject::invokeMethod`, `Qt::QueuedConnection`), nie im Aufruf von `publish`.
  - Darum gibt es keine Rekursion, wenn ein Abonnent selbst etwas veröffentlicht. Die Reihenfolge bleibt je Abonnent gleich.
  - Ein Abonnent in einem anderen Thread, zum Beispiel der Mischer aus P12, bekommt die Ereignisse in seinem Thread.
- **Nur Ereignisse, keine Befehle:**
  - Über den Fluss laufen nur Dinge, die geschehen sind.
  - Handlungen gehen über die Schnittstelle des Dienstes, zum Beispiel in P14 `TwitchChat::send(...)`. Das Ergebnis kommt wieder als Ereignis.
- **Typenliste:**
  - `src/events/eventtypes.h` führt jeden Typ mit seinen Pflichtfeldern **und dem Namen des Arguments für P14** (1.4).
  - `publish` prüft gegen diese Liste. So bleiben Code und `docs/08` deckungsgleich.

### 1.2 Wer veröffentlicht, wer hört

| Modul | Rolle in P13 | Später |
|---|---|---|
| Twitch (`dss_twitch`, neu) | veröffentlicht `twitch.*` direkt auf dem Fluss | P14: Chat senden und weitere Aufrufe über `TwitchApi` |
| OBS (`dss_obs`) | bleibt unverändert. Ein kleiner Adapter im Kern (`src/core/obsevents.{h,cpp}`) veröffentlicht `obs.scene_changed` aus `currentSceneChanged`. Damit ist das Signal aus Punkt 92 auf dem Fluss | E-18 hört auf `obs.scene_changed` |
| Fenster | hört über den Socket (1.5) | P14: Reiter «Chat-Bot», Dock «Chat» |
| Browserquelle (`web/`) | in P13 nichts | E-17 und TX-16: Der Overlay-Server abonniert für ein Overlay nur die Typen, die seine Quellen brauchen. Nur öffentliche Felder, nie Kennungen des Kerns oder Token |
| Ton (P12) | in P13 nichts. Ein Test in `tst_ereignisfluss` stellt sicher, dass ein Abonnent in einem anderen Thread geht | P14: Schritt «Ton abspielen» über einen eigenen Ausgang |
| Aktionssystem (P14) | – | abonniert alle Typen aus 1.3, die als Auslöser dienen, und veröffentlicht eigene Typen (`actions.*`) |

### 1.3 Ereignistypen in P13

Für jeden Typ gelten die Grundfelder aus 1.1. In der Tabelle steht nur `data`. Die Werte von `*_name` sind Anzeigenamen, die von `*_login` Kleinbuchstaben. `user_*` heisst `user_id`, `user_login`, `user_name`. Jeder Typ, der als Auslöser in P14 dient, ist mit **A** markiert.

| Typ | A | Twitch-Abo (Version) | `data` |
|---|---|---|---|
| `twitch.chat_message` | A | `channel.chat.message` (1) | `message_id`, `user_*`, `text`, `fragments[]` (`type`, `text`, `emote_id` oder `null`), `color`, `badges[]` (`set_id`, `id`), `role` (1 Zuschauer, 2 VIP, 3 Moderator, 4 Kanal), `is_subscriber`, `is_vip`, `is_moderator`, `is_broadcaster`, `is_bot_account` (Absender ist das angemeldete Bot-Konto), `bits` (0 ohne Cheer), `reply_to` (message_id oder `null`), `reward_id` (oder `null`), `message_type`, `source_broadcaster_id` (oder `null`, geteilter Chat) |
| `twitch.first_message` | A | – (abgeleitet im Kern, 2.6) | alle Felder von `twitch.chat_message`, dazu `first_ever` (`message_type` = `user_intro`) |
| `twitch.follow` | A | `channel.follow` (2) | `user_*`, `followed_at` |
| `twitch.subscribe` | A | `channel.subscribe` (1) | `user_*`, `tier` (`1000`, `2000`, `3000`), `is_gift` |
| `twitch.resubscribe` | A | `channel.subscription.message` (1) | `user_*`, `tier`, `text`, `months`, `streak_months` (oder `null`), `duration_months` |
| `twitch.gift_bomb` | A | `channel.subscription.gift` (1) | `user_*` (`null` bei anonym), `is_anonymous`, `total`, `tier`, `cumulative_total` (oder `null`) |
| `twitch.gift_sub` | A | – (aus `twitch.subscribe` mit `is_gift`) | `user_*` des Beschenkten, `tier`. Den Schenkenden liefert `channel.subscribe` nicht (Schritt 0) |
| `twitch.cheer` | A | `channel.cheer` (1) | `user_*` (`null` bei anonym), `is_anonymous`, `bits`, `text` |
| `twitch.raid` | A | `channel.raid` (1), Bedingung `to_broadcaster_user_id` | `from_user_id`, `from_user_login`, `from_user_name`, `viewers` |
| `twitch.reward_redeemed` | – | `channel.channel_points_custom_reward_redemption.add` (1) | `redemption_id`, `user_*`, `user_input`, `reward_id`, `reward_title`, `reward_cost`, `status`. Auslöser erst später (Kanalpunkte-Effekte, Entscheid 2026-11-15) |
| `twitch.stream_online` | A | `stream.online` (1) | `started_at`, `kind`, `at_start` (`true`, wenn der Kern beim Verbinden einen laufenden Stream vorfand, 2.5) |
| `twitch.stream_offline` | A | `stream.offline` (1) | – |
| `obs.scene_changed` | – | – (OBS) | `scene_uuid`, `scene_name` |

- `twitch.subscribe` mit `is_gift` geht als `twitch.gift_sub` hinaus, nicht zusätzlich als `twitch.subscribe`. So löst ein Bündel von 10 Geschenken kein «Danke für dein Abo» an 10 Beschenkte aus. Das entspricht den getrennten Auslösern «Gift Sub» und «Gift Bomb» von Streamer.bot.
- `channel.chat.notification` gehört nicht zu P13: Subs und Raids kommen über die eigenen Abos.
- Weitere Typen, für die P13 die Rechte schon holt (Umfrage, Vorhersage, Hype Train, Shoutout erhalten, Werbung, Moderation): kein Abo in P13. Sie kommen mit dem Baustein, der sie braucht, ohne neue Anmeldung.

### 1.4 Argumente für das Aktionssystem (P14)

P14 setzt jedes Ereignis ohne Umweg in die Argumente eines Laufs um. Die Namen folgen Streamer.bot, damit Texte wie «Danke für den Follow, %user%!» später ohne Umschrift importiert werden. Die Zuordnung steht in `eventtypes.h` neben den Pflichtfeldern.

| Feld | Argument | Beleg |
|---|---|---|
| `user_name` | `user` | Daten des Nutzers |
| `user_login` | `userName` | Daten des Nutzers |
| `user_id` | `userId` | Schritt 0 |
| `message_id` | `msgId` | Doku «Command Triggered» |
| `text` | `message`; bei Befehlen zusätzlich `rawInput`, `input0` … | `rawInput`, `input0` Daten des Nutzers; `message` Schritt 0 |
| `role` | `role` | Daten des Nutzers (Wert 4 wird in «First Chatters» ausgeschlossen); Werte 1 bis 3 Schritt 0 |
| `is_subscriber`, `is_vip`, `is_moderator` | `isSubscribed`, `isVip`, `isModerator` | Schritt 0 |
| `tier` | `tier` | Daten des Nutzers; Form des Werts («1000» oder «tier 1») Schritt 0 |
| `bits`, `viewers`, `months`, `streak_months`, `total`, `is_anonymous` | Namen nach Schritt 0 | Schritt 0 |
| `from_user_name`, `from_user_login` (Raid) | `user`, `userName` | Schritt 0 |
| Kanal (aus der Anmeldung) | `broadcastUser`, `broadcastUserName`, `broadcastUserId` | erste zwei Daten des Nutzers |

- Ist ein Name nach Schritt 0 nicht belegbar, nimmt der Agent einen eigenen in derselben Schreibweise und führt ihn als `[OFFEN]` im Bericht.
- Felder ohne Gegenstück bei Streamer.bot bekommen eigene Argumente mit demselben Namen in der Schreibweise von Streamer.bot (zum Beispiel `firstEver`).

### 1.5 Ereignisse über den Socket zum Fenster (Antwort auf Punkt 92: ja, nur auf Bestellung)

Die Version der Schnittstelle steigt um 1 auf 14.

- **`subscribe_events`** (Fenster → Kern): `types` (Liste von Filtern, leer = abbestellen) und `history` (true/false).
  - Antwort: `event_history` mit `events[]` (bei `history: true`, höchstens 200), danach `event` mit `event` für jedes neue Ereignis, **nur an diese Verbindung**.
  - Ohne Bestellung kommt nichts. Ein altes Fenster bestellt nicht und bekommt also nichts.
- **Gegendruck:** Liegen bei einer Verbindung mehr als 1 MiB ungesendet (`bytesToWrite`), verwirft der Kern weitere Ereignisse für sie und schickt danach einmal `events_dropped` mit `count`. So bremst ein hängendes Fenster den Kern nicht.
- **`dss-cli ereignisse [--folgen] [--typ FILTER]`:** zeigt den Verlauf und mit `--folgen` neue Ereignisse, eine JSON-Zeile je Ereignis. Werkzeug für Lotse im Handtest, ohne Bildschirmfoto.

## 2. Twitch

### 2.1 Anmeldung: zwei Konten

| Konto | Wofür | Anmeldung |
|---|---|---|
| **Kanal** | Ereignisse (EventSub geht nur mit dem Token des Kanals), Chat lesen, alle Aufrufe mit Rechten des Kanals (Shoutout, Ansage, später Kategorie, Umfrage …), Chat schreiben «als Kanal» | Gerätecode im Kanal-Konto |
| **Bot** | Chat schreiben «als Bot» (Streamer.bot: «an optional second connection … can only send chat messages or whispers») | Gerätecode im Bot-Konto, eigene Datei. Beide nutzen dieselbe Client-ID |

- **Ablauf je Konto:**
  - Der Kern startet den Gerätefluss für das gewählte Konto.
  - Das Fenster zeigt `user_code`, die Adresse `verification_uri` von Twitch, «Im Browser öffnen» (`QDesktopServices`), «Code kopieren» und die Restzeit.
  - Der Kern fragt im Abstand `interval` ab: 400 `authorization_pending` heisst weiter warten. Ist `expires_in` abgelaufen oder kommt «invalid device code», heisst die Meldung «Code abgelaufen, neu anmelden».
  - Hinweis im Fenster beim Bot: «Im Browser zuerst vom Kanal-Konto abmelden oder ein privates Fenster nutzen.» Sonst bestätigt der Nutzer den Code versehentlich mit dem Kanal-Konto.
- **Prüfung nach der Anmeldung:** Ist das Bot-Konto dasselbe wie das Kanal-Konto (`user_id` gleich), weist der Kern die Anmeldung ab: «Das Bot-Konto ist das Kanal-Konto. Im Browser mit dem Bot-Konto anmelden.»
- **Ohne Bot-Konto** läuft alles; Schritte «als Bot» senden dann als Kanal und schreiben einen Hinweis in den Verlauf (P14).
- **Kein offener Port, keine Rückrufadresse.** Bei der Registrierung ist die Rückrufadresse Pflicht, dort steht `http://localhost`. Sie wird beim Gerätecode nicht benutzt.
- **Client Secret (Entscheid 2026-11-15, Abzeichen):** Die App ist vertraulich. Das Secret braucht der Kern für das App-Token (2.4). Ob der Gerätefluss einer vertraulichen App das Secret bei `…/oauth2/device` oder `…/oauth2/token` zusätzlich verlangt, klärt Schritt 0; der Kern schickt es dann mit, nie in Protokoll oder Nachrichten.
- **Der `device_code` verlässt den Kern nie.** `user_code` geht nur an das Fenster, nicht ins Protokoll.

### 2.2 Token sicher ablegen (Regel 12)

- **Verallgemeinern:** `ObsSecretStore` wird zu `dss::SecretStore` in der neuen Bibliothek `src/secrets/` (`dss_secrets`).
  - Das Verhalten bleibt gleich. `obs-passwort.cred` behält Name und Inhalt.
  - `dss_obs` und `dss_twitch` nutzen beide diese Bibliothek. `dss_obs` bindet weiter kein `dss_common` ein.
- **Dateien** neben `kern.ini`, Rechte 0600, je Version getrennt:
  - `twitch-token.cred` (`--name=dss-twitch-token`) für den Kanal;
  - `twitch-bot-token.cred` (`--name=dss-twitch-bot-token`) für den Bot.
  - Inhalt je als JSON: `access_token`, `refresh_token`, `expires_at`, `user_id`, `login`, `scopes`.
- **Client Secret:** `twitch-client-secret.cred` (`--name=dss-twitch-client-secret`), Inhalt nur das Secret. Eingabe im Bereich «Twitch» der Einstellungen (Feld mit verdeckter Eingabe, «Secret speichern», «Secret löschen»); das Fenster schickt es einmal über den Socket, der Kern legt es ab und gibt es nie zurück (nur «gesetzt ja/nein»).
- **Client-ID:** Sie ist kein Geheimnis und steht in `kern.ini` unter `[twitch] client-id`. Dazu kommt `[twitch] aktiv=ja|nein`, Standard `nein`.
- **Protokoll und Nachrichten:** Token, Refresh-Token und `device_code` stehen nie in Protokoll, Nachrichten oder Tests im Klartext. `tst_kerntwitch` sucht die Testtoken im Protokoll und im Socket-Verkehr.
- **Grenze wie F1:** Andere Programme desselben Benutzers könnten die Dateien entschlüsseln.

### 2.3 Erneuerung und Prüfung (je Konto)

- **Erneuern:**
  - Von selbst 10 min vor `expires_at`, ausserdem nach jedem 401 einmal, danach wird die Anfrage wiederholt.
  - Das neue Refresh-Token wird **sofort** geschrieben, denn das alte gilt nach einmaligem Gebrauch nicht mehr.
- **Prüfen:** `validate` beim Start und danach alle 60 min (Pflicht laut Twitch). Dabei werden die Rechte mit den nötigen verglichen.
- **Fehlschläge:**
  - Refresh abgewiesen (400 bzw. 401) oder `validate` mit 401: Knopf rot (Kanal) bzw. Hinweis «Bot abgemeldet» (Bot), Meldung «Anmeldung bei Twitch abgelaufen (Kanal|Bot). Im Dialog «Einstellungen» neu anmelden.» Danach kein weiterer Versuch für dieses Konto, bis der Nutzer handelt (wie F9 und 4009 bei OBS).
  - Netzfehler: neuer Versuch nach 2 s, danach jeweils doppelt so lang bis 60 s, ±20 % Zufall.
- **«Abmelden»:** `POST …/oauth2/revoke`, Datei des Kontos löschen. Beim Kanal wird die Verbindung getrennt.

### 2.4 Rechte (Scopes) (Frage 1)

Die Namen stammen aus der Recherche vom 2026-11-09 (https://dev.twitch.tv/docs/authentication/scopes/). Schritt 0 prüft jeden Namen gegen diese Seite und gegen die Endpunkte, die P14 nutzt.

**Kanal-Konto, lesen:**

| Zweck | Rechte |
|---|---|
| Ereignisse aus 1.3 | `user:read:chat`, `moderator:read:followers`, `channel:read:subscriptions`, `bits:read`, `channel:read:redemptions` |
| später, ohne neue Anmeldung | `channel:read:hype_train`, `channel:read:polls`, `channel:read:predictions`, `channel:read:ads`, `channel:read:goals`, `channel:read:charity`, `channel:read:vips`, `channel:read:editors`, `moderator:read:shoutouts`, `moderator:read:chatters`, `moderator:read:chat_settings`, `moderator:read:banned_users`, `moderation:read`, `user:read:whispers`, `user:read:emotes` |

**Kanal-Konto, schreiben:**

| Zweck | Rechte |
|---|---|
| P14 | `user:write:chat` (Chat als Kanal), `moderator:manage:shoutouts` (Shoutout), `moderator:manage:announcements` (Ansage) |
| später (Entscheid 2026-11-15: nicht für die ersten Streams) | `channel:manage:broadcast` (Titel, Kategorie, Marker), `channel:manage:polls`, `channel:manage:predictions`, `channel:manage:redemptions`, `channel:edit:commercial`, `channel:manage:ads`, `channel:manage:raids`, `clips:edit`, `moderator:manage:banned_users` (Timeout, Bann), `moderator:manage:chat_messages`, `moderator:manage:chat_settings`, `channel:manage:vips`, `user:manage:whispers` |

**Bot-Konto:** `user:write:chat`, `user:bot` (Abzeichen) und `moderator:manage:announcements` (Ansage als Bot, wirkt nur, wenn der Bot Moderator ist). Lesen geht immer über den Kanal.

**Kanal-Konto, für das Abzeichen:** zusätzlich `channel:bot` (erlaubt dem Bot das Schreiben mit App-Token; als Moderator ginge es auch ohne, `channel:bot` macht es unabhängig davon).

**Weggelassen (Empfehlung, Frage 1):** `channel:read:stream_key` (Streamschlüssel), `user:read:email`, `user:edit`, `channel:manage:videos` (Videos löschen), `channel:manage:moderators`, `channel:manage:extensions`.

- Fehlt ein Recht, zeigt der Kern «Rechte fehlen: … Neu anmelden.». Die Neuanmeldung verlangt die nötigen Rechte neu, die vorhandenen Ereignisse laufen bis dahin weiter.
- Raid, Stream online und offline brauchen kein Recht (Schritt 0).

**Abzeichen «Chat Bot» (Frage 2, entschieden 2026-11-15: ja):**

- **Was es braucht:** ein App-Token (Client-Credentials-Fluss) mit Client-ID **und Client Secret**; `user:bot` vom Bot-Konto; `channel:bot` vom Kanal oder den Bot als Moderator; Bot-Konto ≠ Kanal-Konto. Gesendet wird dann mit dem App-Token statt mit dem Token des Bots.
- **Umsetzung im Studio:**
  - Die App wird von Anfang an vertraulich registriert (Abschnitt 3), es gibt keine öffentliche App.
  - Der Kern holt das App-Token mit Client-Credentials (`POST …/oauth2/token`, `grant_type=client_credentials`), legt es nur im Speicher ab und erneuert es vor Ablauf; bei 401 einmal neu holen.
  - Senden «als Bot»: `POST /helix/chat/messages` mit App-Token, `sender_id` = Bot. Ohne Secret oder ohne `user:bot`/`channel:bot` sendet der Bot mit seinem User-Token (ohne Abzeichen) und der Kern meldet «Abzeichen nicht aktiv: …» im Bereich «Twitch».
  - Aufwand etwa ½ Sitzung (Client-Credentials, Ablage des Secrets, Senden mit App-Token, Rückfall, Test).
- **Grund für den Entscheid:** Zuschauer erkennen den Bot sofort als Bot, und Twitch empfiehlt diesen Weg für Bots. Preis: ein zweites Geheimnis (Secret) und etwa ½ Sitzung mehr.

### 2.5 EventSub über WebSocket

- **Verbinden:**
  - `wss://eventsub.wss.twitch.tv/ws?keepalive_timeout_seconds=10`.
  - Nach `session_welcome` gehen alle Abos aus 1.3 parallel als POST mit der `session_id` hinaus, innerhalb von 10 s.
  - Ein abgewiesenes Abo (403 Recht fehlt, 409, 429) hält die anderen nicht auf.
- **Stream-Zustand beim Start:** Nach dem Welcome fragt der Kern `GET https://api.twitch.tv/helix/streams?user_id=<Kanal>` (Schritt 0: Recht, Antwort bei offline). Läuft der Stream, veröffentlicht er einmal `twitch.stream_online` mit `at_start: true`. So wissen Zeitnachrichten «nur im Stream» und «erste Nachricht» Bescheid, auch wenn das Studio erst während des Streams startet. `twitch_status.live` zeigt den Zustand.
- **Keine Nachricht innerhalb von Keepalive + 5 s:** Die Verbindung gilt als verloren. Neu verbinden mit Wartezeit wie 2.3, die Abos neu anlegen.
- **`session_reconnect`:** neue Verbindung zu `reconnect_url` (unverändert benutzt), die alte erst nach deren `session_welcome` schliessen. Keine neuen Abos.
- **`revocation`:** Das Abo gilt als weg, die Meldung nennt `status`. Bei `authorization_revoked` wird die Anmeldung als ungültig behandelt.
- **Close-Codes:**
  - 4003 und 4007 sind Fehler im eigenen Ablauf: Protokoll als kritisch, neu verbinden.
  - 4000, 4005 und 4006: neu verbinden.
  - 4001, 4002 und 4004: Protokoll und neu verbinden.
- **Doppelte Nachrichten:** Die letzten 500 `message_id` werden gemerkt, eine doppelte wird übergangen. Die Doku sagt dazu nichts, der Aufwand ist klein.
- **Verbindungen und Kosten:** genau 1 Verbindung (Grenze 3), 11 Abos (Grenze 300), Kosten 0.
- **TLS:** Prüfung über die Zertifikate des Systems, kein Abschalten der Prüfung. `QSslSocket::supportsSsl()` wird beim Start geprüft, sonst gilt die Meldung «TLS fehlt».

### 2.6 Bibliothek `src/twitch/` (`dss_twitch`)

Qt Core, Network und WebSockets, kein Qt Gui und kein `protocol.h`, bindet `dss_events` und `dss_secrets` ein.

| Datei | Inhalt |
|---|---|
| `eventsubprotocol.{h,cpp}` | Nachrichten lesen und bauen, Ereignisse in `data` nach 1.3 umsetzen (Rollen aus `badges[]`, `is_bot_account`, Geschenke trennen), reine Funktionen |
| `twitchauth.{h,cpp}` | Gerätefluss, Erneuerung, Prüfung, Abmelden; eine Instanz je Konto |
| `helixclient.{h,cpp}` | Aufrufe mit `Client-Id` und Bearer des gewählten Kontos, 401 → einmal erneuern, 429 → `Ratelimit-Reset` beachten. P14 baut darauf die schreibenden Aufrufe |
| `eventsubconnection.{h,cpp}` | WebSocket, Keepalive, Reconnect, Abos |
| `chatsession.{h,cpp}` | «erste Nachricht in diesem Stream»: merkt die `user_id`, die seit `twitch.stream_online` geschrieben haben, und veröffentlicht für die erste Nachricht jedes Zuschauers zusätzlich `twitch.first_message`. Leert die Liste bei `twitch.stream_offline` und bei einem neuen `stream_online`. Ohne laufenden Stream gilt die Zeit seit dem Start des Kerns |
| `twitchservice.{h,cpp}` | hält alles zusammen, `stateChanged`, veröffentlicht auf dem Fluss |

- **Neustart während des Streams:** `chatsession` schreibt die Liste der `user_id` (ohne Namen) verzögert nach `twitch-sitzung.json` im Datenordner und liest sie beim Start, wenn derselbe Stream (`started_at`) noch läuft. Sonst würde nach einem Neustart des Kerns jeder Zuschauer noch einmal begrüsst. Bei `stream_offline` wird die Datei gelöscht.
- **Testserver:** Die Adressen von `id.twitch.tv`, `api.twitch.tv` und `eventsub.wss.twitch.tv` lassen sich für Tests ersetzen:
  - über `dss-core --twitch-testserver http://127.0.0.1:PORT`;
  - nur `127.0.0.1` ist erlaubt, sonst bricht der Start mit Exit-Code 1 ab;
  - so kann die Option kein Token an einen fremden Rechner schicken.

### 2.7 Kern, Schnittstelle, Fenster

- **Kern:**
  - `connections.twitch` zeigt den Zustand des Kanals: `off` (aus, ohne Client-ID oder ohne Anmeldung), `connecting` (erster Versuch), `connected` (alle Abos aktiv), `error` (Anmeldung ungültig, Rechte fehlen, Netz). Der Bot ändert die Farbe nicht; ein Problem des Bots steht im Tooltip.
  - Neu ist `twitch_status` mit `message`, `live`, `subscriptions[]` (`type`, `ok`, `problem`) und `accounts` mit je `channel` und `bot`: `login`, `user_id`, `missing_scopes[]`, `state` (`off`, `ok`, `expired`, `error`) und `login_pending` (`user_code`, `verification_uri`, `expires_at`) oder `null`. Es geht nach `hello` und bei jeder Änderung an alle.
  - Neue Anfragen: `twitch_login_start`, `twitch_login_cancel` und `twitch_logout`, je mit `request_id` und `account` (`channel` oder `bot`).
  - `settings` und `update_settings` bekommen zusätzlich `twitch_enabled` und `twitch_client_id`.
  - `dss-cli einstellung-twitch-client-id ID`.
- **Schalter `DSS_CORE_TWITCH`** (Standard ON): OFF heisst Kern ohne Twitch, der Knopf bleibt grau. Begründung: neue externe Verbindung mit TLS und Token, ein riskanter Teil nach Regel 8.
- **Fenster** (Entscheid 2026-11-09):
  - Bereich «Twitch» im Dialog «Einstellungen»: «Mit Twitch verbinden», «Client-ID», darunter zwei Zeilen «Kanal» und «Bot» mit je «angemeldet als …» oder «nicht angemeldet», «Anmelden …» bzw. «Abmelden» und fehlenden Rechten. Während einer Anmeldung zeigt die Zeile den Code gross mit «Im Browser öffnen», «Code kopieren», Restzeit und «Abbrechen». Darunter die Liste der Abos mit Zustand.
  - Ein Klick auf den Twitch-Knopf öffnet den Bereich (wie F3).
  - Der Reiter «Chat-Bot» bekommt schon in P13 die Kopfzeile mit Unterreitern (Stil der Modul-Reiter) und den Unterreiter «Live» mit dem Bereich «Ereignisse» (Zeit, Art, Name, Details, höchstens 200 Einträge), wie im Entwurf `entwuerfe/chatbot/1-gesamtansicht.html` (Branch `lotse/entwurf-chatbot`). P14 ergänzt «Live» um Chat und «Läufe der Aktionen» und fügt die weiteren Unterreiter hinzu (P14, Abschnitt 6).
  - Farbe des Twitch-Knopfs: Der Entwurf schlägt «grün = beide Konten verbunden» vor. Der Plan bleibt bei «Farbe nach dem Kanal», weil ohne Bot alles weiterläuft (der Kanal sendet ersatzweise); ein fehlender Bot steht im Tooltip und in der Kopfzeile des Reiters «Chat-Bot».
  - Vor dem Commit «Fenster» führt der Agent die Prüfung der Oberflächenmuster durch (`ui-consistency:finding-patterns`) und hält die Checkliste im Bericht fest. Der Bereich «Twitch» folgt dem Bereich «OBS» desselben Dialogs.

## 3. Was der Nutzer selbst tun muss

Die Schritte stehen im Skript `scripts/twitch-einrichten.sh`, Aufruf aus jedem Ordner:

```
~/DoNs_Streaming_Studio/scripts/twitch-einrichten.sh
```

1. Das Skript nennt die Voraussetzung: Zwei-Faktor-Anmeldung am Twitch-Konto des Kanals aktiv (Pflicht von Twitch).
2. Es öffnet `https://dev.twitch.tv/console/apps/create` (`xdg-open`) und zeigt, was einzutragen ist:
   - Name eindeutig, zum Beispiel «DoNs Streaming Studio <Kanalname>»;
   - OAuth Redirect URL `http://localhost`;
   - Category nach der Liste dort (`[OFFEN]`, Schritt 0);
   - Client-Typ «Confidential» (vertraulich, für das Abzeichen).
   Das Secret gibt der Nutzer **nicht** im Skript ein, sondern danach im Studio (Einstellungen → Twitch), wenn er es auf der Seite der App erzeugt hat. Das Skript sagt dazu: «Das Secret ist ein Passwort. Nicht in den Chat und nicht in eine Datei kopieren.»
3. Es fragt nach der **Client-ID**. Das Skript sagt dazu ausdrücklich: «Das ist kein Passwort und kein Token.» Es prüft die Form (nur Kleinbuchstaben und Ziffern, nicht leer) und trägt die ID mit `dss-cli einstellung-twitch-client-id` in die Version in Arbeit ein (`--einsatz` erst bei der Installation). Läuft der Kern nicht, steht dort: «Zuerst das Studio öffnen.»
4. Zum Schluss nennt es die nächsten Schritte:
   - im Studio auf den Twitch-Knopf klicken, unter «App» das Client Secret eingeben und speichern;
   - bei «Kanal» «Anmelden …», den Code im Browser bestätigen;
   - danach bei «Bot» «Anmelden …», den Code in einem privaten Browserfenster mit dem Bot-Konto bestätigen;
   - im eigenen Chat das Bot-Konto zum Moderator machen (`/mod <Botname>`).

Eine neue Claude-Code-Sitzung ist dafür nicht nötig.

## 4. Commits, Prüfung, Handtest

**Schritt 0 (Agent, nur lesen):**
- Die Quellen unten lesen und bestätigen: Felder in 1.3, Argumentnamen in 1.4 (Seiten der Twitch-Auslöser bei Streamer.bot), jeden Namen der Rechte in 2.4, Kategorie bei der Registrierung, `GET /helix/streams`, ob `channel.subscribe` bei Geschenken den Schenkenden nennt, ob der Gerätefluss einer vertraulichen App das Secret verlangt, Client-Credentials-Fluss und Senden mit App-Token (`user:bot`, `channel:bot`) samt Bedingungen für das Abzeichen.
- Ausserdem `https://dev.twitch.tv/docs/chat/irc-migration/` lesen.
- Weicht etwas ab, das den Gerätefluss, die zwei Konten oder die Abos trägt: Halt und Meldung an Lotse.

**Commits (Regel 8):**
1. `src/secrets/` aus `ObsSecretStore` herauslösen, ohne Änderung des Verhaltens. `tst_obspasswort` besteht unverändert.
2. `src/events/`: Fluss, Typenliste mit Argumentnamen und Verlauf. Test `tst_ereignisfluss` ohne Qt Gui deckt ab:
   - Reihenfolge, Filter und Vorsilbe;
   - Verlauf mit 200 Einträgen;
   - Abmelden während der Zustellung, Kontext zerstört;
   - Abonnent in einem anderen Thread;
   - `publish` im Handler, unbekannter Typ;
   - 10 000 Ereignisse mit Zeitmessung.
3. Kern: Fluss in `CoreService`, Adapter `obs.scene_changed`, `subscribe_events`, `event_history`, `event`, `events_dropped`, `dss-cli ereignisse`, Schnittstelle 14.
   - Tests: `tst_protocol`, `tst_aenderungen`, `tst_kernverbindung` (Szenenwechsel vom Testserver von OBS kommt als `event` beim Fenster an, nicht beim Fenster ohne Bestellung, Gegendruck).
4. `eventsubprotocol` und `chatsession` mit Test `tst_twitchprotokoll`.
   - Daten: `tests/daten/twitch/` mit den Beispielen aus der Doku (je Datei die URL in `LIESMICH.md`) und den Fällen «anonym», «ohne Emote», «Antwort», «Geschenkbündel», «Bot schreibt», «Moderator», «VIP».
   - `chatsession`: erste Nachricht je Zuschauer, Leeren bei offline, Neustart mit `twitch-sitzung.json` im selben und in einem neuen Stream.
5. `twitchauth` und `helixclient` mit Test `tst_twitchanmeldung` gegen `tests/twitchtestserver.h`:
   - `authorization_pending`, Ablauf des Codes, Abbrechen;
   - Erneuerung mit neuem Refresh-Token, 401 → Erneuern → Wiederholen, Refresh abgewiesen;
   - stündliche Prüfung mit gekürzter Zeit, fehlende Rechte, Abmelden;
   - zwei Konten nebeneinander, Bot gleich Kanal wird abgewiesen.
6. `eventsubconnection` mit Test `tst_twitchverbindung`:
   - Abos innerhalb von 10 s, ein Abo abgewiesen;
   - Keepalive verpasst, `session_reconnect` (alte Verbindung erst nach dem Welcome zu);
   - Close-Codes, `revocation`, doppelte `message_id`, Wartezeiten;
   - Stream-Zustand beim Start (`at_start`);
   - `QSslSocket::supportsSsl()`.
7. Kern: `TwitchService`, `kern.ini [twitch]`, drei `.cred`-Dateien (Kanal, Bot, Client Secret), App-Token mit Client-Credentials für das Abzeichen, `twitch_status` und die Anfragen, `connections.twitch`, Schalter `DSS_CORE_TWITCH`, Option `--twitch-testserver`.
   - Tests: `tst_kerneinstellungen` (`[twitch]`), neu `tst_kerntwitch` mit dem Kern als eigenem Prozess gegen den Testserver:
     - Anmeldung beider Konten über den Socket, Ereignisse bis zum Fenster;
     - Neustart behält beide Anmeldungen, Abmelden löscht die Datei des Kontos;
     - **kein Testtoken in Protokoll oder Socket**;
     - `--twitch-testserver` mit einer anderen Adresse als `127.0.0.1` wird abgewiesen.
8. Fenster: Bereich «Twitch» mit Client Secret (verdeckt) und zwei Konten, Knopf, Reiter «Chat-Bot» mit Kopfzeile und Unterreiter «Live» (Bereich «Ereignisse»); die fünf Docks des Overlay-Editors sind im Reiter «Chat-Bot» ausgeblendet (Entscheid 2026-11-15). Test `tst_fensterbearbeiten`.
9. `scripts/twitch-einrichten.sh`, geprüft mit eigenem `XDG_CONFIG_HOME` und Kern mit eigenem Datenordner.
10. Dokumente:
    - `01` (Twitch, zwei Konten, F-Entscheide), `02` (TLS, Adressen von Twitch), `03`, `04` (Punkt 92 ins Archiv, neue Punkte), `05`, `06` (`twitch-token.cred`, `twitch-bot-token.cred`, `twitch-sitzung.json`), `08` (Version 14, Abschnitte «Ereignisfluss» und «Anbindung an Twitch»);
    - Bericht `docs/archiv/berichte/P13-bericht.md`.

**Testserver statt Twitch CLI:** `tests/twitchtestserver.h` folgt dem Muster von `tests/obstestserver.h`.
- `QWebSocketServer` mit `ws://127.0.0.1` und ein kleiner HTTP-Teil auf `QTcpServer` für `/oauth2/*`, `/helix/eventsub/subscriptions` und `/helix/streams`.
- Beides wird vom Test gesteuert und kommt ohne Netz, ohne Konto und ohne TLS aus. P14 erweitert ihn um die schreibenden Aufrufe.
- Die Twitch CLI wird nicht genommen: fremdes Programm ausserhalb von Debian, [BETA].

**Prüfung je Sitzung (Stufe «Klein»):** `cmake --build build -j8` und `ctest --test-dir build`; Lotse prüft nach jeder Sitzung Bau und `ctest`.

**Prüfstufe «Baustein»** am Ende (Ordner `build/pruefung-p13/`):
- `cmake --build build -j8`
- `ctest --test-dir build` mit aktiver X11-Sitzung, 0 übersprungen
- frischer Klon (Bau und `ctest`)
- `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p13/start/startpruefung`
- `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p13/dienst`
- **50 Wiederholungen**, weil Zeitgrenzen und Threads betroffen sind: `tst_ereignisfluss`, `tst_twitchanmeldung`, `tst_twitchverbindung`, `tst_kerntwitch`, `tst_kernverbindung`. Ohne Last daneben, nie zwei Fenstertests gleichzeitig.
- Last: Der Testserver schickt 60 s lang 50 Chat-Nachrichten pro Sekunde an den Kern, ein Fenster hat bestellt.
  - Ziel: alle Ereignisse in Reihenfolge beim Fenster;
  - Antwortzeit auf `get_overlay_list` höchstens 25 ms (wie `tst_uebernehmen`);
  - CPU des Kerns mit Messwert im Bericht.

### Handtest (Regel 10)

**Prüft Lotse** (mit Kern und Fenster aus `build/`, eigenem `XDG_CONFIG_HOME` für `dss-app`, kein Zugriff auf Twitch-Konten):

**L1 Bildschirmfotos.**
- Was: Bereich «Twitch» in den Zuständen «abgemeldet», «Code angezeigt» (Kanal und Bot), «angemeldet» und «Rechte fehlen», dazu die Liste «Ereignisse». Ablage nur im Scratchpad.
- Warum: Aussehen.

**L2 Ereignisse aus dem echten Chat.**
- Was: Lotse liest `dss-cli ereignisse --folgen` mit, während der Nutzer in N3 schreibt. Erwartet:
  - `twitch.chat_message` mit dem Text des Nutzers innerhalb von 2 s, `role` 4;
  - `twitch.first_message` genau einmal je Konto, das zum ersten Mal schreibt;
  - eine Nachricht des Bot-Kontos (vom Nutzer im Browser geschrieben) mit `is_bot_account: true`;
  - `obs.scene_changed` beim Wechsel auf «test 2» und zurück auf «Szene» (Rechte aus «Umgebung», nie während eines Streams).
- Warum: echter Weg über TLS zu Twitch, den `ctest` nicht hat.

**L3 Dauerlauf, nachts, mindestens 5 h.**
- Was: Kern und Fenster laufen, Lotse liest nur das Protokoll. Erwartet:
  - mindestens eine echte Erneuerung beider Token nach etwa 4 h;
  - stündliche Prüfung beider Konten;
  - Verbindung bleibt, Reconnects mit Grund im Protokoll;
  - kein Token im Protokoll.
- Warum: Klärt den Widerspruch in der Doku zum Erneuern beim Gerätefluss.

**L4 Neustart.**
- **Eigener Schritt:** `systemctl --user restart dss-core-entwicklung.service`.
- Was: Erwartet wird, dass beide Anmeldungen bleiben, keine neue Anmeldung nötig ist und die Abos innerhalb von 10 s wieder aktiv sind.
- Warum: Ablage mit `systemd-creds` im echten Dienst.

**Prüft der Nutzer:**

**N1 App registrieren.**
- Was: `scripts/twitch-einrichten.sh`.
- Warum: Konto und Zwei-Faktor-Anmeldung gehören dem Nutzer.

**N2 Anmelden, beide Konten.**
- Was:
  - Twitch-Knopf, bei «Kanal» «Anmelden …», Code im Browser bestätigen, Rechte ansehen.
  - Bei «Bot» «Anmelden …», Code in einem privaten Browserfenster mit dem Bot-Konto bestätigen.
  - Im eigenen Chat `/mod <Botname>` eingeben.
- Warum: Nur der Nutzer kann sich anmelden.

**N3 Ereignisse auslösen.**
- Was:
  - eine Nachricht im eigenen Chat schreiben, dann eine mit dem Bot-Konto im Browser;
  - Follow, Sub, Raid und Cheer im nächsten Stream bei Gelegenheit. Lotse liest danach das Protokoll.
- Warum: echte Ereignisse lassen sich nur über Konten auslösen.

**N4 Abmelden und Freigabe entziehen.**
- **Eigener Schritt:** «Abmelden» beim Bot, danach wieder anmelden.
- **Eigener Schritt:** Auf twitch.tv mit dem Kanal-Konto unter «Einstellungen → Verbindungen» die App trennen. Erwartet: Knopf rot und «Anmeldung abgelaufen …», spätestens bei der nächsten stündlichen Prüfung. Danach wieder anmelden.
- Warum: Bedienung und Konto.

Was Lotse oder der Agent schon geprüft haben, wiederholt der Nutzer nicht.

## 5. Pakete (Regel 5)

- **Kein neues Paket.** Benutzt werden:
  - Qt Network 6.8.2+dfsg-9+deb13u2 mit `libqopensslbackend.so`;
  - `libssl3t64` 3.5.7-1~deb13u3;
  - `ca-certificates` 20250419+deb13u1;
  - `qt6-websockets-dev` und `libqt6websockets6` 6.8.2-3, schon in `scripts/pakete.txt`.
- **Nicht genommen:**
  - `qt6-networkauth-dev` 6.8.2-4: Der Gerätefluss ist in 6.8 nicht enthalten.
  - `libsecret` bzw. Schlüsselbund: F1 gilt.
  - Twitch CLI: nicht in Debian.

## 6. Schätzung

| Teil | Agent | Lotse | Nutzer |
|---|---|---|---|
| Bau | 3 Sitzungen à ~4–5 h (Commits 1–3, 4–6, 7–10), dazu ~½ Sitzung für das zweite Konto, die erste Nachricht und den Stream-Zustand und ~½ Sitzung für das Abzeichen; 50er-Läufe nachts ~2 h | nach jeder Sitzung ~15 min (Bau, `ctest`) | – |
| Handtest | – | ~1 h (L1, L2, L4), dazu L3 über Nacht (nur lesen) | ~30 min (N1 ~10 min, N2 bis N4 ~20 min), dazu N3 im nächsten Stream |

## 7. Fragen an den Nutzer (gebündelt)

1. **Welche Rechte holt die Anmeldung des Kanals?** (Konto, sichtbar beim Bestätigen)
   - **A) Empfohlen:** alles aus 2.4 lesen und schreiben, **ohne** Streamschlüssel, E-Mail, Konto ändern, Videos löschen, Moderatoren und Erweiterungen verwalten. Grund: Eine Zustimmung reicht für P14 und alles Spätere; die weggelassenen Rechte braucht kein geplanter Baustein und sie wären bei einem gestohlenen Token besonders schädlich.
   - B) Wirklich alles, was Twitch anbietet. Gegenargument zu A: Wer später eines der weggelassenen Rechte braucht, muss sich einmal neu anmelden (1 min).
   - C) Nur, was P14 braucht. Gegenargument: Jeder spätere Baustein (Kategorie, Umfrage, Moderation) verlangt dann eine neue Anmeldung.
2. ~~Abzeichen «Chat Bot»?~~ **Entschieden 2026-11-15: B, mit Abzeichen schon in P13** (vertrauliche App, Secret im Studio, App-Token, +½ Sitzung). Bot-Konto: das bisherige aus der Zeit mit Streamer.bot.
3. **Go für den angepassten Plan P13.**
   - **A) Go wie geplant (empfohlen):** Der Agent baut die Commits 1–10. Die Registrierung der App mit dem Skript ist erst vor dem Handtest nötig.
   - B) Änderungen am Plan, bitte nennen.

## 8. Risiken und `[OFFEN]`

| Risiko oder offene Frage | Gegenmassnahme oder Stand |
|---|---|
| Doku widersprüchlich zum Erneuern beim Gerätefluss; 30 Tage «after generated» oder «inactive» | L3 (echte Erneuerung). Scheitert sie: Anmeldung ohne Code (mit Secret und Port auf `localhost`), dann entscheidet der Nutzer |
| Absturz zwischen der Antwort beim Erneuern und dem Schreiben der Datei: altes Refresh-Token ungültig | sofort schreiben, Meldung «neu anmelden». Selten, dann eine Minute Aufwand |
| Felder von Sub, Gift, Raid, Cheer und Stream nur aus einer Zusammenfassung, nicht wörtlich | Schritt 0 |
| Argumentnamen von Streamer.bot nur teilweise belegt (Doku eingeklappt) | Schritt 0; fehlende als `[OFFEN]` mit eigenem Namen. Der Import (eigener Baustein) gleicht ab |
| Für `channel.raid` laut Zusammenfassung kein Recht nötig; Kanal als eigener Moderator bei `channel.follow` | Schritt 0, `[OFFEN]` bis L2 bzw. zum ersten echten Raid |
| Feld «Client Type» und die Kategorien bei der Registrierung nicht dokumentiert | Skript sagt «falls vorhanden». N1 zeigt es |
| Bot im Browser versehentlich mit dem Kanal-Konto bestätigt | Prüfung «Bot ≠ Kanal» (2.1), Hinweis auf privates Fenster |
| Abzeichen: Gerätefluss mit vertraulicher App und Client-Credentials gegen aktuelle Doku bestätigen | Schritt 0; Rückfall: Bot sendet mit User-Token ohne Abzeichen, Meldung im Bereich «Twitch» |
| Kein Nachliefern verlorener Ereignisse (Twitch) | Reconnect früh erkennen (Keepalive 10 s). Ein Verlust steht im Protokoll |
| Der Kern läuft nur mit dem Fenster. Ohne Studio keine Ereignisse und kein Bot | vom Nutzer so entschieden (2026-11-15) |
| «Letzter Follower» (TX-16) über einen Neustart hinweg braucht einen gespeicherten Verlauf | nicht in P13, Verlauf nur im Speicher |
| IRC-Hinweis (`irc-migration`) nicht gelesen | Schritt 0 |

## Annahmen und Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| EventSub statt IRC | von Twitch empfohlen; derselbe Weg für Chat und andere Ereignisse |
| Gerätefluss selbst gebaut, ohne Qt NetworkAuth | in 6.8 nicht vorhanden; zwei POST-Aufrufe |
| Eigene Bibliothek `dss_events`, nur Qt Core | Regel 9 und 14; jedes Modul bindet sie ohne Kern und ohne `protocol.h` ein |
| Zustellung immer verzögert über das Kontextobjekt | keine Rekursion, Reihenfolge fest, Threads (P12) ohne Sonderweg |
| Fluss nur für Ereignisse, Handlungen über die Schnittstelle des Dienstes | keine Anfragen mit Antwort über den Fluss; einfacher zu testen |
| Typenliste mit Pflichtfeldern und Argumentnamen im Code | Tippfehler fallen sofort auf; `docs/08`, Code und Aktionssystem bleiben deckungsgleich |
| Eigene Feldnamen im Fluss, Argumentnamen wie Streamer.bot erst im Aktionssystem | Module hängen nicht vom Format von Twitch ab; Platz für YouTube; Import ohne Umschrift |
| «Erste Nachricht» im Kern abgeleitet, Liste ohne Namen gespeichert | Twitch liefert nur «erste Nachricht überhaupt»; nach einem Neustart sonst doppelte Begrüssung |
| Geschenk einzeln (`gift_sub`) statt `subscribe` mit `is_gift` | Bündel lösen sonst je Beschenktem einen Sub-Alert aus |
| Rollen und `is_bot_account` schon in P13 | Rechte der Befehle und «Bot-Konto übergehen» brauchen sie in P14 |
| `dss_obs` unverändert, Adapter im Kern | getesteter Code von P10c bleibt; nur ein `connect` mehr |
| Ereignisse zum Fenster nur auf Bestellung, mit Gegendruck 1 MiB | alte Fenster unberührt; ein hängendes Fenster bremst den Kern nicht |
| Verlauf 200 Ereignisse, nur im Speicher | genug für Anzeige und Prüfung; Speichern erst bei Bedarf (TX-16) |
| `SecretStore` aus `ObsSecretStore` herausgelöst | ein Weg für alle Geheimnisse (F1), keine Kopie des Codes |
| Access- und Refresh-Token zusammen in einer Datei je Konto | beim Start kein Erneuern nötig; schont das Refresh-Token, das nur einmal gilt |
| Client-ID in `kern.ini`, eine für beide Konten | laut Twitch kein Geheimnis beim Public Client; eine App reicht |
| Keepalive 10 s (+5 s Spielraum) | Verlust früh erkannt; Last vernachlässigbar |
| Wartezeit 2 bis 60 s mit Zufall | kein Ansturm nach Störungen |
| Doppelte `message_id` übergehen (500) | Doku schweigt; billig |
| Chat-Texte nicht ins Protokoll, nur Anzahl und Fehler | Protokoll klein; Nachrichten der Zuschauer bleiben aus dem Journal |
| Testserver selbst gebaut statt Twitch CLI | kein fremdes Programm (Regel 5); steuerbar wie `obstestserver.h` |
| `--twitch-testserver` nur mit `127.0.0.1` | eine Testoption darf nie ein Token ins Netz schicken (Regel 12) |
| Neuer Schalter `DSS_CORE_TWITCH` | riskanter Teil: externe Verbindung mit TLS und Token |
| Abos nur für Ereignisse, die P14 nutzt; Rechte schon für alles | weniger Verkehr und Tests; keine neue Anmeldung bei späteren Bausteinen |

## Quellen

- **Lokal gelesen (2026-11-09, nachgeführt 2026-11-15):**
  - `/var/lib/dpkg/status` (Versionen in Abschnitt 5, dazu `systemd` 257.13-1~deb13u1)
  - `/usr/lib/x86_64-linux-gnu/qt6/plugins/tls/`
  - Repo: `src/obs/obsconnection.h`, `src/obs/obssecretstore.h`, `src/obs/obsprotocol.h`, `src/obs/CMakeLists.txt`, `src/core/coreservice.h`, `src/core/coreservice.cpp`, `src/core/CMakeLists.txt`, `src/core/main.cpp`, `src/common/connectionstate.h`, `src/common/protocol.h` (Version 13), `src/app/mainwindow.cpp`, `src/app/headerbar.cpp`, `src/app/settingsdialog.h`, `src/cli/main.cpp`, `scripts/pakete.txt`, `CMakeLists.txt`, `docs/01`, `03`, `04`, `05`, `07`, `08`, `docs/archiv/prompts/P12-ton.md`
  - Recherchen von Lotse (2026-11-09, nicht im Repo): «Chat-Bot als Aktionssystem», «So nutzt der Nutzer Streamer.bot»; Entscheide des Nutzers vom 2026-11-15
- **Web gelesen (2026-11-09, teils durch die Recherche):**
  - https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/
  - https://dev.twitch.tv/docs/authentication/refresh-tokens/
  - https://dev.twitch.tv/docs/authentication/validate-tokens/
  - https://dev.twitch.tv/docs/authentication/revoke-tokens/
  - https://dev.twitch.tv/docs/authentication/register-app/
  - https://dev.twitch.tv/docs/authentication/scopes/
  - https://dev.twitch.tv/docs/eventsub/handling-websocket-events/
  - https://dev.twitch.tv/docs/eventsub/manage-subscriptions/
  - https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/ (nur teilweise wörtlich)
  - https://dev.twitch.tv/docs/eventsub/eventsub-reference/ (nur teilweise wörtlich)
  - https://dev.twitch.tv/docs/chat/
  - https://dev.twitch.tv/docs/chat/authenticating/
  - https://dev.twitch.tv/docs/chat/send-receive-messages/
  - https://dev.twitch.tv/docs/api/reference/ (Send Chat Message nur zusammengefasst, Create EventSub Subscription nicht lesbar)
  - https://dev.twitch.tv/docs/cli/websocket-event-command/
  - https://docs.streamer.bot/guide/platforms/twitch
  - https://docs.streamer.bot/api/triggers
  - https://docs.streamer.bot/api/triggers/core/commands/command-triggered
  - https://doc.qt.io/qt-6/qoauth2deviceauthorizationflow.html
  - https://packages.debian.org/trixie/qt6-networkauth-dev
  - https://www.freedesktop.org/software/systemd/man/latest/systemd-creds.html (Muster F1, wie P10c)
- **Nicht gelesen (in Schritt 0):**
  - https://dev.twitch.tv/docs/chat/irc-migration/
  - https://dev.twitch.tv/docs/api/reference/#get-streams
  - https://dev.twitch.tv/docs/cli/configure-command/
  - Seiten der Twitch-Auslöser unter https://docs.streamer.bot/api/triggers/twitch/ (Argumente)
  - https://doc.qt.io/qt-6.8/qsslsocket.html
  - https://doc.qt.io/qt-6.8/qnetworkaccessmanager.html
- **Am 2026-11-15 nicht erneut im Web geprüft:** Dieser Plan stützt die Aussagen zu Rechten, Abzeichen und Grenzen auf die Lesungen vom 2026-11-09. Schritt 0 bestätigt sie gegen die aktuelle Doku (Regel 4).
