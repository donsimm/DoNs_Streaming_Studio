// Anmeldung bei Twitch und Helix (P13, Commit 5) gegen den Testserver tests/twitchtestserver.h auf
// 127.0.0.1: Gerätefluss mit authorization_pending, Ablauf des Codes, Abbrechen; Erneuerung mit neuem
// Refresh-Token, 401 → Erneuern → Wiederholen, Refresh abgewiesen; stündliche Prüfung mit gekürzter
// Zeit, fehlende Rechte, Abmelden; zwei Konten nebeneinander, Bot gleich Kanal abgewiesen; Laden beim
// Start, 429, Netzfehler. Kein Token, Gerätecode oder Secret im Protokoll.
//
// Ablage: SecretStore mit einem Ersatz für systemd-creds (base64), damit die Tests schnell und ohne
// Schlüssel des Rechners laufen; systemd-creds selbst prüft tst_obspasswort.

#include "helixclient.h"
#include "secretstore.h"
#include "twitchauth.h"
#include "twitchtestserver.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using dss::SecretStore;
using dss::twitch::Endpoints;
using dss::twitch::HelixClient;
using dss::twitch::TwitchAuth;
using testhelp::TwitchTestServer;
using testhelp::TwitchTestUser;

namespace {

constexpr int kWait = 10000;
const TwitchTestUser kChannelUser{QStringLiteral("100000001"), QStringLiteral("testkanal")};
const TwitchTestUser kBotUser{QStringLiteral("100000002"), QStringLiteral("testbot")};

QStringList g_messages;
QtMessageHandler g_previousHandler = nullptr;

void captureMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    g_messages.append(message);
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

TwitchAuth::Timing fastTiming()
{
    TwitchAuth::Timing timing;
    timing.pollInterval = 50;
    timing.retryMin = 50;
    timing.retryMax = 400;
    timing.requestTimeout = 5000;
    return timing;
}

// Ersatz für systemd-creds: base64 statt Verschlüsselung, Befehlszeile wie SecretStore.
QString fakeCreds(const QString &dir)
{
    const QString path = dir + QStringLiteral("/ersatz-creds.sh");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("#!/bin/sh\nif [ \"$3\" = encrypt ]; then exec base64 -w0; fi\nexec base64 -d \"$5\"\n");
    f.close();
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

QJsonObject storedToken(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(QByteArray::fromBase64(f.readAll())).object();
}

// Ein Konto mit eigener Datei, wie im Kern (twitch-token.cred, twitch-bot-token.cred).
struct Account
{
    Account(TwitchAuth::Account kind, const QString &file, const QString &program, QNetworkAccessManager *network,
            TwitchTestServer *server, const TwitchAuth::Timing &timing = fastTiming())
        : path(file)
        , store(file, QStringLiteral("dss-twitch-test"), {QStringLiteral("Twitch-Token"), QStringLiteral("Anmeldung")},
                program)
        , auth(kind, network, &store, timing)
    {
        auth.setEndpoints(Endpoints::testServer(server->baseUrl()));
        auth.setClientId(server->clientId);
        auth.setRequiredScopes(kind == TwitchAuth::Account::Channel ? dss::twitch::channelScopes()
                                                                    : dss::twitch::botScopes());
    }
    QString path;
    SecretStore store;
    TwitchAuth auth;
};

} // namespace

class TestTwitchLogin : public QObject
{
    Q_OBJECT

    QNetworkAccessManager m_network;
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<TwitchTestServer> m_server;
    QString m_program;
    QStringList m_secrets; // alle ausgegebenen Geheimnisse, für die Suche im Protokoll

    QString file(const QString &name) const { return m_dir->filePath(name); }

    std::unique_ptr<Account> account(TwitchAuth::Account kind, const QString &name,
                                     const TwitchAuth::Timing &timing = fastTiming())
    {
        return std::make_unique<Account>(kind, file(name), m_program, &m_network, m_server.get(), timing);
    }

