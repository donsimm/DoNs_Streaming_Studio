#include "actionengine.h"

#include "actionrun.h"

#include <QRandomGenerator>

#include <algorithm>

namespace dss::actions {

QString runStateKey(RunState state)
{
    switch (state) {
    case RunState::Waiting: return QStringLiteral("waiting");
    case RunState::Running: return QStringLiteral("running");
    case RunState::Ok: return QStringLiteral("ok");
    case RunState::Break: return QStringLiteral("break");
    case RunState::Error: return QStringLiteral("error");
    case RunState::Rejected: return QStringLiteral("rejected");
    case RunState::Muted: return QStringLiteral("muted");
    case RunState::Dropped: return QStringLiteral("dropped");
    case RunState::Limit: return QStringLiteral("limit");
    case RunState::Cancelled: return QStringLiteral("cancelled");
    }
    return QString();
}

bool isFinished(RunState state)
{
    return state != RunState::Waiting && state != RunState::Running;
}

QString stepResultKey(StepLogEntry::Result result)
{
    switch (result) {
    case StepLogEntry::Result::Ok: return QStringLiteral("ok");
    case StepLogEntry::Result::Error: return QStringLiteral("error");
    case StepLogEntry::Result::Skipped: return QStringLiteral("skipped");
    case StepLogEntry::Result::DryRun: return QStringLiteral("dry_run");
    case StepLogEntry::Result::Note: return QStringLiteral("note");
    }
    return QString();
}

ActionEngine::ActionEngine(ActionClock *clock, VariableStore *variables, QObject *parent)
    : QObject(parent)
    , m_clock(clock)
    , m_variables(variables)
{
}

ActionEngine::~ActionEngine()
{
    // Läufe zuerst: ihr Destruktor hält die Zeitgeber der Uhr an
    m_runs.clear();
    m_graveyard.clear();
}

void ActionEngine::setConfig(const ActionsConfig &config)
{
    m_config = config;
    // Eine Warteschlange kann jetzt nicht mehr blockierend sein
    pump();
}

QUuid ActionEngine::normalizedQueue(const QUuid &queue) const
{
    // gelöschte Warteschlange = «Standard» (der Verweis-Check meldet sie, actionrefs.h)
    return m_config.queue(queue) ? queue : QUuid();
}

bool ActionEngine::queueBlocking(const QUuid &queue) const
{
    const Queue *q = m_config.queue(queue);
    return q && q->blocking;
}

bool ActionEngine::queueBusy(const QUuid &queue) const
{
    for (const QUuid &id : m_running) {
        if (m_runs.value(id)->record().queue == queue)
            return true;
    }
    return false;
}

QString ActionEngine::queueName(const QUuid &queue) const
{
    const Queue *q = m_config.queue(queue);
    return q ? q->name : QStringLiteral("Standard");
}

QUuid ActionEngine::enqueue(const StartRequest &request)
{
    const Action *action = m_config.action(request.actionId);
    if (!action)
        return QUuid();

    RunRecord record;
    record.runId = QUuid::createUuid();
    record.actionId = action->id;
    record.actionName = action->name;
    record.triggerType = request.triggerType;
    record.triggerId = request.triggerId;
    record.queue = normalizedQueue(action->queue);
    record.queueName = queueName(record.queue);
    record.dryRun = request.dryRun;
    record.state = RunState::Waiting;
    record.queuedAt = m_clock->now();

    auto run = std::make_shared<ActionRun>(this, *action, request, record);
    if (!action->queue.isNull() && record.queue.isNull())
        run->note(QStringLiteral("Warteschlange fehlt, läuft in «Standard»"));
    m_runs.insert(record.runId, run);
    m_waiting.append(record.runId);
    if (!action->excludeFromHistory)
        emit runStarted(run->record());

    // Höchstens kMaxWaitingPerQueue wartende je Warteschlange, ältere zuerst verworfen (Plan 3.3)
    const QList<QUuid> waiting = waitingRuns(record.queue);
    for (qsizetype i = 0; i + kMaxWaitingPerQueue < waiting.size(); ++i) {
        if (const std::shared_ptr<ActionRun> old = m_runs.value(waiting.at(i)))
            old->finish(RunState::Dropped,
                        QStringLiteral("Warteschlange «%1» voll (%2 wartende)").arg(record.queueName).arg(kMaxWaitingPerQueue));
    }
    pump();
    return record.runId;
}

QUuid ActionEngine::addRecord(const QUuid &actionId, const QString &triggerType, RunState state,
                              const QString &message)
{
    const Action *action = m_config.action(actionId);
    RunRecord record;
    record.runId = QUuid::createUuid();
    record.actionId = actionId;
    record.actionName = action ? action->name : QString();
    record.triggerType = triggerType;
    record.state = state;
    record.message = message;
    record.queuedAt = m_clock->now();
    record.finishedAt = record.queuedAt;
    if (action && action->excludeFromHistory)
        return record.runId;
    storeRecord(record);
    emit runFinished(record);
    return record.runId;
}

bool ActionEngine::cancel(const QUuid &runId, const QString &message)
{
    const std::shared_ptr<ActionRun> run = m_runs.value(runId);
    if (!run)
        return false;
    run->finish(RunState::Cancelled, message.isEmpty() ? QStringLiteral("abgebrochen") : message);
    return true;
}

int ActionEngine::cancelAction(const QUuid &actionId, const QString &message)
{
    return cancelActions({{actionId, message}});
}

int ActionEngine::cancelActions(const QHash<QUuid, QString> &messages)
{
    return cancelMatching([&messages](const RunRecord &record) { return messages.value(record.actionId); });
}

int ActionEngine::cancelAll(const QString &message)
{
    return cancelMatching([&message](const RunRecord &) { return message; });
}

int ActionEngine::cancelMatching(const std::function<QString(const RunRecord &)> &messageFor)
{
    // Zuerst die wartenden, dann die laufenden: finishRun ruft pump() auf, ein Lauf, der
    // noch wartet, würde sonst gestartet und gleich wieder abgebrochen (Ton, Schritte)
    QList<QPair<QUuid, QString>> targets;
    for (const QUuid &id : std::as_const(m_waiting)) {
        const QString message = messageFor(m_runs.value(id)->record());
        if (!message.isEmpty())
            targets.append({id, message});
    }
    for (auto it = m_runs.cbegin(); it != m_runs.cend(); ++it) {
        if (m_waiting.contains(it.key()))
            continue;
        const QString message = messageFor(it.value()->record());
        if (!message.isEmpty())
            targets.append({it.key(), message});
    }
    int count = 0;
    for (const auto &[id, message] : std::as_const(targets)) {
        if (cancel(id, message))
            ++count;
    }
    return count;
}

void ActionEngine::pauseQueue(const QUuid &queue)
{
    m_pausedQueues.insert(queue);
}

void ActionEngine::resumeQueue(const QUuid &queue)
{
    m_pausedQueues.remove(queue);
    pump();
}

int ActionEngine::clearQueue(const QUuid &queue)
{
    const QList<QUuid> waiting = waitingRuns(queue);
    for (const QUuid &id : waiting) {
        if (const std::shared_ptr<ActionRun> run = m_runs.value(id))
            run->finish(RunState::Cancelled, QStringLiteral("Warteschlange geleert"));
    }
    return int(waiting.size());
}

QList<QUuid> ActionEngine::waitingRuns(const QUuid &queue) const
{
    QList<QUuid> result;
    for (const QUuid &id : m_waiting) {
        if (m_runs.value(id)->record().queue == queue)
            result.append(id);
    }
    return result;
}

QList<RunRecord> ActionEngine::history() const
{
    QList<RunRecord> result = m_finished;
    QList<RunRecord> active;
    for (const std::shared_ptr<ActionRun> &run : m_runs) {
        if (!run->action().excludeFromHistory)
            active.append(run->record());
    }
    std::stable_sort(active.begin(), active.end(),
                     [](const RunRecord &a, const RunRecord &b) { return a.queuedAt < b.queuedAt; });
    return result + active;
}

std::optional<RunRecord> ActionEngine::record(const QUuid &runId) const
{
    if (const std::shared_ptr<ActionRun> run = m_runs.value(runId))
        return run->record();
    for (const RunRecord &r : m_finished) {
        if (r.runId == runId)
            return r;
    }
    return std::nullopt;
}

void ActionEngine::pump()
{
    if (m_pumping) {
        m_pumpAgain = true;
        return;
    }
    m_pumping = true;
    do {
        m_pumpAgain = false;
        for (qsizetype i = 0; i < m_waiting.size(); ++i) {
            if (m_running.size() >= kMaxRunningRuns)
                break;
            const std::shared_ptr<ActionRun> run = m_runs.value(m_waiting.at(i));
            const QUuid queue = run->record().queue;
            // angehalten: nur always_run (Plan 2.3); blockierend: einer nach dem anderen (Plan 2.8)
            if (m_pausedQueues.contains(queue) && !run->action().alwaysRun)
                continue;
            if (queueBlocking(queue) && queueBusy(queue))
                continue;
            m_waiting.removeAt(i);
            startRun(run.get());
            // startRun kann Läufe anlegen und beenden: von vorne
            m_pumpAgain = true;
            break;
        }
    } while (m_pumpAgain);
    m_pumping = false;
}

void ActionEngine::startRun(ActionRun *run)
{
    m_running.insert(run->record().runId);
    run->start();
}

void ActionEngine::finishRun(ActionRun *run)
{
    const QUuid id = run->record().runId;
    m_waiting.removeAll(id);
    m_running.remove(id);
    // nicht im eigenen Aufruf löschen: später über die Ereignisschleife
    if (m_graveyard.isEmpty())
        QMetaObject::invokeMethod(this, [this] { m_graveyard.clear(); }, Qt::QueuedConnection);
    m_graveyard.append(m_runs.take(id));
    if (!run->action().excludeFromHistory) {
        storeRecord(run->record());
        emit runFinished(run->record());
    }
    pump();
}

void ActionEngine::storeRecord(const RunRecord &record)
{
    m_finished.append(record);
    while (m_finished.size() > kRunHistorySize)
        m_finished.removeFirst();
}

void ActionEngine::notifyChanged(const ActionRun *run)
{
    if (!run->action().excludeFromHistory)
        emit runChanged(run->record());
}

double ActionEngine::random()
{
    return m_random ? m_random() : QRandomGenerator::global()->generateDouble();
}

} // namespace dss::actions
