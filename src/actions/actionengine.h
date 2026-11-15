#pragma once

#include "actionclock.h"
#include "actionmodel.h"
#include "actionservices.h"
#include "placeholders.h"
#include "variables.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QUuid>

#include <functional>
#include <memory>
#include <optional>

// Ablauf des Aktionssystems (P14 b1, Plan 3): Läufe als Zustandsmaschine im Hauptthread des
// Kerns, Warteschlangen und Grenzen (3.3). Nur Qt Core, kein Netz, kein protocol.h (Regel 9, 14).
//
// - Ein Lauf arbeitet mit dem Stand der Aktion bei seinem Beginn (Plan 3.1); setConfig gilt ab
//   dem nächsten Lauf.
// - Schritte mit Wartezeit (Pause, Dienste nach aussen) geben die Kontrolle ab und machen bei
//   ihrer Antwort weiter. Es wird nie geschlafen.
// - Fehler eines Schritts: Eintrag im Lauf, der Lauf geht weiter. Nur «Abbruch» und die Grenzen
//   beenden ihn (Plan 3.1).
// - Probelauf (Plan 3.6): Schritte, die nach Twitch oder OBS schreiben (Chat, Ansage, Shoutout,
//   OBS-Quelle), werden nur gemeldet. Benutzerinfo, Overlay, Ton und Pause laufen echt, Variablen in
//   einem eigenen flüchtigen Satz (Kopie beim Beginn des Laufs).

namespace dss::actions {

// Grenzen (Plan 3.3)
inline constexpr int kMaxImmediateDepth = 8;          // run_action sofort, verschachtelt
inline constexpr int kMaxStepsPerRun = 2000;          // ausgeführte Schritte je Lauf
inline constexpr int kMaxRunningRuns = 32;            // gleichzeitige Läufe
inline constexpr int kMaxWaitingPerQueue = 50;        // wartende Läufe je Warteschlange
inline constexpr qint64 kMaxRunDurationMs = 15 * 60 * 1000; // Dauer eines Laufs ab Beginn
inline constexpr int kRunHistorySize = 200;           // beendete Läufe im Speicher (Plan 3.5)

// Zustände eines Laufs (Plan 3.5). Rejected und Muted kommen nicht aus dem Ablauf, sondern aus
// den Auslösern bzw. dem Kern (addRecord).
enum class RunState {
    Waiting,
    Running,
    Ok,
    Break,
    Error,
    Rejected,
    Muted,
    Dropped,
    Limit,
    Cancelled,
};
QString runStateKey(RunState state); // "waiting", "running", "ok", …
bool isFinished(RunState state);

struct StepLogEntry
{
    enum class Result { Ok, Error, Skipped, DryRun, Note };

    QDateTime time;
    QUuid stepId;      // null bei Hinweisen zum ganzen Lauf
    QString type;
    QList<int> path;   // Nummern ab 1 je Ebene innerhalb der Aktion, die den Schritt enthält
    int depth = 0;     // Tiefe von run_action (sofort); 0 = Aktion des Laufs
    Result result = Result::Ok;
    QString message;
};
QString stepResultKey(StepLogEntry::Result result); // "ok", "error", "skipped", "dry_run", "note"

struct RunRecord
{
    QUuid runId;
    QUuid actionId;
    QString actionName;
    QString triggerType;   // Typ des Auslösers (triggertypes.h) oder «run_action»
    QUuid triggerId;       // null, wenn kein Auslöser der Aktion (run_action, Test)
    QUuid queue;           // null = «Standard»
    QString queueName;
    bool dryRun = false;
    RunState state = RunState::Waiting;
    QString message;       // Grund bei rejected, muted, dropped, limit, cancelled
    QDateTime queuedAt;
    QDateTime startedAt;
    QDateTime finishedAt;
    int stepIndex = 0;     // «Schritt x von y»: Nummer des aktuellen Schritts der Aktion in der
    int stepCount = 0;     // Reihenfolge der Anzeige, 0 = noch keiner; y = Anzahl aller Schritte
    int stepsExecuted = 0;
    QList<StepLogEntry> log;
};

class ActionRun;

class ActionEngine : public QObject
{
    Q_OBJECT
public:
    // clock und variables gehören dem Aufrufer und leben länger als die Engine.
    ActionEngine(ActionClock *clock, VariableStore *variables, QObject *parent = nullptr);
    ~ActionEngine() override;

