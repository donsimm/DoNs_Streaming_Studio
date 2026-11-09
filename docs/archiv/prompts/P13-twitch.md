# P13 – Twitch-Anbindung mit Ereignisfluss (Plan)

Plan des Projektleiter-Agenten vom 2026-11-09. Grundlage:
- Bauplan P13 und Punkt 92 (`docs/04-offene-punkte.md`)
- Regeln 4, 5, 8, 9, 12 und 14 (`CLAUDE.md`)
- `docs/01-festlegungen.md` («Module», F1)
- `docs/08-kern-schnittstelle.md` (Version 11)
- Bericht `~/.claude/lotse/berichte/archiv/dss-bauplan-pruefung.md` (Befunde 2, 3, 7)

Ausführung in drei Sitzungen des Agenten, danach Handtest. P14 (Basisfunktion Chat-Bot) baut auf P13 auf.

## Entscheide des Nutzers (bisher)

- Module sind fest eingebaut. Jedes Modul nutzt dieselbe Schnittstelle zum Ereignisfluss, es gibt keine Plugins (Regel 14, `01-festlegungen.md:22`).
- Zugangsdaten stehen nie im Klartext (Regel 12). Das Muster ist F1 (P10c): `systemd-creds --user`, ohne Nachfrage.
- Die Verbindungsknöpfe im Kopfbereich zeigen Twitch, YouTube, OBS und NDI mit vier Farben (`01-festlegungen.md:120`). Was mit YouTube geschieht, ist offen und wird beim Chat-Bot geklärt. YouTube gehört nicht zu P13.
- Die Reihenfolge ist P12 Ton, P13 Twitch, P14 Chat-Bot (2026-11-09).

## Wichtigste Befunde

| Befund | Beleg |
|---|---|
| Einen Ereignisfluss gibt es nicht. OBS gibt nur die Qt-Signale `stateChanged` und `currentSceneChanged` nach aussen, verbunden an einer Stelle | `src/obs/obsconnection.h:103-108`, `src/core/coreservice.cpp:495-498` |
| `dss_obs` bindet absichtlich weder `dss_common` noch `protocol.h` ein. Das Passwort liegt in `ObsSecretStore` (`systemd-creds` über `QProcess`, stdin und stdout, `QSaveFile` mit 0600) | `src/obs/CMakeLists.txt:1-3`, `src/obs/obssecretstore.h:13-31` |
| Twitch ist im Code nur als Knopf vorhanden (`connections.twitch` immer `off`). Der Reiter «Chat-Bot» zeigt «Noch nicht vorhanden» | `src/common/connectionstate.h:11`, `src/app/mainwindow.cpp:186` |
| TLS ist neu, denn OBS läuft über `ws://`. Vorhanden sind `libqopensslbackend.so`, `libssl3t64` 3.5.7-1~deb13u3 und `ca-certificates` 20250419+deb13u1, dazu Qt Network 6.8.2+dfsg-9+deb13u2 und `libqt6websockets6` 6.8.2-3 | `/usr/lib/x86_64-linux-gnu/qt6/plugins/tls/`, `/var/lib/dpkg/status` |
| Qt NetworkAuth ist nicht installiert. `QOAuth2DeviceAuthorizationFlow` gibt es erst seit Qt 6.9, Debian 13 hat `qt6-networkauth-dev` 6.8.2-4. Der Gerätefluss besteht aus zwei POST-Aufrufen und wird mit `QNetworkAccessManager` selbst gebaut | https://doc.qt.io/qt-6/qoauth2deviceauthorizationflow.html, https://packages.debian.org/trixie/qt6-networkauth-dev |
| Gerätefluss (DCF): `POST https://id.twitch.tv/oauth2/device` liefert `device_code`, `user_code`, `verification_uri`, `expires_in` (Beispiel 1800) und `interval` (Beispiel 5). Danach wird `POST …/oauth2/token` mit `grant_type=urn:ietf:params:oauth:grant-type:device_code` abgefragt. Solange der Nutzer nicht bestätigt hat, kommt 400 `authorization_pending`. Ein Public Client braucht **kein Client Secret** | https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/ |
| Ein Access-Token gilt 4 h. Refresh-Tokens aus dem DCF sind «for one time use only». Beim Public Client verfallen sie nach 30 Tagen, wobei eine Seite «after they are generated» schreibt und die andere «inactive». Die Refresh-Seite sagt zudem, nur Tokens aus dem Autorisierungscode liessen sich ohne Zustimmung erneuern. **Das widerspricht der DCF-Seite** | dieselbe Seite und https://dev.twitch.tv/docs/authentication/refresh-tokens/ |
| Pflicht: «validate the OAuth token when it starts and on an hourly basis thereafter», ausdrücklich auch für Chat-Bots (`GET https://id.twitch.tv/oauth2/validate`) | https://dev.twitch.tv/docs/authentication/validate-tokens/ |
| Für das Registrieren der App muss am Konto die Zwei-Faktor-Anmeldung aktiv sein. Die Felder sind Name (eindeutig), OAuth Redirect URLs und Category. Das Feld «Client Type» nennt die Seite nicht | https://dev.twitch.tv/docs/authentication/register-app/ |
| Für den Chat empfiehlt Twitch EventSub und die API: «The preferred method of viewing and sending chats on Twitch is through EventSub and Twitch API» | https://dev.twitch.tv/docs/chat/ |
| EventSub über WebSocket (`wss://eventsub.wss.twitch.tv/ws`): Nach `session_welcome` bleiben 10 s für das erste Abo, sonst Close 4003. `keepalive_timeout_seconds` liegt zwischen 10 und 600. `session_reconnect` kommt 30 s vorher mit `reconnect_url`, die alte Verbindung wird erst nach dem Welcome auf der neuen geschlossen, die Abos bleiben. Verlorene Ereignisse werden nicht nachgeliefert. Close-Codes 4000 bis 4007 | https://dev.twitch.tv/docs/eventsub/handling-websocket-events/ |
| Ein Abo über WebSocket geht nur mit einem User-Token (`POST https://api.twitch.tv/helix/eventsub/subscriptions`). Je Token höchstens 3 Verbindungen und 300 Abos je Verbindung. Hat der Nutzer die App freigegeben, kostet ein Abo nichts. `max_total_cost` steht im Text als 10, in den Beispielen als 10000 | https://dev.twitch.tv/docs/eventsub/manage-subscriptions/ |
| Twitch CLI hat einen eigenen Testserver (`twitch event websocket start-server`, [BETA]). Er ist ein Go-Programm und nicht in Debian | https://dev.twitch.tv/docs/cli/websocket-event-command/ |

