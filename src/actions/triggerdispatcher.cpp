// Quellen:
//   Befehle (Location, Case Sensitive, Ignore Bot Account, Ignore Internal, Cooldown, Permissions):
//   Plan P14 2.6 nach https://docs.streamer.bot/guide/commands (Recherche von Lotse, 2026-11-09)
//   Zeitnachrichten: Plan P14 2.7 nach https://docs.streamer.bot/guide/core/timers
//   Auslöser und Filter: Plan P14 2.5, https://docs.streamer.bot/guide/triggers
//   Geteilter Chat (source_broadcaster_user_id null im eigenen Kanal):
//   https://dev.twitch.tv/docs/eventsub/reference/chat#channel-chat-message

#include "triggerdispatcher.h"

#include "fieldspec.h"
#include "triggertypes.h"

#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace dss::actions {

namespace {

const QString kCommandSource = QStringLiteral("twitch_message");

bool sameTiming(const Timer &a, const Timer &b)
{
    return a.repeat == b.repeat && a.intervalS == b.intervalS && a.random == b.random
           && a.randomMinS == b.randomMinS && a.randomMaxS == b.randomMaxS;
}

bool containsUser(const QList<UserRef> &users, const QString &userId)
{
    return std::any_of(users.cbegin(), users.cend(), [&](const UserRef &u) { return u.userId == userId; });
}

// Wert, gegen den min/max eines Auslösers prüft (Plan 2.5)
std::optional<double> countValue(const QString &type, const QJsonObject &data)
{
    if (type == QLatin1String(kTriggerGiftBomb))
        return data.value(QStringLiteral("total")).toDouble();
    if (type == QLatin1String(kTriggerCheer))
        return data.value(QStringLiteral("bits")).toDouble();
    if (type == QLatin1String(kTriggerRaid))
        return data.value(QStringLiteral("viewers")).toDouble();
    return std::nullopt;
}

} // namespace

void submit(ActionEngine &engine, const TriggerResult &result)
{
    for (const TriggerRejection &r : result.rejections)
        engine.addRecord(r.actionId, r.triggerType, r.state, r.reason);
    for (const TriggerHit &hit : result.hits)
        engine.enqueue({hit.actionId, hit.triggerType, hit.triggerId, hit.arguments, false});
}

Arguments testArguments(const Trigger &trigger)
{
    Arguments args;
    const QJsonObject values = trigger.fields.value(QStringLiteral("variables")).toObject();
    for (auto it = values.begin(); it != values.end(); ++it)
        args.insert(it.key(), it.value());
    return args;
}

TriggerDispatcher::TriggerDispatcher(ActionClock *clock, VariableStore *variables)
    : m_clock(clock)
    , m_variables(variables)
{
}

TriggerDispatcher::~TriggerDispatcher()
{
    for (const TimerState &s : std::as_const(m_timers)) {
        if (s.clockTimer)
            m_clock->stopTimer(s.clockTimer);
    }
}

void TriggerDispatcher::setConfig(const ActionsConfig &config)
{
    m_config = config;
    QSet<QUuid> active;
    for (const Timer &timer : config.timers) {
        if (!timer.enabled)
            continue;
        active.insert(timer.id);
        auto it = m_timers.find(timer.id);
        if (it != m_timers.end() && sameTiming(it->timer, timer)) {
            it->timer = timer; // Name, lines, only_live gelten beim nächsten Fälligwerden
            continue;
        }
        if (it != m_timers.end() && it->clockTimer)
            m_clock->stopTimer(it->clockTimer);
        TimerState &state = m_timers[timer.id];
        state = TimerState();
        state.timer = timer;
        scheduleTimer(state);
    }
    for (auto it = m_timers.begin(); it != m_timers.end();) {
        if (active.contains(it.key())) {
            ++it;
            continue;
        }
        if (it->clockTimer)
            m_clock->stopTimer(it->clockTimer);
        it = m_timers.erase(it);
    }
}

