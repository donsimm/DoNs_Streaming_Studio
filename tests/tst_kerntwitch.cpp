// Tests der Anbindung an Twitch im Kern (P13, Commit 7): dss-core als eigener Prozess gegen den
// Testserver tests/twitchtestserver.h (--twitch-testserver), ein Fenster als roher Socket.
// - Client Secret über update_settings, Anmeldung beider Konten über den Socket, Abzeichen aktiv,
//   Ereignisse bis zum Fenster (twitch.chat_message und twitch.first_message);
// - Neustart behält beide Anmeldungen, Abmelden löscht die Datei des Kontos;
// - kein Testtoken, kein device_code und kein Secret in Protokoll oder Socket (user_code nur im Socket);
// - --twitch-testserver mit anderer Adresse als http://127.0.0.1:PORT bricht mit Exit-Code 1 ab.
// Eigener Datenordner, eigener Ordner der Einstellungen; die Geheimnisse legt systemd-creds --user ab
// (wie tst_kernverbindung).

#include "configpaths.h"
#include "coresettings.h"
#include "prozesshilfe.h"
#include "protocol.h"
#include "twitchtestserver.h"
#include "version.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace dss;
using testhelp::TwitchTestServer;
using testhelp::TwitchTestUser;

namespace {

constexpr int kWait = 15000;
const TwitchTestUser kChannelUser{QStringLiteral("100000001"), QStringLiteral("testkanal")};
const TwitchTestUser kBotUser{QStringLiteral("100000002"), QStringLiteral("testbot")};

QJsonObject testFile(const QString &name)
{
    QFile f(QStringLiteral(DSS_TEST_TWITCH_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

// Fenster als roher Socket: sammelt jedes Byte (Suche nach Token) und die Nachrichten.
class Window
{
public:
    bool open(const QString &socketPath)
    {
        m_socket.connectToServer(socketPath);
        if (!m_socket.waitForConnected(5000))
            return false;
        QObject::connect(&m_socket, &QLocalSocket::readyRead, [this] { read(); });
        Hello hello;
        hello.program = QStringLiteral("tst_kerntwitch");
        hello.version = version();
        hello.protocolVersion = kProtocolVersion;
        send(helloMessage(hello));
        return true;
    }
    void send(const QJsonObject &message)
    {
        const QByteArray line = encodeMessage(message);
        m_sent += line;
        m_socket.write(line);
        m_socket.flush();
    }
    // Neueste Nachricht dieses Typs; leer, wenn keine.
    QJsonObject latest(const char *type) const
    {
        for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
            if (messageType(m_messages[i]) == QLatin1String(type))
                return m_messages[i];
        }
        return {};
    }
    std::optional<TwitchStatusInfo> status() const
    {
        const QJsonObject m = latest(msg::kTwitchStatus);
        TwitchStatusInfo info;
        QString error;
        if (m.isEmpty() || !parseTwitchStatus(m, &info, &error))
            return std::nullopt;
        return info;
    }
    QString twitchConnection() const
    {
        return latest(msg::kConnections).value(QStringLiteral("twitch")).toString();
    }
    std::optional<ChangeResult> result(qint64 requestId) const
    {
        for (const QJsonObject &m : m_messages) {
            ChangeResult r;
            QString error;
            if (messageType(m) == QLatin1String(msg::kChangeResult) && parseChangeResult(m, &r, &error)
                && r.requestId == requestId)
                return r;
        }
        return std::nullopt;
    }
    QStringList eventTypes() const
    {
        QStringList types;
        for (const QJsonObject &m : m_messages) {
            if (messageType(m) == QLatin1String(msg::kEvent))
                types << m.value(QStringLiteral("event")).toObject().value(QStringLiteral("type")).toString();
        }
        return types;
    }
    QByteArray traffic() const { return m_received + m_sent; }
    QByteArray received() const { return m_received; }
    void close() { m_socket.abort(); }

private:
    void read()
    {
        const QByteArray data = m_socket.readAll();
        m_received += data;
        m_lines.append(data);
        QByteArray line;
        while (m_lines.next(&line) == LineSplitter::Result::Line) {
            QJsonObject message;
            QString error;
            if (decodeMessage(line, &message, &error))
                m_messages.append(message);
        }
    }

    QLocalSocket m_socket;
    LineSplitter m_lines;
    QList<QJsonObject> m_messages;
    QByteArray m_received;
    QByteArray m_sent;
};

class Core
{
public:
    Core(const QString &dataDir, const QString &socketPath, const QString &configHome, const QStringList &args)
    {
        m_process = testhelp::startCore(dataDir, socketPath, configHome, args);
        QObject::connect(m_process.get(), &QProcess::readyReadStandardError,
                         [this] { m_log += QString::fromUtf8(m_process->readAllStandardError()); });
    }
    bool waitForLog(const QString &text, int timeoutMs = kWait)
    {
        return QTest::qWaitFor([&] { return m_log.contains(text); }, timeoutMs);
    }
    bool stop()
    {
        m_process->terminate();
        const bool ok = m_process->waitForFinished(10000) && m_process->exitStatus() == QProcess::NormalExit
                        && m_process->exitCode() == 0;
        m_log += QString::fromUtf8(m_process->readAllStandardError());
        m_log += QString::fromUtf8(m_process->readAllStandardOutput());
        return ok;
    }
    QString log() const { return m_log; }

private:
    std::unique_ptr<QProcess> m_process;
    QString m_log;
};

// Ordner der Einstellungen mit kern.ini und [twitch] eingeschaltet
QString twitchConfigHome()
{
    const QString home = testhelp::testConfigHome();
    QFile ini(home + QStringLiteral("/dons-streaming-studio-entwicklung/kern.ini"));
    if (ini.open(QIODevice::Append))
        ini.write("[twitch]\naktiv=ja\nclient-id=testclient\n");
    return home;
}

} // namespace

class TestCoreTwitch : public QObject
{
    Q_OBJECT

private slots:
    // Anmeldung beider Konten, Ereignisse bis zum Fenster, Neustart, Abmelden, keine Geheimnisse.
    void loginRestartLogout()
    {
        TwitchTestServer server;
        server.deviceInterval = 1;
        server.clientSecret = QStringLiteral("geheim-") + QUuid::createUuid().toString(QUuid::Id128).left(16);
        QVERIFY(server.start());
        const QStringList args{QStringLiteral("--twitch-testserver"), server.baseUrl().toString()};

        QTemporaryDir data;
        const auto runtime = testhelp::runtimeDir();
        QVERIFY(data.isValid() && runtime->isValid());
        const QString socketPath = runtime->filePath(QStringLiteral("kern.sock"));
        const QString configHome = twitchConfigHome();
        const QString settingsDir = configHome + QStringLiteral("/dons-streaming-studio-entwicklung");
        const QString channelFile = settingsDir + QStringLiteral("/twitch-token.cred");
        const QString botFile = settingsDir + QStringLiteral("/twitch-bot-token.cred");
        const QString secretFile = settingsDir + QStringLiteral("/twitch-client-secret.cred");
        QString log;
        QByteArray traffic;
        QByteArray received;

        {
            Core core(data.path(), socketPath, configHome, args);
            QVERIFY2(core.waitForLog(QStringLiteral("Twitch: eingeschaltet")), qPrintable(core.log()));
            Window window;
            QVERIFY(QTest::qWaitFor([&] { return window.open(socketPath); }, kWait));

            // Eingeschaltet, nicht angemeldet: grau, Hinweis
            QTRY_VERIFY_WITH_TIMEOUT(window.status().has_value(), kWait);
            QVERIFY(window.status()->enabled);
            QCOMPARE(window.status()->channel.state, QStringLiteral("off"));
            QVERIFY2(window.status()->message.contains(QStringLiteral("nicht angemeldet")),
                     qPrintable(window.status()->message));
            QCOMPARE(window.twitchConnection(), QStringLiteral("off"));

            // Client Secret: verschlüsselt neben kern.ini, nur «gesetzt» geht hinaus
            SettingsUpdate update;
            update.requestId = 1;
            update.twitchClientSecret = server.clientSecret;
            window.send(settingsUpdateMessage(update));
            QTRY_VERIFY_WITH_TIMEOUT(window.result(1).has_value(), kWait);
            QVERIFY2(window.result(1)->ok, qPrintable(window.result(1)->message));
            QTRY_VERIFY(window.latest(msg::kSettings).value(QStringLiteral("twitch_client_secret_set")).toBool());
            QFile secret(secretFile);
            QVERIFY(secret.open(QIODevice::ReadOnly));
            QVERIFY(!secret.readAll().contains(server.clientSecret.toUtf8()));

            window.send(subscribeEventsMessage({{QStringLiteral("twitch.")}, false}));

            // Kanal anmelden: Code im Fenster, Bestätigung im «Browser», danach verbunden
            window.send(twitchAccountRequestMessage(msg::kTwitchLoginStart, {2, QStringLiteral("channel")}));
            QTRY_VERIFY_WITH_TIMEOUT(window.result(2).has_value() && window.result(2)->ok, kWait);
            QTRY_VERIFY_WITH_TIMEOUT(window.status()->channel.loginPending.has_value(), kWait);
            QVERIFY(window.status()->channel.loginPending->userCode.startsWith(QStringLiteral("NUTZER")));
            QVERIFY(server.approveLatest(kChannelUser));
            QTRY_COMPARE_WITH_TIMEOUT(window.status()->channel.state, QStringLiteral("ok"), kWait);
            QCOMPARE(window.status()->channel.login, kChannelUser.login);
            QCOMPARE(window.status()->channel.userId, kChannelUser.id);
            QVERIFY(window.status()->channel.missingScopes.isEmpty());
            QVERIFY(!window.status()->channel.loginPending.has_value());
            QTRY_COMPARE_WITH_TIMEOUT(window.twitchConnection(), QStringLiteral("connected"), kWait);
            QVERIFY(!window.status()->subscriptions.isEmpty());
            QVERIFY(QFile::exists(channelFile));

            // Bot anmelden; danach ist das Abzeichen aktiv (App-Token, user:bot, channel:bot)
            QVERIFY(!window.status()->badgeProblem.isEmpty());
            window.send(twitchAccountRequestMessage(msg::kTwitchLoginStart, {3, QStringLiteral("bot")}));
            QTRY_VERIFY_WITH_TIMEOUT(window.status()->bot.loginPending.has_value(), kWait);
            QVERIFY(server.approveLatest(kBotUser));
            QTRY_COMPARE_WITH_TIMEOUT(window.status()->bot.state, QStringLiteral("ok"), kWait);
            QCOMPARE(window.status()->bot.login, kBotUser.login);
            QTRY_COMPARE_WITH_TIMEOUT(window.status()->badgeProblem, QString(), kWait);
            QVERIFY(server.count(QStringLiteral("/oauth2/token"), QStringLiteral("client_credentials")) >= 1);
            QVERIFY(QFile::exists(botFile));

            // Ereignisse bis zum Fenster
            QVERIFY(server.latestSession());
            server.sendNotification(server.latestSession(), testFile(QStringLiteral("chat_message.json")),
                                    QStringLiteral("kern-chat-1"));
            QTRY_VERIFY_WITH_TIMEOUT(window.eventTypes().contains(QStringLiteral("twitch.chat_message")), kWait);
            QTRY_VERIFY_WITH_TIMEOUT(window.eventTypes().contains(QStringLiteral("twitch.first_message")), kWait);

            traffic += window.traffic();
            received += window.received();
            window.close();
            QVERIFY2(core.stop(), qPrintable(core.log()));
            log += core.log();
        }

        // Neustart: beide Anmeldungen bleiben
        {
            Core core(data.path(), socketPath, configHome, args);
            Window window;
            QVERIFY(QTest::qWaitFor([&] { return window.open(socketPath); }, kWait));
            QTRY_VERIFY_WITH_TIMEOUT(window.status() && window.status()->channel.state == QLatin1String("ok")
                                         && window.status()->bot.state == QLatin1String("ok"),
                                     kWait);
            QCOMPARE(window.status()->channel.login, kChannelUser.login);
            QCOMPARE(window.status()->bot.login, kBotUser.login);
            QTRY_COMPARE_WITH_TIMEOUT(window.twitchConnection(), QStringLiteral("connected"), kWait);

            // Abmelden löscht die Datei des Kontos
            window.send(twitchAccountRequestMessage(msg::kTwitchLogout, {10, QStringLiteral("bot")}));
            QTRY_COMPARE_WITH_TIMEOUT(window.status()->bot.state, QStringLiteral("off"), kWait);
            QVERIFY(!QFile::exists(botFile));
            QVERIFY(QFile::exists(channelFile));
            QCOMPARE(window.twitchConnection(), QStringLiteral("connected"));
            window.send(twitchAccountRequestMessage(msg::kTwitchLogout, {11, QStringLiteral("channel")}));
            QTRY_COMPARE_WITH_TIMEOUT(window.status()->channel.state, QStringLiteral("off"), kWait);
            QVERIFY(!QFile::exists(channelFile));
            QTRY_COMPARE_WITH_TIMEOUT(window.twitchConnection(), QStringLiteral("off"), kWait);
            QVERIFY(server.count(QStringLiteral("/oauth2/revoke")) >= 2);

            // Unbekanntes Konto wird abgewiesen
            QJsonObject wrong = twitchAccountRequestMessage(msg::kTwitchLoginStart, {12, QStringLiteral("x")});
            window.send(wrong);
            QTRY_VERIFY_WITH_TIMEOUT(window.result(12).has_value(), kWait);
            QVERIFY(!window.result(12)->ok);

            traffic += window.traffic();
            received += window.received();
            window.close();
            QVERIFY2(core.stop(), qPrintable(core.log()));
            log += core.log();
        }

        // Kein Geheimnis in Protokoll oder Socket. user_code darf nur ans Fenster (Plan 2.1).
        const QStringList secrets = server.secrets();
        QVERIFY(secrets.size() >= 6);
        for (const QString &s : secrets) {
            QVERIFY2(!log.contains(s), qPrintable(QStringLiteral("im Protokoll: %1").arg(s.left(8))));
            if (s.startsWith(QLatin1String("NUTZER")))
                continue;
            if (s == server.clientSecret) // vom Fenster an den Kern geschickt, nie zurück
                QVERIFY2(!received.contains(s.toUtf8()), "Secret vom Kern geschickt");
            else
                QVERIFY2(!traffic.contains(s.toUtf8()), qPrintable(QStringLiteral("im Socket: %1").arg(s.left(8))));
        }
    }

    // Plan 2.6: nur http://127.0.0.1:PORT, sonst Abbruch mit Exit-Code 1.
    void foreignTestServerRejected_data()
    {
        QTest::addColumn<QString>("address");
        QTest::newRow("fremder Rechner") << QStringLiteral("http://192.0.2.1:8000");
        QTest::newRow("Name") << QStringLiteral("http://localhost:8000");
        QTest::newRow("https") << QStringLiteral("https://127.0.0.1:8000");
        QTest::newRow("ohne Port") << QStringLiteral("http://127.0.0.1");
        QTest::newRow("mit Pfad") << QStringLiteral("http://127.0.0.1:8000/x");
        QTest::newRow("Benutzer") << QStringLiteral("http://a@127.0.0.1:8000");
    }
    void foreignTestServerRejected()
    {
        QFETCH(QString, address);
        QTemporaryDir data;
        const auto runtime = testhelp::runtimeDir();
        std::unique_ptr<QProcess> core = testhelp::startCore(
            data.path(), runtime->filePath(QStringLiteral("kern.sock")), testhelp::testConfigHome(),
            {QStringLiteral("--twitch-testserver"), address});
        QVERIFY(core->waitForFinished(10000));
        const QString log = QString::fromUtf8(core->readAllStandardError());
        QCOMPARE(core->exitStatus(), QProcess::NormalExit);
        QCOMPARE(core->exitCode(), 1);
        QVERIFY2(log.contains(QStringLiteral("--twitch-testserver")), qPrintable(log));
        QVERIFY(!QFileInfo::exists(runtime->filePath(QStringLiteral("kern.sock"))));
    }
};

QTEST_GUILESS_MAIN(TestCoreTwitch)
#include "tst_kerntwitch.moc"
