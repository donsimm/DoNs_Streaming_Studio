// Schreibende Aufrufe an Twitch (P14 c1a, Plan 4.1 und 10.4) gegen tests/twitchtestserver.h, ohne
// Netz: Send Chat Message mit User-Token (Kanal), mit App-Token (Bot mit Abzeichen), Antwort
// (reply_parent_message_id), Nachricht von Twitch nicht gesendet (drop_reason), falscher Absender
// (Fehler von Twitch). c1b ergänzt Benutzerinfo, Shoutout und Ansage.

#include "chatmessage.h"
#include "helixclient.h"
#include "twitchauth.h"
#include "twitchtestserver.h"

#include <QNetworkAccessManager>
#include <QTest>

#include <optional>

using namespace dss::twitch;
using testhelp::TwitchTestServer;
using testhelp::TwitchTestUser;

namespace {

constexpr int kWait = 10000;
const TwitchTestUser kChannelUser{QStringLiteral("100000001"), QStringLiteral("testkanal")};
const TwitchTestUser kBotUser{QStringLiteral("100000002"), QStringLiteral("testbot")};

class FixedToken : public TokenSource
{
public:
    QString token;
    QString accessToken() const override { return token; }
    void renew(std::function<void(bool)> done) override { done(false); }
};

} // namespace

class TestTwitchSchreiben : public QObject
{
    Q_OBJECT

    QNetworkAccessManager m_network;
    std::unique_ptr<TwitchTestServer> m_server;
    std::unique_ptr<HelixClient> m_helix;

    std::optional<ChatMessageResult> send(const QString &token, const ChatMessage &message)
    {
        FixedToken source;
        source.token = token;
        std::optional<ChatMessageResult> result;
        sendChatMessage(m_helix.get(), &source, message, [&](const ChatMessageResult &r) { result = r; });
        if (!QTest::qWaitFor([&] { return result.has_value(); }, kWait))
            return std::nullopt;
        return result;
    }

private slots:
    void init()
    {
        m_server = std::make_unique<TwitchTestServer>();
        QVERIFY(m_server->start());
        m_helix = std::make_unique<HelixClient>(&m_network);
        m_helix->setEndpoints(Endpoints::testServer(m_server->baseUrl()));
        m_helix->setClientId(m_server->clientId);
    }

    void channelWithReply()
    {
        const QString token = m_server->addToken(kChannelUser, channelScopes());
        const auto r = send(token, {kChannelUser.id, kChannelUser.id, QString::fromUtf8("Grüezi 😀"), QStringLiteral("eltern-1")});
        QVERIFY(r);
        QVERIFY2(r->sent, qPrintable(r->error));
        QVERIFY(r->messageId.startsWith(QStringLiteral("nachricht-")));
        QVERIFY(r->error.isEmpty());
        QCOMPARE(m_server->chatMessages.size(), 1);
        const QJsonObject body = m_server->chatMessages[0].body;
        QCOMPARE(body.value(QStringLiteral("broadcaster_id")).toString(), kChannelUser.id);
        QCOMPARE(body.value(QStringLiteral("sender_id")).toString(), kChannelUser.id);
        QCOMPARE(body.value(QStringLiteral("message")).toString(), QString::fromUtf8("Grüezi 😀"));
        QCOMPARE(body.value(QStringLiteral("reply_parent_message_id")).toString(), QStringLiteral("eltern-1"));
        QCOMPARE(m_server->chatMessages[0].messageId, r->messageId);

        // Ohne Antwort fehlt das Feld
        QVERIFY(send(token, {kChannelUser.id, kChannelUser.id, QStringLiteral("ohne"), {}})->sent);
        QVERIFY(!m_server->chatMessages[1].body.contains(QStringLiteral("reply_parent_message_id")));
    }

    void botWithAppToken()
    {
        const QString app = m_server->addToken(TwitchTestUser(), {});
        const auto r = send(app, {kChannelUser.id, kBotUser.id, QStringLiteral("als Bot"), {}});
        QVERIFY(r && r->sent);
        QVERIFY(m_server->chatMessages[0].appToken);
        QCOMPARE(m_server->chatMessages[0].body.value(QStringLiteral("sender_id")).toString(), kBotUser.id);
    }

    void dropped()
    {
        const QString token = m_server->addToken(kBotUser, botScopes());
        m_server->dropNextChat = QStringLiteral("automod_held");
        const auto r = send(token, {kChannelUser.id, kBotUser.id, QStringLiteral("gesperrt"), {}});
        QVERIFY(r);
        QVERIFY(!r->sent);
        QCOMPARE(r->dropCode, QStringLiteral("automod_held"));
        QCOMPARE(r->error, QStringLiteral("von Twitch nicht gesendet: Test"));
        QVERIFY(!r->messageId.isEmpty());
    }

    void wrongSender()
    {
        const QString token = m_server->addToken(kBotUser, botScopes());
        const auto r = send(token, {kChannelUser.id, kChannelUser.id, QStringLiteral("falsch"), {}});
        QVERIFY(r);
        QVERIFY(!r->sent);
        QVERIFY(r->dropCode.isEmpty());
        QVERIFY2(r->error.startsWith(QStringLiteral("Twitch antwortet mit 401")), qPrintable(r->error));
        QVERIFY(m_server->chatMessages.isEmpty());
        QVERIFY(!r->error.contains(token));
    }
};

QTEST_GUILESS_MAIN(TestTwitchSchreiben)
#include "tst_twitchschreiben.moc"
