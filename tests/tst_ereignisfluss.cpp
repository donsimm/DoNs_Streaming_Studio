// Ereignisfluss (P13, Plan 1.1 und Commit 2): Reihenfolge, Filter und Vorsilbe, Verlauf mit 200
// Einträgen, Abmelden während der Zustellung, Kontext zerstört, Abonnent in einem anderen Thread,
// publish im Handler, unbekannter Typ, 10 000 Ereignisse mit Zeitmessung. Ohne Qt Gui.

#include "eventbus.h"
#include "eventtypes.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QMutex>
#include <QSet>
#include <QTest>
#include <QThread>

#include <optional>

using namespace dss::events;

namespace {

constexpr int kWait = 5000;

// Gültiges Ereignis: alle Pflichtfelder des Typs, Werte null ausser den angegebenen.
Event makeEvent(const QString &type, const QJsonObject &values = {})
{
    Event event;
    event.type = type;
    const EventType *spec = findEventType(type);
    if (spec) {
        for (const EventField &field : spec->fields)
            event.data.insert(QString::fromLatin1(field.name), QJsonValue::Null);
    }
    for (auto it = values.begin(); it != values.end(); ++it)
        event.data.insert(it.key(), it.value());
    return event;
}

Event chatMessage(int number)
{
    return makeEvent(QStringLiteral("twitch.chat_message"),
                     {{QStringLiteral("message_id"), QString::number(number)},
                      {QStringLiteral("text"), QStringLiteral("Nachricht %1").arg(number)}});
}

} // namespace

