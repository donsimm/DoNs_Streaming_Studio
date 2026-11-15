// Verbindung zu EventSub über WebSocket (P13, Commit 6) gegen den Testserver tests/twitchtestserver.h
// auf 127.0.0.1: Abos innerhalb von 10 s, ein Abo abgewiesen; Keepalive verpasst, session_reconnect
// (alte Verbindung erst nach dem Welcome der neuen zu), Wechsel ohne Welcome; Close-Codes 4000 bis
// 4007, revocation, doppelte message_id, Wartezeiten; Stream-Zustand beim Start (at_start);
// QSslSocket::supportsSsl. Ohne Netz und ohne Qt Gui; Nachrichten aus tests/daten/twitch/.

#include "eventbus.h"
#include "eventsubconnection.h"
#include "eventsubprotocol.h"
#include "helixclient.h"
#include "twitchauth.h"
#include "twitchtestserver.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using dss::events::Event;
using dss::events::EventBus;
using dss::twitch::EventSubConnection;
using dss::twitch::Endpoints;
using dss::twitch::HelixClient;
using dss::twitch::TokenSource;
using testhelp::TwitchTestServer;
using testhelp::TwitchTestUser;
using State = EventSubConnection::State;

namespace {

constexpr int kWait = 10000;
const TwitchTestUser kChannelUser{QStringLiteral("100000001"), QStringLiteral("testkanal")};
const QString kBotUserId = QStringLiteral("100000002");

struct LogEntry
{
    QtMsgType type;
    QString text;
};
QList<LogEntry> g_messages;
QtMessageHandler g_previousHandler = nullptr;

void captureMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    g_messages.append({type, message});
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

bool logContains(const QString &text, QtMsgType type)
{
    for (const LogEntry &entry : std::as_const(g_messages)) {
        if (entry.type == type && entry.text.contains(text))
            return true;
    }
    return false;
}

QJsonObject testFile(const QString &name)
{
    QFile f(QStringLiteral(DSS_TEST_TWITCH_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// Fester Token des Testservers; renew klappt nie.
class FixedToken : public TokenSource
{
public:
    QString token;
    QString accessToken() const override { return token; }
    void renew(std::function<void(bool)> done) override { done(false); }
};

EventSubConnection::Timing fastTiming()
{
    EventSubConnection::Timing timing;
    timing.retryMin = 50;
    timing.retryMax = 400;
    return timing;
}

} // namespace

class TestTwitchConnection : public QObject
{
    Q_OBJECT

    QNetworkAccessManager m_network;
    std::unique_ptr<TwitchTestServer> m_server;
    std::unique_ptr<EventBus> m_bus;
    std::unique_ptr<HelixClient> m_helix;
    FixedToken m_token;
    QList<Event> m_events;
    dss::events::Subscription m_subscription;
    QStringList m_secrets;

    std::unique_ptr<EventSubConnection> connection(const EventSubConnection::Timing &timing = fastTiming())
    {
        auto c = std::make_unique<EventSubConnection>(m_bus.get(), m_helix.get(), timing);
        c->setEndpoints(Endpoints::testServer(m_server->baseUrl()));
        c->setBotUserId(kBotUserId);
        return c;
    }
    bool waitConnected(EventSubConnection &c, int sessions = 1)
    {
        return QTest::qWaitFor([&] {
            return int(m_server->sessions.size()) >= sessions && c.state() == State::Connected
                   && c.sessionId() == m_server->latestSession()->id;
        }, kWait);
    }
    QList<Event> eventsOf(const QString &type) const
    {
        QList<Event> result;
        for (const Event &e : m_events) {
            if (e.type == type)
                result.append(e);
        }
        return result;
    }

private slots:
    void initTestCase()
    {
        g_previousHandler = qInstallMessageHandler(captureMessage);
    }

    void init()
    {
        m_server = std::make_unique<TwitchTestServer>();
        QVERIFY(m_server->start());
        m_bus = std::make_unique<EventBus>();
        m_helix = std::make_unique<HelixClient>(&m_network);
        m_helix->setEndpoints(Endpoints::testServer(m_server->baseUrl()));
        m_helix->setClientId(m_server->clientId);
        m_token.token = m_server->addToken(kChannelUser, dss::twitch::channelScopes());
        m_events.clear();
        m_subscription = m_bus->subscribe(this, {QStringLiteral("twitch.")}, [this](const Event &e) { m_events.append(e); });
    }

    void cleanup()
    {
        m_secrets << m_server->secrets();
        m_subscription.unsubscribe();
        m_helix.reset();
        m_bus.reset();
        m_server.reset();
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
        QVERIFY(m_secrets.size() > 5);
        for (const QString &secret : std::as_const(m_secrets)) {
            for (const LogEntry &entry : std::as_const(g_messages))
                QVERIFY2(!entry.text.contains(secret), qPrintable(QStringLiteral("Token im Protokoll: ") + entry.text.left(40)));
        }
    }

    // Plan 2.5: TLS über OpenSSL muss da sein; sonst «TLS fehlt» und kein Versuch.
    void tls()
    {
        QVERIFY(EventSubConnection::tlsSupported());
        auto c = connection();
        c->m_tlsSupported = false;
        c->setEndpoints(Endpoints::twitch());
        c->start(&m_token, kChannelUser.id);
        QCOMPARE(c->state(), State::Error);
        QVERIFY(c->message().startsWith(QStringLiteral("TLS fehlt")));
        QTest::qWait(200);
        QCOMPARE(int(m_server->sessions.size()), 0);
        QVERIFY(!c->m_retryTimer->isActive());
    }

    // Alle 10 Abos mit der session_id, innerhalb von 10 s nach dem Welcome; Bedingungen je Typ.
    void subscribesWithinTenSeconds()
    {
        auto c = connection();
        QSignalSpy changed(c.get(), &EventSubConnection::changed);
        c->start(&m_token, kChannelUser.id);
        QCOMPARE(c->state(), State::Connecting);
        QVERIFY(waitConnected(*c));
        TwitchTestServer::WsSession *s = m_server->latestSession();
        QCOMPARE(QUrlQuery(s->requestUrl).queryItemValue(QStringLiteral("keepalive_timeout_seconds")), QStringLiteral("10"));
        const QList<TwitchTestServer::SubscriptionRecord> subs = m_server->subscriptionsFor(s->id);
        QCOMPARE(subs.size(), qsizetype(std::size(dss::twitch::kSubscriptions)));
        QCOMPARE(subs.size(), 10);
        QStringList types;
        for (const auto &r : subs) {
            types << r.type;
            QVERIFY2(s->welcomedAt.msecsTo(r.at) < 10000, qPrintable(r.type));
            if (r.type == QLatin1String("channel.raid"))
                QCOMPARE(r.condition.value(QStringLiteral("to_broadcaster_user_id")).toString(), kChannelUser.id);
            else
                QCOMPARE(r.condition.value(QStringLiteral("broadcaster_user_id")).toString(), kChannelUser.id);
        }
        types.sort();
        types.removeDuplicates();
        QCOMPARE(types.size(), 10);
        QVERIFY(c->allSubscriptionsOk());
        QVERIFY(c->message().isEmpty());
        QCOMPARE(c->subscriptions().size(), 10);
        QVERIFY(!changed.isEmpty());
        QVERIFY(!c->isLive());
        QVERIFY(eventsOf(QStringLiteral("twitch.stream_online")).isEmpty());
        QCOMPARE(m_server->openSessions(), 1);

        c->stop();
        QCOMPARE(c->state(), State::Off);
        QVERIFY(QTest::qWaitFor([this] { return m_server->openSessions() == 0; }, kWait));
    }

    // Ein abgewiesenes Abo hält die anderen nicht auf.
    void oneSubscriptionRejected()
    {
        m_server->rejectSubscriptions.insert(QStringLiteral("channel.follow"), 403);
        auto c = connection();
        c->start(&m_token, kChannelUser.id);
        QVERIFY(waitConnected(*c));
        QCOMPARE(m_server->subscriptionsFor(c->sessionId()).size(), 9);
        QVERIFY(!c->allSubscriptionsOk());
        int ok = 0;
        for (const EventSubConnection::Subscription &s : c->subscriptions()) {
            if (s.type == QLatin1String("channel.follow")) {
                QVERIFY(!s.ok);
                QVERIFY2(s.problem.contains(QStringLiteral("403")), qPrintable(s.problem));
            } else {
                ok += s.ok ? 1 : 0;
            }
        }
        QCOMPARE(ok, 9);
        QVERIFY(c->message().contains(QStringLiteral("channel.follow")));
        QVERIFY(logContains(QStringLiteral("Abo channel.follow abgewiesen"), QtWarningMsg));
    }

    // Benachrichtigungen werden Ereignisse; dieselbe message_id nur einmal; Bot-Konto erkannt.
    void notificationsAndDuplicates()
    {
        auto c = connection();
        c->start(&m_token, kChannelUser.id);
        QVERIFY(waitConnected(*c));
        TwitchTestServer::WsSession *s = m_server->latestSession();
        const QJsonObject chat = testFile(QStringLiteral("chat_message.json"));
        QVERIFY(!chat.isEmpty());
        m_server->sendNotification(s, chat, QStringLiteral("doppelt-1"));
        m_server->sendNotification(s, chat, QStringLiteral("doppelt-1"));
        m_server->sendNotification(s, testFile(QStringLiteral("chat_bot.json")), QStringLiteral("bot-1"));
        m_server->sendNotification(s, testFile(QStringLiteral("follow.json")), QStringLiteral("follow-1"));
        QVERIFY(QTest::qWaitFor([this] { return !eventsOf(QStringLiteral("twitch.follow")).isEmpty(); }, kWait));
        QTest::qWait(100);
        const QList<Event> messages = eventsOf(QStringLiteral("twitch.chat_message"));
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).originId, QStringLiteral("doppelt-1"));
        QCOMPARE(messages.at(0).data.value(QStringLiteral("is_bot_account")).toBool(), false);
        QCOMPARE(messages.at(1).originId, QStringLiteral("bot-1"));
        QCOMPARE(messages.at(1).data.value(QStringLiteral("is_bot_account")).toBool(), true);
        QVERIFY(logContains(QStringLiteral("doppelte Nachricht"), QtInfoMsg));
        // Kein Chat-Text im Protokoll (Plan, Interna «Chat-Texte nicht ins Protokoll»)
        for (const LogEntry &entry : std::as_const(g_messages))
            QVERIFY(!entry.text.contains(QStringLiteral("Hallo zusammen")));

        // Die Liste ist begrenzt (500): nach 500 anderen gilt doppelt-1 wieder als neu.
        for (int i = 0; i < 500; ++i)
            QVERIFY(c->remember(QStringLiteral("andere-%1").arg(i)));
        QVERIFY(c->remember(QStringLiteral("doppelt-1")));
        QVERIFY(!c->remember(QStringLiteral("andere-499")));
    }

    // Stream läuft schon beim Verbinden: einmal twitch.stream_online mit at_start true.
    void streamStateAtStart()
    {
        m_server->streams = testFile(QStringLiteral("streams_live.json"));
        QVERIFY(!m_server->streams.isEmpty());
        auto c = connection();
        c->start(&m_token, kChannelUser.id);
        QVERIFY(waitConnected(*c));
        QVERIFY(QTest::qWaitFor([this] { return !eventsOf(QStringLiteral("twitch.stream_online")).isEmpty(); }, kWait));
        const QList<Event> online = eventsOf(QStringLiteral("twitch.stream_online"));
        QCOMPARE(online.size(), 1);
        QCOMPARE(online.first().data.value(QStringLiteral("at_start")).toBool(), true);
        QCOMPARE(online.first().data.value(QStringLiteral("started_at")).toString(), QStringLiteral("2026-11-15T17:58:00Z"));
        QVERIFY(c->isLive());
        const QList<testhelp::TwitchHttpRequest> requests = m_server->requestsTo(QStringLiteral("/helix/streams"));
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().query.queryItemValue(QStringLiteral("user_id")), kChannelUser.id);

        // Neu verbinden bei laufendem Stream: Zustand wird wieder abgefragt, aber kein zweites at_start.
        m_server->closeSession(m_server->latestSession(), 4000);
        QVERIFY(waitConnected(*c, 2));
        QVERIFY(QTest::qWaitFor([this] { return m_server->requestsTo(QStringLiteral("/helix/streams")).size() == 2; }, kWait));
        QTest::qWait(100);
        QCOMPARE(eventsOf(QStringLiteral("twitch.stream_online")).size(), 1);

        // stream.offline über EventSub beendet den Zustand.
        m_server->sendNotification(m_server->latestSession(), testFile(QStringLiteral("stream_offline.json")),
                                   QStringLiteral("offline-1"));
        // Der Bus stellt verzögert zu (QueuedConnection): auf das Ereignis warten, nicht auf isLive().
        QVERIFY(QTest::qWaitFor([this] { return !eventsOf(QStringLiteral("twitch.stream_offline")).isEmpty(); }, kWait));
        QCOMPARE(eventsOf(QStringLiteral("twitch.stream_offline")).size(), 1);
        QVERIFY(!c->isLive());
    }