## 1. Ereignisfluss im Kern (Regel 14, Punkt 92)

### 1.1 Aufbau

Neue Bibliothek `src/events/` (`dss_events`) mit Qt Core, ohne Qt Gui und ohne `protocol.h`. Sie ist klein und wird von allen Modulen genutzt.

```cpp
namespace dss::events {
struct Event {
    QUuid id;          // vom Kern vergeben
    QString type;      // «twitch.chat_message», «obs.scene_changed», …
    QString origin;    // «twitch», «obs», später «chatbot», …
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
  - Handlungen gehen über die Schnittstelle des Dienstes, zum Beispiel später in P14 `TwitchChat::send(...)`. Das Ergebnis kommt wieder als Ereignis.
- **Typenliste:**
  - `src/events/eventtypes.h` führt jeden Typ mit seinen Pflichtfeldern.
  - `publish` prüft gegen diese Liste. So bleiben Code und `docs/08` deckungsgleich.

### 1.2 Wer veröffentlicht, wer hört

| Modul | Rolle in P13 | Später |
|---|---|---|
| Twitch (`dss_twitch`, neu) | veröffentlicht `twitch.*` direkt auf dem Fluss | P14: Chat senden über `TwitchChat` |
| OBS (`dss_obs`) | bleibt unverändert. Ein kleiner Adapter im Kern (`src/core/obsevents.{h,cpp}`) veröffentlicht `obs.scene_changed` aus `currentSceneChanged`. Damit ist das Signal aus Punkt 92 auf dem Fluss | E-18 hört auf `obs.scene_changed` |
| Fenster (Editor) | hört über den Socket (1.4) | P14: Reiter «Chat-Bot» |
| Browserquelle (`web/`) | in P13 nichts | E-17 und TX-16: Der Overlay-Server abonniert für ein Overlay nur die Typen, die seine Quellen brauchen, und schickt sie als SSE `event: dss_event` an die Seite. Nur öffentliche Felder, nie Kennungen des Kerns oder Token |
| Ton (P12) | in P13 nichts. P12 koppelt den Ton an die Meldungen der Seite und an das Overlay-Modell, das bleibt Sache des Kerns. Ein Test in `tst_ereignisfluss` stellt sicher, dass ein Abonnent in einem anderen Thread geht | Töne bei Ereignissen über eigene Ausgänge (Wunsch des Nutzers vom 2026-11-09): Der Mischer abonniert mit seinem Thread als Kontext |
| Chat-Bot (P14) | – | abonniert `twitch.chat_message` und veröffentlicht eigene Typen (`chatbot.*`) |

### 1.3 Ereignistypen in P13 (Umfang nach Frage 2, Empfehlung A)

Für jeden Typ gelten die Grundfelder aus 1.1. In der Tabelle steht nur `data`. Die Werte von `*_name` sind Anzeigenamen, die von `*_login` Kleinbuchstaben.

| Typ | Twitch-Abo (Version) | `data` |
|---|---|---|
| `twitch.chat_message` | `channel.chat.message` (1) | `message_id`, `user_id`, `user_login`, `user_name`, `text`, `fragments[]` (`type`, `text`, `emote_id` oder `null`), `color`, `badges[]` (`set_id`, `id`), `reply_to` (message_id oder `null`), `reward_id` (oder `null`) |
| `twitch.follow` | `channel.follow` (2) | `user_id`, `user_login`, `user_name`, `followed_at` |
| `twitch.subscribe` | `channel.subscribe` (1) | `user_*`, `tier` (`1000`, `2000`, `3000`), `is_gift` |
| `twitch.resubscribe` | `channel.subscription.message` (1) | `user_*`, `tier`, `text`, `months`, `streak_months` (oder `null`), `duration_months` |
| `twitch.gift` | `channel.subscription.gift` (1) | `user_*` (`null` bei anonym), `is_anonymous`, `total`, `tier`, `cumulative_total` (oder `null`) |
| `twitch.raid` | `channel.raid` (1), Bedingung `to_broadcaster_user_id` | `from_user_id`, `from_user_login`, `from_user_name`, `viewers` |
| `twitch.cheer` | `channel.cheer` (1) | `user_*` (`null` bei anonym), `is_anonymous`, `bits`, `text` |
| `twitch.reward_redeemed` | `channel.channel_points_custom_reward_redemption.add` (1) | `redemption_id`, `user_*`, `user_input`, `reward_id`, `reward_title`, `reward_cost`, `status` |
| `twitch.stream_online` | `stream.online` (1) | `started_at`, `kind` |
| `twitch.stream_offline` | `stream.offline` (1) | – |
| `obs.scene_changed` | – (OBS) | `scene_uuid`, `scene_name` |

`channel.chat.notification` gehört nicht zu P13: Subs und Raids kommen schon über die eigenen Abos. Ob P14 die Hinweise im Chat braucht, entscheidet P14.

### 1.4 Ereignisse über den Socket zum Fenster (Antwort auf Punkt 92: ja, nur auf Bestellung)

Die Version der Schnittstelle steigt um 1: auf 13, oder auf 12, falls P12b noch nicht auf `main` ist.

- **`subscribe_events`** (Fenster → Kern): `types` (Liste von Filtern, leer = abbestellen) und `history` (true/false).
  - Antwort: `event_history` mit `events[]` (bei `history: true`, höchstens 200), danach `event` mit `event` für jedes neue Ereignis, **nur an diese Verbindung**.
  - Ohne Bestellung kommt nichts. Ein altes Fenster bestellt nicht und bekommt also nichts.
- **Gegendruck:** Liegen bei einer Verbindung mehr als 1 MiB ungesendet (`bytesToWrite`), verwirft der Kern weitere Ereignisse für sie und schickt danach einmal `events_dropped` mit `count`. So bremst ein hängendes Fenster den Kern nicht.
- **`dss-cli ereignisse [--folgen] [--typ FILTER]`:** zeigt den Verlauf und mit `--folgen` neue Ereignisse, eine JSON-Zeile je Ereignis. Werkzeug für Lotse im Handtest, ohne Bildschirmfoto.

## 2. Twitch

### 2.1 Anmeldung (Frage 1, Empfehlung: Gerätecode)

- **Ablauf:**
  - Der Kern startet den DCF.
  - Das Fenster zeigt `user_code`, die Adresse `verification_uri` von Twitch, «Im Browser öffnen» (`QDesktopServices`), «Code kopieren» und die Restzeit.
  - Der Kern fragt im Abstand `interval` ab: 400 `authorization_pending` heisst weiter warten. Ist `expires_in` abgelaufen oder kommt «invalid device code», heisst die Meldung «Code abgelaufen, neu anmelden».
- **Kein Client Secret, kein offener Port, keine Rückrufadresse.** Bei der Registrierung ist die Rückrufadresse Pflicht, dort steht `http://localhost`. Sie wird beim Gerätecode nicht benutzt.
- **Der `device_code` verlässt den Kern nie.** `user_code` geht nur an das Fenster, nicht ins Protokoll.
- **Konto:** das Kanal-Konto des Nutzers. Ein eigenes Bot-Konto mit «Chat Bot»-Abzeichen braucht laut Doku ein App-Token, also ein Client Secret. Das klärt P14 (`[OFFEN]`).

