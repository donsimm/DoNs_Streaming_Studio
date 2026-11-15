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
// Argumentnamen nach Schritt 0 (docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md,
// Abschnitt C). Ohne Kommentar: dort belegt. «eigener Name»: bei Streamer.bot nicht belegt oder ohne
// Gegenstück; ein eigener Name in der Schreibweise von Streamer.bot (Plan 1.4). «abgeleitet»: aus
// den Variablengruppen von Streamer.bot, beim Auslöser nicht einzeln belegt.
//
// Abweichungen vom Plan 1.3 nach Schritt 0:
// - tier: Twitch liefert 1000/2000/3000 (Feld tier, als Zahl, Argument subscriptionTier);
//   Streamer.bot nennt bei den Sub-Auslösern tier als Text «tier 1» … (Feld tier_name).
// - twitch.gift_sub: Bei Streamer.bot ist user der Schenkende und recipientUser der Beschenkte.
//   channel.subscribe nennt nur den Beschenkten → recipient_user_*; user_* bleibt null.
// - twitch.chat_message: is_highlight (Argument isHighlight) aus message_type.
// - twitch.stream_offline: ended_at (Argument endedAt), Zeitpunkt der Nachricht von Twitch.
//
// Quellen: https://dev.twitch.tv/docs/eventsub/reference/ (Unterseiten /chat, /channel,
// /channel-points, /stream), https://github.com/Streamerbot/docs/tree/main/streamerbot/3.api/.variables,
// https://docs.streamer.bot/api/triggers/twitch/,
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

// user_id, user_login, user_name (Plan 1.3: «user_*»); Gruppe «User» bei Streamer.bot
#define DSS_EVENT_USER_FIELDS                                                                    \
    {"user_id", "userId"}, {"user_login", "userName"}, {"user_name", "user"}

// tier: Zahl 1000, 2000, 3000 (Twitch); tier_name: «tier 1», «tier 2», «tier 3» (Streamer.bot,
// dort auch «prime»; Prime erkennt EventSub bei channel.subscribe nicht)
#define DSS_EVENT_TIER_FIELDS {"tier", "subscriptionTier"}, {"tier_name", "tier"}

#define DSS_EVENT_CHAT_FIELDS                                                                    \
    {"message_id", "msgId"}, DSS_EVENT_USER_FIELDS, {"text", "message"},                         \
        {"fragments", "fragments"},                     /* eigener Name; je type, text, emote_id oder null */ \
        {"color", "color"}, {"badges", "badges"},       /* badges: eigener Name; je set_id, id */ \
        {"role", "role"},                               /* 1 Zuschauer, 2 VIP, 3 Moderator, 4 Kanal */ \
        {"is_subscriber", "isSubscribed"}, {"is_vip", "isVip"}, {"is_moderator", "isModerator"}, \
        {"is_broadcaster", "isBroadcaster"},            /* eigener Name */                       \
        {"is_bot_account", "isBotAccount"},             /* eigener Name; Absender ist das angemeldete Bot-Konto */ \
        {"bits", "bits"},                               /* 0 ohne Cheer */                       \
        {"reply_to", "replyTo"},                        /* eigener Name; message_id oder null */ \
        {"reward_id", "rewardId"},                      /* eigener Name; oder null */            \
        {"message_type", "messageType"},                /* eigener Name */                       \
        {"is_highlight", "isHighlight"},                /* message_type channel_points_highlighted */ \
        {"source_broadcaster_id", "sourceBroadcasterId"} /* eigener Name; oder null (geteilter Chat) */

inline constexpr EventField kChatMessage[] = {DSS_EVENT_CHAT_FIELDS};

// Alle Felder von twitch.chat_message und first_ever (message_type = user_intro)
inline constexpr EventField kFirstMessage[] = {
    DSS_EVENT_CHAT_FIELDS,
    {"first_ever", "firstEver"}, // eigener Name (Plan 1.4)
};

inline constexpr EventField kFollow[] = {
    DSS_EVENT_USER_FIELDS,
    {"followed_at", "followDate"}, // eigener Name; %followDate% in den Daten des Nutzers, Herkunft nicht belegt
};

// Nur Abos ohne Geschenk; Geschenke gehen als twitch.gift_sub hinaus (Plan 1.3).
inline constexpr EventField kSubscribe[] = {
    DSS_EVENT_USER_FIELDS,
    DSS_EVENT_TIER_FIELDS,
    {"is_gift", "isGift"}, // eigener Name; hier immer false
};

inline constexpr EventField kResubscribe[] = {
    DSS_EVENT_USER_FIELDS,
    DSS_EVENT_TIER_FIELDS,
    {"text", "message"},
    {"months", "cumulative"},
    {"streak_months", "monthStreak"},          // oder null (nicht geteilt)
    {"duration_months", "multiMonthDuration"},
};

// user_* null bei anonym
inline constexpr EventField kGiftBomb[] = {
    DSS_EVENT_USER_FIELDS,
    {"is_anonymous", "anonymous"},
    {"total", "gifts"},
    DSS_EVENT_TIER_FIELDS,
    {"cumulative_total", "totalGifts"}, // oder null (anonym oder nicht geteilt)
};

// Beschenkter in recipient_user_*. Den Schenkenden liefert channel.subscribe nicht (Schritt 0, A3):
// user_* ist null.
inline constexpr EventField kGiftSub[] = {
    DSS_EVENT_USER_FIELDS,
    {"recipient_user_id", "recipientId"},
    {"recipient_user_login", "recipientUserName"},
    {"recipient_user_name", "recipientUser"},
    DSS_EVENT_TIER_FIELDS,
};

// user_* null bei anonym
inline constexpr EventField kCheer[] = {
    DSS_EVENT_USER_FIELDS,
    {"is_anonymous", "anonymous"}, // eigener Name (bei Cheer nicht belegt, bei Geschenken «anonymous»)
    {"bits", "bits"},
    {"text", "message"},
};

inline constexpr EventField kRaid[] = {
    {"from_user_id", "userId"},      // abgeleitet
    {"from_user_login", "userName"}, // abgeleitet
    {"from_user_name", "user"},      // abgeleitet
    {"viewers", "viewers"},
};

inline constexpr EventField kRewardRedeemed[] = {
    {"redemption_id", "redemptionId"}, // eigener Name
    DSS_EVENT_USER_FIELDS,
    {"user_input", "userInput"},       // eigener Name
    {"reward_id", "rewardId"},         // eigener Name
    {"reward_title", "rewardTitle"},   // eigener Name
    {"reward_cost", "rewardCost"},     // eigener Name
    {"status", "status"},              // eigener Name
};

inline constexpr EventField kStreamOnline[] = {
    {"started_at", "startedAt"},
    {"kind", "kind"},        // eigener Name (type bei Twitch: live, playlist, …)
    {"at_start", "atStart"}, // eigener Name (Plan 2.5)
};

inline constexpr EventField kStreamOffline[] = {
    {"ended_at", "endedAt"}, // Zeitpunkt der Nachricht von Twitch (stream.offline hat kein eigenes Feld)
};

// Eigene Namen: kein Auslöser in P14.
inline constexpr EventField kObsSceneChanged[] = {
    {"scene_uuid", "sceneUuid"},
    {"scene_name", "sceneName"},
};

#undef DSS_EVENT_CHAT_FIELDS
#undef DSS_EVENT_TIER_FIELDS
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
    {"twitch.stream_offline", "twitch", true, detail::kStreamOffline},
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