class TestEventBus : public QObject
{
    Q_OBJECT

private slots:
    void typeListIsConsistent()
    {
        // Plan 1.3: 12 Typen von Twitch und obs.scene_changed; P14 b3: drei Typen actions.run_*;
        // P14 c1a: twitch.chat_sent und twitch.chat_dropped
        const QStringList names = eventTypeNames();
        QCOMPARE(names.size(), 18);
        QCOMPARE(QSet<QString>(names.begin(), names.end()).size(), names.size());
        for (const EventType &type : kEventTypes) {
            const QString name = QString::fromLatin1(type.type);
            QVERIFY2(name.startsWith(QLatin1String(type.origin) + QLatin1Char('.')), qPrintable(name));
            QSet<QString> fields;
            QSet<QString> arguments;
            for (const EventField &field : type.fields) {
                fields.insert(QString::fromLatin1(field.name));
                arguments.insert(QString::fromLatin1(field.argument));
                QVERIFY2(!QString::fromLatin1(field.argument).isEmpty(), field.name);
            }
            QVERIFY2(fields.size() == qsizetype(type.fields.size()), qPrintable(name)); // keine doppelten Felder
            QVERIFY2(arguments.size() == qsizetype(type.fields.size()), qPrintable(name)); // keine doppelten Argumente
        }
        // Plan 1.4: belegte Namen
        const EventType *chat = findEventType(QStringLiteral("twitch.chat_message"));
        QVERIFY(chat);
        QHash<QString, QString> argument;
        for (const EventField &field : chat->fields)
            argument.insert(QString::fromLatin1(field.name), QString::fromLatin1(field.argument));
        QCOMPARE(argument.value(QStringLiteral("user_name")), QStringLiteral("user"));
        QCOMPARE(argument.value(QStringLiteral("user_login")), QStringLiteral("userName"));
        QCOMPARE(argument.value(QStringLiteral("message_id")), QStringLiteral("msgId"));
        QCOMPARE(argument.value(QStringLiteral("role")), QStringLiteral("role"));
        // first_message hat alle Felder von chat_message und first_ever
        const EventType *first = findEventType(QStringLiteral("twitch.first_message"));
        QVERIFY(first);
        QCOMPARE(first->fields.size(), chat->fields.size() + 1);
        for (size_t i = 0; i < chat->fields.size(); ++i) {
            QCOMPARE(QLatin1String(first->fields[i].name), QLatin1String(chat->fields[i].name));
            QCOMPARE(QLatin1String(first->fields[i].argument), QLatin1String(chat->fields[i].argument));
        }
        QCOMPARE(QLatin1String(first->fields.back().name), QLatin1String("first_ever"));
        // Schritt 0: belegte Namen bei Sub, Geschenk und Stream
        const auto argumentsOf = [](const char *type) {
            QHash<QString, QString> result;
            for (const EventField &field : findEventType(QLatin1String(type))->fields)
                result.insert(QString::fromLatin1(field.name), QString::fromLatin1(field.argument));
            return result;
        };
        QCOMPARE(argument.value(QStringLiteral("user_id")), QStringLiteral("userId"));
        QCOMPARE(argument.value(QStringLiteral("is_highlight")), QStringLiteral("isHighlight"));
        const auto subscribe = argumentsOf("twitch.subscribe");
        QCOMPARE(subscribe.value(QStringLiteral("tier")), QStringLiteral("subscriptionTier"));
        QCOMPARE(subscribe.value(QStringLiteral("tier_name")), QStringLiteral("tier"));
        const auto resub = argumentsOf("twitch.resubscribe");
        QCOMPARE(resub.value(QStringLiteral("months")), QStringLiteral("cumulative"));
        QCOMPARE(resub.value(QStringLiteral("streak_months")), QStringLiteral("monthStreak"));
        QCOMPARE(resub.value(QStringLiteral("duration_months")), QStringLiteral("multiMonthDuration"));
        const auto bomb = argumentsOf("twitch.gift_bomb");
        QCOMPARE(bomb.value(QStringLiteral("total")), QStringLiteral("gifts"));
        QCOMPARE(bomb.value(QStringLiteral("cumulative_total")), QStringLiteral("totalGifts"));
        const auto gift = argumentsOf("twitch.gift_sub");
        QCOMPARE(gift.value(QStringLiteral("user_name")), QStringLiteral("user")); // Schenkender
        QCOMPARE(gift.value(QStringLiteral("recipient_user_name")), QStringLiteral("recipientUser"));
        QCOMPARE(gift.value(QStringLiteral("recipient_user_id")), QStringLiteral("recipientId"));
        QCOMPARE(argumentsOf("twitch.stream_online").value(QStringLiteral("started_at")), QStringLiteral("startedAt"));
        QCOMPARE(argumentsOf("twitch.stream_offline").value(QStringLiteral("ended_at")), QStringLiteral("endedAt"));
        QVERIFY(!findEventType(QStringLiteral("twitch.reward_redeemed"))->trigger);
        QVERIFY(!findEventType(QStringLiteral("obs.scene_changed"))->trigger);
        QVERIFY(findEventType(QStringLiteral("twitch.follow"))->trigger);
    }

    void filters()
    {
        QVERIFY(matchesFilter(QStringLiteral("twitch.follow"), QStringLiteral("twitch.follow")));
        QVERIFY(matchesFilter(QStringLiteral("twitch.follow"), QStringLiteral("twitch.")));
        QVERIFY(!matchesFilter(QStringLiteral("twitch.follow"), QStringLiteral("twitch")));
        QVERIFY(!matchesFilter(QStringLiteral("twitch.follow"), QStringLiteral("twitch.fol")));
        QVERIFY(!matchesFilter(QStringLiteral("obs.scene_changed"), QStringLiteral("twitch.")));
        QVERIFY(!matchesFilters(QStringLiteral("twitch.follow"), {}));
        QVERIFY(isValidFilter(QStringLiteral("twitch.")));
        QVERIFY(isValidFilter(QStringLiteral("obs.scene_changed")));
        QVERIFY(!isValidFilter(QStringLiteral("twitch")));
        QVERIFY(!isValidFilter(QStringLiteral(".")));
        QVERIFY(!isValidFilter(QStringLiteral("youtube.")));
        QVERIFY(!isValidFilter(QStringLiteral("twitch.gibtsnicht")));
        QVERIFY(!isValidFilter(QString()));
    }