    bool login(Account &a, const TwitchTestUser &user, const QStringList &granted = {})
    {
        a.auth.startLogin();
        if (!QTest::qWaitFor([&a] { return a.auth.pending().has_value(); }, kWait))
            return false;
        if (!m_server->approveLatest(user, granted))
            return false;
        return QTest::qWaitFor([&a] { return a.auth.state() == TwitchAuth::State::Ok; }, kWait);
    }

private slots:
    void initTestCase()
    {
        g_previousHandler = qInstallMessageHandler(captureMessage);
    }

    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        QVERIFY(m_dir->isValid());
        m_program = fakeCreds(m_dir->path());
        m_server = std::make_unique<TwitchTestServer>();
        QVERIFY(m_server->start());
    }

    void cleanup()
    {
        m_secrets << m_server->secrets();
        m_server.reset();
        m_dir.reset();
    }

    // Zum Schluss: kein Token, Gerätecode, Nutzercode oder Secret in irgendeiner Meldung.
    void cleanupTestCase()
    {
        qInstallMessageHandler(g_previousHandler);
        QVERIFY(m_secrets.size() > 20);
        for (const QString &secret : std::as_const(m_secrets)) {
            for (const QString &message : std::as_const(g_messages))
                QVERIFY2(!message.contains(secret), qPrintable(QStringLiteral("Geheimnis im Protokoll: ") + message.left(40)));
        }
    }

    void scopeLists()
    {
        const QStringList channel = dss::twitch::channelScopes();
        QCOMPARE(channel.size(), 78);
        QCOMPARE(QSet<QString>(channel.begin(), channel.end()).size(), 78);
        for (const char *needed : {"user:read:chat", "user:write:chat", "channel:bot", "moderator:read:followers",
                                   "channel:read:subscriptions", "bits:read", "channel:read:redemptions",
                                   "moderator:manage:shoutouts", "moderator:manage:announcements"})
            QVERIFY2(channel.contains(QLatin1String(needed)), needed);
        QVERIFY(!channel.contains(QStringLiteral("chat:read")));
        QCOMPARE(dss::twitch::botScopes(),
                 (QStringList{"user:write:chat", "user:bot", "moderator:manage:announcements"}));
    }

    void deviceFlowPendingThenApproved()
    {
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        QCOMPARE(a->auth.state(), TwitchAuth::State::Off);
        a->auth.startLogin();
        QTRY_VERIFY_WITH_TIMEOUT(a->auth.pending().has_value(), kWait);
        const TwitchAuth::Pending pending = *a->auth.pending();
        QCOMPARE(pending.userCode, m_server->devices.first().userCode);
        QVERIFY(pending.verificationUri.toString().startsWith(QStringLiteral("https://www.twitch.tv/activate")));
        QVERIFY(pending.expiresAt > QDateTime::currentDateTimeUtc().addSecs(1700));
        // Gerätecode verlangt mit Client-ID und allen Rechten
        const auto device = m_server->requestsTo(QStringLiteral("/oauth2/device"));
        QCOMPARE(device.size(), 1);
        QCOMPARE(device.first().formValue(QStringLiteral("client_id")), QStringLiteral("testclient"));
        QCOMPARE(device.first().formValue(QStringLiteral("scopes")).split(QLatin1Char(' ')), dss::twitch::channelScopes());

        // Noch nicht bestätigt: authorization_pending, weiter fragen
        QTRY_VERIFY_WITH_TIMEOUT(m_server->count(QStringLiteral("/oauth2/token"), TwitchTestServer::kDeviceGrant) >= 3, kWait);
        QCOMPARE(a->auth.state(), TwitchAuth::State::Off);
        QVERIFY(a->auth.pending().has_value());

        QVERIFY(m_server->approveLatest(kChannelUser));
        QTRY_COMPARE_WITH_TIMEOUT(a->auth.state(), TwitchAuth::State::Ok, kWait);
        QVERIFY(!a->auth.pending().has_value());
        QCOMPARE(a->auth.userId(), kChannelUser.id);
        QCOMPARE(a->auth.login(), kChannelUser.login);
        QVERIFY(a->auth.missingScopes().isEmpty());
        QCOMPARE(a->auth.message(), QString());
        QCOMPARE(a->auth.accessToken(), m_server->latestToken(kChannelUser.id)->access);
        // Ohne Secret geht keines mit
        for (const auto &request : m_server->requestsTo(QStringLiteral("/oauth2/token")))
            QVERIFY(request.formValue(QStringLiteral("client_secret")).isEmpty());

        // Abgelegt mit allen Feldern
        QTRY_VERIFY_WITH_TIMEOUT(!storedToken(a->path).isEmpty(), kWait);
        const QJsonObject stored = storedToken(a->path);
        QCOMPARE(stored.value(QStringLiteral("access_token")).toString(), m_server->latestToken(kChannelUser.id)->access);
        QCOMPARE(stored.value(QStringLiteral("refresh_token")).toString(), m_server->latestToken(kChannelUser.id)->refresh);
        QCOMPARE(stored.value(QStringLiteral("user_id")).toString(), kChannelUser.id);
        QCOMPARE(stored.value(QStringLiteral("login")).toString(), kChannelUser.login);
        QCOMPARE(stored.value(QStringLiteral("scopes")).toArray().size(), 78);
        const QDateTime expiresAt = QDateTime::fromString(stored.value(QStringLiteral("expires_at")).toString(), Qt::ISODateWithMs);
        QVERIFY(qAbs(expiresAt.secsTo(QDateTime::currentDateTimeUtc().addSecs(14400))) < 30);
    }