### 2.2 Token sicher ablegen (Regel 12)

- **Verallgemeinern:** `ObsSecretStore` wird zu `dss::SecretStore` in der neuen Bibliothek `src/secrets/` (`dss_secrets`).
  - Das Verhalten bleibt gleich. `obs-passwort.cred` behält Name und Inhalt.
  - `dss_obs` und `dss_twitch` nutzen beide diese Bibliothek. `dss_obs` bindet weiter kein `dss_common` ein.
- **Datei:** `twitch-token.cred` neben `kern.ini`, Rechte 0600, je Version getrennt.
  - `--name=dss-twitch-token`.
  - Inhalt als JSON: `access_token`, `refresh_token`, `expires_at`, `user_id`, `login`, `scopes`.
- **Client-ID:** Sie ist kein Geheimnis und steht in `kern.ini` unter `[twitch] client-id`. Dazu kommt `[twitch] aktiv=ja|nein`, Standard `nein`.
- **Protokoll und Nachrichten:** Token, Refresh-Token und `device_code` stehen nie in Protokoll, Nachrichten oder Tests im Klartext. `tst_kerntwitch` sucht die Testtoken im Protokoll und im Socket-Verkehr.
- **Grenze wie F1:** Andere Programme desselben Benutzers könnten die Datei entschlüsseln.

### 2.3 Erneuerung und Prüfung

