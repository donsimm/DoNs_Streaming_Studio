// Chat senden im Kern ohne Netz (P14 c1a, Plan 4.1): ChatOutbox mit ersetzter Uhr (aktionsuhr.h) und
// ersetztem Senden. Konto und Ersatz, Grenze 1/s und 20 je 30 s, Warteschlange 10, verworfen nach
// 30 s, Kürzen auf 500 ohne ein Zeichen zu zerschneiden, Schutz vor Schleifen, Ergebnis an den Kern.

#include "aktionsuhr.h"
#include "chatoutbox.h"

#include <QTest>

#include <functional>

using namespace dss::actions;

namespace {

const QString kBotId = QStringLiteral("100000002");
const QString kChannelId = QStringLiteral("100000001");

class FakeTransport : public ChatTransport
{
public:
    struct Post
    {
        ChatAccount account;
        QString text;
        QString replyTo;
        qint64 atMs;
        std::function<void(const ChatPostResult &)> done;
    };

    QString botReason;
    QString channelReason;
    bool answerAtOnce = true; // sonst hält der Test die Antwort zurück (answer())
    QString dropCode;         // gesetzt: Twitch sendet nicht
    QList<Post> posts;
    FakeActionClock *clock = nullptr;
    QDateTime start;

    QString unavailableReason(ChatAccount account) const override
    {
        return account == ChatAccount::Bot ? botReason : channelReason;
    }
    QString userId(ChatAccount account) const override
    {
        return account == ChatAccount::Bot ? kBotId : kChannelId;
    }
    void post(ChatAccount account, const QString &text, const QString &replyTo,
              std::function<void(const ChatPostResult &)> done) override
    {
        posts.append({account, text, replyTo, start.msecsTo(clock->now()), done});
        if (answerAtOnce)
            answer(posts.size() - 1);
    }
    void answer(qsizetype i)
    {
        const QString id = QStringLiteral("nachricht-%1").arg(i + 1);
        if (dropCode.isEmpty())
            posts[i].done({true, id, {}, {}});
        else
            posts[i].done({false, id, dropCode, QStringLiteral("von Twitch nicht gesendet: Test")});
    }
};

struct Harness
{
    FakeActionClock clock;
    FakeTransport transport;
    ChatOutbox outbox{&clock, &transport};
    QList<ChatOutcome> outcomes;
    QList<StepResult> results;

    Harness()
    {
        transport.clock = &clock;
        transport.start = clock.now();
        outbox.setResultHandler([this](const ChatOutcome &o) { outcomes.append(o); });
    }
    void send(const QString &text, bool useBot = true, bool fallback = true, const QString &replyTo = {})
    {
        outbox.sendChat({text, useBot, fallback, replyTo}, [this](const StepResult &r) { results.append(r); });
    }
};

} // namespace

