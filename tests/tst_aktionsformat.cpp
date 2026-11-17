// Prüft das Modell der Aktionen ohne Qt Gui (P14 a, Plan 2.2 bis 2.8 und 2.11): Lesen und
// Schreiben von aktionen.json mit den erfundenen Testdaten tests/daten/aktionen/, unbekannte
// Felder (extra), script und unsupported, Typtabellen, ungültige Dateien, Verweise, und
// dss-core --format-pruefen mit allen Dateien des Aktionssystems.

#include "actionjson.h"
#include "actionrefs.h"
#include "dataformat.h"
#include "steptypes.h"
#include "triggertypes.h"
#include "variables.h"
#include "viewerdata.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <optional>

using namespace dss::actions;

namespace {

const QString kDir = QStringLiteral(DSS_TEST_AKTIONEN_DIR);

QByteArray readFile(const QString &name)
{
    QFile f(kDir + QLatin1Char('/') + name);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QJsonObject readObject(const QString &name)
{
    return QJsonDocument::fromJson(readFile(name)).object();
}

QUuid id(const char *text)
{
    return QUuid::fromString(QLatin1String(text));
}

// Ändert den Wert unter path («actions.0.steps.2.text»); ohne value wird er entfernt.
QJsonValue modify(const QJsonValue &node, QStringList path, const std::optional<QJsonValue> &value)
{
    const QString key = path.takeFirst();
    if (node.isArray()) {
        QJsonArray a = node.toArray();
        const int i = key.toInt();
        if (path.isEmpty()) {
            if (value)
                a[i] = *value;
            else
                a.removeAt(i);
        } else {
            a[i] = modify(a.at(i), path, value);
        }
        return a;
    }
    QJsonObject o = node.toObject();
    if (path.isEmpty()) {
        if (value)
            o[key] = *value;
        else
            o.remove(key);
    } else {
        o[key] = modify(o.value(key), path, value);
    }
    return o;
}

QJsonObject modified(const QJsonObject &root, const QString &path, const std::optional<QJsonValue> &value)
{
    return modify(root, path.split(QLatin1Char('.')), value).toObject();
}

QStringList messages(const QList<ReferenceWarning> &warnings)
{
    QStringList m;
    for (const ReferenceWarning &w : warnings)
        m.append(w.message);
    return m;
}

const char kRegexWarning[] =
    "Aktion «First Chatters», Schritt 3.2 (Wenn): Vergleich «regex» wird gespeichert, aber nicht ausgeführt";

// Overlay und Medien passend zu vollstaendig.json
ReferenceContext fullContext()
{
    ReferenceContext c;
    c.overlays = QHash<QUuid, QSet<QUuid>>{
        {id("a6000000-0000-4000-8000-000000000001"),
         {id("a7000000-0000-4000-8000-000000000001"), id("a7000000-0000-4000-8000-000000000002")}}};
    c.mediaFiles = QSet<QString>{
        QStringLiteral("media/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.mp3")};
    return c;
}

} // namespace

class TestActionFormat : public QObject
{
    Q_OBJECT

private slots:
    void readsCompleteFile()
    {
        ActionsConfig c;
        QString error;
        QVERIFY2(parseActions(readFile(QStringLiteral("vollstaendig.json")), &c, &error), qPrintable(error));
        QCOMPARE(c.enabled, false);
        QCOMPARE(c.queues.size(), 2);
        QCOMPARE(c.groups.size(), 1);
        QCOMPARE(c.actions.size(), 5);
        QCOMPARE(c.commands.size(), 2);
        QCOMPARE(c.timers.size(), 2);

        const Action &greeting = c.actions.at(0);
        QCOMPARE(greeting.name, QStringLiteral("Begrüssung"));
        QCOMPARE(greeting.queue, id("a1000000-0000-4000-8000-000000000002"));
        QCOMPARE(greeting.templateId, id("a9000000-0000-4000-8000-000000000001"));
        QCOMPARE(greeting.steps.size(), 4);
        // Wenn mit Dann (Abbruch) und Sonst (leer)
        const Step &ifStep = greeting.steps.at(0);
        QCOMPARE(ifStep.type, QStringLiteral("if"));
        QCOMPARE(ifStep.steps.size(), 2);
        QCOMPARE(ifStep.steps.at(0).steps.at(0).type, QStringLiteral("break"));
        QVERIFY(ifStep.steps.at(1).steps.isEmpty());
        QCOMPARE(greeting.steps.at(2).fields.value(QStringLiteral("text")).toString(),
                 QStringLiteral("Willkommen, %user%! 100% Freude"));

        const Action &firsts = c.actions.at(3);
        QVERIFY(firsts.queue.isNull());
        QCOMPARE(firsts.steps.at(0).weight, 1.5);
        // Tiefe 4: Gruppe → Wenn → Dann → Pause
        QCOMPARE(firsts.steps.at(2).steps.at(1).steps.at(0).steps.at(0).type, QStringLiteral("delay"));

        // script und unsupported bleiben mit ihren Feldern erhalten (Plan 2.4)
        const Action &all = c.actions.at(4);
        QCOMPARE(all.triggers.size(), 12);
        QCOMPARE(all.steps.at(2).type, QStringLiteral("script"));
        QCOMPARE(all.steps.at(2).fields.value(QStringLiteral("code")).toString(),
                 QStringLiteral("public class CPHInline { }"));
        QCOMPARE(all.steps.at(3).type, QStringLiteral("unsupported"));
        QCOMPARE(all.steps.at(3).fields.value(QStringLiteral("sb_type")).toInt(), 16);
        QCOMPARE(all.steps.at(3).fields.value(QStringLiteral("original")).toObject()
                     .value(QStringLiteral("gameName")).toString(),
                 QStringLiteral("Beispiel"));

        const Command &so = c.commands.at(1);
        QCOMPARE(so.permissions.grant, QStringLiteral("deny"));
        QCOMPARE(so.permissions.roles, (QStringList{QStringLiteral("moderator"), QStringLiteral("vip")}));
        QCOMPARE(so.permissions.groups, QList<QUuid>{id("a2000000-0000-4000-8000-000000000001")});
        QCOMPARE(so.permissions.users.at(0).userId, QStringLiteral("100000003"));
        QVERIFY(so.sources.isEmpty());
        QCOMPARE(c.timers.at(1).randomMaxS, 600);
    }

