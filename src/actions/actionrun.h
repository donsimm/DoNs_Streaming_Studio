#pragma once

#include "actionengine.h"

#include <memory>

// Ein Lauf (P14 b1, Plan 3.1): Zustandsmaschine über die verschachtelten Schritte einer Aktion.
// Nur für ActionEngine; Tests gehen über die Engine.

namespace dss::actions {

class ActionRun
{
public:
    ActionRun(ActionEngine *engine, const Action &action, const ActionEngine::StartRequest &request,
              const RunRecord &record);
    ~ActionRun();

    RunRecord &record() { return m_record; }
    const RunRecord &record() const { return m_record; }
    const Action &action() const { return m_action; }

    // Von der Engine: beginnt den Lauf (Zustand running)
    void start();
    // Beendet den Lauf mit state (Grenze, Abbruch). Nichts, wenn er schon beendet ist.
    void finish(RunState state, const QString &message = QString());
    // Hinweis zum ganzen Lauf (ohne Schritt)
    void note(const QString &message);

private:
    struct Frame
    {
        QList<Step> steps;
        QList<int> numbers; // Nummer ab 1 je Schritt in der ursprünglichen Liste
        qsizetype pos = 0;
        QList<int> prefix;  // Pfad des enthaltenden Schritts
        int depth = 0;      // Tiefe von run_action (sofort)
    };
    struct Current
    {
        QUuid id;
        QString type;
        QList<int> path;
        int depth = 0;
    };

    void resume();
    void loop();
    void pushFrame(const QList<Step> &steps, bool randomOne, const QList<int> &prefix, int depth);
    void execute(const Step &step, const QList<int> &path, int depth);
    void executeIf(const Step &step, const QList<int> &path, int depth);
    void executeRunAction(const Step &step, int depth);
    void executeGlobalGet(const Step &step);
    void executeGlobalSet(const Step &step);
    void executeDelay(const Step &step);
    void executeOverlay(const Step &step);

    // Wartet auf den Rückruf eines Dienstes. then: was nach einem Erfolg geschieht (Overlay mit
    // wait); ohne then geht der Lauf weiter.
    StepDone waitForService(std::function<void(const StepResult &)> then = {});
    void waitForTimer(qint64 ms, const QString &message);
    void log(StepLogEntry::Result result, const QString &message);

    QString text(const Step &step, const QString &field) const;
    QString placeholders(const QString &text) const;
    std::optional<VariableKey> variableKey(const Step &step, QString *error) const;
    VariableStore &variables();
    const VariableStore &variables() const;

    ActionEngine *m_engine;
    Action m_action;            // Stand bei Beginn des Laufs
    ActionEngine::StartRequest m_request;
    RunRecord m_record;
    Arguments m_args;
    QList<Frame> m_frames;
    Current m_current;
    QHash<QUuid, int> m_ordinal; // Kennung → Nummer in der Reihenfolge der Anzeige (stepIndex)
    std::unique_ptr<VariableStore> m_dryVariables;
    std::shared_ptr<int> m_alive; // Rückrufe nach dem Ende prüfen ihn über weak_ptr
    ActionClock::TimerId m_stepTimer = 0;
    ActionClock::TimerId m_limitTimer = 0;
    bool m_waiting = false;
    bool m_inLoop = false;
    bool m_resumeAgain = false;
    bool m_hadError = false;
};

} // namespace dss::actions
