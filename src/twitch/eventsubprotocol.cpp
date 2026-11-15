#include "eventsubprotocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace dss::twitch {

namespace {

using events::Event;

// Liest Felder aus einem JSON-Objekt; der erste Fehler bleibt stehen. Fehlende Felder, die laut Doku
// null sein dürfen, gelten als null (ältere oder gekürzte Nachrichten).
class Reader
{
public:
    Reader(const QJsonObject &object, QString *error)
        : m_object(object)
        , m_error(error)
    {
    }

    bool ok() const { return m_ok; }

    QString string(const char *name, const QString &prefix = {})
    {
        const QJsonValue value = m_object.value(QLatin1String(name));
        if (!value.isString())
            fail(prefix, name, "Text");
        return value.toString();
    }
    QJsonValue nullableString(const char *name, const QString &prefix = {})
    {
        const QJsonValue value = m_object.value(QLatin1String(name));
        if (value.isString())
            return value;
        if (!value.isUndefined() && !value.isNull())
            fail(prefix, name, "Text oder null");
        return QJsonValue::Null;
    }
    int integer(const char *name, const QString &prefix = {})
    {
        const QJsonValue value = m_object.value(QLatin1String(name));
        if (!value.isDouble())
            fail(prefix, name, "Zahl");
        return value.toInt();
    }
    QJsonValue nullableInteger(const char *name, const QString &prefix = {})
    {
        const QJsonValue value = m_object.value(QLatin1String(name));
        if (value.isDouble())
            return value.toInt();
        if (!value.isUndefined() && !value.isNull())
            fail(prefix, name, "Zahl oder null");
        return QJsonValue::Null;
    }
    bool boolean(const char *name, const QString &prefix = {})
    {
        const QJsonValue value = m_object.value(QLatin1String(name));
        if (!value.isBool())
            fail(prefix, name, "true oder false");
        return value.toBool();
    }
    QJsonObject object(const char *name, const QString &prefix = {})
    {
        const QJsonValue value = m_object.value(QLatin1String(name));
        if (!value.isObject())
            fail(prefix, name, "Objekt");
        return value.toObject();
    }
    // std::nullopt bei null oder fehlend.
    std::optional<QJsonObject> nullableObject(const char *name, const QString &prefix = {})
    {
        const QJsonValue value = m_object.value(QLatin1String(name));
        if (value.isObject())
            return value.toObject();
        if (!value.isUndefined() && !value.isNull())
            fail(prefix, name, "Objekt oder null");
        return std::nullopt;
    }
    QJsonArray array(const char *name, const QString &prefix = {})
    {
        const QJsonValue value = m_object.value(QLatin1String(name));
        if (!value.isArray())
            fail(prefix, name, "Liste");
        return value.toArray();
    }

    void fail(const QString &prefix, const char *name, const char *expected)
    {
        if (!m_ok)
            return;
        m_ok = false;
        if (m_error)
            *m_error = QStringLiteral("Feld «%1%2»: %3 erwartet").arg(prefix, QLatin1String(name), QLatin1String(expected));
    }
    void failText(const QString &text)
    {
        if (!m_ok)
            return;
        m_ok = false;
        if (m_error)
            *m_error = text;
    }

private:
    QJsonObject m_object;
    QString *m_error;
    bool m_ok = true;
};

QDateTime parseTime(const QString &text)
{
    // RFC 3339 mit Nanosekunden (Beispiele der Doku); Qt liest höchstens Millisekunden.
    QString trimmed = text;
    const qsizetype dot = trimmed.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        qsizetype end = dot + 1;
        while (end < trimmed.size() && trimmed.at(end).isDigit())
            ++end;
        const qsizetype digits = end - dot - 1;
        if (digits > 3)
            trimmed.remove(dot + 4, digits - 3);
    }
    QDateTime time = QDateTime::fromString(trimmed, Qt::ISODateWithMs);
    if (!time.isValid())
        time = QDateTime::fromString(trimmed, Qt::ISODate);
    return time.isValid() ? time.toUTC() : QDateTime();
}

