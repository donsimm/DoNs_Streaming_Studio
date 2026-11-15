// Protokoll von EventSub und «erste Nachricht in diesem Stream» (P13, Commit 4): Nachrichten lesen,
// Benachrichtigungen in Ereignisse nach eventtypes.h umsetzen (Rollen, Bot-Konto, Geschenke getrennt,
// anonym), Abos bauen; ChatSession mit erster Nachricht je Zuschauer, Leeren bei offline, Neustart
// mit twitch-sitzung.json im selben und in einem neuen Stream. Ohne Verbindung, ohne Qt Gui.
// Testdaten: tests/daten/twitch/ (nachgebaut nach Doku, LIESMICH.md).

#include "chatsession.h"
#include "eventbus.h"
#include "eventsubprotocol.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace dss::twitch;
using dss::events::Event;
using dss::events::EventBus;

namespace {

constexpr int kWait = 5000;
const QString kChannel = QStringLiteral("100000001");
const QString kBot = QStringLiteral("100000002");

QByteArray readData(const QString &name)
{
    QFile file(QStringLiteral(DSS_TEST_TWITCH_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Testdatei %s fehlt", qPrintable(name));
    return file.readAll();
}

Message parse(const QString &name)
{
    Message message;
    QString error;
    if (!parseMessage(readData(name), &message, &error))
        qFatal("%s nicht lesbar: %s", qPrintable(name), qPrintable(error));
    return message;
}

// Datei → Ereignis; prüft dabei gegen die Typenliste.
Event eventFrom(const QString &name, const Context &context = {kChannel, kBot})
{
    QString error;
    const std::optional<Event> event = toEvent(parse(name), context, &error);
    if (!event)
        qFatal("%s: %s", qPrintable(name), qPrintable(error));
    const QString problem = EventBus::validate(*event);
    if (!problem.isEmpty())
        qFatal("%s passt nicht zur Typenliste: %s", qPrintable(name), qPrintable(problem));
    return *event;
}

// Ereignis wie aus toEvent, ohne Datei.
Event chatEvent(const QString &userId, const QString &messageType = QStringLiteral("text"))
{
    Event event = eventFrom(QStringLiteral("chat_ohne_emote.json"));
    event.data.insert(QStringLiteral("user_id"), userId);
    event.data.insert(QStringLiteral("message_type"), messageType);
    return event;
}

Event streamOnline(const QString &startedAt, bool atStart = false)
{
    Event event = eventFrom(QStringLiteral("stream_online.json"));
    event.data.insert(QStringLiteral("started_at"), startedAt);
    event.data.insert(QStringLiteral("at_start"), atStart);
    return event;
}

// Sammelt twitch.first_message auf dem Fluss.
struct FirstMessages
{
    explicit FirstMessages(EventBus *bus)
        : subscription(bus->subscribe(&context, {QStringLiteral("twitch.first_message")},
                                      [this](const Event &e) { received.append(e); }))
    {
    }
    QObject context;
    QList<Event> received;
    dss::events::Subscription subscription;
};

} // namespace

class TestTwitchProtocol : public QObject
{
    Q_OBJECT

private slots:
    void sessionMessages()
    {
        const Message welcome = parse(QStringLiteral("session_welcome.json"));
        QCOMPARE(welcome.kind, MessageKind::Welcome);
        QCOMPARE(welcome.sessionId, QStringLiteral("AgoQ-testsitzung-0001"));
        QCOMPARE(welcome.keepaliveSeconds, 10);
        QVERIFY(welcome.reconnectUrl.isEmpty());
        QCOMPARE(welcome.messageId, QStringLiteral("nachricht-0001"));
        QCOMPARE(welcome.timestamp, QDateTime(QDate(2026, 11, 15), QTime(17, 59), QTimeZone::UTC));

        QCOMPARE(parse(QStringLiteral("session_keepalive.json")).kind, MessageKind::Keepalive);

        const Message reconnect = parse(QStringLiteral("session_reconnect.json"));
        QCOMPARE(reconnect.kind, MessageKind::Reconnect);
        QCOMPARE(reconnect.reconnectUrl, QStringLiteral("wss://eventsub.wss.twitch.tv/ws?id=neu-0001"));
        QCOMPARE(reconnect.keepaliveSeconds, 0);

        const Message revocation = parse(QStringLiteral("revocation.json"));
        QCOMPARE(revocation.kind, MessageKind::Revocation);
        QCOMPARE(revocation.subscriptionType, QStringLiteral("channel.follow"));
        QCOMPARE(revocation.subscriptionStatus, QStringLiteral("authorization_revoked"));
        QCOMPARE(revocation.subscriptionId, QStringLiteral("abo-0004"));
        QString error;
        QVERIFY(!toEvent(revocation, {}, &error));
    }

    void notificationMetadata()
    {
        const Message message = parse(QStringLiteral("chat_message.json"));
        QCOMPARE(message.kind, MessageKind::Notification);
        QCOMPARE(message.subscriptionType, QStringLiteral("channel.chat.message"));
        QCOMPARE(message.subscriptionVersion, QStringLiteral("1"));
        // Nanosekunden der Doku → Millisekunden, UTC
        QCOMPARE(message.timestamp, QDateTime(QDate(2026, 11, 15), QTime(18, 0, 1, 123), QTimeZone::UTC));
        const Event event = eventFrom(QStringLiteral("chat_message.json"));
        QCOMPARE(event.type, QStringLiteral("twitch.chat_message"));
        QCOMPARE(event.origin, QStringLiteral("twitch"));
        QCOMPARE(event.originId, QStringLiteral("nachricht-0010"));
        QCOMPARE(event.time, message.timestamp);
    }

    void badInput_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<QString>("error");
        QTest::newRow("kein JSON") << QByteArray("{nicht json") << "kein JSON-Objekt";
        QTest::newRow("ohne metadata") << QByteArray(R"({"payload":{}})") << "«metadata»";
        QTest::newRow("ohne Zeit")
            << QByteArray(R"({"metadata":{"message_id":"a","message_type":"session_keepalive"},"payload":{}})")
            << "«metadata.message_timestamp»";
        QTest::newRow("Welcome ohne Sitzung")
            << QByteArray(R"({"metadata":{"message_id":"a","message_type":"session_welcome",)"
                          R"("message_timestamp":"2026-11-15T18:00:00Z"},"payload":{}})")
            << "«payload.session»";
    }
    void badInput()
    {
        QFETCH(QByteArray, text);
        QFETCH(QString, error);
        Message message;
        QString problem;
        QVERIFY(!parseMessage(text, &message, &problem));
        QVERIFY2(problem.contains(error), qPrintable(problem));
    }

    void unknownMessageType()
    {
        Message message;
        QString error;
        QVERIFY(parseMessage(R"({"metadata":{"message_id":"a","message_type":"neu_ab_2027",)"
                             R"("message_timestamp":"2026-11-15T18:00:00Z"},"payload":{}})",
                             &message, &error));
        QCOMPARE(message.kind, MessageKind::Unknown);
    }

    void chatViewerWithEmote()
    {
        const QJsonObject data = eventFrom(QStringLiteral("chat_message.json")).data;
        QCOMPARE(data.value(QStringLiteral("message_id")).toString(), QStringLiteral("chat-0010"));
        QCOMPARE(data.value(QStringLiteral("user_id")).toString(), QStringLiteral("100000101"));
        QCOMPARE(data.value(QStringLiteral("user_login")).toString(), QStringLiteral("zuschauer_eins"));
        QCOMPARE(data.value(QStringLiteral("user_name")).toString(), QStringLiteral("Zuschauer_Eins"));
        QCOMPARE(data.value(QStringLiteral("text")).toString(), QStringLiteral("Hallo zusammen Kappa"));
        const QJsonArray fragments = data.value(QStringLiteral("fragments")).toArray();
        QCOMPARE(fragments.size(), 2);
        QCOMPARE(fragments.at(0).toObject(),
                 (QJsonObject{{"type", "text"}, {"text", "Hallo zusammen "}, {"emote_id", QJsonValue::Null}}));
        QCOMPARE(fragments.at(1).toObject(), (QJsonObject{{"type", "emote"}, {"text", "Kappa"}, {"emote_id", "25"}}));
        QCOMPARE(data.value(QStringLiteral("color")).toString(), QStringLiteral("#1E90FF"));
        QCOMPARE(data.value(QStringLiteral("badges")).toArray(), (QJsonArray{QJsonObject{{"set_id", "subscriber"}, {"id", "12"}}}));
        QCOMPARE(data.value(QStringLiteral("role")).toInt(), 1);
        QVERIFY(data.value(QStringLiteral("is_subscriber")).toBool());
        QVERIFY(!data.value(QStringLiteral("is_vip")).toBool());
        QVERIFY(!data.value(QStringLiteral("is_moderator")).toBool());
        QVERIFY(!data.value(QStringLiteral("is_broadcaster")).toBool());
        QVERIFY(!data.value(QStringLiteral("is_bot_account")).toBool());
        QCOMPARE(data.value(QStringLiteral("bits")).toInt(), 0);
        QVERIFY(data.value(QStringLiteral("reply_to")).isNull());
        QVERIFY(data.value(QStringLiteral("reward_id")).isNull());
        QCOMPARE(data.value(QStringLiteral("message_type")).toString(), QStringLiteral("text"));
        QVERIFY(!data.value(QStringLiteral("is_highlight")).toBool());
        QVERIFY(data.value(QStringLiteral("source_broadcaster_id")).isNull());
    }

    void chatCases_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("role");
        QTest::addColumn<bool>("subscriber");
        QTest::addColumn<bool>("vip");
        QTest::addColumn<bool>("moderator");
        QTest::addColumn<bool>("broadcaster");
        QTest::addColumn<bool>("bot");
        QTest::newRow("ohne Emote") << "chat_ohne_emote.json" << 1 << false << false << false << false << false;
        QTest::newRow("Moderator") << "chat_moderator.json" << 3 << true << false << true << false << false;
        QTest::newRow("VIP") << "chat_vip.json" << 2 << false << true << false << false << false;
        QTest::newRow("Bot schreibt") << "chat_bot.json" << 3 << false << false << true << false << true;
        QTest::newRow("Kanal") << "chat_kanal.json" << 4 << false << false << false << true << false;
    }
    void chatCases()
    {
        QFETCH(QString, file);
        QFETCH(int, role);
        QFETCH(bool, subscriber);
        QFETCH(bool, vip);
        QFETCH(bool, moderator);
        QFETCH(bool, broadcaster);
        QFETCH(bool, bot);
        const QJsonObject data = eventFrom(file).data;
        QCOMPARE(data.value(QStringLiteral("role")).toInt(), role);
        QCOMPARE(data.value(QStringLiteral("is_subscriber")).toBool(), subscriber);
        QCOMPARE(data.value(QStringLiteral("is_vip")).toBool(), vip);
        QCOMPARE(data.value(QStringLiteral("is_moderator")).toBool(), moderator);
        QCOMPARE(data.value(QStringLiteral("is_broadcaster")).toBool(), broadcaster);
        QCOMPARE(data.value(QStringLiteral("is_bot_account")).toBool(), bot);
    }