- **Erneuern:**
  - Von selbst 10 min vor `expires_at`, ausserdem nach jedem 401 einmal, danach wird die Anfrage wiederholt.
  - Das neue Refresh-Token wird **sofort** geschrieben, denn das alte gilt nach einmaligem Gebrauch nicht mehr.
- **Prüfen:** `validate` beim Start und danach alle 60 min (Pflicht laut Twitch). Dabei werden die Rechte mit den nötigen verglichen.
- **Fehlschläge:**
  - Refresh abgewiesen (400 bzw. 401) oder `validate` mit 401: Knopf rot, Meldung «Anmeldung bei Twitch abgelaufen. Im Dialog «Einstellungen» neu anmelden.» Danach kein weiterer Versuch, bis der Nutzer handelt (wie F9 und 4009 bei OBS).
  - Netzfehler: neuer Versuch nach 2 s, danach jeweils doppelt so lang bis 60 s, ±20 % Zufall.
- **«Abmelden»:** `POST …/oauth2/revoke`, `twitch-token.cred` löschen, Verbindung trennen.

### 2.4 Rechte (Scopes), nur lesend

| Ereignis | Recht |
|---|---|
| Chat-Nachricht | `user:read:chat` |
| Follow | `moderator:read:followers` (der Kanal gilt als eigener Moderator, Bedingung `moderator_user_id` = eigene Kennung) |
| Sub, Resub, Geschenk | `channel:read:subscriptions` |
| Cheer | `bits:read` |
| Kanalpunkte | `channel:read:redemptions` |
| Raid, Stream online und offline | keines |

P14 braucht zusätzlich `user:write:chat`. Fehlt ein Recht, zeigt der Kern «Rechte fehlen: … Neu anmelden.». Die Neuanmeldung verlangt die nötigen Rechte neu, die vorhandenen Ereignisse laufen bis dahin weiter.

### 2.5 EventSub über WebSocket

- **Verbinden:**
  - `wss://eventsub.wss.twitch.tv/ws?keepalive_timeout_seconds=10`.
  - Nach `session_welcome` gehen alle Abos aus 1.3 parallel als POST mit der `session_id` hinaus, innerhalb von 10 s.
  - Ein abgewiesenes Abo (403 Recht fehlt, 409, 429) hält die anderen nicht auf.
- **Keine Nachricht innerhalb von Keepalive + 5 s:** Die Verbindung gilt als verloren. Neu verbinden mit Wartezeit wie 2.3, die Abos neu anlegen.
- **`session_reconnect`:** neue Verbindung zu `reconnect_url` (unverändert benutzt), die alte erst nach deren `session_welcome` schliessen. Keine neuen Abos.
- **`revocation`:** Das Abo gilt als weg, die Meldung nennt `status`. Bei `authorization_revoked` wird die Anmeldung als ungültig behandelt.
- **Close-Codes:**
  - 4003 und 4007 sind Fehler im eigenen Ablauf: Protokoll als kritisch, neu verbinden.
  - 4000, 4005 und 4006: neu verbinden.
  - 4001, 4002 und 4004: Protokoll und neu verbinden.
- **Doppelte Nachrichten:** Die letzten 500 `message_id` werden gemerkt, eine doppelte wird übergangen. Die Doku sagt dazu nichts, der Aufwand ist klein.
- **Verbindungen und Kosten:** genau 1 Verbindung (Grenze 3), etwa 10 Abos (Grenze 300), Kosten 0.
- **TLS:** Prüfung über die Zertifikate des Systems, kein Abschalten der Prüfung. `QSslSocket::supportsSsl()` wird beim Start geprüft, sonst gilt die Meldung «TLS fehlt».

### 2.6 Bibliothek `src/twitch/` (`dss_twitch`)

Qt Core, Network und WebSockets, kein Qt Gui und kein `protocol.h`, bindet `dss_events` und `dss_secrets` ein.

| Datei | Inhalt |
|---|---|
| `eventsubprotocol.{h,cpp}` | Nachrichten lesen und bauen, Ereignisse in `data` nach 1.3 umsetzen, reine Funktionen |
| `twitchauth.{h,cpp}` | DCF, Erneuerung, Prüfung, Abmelden |
| `helixclient.{h,cpp}` | Aufrufe mit `Client-Id` und Bearer, 401 → einmal erneuern, 429 → `Ratelimit-Reset` beachten |
| `eventsubconnection.{h,cpp}` | WebSocket, Keepalive, Reconnect, Abos |
| `twitchservice.{h,cpp}` | hält alles zusammen, `stateChanged`, veröffentlicht auf dem Fluss |

Die Adressen von `id.twitch.tv`, `api.twitch.tv` und `eventsub.wss.twitch.tv` lassen sich für Tests ersetzen:
- über `dss-core --twitch-testserver http://127.0.0.1:PORT`;
- nur `127.0.0.1` ist erlaubt, sonst bricht der Start mit Exit-Code 1 ab;
- so kann die Option kein Token an einen fremden Rechner schicken.

### 2.7 Kern, Schnittstelle, Fenster

