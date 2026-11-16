// Ansichtslogik des Editors im Reiter «Chat-Bot» (P14 e1, Plan 6 und 8) ohne Fenster und ohne Kern:
// Listen, Auswahl, Filter, Bearbeiten über Änderungsbefehle und Prüfmeldungen der Unterreiter
// «Aktionen», «Befehle», «Zeitnachrichten» und «Variablen». Daten: tests/daten/aktionen/vollstaendig.json
// (erfunden, Plan 2.10) und erfundene Variablen und Zuschauer.

#include "actionjson.h"
#include "actionview.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QFile>
#include <QJsonArray>
#include <QTest>

using namespace dss;
using namespace dss::actions;

namespace {

QUuid id(const char *text)
{
    return QUuid(QLatin1String(text));
}

// Kennungen aus vollstaendig.json
const QUuid kBegruessung = id("a3000000-0000-4000-8000-000000000001");
const QUuid kShoutoutRun = id("a3000000-0000-4000-8000-000000000002");
const QUuid kShoutoutCommand = id("a3000000-0000-4000-8000-000000000003");
const QUuid kFirstChatters = id("a3000000-0000-4000-8000-000000000004");
const QUuid kAllTriggers = id("a3000000-0000-4000-8000-000000000005");
const QUuid kQueueAlerts = id("a1000000-0000-4000-8000-000000000001");
const QUuid kQueueMessages = id("a1000000-0000-4000-8000-000000000002");
const QUuid kCommandDiscord = id("a8000000-0000-4000-8000-000000000001");
const QUuid kCommandShoutout = id("a8000000-0000-4000-8000-000000000002");
const QUuid kTimerInfo = id("ab000000-0000-4000-8000-000000000001");
const QUuid kTimerRandom = id("ab000000-0000-4000-8000-000000000002");
// Schritte (kId…, weil steptypes.h die Typen als kStep… führt)
const QUuid kIdIf = id("a5000000-0000-4000-8000-000000000001");
const QUuid kIdIfTrue = id("a5000000-0000-4000-8000-000000000002");
const QUuid kIdBreak = id("a5000000-0000-4000-8000-000000000003");
const QUuid kIdIfFalse = id("a5000000-0000-4000-8000-000000000004");
const QUuid kIdUserInfo = id("a5000000-0000-4000-8000-000000000005");
const QUuid kIdChat = id("a5000000-0000-4000-8000-000000000006");
const QUuid kIdOverlayStep = id("a5000000-0000-4000-8000-000000000007");
const QUuid kIdGroup = id("a5000000-0000-4000-8000-000000000032");
const QUuid kIdDelay = id("a5000000-0000-4000-8000-000000000036");
const QUuid kOverlay = id("a6000000-0000-4000-8000-000000000001");
const QUuid kSourceImage = id("a7000000-0000-4000-8000-000000000001");
const QUuid kSourceName = id("a7000000-0000-4000-8000-000000000002");
const QString kSoundFile = QStringLiteral("media/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.mp3");

ActionsConfig loadConfig()
{
    QFile f(QStringLiteral(DSS_TEST_AKTIONEN_DIR "/vollstaendig.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("vollstaendig.json fehlt");
    ActionsConfig config;
    QString error;
    if (!parseActions(f.readAll(), &config, &error))
        qFatal("vollstaendig.json ungültig: %s", qPrintable(error));
    return config;
}

ActionViewNames names()
{
    ActionViewNames n;
    n.overlays.insert(kOverlay, QStringLiteral("Chat-Bot"));
    n.sources.insert(kSourceImage, QStringLiteral("Begrüssung Bild"));
    n.sources.insert(kSourceName, QStringLiteral("Begrüssung Name"));
    n.media.insert(kSoundFile, QStringLiteral("gong.mp3"));
    return n;
}

// Die Aktion aus einem action_put, so wie der Kern sie liest
Action putAction(const EditResult &r, int index = 0)
{
    Action a;
    QString error;
    if (r.edits.size() <= index || r.edits.at(index).type != QLatin1String("action_put")
        || !actionFromJson(r.edits.at(index).fields.value(QStringLiteral("action")).toObject(), &a, &error))
        qWarning("kein gültiges action_put: %s", qPrintable(error));
    return a;
}

QList<QUuid> topLevelIds(const QList<Step> &steps)
{
    QList<QUuid> ids;
    for (const Step &s : steps)
        ids.append(s.id);
    return ids;
}

QStringList rowTexts(const QList<ActionListRow> &rows)
{
    QStringList texts;
    for (const ActionListRow &r : rows)
        texts.append(QStringLiteral("%1%2").arg(QString(r.depth * 2, QLatin1Char(' ')), r.text));
    return texts;
}

VariableValue value(const QJsonValue &v, VariableOrigin origin = VariableOrigin::Studio)
{
    return {v, origin, QDateTime(QDate(2026, 11, 15), QTime(12, 0), QTimeZone::UTC)};
}

VariablesData variables()
{
    VariablesData d;
    d.global.insert(QStringLiteral("firstChatterPlace"), value(2, VariableOrigin::Streamerbot));
    d.global.insert(QStringLiteral("motto"), value(QStringLiteral("Hallo Welt")));
    d.users[QStringLiteral("100000001")].insert(QStringLiteral("firsts"), value(3, VariableOrigin::Streamerbot));
    d.users[QStringLiteral("100000002")].insert(QStringLiteral("firsts"), value(1));
    d.users[QStringLiteral("100000002")].insert(QStringLiteral("watchtime"), value(1234.5, VariableOrigin::Streamerbot));
    d.commandCounters[kCommandShoutout].counter = value(42);
    return d;
}

ViewersData viewers()
{
    ViewersData v;
    v.viewers.insert(QStringLiteral("100000001"), {QStringLiteral("zuschauer_eins"), QStringLiteral("Zuschauer_Eins"), {}});
    v.viewers.insert(QStringLiteral("100000002"), {QStringLiteral("zwei"), QString(), {}});
    return v;
}

} // namespace

class TestActionView : public QObject
{
    Q_OBJECT

private slots:
    // --- Allgemein ---

    void texts()
    {
        QCOMPARE(durationText(500), QStringLiteral("500 ms"));
        QCOMPARE(durationText(1500), QStringLiteral("1.5 s"));
        QCOMPARE(durationText(1250), QStringLiteral("1.25 s"));
        QCOMPARE(durationText(10000), QStringLiteral("10 s"));
        QCOMPARE(durationText(90000), QStringLiteral("90 s"));
        QCOMPARE(durationText(1200000), QStringLiteral("20 min"));
        QCOMPARE(savedStatusText(QStringLiteral("Begrüssung")), QStringLiteral("Gespeichert: Begrüssung"));
        QCOMPARE(copyName(QStringLiteral("A"), {QStringLiteral("A")}), QStringLiteral("A (Kopie)"));
        QCOMPARE(copyName(QStringLiteral("A"), {QStringLiteral("A"), QStringLiteral("A (Kopie)")}),
                 QStringLiteral("A (Kopie 2)"));
        // Ein Name mit «%1» wird nicht als Platzhalter von arg() ersetzt
        QCOMPARE(copyName(QStringLiteral("%1 x"), {QStringLiteral("%1 x (Kopie)")}), QStringLiteral("%1 x (Kopie 2)"));
    }

    void keptSelectionAfterUpdate()
    {
        const QList<QUuid> before{kBegruessung, kShoutoutRun, kShoutoutCommand};
        QCOMPARE(keptSelection(before, before, kShoutoutRun), kShoutoutRun);
        // gelöscht: die nächste, am Ende die davor, ohne Liste keine
        QCOMPARE(keptSelection(before, {kBegruessung, kShoutoutCommand}, kShoutoutRun), kShoutoutCommand);
        QCOMPARE(keptSelection(before, {kBegruessung, kShoutoutRun}, kShoutoutCommand), kShoutoutRun);
        QCOMPARE(keptSelection(before, {}, kShoutoutRun), QUuid());
        QCOMPARE(keptSelection(before, before, QUuid()), QUuid());
        // nach einem Filter nicht mehr sichtbar: die nächste sichtbare
        QCOMPARE(keptSelection(before, {kBegruessung}, kShoutoutCommand), kBegruessung);
    }

    // --- «Aktionen», links ---

    void actionListInFileOrder()
    {
        const ActionsConfig config = loadConfig();
        const QList<ActionListRow> rows = actionListRows(config, QString(), {});
        // Gruppen dort, wo ihre erste Aktion steht; ohne Gruppe auf der obersten Ebene (Plan 3.2)
        QCOMPARE(rowTexts(rows), (QStringList{QStringLiteral("Begrüssung"), QStringLiteral("  Begrüssung"),
                                              QStringLiteral("Shoutout"), QStringLiteral("  Shoutout ausführen"),
                                              QStringLiteral("  Shoutout per Befehl"), QStringLiteral("First Chatters"),
                                              QStringLiteral("Test"), QStringLiteral("  Alle Auslöser")}));
        QVERIFY(rows.at(0).isGroup);
        QCOMPARE(rows.at(2).detail, QStringLiteral("(2)"));
        QCOMPARE(rows.at(1).actionId, kBegruessung);
        QCOMPARE(rows.at(1).detail, QStringLiteral("Erste Nachricht"));
        QCOMPARE(rows.at(4).detail, QStringLiteral("!so"));
        QVERIFY(!rows.at(5).isGroup);
        QCOMPARE(rows.at(5).depth, 0);
        QCOMPARE(rows.at(7).enabled, false);
        // nur aktive Auslöser, Zeitnachricht mit Abstand
        QCOMPARE(rows.at(7).detail,
                 QStringLiteral("Follow, Abo, Abo verlängert, Geschenk-Abo, Geschenk-Bündel, Cheer, Raid, "
                                "Stream online, Stream offline, alle 20 min, Test"));
    }

    void actionListSearch()
    {
        const ActionsConfig config = loadConfig();
        // Name der Gruppe passt: alle ihre Aktionen
        QCOMPARE(rowTexts(actionListRows(config, QStringLiteral("  shoutout "), {})),
                 (QStringList{QStringLiteral("Shoutout"), QStringLiteral("  Shoutout ausführen"),
                              QStringLiteral("  Shoutout per Befehl")}));
        // Befehl im Auslöser passt: nur diese Aktion, Gruppe mit Anzahl der gezeigten
        const QList<ActionListRow> rows = actionListRows(config, QStringLiteral("!SO"), {});
        QCOMPARE(rowTexts(rows), (QStringList{QStringLiteral("Shoutout"), QStringLiteral("  Shoutout per Befehl")}));
        QCOMPARE(rows.at(0).detail, QStringLiteral("(1)"));
        QCOMPARE(rowTexts(actionListRows(config, QStringLiteral("chatters"), {})),
                 QStringList{QStringLiteral("First Chatters")});
        QVERIFY(actionListRows(config, QStringLiteral("gibt es nicht"), {}).isEmpty());
    }

    void warningsAtRows()
    {
        const ActionsConfig config = loadConfig();
        // Verweise mit leeren Overlays und Medien: Overlay und Datei fehlen
        ReferenceContext context;
        context.overlays = QHash<QUuid, QSet<QUuid>>();
        context.mediaFiles = QSet<QString>();
        const QList<ReferenceWarning> warnings = checkReferences(config, context);
        const QList<ActionListRow> rows = actionListRows(config, QString(), warnings);
        QVERIFY(rows.at(1).warning.contains(QStringLiteral("Overlay fehlt")));
        QVERIFY(rows.at(5).warning.contains(QStringLiteral("regex")));
        QVERIFY(rows.at(3).warning.isEmpty());
        // am Schritt nur seine eigenen
        const QList<StepRow> steps = stepRows(*config.action(kBegruessung), config, names(), warnings);
        QVERIFY(steps.at(6).warning.contains(QStringLiteral("Overlay fehlt")));
        QVERIFY(steps.at(5).warning.isEmpty());
        // Befehl: «!so» wird nicht doppelt gemeldet, Gruppe «Stammgäste» gibt es
        QVERIFY(warningText(warnings, kCommandShoutout).isEmpty());
    }

    void newCopyRemoveReset()
    {
        ActionsConfig config = loadConfig();
        EditResult r = newAction(config, QStringLiteral(" Shoutout "));
        QCOMPARE(r.edits.size(), 1);
        Action a = putAction(r);
        QCOMPARE(a.id, r.created);
        QCOMPARE(a.name, QStringLiteral("Neue Aktion"));
        QCOMPARE(a.group, QStringLiteral("Shoutout"));
        QVERIFY(a.triggers.isEmpty() && a.steps.isEmpty());
        config.actions.append(a);
        QCOMPARE(putAction(newAction(config, QString())).name, QStringLiteral("Neue Aktion 2"));

        // «Neue Gruppe»: eine neue Aktion in einer neuen Gruppe
        a = putAction(newActionGroup(config));
        QCOMPARE(a.group, QStringLiteral("Neue Gruppe"));
        config.actions.append(a);
        QCOMPARE(putAction(newActionGroup(config)).group, QStringLiteral("Neue Gruppe 2"));

        // Kopieren: Der Kern legt die Kopie an (action_copy, Plan 5)
        r = copyAction(config, kBegruessung);
        QCOMPARE(r.edits, (QList<ActionsEdit>{{QStringLiteral("action_copy"),
                                               {{QStringLiteral("action_id"), uuidText(kBegruessung)}}}}));
        QVERIFY(copyAction(config, QUuid::createUuid()).edits.isEmpty());
        QCOMPARE(removeAction(config, kShoutoutRun).edits.first().type, QStringLiteral("action_remove"));
        QVERIFY(removeAction(config, QUuid::createUuid()).edits.isEmpty());

        // Zurücksetzen nur bei mitgelieferten (template_id)
        QVERIFY(actionResettable(*config.action(kBegruessung)));
        QVERIFY(!actionResettable(*config.action(kShoutoutRun)));
        QCOMPARE(resetAction(config, kBegruessung).edits.first().type, QStringLiteral("action_reset"));
        QVERIFY(resetAction(config, kShoutoutRun).edits.isEmpty());
        QVERIFY(resetActionQuestion(*config.action(kBegruessung)).contains(QStringLiteral("«Begrüssung»")));
    }

    void renameAndRemoveGroup()
    {
        const ActionsConfig config = loadConfig();
        // Umbenennen: je Aktion der Gruppe ein action_put, sonst unverändert
        EditResult r = renameActionGroup(config, QStringLiteral("Shoutout"), QStringLiteral(" Shoutouts "));
        QVERIFY(r.problem.isEmpty());
        QCOMPARE(r.edits.size(), 2);
        Action a = putAction(r, 0);
        QCOMPARE(a.id, kShoutoutRun);
        QCOMPARE(a.group, QStringLiteral("Shoutouts"));
        a.group = QStringLiteral("Shoutout");
        QCOMPARE(a, *config.action(kShoutoutRun));
        QCOMPARE(putAction(r, 1).id, kShoutoutCommand);
        QCOMPARE(putAction(r, 1).group, QStringLiteral("Shoutouts"));
        // mitgelieferte Aktionen auch (Plan 2.3: Gruppe nur zum Ordnen)
        QCOMPARE(putAction(renameActionGroup(config, QStringLiteral("Begrüssung"), QStringLiteral("Hallo"))).templateId,
                 config.action(kBegruessung)->templateId);
        // leer, bestehende Gruppe (kein Zusammenführen), gleich oder unbekannt
        r = renameActionGroup(config, QStringLiteral("Shoutout"), QStringLiteral("  "));
        QCOMPARE(r.problem, QStringLiteral("Der Name darf nicht leer sein."));
        QVERIFY(r.edits.isEmpty());
        r = renameActionGroup(config, QStringLiteral("Shoutout"), QStringLiteral("Test"));
        QCOMPARE(r.problem, QStringLiteral("Die Gruppe «Test» gibt es schon."));
        QVERIFY(r.edits.isEmpty());
        // ohne Rücksicht auf Gross- und Kleinschreibung (Entscheid 2026-11-16): andere Schreibweise einer
        // anderen Gruppe abgewiesen, mit deren Schreibweise; eigene Schreibweise ändern erlaubt
        r = renameActionGroup(config, QStringLiteral("Shoutout"), QStringLiteral("TEST"));
        QCOMPARE(r.problem, QStringLiteral("Die Gruppe «Test» gibt es schon."));
        QVERIFY(r.edits.isEmpty());
        r = renameActionGroup(config, QStringLiteral("Shoutout"), QStringLiteral("shoutout"));
        QVERIFY(r.problem.isEmpty());
        QCOMPARE(r.edits.size(), 2);
        QCOMPARE(putAction(r, 0).group, QStringLiteral("shoutout"));
        QCOMPARE(putAction(r, 1).group, QStringLiteral("shoutout"));
        r = renameActionGroup(config, QStringLiteral("SHOUTOUT"), QStringLiteral("Shoutouts"));
        QCOMPARE(r.edits.size(), 2);
        QCOMPARE(putAction(r, 1).group, QStringLiteral("Shoutouts"));
        r = renameActionGroup(config, QStringLiteral("Shoutout"), QStringLiteral("Shoutout "));
        QVERIFY(r.edits.isEmpty() && r.problem.isEmpty());
        r = renameActionGroup(config, QStringLiteral("gibt es nicht"), QStringLiteral("Neu"));
        QVERIFY(r.edits.isEmpty() && r.problem.isEmpty());
        QVERIFY(renameActionGroup(config, QString(), QStringLiteral("Neu")).edits.isEmpty());

        // Löschen: alle Aktionen der Gruppe, in der Reihenfolge der Datei
        QCOMPARE(removeActionGroup(config, QStringLiteral("Shoutout")).edits,
                 (QList<ActionsEdit>{{QStringLiteral("action_remove"), {{QStringLiteral("action_id"), uuidText(kShoutoutRun)}}},
                                     {QStringLiteral("action_remove"),
                                      {{QStringLiteral("action_id"), uuidText(kShoutoutCommand)}}}}));
        QVERIFY(removeActionGroup(config, QStringLiteral("gibt es nicht")).edits.isEmpty());
        // Aktionen ohne Gruppe sind keine Gruppe
        QVERIFY(removeActionGroup(config, QString()).edits.isEmpty());
    }

    void groupsIgnoreCase()
    {
        const ActionsConfig original = loadConfig();
        // «Shoutout ausführen» steht vor «Shoutout per Befehl»; dieser bekommt «shoutout»
        ActionsConfig config = original;
        for (Action &a : config.actions) {
            if (a.id == kShoutoutCommand)
                a.group = QStringLiteral("shoutout");
        }

        // Liste: eine Gruppe in der Schreibweise der ersten Aktion, auch an ihren Aktionen
        const QList<ActionListRow> rows = actionListRows(config, QString(), {});
        QCOMPARE(rowTexts(rows), rowTexts(actionListRows(original, QString(), {})));
        int groupRows = 0;
        for (const ActionListRow &row : rows) {
            if (row.isGroup && row.group.compare(QStringLiteral("shoutout"), Qt::CaseInsensitive) == 0) {
                ++groupRows;
                QCOMPARE(row.text, QStringLiteral("Shoutout"));
                QCOMPARE(row.detail, QStringLiteral("(2)"));
            }
            if (row.actionId == kShoutoutCommand)
                QCOMPARE(row.group, QStringLiteral("Shoutout"));
        }
        QCOMPARE(groupRows, 1);
        QCOMPARE(actionGroupChoices(config), actionGroupChoices(original));

        // Löschen und Rückfrage über beide Schreibweisen
        const QList<ActionsEdit> removeAll = removeActionGroup(original, QStringLiteral("Shoutout")).edits;
        QCOMPARE(removeActionGroup(config, QStringLiteral("shoutout")).edits, removeAll);
        QCOMPARE(removeActionGroup(config, QStringLiteral("Shoutout")).edits, removeAll);
        const QString question =
            QStringLiteral("Gruppe «Shoutout» mit 2 Aktionen löschen? Sie werden von 1 Befehl benutzt.");
        QCOMPARE(removeActionGroupQuestion(config, QStringLiteral("shoutout")), question);
        QCOMPARE(removeActionGroupQuestion(config, QStringLiteral("SHOUTOUT")), question);

        // Umbenennen in die Schreibweise der ersten Aktion: nur die abweichende Aktion
        EditResult r = renameActionGroup(config, QStringLiteral("shoutout"), QStringLiteral("Shoutout"));
        QCOMPARE(r.edits.size(), 1);
        QCOMPARE(putAction(r).id, kShoutoutCommand);
        QCOMPARE(putAction(r).group, QStringLiteral("Shoutout"));

        // Neu in der Gruppe und «Neue Gruppe» übernehmen bzw. meiden bestehende Schreibweisen
        QCOMPARE(putAction(newAction(original, QStringLiteral(" shoutout "))).group, QStringLiteral("Shoutout"));
        Action lower = putAction(newAction(original, QStringLiteral("neue gruppe")));
        QCOMPARE(lower.group, QStringLiteral("neue gruppe"));
        ActionsConfig withLower = original;
        withLower.actions.append(lower);
        QCOMPARE(putAction(newActionGroup(withLower)).group, QStringLiteral("Neue Gruppe 2"));

        // Feld «Gruppe»: bestehende Schreibweise einer anderen Aktion wird übernommen
        const Action begruessung = *original.action(kBegruessung);
        Action after = begruessung;
        after.group = QStringLiteral(" shoutout ");
        QCOMPARE(putAction(putAction(original, begruessung, after)).group, QStringLiteral("Shoutout"));
        // allein in der Gruppe: die eigene Schreibweise darf sich ändern
        after.group = QStringLiteral("begrüssung");
        QCOMPARE(putAction(putAction(original, begruessung, after)).group, QStringLiteral("begrüssung"));
        // neue Gruppe: wie eingegeben, ohne Leerzeichen am Rand
        after.group = QStringLiteral(" Neu ");
        QCOMPARE(putAction(putAction(original, begruessung, after)).group, QStringLiteral("Neu"));
    }

    void removeQuestions()
    {
        ActionsConfig config = loadConfig();
        // Aktion: von «Aktion ausführen» einer anderen, von einer Zeitnachricht, ohne Verweise
        QCOMPARE(removeActionQuestion(config, kShoutoutRun),
                 QStringLiteral("Aktion «Shoutout ausführen» löschen? Sie wird von 1 Aktion benutzt."));
        QCOMPARE(removeActionQuestion(config, kShoutoutCommand),
                 QStringLiteral("Aktion «Shoutout per Befehl» löschen? Sie wird von 1 Befehl benutzt."));
        QCOMPARE(removeActionQuestion(config, kAllTriggers),
                 QStringLiteral("Aktion «Alle Auslöser» löschen? Sie wird von 1 Zeitnachricht benutzt."));
        QCOMPARE(removeActionQuestion(config, kFirstChatters), QString());
        QCOMPARE(removeActionQuestion(config, QUuid::createUuid()), QString());
        // Gruppe: immer, Verweise nur von ausserhalb («Aktion ausführen» innerhalb zählt nicht)
        QCOMPARE(removeActionGroupQuestion(config, QStringLiteral("Shoutout")),
                 QStringLiteral("Gruppe «Shoutout» mit 2 Aktionen löschen? Sie werden von 1 Befehl benutzt."));
        QCOMPARE(removeActionGroupQuestion(config, QStringLiteral("Begrüssung")),
                 QStringLiteral("Gruppe «Begrüssung» mit 1 Aktion löschen?"));
        QCOMPARE(removeActionGroupQuestion(config, QStringLiteral("Test")),
                 QStringLiteral("Gruppe «Test» mit 1 Aktion löschen? Sie wird von 1 Zeitnachricht benutzt."));
        QCOMPARE(removeActionGroupQuestion(config, QStringLiteral("gibt es nicht")), QString());
        QCOMPARE(removeActionGroupQuestion(config, QString()), QString());

        // mehrere Verweise: Befehl und Zeitnachricht dazu, Aktion ausführen aus zwei anderen Aktionen
        Action *run = &config.actions[1]; // Shoutout ausführen
        Trigger command;
        command.id = QUuid::createUuid();
        command.type = QString::fromLatin1(kTriggerCommand);
        command.fields.insert(QStringLiteral("command_id"), uuidText(kCommandDiscord));
        run->triggers.append(command);
        Trigger missing = command; // Befehl, den es nicht gibt, zählt nicht
        missing.fields.insert(QStringLiteral("command_id"), uuidText(QUuid::createUuid()));
        run->triggers.append(missing);
        Trigger timer;
        timer.type = QString::fromLatin1(kTriggerTimer);
        timer.fields.insert(QStringLiteral("timer_id"), uuidText(kTimerRandom));
        run->triggers.append(timer);
        Step call;
        call.id = QUuid::createUuid();
        call.type = QString::fromLatin1(kStepRunAction);
        call.fields.insert(QStringLiteral("action_id"), uuidText(kShoutoutRun));
        config.actions[3].steps.append(call); // First Chatters
        QCOMPARE(removeActionQuestion(config, kShoutoutRun),
                 QStringLiteral("Aktion «Shoutout ausführen» löschen? Sie wird von 1 Befehl, 1 Zeitnachricht "
                                "und 2 Aktionen benutzt."));
        QCOMPARE(removeCommandQuestion(config, kCommandDiscord),
                 QStringLiteral("Befehl «!discord» löschen? Er wird von 1 Aktion benutzt."));
        config.actions[0].triggers.append(command);
        QCOMPARE(removeCommandQuestion(config, kCommandDiscord),
                 QStringLiteral("Befehl «!discord» löschen? Er wird von 2 Aktionen benutzt."));
    }

    // --- «Aktionen», Bereich «Aktion» ---

    void actionHeader()
    {
        const ActionsConfig config = loadConfig();
        const Action &a = *config.action(kBegruessung);
        QCOMPARE(actionTitle(a), QStringLiteral("Aktion «Begrüssung»"));
        const QList<QueueChoice> choices = queueChoices(config, a.queue);
        QCOMPARE(choices, (QList<QueueChoice>{{QUuid(), QStringLiteral("Standard"), false},
                                              {kQueueAlerts, QStringLiteral("Alerts (nacheinander)"), false},
                                              {kQueueMessages, QStringLiteral("Meldungen (nacheinander)"), false},
                                              {QUuid(), QStringLiteral("Warteschlangen verwalten …"), true}}));
        QCOMPARE(queueChoiceIndex(choices, a.queue), 2);
        QCOMPARE(queueChoiceIndex(choices, QUuid()), 0);
        // fehlende Warteschlange bleibt wählbar sichtbar
        const QUuid missing = QUuid::createUuid();
        const QList<QueueChoice> withMissing = queueChoices(config, missing);
        QCOMPARE(withMissing.at(1).label, QStringLiteral("Warteschlange fehlt"));
        QCOMPARE(queueChoiceIndex(withMissing, missing), 1);
        QCOMPARE(actionGroupChoices(config),
                 (QStringList{QStringLiteral("Begrüssung"), QStringLiteral("Shoutout"), QStringLiteral("Test")}));

        // mitgeliefert, geändert (Vorlage kommt mit f)
        QCOMPARE(templateHint(*config.action(kShoutoutRun), nullptr), QString());
        QCOMPARE(templateHint(a, nullptr), QStringLiteral("mitgeliefert"));
        Action templ = a;
        templ.id = QUuid::createUuid();
        QCOMPARE(templateHint(a, &templ), QStringLiteral("mitgeliefert"));
        templ.steps.removeLast();
        QCOMPARE(templateHint(a, &templ), QStringLiteral("mitgeliefert, geändert"));
    }

    void putActionChecks()
    {
        const ActionsConfig config = loadConfig();
        const Action &before = *config.action(kBegruessung);
        Action after = before;
        QVERIFY(putAction(before, after).edits.isEmpty());
        QVERIFY(putAction(before, after).problem.isEmpty());
        after.name = QStringLiteral("   ");
        EditResult r = putAction(before, after);
        QCOMPARE(r.problem, QStringLiteral("Der Name darf nicht leer sein."));
        QVERIFY(r.edits.isEmpty());
        // Leerzeichen am Rand zählen nicht als Änderung
        after.name = QStringLiteral(" Begrüssung ");
        QVERIFY(putAction(before, after).edits.isEmpty());
        after.name = QStringLiteral("Hallo");
        after.queue = kQueueAlerts;
        after.enabled = false;
        const Action saved = putAction(putAction(before, after));
        QCOMPARE(saved.name, QStringLiteral("Hallo"));
        QCOMPARE(saved.queue, kQueueAlerts);
        QVERIFY(!saved.enabled);
        // unbekannte Felder bleiben (Plan 2.1)
        QCOMPARE(saved.extra, before.extra);
        // Was der Kern nicht liest, geht nicht hinaus: doppelte Kennung
        after = before;
        after.triggers.append(after.triggers.first());
        r = putAction(before, after);
        QVERIFY(r.problem.contains(QStringLiteral("mehrfach")));
        QVERIFY(r.edits.isEmpty());
    }

    // --- «Aktionen», Bereich «Auslöser» ---

    void triggerTexts()
    {
        ActionsConfig config = loadConfig();
        const QList<TriggerRow> rows = triggerRows(*config.action(kAllTriggers), config, {});
        QCOMPARE(rows.size(), 12);
        QCOMPARE(rows.at(0).text, QStringLiteral("Twitch · Chat-Nachricht"));
        QVERIFY(!rows.at(0).enabled);
        QCOMPARE(rows.at(2).detail, QStringLiteral("Stufe 1, 3"));
        QCOMPARE(rows.at(3).detail, QStringLiteral("ab 2 Monaten"));
        QCOMPARE(rows.at(5).detail, QStringLiteral("ab 5 Abos"));
        QCOMPARE(rows.at(6).detail, QStringLiteral("ab 1 Bits"));
        QCOMPARE(rows.at(7).detail, QStringLiteral("1 bis 100 Zuschauer"));
        QCOMPARE(rows.at(8).detail, QStringLiteral("nicht beim Start des Studios im laufenden Stream"));
        QCOMPARE(rows.at(10).text, QStringLiteral("Zeitnachricht · Info alle 20 min"));
        QCOMPARE(rows.at(10).detail, QStringLiteral("alle 20 min"));
        QCOMPARE(rows.at(11).text, QStringLiteral("Test"));
        QCOMPARE(rows.at(11).detail, QStringLiteral("2 Testwerte"));
        QCOMPARE(triggerRows(*config.action(kShoutoutCommand), config, {}).first().text,
                 QStringLiteral("Befehl · !so"));
        // Cheer: nur «bis» heisst genau dieser Wert (Plan 2.5)
        EditResult r = setTriggerField(*config.action(kAllTriggers), id("a4000000-0000-4000-8000-000000000016"),
                                       QStringLiteral("min"), 0);
        Action a = putAction(r);
        r = setTriggerField(a, id("a4000000-0000-4000-8000-000000000016"), QStringLiteral("max"), 100);
        a = putAction(r);
        QCOMPARE(triggerRows(a, config, {}).at(6).detail, QStringLiteral("genau 100 Bits"));
        // fehlender Befehl
        config.commands.removeIf([](const Command &c) { return c.id == kCommandShoutout; });
        QCOMPARE(triggerRows(*config.action(kShoutoutCommand), config, {}).first().text,
                 QStringLiteral("Befehl · Befehl fehlt"));
    }

    void triggerEdits()
    {
        const ActionsConfig config = loadConfig();
        const Action &before = *config.action(kBegruessung);
        EditResult r = addTrigger(before, QString::fromLatin1(kTriggerRaid));
        Action a = putAction(r);
        QCOMPARE(a.triggers.size(), 2);
        QCOMPARE(a.triggers.last().id, r.created);
        QCOMPARE(a.triggers.last().fields.value(QStringLiteral("min")).toInt(), 0);
        QVERIFY(!addTrigger(before, QStringLiteral("gibt.es.nicht")).problem.isEmpty());

        a = putAction(setTriggerEnabled(a, r.created, false));
        QVERIFY(!a.triggers.last().enabled);
        // Prüfmeldung am Feld, keine Änderung
        r = setTriggerField(a, a.triggers.last().id, QStringLiteral("min"), -1);
        QVERIFY(!r.problem.isEmpty());
        QVERIFY(r.edits.isEmpty());
        r = setTriggerField(a, a.triggers.last().id, QStringLiteral("farbe"), 1);
        QCOMPARE(r.problem, QStringLiteral("Unbekanntes Feld «farbe»."));
        a = putAction(removeTrigger(a, a.triggers.last().id));
        QCOMPARE(a.triggers, before.triggers);
        QVERIFY(removeTrigger(a, QUuid::createUuid()).edits.isEmpty());

        // Menü «+»: alle Typen, Twitch zuerst
        const QList<TypeChoice> choices = triggerChoices();
        QCOMPARE(choices.size(), triggerTypes().size());
        QCOMPARE(choices.first().section, QStringLiteral("Twitch"));
        QCOMPARE(choices.last().section, QStringLiteral("Chat-Bot"));
    }

    // --- «Aktionen», Bereich «Schritte» ---

    void stepTree()
    {
        const ActionsConfig config = loadConfig();
        const QList<StepRow> rows = stepRows(*config.action(kBegruessung), config, names(), {});
        QStringList numbers;
        for (const StepRow &r : rows)
            numbers.append(r.number);
        QCOMPARE(numbers, (QStringList{QStringLiteral("1"), QStringLiteral("1.1"), QStringLiteral("1.1.1"),
                                       QStringLiteral("1.2"), QStringLiteral("2"), QStringLiteral("3"),
                                       QStringLiteral("4")}));
        QCOMPARE(rows.at(0).type, QStringLiteral("Wenn"));
        QCOMPARE(rows.at(0).summary, QStringLiteral("Wenn %role% ist gleich «4»"));
        QVERIFY(rows.at(0).hasChildren);
        QCOMPARE(rows.at(1).type, QStringLiteral("Dann"));
        QVERIFY(rows.at(1).branch);
        QCOMPARE(rows.at(2).depth, 2);
        QCOMPARE(rows.at(2).summary, QStringLiteral("Lauf beenden"));
        QVERIFY(!rows.at(3).hasChildren);
        QCOMPARE(rows.at(4).summary, QStringLiteral("Benutzer: %userName%"));
        QCOMPARE(rows.at(5).summary, QStringLiteral("Bot: «Willkommen, %user%! 100% Freude»"));
        QCOMPARE(rows.at(6).summary,
                 QStringLiteral("zeigen: «Begrüssung Bild», «Begrüssung Name» in Overlay «Chat-Bot» · 8 s"));
        // ohne Namen: was fehlt, steht da
        QCOMPARE(stepSummary(config.action(kBegruessung)->steps.at(3), config, {}),
                 QStringLiteral("zeigen: Element fehlt, Element fehlt in Overlay fehlt · 8 s"));
    }

    void stepSummaries()
    {
        const ActionsConfig config = loadConfig();
        QList<StepRow> rows = stepRows(*config.action(kFirstChatters), config, names(), {});
        QCOMPARE(rows.at(0).summary, QStringLiteral("Variable «firstChatterPlace» + 1 (für alle, dauerhaft)"));
        QCOMPARE(rows.at(1).summary,
                 QStringLiteral("Variable «firsts» (je Zuschauer (Ziel), flüchtig) → %firstsBisher%"));
        // abgeschaltete Gruppe: sie und alles darin rot durchgestrichen (Plan 3.1)
        QCOMPARE(rows.at(2).summary, QStringLiteral("Zufall, zufällig einer"));
        QVERIFY(!rows.at(2).enabled && rows.at(2).inactive);
        QVERIFY(rows.at(3).enabled && rows.at(3).inactive);
        QCOMPARE(rows.at(3).summary, QStringLiteral("nur eine Notiz"));
        QCOMPARE(rows.at(4).summary, QStringLiteral("Wenn %firstsBisher% passt zum Regex «^[0-9]+$» (wird nicht ausgeführt)"));
        QCOMPARE(rows.at(6).summary, QStringLiteral("500 ms bis 60 min, zufällig"));
        QVERIFY(rows.at(6).inactive);
        QVERIFY(!rows.at(0).inactive);

        rows = stepRows(*config.action(kShoutoutRun), config, names(), {});
        QCOMPARE(rows.at(1).summary, QStringLiteral("für %targetUserName%"));
        QCOMPARE(rows.at(2).summary, QStringLiteral("Kanal: «Schaut bei %targetUser% vorbei»"));
        QVERIFY(rows.at(2).inactive);
        QCOMPARE(rows.at(3).summary, QStringLiteral("Kanal: «Danke ~raidDank~» · als Antwort"));

        rows = stepRows(*config.action(kShoutoutCommand), config, names(), {});
        QCOMPARE(rows.at(0).summary, QStringLiteral("%shoutoutLogin% = «%input0%»"));
        QCOMPARE(rows.at(1).summary, QStringLiteral("Aktion «Shoutout ausführen», sofort"));

        rows = stepRows(*config.action(kAllTriggers), config, names(), {});
        QCOMPARE(rows.at(0).summary, QStringLiteral("gong.mp3 · 150 % · nicht warten"));
        QCOMPARE(rows.at(1).summary, QStringLiteral("Quelle «Kamera» in Szene «test 2»: umschalten"));
        QCOMPARE(rows.at(2).type, QStringLiteral("Skript-Schritt nicht verfügbar"));
        QCOMPARE(rows.at(3).type, QStringLiteral("nicht unterstützt: 16"));
        QVERIFY(rows.at(3).inactive);
        // ohne Namen der Medien: der Verweis
        QVERIFY(stepSummary(config.action(kAllTriggers)->steps.at(0), config, {}).startsWith(QStringLiteral("media/")));
    }

    void stepMenu()
    {
        const QList<TypeChoice> choices = stepChoices();
        QStringList keys;
        for (const TypeChoice &c : choices)
            keys.append(c.key);
        QCOMPARE(keys.first(), QString::fromLatin1(kStepChatSend));
        QCOMPARE(choices.first().section, QStringLiteral("Chat und Twitch"));
        // nur anlegbare Schritte, die in P14-1 laufen (OBS kommt mit P14-2)
        for (const char *hidden : {kStepIfTrue, kStepIfFalse, kStepScript, kStepUnsupported, kStepObsSourceVisible})
            QVERIFY2(!keys.contains(QString::fromLatin1(hidden)), hidden);
        for (const StepTypeInfo &t : stepTypes()) {
            if (t.creatable && t.part == StepPart::P14_1)
                QVERIFY2(keys.contains(t.key), qPrintable(t.key));
        }
        QCOMPARE(keys.size(), QSet<QString>(keys.cbegin(), keys.cend()).size());
    }

    void addSteps()
    {
        const ActionsConfig config = loadConfig();
        const Action &before = *config.action(kBegruessung);
        // ohne Auswahl ans Ende
        EditResult r = addStep(before, QUuid(), QString::fromLatin1(kStepDelay));
        Action a = putAction(r);
        QCOMPARE(a.steps.size(), 5);
        QCOMPARE(a.steps.last().id, r.created);
        QCOMPARE(a.steps.last().fields.value(QStringLiteral("value_ms")).toInt(), 1000);
        // nach dem gewählten Schritt
        r = addStep(before, kIdUserInfo, QString::fromLatin1(kStepChatSend));
        a = putAction(r);
        QCOMPARE(topLevelIds(a.steps).indexOf(r.created), 2);
        // in «Dann» ans Ende
        r = addStep(before, kIdIfTrue, QString::fromLatin1(kStepComment));
        a = putAction(r);
        QCOMPARE(a.steps.at(0).steps.at(0).steps.size(), 2);
        QCOMPARE(a.steps.at(0).steps.at(0).steps.last().id, r.created);
        // in eine Gruppe
        r = addStep(*config.action(kFirstChatters), kIdGroup, QString::fromLatin1(kStepBreak));
        a = putAction(r);
        QCOMPARE(a.steps.at(2).steps.last().id, r.created);
        // nach einem «Wenn» auf derselben Ebene; ein neues «Wenn» hat «Dann» und «Sonst»
        r = addStep(before, kIdIf, QString::fromLatin1(kStepIf));
        a = putAction(r);
        QCOMPARE(a.steps.at(1).id, r.created);
        QCOMPARE(a.steps.at(1).steps.size(), 2);
        QCOMPARE(a.steps.at(1).steps.at(0).type, QString::fromLatin1(kStepIfTrue));
        // nicht anlegbar
        r = addStep(before, QUuid(), QString::fromLatin1(kStepScript));
        QVERIFY(!r.problem.isEmpty());
        QVERIFY(r.edits.isEmpty() && r.created.isNull());
    }

    void removeMoveToggle()
    {
        const ActionsConfig config = loadConfig();
        const Action &before = *config.action(kBegruessung);
        Action a = putAction(removeStep(before, kIdIf));
        QCOMPARE(topLevelIds(a.steps), (QList<QUuid>{kIdUserInfo, kIdChat, kIdOverlayStep}));
        a = putAction(removeStep(before, kIdBreak));
        QVERIFY(a.steps.at(0).steps.at(0).steps.isEmpty());
        // «Dann» und «Sonst» bleiben
        QVERIFY(removeStep(before, kIdIfTrue).edits.isEmpty());
        // Rückfrage nur bei Inhalt, gezählt ohne «Dann» und «Sonst»
        QCOMPARE(removeStepQuestion(before, kIdIf), QStringLiteral("Schritt «Wenn» samt 1 Schritt darin löschen?"));
        QCOMPARE(removeStepQuestion(*config.action(kFirstChatters), kIdGroup),
                 QStringLiteral("Schritt «Gruppe» samt 3 Schritten darin löschen?"));
        QCOMPARE(removeStepQuestion(before, kIdChat), QString());
        QCOMPARE(removeStepQuestion(before, kIdIfTrue), QString());
        QCOMPARE(removeStepQuestion(before, QUuid::createUuid()), QString());
        Action emptyIf = putAction(removeStep(before, kIdBreak));
        QCOMPARE(removeStepQuestion(emptyIf, kIdIf), QString());
        QVERIFY(moveStep(before, kIdIfFalse, -1).edits.isEmpty());
        QVERIFY(setStepEnabled(before, kIdIfTrue, false).edits.isEmpty());

        a = putAction(moveStep(before, kIdChat, -1));
        QCOMPARE(topLevelIds(a.steps), (QList<QUuid>{kIdIf, kIdChat, kIdUserInfo, kIdOverlayStep}));
        a = putAction(moveStep(before, kIdChat, +1));
        QCOMPARE(topLevelIds(a.steps), (QList<QUuid>{kIdIf, kIdUserInfo, kIdOverlayStep, kIdChat}));
        QVERIFY(moveStep(before, kIdIf, -1).edits.isEmpty());
        QVERIFY(moveStep(before, kIdOverlayStep, +1).edits.isEmpty());
        QVERIFY(moveStep(before, kIdBreak, +1).edits.isEmpty()); // allein in «Dann»

        a = putAction(setStepEnabled(before, kIdChat, false));
        QVERIFY(!a.steps.at(2).enabled);
        QVERIFY(setStepEnabled(before, kIdChat, true).edits.isEmpty()); // schon an

        StepButtons b = stepButtons(before, kIdIf);
        QVERIFY(b.canRemove && !b.canMoveUp && b.canMoveDown);
        b = stepButtons(before, kIdOverlayStep);
        QVERIFY(b.canRemove && b.canMoveUp && !b.canMoveDown);
        b = stepButtons(before, kIdIfTrue);
        QVERIFY(!b.canRemove && !b.canMoveUp && !b.canMoveDown);
        b = stepButtons(before, QUuid());
        QVERIFY(!b.canRemove);
    }

    void stepFields()
    {
        const ActionsConfig config = loadConfig();
        const Action &before = *config.action(kFirstChatters);
        Action a = putAction(setStepField(before, kIdDelay, QStringLiteral("value_ms"), 2000));
        QCOMPARE(stepSummary(a.steps.at(2).steps.at(1).steps.at(0).steps.at(0), config, {}),
                 QStringLiteral("2 s bis 60 min, zufällig"));
        // Pause höchstens 1 h (Plan 2.4)
        EditResult r = setStepField(before, kIdDelay, QStringLiteral("value_ms"), 3600001);
        QVERIFY(!r.problem.isEmpty());
        QVERIFY(r.edits.isEmpty());
        r = setStepField(before, kIdDelay, QStringLiteral("random"), QStringLiteral("ja"));
        QCOMPARE(r.problem, QStringLiteral("muss true oder false sein"));
        QCOMPARE(setStepField(before, kIdDelay, QStringLiteral("text"), QString()).problem,
                 QStringLiteral("Unbekanntes Feld «text»."));
        // unveränderter Wert: nichts
        QVERIFY(setStepField(before, kIdDelay, QStringLiteral("value_ms"), 500).edits.isEmpty());
        QVERIFY(setStepField(before, QUuid::createUuid(), QStringLiteral("value_ms"), 1).edits.isEmpty());
    }

    // --- Ausführen: Aktion ---

    void linkCommandAndTimer()
    {
        const ActionsConfig config = loadConfig();
        const QList<ActionChoice> choices = actionChoices(config);
        QCOMPARE(choices.first(), (ActionChoice{QUuid(), QStringLiteral("Keine")}));
        QCOMPARE(choices.at(1).label, QStringLiteral("Alle Auslöser"));
        QCOMPARE(choices.size(), config.actions.size() + 1);

        QCOMPARE(commandAction(config, kCommandShoutout), kShoutoutCommand);
        QCOMPARE(commandAction(config, kCommandDiscord), QUuid());
        QCOMPARE(timerAction(config, kTimerInfo), kAllTriggers);
        QCOMPARE(timerAction(config, kTimerRandom), QUuid());

        // ohne Aktion → neue: ein Auslöser kommt dazu
        EditResult r = setCommandAction(config, kCommandDiscord, kBegruessung);
        QCOMPARE(r.edits.size(), 1);
        Action a = putAction(r);
        QCOMPARE(a.id, kBegruessung);
        QCOMPARE(a.triggers.last().id, r.created);
        QCOMPARE(a.triggers.last().type, QString::fromLatin1(kTriggerCommand));
        QCOMPARE(a.triggers.last().fields.value(QStringLiteral("command_id")).toString(), uuidText(kCommandDiscord));

        // andere Aktion: die bisherige gibt den Auslöser ab
        r = setCommandAction(config, kCommandShoutout, kFirstChatters);
        QCOMPARE(r.edits.size(), 2);
        QCOMPARE(putAction(r, 0).id, kShoutoutCommand);
        QVERIFY(putAction(r, 0).triggers.isEmpty());
        QCOMPARE(putAction(r, 1).id, kFirstChatters);
        QCOMPARE(putAction(r, 1).triggers.size(), 1);

        // keine Aktion
        r = setCommandAction(config, kCommandShoutout, QUuid());
        QCOMPARE(r.edits.size(), 1);
        QVERIFY(putAction(r).triggers.isEmpty());
        QVERIFY(setCommandAction(config, kCommandShoutout, kShoutoutCommand).edits.isEmpty());
        QCOMPARE(setCommandAction(config, kCommandShoutout, QUuid::createUuid()).problem,
                 QStringLiteral("Die Aktion fehlt."));

        // Zeitnachricht wie Befehl
        r = setTimerAction(config, kTimerInfo, kBegruessung);
        QCOMPARE(r.edits.size(), 2);
        QCOMPARE(putAction(r, 0).triggers.size(), 11);
        QCOMPARE(putAction(r, 1).triggers.last().fields.value(QStringLiteral("timer_id")).toString(),
                 uuidText(kTimerInfo));
    }

    // --- Unterreiter «Befehle» ---

    void commandTable()
    {
        const ActionsConfig config = loadConfig();
        const VariablesData vars = variables();
        QList<CommandRow> rows = commandRows(config, QString(), QString(), &vars, {});
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).commands, QStringLiteral("!discord, !dc"));
        QCOMPARE(rows.at(0).group, QStringLiteral("Info"));
        QCOMPARE(rows.at(0).permissions, QStringLiteral("Alle"));
        QCOMPARE(rows.at(0).globalCooldown, QStringLiteral("5 s"));
        QCOMPARE(rows.at(0).userCooldown, QStringLiteral("—"));
        QCOMPARE(rows.at(0).actions, QStringLiteral("—"));
        QCOMPARE(rows.at(0).used, QStringLiteral("—"));
        QCOMPARE(rows.at(1).permissions, QStringLiteral("alle ausser Moderatoren, VIPs, Stammgäste, 1 Zuschauer"));
        QCOMPARE(rows.at(1).globalCooldown, QStringLiteral("—"));
        QCOMPARE(rows.at(1).userCooldown, QStringLiteral("30 s"));
        QCOMPARE(rows.at(1).actions, QStringLiteral("Shoutout per Befehl"));
        QCOMPARE(rows.at(1).used, QStringLiteral("42"));
        // Zähler noch nicht geholt
        QCOMPARE(commandRows(config, QString(), QString(), nullptr, {}).at(1).used, QStringLiteral("—"));

        // Filter Gruppe und Suche
        rows = commandRows(config, QString(), QStringLiteral("Info"), &vars, {});
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().id, kCommandDiscord);
        QCOMPARE(commandRows(config, QStringLiteral("DC"), QString(), &vars, {}).first().id, kCommandDiscord);
        QCOMPARE(commandRows(config, QStringLiteral("per befehl"), QString(), &vars, {}).first().id, kCommandShoutout);
        QVERIFY(commandRows(config, QStringLiteral("!so"), QStringLiteral("Info"), &vars, {}).isEmpty());
        QCOMPARE(commandGroupChoices(config), (QStringList{QString(), QStringLiteral("Info")}));
        QCOMPARE(commandGroupLabel(QString()), QStringLiteral("Alle Gruppen"));

