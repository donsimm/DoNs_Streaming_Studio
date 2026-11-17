// P14 b2: Auslöser ohne Netz (triggerevent.h, triggerdispatcher.h): Argumente aus Ereignissen nach
// Schritt 0, Filter, Befehle mit Rechten, Abklingzeiten und Zählern, Zeitnachrichten, Hauptschalter
// und «stumm». Ohne Qt Gui, ohne Netz, Uhr ersetzt; Ereignisse und Namen erfunden (Plan 2.10).
// Ereignistypen und Felder nach Plan P13 1.3.

#include "actionengine.h"
#include "aktionsuhr.h"
#include "eventtypes.h"
#include "fieldspec.h"
#include "steptypes.h"
#include "triggerdispatcher.h"
#include "triggertypes.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace dss::actions;

namespace {

const QString kBroadcaster = QStringLiteral("100000000");

TriggerEvent ev(const char *type, const QJsonObject &data = {})
{
    return {QString::fromLatin1(type), QDateTime(QDate(2026, 11, 15), QTime(20, 0), QTimeZone::UTC),
            QStringLiteral("n-1"), data};
}

QJsonObject user(const QString &id = QStringLiteral("100000001"), const QString &name = QStringLiteral("Testzuschauer"))
{
    return {{"user_id", id}, {"user_login", name.toLower()}, {"user_name", name}};
}

QJsonObject merged(QJsonObject a, const QJsonObject &b)
{
    for (auto it = b.begin(); it != b.end(); ++it)
        a.insert(it.key(), it.value());
    return a;
}

// Chat-Nachricht mit allen Pflichtfeldern aus P13 1.3
QJsonObject chatData(const QString &text, const QJsonObject &extra = {})
{
    return merged(merged(user(), {{"message_id", "m-1"},
                                  {"text", text},
                                  {"fragments", QJsonArray()},
                                  {"color", "#FF0000"},
                                  {"badges", QJsonArray()},
                                  {"role", 1},
                                  {"is_subscriber", false},
                                  {"is_vip", false},
                                  {"is_moderator", false},
                                  {"is_broadcaster", false},
                                  {"is_bot_account", false},
                                  {"bits", 0},
                                  {"reply_to", QJsonValue()},
                                  {"reward_id", QJsonValue()},
                                  {"message_type", "text"},
                                  {"is_highlight", false},
                                  {"source_broadcaster_id", QJsonValue()}}),
                  extra);
}

Trigger trig(const char *type, const QJsonObject &fields = {}, bool enabled = true)
{
    Trigger t = newTrigger(QString::fromLatin1(type));
    for (auto it = fields.begin(); it != fields.end(); ++it)
        t.fields.insert(it.key(), it.value());
    t.enabled = enabled;
    return t;
}

Action act(const QString &name, const QList<Trigger> &triggers, const QList<Step> &steps = {})
{
    Action a;
    a.id = QUuid::createUuid();
    a.name = name;
    a.triggers = triggers;
    a.steps = steps;
    return a;
}

Command cmd(const QString &name, const QStringList &commands)
{
    Command c;
    c.id = QUuid::createUuid();
    c.name = name;
    c.commands = commands;
    return c;
}

Trigger commandTrigger(const Command &c)
{
    return trig(kTriggerCommand, {{"command_id", uuidText(c.id)}});
}

struct Env
{
    FakeActionClock clock;
    VariableStore variables;
    TriggerDispatcher dispatcher{&clock, &variables};
    ActionsConfig config;
    QList<TriggerResult> timerResults;

    Env()
    {
        config.enabled = true;
        dispatcher.setTimerHandler([this](const TriggerResult &r) { timerResults.append(r); });
    }
    QUuid add(const Action &a)
    {
        config.actions.append(a);
        dispatcher.setConfig(config);
        return a.id;
    }
    void add(const Command &c)
    {
        config.commands.append(c);
        dispatcher.setConfig(config);
    }
    void apply() { dispatcher.setConfig(config); }
    TriggerResult send(const TriggerEvent &e, bool internal = false)
    {
        return dispatcher.handleEvent(e, {kBroadcaster, internal});
    }
    TriggerResult chat(const QString &text, const QJsonObject &extra = {}, bool internal = false)
    {
        return send(ev(kTriggerChatMessage, chatData(text, extra)), internal);
    }
};

QList<QUuid> hitActions(const TriggerResult &r)
{
    QList<QUuid> ids;
    for (const TriggerHit &h : r.hits)
        ids.append(h.actionId);
    return ids;
}

QStringList reasons(const TriggerResult &r)
{
    QStringList list;
    for (const TriggerRejection &x : r.rejections)
        list.append(x.reason);
    return list;
}

} // namespace

