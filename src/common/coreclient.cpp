#include "coreclient.h"

#include <QDeadlineTimer>
#include <QFileInfo>
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

void CoreClient::requestMediaList()
{
    if (m_connected)
        send(simpleMessage(msg::kGetMediaList));
}

void CoreClient::requestOverlayList()
{
    if (m_connected)
        send(simpleMessage(msg::kGetOverlayList));
}

void CoreClient::requestFonts()
{
    if (m_connected)
        send(simpleMessage(msg::kGetFonts));
}

void CoreClient::requestSettings()
{
    if (m_connected)
        send(simpleMessage(msg::kGetSettings));
}

void CoreClient::requestBrowserSource(const QUuid &id)
{
    if (id == m_browserSourceWanted)
        return;
    m_browserSourceWanted = id;
    if (m_browserSource) {
        m_browserSource.reset();
        emit browserSourceChanged();
    }
    if (m_connected)
        send(getBrowserSourceMessage(id));
}

qint64 CoreClient::sendSettingsUpdate(SettingsUpdate update)
{
    if (!m_connected)
        return 0;
    update.requestId = m_nextRequestId++;
    m_settingsRequests.insert(update.requestId);
    send(settingsUpdateMessage(update));
    return update.requestId;
}

qint64 CoreClient::sendSettingsReset()
{
    if (!m_connected)
        return 0;
    const qint64 id = m_nextRequestId++;
    m_settingsRequests.insert(id);
    send(resetSettingsMessage(id));
    return id;
}

ObsSourceInfo CoreClient::obsSource(const QUuid &overlayId) const
{
    for (const ObsSourceInfo &s : m_obsSources) {
        if (s.overlayId == overlayId)
            return s;
    }
    return {overlayId, ObsSourceState::None, QString()};
}

qint64 CoreClient::sendObsCreateSource(const QUuid &overlayId, const QString &sceneUuid, const QString &name)
{
    if (!m_connected)
        return 0;
    const qint64 id = m_nextRequestId++;
    m_obsRequests.insert(id);
    send(obsCreateSourceMessage({id, overlayId, sceneUuid, name}));
    return id;
}

qint64 CoreClient::sendObsAddToScene(const QUuid &overlayId, const QString &sceneUuid)
{
    if (!m_connected)
        return 0;
    const qint64 id = m_nextRequestId++;
    m_obsRequests.insert(id);
    send(obsAddToSceneMessage({id, overlayId, sceneUuid, QString()}));
    return id;
}

qint64 CoreClient::sendChange(ChangeRequest request, const QString &coalesceKey)
{
    if (!m_connected)
        return 0;
    if (!coalesceKey.isEmpty() && m_requestOfKey.contains(coalesceKey)) {
        m_heldChanges.insert(coalesceKey, request); // ersetzt eine ältere zurückgehaltene
        return -1;
    }
    return transmit(request, coalesceKey);
}

qint64 CoreClient::transmit(ChangeRequest request, const QString &coalesceKey)
{
    request.requestId = m_nextRequestId++;
    if (!coalesceKey.isEmpty()) {
        m_keyOfRequest.insert(request.requestId, coalesceKey);
        m_requestOfKey.insert(coalesceKey, request.requestId);
    }
    send(changeRequestMessage(request));
    // Änderungen, für die der Kern zuerst eine Datei übernimmt (P10-vor): Hinweis im Fenster.
    // Ein Verweis "media/…" (Schrift aus dem Datenordner) ist schon übernommen.
    // Seit P10b auch add_media, und Bild und Video aus der Medienliste sind schon übernommen.
    const bool imports = (request.type == QLatin1String(msg::kAddImageSource)
                          || request.type == QLatin1String(msg::kAddVideoSource)
                          || request.type == QLatin1String(msg::kReplaceImageFile)
                          || request.type == QLatin1String(msg::kSetTextFont)
                          || request.type == QLatin1String(msg::kAddMedia))
                         && !validateMediaReference(request.file).isEmpty();
    if (imports) {
        m_importNames.insert(request.requestId, QFileInfo(request.file).fileName());
        emit pendingImportsChanged();
    }
    return request.requestId;
}

QStringList CoreClient::pendingImports() const
{
    return m_importNames.values();
}

