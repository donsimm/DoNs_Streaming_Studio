// Schreibende Aufrufe an Twitch (P14 c1a, Plan 4.1 und 10.4) gegen tests/twitchtestserver.h, ohne
// Netz: Send Chat Message mit User-Token (Kanal), mit App-Token (Bot mit Abzeichen), Antwort
// (reply_parent_message_id), Nachricht von Twitch nicht gesendet (drop_reason), falscher Absender
// (Fehler von Twitch). c1b: Benutzerinfo (gefunden, unbekannt, Fehler), Shoutout (angenommen,
// abgelehnt), Ansage (mit und ohne Farbe, 403 ohne Moderator).

#include "chatmessage.h"
#include "helixactions.h"
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

    // --- c1b (Plan 4.8 und 4.9) ---

private:
    template <typename Result, typename Call>
    std::optional<Result> wait(Call call)
    {
        std::optional<Result> result;
        call([&](const Result &r) { result = r; });
        if (!QTest::qWaitFor([&] { return result.has_value(); }, kWait))
            return std::nullopt;
        return result;
    }

private slots:
    void userInfo()
    {
        FixedToken token;
        token.token = m_server->addToken(kChannelUser, channelScopes());
        m_server->addUser(QStringLiteral("900000001"), QStringLiteral("zuschauer42"), QStringLiteral("Zuschauer42"),
                          QStringLiteral("Just Chatting"));
        const auto found = wait<UserInfoResult>([&](auto done) { getUser(m_helix.get(), &token, QStringLiteral("zuschauer42"), done); });
        QVERIFY(found);
        QVERIFY2(found->error.isEmpty(), qPrintable(found->error));
        QVERIFY(found->found);
        QCOMPARE(found->user.id, QStringLiteral("900000001"));
        QCOMPARE(found->user.login, QStringLiteral("zuschauer42"));
        QCOMPARE(found->user.displayName, QStringLiteral("Zuschauer42"));
        QCOMPARE(found->user.description, QStringLiteral("Beschreibung von Zuschauer42"));
        QCOMPARE(found->user.profileImageUrl, QStringLiteral("https://bild.test/900000001.png"));
        QCOMPARE(found->game, QStringLiteral("Just Chatting"));
        QVERIFY(found->gameError.isEmpty());
        const auto channels = m_server->requestsTo(QStringLiteral("/helix/channels"));
        QCOMPARE(channels.size(), 1);
        QCOMPARE(channels[0].query.queryItemValue(QStringLiteral("broadcaster_id")), QStringLiteral("900000001"));

        // Unbekannt: kein Fehler, nicht gefunden, keine zweite Anfrage
        const auto unknown = wait<UserInfoResult>([&](auto done) { getUser(m_helix.get(), &token, QStringLiteral("niemand"), done); });
        QVERIFY(unknown && unknown->error.isEmpty() && !unknown->found);
        QCOMPARE(m_server->requestsTo(QStringLiteral("/helix/channels")).size(), 1);

        // Ungültiges Token: Fehler
        token.token = QStringLiteral("ungueltig");
        const auto failed = wait<UserInfoResult>([&](auto done) { getUser(m_helix.get(), &token, QStringLiteral("zuschauer42"), done); });
        QVERIFY(failed && failed->error.startsWith(QStringLiteral("Twitch antwortet mit 401")));
    }

    void shoutout()
    {
        FixedToken token;
        token.token = m_server->addToken(kChannelUser, channelScopes());
        const auto ok = wait<HelixCallResult>([&](auto done) {
            sendShoutout(m_helix.get(), &token, kChannelUser.id, QStringLiteral("900000001"), kChannelUser.id, done);
        });
        QVERIFY(ok);
        QVERIFY2(ok->ok, qPrintable(ok->error));
        QCOMPARE(ok->status, 204);
        QCOMPARE(m_server->shoutouts.size(), 1);
        const QUrlQuery q = m_server->shoutouts[0].query;
        QCOMPARE(q.queryItemValue(QStringLiteral("from_broadcaster_id")), kChannelUser.id);
        QCOMPARE(q.queryItemValue(QStringLiteral("to_broadcaster_id")), QStringLiteral("900000001"));
        QCOMPARE(q.queryItemValue(QStringLiteral("moderator_id")), kChannelUser.id);

        // Abgelehnt (nicht live): Fehler mit der Meldung von Twitch
        m_server->rejectNextShoutout = QStringLiteral("The broadcaster is not streaming live");
        const auto rejected = wait<HelixCallResult>([&](auto done) {
            sendShoutout(m_helix.get(), &token, kChannelUser.id, QStringLiteral("900000001"), kChannelUser.id, done);
        });
        QVERIFY(rejected && !rejected->ok);
        QCOMPARE(rejected->status, 400);
        QCOMPARE(rejected->error, QStringLiteral("Twitch antwortet mit 400: The broadcaster is not streaming live"));
        QCOMPARE(m_server->shoutouts.size(), 1);
    }

    void announcement()
    {
        FixedToken bot;
        bot.token = m_server->addToken(kBotUser, botScopes());
        const auto ok = wait<HelixCallResult>([&](auto done) {
            sendAnnouncement(m_helix.get(), &bot, kChannelUser.id, kBotUser.id, QString::fromUtf8("Grüezi 😀"),
                             QStringLiteral("purple"), done);
        });
        QVERIFY(ok);
        QVERIFY2(ok->ok, qPrintable(ok->error));
        QCOMPARE(m_server->announcements.size(), 1);
        const auto &a = m_server->announcements[0];
        QCOMPARE(a.query.queryItemValue(QStringLiteral("broadcaster_id")), kChannelUser.id);
        QCOMPARE(a.query.queryItemValue(QStringLiteral("moderator_id")), kBotUser.id);
        QCOMPARE(a.body.value(QStringLiteral("message")).toString(), QString::fromUtf8("Grüezi 😀"));
        QCOMPARE(a.body.value(QStringLiteral("color")).toString(), QStringLiteral("purple"));

        // Ohne Farbe fehlt das Feld (Twitch: primary)
        QVERIFY(wait<HelixCallResult>([&](auto done) {
            sendAnnouncement(m_helix.get(), &bot, kChannelUser.id, kBotUser.id, QStringLiteral("ohne"), {}, done);
        })->ok);
        QVERIFY(!m_server->announcements[1].body.contains(QStringLiteral("color")));

        // Bot moderiert nicht: 403
        m_server->nonModerators.insert(kBotUser.id);
        const auto forbidden = wait<HelixCallResult>([&](auto done) {
            sendAnnouncement(m_helix.get(), &bot, kChannelUser.id, kBotUser.id, QStringLiteral("nein"), {}, done);
        });
        QVERIFY(forbidden && !forbidden->ok);
        QCOMPARE(forbidden->status, 403);
        QCOMPARE(m_server->announcements.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TestTwitchSchreiben)
#include "tst_twitchschreiben.moc"
