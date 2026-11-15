#pragma once

#include "placeholders.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>

// Ereignis als Eingang der Auslöser (P14 b2). Gleicher Aufbau wie dss::events::Event aus P13 1.1
// (type, time, originId, data); dss_events wird hier nicht eingebunden, weil P13 auf einem eigenen
// Branch entsteht. Der Kern (b3) setzt das Ereignis des Flusses ohne Umweg in TriggerEvent um.
// Nur Qt Core.
//
// Typen und Felder in data nach Plan P13 1.3 (eigene Namen in snake_case, user_* = user_id,
// user_login, user_name). Die Argumente eines Laufs heissen wie bei Streamer.bot (Schritt-0-Bericht
// vom 2026-11-15, Abschnitt C 19; Plan P13 1.4).

namespace dss::actions {

struct TriggerEvent
{
    QString type;      // «twitch.chat_message», …
    QDateTime time;    // UTC
    QString originId;  // Twitch: message_id der Benachrichtigung
    QJsonObject data;
};

// Argumente eines Laufs aus einem Ereignis. Felder mit null (anonym, nicht geteilt) ergeben kein
// Argument. Unbekannter Typ: keine Argumente.
Arguments eventArguments(const TriggerEvent &event);

// Stufe wie Streamer.bot aus der Stufe von Twitch: «1000» → «tier 1», «2000» → «tier 2»,
// «3000» → «tier 3»; Unbekanntes bleibt. «prime» liefert P13 nicht (channel.subscribe kennt keine
// Prime-Abos als eigene Stufe, Schritt 0 A 3).
QString tierText(const QString &tier);
// Feld tier als Text, auch wenn es als Zahl kommt
QString eventTier(const QJsonObject &data);

} // namespace dss::actions