    // Unbekannte Felder bleiben auf jeder Ebene erhalten (Plan 2.1)
    void keepsUnknownFields()
    {
        ActionsConfig c;
        QString error;
        QVERIFY2(parseActions(readFile(QStringLiteral("vollstaendig.json")), &c, &error), qPrintable(error));
        QCOMPARE(c.extra.value(QStringLiteral("test_unbekannt_oben")).toString(), QStringLiteral("bleibt erhalten"));
        QCOMPARE(c.queues.at(1).extra.value(QStringLiteral("test_unbekannt")), QJsonValue(QJsonArray{1, 2}));
        QCOMPARE(c.groups.at(0).users.at(1).extra.value(QStringLiteral("test_unbekannt")).toString(),
                 QStringLiteral("erfunden"));
        QCOMPARE(c.actions.at(0).extra.value(QStringLiteral("exclude_from_pending")), QJsonValue(false));
        QCOMPARE(c.actions.at(4).triggers.at(1).extra.value(QStringLiteral("test_unbekannt")).toString(),
                 QStringLiteral("x"));
        QCOMPARE(c.commands.at(1).extra.value(QStringLiteral("test_unbekannt")),
                 QJsonValue(QJsonObject{{QStringLiteral("a"), 1}}));
        QCOMPARE(c.commands.at(1).permissions.extra.value(QStringLiteral("test_unbekannt")), QJsonValue(7));
        QVERIFY(c.timers.at(1).extra.contains(QStringLiteral("test_unbekannt")));
        QVERIFY(c.timers.at(1).extra.value(QStringLiteral("test_unbekannt")).isNull());
        // Element eines Overlay-Schritts: steht als Ganzes in fields
        const QJsonObject element = c.actions.at(0).steps.at(3).fields.value(QStringLiteral("elements"))
                                        .toArray().at(1).toObject();
        QCOMPARE(element.value(QStringLiteral("test_unbekannt")), QJsonValue(true));
    }

    // Schreiben und wieder Lesen ändert nichts; die Testdatei ist so geschrieben, wie der Kern schreibt.
    void roundTrip()
    {
        const QJsonObject original = readObject(QStringLiteral("vollstaendig.json"));
        ActionsConfig c;
        QString error;
        QVERIFY2(actionsFromJson(original, &c, &error), qPrintable(error));
        const QByteArray written = serializeActions(c);
        QCOMPARE(QJsonDocument::fromJson(written).object(), original);
        ActionsConfig again;
        QVERIFY2(parseActions(written, &again, &error), qPrintable(error));
        QCOMPARE(again, c);
        QCOMPARE(serializeActions(again), written);
        // Einzelteile wie für action_put
        Action action;
        QVERIFY2(actionFromJson(actionToJson(c.actions.at(3)), &action, &error), qPrintable(error));
        QCOMPARE(action, c.actions.at(3));
        Command command;
        QVERIFY2(commandFromJson(commandToJson(c.commands.at(1)), &command, &error), qPrintable(error));
        QCOMPARE(command, c.commands.at(1));
        Timer timer;
        QVERIFY2(timerFromJson(timerToJson(c.timers.at(1)), &timer, &error), qPrintable(error));
        QCOMPARE(timer, c.timers.at(1));
        Queue queue;
        QVERIFY2(queueFromJson(queueToJson(c.queues.at(1)), &queue, &error), qPrintable(error));
        QCOMPARE(queue, c.queues.at(1));
        UserGroup group;
        QVERIFY2(userGroupFromJson(userGroupToJson(c.groups.at(0)), &group, &error), qPrintable(error));
        QCOMPARE(group, c.groups.at(0));
    }

    // Beispiel aus Plan 2.2: Schritte ohne weight, Warteschlange als UUID aus lauter Nullen
    void planExample()
    {
        ActionsConfig c;
        QString error;
        QVERIFY2(parseActions(readFile(QStringLiteral("plan-beispiel.json")), &c, &error), qPrintable(error));
        QVERIFY(c.actions.at(0).queue.isNull());
        QCOMPARE(c.actions.at(0).steps.at(0).weight, 0.0);
        const QJsonObject written = QJsonDocument::fromJson(serializeActions(c)).object();
        const QJsonObject action = written.value(QStringLiteral("actions")).toArray().at(0).toObject();
        QVERIFY(action.value(QStringLiteral("queue")).isNull());
        QCOMPARE(action.value(QStringLiteral("steps")).toArray().at(0).toObject().value(QStringLiteral("weight")),
                 QJsonValue(0));
    }

