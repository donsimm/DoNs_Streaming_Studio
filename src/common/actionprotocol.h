#pragma once

#include "actionengine.h"
#include "actionmodel.h"
#include "actionview.h"
#include "variables.h"
#include "viewerdata.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <optional>

// Nachrichten des Aktionssystems zwischen Kern und Fenster (P14 b3, Plan 5; Schnittstelle 15).
// Eigene Datei neben protocol.h, damit die Spuren von P14 dort nur die Version berühren.
// Beschreibung: docs/08-kern-schnittstelle.md, «Aktionen». Nur Qt Core (Regel 9).
//
// Anfragen des Fensters tragen request_id; der Kern antwortet mit change_result (ok, code,
// message, id) oder, bei Abfragen, mit einer eigenen Nachricht mit derselben request_id.
// Chat-Texte und Zuschauernamen stehen nur in action_run, variables, viewers und quotes, nie in
// actions_status oder auf dem Fluss (actions.run_*).

namespace dss {

namespace msg {
// Kern → Fenster, nach hello und bei Änderung an alle
inline constexpr char kActionsConfig[] = "actions_config";
inline constexpr char kActionsStatus[] = "actions_status";
// Kern → Fenster, Antworten auf Abfragen
inline constexpr char kVariables[] = "variables";
inline constexpr char kViewers[] = "viewers";
inline constexpr char kQuotes[] = "quotes";
inline constexpr char kActionRuns[] = "action_runs";
inline constexpr char kActionRun[] = "action_run";
inline constexpr char kViewerImportResult[] = "viewer_import_result";
// Fenster → Kern: Änderungen (Antwort change_result)
inline constexpr char kActionsSetEnabled[] = "actions_set_enabled";
inline constexpr char kActionPut[] = "action_put";
inline constexpr char kActionRemove[] = "action_remove";
inline constexpr char kActionCopy[] = "action_copy";
inline constexpr char kActionReset[] = "action_reset";
inline constexpr char kCommandPut[] = "command_put";
inline constexpr char kCommandRemove[] = "command_remove";
inline constexpr char kTimerPut[] = "timer_put";
inline constexpr char kTimerRemove[] = "timer_remove";
inline constexpr char kQueuePut[] = "queue_put";
inline constexpr char kQueueRemove[] = "queue_remove";
inline constexpr char kQueueControl[] = "queue_control";
inline constexpr char kGroupPut[] = "group_put";
inline constexpr char kGroupRemove[] = "group_remove";
inline constexpr char kVariableSet[] = "variable_set";
inline constexpr char kVariableRemove[] = "variable_remove";
inline constexpr char kQuotePut[] = "quote_put";
inline constexpr char kQuoteRemove[] = "quote_remove";
// Fenster → Kern: Läufe (Antwort change_result, bei action_test mit der Kennung des Laufs)
inline constexpr char kActionTest[] = "action_test";
inline constexpr char kActionRunCancel[] = "action_run_cancel";
// Fenster → Kern: Chat senden als bot oder broadcaster (seit Version 18, Antwort change_result;
// Ergebnis twitch.chat_sent oder twitch.chat_dropped auf dem Fluss)
inline constexpr char kChatSend[] = "chat_send";
// Fenster → Kern: Abfragen (Antwort mit eigener Nachricht)
inline constexpr char kActionRunDetails[] = "action_run_details";
inline constexpr char kGetActionRuns[] = "get_action_runs";
inline constexpr char kGetVariables[] = "get_variables";
inline constexpr char kGetViewers[] = "get_viewers";
inline constexpr char kGetQuotes[] = "get_quotes";
// Fenster → Kern: Zuschauer-Import aus Streamer.bot (P14 z1b, Antwort viewer_import_result)
inline constexpr char kViewerImport[] = "viewer_import";
// Nur mit dss-core --testereignisse (Tests): Ereignis auf den Fluss legen
inline constexpr char kTestEvent[] = "test_event";
} // namespace msg

namespace err {
inline constexpr char kActionsFileError[] = "actions_file_error"; // Datei ungültig, Änderung abgewiesen
inline constexpr char kActionsUnavailable[] = "actions_unavailable"; // Schalter DSS_CORE_ACTIONS aus
inline constexpr char kUnknownAction[] = "unknown_action";
inline constexpr char kUnknownRun[] = "unknown_run";
} // namespace err

// Alle Anfragen des Aktionssystems (Fenster → Kern), in der Reihenfolge von docs/08
QStringList actionsRequestTypes();
bool isActionsRequestType(const QString &type);

// Anfrage: type, request_id und die Felder des Typs (geprüft nach Art, ohne Inhalt: eine Aktion
// prüft der Kern mit actionFromJson).
struct ActionsRequest
{
    qint64 requestId = 0;
    QString type;
    QJsonObject fields; // ohne type und request_id