class TestTriggers : public QObject
{
    Q_OBJECT

private slots:
    // Argumente je Ereignis nach Schritt 0 (C 19); jedes Argument steht in der Tabelle des Auslösers
    void eventArguments_data()
    {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QJsonObject>("data");
        QTest::addColumn<QJsonObject>("expected"); // Auszug
        QTest::addColumn<QStringList>("absent");

        QTest::newRow("Chat") << "twitch.chat_message"
                              << chatData(QStringLiteral("Hallo"), {{"role", 3}, {"is_moderator", true},
                                                                    {"message_type", "channel_points_highlighted"},
                                                                    {"is_highlight", true}})
                              << QJsonObject{{"user", "Testzuschauer"}, {"userName", "testzuschauer"},
                                             {"userId", "100000001"}, {"msgId", "m-1"}, {"message", "Hallo"},
                                             {"role", 3}, {"isModerator", true}, {"isSubscribed", false},
                                             {"isHighlight", true}, {"firstMessage", false}}
                              << QStringList{QStringLiteral("isHighlighted")};
        QTest::newRow("Erste Nachricht")
            << "twitch.first_message"
            << merged(chatData(QStringLiteral("Hi"), {{"message_type", "user_intro"}}), {{"first_ever", true}})
            << QJsonObject{{"message", "Hi"}, {"firstMessage", true}, {"firstEver", true}} << QStringList();
        QTest::newRow("Follow") << "twitch.follow" << merged(user(), {{"followed_at", "2026-11-15T20:00:00Z"}})
                                << QJsonObject{{"user", "Testzuschauer"}, {"userId", "100000001"}} << QStringList();
        QTest::newRow("Abo") << "twitch.subscribe"
                             << merged(user(), {{"tier", 2000}, {"tier_name", "tier 2"}, {"is_gift", false}})
                             << QJsonObject{{"tier", "tier 2"}, {"subscriptionTier", 2000}, {"isGift", false}}
                             << QStringList();
        QTest::newRow("Resub geteilt")
            << "twitch.resubscribe"
            << merged(user(), {{"tier", 1000}, {"tier_name", "tier 1"}, {"text", "Danke"}, {"months", 14},
                               {"streak_months", 3}, {"duration_months", 6}})
            << QJsonObject{{"tier", "tier 1"}, {"subscriptionTier", 1000}, {"message", "Danke"}, {"cumulative", 14},
                           {"monthStreak", 3}, {"streakShared", true}, {"multiMonthDuration", 6},
                           {"isMultiMonth", true}}
            << QStringList();
        QTest::newRow("Resub nicht geteilt")
            << "twitch.resubscribe"
            << merged(user(), {{"tier", 3000}, {"tier_name", "tier 3"}, {"text", ""}, {"months", 2},
                               {"streak_months", QJsonValue()}, {"duration_months", 1}})
            << QJsonObject{{"tier", "tier 3"}, {"monthStreak", 0}, {"streakShared", false}, {"isMultiMonth", false}}
            << QStringList();
        // P13: Beschenkter in recipient_user_*, user_* (Schenkender) null
        QTest::newRow("Geschenk-Abo")
            << "twitch.gift_sub"
            << QJsonObject{{"user_id", QJsonValue()}, {"user_login", QJsonValue()}, {"user_name", QJsonValue()},
                           {"recipient_user_id", "100000001"}, {"recipient_user_login", "testzuschauer"},
                           {"recipient_user_name", "Testzuschauer"}, {"tier", 1000}, {"tier_name", "tier 1"}}
            << QJsonObject{{"recipientUser", "Testzuschauer"}, {"recipientUserName", "testzuschauer"},
                           {"recipientId", "100000001"}, {"tier", "tier 1"}}
            << QStringList{QStringLiteral("user"), QStringLiteral("userId")};
        QTest::newRow("Geschenk-Bündel anonym")
            << "twitch.gift_bomb"
            << QJsonObject{{"user_id", QJsonValue()}, {"user_login", QJsonValue()}, {"user_name", QJsonValue()},
                           {"is_anonymous", true}, {"total", 5}, {"tier", 1000}, {"tier_name", "tier 1"},
                           {"cumulative_total", QJsonValue()}}
            << QJsonObject{{"gifts", 5}, {"anonymous", true}, {"tier", "tier 1"}}
            << QStringList{QStringLiteral("user"), QStringLiteral("totalGifts")};
        QTest::newRow("Geschenk-Bündel")
            << "twitch.gift_bomb"
            << merged(user(), {{"is_anonymous", false}, {"total", 10}, {"tier", 1000}, {"tier_name", "tier 1"},
                               {"cumulative_total", 42}})
            << QJsonObject{{"user", "Testzuschauer"}, {"gifts", 10}, {"totalGifts", 42}, {"tier", "tier 1"},
                           {"subscriptionTier", 1000}}
            << QStringList();
        // anonymous: Name aus P13 (P14 c6, vorher isAnonymous)
        QTest::newRow("Cheer") << "twitch.cheer"
                               << merged(user(), {{"is_anonymous", false}, {"bits", 100}, {"text", "Cheer100 hallo"}})
                               << QJsonObject{{"bits", 100}, {"message", "Cheer100 hallo"}, {"anonymous", false}}
                               << QStringList{QStringLiteral("isAnonymous")};
        QTest::newRow("Raid") << "twitch.raid"
                              << QJsonObject{{"from_user_id", "100000002"}, {"from_user_login", "testkanal"},
                                             {"from_user_name", "Testkanal"}, {"viewers", 12}}
                              << QJsonObject{{"user", "Testkanal"}, {"userName", "testkanal"}, {"userId", "100000002"},
                                             {"viewers", 12}}
                              << QStringList();
        QTest::newRow("Stream online") << "twitch.stream_online"
                                       << QJsonObject{{"started_at", "2026-11-15T19:00:00Z"}, {"kind", "live"},
                                                      {"at_start", false}}
                                       << QJsonObject{{"startedAt", "2026-11-15T19:00:00Z"}}
                                       << QStringList{QStringLiteral("game")};
        QTest::newRow("Stream offline") << "twitch.stream_offline"
                                        << QJsonObject{{"ended_at", "2026-11-15T20:00:00.000Z"}}
                                        << QJsonObject{{"endedAt", "2026-11-15T20:00:00.000Z"}} << QStringList();
    }
    void eventArguments()
    {
        QFETCH(QString, type);
        QFETCH(QJsonObject, data);
        QFETCH(QJsonObject, expected);
        QFETCH(QStringList, absent);
        TriggerEvent e = ev("", data);
        e.type = type;
        const Arguments args = dss::actions::eventArguments(e);
        for (auto it = expected.begin(); it != expected.end(); ++it)
            QVERIFY2(args.value(it.key()) == it.value(), qPrintable(it.key()));
        for (const QString &name : absent)
            QVERIFY2(!args.contains(name), qPrintable(name));
        const TriggerTypeInfo *info = triggerType(type);
        QVERIFY(info);
        for (auto it = args.cbegin(); it != args.cend(); ++it)
            QVERIFY2(info->arguments.contains(it.key()), qPrintable(it.key()));
    }