    // Keine Nachricht innerhalb von Keepalive + Spielraum: neu verbinden, Abos neu anlegen.
    void keepaliveMissed()
    {
        m_server->autoKeepalive = false;
        EventSubConnection::Timing timing = fastTiming();
        timing.keepaliveSeconds = 1;
        timing.keepaliveGrace = 700;
        auto c = connection(timing);
        c->start(&m_token, kChannelUser.id);
        QVERIFY(waitConnected(*c));
        const QString first = c->sessionId();
        QElapsedTimer timer;
        timer.start();
        QVERIFY(waitConnected(*c, 2));
        QVERIFY2(timer.elapsed() >= 1000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(c->sessionId() != first);
        QCOMPARE(m_server->subscriptionsFor(c->sessionId()).size(), 10);
        QVERIFY(logContains(QStringLiteral("Keepalive"), QtWarningMsg));
        QVERIFY(QTest::qWaitFor([this] { return m_server->sessions.front()->closed; }, kWait));

        // Mit Keepalive vom Server bleibt die Verbindung.
        c->stop();
        m_server->autoKeepalive = true;
        c->start(&m_token, kChannelUser.id);
        const int sessions = int(m_server->sessions.size()) + 1;
        QVERIFY(waitConnected(*c, sessions));
        QTest::qWait(2500);
        QCOMPARE(int(m_server->sessions.size()), sessions);
        QCOMPARE(c->state(), State::Connected);
    }

    // session_reconnect: neue Verbindung zu reconnect_url, alte erst nach deren Welcome zu, keine
    // neuen Abos; die alte liefert bis dahin weiter.
    void sessionReconnect()
    {
        auto c = connection();
        c->start(&m_token, kChannelUser.id);
        QVERIFY(waitConnected(*c));
        TwitchTestServer::WsSession *old = m_server->latestSession();
        m_server->autoWelcome = false;
        m_server->sendReconnect(old);
        QVERIFY(QTest::qWaitFor([this] { return m_server->sessions.size() == 2; }, kWait));
        TwitchTestServer::WsSession *next = m_server->latestSession();
        QCOMPARE(next->requestUrl.toString(), m_server->reconnectUrl(old));
        QTest::qWait(200);
        QVERIFY(!old->closed);
        QCOMPARE(c->state(), State::Connected);
        m_server->sendNotification(old, testFile(QStringLiteral("follow.json")), QStringLiteral("alt-1"));
        QVERIFY(QTest::qWaitFor([this] { return eventsOf(QStringLiteral("twitch.follow")).size() == 1; }, kWait));

        m_server->sendWelcome(next);
        QVERIFY(QTest::qWaitFor([old] { return old->closed; }, kWait));
        QVERIFY(!next->closed);
        QCOMPARE(m_server->subscriptions.size(), 10);
        QCOMPARE(c->state(), State::Connected);
        m_server->sendNotification(next, testFile(QStringLiteral("raid.json")), QStringLiteral("neu-1"));
        QVERIFY(QTest::qWaitFor([this] { return eventsOf(QStringLiteral("twitch.raid")).size() == 1; }, kWait));
        QVERIFY(logContains(QStringLiteral("gewechselt"), QtInfoMsg));
    }

    // Ohne Welcome auf der neuen Verbindung: wie verloren, frisch verbinden mit neuen Abos.
    void reconnectWithoutWelcome()
    {
        EventSubConnection::Timing timing = fastTiming();
        timing.reconnectTimeout = 500;
        auto c = connection(timing);
        c->start(&m_token, kChannelUser.id);
        QVERIFY(waitConnected(*c));
        m_server->autoWelcome = false;
        m_server->sendReconnect(m_server->latestSession());
        QVERIFY(QTest::qWaitFor([this] { return m_server->sessions.size() == 2; }, kWait));
        m_server->autoWelcome = true;
        QVERIFY(waitConnected(*c, 3));
        QVERIFY(!m_server->latestSession()->reconnected);
        QCOMPARE(m_server->subscriptionsFor(c->sessionId()).size(), 10);
        QVERIFY(m_server->sessions.at(0)->closed);
        QVERIFY(m_server->sessions.at(1)->closed);
        QVERIFY(logContains(QStringLiteral("ohne Begrüssung"), QtWarningMsg));
    }

    // Close-Codes 4000 bis 4007: immer neu verbinden, 4003 und 4007 kritisch im Protokoll.
    void closeCodes_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<int>("type");
        QTest::newRow("4000") << 4000 << int(QtInfoMsg);
        QTest::newRow("4001") << 4001 << int(QtWarningMsg);
        QTest::newRow("4002") << 4002 << int(QtWarningMsg);
        QTest::newRow("4003") << 4003 << int(QtCriticalMsg);
        QTest::newRow("4004") << 4004 << int(QtWarningMsg);
        QTest::newRow("4005") << 4005 << int(QtInfoMsg);
        QTest::newRow("4006") << 4006 << int(QtInfoMsg);
        QTest::newRow("4007") << 4007 << int(QtCriticalMsg);
    }
    void closeCodes()
    {
        QFETCH(int, code);
        QFETCH(int, type);
        auto c = connection();
        c->start(&m_token, kChannelUser.id);
        QVERIFY(waitConnected(*c));
        g_messages.clear();
        m_server->closeSession(m_server->latestSession(), code);
        QVERIFY(QTest::qWaitFor([&] { return c->state() == State::Error; }, kWait));
        QVERIFY2(c->message().contains(QString::number(code)), qPrintable(c->message()));
        QVERIFY(logContains(QStringLiteral("Verbindung beendet (%1").arg(code), QtMsgType(type)));
        QVERIFY(waitConnected(*c, 2));
        QCOMPARE(m_server->subscriptionsFor(c->sessionId()).size(), 10);
    }