// user_id, user_login, user_name aus <prefix>user_id usw.; nullable: null bei anonym.
void insertUser(QJsonObject *data, Reader &reader, const char *source, bool nullable, const char *target = "user")
{
    const QString s = QLatin1String(source);
    const QString t = QLatin1String(target);
    const auto read = [&](const char *suffix) -> QJsonValue {
        const QByteArray name = (s + QLatin1String(suffix)).toLatin1();
        if (nullable)
            return reader.nullableString(name.constData());
        return reader.string(name.constData());
    };
    data->insert(t + QLatin1String("_id"), read("_id"));
    data->insert(t + QLatin1String("_login"), read("_login"));
    data->insert(t + QLatin1String("_name"), read("_name"));
}

void insertNullUser(QJsonObject *data)
{
    data->insert(QStringLiteral("user_id"), QJsonValue::Null);
    data->insert(QStringLiteral("user_login"), QJsonValue::Null);
    data->insert(QStringLiteral("user_name"), QJsonValue::Null);
}

void insertTier(QJsonObject *data, Reader &reader)
{
    const QString tier = reader.string("tier");
    const int number = tierNumber(tier);
    if (reader.ok() && number == 0)
        reader.failText(QStringLiteral("Feld «tier»: unbekannter Wert"));
    data->insert(QStringLiteral("tier"), number);
    data->insert(QStringLiteral("tier_name"), tierName(number));
}

// Rollen nach Streamer.bot (Schritt 0, C 19): 1 Zuschauer, 2 VIP, 3 Moderator, 4 Kanal. Abzeichen
// set_id wie im Beispiel der Doku (moderator, subscriber) und bei Get Global Chat Badges
// (broadcaster, vip): https://dev.twitch.tv/docs/eventsub/reference/chat#channel-chat-message
QJsonObject chatData(const Context &context, Reader &reader)
{
    QJsonObject data;
    data.insert(QStringLiteral("message_id"), reader.string("message_id"));
    insertUser(&data, reader, "chatter_user", false);
    const QJsonObject message = reader.object("message");
    Reader messageReader(message, nullptr);
    data.insert(QStringLiteral("text"), messageReader.string("text"));
    QJsonArray fragments;
    for (const QJsonValue &value : messageReader.array("fragments")) {
        const QJsonObject fragment = value.toObject();
        const QJsonValue emote = fragment.value(QStringLiteral("emote"));
        fragments.append(QJsonObject{
            {QStringLiteral("type"), fragment.value(QStringLiteral("type")).toString()},
            {QStringLiteral("text"), fragment.value(QStringLiteral("text")).toString()},
            {QStringLiteral("emote_id"), emote.isObject() ? emote.toObject().value(QStringLiteral("id")) : QJsonValue::Null},
        });
    }
    if (!messageReader.ok())
        reader.failText(QStringLiteral("Feld «message»: text und fragments erwartet"));
    data.insert(QStringLiteral("fragments"), fragments);
    data.insert(QStringLiteral("color"), reader.string("color"));

    QJsonArray badges;
    bool subscriber = false;
    bool vip = false;
    bool moderator = false;
    bool broadcaster = false;
    for (const QJsonValue &value : reader.array("badges")) {
        const QJsonObject badge = value.toObject();
        const QString setId = badge.value(QStringLiteral("set_id")).toString();
        badges.append(QJsonObject{{QStringLiteral("set_id"), setId},
                                  {QStringLiteral("id"), badge.value(QStringLiteral("id")).toString()}});
        subscriber |= setId == QLatin1String("subscriber");
        vip |= setId == QLatin1String("vip");
        moderator |= setId == QLatin1String("moderator");
        broadcaster |= setId == QLatin1String("broadcaster");
    }
    const QString userId = data.value(QStringLiteral("user_id")).toString();
    broadcaster |= !context.broadcasterUserId.isEmpty() && userId == context.broadcasterUserId;
    data.insert(QStringLiteral("badges"), badges);
    data.insert(QStringLiteral("role"), broadcaster ? 4 : moderator ? 3 : vip ? 2 : 1);
    data.insert(QStringLiteral("is_subscriber"), subscriber);
    data.insert(QStringLiteral("is_vip"), vip);
    data.insert(QStringLiteral("is_moderator"), moderator);
    data.insert(QStringLiteral("is_broadcaster"), broadcaster);
    data.insert(QStringLiteral("is_bot_account"), !context.botUserId.isEmpty() && userId == context.botUserId);

    const std::optional<QJsonObject> cheer = reader.nullableObject("cheer");
    data.insert(QStringLiteral("bits"), cheer ? cheer->value(QStringLiteral("bits")).toInt() : 0);
    const std::optional<QJsonObject> reply = reader.nullableObject("reply");
    data.insert(QStringLiteral("reply_to"),
                reply ? reply->value(QStringLiteral("parent_message_id")) : QJsonValue(QJsonValue::Null));
    data.insert(QStringLiteral("reward_id"), reader.nullableString("channel_points_custom_reward_id"));
    const QString messageType = reader.string("message_type");
    data.insert(QStringLiteral("message_type"), messageType);
    data.insert(QStringLiteral("is_highlight"), messageType == QLatin1String("channel_points_highlighted"));
    data.insert(QStringLiteral("source_broadcaster_id"), reader.nullableString("source_broadcaster_user_id"));
    return data;
}

} // namespace