- **Kern:**
  - `connections.twitch` zeigt den echten Zustand: `off` (aus, ohne Client-ID oder ohne Anmeldung), `connecting` (erster Versuch), `connected` (alle Abos aktiv), `error` (Anmeldung ungültig, Rechte fehlen, Netz).
  - Neu ist `twitch_status` mit `message`, `login`, `user_id`, `missing_scopes[]`, `subscriptions[]` (`type`, `ok`, `problem`) und `login_pending` (`user_code`, `verification_uri`, `expires_at`) oder `null`. Es geht nach `hello` und bei jeder Änderung an alle.
  - Neue Anfragen: `twitch_login_start`, `twitch_login_cancel` und `twitch_logout` mit `request_id`.
  - `settings` und `update_settings` bekommen zusätzlich `twitch_enabled` und `twitch_client_id`.
  - `dss-cli einstellung-twitch-client-id ID`.
- **Schalter `DSS_CORE_TWITCH`** (Standard ON): OFF heisst Kern ohne Twitch, der Knopf bleibt grau. Begründung: neue externe Verbindung mit TLS und Token, ein riskanter Teil nach Regel 8.
- **Fenster** (Frage 3):
  - Bereich «Twitch» im Dialog «Einstellungen»: «Mit Twitch verbinden», «Client-ID», «Konto: angemeldet als …», «Anmelden …» bzw. «Abmelden», während der Anmeldung den Code gross mit «Im Browser öffnen», «Code kopieren», Restzeit und «Abbrechen», Liste fehlender Rechte und Abos.
  - Ein Klick auf den Twitch-Knopf öffnet den Bereich (wie F3).
  - Der Reiter «Chat-Bot» zeigt bis P14 die Liste «Ereignisse» mit Zeit, Art, Name und Text, höchstens 200 Einträge.

## 3. Was der Nutzer selbst tun muss

Die Schritte stehen im Skript `scripts/twitch-einrichten.sh`, Aufruf aus jedem Ordner:

```
~/DoNs_Streaming_Studio/scripts/twitch-einrichten.sh
```

1. Das Skript nennt die Voraussetzung: Zwei-Faktor-Anmeldung am Twitch-Konto aktiv (Pflicht von Twitch).
2. Es öffnet `https://dev.twitch.tv/console/apps/create` (`xdg-open`) und zeigt, was einzutragen ist:
   - Name eindeutig, zum Beispiel «DoNs Streaming Studio <Kanalname>»;
   - OAuth Redirect URL `http://localhost`;
   - Category nach der Liste dort (`[OFFEN]`, Schritt 0);
   - Client-Typ «Public», falls das Feld erscheint.
   Ein Secret wird nicht gebraucht.
3. Es fragt nach der **Client-ID**. Das Skript sagt dazu ausdrücklich: «Das ist kein Passwort und kein Token.» Es prüft die Form (nur Kleinbuchstaben und Ziffern, nicht leer) und trägt die ID mit `dss-cli einstellung-twitch-client-id` in die Version in Arbeit ein (`--einsatz` erst bei der Installation). Läuft der Kern nicht, steht dort: «Zuerst das Studio öffnen.»
4. Zum Schluss nennt es den nächsten Schritt: im Studio auf den Twitch-Knopf klicken, «Anmelden …», den Code im Browser bestätigen.

Eine neue Claude-Code-Sitzung ist dafür nicht nötig.

## 4. Commits, Prüfung, Handtest

**Schritt 0 (Agent):**
- Die Quellen unten lesen und die Felder in 1.3, die Rechte in 2.4 und die Kategorie bestätigen.
- Ausserdem `https://dev.twitch.tv/docs/chat/irc-migration/` lesen.
- Weicht etwas ab, das den DCF oder die Abos trägt: Halt und Meldung an Lotse.

**Commits (Regel 8):**
1. `src/secrets/` aus `ObsSecretStore` herauslösen, ohne Änderung des Verhaltens. `tst_obspasswort` besteht unverändert.
2. `src/events/`: Fluss, Typenliste und Verlauf. Test `tst_ereignisfluss` ohne Qt Gui deckt ab:
   - Reihenfolge, Filter und Vorsilbe;
   - Verlauf mit 200 Einträgen;
   - Abmelden während der Zustellung, Kontext zerstört;
   - Abonnent in einem anderen Thread;
   - `publish` im Handler, unbekannter Typ;
   - 10 000 Ereignisse mit Zeitmessung.
3. Kern: Fluss in `CoreService`, Adapter `obs.scene_changed`, `subscribe_events`, `event_history`, `event`, `events_dropped`, `dss-cli ereignisse`, Schnittstelle +1.
   - Tests: `tst_protocol`, `tst_aenderungen`, `tst_kernverbindung` (Szenenwechsel vom Testserver von OBS kommt als `event` beim Fenster an, nicht beim Fenster ohne Bestellung, Gegendruck).
4. `eventsubprotocol` mit Test `tst_twitchprotokoll`.
   - Daten: `tests/daten/twitch/` mit den Beispielen aus der Doku (je Datei die URL in `LIESMICH.md`) und den Fällen «anonym», «ohne Emote», «Antwort».
