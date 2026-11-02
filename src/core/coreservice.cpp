#include "coreservice.h"

#include "version.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

namespace {

constexpr int kStatsIntervalMs = 1000; // Leistungswerte einmal pro Sekunde (Teil E)
constexpr int kProbeTimeoutMs = 500;   // Prüfen, ob unter dem Pfad schon ein Kern antwortet
constexpr qsizetype kMaxSocketPathBytes = 107;

} // namespace

CoreService::CoreService(dss::OverlayStore *store, const QString &socketPath, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_socketPath(socketPath)
    , m_server(new QLocalServer(this))
    , m_statsTimer(new QTimer(this))
{
    // In P07 gibt es noch keine Verbindungen: alle «nicht eingerichtet» (grau).
    for (dss::Connection c : dss::kAllConnections)
        m_connectionStates.insert(c, dss::ConnectionState::Off);
    connect(m_server, &QLocalServer::newConnection, this, &CoreService::onNewConnection);
    m_statsTimer->setInterval(kStatsIntervalMs);
    connect(m_statsTimer, &QTimer::timeout, this, &CoreService::sampleStats);
}

CoreService::~CoreService()
{
    const QList<QLocalSocket *> sockets = m_clients.keys();
    m_clients.clear();
    for (QLocalSocket *socket : sockets) {
        socket->disconnect(this);
        socket->abort();
    }
    m_server->close(); // entfernt die Socket-Datei
}

CoreService::ListenResult CoreService::listen(QString *error)
{
    // sun_path in sockaddr_un fasst 108 Byte einschliesslich der abschliessenden Null (unix(7),
    // https://man7.org/linux/man-pages/man7/unix.7.html); Qt meldet sonst nur «Name error».
    if (QFile::encodeName(m_socketPath).size() > kMaxSocketPathBytes) {
        *error = QStringLiteral("Pfad der Verbindung zu lang (%1 Byte, höchstens %2): %3")
                     .arg(QFile::encodeName(m_socketPath).size()).arg(kMaxSocketPathBytes)
                     .arg(m_socketPath);
        return ListenResult::Failed;
    }
    const QString dir = QFileInfo(m_socketPath).absolutePath();
    if (!QDir().mkpath(dir)
        || !QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) {
        *error = QStringLiteral("Ordner %1 kann nicht angelegt werden").arg(dir);
        return ListenResult::Failed;
    }
    // Eine übrig gebliebene Socket-Datei (Absturz) lässt listen() scheitern und wird
    // entfernt, aber nur, wenn dort kein Kern mehr antwortet.
    // https://doc.qt.io/qt-6.8/qlocalserver.html#removeServer
    if (QFileInfo::exists(m_socketPath)) {
        QLocalSocket probe;
        probe.connectToServer(m_socketPath);
        if (probe.waitForConnected(kProbeTimeoutMs)) {
            probe.abort();
            *error = QStringLiteral("Unter %1 antwortet schon ein Kern").arg(m_socketPath);
            return ListenResult::InUse;
        }
        QLocalServer::removeServer(m_socketPath);
    }
    // Nur der Benutzer darf sich verbinden.
    // https://doc.qt.io/qt-6.8/qlocalserver.html#SocketOption-enum
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_socketPath)) {
        *error = QStringLiteral("Verbindung %1 kann nicht geöffnet werden (%2)")
                     .arg(m_socketPath, m_server->errorString());
        return ListenResult::Failed;
    }
    qInfo("Verbindung offen: %s", qPrintable(m_server->fullServerName()));
    m_sinceLastSample.start();
    sampleStats();
    m_statsTimer->start();
    return ListenResult::Ok;
}

QList<dss::OverlaySummary> CoreService::summaries() const
{
    QList<dss::OverlaySummary> list;
    for (const dss::Overlay &o : m_store->overlays())
        list.append(dss::summarizeOverlay(o));
    return list;
}

void CoreService::reload()
{
    const QList<dss::Overlay> before = m_store->overlays();
    QString error;
    if (!m_store->load(&error)) {
        qCritical("Neu laden fehlgeschlagen: %s", qPrintable(error));
        return;
    }
    for (const QString &e : m_store->loadErrors())
        qWarning("Fehler beim Laden: %s", qPrintable(e));
    for (const QString &w : m_store->loadWarnings())
        qWarning("Warnung: %s", qPrintable(w));

    int changed = 0;
    int removed = 0;
    const QList<dss::Overlay> after = m_store->overlays();
    for (const dss::Overlay &o : after) {
        const auto old = std::find_if(before.begin(), before.end(),
                                      [&o](const dss::Overlay &b) { return b.id == o.id; });
        if (old == before.end() || !(*old == o)) {
            broadcast(dss::overlayMessage(dss::msg::kOverlayChanged, o));
            ++changed;
        }
    }
    for (const dss::Overlay &b : before) {
        if (!m_store->overlay(b.id)) {
            broadcast(dss::overlayIdMessage(dss::msg::kOverlayRemoved, b.id));
            ++removed;
        }
    }
    if (changed > 0 || removed > 0)
        broadcast(dss::overlayListMessage(summaries()));
    qInfo("Neu geladen: %lld Overlays, %d geändert oder neu, %d entfernt",
          static_cast<long long>(after.size()), changed, removed);
}

void CoreService::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        m_clients.insert(socket, Client());
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] { onDisconnected(socket); });
        // Der Kern grüsst zuerst (docs/08-kern-schnittstelle.md, «Ablauf»).
        send(socket, dss::helloMessage({QStringLiteral("dss-core"), dss::version(),
                                        dss::kProtocolVersion}));
    }
}