    // P14 c6: Argumentnamen nur aus der Typenliste von P13. Je Ereignis mit allen Feldern gesetzt
    // trägt ein Lauf genau die Argumente aus eventtypes.h; der Auslöser nennt dieselben.
    void eventArgumentsFromEventTypes()
    {
        for (const dss::events::EventType &type : dss::events::kEventTypes) {
            const QString name = QString::fromLatin1(type.type);
            QJsonObject data;
            for (const dss::events::EventField &field : type.fields)
                data.insert(QLatin1String(field.name), 1);
            TriggerEvent e = ev("", data);
            e.type = name;
            const Arguments args = dss::actions::eventArguments(e);
            const QStringList expected = dss::events::eventArgumentNames(name);
            QVERIFY2(!expected.isEmpty(), qPrintable(name));
            QCOMPARE(QSet<QString>(expected.cbegin(), expected.cend()).size(), expected.size());
            QStringList keys = args.keys();
            keys.sort();
            QStringList sorted = expected;
            sorted.sort();
            QCOMPARE(keys, sorted);
            if (!type.trigger)
                continue;
            const TriggerTypeInfo *info = triggerType(name);
            QVERIFY2(info, qPrintable(name));
            QCOMPARE(info->arguments, expected);
        }
        const QStringList command = triggerType(QStringLiteral("command"))->arguments;
        for (const QString &arg : dss::events::eventArgumentNames(QStringLiteral("twitch.chat_message")))
            QVERIFY2(command.contains(arg), qPrintable(arg));
    }

    void tiers()
    {
        QCOMPARE(eventTier({{"tier", 2000}}), QStringLiteral("2000"));
        QCOMPARE(eventTier({{"tier", "3000"}}), QStringLiteral("3000"));
        QVERIFY(dss::actions::eventArguments(ev("gibt.es_nicht", user())).isEmpty());
    }

    // Filter der Auslöser (Plan 2.5): Bereich schliesst beide Enden ein
    void filters_data()
    {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QJsonObject>("fields");
        QTest::addColumn<QJsonObject>("data");
        QTest::addColumn<bool>("hit");

        const QJsonObject sub = merged(user(), {{"tier", "2000"}, {"is_gift", false}});
        QTest::newRow("Stufe leer = alle") << "twitch.subscribe" << QJsonObject{{"tiers", QJsonArray()}} << sub << true;
        QTest::newRow("Stufe passt") << "twitch.subscribe" << QJsonObject{{"tiers", QJsonArray{"2000", "3000"}}} << sub << true;
        QTest::newRow("Stufe passt nicht") << "twitch.subscribe" << QJsonObject{{"tiers", QJsonArray{"1000"}}} << sub << false;
        const QJsonObject resub = merged(user(), {{"tier", "1000"}, {"text", ""}, {"months", 6},
                                                  {"streak_months", QJsonValue()}, {"duration_months", 1}});
        QTest::newRow("Monate genug") << "twitch.resubscribe" << QJsonObject{{"min_months", 6}} << resub << true;
        QTest::newRow("Monate zu wenig") << "twitch.resubscribe" << QJsonObject{{"min_months", 7}} << resub << false;
        const auto bomb = [](int total) {
            return merged(user(), {{"is_anonymous", false}, {"total", total}, {"tier", "1000"},
                                   {"cumulative_total", QJsonValue()}});
        };
        QTest::newRow("Bündel ab 5: 5") << "twitch.gift_bomb" << QJsonObject{{"min", 5}, {"max", 0}} << bomb(5) << true;
        QTest::newRow("Bündel ab 5: 4") << "twitch.gift_bomb" << QJsonObject{{"min", 5}, {"max", 0}} << bomb(4) << false;
        QTest::newRow("Bündel 5–10: 10") << "twitch.gift_bomb" << QJsonObject{{"min", 5}, {"max", 10}} << bomb(10) << true;
        QTest::newRow("Bündel 5–10: 11") << "twitch.gift_bomb" << QJsonObject{{"min", 5}, {"max", 10}} << bomb(11) << false;
        QTest::newRow("Bündel bis 10: 3") << "twitch.gift_bomb" << QJsonObject{{"min", 0}, {"max", 10}} << bomb(3) << true;
        const auto cheer = [](int bits) {
            return merged(user(), {{"is_anonymous", false}, {"bits", bits}, {"text", ""}});
        };
        QTest::newRow("Cheer alle") << "twitch.cheer" << QJsonObject{{"min", 0}, {"max", 0}} << cheer(1) << true;
        QTest::newRow("Cheer ab 100: 99") << "twitch.cheer" << QJsonObject{{"min", 100}, {"max", 0}} << cheer(99) << false;
        QTest::newRow("Cheer ab 100: 100") << "twitch.cheer" << QJsonObject{{"min", 100}, {"max", 0}} << cheer(100) << true;
        QTest::newRow("Cheer nur max: genau") << "twitch.cheer" << QJsonObject{{"min", 0}, {"max", 500}} << cheer(500) << true;
        QTest::newRow("Cheer nur max: kleiner") << "twitch.cheer" << QJsonObject{{"min", 0}, {"max", 500}} << cheer(100) << false;
        QTest::newRow("Cheer 100–500: 500") << "twitch.cheer" << QJsonObject{{"min", 100}, {"max", 500}} << cheer(500) << true;
        const auto raid = [](int viewers) {
            return QJsonObject{{"from_user_id", "100000002"}, {"from_user_login", "testkanal"},
                               {"from_user_name", "Testkanal"}, {"viewers", viewers}};
        };
        QTest::newRow("Raid 1–100: 1") << "twitch.raid" << QJsonObject{{"min", 1}, {"max", 100}} << raid(1) << true;
        QTest::newRow("Raid 1–100: 0") << "twitch.raid" << QJsonObject{{"min", 1}, {"max", 100}} << raid(0) << false;
        QTest::newRow("Raid 1–100: 101") << "twitch.raid" << QJsonObject{{"min", 1}, {"max", 100}} << raid(101) << false;
        const QJsonObject atStart{{"started_at", "2026-11-15T19:00:00Z"}, {"kind", "live"}, {"at_start", true}};
        const QJsonObject live{{"started_at", "2026-11-15T19:00:00Z"}, {"kind", "live"}, {"at_start", false}};
        QTest::newRow("Online beim Verbinden übersprungen") << "twitch.stream_online" << QJsonObject{{"skip_at_start", true}} << atStart << false;
        QTest::newRow("Online beim Verbinden gewünscht") << "twitch.stream_online" << QJsonObject{{"skip_at_start", false}} << atStart << true;
        QTest::newRow("Online neu") << "twitch.stream_online" << QJsonObject{{"skip_at_start", true}} << live << true;
    }
    void filters()
    {
        QFETCH(QString, type);
        QFETCH(QJsonObject, fields);
        QFETCH(QJsonObject, data);
        QFETCH(bool, hit);
        Env env;
        Trigger t = newTrigger(type);
        for (auto it = fields.begin(); it != fields.end(); ++it)
            t.fields.insert(it.key(), it.value());
        const QUuid a = env.add(act(QStringLiteral("A"), {t}));
        TriggerEvent e = ev("", data);
        e.type = type;
        const TriggerResult r = env.send(e);
        QCOMPARE(r.hits.size(), hit ? 1 : 0);
        if (hit) {
            QCOMPARE(r.hits.first().actionId, a);
            QCOMPARE(r.hits.first().triggerId, t.id);
            QCOMPARE(r.hits.first().triggerType, type);
        }
    }