    void chatSpecialCases()
    {
        const QJsonObject plain = eventFrom(QStringLiteral("chat_ohne_emote.json")).data;
        QCOMPARE(plain.value(QStringLiteral("color")).toString(), QString());
        QCOMPARE(plain.value(QStringLiteral("badges")).toArray(), QJsonArray());
        QCOMPARE(plain.value(QStringLiteral("fragments")).toArray().size(), 1);

        const QJsonObject reply = eventFrom(QStringLiteral("chat_antwort.json")).data;
        QCOMPARE(reply.value(QStringLiteral("reply_to")).toString(), QStringLiteral("chat-0013"));
        QCOMPARE(reply.value(QStringLiteral("fragments")).toArray().at(0).toObject().value(QStringLiteral("type")).toString(),
                 QStringLiteral("mention"));

        const QJsonObject cheer = eventFrom(QStringLiteral("chat_cheer.json")).data;
        QCOMPARE(cheer.value(QStringLiteral("bits")).toInt(), 100);
        QVERIFY(cheer.value(QStringLiteral("fragments")).toArray().at(0).toObject().value(QStringLiteral("emote_id")).isNull());

        const QJsonObject intro = eventFrom(QStringLiteral("chat_user_intro.json")).data;
        QCOMPARE(intro.value(QStringLiteral("message_type")).toString(), QStringLiteral("user_intro"));

        const QJsonObject highlight = eventFrom(QStringLiteral("chat_hervorgehoben.json")).data;
        QVERIFY(highlight.value(QStringLiteral("is_highlight")).toBool());

        // Ohne Bot-Konto ist keine Nachricht vom Bot; Kanal auch ohne Abzeichen an der Kennung erkannt.
        QVERIFY(!eventFrom(QStringLiteral("chat_bot.json"), {kChannel, QString()}).data.value(QStringLiteral("is_bot_account")).toBool());
        Message message = parse(QStringLiteral("chat_kanal.json"));
        message.event.insert(QStringLiteral("badges"), QJsonArray());
        QString error;
        const std::optional<Event> withoutBadge = toEvent(message, {kChannel, kBot}, &error);
        QVERIFY(withoutBadge);
        QCOMPARE(withoutBadge->data.value(QStringLiteral("role")).toInt(), 4);
    }