void CoreService::onDisconnected(QLocalSocket *socket)
{
    const auto it = m_clients.find(socket);
    if (it != m_clients.end()) {
        if (it->greeted)
            qInfo("Verbindung getrennt: %s (noch %lld)", qPrintable(it->hello.program),
                  static_cast<long long>(m_clients.size() - 1));
        m_clients.erase(it);
    }
    socket->deleteLater();
}

void CoreService::onReadyRead(QLocalSocket *socket)
{
    auto it = m_clients.find(socket);
    if (it == m_clients.end())
        return;
    it->lines.append(socket->readAll());
    QByteArray line;
    for (;;) {
        it = m_clients.find(socket);
        if (it == m_clients.end())
            return;
        const dss::LineSplitter::Result r = it->lines.next(&line);
        if (r == dss::LineSplitter::Result::None)
            return;
        if (r == dss::LineSplitter::Result::TooLong) {
            sendError(socket, dss::err::kMessageTooLong,
                      QStringLiteral("Nachricht länger als %1 Byte, Verbindung wird getrennt")
                          .arg(dss::kMaxMessageBytes),
                      QString());
            socket->disconnectFromServer();
            return;
        }
        QJsonObject message;
        QString error;
        if (!dss::decodeMessage(line, &message, &error)) {
            sendError(socket, dss::err::kInvalidJson, error, QString());
            continue;
        }
        handleMessage(socket, *it, message);
    }
}

void CoreService::handleMessage(QLocalSocket *socket, Client &client, const QJsonObject &message)
{
    const QString type = dss::messageType(message);
    QString error;
    if (type == QLatin1String(dss::msg::kHello)) {
        dss::Hello hello;
        if (!dss::parseHello(message, &hello, &error)) {
            sendError(socket, dss::err::kInvalidMessage, error, type);
            return;
        }
        if (hello.protocolVersion != dss::kProtocolVersion) {
            sendError(socket, dss::err::kProtocolVersion,
                      QStringLiteral("Schnittstelle Version %1 erwartet, erhalten %2")
                          .arg(dss::kProtocolVersion).arg(hello.protocolVersion),
                      type);
            socket->disconnectFromServer();
            return;
        }
        client.greeted = true;
        client.hello = hello;
        qInfo("Verbunden: %s %s (%lld Verbindungen)", qPrintable(hello.program),
              qPrintable(hello.version), static_cast<long long>(m_clients.size()));
        send(socket, dss::overlayListMessage(summaries()));
        send(socket, dss::connectionsMessage(m_connectionStates));
        if (m_lastStats)
            send(socket, dss::statsMessage(*m_lastStats));
        return;
    }
    if (!client.greeted) {
        sendError(socket, dss::err::kHelloMissing,
                  QStringLiteral("Zuerst hello schicken"), type);
        return;
    }
    if (type == QLatin1String(dss::msg::kGetOverlayList)) {
        send(socket, dss::overlayListMessage(summaries()));
    } else if (type == QLatin1String(dss::msg::kGetOverlay)) {
        QUuid id;
        if (!dss::parseOverlayId(message, &id, &error)) {
            sendError(socket, dss::err::kInvalidMessage, error, type);
            return;
        }
        const dss::Overlay *o = m_store->overlay(id);
        if (!o) {
            sendError(socket, dss::err::kUnknownOverlay,
                      QStringLiteral("Kein Overlay mit der Kennung %1")
                          .arg(id.toString(QUuid::WithoutBraces)),
                      type);
            return;
        }
        send(socket, dss::overlayMessage(dss::msg::kOverlay, *o));
    } else if (type == QLatin1String(dss::msg::kQuitRequest)) {
        int otherWindows = 0;
        for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
            if (it.key() != socket && it->greeted
                && it->hello.program == QLatin1String(kWindowProgram))
                ++otherWindows;
        }
        const dss::QuitReply reply{otherWindows == 0, otherWindows};
        send(socket, dss::quitReplyMessage(reply));
        socket->flush();
        if (reply.accepted) {
            qInfo("Beenden erbeten von %s, kein anderes Fenster verbunden: beende",
                  qPrintable(client.hello.program));
            QTimer::singleShot(0, this, &CoreService::quitRequested);
        } else {
            qInfo("Beenden erbeten von %s, abgelehnt: %d andere Fenster verbunden",
                  qPrintable(client.hello.program), otherWindows);
        }
    } else {
        sendError(socket, dss::err::kUnknownType,
                  QStringLiteral("Unbekannte Nachricht «%1»").arg(type), type);
    }
}

void CoreService::send(QLocalSocket *socket, const QJsonObject &message)
{
    socket->write(dss::encodeMessage(message));
}

void CoreService::sendError(QLocalSocket *socket, const char *code, const QString &text,
                            const QString &request)
{
    qWarning("Fehler an Verbindung: %s: %s", code, qPrintable(text));
    send(socket, dss::errorMessage({QString::fromLatin1(code), text, request}));
}

void CoreService::broadcast(const QJsonObject &message)
{
    const QByteArray line = dss::encodeMessage(message);
    for (auto it = m_clients.cbegin(); it != m_clients.cend(); ++it) {
        if (it->greeted)
            it.key()->write(line);
    }
}

void CoreService::sampleStats()
{
    const qint64 elapsed = m_sinceLastSample.restart();
    m_lastStats = m_sampler.sample(elapsed);
    broadcast(dss::statsMessage(*m_lastStats));
}
