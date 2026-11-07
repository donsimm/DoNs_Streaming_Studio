#pragma once

// Testserver für die Anbindung an OBS (P10c): spricht das Protokoll von obs-websocket 5.x nach
// docs/generated/protocol.md auf 127.0.0.1, ohne OBS. Felder der Elemente von «scenes» und «inputs»
// wie Obs_ArrayHelper.cpp im Repo von obs-websocket. Kein Test berührt das echte OBS.
//
// TcpGate: TCP-Weiterleitung, die alles zurückhalten kann. So stirbt eine Verbindung, ohne dass
// sich die Gegenstelle abmeldet (QWebSocketServer beantwortet Ping sonst immer).

#include "obsprotocol.h"

#include <QCryptographicHash>
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
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

namespace testhelp {

struct ObsTestInput
{
    QString uuid;
    QString name;
    QString kind;
    QJsonObject settings;
};

struct ObsTestScene
{
    QString uuid;
    QString name;
};

struct ObsTestRequest
{
    QString type;
    QJsonObject data;
};

class ObsTestServer : public QObject
{
public:
    explicit ObsTestServer(QObject *parent = nullptr)
        : QObject(parent)
    {
        scenes = {{QStringLiteral("scene-live"), QStringLiteral("Live")},
                  {QStringLiteral("scene-pause"), QStringLiteral("Pause")}};
        current = scenes.first();
    }
    ~ObsTestServer() override { stop(); }

    // Port 0: frei wählen; danach bleibt der Port auch nach stop() und start().
    bool start(quint16 port = 0)
    {
        stop();
        m_server = new QWebSocketServer(QStringLiteral("obs-test"), QWebSocketServer::NonSecureMode, this);
        m_server->setSupportedSubprotocols({QString::fromLatin1(dss::obs::kSubprotocol)});
        connect(m_server, &QWebSocketServer::newConnection, this, &ObsTestServer::onNewConnection);
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
        const QList<QPointer<QWebSocket>> clients = m_clients;
        m_clients.clear();
        m_identified.clear();
        for (const QPointer<QWebSocket> &c : clients) {
            if (c) {
                c->abort();
                c->deleteLater();
            }
        }
    }

    // Trennt alle Verbindungen mit einem Code, wie OBS es tut (4011 usw.).
    void closeClients(int code)
    {
        const QList<QPointer<QWebSocket>> clients = m_clients;
        for (const QPointer<QWebSocket> &c : clients) {
            if (c)
                c->close(QWebSocketProtocol::CloseCode(code));
        }
    }

    quint16 port() const { return m_port; }
    int connectionCount() const { return m_connectionCount; }
    int identifiedCount() const { return int(m_identified.size()); }

    int requestCount(const QString &type) const
    {
        int count = 0;
        for (const ObsTestRequest &r : log)
            count += r.type == type;
        return count;
    }
    QList<ObsTestRequest> requestsOf(const QString &type) const
    {
        QList<ObsTestRequest> result;
        for (const ObsTestRequest &r : log) {
            if (r.type == type)
                result.append(r);
        }
        return result;
    }

    void sendEvent(const QString &type, const QJsonObject &data = {})
    {
        const QString frame = dss::obs::encodeFrame(
            dss::obs::OpCode::Event,
            QJsonObject{{QStringLiteral("eventType"), type}, {QStringLiteral("eventIntent"), 1},
                        {QStringLiteral("eventData"), data}});
        for (QWebSocket *c : std::as_const(m_identified))
            c->sendTextMessage(frame);
    }

    void switchScene(const ObsTestScene &scene)
    {
        current = scene;
        sendEvent(QStringLiteral("CurrentProgramSceneChanged"),
                  {{QStringLiteral("sceneName"), scene.name}, {QStringLiteral("sceneUuid"), scene.uuid}});
    }

    QString addInput(const QString &name, const QJsonObject &settings)
    {
        const QString uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        inputs.append({uuid, name, QString::fromLatin1(dss::obs::kBrowserKind), settings});
        return uuid;
    }

    ObsTestInput *input(const QString &uuid)
    {
        for (ObsTestInput &i : inputs) {
            if (i.uuid == uuid)
                return &i;
        }
        return nullptr;
    }

    void removeInputWithEvent(const QString &uuid)
    {
        ObsTestInput *i = input(uuid);
        if (!i)
            return;
        const QJsonObject data{{QStringLiteral("inputName"), i->name}, {QStringLiteral("inputUuid"), uuid}};
        inputs.removeIf([&uuid](const ObsTestInput &x) { return x.uuid == uuid; });
        sendEvent(QStringLiteral("InputRemoved"), data);
    }

