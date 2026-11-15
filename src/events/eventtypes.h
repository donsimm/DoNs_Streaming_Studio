#pragma once

#include <QString>
#include <QStringList>

#include <span>

// Typenliste des Ereignisflusses (P13, Plan 1.3 und 1.4). Jeder Typ mit Herkunft, Kennzeichen
// «Auslöser in P14» und seinen Pflichtfeldern in data; je Feld der Name des Arguments für das
// Aktionssystem (P14). Die Namen der Argumente folgen Streamer.bot, damit Texte wie
// «Danke für den Follow, %user%!» ohne Umschrift importiert werden.
//
// EventBus::publish prüft jedes Ereignis gegen diese Liste: unbekannter Typ, fehlendes oder
// zusätzliches Feld → abgewiesen. Pflichtfelder sind immer da; «oder null» heisst, der Wert darf
// null sein. So bleiben Code und docs/08 («Ereignisfluss») deckungsgleich.
//
// «Schritt 0 offen»: Name des Arguments ist nicht belegt (Doku von Streamer.bot eingeklappt), bis
// zur Prüfung in Schritt 0 ein eigener Name in der Schreibweise von Streamer.bot (Plan 1.4),
// meist der Name des Feldes in camelCase.
// Belegt sind: user, userName, role, tier (Daten des Nutzers), msgId (Doku «Command Triggered»).
// Quellen: https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/,
// https://dev.twitch.tv/docs/eventsub/eventsub-reference/,
// https://docs.streamer.bot/api/triggers/core/commands/command-triggered