    void orderAndDeferredDelivery()
    {
        EventBus bus;
        QObject context;
        QStringList received;
        const Subscription sub = bus.subscribe(&context, {QStringLiteral("twitch.chat_message")},
                                               [&received](const Event &e) {
                                                   received.append(e.data.value(QStringLiteral("message_id")).toString());
                                               });
        for (int i = 0; i < 5; ++i)
            QVERIFY(bus.publish(chatMessage(i)));
        // Nie im Aufruf von publish
        QVERIFY(received.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(received.size(), 5, kWait);
        QCOMPARE(received, (QStringList{"0", "1", "2", "3", "4"}));
    }

    void fieldsFilledByBus()
    {
        EventBus bus;
        QObject context;
        std::optional<Event> received;
        const Subscription sub = bus.subscribe(&context, {QStringLiteral("obs.")},
                                               [&received](const Event &e) { received = e; });
        const QDateTime before = QDateTime::currentDateTimeUtc();
        QVERIFY(bus.publish(makeEvent(QStringLiteral("obs.scene_changed"),
                                      {{QStringLiteral("scene_uuid"), QStringLiteral("u")},
                                       {QStringLiteral("scene_name"), QStringLiteral("Szene")}})));
        QTRY_VERIFY_WITH_TIMEOUT(received.has_value(), kWait);
        QVERIFY(!received->id.isNull());
        QCOMPARE(received->origin, QStringLiteral("obs"));
        QCOMPARE(received->time.timeSpec(), Qt::UTC);
        QVERIFY(received->time >= before.addMSecs(-1));
        QCOMPARE(received->data.value(QStringLiteral("scene_name")).toString(), QStringLiteral("Szene"));

        // Zeit der Herkunft bleibt (in UTC)
        Event withTime = makeEvent(QStringLiteral("twitch.stream_offline"));
        withTime.time = QDateTime(QDate(2026, 11, 15), QTime(12, 0), QTimeZone::fromSecondsAheadOfUtc(3600));
        withTime.originId = QStringLiteral("abc");
        QVERIFY(bus.publish(withTime));
        const QList<Event> history = bus.history({QStringLiteral("twitch.stream_offline")});
        QCOMPARE(history.size(), 1);
        QCOMPARE(history.first().time, QDateTime(QDate(2026, 11, 15), QTime(11, 0), QTimeZone::UTC));
        QCOMPARE(history.first().originId, QStringLiteral("abc"));
    }

    void filterAndPrefix()
    {
        EventBus bus;
        QObject context;
        QStringList prefix;
        QStringList exact;
        QStringList none;
        const Subscription a = bus.subscribe(&context, {QStringLiteral("twitch.")},
                                             [&prefix](const Event &e) { prefix.append(e.type); });
        const Subscription b = bus.subscribe(&context, {QStringLiteral("twitch.follow"), QStringLiteral("obs.scene_changed")},
                                             [&exact](const Event &e) { exact.append(e.type); });
        const Subscription c = bus.subscribe(&context, {}, [&none](const Event &e) { none.append(e.type); });
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.follow"))));
        QVERIFY(bus.publish(makeEvent(QStringLiteral("obs.scene_changed"))));
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.raid"))));
        QTRY_COMPARE_WITH_TIMEOUT(prefix.size(), 2, kWait);
        QTRY_COMPARE_WITH_TIMEOUT(exact.size(), 2, kWait);
        QCoreApplication::processEvents();
        QCOMPARE(prefix, (QStringList{"twitch.follow", "twitch.raid"}));
        QCOMPARE(exact, (QStringList{"twitch.follow", "obs.scene_changed"}));
        QVERIFY(none.isEmpty());
    }

    void changeFilters()
    {
        EventBus bus;
        QObject context;
        QStringList received;
        Subscription sub = bus.subscribe(&context, {QStringLiteral("twitch.follow")},
                                         [&received](const Event &e) { received.append(e.type); });
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.raid"))));
        sub.setFilters({QStringLiteral("twitch.raid")});
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.raid"))));
        QTRY_COMPARE_WITH_TIMEOUT(received.size(), 1, kWait);
        sub.setFilters({});
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.raid"))));
        QCoreApplication::processEvents();
        QCOMPARE(received.size(), 1);
    }

    void historyKeeps200()
    {
        EventBus bus;
        for (int i = 0; i < 250; ++i)
            QVERIFY(bus.publish(chatMessage(i)));
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.follow"))));
        const QList<Event> all = bus.history({QStringLiteral("twitch.")});
        QCOMPARE(all.size(), EventBus::kHistorySize);
        QCOMPARE(all.first().data.value(QStringLiteral("message_id")).toString(), QStringLiteral("51"));
        QCOMPARE(all.last().type, QStringLiteral("twitch.follow"));
        QCOMPARE(bus.history({QStringLiteral("twitch.follow")}).size(), 1);
        QCOMPARE(bus.history({QStringLiteral("twitch.chat_message")}).size(), 199);
        QVERIFY(bus.history({}).isEmpty());
        QVERIFY(bus.history({QStringLiteral("obs.")}).isEmpty());
    }

    void unsubscribeDuringDelivery()
    {
        EventBus bus;
        QObject context;
        QStringList first;
        QStringList second;
        Subscription b;
        Subscription a = bus.subscribe(&context, {QStringLiteral("twitch.chat_message")},
                                       [&first, &b](const Event &e) {
                                           first.append(e.data.value(QStringLiteral("message_id")).toString());
                                           b.unsubscribe(); // das andere Abo, mitten in der Zustellung
                                       });
        b = bus.subscribe(&context, {QStringLiteral("twitch.chat_message")}, [&second](const Event &e) {
            second.append(e.data.value(QStringLiteral("message_id")).toString());
        });
        QVERIFY(bus.publish(chatMessage(1)));
        QVERIFY(bus.publish(chatMessage(2)));
        QTRY_COMPARE_WITH_TIMEOUT(first.size(), 2, kWait);
        QCoreApplication::processEvents();
        // Zustellung an a für 1 kommt vor der an b für 1: b ist danach abgemeldet, auch was schon
        // unterwegs war, kommt nicht mehr an.
        QVERIFY(second.isEmpty());
        QVERIFY(!b.isActive());
        QVERIFY(a.isActive());

        // Sich selbst im Handler abmelden
        int self = 0;
        Subscription own;
        own = bus.subscribe(&context, {QStringLiteral("twitch.raid")}, [&self, &own](const Event &) {
            ++self;
            own.unsubscribe();
        });
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.raid"))));
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.raid"))));
        QTRY_COMPARE_WITH_TIMEOUT(self, 1, kWait);
        QCoreApplication::processEvents();
        QCOMPARE(self, 1);
    }

    void subscriptionDestroyedBeforeDelivery()
    {
        EventBus bus;
        QObject context;
        int received = 0;
        {
            const Subscription sub = bus.subscribe(&context, {QStringLiteral("twitch.")},
                                                   [&received](const Event &) { ++received; });
            QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.follow"))));
        }
        QCoreApplication::processEvents();
        QCOMPARE(received, 0);
        // Abgemeldete Abos fallen beim nächsten publish weg
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.follow"))));
        QCoreApplication::processEvents();
        QCOMPARE(received, 0);
    }

    void contextDestroyed()
    {
        EventBus bus;
        int received = 0;
        auto *context = new QObject;
        const Subscription sub = bus.subscribe(context, {QStringLiteral("twitch.")},
                                               [&received](const Event &) { ++received; });
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.follow"))));
        delete context; // Zustellung unterwegs
        QVERIFY(!sub.isActive());
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.follow"))));
        QCoreApplication::processEvents();
        QCOMPARE(received, 0);
    }

    void busDestroyedBeforeSubscription()
    {
        int received = 0;
        QObject context;
        Subscription sub;
        {
            EventBus bus;
            sub = bus.subscribe(&context, {QStringLiteral("twitch.")}, [&received](const Event &) { ++received; });
            QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.follow"))));
        }
        QVERIFY(!sub.isActive());
        QCoreApplication::processEvents();
        QCOMPARE(received, 0);
        sub.unsubscribe(); // kein Absturz
    }

    void subscriberInOtherThread()
    {
        EventBus bus;
        QThread thread;
        thread.start();
        auto *context = new QObject;
        context->moveToThread(&thread);
        QMutex mutex;
        QList<int> received;
        QSet<QThread *> threads;
        Subscription sub = bus.subscribe(context, {QStringLiteral("twitch.chat_message")},
                                         [&](const Event &e) {
                                             QMutexLocker locker(&mutex);
                                             received.append(e.data.value(QStringLiteral("message_id")).toString().toInt());
                                             threads.insert(QThread::currentThread());
                                         });
        constexpr int kCount = 1000;
        for (int i = 0; i < kCount; ++i)
            QVERIFY(bus.publish(chatMessage(i)));
        QTRY_VERIFY_WITH_TIMEOUT(([&] { QMutexLocker l(&mutex); return received.size() == kCount; })(), kWait);
        {
            QMutexLocker locker(&mutex);
            QCOMPARE(threads, QSet<QThread *>{&thread});
            for (int i = 0; i < kCount; ++i)
                QCOMPARE(received.at(i), i);
        }
        // Kontext im anderen Thread zerstören, während weiter veröffentlicht wird: kein Absturz,
        // danach nichts mehr.
        for (int i = 0; i < 200; ++i)
            QVERIFY(bus.publish(chatMessage(kCount + i)));
        QMetaObject::invokeMethod(context, [context] { delete context; }, Qt::BlockingQueuedConnection);
        QVERIFY(!sub.isActive());
        for (int i = 0; i < 200; ++i)
            QVERIFY(bus.publish(chatMessage(kCount + 200 + i)));
        thread.quit();
        QVERIFY(thread.wait(kWait));
        QMutexLocker locker(&mutex);
        QVERIFY(received.size() <= kCount + 200);
        for (int i = 0; i < received.size(); ++i)
            QCOMPARE(received.at(i), i);
    }

    void publishInHandler()
    {
        EventBus bus;
        QObject context;
        QStringList order;
        int depth = 0;
        int maxDepth = 0;
        const Subscription sub = bus.subscribe(
            &context, {QStringLiteral("twitch.")}, [&](const Event &e) {
                ++depth;
                maxDepth = std::max(maxDepth, depth);
                order.append(e.type);
                if (e.type == QLatin1String("twitch.follow")) {
                    QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.raid"))));
                    order.append(QStringLiteral("nach publish"));
                }
                --depth;
            });
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.follow"))));
        QVERIFY(bus.publish(makeEvent(QStringLiteral("twitch.cheer"))));
        QTRY_COMPARE_WITH_TIMEOUT(order.size(), 4, kWait);
        QCOMPARE(maxDepth, 1);
        // Das im Handler veröffentlichte Ereignis kommt nach den schon wartenden.
        QCOMPARE(order, (QStringList{"twitch.follow", "nach publish", "twitch.cheer", "twitch.raid"}));
    }

    void rejectsInvalidEvents_data()
    {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("origin");
        QTest::addColumn<QJsonObject>("data");
        QTest::addColumn<QString>("reason");
        QTest::newRow("unbekannter Typ") << "twitch.gibtsnicht" << "" << QJsonObject() << "unbekannter Typ";
        QTest::newRow("leerer Typ") << "" << "" << QJsonObject() << "unbekannter Typ";
        QJsonObject follow = makeEvent(QStringLiteral("twitch.follow")).data;
        QTest::newRow("falsche Herkunft") << "twitch.follow" << "obs" << follow
                                          << "Herkunft «obs» statt «twitch»";
        QJsonObject missing = follow;
        missing.remove(QStringLiteral("followed_at"));
        QTest::newRow("Feld fehlt") << "twitch.follow" << "" << missing << "Feld «followed_at» fehlt";
        QJsonObject extra = follow;
        extra.insert(QStringLiteral("access_token"), QStringLiteral("x"));
        QTest::newRow("Feld zu viel") << "twitch.follow" << "" << extra
                                      << "Feld «access_token» gehört nicht zum Typ";
    }

    void rejectsInvalidEvents()
    {
        QFETCH(QString, type);
        QFETCH(QString, origin);
        QFETCH(QJsonObject, data);
        QFETCH(QString, reason);
        EventBus bus;
        QObject context;
        int received = 0;
        const Subscription sub = bus.subscribe(&context, {QStringLiteral("twitch."), QStringLiteral("obs.")},
                                               [&received](const Event &) { ++received; });
        Event event;
        event.type = type;
        event.origin = origin;
        event.data = data;
        QCOMPARE(EventBus::validate(event), reason);
        QTest::ignoreMessage(QtWarningMsg,
                             qPrintable(QStringLiteral("Ereignisfluss: Ereignis «%1» abgewiesen: %2").arg(type, reason)));
        QVERIFY(!bus.publish(event));
        QCOMPARE(bus.rejectedCount(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(received, 0);
        QVERIFY(bus.history({QStringLiteral("twitch."), QStringLiteral("obs.")}).isEmpty());
    }

    void jsonRoundTrip()
    {
        EventBus bus;
        Event event = chatMessage(7);
        event.data.insert(QStringLiteral("badges"), QJsonArray{QJsonObject{{"set_id", "vip"}, {"id", "1"}}});
        event.originId = QStringLiteral("abc-123");
        QVERIFY(bus.publish(event));
        const Event stored = bus.history({QStringLiteral("twitch.chat_message")}).first();
        const QJsonObject json = eventToJson(stored);
        QCOMPARE(json.value(QStringLiteral("type")).toString(), QStringLiteral("twitch.chat_message"));
        QVERIFY(json.value(QStringLiteral("time")).toString().endsWith(QLatin1Char('Z')));
        Event back;
        QString error;
        QVERIFY2(eventFromJson(json, &back, &error), qPrintable(error));
        QCOMPARE(back, stored);
        QJsonObject broken = json;
        broken.remove(QStringLiteral("data"));
        QVERIFY(!eventFromJson(broken, &back, &error));
        broken = json;
        broken.insert(QStringLiteral("time"), QStringLiteral("gestern"));
        QVERIFY(!eventFromJson(broken, &back, &error));
    }

    // 10 000 Ereignisse an drei Abonnenten, Zeit von publish bis zur letzten Zustellung.
    void tenThousandEvents()
    {
        EventBus bus;
        QObject context;
        int prefix = 0;
        int exact = 0;
        int other = 0;
        const Subscription a = bus.subscribe(&context, {QStringLiteral("twitch.")}, [&prefix](const Event &) { ++prefix; });
        const Subscription b = bus.subscribe(&context, {QStringLiteral("twitch.chat_message")}, [&exact](const Event &) { ++exact; });
        const Subscription c = bus.subscribe(&context, {QStringLiteral("obs.")}, [&other](const Event &) { ++other; });
        constexpr int kCount = 10000;
        QList<Event> events;
        events.reserve(kCount);
        for (int i = 0; i < kCount; ++i)
            events.append(chatMessage(i));
        QElapsedTimer timer;
        timer.start();
        for (const Event &event : std::as_const(events))
            QVERIFY(bus.publish(event));
        const qint64 publishMs = timer.elapsed();
        QTRY_COMPARE_WITH_TIMEOUT(exact, kCount, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(prefix, kCount, 10000);
        const qint64 totalMs = timer.elapsed();
        QCOMPARE(other, 0);
        qInfo("10000 Ereignisse: publish %lld ms, bis zur letzten Zustellung %lld ms", publishMs, totalMs);
        // Grenze grosszügig (Last durch andere Bauten): 50 Chat-Nachrichten pro Sekunde (Lastprobe
        // im Plan) brauchen nur einen Bruchteil davon.
        QVERIFY2(totalMs < 3000, qPrintable(QString::number(totalMs)));
    }
};

QTEST_GUILESS_MAIN(TestEventBus)
#include "tst_ereignisfluss.moc"