5. `twitchauth` und `helixclient` mit Test `tst_twitchanmeldung` gegen `tests/twitchtestserver.h`:
   - `authorization_pending`, Ablauf des Codes, Abbrechen;
   - Erneuerung mit neuem Refresh-Token, 401 → Erneuern → Wiederholen, Refresh abgewiesen;
   - stündliche Prüfung mit gekürzter Zeit, fehlende Rechte, Abmelden.
6. `eventsubconnection` mit Test `tst_twitchverbindung`:
   - Abos innerhalb von 10 s, ein Abo abgewiesen;
   - Keepalive verpasst, `session_reconnect` (alte Verbindung erst nach dem Welcome zu);
   - Close-Codes, `revocation`, doppelte `message_id`, Wartezeiten;
   - `QSslSocket::supportsSsl()`.
7. Kern: `TwitchService`, `kern.ini [twitch]`, `twitch-token.cred`, `twitch_status` und die Anfragen, `connections.twitch`, Schalter `DSS_CORE_TWITCH`, Option `--twitch-testserver`.
   - Tests: `tst_kerneinstellungen` (`[twitch]`), neu `tst_kerntwitch` mit dem Kern als eigenem Prozess gegen den Testserver:
     - Anmeldung über den Socket, Ereignisse bis zum Fenster;
     - Neustart behält die Anmeldung, Abmelden löscht die Datei;
     - **kein Testtoken in Protokoll oder Socket**;
     - `--twitch-testserver` mit einer anderen Adresse als `127.0.0.1` wird abgewiesen.
8. Fenster: Bereich «Twitch», Knopf, Liste «Ereignisse» nach Frage 3. Test `tst_fensterbearbeiten`.
9. `scripts/twitch-einrichten.sh`, geprüft mit eigenem `XDG_CONFIG_HOME` und Kern mit eigenem Datenordner.
10. Dokumente:
    - `01` (Twitch, F-Entscheide aus den Fragen), `02` (TLS, Adressen von Twitch), `03`, `04` (Punkt 92 ins Archiv, neue Punkte), `05`, `06` (`twitch-token.cred`), `08` (Version, Abschnitte «Ereignisfluss» und «Anbindung an Twitch»);
    - Bericht `docs/archiv/berichte/P13-bericht.md`.

**Testserver statt Twitch CLI:** `tests/twitchtestserver.h` folgt dem Muster von `tests/obstestserver.h`.
- `QWebSocketServer` mit `ws://127.0.0.1` und ein kleiner HTTP-Teil auf `QTcpServer` für `/oauth2/*` und `/helix/eventsub/subscriptions`.
- Beides wird vom Test gesteuert und kommt ohne Netz, ohne Konto und ohne TLS aus.
- Die Twitch CLI wird nicht genommen: fremdes Programm ausserhalb von Debian, [BETA].

**Prüfstufe «Baustein»** (Ordner `build/pruefung-p13/`):
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
- Was: Bereich «Twitch» in den Zuständen «abgemeldet», «Code angezeigt», «angemeldet» und «Rechte fehlen», dazu die Liste «Ereignisse». Ablage nur im Scratchpad.
- Warum: Aussehen.

**L2 Ereignisse aus dem echten Chat.**
- Was: Lotse liest `dss-cli ereignisse --folgen` mit, während der Nutzer in N3 schreibt. Erwartet: `twitch.chat_message` mit dem Text des Nutzers innerhalb von 2 s. `obs.scene_changed` beim Wechsel auf «test 2» und zurück auf «Szene» (Rechte aus «Umgebung», nie während eines Streams).
- Warum: echter Weg über TLS zu Twitch, den `ctest` nicht hat.

**L3 Dauerlauf, nachts, mindestens 5 h.**
- Was: Kern und Fenster laufen, Lotse liest nur das Protokoll. Erwartet:
  - mindestens eine echte Erneuerung des Tokens nach etwa 4 h;
  - stündliche Prüfung;
  - Verbindung bleibt, Reconnects mit Grund im Protokoll;
  - kein Token im Protokoll.
- Warum: Klärt den Widerspruch in der Doku zum Erneuern beim DCF.

**L4 Neustart.**
- **Eigener Schritt:** `systemctl --user restart dss-core-entwicklung.service`.
- Was: Erwartet wird, dass die Anmeldung bleibt, keine neue Anmeldung nötig ist und die Abos innerhalb von 10 s wieder aktiv sind.
- Warum: Ablage mit `systemd-creds` im echten Dienst.

**Prüft der Nutzer:**

**N1 App registrieren.**
- Was: `scripts/twitch-einrichten.sh`.
- Warum: Konto und Zwei-Faktor-Anmeldung gehören dem Nutzer.

**N2 Anmelden.**
- Was: Twitch-Knopf, «Anmelden …», Code im Browser bestätigen, Rechte ansehen.
- Warum: Nur der Nutzer kann sich anmelden.

**N3 Ereignisse auslösen.**
- Was:
  - eine Nachricht im eigenen Chat schreiben;
  - eine eigene Kanalpunkte-Belohnung einlösen, falls Twitch das dem Kanal erlaubt (nicht geprüft).
  - Follow, Sub, Raid und Cheer im nächsten Stream bei Gelegenheit. Lotse liest danach das Protokoll.