bool parseMessage(const QByteArray &text, Message *message, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text, &parseError);
    if (!document.isObject()) {
        *error = QStringLiteral("kein JSON-Objekt (%1)").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    Reader rootReader(root, error);
    const QJsonObject metadata = rootReader.object("metadata");
    const QJsonObject payload = rootReader.object("payload");
    if (!rootReader.ok())
        return false;
    Reader meta(metadata, error);
    Message result;
    result.messageId = meta.string("message_id", QStringLiteral("metadata."));
    result.messageType = meta.string("message_type", QStringLiteral("metadata."));
    const QString timestamp = meta.string("message_timestamp", QStringLiteral("metadata."));
    if (!meta.ok())
        return false;
    result.timestamp = parseTime(timestamp);
    if (!result.timestamp.isValid()) {
        *error = QStringLiteral("Feld «metadata.message_timestamp»: Zeit erwartet");
        return false;
    }

    const QString &type = result.messageType;
    if (type == QLatin1String("session_welcome") || type == QLatin1String("session_reconnect")) {
        result.kind = type == QLatin1String("session_welcome") ? MessageKind::Welcome : MessageKind::Reconnect;
        Reader p(payload, error);
        const QJsonObject session = p.object("session", QStringLiteral("payload."));
        if (!p.ok())
            return false;
        Reader s(session, error);
        result.sessionId = s.string("id", QStringLiteral("payload.session."));
        result.keepaliveSeconds = s.nullableInteger("keepalive_timeout_seconds", QStringLiteral("payload.session.")).toInt();
        result.reconnectUrl = s.nullableString("reconnect_url", QStringLiteral("payload.session.")).toString();
        if (!s.ok())
            return false;
        if (result.kind == MessageKind::Reconnect && result.reconnectUrl.isEmpty()) {
            *error = QStringLiteral("Feld «payload.session.reconnect_url» fehlt");
            return false;
        }
    } else if (type == QLatin1String("session_keepalive")) {
        result.kind = MessageKind::Keepalive;
    } else if (type == QLatin1String("notification") || type == QLatin1String("revocation")) {
        result.kind = type == QLatin1String("notification") ? MessageKind::Notification : MessageKind::Revocation;
        Reader p(payload, error);
        const QJsonObject subscription = p.object("subscription", QStringLiteral("payload."));
        if (result.kind == MessageKind::Notification)
            result.event = p.object("event", QStringLiteral("payload."));
        if (!p.ok())
            return false;
        Reader s(subscription, error);
        result.subscriptionId = s.string("id", QStringLiteral("payload.subscription."));
        result.subscriptionType = s.string("type", QStringLiteral("payload.subscription."));
        result.subscriptionVersion = s.string("version", QStringLiteral("payload.subscription."));
        result.subscriptionStatus = s.string("status", QStringLiteral("payload.subscription."));
        if (!s.ok())
            return false;
    } else {
        result.kind = MessageKind::Unknown;
    }
    *message = result;
    return true;
}

