#include "eventsubconnection.h"

#include "eventbus.h"
#include "eventsubprotocol.h"
#include "helixclient.h"

#include <QJsonArray>
#include <QPointer>
#include <QRandomGenerator>
#include <QSslSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace dss::twitch {

namespace {

constexpr qsizetype kRememberedIds = 500;

// Bedeutung der Close-Codes laut https://dev.twitch.tv/docs/eventsub/handling-websocket-events/
QString closeText(int code)
{
    switch (code) {
    case 4000:
        return QStringLiteral("4000, interner Fehler bei Twitch");
    case 4001:
        return QStringLiteral("4001, Twitch hat eine Nachricht des Studios abgelehnt");
    case 4002:
        return QStringLiteral("4002, Ping nicht beantwortet");
    case 4003:
        return QStringLiteral("4003, keine Abos innerhalb der Frist");
    case 4004:
        return QStringLiteral("4004, Frist für den Wechsel der Verbindung abgelaufen");
    case 4005:
        return QStringLiteral("4005, Zeitüberschreitung im Netz");
    case 4006:
        return QStringLiteral("4006, Netzfehler");
    case 4007:
        return QStringLiteral("4007, ungültige Adresse für den Wechsel");
    default:
        return QString::number(code);
    }
}

} // namespace

int backoffDelay(int attempt, int minMs, int maxMs, double random)
{
    const double base = std::min<double>(maxMs, minMs * std::pow(2.0, std::clamp(attempt, 0, 30)));
    const double jitter = 0.8 + 0.4 * std::clamp(random, 0.0, 1.0);
    return int(std::min<double>(maxMs, base * jitter));
}

EventSubConnection::EventSubConnection(events::EventBus *bus, HelixClient *helix, QObject *parent)
    : EventSubConnection(bus, helix, Timing(), parent)
{
}

EventSubConnection::EventSubConnection(events::EventBus *bus, HelixClient *helix, const Timing &timing, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_helix(helix)
    , m_timing(timing)
    , m_tlsSupported(tlsSupported())
    , m_keepaliveTimer(new QTimer(this))
    , m_welcomeTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
    , m_retryTimer(new QTimer(this))
{
    m_keepaliveTimer->setSingleShot(true);
    m_welcomeTimer->setSingleShot(true);
    m_welcomeTimer->setInterval(m_timing.welcomeTimeout);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(m_timing.reconnectTimeout);
    m_retryTimer->setSingleShot(true);
    connect(m_keepaliveTimer, &QTimer::timeout, this, [this] {
        qWarning("Twitch EventSub: keine Nachricht innerhalb von %d ms (Keepalive)", m_keepaliveTimer->interval());
        lose(QStringLiteral("Keepalive verpasst"));
    });
    connect(m_welcomeTimer, &QTimer::timeout, this, [this] {
        qWarning("Twitch EventSub: keine Begrüssung innerhalb von %d ms", m_timing.welcomeTimeout);
        lose(QStringLiteral("keine Begrüssung von Twitch"));
    });
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        qWarning("Twitch EventSub: neue Verbindung nach session_reconnect ohne Begrüssung innerhalb von %d ms",
                 m_timing.reconnectTimeout);
        lose(QStringLiteral("Wechsel der Verbindung gescheitert"));
    });
    connect(m_retryTimer, &QTimer::timeout, this, &EventSubConnection::openFresh);
}

EventSubConnection::~EventSubConnection()
{
    dropSocket(m_socket);
    dropSocket(m_nextSocket);
}

bool EventSubConnection::tlsSupported()
{
    return QSslSocket::supportsSsl();
}

bool EventSubConnection::allSubscriptionsOk() const
{
    if (m_phase != Phase::Ready || m_subscriptions.isEmpty())
        return false;
    return std::all_of(m_subscriptions.cbegin(), m_subscriptions.cend(), [](const Subscription &s) { return s.ok; });
}

void EventSubConnection::setState(State state, const QString &message)
{
    if (state == m_state && message == m_message)
        return;
    m_state = state;
    m_message = message;
    emit changed();
}

// --- Verbinden -----------------------------------------------------------------------------------

void EventSubConnection::start(TokenSource *token, const QString &broadcasterUserId)
{
    stop();
    m_token = token;
    m_broadcasterUserId = broadcasterUserId;
    m_retryAttempt = 0;
    if (m_endpoints.eventsub.scheme() == QLatin1String("wss") && !m_tlsSupported) {
        qCritical("Twitch EventSub: TLS fehlt (QSslSocket::supportsSsl ist false), keine Verbindung möglich");
        setState(State::Error, QStringLiteral("TLS fehlt: Qt findet OpenSSL nicht. Ohne TLS keine Verbindung zu Twitch."));
        return;
    }
    setState(State::Connecting, QStringLiteral("Verbindung zu Twitch wird aufgebaut."));
    openFresh();
}

