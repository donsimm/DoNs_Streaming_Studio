#pragma once

// Testserver für die Anbindung an Twitch (P13, Plan 4 «Testserver statt Twitch CLI»): ein kleiner
// HTTP-Teil auf QTcpServer für /oauth2/* und /helix/* auf 127.0.0.1, ohne Netz, ohne Konto, ohne
// TLS. Vom Test gesteuert, Muster wie tests/obstestserver.h. Der WebSocket-Teil für EventSub kommt mit
// Commit 6, die schreibenden Aufrufe mit P14.
//
// Verhalten nach der Doku von Twitch (Quellen in src/twitch/twitchauth.h und helixclient.h):
// - POST /oauth2/device: device_code, user_code, verification_uri, expires_in, interval
// - POST /oauth2/token: grant_type device_code (400 «authorization_pending», 400 «invalid device
//   code») und refresh_token (Refresh-Token einmal verwendbar, sonst 400 «Invalid refresh token»).
//   client_secret: Ist eines gesetzt, muss ein mitgeschicktes stimmen; ohne wird angenommen (ob Twitch
//   es beim Gerätecode verlangt, ist nicht belegt, Schritt 0).
// - GET /oauth2/validate mit «Authorization: OAuth …»: client_id, login, scopes, user_id, expires_in
// - POST /oauth2/revoke: client_id, token
// - /helix/*: Client-Id und «Authorization: Bearer …», sonst 401; GET /helix/users, GET
//   /helix/streams, POST /helix/eventsub/subscriptions; 429 mit Ratelimit-Reset auf Wunsch.
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
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

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

    explicit TwitchTestServer(QObject *parent = nullptr)
        : QObject(parent)
    {
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

    void onData(QTcpSocket *socket)
    {
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
            subscription.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
            subscription.insert(QStringLiteral("status"), QStringLiteral("enabled"));
            subscription.insert(QStringLiteral("cost"), 0);
            reply(socket, 202, {{QStringLiteral("data"), QJsonArray{subscription}},
                                {QStringLiteral("total"), 1},
                                {QStringLiteral("total_cost"), 0},
                                {QStringLiteral("max_total_cost"), 10000}});
        } else {
            error(socket, 404, QStringLiteral("not found"));
        }
    }

    QTcpServer *m_server = nullptr;
    QList<QPointer<QTcpSocket>> m_clients;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    quint16 m_port = 0;
};

} // namespace testhelp
