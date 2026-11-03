#include "coreservice.h"

#include "version.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTimer>

namespace {

constexpr int kStatsIntervalMs = 1000; // Leistungswerte einmal pro Sekunde (Teil E)
constexpr int kProbeTimeoutMs = 500;   // Prüfen, ob unter dem Pfad schon ein Kern antwortet
constexpr qsizetype kMaxSocketPathBytes = 107;
// Übernehmen (P10-vor, Punkt 67): höchstens zwei Dateien gleichzeitig. Eine kleine Datei wartet
// so nicht hinter einer grossen; mehr Threads teilen sich nur dieselbe Platte.
constexpr int kImportThreads = 2;

} // namespace

CoreService::CoreService(dss::OverlayStore *store, const QString &socketPath,
                         dss::CoreSettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_socketPath(socketPath)
    , m_settings(settings)
    , m_server(new QLocalServer(this))
    , m_statsTimer(new QTimer(this))
    , m_saver(new dss::SaveScheduler(this))
    , m_fontLoader(new dss::FontCatalogLoader(this))
{
    // Änderungen gelten sofort im Speicher, die Dateien schreibt m_saver zusammengefasst (Teil A).
    m_store->setDeferredWrites(true);
    connect(m_saver, &dss::SaveScheduler::saveRequested, this, &CoreService::onSaveRequested);
    connect(m_fontLoader, &dss::FontCatalogLoader::finished, this, &CoreService::onFontsLoaded);
#if DSS_CORE_IMPORT_THREAD
    // Kopie und SHA-256 neuer Dateien in eigenen Threads (P10-vor, Punkt 67); Overlays und
    // liste.json ändert der Kern weiterhin nur hier in der Hauptschleife (onImportFinished).
    m_importer = new dss::MediaImporter(m_store->mediaDir(), kImportThreads, this);
    connect(m_importer, &dss::MediaImporter::finished, this, &CoreService::onImportFinished);
#endif
    // In P07 gibt es noch keine Verbindungen: alle «nicht eingerichtet» (grau).
    for (dss::Connection c : dss::kAllConnections)
        m_connectionStates.insert(c, dss::ConnectionState::Off);
    connect(m_server, &QLocalServer::newConnection, this, &CoreService::onNewConnection);
    m_statsTimer->setInterval(kStatsIntervalMs);
    connect(m_statsTimer, &QTimer::timeout, this, &CoreService::sampleStats);
}

CoreService::~CoreService()
{
    stopImports();
    QString error;
    if (!savePending(&error))
        qCritical("Nicht gespeichert: %s", qPrintable(error));
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

QJsonObject CoreService::listMessage() const
{
    return dss::overlayListMessage(m_store->layout(), summaries(), m_store->layoutError());
}

int CoreService::stopImports()
{
    if (!m_importer || m_imports.isEmpty())
        return 0;
    const int count = m_importer->cancelAll();
    for (const PendingImport &p : std::as_const(m_imports))
        qWarning("Übernehmen abgebrochen: %s", qPrintable(p.file));
    m_imports.clear();
    return count;
}

bool CoreService::savePending(QString *error)
{
    if (!m_store->hasPendingWrites())
        return true;
    return m_store->flushPendingWrites(error);
}

void CoreService::onSaveRequested()
{
    QString error;
    if (savePending(&error))
        return;
    // Bleibt vorgemerkt; der nächste Versuch kommt mit der nächsten Änderung und beim Beenden.
    qCritical("Speichern fehlgeschlagen: %s", qPrintable(error));
    broadcast(dss::errorMessage({QString::fromLatin1(dss::err::kSaveFailed),
                                 QStringLiteral("Speichern fehlgeschlagen: ") + error, QString()}));
}

std::pair<int, int> CoreService::broadcastDifferences(const QList<dss::Overlay> &before,
                                                      const QJsonObject &listBefore)
{
    int changed = 0;
    int removed = 0;
    for (const dss::Overlay &o : m_store->overlays()) {
        const auto old = std::find_if(before.begin(), before.end(),
                                      [&o](const dss::Overlay &b) { return b.id == o.id; });
        if (old == before.end() || !(*old == o)) {
            broadcast(dss::overlayMessage(dss::msg::kOverlayChanged, o, m_store->mediaSizes(o)));
            ++changed;
        }
    }
    for (const dss::Overlay &b : before) {
        if (!m_store->overlay(b.id)) {
            broadcast(dss::overlayIdMessage(dss::msg::kOverlayRemoved, b.id));
            ++removed;
        }
    }
    const QJsonObject listAfter = listMessage();
    if (listAfter != listBefore)
        broadcast(listAfter);
    return {changed, removed};
}

void CoreService::reload()
{
    // Zuerst speichern, was noch offen ist, sonst ginge es beim Neu laden verloren.
    QString error;
    if (!savePending(&error))
        qCritical("Vor dem Neu laden nicht gespeichert: %s", qPrintable(error));
    const QList<dss::Overlay> before = m_store->overlays();
    const QJsonObject listBefore = listMessage();
    if (!m_store->load(&error)) {
        qCritical("Neu laden fehlgeschlagen: %s", qPrintable(error));
        return;
    }
    for (const QString &e : m_store->loadErrors())
        qWarning("Fehler beim Laden: %s", qPrintable(e));
    for (const QString &w : m_store->loadWarnings())
        qWarning("Warnung: %s", qPrintable(w));

    const auto [changed, removed] = broadcastDifferences(before, listBefore);
    qInfo("Neu geladen: %lld Overlays, %d geändert oder neu, %d entfernt",
          static_cast<long long>(m_store->overlays().size()), changed, removed);
    // Schriftdateien im Datenordner können sich geändert haben.
    if (m_fontsLoaded) {
        m_fontsLoaded = false;
        m_broadcastFonts = true;
        startFontLoading();
    }
}

void CoreService::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        m_clients.insert(socket, Client());
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] { onDisconnected(socket); });
        // Der Kern grüsst zuerst (docs/08-kern-schnittstelle.md, «Ablauf»).
        // Datenordner im hello (P09): Das Fenster liest Bilder, Videos und Schriften von dort.
        send(socket, dss::helloMessage({QStringLiteral("dss-core"), dss::version(),
                                        dss::kProtocolVersion,
                                        QFileInfo(m_store->dataDir()).absoluteFilePath()}));
    }
}

