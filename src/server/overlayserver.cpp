#include "overlayserver.h"

#include "browsersource.h"
#include "httpmessage.h"
#include "imageorientation.h"
#include "overlayjson.h"
#include "overlaystore.h"
#include "portowner.h"
#include "sourcegeometry.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace dss {

namespace {

// Dateien werden in Stücken geschickt; neues Stück erst, wenn weniger als kLowWater unterwegs ist.
constexpr qint64 kChunkBytes = 256 * 1024;
constexpr qint64 kLowWater = 512 * 1024;
// Höchstens so viele Meldungen der Seiten pro Minute ins Protokoll.
constexpr int kMaxReportsPerMinute = 20;
constexpr qsizetype kMaxReportChars = 300;
constexpr qint64 kInvalidLogIntervalMs = 60 * 1000;

// Leere, durchsichtige Seite für unbekannte Adressen (Teil A 5). Neu laden alle 10 s, damit das
// Fenster den Hinweis zeigt, solange die Browserquelle die alte Adresse benutzt.
const QByteArray kEmptyPage =
    "<!doctype html><html><head><meta charset=\"utf-8\"><meta http-equiv=\"refresh\" content=\"10\">"
    "<style>html,body{margin:0;background:transparent}</style></head><body></body></html>\n";

// Teile einer Adresse: nur Zeichen, die addressFromName erzeugt.
bool plausibleAddress(const QString &address)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9-]{1,200}$"));
    return pattern.match(address).hasMatch();
}

QString peerText(const QTcpSocket *socket)
{
    QHostAddress a = socket->peerAddress();
    bool ok = false;
    const QHostAddress v4(a.toIPv4Address(&ok));
    return ok ? v4.toString() : a.toString();
}

const QList<std::pair<QByteArray, QByteArray>> kNoStore = {{"Cache-Control", "no-store"}};

} // namespace

struct OverlayServer::Connection
{
    QByteArray buffer;
    QString peer;
    bool keepAlive = true;
    bool closing = false;
    // Datei wird geschickt
    std::unique_ptr<QFile> file;
    qint64 position = 0;
    qint64 remaining = 0;
    BytePatches patches;
    // Server-Sent Events
    bool events = false;
    QUuid overlayId;
    QString address;
    QString token;
    QByteArray lastData;
};