void EventSubConnection::stop()
{
    ++m_generation;
    dropSocket(m_socket);
    dropSocket(m_nextSocket);
    m_keepaliveTimer->stop();
    m_welcomeTimer->stop();
    m_reconnectTimer->stop();
    m_retryTimer->stop();
    m_phase = Phase::Idle;
    m_sessionId.clear();
    m_subscriptions.clear();
    m_openSubscriptions = 0;
    m_live = false;
    m_token = nullptr;
    setState(State::Off, QString());
}

QWebSocket *EventSubConnection::createSocket()
{
    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(socket, &QWebSocket::textMessageReceived, this, [this, socket](const QString &text) { onText(socket, text); });
    // Auch ein gescheiterter Aufbau endet in Unconnected (wie ObsConnection).
    connect(socket, &QWebSocket::stateChanged, this, [this, socket](QAbstractSocket::SocketState state) {
        if (state == QAbstractSocket::UnconnectedState)
            onClosed(socket);
    });
    return socket;
}

void EventSubConnection::dropSocket(QWebSocket *&socket)
{
    if (!socket)
        return;
    socket->disconnect(this);
    socket->abort();
    socket->deleteLater();
    socket = nullptr;
}

void EventSubConnection::openFresh()
{
    dropSocket(m_socket);
    dropSocket(m_nextSocket);
    ++m_generation;
    m_phase = Phase::Opening;
    m_sessionId.clear();
    QUrl url = m_endpoints.eventsub;
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("keepalive_timeout_seconds"), QString::number(m_timing.keepaliveSeconds));
    url.setQuery(query);
    m_socket = createSocket();
    m_welcomeTimer->start();
    m_socket->open(url);
}

void EventSubConnection::lose(const QString &reason)
{
    ++m_generation;
    dropSocket(m_socket);
    dropSocket(m_nextSocket);
    m_keepaliveTimer->stop();
    m_welcomeTimer->stop();
    m_reconnectTimer->stop();
    m_phase = Phase::Idle;
    m_sessionId.clear();
    m_openSubscriptions = 0;
    for (Subscription &subscription : m_subscriptions) {
        subscription.ok = false;
        subscription.problem = QStringLiteral("Verbindung unterbrochen");
    }
    const int delay = backoffDelay(m_retryAttempt, m_timing.retryMin, m_timing.retryMax,
                                   QRandomGenerator::global()->generateDouble());
    if (m_retryAttempt < 30)
        ++m_retryAttempt;
    m_retryTimer->start(delay);
    const int seconds = (delay + 999) / 1000;
    qWarning("Twitch EventSub: Verbindung weg (%s), neuer Versuch in %d ms", qPrintable(reason), delay);
    setState(State::Error, QStringLiteral("Verbindung zu Twitch unterbrochen (%1). Neuer Versuch in %2 s.").arg(reason).arg(seconds));
    emit changed();
}

void EventSubConnection::startKeepalive(int seconds)
{
    const int keepalive = seconds > 0 ? seconds : m_timing.keepaliveSeconds;
    m_keepaliveTimer->start(keepalive * 1000 + m_timing.keepaliveGrace);
}

// --- Nachrichten ---------------------------------------------------------------------------------

void EventSubConnection::onText(QWebSocket *socket, const QString &text)
{
    if (socket != m_socket && socket != m_nextSocket)
        return;
    Message message;
    QString error;
    // Nie den Text selbst ins Protokoll: Er enthält Chat-Nachrichten und Namen.
    if (!parseMessage(text.toUtf8(), &message, &error)) {
        qWarning("Twitch EventSub: ungültige Nachricht (%s)", qPrintable(error));
        return;
    }
    // Jede Nachricht zählt als Lebenszeichen; Ping und Pong nicht (Doku «Keepalive message»).
    if (m_phase == Phase::Subscribing || m_phase == Phase::Ready)
        m_keepaliveTimer->start();

    switch (message.kind) {
    case MessageKind::Welcome:
        onWelcome(socket, message.sessionId, message.keepaliveSeconds);
        break;
    case MessageKind::Keepalive:
        break;
    case MessageKind::Notification: {
        if (!remember(message.messageId)) {
            qInfo("Twitch EventSub: doppelte Nachricht (%s) übergangen", qPrintable(message.subscriptionType));
            return;
        }
        const std::optional<events::Event> event
            = toEvent(message, Context{m_broadcasterUserId, m_botUserId}, &error);
        if (!event) {
            qWarning("Twitch EventSub: Benachrichtigung %s nicht lesbar (%s)", qPrintable(message.subscriptionType),
                     qPrintable(error));
            return;
        }
        const bool live = event->type == QLatin1String("twitch.stream_online") ? true
                          : event->type == QLatin1String("twitch.stream_offline") ? false
                                                                                   : m_live;
        m_bus->publish(*event);
        if (live != m_live) {
            m_live = live;
            emit changed();
        }
        break;
    }
    case MessageKind::Reconnect:
        if (socket == m_socket)
            onReconnect(message.reconnectUrl);
        break;
    case MessageKind::Revocation:
        onRevocation(message.subscriptionType, message.subscriptionStatus);
        break;
    case MessageKind::Unknown:
        qInfo("Twitch EventSub: unbekannte Nachricht «%s» übergangen", qPrintable(message.messageType));
        break;
    }
}