    // Typtabellen (steptypes.h, triggertypes.h) und Abdeckung der Testdatei
    void typeTables()
    {
        QSet<QString> stepKeys;
        for (const StepTypeInfo &t : stepTypes()) {
            QVERIFY2(!stepKeys.contains(t.key), qPrintable(t.key));
            stepKeys.insert(t.key);
            QVERIFY(!t.label.isEmpty());
        }
        QCOMPARE(stepKeys.size(), 22);
        // Nummern aus Plan 2.4
        const QList<std::pair<QString, int>> numbers{
            {QStringLiteral("comment"), 1009}, {QStringLiteral("group"), 99900}, {QStringLiteral("if"), 120},
            {QStringLiteral("if_true"), 99901}, {QStringLiteral("if_false"), 99902}, {QStringLiteral("break"), 124},
            {QStringLiteral("delay"), 1002}, {QStringLiteral("run_action"), 4}, {QStringLiteral("set_argument"), 123},
            {QStringLiteral("global_get"), 121}, {QStringLiteral("global_set"), 122}, {QStringLiteral("chat_send"), 10},
            {QStringLiteral("user_info"), 50}, {QStringLiteral("twitch_announce"), 23}, {QStringLiteral("play_sound"), 1},
            {QStringLiteral("obs_source_visible"), 30}, {QStringLiteral("script"), 99999}};
        for (const auto &[key, number] : numbers) {
            QCOMPARE(stepType(key)->sbType, number);
            QCOMPARE(stepTypeForSbType(number), key);
        }
        QCOMPARE(stepTypeForSbType(16), QStringLiteral("unsupported"));
        QCOMPARE(stepTypeForSbType(0), QStringLiteral("unsupported"));
        QCOMPARE(stepType(QStringLiteral("script"))->part, StepPart::Never);
        QVERIFY(!stepType(QStringLiteral("unsupported"))->creatable);
        QCOMPARE(stepType(QStringLiteral("obs_source_visible"))->part, StepPart::P14_2);
        // P15 f1: ohne Entsprechung bei Streamer.bot, im Editor anlegbar, nur das Feld preset
        for (const char *key : {"camera_preset", "light_preset"}) {
            const StepTypeInfo *t = stepType(QString::fromLatin1(key));
            QVERIFY2(t && t->creatable && t->part == StepPart::P14_1 && t->sbType == 0, key);
            QCOMPARE(t->fields.size(), 1);
            QCOMPARE(t->fields[0].name, QStringLiteral("preset"));
        }
        QVERIFY(stepType(QStringLiteral("gibt-es-nicht")) == nullptr);

        QSet<QString> triggerKeys;
        for (const TriggerTypeInfo &t : triggerTypes()) {
            QVERIFY2(!triggerKeys.contains(t.key), qPrintable(t.key));
            triggerKeys.insert(t.key);
            // Twitch-Auslöser hören auf das gleichnamige Ereignis aus P13 1.3
            if (t.key.startsWith(QStringLiteral("twitch.")))
                QCOMPARE(t.eventType, t.key);
        }
        QCOMPARE(triggerKeys.size(), 14);
        QCOMPARE(triggerType(QStringLiteral("command"))->sbType, 401);
        QCOMPARE(triggerType(QStringLiteral("command"))->eventType, QStringLiteral("twitch.chat_message"));
        QVERIFY(triggerType(QStringLiteral("command"))->arguments.contains(QStringLiteral("input9")));
        QCOMPARE(triggerType(QStringLiteral("twitch.stream_offline"))->sbType, 155);
        QCOMPARE(commonArguments().size(), 11);
        // Argumentnamen nach Schritt 0 (Bericht 2026-11-15, Abschnitt C 19)
        const auto args = [](const char *type) { return triggerType(QString::fromLatin1(type))->arguments; };
        QVERIFY(args("twitch.chat_message").contains(QStringLiteral("isHighlight")));
        QVERIFY(!args("twitch.chat_message").contains(QStringLiteral("isHighlighted")));
        QVERIFY(args("command").contains(QStringLiteral("firstMessage")));
        QCOMPARE(args("command").count(QStringLiteral("msgId")), 1);
        QVERIFY(args("twitch.subscribe").contains(QStringLiteral("subscriptionTier")));
        QVERIFY(args("twitch.resubscribe").contains(QStringLiteral("monthStreak")));
        QVERIFY(args("twitch.gift_sub").contains(QStringLiteral("recipientId")));
        QVERIFY(args("twitch.gift_sub").contains(QStringLiteral("recipientUser")));
        QVERIFY(args("twitch.gift_bomb").contains(QStringLiteral("totalGifts")));
        // anonymous bei Cheer: Name aus P13 (Schritt 0: bei Cheer nicht belegbar), P14 c6
        QVERIFY(args("twitch.cheer").contains(QStringLiteral("anonymous")));
        QVERIFY(!args("twitch.cheer").contains(QStringLiteral("isAnonymous")));
        QVERIFY(args("twitch.stream_online").contains(QStringLiteral("startedAt")));
        QCOMPARE(args("twitch.stream_offline"), QStringList{QStringLiteral("endedAt")});
        for (const TriggerTypeInfo &t : triggerTypes())
            QCOMPARE(QSet<QString>(t.arguments.cbegin(), t.arguments.cend()).size(), t.arguments.size());

        // vollstaendig.json enthält jeden Typ
        ActionsConfig c;
        QString error;
        QVERIFY2(parseActions(readFile(QStringLiteral("vollstaendig.json")), &c, &error), qPrintable(error));
        QSet<QString> usedSteps;
        QSet<QString> usedTriggers;
        for (const Action &a : c.actions) {
            forEachStep(a.steps, [&](const Step &s, const QList<int> &) { usedSteps.insert(s.type); });
            for (const Trigger &t : a.triggers)
                usedTriggers.insert(t.type);
        }
        QCOMPARE(usedSteps, stepKeys);
        QCOMPARE(usedTriggers, triggerKeys);
    }