    void follow()
    {
        const Event event = eventFrom(QStringLiteral("follow.json"));
        QCOMPARE(event.type, QStringLiteral("twitch.follow"));
        QCOMPARE(event.data.value(QStringLiteral("user_login")).toString(), QStringLiteral("zuschauer_eins"));
        QCOMPARE(event.data.value(QStringLiteral("followed_at")).toString(), QStringLiteral("2026-11-15T18:00:00.987654321Z"));
    }

    void subscribeAndGiftSeparated()
    {
        const Event sub = eventFrom(QStringLiteral("subscribe.json"));
        QCOMPARE(sub.type, QStringLiteral("twitch.subscribe"));
        QCOMPARE(sub.data.value(QStringLiteral("user_name")).toString(), QStringLiteral("Zuschauer_Eins"));
        QCOMPARE(sub.data.value(QStringLiteral("tier")).toInt(), 1000);
        QCOMPARE(sub.data.value(QStringLiteral("tier_name")).toString(), QStringLiteral("tier 1"));
        QCOMPARE(sub.data.value(QStringLiteral("is_gift")).toBool(), false);

        // Geschenk: nicht als twitch.subscribe, Beschenkter in recipient_user_*, Schenkender unbekannt
        const Event gift = eventFrom(QStringLiteral("subscribe_geschenk.json"));
        QCOMPARE(gift.type, QStringLiteral("twitch.gift_sub"));
        QVERIFY(gift.data.value(QStringLiteral("user_id")).isNull());
        QVERIFY(gift.data.value(QStringLiteral("user_login")).isNull());
        QVERIFY(gift.data.value(QStringLiteral("user_name")).isNull());
        QCOMPARE(gift.data.value(QStringLiteral("recipient_user_id")).toString(), QStringLiteral("100000104"));
        QCOMPARE(gift.data.value(QStringLiteral("recipient_user_login")).toString(), QStringLiteral("neu_hier"));
        QCOMPARE(gift.data.value(QStringLiteral("recipient_user_name")).toString(), QStringLiteral("Neu_hier"));
        QCOMPARE(gift.data.value(QStringLiteral("tier")).toInt(), 2000);
        QCOMPARE(gift.data.value(QStringLiteral("tier_name")).toString(), QStringLiteral("tier 2"));
    }