int tierNumber(const QString &tier)
{
    if (tier == QLatin1String("1000"))
        return 1000;
    if (tier == QLatin1String("2000"))
        return 2000;
    if (tier == QLatin1String("3000"))
        return 3000;
    return 0;
}

QString tierName(int tier)
{
    switch (tier) {
    case 1000:
        return QStringLiteral("tier 1");
    case 2000:
        return QStringLiteral("tier 2");
    case 3000:
        return QStringLiteral("tier 3");
    default:
        return {};
    }
}

std::optional<Event> toEvent(const Message &message, const Context &context, QString *error)
{
    if (message.kind != MessageKind::Notification) {
        *error = QStringLiteral("keine Benachrichtigung");
        return std::nullopt;
    }
    const QString &subscription = message.subscriptionType;
    Reader reader(message.event, error);
    Event event;
    event.origin = QStringLiteral("twitch");
    event.time = message.timestamp;
    event.originId = message.messageId;
    QJsonObject data;

    if (subscription == QLatin1String("channel.chat.message")) {
        event.type = QStringLiteral("twitch.chat_message");
        data = chatData(context, reader);
    } else if (subscription == QLatin1String("channel.follow")) {
        event.type = QStringLiteral("twitch.follow");
        insertUser(&data, reader, "user", false);
        data.insert(QStringLiteral("followed_at"), reader.string("followed_at"));
    } else if (subscription == QLatin1String("channel.subscribe")) {
        const bool gift = reader.boolean("is_gift");
        if (gift) {
            // Den Schenkenden nennt channel.subscribe nicht (Schritt 0, A3).
            event.type = QStringLiteral("twitch.gift_sub");
            insertNullUser(&data);
            insertUser(&data, reader, "user", false, "recipient_user");
        } else {
            event.type = QStringLiteral("twitch.subscribe");
            insertUser(&data, reader, "user", false);
            data.insert(QStringLiteral("is_gift"), false);
        }
        insertTier(&data, reader);
    } else if (subscription == QLatin1String("channel.subscription.message")) {
        event.type = QStringLiteral("twitch.resubscribe");
        insertUser(&data, reader, "user", false);
        insertTier(&data, reader);
        const QJsonObject text = reader.object("message");
        if (!reader.ok())
            return std::nullopt;
        Reader textReader(text, error);
        data.insert(QStringLiteral("text"), textReader.string("text", QStringLiteral("message.")));
        if (!textReader.ok())
            return std::nullopt;
        data.insert(QStringLiteral("months"), reader.integer("cumulative_months"));
        data.insert(QStringLiteral("streak_months"), reader.nullableInteger("streak_months"));
        data.insert(QStringLiteral("duration_months"), reader.integer("duration_months"));
    } else if (subscription == QLatin1String("channel.subscription.gift")) {
        event.type = QStringLiteral("twitch.gift_bomb");
        insertUser(&data, reader, "user", true);
        data.insert(QStringLiteral("is_anonymous"), reader.boolean("is_anonymous"));
        data.insert(QStringLiteral("total"), reader.integer("total"));
        insertTier(&data, reader);
        data.insert(QStringLiteral("cumulative_total"), reader.nullableInteger("cumulative_total"));
    } else if (subscription == QLatin1String("channel.cheer")) {
        event.type = QStringLiteral("twitch.cheer");
        insertUser(&data, reader, "user", true);
        data.insert(QStringLiteral("is_anonymous"), reader.boolean("is_anonymous"));
        data.insert(QStringLiteral("bits"), reader.integer("bits"));
        data.insert(QStringLiteral("text"), reader.string("message"));
    } else if (subscription == QLatin1String("channel.raid")) {
        event.type = QStringLiteral("twitch.raid");
        insertUser(&data, reader, "from_broadcaster_user", false, "from_user");
        data.insert(QStringLiteral("viewers"), reader.integer("viewers"));
    } else if (subscription == QLatin1String("channel.channel_points_custom_reward_redemption.add")) {
        event.type = QStringLiteral("twitch.reward_redeemed");
        data.insert(QStringLiteral("redemption_id"), reader.string("id"));
        insertUser(&data, reader, "user", false);
        data.insert(QStringLiteral("user_input"), reader.string("user_input"));
        const QJsonObject reward = reader.object("reward");
        if (!reader.ok())
            return std::nullopt;
        Reader rewardReader(reward, error);
        data.insert(QStringLiteral("reward_id"), rewardReader.string("id", QStringLiteral("reward.")));
        data.insert(QStringLiteral("reward_title"), rewardReader.string("title", QStringLiteral("reward.")));
        data.insert(QStringLiteral("reward_cost"), rewardReader.integer("cost", QStringLiteral("reward.")));
        if (!rewardReader.ok())
            return std::nullopt;
        data.insert(QStringLiteral("status"), reader.string("status"));
    } else if (subscription == QLatin1String("stream.online")) {
        event.type = QStringLiteral("twitch.stream_online");
        data.insert(QStringLiteral("started_at"), reader.string("started_at"));
        data.insert(QStringLiteral("kind"), reader.string("type"));
        data.insert(QStringLiteral("at_start"), false);
    } else if (subscription == QLatin1String("stream.offline")) {
        event.type = QStringLiteral("twitch.stream_offline");
        // stream.offline hat keine Zeit; die Nachricht von Twitch schon (UTC, ms).
        data.insert(QStringLiteral("ended_at"), message.timestamp.toString(Qt::ISODateWithMs));
    } else {
        *error = QStringLiteral("Abo «%1» gehört nicht zu P13").arg(subscription);
        return std::nullopt;
    }
    if (!reader.ok())
        return std::nullopt;
    event.data = data;
    return event;
}

