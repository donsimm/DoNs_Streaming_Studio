#pragma once

// Testserver für die Anbindung an Twitch (P13, Plan 4 «Testserver statt Twitch CLI»): ein kleiner
// HTTP-Teil auf QTcpServer für /oauth2/* und /helix/* und EventSub über WebSocket unter /ws, alles
// auf demselben Port von 127.0.0.1, ohne Netz, ohne Konto, ohne TLS. Vom Test gesteuert, Muster wie
// tests/obstestserver.h. Schreibende Aufrufe: P14 c1a POST /helix/chat/messages (c1b folgt).
//
// Verhalten nach der Doku von Twitch (Quellen in src/twitch/twitchauth.h und helixclient.h):
// - POST /oauth2/device: device_code, user_code, verification_uri, expires_in, interval
// - POST /oauth2/token: grant_type device_code (400 «authorization_pending», 400 «invalid device
//   code»), refresh_token (Refresh-Token einmal verwendbar, sonst 400 «Invalid refresh token») und
//   client_credentials (App-Token, nur mit gesetztem und mitgeschicktem Secret).
//   client_secret: Ist eines gesetzt, muss ein mitgeschicktes stimmen; ohne wird angenommen (ob Twitch
//   es beim Gerätecode verlangt, ist nicht belegt, Schritt 0).
// - GET /oauth2/validate mit «Authorization: OAuth …»: client_id, login, scopes, user_id, expires_in
// - POST /oauth2/revoke: client_id, token
// - /helix/*: Client-Id und «Authorization: Bearer …», sonst 401; GET /helix/users, GET
//   /helix/streams, POST /helix/eventsub/subscriptions; 429 mit Ratelimit-Reset auf Wunsch.
// - GET /ws (WebSocket, Übergabe an QWebSocketServer::handleConnection): session_welcome mit
//   keepalive_timeout_seconds aus der Adresse (anders als Twitch auch unter 10, damit Tests kurz
//   bleiben), session_keepalive in diesem Abstand, ohne Abo nach unusedTimeout Close 4003. Ein Abo
//   gilt nur mit der session_id einer offenen Verbindung (sonst 400). session_reconnect nennt
//   /ws?reconnect=<session_id>; die neue Verbindung behält session_id und Abos.
//   https://dev.twitch.tv/docs/eventsub/handling-websocket-events/,
//   https://dev.twitch.tv/docs/eventsub/websocket-reference/
//
// Tokens sind zufällig mit fester Vorsilbe (zugang-, erneuerung-, geraet-, NUTZER), damit Tests sie
// im Protokoll suchen können.

#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

#include <memory>
#include <optional>

namespace testhelp {

struct TwitchHttpRequest
{
    QByteArray method;
    QString path;
    QUrlQuery query;
    QHash<QByteArray, QByteArray> headers; // Namen in Kleinbuchstaben
    QByteArray body;

    QUrlQuery form() const { return QUrlQuery(QString::fromUtf8(body).replace(QLatin1Char('+'), QLatin1Char(' '))); }
    QString formValue(const QString &name) const
    {
        return form().queryItemValue(name, QUrl::FullyDecoded);
    }
};

struct TwitchTestUser
{
    QString id;
    QString login;
};

class TwitchTestServer : public QObject
{
public:
    struct Device
    {
        QString deviceCode;
        QString userCode;
        QStringList scopes;
        QDateTime expiresAt;
        std::optional<TwitchTestUser> approvedBy;
        QStringList grantedScopes; // leer: die verlangten
        bool used = false;
    };
    struct Token
    {
        QString access;
        QString refresh;
        TwitchTestUser user;
        QStringList scopes;
        QDateTime expiresAt;
        bool accessValid = true;
        bool refreshUsed = false;
        bool revoked = false;
    };
    // Eine Verbindung zu EventSub (WebSocket unter /ws).
    struct WsSession
    {
        QPointer<QWebSocket> socket;
        QString id;           // session_id (nach session_reconnect dieselbe)
        QUrl requestUrl;      // Adresse, mit der der Client verbunden hat
        int keepaliveSeconds = 10;
        bool reconnected = false; // über reconnect_url verbunden
        bool welcomed = false;
        bool subscribed = false;
        bool closed = false;
        QDateTime connectedAt;
        QDateTime welcomedAt;
        QTimer *keepaliveTimer = nullptr;
        QTimer *unusedTimer = nullptr;
    };
    struct SubscriptionRecord
    {
        QString type;
        QString version;
        QString sessionId;
        QJsonObject condition;
        QDateTime at;
    };