class TestChatSenden : public QObject
{
    Q_OBJECT

private slots:
    void truncate_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("expectedLength");
        const QString a499 = QString(499, QLatin1Char('a'));
        QTest::newRow("kurz bleibt") << QStringLiteral("Hallo") << 5;
        QTest::newRow("genau 500") << QString(500, QLatin1Char('a')) << 500;
        QTest::newRow("600 Buchstaben") << QString(600, QLatin1Char('a')) << 500;
        // 😀 = Ersatzpaar (2 UTF-16-Einheiten) über der Grenze: ganz weg
        QTest::newRow("Ersatzpaar") << a499 + QString::fromUtf8("😀") + QStringLiteral("xyz") << 499;
        // e + kombinierender Akut über der Grenze: beide weg
        QTest::newRow("kombinierender Akzent") << a499 + QStringLiteral("é") + QStringLiteral("xyz") << 499;
        // Familie mit ZWJ (👩‍👩‍👧, 8 Einheiten) ab Stelle 495: ganz weg
        QTest::newRow("ZWJ-Folge") << QString(495, QLatin1Char('a')) + QString::fromUtf8("👩‍👩‍👧") + QStringLiteral("xyz") << 495;
        QTest::newRow("Umlaut zählt eins") << QString(600, QChar(0x00FC)) << 500;
    }
    void truncate()
    {
        QFETCH(QString, text);
        QFETCH(int, expectedLength);
        const QString result = truncateChatText(text, 500);
        QCOMPARE(result.size(), expectedLength);
        QVERIFY(text.startsWith(result));
    }

    void accountAndFallback()
    {
        Harness h;
        h.send(QStringLiteral("eins"));
        QCOMPARE(h.transport.posts.size(), 1);
        QCOMPARE(h.transport.posts[0].account, ChatAccount::Bot);
        QVERIFY(h.results[0].ok);
        QCOMPARE(h.results[0].message, QStringLiteral("gesendet als Bot"));

        // Bot nicht angemeldet, Ersatz an: Kanal sendet
        h.transport.botReason = QStringLiteral("Bot nicht angemeldet");
        h.send(QStringLiteral("zwei"), true, true, QStringLiteral("eltern-1"));
        QCOMPARE(h.transport.posts.size(), 2);
        QCOMPARE(h.transport.posts[1].account, ChatAccount::Broadcaster);
        QCOMPARE(h.transport.posts[1].replyTo, QStringLiteral("eltern-1"));
        QCOMPARE(h.results[1].message, QStringLiteral("gesendet als Kanal"));

        // Ersatz aus: Fehler im Lauf, nichts gesendet
        h.send(QStringLiteral("drei"), true, false);
        QCOMPARE(h.transport.posts.size(), 2);
        QVERIFY(!h.results[2].ok);
        QCOMPARE(h.results[2].message, QStringLiteral("Bot nicht angemeldet"));
        QCOMPARE(h.outcomes.last().code, QStringLiteral("unavailable"));
        QCOMPARE(h.outcomes.last().account, ChatAccount::Bot);

        // Beide nicht: beide Gründe
        h.transport.channelReason = QStringLiteral("Kanal nicht angemeldet");
        h.send(QStringLiteral("vier"));
        QVERIFY(!h.results[3].ok);
        QCOMPARE(h.results[3].message, QStringLiteral("Bot nicht angemeldet; Ersatz Kanal: Kanal nicht angemeldet"));

        // Ohne Twitch (Kern ohne DSS_CORE_TWITCH)
        FakeActionClock clock;
        ChatOutbox none(&clock, nullptr);
        QCOMPARE(none.unavailableReason(ChatAccount::Bot), QStringLiteral("Twitch nicht verbunden"));
        StepResult result;
        none.sendChat({QStringLiteral("x"), true, true, {}}, [&](const StepResult &r) { result = r; });
        QVERIFY(!result.ok);
        QCOMPARE(result.message, QStringLiteral("Twitch nicht verbunden"));
    }

    void outcomes()
    {
        Harness h;
        h.send(QString(510, QLatin1Char('b')), true, true, QStringLiteral("eltern-2"));
        QCOMPARE(h.outcomes.size(), 1);
        const ChatOutcome &o = h.outcomes[0];
        QVERIFY(o.sent);
        QCOMPARE(o.senderId, kBotId);
        QCOMPARE(o.messageId, QStringLiteral("nachricht-1"));
        QCOMPARE(o.text.size(), 500);
        QVERIFY(o.truncated);
        QCOMPARE(o.replyTo, QStringLiteral("eltern-2"));
        QCOMPARE(h.results[0].message, QStringLiteral("gesendet als Bot, gekürzt auf 500 Zeichen"));

        // Twitch sendet nicht (drop_reason)
        h.clock.advance(1000);
        h.transport.dropCode = QStringLiteral("automod_held");
        h.send(QStringLiteral("gesperrt"));
        QVERIFY(!h.outcomes[1].sent);
        QCOMPARE(h.outcomes[1].code, QStringLiteral("automod_held"));
        QVERIFY(!h.results[1].ok);
        QCOMPARE(h.results[1].message, QStringLiteral("von Twitch nicht gesendet: Test"));
    }

    // 1 je Sekunde und 20 je 30 s, je Konto getrennt
    void rateLimit()
    {
        Harness h;
        for (int i = 0; i < 3; ++i)
            h.send(QStringLiteral("b%1").arg(i));
        h.send(QStringLiteral("k0"), false);
        QCOMPARE(h.transport.posts.size(), 2); // b0 und k0 sofort (anderes Konto)
        h.clock.advance(999);
        QCOMPARE(h.transport.posts.size(), 2);
        h.clock.advance(1);
        QCOMPARE(h.transport.posts.size(), 3);
        QCOMPARE(h.transport.posts[2].atMs, 1000);
        h.clock.advance(1000);
        QCOMPARE(h.transport.posts.size(), 4);
        QCOMPARE(h.transport.posts[3].text, QStringLiteral("b2"));

        // 20 je 30 s: b0..b2 bei 0, 1, 2 s; weitere 17 im Abstand von 1 s bis 19 s
        for (int i = 3; i < 20; ++i) {
            h.clock.advance(1000);
            h.send(QStringLiteral("b%1").arg(i));
        }
        int bot = 0;
        for (const auto &p : h.transport.posts)
            bot += p.account == ChatAccount::Bot ? 1 : 0;
        QCOMPARE(bot, 20);
        // die 21. wartet, bis die erste (bei 0 s) 30 s alt ist
        h.clock.advance(1000); // 20 s
        h.send(QStringLiteral("b20"));
        h.clock.advance(9999); // 29,999 s
        QCOMPARE(h.transport.posts.last().text, QStringLiteral("b19"));
        h.clock.advance(1);
        QCOMPARE(h.transport.posts.last().text, QStringLiteral("b20"));
        QCOMPARE(h.transport.posts.last().atMs, 30000);
        QVERIFY(h.results.size() == h.transport.posts.size());
    }

    // Warteschlange 10 je Konto; eine Nachricht nach der anderen; älter als 30 s verworfen
    void queueAndExpiry()
    {
        Harness h;
        h.transport.answerAtOnce = false;
        h.send(QStringLiteral("unterwegs"));
        QCOMPARE(h.transport.posts.size(), 1);
        for (int i = 0; i < 10; ++i)
            h.send(QStringLiteral("w%1").arg(i));
        QCOMPARE(h.outbox.waiting(ChatAccount::Bot), 10);
        h.send(QStringLiteral("zuviel"));
        QCOMPARE(h.results.size(), 1);
        QCOMPARE(h.results[0].message, QStringLiteral("Chat-Warteschlange Bot voll (10), verworfen"));
        QCOMPARE(h.outcomes[0].code, QStringLiteral("queue_full"));
        QCOMPARE(h.outcomes[0].text, QStringLiteral("zuviel"));
        // anderes Konto hat eine eigene Warteschlange
        h.send(QStringLiteral("kanal"), false);
        QCOMPARE(h.transport.posts.size(), 2);

        // Die Antwort auf die erste kommt erst nach 31 s: alle zehn sind dann zu alt
        h.clock.advance(31000);
        QCOMPARE(h.transport.posts.size(), 2); // ohne Antwort geht nichts hinaus
        h.transport.answer(0);
        QCOMPARE(h.outbox.waiting(ChatAccount::Bot), 0);
        QCOMPARE(h.transport.posts.size(), 2);
        QCOMPARE(h.results.size(), 12);
        QCOMPARE(h.results.last().message, QStringLiteral("verworfen: wartete länger als 30 s"));
        QCOMPARE(h.outcomes.last().code, QStringLiteral("expired"));

        // Genau 30 s alt geht noch
        h.send(QStringLiteral("alt"));
        QCOMPARE(h.transport.posts.size(), 3);
        h.send(QStringLiteral("grenze"));
        h.clock.advance(30000);
        h.transport.answer(2);
        QCOMPARE(h.transport.posts.size(), 4);
        QCOMPARE(h.transport.posts.last().text, QStringLiteral("grenze"));
    }

    // Konto fällt zwischen Eintreffen und Senden aus: verworfen, kein neuer Ersatz
    void accountLostWhileQueued()
    {
        Harness h;
        h.send(QStringLiteral("a"));
        h.send(QStringLiteral("b"));
        h.transport.botReason = QStringLiteral("Bot nicht angemeldet");
        h.clock.advance(1000);
        QCOMPARE(h.transport.posts.size(), 1);
        QCOMPARE(h.results.size(), 2);
        QCOMPARE(h.results[1].message, QStringLiteral("Bot nicht angemeldet"));
    }

    // Der Lauf sendet im Rückruf gleich wieder (Schritt nach Schritt): Reihenfolge und Grenze bleiben
    void sendFromCallback()
    {
        Harness h;
        int remaining = 3;
        std::function<void()> next = [&] {
            h.outbox.sendChat({QStringLiteral("r%1").arg(remaining), true, true, {}}, [&](const StepResult &) {
                if (--remaining > 0)
                    next();
            });
        };
        next();
        QCOMPARE(h.transport.posts.size(), 1);
        h.clock.advance(1000);
        QCOMPARE(h.transport.posts.size(), 2);
        h.clock.advance(1000);
        QCOMPARE(h.transport.posts.size(), 3);
        QCOMPARE(remaining, 0);
    }

    void loopProtection()
    {
        Harness h;
        h.send(QStringLiteral("!befehl Echo"));
        // message_id aus der Antwort
        QVERIFY(h.outbox.isInternal(QStringLiteral("nachricht-1"), QStringLiteral("900000001"), QStringLiteral("anders")));
        // Absender und Text innerhalb von 10 s
        QVERIFY(h.outbox.isInternal(QStringLiteral("fremd-1"), kBotId, QStringLiteral("!befehl Echo")));
        QVERIFY(!h.outbox.isInternal(QStringLiteral("fremd-2"), kChannelId, QStringLiteral("!befehl Echo")));
        QVERIFY(!h.outbox.isInternal(QStringLiteral("fremd-3"), kBotId, QStringLiteral("!befehl anders")));
        h.clock.advance(10000);
        QVERIFY(h.outbox.isInternal(QStringLiteral("fremd-4"), kBotId, QStringLiteral("!befehl Echo")));
        h.clock.advance(1);
        QVERIFY(!h.outbox.isInternal(QStringLiteral("fremd-5"), kBotId, QStringLiteral("!befehl Echo")));
        QVERIFY(h.outbox.isInternal(QStringLiteral("nachricht-1"), kBotId, QStringLiteral("!befehl Echo")));

        // Echo kommt vor der Antwort von Twitch: Absender und Text greifen schon
        h.transport.answerAtOnce = false;
        h.send(QStringLiteral("frueh"));
        QVERIFY(h.outbox.isInternal(QStringLiteral("unbekannt"), kBotId, QStringLiteral("frueh")));

        // Nur die letzten 100 message_id
        Harness many;
        for (int i = 0; i < 101; ++i) {
            many.send(QStringLiteral("n%1").arg(i));
            many.clock.advance(1000);
            if (i % 20 == 19)
                many.clock.advance(30000);
        }
        QCOMPARE(many.transport.posts.size(), 101);
        QVERIFY(!many.outbox.isInternal(QStringLiteral("nachricht-1"), {}, {}));
        QVERIFY(many.outbox.isInternal(QStringLiteral("nachricht-2"), {}, {}));
        QVERIFY(many.outbox.isInternal(QStringLiteral("nachricht-101"), {}, {}));
    }
};

QTEST_GUILESS_MAIN(TestChatSenden)
#include "tst_chatsenden.moc"
