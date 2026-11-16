#include "chatbotlive.h"

#include "actionview.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <utility>

namespace dss {

using actions::RunRecord;
using actions::RunState;

namespace {

QString withReason(const QString &text, const QString &reason)
{
    return reason.isEmpty() ? text : QStringLiteral("%1: %2").arg(text, reason);
}

// Fehlermeldung eines Laufs: Der Ablauf setzt bei «error» keine Meldung zum ganzen Lauf, die
// Fehler stehen bei den Schritten (actionrun.cpp). Gezeigt wird der erste; die Übersicht aus
// action_runs hat keine Schritte, dann bleibt es bei «Fehler».
QString errorMessage(const RunRecord &record)
{
    if (!record.message.isEmpty())
        return record.message;
    for (const actions::StepLogEntry &e : record.log) {
        if (e.result == actions::StepLogEntry::Result::Error && !e.message.isEmpty())
            return e.message;
    }
    return QString();
}

QString stateColor(RunState state)
{
    switch (state) {
    case RunState::Ok:
    case RunState::Break:
        return QStringLiteral("ok");
    case RunState::Rejected:
    case RunState::Muted:
    case RunState::Cancelled:
        return QStringLiteral("warning");
    case RunState::Error:
    case RunState::Dropped:
    case RunState::Limit:
        return QStringLiteral("error");
    case RunState::Waiting:
    case RunState::Running:
        break;
    }
    return QString();
}

QString timeText(const RunRecord &record)
{
    for (const QDateTime &t : {record.queuedAt, record.startedAt, record.finishedAt}) {
        if (t.isValid())
            return rowTimeText(t);
    }
    return rowTimeText(QDateTime());
}

// Graue Zeile zu einem Ereignis aus dem Bereich «Ereignisse»: «Follow: Name», «Raid: Name, 23 Zuschauer»
QString eventChatText(const events::Event &event)
{
    const EventRow row = eventRow(event);
    QString text = row.kind;
    if (!row.name.isEmpty() && row.name != QStringLiteral("—"))
        text += QStringLiteral(": ") + row.name;
    if (!row.details.isEmpty())
        text += QStringLiteral(", ") + row.details;
    return text;
}

// account in chat_send und actions_status (chatAccountKey in chatoutbox.h)
constexpr QLatin1String kAccountBot("bot");
constexpr QLatin1String kAccountBroadcaster("broadcaster");

} // namespace

QString runResultText(const RunRecord &record)
{
    QString text;
    switch (record.state) {
    case RunState::Waiting:
        text = QStringLiteral("wartet: Warteschlange %1")
                   .arg(record.queueName.isEmpty() ? QStringLiteral("Standard") : record.queueName);
        break;
    case RunState::Running:
        text = record.stepIndex > 0 && record.stepCount > 0
                   ? QStringLiteral("läuft, Schritt %1 von %2").arg(record.stepIndex).arg(record.stepCount)
                   : QStringLiteral("läuft");
        break;
    case RunState::Ok:
        text = record.startedAt.isValid() && record.finishedAt.isValid()
                   ? QStringLiteral("fertig (%1)")
                         .arg(durationText(std::max<qint64>(0, record.startedAt.msecsTo(record.finishedAt))))
                   : QStringLiteral("fertig");
        break;
    case RunState::Break:
        text = QStringLiteral("beendet");
        break;
    case RunState::Error:
        text = withReason(QStringLiteral("Fehler"), errorMessage(record));
        break;
    case RunState::Rejected:
        text = withReason(QStringLiteral("abgewiesen"), record.message);
        break;
    case RunState::Muted:
        text = QStringLiteral("stumm");
        break;
    case RunState::Dropped:
        text = withReason(QStringLiteral("verworfen"), record.message);
        break;
    case RunState::Limit:
        text = withReason(QStringLiteral("Grenze"), record.message);
        break;
    case RunState::Cancelled:
        // Ohne Grund setzt der Ablauf «abgebrochen» als Meldung (actionengine.cpp, cancel)
        text = withReason(QStringLiteral("abgebrochen"),
                          record.message == QLatin1String("abgebrochen") ? QString() : record.message);
        break;
    }
    return record.dryRun ? QStringLiteral("Probelauf · ") + text : text;
}

QString runTriggerText(const QString &triggerType)
{
    if (const actions::TriggerTypeInfo *info = actions::triggerType(triggerType))
        return info->label;
    // Eigener Lauf aus dem Schritt «Aktion ausführen» (actionrun.cpp)
    if (const actions::StepTypeInfo *info = actions::stepType(triggerType))
        return info->label;
    return triggerType;
}

RunRow runRow(const RunRecord &record)
{
    RunRow row;
    row.runId = record.runId;
    row.actionId = record.actionId;
    row.time = timeText(record);
    row.action = record.actionName;
    row.trigger = runTriggerText(record.triggerType);
    row.result = runResultText(record);
    row.state = stateColor(record.state);
    row.cancellable = record.state == RunState::Waiting || record.state == RunState::Running;
    row.isError = row.state == QLatin1String("error");
    return row;
}

void RunRows::reset(const QList<RunRecord> &records)
{
    m_rows.clear();
    for (const RunRecord &r : records)
        apply(r);
}

RunRows::Change RunRows::apply(const RunRecord &record)
{
    const RunRow row = runRow(record);
    Change change;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].runId != record.runId)
            continue;
        const bool wasVisible = visible(m_rows[i]);
        m_rows[i] = row;
        if (visible(row)) {
            change.index = visibleIndex(i);
            change.appended = !wasVisible;
        }
        return change;
    }
    if (m_rows.size() >= kMaxRows) {
        if (visible(m_rows.first()))
            change.removedFront = 1;
        m_rows.removeFirst();
    }
    m_rows.append(row);
    if (visible(row)) {
        change.index = visibleIndex(int(m_rows.size()) - 1);
        change.appended = true;
    }
    return change;
}