    // Neue Schritte und Auslöser mit Standardwerten sind gültig
    void newItemsAreValid()
    {
        Action action;
        action.id = QUuid::createUuid();
        action.name = QStringLiteral("Neu");
        for (const StepTypeInfo &t : stepTypes()) {
            if (!t.creatable)
                continue;
            const Step s = newStep(t.key);
            QVERIFY(!s.id.isNull());
            QVERIFY(s.enabled);
            action.steps.append(s);
        }
        QCOMPARE(action.steps.at(2).type, QStringLiteral("if"));
        QCOMPARE(action.steps.at(2).steps.size(), 2);
        for (const TriggerTypeInfo &t : triggerTypes())
            action.triggers.append(newTrigger(t.key));
        Action read;
        QString error;
        QVERIFY2(actionFromJson(actionToJson(action), &read, &error), qPrintable(error));
        QCOMPARE(read, action);
        QVERIFY(newStep(QStringLiteral("gibt-es-nicht")).id.isNull());
        QVERIFY(!newStep(QStringLiteral("unsupported")).enabled);
    }

    void invalidFile_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<bool>("remove");
        QTest::addColumn<QString>("expected");

        const auto add = [](const char *name, const QString &path, const QJsonValue &value, const QString &expected) {
            QTest::newRow(name) << path << value << false << expected;
        };
        const auto removeRow = [](const char *name, const QString &path, const QString &expected) {
            QTest::newRow(name) << path << QJsonValue() << true << expected;
        };
        add("neueres Format", QStringLiteral("format_version"), 2, QStringLiteral("format_version 2 ist unbekannt"));
        add("Format keine ganze Zahl", QStringLiteral("format_version"), 1.5, QStringLiteral("«format_version»"));
        removeRow("Hauptschalter fehlt", QStringLiteral("enabled"), QStringLiteral("Feld «enabled»: fehlt"));
        add("Liste ist Objekt", QStringLiteral("actions"), QJsonObject(), QStringLiteral("Feld «actions»: muss eine Liste sein"));
        add("Aktion ohne Namen", QStringLiteral("actions.0.name"), QStringLiteral("  "),
            QStringLiteral("Feld «actions[0].name»: darf nicht leer sein"));
        add("Kennung gross", QStringLiteral("actions.0.id"), QStringLiteral("A3000000-0000-4000-8000-000000000001"),
            QStringLiteral("Feld «actions[0].id»: muss eine UUID"));
        add("Kennung doppelt", QStringLiteral("actions.1.id"), QStringLiteral("a3000000-0000-4000-8000-000000000001"),
            QStringLiteral("Feld «actions[1].id»: Kennung kommt mehrfach vor"));
        add("Schritt-Kennung doppelt", QStringLiteral("actions.1.steps.0.id"),
            QStringLiteral("a5000000-0000-4000-8000-000000000001"),
            QStringLiteral("Feld «actions[1].steps[0].id»: Kennung des Schritts kommt mehrfach vor"));
        add("Auslöser-Kennung doppelt", QStringLiteral("actions.2.triggers.0.id"),
            QStringLiteral("a4000000-0000-4000-8000-000000000001"),
            QStringLiteral("Feld «actions[2].triggers[0].id»: Kennung des Auslösers kommt mehrfach vor"));
        add("Schritt unbekannt", QStringLiteral("actions.0.steps.1.type"), QStringLiteral("teleport"),
            QStringLiteral("Feld «actions[0].steps[1].type»: unbekannter Typ «teleport»"));
        removeRow("Feld des Schritts fehlt", QStringLiteral("actions.0.steps.2.text"),
                  QStringLiteral("Feld «actions[0].steps[2].text»: fehlt"));
        add("Text ist Zahl", QStringLiteral("actions.0.steps.2.text"), 5,
            QStringLiteral("Feld «actions[0].steps[2].text»: muss Text sein"));
        add("Pause über 1 h", QStringLiteral("actions.3.steps.2.steps.1.steps.0.steps.0.max_value_ms"), 3600001,
            QStringLiteral("Feld «actions[3].steps[2].steps[1].steps[0].steps[0].max_value_ms»: muss von 0 bis 3600000 sein"));
        add("Pause mit Bruch", QStringLiteral("actions.3.steps.2.steps.1.steps.0.steps.0.value_ms"), 0.5,
            QStringLiteral("value_ms»: muss eine ganze Zahl"));
        add("Lautstärke 201", QStringLiteral("actions.4.steps.0.volume"), 201,
            QStringLiteral("Feld «actions[4].steps[0].volume»: muss von 0 bis 200 sein"));
        add("Vergleich unbekannt", QStringLiteral("actions.0.steps.0.operation"), QStringLiteral("ungefähr"),
            QStringLiteral("Feld «actions[0].steps[0].operation»: muss einer der Werte"));
        removeRow("Wenn ohne Sonst", QStringLiteral("actions.0.steps.0.steps.1"),
                  QStringLiteral("Feld «actions[0].steps[0].steps»: «if» hat genau"));
        add("Wenn mit Dann zweimal", QStringLiteral("actions.0.steps.0.steps.1.type"), QStringLiteral("if_true"),
            QStringLiteral("Feld «actions[0].steps[0].steps»: «if» hat genau"));
        add("Wenn mit Pause", QStringLiteral("actions.0.steps.0.steps.1"),
            QJsonObject{{QStringLiteral("id"), QStringLiteral("c5000000-0000-4000-8000-000000000001")},
                        {QStringLiteral("type"), QStringLiteral("break")},
                        {QStringLiteral("enabled"), true}},
            QStringLiteral("Feld «actions[0].steps[0].steps[1].type»: in «if» stehen nur"));
        add("Dann ausserhalb von Wenn", QStringLiteral("actions.0.steps.1.type"), QStringLiteral("if_true"),
            QStringLiteral("«if_true» nur direkt in «if»"));
        add("steps bei Benutzerinfo",QStringLiteral("actions.0.steps.1.steps"), QJsonArray(),
            QStringLiteral("Feld «actions[0].steps[1].steps»: nur bei"));
        add("unsupported an", QStringLiteral("actions.4.steps.3.enabled"), true,
            QStringLiteral("Feld «actions[4].steps[3].enabled»: ein nicht unterstützter Schritt ist immer aus"));
        add("weight negativ", QStringLiteral("actions.3.steps.0.weight"), -1,
            QStringLiteral("Feld «actions[3].steps[0].weight»: muss eine Zahl ab 0 sein"));
        add("Element ohne Quelle", QStringLiteral("actions.0.steps.3.elements.0.source"), QJsonValue::Null,
            QStringLiteral("Feld «actions[0].steps[3].elements»: «source» muss die UUID"));
        add("Element-Bild Zahl", QStringLiteral("actions.0.steps.3.elements.1.image"), 3,
            QStringLiteral("«image» muss Text oder null sein"));
        add("Ziel keine UUID", QStringLiteral("actions.2.steps.1.action_id"), QStringLiteral("Aktion 2"),
            QStringLiteral("Feld «actions[2].steps[1].action_id»: muss eine UUID"));
        add("Auslöser unbekannt", QStringLiteral("actions.4.triggers.0.type"), QStringLiteral("twitch.hype_train"),
            QStringLiteral("Feld «actions[4].triggers[0].type»: unbekannter Typ «twitch.hype_train»"));
        add("Stufe 4000", QStringLiteral("actions.4.triggers.2.tiers"), QJsonArray{QStringLiteral("4000")},
            QStringLiteral("Feld «actions[4].triggers[2].tiers»: erlaubt sind nur 1000, 2000, 3000"));
        add("Stufe doppelt", QStringLiteral("actions.4.triggers.2.tiers"),
            QJsonArray{QStringLiteral("1000"), QStringLiteral("1000")}, QStringLiteral("«1000» kommt mehrfach vor"));
        removeRow("Filter fehlt", QStringLiteral("actions.4.triggers.7.min"),
                  QStringLiteral("Feld «actions[4].triggers[7].min»: fehlt"));
        add("Bits negativ", QStringLiteral("actions.4.triggers.6.min"), -1,
            QStringLiteral("Feld «actions[4].triggers[6].min»: muss ab 0 sein"));
        add("Testwert Zahl", QStringLiteral("actions.4.triggers.11.variables"), QJsonObject{{QStringLiteral("bits"), 100}},
            QStringLiteral("«bits» muss Text sein"));
        add("Abklingzeit Bruch", QStringLiteral("commands.0.global_cooldown_s"), 1.5,
            QStringLiteral("Feld «commands[0].global_cooldown_s»: muss eine ganze Zahl ab 0 sein"));
        add("Modus unbekannt", QStringLiteral("commands.0.mode"), QStringLiteral("fuzzy"),
            QStringLiteral("Feld «commands[0].mode»: muss einer der Werte basic, regex sein"));
        add("Quelle youtube", QStringLiteral("commands.0.sources"), QJsonArray{QStringLiteral("youtube_message")},
            QStringLiteral("Feld «commands[0].sources»: erlaubt sind nur twitch_message"));
        add("Befehl leer", QStringLiteral("commands.0.commands"), QJsonArray{QStringLiteral("")},
            QStringLiteral("Feld «commands[0].commands»: muss eine Liste von Texten (nicht leer) sein"));
        add("Rolle admin", QStringLiteral("commands.1.permissions.roles"), QJsonArray{QStringLiteral("admin")},
            QStringLiteral("Feld «commands[1].permissions.roles»: erlaubt sind nur subscriber, vip, moderator"));
        add("Recht unbekannt", QStringLiteral("commands.1.permissions.grant"), QStringLiteral("maybe"),
            QStringLiteral("Feld «commands[1].permissions.grant»"));
        add("Zuschauer doppelt", QStringLiteral("groups.0.users.1.user_id"), QStringLiteral("100000001"),
            QStringLiteral("Feld «groups[0].users[1].user_id»: Zuschauer kommt mehrfach vor"));
        add("Zeitnachricht 59 s", QStringLiteral("timers.0.interval_s"), 59,
            QStringLiteral("Feld «timers[0].interval_s»: muss eine ganze Zahl ab 60 sein"));
        add("Zufall min über max", QStringLiteral("timers.1.random_min_s"), 601,
            QStringLiteral("Feld «timers[1].random_max_s»: muss bei Zufall mindestens random_min_s sein"));
        add("Zufall unter 60", QStringLiteral("timers.1.random_min_s"), 30,
            QStringLiteral("Feld «timers[1].random_min_s»: muss bei Zufall mindestens 60 sein"));
        add("Warteschlange Standard in Liste", QStringLiteral("queues.0.id"),
            QStringLiteral("00000000-0000-0000-0000-000000000000"), QStringLiteral("Feld «queues[0].id»: muss eine UUID"));
    }