    // Reihenfolge der Liste, ein Lauf je Aktion, Abgeschaltetes zählt nicht (Plan 3.2)
    void orderAndDisabled()
    {
        Env env;
        Action off = act(QStringLiteral("Aus"), {trig(kTriggerFollow)});
        off.enabled = false;
        const QUuid first = env.add(act(QStringLiteral("Erste"), {trig(kTriggerFollow), trig(kTriggerFollow)}));
        env.add(off);
        env.add(act(QStringLiteral("Auslöser aus"), {trig(kTriggerFollow, {}, false)}));
        env.add(act(QStringLiteral("Anderes"), {trig(kTriggerRaid)}));
        const QUuid last = env.add(act(QStringLiteral("Letzte"), {trig(kTriggerSubscribe), trig(kTriggerFollow)}));
        const TriggerResult r = env.send(ev(kTriggerFollow, merged(user(), {{"followed_at", "x"}})));
        QCOMPARE(hitActions(r), (QList<QUuid>{first, last}));
        QCOMPARE(r.hits.at(0).triggerId, env.config.actions.at(0).triggers.at(0).id);
        QCOMPARE(r.hits.at(1).arguments.value(QStringLiteral("user")).toString(), QStringLiteral("Testzuschauer"));
        QVERIFY(r.rejections.isEmpty());
    }

    // Befehle: Lage, Gross-/Kleinschreibung, Argumente (Plan 2.6)
    void commandMatching_data()
    {
        QTest::addColumn<QString>("location");
        QTest::addColumn<bool>("caseSensitive");
        QTest::addColumn<QString>("message");
        QTest::addColumn<bool>("hit");
        QTest::addColumn<QString>("rawInput");

        QTest::newRow("start") << "start" << false << "!so @Testkanal danke" << true << "@Testkanal danke";
        QTest::newRow("start allein") << "start" << false << "  !so  " << true << "";
        QTest::newRow("start: !d trifft nicht !discord") << "start" << false << "!sonst" << false << "";
        QTest::newRow("start: nicht am Anfang") << "start" << false << "hallo !so x" << false << "";
        QTest::newRow("start ohne Gross/klein") << "start" << false << "!SO x" << true << "x";
        QTest::newRow("start mit Gross/klein") << "start" << true << "!SO x" << false << "";
        QTest::newRow("zweiter Befehl") << "start" << false << "!shoutout a" << true << "a";
        QTest::newRow("exact") << "exact" << false << "!so" << true << "";
        QTest::newRow("exact mit Rest") << "exact" << false << "!so x" << false << "";
        QTest::newRow("anywhere") << "anywhere" << false << "bitte !so x y" << true << "x y";
        QTest::newRow("anywhere nur ganzes Wort") << "anywhere" << false << "bitte!so x" << false << "";
    }
    void commandMatching()
    {
        QFETCH(QString, location);
        QFETCH(bool, caseSensitive);
        QFETCH(QString, message);
        QFETCH(bool, hit);
        QFETCH(QString, rawInput);
        Env env;
        Command c = cmd(QStringLiteral("Shoutout"), {QStringLiteral("!so"), QStringLiteral("!shoutout")});
        c.location = location;
        c.caseSensitive = caseSensitive;
        env.add(c);
        env.add(act(QStringLiteral("SO"), {commandTrigger(c)}));
        const TriggerResult r = env.chat(message);
        QCOMPARE(r.hits.size(), hit ? 1 : 0);
        if (!hit)
            return;
        const Arguments args = r.hits.first().arguments;
        QCOMPARE(args.value(QStringLiteral("rawInput")).toString(), rawInput);
        QCOMPARE(args.value(QStringLiteral("commandName")).toString(), QStringLiteral("Shoutout"));
        QCOMPARE(args.value(QStringLiteral("commandId")).toString(), uuidText(c.id));
        QVERIFY(args.value(QStringLiteral("command")).toString().startsWith(QStringLiteral("!s")));
        QCOMPARE(args.value(QStringLiteral("msgId")).toString(), QStringLiteral("m-1"));
        QCOMPARE(args.value(QStringLiteral("counter")).toInt(), 1);
        const QStringList words = rawInput.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (qsizetype i = 0; i < words.size(); ++i)
            QCOMPARE(args.value(QStringLiteral("input%1").arg(i)).toString(), words.at(i));
        QVERIFY(!args.contains(QStringLiteral("input%1").arg(words.size())));
        // jedes Argument steht in der Tabelle des Auslösers «command»
        for (auto it = args.cbegin(); it != args.cend(); ++it)
            QVERIFY2(triggerType(QStringLiteral("command"))->arguments.contains(it.key()), qPrintable(it.key()));
    }