void EventSubConnection::onWelcome(QWebSocket *socket, const QString &sessionId, int keepaliveSeconds)
{
    if (socket == m_nextSocket) {
        // Wechsel nach session_reconnect: Die alte Verbindung erst jetzt schliessen; die Abos sind
        // schon auf der neuen (Doku «Reconnect message»).
        m_reconnectTimer->stop();
        if (m_socket) {
            QWebSocket *old = m_socket;
            old->disconnect(this);
            connect(old, &QWebSocket::disconnected, old, &QObject::deleteLater);
            old->close();
        }
        m_socket = m_nextSocket;
        m_nextSocket = nullptr;
        m_sessionId = sessionId;
        startKeepalive(keepaliveSeconds);
        qInfo("Twitch EventSub: Verbindung nach session_reconnect gewechselt");
        return;
    }
    if (socket != m_socket || m_phase != Phase::Opening)
        return;
    m_welcomeTimer->stop();
    m_sessionId = sessionId;
    m_phase = Phase::Subscribing;
    startKeepalive(keepaliveSeconds);
    qInfo("Twitch EventSub: verbunden, %lld Abos werden angelegt", qlonglong(std::size(kSubscriptions)));
    subscribeAll();
    checkStream();
}

void EventSubConnection::onReconnect(const QString &url)
{
    if (m_nextSocket)
        return;
    const QUrl target(url);
    // Nie von wss auf ws wechseln: Die Adresse muss dasselbe Schema haben wie die eingestellte.
    if (!target.isValid() || target.scheme() != m_endpoints.eventsub.scheme()) {
        qCritical("Twitch EventSub: session_reconnect mit ungültiger Adresse übergangen");
        return;
    }
    qInfo("Twitch EventSub: session_reconnect, neue Verbindung wird aufgebaut");
    m_nextSocket = createSocket();
    m_reconnectTimer->start();
    m_nextSocket->open(target);
}

void EventSubConnection::onRevocation(const QString &type, const QString &status)
{
    qWarning("Twitch EventSub: Abo %s von Twitch beendet (%s)", qPrintable(type), qPrintable(status));
    for (Subscription &subscription : m_subscriptions) {
        if (subscription.type == type) {
            subscription.ok = false;
            subscription.problem = QStringLiteral("von Twitch beendet (%1)").arg(status);
        }
    }
    updateState();
    emit changed();
    if (status == QLatin1String("authorization_revoked"))
        emit authorizationRevoked();
}

void EventSubConnection::onClosed(QWebSocket *socket)
{
    const int code = int(socket->closeCode());
    if (socket == m_nextSocket) {
        qWarning("Twitch EventSub: neue Verbindung nach session_reconnect beendet (%s)", qPrintable(closeText(code)));
        lose(QStringLiteral("Wechsel der Verbindung gescheitert"));
        return;
    }
    if (socket != m_socket)
        return;
    if (m_nextSocket) {
        // Die alte Verbindung darf vor dem Welcome der neuen enden; die neue übernimmt.
        qInfo("Twitch EventSub: alte Verbindung während des Wechsels beendet (%s)", qPrintable(closeText(code)));
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
        return;
    }
    if (m_phase == Phase::Opening) {
        lose(QStringLiteral("Twitch nicht erreichbar: %1").arg(socket->errorString()));
        return;
    }
    if (code == 4003 || code == 4007)
        qCritical("Twitch EventSub: Verbindung beendet (%s), Fehler im Ablauf des Studios", qPrintable(closeText(code)));
    else if (code == 4001 || code == 4002 || code == 4004)
        qWarning("Twitch EventSub: Verbindung beendet (%s)", qPrintable(closeText(code)));
    else
        qInfo("Twitch EventSub: Verbindung beendet (%s)", qPrintable(closeText(code)));
    lose(QStringLiteral("Code %1").arg(closeText(code)));
}

// --- Abos und Stream-Zustand ---------------------------------------------------------------------

