// Tests der Anbindung an Twitch im Kern (P13, Commit 7): dss-core als eigener Prozess gegen den
// Testserver tests/twitchtestserver.h (--twitch-testserver), ein Fenster als roher Socket.
// - Client Secret über update_settings, Anmeldung beider Konten über den Socket, Abzeichen aktiv,
//   Ereignisse bis zum Fenster (twitch.chat_message und twitch.first_message);
// - Neustart behält beide Anmeldungen, Abmelden löscht die Datei des Kontos;
// - kein Testtoken, kein device_code und kein Secret in Protokoll oder Socket (user_code nur im Socket);
// - --twitch-testserver mit anderer Adresse als http://127.0.0.1:PORT bricht mit Exit-Code 1 ab.
// - P14 c1a: Chat senden aus einer Aktion (Ersatz Kanal, Bot, Antwort), Konten in actions_status,
//   eigene Nachrichten lösen keinen Befehl aus, kein Testtoken und kein Chat-Text im Protokoll.
//   Die Chat-Nachrichten kommen über test_event (dss-core --testereignisse).
// - P14 c1b: Benutzerinfo, Shoutout und Ansage aus einer Aktion, zuschauer.json nachgeführt.
// Eigener Datenordner, eigener Ordner der Einstellungen; die Geheimnisse legt systemd-creds --user ab
// (wie tst_kernverbindung).

