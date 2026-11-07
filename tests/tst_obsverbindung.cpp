// Verbindung zu OBS (P10c, T-05) gegen den Testserver auf 127.0.0.1, ohne Kern und ohne Fenster:
// Anmeldung, Szenenwechsel, Wiederverbinden, tote Verbindung, Abbruch während einer Anfrage.

#include "obsconnection.h"
#include "obstestserver.h"

#include <QJsonObject>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>

using namespace dss::obs;
using testhelp::ObsTestServer;
using testhelp::TcpGate;

namespace {

ObsConnection::Timing fastTiming()
{
    ObsConnection::Timing t;
    t.reconnect = 100;
    t.connectTimeout = 1000;
    t.ping = 100;
    t.pongTimeout = 400;
    t.requestTimeout = 400;
    t.busyRetry = 50;
    return t;
}

ObsConnection::PasswordProvider passwordOf(const QString *password)
{
    return [password](const ObsConnection::PasswordDone &done) { done(*password); };
}

QString randomPassword()
{
    return QString::number(QRandomGenerator::system()->generate64(), 36);
}

Config configFor(quint16 port)
{
    return {true, QStringLiteral("127.0.0.1"), port};
}

constexpr int kWait = 5000;

} // namespace

// Zugriff auf die Anfragen: In der Software gehören sie nur ObsSources (friend).
class TestObsConnection : public QObject
{
    Q_OBJECT

private slots:
    void offStaysOffAndDoesNotConnect()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        QCOMPARE(connection.state(), ObsConnection::State::Off);
        connection.configure({false, QStringLiteral("127.0.0.1"), server.port()});
        QTest::qWait(200);
        QCOMPARE(connection.state(), ObsConnection::State::Off);
        QCOMPARE(server.connectionCount(), 0);
    }

    void connectsWithPassword()
    {
        ObsTestServer server;
        server.password = randomPassword();
        QVERIFY(server.start());
        QString password = server.password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        QSignalSpy states(&connection, &ObsConnection::stateChanged);
        connection.configure(configFor(server.port()));
        QCOMPARE(connection.state(), ObsConnection::State::Connecting);
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        QCOMPARE(connection.obsVersion(), QStringLiteral("33.0.1"));
        QCOMPARE(connection.websocketVersion(), QStringLiteral("5.7.6"));
        QCOMPARE(connection.currentScene().name, QStringLiteral("Live"));
        QVERIFY(connection.message().isEmpty());
        QCOMPARE(server.lastEventSubscriptions, 15);
        QCOMPARE(states.size(), 2); // Connecting, Connected
    }

    void connectsWithoutPasswordWhenNotRequired()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
    }

    void wrongPasswordStopsUntilChanged()
    {
        ObsTestServer server;
        server.password = randomPassword();
        QVERIFY(server.start());
        QString password = randomPassword();
        ObsConnection connection(passwordOf(&password), fastTiming());
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Error, kWait);
        QCOMPARE(connection.message(),
                 QStringLiteral("OBS hat das Passwort abgelehnt. Im Dialog «Einstellungen» neu eingeben."));
        // Kein neuer Versuch ohne neues Passwort.
        const int count = server.connectionCount();
        QTest::qWait(400);
        QCOMPARE(server.connectionCount(), count);
        password = server.password;
        connection.configure(configFor(server.port()), true);
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
    }

    void unreadablePasswordDoesNotConnect()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        ObsConnection connection([](const ObsConnection::PasswordDone &done) { done(std::nullopt); }, fastTiming());
        connection.configure(configFor(server.port()));
        QCOMPARE(connection.state(), ObsConnection::State::Error);
        QCOMPARE(connection.message(), QStringLiteral("Gespeichertes Passwort nicht lesbar. Neu eingeben."));
        QTest::qWait(200);
        QCOMPARE(server.connectionCount(), 0);
    }

    void unreadablePasswordIsNotPassedAfterSwitchingOff()
    {
        // Antwort auf einen veralteten Leseauftrag: verworfen.
        ObsTestServer server;
        QVERIFY(server.start());
        ObsConnection::PasswordDone pending;
        ObsConnection connection([&pending](const ObsConnection::PasswordDone &done) { pending = done; }, fastTiming());
        connection.configure(configFor(server.port()));
        connection.configure({false, QStringLiteral("127.0.0.1"), server.port()});
        pending(QStringLiteral("egal"));
        QTest::qWait(100);
        QCOMPARE(connection.state(), ObsConnection::State::Off);
        QCOMPARE(server.connectionCount(), 0);
    }

    void closeCodesWithoutReconnect_data()
    {
        QTest::addColumn<int>("code");
        QTest::addColumn<QString>("message");
        QTest::newRow("4010") << 4010 << QStringLiteral("OBS nutzt eine andere Version von obs-websocket.");
        QTest::newRow("4011") << 4011 << QStringLiteral("OBS hat die Verbindung beendet.");
    }
    void closeCodesWithoutReconnect()
    {
        QFETCH(int, code);
        QFETCH(QString, message);
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        const Config config = configFor(server.port());
        connection.configure(config);
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        server.closeClients(code);
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Error, kWait);
        QCOMPARE(connection.message(), message);
        const int count = server.connectionCount();
        QTest::qWait(400);
        QCOMPARE(server.connectionCount(), count);
        // «OK» im Dialog: gleiche Werte, trotzdem ein neuer Versuch.
        connection.configure(config);
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
    }

    void sameConfigDoesNotReconnect()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        const Config config = configFor(server.port());
        connection.configure(config);
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        connection.configure(config);
        QTest::qWait(100);
        QCOMPARE(server.connectionCount(), 1);
        QCOMPARE(connection.state(), ObsConnection::State::Connected);
    }

    void serverOffGoesRedWithoutFlickerAndRecovers()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        const quint16 port = server.port();
        server.stop();
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        QList<ObsConnection::State> seen;
        connect(&connection, &ObsConnection::stateChanged, this,
                [&seen](ObsConnection::State s) { seen.append(s); });
        connection.configure(configFor(port));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Error, kWait);
        QCOMPARE(connection.message(), QStringLiteral("OBS unter 127.0.0.1:%1 nicht erreichbar. Neuer Versuch läuft.").arg(port));
        QTest::qWait(350); // mehrere Versuche
        QVERIFY(server.start(port));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        // Orange nur beim ersten Versuch, dann rot bis zum Erfolg (F9).
        const QList<ObsConnection::State> expected{ObsConnection::State::Connecting, ObsConnection::State::Error,
                                                   ObsConnection::State::Connected};
        QCOMPARE(seen, expected);
    }

    void obsRestartReconnects()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        const quint16 port = server.port();
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        connection.configure(configFor(port));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        server.sendEvent(QStringLiteral("ExitStarted"));
        server.stop();
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Error, kWait);
        QVERIFY(server.start(port));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        QCOMPARE(server.connectionCount(), 2);
    }

    void deadConnectionIsDetected()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        TcpGate gate(server.port());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        connection.configure(configFor(gate.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        QTest::qWait(600); // Pings werden beantwortet: länger als pongTimeout ohne Abbruch
        QCOMPARE(connection.state(), ObsConnection::State::Connected);
        gate.hold(true);
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Error, kWait);
        gate.hold(false);
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
    }

    void silentServerTimesOutDuringLogin()
    {
        // Antwortet auf GetSceneList nie: Versuch wird beendet und wiederholt, nicht grün.
        ObsTestServer server;
        server.silentRequests.insert(QStringLiteral("GetSceneList"));
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Error, kWait);
        server.silentRequests.clear();
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
    }

    void sceneSignalOncePerChangeWithValue()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        qRegisterMetaType<dss::obs::Scene>();
        QSignalSpy spy(&connection, &ObsConnection::currentSceneChanged);
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        // Erster Stand nach dem Verbinden
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).value<Scene>(), (Scene{QStringLiteral("scene-live"), QStringLiteral("Live")}));
        // Echter Wechsel
        server.switchScene(server.scenes.at(1));
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, kWait);
        QCOMPARE(spy.takeFirst().at(0).value<Scene>(), (Scene{QStringLiteral("scene-pause"), QStringLiteral("Pause")}));
        QCOMPARE(connection.currentScene().name, QStringLiteral("Pause"));
        // Gleiche Szene: kein Signal.
        server.switchScene(server.scenes.at(1));
        QTest::qWait(100);
        QCOMPARE(spy.size(), 0);
    }

    void renamedSceneIsNoChange()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        qRegisterMetaType<dss::obs::Scene>();
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        QSignalSpy spy(&connection, &ObsConnection::currentSceneChanged);
        server.sendEvent(QStringLiteral("SceneNameChanged"),
                         {{QStringLiteral("sceneUuid"), QStringLiteral("scene-live")},
                          {QStringLiteral("oldSceneName"), QStringLiteral("Live")},
                          {QStringLiteral("sceneName"), QStringLiteral("Sendung")}});
        QTRY_COMPARE_WITH_TIMEOUT(connection.currentScene().name, QStringLiteral("Sendung"), kWait);
        QCOMPARE(spy.size(), 0);
    }

    void sceneSignalAfterReconnectOnlyOnDifference()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        const quint16 port = server.port();
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        qRegisterMetaType<dss::obs::Scene>();
        connection.configure(configFor(port));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        QSignalSpy spy(&connection, &ObsConnection::currentSceneChanged);
        // Neu verbunden, gleiche Szene: kein Signal.
        server.stop();
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Error, kWait);
        QVERIFY(server.start(port));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        QCOMPARE(spy.size(), 0);
        // Neu verbunden, andere Szene: ein Signal.
        server.stop();
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Error, kWait);
        server.current = server.scenes.at(1);
        QVERIFY(server.start(port));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).value<Scene>().name, QStringLiteral("Pause"));
    }

    // Anfragen (für ObsSources)

    void requestNotConnectedAnswersAtOnce()
    {
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        RequestFailure failure = RequestFailure::None;
        connection.request(QStringLiteral("GetVersion"), {},
                           [&failure](const Response &, RequestFailure f) { failure = f; });
        QCOMPARE(failure, RequestFailure::NotConnected);
    }

    void requestAbortAnswersDisconnected()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        server.closingRequests.insert(QStringLiteral("CreateInput"));
        std::optional<RequestFailure> failure;
        connection.request(QStringLiteral("CreateInput"), {},
                           [&failure](const Response &, RequestFailure f) { failure = f; });
        QTRY_VERIFY_WITH_TIMEOUT(failure.has_value(), kWait);
        QCOMPARE(*failure, RequestFailure::Disconnected);
        QCOMPARE(connection.state(), ObsConnection::State::Error);
    }

    void requestWithoutAnswerTimesOut()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        server.silentRequests.insert(QStringLiteral("GetInputList"));
        std::optional<RequestFailure> failure;
        connection.request(QStringLiteral("GetInputList"), {},
                           [&failure](const Response &, RequestFailure f) { failure = f; });
        QTRY_VERIFY_WITH_TIMEOUT(failure.has_value(), kWait);
        QCOMPARE(*failure, RequestFailure::Timeout);
        QCOMPARE(connection.state(), ObsConnection::State::Connected); // Verbindung bleibt
    }

    void busyIsRetried()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        server.busyAnswers = 3;
        server.busyRequest = QStringLiteral("GetInputList");
        std::optional<Response> response;
        connection.request(QStringLiteral("GetInputList"), {}, [&response](const Response &r, RequestFailure f) {
            QCOMPARE(f, RequestFailure::None);
            response = r;
        });
        QTRY_VERIFY_WITH_TIMEOUT(response.has_value(), kWait);
        QVERIFY(response->ok);
        QCOMPARE(server.requestCount(QStringLiteral("GetInputList")), 4);
    }

    void busyGivesUpAfterFiveRetries()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        server.busyAnswers = 100;
        server.busyRequest = QStringLiteral("GetInputList");
        std::optional<RequestFailure> failure;
        connection.request(QStringLiteral("GetInputList"), {},
                           [&failure](const Response &, RequestFailure f) { failure = f; });
        QTRY_VERIFY_WITH_TIMEOUT(failure.has_value(), kWait);
        QCOMPARE(*failure, RequestFailure::Timeout);
        QCOMPARE(server.requestCount(QStringLiteral("GetInputList")), 6); // 1 und 5 Wiederholungen
    }

    void requestsWaitDuringCollectionChange()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        qRegisterMetaType<dss::obs::Scene>();
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        QSignalSpy scenes(&connection, &ObsConnection::currentSceneChanged);
        const int listsBefore = server.requestCount(QStringLiteral("GetSceneList"));

        server.collectionChanging = true;
        server.sendEvent(QStringLiteral("CurrentSceneCollectionChanging"),
                         {{QStringLiteral("sceneCollectionName"), QStringLiteral("Alt")}});
        QTest::qWait(50);
        std::optional<Response> response;
        connection.request(QStringLiteral("GetInputList"), {},
                           [&response](const Response &r, RequestFailure) { response = r; });
        QTest::qWait(50);
        QVERIFY(!response.has_value());
        QCOMPARE(server.requestCount(QStringLiteral("GetInputList")), 0);

        server.collectionChanging = false;
        server.current = server.scenes.at(1);
        server.sendEvent(QStringLiteral("CurrentSceneCollectionChanged"),
                         {{QStringLiteral("sceneCollectionName"), QStringLiteral("Neu")}});
        QTRY_VERIFY_WITH_TIMEOUT(response.has_value(), kWait);
        QVERIFY(response->ok);
        QCOMPARE(server.requestsDuringCollectionChange, 0);
        // Neue Sammlung: aktuelle Szene neu geholt, ein Signal.
        QTRY_COMPARE_WITH_TIMEOUT(scenes.size(), 1, kWait);
        QCOMPARE(server.requestCount(QStringLiteral("GetSceneList")), listsBefore + 1);
    }

    void exitStartedKeepsEventsAway()
    {
        ObsTestServer server;
        QVERIFY(server.start());
        QString password;
        ObsConnection connection(passwordOf(&password), fastTiming());
        qRegisterMetaType<dss::obs::Scene>();
        connection.configure(configFor(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(connection.state(), ObsConnection::State::Connected, kWait);
        QSignalSpy scenes(&connection, &ObsConnection::currentSceneChanged);
        server.sendEvent(QStringLiteral("ExitStarted"));
        server.switchScene(server.scenes.at(1));
        QTest::qWait(100);
        QCOMPARE(scenes.size(), 0);
        RequestFailure failure = RequestFailure::None;
        connection.request(QStringLiteral("GetVersion"), {},
                           [&failure](const Response &, RequestFailure f) { failure = f; });
        QCOMPARE(failure, RequestFailure::NotConnected);
    }
};

QTEST_GUILESS_MAIN(TestObsConnection)
#include "tst_obsverbindung.moc"