OverlayServer::OverlayServer(const OverlayStore *store, const Options &options, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_options(options)
    , m_retryTimer(new QTimer(this))
    , m_heartbeatTimer(new QTimer(this))
{
    if (!m_options.localAddresses)
        m_options.localAddresses = [] { return QNetworkInterface::allAddresses(); };
    m_retryTimer->setInterval(m_options.retryIntervalMs);
    connect(m_retryTimer, &QTimer::timeout, this, &OverlayServer::retry);
    m_heartbeatTimer->setInterval(m_options.heartbeatMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &OverlayServer::heartbeat);
    m_reportWindow.start();
}

OverlayServer::~OverlayServer()
{
    stop();
}

void OverlayServer::stop()
{
    m_retryTimer->stop();
    m_heartbeatTimer->stop();
    const QList<QTcpSocket *> sockets = m_connections.keys();
    m_connections.clear();
    for (QTcpSocket *s : sockets) {
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    qDeleteAll(m_servers);
    m_servers.clear();
    m_homeListening = false;
    m_loopbackListening = false;
}

void OverlayServer::configure(int port, const QString &homeAddress)
{
    if (port == m_port && homeAddress == m_homeAddress && !m_servers.isEmpty())
        return;
    stop();
    m_port = port;
    m_homeAddress = homeAddress;
    m_listenProblems.clear();
    listenAll();
    m_retryTimer->start();
    m_heartbeatTimer->start();
    updateStatus();
}

void OverlayServer::listenAll()
{
    const auto listenOn = [&](const QHostAddress &address, bool *flag) {
        if (*flag)
            return;
        auto *server = new QTcpServer(this);
        if (!server->listen(address, quint16(m_port))) {
            QString problem;
            if (server->serverError() == QAbstractSocket::AddressInUseError)
                problem = portBusyMessage(m_port, findPortOwner(m_port, m_options.procRoot),
                                          m_options.installedDir);
            else
                problem = QStringLiteral("Der Overlay-Server kann nicht auf %1:%2 lauschen: %3")
                              .arg(address.toString()).arg(m_port).arg(server->errorString());
            if (!m_listenProblems.contains(problem)) {
                m_listenProblems.append(problem);
                qWarning("Overlay-Server: %s", qPrintable(problem));
            }
            delete server;
            return;
        }
        connect(server, &QTcpServer::newConnection, this, [this, server] { onNewConnection(server); });
        m_servers.append(server);
        *flag = true;
        qInfo("Overlay-Server lauscht auf %s:%d", qPrintable(address.toString()), m_port);
    };
    m_listenProblems.clear();
    listenOn(QHostAddress(QHostAddress::LocalHost), &m_loopbackListening);
    if (m_homeAddress.isEmpty())
        return;
    const QHostAddress home(m_homeAddress);
    if (m_options.localAddresses().contains(home))
        listenOn(home, &m_homeListening);
}

void OverlayServer::retry()
{
    const bool homePresent = !m_homeAddress.isEmpty()
                             && m_options.localAddresses().contains(QHostAddress(m_homeAddress));
    if (!m_loopbackListening || (homePresent && !m_homeListening)) {
        const bool homeBefore = m_homeListening;
        listenAll();
        if (m_homeListening && !homeBefore)
            qInfo("Overlay-Server: Adresse im Heimnetz %s ist da", qPrintable(m_homeAddress));
    }
    updateStatus();
}

void OverlayServer::updateStatus()
{
    Status s;
    for (const QTcpServer *server : std::as_const(m_servers))
        s.listening.append(QStringLiteral("%1:%2").arg(server->serverAddress().toString()).arg(m_port));
    s.problems = m_listenProblems;
    if (m_homeAddress.isEmpty()) {
        s.problems.append(QStringLiteral("Keine Adresse im Heimnetz eingestellt. Die Browserquelle ist "
                                         "nur auf diesem PC erreichbar (127.0.0.1)."));
    } else if (!m_options.localAddresses().contains(QHostAddress(m_homeAddress))) {
        s.problems.append(QStringLiteral("Die Adresse im Heimnetz %1 ist auf diesem PC gerade nicht "
                                         "vorhanden (Netzwerkkabel?). Der Overlay-Server lauscht bis dahin "
                                         "nur auf 127.0.0.1 und startet auf %1 von selbst, sobald sie da ist.")
                              .arg(m_homeAddress));
    }
    if (m_lastInvalid.isValid() && m_lastInvalid.elapsed() < m_options.invalidHintMs)
        s.invalidUse = m_lastInvalidText;
    for (const auto &c : std::as_const(m_connections)) {
        if (c->events)
            s.pages[c->overlayId] += 1;
    }
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}

void OverlayServer::onNewConnection(QTcpServer *server)
{
    while (QTcpSocket *socket = server->nextPendingConnection()) {
        auto c = std::make_shared<Connection>();
        c->peer = peerText(socket);
        m_connections.insert(socket, c);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::bytesWritten, this, [this, socket] { onBytesWritten(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { onDisconnected(socket); });
    }
}

void OverlayServer::onDisconnected(QTcpSocket *socket)
{
    const std::shared_ptr<Connection> c = m_connections.take(socket);
    socket->deleteLater();
    if (c && c->events) {
        qInfo("Browserquelle getrennt: %s von %s", qPrintable(browserSourceLogPath(c->address)),
              qPrintable(c->peer));
        updateStatus();
    }
}

void OverlayServer::onReadyRead(QTcpSocket *socket)
{
    const std::shared_ptr<Connection> c = m_connections.value(socket);
    if (!c)
        return;
    c->buffer.append(socket->readAll());
    // Eine Antwort nach der anderen: Während eine Datei läuft oder Ereignisse fliessen, warten
    // weitere Anfragen im Puffer.
    while (!c->file && !c->events && !c->closing) {
        HttpRequest request;
        const ParseResult r = takeHttpRequest(&c->buffer, &request);
        if (r == ParseResult::Incomplete)
            return;
        if (r == ParseResult::Invalid) {
            c->keepAlive = false;
            HttpRequest bad;
            bad.version = "HTTP/1.1";
            respond(socket, *c, bad, 400, "text/plain; charset=utf-8", "Ungültige Anfrage\n", kNoStore);
            return;
        }
        c->keepAlive = request.keepAlive();
        handle(socket, *c, request);
    }
}

void OverlayServer::onBytesWritten(QTcpSocket *socket)
{
    const std::shared_ptr<Connection> c = m_connections.value(socket);
    if (c && c->file)
        continueFile(socket, *c);
}

void OverlayServer::handle(QTcpSocket *socket, Connection &c, const HttpRequest &request)
{
    if (request.method != "GET" && request.method != "HEAD" && request.method != "POST") {
        c.keepAlive = false;
        respond(socket, c, request, 405, "text/plain; charset=utf-8", "", kNoStore);
        return;
    }
    const QString target = QString::fromUtf8(QByteArray::fromPercentEncoding(request.target));
    if (!target.startsWith(QLatin1String("/o/"))) {
        // Nicht die Adresse einer Browserquelle (zum Beispiel /favicon.ico): leer, ohne Hinweis.
        if (target == QLatin1String("/"))
            respond(socket, c, request, 200, "text/html; charset=utf-8", kEmptyPage, kNoStore);
        else
            respond(socket, c, request, 404, "text/plain; charset=utf-8", "", kNoStore);
        return;
    }
    // /o/<adresse>/<token>/<rest>
    const QStringList parts = target.mid(3).split(QLatin1Char('/'));
    const QString address = parts.value(0);
    const QString token = parts.value(1);
    const QString rest = parts.size() > 2 ? parts.mid(2).join(QLatin1Char('/')) : QString();
    const Overlay *overlay = plausibleAddress(address) ? m_store->overlayByAddress(address) : nullptr;
    // Immer vergleichen, auch ohne Overlay (gleiche Laufzeit), mit gleichbleibender Laufzeit.
    const QString expected = overlay ? m_store->token(overlay->id) : QString(43, QLatin1Char('-'));
    const bool valid = overlay && !expected.isEmpty()
                       && constantTimeEquals(token.toUtf8(), expected.toUtf8());
    if (!valid) {
        handleInvalid(socket, c, request, address, rest);
        return;
    }
    if (parts.size() == 2) {
        // Ohne «/» am Ende: relative Verweise der Seite gingen sonst ins Leere.
        respond(socket, c, request, 302, "", "",
                {{"Location", request.target + "/"}, {"Cache-Control", "no-store"}});
        return;
    }
    handleOverlay(socket, c, request, overlay->id, rest);
}

void OverlayServer::handleInvalid(QTcpSocket *socket, Connection &c, const HttpRequest &request,
                                  const QString &address, const QString &rest)
{
    noteInvalid(socket, address);
    if (rest == QLatin1String("events")) {
        // Die Seite der alten Adresse lädt neu und bekommt die leere Seite.
        socket->write(responseHead(200, "text/event-stream", -1,
                                   {{"Cache-Control", "no-store"}, {"Connection", "close"}}));
        sendGone(socket);
        c.closing = true;
        socket->disconnectFromHost();
        return;
    }
    if (rest.isEmpty())
        respond(socket, c, request, 200, "text/html; charset=utf-8", kEmptyPage, kNoStore);
    else
        respond(socket, c, request, 404, "text/plain; charset=utf-8", "", kNoStore);
}

void OverlayServer::handleOverlay(QTcpSocket *socket, Connection &c, const HttpRequest &request,
                                  const QUuid &overlayId, const QString &rest)
{
    if (request.method == "POST") {
        if (rest != QLatin1String("report")) {
            respond(socket, c, request, 405, "text/plain; charset=utf-8", "", kNoStore);
            return;
        }
        // Meldung der Seite (zum Beispiel Video nicht abspielbar, Teil B 4), begrenzt.
        if (m_reportWindow.elapsed() > 60 * 1000) {
            m_reportWindow.restart();
            m_reportsInWindow = 0;
        }
        if (++m_reportsInWindow <= kMaxReportsPerMinute) {
            QString text = QString::fromUtf8(request.body).left(kMaxReportChars);
            text.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f]")), QStringLiteral(" "));
            const Overlay *o = m_store->overlay(overlayId);
            qWarning("Browserquelle %s meldet: %s", qPrintable(browserSourceLogPath(o->address)),
                     qPrintable(text));
        }
        respond(socket, c, request, 204, "", "", kNoStore);
        return;
    }
    if (rest.isEmpty() || rest == QLatin1String("overlay.js")) {
        const QString name = rest.isEmpty() ? QStringLiteral("overlay.html") : rest;
        sendFile(socket, c, request, m_options.webDir + QLatin1Char('/') + name, {}, false);
        return;
    }
    if (rest == QLatin1String("events")) {
        if (request.method != "GET") {
            respond(socket, c, request, 405, "text/plain; charset=utf-8", "", kNoStore);
            return;
        }
        startEvents(socket, c, overlayId);
        return;
    }
    if (rest.startsWith(QLatin1String("media/"))) {
        // Nur Dateien, die dieses Overlay benutzt (kein Zugriff auf andere Dateien).
        const Overlay *o = m_store->overlay(overlayId);
        bool used = false;
        for (const Source &s : o->sources)
            used = used || s.file == rest || (s.kind == SourceKind::Text && s.text.fontFile == rest);
        if (used && !rest.contains(QLatin1String(".."))) {
            sendFile(socket, c, request, mediaPath(rest), patchesOf(rest), true);
            return;
        }
    }
    respond(socket, c, request, 404, "text/plain; charset=utf-8", "", kNoStore);
}

void OverlayServer::respond(QTcpSocket *socket, Connection &c, const HttpRequest &request, int status,
                            const QByteArray &type, const QByteArray &body,
                            const QList<std::pair<QByteArray, QByteArray>> &extra)
{
    auto headers = extra;
    if (!c.keepAlive)
        headers.append(std::pair<QByteArray, QByteArray>("Connection", "close"));
    socket->write(responseHead(status, type, body.size(), headers));
    if (request.method != "HEAD")
        socket->write(body);
    finishResponse(socket, c);
}

void OverlayServer::finishResponse(QTcpSocket *socket, Connection &c)
{
    if (!c.keepAlive) {
        c.closing = true;
        socket->disconnectFromHost();
        return;
    }
    // Nächste Anfrage im Puffer (Keep-Alive)
    if (!c.buffer.isEmpty())
        QTimer::singleShot(0, this, [this, socket] {
            if (m_connections.contains(socket))
                onReadyRead(socket);
        });
}

void OverlayServer::sendFile(QTcpSocket *socket, Connection &c, const HttpRequest &request,
                             const QString &path, const BytePatches &patches, bool immutable)
{
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        respond(socket, c, request, 404, "text/plain; charset=utf-8", "", kNoStore);
        return;
    }
    const qint64 size = file->size();
    const QByteArray type = contentTypeForSuffix(QFileInfo(path).suffix());
    // Dateien in media/ heissen nach ihrem Inhalt (SHA-256) und ändern sich nie.
    QList<std::pair<QByteArray, QByteArray>> headers = {
        {"Accept-Ranges", "bytes"},
        {"Cache-Control", immutable ? "max-age=31536000, immutable" : "no-cache"}};
    if (!c.keepAlive)
        headers.append(std::pair<QByteArray, QByteArray>("Connection", "close"));
    const std::optional<ByteRange> range = parseRange(request.headers.value("range"), size);
    qint64 first = 0;
    qint64 length = size;
    int status = 200;
    if (range) {
        if (range->first < 0) {
            headers.append(std::pair<QByteArray, QByteArray>("Content-Range", "bytes */" + QByteArray::number(size)));
            respond(socket, c, request, 416, "text/plain; charset=utf-8", "", headers);
            return;
        }
        first = range->first;
        length = range->last - range->first + 1;
        status = 206;
        headers.append({"Content-Range", "bytes " + QByteArray::number(range->first) + '-'
                                             + QByteArray::number(range->last) + '/'
                                             + QByteArray::number(size)});
    }
    socket->write(responseHead(status, type, length, headers));
    if (request.method == "HEAD" || length == 0) {
        finishResponse(socket, c);
        return;
    }
    file->seek(first);
    c.file = std::move(file);
    c.position = first;
    c.remaining = length;
    c.patches = patches;
    continueFile(socket, c);
}

void OverlayServer::continueFile(QTcpSocket *socket, Connection &c)
{
    while (c.file && socket->bytesToWrite() < kLowWater) {
        QByteArray chunk = c.file->read(std::min(kChunkBytes, c.remaining));
        if (chunk.isEmpty()) {
            // Datei kürzer als erwartet: Verbindung beenden, der Browser fragt neu.
            c.file.reset();
            c.keepAlive = false;
            finishResponse(socket, c);
            return;
        }
        applyPatches(&chunk, c.position, c.patches);
        socket->write(chunk);
        c.position += chunk.size();
        c.remaining -= chunk.size();
        if (c.remaining == 0) {
            c.file.reset();
            c.patches.clear();
            finishResponse(socket, c);
            return;
        }
    }
}

void OverlayServer::startEvents(QTcpSocket *socket, Connection &c, const QUuid &overlayId)
{
    const Overlay *o = m_store->overlay(overlayId);
    c.events = true;
    c.overlayId = overlayId;
    c.address = o->address;
    c.token = m_store->token(overlayId);
    socket->write(responseHead(200, "text/event-stream; charset=utf-8", -1, kNoStore));
    // Wiederverbinden nach 1 s (EventSource, «retry»)
    socket->write("retry: 1000\n\n");
    sendOverlayEvent(socket, c);
    qInfo("Browserquelle verbunden: %s von %s", qPrintable(browserSourceLogPath(c.address)),
          qPrintable(c.peer));
    updateStatus();
}

void OverlayServer::sendGone(QTcpSocket *socket)
{
    socket->write("event: gone\ndata: {}\n\n");
}

void OverlayServer::sendOverlayEvent(QTcpSocket *socket, Connection &c)
{
    const QByteArray data = QJsonDocument(pageData(c.overlayId)).toJson(QJsonDocument::Compact);
    if (data == c.lastData)
        return;
    c.lastData = data;
    socket->write("event: overlay\ndata: " + data + "\n\n");
}

void OverlayServer::checkStreams(const QUuid &overlayId)
{
    const Overlay *o = m_store->overlay(overlayId);
    const QString token = o ? m_store->token(overlayId) : QString();
    QList<QTcpSocket *> close;
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
        Connection &c = **it;
        if (!c.events || c.overlayId != overlayId)
            continue;
        if (!o || o->address != c.address || token != c.token) {
            // Alte Adresse: Seite lädt neu und bekommt die leere Seite (Teil A 5).
            qInfo("Browserquelle %s: Adresse gilt nicht mehr, Seite wird leer",
                  qPrintable(browserSourceLogPath(c.address)));
            sendGone(it.key());
            c.events = false;
            c.closing = true;
            close.append(it.key());
            continue;
        }
        sendOverlayEvent(it.key(), c);
    }
    // Erst nach der Schleife: disconnectFromHost kann disconnected() sofort auslösen.
    for (QTcpSocket *socket : std::as_const(close))
        socket->disconnectFromHost();
    updateStatus();
}