#include "actionjson.h"
#include "actionprotocol.h"
#include "configpaths.h"
#include "coresettings.h"
#include "steptypes.h"
#include "triggertypes.h"
#include "prozesshilfe.h"
#include "protocol.h"
#include "twitchtestserver.h"
#include "version.h"
#include "viewerdata.h"

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
    // data des neuesten Ereignisses dieses Typs; leer, wenn keines
    QJsonObject latestEventData(const QString &type) const
    {
        for (qsizetype i = m_messages.size() - 1; i >= 0; --i) {
            const QJsonObject event = m_messages[i].value(QStringLiteral("event")).toObject();
            if (messageType(m_messages[i]) == QLatin1String(msg::kEvent) && event.value(QStringLiteral("type")).toString() == type)
                return event.value(QStringLiteral("data")).toObject();
        }
        return {};
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

// twitch.chat_message mit allen Feldern der Typenliste (eventtypes.h), als test_event
QJsonObject chatEvent(const QString &messageId, const TwitchTestUser &user, const QString &text, bool broadcaster)
{
    const QJsonObject data{{QStringLiteral("message_id"), messageId},
                           {QStringLiteral("user_id"), user.id},
                           {QStringLiteral("user_login"), user.login},
                           {QStringLiteral("user_name"), user.login},
                           {QStringLiteral("text"), text},
                           {QStringLiteral("fragments"), QJsonArray()},
                           {QStringLiteral("color"), QString()},
                           {QStringLiteral("badges"), QJsonArray()},
                           {QStringLiteral("role"), broadcaster ? 4 : 1},
                           {QStringLiteral("is_subscriber"), false},
                           {QStringLiteral("is_vip"), false},
                           {QStringLiteral("is_moderator"), false},
                           {QStringLiteral("is_broadcaster"), broadcaster},
                           {QStringLiteral("is_bot_account"), false},
                           {QStringLiteral("bits"), 0},
                           {QStringLiteral("reply_to"), QJsonValue::Null},
                           {QStringLiteral("reward_id"), QJsonValue::Null},
                           {QStringLiteral("message_type"), QStringLiteral("text")},
                           {QStringLiteral("is_highlight"), false},
                           {QStringLiteral("source_broadcaster_id"), QJsonValue::Null}};
    return {{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {QStringLiteral("type"), QStringLiteral("twitch.chat_message")},
            {QStringLiteral("origin"), QStringLiteral("twitch")},
            {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
            {QStringLiteral("origin_id"), QString()},
            {QStringLiteral("data"), data}};
}

// Konto → Grund aus dem neuesten actions_status (leer = kann senden); leer ohne Status
QHash<QString, QString> accounts(const Window &window)
{
    QHash<QString, QString> result;
    ActionsStatus status;
    QString error;
    if (!parseActionsStatus(window.latest(msg::kActionsStatus), &status, &error))
        return result;
    for (const ActionsAccountStatus &a : status.accounts)
        result.insert(a.account, a.canSend ? QString() : a.message);
    return result;
}

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

    // P14 c1a (Plan 4.1): Befehl «!dsstest» mit zwei Schritten «Chat senden»: als Bot mit Ersatz und
    // Antwort, danach als Kanal. Die Antwort beginnt selbst mit «!dsstest» (Schleife, wenn das Echo
    // nicht als «intern» erkannt würde).
    void chatFromAction()
    {
#if !DSS_CORE_ACTIONS
        QSKIP("Aktionssystem nicht eingebaut (DSS_CORE_ACTIONS aus)");
#else
        using namespace dss::actions;
        TwitchTestServer server;
        server.deviceInterval = 1;
        QVERIFY(server.start());
        QTemporaryDir data;
        const auto runtime = testhelp::runtimeDir();
        QVERIFY(data.isValid() && runtime->isValid());
        const QString socketPath = runtime->filePath(QStringLiteral("kern.sock"));
        // Version im Einsatz: Verbindung ohne Kern, sonst wäre dieser Kern stumm
        const QStringList args{QStringLiteral("--twitch-testserver"), server.baseUrl().toString(),
                               QStringLiteral("--testereignisse"), QStringLiteral("--einsatz-verbindung"),
                               runtime->filePath(QStringLiteral("einsatz.sock"))};
        const TwitchTestUser viewer{QStringLiteral("900000001"), QStringLiteral("zuschauer_geheim_4711")};
        const QString replyText = QStringLiteral("!dsstest Antwort-Geheimtext-5521");
        Core core(data.path(), socketPath, twitchConfigHome(), args);
        QVERIFY2(core.waitForLog(QStringLiteral("Twitch: eingeschaltet")), qPrintable(core.log()));
        Window window;
        QVERIFY(QTest::qWaitFor([&] { return window.open(socketPath); }, kWait));
        window.send(subscribeEventsMessage({{QStringLiteral("twitch."), QStringLiteral("actions.")}, false}));
        const auto count = [&](const QString &type) { return window.eventTypes().count(type); };
        const auto login = [&](qint64 id, const QString &account, const TwitchTestUser &user) {
            window.send(twitchAccountRequestMessage(msg::kTwitchLoginStart, {id, account}));
            return QTest::qWaitFor([&] { return server.approveLatest(user); }, kWait);
        };
        const auto request = [&](qint64 id, const char *type, const QJsonObject &fields) {
            window.send(actionsRequestMessage({id, QString::fromLatin1(type), fields}));
            return QTest::qWaitFor([&] { return window.result(id).has_value(); }, kWait) && window.result(id)->ok;
        };

        // Ohne Anmeldung kann kein Konto senden
        QTRY_COMPARE_WITH_TIMEOUT(accounts(window).value(QStringLiteral("bot")), QStringLiteral("Kanal nicht angemeldet"), kWait);
        QVERIFY(login(2, QStringLiteral("channel"), kChannelUser));
        QTRY_COMPARE_WITH_TIMEOUT(accounts(window).value(QStringLiteral("broadcaster"), QStringLiteral("x")), QString(), kWait);
        QCOMPARE(accounts(window).value(QStringLiteral("bot")), QStringLiteral("Bot nicht angemeldet"));

        Command command;
        command.id = QUuid::createUuid();
        command.name = QStringLiteral("DSS-Test");
        command.commands = {QStringLiteral("!dsstest")};
        Action action;
        action.id = QUuid::createUuid();
        action.name = QStringLiteral("DSS-Test Chat");
        Trigger trigger = newTrigger(QString::fromLatin1(kTriggerCommand));
        trigger.fields.insert(QStringLiteral("command_id"), command.id.toString(QUuid::WithoutBraces));
        action.triggers.append(trigger);
        Step asBot = newStep(QString::fromLatin1(kStepChatSend));
        asBot.fields.insert(QStringLiteral("text"), replyText + QStringLiteral(" %user%"));
        asBot.fields.insert(QStringLiteral("reply"), true);
        action.steps.append(asBot);
        Step asChannel = newStep(QString::fromLatin1(kStepChatSend));
        asChannel.fields.insert(QStringLiteral("text"), replyText);
        asChannel.fields.insert(QStringLiteral("use_bot"), false);
        action.steps.append(asChannel);
        QVERIFY(request(10, msg::kCommandPut, {{QStringLiteral("command"), commandToJson(command)}}));
        QVERIFY(request(11, msg::kActionPut, {{QStringLiteral("action"), actionToJson(action)}}));
        QVERIFY(request(12, msg::kActionsSetEnabled, {{QStringLiteral("enabled"), true}}));

        // 1. Bot nicht angemeldet: der Kanal sendet beide (Ersatz), die erste als Antwort
        QVERIFY(request(13, msg::kTestEvent, {{QStringLiteral("event"), chatEvent(QStringLiteral("eins"), viewer, QStringLiteral("!dsstest eins"), false)}}));
        QTRY_COMPARE_WITH_TIMEOUT(server.chatMessages.size(), 2, kWait);
        QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("twitch.chat_sent")), 2, kWait);
        const QJsonObject first = server.chatMessages[0].body;
        QCOMPARE(first.value(QStringLiteral("broadcaster_id")).toString(), kChannelUser.id);
        QCOMPARE(first.value(QStringLiteral("sender_id")).toString(), kChannelUser.id);
        QCOMPARE(first.value(QStringLiteral("message")).toString(), replyText + QLatin1Char(' ') + viewer.login);
        QCOMPARE(first.value(QStringLiteral("reply_parent_message_id")).toString(), QStringLiteral("eins"));
        QCOMPARE(server.chatMessages[1].body.value(QStringLiteral("sender_id")).toString(), kChannelUser.id);
        QVERIFY(!server.chatMessages[1].body.contains(QStringLiteral("reply_parent_message_id")));

        // 2. Bot angemeldet: der Bot sendet die erste (ohne Secret mit seinem Token, ohne Abzeichen)
        QVERIFY(login(3, QStringLiteral("bot"), kBotUser));
        QTRY_COMPARE_WITH_TIMEOUT(accounts(window).value(QStringLiteral("bot"), QStringLiteral("x")), QString(), kWait);
        QVERIFY(request(14, msg::kTestEvent, {{QStringLiteral("event"), chatEvent(QStringLiteral("zwei"), viewer, QStringLiteral("!dsstest zwei"), false)}}));
        QTRY_COMPARE_WITH_TIMEOUT(server.chatMessages.size(), 4, kWait);
        QCOMPARE(server.chatMessages[2].body.value(QStringLiteral("sender_id")).toString(), kBotUser.id);
        QVERIFY(!server.chatMessages[2].appToken);
        QCOMPARE(server.chatMessages[3].body.value(QStringLiteral("sender_id")).toString(), kChannelUser.id);
        QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("twitch.chat_sent")), 4, kWait);

        // 3. Echo der eigenen Nachrichten löst den Befehl nicht aus: message_id aus der Antwort, und
        //    Absender mit gleichem Text innerhalb von 10 s. Danach ein Zuschauer: genau ein neuer Lauf.
        const int runs = count(QStringLiteral("actions.run_started"));
        QCOMPARE(runs, 2);
        QVERIFY(request(15, msg::kTestEvent, {{QStringLiteral("event"), chatEvent(server.chatMessages[3].messageId, kChannelUser,
                                                                                    QStringLiteral("!dsstest anders"), true)}}));
        QVERIFY(request(16, msg::kTestEvent, {{QStringLiteral("event"), chatEvent(QStringLiteral("echo-ohne-id"), kChannelUser,
                                                                                    replyText, true)}}));
        QVERIFY(request(17, msg::kTestEvent, {{QStringLiteral("event"), chatEvent(QStringLiteral("drei"), viewer, QStringLiteral("!dsstest drei"), false)}}));
        QTRY_COMPARE_WITH_TIMEOUT(server.chatMessages.size(), 6, kWait);
        QCOMPARE(count(QStringLiteral("actions.run_started")), runs + 1);
        QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("actions.run_finished")), 3, kWait);

        const QByteArray traffic = window.traffic();
        window.close();
        QVERIFY2(core.stop(), qPrintable(core.log()));

        // Kein Testtoken im Protokoll oder an das Fenster; keine Chat-Texte und Zuschauernamen im
        // Protokoll (Plan 3.5)
        const QStringList secrets = server.secrets();
        QVERIFY(secrets.size() >= 4);
        for (const QString &s : secrets) {
            QVERIFY2(!core.log().contains(s), qPrintable(QStringLiteral("im Protokoll: %1").arg(s.left(8))));
            if (!s.startsWith(QLatin1String("NUTZER")))
                QVERIFY2(!traffic.contains(s.toUtf8()), qPrintable(QStringLiteral("im Socket: %1").arg(s.left(8))));
        }
        for (const QString &text : {QStringLiteral("Geheimtext"), viewer.login})
            QVERIFY2(!core.log().contains(text), qPrintable(text));
        QVERIFY2(core.log().contains(QStringLiteral("Lauf «DSS-Test Chat»: ok")), qPrintable(core.log()));