TriggerResult TriggerDispatcher::handleEvent(const TriggerEvent &event, const TriggerContext &context)
{
    TriggerResult result;
    const QString &type = event.type;
    const QJsonObject &data = event.data;
    if (type == QLatin1String(kTriggerStreamOnline))
        m_live = true;
    else if (type == QLatin1String(kTriggerStreamOffline))
        m_live = false;

    const bool chat = type == QLatin1String(kTriggerChatMessage) || type == QLatin1String(kTriggerFirstMessage);
    if (chat) {
        // geteilter Chat: nur Nachrichten aus dem eigenen Kanal (null = eigener Kanal)
        const QString source = data.value(QStringLiteral("source_broadcaster_id")).toString();
        if (!source.isEmpty() && source != context.broadcasterId)
            return result;
    }
    const Arguments args = eventArguments(event);
    const bool isBlocked = blocked();

    if (type == QLatin1String(kTriggerChatMessage)) {
        if (!context.internal) {
            for (TimerState &s : m_timers)
                ++s.lines;
        }
        handleChat(event, context, args, isBlocked, &result);
    } else if (!(chat && context.internal)) {
        for (const Action &action : std::as_const(m_config.actions)) {
            if (!action.enabled)
                continue;
            for (const Trigger &trigger : action.triggers) {
                if (trigger.enabled && trigger.type == type && filtersPass(trigger, event)) {
                    result.hits.append({action.id, trigger.id, type, args});
                    break;
                }
            }
        }
    }
    if (isBlocked)
        blockAll(&result);
    return result;
}

void TriggerDispatcher::handleChat(const TriggerEvent &event, const TriggerContext &context,
                                   const Arguments &chatArgs, bool blocked, TriggerResult *result)
{
    const QJsonObject &data = event.data;
    const QString text = data.value(QStringLiteral("text")).toString();
    const QString userId = data.value(QStringLiteral("user_id")).toString();
    const bool fromBot = data.value(QStringLiteral("is_bot_account")).toBool();

    // Befehle, die diese Nachricht auslöst, mit ihren Argumenten
    QHash<QUuid, Arguments> accepted;
    for (const Command &command : std::as_const(m_config.commands)) {
        if (!command.enabled || command.mode != QLatin1String("basic") || !command.sources.contains(kCommandSource))
            continue;
        if ((command.ignoreBotAccount && fromBot) || (command.ignoreInternal && context.internal))
            continue;
        const std::optional<CommandMatch> match = matchCommand(command, text);
        if (!match)
            continue;
        Arguments args = chatArgs;
        if (!blocked) {
            const QString reason = checkCommand(command, data, context.broadcasterId);
            if (!reason.isEmpty()) {
                for (const Action &action : std::as_const(m_config.actions)) {
                    if (!action.enabled)
                        continue;
                    for (const Trigger &t : action.triggers) {
                        if (t.enabled && t.type == QLatin1String(kTriggerCommand)
                            && t.fields.value(QStringLiteral("command_id")).toString() == uuidText(command.id)) {
                            result->rejections.append({action.id, t.type, RunState::Rejected, reason});
                            break;
                        }
                    }
                }
                continue;
            }
            const VariableStore::CommandCount count = m_variables->countCommand(
                command.id, userId, command.persistCounter, command.persistUserCounter, m_clock->now());
            args.insert(QStringLiteral("counter"), double(count.counter));
            args.insert(QStringLiteral("userCounter"), double(count.userCounter));
        }
        args.insert(QStringLiteral("command"), match->text);
        args.insert(QStringLiteral("commandId"), uuidText(command.id));
        args.insert(QStringLiteral("commandName"), command.name);
        args.insert(QStringLiteral("rawInput"), match->rawInput);
        static const QRegularExpression space(QStringLiteral("\\s+"));
        const QStringList words = match->rawInput.split(space, Qt::SkipEmptyParts);
        for (qsizetype i = 0; i < std::min<qsizetype>(words.size(), 10); ++i)
            args.insert(QStringLiteral("input%1").arg(i), words.at(i));
        accepted.insert(command.id, args);
    }

    for (const Action &action : std::as_const(m_config.actions)) {
        if (!action.enabled)
            continue;
        for (const Trigger &trigger : action.triggers) {
            if (!trigger.enabled)
                continue;
            if (trigger.type == QLatin1String(kTriggerChatMessage) && !context.internal) {
                result->hits.append({action.id, trigger.id, trigger.type, chatArgs});
                break;
            }
            if (trigger.type == QLatin1String(kTriggerCommand)) {
                QUuid id;
                if (parseUuidText(trigger.fields.value(QStringLiteral("command_id")).toString(), &id)
                    && accepted.contains(id)) {
                    result->hits.append({action.id, trigger.id, trigger.type, accepted.value(id)});
                    break;
                }
            }
        }
    }
}