    // Alle Abos abgewiesen: Twitch beendet nach der Frist mit 4003 (Testserver wie Twitch), der
    // Kern versucht es neu; klappen die Abos wieder, bleibt die Verbindung.
    void unusedConnectionClosed()
    {
        m_server->unusedTimeout = 300;
        for (const dss::twitch::SubscriptionSpec &spec : dss::twitch::kSubscriptions)
            m_server->rejectSubscriptions.insert(QString::fromLatin1(spec.type), 403);
        auto c = connection();
        c->start(&m_token, kChannelUser.id);
        QVERIFY(QTest::qWaitFor([this] { return m_server->sessions.size() >= 2; }, kWait));
        QVERIFY(m_server->sessions.front()->closed);
        QVERIFY(logContains(QStringLiteral("Verbindung beendet (4003"), QtCriticalMsg));
        QVERIFY(c->subscriptions().size() == 10);
        m_server->rejectSubscriptions.clear();
        QVERIFY(QTest::qWaitFor([&] { return c->allSubscriptionsOk(); }, kWait));
        const qsizetype sessions = m_server->sessions.size();
        QTest::qWait(800);
        QCOMPARE(m_server->sessions.size(), sessions);
        QCOMPARE(c->state(), State::Connected);
    }

    // revocation: Abo weg mit Status; authorization_revoked meldet authorizationRevoked().
    void revocation()
    {
        auto c = connection();
        QSignalSpy revoked(c.get(), &EventSubConnection::authorizationRevoked);
        c->start(&m_token, kChannelUser.id);
        QVERIFY(waitConnected(*c));
        m_server->sendRevocation(m_server->latestSession(), QStringLiteral("channel.cheer"), QStringLiteral("1"),
                                 QStringLiteral("user_removed"));
        QVERIFY(QTest::qWaitFor([&] { return !c->allSubscriptionsOk(); }, kWait));
        for (const EventSubConnection::Subscription &s : c->subscriptions()) {
            if (s.type == QLatin1String("channel.cheer"))
                QVERIFY2(s.problem.contains(QStringLiteral("user_removed")), qPrintable(s.problem));
            else
                QVERIFY(s.ok);
        }
        QVERIFY(c->message().contains(QStringLiteral("channel.cheer")));
        QCOMPARE(revoked.size(), 0);

        // Echte Datei aus der Doku: authorization_revoked bei channel.follow.
        const QJsonObject file = testFile(QStringLiteral("revocation.json"));
        QVERIFY(!file.isEmpty());
        m_server->send(m_server->latestSession(), file);
        QVERIFY(revoked.wait(kWait));
        QCOMPARE(revoked.size(), 1);
        QVERIFY(logContains(QStringLiteral("authorization_revoked"), QtWarningMsg));
        QCOMPARE(c->state(), State::Connected);
    }