    void setConfig(const ActionsConfig &config);
    const ActionsConfig &config() const { return m_config; }
    void setServices(const ActionServices &services) { m_services = services; }
    const ActionServices &services() const { return m_services; }
    // Zufallszahl in [0, 1) für «zufällig einen» und die zufällige Pause; Standard
    // QRandomGenerator::global()
    void setRandomSource(std::function<double()> random) { m_random = std::move(random); }
    // Argumente aus dem Kern für jeden Lauf (broadcastUser, broadcastUserName, broadcastUserId,
    // isLive; Plan 2.5)
    void setBaseArguments(std::function<Arguments()> base) { m_baseArguments = std::move(base); }

    struct StartRequest
    {
        QUuid actionId;
        QString triggerType;
        QUuid triggerId;
        Arguments arguments;
        bool dryRun = false;
    };
    // Legt einen Lauf in der Warteschlange der Aktion an und startet ihn, wenn möglich, sofort.
    // Unbekannte Aktion: null. Ob die Aktion aktiv ist, prüfen die Auslöser, nicht die Engine.
    QUuid enqueue(const StartRequest &request);
    // Eintrag ohne Lauf, zum Beispiel ein abgewiesener Befehl oder «stumm» (Plan 3.4, 3.5)
    QUuid addRecord(const QUuid &actionId, const QString &triggerType, RunState state,
                    const QString &message);
    // Wartende Läufe werden verworfen, laufende beendet. false, wenn der Lauf nicht (mehr) läuft.
    bool cancel(const QUuid &runId, const QString &message = QString());

    // Warteschlangen (Plan 2.8); null = «Standard»
    void pauseQueue(const QUuid &queue);
    void resumeQueue(const QUuid &queue);
    bool isQueuePaused(const QUuid &queue) const { return m_pausedQueues.contains(queue); }
    // Verwirft die wartenden Läufe (Zustand cancelled), Ergebnis: Anzahl
    int clearQueue(const QUuid &queue);
    QList<QUuid> waitingRuns(const QUuid &queue) const;
    int runningCount() const { return int(m_running.size()); }

    // Laufende, wartende und die letzten kRunHistorySize beendeten Läufe, älteste zuerst
    QList<RunRecord> history() const;
    std::optional<RunRecord> record(const QUuid &runId) const;

signals:
    void runStarted(const dss::actions::RunRecord &record); // neu angelegt (wartend oder laufend)
    void runChanged(const dss::actions::RunRecord &record);
    void runFinished(const dss::actions::RunRecord &record);

private:
    friend class ActionRun;

    QUuid normalizedQueue(const QUuid &queue) const;
    bool queueBlocking(const QUuid &queue) const;
    bool queueBusy(const QUuid &queue) const;
    void pump();
    void startRun(ActionRun *run);
    void finishRun(ActionRun *run);
    void storeRecord(const RunRecord &record);
    void notifyChanged(const ActionRun *run);
    double random();
    QString queueName(const QUuid &queue) const;

    ActionClock *m_clock;
    VariableStore *m_variables;
    ActionsConfig m_config;
    ActionServices m_services;
    std::function<double()> m_random;
    std::function<Arguments()> m_baseArguments;

    QHash<QUuid, std::shared_ptr<ActionRun>> m_runs; // wartend und laufend
    QList<QUuid> m_waiting;                          // Reihenfolge des Eintreffens
    QSet<QUuid> m_running;
    QSet<QUuid> m_pausedQueues;
    QList<RunRecord> m_finished;                      // ohne exclude_from_history
    QList<std::shared_ptr<ActionRun>> m_graveyard;   // beendet, gelöscht ausserhalb ihres Aufrufs
    bool m_pumping = false;
    bool m_pumpAgain = false;
};

} // namespace dss::actions