- Warum: echte Ereignisse lassen sich nur über Konten auslösen.

**N4 Abmelden und Freigabe entziehen.**
- **Eigener Schritt:** «Abmelden». Danach wieder anmelden.
- **Eigener Schritt:** Auf twitch.tv unter «Einstellungen → Verbindungen» die App trennen. Erwartet: Knopf rot und «Anmeldung abgelaufen …», spätestens bei der nächsten stündlichen Prüfung.
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
| Bau | 3 Sitzungen à ~4–5 h (Commits 1–3, 4–6, 7–10), 50er-Läufe nachts ~2 h | – | – |
| Handtest | – | ~1 h (L1, L2, L4), dazu L3 über Nacht (nur lesen) | ~20 min (N1 ~10 min, N2 bis N4 ~10 min), dazu N3 im nächsten Stream |

## 7. Fragen an den Nutzer (gebündelt, höchstens 4)

1. **Anmeldung bei Twitch** (bedienbar, Konto).
   - **A) Mit Code (empfohlen):** «Anmelden …», das Studio zeigt einen Code, im Browser bestätigen. Kein zweites Geheimnis und kein offener Port. Wird das Studio 30 Tage lang nicht benutzt, ist eine neue Anmeldung nötig.
   - B) Ohne Code: Der Browser öffnet die Twitch-Seite und kehrt nach «Zulassen» zum Studio zurück. Dafür braucht es ein Client Secret, das auch verschlüsselt gespeichert wird, und einen Port auf `localhost`.
   - Angemeldet wird mit dem Kanal-Konto. Ob der Chat-Bot später mit einem eigenen Bot-Konto schreibt, wird vor P14 geklärt.
2. **Welche Ereignisse zuerst** (Konto, Rechte).
   - **A) Alle aus 1.3 (empfohlen):** Chat-Nachricht, Follow, Sub/Resub/Geschenk, Raid, Cheer, Kanalpunkte, Stream online/offline. Alle Rechte sind nur lesend, die Anmeldung erfolgt einmal, und Chat-Bot und Alerts brauchen sie später.
   - B) Nur Chat-Nachricht. Weitere Ereignisse später, dann mit neuer Anmeldung.
   - Dazu: Ist der Kanal Affiliate oder Partner? Ohne das gibt es keine Subs und keine Cheers.
3. **Anzeige im Fenster** (sichtbar).
   - **A) Empfohlen:** Bereich «Twitch» im Dialog «Einstellungen», der Twitch-Knopf öffnet ihn (wie bei OBS). Der Reiter «Chat-Bot» zeigt bis P14 die Liste «Ereignisse» (Zeit, Art, Name, Text, letzte 200). Knopf grün nur, wenn alle Ereignisse ankommen, sonst rot mit Grund im Tooltip.
   - B) Wie A, aber ohne Liste. Ereignisse nur über `dss-cli`.
   - C) Liste als eigenes Dock «Ereignisse» im Editor.
4. **Go für P13.**
   - **A) Go wie geplant (empfohlen):** Der Agent baut die Commits 1–10. Die Registrierung der App mit dem Skript ist erst vor dem Handtest nötig.
   - B) Erst P12 ganz abschliessen, dann P13.
   - C) Änderungen am Plan, bitte nennen.

## 8. Risiken und `[OFFEN]`

| Risiko oder offene Frage | Gegenmassnahme oder Stand |
|---|---|
| Doku widersprüchlich zum Erneuern beim DCF; 30 Tage «after generated» oder «inactive» | L3 (echte Erneuerung). Scheitert sie: Variante B aus Frage 1, dann entscheidet der Nutzer |
| Absturz zwischen der Antwort beim Erneuern und dem Schreiben der Datei: altes Refresh-Token ungültig | sofort schreiben, Meldung «neu anmelden». Selten, dann eine Minute Aufwand |
| Felder von Sub, Gift, Raid, Cheer und Stream nur aus einer Zusammenfassung, nicht wörtlich | Schritt 0 |
| Für `channel.raid` laut Zusammenfassung kein Recht nötig; Kanal als eigener Moderator bei `channel.follow` | Schritt 0, `[OFFEN]` bis L2 bzw. zum ersten echten Raid |
| Feld «Client Type» und die Kategorien bei der Registrierung nicht dokumentiert | Skript sagt «falls vorhanden». N1 zeigt es |
| `max_total_cost` 10 oder 10000 | ohne Bedeutung, die Kosten sind 0 |
| Affiliate-Status unbekannt | Frage 2 |
| Kein Nachliefern verlorener Ereignisse (Twitch) | Reconnect früh erkennen (Keepalive 10 s). Ein Verlust steht im Protokoll |
| Der Kern läuft nur mit dem Fenster (`08-kern-schnittstelle.md`, «Start»). Ohne Studio gibt es keine Ereignisse | betrifft vor allem P14 (Chat-Bot), `[OFFEN]` für P14 |
| Bot-Abzeichen nur mit App-Token, das ein Client Secret braucht | vor P14 klären |
| «Letzter Follower» (TX-16) über einen Neustart hinweg braucht einen gespeicherten Verlauf | nicht in P13, Verlauf nur im Speicher. Neuer offener Punkt |
| IRC-Hinweis (`irc-migration`) nicht gelesen | Schritt 0 |