    void resubscribe()
    {
        const Event event = eventFrom(QStringLiteral("resubscribe.json"));
        QCOMPARE(event.type, QStringLiteral("twitch.resubscribe"));
        QCOMPARE(event.data.value(QStringLiteral("text")).toString(), QStringLiteral("Schon ein Jahr dabei"));
        QCOMPARE(event.data.value(QStringLiteral("months")).toInt(), 12);
        QVERIFY(event.data.value(QStringLiteral("streak_months")).isNull());
        QCOMPARE(event.data.value(QStringLiteral("duration_months")).toInt(), 1);
        QCOMPARE(event.data.value(QStringLiteral("tier_name")).toString(), QStringLiteral("tier 3"));
    }

    void giftBomb()
    {
        const Event bomb = eventFrom(QStringLiteral("gift_bomb.json"));
        QCOMPARE(bomb.type, QStringLiteral("twitch.gift_bomb"));
        QCOMPARE(bomb.data.value(QStringLiteral("user_login")).toString(), QStringLiteral("schenker"));
        QCOMPARE(bomb.data.value(QStringLiteral("total")).toInt(), 5);
        QCOMPARE(bomb.data.value(QStringLiteral("cumulative_total")).toInt(), 42);
        QCOMPARE(bomb.data.value(QStringLiteral("is_anonymous")).toBool(), false);

        const Event anonymous = eventFrom(QStringLiteral("gift_bomb_anonym.json"));
        QVERIFY(anonymous.data.value(QStringLiteral("user_id")).isNull());
        QVERIFY(anonymous.data.value(QStringLiteral("user_name")).isNull());
        QVERIFY(anonymous.data.value(QStringLiteral("cumulative_total")).isNull());
        QCOMPARE(anonymous.data.value(QStringLiteral("is_anonymous")).toBool(), true);
        QCOMPARE(anonymous.data.value(QStringLiteral("total")).toInt(), 10);
    }