    void invalidFile()
    {
        QFETCH(QString, path);
        QFETCH(QJsonValue, value);
        QFETCH(bool, remove);
        QFETCH(QString, expected);
        const QJsonObject data = modified(readObject(QStringLiteral("vollstaendig.json")), path,
                                          remove ? std::nullopt : std::optional<QJsonValue>(value));
        ActionsConfig c;
        c.enabled = true;
        QString error;
        QVERIFY(!actionsFromJson(data, &c, &error));
        QVERIFY2(error.contains(expected), qPrintable(error));
        QCOMPARE(c.enabled, true); // bei einem Fehler unverändert
    }

    void notJson()
    {
        ActionsConfig c;
        QString error;
        QVERIFY(!parseActions("{ kaputt", &c, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!parseActions("[]", &c, &error));
        QCOMPARE(error, QStringLiteral("muss ein JSON-Objekt sein"));
    }

    // Beliebig tiefe Verschachtelung liest sich nicht (Schutz des Lesens)
    void depthLimit()
    {
        const auto build = [](int depth) {
            QJsonArray steps;
            for (int i = 0; i < depth; ++i) {
                QJsonObject group{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                  {QStringLiteral("type"), QStringLiteral("group")},
                                  {QStringLiteral("enabled"), true},
                                  {QStringLiteral("name"), QStringLiteral("G")},
                                  {QStringLiteral("random"), false},
                                  {QStringLiteral("steps"), steps}};
                steps = QJsonArray{group};
            }
            QJsonObject root = readObject(QStringLiteral("plan-beispiel.json"));
            return modified(root, QStringLiteral("actions.0.steps"), steps);
        };
        ActionsConfig c;
        QString error;
        QVERIFY2(actionsFromJson(build(kMaxStepDepth), &c, &error), qPrintable(error));
        QVERIFY(!actionsFromJson(build(kMaxStepDepth + 1), &c, &error));
        QVERIFY2(error.contains(QStringLiteral("höchstens 32 Ebenen")), qPrintable(error));
    }

    // Verweise (Plan 2.11): Warnungen, die Datei bleibt gültig
    void referencesComplete()
    {
        ActionsConfig c;
        QString error;
        QVERIFY2(parseActions(readFile(QStringLiteral("vollstaendig.json")), &c, &error), qPrintable(error));
        const QList<ReferenceWarning> w = checkReferences(c, fullContext());
        QCOMPARE(messages(w), QStringList{QString::fromUtf8(kRegexWarning)});
        QCOMPARE(w.at(0).objectId, id("a3000000-0000-4000-8000-000000000004"));
        QCOMPARE(w.at(0).itemId, id("a5000000-0000-4000-8000-000000000034"));
        // Ohne Overlays und Medien im Kontext: keine Prüfung dieser Verweise
        QCOMPARE(messages(checkReferences(c, ReferenceContext())), QStringList{QString::fromUtf8(kRegexWarning)});
    }

    void referencesMissing_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<bool>("remove");
        QTest::addColumn<QStringList>("expected");

        QTest::newRow("Warteschlange fehlt") << QStringLiteral("queues.1") << QJsonValue() << true
            << QStringList{QStringLiteral("Aktion «Begrüssung»: Warteschlange fehlt")};
        QTest::newRow("Befehl fehlt") << QStringLiteral("commands.1") << QJsonValue() << true
            << QStringList{QStringLiteral("Aktion «Shoutout per Befehl», Auslöser 1 (Befehl): Befehl fehlt")};
        QTest::newRow("Befehl nicht gewählt") << QStringLiteral("actions.2.triggers.0.command_id")
            << QJsonValue(QJsonValue::Null) << false
            << QStringList{QStringLiteral("Aktion «Shoutout per Befehl», Auslöser 1 (Befehl): kein Befehl gewählt")};
        QTest::newRow("Zeitnachricht fehlt") << QStringLiteral("timers.0") << QJsonValue() << true
            << QStringList{QStringLiteral("Aktion «Alle Auslöser», Auslöser 11 (Zeitnachricht): Zeitnachricht fehlt")};
        QTest::newRow("Ziel-Aktion fehlt") << QStringLiteral("actions.1") << QJsonValue() << true
            << QStringList{QStringLiteral("Aktion «Shoutout per Befehl», Schritt 2 (Aktion ausführen): Ziel-Aktion fehlt")};
        QTest::newRow("Ziel aus und leer") << QStringLiteral("actions.2.steps.1")
            << QJsonValue(QJsonObject{{QStringLiteral("id"), QStringLiteral("c5000000-0000-4000-8000-000000000002")},
                                      {QStringLiteral("type"), QStringLiteral("run_action")},
                                      {QStringLiteral("enabled"), false},
                                      {QStringLiteral("action_id"), QJsonValue::Null},
                                      {QStringLiteral("immediately"), false}})
            << false << QStringList();
        QTest::newRow("Ton ohne Datei") << QStringLiteral("actions.4.steps.0.file") << QJsonValue(QString()) << false
            << QStringList{QStringLiteral("Aktion «Alle Auslöser», Schritt 1 (Ton abspielen): keine Datei gewählt")};
        QTest::newRow("Ton fehlt") << QStringLiteral("actions.4.steps.0.file")
            << QJsonValue(QStringLiteral("media/bbbb.mp3")) << false
            << QStringList{QStringLiteral("Aktion «Alle Auslöser», Schritt 1 (Ton abspielen): Datei fehlt in den Medien (media/bbbb.mp3)")};
        QTest::newRow("Ton aus Platzhalter") << QStringLiteral("actions.4.steps.0.file")
            << QJsonValue(QStringLiteral("media/%datei%")) << false << QStringList();
        QTest::newRow("Overlay fehlt") << QStringLiteral("actions.0.steps.3.overlay")
            << QJsonValue(QStringLiteral("c6000000-0000-4000-8000-000000000001")) << false
            << QStringList{QStringLiteral("Aktion «Begrüssung», Schritt 4 (Overlay-Element zeigen/verstecken): Overlay fehlt")};
        QTest::newRow("Element fehlt") << QStringLiteral("actions.0.steps.3.elements.1.source")
            << QJsonValue(QStringLiteral("c7000000-0000-4000-8000-000000000001")) << false
            << QStringList{QStringLiteral("Aktion «Begrüssung», Schritt 4 (Overlay-Element zeigen/verstecken): Element 2 fehlt im Overlay")};
        QTest::newRow("Bild fehlt") << QStringLiteral("actions.0.steps.3.elements.1.image")
            << QJsonValue(QStringLiteral("media/cccc.png")) << false
            << QStringList{QStringLiteral("Aktion «Begrüssung», Schritt 4 (Overlay-Element zeigen/verstecken): Bild von Element 2 fehlt in den Medien (media/cccc.png)")};
        QTest::newRow("kein Element") << QStringLiteral("actions.0.steps.3.elements") << QJsonValue(QJsonArray())
            << false
            << QStringList{QStringLiteral("Aktion «Begrüssung», Schritt 4 (Overlay-Element zeigen/verstecken): kein Element gewählt")};
        QTest::newRow("Benutzergruppe fehlt") << QStringLiteral("groups.0") << QJsonValue() << true
            << QStringList{QStringLiteral("Befehl «Shoutout»: Benutzergruppe fehlt")};
        QTest::newRow("Befehl doppelt") << QStringLiteral("commands.1.commands")
            << QJsonValue(QJsonArray{QStringLiteral("!so"), QStringLiteral("!DC")}) << false
            << QStringList{QStringLiteral("Befehl «Shoutout»: «!DC» auch in «Discord»")};
        QTest::newRow("doppelt, aber aus") << QStringLiteral("commands.0.enabled") << QJsonValue(false) << false
            << QStringList();
        QTest::newRow("Modus regex") << QStringLiteral("commands.0.mode") << QJsonValue(QStringLiteral("regex"))
            << false << QStringList{QStringLiteral("Befehl «Discord»: Modus «regex» wird gespeichert, aber nicht ausgeführt")};
    }