void OverlayServer::overlayChanged(const QUuid &overlayId)
{
    checkStreams(overlayId);
}

void OverlayServer::overlayRemoved(const QUuid &overlayId)
{
    checkStreams(overlayId);
}

void OverlayServer::tokenChanged(const QUuid &overlayId)
{
    checkStreams(overlayId);
}

void OverlayServer::allChanged()
{
    QSet<QUuid> ids;
    for (const auto &c : std::as_const(m_connections)) {
        if (c->events)
            ids.insert(c->overlayId);
    }
    for (const QUuid &id : std::as_const(ids))
        checkStreams(id);
}

void OverlayServer::heartbeat()
{
    // Kommentarzeile: hält die Verbindung offen und lässt tote Verbindungen auffallen.
    for (auto it = m_connections.cbegin(); it != m_connections.cend(); ++it) {
        if ((*it)->events)
            it.key()->write(": \n\n");
    }
}

void OverlayServer::noteInvalid(const QTcpSocket *socket, const QString &address)
{
    // Im Protokoll und im Fenster nur die lesbare Adresse, nie der Token (Regel 12).
    const QString shown = plausibleAddress(address) ? address : QStringLiteral("?");
    const QString peer = peerText(socket);
    const QString key = peer + QLatin1Char(' ') + shown;
    auto it = m_invalidLogged.find(key);
    if (it == m_invalidLogged.end() || it->elapsed() > kInvalidLogIntervalMs) {
        qWarning("Browserquelle: unbekannte Adresse %s von %s", qPrintable(browserSourceLogPath(shown)),
                 qPrintable(peer));
        QElapsedTimer t;
        t.start();
        m_invalidLogged.insert(key, t);
    }
    m_lastInvalid.start();
    m_lastInvalidText = QStringLiteral("Eine Browserquelle (%1) benutzt eine ungültige Adresse (%2). In OBS "
                                       "die Adresse aus «Kopieren» im Dock «Steuerung» eintragen.")
                            .arg(peer, browserSourceLogPath(shown));
    updateStatus();
}