    void cheer()
    {
        const Event cheer = eventFrom(QStringLiteral("cheer.json"));
        QCOMPARE(cheer.type, QStringLiteral("twitch.cheer"));
        QCOMPARE(cheer.data.value(QStringLiteral("bits")).toInt(), 500);
        QCOMPARE(cheer.data.value(QStringLiteral("text")).toString(), QStringLiteral("Cheer500 weiter so"));
        QCOMPARE(cheer.data.value(QStringLiteral("user_login")).toString(), QStringLiteral("zuschauer_eins"));
        const Event anonymous = eventFrom(QStringLiteral("cheer_anonym.json"));
        QVERIFY(anonymous.data.value(QStringLiteral("user_login")).isNull());
        QCOMPARE(anonymous.data.value(QStringLiteral("is_anonymous")).toBool(), true);
    }

    void raidRewardStream()
    {
        const Event raid = eventFrom(QStringLiteral("raid.json"));
        QCOMPARE(raid.type, QStringLiteral("twitch.raid"));
        QCOMPARE(raid.data.value(QStringLiteral("from_user_id")).toString(), QStringLiteral("100000201"));
        QCOMPARE(raid.data.value(QStringLiteral("from_user_login")).toString(), QStringLiteral("anderer_kanal"));
        QCOMPARE(raid.data.value(QStringLiteral("from_user_name")).toString(), QStringLiteral("Anderer_Kanal"));
        QCOMPARE(raid.data.value(QStringLiteral("viewers")).toInt(), 9001);

        const Event reward = eventFrom(QStringLiteral("reward_redemption.json"));
        QCOMPARE(reward.type, QStringLiteral("twitch.reward_redeemed"));
        QCOMPARE(reward.data.value(QStringLiteral("redemption_id")).toString(), QStringLiteral("einloesung-0001"));
        QCOMPARE(reward.data.value(QStringLiteral("reward_title")).toString(), QStringLiteral("Lied wünschen"));
        QCOMPARE(reward.data.value(QStringLiteral("reward_cost")).toInt(), 500);
        QCOMPARE(reward.data.value(QStringLiteral("status")).toString(), QStringLiteral("unfulfilled"));
        QCOMPARE(reward.data.value(QStringLiteral("user_input")).toString(), QStringLiteral("bitte spielen"));

        const Event online = eventFrom(QStringLiteral("stream_online.json"));
        QCOMPARE(online.type, QStringLiteral("twitch.stream_online"));
        QCOMPARE(online.data.value(QStringLiteral("started_at")).toString(), QStringLiteral("2026-11-15T17:58:00Z"));
        QCOMPARE(online.data.value(QStringLiteral("kind")).toString(), QStringLiteral("live"));
        QCOMPARE(online.data.value(QStringLiteral("at_start")).toBool(), false);

        const Event offline = eventFrom(QStringLiteral("stream_offline.json"));
        QCOMPARE(offline.type, QStringLiteral("twitch.stream_offline"));
        QCOMPARE(offline.data.value(QStringLiteral("ended_at")).toString(), QStringLiteral("2026-11-15T21:00:00.500Z"));
    }