    void commandInputLimit()
    {
        Env env;
        const Command c = cmd(QStringLiteral("Viele"), {QStringLiteral("!x")});
        env.add(c);
        env.add(act(QStringLiteral("X"), {commandTrigger(c)}));
        const Arguments args = env.chat(QStringLiteral("!x 0 1 2 3 4 5 6 7 8 9 10 11")).hits.first().arguments;
        QCOMPARE(args.value(QStringLiteral("input9")).toString(), QStringLiteral("9"));
        QVERIFY(!args.contains(QStringLiteral("input10")));
        QCOMPARE(args.value(QStringLiteral("rawInput")).toString(), QStringLiteral("0 1 2 3 4 5 6 7 8 9 10 11"));
    }

    // Bot-Konto, eigene Nachrichten, Quellen, regex, abgeschalteter Befehl
    void commandIgnored()
    {
        Env env;
        Command plain = cmd(QStringLiteral("Normal"), {QStringLiteral("!a")});
        Command botOk = cmd(QStringLiteral("Bot erlaubt"), {QStringLiteral("!a")});
        botOk.ignoreBotAccount = false;
        botOk.ignoreInternal = false;
        Command regex = cmd(QStringLiteral("Regex"), {QStringLiteral("!a")});
        regex.mode = QStringLiteral("regex");
        Command noSource = cmd(QStringLiteral("Ohne Quelle"), {QStringLiteral("!a")});
        noSource.sources.clear();
        Command off = cmd(QStringLiteral("Aus"), {QStringLiteral("!a")});
        off.enabled = false;
        for (const Command &c : {plain, botOk, regex, noSource, off})
            env.add(c);
        QList<QUuid> actions;
        for (const Command &c : {plain, botOk, regex, noSource, off})
            actions.append(env.add(act(c.name, {commandTrigger(c)})));

        QCOMPARE(hitActions(env.chat(QStringLiteral("!a"))), (QList<QUuid>{actions.at(0), actions.at(1)}));
        QCOMPARE(hitActions(env.chat(QStringLiteral("!a"), {{"is_bot_account", true}})), QList<QUuid>{actions.at(1)});
        QCOMPARE(hitActions(env.chat(QStringLiteral("!a"), {}, true)), QList<QUuid>{actions.at(1)});
    }

    // Wer darf (Plan 2.6): erlauben, verbieten, Rollen, Gruppen, einzelne; Kanal immer
    void permissions_data()
    {
        QTest::addColumn<QString>("grant");
        QTest::addColumn<QStringList>("roles");
        QTest::addColumn<bool>("inGroup");
        QTest::addColumn<bool>("listedUser");
        QTest::addColumn<QJsonObject>("sender");
        QTest::addColumn<bool>("allowed");

        const QJsonObject viewer;
        const QJsonObject mod{{"role", 3}, {"is_moderator", true}};
        const QJsonObject sub{{"is_subscriber", true}};
        const QJsonObject channel{{"role", 4}, {"is_broadcaster", true}};
        QTest::newRow("alle") << "allow" << QStringList() << false << false << viewer << true;
        QTest::newRow("nur Mods: Zuschauer") << "allow" << QStringList{"moderator"} << false << false << viewer << false;
        QTest::newRow("nur Mods: Mod") << "allow" << QStringList{"moderator"} << false << false << mod << true;
        QTest::newRow("nur Mods: Kanal") << "allow" << QStringList{"moderator"} << false << false << channel << true;
        QTest::newRow("Abonnenten und VIP: Abonnent") << "allow" << QStringList{"subscriber", "vip"} << false << false << sub << true;
        QTest::newRow("Gruppe: Mitglied") << "allow" << QStringList() << true << false << viewer << true;
        QTest::newRow("einzeln: genannt") << "allow" << QStringList() << false << true << viewer << true;
        QTest::newRow("verbieten leer") << "deny" << QStringList() << false << false << viewer << true;
        QTest::newRow("verbieten Abonnenten: Abonnent") << "deny" << QStringList{"subscriber"} << false << false << sub << false;
        QTest::newRow("verbieten Abonnenten: Zuschauer") << "deny" << QStringList{"subscriber"} << false << false << viewer << true;
        QTest::newRow("verbieten Gruppe: Mitglied") << "deny" << QStringList() << true << false << viewer << false;
        QTest::newRow("verbieten einzeln: Kanal darf") << "deny" << QStringList() << false << true << channel << true;
    }
    void permissions()
    {
        QFETCH(QString, grant);
        QFETCH(QStringList, roles);
        QFETCH(bool, inGroup);
        QFETCH(bool, listedUser);
        QFETCH(QJsonObject, sender);
        QFETCH(bool, allowed);
        Env env;
        UserGroup group;
        group.id = QUuid::createUuid();
        group.name = QStringLiteral("Stammgäste");
        group.users = {{QStringLiteral("100000001"), {}}};
        env.config.groups.append(group);
        Command c = cmd(QStringLiteral("Recht"), {QStringLiteral("!r")});
        c.permissions.grant = grant;
        c.permissions.roles = roles;
        if (inGroup)
            c.permissions.groups = {group.id};
        if (listedUser)
            c.permissions.users = {{QStringLiteral("100000001"), {}}};
        env.add(c);
        env.add(act(QStringLiteral("R"), {commandTrigger(c)}));
        const TriggerResult r = env.chat(QStringLiteral("!r"), sender);
        QCOMPARE(r.hits.size(), allowed ? 1 : 0);
        QCOMPARE(reasons(r), allowed ? QStringList() : QStringList{QStringLiteral("keine Berechtigung")});
    }

