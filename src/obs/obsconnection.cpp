#include "obsconnection.h"

#include <QNetworkProxy>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketHandshakeOptions>

namespace dss::obs {

ObsConnection::ObsConnection(PasswordProvider passwordProvider, QObject *parent)
    : ObsConnection(std::move(passwordProvider), Timing(), parent)
{
}

ObsConnection::ObsConnection(PasswordProvider passwordProvider, const Timing &timing, QObject *parent)
    : QObject(parent)
    , m_passwordProvider(std::move(passwordProvider))
    , m_timing(timing)
    , m_reconnectTimer(new QTimer(this))
    , m_attemptTimer(new QTimer(this))
    , m_pingTimer(new QTimer(this))
    , m_pongTimer(new QTimer(this))
    , m_message(QStringLiteral("Nicht eingerichtet oder abgeschaltet"))
{
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(m_timing.reconnect);
    connect(m_reconnectTimer, &QTimer::timeout, this, &ObsConnection::startAttempt);
    m_attemptTimer->setSingleShot(true);
    m_attemptTimer->setInterval(m_timing.connectTimeout);
    connect(m_attemptTimer, &QTimer::timeout, this, [this] {
        qWarning("OBS: keine Anmeldung innerhalb von %d ms unter %s", m_timing.connectTimeout,
                 qPrintable(endpoint()));
        // Über onSocketClosed wie jeder Abbruch; abort() meldet nicht in jedem Zustand einen Wechsel.
        QWebSocket *socket = m_socket;
        onSocketClosed();
        socket->abort();
    });
    m_pingTimer->setInterval(m_timing.ping);
    connect(m_pingTimer, &QTimer::timeout, this, [this] {
        if (m_socket)
            m_socket->ping();
    });
    m_pongTimer->setSingleShot(true);
    m_pongTimer->setInterval(m_timing.pongTimeout);
    connect(m_pongTimer, &QTimer::timeout, this, [this] {
        qWarning("OBS: kein Pong innerhalb von %d ms, Verbindung gilt als tot", m_timing.pongTimeout);
        QWebSocket *socket = m_socket;
        onSocketClosed();
        socket->abort();
    });
}

ObsConnection::~ObsConnection()
{
    m_phase = Phase::Idle;
    m_listener = nullptr;
    failAllRequests(RequestFailure::Disconnected);
    dropSocket();
}

QString ObsConnection::endpoint() const
{
    return QStringLiteral("%1:%2").arg(m_config.host).arg(m_config.port);
}

void ObsConnection::setStatus(State state, const QString &message)
{
    if (state == m_state && message == m_message)
        return;
    m_state = state;
    m_message = message;
    // Protokoll nur beim Wechsel, ohne Passwort (Regel 12).
    qInfo("OBS: %s", message.isEmpty() ? "verbunden" : qPrintable(message));
    emit stateChanged(state);
}

void ObsConnection::configure(const Config &config, bool passwordChanged)
{
    if (config == m_config && !passwordChanged && !m_waitsForUser)
        return;
    if (!(config == m_config))
        m_currentScene = {}; // Andere Gegenstelle: Der nächste Stand ist ein Wechsel.
    m_config = config;
    m_waitsForUser = false;
    m_password.clear();
    ++m_passwordGeneration;
    m_reconnectTimer->stop();
    m_attemptTimer->stop();
    m_pingTimer->stop();
    m_pongTimer->stop();
    const bool wasReady = m_phase == Phase::Ready;
    m_phase = Phase::Idle;
    m_collectionChanging = false;
    m_exiting = false;
    failAllRequests(RequestFailure::Disconnected);
    if (wasReady && m_listener)
        m_listener->onObsLost();
    dropSocket();
    if (!m_config.enabled) {
        m_obsVersion.clear();
        m_websocketVersion.clear();
        setStatus(State::Off, QStringLiteral("Nicht eingerichtet oder abgeschaltet"));
        return;
    }
    startConnecting();
}

void ObsConnection::startConnecting()
{
    setStatus(State::Connecting, QStringLiteral("Verbindung zu OBS unter %1 wird aufgebaut.").arg(endpoint()));
    // Das Passwort bleibt danach nur im Speicher (Plan P10c, Abschnitt 1.4).
    const quint64 generation = m_passwordGeneration;
    QPointer<ObsConnection> self(this);
    m_passwordProvider([self, generation](const std::optional<QString> &password) {
        if (!self || generation != self->m_passwordGeneration || !self->m_config.enabled)
            return;
        if (!password) {
            self->m_waitsForUser = true;
            self->setStatus(State::Error, QStringLiteral("Gespeichertes Passwort nicht lesbar. Neu eingeben."));
            return;
        }
        self->m_password = *password;
        self->startAttempt();
    });
}

void ObsConnection::dropSocket()
{
    if (!m_socket)
        return;
    m_socket->disconnect(this);
    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
}

void ObsConnection::startAttempt()
{
    // Für jeden Versuch ein neuer QWebSocket: closeCode() bleibt sonst vom letzten Versuch stehen.
    dropSocket();
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_socket->setProxy(QNetworkProxy::NoProxy);
    connect(m_socket, &QWebSocket::connected, this, [this] {
        if (m_phase == Phase::Opening)
            m_phase = Phase::WaitHello;
    });
    // Auch ein gescheiterter Aufbau endet in Unconnected; errorOccurred kommt davor und zählt nicht.
    connect(m_socket, &QWebSocket::stateChanged, this, [this](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::UnconnectedState)
            onSocketClosed();
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, &ObsConnection::onTextMessage);
    connect(m_socket, &QWebSocket::pong, this, [this] { m_pongTimer->start(); });
    QUrl url;
    url.setScheme(QStringLiteral("ws"));
    url.setHost(m_config.host);
    url.setPort(m_config.port);
    QWebSocketHandshakeOptions options;
    options.setSubprotocols({QString::fromLatin1(kSubprotocol)});
    m_phase = Phase::Opening;
    m_exiting = false;
    m_collectionChanging = false;
    m_attemptTimer->start();
    m_socket->open(url, options);
}

void ObsConnection::onSocketClosed()
{
    if (m_phase == Phase::Idle)
        return;
    const bool wasReady = m_phase == Phase::Ready;
    const int closeCode = m_socket ? int(m_socket->closeCode()) : 0;
    m_phase = Phase::Idle;
    m_attemptTimer->stop();
    m_pingTimer->stop();
    m_pongTimer->stop();
    m_collectionChanging = false;
    m_exiting = false;
    failAllRequests(RequestFailure::Disconnected);
    if (wasReady && m_listener)
        m_listener->onObsLost();
    if (!closeCodeAllowsReconnect(closeCode)) {
        m_waitsForUser = true;
        m_password.clear();
        if (closeCode == kCloseAuthenticationFailed)
            setStatus(State::Error, QStringLiteral("OBS hat das Passwort abgelehnt. Im Dialog «Einstellungen» neu eingeben."));
        else if (closeCode == kCloseUnsupportedRpcVersion)
            setStatus(State::Error, QStringLiteral("OBS nutzt eine andere Version von obs-websocket."));
        else
            setStatus(State::Error, QStringLiteral("OBS hat die Verbindung beendet."));
        return;
    }
    setStatus(State::Error, QStringLiteral("OBS unter %1 nicht erreichbar. Neuer Versuch läuft.").arg(endpoint()));
    m_reconnectTimer->start();
}

void ObsConnection::onTextMessage(const QString &text)
{
    OpCode op = OpCode::Event;
    QJsonObject data;
    QString error;
    // Nie den Text selbst ins Protokoll: Er kann Adressen mit Token enthalten (Regel 12).
    if (!parseFrame(text, &op, &data, &error)) {
        qWarning("OBS: ungültige Nachricht: %s", qPrintable(error));
        return;
    }
    switch (op) {
    case OpCode::Hello:
        onHello(data);
        break;
    case OpCode::Identified:
        onIdentified();
        break;
    case OpCode::Event:
        onEvent(data);
        break;
    case OpCode::RequestResponse:
        onResponse(data);
        break;
    default:
        break;
    }
}

void ObsConnection::onHello(const QJsonObject &data)
{
    if (m_phase != Phase::WaitHello && m_phase != Phase::Opening)
        return;
    QString error;
    if (!parseHello(data, &m_hello, &error)) {
        qWarning("OBS: %s", qPrintable(error));
        return; // Die Zeitgrenze des Versuchs beendet ihn.
    }
    m_obsVersion = m_hello.obsVersion;
    m_websocketVersion = m_hello.websocketVersion;
    m_phase = Phase::WaitIdentified;
    m_socket->sendTextMessage(encodeFrame(OpCode::Identify, identifyData(m_hello, m_password)));
}

void ObsConnection::onIdentified()
{
    if (m_phase != Phase::WaitIdentified)
        return;
    m_phase = Phase::Syncing;
    m_attemptTimer->stop();
    m_pingTimer->start();
    m_pongTimer->start();
    // Bleibt eine dieser Antworten aus, wird der Versuch beendet und wiederholt.
    const auto giveUp = [this](RequestFailure failure) {
        if (failure == RequestFailure::Timeout && m_phase == Phase::Syncing && m_socket) {
            QWebSocket *socket = m_socket;
            onSocketClosed();
            socket->abort();
        }
    };
    request(QStringLiteral("GetVersion"), {}, [this, giveUp](const Response &r, RequestFailure failure) {
        if (failure != RequestFailure::None) {
            giveUp(failure);
            return;
        }
        if (r.ok) {
            m_obsVersion = r.data.value(QStringLiteral("obsVersion")).toString(m_obsVersion);
            m_websocketVersion = r.data.value(QStringLiteral("obsWebSocketVersion")).toString(m_websocketVersion);
        }
    });
    request(QStringLiteral("GetSceneList"), {}, [this, giveUp](const Response &r, RequestFailure failure) {
        if (failure != RequestFailure::None) {
            giveUp(failure);
            return;
        }
        if (m_phase != Phase::Syncing)
            return;
        if (r.ok)
            updateCurrentScene(parseCurrentScene(r.data));
        becomeReady();
    });
}

void ObsConnection::becomeReady()
{
    m_phase = Phase::Ready;
    setStatus(State::Connected, QString());
    if (m_listener)
        m_listener->onObsReady();
}

void ObsConnection::updateCurrentScene(const Scene &scene)
{
    if (scene == m_currentScene)
        return;
    const bool same = !scene.uuid.isEmpty() && scene.uuid == m_currentScene.uuid;
    m_currentScene = scene;
    if (!same)
        emit currentSceneChanged(scene);
}

void ObsConnection::onEvent(const QJsonObject &data)
{
    Event event;
    QString error;
    if (!parseEvent(data, &event, &error)) {
        qWarning("OBS: %s", qPrintable(error));
        return;
    }
    // OBS entfernt beim Beenden alle Quellen; das heisst nicht, dass der Nutzer sie gelöscht hat.
    if (m_exiting || m_phase != Phase::Ready)
        return;
    const QString &type = event.type;
    if (type == QLatin1String("ExitStarted")) {
        m_exiting = true;
        return;
    }
    if (type == QLatin1String("CurrentProgramSceneChanged")) {
        updateCurrentScene(parseChangedScene(event.data));
    } else if (type == QLatin1String("SceneNameChanged")) {
        // Umbenennen ist kein Wechsel: neuer Name ohne Signal.
        if (event.data.value(QStringLiteral("sceneUuid")).toString() == m_currentScene.uuid)
            m_currentScene.name = event.data.value(QStringLiteral("sceneName")).toString();
    } else if (type == QLatin1String("CurrentSceneCollectionChanging")) {
        m_collectionChanging = true;
    } else if (type == QLatin1String("CurrentSceneCollectionChanged")) {
        m_collectionChanging = false;
        const QList<QString> held = m_held;
        m_held.clear();
        for (const QString &id : held)
            sendRequest(id);
        // Andere Sammlung: andere aktuelle Szene.
        request(QStringLiteral("GetSceneList"), {}, [this](const Response &r, RequestFailure failure) {
            if (failure == RequestFailure::None && r.ok)
                updateCurrentScene(parseCurrentScene(r.data));
        });
    }
    if (m_listener)
        m_listener->onObsEvent(event);
}

void ObsConnection::request(const QString &type, const QJsonObject &fields, ResponseHandler handler)
{
    const bool usable = m_socket && !m_exiting && (m_phase == Phase::Syncing || m_phase == Phase::Ready);
    if (!usable) {
        handler({}, RequestFailure::NotConnected);
        return;
    }
    const QString id = QString::number(m_nextRequest++);
    auto *timeout = new QTimer(this);
    timeout->setSingleShot(true);
    timeout->setInterval(m_timing.requestTimeout);
    connect(timeout, &QTimer::timeout, this, [this, id] { finishRequest(id, {}, RequestFailure::Timeout); });
    timeout->start();
    m_pending.insert(id, Pending{type, fields, std::move(handler), timeout});
    if (m_collectionChanging)
        m_held.append(id);
    else
        sendRequest(id);
}

void ObsConnection::sendRequest(const QString &id)
{
    const auto it = m_pending.constFind(id);
    if (it == m_pending.constEnd() || !m_socket)
        return;
    m_socket->sendTextMessage(encodeFrame(OpCode::Request, requestData(it->type, id, it->fields)));
}

void ObsConnection::onResponse(const QJsonObject &data)
{
    Response response;
    QString error;
    if (!parseResponse(data, &response, &error)) {
        qWarning("OBS: %s", qPrintable(error));
        return;
    }
    const auto it = m_pending.find(response.requestId);
    if (it == m_pending.end())
        return;
    if (response.code == kStatusNotReady) {
        // OBS beschäftigt: dieselbe Anfrage nach busyRetry nochmals, höchstens busyRetries-mal.
        if (it->busyRetries >= m_timing.busyRetries) {
            finishRequest(response.requestId, {}, RequestFailure::Timeout);
            return;
        }
        ++it->busyRetries;
        it->timeout->start(m_timing.busyRetry + m_timing.requestTimeout);
        const QString id = response.requestId;
        QTimer::singleShot(m_timing.busyRetry, this, [this, id] {
            if (m_collectionChanging)
                m_held.append(id);
            else
                sendRequest(id);
        });
        return;
    }
    finishRequest(response.requestId, response, RequestFailure::None);
}

void ObsConnection::finishRequest(const QString &id, const Response &response, RequestFailure failure)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const Pending pending = *it;
    m_pending.erase(it);
    m_held.removeAll(id);
    pending.timeout->stop();
    pending.timeout->deleteLater();
    pending.handler(response, failure);
}

void ObsConnection::failAllRequests(RequestFailure failure)
{
    const QList<QString> ids = m_pending.keys();
    for (const QString &id : ids)
        finishRequest(id, {}, failure);
    m_held.clear();
}

} // namespace dss::obs