void RunRows::setOnlyErrors(bool onlyErrors)
{
    m_onlyErrors = onlyErrors;
}

QList<RunRow> RunRows::visibleRows() const
{
    QList<RunRow> result;
    for (const RunRow &r : m_rows) {
        if (visible(r))
            result.append(r);
    }
    return result;
}

const RunRow *RunRows::row(const QUuid &runId) const
{
    for (const RunRow &r : m_rows) {
        if (r.runId == runId)
            return &r;
    }
    return nullptr;
}

int RunRows::visibleIndex(int index) const
{
    int n = 0;
    for (int i = 0; i < index; ++i) {
        if (visible(m_rows[i]))
            ++n;
    }
    return n;
}

std::optional<ChatRow> chatRow(const events::Event &event, const std::optional<TwitchStatusInfo> &twitch)
{
    ChatRow row;
    row.time = rowTimeText(event.time);
    const QJsonObject &d = event.data;
    const QString &t = event.type;
    if (t == QLatin1String("twitch.chat_message")) {
        if (d.value(QStringLiteral("is_bot_account")).toBool())
            row.badges.append(QStringLiteral("Bot"));
        if (d.value(QStringLiteral("is_broadcaster")).toBool())
            row.badges.append(QStringLiteral("Kanal"));
        if (d.value(QStringLiteral("is_moderator")).toBool())
            row.badges.append(QStringLiteral("Mod"));
        row.name = eventRow(event).name;
        row.color = d.value(QStringLiteral("color")).toString();
        row.text = d.value(QStringLiteral("text")).toString();
        row.messageId = d.value(QStringLiteral("message_id")).toString();
        return row;
    }
    if (t == QLatin1String("twitch.chat_sent")) {
        // Eigene Nachricht: Name aus twitch_status; die Chatfarbe kennt erst ein Echo
        const bool bot = d.value(QStringLiteral("account")).toString() == kAccountBot;
        row.badges.append(bot ? QStringLiteral("Bot") : QStringLiteral("Kanal"));
        if (twitch)
            row.name = bot ? twitch->bot.login : twitch->channel.login;
        if (row.name.isEmpty())
            row.name = QStringLiteral("—");
        row.text = d.value(QStringLiteral("text")).toString();
        row.messageId = d.value(QStringLiteral("message_id")).toString();
        return row;
    }
    row.isEvent = true;
    if (t == QLatin1String("twitch.chat_dropped")) {
        row.text = withReason(QStringLiteral("Nicht gesendet"), d.value(QStringLiteral("message")).toString());
        return row;
    }
    if (t == QLatin1String("actions.run_started")) {
        RunRecord record;
        QString error;
        if (!runFromJson(d, &record, &error))
            return std::nullopt;
        row.text = QStringLiteral("Aktion «%1» ausgelöst").arg(record.actionName);
        if (record.dryRun)
            row.text.prepend(QStringLiteral("Probelauf · "));
        return row;
    }
    if (chatBotEventFilters().contains(t)) {
        row.text = eventChatText(event);
        return row;
    }
    return std::nullopt;
}