    void secretSentWhenSet()
    {
        m_server->clientSecret = QStringLiteral("geheim-") + QUuid::createUuid().toString(QUuid::Id128);
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        a->auth.setClientSecret(m_server->clientSecret);
        QVERIFY(login(*a, kChannelUser));
        const auto tokenRequests = m_server->requestsTo(QStringLiteral("/oauth2/token"));
        QVERIFY(!tokenRequests.isEmpty());
        for (const auto &request : tokenRequests)
            QCOMPARE(request.formValue(QStringLiteral("client_secret")), m_server->clientSecret);
        // Beim Gerätecode selbst geht es nicht mit (nicht in der Doku)
        QVERIFY(m_server->requestsTo(QStringLiteral("/oauth2/device")).first().formValue(QStringLiteral("client_secret")).isEmpty());

        // Erneuern schickt es ebenfalls
        m_server->invalidateAccess(kChannelUser.id);
        HelixClient helix(&m_network);
        helix.setEndpoints(Endpoints::testServer(m_server->baseUrl()));
        helix.setClientId(m_server->clientId);
        std::optional<HelixClient::Reply> result;
        helix.get(&a->auth, QStringLiteral("/helix/users"), {}, [&result](const HelixClient::Reply &r) { result = r; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), kWait);
        QCOMPARE(result->status, 200);
        const auto refresh = m_server->requestsTo(QStringLiteral("/oauth2/token")).last();
        QCOMPARE(refresh.formValue(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));
        QCOMPARE(refresh.formValue(QStringLiteral("client_secret")), m_server->clientSecret);
    }

    void codeExpiresAtTwitch()
    {
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        a->auth.startLogin();
        QTRY_VERIFY_WITH_TIMEOUT(a->auth.pending().has_value(), kWait);
        m_server->expireDevices();
        QTRY_VERIFY_WITH_TIMEOUT(!a->auth.pending().has_value(), kWait);
        QCOMPARE(a->auth.message(), QStringLiteral("Code abgelaufen, neu anmelden."));
        QCOMPARE(a->auth.state(), TwitchAuth::State::Off);
        const int polls = m_server->count(QStringLiteral("/oauth2/token"));
        QTest::qWait(300);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/token")), polls); // keine weiteren Abfragen
        QVERIFY(!QFile::exists(a->path));
    }

    void codeExpiresLocally()
    {
        m_server->deviceExpiresIn = 1;
        auto a = account(TwitchAuth::Account::Bot, QStringLiteral("twitch-bot-token.cred"));
        a->auth.startLogin();
        QTRY_VERIFY_WITH_TIMEOUT(a->auth.pending().has_value(), kWait);
        // Der Server sagt bis zum Schluss «pending» oder «invalid»; spätestens die eigene Uhr beendet.
        QTRY_VERIFY_WITH_TIMEOUT(!a->auth.pending().has_value(), 3000);
        QCOMPARE(a->auth.message(), QStringLiteral("Code abgelaufen, neu anmelden."));
    }