#endif
    }

    // P14 c1b (Plan 4.8 und 4.9): Befehl «!dssso» mit Benutzerinfo (mit «@», gross geschrieben),
    // Chat mit allen Platzhaltern, Shoutout abgelehnt (nicht live) und angenommen, Ansage als Bot und
    // als Kanal (2 s Abstand), Ansage als Bot ohne Moderator (Kanal sendet), unbekannte Farbe,
    // unbekannter Name. zuschauer.json nachgeführt; keine Namen, Texte und Tokens im Protokoll.
    void userInfoShoutoutAnnounce()
    {
#if !DSS_CORE_ACTIONS
        QSKIP("Aktionssystem nicht eingebaut (DSS_CORE_ACTIONS aus)");
#else
        using namespace dss::actions;
        TwitchTestServer server;
        server.deviceInterval = 1;
        QVERIFY(server.start());
        const TwitchTestUser viewer{QStringLiteral("900000001"), QStringLiteral("zuschauer_geheim_4711")};
        server.addUser(viewer.id, viewer.login, QStringLiteral("Zuschauer_Geheim_4711"), QStringLiteral("Just Chatting"));
        QTemporaryDir data;
        const auto runtime = testhelp::runtimeDir();
        QVERIFY(data.isValid() && runtime->isValid());
        const QString socketPath = runtime->filePath(QStringLiteral("kern.sock"));
        const QStringList args{QStringLiteral("--twitch-testserver"), server.baseUrl().toString(),
                               QStringLiteral("--testereignisse"), QStringLiteral("--einsatz-verbindung"),
                               runtime->filePath(QStringLiteral("einsatz.sock"))};
        Core core(data.path(), socketPath, twitchConfigHome(), args);
        QVERIFY2(core.waitForLog(QStringLiteral("Twitch: eingeschaltet")), qPrintable(core.log()));
        Window window;
        QVERIFY(QTest::qWaitFor([&] { return window.open(socketPath); }, kWait));
        window.send(subscribeEventsMessage({{QStringLiteral("actions.")}, false}));
        const auto login = [&](qint64 id, const QString &account, const TwitchTestUser &user) {
            window.send(twitchAccountRequestMessage(msg::kTwitchLoginStart, {id, account}));
            return QTest::qWaitFor([&] { return server.approveLatest(user); }, kWait);
        };
        const auto request = [&](qint64 id, const char *type, const QJsonObject &fields) {
            window.send(actionsRequestMessage({id, QString::fromLatin1(type), fields}));
            return QTest::qWaitFor([&] { return window.result(id).has_value(); }, kWait) && window.result(id)->ok;
        };
        QVERIFY(login(2, QStringLiteral("channel"), kChannelUser));
        QVERIFY(login(3, QStringLiteral("bot"), kBotUser));
        QTRY_COMPARE_WITH_TIMEOUT(accounts(window).value(QStringLiteral("bot"), QStringLiteral("x")), QString(), kWait);

        Command command;
        command.id = QUuid::createUuid();
        command.name = QStringLiteral("DSS-Test");
        command.commands = {QStringLiteral("!dssso")};
        Action action;
        action.id = QUuid::createUuid();
        action.name = QStringLiteral("DSS-Test Shoutout");
        Trigger trigger = newTrigger(QString::fromLatin1(kTriggerCommand));
        trigger.fields.insert(QStringLiteral("command_id"), command.id.toString(QUuid::WithoutBraces));
        action.triggers.append(trigger);
        const auto step = [&](const char *type, const QJsonObject &fields) {
            Step s = newStep(QString::fromLatin1(type));
            for (auto it = fields.begin(); it != fields.end(); ++it)
                s.fields.insert(it.key(), it.value());
            action.steps.append(s);
        };
        step(kStepUserInfo, {{QStringLiteral("login"), QStringLiteral("@Zuschauer_Geheim_4711")}});
        step(kStepChatSend, {{QStringLiteral("text"), QStringLiteral("%targetUser%|%targetUserName%|%targetUserId%|"
                                                                     "%targetUserProfileImageUrl%|%targetDescription%|%game%|%targetUserFound%")},
                             {QStringLiteral("use_bot"), false}});
        step(kStepTwitchShoutout, {{QStringLiteral("login"), QStringLiteral("%targetUserName%")}}); // abgelehnt
        step(kStepTwitchShoutout, {{QStringLiteral("login"), QStringLiteral("@%targetUserName%")}});
        step(kStepTwitchAnnounce, {{QStringLiteral("text"), QStringLiteral("Ansage-Geheimtext eins")}, {QStringLiteral("color"), QStringLiteral("Blue")}});
        step(kStepTwitchAnnounce, {{QStringLiteral("text"), QStringLiteral("Ansage-Geheimtext zwei")}, {QStringLiteral("use_bot"), false},
                                   {QStringLiteral("color"), QString()}});
        step(kStepTwitchAnnounce, {{QStringLiteral("text"), QStringLiteral("Ansage-Geheimtext drei")}, {QStringLiteral("color"), QStringLiteral("rot")}});
        step(kStepUserInfo, {{QStringLiteral("login"), QStringLiteral("niemand")}});
        step(kStepChatSend, {{QStringLiteral("text"), QStringLiteral("gefunden %targetUserFound%")}, {QStringLiteral("use_bot"), false}});
        QVERIFY(request(10, msg::kCommandPut, {{QStringLiteral("command"), commandToJson(command)}}));
        QVERIFY(request(11, msg::kActionPut, {{QStringLiteral("action"), actionToJson(action)}}));
        QVERIFY(request(12, msg::kActionsSetEnabled, {{QStringLiteral("enabled"), true}}));

        server.rejectNextShoutout = QStringLiteral("The broadcaster is not streaming live");
        QVERIFY(request(13, msg::kTestEvent, {{QStringLiteral("event"), chatEvent(QStringLiteral("eins"), viewer, QStringLiteral("!dssso"), false)}}));
        QTRY_COMPARE_WITH_TIMEOUT(server.chatMessages.size(), 2, kWait);
        QTRY_COMPARE_WITH_TIMEOUT(window.eventTypes().count(QStringLiteral("actions.run_finished")), 1, kWait);

        QCOMPARE(server.chatMessages[0].body.value(QStringLiteral("message")).toString(),
                 QStringLiteral("Zuschauer_Geheim_4711|zuschauer_geheim_4711|900000001|https://bild.test/900000001.png|"
                                "Beschreibung von Zuschauer_Geheim_4711|Just Chatting|True"));
        QCOMPARE(server.chatMessages[1].body.value(QStringLiteral("message")).toString(), QStringLiteral("gefunden False"));
        QCOMPARE(server.requestsTo(QStringLiteral("/helix/users")).last().query.queryItemValue(QStringLiteral("login")),
                 QStringLiteral("niemand"));
        // Shoutout: der zweite angenommen, vom Kanal an den Zuschauer
        QCOMPARE(server.shoutouts.size(), 1);
        QCOMPARE(server.shoutouts[0].query.queryItemValue(QStringLiteral("to_broadcaster_id")), viewer.id);
        QCOMPARE(server.shoutouts[0].query.queryItemValue(QStringLiteral("from_broadcaster_id")), kChannelUser.id);
        QCOMPARE(server.shoutouts[0].tokenUserId, kChannelUser.id);
        // Ansagen: als Bot, dann als Kanal, mindestens 2 s dazwischen; unbekannte Farbe geht nicht hinaus
        QCOMPARE(server.announcements.size(), 2);
        QCOMPARE(server.announcements[0].tokenUserId, kBotUser.id);
        QCOMPARE(server.announcements[0].body.value(QStringLiteral("color")).toString(), QStringLiteral("blue"));
        QCOMPARE(server.announcements[1].tokenUserId, kChannelUser.id);
        QVERIFY(!server.announcements[1].body.contains(QStringLiteral("color")));
        const qint64 gap = server.announcements[0].at.msecsTo(server.announcements[1].at);
        QVERIFY2(gap >= 1900, qPrintable(QString::number(gap)));

        // Lauf: Fehler bei Shoutout und Farbe, alle Schritte ausgeführt
        const QJsonObject finished = window.latestEventData(QStringLiteral("actions.run_finished"));
        QCOMPARE(finished.value(QStringLiteral("state")).toString(), QStringLiteral("error"));
        window.send(actionsRequestMessage({20, QString::fromLatin1(msg::kActionRunDetails),
                                           {{QStringLiteral("run_id"), finished.value(QStringLiteral("run_id"))}}}));
        QTRY_VERIFY_WITH_TIMEOUT(!window.latest(msg::kActionRun).isEmpty(), kWait);
        RunRecord run;
        QString error;
        QVERIFY(parseActionRun(window.latest(msg::kActionRun), &run, &error));
        QStringList messages;
        for (const StepLogEntry &e : run.log)
            messages << e.message;
        QCOMPARE(run.log.size(), 9);
        QCOMPARE(messages.at(0), QStringLiteral("Benutzer gefunden"));
        QCOMPARE(run.log.at(2).result, StepLogEntry::Result::Error);
        QCOMPARE(messages.at(2), QStringLiteral("Shoutout abgelehnt: Twitch antwortet mit 400: The broadcaster is not streaming live"));
        QCOMPARE(messages.at(3), QStringLiteral("Shoutout gegeben"));
        QCOMPARE(messages.at(4), QStringLiteral("Ansage gesendet als Bot"));
        QCOMPARE(messages.at(5), QStringLiteral("Ansage gesendet als Kanal"));
        QCOMPARE(run.log.at(6).result, StepLogEntry::Result::Error);
        QVERIFY2(messages.at(6).startsWith(QStringLiteral("Farbe «rot» unbekannt")), qPrintable(messages.at(6)));
        QCOMPARE(messages.at(7), QStringLiteral("Benutzer nicht gefunden"));

        // Bot moderiert nicht (403): der Kanal sendet
        server.nonModerators.insert(kBotUser.id);
        Action announceOnly = action;
        announceOnly.id = QUuid::createUuid();
        announceOnly.name = QStringLiteral("DSS-Test Ansage");
        announceOnly.triggers.clear();
        announceOnly.steps.clear();
        Step botAnnounce = newStep(QString::fromLatin1(kStepTwitchAnnounce));
        botAnnounce.fields.insert(QStringLiteral("text"), QStringLiteral("Ansage-Geheimtext vier"));
        announceOnly.steps.append(botAnnounce);
        QVERIFY(request(21, msg::kActionPut, {{QStringLiteral("action"), actionToJson(announceOnly)}}));
        QVERIFY(request(22, msg::kActionTest, {{QStringLiteral("action_id"), announceOnly.id.toString(QUuid::WithoutBraces)},
                                               {QStringLiteral("arguments"), QJsonObject()},
                                               {QStringLiteral("dry_run"), false}}));
        QTRY_COMPARE_WITH_TIMEOUT(server.announcements.size(), 3, kWait);
        QCOMPARE(server.announcements[2].tokenUserId, kChannelUser.id);

        const QByteArray traffic = window.traffic();
        window.close();
        QVERIFY2(core.stop(), qPrintable(core.log()));

        // zuschauer.json nachgeführt (Login und Anzeigename aus der Benutzerinfo)
        QFile viewersFile(data.filePath(QString::fromLatin1(kViewersFileName)));
        QVERIFY(viewersFile.open(QIODevice::ReadOnly));
        ViewersData viewers;
        QVERIFY2(parseViewers(viewersFile.readAll(), &viewers, &error), qPrintable(error));
        QCOMPARE(viewers.viewers.value(viewer.id).login, viewer.login);
        QCOMPARE(viewers.viewers.value(viewer.id).displayName, QStringLiteral("Zuschauer_Geheim_4711"));

        for (const QString &s : server.secrets()) {
            QVERIFY2(!core.log().contains(s), qPrintable(QStringLiteral("im Protokoll: %1").arg(s.left(8))));
            if (!s.startsWith(QLatin1String("NUTZER")))
                QVERIFY2(!traffic.contains(s.toUtf8()), qPrintable(QStringLiteral("im Socket: %1").arg(s.left(8))));
        }
        for (const QString &text : {QStringLiteral("Geheimtext"), QStringLiteral("eheim_4711")})
            QVERIFY2(!core.log().contains(text), qPrintable(text));
#endif
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
