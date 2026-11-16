#pragma once

#include "placeholders.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>

// Ereignis als Eingang der Auslöser (P14 b2). Gleicher Aufbau wie dss::events::Event aus P13 1.1
// (type, time, originId, data). Der Kern (b3) setzt das Ereignis des Flusses ohne Umweg in
// TriggerEvent um. Nur Qt Core.
//
// Typen, Felder in data und Argumentnamen stehen nur in der Typenliste von P13
// (src/events/eventtypes.h, P14 c6); hier wird nur zugeordnet.

namespace dss::actions {

struct TriggerEvent
{
    QString type;      // «twitch.chat_message», …
    QDateTime time;    // UTC
    QString originId;  // Twitch: message_id der Benachrichtigung
    QJsonObject data;
};

// Argumente eines Laufs aus einem Ereignis: je Feld das Argument aus der Typenliste, dazu die
// gebildeten. Felder mit null (anonym, nicht geteilt) ergeben kein Argument, ausser streak_months
// (monthStreak 0). Unbekannter Typ: keine Argumente.
Arguments eventArguments(const TriggerEvent &event);

// Feld tier als Text, auch wenn es als Zahl kommt (Filter tiers)
QString eventTier(const QJsonObject &data);

} // namespace dss::actions