std::optional<TriggerDispatcher::CommandMatch> TriggerDispatcher::matchCommand(const Command &command,
                                                                               const QString &message) const
{
    const Qt::CaseSensitivity cs = command.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    const QString msg = message.trimmed();
    const auto boundaryAfter = [&](qsizetype end) { return end == msg.size() || msg.at(end).isSpace(); };
    for (const QString &raw : command.commands) {
        const QString c = raw.trimmed();
        if (c.isEmpty())
            continue;
        if (command.location == QLatin1String("exact")) {
            if (msg.compare(c, cs) == 0)
                return CommandMatch{&command, c, QString()};
        } else if (command.location == QLatin1String("anywhere")) {
            // als ganzes Wort irgendwo in der Nachricht
            for (qsizetype i = msg.indexOf(c, 0, cs); i >= 0; i = msg.indexOf(c, i + 1, cs)) {
                if ((i == 0 || msg.at(i - 1).isSpace()) && boundaryAfter(i + c.size()))
                    return CommandMatch{&command, c, msg.mid(i + c.size()).trimmed()};
            }
        } else {
            // start: erstes Wort gleich dem Befehl, «!d» trifft nicht «!discord» (Plan 2.6)
            if (msg.startsWith(c, cs) && boundaryAfter(c.size()))
                return CommandMatch{&command, c, msg.mid(c.size()).trimmed()};
        }
    }
    return std::nullopt;
}

QString TriggerDispatcher::checkCommand(const Command &command, const QJsonObject &data, const QString &broadcasterId)
{
    const QString userId = data.value(QStringLiteral("user_id")).toString();
    // Der Kanal darf immer und hat keine Abklingzeit (Plan 2.6)
    if (data.value(QStringLiteral("role")).toInt() == 4 || (!broadcasterId.isEmpty() && userId == broadcasterId))
        return QString();

    const Permissions &p = command.permissions;
    bool listed = containsUser(p.users, userId);
    for (const QString &role : p.roles) {
        if ((role == QLatin1String("subscriber") && data.value(QStringLiteral("is_subscriber")).toBool())
            || (role == QLatin1String("vip") && data.value(QStringLiteral("is_vip")).toBool())
            || (role == QLatin1String("moderator") && data.value(QStringLiteral("is_moderator")).toBool()))
            listed = true;
    }
    for (const QUuid &g : p.groups) {
        if (const UserGroup *group = m_config.group(g); group && containsUser(group->users, userId))
            listed = true;
    }
    const bool everyone = p.roles.isEmpty() && p.groups.isEmpty() && p.users.isEmpty();
    if (p.grant == QLatin1String("deny") ? listed : (!everyone && !listed))
        return QStringLiteral("keine Berechtigung");

    const QDateTime now = m_clock->now();
    const auto remaining = [&](const QDateTime &last, int cooldownS) -> qint64 {
        if (!last.isValid() || cooldownS <= 0)
            return 0;
        const qint64 left = qint64(cooldownS) * 1000 - last.msecsTo(now);
        return left > 0 ? (left + 999) / 1000 : 0;
    };
    if (const qint64 s = remaining(m_globalUsed.value(command.id), command.globalCooldownS))
        return QStringLiteral("Abklingzeit Kanal: noch %1 s").arg(s);
    if (const qint64 s = remaining(m_userUsed.value(command.id).value(userId), command.userCooldownS))
        return QStringLiteral("Abklingzeit Person: noch %1 s").arg(s);
    m_globalUsed.insert(command.id, now);
    if (!userId.isEmpty())
        m_userUsed[command.id].insert(userId, now);
    return QString();
}

