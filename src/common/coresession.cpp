#include "coresession.h"

#include "actionprotocol.h"
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

std::optional<MediaListData> CoreSession::getMediaList(QString *error)
{
    if (!send(simpleMessage(msg::kGetMediaList), error))
        return std::nullopt;
    const std::optional<QJsonObject> reply = waitFor(QString::fromLatin1(msg::kMediaList), error);
    if (!reply)
        return std::nullopt;
    MediaListData data;
    if (!parseMediaList(*reply, &data, error))
        return std::nullopt;
    return data;
}

std::optional<ChangeResult> CoreSession::change(ChangeRequest request, QString *error)
{
    request.requestId = m_nextRequestId++;
    if (!send(changeRequestMessage(request), error))
        return std::nullopt;
    const std::optional<QJsonObject> reply =
        waitFor(QString::fromLatin1(msg::kChangeResult), error, request.requestId);
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

std::optional<QList<events::Event>> CoreSession::subscribeEvents(const QStringList &filters, QString *error)
{
    if (!send(subscribeEventsMessage({filters, true}), error))
        return std::nullopt;
    const std::optional<QJsonObject> reply = waitFor(QString::fromLatin1(msg::kEventHistory), error);
    if (!reply)
        return std::nullopt;
    QList<events::Event> history;
    if (!parseEventHistory(*reply, &history, error))
        return std::nullopt;
    return history;
}

std::optional<QJsonObject> CoreSession::nextEventMessage(QString *error)
{
    return waitForAny({QString::fromLatin1(msg::kEvent), QString::fromLatin1(msg::kEventsDropped)}, error,
                      -1, -1);
}

std::optional<QJsonObject> CoreSession::actionsRequest(const QString &type, const QJsonObject &fields,
                                                       const QString &replyType, QString *error)
{
    const qint64 id = m_nextRequestId++;
    if (!send(actionsRequestMessage({id, type, fields}), error))
        return std::nullopt;
    return waitForAny({replyType, QString::fromLatin1(msg::kChangeResult)}, error, id, m_timeoutMs);
}

std::optional<QJsonObject> CoreSession::waitForMessage(const QString &type, QString *error)
{
    return waitFor(type, error);
}

std::optional<QJsonObject> CoreSession::waitFor(const QString &type, QString *error, qint64 requestId)
{
    return waitForAny({type}, error, requestId, m_timeoutMs);
}

std::optional<QJsonObject> CoreSession::waitForAny(const QStringList &types, QString *error, qint64 requestId,
                                                   int timeoutMs)
{
    const QDeadlineTimer deadline = timeoutMs < 0 ? QDeadlineTimer(QDeadlineTimer::Forever)
                                                  : QDeadlineTimer(timeoutMs);
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
            if (types.contains(t) && (requestId < 0 || messageRequestId(o) == requestId))
                return o;
        }
        if (m_socket.state() != QLocalSocket::ConnectedState) {
            *error = QStringLiteral("Kern hat die Verbindung getrennt");
            return std::nullopt;
        }
        if (deadline.hasExpired()) {
            *error = QStringLiteral("Keine Antwort «%1» vom Kern nach %2 ms")
                         .arg(types.join(QStringLiteral("» oder «"))).arg(timeoutMs);
            return std::nullopt;
        }
        if (m_socket.waitForReadyRead(int(deadline.remainingTime())) || m_socket.bytesAvailable() > 0)
            m_lines.append(m_socket.readAll());
    }
}

} // namespace dss
