#include "coreclient.h"

#include <QDeadlineTimer>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QSet>
#include <QTimer>

namespace dss {

QString socketErrorText(QLocalSocket::LocalSocketError error)
{
    // Qt nennt ENOENT «Invalid name» und ECONNREFUSED «Connection refused»
    // (QLocalSocketPrivate::_q_connectToSocket und generateErrorString in
    // https://code.qt.io/cgit/qt/qtbase.git/tree/src/network/socket/qlocalsocket_unix.cpp?h=v6.8.2).
    switch (error) {
    case QLocalSocket::ServerNotFoundError:
        return QStringLiteral("keine Verbindung unter diesem Pfad");
    case QLocalSocket::ConnectionRefusedError:
        return QStringLiteral("Verbindung abgelehnt, Kern läuft nicht");
    case QLocalSocket::SocketAccessError:
        return QStringLiteral("kein Zugriff auf die Verbindung");
    case QLocalSocket::SocketTimeoutError:
        return QStringLiteral("Zeitüberschreitung");
    default:
        return QStringLiteral("Fehler %1").arg(int(error));
    }
}

CoreClient::CoreClient(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
    , m_socket(new QLocalSocket(this))
    , m_reconnectTimer(new QTimer(this))
{
    // https://doc.qt.io/qt-6.8/qlocalsocket.html
    connect(m_socket, &QLocalSocket::connected, this, &CoreClient::onConnected);
    connect(m_socket, &QLocalSocket::disconnected, this, &CoreClient::onDisconnected);
    connect(m_socket, &QLocalSocket::errorOccurred, this, &CoreClient::onSocketError);
    connect(m_socket, &QLocalSocket::readyRead, this, &CoreClient::onReadyRead);
    m_reconnectTimer->setInterval(m_options.reconnectIntervalMs);
    connect(m_reconnectTimer, &QTimer::timeout, this, &CoreClient::tryConnect);
    for (Connection c : kAllConnections)
        m_connectionStates.insert(c, ConnectionState::Off);
}

CoreClient::~CoreClient()
{
    if (m_starter && m_starter->state() != QProcess::NotRunning) {
        // systemctl nicht als Waise zurücklassen; der Start-Auftrag bei systemd läuft weiter.
        m_starter->kill();
        m_starter->waitForFinished(1000);
    }
}

void CoreClient::start()
{
    m_stopped = false;
    setStatus(QStringLiteral("Verbindung zum Kern wird aufgebaut …"));
    m_reconnectTimer->start();
    tryConnect();
}

void CoreClient::stop()
{
    m_stopped = true;
    m_reconnectTimer->stop();
    m_socket->abort();
    if (m_connected) {
        setConnected(false);
        clearData();
    }
}

std::optional<Overlay> CoreClient::overlay(const QUuid &id) const
{
    const auto it = m_overlayCache.constFind(id);
    if (it == m_overlayCache.constEnd())
        return std::nullopt;
    return *it;
}

void CoreClient::requestOverlay(const QUuid &id)
{
    if (m_connected)
        send(overlayIdMessage(msg::kGetOverlay, id));
}

void CoreClient::requestOverlayList()
{
    if (m_connected)
        send(simpleMessage(msg::kGetOverlayList));
}

std::optional<QuitReply> CoreClient::requestQuit(int timeoutMs)
{
    if (!m_connected)
        return std::nullopt;
    m_quitReply.reset();
    send(simpleMessage(msg::kQuitRequest));
    const QDeadlineTimer deadline(timeoutMs);
    m_socket->waitForBytesWritten(int(deadline.remainingTime()));
    // waitForReadyRead löst readyRead aus, onReadyRead setzt m_quitReply.
    while (!m_quitReply && m_socket->state() == QLocalSocket::ConnectedState
           && !deadline.hasExpired()) {
        if (!m_socket->waitForReadyRead(int(deadline.remainingTime())))
            break;
    }
    const std::optional<QuitReply> reply = m_quitReply;
    stop();
    return reply;
}

void CoreClient::tryConnect()
{
    if (m_stopped || m_socket->state() != QLocalSocket::UnconnectedState)
        return;
    if (m_starter && m_starter->state() != QProcess::NotRunning)
        return;
    m_lines = LineSplitter();
    m_socket->connectToServer(m_options.socketPath);
}

void CoreClient::onConnected()
{
    // Beide Seiten begrüssen sich sofort; verbunden gilt erst nach der Begrüssung des Kerns.
    send(helloMessage({m_options.program, m_options.version, kProtocolVersion}));
}

void CoreClient::onDisconnected()
{
    const bool was = m_connected;
    if (was) {
        setConnected(false);
        clearData();
    }
    if (!m_stopped && was)
        setStatus(QStringLiteral("Verbindung zum Kern verloren, neuer Versuch alle %1 s")
                      .arg(m_options.reconnectIntervalMs / 1000.0));
}

void CoreClient::onSocketError()
{
    // Bei einem gescheiterten Verbindungsversuch meldet QLocalSocket den Fehler, solange der
    // Zustand noch «Connecting» ist; erst danach wechselt er auf «Unconnected» (Qt 6.8.2,
    // QLocalSocketPrivate::setErrorAndEmit in
    // https://code.qt.io/cgit/qt/qtbase.git/tree/src/network/socket/qlocalsocket_unix.cpp?h=v6.8.2).
    // Deshalb nur der Zustand «Connected» ausschliessen.
    if (m_stopped || m_connected || m_socket->state() == QLocalSocket::ConnectedState)
        return;
    // Verbindungsversuch gescheitert.
    if (m_options.startUnit && !m_unitStartTried) {
        startUnit();
        return;
    }
    if (!m_keepStartError && !m_wasConnected)
        setStatus(QStringLiteral("Kern nicht erreichbar (%1), neuer Versuch alle %2 s")
                      .arg(socketErrorText(m_socket->error()))
                      .arg(m_options.reconnectIntervalMs / 1000.0));
}

void CoreClient::startUnit()
{
    m_unitStartTried = true;
    setStatus(QStringLiteral("Kern-Dienst wird gestartet (%1) …").arg(m_options.unitName));
    m_starter = new QProcess(this);
    // Mit Type=notify kehrt «systemctl --user start» erst zurück, wenn der Kern bereit ist.
    // https://www.freedesktop.org/software/systemd/man/latest/systemctl.html
    connect(m_starter, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        const QString stderrText = QString::fromLocal8Bit(m_starter->readAllStandardError()).trimmed();
        if (status == QProcess::NormalExit && exitCode == 0) {
            setStatus(QStringLiteral("Kern-Dienst gestartet, Verbindung wird aufgebaut …"));
            tryConnect();
            return;
        }
        m_keepStartError = true;
        setStatus(QStringLiteral("Kern-Dienst konnte nicht gestartet werden: %1")
                      .arg(stderrText.isEmpty()
                               ? QStringLiteral("systemctl endete mit Exit-Code %1").arg(exitCode)
                               : stderrText.section(QLatin1Char('\n'), 0, 0)));
    });
    connect(m_starter, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_keepStartError = true;
        setStatus(QStringLiteral("Kern-Dienst konnte nicht gestartet werden: systemctl nicht "
                                 "ausführbar (%1)").arg(m_starter->errorString()));
    });
    m_starter->start(QStringLiteral("systemctl"),
                     {QStringLiteral("--user"), QStringLiteral("start"), m_options.unitName});
}

