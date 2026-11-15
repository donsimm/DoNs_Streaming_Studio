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

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUuid>

#include <unistd.h>

#include <algorithm>
#include <functional>

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
    // Wird für jede eingelesene Nachricht gleich beim Lesen aufgerufen (Lastprobe: Antwortzeit).
    std::function<void(const QJsonObject &)> onMessage;

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
            if (decodeMessage(line, &message, &error)) {
                m_messages.append(message);
                if (onMessage)
                    onMessage(message);
            }
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
    qint64 pid() const { return m_process->processId(); }

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

// Verbrauchte CPU-Zeit eines Prozesses in Ticks (utime + stime, Felder 14 und 15 von
// /proc/<pid>/stat, man 5 proc); -1 bei Fehler.
qint64 cpuTicks(qint64 pid)
{
    QFile f(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!f.open(QIODevice::ReadOnly))
        return -1;
    const QByteArray stat = f.readAll();
    const qsizetype end = stat.lastIndexOf(')');
    if (end < 0)
        return -1;
    // Nach dem Namen beginnt Feld 3 (state); utime ist Feld 14, stime Feld 15.
    const QList<QByteArray> fields = stat.mid(end + 2).split(' ');
    if (fields.size() < 13)
        return -1;
    return fields[11].toLongLong() + fields[12].toLongLong();
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

    // Lastprobe (Plan P13, Prüfstufe «Baustein»): Der Testserver schickt 60 s lang 50 Chat-Nachrichten
    // pro Sekunde, ein Fenster hat bestellt. Ziele: alle Ereignisse in Reihenfolge beim Fenster,
    // Antwort auf get_overlay_list höchstens 25 ms; CPU des Kerns je Sekunde als Messwert. Nur mit
    // DSS_LASTPROBE=1, sonst sofort bestanden (ohne QSKIP: ctest verlangt 0 übersprungen, bleibt kurz).
    // Aufruf: ctest --test-dir build -C Lastprobe -R '^tst_kerntwitch_last$' -V
    void chatLoad()
    {
        if (qEnvironmentVariable("DSS_LASTPROBE") != QLatin1String("1"))
            return;
        constexpr int kRate = 50;
        constexpr int kSeconds = 60;
        constexpr int kTotal = kRate * kSeconds;
        constexpr qint64 kMaxAnswerMs = 25;

        TwitchTestServer server;
        server.deviceInterval = 1;
        QVERIFY(server.start());
        QTemporaryDir data;
        const auto runtime = testhelp::runtimeDir();
        QVERIFY(data.isValid() && runtime->isValid());
        const QString socketPath = runtime->filePath(QStringLiteral("kern.sock"));
        Core core(data.path(), socketPath, twitchConfigHome(),
                  {QStringLiteral("--twitch-testserver"), server.baseUrl().toString()});
        QVERIFY2(core.waitForLog(QStringLiteral("Twitch: eingeschaltet")), qPrintable(core.log()));
        Window window;
        QVERIFY(QTest::qWaitFor([&] { return window.open(socketPath); }, kWait));

        // Nur der Kanal: channel.chat.message liest als Kanal (eventsubprotocol.cpp)
        window.send(twitchAccountRequestMessage(msg::kTwitchLoginStart, {1, QStringLiteral("channel")}));
        QTRY_VERIFY_WITH_TIMEOUT(window.status() && window.status()->channel.loginPending.has_value(), kWait);
        QVERIFY(server.approveLatest(kChannelUser));
        QTRY_COMPARE_WITH_TIMEOUT(window.twitchConnection(), QStringLiteral("connected"), kWait);
        QVERIFY(server.latestSession());
        window.send(subscribeEventsMessage({{QStringLiteral("twitch.chat_message")}, false}));

        // Ankunft der Ereignisse und Antworten gleich beim Lesen festhalten
        QElapsedTimer clock;
        clock.start();
        QStringList arrived;
        qint64 dropped = 0;
        qint64 lastEventMs = 0;
        qint64 askedAt = -1;
        QList<qint64> answers;
        window.onMessage = [&](const QJsonObject &m) {
            const QString type = messageType(m);
            if (type == QLatin1String(msg::kEvent)) {
                const QJsonObject event = m.value(QStringLiteral("event")).toObject();
                if (event.value(QStringLiteral("type")).toString() == QLatin1String("twitch.chat_message")) {
                    arrived << event.value(QStringLiteral("origin_id")).toString();
                    lastEventMs = clock.elapsed();
                }
            } else if (type == QLatin1String(msg::kEventsDropped)) {
                qint64 n = 0;
                QString error;
                if (parseEventsDropped(m, &n, &error))
                    dropped += n;
            } else if (type == QLatin1String(msg::kOverlayList) && askedAt >= 0) {
                answers << clock.elapsed() - askedAt;
                askedAt = -1;
            }
        };

        const QJsonObject chat = testFile(QStringLiteral("chat_message.json"));
        QVERIFY(!chat.isEmpty());
        auto id = [](int i) { return QStringLiteral("last-%1").arg(i, 6, 10, QLatin1Char('0')); };
        int sent = 0;
        QTimer sender;
        sender.setTimerType(Qt::PreciseTimer);
        QObject::connect(&sender, &QTimer::timeout, [&] {
            // Nachholen, falls ein Takt zu spät kommt: Soll ist kRate je Sekunde ab Beginn
            const qint64 due = std::min<qint64>(kTotal, clock.elapsed() * kRate / 1000);
            while (sent < due) {
                server.sendNotification(server.latestSession(), chat, id(sent));
                ++sent;
            }
        });
        QTimer asker;
        QObject::connect(&asker, &QTimer::timeout, [&] {
            if (askedAt >= 0)
                return; // vorige Antwort fehlt noch; zählt unten über die Anzahl
            askedAt = clock.elapsed();
            window.send(simpleMessage(msg::kGetOverlayList));
        });
        QList<double> cpu; // % eines Kerns je Sekunde
        const double ticksPerSecond = double(sysconf(_SC_CLK_TCK));
        qint64 lastTicks = cpuTicks(core.pid());
        qint64 lastCpuMs = 0;
        QVERIFY(lastTicks >= 0);
        QTimer sampler;
        sampler.setTimerType(Qt::PreciseTimer);
        QObject::connect(&sampler, &QTimer::timeout, [&] {
            const qint64 ticks = cpuTicks(core.pid());
            const qint64 now = clock.elapsed();
            if (ticks >= 0 && now > lastCpuMs)
                cpu << 100.0 * double(ticks - lastTicks) / ticksPerSecond / (double(now - lastCpuMs) / 1000.0);
            lastTicks = ticks;
            lastCpuMs = now;
        });

        clock.restart();
        sender.start(20);
        asker.start(200);
        sampler.start(1000);
        QTRY_VERIFY_WITH_TIMEOUT(sent == kTotal, (kSeconds + 5) * 1000);
        const qint64 sendMs = clock.elapsed();
        QTRY_VERIFY_WITH_TIMEOUT(arrived.size() >= kTotal, kWait);
        sender.stop();
        asker.stop();
        sampler.stop();
        QTest::qWait(300);

        // Reihenfolge und Vollständigkeit
        QCOMPARE(dropped, 0);
        QCOMPARE(arrived.size(), kTotal);
        for (int i = 0; i < kTotal; ++i)
            QVERIFY2(arrived[i] == id(i), qPrintable(QStringLiteral("Stelle %1: %2").arg(i).arg(arrived[i])));

        window.onMessage = nullptr;
        std::sort(answers.begin(), answers.end());
        QVERIFY2(answers.size() >= kSeconds * 4, qPrintable(QStringLiteral("nur %1 Antworten").arg(answers.size())));
        QVERIFY2(cpu.size() >= kSeconds - 2, qPrintable(QStringLiteral("nur %1 CPU-Werte").arg(cpu.size())));
        QList<double> cpuSorted = cpu;
        std::sort(cpuSorted.begin(), cpuSorted.end());
        double cpuSum = 0;
        for (double v : cpu)
            cpuSum += v;
        auto pct = [](const auto &list, double p) { return list.at(std::min<qsizetype>(list.size() - 1, qsizetype(p * list.size()))); };
        qInfo("Lastprobe: %d Nachrichten in %lld ms gesendet, %lld angekommen, letzte nach %lld ms, 0 verworfen",
              kTotal, static_cast<long long>(sendMs), static_cast<long long>(arrived.size()),
              static_cast<long long>(lastEventMs));
        qInfo("get_overlay_list: %lld Antworten, min %lld / Median %lld / p95 %lld / max %lld ms",
              static_cast<long long>(answers.size()), static_cast<long long>(answers.first()),
              static_cast<long long>(pct(answers, 0.5)), static_cast<long long>(pct(answers, 0.95)),
              static_cast<long long>(answers.last()));
        qInfo("CPU dss-core (%% eines Kerns, %lld Werte je 1 s): Mittel %.2f / Median %.1f / p95 %.1f / max %.1f",
              static_cast<long long>(cpu.size()), cpu.isEmpty() ? 0.0 : cpuSum / double(cpu.size()),
              pct(cpuSorted, 0.5), pct(cpuSorted, 0.95), cpuSorted.last());
        QStringList perSecond;
        for (double v : cpu)
            perSecond << QString::number(v, 'f', 1);
        qInfo("CPU je Sekunde: %s", qPrintable(perSecond.join(QLatin1Char(' '))));
        QVERIFY2(answers.last() <= kMaxAnswerMs, qPrintable(QStringLiteral("Antwort nach %1 ms").arg(answers.last())));

        window.close();
        QVERIFY2(core.stop(), qPrintable(core.log()));
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
