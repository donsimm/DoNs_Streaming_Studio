#pragma once

#include "actionclock.h"
#include "actionengine.h"
#include "actionmodel.h"
#include "triggerevent.h"
#include "variables.h"

#include <QHash>
#include <QList>
#include <QUuid>

#include <functional>

// Auslöser ohne Netz (P14 b2, Plan 2.5 bis 2.7, 3.2, 3.4): welche Aktionen ein Ereignis, ein
// Befehl im Chat oder eine Zeitnachricht startet, mit Filtern, Rechten, Abklingzeiten und Zählern.
// Der Kern (b3) gibt die Ereignisse des Flusses hinein und reicht das Ergebnis mit submit() an die
// ActionEngine weiter. Nur Qt Core.
//
// - Reihenfolge: aktive Aktionen in der Reihenfolge der Liste (Plan 3.2); je Aktion höchstens ein
//   Lauf je Ereignis, auch wenn mehrere ihrer Auslöser passen.
// - Filter: tiers (leer = alle) gegen die Stufe von Twitch; min_months gegen die Monate insgesamt;
//   min/max bei Geschenk-Bündel (Anzahl), Cheer (Bits) und Raid (Zuschauer), beide Enden
//   eingeschlossen, max 0 = ohne Obergrenze. Nur beim Cheer heisst «nur max» (min 0, max > 0) der
//   genaue Wert (Plan 2.5). stream_online mit skip_at_start: nicht beim Verbinden mit einem
//   laufenden Stream (at_start).
// - Geteilter Chat: Chat-Auslöser und Befehle nur aus dem eigenen Kanal (Plan 4.1).
// - Nachrichten, die das Studio selbst gesendet hat (internal), lösen keine Chat-Auslöser aus und
//   Befehle nur ohne ignore_internal (Schutz vor Schleifen, Plan 4.1).
// - Hauptschalter aus: Befehle und Ereignisse werden als «abgewiesen: Chat-Bot aus» eingetragen,
//   ohne Abklingzeit und Zähler; Zeitnachrichten laufen dann nicht. Stumm (Plan 3.4): dasselbe mit
//   Zustand muted.

namespace dss::actions {

struct TriggerContext
{
    QString broadcasterId; // Twitch-ID des eigenen Kanals (Kanal darf immer, geteilter Chat)
    bool internal = false; // vom Studio selbst gesendet (Plan 4.1)
};

struct TriggerHit
{
    QUuid actionId;
    QUuid triggerId;
    QString triggerType;
    Arguments arguments;
};

// Kein Lauf, nur ein Eintrag in «Läufe der Aktionen» (Plan 2.6, 3.4)
struct TriggerRejection
{
    QUuid actionId;
    QString triggerType;
    RunState state = RunState::Rejected; // Rejected oder Muted
    QString reason;
};

struct TriggerResult
{
    QList<TriggerHit> hits;
    QList<TriggerRejection> rejections;
};

// Ergebnis an die Engine: Läufe anlegen, Abweisungen eintragen
void submit(ActionEngine &engine, const TriggerResult &result);

// Argumente eines Auslösers «test» aus seinen Testwerten (Plan 2.5)
Arguments testArguments(const Trigger &trigger);

class TriggerDispatcher
{
public:
    // clock und variables gehören dem Aufrufer und leben länger.
    TriggerDispatcher(ActionClock *clock, VariableStore *variables);
    ~TriggerDispatcher();

    // Neuer Stand: Zeitnachrichten mit unveränderten Zeiten laufen weiter, geänderte und neue
    // beginnen von vorne, gelöschte und abgeschaltete enden.
    void setConfig(const ActionsConfig &config);
    // Version im Einsatz läuft (Plan 3.4)
    void setMuted(bool muted) { m_muted = muted; }
    // Stream läuft (Zeitnachrichten mit only_live); stream_online/offline setzen es auch selbst
    void setLive(bool live) { m_live = live; }
    bool isLive() const { return m_live; }
    // Ergebnis einer fälligen Zeitnachricht
    void setTimerHandler(std::function<void(const TriggerResult &)> handler) { m_timerHandler = std::move(handler); }
    // Zufallszahl in [0, 1) für Zeitnachrichten mit random; Standard QRandomGenerator::global()
    void setRandomSource(std::function<double()> random) { m_random = std::move(random); }

    TriggerResult handleEvent(const TriggerEvent &event, const TriggerContext &context);

private:
    struct CommandMatch
    {
        const Command *command;
        QString text;     // der getroffene Befehl, zum Beispiel «!discord»
        QString rawInput; // Rest der Nachricht
    };
    struct TimerState
    {
        Timer timer;
        ActionClock::TimerId clockTimer = 0;
        int lines = 0; // Chat-Nachrichten seit dem letzten Lauf
    };

    std::optional<CommandMatch> matchCommand(const Command &command, const QString &message) const;
    // Rechte und Abklingzeiten (Plan 2.6). Leer = erlaubt; dann ist die Nutzung schon vermerkt.
    QString checkCommand(const Command &command, const QJsonObject &data, const QString &broadcasterId);
    bool filtersPass(const Trigger &trigger, const TriggerEvent &event) const;
    void handleChat(const TriggerEvent &event, const TriggerContext &context, const Arguments &chatArgs,
                    bool blocked, TriggerResult *result);
    void blockAll(TriggerResult *result) const;
    bool blocked() const { return !m_config.enabled || m_muted; }
    void scheduleTimer(TimerState &state);
    void timerDue(const QUuid &timerId);

    ActionClock *m_clock;
    VariableStore *m_variables;
    ActionsConfig m_config;
    bool m_muted = false;
    bool m_live = false;
    std::function<void(const TriggerResult &)> m_timerHandler;
    std::function<double()> m_random;
    QHash<QUuid, QDateTime> m_globalUsed;             // Befehl → letzte Nutzung
    QHash<QUuid, QHash<QString, QDateTime>> m_userUsed; // Befehl → Twitch-ID → letzte Nutzung
    QHash<QUuid, TimerState> m_timers;
};

} // namespace dss::actions