void CoreClient::onReadyRead()
{
    m_lines.append(m_socket->readAll());
    QByteArray line;
    for (;;) {
        const LineSplitter::Result r = m_lines.next(&line);
        if (r == LineSplitter::Result::None)
            return;
        if (r == LineSplitter::Result::TooLong) {
            qWarning("Kern: Nachricht länger als %lld Byte, Verbindung getrennt",
                     static_cast<long long>(kMaxMessageBytes));
            m_socket->abort();
            return;
        }
        QJsonObject message;
        QString error;
        if (!decodeMessage(line, &message, &error)) {
            qWarning("Kern: ungültige Nachricht: %s", qPrintable(error));
            continue;
        }
        handleMessage(message);
        if (m_socket->state() != QLocalSocket::ConnectedState)
            return;
    }
}

void CoreClient::handleMessage(const QJsonObject &message)
{
    const QString type = messageType(message);
    QString error;
    if (type == QLatin1String(msg::kHello)) {
        Hello hello;
        if (!parseHello(message, &hello, &error)) {
            qWarning("Kern: Begrüssung ungültig: %s", qPrintable(error));
            m_socket->abort();
            return;
        }
        if (hello.protocolVersion != kProtocolVersion) {
            setStatus(QStringLiteral("Kern hat Schnittstelle Version %1, das Fenster kennt Version %2")
                          .arg(hello.protocolVersion).arg(kProtocolVersion));
            m_keepStartError = true;
            m_socket->abort();
            return;
        }
        m_coreHello = hello;
        m_keepStartError = false;
        m_wasConnected = true;
        setConnected(true);
        setStatus(QStringLiteral("Mit dem Kern verbunden (%1 %2)").arg(hello.program, hello.version));
        return;
    }
    if (!m_connected) {
        qWarning("Kern: Nachricht «%s» vor der Begrüssung, übergangen", qPrintable(type));
        return;
    }
    if (type == QLatin1String(msg::kOverlayList)) {
        QList<OverlaySummary> list;
        if (!parseOverlayList(message, &list, &error)) {
            qWarning("Kern: overlay_list ungültig: %s", qPrintable(error));
            return;
        }
        m_overlays = list;
        // Overlays, die es nicht mehr gibt, auch aus dem Zwischenspeicher nehmen.
        QSet<QUuid> ids;
        for (const OverlaySummary &s : std::as_const(list))
            ids.insert(s.id);
        for (auto it = m_overlayCache.begin(); it != m_overlayCache.end();) {
            if (ids.contains(it.key()))
                ++it;
            else
                it = m_overlayCache.erase(it);
        }
        emit overlayListChanged();
    } else if (type == QLatin1String(msg::kOverlay) || type == QLatin1String(msg::kOverlayChanged)) {
        Overlay o;
        if (!parseOverlayMessage(message, &o, &error)) {
            qWarning("Kern: %s ungültig: %s", qPrintable(type), qPrintable(error));
            return;
        }
        m_overlayCache.insert(o.id, o);
        // Eintrag der Liste nachführen (Name, Auflösung, Anzahl Quellen).
        bool listChanged = false;
        for (OverlaySummary &s : m_overlays) {
            if (s.id == o.id && !(s == summarizeOverlay(o))) {
                s = summarizeOverlay(o);
                listChanged = true;
            }
        }
        if (listChanged)
            emit overlayListChanged();
        emit overlayChanged(o.id);
    } else if (type == QLatin1String(msg::kOverlayRemoved)) {
        QUuid id;
        if (!parseOverlayId(message, &id, &error)) {
            qWarning("Kern: overlay_removed ungültig: %s", qPrintable(error));
            return;
        }
        m_overlayCache.remove(id);
        const qsizetype before = m_overlays.size();
        m_overlays.removeIf([&id](const OverlaySummary &s) { return s.id == id; });
        if (m_overlays.size() != before)
            emit overlayListChanged();
        emit overlayRemoved(id);
    } else if (type == QLatin1String(msg::kStats)) {
        SystemStats stats;
        if (!parseStats(message, &stats, &error)) {
            qWarning("Kern: stats ungültig: %s", qPrintable(error));
            return;
        }
        m_lastStats = stats;
        emit statsReceived(stats);
    } else if (type == QLatin1String(msg::kConnections)) {
        ConnectionStates states;
        if (!parseConnections(message, &states, &error)) {
            qWarning("Kern: connections ungültig: %s", qPrintable(error));
            return;
        }
        m_connectionStates = states;
        emit connectionStatesChanged();
    } else if (type == QLatin1String(msg::kQuitReply)) {
        QuitReply reply;
        if (!parseQuitReply(message, &reply, &error)) {
            qWarning("Kern: quit_reply ungültig: %s", qPrintable(error));
            return;
        }
        m_quitReply = reply;
        emit quitReplied(reply);
    } else if (type == QLatin1String(msg::kError)) {
        ErrorInfo info;
        if (!parseError(message, &info, &error)) {
            qWarning("Kern: error ungültig: %s", qPrintable(error));
            return;
        }
        qWarning("Kern meldet Fehler %s: %s", qPrintable(info.code), qPrintable(info.message));
        emit errorReceived(info);
    } else {
        // Unbekannte Nachrichten übergehen (docs/08-kern-schnittstelle.md, «Ablauf»).
        qWarning("Kern: unbekannte Nachricht «%s», übergangen", qPrintable(type));
    }
}

void CoreClient::send(const QJsonObject &message)
{
    m_socket->write(encodeMessage(message));
}

void CoreClient::setConnected(bool connected)
{
    if (m_connected == connected)
        return;
    m_connected = connected;
    emit connectedChanged(connected);
}

void CoreClient::setStatus(const QString &text)
{
    if (m_statusText == text)
        return;
    m_statusText = text;
    emit statusTextChanged(text);
}

void CoreClient::clearData()
{
    m_coreHello = Hello();
    m_overlays.clear();
    m_overlayCache.clear();
    m_lastStats.reset();
    for (Connection c : kAllConnections)
        m_connectionStates.insert(c, ConnectionState::Off);
    emit overlayListChanged();
    emit connectionStatesChanged();
}

} // namespace dss