    // Verhalten (öffentlich, von den Tests gesetzt)
    QString password;                  // leer: keine Anmeldung verlangt
    QList<ObsTestScene> scenes;
    ObsTestScene current;
    QList<ObsTestInput> inputs;
    QHash<QString, QStringList> sceneItems; // Szene → Quellen
    QList<ObsTestRequest> log;
    QSet<QString> silentRequests;      // keine Antwort
    QSet<QString> closingRequests;     // Verbindung bricht nach dem Ausführen ab, ohne Antwort
    int busyAnswers = 0;               // die nächsten Anfragen mit 207 beantworten
    QString busyRequest;               // nur diese Anfrage; leer = alle
    bool collectionChanging = false;
    int requestsDuringCollectionChange = 0;

private:
    void send(QWebSocket *socket, dss::obs::OpCode op, const QJsonObject &data)
    {
        socket->sendTextMessage(dss::obs::encodeFrame(op, data));
    }

    void onNewConnection()
    {
        while (QWebSocket *socket = m_server->nextPendingConnection()) {
            ++m_connectionCount;
            m_clients.append(socket);
            connect(socket, &QWebSocket::textMessageReceived, this,
                    [this, socket](const QString &text) { onText(socket, text); });
            connect(socket, &QWebSocket::disconnected, this, [this, socket] {
                m_identified.remove(socket);
                m_clients.removeAll(QPointer<QWebSocket>(socket));
                socket->deleteLater();
            });
            m_salt = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_challenge = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QJsonObject hello{{QStringLiteral("obsStudioVersion"), QStringLiteral("33.0.1")},
                              {QStringLiteral("obsWebSocketVersion"), QStringLiteral("5.7.6")},
                              {QStringLiteral("rpcVersion"), 1}};
            if (!password.isEmpty())
                hello.insert(QStringLiteral("authentication"),
                             QJsonObject{{QStringLiteral("challenge"), m_challenge},
                                         {QStringLiteral("salt"), m_salt}});
            send(socket, dss::obs::OpCode::Hello, hello);
        }
    }

    QString expectedAuthentication() const
    {
        const auto hash = [](const QByteArray &text) {
            return QCryptographicHash::hash(text, QCryptographicHash::Sha256).toBase64();
        };
        return QString::fromLatin1(hash(hash((password + m_salt).toUtf8()) + m_challenge.toUtf8()));
    }

    void onText(QWebSocket *socket, const QString &text)
    {
        dss::obs::OpCode op = dss::obs::OpCode::Hello;
        QJsonObject data;
        QString error;
        if (!dss::obs::parseFrame(text, &op, &data, &error)) {
            socket->close(QWebSocketProtocol::CloseCode(4002));
            return;
        }
        if (op == dss::obs::OpCode::Identify) {
            if (!password.isEmpty()
                && data.value(QStringLiteral("authentication")).toString() != expectedAuthentication()) {
                socket->close(QWebSocketProtocol::CloseCode(dss::obs::kCloseAuthenticationFailed));
                return;
            }
            lastEventSubscriptions = data.value(QStringLiteral("eventSubscriptions")).toInt();
            m_identified.insert(socket);
            send(socket, dss::obs::OpCode::Identified, {{QStringLiteral("negotiatedRpcVersion"), 1}});
        } else if (op == dss::obs::OpCode::Request) {
            onRequest(socket, data);
        }
    }