    void cancelLogin()
    {
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        a->auth.startLogin();
        QTRY_VERIFY_WITH_TIMEOUT(a->auth.pending().has_value(), kWait);
        QTRY_VERIFY_WITH_TIMEOUT(m_server->count(QStringLiteral("/oauth2/token")) >= 1, kWait);
        a->auth.cancelLogin();
        QVERIFY(!a->auth.pending().has_value());
        QTest::qWait(100);
        const int polls = m_server->count(QStringLiteral("/oauth2/token"));
        QVERIFY(m_server->approveLatest(kChannelUser)); // bestätigt nach dem Abbrechen: zählt nicht
        QTest::qWait(300);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/token")), polls);
        QCOMPARE(a->auth.state(), TwitchAuth::State::Off);
        QVERIFY(!QFile::exists(a->path));
    }

    void refreshBeforeExpiry()
    {
        m_server->accessExpiresIn = 3;
        TwitchAuth::Timing timing = fastTiming();
        timing.refreshMargin = 2000; // erneuert etwa 1 s nach der Anmeldung
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"), timing);
        QVERIFY(login(*a, kChannelUser));
        m_server->accessExpiresIn = 14400; // das erneuerte Token gilt wieder lange: genau eine Erneuerung
        const TwitchTestServer::Token first = *m_server->latestToken(kChannelUser.id);
        QTRY_VERIFY_WITH_TIMEOUT(a->auth.accessToken() != first.access, kWait);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/token"), QStringLiteral("refresh_token")), 1);
        const TwitchTestServer::Token second = *m_server->latestToken(kChannelUser.id);
        QCOMPARE(a->auth.accessToken(), second.access);
        QVERIFY(second.refresh != first.refresh);
        QVERIFY(m_server->tokens.first().refreshUsed); // das alte gilt nicht mehr
        // Das neue Refresh-Token ist sofort abgelegt
        QTRY_COMPARE_WITH_TIMEOUT(storedToken(a->path).value(QStringLiteral("refresh_token")).toString(), second.refresh, kWait);
        QCOMPARE(a->auth.state(), TwitchAuth::State::Ok);
        QVERIFY(qAbs(a->auth.expiresAt().secsTo(QDateTime::currentDateTimeUtc().addSecs(14400))) < 30);
    }

    void unauthorizedRenewsAndRetries()
    {
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        QVERIFY(login(*a, kChannelUser));
        m_server->invalidateAccess(kChannelUser.id);
        HelixClient helix(&m_network);
        helix.setEndpoints(Endpoints::testServer(m_server->baseUrl()));
        helix.setClientId(m_server->clientId);
        std::optional<HelixClient::Reply> result;
        helix.get(&a->auth, QStringLiteral("/helix/users"), {}, [&result](const HelixClient::Reply &r) { result = r; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), kWait);
        QCOMPARE(result->status, 200);
        QVERIFY(result->ok());
        QCOMPARE(result->body.value(QStringLiteral("data")).toArray().first().toObject().value(QStringLiteral("login")).toString(),
                 kChannelUser.login);
        QCOMPARE(m_server->count(QStringLiteral("/helix/users")), 2);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/token"), QStringLiteral("refresh_token")), 1);
        // Client-Id und Bearer des Kontos
        const auto users = m_server->requestsTo(QStringLiteral("/helix/users"));
        QCOMPARE(users.last().headers.value("client-id"), QByteArray("testclient"));
        QCOMPARE(users.last().headers.value("authorization"), "Bearer " + a->auth.accessToken().toUtf8());
    }

    void refreshRejected()
    {
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        QVERIFY(login(*a, kChannelUser));
        m_server->rejectRefresh = true;
        m_server->invalidateAccess(kChannelUser.id);
        HelixClient helix(&m_network);
        helix.setEndpoints(Endpoints::testServer(m_server->baseUrl()));
        helix.setClientId(m_server->clientId);
        std::optional<HelixClient::Reply> result;
        helix.get(&a->auth, QStringLiteral("/helix/users"), {}, [&result](const HelixClient::Reply &r) { result = r; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), kWait);
        QCOMPARE(result->status, 401);
        QCOMPARE(a->auth.state(), TwitchAuth::State::Expired);
        QCOMPARE(a->auth.message(),
                 QStringLiteral("Anmeldung bei Twitch abgelaufen (Kanal). Im Dialog «Einstellungen» neu anmelden."));
        QVERIFY(a->auth.accessToken().isEmpty());
        // Kein weiterer Versuch, bis der Nutzer handelt
        QTest::qWait(500);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/token"), QStringLiteral("refresh_token")), 1);
        result.reset();
        helix.get(&a->auth, QStringLiteral("/helix/users"), {}, [&result](const HelixClient::Reply &r) { result = r; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), kWait);
        QCOMPARE(result->status, 401);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/token"), QStringLiteral("refresh_token")), 1);
        // Neu anmelden hilft
        m_server->rejectRefresh = false;
        QVERIFY(login(*a, kChannelUser));
        QCOMPARE(a->auth.message(), QString());
    }

