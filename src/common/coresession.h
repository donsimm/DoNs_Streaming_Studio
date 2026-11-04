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

private:
    bool send(const QJsonObject &message, QString *error);
    // Nächste Nachricht vom Typ type; error-Nachrichten des Kerns brechen mit deren Text ab,
    // andere (overlay_list, stats, …) werden übersprungen.
    std::optional<QJsonObject> waitFor(const QString &type, QString *error,
                                       qint64 requestId = -1);

    int m_timeoutMs;
    QLocalSocket m_socket;
    LineSplitter m_lines;
    qint64 m_nextRequestId = 1;
};

} // namespace dss