void EventSubConnection::subscribeAll()
{
    m_subscriptions.clear();
    for (const SubscriptionSpec &spec : kSubscriptions)
        m_subscriptions.append({QString::fromLatin1(spec.type), QString::fromLatin1(spec.version), false,
                                QStringLiteral("wird angelegt")});
    m_openSubscriptions = m_subscriptions.size();
    emit changed();
    const quint64 generation = m_generation;
    const QPointer<EventSubConnection> self(this);
    for (qsizetype i = 0; i < m_subscriptions.size(); ++i) {
        m_helix->post(m_token, QStringLiteral("/helix/eventsub/subscriptions"),
                      subscriptionRequest(kSubscriptions[i], m_broadcasterUserId, m_sessionId),
                      [self, generation, i](const HelixClient::Reply &reply) {
                          if (!self || generation != self->m_generation)
                              return;
                          QString problem;
                          if (!reply.ok()) {
                              problem = reply.status == 0
                                            ? reply.error
                                            : QStringLiteral("HTTP %1: %2")
                                                  .arg(reply.status)
                                                  .arg(reply.body.value(QStringLiteral("message")).toString());
                          }
                          self->onSubscribed(i, reply.status, problem);
                      });
    }
}

void EventSubConnection::onSubscribed(qsizetype index, int status, const QString &problem)
{
    Subscription &subscription = m_subscriptions[index];
    subscription.ok = problem.isEmpty();
    subscription.problem = problem;
    if (!subscription.ok)
        qWarning("Twitch EventSub: Abo %s abgewiesen (%s)", qPrintable(subscription.type), qPrintable(problem));
    else if (status != 202)
        qInfo("Twitch EventSub: Abo %s angelegt (HTTP %d)", qPrintable(subscription.type), status);
    if (--m_openSubscriptions > 0) {
        emit changed();
        return;
    }
    m_phase = Phase::Ready;
    const bool anyOk = std::any_of(m_subscriptions.cbegin(), m_subscriptions.cend(), [](const Subscription &s) { return s.ok; });
    if (anyOk)
        m_retryAttempt = 0; // erst dann gilt die Verbindung als gelungen
    const qsizetype okCount = std::count_if(m_subscriptions.cbegin(), m_subscriptions.cend(), [](const Subscription &s) { return s.ok; });
    qInfo("Twitch EventSub: %lld von %lld Abos aktiv", qlonglong(okCount), qlonglong(m_subscriptions.size()));
    updateState();
    emit changed();
}

void EventSubConnection::updateState()
{
    if (m_phase != Phase::Ready)
        return;
    QStringList failed;
    for (const Subscription &subscription : std::as_const(m_subscriptions)) {
        if (!subscription.ok)
            failed.append(subscription.type);
    }
    setState(State::Connected, failed.isEmpty() ? QString()
                                                : QStringLiteral("Abos nicht aktiv: %1.").arg(failed.join(QStringLiteral(", "))));
}

void EventSubConnection::checkStream()
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("user_id"), m_broadcasterUserId);
    const quint64 generation = m_generation;
    const QPointer<EventSubConnection> self(this);
    m_helix->get(m_token, QStringLiteral("/helix/streams"), query, [self, generation](const HelixClient::Reply &reply) {
        if (!self || generation != self->m_generation)
            return;
        if (!reply.ok()) {
            qWarning("Twitch EventSub: Stream-Zustand nicht abrufbar (%s)",
                     qPrintable(reply.status == 0 ? reply.error : QStringLiteral("HTTP %1").arg(reply.status)));
            return;
        }
        const QJsonArray data = reply.body.value(QStringLiteral("data")).toArray();
        if (data.isEmpty()) {
            // Offline: Ein verpasstes stream.offline wird nicht nachgebildet (Ereignisse nur von Twitch).
            if (self->m_live) {
                qInfo("Twitch EventSub: Stream läuft nicht mehr");
                self->m_live = false;
                emit self->changed();
            }
            return;
        }
        // Schon bekannt (Neuverbinden bei laufendem Stream): kein zweites at_start.
        if (self->m_live)
            return;
        QString error;
        const std::optional<events::Event> event = streamOnlineAtStart(data.first().toObject(), &error);
        if (!event) {
            qWarning("Twitch EventSub: Stream-Zustand nicht lesbar (%s)", qPrintable(error));
            return;
        }
        qInfo("Twitch EventSub: Stream läuft schon");
        self->m_bus->publish(*event);
        self->m_live = true;
        emit self->changed();
    });
}

bool EventSubConnection::remember(const QString &messageId)
{
    if (messageId.isEmpty())
        return true;
    if (m_seenIds.contains(messageId))
        return false;
    m_seenIds.insert(messageId);
    m_seenOrder.enqueue(messageId);
    while (m_seenOrder.size() > kRememberedIds)
        m_seenIds.remove(m_seenOrder.dequeue());
    return true;
}

} // namespace dss::twitch