    void hourlyValidationShortened()
    {
        TwitchAuth::Timing timing = fastTiming();
        timing.validateInterval = 150;
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"), timing);
        QVERIFY(login(*a, kChannelUser));
        const int afterLogin = m_server->count(QStringLiteral("/oauth2/validate"));
        QVERIFY(afterLogin >= 1); // Prüfung des neuen Tokens
        QTRY_VERIFY_WITH_TIMEOUT(m_server->count(QStringLiteral("/oauth2/validate")) >= afterLogin + 3, kWait);
        QCOMPARE(a->auth.state(), TwitchAuth::State::Ok);

        // 401 bei der Prüfung: einmal erneuern, dann wieder Ok
        m_server->invalidateAccess(kChannelUser.id);
        QTRY_VERIFY_WITH_TIMEOUT(m_server->count(QStringLiteral("/oauth2/token"), QStringLiteral("refresh_token")) == 1, kWait);
        QTRY_COMPARE_WITH_TIMEOUT(a->auth.accessToken(), m_server->latestToken(kChannelUser.id)->access, kWait);
        QCOMPARE(a->auth.state(), TwitchAuth::State::Ok);

        // Freigabe entzogen (Token und Refresh-Token ungültig): Expired bei der nächsten Prüfung
        m_server->rejectRefresh = true;
        m_server->invalidateAccess(kChannelUser.id);
        QTRY_COMPARE_WITH_TIMEOUT(a->auth.state(), TwitchAuth::State::Expired, kWait);
        const int validations = m_server->count(QStringLiteral("/oauth2/validate"));
        QTest::qWait(500);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/validate")), validations);
    }

    void missingScopes()
    {
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        QStringList granted = dss::twitch::channelScopes();
        granted.removeAll(QStringLiteral("bits:read"));
        granted.removeAll(QStringLiteral("channel:bot"));
        QVERIFY(login(*a, kChannelUser, granted));
        QCOMPARE(a->auth.missingScopes(), (QStringList{"bits:read", "channel:bot"}));
        QCOMPARE(a->auth.message(), QStringLiteral("Rechte fehlen: bits:read, channel:bot. Neu anmelden."));
        QCOMPARE(a->auth.state(), TwitchAuth::State::Ok);
    }