bool TriggerDispatcher::filtersPass(const Trigger &trigger, const TriggerEvent &event) const
{
    const QJsonObject &f = trigger.fields;
    const QJsonObject &d = event.data;
    if (f.contains(QStringLiteral("tiers"))) {
        const QJsonArray tiers = f.value(QStringLiteral("tiers")).toArray();
        if (!tiers.isEmpty() && !tiers.contains(QJsonValue(eventTier(d))))
            return false;
    }
    if (f.contains(QStringLiteral("min_months"))
        && d.value(QStringLiteral("months")).toDouble() < f.value(QStringLiteral("min_months")).toDouble())
        return false;
    if (const std::optional<double> value = countValue(trigger.type, d); value && f.contains(QStringLiteral("min"))) {
        const double min = f.value(QStringLiteral("min")).toDouble();
        const double max = f.value(QStringLiteral("max")).toDouble();
        if (trigger.type == QLatin1String(kTriggerCheer) && min == 0 && max > 0)
            return *value == max;
        if (*value < min || (max > 0 && *value > max))
            return false;
    }
    if (f.value(QStringLiteral("skip_at_start")).toBool() && d.value(QStringLiteral("at_start")).toBool())
        return false;
    return true;
}

void TriggerDispatcher::blockAll(TriggerResult *result) const
{
    const bool off = !m_config.enabled;
    const RunState state = off ? RunState::Rejected : RunState::Muted;
    const QString reason = off ? QStringLiteral("Chat-Bot aus") : QStringLiteral("stumm: Version im Einsatz läuft");
    for (const TriggerHit &hit : std::as_const(result->hits))
        result->rejections.append({hit.actionId, hit.triggerType, state, reason});
    for (TriggerRejection &r : result->rejections) {
        r.state = state;
        r.reason = reason;
    }
    result->hits.clear();
}

void TriggerDispatcher::scheduleTimer(TimerState &state)
{
    const Timer &t = state.timer;
    qint64 seconds = t.intervalS;
    if (t.random && t.randomMaxS >= t.randomMinS) {
        const double r = std::clamp(m_random ? m_random() : QRandomGenerator::global()->generateDouble(), 0.0,
                                    std::nextafter(1.0, 0.0));
        seconds = t.randomMinS + qint64(std::floor(r * double(t.randomMaxS - t.randomMinS + 1)));
    }
    seconds = std::max<qint64>(seconds, kMinTimerIntervalS);
    const QUuid id = t.id;
    state.clockTimer = m_clock->startTimer(seconds * 1000, [this, id] { timerDue(id); });
}

void TriggerDispatcher::timerDue(const QUuid &timerId)
{
    auto it = m_timers.find(timerId);
    if (it == m_timers.end())
        return;
    it->clockTimer = 0;
    const Timer timer = it->timer;
    // Bot aus oder nicht live oder zu wenig Chat: diesmal nicht (Plan 2.7)
    const bool fire = m_config.enabled && (!timer.onlyLive || m_live) && (timer.lines <= 0 || it->lines >= timer.lines);
    if (timer.repeat || !fire)
        scheduleTimer(*it);
    if (!fire)
        return;
    it->lines = 0;

    TriggerResult result;
    for (const Action &action : std::as_const(m_config.actions)) {
        if (!action.enabled)
            continue;
        for (const Trigger &trigger : action.triggers) {
            if (trigger.enabled && trigger.type == QLatin1String(kTriggerTimer)
                && trigger.fields.value(QStringLiteral("timer_id")).toString() == uuidText(timerId)) {
                result.hits.append({action.id, trigger.id, trigger.type, {{QStringLiteral("timerName"), timer.name}}});
                break;
            }
        }
    }
    if (m_muted)
        blockAll(&result);
    if (m_timerHandler && (!result.hits.isEmpty() || !result.rejections.isEmpty()))
        m_timerHandler(result);
}

} // namespace dss::actions
