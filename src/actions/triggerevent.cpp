// Quellen:
//   Felder der Ereignisse und Argumentnamen: einzige Quelle ist die Typenliste von P13
//   (src/events/eventtypes.h, Plan P13 1.3 und 1.4, Schritt 0 Abschnitt C 19 in
//   docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md)
//   message_type «user_intro»: https://dev.twitch.tv/docs/eventsub/reference/chat#channel-chat-message

#include "triggerevent.h"

#include "eventtypes.h"

namespace dss::actions {

namespace events = dss::events;

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
    const events::EventType *type = events::findEventType(event.type);
    if (!type)
        return args;
    const QJsonObject &d = event.data;
    for (const events::EventField &field : type->fields) {
        const QJsonValue v = d.value(QLatin1String(field.name));
        if (!v.isNull() && !v.isUndefined())
            args.insert(QString::fromLatin1(field.argument), v);
        else if (QLatin1String(field.name) == QLatin1String("streak_months"))
            args.insert(QString::fromLatin1(field.argument), 0); // nicht geteilt: 0 wie Streamer.bot
    }
    for (const char *name : type->derived) {
        const QLatin1String arg(name);
        bool value = false;
        if (arg == QLatin1String(events::kArgFirstMessage))
            value = d.value(QStringLiteral("message_type")).toString() == QLatin1String("user_intro");
        else if (arg == QLatin1String(events::kArgStreakShared))
            value = d.value(QStringLiteral("streak_months")).isDouble();
        else if (arg == QLatin1String(events::kArgIsMultiMonth))
            value = d.value(QStringLiteral("duration_months")).toDouble() > 1;
        args.insert(QString(arg), value);
    }
    return args;
}

} // namespace dss::actions