    void logout()
    {
        auto a = account(TwitchAuth::Account::Bot, QStringLiteral("twitch-bot-token.cred"));
        QVERIFY(login(*a, kBotUser));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(a->path), kWait);
        const QString access = a->auth.accessToken();
        a->auth.logout();
        QCOMPARE(a->auth.state(), TwitchAuth::State::Off);
        QVERIFY(a->auth.accessToken().isEmpty());
        QVERIFY(a->auth.userId().isEmpty());
        QVERIFY(!QFile::exists(a->path));
        QTRY_COMPARE_WITH_TIMEOUT(m_server->count(QStringLiteral("/oauth2/revoke")), 1, kWait);
        const auto revoke = m_server->requestsTo(QStringLiteral("/oauth2/revoke")).first();
        QCOMPARE(revoke.formValue(QStringLiteral("client_id")), QStringLiteral("testclient"));
        QVERIFY(revoke.formValue(QStringLiteral("token")) == access);
        QTRY_VERIFY_WITH_TIMEOUT(m_server->tokenByAccess(access)->revoked, kWait);
    }

    void twoAccountsSideBySide()
    {
        auto channel = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        auto bot = account(TwitchAuth::Account::Bot, QStringLiteral("twitch-bot-token.cred"));
        bot->auth.setRejectedUserId(kChannelUser.id);
        QVERIFY(login(*channel, kChannelUser));
        QVERIFY(login(*bot, kBotUser));
        QCOMPARE(channel->auth.userId(), kChannelUser.id);
        QCOMPARE(bot->auth.userId(), kBotUser.id);
        QVERIFY(channel->auth.accessToken() != bot->auth.accessToken());
        QCOMPARE(m_server->requestsTo(QStringLiteral("/oauth2/device")).last().formValue(QStringLiteral("scopes")),
                 dss::twitch::botScopes().join(QLatin1Char(' ')));
        QTRY_VERIFY_WITH_TIMEOUT(!storedToken(channel->path).isEmpty() && !storedToken(bot->path).isEmpty(), kWait);
        QCOMPARE(storedToken(channel->path).value(QStringLiteral("user_id")).toString(), kChannelUser.id);
        QCOMPARE(storedToken(bot->path).value(QStringLiteral("user_id")).toString(), kBotUser.id);
        // Abmelden des Bots lässt den Kanal unberührt
        bot->auth.logout();
        QCOMPARE(channel->auth.state(), TwitchAuth::State::Ok);
        QVERIFY(QFile::exists(channel->path));
        QVERIFY(!QFile::exists(bot->path));
    }

    void botEqualsChannelRejected()
    {
        auto bot = account(TwitchAuth::Account::Bot, QStringLiteral("twitch-bot-token.cred"));
        bot->auth.setRejectedUserId(kChannelUser.id);
        bot->auth.startLogin();
        QTRY_VERIFY_WITH_TIMEOUT(bot->auth.pending().has_value(), kWait);
        QVERIFY(m_server->approveLatest(kChannelUser)); // im Browser versehentlich mit dem Kanal bestätigt
        QTRY_COMPARE_WITH_TIMEOUT(bot->auth.state(), TwitchAuth::State::Error, kWait);
        QCOMPARE(bot->auth.message(),
                 QStringLiteral("Das Bot-Konto ist das Kanal-Konto. Im Browser mit dem Bot-Konto anmelden."));
        QVERIFY(bot->auth.accessToken().isEmpty());
        QVERIFY(bot->auth.userId().isEmpty());
        QTest::qWait(100);
        QVERIFY(!QFile::exists(bot->path));
        QTRY_VERIFY_WITH_TIMEOUT(m_server->tokens.first().revoked, kWait); // Token zurückgegeben
        // Danach mit dem richtigen Konto
        QVERIFY(login(*bot, kBotUser));
        QCOMPARE(bot->auth.message(), QString());
    }

    void loadAtStart()
    {
        {
            auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
            QVERIFY(login(*a, kChannelUser));
            QTRY_VERIFY_WITH_TIMEOUT(!storedToken(a->path).isEmpty(), kWait);
        }
        const int devices = m_server->count(QStringLiteral("/oauth2/device"));
        const int validations = m_server->count(QStringLiteral("/oauth2/validate"));
        auto again = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        again->auth.load();
        QCOMPARE(again->auth.state(), TwitchAuth::State::Checking);
        QTRY_COMPARE_WITH_TIMEOUT(again->auth.state(), TwitchAuth::State::Ok, kWait);
        QCOMPARE(again->auth.login(), kChannelUser.login);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/device")), devices); // keine neue Anmeldung
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/validate")), validations + 1);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/token"), QStringLiteral("refresh_token")), 0);

        // Ohne Datei: aus
        auto none = account(TwitchAuth::Account::Bot, QStringLiteral("twitch-bot-token.cred"));
        none->auth.load();
        QCOMPARE(none->auth.state(), TwitchAuth::State::Off);
    }

    void loadExpiredTokenRefreshesFirst()
    {
        // Abgelegt mit abgelaufenem Access-Token (Kern war länger aus)
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        QVERIFY(login(*a, kChannelUser));
        const TwitchTestServer::Token token = *m_server->latestToken(kChannelUser.id);
        a.reset();
        QFile f(file(QStringLiteral("twitch-token.cred")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{{"access_token", token.access}, {"refresh_token", token.refresh},
                                          {"expires_at", "2026-01-01T00:00:00.000Z"}, {"user_id", kChannelUser.id},
                                          {"login", kChannelUser.login},
                                          {"scopes", QJsonArray::fromStringList(dss::twitch::channelScopes())}})
                    .toJson(QJsonDocument::Compact)
                    .toBase64());
        f.close();
        m_server->invalidateAccess(kChannelUser.id);
        auto again = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        again->auth.load();
        QTRY_COMPARE_WITH_TIMEOUT(again->auth.state(), TwitchAuth::State::Ok, kWait);
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/token"), QStringLiteral("refresh_token")), 1);
        QCOMPARE(again->auth.accessToken(), m_server->latestToken(kChannelUser.id)->access);
    }

    void damagedFile()
    {
        QFile f(file(QStringLiteral("twitch-token.cred")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("kein json").toBase64());
        f.close();
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        a->auth.load();
        QTRY_COMPARE_WITH_TIMEOUT(a->auth.state(), TwitchAuth::State::Error, kWait);
        QVERIFY(a->auth.message().contains(QStringLiteral("nicht lesbar")));
    }

    void rateLimitWaitsForReset()
    {
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        QVERIFY(login(*a, kChannelUser));
        m_server->rateLimited.insert(QStringLiteral("/helix/streams"), 1);
        HelixClient helix(&m_network);
        helix.setEndpoints(Endpoints::testServer(m_server->baseUrl()));
        helix.setClientId(m_server->clientId);
        std::optional<HelixClient::Reply> result;
        QElapsedTimer timer;
        timer.start();
        helix.get(&a->auth, QStringLiteral("/helix/streams"), QUrlQuery(QStringLiteral("user_id=100000001")),
                  [&result](const HelixClient::Reply &r) { result = r; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), kWait);
        QCOMPARE(result->status, 200);
        QCOMPARE(m_server->count(QStringLiteral("/helix/streams")), 2);
        QVERIFY(timer.elapsed() < 2500); // Reset liegt höchstens 1 s in der Zukunft
        QCOMPARE(m_server->requestsTo(QStringLiteral("/helix/streams")).last().query.queryItemValue(QStringLiteral("user_id")),
                 QStringLiteral("100000001"));

        // Zweimal 429: der Aufrufer bekommt das zweite
        m_server->rateLimited.insert(QStringLiteral("/helix/streams"), 2);
        result.reset();
        helix.setMaxRateLimitWait(10);
        helix.get(&a->auth, QStringLiteral("/helix/streams"), {}, [&result](const HelixClient::Reply &r) { result = r; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), kWait);
        QCOMPARE(result->status, 429);
    }

    void networkErrorRetries()
    {
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        QVERIFY(login(*a, kChannelUser));
        const quint16 port = m_server->port();
        m_server->stop();
        m_server->invalidateAccess(kChannelUser.id);
        HelixClient helix(&m_network);
        helix.setEndpoints(Endpoints::testServer(m_server->baseUrl()));
        helix.setClientId(m_server->clientId);
        std::optional<HelixClient::Reply> result;
        helix.get(&a->auth, QStringLiteral("/helix/users"), {}, [&result](const HelixClient::Reply &r) { result = r; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), kWait);
        QCOMPARE(result->status, 0);
        QVERIFY(result->error.startsWith(QStringLiteral("Twitch nicht erreichbar")));
        // Kanal ohne Server: renew wird nur über 401 ausgelöst; das Erneuern direkt anstossen
        bool renewed = false;
        bool answered = false;
        a->auth.renew([&](bool ok) {
            renewed = ok;
            answered = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(answered, kWait);
        QVERIFY(!renewed);
        QCOMPARE(a->auth.state(), TwitchAuth::State::Error);
        QVERIFY(a->auth.message().startsWith(QStringLiteral("Twitch nicht erreichbar")));
        // Server wieder da: der neue Versuch läuft von selbst
        QVERIFY(m_server->start(port));
        QTRY_COMPARE_WITH_TIMEOUT(a->auth.state(), TwitchAuth::State::Ok, kWait);
        QCOMPARE(a->auth.accessToken(), m_server->latestToken(kChannelUser.id)->access);
    }

    void withoutClientId()
    {
        auto a = account(TwitchAuth::Account::Channel, QStringLiteral("twitch-token.cred"));
        a->auth.setClientId(QString());
        a->auth.startLogin();
        QCOMPARE(a->auth.state(), TwitchAuth::State::Error);
        QCOMPARE(a->auth.message(), QStringLiteral("Client-ID fehlt. Zuerst die Client-ID eintragen."));
        QCOMPARE(m_server->count(QStringLiteral("/oauth2/device")), 0);
    }
};

QTEST_GUILESS_MAIN(TestTwitchLogin)
#include "tst_twitchanmeldung.moc"