    void referencesMissing()
    {
        QFETCH(QString, path);
        QFETCH(QJsonValue, value);
        QFETCH(bool, remove);
        QFETCH(QStringList, expected);
        QJsonObject data = modified(readObject(QStringLiteral("vollstaendig.json")), path,
                                    remove ? std::nullopt : std::optional<QJsonValue>(value));
        if (QByteArray(QTest::currentDataTag()) == "doppelt, aber aus")
            data = modified(data, QStringLiteral("commands.1.commands"), QJsonArray{QStringLiteral("!dc")});
        ActionsConfig c;
        QString error;
        QVERIFY2(actionsFromJson(data, &c, &error), qPrintable(error));
        QStringList got = messages(checkReferences(c, fullContext()));
        got.removeAll(QString::fromUtf8(kRegexWarning));
        QCOMPARE(got, expected);
    }

    // Plan 2.11: dss-core --format-pruefen kennt alle neuen Dateien; format_version > 1 heisst
    // neueres Format (Exit-Code 5), eine ungültige Datei wird gemeldet und nicht geändert.
    void formatCheck()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString dir = temp.path();
        const auto write = [&](const QString &name, const QByteArray &data) {
            QFile f(dir + QLatin1Char('/') + name);
            return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
        };
        const auto read = [&](const QString &name) {
            QFile f(dir + QLatin1Char('/') + name);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        ViewersData viewers;
        noteViewer(&viewers, QStringLiteral("100000001"), QStringLiteral("testzuschauer"), QStringLiteral("Test"));
        QVERIFY(write(QStringLiteral("aktionen.json"), readFile(QStringLiteral("vollstaendig.json"))));
        QVERIFY(write(QStringLiteral("variablen.json"), serializeVariables(VariablesData())));
        QVERIFY(write(QStringLiteral("zuschauer.json"), serializeViewers(viewers)));
        QVERIFY(write(QStringLiteral("zitate.json"), serializeQuotes(QuotesData())));
        QVERIFY(write(QStringLiteral("streamerbot-archiv.json"), serializeArchive(ArchiveData())));

        dss::DataFormatStatus s = dss::checkDataFormat(dir);
        QVERIFY2(s.unreadable.isEmpty(), qPrintable(s.unreadable.join(QLatin1Char('\n'))));
        QVERIFY(s.newer.isEmpty());
        QVERIFY(!s.needsConversion());

        const auto runCore = [&](int *exitCode, QString *out, QString *err) {
            QProcess p;
            p.start(QStringLiteral(DSS_CORE_BIN), {QStringLiteral("--datenordner"), dir, QStringLiteral("--format-pruefen")});
            if (!p.waitForFinished(30000) || p.exitStatus() != QProcess::NormalExit)
                return false;
            *exitCode = p.exitCode();
            *out = QString::fromUtf8(p.readAllStandardOutput());
            *err = QString::fromUtf8(p.readAllStandardError());
            return true;
        };
        int exitCode = -1;
        QString out;
        QString err;
        QVERIFY(runCore(&exitCode, &out, &err));
        QCOMPARE(exitCode, 0);
        QVERIFY2(out.startsWith(QStringLiteral("Format aktuell")), qPrintable(out));

        // Ungültig: gemeldet, nicht geändert
        QByteArray broken = readFile(QStringLiteral("vollstaendig.json"));
        broken.replace("\"interval_s\": 1200", "\"interval_s\": 5");
        QVERIFY(write(QStringLiteral("aktionen.json"), broken));
        s = dss::checkDataFormat(dir);
        QCOMPARE(s.unreadable, QStringList{QStringLiteral(
                                   "aktionen.json: Feld «timers[0].interval_s»: muss eine ganze Zahl ab 60 sein")});
        QVERIFY(runCore(&exitCode, &out, &err));
        QCOMPARE(exitCode, 0);
        QVERIFY2(err.contains(QStringLiteral("nicht lesbar, bleibt unverändert: aktionen.json")), qPrintable(err));
        QCOMPARE(read(QStringLiteral("aktionen.json")), broken);

        // Neueres Format: Exit-Code 5, Umwandeln lehnt ab
        for (const QString &name : {QStringLiteral("variablen.json"), QStringLiteral("zuschauer.json"),
                                    QStringLiteral("zitate.json"), QStringLiteral("streamerbot-archiv.json")}) {
            const QByteArray before = read(name);
            QByteArray newer = before;
            newer.replace("\"format_version\": 1", "\"format_version\": 2");
            QVERIFY(write(name, newer));
            s = dss::checkDataFormat(dir);
            QCOMPARE(s.newer, QStringList{name + QStringLiteral(" (Format 2)")});
            QVERIFY(runCore(&exitCode, &out, &err));
            QCOMPARE(exitCode, 5);
            QVERIFY2(err.contains(name + QStringLiteral(" (Format 2)")), qPrintable(err));
            QStringList converted;
            QString error;
            QVERIFY(!dss::convertDataFormat(dir, &converted, &error));
            QVERIFY2(error.contains(QStringLiteral("neueres Format")), qPrintable(error));
            QCOMPARE(read(name), newer);
            QVERIFY(write(name, before));
        }
        // aktionen.json in neuerem Format: neuer, nicht «nicht lesbar»
        broken.replace("\"format_version\": 1", "\"format_version\": 3");
        QVERIFY(write(QStringLiteral("aktionen.json"), broken));
        s = dss::checkDataFormat(dir);
        QCOMPARE(s.newer, QStringList{QStringLiteral("aktionen.json (Format 3)")});
        QVERIFY(s.unreadable.isEmpty());
        // kein JSON
        QVERIFY(write(QStringLiteral("zitate.json"), "{ kaputt"));
        s = dss::checkDataFormat(dir);
        QCOMPARE(s.unreadable.size(), 1);
        QVERIFY2(s.unreadable.first().startsWith(QStringLiteral("zitate.json: ")), qPrintable(s.unreadable.first()));
    }
};

QTEST_GUILESS_MAIN(TestActionFormat)
#include "tst_aktionsformat.moc"
