#pragma once

#include "fieldspec.h"

#include <QList>
#include <QString>
#include <QStringList>

// Typen der Auslöser (P14 a, Plan 2.5) mit Nummer bei Streamer.bot, Ereignis des Ereignisflusses
// (Plan P13 1.3) und den Argumenten, die ein Lauf bekommt (Plan P13 1.4). Nur Qt Core (CLAUDE.md
// Regel 9). Die Argumente aus Ereignissen stehen nur in der Typenliste von P13
// (src/events/eventtypes.h, P14 c6); wie ein Ereignis zu Argumenten wird, steht in triggerevent.cpp.
//
// Quellen: Plan P14 2.5 (Recherche von Lotse vom 2026-11-09, https://docs.streamer.bot/guide/triggers,
// https://docs.streamer.bot/api/triggers/core/commands/command-triggered), Plan P13 1.3 und 1.4,
// https://github.com/Streamerbot/docs/tree/main/streamerbot/3.api/.variables,
// https://docs.streamer.bot/api/triggers/twitch/

namespace dss::actions {

struct TriggerTypeInfo
{
    QString key;            // Typ in aktionen.json
    QString label;          // Bezeichnung auf Deutsch (Meldungen; vorläufig, Editor in e1)
    int sbType = 0;         // Nummer bei Streamer.bot; 0 = Typnummer aus z0 (nicht dokumentiert)
    QString eventType;      // Ereignis aus P13 1.3, das den Auslöser speist; leer = keins
    QList<FieldSpec> filters; // Filter ohne die gemeinsamen Felder (id, type, enabled)
    QStringList arguments;  // zusätzliche Argumente eines Laufs (Plan 2.5)
};

inline constexpr char kTriggerCommand[] = "command";
inline constexpr char kTriggerTimer[] = "timer";
inline constexpr char kTriggerTest[] = "test";
inline constexpr char kTriggerChatMessage[] = "twitch.chat_message";
inline constexpr char kTriggerFirstMessage[] = "twitch.first_message";
inline constexpr char kTriggerFollow[] = "twitch.follow";
inline constexpr char kTriggerSubscribe[] = "twitch.subscribe";
inline constexpr char kTriggerResubscribe[] = "twitch.resubscribe";
inline constexpr char kTriggerGiftSub[] = "twitch.gift_sub";
inline constexpr char kTriggerGiftBomb[] = "twitch.gift_bomb";
inline constexpr char kTriggerCheer[] = "twitch.cheer";
inline constexpr char kTriggerRaid[] = "twitch.raid";
inline constexpr char kTriggerStreamOnline[] = "twitch.stream_online";
inline constexpr char kTriggerStreamOffline[] = "twitch.stream_offline";

// Alle Typen in der Reihenfolge des Plans
const QList<TriggerTypeInfo> &triggerTypes();
// nullptr, wenn der Typ unbekannt ist
const TriggerTypeInfo *triggerType(const QString &key);

// Stufen der Abos im Filter tiers wie in P13 1.3 (Feld tier, Zahl als Text). Das Argument tier hat
// dagegen die Form von Streamer.bot («tier 1»), subscriptionTier die Zahl (Schritt 0, C 19).
QStringList subscriptionTiers();

// Allgemeine Argumente in jedem Lauf (Plan 2.5)
QStringList commonArguments();

} // namespace dss::actions