QString OverlayServer::mediaPath(const QString &reference) const
{
    return m_store->dataDir() + QLatin1Char('/') + reference;
}

int OverlayServer::orientationOf(const QString &reference)
{
    auto it = m_orientations.constFind(reference);
    if (it != m_orientations.constEnd())
        return *it;
    const QString path = mediaPath(reference);
    int orientation = imageFileOrientation(path);
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool video = contentTypeForSuffix(suffix).startsWith("video/");
    if (video)
        orientation = m_options.videoOrientation ? m_options.videoOrientation(path) : kOrientationNormal;
    m_orientations.insert(reference, orientation);
    return orientation;
}

const BytePatches &OverlayServer::patchesOf(const QString &reference)
{
    auto it = m_patches.find(reference);
    if (it == m_patches.end()) {
        // Ohne Leser für Videos (DSS_MEDIA_SIZE aus) bleibt die Drehung im Video: dann dreht der
        // Browser, wie er will, die Seite dreht nicht (Ausrichtung 1).
        const bool video = contentTypeForSuffix(QFileInfo(reference).suffix()).startsWith("video/");
        it = m_patches.insert(reference, video && !m_options.videoOrientation
                                             ? BytePatches()
                                             : orientationPatches(mediaPath(reference)));
    }
    return *it;
}

QJsonObject OverlayServer::pageData(const QUuid &overlayId)
{
    const Overlay *o = m_store->overlay(overlayId);
    if (!o)
        return {};
    QJsonObject media;
    QJsonObject fonts;
    for (const Source &s : o->sources) {
        if (s.kind == SourceKind::Text) {
            if (fonts.contains(s.text.fontFile))
                continue;
            QFile f(mediaPath(s.text.fontFile));
            FontLineMetrics m;
            if (f.open(QIODevice::ReadOnly) && fontLineMetrics(f.readAll(), &m))
                fonts.insert(s.text.fontFile, QJsonObject{{QStringLiteral("units_per_em"), m.unitsPerEm},
                                                         {QStringLiteral("ascent"), m.ascent},
                                                         {QStringLiteral("descent"), m.descent},
                                                         {QStringLiteral("leading"), m.leading}});
            continue;
        }
        if (!media.contains(s.file))
            media.insert(s.file, QJsonObject{{QStringLiteral("orientation"), orientationOf(s.file)}});
    }
    return {{QStringLiteral("overlay"), overlayToJson(*o)},
            {QStringLiteral("media"), media},
            {QStringLiteral("fonts"), fonts}};
}

} // namespace dss