    void streamStateAtStart()
    {
        const QJsonArray live = QJsonDocument::fromJson(readData(QStringLiteral("streams_live.json")))
                                    .object().value(QStringLiteral("data")).toArray();
        QCOMPARE(live.size(), 1);
        QString error;
        const std::optional<Event> event = streamOnlineAtStart(live.first().toObject(), &error);
        QVERIFY2(event, qPrintable(error));
        QVERIFY(EventBus::validate(*event).isEmpty());
        QCOMPARE(event->data.value(QStringLiteral("at_start")).toBool(), true);
        QCOMPARE(event->data.value(QStringLiteral("started_at")).toString(), QStringLiteral("2026-11-15T17:58:00Z"));
        const QJsonArray offline = QJsonDocument::fromJson(readData(QStringLiteral("streams_offline.json")))
                                       .object().value(QStringLiteral("data")).toArray();
        QVERIFY(offline.isEmpty());
        QVERIFY(!streamOnlineAtStart(QJsonObject{}, &error));
    }

    void allFilesMatchTypeList()
    {
        const QStringList files{"chat_message.json", "chat_ohne_emote.json", "chat_antwort.json", "chat_moderator.json",
                                "chat_vip.json", "chat_bot.json", "chat_kanal.json", "chat_user_intro.json",
                                "chat_cheer.json", "chat_hervorgehoben.json", "follow.json", "subscribe.json",
                                "subscribe_geschenk.json", "resubscribe.json", "gift_bomb.json",
                                "gift_bomb_anonym.json", "cheer.json", "cheer_anonym.json", "raid.json",
                                "reward_redemption.json", "stream_online.json", "stream_offline.json"};
        EventBus bus;
        for (const QString &file : files)
            QVERIFY2(bus.publish(eventFrom(file)), qPrintable(file));
        QCOMPARE(bus.rejectedCount(), 0);
    }

    // Fehlermeldungen nennen Typ und Feld, nie Inhalte (Chat-Texte bleiben aus dem Protokoll).
    void missingFieldNamesFieldOnly()
    {
        Message message = parse(QStringLiteral("chat_message.json"));
        message.event.remove(QStringLiteral("color"));
        QString error;
        QVERIFY(!toEvent(message, {kChannel, kBot}, &error));
        QCOMPARE(error, QStringLiteral("Feld «color»: Text erwartet"));

        message = parse(QStringLiteral("chat_message.json"));
        message.event.insert(QStringLiteral("message"), QStringLiteral("Hallo zusammen Kappa"));
        QVERIFY(!toEvent(message, {kChannel, kBot}, &error));
        QVERIFY2(!error.contains(QStringLiteral("Hallo")), qPrintable(error));

        message = parse(QStringLiteral("subscribe.json"));
        message.event.insert(QStringLiteral("tier"), QStringLiteral("4000"));
        QVERIFY(!toEvent(message, {}, &error));
        QCOMPARE(error, QStringLiteral("Feld «tier»: unbekannter Wert"));

        message = parse(QStringLiteral("gift_bomb.json"));
        message.event.insert(QStringLiteral("user_id"), 5);
        QVERIFY(!toEvent(message, {}, &error));
        QCOMPARE(error, QStringLiteral("Feld «user_id»: Text oder null erwartet"));

        message = parse(QStringLiteral("follow.json"));
        message.subscriptionType = QStringLiteral("channel.ad_break.begin");
        QVERIFY(!toEvent(message, {}, &error));
        QCOMPARE(error, QStringLiteral("Abo «channel.ad_break.begin» gehört nicht zu P13"));
    }

    void tiers()
    {
        QCOMPARE(tierNumber(QStringLiteral("1000")), 1000);
        QCOMPARE(tierNumber(QStringLiteral("3000")), 3000);
        QCOMPARE(tierNumber(QStringLiteral("prime")), 0);
        QCOMPARE(tierName(2000), QStringLiteral("tier 2"));
        QCOMPARE(tierName(0), QString());
    }

