#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QUuid>

// Ein Ereignis auf dem Fluss des Kerns (P13, Plan 1.1, Punkt 92). Nur Dinge, die geschehen sind;
// Handlungen gehen über die Schnittstelle des jeweiligen Dienstes. Felder je Typ: eventtypes.h.

namespace dss::events {

struct Event
{
    QUuid id;          // vom Fluss vergeben, wenn leer
    QString type;      // «twitch.chat_message», «obs.scene_changed», …
    QString origin;    // «twitch», «obs», später «actions», …; leer = aus der Typenliste
    QDateTime time;    // UTC, Zeitpunkt laut Herkunft (Twitch: message_timestamp), sonst Empfang
    QString originId;  // Kennung bei der Herkunft (Twitch: message_id), leer wenn keine
    QJsonObject data;  // Felder je Typ (eventtypes.h), nie Rohdaten oder Token

    bool operator==(const Event &) const = default;
};

// Form in der Schnittstelle (docs/08, «Ereignisfluss»): id, type, origin, time (ISO 8601, UTC, ms),
// origin_id, data.
QJsonObject eventToJson(const Event &event);
// false mit Meldung, wenn ein Grundfeld fehlt oder die falsche Art hat. Prüft nicht gegen die
// Typenliste (ein neueres Fenster kann ältere Kerne lesen und umgekehrt).
bool eventFromJson(const QJsonObject &object, Event *event, QString *error);

} // namespace dss::events