    bool operator==(const ActionsRequest &) const = default;
};
QJsonObject actionsRequestMessage(const ActionsRequest &request);
// Aus dem Vorschlag der Ansichtslogik (actionview.h)
QJsonObject actionsEditMessage(const ActionsEdit &edit, qint64 requestId);
bool parseActionsRequest(const QJsonObject &message, ActionsRequest *request, QString *error);

// Kennung in einem Feld: Text wie QUuid::toString (mit oder ohne Klammern). null oder fehlend:
// allowNull entscheidet.
std::optional<QUuid> uuidField(const QJsonObject &fields, const QString &name, bool allowNull, QString *error);

// actions_config: ganzer Inhalt von aktionen.json; bei ungültiger Datei config leer und file_error
struct ActionsConfigInfo
{
    std::optional<actions::ActionsConfig> config;
    QString fileError;

    bool operator==(const ActionsConfigInfo &) const = default;
};
QJsonObject actionsConfigMessage(const ActionsConfigInfo &info);
bool parseActionsConfig(const QJsonObject &message, ActionsConfigInfo *info, QString *error);

// actions_status
struct ActionsQueueStatus
{
    QUuid id;         // null = «Standard»
    QString name;
    bool blocking = false;
    bool paused = false;
    int waiting = 0;
    int running = 0;

    bool operator==(const ActionsQueueStatus &) const = default;
};
struct ActionsAccountStatus
{
    QString account;   // bot, broadcaster
    bool canSend = false;
    QString message;   // Grund, wenn nicht

    bool operator==(const ActionsAccountStatus &) const = default;
};
// state: unavailable (Schalter aus), file_error, off (Hauptschalter aus), muted (Version im Einsatz
// läuft, Plan 3.4), on
struct ActionsStatus
{
    QString state;
    QString message;
    int running = 0;
    QList<ActionsQueueStatus> queues;
    QList<ActionsAccountStatus> accounts;

    bool operator==(const ActionsStatus &) const = default;
};
QJsonObject actionsStatusMessage(const ActionsStatus &status);
bool parseActionsStatus(const QJsonObject &message, ActionsStatus *status, QString *error);

// Läufe (Plan 3.5). Kurzform ohne Schritte: get_action_runs und Fluss; ganz: action_run.
QJsonObject runSummaryToJson(const actions::RunRecord &record);
QJsonObject runToJson(const actions::RunRecord &record);
// Gegenstück für Fenster und dss-cli; liest Kurz- und ganze Form (log fehlt = leer)
bool runFromJson(const QJsonObject &object, actions::RunRecord *record, QString *error);
QJsonObject actionRunsMessage(qint64 requestId, const QList<actions::RunRecord> &runs);
bool parseActionRuns(const QJsonObject &message, QList<actions::RunRecord> *runs, QString *error);
QJsonObject actionRunMessage(qint64 requestId, const actions::RunRecord &record);
bool parseActionRun(const QJsonObject &message, actions::RunRecord *record, QString *error);
// data der Ereignisse actions.run_* (eventtypes.h)
QJsonObject runEventData(const actions::RunRecord &record);

// variables: dauerhafte Werte wie in variablen.json (mit Herkunft) und flüchtige ohne Herkunft
struct VariablesInfo
{
    actions::VariablesData persisted;
    VolatileValues volatileGlobal;
    VolatileUserValues volatileUsers;
    QString fileError; // variablen.json ungültig (Änderungen abgewiesen)

    bool operator==(const VariablesInfo &) const = default;
};
QJsonObject variablesMessage(qint64 requestId, const VariablesInfo &info);
bool parseVariablesMessage(const QJsonObject &message, VariablesInfo *info, QString *error);

// viewers: nur die erfragten Twitch-IDs, die bekannt sind
QJsonObject viewersMessage(qint64 requestId, const actions::ViewersData &viewers);
bool parseViewersMessage(const QJsonObject &message, actions::ViewersData *viewers, QString *error);

// quotes
QJsonObject quotesMessage(qint64 requestId, const actions::QuotesData &quotes);
bool parseQuotesMessage(const QJsonObject &message, actions::QuotesData *quotes, QString *error);

// viewer_import_result (P14 z1b, seit Version 17): Bericht ohne Namen und Werte von Zuschauern
struct ViewerImportResult
{
    bool dryRun = true;
    QString report;      // importReportText (viewerimport.h)
    QStringList written; // geschriebene Dateien (Probelauf: leer)
    QString backup;      // Ordner der Sicherung relativ zum Datenordner, leer = keine

    bool operator==(const ViewerImportResult &) const = default;
};
QJsonObject viewerImportResultMessage(qint64 requestId, const ViewerImportResult &result);
bool parseViewerImportResult(const QJsonObject &message, ViewerImportResult *result, QString *error);

} // namespace dss