    // Abklingzeiten Kanal und Person; der Kanal hat keine (Plan 2.6)
    void cooldowns()
    {
        Env env;
        Command c = cmd(QStringLiteral("Discord"), {QStringLiteral("!discord")});
        c.globalCooldownS = 10;
        c.userCooldownS = 30;
        env.add(c);
        const QUuid a = env.add(act(QStringLiteral("Info"), {commandTrigger(c)}));
        env.add(act(QStringLiteral("Ohne Befehl"), {trig(kTriggerFollow)}));
        const QJsonObject other = user(QStringLiteral("100000002"), QStringLiteral("Andere"));
        const QJsonObject channel = merged(user(kBroadcaster, QStringLiteral("Testkanal")), {{"role", 4}});

        QCOMPARE(env.chat(QStringLiteral("!discord")).hits.size(), 1);
        env.clock.advance(4000);
        TriggerResult r = env.chat(QStringLiteral("!discord"), other);
        QCOMPARE(r.hits.size(), 0);
        QCOMPARE(reasons(r), QStringList{QStringLiteral("Abklingzeit Kanal: noch 6 s")});
        QCOMPARE(r.rejections.first().actionId, a);
        QCOMPARE(r.rejections.first().triggerType, QStringLiteral("command"));
        QCOMPARE(r.rejections.first().state, RunState::Rejected);
        // Kanal: keine Abklingzeit und keine neue
        QCOMPARE(env.chat(QStringLiteral("!discord"), channel).hits.size(), 1);
        env.clock.advance(6000);
        QCOMPARE(env.chat(QStringLiteral("!discord"), other).hits.size(), 1);
        env.clock.advance(10000);
        r = env.chat(QStringLiteral("!discord"));
        QCOMPARE(reasons(r), QStringList{QStringLiteral("Abklingzeit Person: noch 10 s")});
        env.clock.advance(10000);
        QCOMPARE(env.chat(QStringLiteral("!discord")).hits.size(), 1);
    }

    // Zähler der Befehle (Plan 2.6): dauerhaft je nach persist_counter / persist_user_counter
    void counters()
    {
        Env env;
        Command persisted = cmd(QStringLiteral("Dauerhaft"), {QStringLiteral("!d")});
        persisted.persistCounter = true;
        persisted.persistUserCounter = true;
        Command fleeting = cmd(QStringLiteral("Flüchtig"), {QStringLiteral("!f")});
        env.add(persisted);
        env.add(fleeting);
        env.add(act(QStringLiteral("D"), {commandTrigger(persisted)}));
        env.add(act(QStringLiteral("F"), {commandTrigger(fleeting)}));
        env.chat(QStringLiteral("!d"));
        env.chat(QStringLiteral("!d"), user(QStringLiteral("100000002"), QStringLiteral("Andere")));
        Arguments args = env.chat(QStringLiteral("!d")).hits.first().arguments;
        QCOMPARE(args.value(QStringLiteral("counter")).toInt(), 3);
        QCOMPARE(args.value(QStringLiteral("userCounter")).toInt(), 2);
        args = env.chat(QStringLiteral("!f")).hits.first().arguments;
        QCOMPARE(args.value(QStringLiteral("counter")).toInt(), 1);
        QCOMPARE(args.value(QStringLiteral("userCounter")).toInt(), 1);
        const CommandCounter saved = env.variables.data().commandCounters.value(persisted.id);
        QCOMPARE(saved.counter->value, QJsonValue(3));
        QCOMPARE(saved.userCounters.value(QStringLiteral("100000001")).value, QJsonValue(2));
        QVERIFY(!env.variables.data().commandCounters.contains(fleeting.id));
    }

    // Chat-Nachricht und Befehl an einer Nachricht in der Reihenfolge der Liste; eigene Nachrichten
    // lösen keine Chat-Auslöser aus (Plan 3.2, 4.1)
    void chatAndCommand()
    {
        Env env;
        Command c = cmd(QStringLiteral("Hallo"), {QStringLiteral("!hallo")});
        c.ignoreInternal = false;
        env.add(c);
        const QUuid chatAction = env.add(act(QStringLiteral("Jede Nachricht"), {trig(kTriggerChatMessage)}));
        const QUuid cmdAction = env.add(act(QStringLiteral("Befehl"), {commandTrigger(c), trig(kTriggerChatMessage)}));
        const QUuid firstAction = env.add(act(QStringLiteral("Erste"), {trig(kTriggerFirstMessage)}));
        TriggerResult r = env.chat(QStringLiteral("!hallo du"));
        QCOMPARE(hitActions(r), (QList<QUuid>{chatAction, cmdAction}));
        QCOMPARE(r.hits.at(1).triggerType, QStringLiteral("command"));
        QVERIFY(!r.hits.at(0).arguments.contains(QStringLiteral("rawInput")));
        r = env.chat(QStringLiteral("nur Text"));
        QCOMPARE(hitActions(r), (QList<QUuid>{chatAction, cmdAction}));
        QCOMPARE(r.hits.at(1).triggerType, QStringLiteral("twitch.chat_message"));
        // vom Studio gesendet: nur der Befehl ohne ignore_internal
        r = env.chat(QStringLiteral("!hallo"), {}, true);
        QCOMPARE(hitActions(r), QList<QUuid>{cmdAction});
        QCOMPARE(r.hits.first().triggerType, QStringLiteral("command"));
        QVERIFY(env.send(ev(kTriggerFirstMessage, merged(chatData(QStringLiteral("x")), {{"first_ever", false}})), true)
                    .hits.isEmpty());
        QCOMPARE(hitActions(env.send(ev(kTriggerFirstMessage, merged(chatData(QStringLiteral("x")), {{"first_ever", false}})))),
                 QList<QUuid>{firstAction});
    }