std::optional<Event> streamOnlineAtStart(const QJsonObject &stream, QString *error)
{
    Reader reader(stream, error);
    QJsonObject data;
    data.insert(QStringLiteral("started_at"), reader.string("started_at"));
    data.insert(QStringLiteral("kind"), reader.string("type"));
    data.insert(QStringLiteral("at_start"), true);
    if (!reader.ok())
        return std::nullopt;
    Event event;
    event.type = QStringLiteral("twitch.stream_online");
    event.origin = QStringLiteral("twitch");
    event.data = data;
    return event;
}

QJsonObject subscriptionRequest(const SubscriptionSpec &spec, const QString &broadcasterUserId,
                                const QString &sessionId)
{
    const QLatin1String type(spec.type);
    QJsonObject condition;
    if (type == QLatin1String("channel.raid")) {
        condition.insert(QStringLiteral("to_broadcaster_user_id"), broadcasterUserId);
    } else {
        condition.insert(QStringLiteral("broadcaster_user_id"), broadcasterUserId);
        if (type == QLatin1String("channel.chat.message"))
            condition.insert(QStringLiteral("user_id"), broadcasterUserId);
        else if (type == QLatin1String("channel.follow"))
            condition.insert(QStringLiteral("moderator_user_id"), broadcasterUserId);
    }
    return QJsonObject{
        {QStringLiteral("type"), QString(type)},
        {QStringLiteral("version"), QString::fromLatin1(spec.version)},
        {QStringLiteral("condition"), condition},
        {QStringLiteral("transport"),
         QJsonObject{{QStringLiteral("method"), QStringLiteral("websocket")}, {QStringLiteral("session_id"), sessionId}}},
    };
}

QList<QJsonObject> subscriptionRequests(const QString &broadcasterUserId, const QString &sessionId)
{
    QList<QJsonObject> result;
    for (const SubscriptionSpec &spec : kSubscriptions)
        result.append(subscriptionRequest(spec, broadcasterUserId, sessionId));
    return result;
}

} // namespace dss::twitch
