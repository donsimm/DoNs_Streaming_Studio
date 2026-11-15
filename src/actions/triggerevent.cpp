// Quellen:
//   Felder der Ereignisse: Plan P13 1.3 (docs/archiv/prompts/P13-twitch.md)
//   Argumentnamen: docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md, Abschnitt C 19,
//   https://github.com/Streamerbot/docs/tree/main/streamerbot/3.api/.variables,
//   https://docs.streamer.bot/api/triggers/twitch/
//   message_type «channel_points_highlighted», «user_intro»:
//   https://dev.twitch.tv/docs/eventsub/reference/chat#channel-chat-message

#include "triggerevent.h"

#include "triggertypes.h"

namespace dss::actions {

namespace {

void put(Arguments &args, const QString &name, const QJsonObject &data, const QString &field)
{
    const QJsonValue v = data.value(field);
    if (!v.isNull() && !v.isUndefined())
        args.insert(name, v);
}

void putUser(Arguments &args, const QJsonObject &data, const QString &prefix = QStringLiteral("user"))
{
    put(args, QStringLiteral("user"), data, prefix + QStringLiteral("_name"));
    put(args, QStringLiteral("userName"), data, prefix + QStringLiteral("_login"));
    put(args, QStringLiteral("userId"), data, prefix + QStringLiteral("_id"));
}

void putTier(Arguments &args, const QJsonObject &data)
{
    const QString tier = eventTier(data);
    if (tier.isEmpty())
        return;
    args.insert(QStringLiteral("tier"), tierText(tier));
    bool ok = false;
    const int number = tier.toInt(&ok);
    if (ok)
        args.insert(QStringLiteral("subscriptionTier"), number);
}

void putChat(Arguments &args, const QJsonObject &data)
{
    putUser(args, data);
    put(args, QStringLiteral("msgId"), data, QStringLiteral("message_id"));
    put(args, QStringLiteral("message"), data, QStringLiteral("text"));
    put(args, QStringLiteral("role"), data, QStringLiteral("role"));
    put(args, QStringLiteral("isSubscribed"), data, QStringLiteral("is_subscriber"));
    put(args, QStringLiteral("isVip"), data, QStringLiteral("is_vip"));
    put(args, QStringLiteral("isModerator"), data, QStringLiteral("is_moderator"));
    const QString messageType = data.value(QStringLiteral("message_type")).toString();
    args.insert(QStringLiteral("isHighlight"), messageType == QLatin1String("channel_points_highlighted"));
    // firstMessage: erste Nachricht überhaupt im Kanal (Twitch: user_intro)
    args.insert(QStringLiteral("firstMessage"), messageType == QLatin1String("user_intro"));
}

} // namespace

QString tierText(const QString &tier)
{
    if (tier == QLatin1String("1000"))
        return QStringLiteral("tier 1");
    if (tier == QLatin1String("2000"))
        return QStringLiteral("tier 2");
    if (tier == QLatin1String("3000"))
        return QStringLiteral("tier 3");
    return tier;
}

QString eventTier(const QJsonObject &data)
{
    const QJsonValue v = data.value(QStringLiteral("tier"));
    if (v.isDouble())
        return QString::number(v.toInteger());
    return v.toString();
}

Arguments eventArguments(const TriggerEvent &event)
{
    Arguments args;
    const QJsonObject &d = event.data;
    const QString &type = event.type;
    if (type == QLatin1String(kTriggerChatMessage)) {
        putChat(args, d);
    } else if (type == QLatin1String(kTriggerFirstMessage)) {
        putChat(args, d);
        put(args, QStringLiteral("firstEver"), d, QStringLiteral("first_ever"));
    } else if (type == QLatin1String(kTriggerFollow)) {
        putUser(args, d);
    } else if (type == QLatin1String(kTriggerSubscribe)) {
        putUser(args, d);
        putTier(args, d);
    } else if (type == QLatin1String(kTriggerResubscribe)) {
        putUser(args, d);
        putTier(args, d);
        put(args, QStringLiteral("message"), d, QStringLiteral("text"));
        put(args, QStringLiteral("cumulative"), d, QStringLiteral("months"));
        // streak_months ist null, wenn der Zuschauer die Serie nicht teilt
        const QJsonValue streak = d.value(QStringLiteral("streak_months"));
        args.insert(QStringLiteral("streakShared"), streak.isDouble());
        args.insert(QStringLiteral("monthStreak"), streak.isDouble() ? streak : QJsonValue(0));
        const QJsonValue duration = d.value(QStringLiteral("duration_months"));
        if (duration.isDouble()) {
            args.insert(QStringLiteral("multiMonthDuration"), duration);
            args.insert(QStringLiteral("isMultiMonth"), duration.toDouble() > 1);
        }
    } else if (type == QLatin1String(kTriggerGiftSub)) {
        // user_* ist bei P13 der Beschenkte; bei Streamer.bot heisst er recipient*
        put(args, QStringLiteral("recipientUser"), d, QStringLiteral("user_name"));
        put(args, QStringLiteral("recipientUserName"), d, QStringLiteral("user_login"));
        put(args, QStringLiteral("recipientId"), d, QStringLiteral("user_id"));
        putTier(args, d);
    } else if (type == QLatin1String(kTriggerGiftBomb)) {
        putUser(args, d);
        putTier(args, d);
        put(args, QStringLiteral("gifts"), d, QStringLiteral("total"));
        put(args, QStringLiteral("anonymous"), d, QStringLiteral("is_anonymous"));
        put(args, QStringLiteral("totalGifts"), d, QStringLiteral("cumulative_total"));
    } else if (type == QLatin1String(kTriggerCheer)) {
        putUser(args, d);
        put(args, QStringLiteral("bits"), d, QStringLiteral("bits"));
        put(args, QStringLiteral("message"), d, QStringLiteral("text"));
        put(args, QStringLiteral("isAnonymous"), d, QStringLiteral("is_anonymous"));
    } else if (type == QLatin1String(kTriggerRaid)) {
        putUser(args, d, QStringLiteral("from_user"));
        put(args, QStringLiteral("viewers"), d, QStringLiteral("viewers"));
    } else if (type == QLatin1String(kTriggerStreamOnline)) {
        put(args, QStringLiteral("startedAt"), d, QStringLiteral("started_at"));
    } else if (type == QLatin1String(kTriggerStreamOffline)) {
        if (event.time.isValid())
            args.insert(QStringLiteral("endedAt"), event.time.toUTC().toString(Qt::ISODateWithMs));
    }
    return args;
}

} // namespace dss::actions
