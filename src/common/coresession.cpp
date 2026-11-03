#include "coresession.h"

#include "coreclient.h"
#include "version.h"

#include <QDeadlineTimer>

namespace dss {

CoreSession::CoreSession(int timeoutMs)
    : m_timeoutMs(timeoutMs)
{
}

bool CoreSession::open(const QString &socketPath, const QString &program, QString *error,
                       bool *notRunning)
{
    if (notRunning)
        *notRunning = false;
    m_socket.connectToServer(socketPath);
    if (!m_socket.waitForConnected(m_timeoutMs)) {
        const QLocalSocket::LocalSocketError e = m_socket.error();
        if (notRunning)
            *notRunning = e == QLocalSocket::ServerNotFoundError
                          || e == QLocalSocket::ConnectionRefusedError;
        *error = QStringLiteral("Kern unter %1 nicht erreichbar: %2").arg(socketPath, socketErrorText(e));
        return false;
    }
    // Der Kern grüsst zuerst (docs/08-kern-schnittstelle.md, «Ablauf»).
    const std::optional<QJsonObject> hello = waitFor(QString::fromLatin1(msg::kHello), error);
    if (!hello)
        return false;
    Hello core;
    if (!parseHello(*hello, &core, error))
        return false;
    if (core.protocolVersion != kProtocolVersion) {
        *error = QStringLiteral("Kern spricht Schnittstelle Version %1, dss-cli Version %2")
                     .arg(core.protocolVersion).arg(kProtocolVersion);
        return false;
    }
    return send(helloMessage({program, version(), kProtocolVersion}), error);
}

std::optional<SettingsInfo> CoreSession::getSettings(QString *error)
{
    if (!send(QJsonObject{{QStringLiteral("type"), QString::fromLatin1(msg::kGetSettings)}}, error))
        return std::nullopt;
    const std::optional<QJsonObject> reply = waitFor(QString::fromLatin1(msg::kSettings), error);
    if (!reply)
        return std::nullopt;
    SettingsInfo info;
    if (!parseSettings(*reply, &info, error))
        return std::nullopt;
    return info;
}

std::optional<ChangeResult> CoreSession::updateSettings(const SettingsUpdate &update, QString *error)
{
    SettingsUpdate u = update;
    u.requestId = m_nextRequestId++;
    if (!send(settingsUpdateMessage(u), error))
        return std::nullopt;
    const std::optional<QJsonObject> reply =
        waitFor(QString::fromLatin1(msg::kChangeResult), error, u.requestId);
    if (!reply)
        return std::nullopt;
    ChangeResult result;
    if (!parseChangeResult(*reply, &result, error))
        return std::nullopt;
    return result;
}

bool CoreSession::send(const QJsonObject &message, QString *error)
{
    m_socket.write(encodeMessage(message));
    if (!m_socket.waitForBytesWritten(m_timeoutMs) && m_socket.bytesToWrite() > 0) {
        *error = QStringLiteral("Senden an den Kern gescheitert: %1").arg(socketErrorText(m_socket.error()));
        return false;
    }
    return true;
}

std::optional<QJsonObject> CoreSession::waitFor(const QString &type, QString *error, qint64 requestId)
{
    const QDeadlineTimer deadline(m_timeoutMs);
    for (;;) {
        QByteArray line;
        while (m_lines.next(&line) == LineSplitter::Result::Line) {
            QJsonObject o;
            if (!decodeMessage(line, &o, error))
                return std::nullopt;
            const QString t = messageType(o);
            if (t == QLatin1String(msg::kError)) {
                ErrorInfo e;
                QString parseText;
                *error = parseError(o, &e, &parseText)
                             ? QStringLiteral("Kern meldet %1: %2").arg(e.code, e.message)
                             : parseText;
                return std::nullopt;
            }
            if (t == type && (requestId < 0 || messageRequestId(o) == requestId))
                return o;
        }
        if (m_socket.state() != QLocalSocket::ConnectedState) {
            *error = QStringLiteral("Kern hat die Verbindung getrennt");
            return std::nullopt;
        }
        if (deadline.hasExpired()) {
            *error = QStringLiteral("Keine Antwort «%1» vom Kern nach %2 ms").arg(type).arg(m_timeoutMs);
            return std::nullopt;
        }
        if (m_socket.waitForReadyRead(int(deadline.remainingTime())) || m_socket.bytesAvailable() > 0)
            m_lines.append(m_socket.readAll());
    }
}

} // namespace dss
