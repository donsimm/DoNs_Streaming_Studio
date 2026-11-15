#pragma once

#include "protocol.h"

#include <QLocalSocket>
#include <QString>

#include <optional>

// Kurze, blockierende Verbindung zum Kern für dss-cli (P10-vor, Teil A 3): verbinden, hello
// tauschen, eine Anfrage schicken, auf die Antwort warten. Das Fenster nutzt CoreClient
// (ereignisgesteuert, verbindet neu); dss-cli braucht nur eine Antwort und endet dann.
// Nur Qt Core und Qt Network.

namespace dss {

class CoreSession
{
public:
    // timeoutMs gilt für jeden Schritt (Verbinden, hello, Antwort).
    explicit CoreSession(int timeoutMs = 3000);

    // false mit Meldung, wenn der Kern unter socketPath nicht erreichbar ist oder hello nicht
    // passt. *notRunning ist true, wenn dort kein Kern läuft (Socket fehlt oder abgelehnt).
    bool open(const QString &socketPath, const QString &program, QString *error,
              bool *notRunning = nullptr);

    std::optional<SettingsInfo> getSettings(QString *error);
    // Antwort change_result des Kerns; ohne Antwort std::nullopt mit Meldung.
    std::optional<ChangeResult> updateSettings(const SettingsUpdate &update, QString *error);
    // P10b: Medienliste und Änderungen (request.requestId wird hier vergeben).
    std::optional<MediaListData> getMediaList(QString *error);
    std::optional<ChangeResult> change(ChangeRequest request, QString *error);
    // P13: Ereignisse bestellen (request.history wird gesetzt) und den Verlauf zurückgeben.
    std::optional<QList<events::Event>> subscribeEvents(const QStringList &filters, QString *error);
    // P13: nächste Nachricht event oder events_dropped, ohne Zeitgrenze; std::nullopt mit
    // Meldung, wenn der Kern trennt.
    std::optional<QJsonObject> nextEventMessage(QString *error);
    // P14 b3: Anfrage des Aktionssystems (request_id wird hier vergeben) und Antwort vom Typ
    // replyType (change_result oder eigene Nachricht) mit derselben request_id. Ein change_result
    // mit ok false ist eine Antwort, kein Fehler.
    std::optional<QJsonObject> actionsRequest(const QString &type, const QJsonObject &fields,
                                              const QString &replyType, QString *error);
    // P14 b3: nächste Nachricht vom Typ type (zum Beispiel actions_config nach hello)
    std::optional<QJsonObject> waitForMessage(const QString &type, QString *error);

private:
    bool send(const QJsonObject &message, QString *error);
    // Nächste Nachricht vom Typ type; error-Nachrichten des Kerns brechen mit deren Text ab,
    // andere (overlay_list, stats, …) werden übersprungen.
    std::optional<QJsonObject> waitFor(const QString &type, QString *error,
                                       qint64 requestId = -1);
    // Wie waitFor, für mehrere Typen; timeoutMs < 0 = ohne Grenze.
    std::optional<QJsonObject> waitForAny(const QStringList &types, QString *error, qint64 requestId,
                                          int timeoutMs);

    int m_timeoutMs;
    QLocalSocket m_socket;
    LineSplitter m_lines;
    qint64 m_nextRequestId = 1;
};

} // namespace dss
