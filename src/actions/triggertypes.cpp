#include "triggertypes.h"

#include "eventtypes.h"

#include <QJsonArray>
#include <QJsonObject>

namespace dss::actions {

namespace events = dss::events;

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
    // Argumente aus Ereignissen: nur aus der Typenliste von P13 (eventtypes.h, P14 c6)
    const auto eventArgs = [](const char *type) { return events::eventArgumentNames(QString::fromLatin1(type)); };
    QStringList commandArgs{QStringLiteral("command"), QStringLiteral("commandId"),
                            QStringLiteral("commandName"), QStringLiteral("rawInput")};
    for (int i = 0; i <= 9; ++i)
        commandArgs.append(QStringLiteral("input%1").arg(i));
    commandArgs += {QStringLiteral("counter"), QStringLiteral("userCounter")};

    QList<TriggerTypeInfo> t;
    t.append({QString::fromLatin1(kTriggerCommand), QStringLiteral("Befehl"), 401,
              QString::fromLatin1(kTriggerChatMessage),
              {{QStringLiteral("command_id"), FieldKind::NullableUuid, QJsonValue::Null, {}}},
              eventArgs(kTriggerChatMessage) + commandArgs});
    t.append({QString::fromLatin1(kTriggerChatMessage), QStringLiteral("Chat-Nachricht"), 133,
              QString::fromLatin1(kTriggerChatMessage), {}, eventArgs(kTriggerChatMessage)});
    t.append({QString::fromLatin1(kTriggerFirstMessage), QStringLiteral("Erste Nachricht"), 120,
              QString::fromLatin1(kTriggerFirstMessage), {}, eventArgs(kTriggerFirstMessage)});
    t.append({QString::fromLatin1(kTriggerFollow), QStringLiteral("Follow"), 101,
              QString::fromLatin1(kTriggerFollow), {}, eventArgs(kTriggerFollow)});
    t.append({QString::fromLatin1(kTriggerSubscribe), QStringLiteral("Abo"), 103,
              QString::fromLatin1(kTriggerSubscribe), {tiersField()}, eventArgs(kTriggerSubscribe)});
    t.append({QString::fromLatin1(kTriggerResubscribe), QStringLiteral("Abo verlängert"), 104,
              QString::fromLatin1(kTriggerResubscribe), {tiersField(), countField(QStringLiteral("min_months"), 0)},
              eventArgs(kTriggerResubscribe)});
    // Gift Sub: user* wäre der Schenkende (Schritt 0, C 19); P13 liefert ihn nicht (immer null)
    t.append({QString::fromLatin1(kTriggerGiftSub), QStringLiteral("Geschenk-Abo"), 105,
              QString::fromLatin1(kTriggerGiftSub), {tiersField()}, eventArgs(kTriggerGiftSub)});
    t.append({QString::fromLatin1(kTriggerGiftBomb), QStringLiteral("Geschenk-Bündel"), 106,
              QString::fromLatin1(kTriggerGiftBomb),
              {tiersField(), countField(QStringLiteral("min"), 0), countField(QStringLiteral("max"), 0)},
              eventArgs(kTriggerGiftBomb)});
    t.append({QString::fromLatin1(kTriggerCheer), QStringLiteral("Cheer"), 102,
              QString::fromLatin1(kTriggerCheer),
              {countField(QStringLiteral("min"), 0), countField(QStringLiteral("max"), 0)}, eventArgs(kTriggerCheer)});
    t.append({QString::fromLatin1(kTriggerRaid), QStringLiteral("Raid"), 107,
              QString::fromLatin1(kTriggerRaid),
              {countField(QStringLiteral("min"), 0), countField(QStringLiteral("max"), 0)}, eventArgs(kTriggerRaid)});
    // Typnummer aus z0: bei Streamer.bot nicht dokumentiert (Schritt 0, C 20), bis dahin 0
    t.append({QString::fromLatin1(kTriggerStreamOnline), QStringLiteral("Stream online"), 0,
              QString::fromLatin1(kTriggerStreamOnline),
              {{QStringLiteral("skip_at_start"), FieldKind::Bool, true, {}}}, eventArgs(kTriggerStreamOnline)});
    t.append({QString::fromLatin1(kTriggerStreamOffline), QStringLiteral("Stream offline"), 155,
              QString::fromLatin1(kTriggerStreamOffline), {}, eventArgs(kTriggerStreamOffline)});
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