    // Geteilter Chat: nur der eigene Kanal (Plan 4.1)
    void sharedChat()
    {
        Env env;
        const Command c = cmd(QStringLiteral("A"), {QStringLiteral("!a")});
        env.add(c);
        env.add(act(QStringLiteral("Chat"), {trig(kTriggerChatMessage), trig(kTriggerFirstMessage)}));
        env.add(act(QStringLiteral("Befehl"), {commandTrigger(c)}));
        QCOMPARE(env.chat(QStringLiteral("!a"), {{"source_broadcaster_id", "100000099"}}).hits.size(), 0);
        QCOMPARE(env.chat(QStringLiteral("!a"), {{"source_broadcaster_id", kBroadcaster}}).hits.size(), 2);
        QCOMPARE(env.send(ev(kTriggerFirstMessage, merged(chatData(QStringLiteral("x"), {{"source_broadcaster_id", "100000099"}}),
                                                          {{"first_ever", true}})))
                     .hits.size(),
                 0);
        QCOMPARE(env.config.commands.size(), 1);
        QCOMPARE(env.variables.data().commandCounters.size(), 0);
    }

    // Hauptschalter aus und «stumm» (Plan 2.2, 3.4): Eintrag statt Lauf, keine Abklingzeit, kein Zähler
    void offAndMuted()
    {
        Env env;
        env.config.enabled = false;
        Command c = cmd(QStringLiteral("A"), {QStringLiteral("!a")});
        c.globalCooldownS = 60;
        c.persistCounter = true;
        env.add(c);
        const QUuid a = env.add(act(QStringLiteral("Befehl"), {commandTrigger(c)}));
        env.add(act(QStringLiteral("Follow"), {trig(kTriggerFollow)}));
        TriggerResult r = env.chat(QStringLiteral("!a"));
        QVERIFY(r.hits.isEmpty());
        QCOMPARE(r.rejections.size(), 1);
        QCOMPARE(r.rejections.first().actionId, a);
        QCOMPARE(r.rejections.first().state, RunState::Rejected);
        QCOMPARE(r.rejections.first().reason, QStringLiteral("Chat-Bot aus"));
        r = env.chat(QStringLiteral("!a"));
        QCOMPARE(reasons(r), QStringList{QStringLiteral("Chat-Bot aus")});
        QVERIFY(env.variables.data().commandCounters.isEmpty());

        env.config.enabled = true;
        env.apply();
        env.dispatcher.setMuted(true);
        r = env.send(ev(kTriggerFollow, merged(user(), {{"followed_at", "x"}})));
        QVERIFY(r.hits.isEmpty());
        QCOMPARE(r.rejections.first().state, RunState::Muted);
        QCOMPARE(r.rejections.first().reason, QStringLiteral("stumm: Version im Einsatz läuft"));
        env.dispatcher.setMuted(false);
        // keine Abklingzeit aus den Läufen davor
        QCOMPARE(env.chat(QStringLiteral("!a")).hits.size(), 1);
        QCOMPARE(env.chat(QStringLiteral("!a")).hits.size(), 0);
    }

    // Zeitnachrichten (Plan 2.7): Abstand, Chat-Zeilen, nur im Stream, einmalig, Zufall
    void timers()
    {
        Env env;
        Timer t;
        t.id = QUuid::createUuid();
        t.name = QStringLiteral("Info");
        t.intervalS = 1200;
        t.lines = 2;
        t.onlyLive = true;
        env.config.timers.append(t);
        const QUuid a = env.add(act(QStringLiteral("Info"), {trig(kTriggerTimer, {{"timer_id", uuidText(t.id)}})}));
        // nicht live: fällt aus, beginnt neu
        env.clock.advance(1200 * 1000);
        QVERIFY(env.timerResults.isEmpty());
        env.send(ev(kTriggerStreamOnline, {{"started_at", "x"}, {"kind", "live"}, {"at_start", false}}));
        QVERIFY(env.dispatcher.isLive());
        env.chat(QStringLiteral("eins"));
        env.chat(QStringLiteral("eigene"), {}, true); // zählt nicht
        env.clock.advance(1200 * 1000);
        QVERIFY(env.timerResults.isEmpty()); // erst eine Zeile
        env.chat(QStringLiteral("zwei"));
        env.clock.advance(1200 * 1000);
        QCOMPARE(env.timerResults.size(), 1);
        const TriggerHit hit = env.timerResults.first().hits.first();
        QCOMPARE(hit.actionId, a);
        QCOMPARE(hit.triggerType, QStringLiteral("timer"));
        QCOMPARE(hit.arguments.value(QStringLiteral("timerName")).toString(), QStringLiteral("Info"));
        // Zeilen beginnen wieder bei 0
        env.chat(QStringLiteral("drei"));
        env.clock.advance(1200 * 1000);
        QCOMPARE(env.timerResults.size(), 1);
        env.send(ev(kTriggerStreamOffline));
        QVERIFY(!env.dispatcher.isLive());
    }

