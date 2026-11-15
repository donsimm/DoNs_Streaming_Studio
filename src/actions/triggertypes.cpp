#include "triggertypes.h"

#include <QJsonArray>
#include <QJsonObject>

namespace dss::actions {

namespace {

FieldSpec tiersField()
{
    // leere Liste = alle Stufen
    return {QStringLiteral("tiers"), FieldKind::StringList, QJsonArray(), subscriptionTiers()};
}

FieldSpec countField(const QString &name, qint64 defaultValue)
{
    FieldSpec f{name, FieldKind::Int, double(defaultValue), {}};
    f.min = 0;
    return f;
}

QList<TriggerTypeInfo> buildTriggerTypes()
{
    const QStringList user = userArguments();
    const QStringList chat = userArguments() + chatArguments();
    QStringList commandArgs{QStringLiteral("command"), QStringLiteral("commandId"),
                            QStringLiteral("commandName"), QStringLiteral("rawInput")};
    for (int i = 0; i <= 9; ++i)
        commandArgs.append(QStringLiteral("input%1").arg(i));
    commandArgs += {QStringLiteral("msgId"), QStringLiteral("counter"), QStringLiteral("userCounter")};

    QList<TriggerTypeInfo> t;
    t.append({QString::fromLatin1(kTriggerCommand), QStringLiteral("Befehl"), 401,
              QString::fromLatin1(kTriggerChatMessage),
              {{QStringLiteral("command_id"), FieldKind::NullableUuid, QJsonValue::Null, {}}},
              chat + commandArgs});
    t.append({QString::fromLatin1(kTriggerChatMessage), QStringLiteral("Chat-Nachricht"), 133,
              QString::fromLatin1(kTriggerChatMessage), {},
              chat + QStringList{QStringLiteral("message"), QStringLiteral("msgId")}});
    // firstEver: eigenes Argument für first_ever (P13 1.4, letzter Punkt)
    t.append({QString::fromLatin1(kTriggerFirstMessage), QStringLiteral("Erste Nachricht"), 120,
              QString::fromLatin1(kTriggerFirstMessage), {},
              chat + QStringList{QStringLiteral("message"), QStringLiteral("msgId"), QStringLiteral("firstEver")}});
    t.append({QString::fromLatin1(kTriggerFollow), QStringLiteral("Follow"), 101,
              QString::fromLatin1(kTriggerFollow), {}, user});
    t.append({QString::fromLatin1(kTriggerSubscribe), QStringLiteral("Abo"), 103,
              QString::fromLatin1(kTriggerSubscribe), {tiersField()}, user + QStringList{QStringLiteral("tier")}});
    // Schritt 0 offen: Namen months, streakMonths, message (eigene Namen nach P13 1.4)
    t.append({QString::fromLatin1(kTriggerResubscribe), QStringLiteral("Abo verlängert"), 104,
              QString::fromLatin1(kTriggerResubscribe), {tiersField(), countField(QStringLiteral("min_months"), 0)},
              user + QStringList{QStringLiteral("tier"), QStringLiteral("months"), QStringLiteral("streakMonths"),
                                 QStringLiteral("message")}});
    // Schritt 0 offen: Namen des Beschenkten recipientUser, recipientUserName, recipientUserId
    t.append({QString::fromLatin1(kTriggerGiftSub), QStringLiteral("Geschenk-Abo"), 105,
              QString::fromLatin1(kTriggerGiftSub), {tiersField()},
              {QStringLiteral("recipientUser"), QStringLiteral("recipientUserName"),
               QStringLiteral("recipientUserId"), QStringLiteral("tier")}});
    // Schritt 0 offen: Namen gifts, isAnonymous
    t.append({QString::fromLatin1(kTriggerGiftBomb), QStringLiteral("Geschenk-Bündel"), 106,
              QString::fromLatin1(kTriggerGiftBomb),
              {tiersField(), countField(QStringLiteral("min"), 0), countField(QStringLiteral("max"), 0)},
              user + QStringList{QStringLiteral("tier"), QStringLiteral("gifts"), QStringLiteral("isAnonymous")}});
    // Schritt 0 offen: Namen bits, isAnonymous (bits vorläufig nach Plan 7)
    t.append({QString::fromLatin1(kTriggerCheer), QStringLiteral("Cheer"), 102,
              QString::fromLatin1(kTriggerCheer),
              {countField(QStringLiteral("min"), 0), countField(QStringLiteral("max"), 0)},
              user + QStringList{QStringLiteral("bits"), QStringLiteral("message"), QStringLiteral("isAnonymous")}});
    // Raid: from_user_* als user, userName, userId (P13 1.4). Schritt 0 offen: viewers
    t.append({QString::fromLatin1(kTriggerRaid), QStringLiteral("Raid"), 107,
              QString::fromLatin1(kTriggerRaid),
              {countField(QStringLiteral("min"), 0), countField(QStringLiteral("max"), 0)},
              user + QStringList{QStringLiteral("viewers")}});
    // Schritt 0 offen: Nummer von «Stream Online» bei Streamer.bot (Plan 2.5, 10.3)
    t.append({QString::fromLatin1(kTriggerStreamOnline), QStringLiteral("Stream online"), 0,
              QString::fromLatin1(kTriggerStreamOnline),
              {{QStringLiteral("skip_at_start"), FieldKind::Bool, true, {}}}, {}});
    t.append({QString::fromLatin1(kTriggerStreamOffline), QStringLiteral("Stream offline"), 155,
              QString::fromLatin1(kTriggerStreamOffline), {}, {}});
    t.append({QString::fromLatin1(kTriggerTimer), QStringLiteral("Zeitnachricht"), 701, QString(),
              {{QStringLiteral("timer_id"), FieldKind::NullableUuid, QJsonValue::Null, {}}},
              {QStringLiteral("timerName")}});
    // Testwerte: die eingetragenen Variablen werden Argumente des Laufs
    t.append({QString::fromLatin1(kTriggerTest), QStringLiteral("Test"), 702, QString(),
              {{QStringLiteral("variables"), FieldKind::StringMap, QJsonObject(), {}}}, {}});
    return t;
}

} // namespace

QStringList subscriptionTiers()
{
    return {QStringLiteral("1000"), QStringLiteral("2000"), QStringLiteral("3000")};
}

QStringList commonArguments()
{
    return {QStringLiteral("actionName"),    QStringLiteral("actionId"),
            QStringLiteral("runId"),         QStringLiteral("triggerType"),
            QStringLiteral("date"),          QStringLiteral("time"),
            QStringLiteral("unixtime"),      QStringLiteral("broadcastUser"),
            QStringLiteral("broadcastUserName"), QStringLiteral("broadcastUserId"),
            QStringLiteral("isLive")};
}

QStringList userArguments()
{
    // user und userName belegt (Daten des Nutzers), userId Schritt 0 offen (P13 1.4)
    return {QStringLiteral("user"), QStringLiteral("userName"), QStringLiteral("userId")};
}

QStringList chatArguments()
{
    // role belegt; isSubscribed, isVip, isModerator Schritt 0 offen (P13 1.4)
    return {QStringLiteral("role"), QStringLiteral("isSubscribed"), QStringLiteral("isVip"),
            QStringLiteral("isModerator")};
}

const QList<TriggerTypeInfo> &triggerTypes()
{
    static const QList<TriggerTypeInfo> types = buildTriggerTypes();
    return types;
}

const TriggerTypeInfo *triggerType(const QString &key)
{
    for (const TriggerTypeInfo &t : triggerTypes()) {
        if (t.key == key)
            return &t;
    }
    return nullptr;
}

} // namespace dss::actions