namespace dss::events {

struct EventField
{
    const char *name;     // Feld in data
    const char *argument; // Argument im Aktionssystem (P14)
};

struct EventType
{
    const char *type;
    const char *origin;
    bool trigger; // Auslöser im Aktionssystem (P14), im Plan mit «A» markiert
    std::span<const EventField> fields;
};

namespace detail {

// user_id, user_login, user_name (Plan 1.3: «user_*»)
#define DSS_EVENT_USER_FIELDS                                                                    \
    {"user_id", "userId"}, /* Schritt 0 offen */                                                 \
        {"user_login", "userName"}, {"user_name", "user"}

inline constexpr EventField kChatMessage[] = {
    {"message_id", "msgId"},
    DSS_EVENT_USER_FIELDS,
    {"text", "message"},                         // Schritt 0 offen
    {"fragments", "fragments"},                  // Schritt 0 offen; je type, text, emote_id oder null
    {"color", "color"},                          // Schritt 0 offen
    {"badges", "badges"},                        // Schritt 0 offen; je set_id, id
    {"role", "role"},                            // 1 Zuschauer, 2 VIP, 3 Moderator, 4 Kanal; Werte 1 bis 3 Schritt 0 offen
    {"is_subscriber", "isSubscribed"},           // Schritt 0 offen
    {"is_vip", "isVip"},                         // Schritt 0 offen
    {"is_moderator", "isModerator"},             // Schritt 0 offen
    {"is_broadcaster", "isBroadcaster"},         // Schritt 0 offen
    {"is_bot_account", "isBotAccount"},          // Schritt 0 offen; Absender ist das angemeldete Bot-Konto
    {"bits", "bits"},                            // Schritt 0 offen; 0 ohne Cheer
    {"reply_to", "replyTo"},                     // Schritt 0 offen; message_id oder null
    {"reward_id", "rewardId"},                   // Schritt 0 offen; oder null
    {"message_type", "messageType"},             // Schritt 0 offen
    {"source_broadcaster_id", "sourceBroadcasterId"}, // Schritt 0 offen; oder null (geteilter Chat)
};

// Alle Felder von twitch.chat_message und first_ever (message_type = user_intro)
inline constexpr EventField kFirstMessage[] = {
    {"message_id", "msgId"},
    DSS_EVENT_USER_FIELDS,
    {"text", "message"},                         // Schritt 0 offen
    {"fragments", "fragments"},                  // Schritt 0 offen
    {"color", "color"},                          // Schritt 0 offen
    {"badges", "badges"},                        // Schritt 0 offen
    {"role", "role"},                            // Werte 1 bis 3 Schritt 0 offen
    {"is_subscriber", "isSubscribed"},           // Schritt 0 offen
    {"is_vip", "isVip"},                         // Schritt 0 offen
    {"is_moderator", "isModerator"},             // Schritt 0 offen
    {"is_broadcaster", "isBroadcaster"},         // Schritt 0 offen
    {"is_bot_account", "isBotAccount"},          // Schritt 0 offen
    {"bits", "bits"},                            // Schritt 0 offen
    {"reply_to", "replyTo"},                     // Schritt 0 offen
    {"reward_id", "rewardId"},                   // Schritt 0 offen
    {"message_type", "messageType"},             // Schritt 0 offen
    {"source_broadcaster_id", "sourceBroadcasterId"}, // Schritt 0 offen
    {"first_ever", "firstEver"},                 // Schritt 0 offen; eigener Name (Plan 1.4)
};

inline constexpr EventField kFollow[] = {
    DSS_EVENT_USER_FIELDS,
    {"followed_at", "followDate"}, // Schritt 0 offen; %followDate% in den Daten des Nutzers, Zusammenhang nicht belegt
};

inline constexpr EventField kSubscribe[] = {
    DSS_EVENT_USER_FIELDS,
    {"tier", "tier"},      // «1000», «2000», «3000»; Form des Werts bei Streamer.bot Schritt 0 offen
    {"is_gift", "isGift"}, // Schritt 0 offen
};

inline constexpr EventField kResubscribe[] = {
    DSS_EVENT_USER_FIELDS,
    {"tier", "tier"},                       // Form des Werts Schritt 0 offen
    {"text", "message"},                    // Schritt 0 offen
    {"months", "months"},                   // Schritt 0 offen
    {"streak_months", "streakMonths"},      // Schritt 0 offen; oder null
    {"duration_months", "durationMonths"},  // Schritt 0 offen
};

// user_* null bei anonym
inline constexpr EventField kGiftBomb[] = {
    DSS_EVENT_USER_FIELDS,
    {"is_anonymous", "isAnonymous"},        // Schritt 0 offen
    {"total", "total"},                     // Schritt 0 offen
    {"tier", "tier"},                       // Form des Werts Schritt 0 offen
    {"cumulative_total", "cumulativeTotal"}, // Schritt 0 offen; oder null
};

// user_* ist der Beschenkte; den Schenkenden liefert channel.subscribe nicht (Plan 1.3, Schritt 0).
// Bei Streamer.bot heisst der Beschenkte vermutlich anders als «user» (Schritt 0 offen).
inline constexpr EventField kGiftSub[] = {
    DSS_EVENT_USER_FIELDS,
    {"tier", "tier"}, // Form des Werts Schritt 0 offen
};

// user_* null bei anonym
inline constexpr EventField kCheer[] = {
    DSS_EVENT_USER_FIELDS,
    {"is_anonymous", "isAnonymous"}, // Schritt 0 offen
    {"bits", "bits"},            // Schritt 0 offen
    {"text", "message"},           // Schritt 0 offen
};

inline constexpr EventField kRaid[] = {
    {"from_user_id", "userId"},       // Schritt 0 offen
    {"from_user_login", "userName"},  // Schritt 0 offen
    {"from_user_name", "user"},       // Schritt 0 offen
    {"viewers", "viewers"},           // Schritt 0 offen
};

inline constexpr EventField kRewardRedeemed[] = {
    {"redemption_id", "redemptionId"}, // Schritt 0 offen
    DSS_EVENT_USER_FIELDS,
    {"user_input", "userInput"},       // Schritt 0 offen
    {"reward_id", "rewardId"},         // Schritt 0 offen
    {"reward_title", "rewardTitle"},   // Schritt 0 offen
    {"reward_cost", "rewardCost"},     // Schritt 0 offen
    {"status", "status"},              // Schritt 0 offen
};

inline constexpr EventField kStreamOnline[] = {
    {"started_at", "startedAt"}, // Schritt 0 offen
    {"kind", "kind"},            // Schritt 0 offen
    {"at_start", "atStart"},     // Schritt 0 offen; eigener Name (Plan 2.5)
};

// Eigene Namen: kein Auslöser in P14 und nicht Teil von Schritt 0.
inline constexpr EventField kObsSceneChanged[] = {
    {"scene_uuid", "sceneUuid"},
    {"scene_name", "sceneName"},
};

#undef DSS_EVENT_USER_FIELDS

} // namespace detail

inline constexpr EventType kEventTypes[] = {
    {"twitch.chat_message", "twitch", true, detail::kChatMessage},
    {"twitch.first_message", "twitch", true, detail::kFirstMessage},
    {"twitch.follow", "twitch", true, detail::kFollow},
    {"twitch.subscribe", "twitch", true, detail::kSubscribe},
    {"twitch.resubscribe", "twitch", true, detail::kResubscribe},
    {"twitch.gift_bomb", "twitch", true, detail::kGiftBomb},
    {"twitch.gift_sub", "twitch", true, detail::kGiftSub},
    {"twitch.cheer", "twitch", true, detail::kCheer},
    {"twitch.raid", "twitch", true, detail::kRaid},
    // Auslöser erst später (Kanalpunkte-Effekte, Entscheid 2026-11-15)
    {"twitch.reward_redeemed", "twitch", false, detail::kRewardRedeemed},
    {"twitch.stream_online", "twitch", true, detail::kStreamOnline},
    {"twitch.stream_offline", "twitch", true, {}},
    {"obs.scene_changed", "obs", false, detail::kObsSceneChanged},
};

// nullptr, wenn der Typ nicht in der Liste steht.
const EventType *findEventType(const QString &type);
// Alle Typen in der Reihenfolge der Liste.
QStringList eventTypeNames();

// Filter: genauer Typ («twitch.follow») oder Vorsilbe mit Punkt am Ende («twitch.»).
bool matchesFilter(const QString &type, const QString &filter);
bool matchesFilters(const QString &type, const QStringList &filters);
// Gültig: ein Typ aus der Liste oder eine Vorsilbe mit Punkt am Ende, zu der es einen Typ gibt.
bool isValidFilter(const QString &filter);

} // namespace dss::events