## Annahmen und Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| EventSub statt IRC | von Twitch empfohlen; derselbe Weg für Chat und andere Ereignisse |
| Gerätefluss selbst gebaut, ohne Qt NetworkAuth | in 6.8 nicht vorhanden; zwei POST-Aufrufe |
| Eigene Bibliothek `dss_events`, nur Qt Core | Regel 9 und 14; jedes Modul bindet sie ohne Kern und ohne `protocol.h` ein |
| Zustellung immer verzögert über das Kontextobjekt | keine Rekursion, Reihenfolge fest, Threads (P12) ohne Sonderweg |
| Fluss nur für Ereignisse, Handlungen über die Schnittstelle des Dienstes | keine Anfragen mit Antwort über den Fluss; einfacher zu testen |
| Typenliste mit Pflichtfeldern im Code | Tippfehler fallen sofort auf; `docs/08` und der Code bleiben deckungsgleich |
| Eigene Feldnamen statt Rohdaten von Twitch | Module hängen nicht vom Format von Twitch ab; Platz für YouTube später |
| `dss_obs` unverändert, Adapter im Kern | getesteter Code von P10c bleibt; nur ein `connect` mehr |
| Ereignisse zum Fenster nur auf Bestellung, mit Gegendruck 1 MiB | alte Fenster unberührt; ein hängendes Fenster bremst den Kern nicht |
| Verlauf 200 Ereignisse, nur im Speicher | genug für Anzeige und Prüfung; Speichern erst bei Bedarf (TX-16) |
| `SecretStore` aus `ObsSecretStore` herausgelöst | ein Weg für alle Geheimnisse (F1), keine Kopie des Codes |
| Access- und Refresh-Token zusammen in einer Datei | beim Start kein Erneuern nötig; schont das Refresh-Token, das nur einmal gilt |
| Client-ID in `kern.ini` | laut Twitch kein Geheimnis beim Public Client |
| Keepalive 10 s (+5 s Spielraum) | Verlust früh erkannt; Last vernachlässigbar |
| Wartezeit 2 bis 60 s mit Zufall | kein Ansturm nach Störungen |
| Doppelte `message_id` übergehen (500) | Doku schweigt; billig |
| Chat-Texte nicht ins Protokoll, nur Anzahl und Fehler | Protokoll klein; Nachrichten der Zuschauer bleiben aus dem Journal |
| Testserver selbst gebaut statt Twitch CLI | kein fremdes Programm (Regel 5); steuerbar wie `obstestserver.h` |
| `--twitch-testserver` nur mit `127.0.0.1` | eine Testoption darf nie ein Token ins Netz schicken (Regel 12) |
| Neuer Schalter `DSS_CORE_TWITCH` | riskanter Teil: externe Verbindung mit TLS und Token |
| `channel.chat.notification` nicht in P13 | Subs und Raids kommen schon über eigene Abos; P14 entscheidet |

## Quellen

- **Lokal gelesen (2026-11-09):**
  - `/var/lib/dpkg/status` (Versionen in Abschnitt 5, dazu `systemd` 257.13-1~deb13u1)
  - `/usr/lib/x86_64-linux-gnu/qt6/plugins/tls/`
  - Repo: `src/obs/obsconnection.h`, `src/obs/obssecretstore.h`, `src/obs/obsprotocol.h`, `src/obs/CMakeLists.txt`, `src/core/coreservice.h`, `src/core/coreservice.cpp`, `src/core/CMakeLists.txt`, `src/core/main.cpp`, `src/common/connectionstate.h`, `src/app/mainwindow.cpp`, `src/app/headerbar.cpp`, `src/cli/main.cpp`, `scripts/pakete.txt`, `CMakeLists.txt`, `docs/01`, `03`, `04`, `05`, `07`, `08`, `docs/archiv/prompts/P12-ton.md`
- **Web gelesen (2026-11-09):**
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
  - https://dev.twitch.tv/docs/chat/send-receive-messages/
  - https://dev.twitch.tv/docs/api/reference/ (Send Chat Message nur zusammengefasst, Create EventSub Subscription nicht lesbar)
  - https://dev.twitch.tv/docs/cli/websocket-event-command/
  - https://doc.qt.io/qt-6/qoauth2deviceauthorizationflow.html
  - https://packages.debian.org/trixie/qt6-networkauth-dev
  - https://www.freedesktop.org/software/systemd/man/latest/systemd-creds.html (Muster F1, wie P10c)
- **Nicht gelesen (in Schritt 0):**
  - https://dev.twitch.tv/docs/chat/irc-migration/
  - https://dev.twitch.tv/docs/cli/configure-command/
  - https://doc.qt.io/qt-6.8/qsslsocket.html
  - https://doc.qt.io/qt-6.8/qnetworkaccessmanager.html