    // Wartezeiten: 2 s, doppelt so lang bis 60 s, ±20 %.
    void retryDelays()
    {
        QCOMPARE(dss::twitch::backoffDelay(0, 2000, 60000, 0.5), 2000);
        QCOMPARE(dss::twitch::backoffDelay(1, 2000, 60000, 0.5), 4000);
        QCOMPARE(dss::twitch::backoffDelay(4, 2000, 60000, 0.5), 32000);
        QCOMPARE(dss::twitch::backoffDelay(5, 2000, 60000, 0.5), 60000);
        QCOMPARE(dss::twitch::backoffDelay(30, 2000, 60000, 1.0), 60000);
        QCOMPARE(dss::twitch::backoffDelay(0, 2000, 60000, 0.0), 1600);
        QCOMPARE(dss::twitch::backoffDelay(0, 2000, 60000, 1.0), 2400);
        QCOMPARE(dss::twitch::backoffDelay(5, 2000, 60000, 0.0), 48000);

        // Server weg: Fehler mit Wartezeit, wachsende Abstände; Server wieder da: verbunden.
        const quint16 port = m_server->port();
        m_server->stop();
        auto c = connection();
        c->start(&m_token, kChannelUser.id);
        QVERIFY(QTest::qWaitFor([&] { return c->state() == State::Error; }, kWait));
        QVERIFY2(c->message().contains(QStringLiteral("Neuer Versuch")), qPrintable(c->message()));
        QVERIFY(QTest::qWaitFor([&] { return c->m_retryAttempt >= 4; }, kWait));
        QVERIFY(m_server->start(port));
        QVERIFY(waitConnected(*c));
        QCOMPARE(c->m_retryAttempt, 0);
    }
};

QTEST_GUILESS_MAIN(TestTwitchConnection)
#include "tst_twitchverbindung.moc"
