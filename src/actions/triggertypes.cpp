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
    const QStringList chat = userArguments() + chatArguments() + chatMessageArguments();
    const QStringList tiers{QStringLiteral("tier"), QStringLiteral("subscriptionTier")};
    QStringList commandArgs{QStringLiteral("command"), QStringLiteral("commandId"),
                            QStringLiteral("commandName"), QStringLiteral("rawInput")};
    for (int i = 0; i <= 9; ++i)
        commandArgs.append(QStringLiteral("input%1").arg(i));
    commandArgs += {QStringLiteral("counter"), QStringLiteral("userCounter")};

    QList<TriggerTypeInfo> t;
    t.append({QString::fromLatin1(kTriggerCommand), QStringLiteral("Befehl"), 401,
              QString::fromLatin1(kTriggerChatMessage),
              {{QStringLiteral("command_id"), FieldKind::NullableUuid, QJsonValue::Null, {}}},
              chat + commandArgs});
    t.append({QString::fromLatin1(kTriggerChatMessage), QStringLiteral("Chat-Nachricht"), 133,
              QString::fromLatin1(kTriggerChatMessage), {}, chat});
    // firstEver: eigenes Argument für first_ever (P13 1.4, letzter Punkt)
    t.append({QString::fromLatin1(kTriggerFirstMessage), QStringLiteral("Erste Nachricht"), 120,
              QString::fromLatin1(kTriggerFirstMessage), {}, chat + QStringList{QStringLiteral("firstEver")}});
    t.append({QString::fromLatin1(kTriggerFollow), QStringLiteral("Follow"), 101,
              QString::fromLatin1(kTriggerFollow), {}, user});
    // tier als Text wie Streamer.bot («tier 1»), subscriptionTier als Zahl 1000/2000/3000 (Schritt 0, C 19)
    t.append({QString::fromLatin1(kTriggerSubscribe), QStringLiteral("Abo"), 103,
              QString::fromLatin1(kTriggerSubscribe), {tiersField()}, user + tiers});
    // Resub nach Schritt 0 (C 19). multiMonthTenure liefert P13 nicht.
    t.append({QString::fromLatin1(kTriggerResubscribe), QStringLiteral("Abo verlängert"), 104,
              QString::fromLatin1(kTriggerResubscribe), {tiersField(), countField(QStringLiteral("min_months"), 0)},
              user + tiers
                  + QStringList{QStringLiteral("cumulative"), QStringLiteral("monthStreak"),
                                QStringLiteral("streakShared"), QStringLiteral("isMultiMonth"),
                                QStringLiteral("multiMonthDuration"), QStringLiteral("message")}});
    // Gift Sub nach Schritt 0 (C 19): user* wäre der Schenkende, recipient* der Beschenkte. P13 liefert
    // nur den Beschenkten (channel.subscribe mit is_gift, P13 1.3); user*, anonymous, monthsGifted und
    // subBombCount fehlen darum (Bericht b2, [OFFEN]).
    t.append({QString::fromLatin1(kTriggerGiftSub), QStringLiteral("Geschenk-Abo"), 105,
              QString::fromLatin1(kTriggerGiftSub), {tiersField()},
              QStringList{QStringLiteral("recipientUser"), QStringLiteral("recipientUserName"),
                          QStringLiteral("recipientId")}
                  + tiers});
    // Gift Bomb nach Schritt 0 (C 19); user* ist der Schenkende, fehlt bei anonym
    t.append({QString::fromLatin1(kTriggerGiftBomb), QStringLiteral("Geschenk-Bündel"), 106,
              QString::fromLatin1(kTriggerGiftBomb),
              {tiersField(), countField(QStringLiteral("min"), 0), countField(QStringLiteral("max"), 0)},
              user + tiers
                  + QStringList{QStringLiteral("gifts"), QStringLiteral("anonymous"), QStringLiteral("totalGifts")}});
    // Cheer: bits, message aus der Chat-Gruppe (Schritt 0, C 19). anonymous ist bei Streamer.bot nicht
    // belegt, darum der eigene Name isAnonymous ([OFFEN]).
    t.append({QString::fromLatin1(kTriggerCheer), QStringLiteral("Cheer"), 102,
              QString::fromLatin1(kTriggerCheer),
              {countField(QStringLiteral("min"), 0), countField(QStringLiteral("max"), 0)},
              user + QStringList{QStringLiteral("bits"), QStringLiteral("message"), QStringLiteral("isAnonymous")}});
    // Raid: from_user_* als user, userName, userId (P13 1.4), viewers (Schritt 0, C 19)
    t.append({QString::fromLatin1(kTriggerRaid), QStringLiteral("Raid"), 107,
              QString::fromLatin1(kTriggerRaid),
              {countField(QStringLiteral("min"), 0), countField(QStringLiteral("max"), 0)},
              user + QStringList{QStringLiteral("viewers")}});
    // Stream Online: startedAt (Schritt 0, C 19). game kommt nicht im Ereignis (P13 1.3) und fehlt.
    // Typnummer aus z0: bei Streamer.bot nicht dokumentiert (Schritt 0, C 20), bis dahin 0
    t.append({QString::fromLatin1(kTriggerStreamOnline), QStringLiteral("Stream online"), 0,
              QString::fromLatin1(kTriggerStreamOnline),
              {{QStringLiteral("skip_at_start"), FieldKind::Bool, true, {}}}, {QStringLiteral("startedAt")}});
    // endedAt: Zeitpunkt des Ereignisses (stream.offline hat kein eigenes Feld, Schritt 0 A 9)
    t.append({QString::fromLatin1(kTriggerStreamOffline), QStringLiteral("Stream offline"), 155,
              QString::fromLatin1(kTriggerStreamOffline), {}, {QStringLiteral("endedAt")}});
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
    // belegt (Schritt 0, C 19, User-Gruppe)
    return {QStringLiteral("user"), QStringLiteral("userName"), QStringLiteral("userId")};
}

QStringList chatArguments()
{
    // belegt (Schritt 0, C 19): role 1 bis 4, isSubscribed, isVip, isModerator
    return {QStringLiteral("role"), QStringLiteral("isSubscribed"), QStringLiteral("isVip"),
            QStringLiteral("isModerator")};
}

QStringList chatMessageArguments()
{
    // belegt (Schritt 0, C 19, Chat-Gruppe); isHighlight heisst nicht isHighlighted
    return {QStringLiteral("message"), QStringLiteral("msgId"), QStringLiteral("isHighlight"),
            QStringLiteral("firstMessage")};
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