void CoreService::onDisconnected(QLocalSocket *socket)
{
    m_fontWaiters.removeAll(QPointer<QLocalSocket>(socket));
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
        send(socket, listMessage());
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
    if (dss::isChangeType(type)) {
        handleChange(socket, message);
    } else if (type == QLatin1String(dss::msg::kGetFonts)) {
        handleGetFonts(socket);
    } else if (type == QLatin1String(dss::msg::kGetSettings) && m_settings) {
        send(socket, settingsMessage());
    } else if (type == QLatin1String(dss::msg::kUpdateSettings) && m_settings) {
        handleSettingsUpdate(socket, message);
    } else if (type == QLatin1String(dss::msg::kGetOverlayList)) {
        send(socket, listMessage());
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
        send(socket, dss::overlayMessage(dss::msg::kOverlay, *o, m_store->mediaSizes(*o)));
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

QJsonObject CoreService::settingsMessage() const
{
    return dss::settingsMessage({m_settings->settings(), dss::kPortMin, dss::kPortMax,
                                 m_settings->path(), m_settings->fileError()});
}

void CoreService::handleSettingsUpdate(QLocalSocket *socket, const QJsonObject &message)
{
    dss::SettingsUpdate update;
    QString error;
    if (!dss::parseSettingsUpdate(message, &update, &error)) {
        const qint64 id = dss::messageRequestId(message);
        if (id < 0) {
            sendError(socket, dss::err::kInvalidMessage, error, dss::messageType(message));
        } else {
            qWarning("Einstellungen ungültig: %s", qPrintable(error));
            send(socket, dss::changeResultMessage(
                             {id, false, QUuid(), QString::fromLatin1(dss::err::kInvalidMessage), error}));
        }
        return;
    }
    const dss::CoreSettings before = m_settings->settings();
    if (!m_settings->change(update.port, update.homeAddress, &error)) {
        qWarning("Einstellungen abgelehnt: %s", qPrintable(error));
        send(socket, dss::changeResultMessage({update.requestId, false, QUuid(),
                                               QString::fromLatin1(dss::err::kRejected), error}));
        return;
    }
    const dss::CoreSettings after = m_settings->settings();
    if (!(after == before)) {
        // Port und Adresse wirken erst mit dem Overlay-Server (P10).
        qInfo("Einstellungen geändert: Port %d, Adresse im Heimnetz %s", after.port,
              after.homeAddress.isEmpty() ? "leer" : qPrintable(after.homeAddress));
        broadcast(settingsMessage());
    }
    send(socket, dss::changeResultMessage({update.requestId, true, QUuid(), QString(), QString()}));
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

void CoreService::handleChange(QLocalSocket *socket, const QJsonObject &message)
{
    dss::ChangeRequest request;
    QString error;
    if (!dss::parseChangeRequest(message, &request, &error)) {
        const qint64 id = dss::messageRequestId(message);
        if (id < 0) {
            sendError(socket, dss::err::kInvalidMessage, error, dss::messageType(message));
        } else {
            qWarning("Änderung %s ungültig: %s", qPrintable(dss::messageType(message)), qPrintable(error));
            send(socket, dss::changeResultMessage(
                             {id, false, QUuid(), QString::fromLatin1(dss::err::kInvalidMessage), error}));
        }
        return;
    }
    dss::MediaUse use = dss::MediaUse::Image;
    const QString file = m_importer ? fileToImport(request, &use) : QString();
    if (!file.isEmpty()) {
        // Zuerst übernehmen (Thread), dann ändern (onImportFinished). Kennungen jetzt schon
        // prüfen, damit eine falsche Kennung nicht erst nach dem Kopieren auffällt; nach dem
        // Übernehmen prüft applyChange sie nochmals (Overlay oder Quelle können inzwischen
        // gelöscht sein).
        if (std::optional<dss::ChangeResult> failure = checkIds(request)) {
            failure->requestId = request.requestId;
            qWarning("Änderung %s abgelehnt: %s", qPrintable(request.type), qPrintable(failure->message));
            send(socket, dss::changeResultMessage(*failure));
            return;
        }
        const quint64 job = m_importer->start(file, use);
        PendingImport pending{socket, request, file, {}};
        pending.timer.start();
        m_imports.insert(job, pending);
        qInfo("Übernehme %s für %s (%lld laufend)", qPrintable(file), qPrintable(request.type),
              static_cast<long long>(m_imports.size()));
        return;
    }
    finishChange(socket, request, nullptr);
}

void CoreService::finishChange(QLocalSocket *socket, const dss::ChangeRequest &request,
                               const ImportedFile *imported)
{
    const QList<dss::Overlay> before = m_store->overlays();
    const QJsonObject listBefore = listMessage();
    dss::ChangeResult result = applyChange(request, imported);
    result.requestId = request.requestId;
    // Zuerst an alle (auch an das Fenster, das die Änderung geschickt hat), dann die Antwort:
    // Das Fenster übernimmt den Zustand nur aus diesen Nachrichten (Teil A).
    broadcastDifferences(before, listBefore);
    if (m_store->hasPendingWrites())
        m_saver->changed();
    if (!result.ok)
        qWarning("Änderung %s abgelehnt: %s", qPrintable(request.type), qPrintable(result.message));
    // Ist das Fenster inzwischen getrennt (nach einem Übernehmen), gilt die Änderung trotzdem.
    if (socket && m_clients.contains(socket))
        send(socket, dss::changeResultMessage(result));
}

QString CoreService::fileToImport(const dss::ChangeRequest &r, dss::MediaUse *use) const
{
    using namespace dss;
    const QString &type = r.type;
    if (type == QLatin1String(msg::kAddImageSource) || type == QLatin1String(msg::kReplaceImageFile)) {
        *use = MediaUse::Image;
        return r.file;
    }
    if (type == QLatin1String(msg::kAddVideoSource)) {
        *use = MediaUse::Video;
        return r.file;
    }
    if (type == QLatin1String(msg::kAddTextSource)) {
        *use = MediaUse::Font;
        return defaultTextFontFile();
    }
    // Schrift: ein Verweis "media/…" ist schon übernommen.
    if (type == QLatin1String(msg::kSetTextFont) && !validateMediaReference(r.file).isEmpty()) {
        *use = MediaUse::Font;
        return r.file;
    }
    return {};
}

void CoreService::onImportFinished(quint64 job, const dss::MediaImportResult &result)
{
    const auto it = m_imports.find(job);
    if (it == m_imports.end())
        return;
    const PendingImport pending = *it;
    m_imports.erase(it);
    const double seconds = pending.timer.elapsed() / 1000.0;
    if (result.reference.isEmpty()) {
        qWarning("Übernehmen von %s gescheitert nach %.1f s: %s", qPrintable(pending.file), seconds,
                 qPrintable(result.error));
        const dss::ChangeResult failure{pending.request.requestId, false, QUuid(),
                                        QString::fromLatin1(dss::err::kRejected), result.error};
        qWarning("Änderung %s abgelehnt: %s", qPrintable(pending.request.type), qPrintable(result.error));
        if (pending.socket && m_clients.contains(pending.socket.data()))
            send(pending.socket.data(), dss::changeResultMessage(failure));
        return;
    }
    qInfo("Übernommen: %s → %s (%.1f MiB, %.1f s%s)", qPrintable(pending.file),
          qPrintable(result.reference), result.bytes / 1048576.0, seconds,
          result.existed ? ", war schon vorhanden" : "");
    const ImportedFile imported{result.reference, QFileInfo(pending.file).fileName()};
    finishChange(pending.socket.data(), pending.request, &imported);
}

std::optional<dss::ChangeResult> CoreService::checkIds(const dss::ChangeRequest &r) const
{
    using namespace dss;
    const auto fail = [](const char *code, const QString &message) {
        return ChangeResult{0, false, QUuid(), QString::fromLatin1(code), message};
    };
    const QString type = r.type;
    const auto is = [&type](const char *name) { return type == QLatin1String(name); };
    const QString idText = r.overlayId.toString(QUuid::WithoutBraces);

    const bool needsOverlay = !is(msg::kCreateOverlay) && !is(msg::kCreateFolder)
                                  && !is(msg::kRenameFolder) && !is(msg::kRemoveFolder)
                                  && !is(msg::kMoveFolder) && !is(msg::kSetFolderExpanded);
    const Overlay *overlay = m_store->overlay(r.overlayId);
    if (needsOverlay && !overlay)
        return fail(err::kUnknownOverlay, QStringLiteral("Kein Overlay mit der Kennung %1").arg(idText));
    const bool needsSource = is(msg::kRemoveSource) || is(msg::kMoveSource) || is(msg::kUpdateSource)
                             || is(msg::kSetTextFont) || is(msg::kReplaceImageFile);
    if (needsSource && sourceIndex(*overlay, r.sourceId) < 0)
        return fail(err::kUnknownSource, QStringLiteral("Keine Quelle mit der Kennung %1 im Overlay «%2»")
                                             .arg(r.sourceId.toString(QUuid::WithoutBraces), overlay->name));
    const bool needsFolder = is(msg::kRenameFolder) || is(msg::kRemoveFolder) || is(msg::kMoveFolder)
                             || is(msg::kSetFolderExpanded)
                             || (is(msg::kMoveOverlay) && !r.folderId.isNull());
    if (needsFolder && !m_store->layout().folder(r.folderId))
        return fail(err::kUnknownFolder, QStringLiteral("Kein Ordner mit der Kennung %1")
                                             .arg(r.folderId.toString(QUuid::WithoutBraces)));
    return std::nullopt;
}

dss::ChangeResult CoreService::applyChange(const dss::ChangeRequest &r, const ImportedFile *imported)
{
    using namespace dss;
    const auto fail = [](const char *code, const QString &message) {
        return ChangeResult{0, false, QUuid(), QString::fromLatin1(code), message};
    };
    const auto done = [](bool ok, const QString &error, const QUuid &id = QUuid()) {
        return ok ? ChangeResult{0, true, id, QString(), QString()}
                  : ChangeResult{0, false, QUuid(), QString::fromLatin1(err::kRejected), error};
    };
    const QString type = r.type;
    const auto is = [&type](const char *name) { return type == QLatin1String(name); };

    // Kennungen zuerst prüfen, damit das Fenster den Grund unterscheiden kann.
    if (std::optional<ChangeResult> failure = checkIds(r))
        return *failure;
    // Schon übernommene Datei (Thread, P10-vor) statt des Pfads des Nutzers.
    const QString file = imported ? imported->reference : r.file;
    const QString sourceName = imported ? imported->sourceName : QString();

    QString error;
    if (is(msg::kCreateOverlay)) {
        const QUuid id = m_store->createOverlay(r.name, r.width, r.height, &error);
        return done(!id.isNull(), error, id);
    }
    if (is(msg::kRenameOverlay))
        return done(m_store->renameOverlay(r.overlayId, r.name, r.changeAddress, &error), error);
    if (is(msg::kCopyOverlay)) {
        const QUuid id = m_store->copyOverlay(r.overlayId, &error);
        return done(!id.isNull(), error, id);
    }
    if (is(msg::kRemoveOverlay))
        return done(m_store->removeOverlay(r.overlayId, &error), error);
    if (is(msg::kMoveOverlay))
        return done(m_store->moveOverlayInList(r.overlayId, r.folderId, r.index, &error), error);
    if (is(msg::kCreateFolder)) {
        const QUuid id = m_store->createFolder(r.name, &error);
        return done(!id.isNull(), error, id);
    }
    if (is(msg::kRenameFolder))
        return done(m_store->renameFolder(r.folderId, r.name, &error), error);
    if (is(msg::kRemoveFolder))
        return done(m_store->removeFolder(r.folderId, &error), error);
    if (is(msg::kMoveFolder))
        return done(m_store->moveFolderInList(r.folderId, r.index, &error), error);
    if (is(msg::kSetFolderExpanded))
        return done(m_store->setFolderExpanded(r.folderId, r.expanded, &error), error);
    if (is(msg::kAddImageSource) || is(msg::kAddVideoSource)) {
        // Grösse der Datei (P09, Entscheid 6); nicht lesbar: Grösse des Overlays und Hinweis.
        // Video: Standard «nicht wiederholen» und «nicht anhalten» (docs/01-festlegungen.md).
        QString warning;
        const QUuid id = is(msg::kAddImageSource)
                             ? m_store->addImageSource(r.overlayId, file, &error, &warning, sourceName)
                             : m_store->addVideoSource(r.overlayId, file, false, &error, &warning,
                                                       false, sourceName);
        ChangeResult result = done(!id.isNull(), error, id);
        if (result.ok && !warning.isEmpty()) {
            qWarning("%s", qPrintable(warning));
            result.warning = warning;
        }
        return result;
    }
    if (is(msg::kSetOverlaySize))
        return done(m_store->setOverlaySize(r.overlayId, r.width, r.height, &error), error);
    if (is(msg::kAddTextSource)) {
        // Inhalt «Text», Standardschrift Be Vietnam Pro (Entscheid 4, E-11)
        const QUuid id = m_store->addTextSource(r.overlayId, QStringLiteral("Text"),
                                                imported ? file : defaultTextFontFile(), &error);
        if (!id.isNull()) {
            const Overlay *o = m_store->overlay(r.overlayId);
            addMediaFont(o->sources.at(sourceIndex(*o, id)).text.fontFile);
        }
        return done(!id.isNull(), error, id);
    }
    if (is(msg::kRemoveSource))
        return done(m_store->removeSource(r.overlayId, r.sourceId, &error), error);
    if (is(msg::kMoveSource))
        return done(m_store->moveSource(r.overlayId, r.sourceId, r.index, &error), error);
    if (is(msg::kUpdateSource))
        return done(m_store->updateSource(r.overlayId, r.sourceId, r.changes, &error), error);
    if (is(msg::kSetTextFont)) {
        int weight = r.weight.value_or(400);
        bool italic = r.italic.value_or(false);
        if (!r.weight || !r.italic) {
            // Eigene Datei: Gewicht und kursiv stehen in der Datei (fc-query).
            const QString path = validateMediaReference(file).isEmpty()
                                     ? m_store->mediaFilePath(file) : file;
            const QList<FontFace> faces = queryFontFiles({path}, FontOrigin::Media, &error);
            if (faces.isEmpty())
                return fail(err::kRejected, QStringLiteral("%1 ist keine lesbare Schriftdatei%2")
                                                .arg(QFileInfo(r.file).fileName(),
                                                     error.isEmpty() ? QString() : QStringLiteral(" (") + error + QLatin1Char(')')));
            weight = r.weight.value_or(faces.first().weight);
            italic = r.italic.value_or(faces.first().italic);
            error.clear();
        }
        QString reference;
        const bool ok = m_store->setTextFont(r.overlayId, r.sourceId, file, weight, italic, &error,
                                             &reference);
        if (ok)
            addMediaFont(reference);
        return done(ok, error);
    }
    if (is(msg::kReplaceImageFile))
        return done(m_store->replaceImageFile(r.overlayId, r.sourceId, file, &error), error);
    return fail(err::kUnknownType, QStringLiteral("Unbekannte Änderung «%1»").arg(type));
}

void CoreService::handleGetFonts(QLocalSocket *socket)
{
    if (m_fontsLoaded) {
        send(socket, dss::fontListMessage(m_fonts, m_fontError));
        return;
    }
    m_fontWaiters.append(socket);
    startFontLoading();
}

void CoreService::startFontLoading()
{
    if (m_fontLoader->isRunning())
        return;
    // Schriftdateien, die schon im Datenordner liegen, gehören zur Auswahl (Kopien eigener Dateien).
    QStringList files;
    QStringList references;
    const QString mediaDir = m_store->dataDir() + QStringLiteral("/media");
    for (const QString &file : dss::fontFilesIn(mediaDir)) {
        files.append(file);
        references.append(QStringLiteral("media/") + QFileInfo(file).fileName());
    }
    m_fontLoader->start(files, references);
}

void CoreService::onFontsLoaded(const QList<dss::FontFace> &faces, const QString &error)
{
    m_fonts = faces;
    m_fontError = error;
    m_fontsLoaded = true;
    if (!error.isEmpty())
        qWarning("Schriften: %s", qPrintable(error));
    qInfo("Schriften geladen: %lld Schnitte", static_cast<long long>(faces.size()));
    const QJsonObject message = dss::fontListMessage(m_fonts, m_fontError);
    if (m_broadcastFonts) {
        m_broadcastFonts = false;
        m_fontWaiters.clear();
        broadcast(message);
        return;
    }
    const QList<QPointer<QLocalSocket>> waiters = m_fontWaiters;
    m_fontWaiters.clear();
    for (const QPointer<QLocalSocket> &socket : waiters) {
        if (socket && m_clients.contains(socket.data()))
            send(socket.data(), message);
    }
}

void CoreService::addMediaFont(const QString &reference)
{
    // Ohne geladene Liste nichts zu tun: Die Kopie kommt beim Laden mit fc-query dazu.
    if (!m_fontsLoaded || reference.isEmpty() || dss::fontFaceForFile(m_fonts, reference))
        return;
    QString error;
    QList<dss::FontFace> faces = dss::queryFontFiles({m_store->mediaFilePath(reference)},
                                                     dss::FontOrigin::Media, &error);
    if (faces.isEmpty()) {
        qWarning("Schrift %s: %s", qPrintable(reference), qPrintable(error));
        return;
    }
    for (dss::FontFace &f : faces)
        f.file = reference;
    m_fonts.append(faces);
    dss::sortFontFaces(&m_fonts);
    broadcast(dss::fontListMessage(m_fonts, m_fontError));
}