        QCOMPARE(commandListHint(config), QStringLiteral("2 Befehle · Der Kanal darf immer und hat keine Abklingzeit."));
        ActionsConfig changed = config;
        changed.commands.first().enabled = false;
        changed.commands.removeLast();
        QCOMPARE(commandListHint(changed),
                 QStringLiteral("1 Befehl, 1 abgeschaltet · Der Kanal darf immer und hat keine Abklingzeit."));
        QCOMPARE(commandTitle(*config.command(kCommandDiscord)), QStringLiteral("Befehl «!discord»"));
        QCOMPARE(commandLocations().first().label, QStringLiteral("am Anfang der Nachricht"));
    }

    void commandPermissions()
    {
        const ActionsConfig config = loadConfig();
        const Permissions &shoutout = config.command(kCommandShoutout)->permissions;
        PermissionChoice c = permissionChoice(shoutout);
        QVERIFY(!c.all && c.vip && c.moderator && !c.subscriber);
        QCOMPARE(c.groups.size(), 1);
        QVERIFY(permissionChoice(config.command(kCommandDiscord)->permissions).all);

        // «Alle»: Rollen und Gruppen leer; «verbieten» und einzelne Zuschauer bleiben (P14-2)
        c.all = true;
        Permissions p = permissionsFromChoice(shoutout, c);
        QVERIFY(p.roles.isEmpty() && p.groups.isEmpty());
        QCOMPARE(p.grant, QStringLiteral("deny"));
        QCOMPARE(p.users, shoutout.users);
        QCOMPARE(p.extra, shoutout.extra);
        c = PermissionChoice{false, true, false, true, {}};
        p = permissionsFromChoice(config.command(kCommandDiscord)->permissions, c);
        QCOMPARE(p.roles, (QStringList{QStringLiteral("subscriber"), QStringLiteral("moderator")}));
        QCOMPARE(permissionsText(p, config), QStringLiteral("Abonnenten, Moderatoren"));
    }

    void commandEdits()
    {
        ActionsConfig config = loadConfig();
        QCOMPARE(commandsFromText(QStringLiteral(" !discord \n\n!dc\n  ")),
                 (QStringList{QStringLiteral("!discord"), QStringLiteral("!dc")}));
        QCOMPARE(commandsText({QStringLiteral("!a"), QStringLiteral("!b")}), QStringLiteral("!a\n!b"));

        const Command &before = *config.command(kCommandDiscord);
        Command after = before;
        QVERIFY(putCommand(before, after).edits.isEmpty());
        after.commands = {QStringLiteral(" !discord"), QString(), QStringLiteral("!dc ")};
        QVERIFY(putCommand(before, after).edits.isEmpty());
        after.name = QString();
        QCOMPARE(putCommand(before, after).problem, QStringLiteral("Der Name darf nicht leer sein."));
        after.name = QStringLiteral("Discord");
        after.userCooldownS = -1;
        QCOMPARE(putCommand(before, after).problem, QStringLiteral("Die Abklingzeit darf nicht negativ sein."));
        after.userCooldownS = 10;
        after.commands = commandsFromText(QStringLiteral("!discord\n!dc\n!server"));
        EditResult r = putCommand(before, after);
        QCOMPARE(r.edits.size(), 1);
        QCOMPARE(r.edits.first().type, QStringLiteral("command_put"));
        Command saved;
        QString error;
        QVERIFY2(commandFromJson(r.edits.first().fields.value(QStringLiteral("command")).toObject(), &saved, &error),
                 qPrintable(error));
        QCOMPARE(saved.commands.size(), 3);
        QCOMPARE(saved.userCooldownS, 10);

        r = newCommand(config);
        QVERIFY(commandFromJson(r.edits.first().fields.value(QStringLiteral("command")).toObject(), &saved, &error));
        QCOMPARE(saved.id, r.created);
        QCOMPARE(saved.name, QStringLiteral("Neuer Befehl"));
        r = copyCommand(config, kCommandShoutout);
        QVERIFY(commandFromJson(r.edits.first().fields.value(QStringLiteral("command")).toObject(), &saved, &error));
        QCOMPARE(saved.name, QStringLiteral("Shoutout (Kopie)"));
        QVERIFY(saved.id != kCommandShoutout);
        QCOMPARE(saved.permissions, config.command(kCommandShoutout)->permissions);
        QCOMPARE(removeCommand(config, kCommandShoutout).edits,
                 (QList<ActionsEdit>{{QStringLiteral("command_remove"),
                                      {{QStringLiteral("command_id"), uuidText(kCommandShoutout)}}}}));
        QVERIFY(removeCommand(config, QUuid::createUuid()).edits.isEmpty());
        // Rückfrage nur mit Verweisen
        QCOMPARE(removeCommandQuestion(config, kCommandShoutout),
                 QStringLiteral("Befehl «!so» löschen? Er wird von 1 Aktion benutzt."));
        QCOMPARE(removeCommandQuestion(config, kCommandDiscord), QString());
        QCOMPARE(removeCommandQuestion(config, QUuid::createUuid()), QString());
    }

    // --- Unterreiter «Zeitnachrichten» ---

    void timerTable()
    {
        const ActionsConfig config = loadConfig();
        const QList<TimerRow> rows = timerRows(config, {});
        QCOMPARE(rows.size(), 2);
        QCOMPARE(rows.at(0).name, QStringLiteral("Info alle 20 min"));
        QCOMPARE(rows.at(0).interval, QStringLiteral("20 min"));
        QCOMPARE(rows.at(0).lines, QStringLiteral("5"));
        QCOMPARE(rows.at(0).onlyLive, QStringLiteral("ja"));
        QCOMPARE(rows.at(0).actions, QStringLiteral("Alle Auslöser"));
        QVERIFY(!rows.at(1).enabled);
        QCOMPARE(rows.at(1).interval, QStringLiteral("einmal nach 1 min bis 10 min, zufällig"));
        QCOMPARE(rows.at(1).lines, QStringLiteral("—"));
        QCOMPARE(rows.at(1).onlyLive, QStringLiteral("nein"));
        QCOMPARE(rows.at(1).actions, QStringLiteral("—"));
        QCOMPARE(timerTitle(*config.timer(kTimerInfo)), QStringLiteral("Zeitnachricht «Info alle 20 min»"));
    }

    void timerEdits()
    {
        const ActionsConfig config = loadConfig();
        const Timer &before = *config.timer(kTimerInfo);
        Timer after = before;
        QVERIFY(putTimer(before, after).edits.isEmpty());
        after.intervalS = 59;
        QCOMPARE(putTimer(before, after).problem, QStringLiteral("Der Abstand muss mindestens 60 s sein."));
        after.intervalS = 600;
        after.random = true;
        after.randomMinS = 30;
        QCOMPARE(putTimer(before, after).problem, QStringLiteral("Der kürzeste Abstand muss mindestens 60 s sein."));
        after.randomMinS = 120;
        after.randomMaxS = 60;
        QCOMPARE(putTimer(before, after).problem,
                 QStringLiteral("Der längste Abstand darf nicht kürzer sein als der kürzeste."));
        after.randomMaxS = 300;
        EditResult r = putTimer(before, after);
        QCOMPARE(r.edits.size(), 1);
        Timer saved;
        QString error;
        QVERIFY2(timerFromJson(r.edits.first().fields.value(QStringLiteral("timer")).toObject(), &saved, &error),
                 qPrintable(error));
        QCOMPARE(saved.randomMaxS, 300);

        r = newTimer(config);
        QVERIFY(timerFromJson(r.edits.first().fields.value(QStringLiteral("timer")).toObject(), &saved, &error));
        QCOMPARE(saved.name, QStringLiteral("Neue Zeitnachricht"));
        QCOMPARE(saved.id, r.created);
        r = copyTimer(config, kTimerRandom);
        QVERIFY(timerFromJson(r.edits.first().fields.value(QStringLiteral("timer")).toObject(), &saved, &error));
        QCOMPARE(saved.name, QStringLiteral("Zufällig (Kopie)"));
        QCOMPARE(saved.extra, config.timer(kTimerRandom)->extra);
        QCOMPARE(removeTimer(config, kTimerInfo).edits.first().type, QStringLiteral("timer_remove"));
        QVERIFY(removeTimer(config, QUuid::createUuid()).edits.isEmpty());
        QCOMPARE(removeTimerQuestion(config, kTimerInfo),
                 QStringLiteral("Zeitnachricht «Info alle 20 min» löschen? Sie wird von 1 Aktion benutzt."));
        QCOMPARE(removeTimerQuestion(config, kTimerRandom), QString());
        QCOMPARE(removeTimerQuestion(config, QUuid::createUuid()), QString());
    }

    // --- Unterreiter «Variablen» ---

    void globalVariables()
    {
        const VolatileValues volatileValues{{QStringLiteral("letzterRaid"), QStringLiteral("niemand")}};
        QList<VariableRow> rows = globalVariableRows(variables(), volatileValues, QString());
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).name, QStringLiteral("firstChatterPlace"));
        QCOMPARE(rows.at(0).valueText, QStringLiteral("2"));
        QCOMPARE(rows.at(0).origin, QStringLiteral("Streamer.bot"));
        QCOMPARE(rows.at(0).persistedText, QStringLiteral("dauerhaft"));
        QCOMPARE(rows.at(1).name, QStringLiteral("letzterRaid"));
        QVERIFY(!rows.at(1).persisted);
        QCOMPARE(rows.at(1).persistedText, QStringLiteral("flüchtig"));
        QCOMPARE(rows.at(1).origin, QStringLiteral("—"));
        QCOMPARE(rows.at(2).origin, QStringLiteral("Studio"));
        // Suche in Name und Wert
        rows = globalVariableRows(variables(), volatileValues, QStringLiteral("WELT"));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().name, QStringLiteral("motto"));
    }

    void userVariables()
    {
        const VariablesData data = variables();
        VolatileUserValues volatileValues;
        volatileValues[QStringLiteral("100000009")].insert(QStringLiteral("firsts"), 7);
        QList<VariableRow> rows = userVariableRows(data, volatileValues, viewers(), QString(), QString());
        QStringList lines;
        for (const VariableRow &r : rows)
            lines.append(QStringLiteral("%1 %2 %3").arg(r.viewer, r.name, r.valueText));
        // nach Zuschauer (Anzeigename, sonst Login, sonst unbekannt), dann Name
        QCOMPARE(lines, (QStringList{QStringLiteral("unbekannt (100000009) firsts 7"),
                                     QStringLiteral("Zuschauer_Eins firsts 3"), QStringLiteral("zwei firsts 1"),
                                     QStringLiteral("zwei watchtime 1234.5")}));
        QCOMPARE(rows.at(1).origin, QStringLiteral("Streamer.bot"));
        QCOMPARE(rows.at(1).userId, QStringLiteral("100000001"));
        QVERIFY(!rows.at(0).persisted);

        QCOMPARE(userVariableRows(data, volatileValues, viewers(), QString(), QStringLiteral("watchtime")).size(), 1);
        // Suche auch im Login
        rows = userVariableRows(data, volatileValues, viewers(), QStringLiteral("zuschauer_EINS"), QString());
        QCOMPARE(rows.size(), 1);
        rows = userVariableRows(data, {}, viewers(), QStringLiteral("zwei"), QStringLiteral("firsts"));
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().valueText, QStringLiteral("1"));
        QCOMPARE(userVariableNames(data, volatileValues),
                 (QStringList{QString(), QStringLiteral("firsts"), QStringLiteral("watchtime")}));
        QCOMPARE(variableNameLabel(QString()), QStringLiteral("Alle Variablen"));
    }

    void variableEdits()
    {
        const QList<VariableRow> users = userVariableRows(variables(), {}, viewers(), QString(), QString());
        const VariableRow &firsts = users.at(0); // Zuschauer_Eins, 3
        // Eine Zahl bleibt eine Zahl
        EditResult r = setVariableValue(firsts, QStringLiteral("drei"));
        QCOMPARE(r.problem, QStringLiteral("«drei» ist keine Zahl."));
        QVERIFY(r.edits.isEmpty());
        QVERIFY(setVariableValue(firsts, QStringLiteral(" 3 ")).edits.isEmpty());
        r = setVariableValue(firsts, QStringLiteral("4"));
        QCOMPARE(r.edits, (QList<ActionsEdit>{{QStringLiteral("variable_set"),
                                               {{QStringLiteral("scope"), QStringLiteral("user")},
                                                {QStringLiteral("user_id"), QStringLiteral("100000001")},
                                                {QStringLiteral("name"), QStringLiteral("firsts")},
                                                {QStringLiteral("persisted"), true},
                                                {QStringLiteral("value"), 4}}}}));
        // Text bleibt Text, auch wenn er wie eine Zahl aussieht
        const VariableRow motto = globalVariableRows(variables(), {}, QStringLiteral("motto")).first();
        r = setVariableValue(motto, QStringLiteral("42"));
        QCOMPARE(r.edits.first().fields.value(QStringLiteral("value")), QJsonValue(QStringLiteral("42")));
        QVERIFY(!r.edits.first().fields.contains(QStringLiteral("user_id")));
        QCOMPARE(removeVariable(variables(), {}, {}, motto).edits,
                 (QList<ActionsEdit>{{QStringLiteral("variable_remove"),
                                      {{QStringLiteral("scope"), QStringLiteral("global")},
                                       {QStringLiteral("name"), QStringLiteral("motto")},
                                       {QStringLiteral("persisted"), true}}}}));
        QCOMPARE(removeVariable(variables(), {}, {}, firsts).edits.first().type, QStringLiteral("variable_remove"));
        // Löschen nur, was es noch gibt (dauerhaft bzw. flüchtig)
        VariableRow gone = motto;
        gone.name = QStringLiteral("gibt es nicht");
        QVERIFY(removeVariable(variables(), {}, {}, gone).edits.isEmpty());
        VariableRow volatileRow = firsts;
        volatileRow.persisted = false;
        QVERIFY(removeVariable(variables(), {}, {}, volatileRow).edits.isEmpty());
        const VolatileUserValues volatileUsers{{QStringLiteral("100000001"), {{QStringLiteral("firsts"), 1}}}};
        QCOMPARE(removeVariable(variables(), {}, volatileUsers, volatileRow).edits.size(), 1);
        VariableRow volatileGlobal = motto;
        volatileGlobal.persisted = false;
        QVERIFY(removeVariable(variables(), {}, {}, volatileGlobal).edits.isEmpty());
        QCOMPARE(removeVariable(variables(), {{QStringLiteral("motto"), 1}}, {}, volatileGlobal).edits.size(), 1);
        QCOMPARE(variableValueText(QJsonValue(0.1)), QStringLiteral("0.1"));
        QCOMPARE(variableValueText(QJsonValue(-5.0)), QStringLiteral("-5"));
    }
};

QTEST_GUILESS_MAIN(TestActionView)
#include "tst_aktionsansicht.moc"