    void subscriptions()
    {
        const QList<QJsonObject> requests = subscriptionRequests(kChannel, QStringLiteral("sitzung"));
        QCOMPARE(requests.size(), 10);
        QHash<QString, QJsonObject> byType;
        for (const QJsonObject &request : requests) {
            byType.insert(request.value(QStringLiteral("type")).toString(), request);
            QCOMPARE(request.value(QStringLiteral("transport")).toObject(),
                     (QJsonObject{{"method", "websocket"}, {"session_id", "sitzung"}}));
        }
        QCOMPARE(byType.size(), 10);
        QCOMPARE(byType.value("channel.chat.message").value("condition").toObject(),
                 (QJsonObject{{"broadcaster_user_id", kChannel}, {"user_id", kChannel}}));
        QCOMPARE(byType.value("channel.follow").value("version").toString(), QStringLiteral("2"));
        QCOMPARE(byType.value("channel.follow").value("condition").toObject(),
                 (QJsonObject{{"broadcaster_user_id", kChannel}, {"moderator_user_id", kChannel}}));
        QCOMPARE(byType.value("channel.raid").value("condition").toObject(),
                 (QJsonObject{{"to_broadcaster_user_id", kChannel}}));
        QCOMPARE(byType.value("stream.offline").value("condition").toObject(),
                 (QJsonObject{{"broadcaster_user_id", kChannel}}));
        QVERIFY(byType.contains("channel.channel_points_custom_reward_redemption.add"));
    }

    // ChatSession: erste Nachricht je Zuschauer
    void firstMessagePerChatter()
    {
        EventBus bus;
        ChatSession session(&bus, QString());
        FirstMessages first(&bus);
        QVERIFY(bus.publish(chatEvent(QStringLiteral("1"))));
        QVERIFY(bus.publish(chatEvent(QStringLiteral("1"))));
        QVERIFY(bus.publish(chatEvent(QStringLiteral("2"), QStringLiteral("user_intro"))));
        QVERIFY(bus.publish(chatEvent(QStringLiteral("1"))));
        QTRY_COMPARE_WITH_TIMEOUT(first.received.size(), 2, kWait);
        QTest::qWait(50);
        QCOMPARE(first.received.size(), 2);
        QCOMPARE(bus.rejectedCount(), 0);
        QCOMPARE(first.received.at(0).data.value(QStringLiteral("user_id")).toString(), QStringLiteral("1"));
        QCOMPARE(first.received.at(0).data.value(QStringLiteral("first_ever")).toBool(), false);
        QCOMPARE(first.received.at(1).data.value(QStringLiteral("user_id")).toString(), QStringLiteral("2"));
        QCOMPARE(first.received.at(1).data.value(QStringLiteral("first_ever")).toBool(), true);
        // Alle Felder der Nachricht sind dabei, Zeit und Kennung der Herkunft auch.
        const Event chat = chatEvent(QStringLiteral("1"));
        QJsonObject expected = chat.data;
        expected.insert(QStringLiteral("first_ever"), false);
        QCOMPARE(first.received.at(0).data, expected);
        QCOMPARE(first.received.at(0).originId, chat.originId);
        QCOMPARE(first.received.at(0).time, chat.time);
        QCOMPARE(session.chatterCount(), 2);
    }