    void timerConfigChanges()
    {
        Env env;
        env.dispatcher.setLive(true);
        Timer t;
        t.id = QUuid::createUuid();
        t.name = QStringLiteral("Alle 60 s");
        t.intervalS = 60;
        t.lines = 0;
        env.config.timers.append(t);
        env.add(act(QStringLiteral("T"), {trig(kTriggerTimer, {{"timer_id", uuidText(t.id)}})}));
        env.clock.advance(30 * 1000);
        // Name geändert: Zeit läuft weiter
        env.config.timers[0].name = QStringLiteral("Neu");
        env.apply();
        env.clock.advance(30 * 1000);
        QCOMPARE(env.timerResults.size(), 1);
        QCOMPARE(env.timerResults.last().hits.first().arguments.value(QStringLiteral("timerName")).toString(),
                 QStringLiteral("Neu"));
        // Abstand geändert: beginnt neu
        env.clock.advance(30 * 1000);
        env.config.timers[0].intervalS = 120;
        env.apply();
        env.clock.advance(90 * 1000);
        QCOMPARE(env.timerResults.size(), 1);
        env.clock.advance(30 * 1000);
        QCOMPARE(env.timerResults.size(), 2);
        // Bot aus: läuft nicht, ohne Eintrag
        env.config.enabled = false;
        env.apply();
        env.clock.advance(120 * 1000);
        QCOMPARE(env.timerResults.size(), 2);
        // stumm: Eintrag muted
        env.config.enabled = true;
        env.apply();
        env.dispatcher.setMuted(true);
        env.clock.advance(120 * 1000);
        QCOMPARE(env.timerResults.size(), 3);
        QCOMPARE(env.timerResults.last().rejections.first().state, RunState::Muted);
        env.dispatcher.setMuted(false);
        // abgeschaltet: endet
        env.config.timers[0].enabled = false;
        env.apply();
        QCOMPARE(env.clock.pendingTimers(), 0);
    }

    void timerOnceAndRandom()
    {
        Env env;
        env.dispatcher.setLive(true);
        env.dispatcher.setRandomSource([] { return 0.5; });
        Timer once;
        once.id = QUuid::createUuid();
        once.name = QStringLiteral("Einmal");
        once.intervalS = 60;
        once.repeat = false;
        once.onlyLive = false;
        Timer random;
        random.id = QUuid::createUuid();
        random.name = QStringLiteral("Zufall");
        random.random = true;
        random.randomMinS = 60;
        random.randomMaxS = 600;
        random.onlyLive = false;
        env.config.timers = {once, random};
        env.add(act(QStringLiteral("Beide"), {trig(kTriggerTimer, {{"timer_id", uuidText(once.id)}}),
                                              trig(kTriggerTimer, {{"timer_id", uuidText(random.id)}})}));
        env.clock.advance(60 * 1000);
        QCOMPARE(env.timerResults.size(), 1);
        // 60 + floor(0.5 * 541) = 330 s
        env.clock.advance(269 * 1000);
        QCOMPARE(env.timerResults.size(), 1);
        env.clock.advance(1000);
        QCOMPARE(env.timerResults.size(), 2);
        QCOMPARE(env.timerResults.last().hits.first().arguments.value(QStringLiteral("timerName")).toString(),
                 QStringLiteral("Zufall"));
        env.clock.advance(3600 * 1000);
        int onceCount = 0;
        for (const TriggerResult &r : env.timerResults)
            onceCount += r.hits.first().arguments.value(QStringLiteral("timerName")).toString() == QLatin1String("Einmal");
        QCOMPARE(onceCount, 1);
    }

    // Ergebnis an die Engine (Weg wie in b3): Befehl startet eine Aktion mit seinen Argumenten
    void submitToEngine()
    {
        Env env;
        ActionEngine engine(&env.clock, &env.variables);
        Command c = cmd(QStringLiteral("SO"), {QStringLiteral("!so")});
        c.permissions.roles = {QStringLiteral("moderator")};
        env.add(c);
        Step set = newStep(QString::fromLatin1(kStepSetArgument));
        set.fields.insert(QStringLiteral("name"), QStringLiteral("shoutoutLogin"));
        set.fields.insert(QStringLiteral("value"), QStringLiteral("%input0%"));
        const QUuid a = env.add(act(QStringLiteral("Shoutout per Befehl"), {commandTrigger(c)}, {set}));
        engine.setConfig(env.config);
        QList<RunRecord> finished;
        connect(&engine, &ActionEngine::runFinished, this, [&](const RunRecord &r) { finished.append(r); });

        submit(engine, env.chat(QStringLiteral("!so testkanal")));
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().state, RunState::Rejected);
        QCOMPARE(finished.first().message, QStringLiteral("keine Berechtigung"));
        QCOMPARE(finished.first().actionId, a);

        submit(engine, env.chat(QStringLiteral("!so testkanal"), {{"role", 3}, {"is_moderator", true}}));
        QCOMPARE(finished.size(), 2);
        QCOMPARE(finished.last().state, RunState::Ok);
        QCOMPARE(finished.last().triggerType, QStringLiteral("command"));
        QCOMPARE(finished.last().triggerId, env.config.actions.first().triggers.first().id);
    }

    void testTrigger()
    {
        const Trigger t = trig(kTriggerTest, {{"variables", QJsonObject{{"user", "Testzuschauer"}, {"bits", "100"}}}});
        const Arguments args = testArguments(t);
        QCOMPARE(args.size(), 2);
        QCOMPARE(args.value(QStringLiteral("bits")).toString(), QStringLiteral("100"));
    }
};

QTEST_GUILESS_MAIN(TestTriggers)
#include "tst_ausloeser.moc"
