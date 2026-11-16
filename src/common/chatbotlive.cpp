#include "chatbotlive.h"

#include "actionview.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <algorithm>

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
            return t.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
    }
    return QStringLiteral("–");
}

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

} // namespace dss
