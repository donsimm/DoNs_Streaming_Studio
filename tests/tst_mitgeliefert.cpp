// Mitgelieferte Aktionen (P14 f1, Plan 7): Vorlage src/actions/vorlage/aktionen.json als Qt-Ressource,
// Zustände beim Anlegen wie die Tabelle im Plan, Verweise (ohne Overlay «Chat-Bot», das kommt mit
// f2), Anlegen durch den Kern bei fehlender Datei, vorhandene Datei bleibt unberührt, Zurücksetzen
// (Kennung bleibt), Wiederherstellen einer gelöschten, dss-core --format-pruefen auf der Vorlage.
// Ohne Qt Gui; der Kern läuft als eigener Prozess mit eigenem Datenordner.

#include "actionedit.h"
#include "actionjson.h"
#include "actionrefs.h"
#include "actiontemplate.h"
#include "actionview.h"
#include "dataformat.h"
#include "prozesshilfe.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace dss::actions;
using namespace testhelp;

namespace {

QString idText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

const Action *byName(const ActionsConfig &config, const QString &name)
{
    for (const Action &a : config.actions) {
        if (a.name == name)
            return &a;
    }
    return nullptr;
}

const Command *commandByName(const ActionsConfig &config, const QString &name)
{
    for (const Command &c : config.commands) {
        if (c.name == name)
            return &c;
    }
    return nullptr;
}

QString queueName(const ActionsConfig &config, const QUuid &id)
{
    if (id.isNull())
        return QStringLiteral("Standard");
    const Queue *q = config.queue(id);
    return q ? q->name : QStringLiteral("fehlt");
}

// Aktion als JSON ohne die Kennungen von Auslösern und Schritten (die erneuert Zurücksetzen)
QJsonValue withoutIds(const QJsonValue &v)
{
    if (v.isArray()) {
        QJsonArray a;
        for (const QJsonValue &item : v.toArray())
            a.append(withoutIds(item));
        return a;
    }
    if (!v.isObject())
        return v;
    QJsonObject o = v.toObject();
    o.remove(QStringLiteral("id"));
    for (auto it = o.begin(); it != o.end(); ++it)
        it.value() = withoutIds(it.value());
    return o;
}

QJsonObject shape(const Action &a)
{
    QJsonObject o = actionToJson(a);
    QJsonObject result = withoutIds(o).toObject();
    result.insert(QStringLiteral("id"), o.value(QStringLiteral("id"))); // Kennung der Aktion bleibt
    return result;
}

ActionsConfig edited(ActionsConfig config, const QString &type, const QJsonObject &fields, ConfigEditResult *r = nullptr)
{
    const ConfigEditResult result = applyConfigEdit(&config, type, fields);
    if (r)
        *r = result;
    if (!result.ok)
        qWarning("%s: %s", qPrintable(type), qPrintable(result.message));
    return config;
}

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestMitgeliefert : public QObject
{
    Q_OBJECT

private slots:
    void templateIsValid()
    {
        QString error;
        const ActionsConfig &t = actionTemplate(&error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(t.actions.size(), 18);
        QVERIFY(!t.enabled); // Hauptschalter beim ersten Start aus
        QSet<QUuid> templateIds;
        for (const Action &a : t.actions) {
            QVERIFY2(!a.templateId.isNull(), qPrintable(a.name));
            QVERIFY2(!templateIds.contains(a.templateId), qPrintable(a.name));
            templateIds.insert(a.templateId);
            QCOMPARE(templateAction(a.templateId), &a);
        }
        QVERIFY(missingTemplates(t).isEmpty());
        QCOMPARE(missingTemplates(ActionsConfig()).size(), 18);
    }

    // Plan 7, Tabelle: Auslöser, Warteschlange, Zustand beim Anlegen
    void statesLikePlan_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QStringList>("triggers");
        QTest::addColumn<QString>("queue");
        QTest::addColumn<bool>("enabled");

        const QString command = QString::fromLatin1(kTriggerCommand);
        QTest::newRow("!befehle") << QStringLiteral("Info: !befehle") << QStringList{command} << QStringLiteral("Standard") << true;
        QTest::newRow("!discord") << QStringLiteral("Info: !discord") << QStringList{command} << QStringLiteral("Standard") << false;
        QTest::newRow("!regeln") << QStringLiteral("Info: !regeln") << QStringList{command} << QStringLiteral("Standard") << false;
        QTest::newRow("!specs") << QStringLiteral("Info: !specs") << QStringList{command} << QStringLiteral("Standard") << false;
        QTest::newRow("Zeitnachricht") << QStringLiteral("Zeitnachricht Info") << QStringList{QStringLiteral("timer")}
                                       << QStringLiteral("Standard") << true;
        QTest::newRow("Vorstellung") << QStringLiteral("Vorstellung beim Streamstart")
                                     << QStringList{QStringLiteral("twitch.stream_online")} << QStringLiteral("Standard") << true;
        QTest::newRow("Begrüssung") << QStringLiteral("Begrüssung") << QStringList{QStringLiteral("twitch.first_message")}
                                    << QStringLiteral("Meldungen") << true;
        QTest::newRow("Shoutout ausführen") << QStringLiteral("Shoutout ausführen") << QStringList()
                                            << QStringLiteral("Meldungen") << true;
        QTest::newRow("Shoutout per Befehl") << QStringLiteral("Shoutout per Befehl") << QStringList{command}
                                             << QStringLiteral("Standard") << true;
        QTest::newRow("Shoutout bei Raid") << QStringLiteral("Shoutout bei Raid") << QStringList{QStringLiteral("twitch.raid")}
                                           << QStringLiteral("Standard") << true;
        QTest::newRow("First Chatters") << QStringLiteral("First Chatters") << QStringList{QStringLiteral("twitch.first_message")}
                                        << QStringLiteral("Erste Chatter") << true;
        QTest::newRow("First Chatters zurücksetzen") << QStringLiteral("First Chatters zurücksetzen")
                                                     << QStringList{QStringLiteral("twitch.stream_offline")}
                                                     << QStringLiteral("Erste Chatter") << true;
        QTest::newRow("Chat-Sound") << QStringLiteral("Chat-Sound (Vorlage)") << QStringList{command}
                                    << QStringLiteral("Standard") << false;
        QTest::newRow("Alert Follow") << QStringLiteral("Alert Follow") << QStringList{QStringLiteral("twitch.follow")}
                                      << QStringLiteral("Alerts") << true;
        QTest::newRow("Alert Sub") << QStringLiteral("Alert Sub")
                                   << QStringList{QStringLiteral("twitch.subscribe"), QStringLiteral("twitch.resubscribe")}
                                   << QStringLiteral("Alerts") << true;
        QTest::newRow("Alert Geschenk") << QStringLiteral("Alert Geschenk") << QStringList{QStringLiteral("twitch.gift_bomb")}
                                        << QStringLiteral("Alerts") << true;
        QTest::newRow("Alert Cheer") << QStringLiteral("Alert Cheer") << QStringList{QStringLiteral("twitch.cheer")}
                                     << QStringLiteral("Alerts") << true;
        QTest::newRow("Alert Raid") << QStringLiteral("Alert Raid") << QStringList{QStringLiteral("twitch.raid")}
                                    << QStringLiteral("Alerts") << true;
    }

    void statesLikePlan()
    {
        QFETCH(QString, name);
        QFETCH(QStringList, triggers);
        QFETCH(QString, queue);
        QFETCH(bool, enabled);
        const ActionsConfig &t = actionTemplate();
        const Action *a = byName(t, name);
        QVERIFY2(a, qPrintable(name));
        QStringList types;
        for (const Trigger &trigger : a->triggers)
            types.append(trigger.type);
        QCOMPARE(types, triggers);
        QCOMPARE(queueName(t, a->queue), queue);
        QCOMPARE(a->enabled, enabled);
        for (const Trigger &trigger : a->triggers) {
            if (trigger.type == QLatin1String(kTriggerCommand)) {
                // Befehl im selben Zustand wie seine Aktion (die Info-Befehle aus, bis der Nutzer
                // seinen Text einträgt)
                const Command *c = t.command(QUuid::fromString(trigger.fields.value(QStringLiteral("command_id")).toString()));
                QVERIFY(c);
                QCOMPARE(c->enabled, enabled);
            }
        }
    }

    void detailsLikePlan()
    {
        const ActionsConfig &t = actionTemplate();
        for (const Queue &q : t.queues)
            QVERIFY2(q.blocking, qPrintable(q.name)); // «nacheinander»
        // Abklingzeit Kanal: Info 10 s, Chat-Sound 30 s; !so nur Moderatoren
        for (const QString &name : {QStringLiteral("Befehle"), QStringLiteral("Discord"), QStringLiteral("Regeln"),
                                    QStringLiteral("Specs")})
            QCOMPARE(commandByName(t, name)->globalCooldownS, 10);
        QCOMPARE(commandByName(t, QStringLiteral("Chat-Sound (Vorlage)"))->globalCooldownS, 30);
        QCOMPARE(commandByName(t, QStringLiteral("Shoutout"))->commands, QStringList{QStringLiteral("!so")});
        QCOMPARE(commandByName(t, QStringLiteral("Shoutout"))->permissions.roles, QStringList{QStringLiteral("moderator")});
        // Zeitnachricht alle 20 min, 5 Zeilen, nur im Stream
        QCOMPARE(t.timers.size(), 1);
        QCOMPARE(t.timers.at(0).intervalS, 1200);
        QCOMPARE(t.timers.at(0).lines, 5);
        QVERIFY(t.timers.at(0).onlyLive);
        // Streamstart: nicht beim Start des Studios im laufenden Stream; Pause 60 s
        const Action *intro = byName(t, QStringLiteral("Vorstellung beim Streamstart"));
        QVERIFY(intro->triggers.at(0).fields.value(QStringLiteral("skip_at_start")).toBool());
        QCOMPARE(intro->steps.at(0).fields.value(QStringLiteral("value_ms")).toInteger(), 60000);
        // Cheer und Raid ab 1
        for (const QString &name : {QStringLiteral("Alert Cheer"), QStringLiteral("Alert Raid"), QStringLiteral("Shoutout bei Raid")})
            QCOMPARE(byName(t, name)->triggers.at(0).fields.value(QStringLiteral("min")).toInteger(), 1);
        // Töne der Alerts und Chat-Sound: Schritt aus, keine Datei
        for (const Action &a : t.actions) {
            forEachStep(a.steps, [&](const Step &s, const QList<int> &) {
                if (s.type == QLatin1String(kStepPlaySound)) {
                    QVERIFY2(!s.enabled, qPrintable(a.name));
                    QVERIFY(s.fields.value(QStringLiteral("file")).toString().isEmpty());
                }
            });
        }
        // First Chatters: dieselben Namen wie Streamer.bot
        QSet<QString> variables;
        forEachStep(byName(t, QStringLiteral("First Chatters"))->steps, [&](const Step &s, const QList<int> &) {
            if (s.type == QLatin1String(kStepGlobalSet))
                variables.insert(s.fields.value(QStringLiteral("name")).toString());
        });
        QCOMPARE(variables, (QSet<QString>{QStringLiteral("firstChatterPlace"), QStringLiteral("firsts"),
                                           QStringLiteral("seconds"), QStringLiteral("thirds")}));
        // Shoutout per Befehl und bei Raid rufen «Shoutout ausführen» sofort auf
        const Action *run = byName(t, QStringLiteral("Shoutout ausführen"));
        for (const QString &name : {QStringLiteral("Shoutout per Befehl"), QStringLiteral("Shoutout bei Raid")}) {
            const Step &call = byName(t, name)->steps.at(1);
            QCOMPARE(call.type, QString::fromLatin1(kStepRunAction));
            QCOMPARE(call.fields.value(QStringLiteral("action_id")).toString(), idText(run->id));
            QVERIFY(call.fields.value(QStringLiteral("immediately")).toBool());
        }
    }

    // Verweise gültig bis auf das Overlay «Chat-Bot» (f2); alle Schritte nutzen dessen feste Kennungen
    void references()
    {
        const ActionsConfig &t = actionTemplate();
        ReferenceContext none;
        none.overlays = QHash<QUuid, QSet<QUuid>>();
        none.mediaFiles = QSet<QString>();
        int overlaySteps = 0;
        QSet<QUuid> elements;
        for (const ChatBotElement &e : kChatBotElements)
            elements.insert(QUuid::fromString(QLatin1String(e.id)));
        QCOMPARE(elements.size(), 9);
        QSet<QUuid> used;
        for (const Action &a : t.actions) {
            forEachStep(a.steps, [&](const Step &s, const QList<int> &) {
                if (s.type != QLatin1String(kStepOverlaySource))
                    return;
                ++overlaySteps;
                QCOMPARE(s.fields.value(QStringLiteral("overlay")).toString(), QString::fromLatin1(kChatBotOverlayId));
                for (const QJsonValue &e : s.fields.value(QStringLiteral("elements")).toArray()) {
                    const QUuid id = QUuid::fromString(e.toObject().value(QStringLiteral("source")).toString());
                    QVERIFY(elements.contains(id));
                    used.insert(id);
                }
            });
        }
        QCOMPARE(overlaySteps, 7); // Begrüssung, Shoutout, fünf Alerts
        QCOMPARE(used, elements);  // jedes Element wird benutzt
        const QList<ReferenceWarning> warnings = checkReferences(t, none);
        QCOMPARE(warnings.size(), overlaySteps);
        for (const ReferenceWarning &w : warnings)
            QVERIFY2(w.message.endsWith(QStringLiteral(": Overlay fehlt")), qPrintable(w.message));

        // Mit dem Overlay aus f2: keine Warnung
        ReferenceContext withOverlay = none;
        withOverlay.overlays->insert(QUuid::fromString(QLatin1String(kChatBotOverlayId)), elements);
        QVERIFY(checkReferences(t, withOverlay).isEmpty());
    }

    void coreCreatesMissingFile()
    {
        std::unique_ptr<QTemporaryDir> run = runtimeDir();
        QTemporaryDir data;
        QVERIFY(run->isValid() && data.isValid());
        const QString path = data.filePath(QString::fromLatin1(kActionsFileName));
        QVERIFY(!QFile::exists(path));
        QString log;
        std::unique_ptr<QProcess> core = startCore(data.path(), run->path() + QStringLiteral("/a.sock"));
        QVERIFY2(waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        core->terminate();
        QVERIFY(core->waitForFinished(10000));
        QCOMPARE(core->exitCode(), 0);
        log += QString::fromUtf8(core->readAllStandardError());
        QVERIFY2(log.contains(QStringLiteral("mitgelieferte Aktionen angelegt")), qPrintable(log));

        ActionsConfig file;
        QString error;
        QVERIFY2(parseActions(readFile(path), &file, &error), qPrintable(error));
        QCOMPARE(file, actionTemplate());
        QVERIFY(!file.enabled);
    }

    void existingFileUntouched()
    {
        std::unique_ptr<QTemporaryDir> run = runtimeDir();
        QTemporaryDir data;
        QVERIFY(run->isValid() && data.isValid());
        const QString path = data.filePath(QString::fromLatin1(kActionsFileName));
        // Leerer Stand: der Nutzer hat alles gelöscht; der Kern legt nichts dazu
        const QByteArray empty = serializeActions(ActionsConfig());
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write(empty) == empty.size());
        }
        QString log;
        std::unique_ptr<QProcess> core = startCore(data.path(), run->path() + QStringLiteral("/a.sock"));
        QVERIFY2(waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        core->terminate();
        QVERIFY(core->waitForFinished(10000));
        QCOMPARE(core->exitCode(), 0);
        log += QString::fromUtf8(core->readAllStandardError());
        QVERIFY2(!log.contains(QStringLiteral("mitgelieferte Aktionen angelegt")), qPrintable(log));
        QCOMPARE(readFile(path), empty);
    }

    void resetToTemplate()
    {
        const ActionsConfig &t = actionTemplate();
        const Action *original = byName(t, QStringLiteral("Begrüssung"));
        Action changed = *original;
        changed.name = QStringLiteral("Hallo");
        changed.enabled = false;
        changed.steps.removeLast();
        changed.triggers.clear();
        ActionsConfig c = edited(t, QStringLiteral("action_put"), {{QStringLiteral("action"), actionToJson(changed)}});
        c = edited(c, QStringLiteral("actions_set_enabled"), {{QStringLiteral("enabled"), true}});
        QVERIFY(c.action(original->id)->name == QStringLiteral("Hallo"));

        ConfigEditResult r;
        c = edited(c, QStringLiteral("action_reset"), {{QStringLiteral("action_id"), idText(original->id)}}, &r);
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(r.name, QStringLiteral("Begrüssung"));
        const Action *reset = c.action(original->id);
        QVERIFY(reset);
        QCOMPARE(shape(*reset), shape(*original));
        QCOMPARE(c.actions.size(), t.actions.size());
        QCOMPARE(c.actions.indexOf(*reset), t.actions.indexOf(*original)); // Platz in der Liste bleibt
        QVERIFY(c.enabled); // Hauptschalter unberührt

        // Kennung bleibt auch bei einer Aktion mit anderer Kennung (zum Beispiel übernommen)
        Action other = *original;
        other.id = QUuid::createUuid();
        other.name = QStringLiteral("Andere");
        for (Trigger &tr : other.triggers)
            tr.id = QUuid::createUuid();
        other.steps.clear();
        ActionsConfig d = edited(c, QStringLiteral("action_remove"), {{QStringLiteral("action_id"), idText(original->id)}});
        d = edited(d, QStringLiteral("action_put"), {{QStringLiteral("action"), actionToJson(other)}});
        d = edited(d, QStringLiteral("action_reset"), {{QStringLiteral("action_id"), idText(other.id)}}, &r);
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(d.action(other.id)->name, original->name);
        QCOMPARE(d.action(other.id)->steps.size(), original->steps.size());

        // Eigene Aktion: abgewiesen
        Action own;
        own.id = QUuid::createUuid();
        own.name = QStringLiteral("Eigene");
        ActionsConfig e = edited(t, QStringLiteral("action_put"), {{QStringLiteral("action"), actionToJson(own)}});
        r = applyConfigEdit(&e, QStringLiteral("action_reset"), {{QStringLiteral("action_id"), idText(own.id)}});
        QVERIFY(!r.ok);
        QCOMPARE(r.message, QStringLiteral("«Eigene» ist keine mitgelieferte Aktion"));
        // Beide oder keines der Felder: abgewiesen
        QVERIFY(!applyConfigEdit(&e, QStringLiteral("action_reset"), {}).ok);
        QVERIFY(!applyConfigEdit(&e, QStringLiteral("action_reset"),
                                 {{QStringLiteral("action_id"), idText(original->id)},
                                  {QStringLiteral("template_id"), idText(original->templateId)}})
                     .ok);
    }

    void restoreDeleted()
    {
        const ActionsConfig &t = actionTemplate();
        const Action *run = byName(t, QStringLiteral("Shoutout ausführen"));
        const Action *command = byName(t, QStringLiteral("Shoutout per Befehl"));
        const Command *so = commandByName(t, QStringLiteral("Shoutout"));
        ActionsConfig c = edited(t, QStringLiteral("action_remove"), {{QStringLiteral("action_id"), idText(run->id)}});
        c = edited(c, QStringLiteral("action_remove"), {{QStringLiteral("action_id"), idText(command->id)}});
        c = edited(c, QStringLiteral("command_remove"), {{QStringLiteral("command_id"), idText(so->id)}});
        // Warteschlange «Meldungen» gelöscht, eine eigene mit dem Namen angelegt
        c = edited(c, QStringLiteral("queue_remove"), {{QStringLiteral("queue_id"), idText(run->queue)}});
        const QUuid ownQueue = QUuid::createUuid();
        c = edited(c, QStringLiteral("queue_put"),
                   {{QStringLiteral("queue"), queueToJson({ownQueue, QStringLiteral("meldungen"), false, {}})}});
        QCOMPARE(missingTemplates(c), (QList<QUuid>{run->templateId, command->templateId}));
        QCOMPARE(dss::missingTemplateNames(c), (QStringList{run->name, command->name}));
        QVERIFY(dss::restoreTemplatesQuestion(c).contains(run->name));

        // Wie das Fenster: je fehlende Aktion ein action_reset mit template_id
        const dss::EditResult restore = dss::restoreTemplateActions(c);
        QCOMPARE(restore.edits.size(), 2);
        QList<QUuid> created;
        for (const dss::ActionsEdit &e : restore.edits) {
            QCOMPARE(e.type, QStringLiteral("action_reset"));
            ConfigEditResult r;
            c = edited(c, e.type, e.fields, &r);
            QVERIFY2(r.ok, qPrintable(r.message));
            created.append(r.created);
        }
        // Kennung der Vorlage (frei), am Ende der Liste, Befehl wieder da, Warteschlange gleichen Namens
        QCOMPARE(created, (QList<QUuid>{run->id, command->id}));
        QCOMPARE(c.actions.at(c.actions.size() - 2).id, run->id);
        QCOMPARE(c.actions.last().id, command->id);
        QVERIFY(c.command(so->id));
        QCOMPARE(c.action(run->id)->queue, ownQueue);
        QCOMPARE(c.queues.size(), t.queues.size());
        QVERIFY(missingTemplates(c).isEmpty());
        QVERIFY(dss::restoreTemplateActions(c).edits.isEmpty());
        QVERIFY(dss::restoreTemplatesQuestion(c).isEmpty());
        QCOMPARE(shape(*c.action(run->id)).value(QStringLiteral("steps")), shape(*run).value(QStringLiteral("steps")));
        // Wiederhergestellte ohne Warnung (Ziel von «Aktion ausführen», Befehl, Warteschlange);
        // «Begrüssung» meldet die gelöschte Warteschlange weiter
        for (const ReferenceWarning &w : checkReferences(c, ReferenceContext()))
            QVERIFY2(w.objectId != run->id && w.objectId != command->id, qPrintable(w.message));

        // Schon vorhanden: abgewiesen
        const ConfigEditResult again = applyConfigEdit(&c, QStringLiteral("action_reset"),
                                                       {{QStringLiteral("template_id"), idText(run->templateId)}});
        QVERIFY(!again.ok);
        QCOMPARE(again.message, QStringLiteral("Die mitgelieferte Aktion «Shoutout ausführen» gibt es schon"));
        // Unbekannte Vorlage: abgewiesen
        QVERIFY(!applyConfigEdit(&c, QStringLiteral("action_reset"),
                                 {{QStringLiteral("template_id"), idText(QUuid::createUuid())}})
                     .ok);
    }

    // Kennung der Vorlage belegt (zum Beispiel durch eine übernommene Aktion): neue Kennung
    void restoreWithTakenId()
    {
        const ActionsConfig &t = actionTemplate();
        const Action *follow = byName(t, QStringLiteral("Alert Follow"));
        ActionsConfig c = edited(t, QStringLiteral("action_remove"), {{QStringLiteral("action_id"), idText(follow->id)}});
        Action own;
        own.id = follow->id;
        own.name = QStringLiteral("Eigener Follow");
        c = edited(c, QStringLiteral("action_put"), {{QStringLiteral("action"), actionToJson(own)}});
        ConfigEditResult r;
        c = edited(c, QStringLiteral("action_reset"), {{QStringLiteral("template_id"), idText(follow->templateId)}}, &r);
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY(r.created != follow->id);
        QCOMPARE(c.action(r.created)->templateId, follow->templateId);
        QCOMPARE(c.action(follow->id)->name, QStringLiteral("Eigener Follow"));
    }

    // Plan 2.11: dss-core --format-pruefen kennt die Vorlage ohne Fehler
    void formatCheckOnTemplate()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QFile resource(QString::fromLatin1(kActionTemplateResource));
        QVERIFY(resource.open(QIODevice::ReadOnly));
        const QByteArray bytes = resource.readAll();
        {
            QFile f(data.filePath(QString::fromLatin1(kActionsFileName)));
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size());
        }
        const dss::DataFormatStatus s = dss::checkDataFormat(data.path());
        QVERIFY2(s.unreadable.isEmpty(), qPrintable(s.unreadable.join(QLatin1Char('\n'))));
        QVERIFY(s.newer.isEmpty());
        QProcess p;
        p.start(QStringLiteral(DSS_CORE_BIN), {QStringLiteral("--datenordner"), data.path(), QStringLiteral("--format-pruefen")});
        QVERIFY(p.waitForFinished(30000));
        QCOMPARE(p.exitStatus(), QProcess::NormalExit);
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        const QString err = QString::fromUtf8(p.readAllStandardError());
        QCOMPARE(p.exitCode(), 0);
        QVERIFY2(out.startsWith(QStringLiteral("Format aktuell")), qPrintable(out + err));
        QVERIFY2(!err.contains(QStringLiteral("nicht lesbar")), qPrintable(err));
        QCOMPARE(readFile(data.filePath(QString::fromLatin1(kActionsFileName))), bytes);
    }
};

QTEST_GUILESS_MAIN(TestMitgeliefert)
#include "tst_mitgeliefert.moc"