    void streamOnlineAndOfflineClear()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("twitch-sitzung.json"));
        EventBus bus;
        ChatSession session(&bus, file, 10);
        FirstMessages first(&bus);
        QVERIFY(bus.publish(chatEvent(QStringLiteral("1"))));
        QTRY_COMPARE_WITH_TIMEOUT(first.received.size(), 1, kWait);
        QTest::qWait(50);
        QVERIFY(!QFile::exists(file)); // ohne laufenden Stream nichts gespeichert

        // Neuer Stream: Liste leer, dieselbe Person wird wieder begrüsst
        QVERIFY(bus.publish(streamOnline(QStringLiteral("2026-11-15T17:58:00Z"))));
        QVERIFY(bus.publish(chatEvent(QStringLiteral("1"))));
        QTRY_COMPARE_WITH_TIMEOUT(first.received.size(), 2, kWait);
        QVERIFY(session.isLive());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(file), kWait);

        // Derselbe Stream noch einmal gemeldet (Wiederverbinden): Liste bleibt
        QVERIFY(bus.publish(streamOnline(QStringLiteral("2026-11-15T17:58:00Z"), true)));
        QVERIFY(bus.publish(chatEvent(QStringLiteral("1"))));
        QTest::qWait(50);
        QCOMPARE(first.received.size(), 2);

        // Offline: Liste leer, Datei gelöscht
        QVERIFY(bus.publish(eventFrom(QStringLiteral("stream_offline.json"))));
        QTRY_VERIFY_WITH_TIMEOUT(!session.isLive(), kWait);
        QVERIFY(!QFile::exists(file));
        QCOMPARE(session.chatterCount(), 0);
        QVERIFY(bus.publish(chatEvent(QStringLiteral("1"))));
        QTRY_COMPARE_WITH_TIMEOUT(first.received.size(), 3, kWait);
        QTest::qWait(50);
        QVERIFY(!QFile::exists(file));
    }

    void restartInSameStream()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("unterordner/twitch-sitzung.json"));
        {
            EventBus bus;
            ChatSession session(&bus, file, 60000); // nur flush beim Beenden schreibt
            FirstMessages first(&bus);
            QVERIFY(bus.publish(streamOnline(QStringLiteral("2026-11-15T17:58:00Z"))));
            QVERIFY(bus.publish(eventFrom(QStringLiteral("chat_message.json"))));
            QVERIFY(bus.publish(eventFrom(QStringLiteral("chat_vip.json"))));
            QTRY_COMPARE_WITH_TIMEOUT(first.received.size(), 2, kWait);
            QVERIFY(!QFile::exists(file));
        } // Beenden des Kerns
        QVERIFY(QFile::exists(file));
        QFile stored(file);
        QVERIFY(stored.open(QIODevice::ReadOnly));
        const QByteArray content = stored.readAll();
        QCOMPARE(QJsonDocument::fromJson(content).object(),
                 (QJsonObject{{"started_at", "2026-11-15T17:58:00Z"},
                              {"user_ids", QJsonArray{"100000101", "100000103"}}}));
        QVERIFY(!content.contains("zuschauer")); // keine Namen
        QVERIFY(!content.contains("VIP"));

        // Neustart, derselbe Stream läuft noch (at_start)
        EventBus bus;
        ChatSession session(&bus, file, 10);
        FirstMessages first(&bus);
        QVERIFY(bus.publish(streamOnline(QStringLiteral("2026-11-15T17:58:00Z"), true)));
        QVERIFY(bus.publish(eventFrom(QStringLiteral("chat_message.json"))));
        QVERIFY(bus.publish(eventFrom(QStringLiteral("chat_moderator.json"))));
        QTRY_COMPARE_WITH_TIMEOUT(first.received.size(), 1, kWait);
        QTest::qWait(50);
        QCOMPARE(first.received.size(), 1);
        QCOMPARE(first.received.first().data.value(QStringLiteral("user_login")).toString(), QStringLiteral("moderatorin"));
        QCOMPARE(session.chatterCount(), 3);
    }

    void restartInNewStream()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("twitch-sitzung.json"));
        QFile stored(file);
        QVERIFY(stored.open(QIODevice::WriteOnly));
        stored.write(R"({"started_at":"2026-11-14T17:00:00Z","user_ids":["100000101"]})");
        stored.close();

        EventBus bus;
        ChatSession session(&bus, file, 10);
        FirstMessages first(&bus);
        QVERIFY(bus.publish(streamOnline(QStringLiteral("2026-11-15T17:58:00Z"), true)));
        QVERIFY(bus.publish(eventFrom(QStringLiteral("chat_message.json"))));
        QTRY_COMPARE_WITH_TIMEOUT(first.received.size(), 1, kWait);
        QCOMPARE(session.chatterCount(), 1);
        // Die Datei gehört jetzt zum neuen Stream.
        const auto storedStart = [&file] {
            QFile f(file);
            return f.open(QIODevice::ReadOnly)
                       ? QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("started_at")).toString()
                       : QString();
        };
        QTRY_COMPARE_WITH_TIMEOUT(storedStart(), QStringLiteral("2026-11-15T17:58:00Z"), kWait);
    }

    void damagedStateFileIgnored()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("twitch-sitzung.json"));
        QFile stored(file);
        QVERIFY(stored.open(QIODevice::WriteOnly));
        stored.write("kein json");
        stored.close();
        EventBus bus;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("ohne started_at, nicht übernommen")));
        ChatSession session(&bus, file, 10);
        FirstMessages first(&bus);
        QVERIFY(bus.publish(streamOnline(QStringLiteral("2026-11-15T17:58:00Z"), true)));
        QVERIFY(bus.publish(eventFrom(QStringLiteral("chat_message.json"))));
        QTRY_COMPARE_WITH_TIMEOUT(first.received.size(), 1, kWait);
    }
};

QTEST_GUILESS_MAIN(TestTwitchProtocol)
#include "tst_twitchprotokoll.moc"