void ChatRows::reset(const QList<events::Event> &events, const std::optional<TwitchStatusInfo> &twitch)
{
    m_rows.clear();
    for (const events::Event &e : events)
        add(e, twitch);
}

RowsAppend ChatRows::add(const events::Event &event, const std::optional<TwitchStatusInfo> &twitch)
{
    RowsAppend change;
    const std::optional<ChatRow> row = chatRow(event, twitch);
    if (!row || (!row->messageId.isEmpty() && contains(row->messageId)))
        return change;
    if (m_rows.size() >= kMaxRows) {
        m_rows.removeFirst();
        change.removedFront = 1;
    }
    m_rows.append(*row);
    change.appended = true;
    return change;
}

bool ChatRows::contains(const QString &messageId) const
{
    return std::any_of(m_rows.cbegin(), m_rows.cend(), [&](const ChatRow &r) { return r.messageId == messageId; });
}

namespace {

std::optional<ActionsAccountStatus> accountStatus(const QString &account, const std::optional<ActionsStatus> &status)
{
    if (status) {
        for (const ActionsAccountStatus &a : status->accounts) {
            if (a.account == account)
                return a;
        }
    }
    return std::nullopt;
}

} // namespace

QString chatSendProblem(const QString &text, const QString &account, const std::optional<ActionsStatus> &status)
{
    const QString trimmed = text.trimmed();
    if (account != kAccountBot && account != kAccountBroadcaster)
        return QStringLiteral("account muss bot oder broadcaster sein");
    if (trimmed.isEmpty())
        return QStringLiteral("Die Nachricht ist leer");
    if (text.size() > kChatSendMaxLength)
        return QStringLiteral("Die Nachricht ist länger als %1 Zeichen").arg(kChatSendMaxLength);
    if (trimmed.startsWith(QLatin1Char('/')) || trimmed.startsWith(QLatin1Char('.')))
        return QStringLiteral("Twitch-Befehle sind nicht möglich");
    if (!status)
        return QString();
    // unavailable: Kern ohne Aktionen, nimmt chat_send nicht an; file_error sendet trotzdem
    if (status->state == QLatin1String("unavailable"))
        return status->message;
    if (status->state == QLatin1String("muted"))
        return QStringLiteral("stumm: Version im Einsatz läuft");
    const std::optional<ActionsAccountStatus> a = accountStatus(account, status);
    if (a && !a->canSend)
        return a->message;
    return QString();
}

ChatSendView chatSendView(const QString &text, const QString &account, const std::optional<ActionsStatus> &status)
{
    ChatSendView v;
    v.counter = QStringLiteral("%1 / %2").arg(text.size()).arg(kChatSendMaxLength);
    v.tooLong = text.size() > kChatSendMaxLength;
    for (const auto &[key, label] : {std::pair{kAccountBot, QStringLiteral("als Bot")},
                                     std::pair{kAccountBroadcaster, QStringLiteral("als Kanal")}}) {
        ChatSendAccount choice{key, label, true, QString()};
        if (const std::optional<ActionsAccountStatus> a = accountStatus(key, status); a && !a->canSend) {
            choice.enabled = false;
            choice.toolTip = a->message;
        }
        v.accounts.append(choice);
    }
    // Leerer Text: «Senden» aus, ohne Meldung
    if (!text.trimmed().isEmpty())
        v.problem = chatSendProblem(text, account, status);
    v.sendEnabled = !text.trimmed().isEmpty() && v.problem.isEmpty();
    return v;
}

EditResult chatSendEdit(const QString &text, const QString &account, const std::optional<ActionsStatus> &status)
{
    EditResult r;
    r.problem = chatSendProblem(text, account, status);
    if (r.problem.isEmpty())
        r.edits.append({QString::fromLatin1(msg::kChatSend),
                        QJsonObject{{QStringLiteral("text"), text}, {QStringLiteral("account"), QString(account)},
                                    {QStringLiteral("reply_to"), QJsonValue::Null}}});
    return r;
}

} // namespace dss