bool CoreClient::isChangePending(const QString &coalesceKey) const
{
    return m_requestOfKey.contains(coalesceKey) || m_heldChanges.contains(coalesceKey);
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
        if (!m_browserSourceWanted.isNull())
            send(getBrowserSourceMessage(m_browserSourceWanted));
        return;
    }
    if (!m_connected) {
        qWarning("Kern: Nachricht «%s» vor der Begrüssung, übergangen", qPrintable(type));
        return;
    }
    if (type == QLatin1String(msg::kOverlayList)) {
        OverlayListData list;
        if (!parseOverlayList(message, &list, &error)) {
            qWarning("Kern: overlay_list ungültig: %s", qPrintable(error));
            return;
        }
        m_list = list;
        // Overlays, die es nicht mehr gibt, auch aus dem Zwischenspeicher nehmen.
        QSet<QUuid> ids;
        for (const OverlaySummary &s : std::as_const(list.overlays))
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
        MediaSizes sizes;
        if (!parseOverlayMessage(message, &o, &sizes, &error)) {
            qWarning("Kern: %s ungültig: %s", qPrintable(type), qPrintable(error));
            return;
        }
        m_overlayCache.insert(o.id, o);
        // Dateien sind nach Inhalt benannt: Eine Grösse gilt für alle Overlays.
        m_mediaSizes.insert(sizes);
        // Eintrag der Liste nachführen (Name, Auflösung, Anzahl Quellen).
        bool listChanged = false;
        for (OverlaySummary &s : m_list.overlays) {
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
        const qsizetype before = m_list.overlays.size();
        m_list.overlays.removeIf([&id](const OverlaySummary &s) { return s.id == id; });
        removeOverlayFromLayout(&m_list.layout, id);
        if (m_list.overlays.size() != before)
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
    } else if (type == QLatin1String(msg::kFontList)) {
        QList<FontFace> faces;
        QString fontError;
        if (!parseFontList(message, &faces, &fontError, &error)) {
            qWarning("Kern: font_list ungültig: %s", qPrintable(error));
            return;
        }
        m_fonts = faces;
        m_fontError = fontError;
        emit fontsChanged();
    } else if (type == QLatin1String(msg::kSettings)) {
        SettingsInfo info;
        if (!parseSettings(message, &info, &error)) {
            qWarning("Kern: settings ungültig: %s", qPrintable(error));
            return;
        }
        m_settings = info;
        emit settingsChanged();
    } else if (type == QLatin1String(msg::kMediaList)) {
        MediaListData data;
        if (!parseMediaList(message, &data, &error)) {
            qWarning("Kern: media_list ungültig: %s", qPrintable(error));
            return;
        }
        m_mediaList = data;
        emit mediaListChanged();
    } else if (type == QLatin1String(msg::kImportProgress)) {
        QList<ImportProgressData> imports;
        if (!parseImportProgress(message, &imports, &error)) {
            qWarning("Kern: import_progress ungültig: %s", qPrintable(error));
            return;
        }
        m_imports = imports;
        emit importProgressChanged();
    } else if (type == QLatin1String(msg::kServerStatus)) {
        ServerStatusInfo info;
        if (!parseServerStatus(message, &info, &error)) {
            qWarning("Kern: server_status ungültig: %s", qPrintable(error));
            return;
        }
        m_serverStatus = info;
        emit serverStatusChanged();
    } else if (type == QLatin1String(msg::kObsStatus)) {
        ObsStatusInfo info;
        if (!parseObsStatus(message, &info, &error)) {
            qWarning("Kern: obs_status ungültig: %s", qPrintable(error));
            return;
        }
        m_obsStatus = info;
        emit obsStatusChanged();
    } else if (type == QLatin1String(msg::kObsSources)) {
        QList<ObsSourceInfo> sources;
        if (!parseObsSources(message, &sources, &error)) {
            qWarning("Kern: obs_sources ungültig: %s", qPrintable(error));
            return;
        }
        m_obsSources = sources;
        emit obsSourcesChanged();
    } else if (type == QLatin1String(msg::kBrowserSource)) {
        BrowserSourceInfo info;
        if (!parseBrowserSource(message, &info, &error)) {
            qWarning("Kern: browser_source ungültig: %s", qPrintable(error));
            return;
        }
        // Antwort auf eine frühere Anfrage (inzwischen anderes Overlay gewählt): übergehen.
        if (info.overlayId != m_browserSourceWanted)
            return;
        m_browserSource = info;
        emit browserSourceChanged();
    } else if (type == QLatin1String(msg::kChangeResult)) {
        ChangeResult result;
        if (!parseChangeResult(message, &result, &error)) {
            qWarning("Kern: change_result ungültig: %s", qPrintable(error));
            return;
        }
        if (m_settingsRequests.remove(result.requestId)) {
            emit settingsUpdateFinished(result);
            return;
        }
        if (m_obsRequests.remove(result.requestId)) {
            emit obsRequestFinished(result);
            return;
        }
        const QString key = m_keyOfRequest.take(result.requestId);
        if (!key.isEmpty() && m_requestOfKey.value(key) == result.requestId) {
            m_requestOfKey.remove(key);
            // Die zuletzt zurückgehaltene Änderung zu diesem Schlüssel jetzt schicken.
            if (m_heldChanges.contains(key))
                transmit(m_heldChanges.take(key), key);
        }
        const bool imported = m_importNames.remove(result.requestId) > 0;
        emit changeFinished(result);
        if (imported)
            emit pendingImportsChanged();
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
    m_list = OverlayListData();
    m_overlayCache.clear();
    m_mediaSizes.clear();
    m_fonts.clear();
    m_fontError.clear();
    m_keyOfRequest.clear();
    m_requestOfKey.clear();
    m_heldChanges.clear();
    m_settingsRequests.clear();
    m_obsRequests.clear();
    const bool hadImports = !m_importNames.isEmpty();
    m_importNames.clear();
    if (hadImports)
        emit pendingImportsChanged();
    m_lastStats.reset();
    const bool hadSettings = m_settings.has_value();
    m_settings.reset();
    if (hadSettings)
        emit settingsChanged();
    if (m_browserSource) {
        m_browserSource.reset();
        emit browserSourceChanged();
    }
    if (m_serverStatus) {
        m_serverStatus.reset();
        emit serverStatusChanged();
    }
    if (m_obsStatus) {
        m_obsStatus.reset();
        emit obsStatusChanged();
    }
    if (!m_obsSources.isEmpty()) {
        m_obsSources.clear();
        emit obsSourcesChanged();
    }
    m_mediaList = MediaListData();
    emit mediaListChanged();
    if (!m_imports.isEmpty()) {
        m_imports.clear();
        emit importProgressChanged();
    }
    for (Connection c : kAllConnections)
        m_connectionStates.insert(c, ConnectionState::Off);
    emit overlayListChanged();
    emit connectionStatesChanged();
}

} // namespace dss