    void answer(QWebSocket *socket, const QJsonObject &request, int code, const QJsonObject &response = {},
                const QString &comment = {})
    {
        QJsonObject status{{QStringLiteral("result"), code == dss::obs::kStatusSuccess},
                           {QStringLiteral("code"), code}};
        if (!comment.isEmpty())
            status.insert(QStringLiteral("comment"), comment);
        QJsonObject data{{QStringLiteral("requestType"), request.value(QStringLiteral("requestType"))},
                         {QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
                         {QStringLiteral("requestStatus"), status}};
        if (!response.isEmpty())
            data.insert(QStringLiteral("responseData"), response);
        send(socket, dss::obs::OpCode::RequestResponse, data);
    }

    void onRequest(QWebSocket *socket, const QJsonObject &request)
    {
        const QString type = request.value(QStringLiteral("requestType")).toString();
        const QJsonObject fields = request.value(QStringLiteral("requestData")).toObject();
        log.append({type, fields});
        if (collectionChanging)
            ++requestsDuringCollectionChange;
        if (silentRequests.contains(type))
            return;
        if (busyAnswers > 0 && (busyRequest.isEmpty() || busyRequest == type)) {
            --busyAnswers;
            answer(socket, request, dss::obs::kStatusNotReady);
            return;
        }
        int code = dss::obs::kStatusSuccess;
        QJsonObject response;
        execute(type, fields, &code, &response);
        if (closingRequests.contains(type)) {
            socket->abort();
            return;
        }
        answer(socket, request, code, response);
    }

    void execute(const QString &type, const QJsonObject &fields, int *code, QJsonObject *response)
    {
        const QString inputUuid = fields.value(QStringLiteral("inputUuid")).toString();
        const QString sceneUuid = fields.value(QStringLiteral("sceneUuid")).toString();
        const auto sceneExists = [this, &sceneUuid] {
            return std::any_of(scenes.cbegin(), scenes.cend(),
                               [&sceneUuid](const ObsTestScene &s) { return s.uuid == sceneUuid; });
        };
        if (type == QLatin1String("GetVersion")) {
            *response = {{QStringLiteral("obsVersion"), QStringLiteral("33.0.1")},
                         {QStringLiteral("obsWebSocketVersion"), QStringLiteral("5.7.6")},
                         {QStringLiteral("rpcVersion"), 1}};
        } else if (type == QLatin1String("GetSceneList")) {
            QJsonArray list;
            for (qsizetype i = 0; i < scenes.size(); ++i)
                list.append(QJsonObject{{QStringLiteral("sceneIndex"), int(scenes.size() - i - 1)},
                                        {QStringLiteral("sceneName"), scenes.at(i).name},
                                        {QStringLiteral("sceneUuid"), scenes.at(i).uuid}});
            *response = {{QStringLiteral("currentProgramSceneName"), current.name},
                         {QStringLiteral("currentProgramSceneUuid"), current.uuid},
                         {QStringLiteral("scenes"), list}};
        } else if (type == QLatin1String("GetInputList")) {
            const QString kind = fields.value(QStringLiteral("inputKind")).toString();
            QJsonArray list;
            for (const ObsTestInput &i : std::as_const(inputs)) {
                if (kind.isEmpty() || kind == i.kind)
                    list.append(QJsonObject{{QStringLiteral("inputName"), i.name},
                                            {QStringLiteral("inputUuid"), i.uuid},
                                            {QStringLiteral("inputKind"), i.kind}});
            }
            *response = {{QStringLiteral("inputs"), list}};
        } else if (type == QLatin1String("GetInputSettings")) {
            ObsTestInput *i = input(inputUuid);
            if (!i) {
                *code = dss::obs::kStatusResourceNotFound;
                return;
            }
            *response = {{QStringLiteral("inputSettings"), i->settings}, {QStringLiteral("inputKind"), i->kind}};
        } else if (type == QLatin1String("SetInputSettings")) {
            ObsTestInput *i = input(inputUuid);
            if (!i) {
                *code = dss::obs::kStatusResourceNotFound;
                return;
            }
            const QJsonObject changes = fields.value(QStringLiteral("inputSettings")).toObject();
            for (auto it = changes.begin(); it != changes.end(); ++it)
                i->settings.insert(it.key(), it.value());
        } else if (type == QLatin1String("CreateInput")) {
            const QString name = fields.value(QStringLiteral("inputName")).toString();
            if (std::any_of(inputs.cbegin(), inputs.cend(),
                            [&name](const ObsTestInput &i) { return i.name == name; })) {
                *code = dss::obs::kStatusResourceAlreadyExists;
                return;
            }
            if (!sceneExists()) {
                *code = dss::obs::kStatusResourceNotFound;
                return;
            }
            const QString uuid = addInput(name, fields.value(QStringLiteral("inputSettings")).toObject());
            inputs.last().kind = fields.value(QStringLiteral("inputKind")).toString();
            sceneItems[sceneUuid].append(uuid);
            *response = {{QStringLiteral("inputUuid"), uuid}, {QStringLiteral("sceneItemId"), 1}};
        } else if (type == QLatin1String("CreateSceneItem")) {
            const QString sourceUuid = fields.value(QStringLiteral("sourceUuid")).toString();
            if (!sceneExists() || !input(sourceUuid)) {
                *code = dss::obs::kStatusResourceNotFound;
                return;
            }
            sceneItems[sceneUuid].append(sourceUuid);
            *response = {{QStringLiteral("sceneItemId"), 2}};
        } else {
            *code = 204; // UnknownRequestType
        }
    }

public:
    int lastEventSubscriptions = -1;

private:
    QWebSocketServer *m_server = nullptr;
    QList<QPointer<QWebSocket>> m_clients;
    QSet<QWebSocket *> m_identified;
    quint16 m_port = 0;
    int m_connectionCount = 0;
    QString m_salt;
    QString m_challenge;
};

// Eine Verbindung wird weitergeleitet, bis hold() alles zurückhält.
class TcpGate : public QObject
{
public:
    explicit TcpGate(quint16 targetPort, QObject *parent = nullptr)
        : QObject(parent)
        , m_targetPort(targetPort)
    {
        connect(&m_server, &QTcpServer::newConnection, this, &TcpGate::onNewConnection);
        m_server.listen(QHostAddress::LocalHost);
    }

    quint16 port() const { return m_server.serverPort(); }
    void hold(bool holding) { m_holding = holding; }

private:
    void onNewConnection()
    {
        while (QTcpSocket *client = m_server.nextPendingConnection()) {
            auto *target = new QTcpSocket(client);
            target->connectToHost(QHostAddress::LocalHost, m_targetPort);
            connect(client, &QTcpSocket::readyRead, this, [this, client, target] {
                if (!m_holding)
                    target->write(client->readAll());
            });
            connect(target, &QTcpSocket::readyRead, this, [this, client, target] {
                if (!m_holding)
                    client->write(target->readAll());
            });
            connect(client, &QTcpSocket::disconnected, client, &QObject::deleteLater);
            connect(target, &QTcpSocket::disconnected, client, &QTcpSocket::disconnectFromHost);
        }
    }

    QTcpServer m_server;
    quint16 m_targetPort;
    bool m_holding = false;
};

} // namespace testhelp
