#pragma once

#include "event.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

// Nachrichten von EventSub über WebSocket lesen und Abos bauen, Benachrichtigungen in Ereignisse
// des Flusses umsetzen (P13, Plan 1.3 und 2.6). Reine Funktionen ohne Verbindung, ohne Qt Gui und
// ohne protocol.h.
//
// Abweichungen vom Plan 1.3 nach Schritt 0: siehe src/events/eventtypes.h.
//
// Quellen:
//   Nachrichten (metadata, payload, session, subscription, event):
//     https://dev.twitch.tv/docs/eventsub/websocket-reference/,
//     https://dev.twitch.tv/docs/eventsub/handling-websocket-events/
//   Abos (POST /helix/eventsub/subscriptions, transport websocket mit session_id):
//     https://dev.twitch.tv/docs/eventsub/manage-subscriptions/
//   Felder je Typ: https://dev.twitch.tv/docs/eventsub/reference/chat#channel-chat-message,
//     https://dev.twitch.tv/docs/eventsub/reference/channel (follow, subscribe, subscription.message,
//     subscription.gift, cheer, raid),
//     https://dev.twitch.tv/docs/eventsub/reference/channel-points (custom_reward_redemption.add),
//     https://dev.twitch.tv/docs/eventsub/reference/stream (online, offline)
//   Bericht Schritt 0: docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md

namespace dss::twitch {

enum class MessageKind { Welcome, Keepalive, Notification, Reconnect, Revocation, Unknown };

struct Message
{
    MessageKind kind = MessageKind::Unknown;
    QString messageId;        // metadata.message_id (doppelte Zustellung: gleiche Kennung)
    QString messageType;      // metadata.message_type, wie empfangen
    QDateTime timestamp;      // metadata.message_timestamp, UTC
    // Welcome und Reconnect
    QString sessionId;
    int keepaliveSeconds = 0; // 0: nicht angegeben (Reconnect: null)
    QString reconnectUrl;     // nur Reconnect, unverändert zu benutzen
    // Notification und Revocation
    QString subscriptionId;
    QString subscriptionType;
    QString subscriptionVersion;
    QString subscriptionStatus; // Revocation: authorization_revoked, user_removed, version_removed
    QJsonObject event;          // payload.event (nur Notification)
};

// false mit Meldung, wenn kein JSON oder metadata fehlt. Unbekannte message_type geben
// MessageKind::Unknown und true (neuere Fassungen des Protokolls).
bool parseMessage(const QByteArray &text, Message *message, QString *error);

// Angaben, die nicht in der Nachricht stehen.
struct Context
{
    QString broadcasterUserId; // Kanal (für role und is_broadcaster)
    QString botUserId;         // angemeldetes Bot-Konto, leer ohne Bot (für is_bot_account)
};

// Benachrichtigung → Ereignis. std::nullopt mit Meldung, wenn der Typ nicht zu P13 gehört oder ein
// Feld fehlt oder die falsche Art hat. Die Meldung nennt nur Typ und Feld, nie Inhalte.
// channel.subscribe mit is_gift wird twitch.gift_sub, sonst twitch.subscribe (Plan 1.3).
// stream.online setzt at_start false; den Stream-Zustand beim Start baut streamOnlineAtStart.
std::optional<events::Event> toEvent(const Message &message, const Context &context, QString *error);

// twitch.stream_online mit at_start true aus einem Eintrag von GET /helix/streams (Plan 2.5).
// Quelle: https://dev.twitch.tv/docs/api/reference/streams#get-streams (type, started_at)
std::optional<events::Event> streamOnlineAtStart(const QJsonObject &stream, QString *error);

// tier von Twitch («1000») → Zahl und Text wie Streamer.bot («tier 1»); 0 und leer bei
// unbekanntem Wert.
int tierNumber(const QString &tier);
QString tierName(int tier);

struct SubscriptionSpec
{
    const char *type;
    const char *version;
};

// Abos von P13 (Plan 1.3), in dieser Reihenfolge. Plan 2.5 nennt 11; die Tabelle in 1.3 hat 10.
inline constexpr SubscriptionSpec kSubscriptions[] = {
    {"channel.chat.message", "1"},
    {"channel.follow", "2"},
    {"channel.subscribe", "1"},
    {"channel.subscription.message", "1"},
    {"channel.subscription.gift", "1"},
    {"channel.cheer", "1"},
    {"channel.raid", "1"},
    {"channel.channel_points_custom_reward_redemption.add", "1"},
    {"stream.online", "1"},
    {"stream.offline", "1"},
};

// Körper für POST /helix/eventsub/subscriptions. Bedingung je Typ:
// channel.chat.message: broadcaster_user_id und user_id (lesen als Kanal);
// channel.follow: broadcaster_user_id und moderator_user_id (der Kanal selbst);
// channel.raid: to_broadcaster_user_id; alle übrigen: broadcaster_user_id.
QJsonObject subscriptionRequest(const SubscriptionSpec &spec, const QString &broadcasterUserId,
                                const QString &sessionId);
QList<QJsonObject> subscriptionRequests(const QString &broadcasterUserId, const QString &sessionId);

} // namespace dss::twitch
