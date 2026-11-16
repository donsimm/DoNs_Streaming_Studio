#pragma once

#include "actionclock.h"
#include "actionengine.h"
#include "actionmodel.h"
#include "actionprotocol.h"
#include "actionservices.h"
#include "eventbus.h"
#include "triggerdispatcher.h"
#include "variables.h"
#include "viewerdata.h"

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

class QLocalSocket;
class QTimer;

namespace dss {
class SaveScheduler;
}

// Aktionssystem im Kern (P14 b3, Plan 3 und 5, Schalter DSS_CORE_ACTIONS): hält aktionen.json,
// variablen.json, zuschauer.json und zitate.json, startet Läufe aus den Ereignissen des Flusses,
// aus Befehlen und Zeitnachrichten, und beantwortet die Anfragen der Fenster (actionprotocol.h).
// CoreService reicht nur weiter; alle connect zum Aktionssystem stehen hier.
//
// - Speichert sofort: jede Änderung schreibt aktionen.json atomar (QSaveFile). Eine ungültige
//   Datei wird nie überschrieben; der Bot bleibt dann aus, Änderungen werden abgewiesen (Plan 2.11).
//   Variablen und Zuschauer schreibt der Kern zusammengefasst (SaveScheduler), Zitate sofort.
// - Stumm (Plan 3.4): Antwortet unter inUseSocket ein Kern (hello von dss-core), startet dieser
//   Kern keine Läufe aus Ereignissen, Befehlen und Zeitnachrichten; Probeläufe gehen weiter.
// - Journal (Plan 3.5): nur Name der Aktion, Ergebnis und Fehler, keine Chat-Texte und keine Namen
//   von Zuschauern.
// - Twitch (Chat senden, Benutzerinfo, Shoutout, Ansage) meldet bis Abschnitt c1 «Twitch nicht
//   verbunden»; Ton, Overlay und OBS sind «nicht verfügbar» (c2, P14-2).
class ActionService : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString dataDir;
        // Verbindung des Kerns der Version im Einsatz; leer = nicht prüfen (Version im Einsatz)
        QString inUseSocket;
        int probeIntervalMs = 2000;
        // dss-core --testereignisse: test_event legt Ereignisse auf den Fluss (nur Tests)
        bool testEvents = false;
    };

    ActionService(dss::events::EventBus *bus, const Options &options, QObject *parent = nullptr);
    ~ActionService() override;

    // Liest die Dateien (beim Start und beim Neu laden mit SIGHUP) und schickt den Stand an alle.
    void load();
    // Schreibt Variablen und Zuschauer, die noch nicht gespeichert sind.
    bool savePending(QString *error);

    // Nach hello an das neue Fenster
    QList<QJsonObject> helloMessages() const;
    // Anfrage eines Fensters (isActionsRequestType); reply schickt an dieses Fenster.
    void handleRequest(const QJsonObject &message, const std::function<void(const QJsonObject &)> &reply);

    bool isMuted() const { return m_muted; }

signals:
    // An alle Fenster
    void broadcastRequested(const QJsonObject &message);

private:
    class NotConnectedChat;
    class NotConnectedTwitch;

    using Reply = std::function<void(const QJsonObject &)>;
    void handleEdit(const dss::ActionsRequest &request, const Reply &reply);
    void handleVariableEdit(const dss::ActionsRequest &request, const Reply &reply);
    void handleQuoteEdit(const dss::ActionsRequest &request, const Reply &reply);
    void handleQueueControl(const dss::ActionsRequest &request, const Reply &reply);
    void handleTest(const dss::ActionsRequest &request, const Reply &reply);
    void handleTestEvent(const dss::ActionsRequest &request, const Reply &reply);
    void handleViewerImport(const dss::ActionsRequest &request, const Reply &reply);

    void onEvent(const dss::events::Event &event);
    void onRunStarted(const dss::actions::RunRecord &record);
    void onRunChanged(const dss::actions::RunRecord &record);
    void onRunFinished(const dss::actions::RunRecord &record);
    void publishRun(const char *type, const dss::actions::RunRecord &record);

    // aktionen.json atomar schreiben
    bool writeConfig(const dss::actions::ActionsConfig &config, QString *error);
    void applyConfig();
    QString filePath(const char *name) const;

    dss::ActionsStatus status() const;
    QJsonObject configMessage() const;
    // actions_status an alle, wenn er sich geändert hat
    void broadcastStatusIfChanged();
    void noteVariablesChanged();

    void probeInUse();
    void setMuted(bool muted);

    dss::events::EventBus *m_bus;
    Options m_options;
    dss::actions::SystemActionClock m_clock;
    dss::actions::VariableStore m_variables;
    std::unique_ptr<dss::actions::ActionEngine> m_engine;
    std::unique_ptr<dss::actions::TriggerDispatcher> m_dispatcher;
    std::unique_ptr<NotConnectedChat> m_chat;
    std::unique_ptr<NotConnectedTwitch> m_twitch;
    dss::events::Subscription m_subscription;

    dss::actions::ActionsConfig m_config;
    QString m_configError;    // aktionen.json ungültig
    QString m_variablesError; // variablen.json ungültig: nicht überschreiben
    QString m_viewersError;
    QString m_quotesError;
    dss::actions::ViewersData m_viewers;
    dss::actions::QuotesData m_quotes;
    bool m_viewersChanged = false;
    dss::SaveScheduler *m_saver = nullptr;

    bool m_muted = false;
    QTimer *m_probeTimer = nullptr;
    QLocalSocket *m_probe = nullptr;
    QJsonObject m_lastStatus;
};