    explicit TwitchTestServer(QObject *parent = nullptr)
        : QObject(parent)
        , m_ws(new QWebSocketServer(QStringLiteral("dss-twitch-test"), QWebSocketServer::NonSecureMode, this))
    {
        connect(m_ws, &QWebSocketServer::newConnection, this, &TwitchTestServer::onWsConnection);
    }
    ~TwitchTestServer() override { stop(); }

    // Port 0: frei wählen; danach bleibt der Port auch nach stop() und start().
    bool start(quint16 port = 0)
    {
        stop();
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &TwitchTestServer::onNewConnection);
        if (!m_server->listen(QHostAddress::LocalHost, port ? port : m_port))
            return false;
        m_port = m_server->serverPort();
        return true;
    }
    void stop()
    {
        if (m_server) {
            m_server->close();
            m_server->deleteLater();
            m_server = nullptr;
        }
        const QList<QPointer<QTcpSocket>> clients = m_clients;
        m_clients.clear();
        for (const QPointer<QTcpSocket> &c : clients) {
            if (c)
                c->abort();
        }
        for (const std::unique_ptr<WsSession> &s : sessions) {
            stopTimers(s.get());
            if (s->socket)
                s->socket->abort();
        }
    }

    quint16 port() const { return m_port; }
    QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_port)); }

    // --- Steuerung durch den Test
    QString clientId = QStringLiteral("testclient");
    QString clientSecret;           // leer: öffentliche App
    int deviceExpiresIn = 1800;
    int deviceInterval = 5;
    int accessExpiresIn = 14400;
    bool rejectRefresh = false;
    QHash<QString, int> rateLimited; // Pfad → so viele Anfragen mit 429 beantworten
    QJsonObject streams{{QStringLiteral("data"), QJsonArray()}, {QStringLiteral("pagination"), QJsonObject()}};

    QList<Device> devices;
    QList<Token> tokens;
    QList<TwitchHttpRequest> log;

    // P14 c1a: Nachrichten an /helix/chat/messages
    struct ChatRecord
    {
        QJsonObject body;      // broadcaster_id, sender_id, message, reply_parent_message_id
        bool appToken = false; // mit App-Token gesendet (Abzeichen «Chat Bot»)
        QString messageId;
        bool sent = true;
    };
    QList<ChatRecord> chatMessages;
    QString dropNextChat; // gesetzt: nächste Nachricht mit is_sent false und diesem drop_reason.code

    // Bestätigt den neuesten offenen Gerätecode mit diesem Konto (wie der Nutzer im Browser).
    bool approveLatest(const TwitchTestUser &user, const QStringList &grantedScopes = {})
    {
        for (qsizetype i = devices.size() - 1; i >= 0; --i) {
            if (!devices[i].used && !devices[i].approvedBy) {
                devices[i].approvedBy = user;
                devices[i].grantedScopes = grantedScopes;
                return true;
            }
        }
        return false;
    }
    void expireDevices()
    {
        for (Device &d : devices)
            d.expiresAt = QDateTime::currentDateTimeUtc().addSecs(-1);
    }
    // Access-Token dieses Kontos ungültig machen (wie nach Ablauf): Helix und validate geben 401.
    void invalidateAccess(const QString &userId)
    {
        for (Token &t : tokens) {
            if (t.user.id == userId)
                t.accessValid = false;
        }
    }
    const Token *tokenByAccess(const QString &access) const
    {
        for (const Token &t : tokens) {
            if (t.access == access)
                return &t;
        }
        return nullptr;
    }
    const Token *latestToken(const QString &userId) const
    {
        for (qsizetype i = tokens.size() - 1; i >= 0; --i) {
            if (tokens[i].user.id == userId)
                return &tokens[i];
        }
        return nullptr;
    }
    int count(const QString &path, const QString &grantType = {}) const
    {
        int n = 0;
        for (const TwitchHttpRequest &r : log) {
            if (r.path == path && (grantType.isEmpty() || r.formValue(QStringLiteral("grant_type")) == grantType))
                ++n;
        }
        return n;
    }
    QList<TwitchHttpRequest> requestsTo(const QString &path) const
    {
        QList<TwitchHttpRequest> result;
        for (const TwitchHttpRequest &r : log) {
            if (r.path == path)
                result.append(r);
        }
        return result;
    }
    // Alle Geheimnisse, die der Server ausgegeben hat (Suche im Protokoll).
    QStringList secrets() const
    {
        QStringList result;
        for (const Device &d : devices)
            result << d.deviceCode << d.userCode;
        for (const Token &t : tokens)
            result << t.access << t.refresh;
        if (!clientSecret.isEmpty())
            result << clientSecret;
        return result;
    }
    // Token ohne Anmeldung ausgeben (Tests der Verbindung); gibt das Access-Token zurück.
    QString addToken(const TwitchTestUser &user, const QStringList &scopes)
    {
        return issue(user, scopes).access;
    }

    // --- EventSub (WebSocket), Steuerung durch den Test
    bool autoWelcome = true;    // session_welcome gleich nach dem Verbinden
    bool autoKeepalive = true;  // session_keepalive im Abstand keepalive_timeout_seconds
    int unusedTimeout = 10000;  // ms ohne Abo bis Close 4003 (Twitch: 10 s)
    QHash<QString, int> rejectSubscriptions; // Typ → HTTP-Status beim Anlegen
    std::vector<std::unique_ptr<WsSession>> sessions; // alle Verbindungen, älteste zuerst
    QList<SubscriptionRecord> subscriptions;          // angelegte Abos

    WsSession *latestSession() const { return sessions.empty() ? nullptr : sessions.back().get(); }
    int openSessions() const
    {
        int n = 0;
        for (const std::unique_ptr<WsSession> &s : sessions)
            n += s->closed ? 0 : 1;
        return n;
    }
    QList<SubscriptionRecord> subscriptionsFor(const QString &sessionId) const
    {
        QList<SubscriptionRecord> result;
        for (const SubscriptionRecord &r : subscriptions) {
            if (r.sessionId == sessionId)
                result.append(r);
        }
        return result;
    }

    void sendWelcome(WsSession *s)
    {
        s->welcomed = true;
        s->welcomedAt = QDateTime::currentDateTimeUtc();
        send(s, message(QStringLiteral("session_welcome"),
                        {{QStringLiteral("session"), session(s, QStringLiteral("connected"), s->keepaliveSeconds, QJsonValue::Null)}}));
        if (autoKeepalive) {
            s->keepaliveTimer = new QTimer(this);
            connect(s->keepaliveTimer, &QTimer::timeout, this, [this, s] { sendKeepalive(s); });
            s->keepaliveTimer->start(s->keepaliveSeconds * 1000);
        }
        if (!s->subscribed) {
            s->unusedTimer = new QTimer(this);
            s->unusedTimer->setSingleShot(true);
            connect(s->unusedTimer, &QTimer::timeout, this, [this, s] {
                if (!s->subscribed)
                    closeSession(s, 4003);
            });
            s->unusedTimer->start(unusedTimeout);
        }
    }
    void sendKeepalive(WsSession *s) { send(s, message(QStringLiteral("session_keepalive"), {})); }
    // Benachrichtigung aus einer Datei von tests/daten/twitch/: message_id, Zeit und session_id neu.
    void sendNotification(WsSession *s, QJsonObject file, const QString &messageId)
    {
        QJsonObject metadata = file.value(QStringLiteral("metadata")).toObject();
        metadata.insert(QStringLiteral("message_id"), messageId);
        metadata.insert(QStringLiteral("message_timestamp"), now());
        file.insert(QStringLiteral("metadata"), metadata);
        QJsonObject payload = file.value(QStringLiteral("payload")).toObject();
        QJsonObject subscription = payload.value(QStringLiteral("subscription")).toObject();
        QJsonObject transport = subscription.value(QStringLiteral("transport")).toObject();
        transport.insert(QStringLiteral("session_id"), s->id);
        subscription.insert(QStringLiteral("transport"), transport);
        payload.insert(QStringLiteral("subscription"), subscription);
        file.insert(QStringLiteral("payload"), payload);
        send(s, file);
    }
    QString reconnectUrl(const WsSession *s) const
    {
        return QStringLiteral("ws://127.0.0.1:%1/ws?reconnect=%2").arg(m_port).arg(s->id);
    }
    void sendReconnect(WsSession *s)
    {
        send(s, message(QStringLiteral("session_reconnect"),
                        {{QStringLiteral("session"), session(s, QStringLiteral("reconnecting"), QJsonValue::Null, reconnectUrl(s))}}));
    }
    void sendRevocation(WsSession *s, const QString &type, const QString &version, const QString &status)
    {
        QJsonObject metadata = message(QStringLiteral("revocation"), {}).value(QStringLiteral("metadata")).toObject();
        metadata.insert(QStringLiteral("subscription_type"), type);
        metadata.insert(QStringLiteral("subscription_version"), version);
        const QJsonObject subscription{
            {QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("status"), status},
            {QStringLiteral("type"), type},
            {QStringLiteral("version"), version},
            {QStringLiteral("cost"), 0},
            {QStringLiteral("condition"), QJsonObject()},
            {QStringLiteral("transport"), QJsonObject{{QStringLiteral("method"), QStringLiteral("websocket")},
                                                      {QStringLiteral("session_id"), s->id}}},
            {QStringLiteral("created_at"), now()},
        };
        send(s, {{QStringLiteral("metadata"), metadata},
                 {QStringLiteral("payload"), QJsonObject{{QStringLiteral("subscription"), subscription}}}});
    }
    void closeSession(WsSession *s, int code)
    {
        stopTimers(s);
        if (s->socket)
            s->socket->close(QWebSocketProtocol::CloseCode(code), QStringLiteral("Test"));
    }
    void send(WsSession *s, const QJsonObject &object)
    {
        if (s->socket)
            s->socket->sendTextMessage(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    }

    static constexpr const char *kDeviceGrant = "urn:ietf:params:oauth:grant-type:device_code";

private:
    static QString random(const char *prefix)
    {
        return QLatin1String(prefix) + QUuid::createUuid().toString(QUuid::Id128).left(20);
    }

    void onNewConnection()
    {
        while (QTcpSocket *socket = m_server->nextPendingConnection()) {
            m_clients.append(socket);
            connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    }

    static QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
    static QJsonObject message(const QString &type, const QJsonObject &payload)
    {
        return {{QStringLiteral("metadata"), QJsonObject{{QStringLiteral("message_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                                         {QStringLiteral("message_type"), type},
                                                         {QStringLiteral("message_timestamp"), now()}}},
                {QStringLiteral("payload"), payload}};
    }
    static QJsonObject session(const WsSession *s, const QString &status, const QJsonValue &keepalive,
                               const QJsonValue &reconnectUrl)
    {
        return {{QStringLiteral("id"), s->id},
                {QStringLiteral("status"), status},
                {QStringLiteral("connected_at"), s->connectedAt.toString(Qt::ISODateWithMs)},
                {QStringLiteral("keepalive_timeout_seconds"), keepalive},
                {QStringLiteral("reconnect_url"), reconnectUrl},
                {QStringLiteral("recovery_url"), QJsonValue::Null}};
    }
    void stopTimers(WsSession *s)
    {
        for (QTimer **timer : {&s->keepaliveTimer, &s->unusedTimer}) {
            if (*timer) {
                (*timer)->stop();
                (*timer)->deleteLater();
                *timer = nullptr;
            }
        }
    }

    // Anfrage an /ws: an den WebSocket-Server übergeben, bevor etwas gelesen ist (peek).
    void upgrade(QTcpSocket *socket)
    {
        socket->disconnect(this);
        QObject::disconnect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        m_clients.removeAll(QPointer<QTcpSocket>(socket));
        m_ws->handleConnection(socket);
    }
    void onWsConnection()
    {
        while (QWebSocket *socket = m_ws->nextPendingConnection()) {
            socket->setParent(this);
            auto s = std::make_unique<WsSession>();
            WsSession *raw = s.get();
            s->socket = socket;
            s->requestUrl = socket->requestUrl();
            s->connectedAt = QDateTime::currentDateTimeUtc();
            const QUrlQuery query(s->requestUrl);
            const QString reconnect = query.queryItemValue(QStringLiteral("reconnect"));
            s->reconnected = !reconnect.isEmpty();
            s->id = s->reconnected ? reconnect : QStringLiteral("AgoQ-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
            s->subscribed = s->reconnected; // Abos wandern mit
            const int keepalive = query.queryItemValue(QStringLiteral("keepalive_timeout_seconds")).toInt();
            s->keepaliveSeconds = keepalive > 0 ? keepalive : 10;
            connect(socket, &QWebSocket::disconnected, this, [this, raw, socket] {
                raw->closed = true;
                stopTimers(raw);
                socket->deleteLater();
            });
            sessions.push_back(std::move(s));
            if (autoWelcome)
                sendWelcome(raw);
        }
    }
    WsSession *openSession(const QString &id) const
    {
        for (const std::unique_ptr<WsSession> &s : sessions) {
            if (s->id == id && !s->closed && s->welcomed)
                return s.get();
        }
        return nullptr;
    }

    void onData(QTcpSocket *socket)
    {
        if (!m_buffers.contains(socket)) {
            if (socket->bytesAvailable() < 7)
                return;
            if (socket->peek(7) == "GET /ws") {
                upgrade(socket);
                return;
            }
        }
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QList<QByteArray> lines = buffer.left(headerEnd).split('\n');
        TwitchHttpRequest request;
        const QList<QByteArray> first = lines.first().trimmed().split(' ');
        if (first.size() < 2) {
            socket->abort();
            return;
        }
        request.method = first.at(0);
        const QUrl target(QString::fromUtf8(first.at(1)));
        request.path = target.path();
        request.query = QUrlQuery(target.query());
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const qsizetype colon = line.indexOf(':');
            if (colon > 0)
                request.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
        }
        const qsizetype length = request.headers.value("content-length").toLongLong();
        if (buffer.size() < headerEnd + 4 + length)
            return; // Körper noch nicht ganz da
        request.body = buffer.mid(headerEnd + 4, length);
        m_buffers.remove(socket);
        log.append(request);
        handle(socket, request);
    }

    void reply(QTcpSocket *socket, int status, const QJsonObject &body, const QList<QByteArray> &extraHeaders = {})
    {
        const QByteArray content = body.isEmpty() && status == 204 ? QByteArray() : QJsonDocument(body).toJson(QJsonDocument::Compact);
        QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " Test\r\n"
                              "Content-Type: application/json\r\n"
                              "Content-Length: " + QByteArray::number(content.size()) + "\r\n"
                              "Connection: close\r\n";
        for (const QByteArray &header : extraHeaders)
            response += header + "\r\n";
        response += "\r\n" + content;
        socket->write(response);
        socket->disconnectFromHost();
    }
    void error(QTcpSocket *socket, int status, const QString &message)
    {
        reply(socket, status, {{QStringLiteral("status"), status}, {QStringLiteral("message"), message}});
    }

    void handle(QTcpSocket *socket, const TwitchHttpRequest &request)
    {
        if (request.path.startsWith(QLatin1String("/oauth2/")))
            handleOauth(socket, request);
        else if (request.path.startsWith(QLatin1String("/helix/")))
            handleHelix(socket, request);
        else
            error(socket, 404, QStringLiteral("not found"));
    }

    bool secretOk(const TwitchHttpRequest &request) const
    {
        const QString sent = request.formValue(QStringLiteral("client_secret"));
        return sent.isEmpty() || sent == clientSecret;
    }

    Token issue(const TwitchTestUser &user, const QStringList &scopes)
    {
        Token token{random("zugang-"), random("erneuerung-"), user, scopes,
                    QDateTime::currentDateTimeUtc().addSecs(accessExpiresIn)};
        tokens.append(token);
        return token;
    }
    QJsonObject tokenReply(const Token &token) const
    {
        return {{QStringLiteral("access_token"), token.access},
                {QStringLiteral("refresh_token"), token.refresh},
                {QStringLiteral("expires_in"), accessExpiresIn},
                {QStringLiteral("scope"), QJsonArray::fromStringList(token.scopes)},
                {QStringLiteral("token_type"), QStringLiteral("bearer")}};
    }

    void handleOauth(QTcpSocket *socket, const TwitchHttpRequest &request)
    {
        if (request.path == QLatin1String("/oauth2/validate")) {
            const QByteArray auth = request.headers.value("authorization");
            const Token *token = auth.startsWith("OAuth ") ? tokenByAccess(QString::fromUtf8(auth.mid(6))) : nullptr;
            if (!token || !token->accessValid || token->revoked) {
                error(socket, 401, QStringLiteral("invalid access token"));
                return;
            }
            reply(socket, 200, {{QStringLiteral("client_id"), clientId},
                                {QStringLiteral("login"), token->user.login},
                                {QStringLiteral("scopes"), QJsonArray::fromStringList(token->scopes)},
                                {QStringLiteral("user_id"), token->user.id},
                                {QStringLiteral("expires_in"),
                                 int(QDateTime::currentDateTimeUtc().secsTo(token->expiresAt))}});
            return;
        }
        if (request.formValue(QStringLiteral("client_id")) != clientId) {
            error(socket, 400, QStringLiteral("invalid client"));
            return;
        }
        if (request.path == QLatin1String("/oauth2/device")) {
            Device device;
            device.deviceCode = random("geraet-");
            device.userCode = QStringLiteral("NUTZER") + QString::number(devices.size() + 1);
            device.scopes = request.formValue(QStringLiteral("scopes")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            device.expiresAt = QDateTime::currentDateTimeUtc().addSecs(deviceExpiresIn);
            devices.append(device);
            reply(socket, 200, {{QStringLiteral("device_code"), device.deviceCode},
                                {QStringLiteral("expires_in"), deviceExpiresIn},
                                {QStringLiteral("interval"), deviceInterval},
                                {QStringLiteral("user_code"), device.userCode},
                                {QStringLiteral("verification_uri"),
                                 QStringLiteral("https://www.twitch.tv/activate?public=true&device-code=") + device.userCode}});
            return;
        }
        if (request.path == QLatin1String("/oauth2/revoke")) {
            const QString value = request.formValue(QStringLiteral("token"));
            for (Token &t : tokens) {
                if (t.access == value)
                    t.revoked = true;
            }
            reply(socket, 200, {});
            return;
        }
        if (request.path != QLatin1String("/oauth2/token")) {
            error(socket, 404, QStringLiteral("not found"));
            return;
        }
        if (!secretOk(request)) {
            error(socket, 400, QStringLiteral("invalid client secret"));
            return;
        }
        const QString grant = request.formValue(QStringLiteral("grant_type"));
        if (grant == QLatin1String(kDeviceGrant)) {
            const QString code = request.formValue(QStringLiteral("device_code"));
            for (Device &d : devices) {
                if (d.deviceCode != code)
                    continue;
                if (d.used || d.expiresAt < QDateTime::currentDateTimeUtc())
                    break;
                if (!d.approvedBy) {
                    error(socket, 400, QStringLiteral("authorization_pending"));
                    return;
                }
                d.used = true;
                reply(socket, 200, tokenReply(issue(*d.approvedBy, d.grantedScopes.isEmpty() ? d.scopes : d.grantedScopes)));
                return;
            }
            error(socket, 400, QStringLiteral("invalid device code"));
            return;
        }
        if (grant == QLatin1String("refresh_token")) {
            const QString value = request.formValue(QStringLiteral("refresh_token"));
            for (Token &t : tokens) {
                if (t.refresh != value)
                    continue;
                if (t.refreshUsed || t.revoked || rejectRefresh)
                    break;
                t.refreshUsed = true;
                t.accessValid = false;
                const TwitchTestUser user = t.user;
                const QStringList scopes = t.scopes;
                reply(socket, 200, tokenReply(issue(user, scopes)));
                return;
            }
            error(socket, 400, QStringLiteral("Invalid refresh token"));
            return;
        }
        // Commit 7: App-Token (Client Credentials) nur mit Client-ID und dem gesetzten Secret; ohne
        // Refresh-Token. https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#client-credentials-grant-flow
        if (grant == QLatin1String("client_credentials")) {
            if (clientSecret.isEmpty() || request.formValue(QStringLiteral("client_secret")) != clientSecret
                || request.formValue(QStringLiteral("client_id")) != clientId) {
                error(socket, 400, QStringLiteral("invalid client"));
                return;
            }
            const Token app = issue(TwitchTestUser(), {});
            reply(socket, 200, {{QStringLiteral("access_token"), app.access},
                                {QStringLiteral("expires_in"), accessExpiresIn},
                                {QStringLiteral("token_type"), QStringLiteral("bearer")}});
            return;
        }
        error(socket, 400, QStringLiteral("unsupported grant type"));
    }

    void handleHelix(QTcpSocket *socket, const TwitchHttpRequest &request)
    {
        const QByteArray auth = request.headers.value("authorization");
        const Token *token = auth.startsWith("Bearer ") ? tokenByAccess(QString::fromUtf8(auth.mid(7))) : nullptr;
        if (request.headers.value("client-id") != clientId.toUtf8() || !token || !token->accessValid || token->revoked) {
            reply(socket, 401, {{QStringLiteral("error"), QStringLiteral("Unauthorized")},
                                {QStringLiteral("status"), 401},
                                {QStringLiteral("message"), QStringLiteral("Invalid OAuth token")}});
            return;
        }
        if (rateLimited.value(request.path) > 0) {
            --rateLimited[request.path];
            const qint64 reset = QDateTime::currentSecsSinceEpoch() + 1;
            reply(socket, 429, {{QStringLiteral("error"), QStringLiteral("Too Many Requests")},
                                {QStringLiteral("status"), 429},
                                {QStringLiteral("message"), QStringLiteral("Rate limit exceeded")}},
                  {"Ratelimit-Limit: 800", "Ratelimit-Remaining: 0", "Ratelimit-Reset: " + QByteArray::number(reset)});
            return;
        }
        if (request.path == QLatin1String("/helix/users") && request.method == "GET") {
            reply(socket, 200, {{QStringLiteral("data"),
                                 QJsonArray{QJsonObject{{QStringLiteral("id"), token->user.id},
                                                        {QStringLiteral("login"), token->user.login}}}}});
        } else if (request.path == QLatin1String("/helix/streams") && request.method == "GET") {
            reply(socket, 200, streams);
        } else if (request.path == QLatin1String("/helix/eventsub/subscriptions") && request.method == "POST") {
            QJsonObject subscription = QJsonDocument::fromJson(request.body).object();
            const QString type = subscription.value(QStringLiteral("type")).toString();
            const QString sessionId = subscription.value(QStringLiteral("transport")).toObject()
                                          .value(QStringLiteral("session_id")).toString();
            if (rejectSubscriptions.contains(type)) {
                error(socket, rejectSubscriptions.value(type), QStringLiteral("subscription missing proper authorization"));
                return;
            }
            if (!openSession(sessionId)) {
                error(socket, 400, QStringLiteral("websocket transport session does not exist or has already disconnected"));
                return;
            }
            subscriptions.append({type, subscription.value(QStringLiteral("version")).toString(), sessionId,
                                  subscription.value(QStringLiteral("condition")).toObject(),
                                  QDateTime::currentDateTimeUtc()});
            for (const std::unique_ptr<WsSession> &s : sessions) {
                if (s->id == sessionId) {
                    s->subscribed = true;
                    if (s->unusedTimer)
                        s->unusedTimer->stop();
                }
            }
            subscription.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
            subscription.insert(QStringLiteral("status"), QStringLiteral("enabled"));
            subscription.insert(QStringLiteral("cost"), 0);
            reply(socket, 202, {{QStringLiteral("data"), QJsonArray{subscription}},
                                {QStringLiteral("total"), 1},
                                {QStringLiteral("total_cost"), 0},
                                {QStringLiteral("max_total_cost"), 10000}});
        } else if (request.path == QLatin1String("/helix/chat/messages") && request.method == "POST") {
            handleChatMessage(socket, request, *token);
        } else {
            error(socket, 404, QStringLiteral("not found"));
        }
    }

    // P14 c1a: Send Chat Message (https://dev.twitch.tv/docs/api/reference/#send-chat-message).
    // sender_id muss das Konto des Tokens sein; ein App-Token (ohne Konto) darf für jedes senden.
    void handleChatMessage(QTcpSocket *socket, const TwitchHttpRequest &request, const Token &token)
    {
        const QJsonObject body = QJsonDocument::fromJson(request.body).object();
        const QString sender = body.value(QStringLiteral("sender_id")).toString();
        const QString text = body.value(QStringLiteral("message")).toString();
        if (body.value(QStringLiteral("broadcaster_id")).toString().isEmpty() || sender.isEmpty() || text.isEmpty()) {
            error(socket, 400, QStringLiteral("Missing required parameter"));
            return;
        }
        if (!token.user.id.isEmpty() && token.user.id != sender) {
            error(socket, 401, QStringLiteral("The sender must match the user ID in the access token"));
            return;
        }
        if (text.size() > 500) { // Zählung von Twitch nicht belegt; hier UTF-16-Einheiten (Plan 4.1)
            error(socket, 400, QStringLiteral("message too long"));
            return;
        }
        ChatRecord record{body, token.user.id.isEmpty(), random("nachricht-"), dropNextChat.isEmpty()};
        chatMessages.append(record);
        QJsonObject data{{QStringLiteral("message_id"), record.messageId},
                         {QStringLiteral("is_sent"), record.sent},
                         {QStringLiteral("drop_reason"), QJsonValue::Null}};
        if (!record.sent) {
            data.insert(QStringLiteral("drop_reason"), QJsonObject{{QStringLiteral("code"), dropNextChat},
                                                                    {QStringLiteral("message"), QStringLiteral("Test")}});
            dropNextChat.clear();
        }
        reply(socket, 200, {{QStringLiteral("data"), QJsonArray{data}}});
    }

    QTcpServer *m_server = nullptr;
    QWebSocketServer *m_ws;
    QList<QPointer<QTcpSocket>> m_clients;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    quint16 m_port = 0;
};

} // namespace testhelp
