// Mitgelieferte Aktionen (P14 f1, Plan 7): Vorlage src/actions/vorlage/aktionen.json als Qt-Ressource,
// Zustände beim Anlegen wie die Tabelle im Plan, Verweise, Anlegen durch den Kern bei fehlender
// Datei, vorhandene Datei bleibt unberührt, Zurücksetzen (Kennung bleibt), Wiederherstellen einer
// gelöschten, dss-core --format-pruefen auf der Vorlage.
// f2: Overlay «Chat-Bot» (Elemente, Kennungen, Ordner, nicht doppelt, kaputte Datei bleibt),
// «Element fehlt» nach dem Löschen, und Plan 8: die Aktionen auf ein zweites, anders gestaltetes
// Overlay umgestellt zeigen dessen Aussehen, nichts hängt am mitgelieferten Overlay.
// Ohne Qt Gui; der Kern läuft als eigener Prozess mit eigenem Datenordner.

#include "actionedit.h"
#include "actionjson.h"
#include "actionrefs.h"
#include "actiontemplate.h"
#include "actionview.h"
#include "chatbotoverlay.h"
#include "dataformat.h"
#include "fontcatalog.h"
#include "overlaylivestate.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QDir>
#include <QFile>
#include <QRect>
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

QUuid chatBotId()
{
    return QUuid::fromString(QLatin1String(kChatBotOverlayId));
}

// Verweise wie im Kern: Overlays mit ihren Quellen; Mediathek leer (die Bilder der Schritte sind
// Platzhalter, keine Dateien)
ReferenceContext contextOf(const dss::OverlayStore &store)
{
    QHash<QUuid, QSet<QUuid>> overlays;
    for (const dss::Overlay &o : store.overlays()) {
        QSet<QUuid> sources;
        for (const dss::Source &s : o.sources)
            sources.insert(s.id);
        overlays.insert(o.id, sources);
    }
    ReferenceContext context;
    context.overlays = overlays;
    context.mediaFiles = QSet<QString>();
    return context;
}

dss::ActionViewNames namesOf(const dss::OverlayStore &store)
{
    dss::ActionViewNames names;
    for (const dss::Overlay &o : store.overlays()) {
        names.overlays.insert(o.id, o.name);
        for (const dss::Source &s : o.sources)
            names.sources.insert(s.id, s.name);
    }
    return names;
}

// Alle Schritte «Overlay-Element zeigen» (auch in Gruppen und Zweigen)
QList<Step> overlaySteps(const ActionsConfig &config)
{
    QList<Step> steps;
    for (const Action &a : config.actions) {
        forEachStep(a.steps, [&](const Step &s, const QList<int> &) {
            if (s.type == QLatin1String(kStepOverlaySource))
                steps.append(s);
        });
    }
    return steps;
}

// Wie der Nutzer im Schritt: anderes Overlay und andere Elemente wählen
void remapSteps(QList<Step> *steps, const QUuid &overlay, const QHash<QUuid, QUuid> &elements, int *count)
{
    for (Step &s : *steps) {
        remapSteps(&s.steps, overlay, elements, count);
        if (s.type != QLatin1String(kStepOverlaySource))
            continue;
        s.fields.insert(QStringLiteral("overlay"), overlay.toString(QUuid::WithoutBraces));
        QJsonArray list;
        for (const QJsonValue &v : s.fields.value(QStringLiteral("elements")).toArray()) {
            QJsonObject e = v.toObject();
            const QUuid source = QUuid::fromString(e.value(QStringLiteral("source")).toString());
            e.insert(QStringLiteral("source"), elements.value(source).toString(QUuid::WithoutBraces));
            list.append(e);
        }
        s.fields.insert(QStringLiteral("elements"), list);
        ++*count;
    }
}

// Der Schritt im Laufzustand, wie ActionService::OverlayForward (ohne Lauf: Platzhalter setzt hier
// der Laufzustand ein)
dss::LiveSetRequest liveRequest(const Step &s, const QHash<QString, QString> &arguments)
{
    dss::LiveSetRequest r;
    r.overlayId = QUuid::fromString(s.fields.value(QStringLiteral("overlay")).toString());
    r.mode = dss::LiveMode::Show;
    for (const QJsonValue &v : s.fields.value(QStringLiteral("elements")).toArray()) {
        const QJsonObject e = v.toObject();
        dss::LiveElementRequest element;
        element.source = QUuid::fromString(e.value(QStringLiteral("source")).toString());
        if (e.value(QStringLiteral("text")).isString())
            element.text = e.value(QStringLiteral("text")).toString();
        element.image = e.value(QStringLiteral("image")).toString();
        r.elements.append(element);
    }
    r.durationMs = int(s.fields.value(QStringLiteral("duration_ms")).toInteger());
    r.arguments = arguments;
    return r;
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

    // Verweise gültig bis auf das Overlay «Chat-Bot»; alle Schritte nutzen dessen feste Kennungen
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

        // Mit dem angelegten Overlay (f2): keine Warnung
        QTemporaryDir data;
        QVERIFY(data.isValid());
        dss::OverlayStore store(data.path());
        QString error;
        QString warning;
        QVERIFY(store.load(&error));
        QCOMPARE(dss::ensureChatBotOverlay(&store, &error, &warning), dss::ChatBotOverlayResult::Created);
        const QList<ReferenceWarning> withOverlay = checkReferences(t, contextOf(store));
        for (const ReferenceWarning &w : withOverlay)
            qWarning("%s", qPrintable(w.message));
        QVERIFY(withOverlay.isEmpty());
    }

    // f2, Plan 7: Overlay «Chat-Bot» mit den Kennungen der Vorlage, schlicht, alles unsichtbar
    void overlayCreated()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        dss::OverlayStore store(data.path());
        store.setWritesMediaLibrary(true);
        QString error;
        QString warning;
        QVERIFY(store.load(&error));
        QCOMPARE(dss::ensureChatBotOverlay(&store, &error, &warning), dss::ChatBotOverlayResult::Created);
        QVERIFY2(error.isEmpty() && warning.isEmpty(), qPrintable(error + warning));
        QVERIFY(store.overlay(chatBotId()));
        const dss::Overlay o = *store.overlay(chatBotId());
        QCOMPARE(o.name, QStringLiteral("Chat-Bot"));
        QCOMPARE(o.address, QStringLiteral("chat-bot"));
        QCOMPARE(o.width, 1920);
        QCOMPARE(o.height, 1080);
        QVERIFY(o.output);
        QCOMPARE(o.sources.size(), qsizetype(std::size(kChatBotElements)));

        // Schrift: Kopie der mitgelieferten Be Vietnam Pro; Bild: Platzhalter aus der Ressource (PNG)
        const QString font = o.sources.at(0).text.fontFile;
        const QString image = o.sources.at(5).file;
        QCOMPARE(readFile(store.mediaFilePath(font)), readFile(dss::defaultTextFontFile()));
        const QByteArray png = readFile(store.mediaFilePath(image));
        QCOMPARE(png, readFile(QString::fromLatin1(dss::kChatBotPlaceholderResource)));
        QVERIFY(png.startsWith("\x89PNG\r\n\x1a\n"));
        QCOMPARE(store.mediaName(image), QStringLiteral("chatbot-platzhalter.png"));
        QCOMPARE(QDir(store.mediaDir()).entryList(QDir::Files).size(), 2);

        const QStringList texts = {
            QStringLiteral("Neuer Follower: %user%"), QStringLiteral("Neues Abo: %user%"),
            QStringLiteral("%user% verschenkt %gifts% Abos"), QStringLiteral("%user%: %bits% Bits"),
            QStringLiteral("Raid von %user% mit %viewers% Zuschauern"), QString(),
            QStringLiteral("Willkommen, %user%"), QString(), QStringLiteral("Shoutout: %targetUser%")};
        const QRect canvas(0, 0, o.width, o.height);
        for (qsizetype i = 0; i < o.sources.size(); ++i) {
            const dss::Source &s = o.sources.at(i);
            QCOMPARE(s.id, QUuid::fromString(QLatin1String(kChatBotElements[i].id)));
            QCOMPARE(s.name, QString::fromUtf8(kChatBotElements[i].name));
            QVERIFY2(!s.visible, qPrintable(s.name));
            QVERIFY(canvas.contains(QRect(s.x, s.y, s.width, s.height)));
            QCOMPARE(s.rotation, 0);
            if (texts.at(i).isEmpty()) {
                QCOMPARE(s.kind, dss::SourceKind::Image);
                QCOMPARE(s.file, image);
                QCOMPARE(s.width, s.height); // Profilbilder sind quadratisch
            } else {
                QCOMPARE(s.kind, dss::SourceKind::Text);
                QCOMPARE(s.text.content, texts.at(i));
                QCOMPARE(s.text.fontFile, font);
                QCOMPARE(s.text.color, QStringLiteral("#FFFFFFFF")); // weiss, keine Farbflächen
            }
        }

        // Ordner «Chat-Bot»
        dss::OverlayLayout layout = store.layout();
        QCOMPARE(layout.entries.size(), 1);
        QVERIFY(layout.entries.at(0).isFolder);
        QCOMPARE(layout.entries.at(0).folder.name, QStringLiteral("Chat-Bot"));
        QCOMPARE(layout.entries.at(0).folder.overlays, QList<QUuid>{chatBotId()});

        // Nicht doppelt: nochmals und nach neuem Laden
        QCOMPARE(dss::ensureChatBotOverlay(&store, &error, &warning), dss::ChatBotOverlayResult::Exists);
        dss::OverlayStore again(data.path());
        QVERIFY(again.load(&error));
        QCOMPARE(dss::ensureChatBotOverlay(&again, &error, &warning), dss::ChatBotOverlayResult::Exists);
        QCOMPARE(again.overlays().size(), 1);
        QCOMPARE(*again.overlay(chatBotId()), o);
        QCOMPARE(again.layout(), layout);
        QCOMPARE(QDir(store.mediaDir()).entryList(QDir::Files).size(), 2);
    }

    // Ordner gleichen Namens wird benutzt; eine kaputte Datei mit der Kennung bleibt unberührt
    void existingFolderAndBrokenFile()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        dss::OverlayStore store(data.path());
        QString error;
        QString warning;
        QVERIFY(store.load(&error));
        const QUuid other = store.createOverlay(QStringLiteral("Anderes"), 1280, 720, &error);
        const QUuid folder = store.createFolder(QStringLiteral("Chat-Bot"), &error);
        QVERIFY(!other.isNull() && !folder.isNull());
        QCOMPARE(dss::ensureChatBotOverlay(&store, &error, &warning), dss::ChatBotOverlayResult::Created);
        QVERIFY2(warning.isEmpty(), qPrintable(warning));
        const dss::OverlayLayout layout = store.layout();
        QCOMPARE(layout.entries.size(), 2);
        QCOMPARE(layout.entries.at(0).overlayId, other);
        QCOMPARE(layout.entries.at(1).folder.id, folder);
        QCOMPARE(layout.entries.at(1).folder.overlays, QList<QUuid>{chatBotId()});

        QTemporaryDir broken;
        QVERIFY(broken.isValid());
        dss::OverlayStore brokenStore(broken.path());
        QVERIFY(QDir().mkpath(broken.filePath(QStringLiteral("overlays"))));
        const QByteArray bytes = "{kaputt";
        {
            QFile f(brokenStore.overlayFilePath(chatBotId()));
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size());
        }
        QVERIFY(brokenStore.load(&error));
        QCOMPARE(brokenStore.loadErrors().size(), 1);
        QCOMPARE(dss::ensureChatBotOverlay(&brokenStore, &error, &warning), dss::ChatBotOverlayResult::Exists);
        QCOMPARE(readFile(brokenStore.overlayFilePath(chatBotId())), bytes);
        QVERIFY(brokenStore.overlays().isEmpty());
        QVERIFY(QDir(brokenStore.mediaDir()).entryList(QDir::Files).isEmpty());
    }

    // Plan 7: Löscht der Nutzer Elemente oder das Overlay, zeigen die Schritte «Element fehlt» bzw.
    // «Overlay fehlt»; Plan 8: auf ein zweites, anders gestaltetes Overlay umgestellt zeigen die
    // mitgelieferten Aktionen dessen Aussehen und Wortlaut, nichts hängt am Overlay «Chat-Bot».
    void secondOverlay()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        dss::OverlayStore store(data.path());
        store.setWritesMediaLibrary(true);
        QString error;
        QString warning;
        QVERIFY(store.load(&error));
        QCOMPARE(dss::ensureChatBotOverlay(&store, &error, &warning), dss::ChatBotOverlayResult::Created);
        const ActionsConfig &t = actionTemplate();
        const Action *follow = byName(t, QStringLiteral("Alert Follow"));
        const Step &followStep = follow->steps.at(1);
        QCOMPARE(followStep.type, QString::fromLatin1(kStepOverlaySource));

        // Element «Alert Follow» gelöscht: «Element fehlt»
        const QUuid followElement = QUuid::fromString(QLatin1String(kChatBotElements[0].id));
        QVERIFY(store.removeSource(chatBotId(), followElement, &error));
        QCOMPARE(dss::stepSummary(followStep, t, namesOf(store)),
                 QStringLiteral("zeigen: Element fehlt in Overlay «Chat-Bot» · 6 s"));
        QList<ReferenceWarning> warnings = checkReferences(t, contextOf(store));
        QCOMPARE(warnings.size(), 1);
        QCOMPARE(warnings.at(0).objectId, follow->id);
        QVERIFY2(warnings.at(0).message.endsWith(QStringLiteral(": Element 1 fehlt im Overlay")),
                 qPrintable(warnings.at(0).message));

        // Overlay gelöscht: «Overlay fehlt» in allen 7 Schritten
        QVERIFY(store.removeOverlay(chatBotId(), &error));
        QVERIFY(dss::stepSummary(followStep, t, namesOf(store)).contains(QStringLiteral("Overlay fehlt")));
        QCOMPARE(checkReferences(t, contextOf(store)).size(), 7);

        // Zweites Overlay, anders gestaltet: andere Auflösung, Lage, Grösse, Schrift, Farbe,
        // Ausrichtung, Bild und eigener Wortlaut
        const QUuid own = store.createOverlay(QStringLiteral("Eigene Meldungen"), 1280, 720, &error);
        QVERIFY2(!own.isNull(), qPrintable(error));
        const QHash<QString, QString> ownTexts = {
            {QStringLiteral("Alert Follow"), QStringLiteral("Danke fürs Folgen, %user%")},
            {QStringLiteral("Alert Sub"), QStringLiteral("Abo von %user%")},
            {QStringLiteral("Alert Geschenk"), QStringLiteral("%gifts% Geschenke von %user%")},
            {QStringLiteral("Alert Cheer"), QStringLiteral("%bits% Bits von %user%")},
            {QStringLiteral("Alert Raid"), QStringLiteral("%viewers% Leute von %user%")},
            {QStringLiteral("Begrüssung Name"), QStringLiteral("Hoi %user%")},
            {QStringLiteral("Shoutout Name"), QStringLiteral("Schaut bei %targetUser% rein")},
        };
        QHash<QUuid, QUuid> mapped; // Element im Overlay «Chat-Bot» → eigenes Element
        int row = 0;
        for (const ChatBotElement &e : kChatBotElements) {
            const QString name = QString::fromUtf8(e.name);
            QUuid id;
            dss::SourceChanges c;
            c.name = QStringLiteral("Mein ") + name;
            c.x = 20 + row % 2 * 640;
            c.y = 20 + row / 2 * 120;
            c.width = 600;
            c.height = 100;
            c.rotation = 50;
            if (ownTexts.contains(name)) {
                id = store.addTextSource(own, ownTexts.value(name), QStringLiteral(DSS_TEST_FONT), &error);
                c.fontSize = 30;
                c.color = QStringLiteral("#FFCC00FF");
                c.align = dss::TextAlign::Right;
            } else {
                id = store.addImageSource(own, QStringLiteral(DSS_TEST_IMAGE), &error);
            }
            QVERIFY2(!id.isNull(), qPrintable(error));
            QVERIFY2(store.updateSource(own, id, c, &error), qPrintable(error));
            mapped.insert(QUuid::fromString(QLatin1String(e.id)), id);
            ++row;
        }

        // Alle mitgelieferten Aktionen umstellen, wie der Nutzer im Schritt (action_put)
        ActionsConfig c = t;
        int remapped = 0;
        for (const Action &a : t.actions) {
            Action changed = a;
            int count = 0;
            remapSteps(&changed.steps, own, mapped, &count);
            if (count == 0)
                continue;
            remapped += count;
            ConfigEditResult r;
            c = edited(c, QStringLiteral("action_put"), {{QStringLiteral("action"), actionToJson(changed)}}, &r);
            QVERIFY2(r.ok, qPrintable(r.message));
        }
        QCOMPARE(remapped, 7);
        QVERIFY(checkReferences(c, contextOf(store)).isEmpty());
        QCOMPARE(dss::stepSummary(byName(c, QStringLiteral("Alert Follow"))->steps.at(1), c, namesOf(store)),
                 QStringLiteral("zeigen: «Mein Alert Follow» in Overlay «Eigene Meldungen» · 6 s"));

        // Laufzustand wie im Lauf: jedes Element zeigt das eigene Aussehen mit dem eigenen Wortlaut
        dss::OverlayLiveState::Options options;
        options.imageDir = data.filePath(QStringLiteral("live-bilder"));
        dss::OverlayLiveState live(&store, options);
        const QHash<QString, QString> arguments = {
            {QStringLiteral("user"), QStringLiteral("Testname")}, {QStringLiteral("bits"), QStringLiteral("100")},
            {QStringLiteral("gifts"), QStringLiteral("5")}, {QStringLiteral("viewers"), QStringLiteral("12")},
            {QStringLiteral("targetUser"), QStringLiteral("Gast")},
            {QStringLiteral("targetUserProfileImageUrl"), QString()}};
        for (const Step &s : overlaySteps(c)) {
            bool ok = false;
            live.set(liveRequest(s, arguments), [&](const dss::OverlayLiveState::Result &r) {
                ok = r.ok;
                if (!r.ok || !r.warning.isEmpty())
                    qWarning("%s %s", qPrintable(r.message), qPrintable(r.warning));
            });
            QVERIFY(ok);
        }
        QVERIFY(live.elements(chatBotId()).isEmpty());
        const dss::Overlay before = *store.overlay(own);
        const dss::Overlay shown = dss::applyLiveState(before, live.elements(own));
        QCOMPARE(shown.sources.size(), 9);
        for (qsizetype i = 0; i < shown.sources.size(); ++i) {
            const dss::Source &s = shown.sources.at(i);
            const dss::Source &b = before.sources.at(i);
            QVERIFY2(s.visible, qPrintable(s.name));
            // Lage, Grösse, Drehung, Schrift, Farbe, Ausrichtung, Bild: wie im eigenen Overlay
            QCOMPARE(QRect(s.x, s.y, s.width, s.height), QRect(b.x, b.y, b.width, b.height));
            QCOMPARE(s.rotation, 50);
            QCOMPARE(s.file, b.file);
            QCOMPARE(s.text.fontFile, b.text.fontFile);
            QCOMPARE(s.text.color, b.text.color);
            QCOMPARE(s.text.align, b.text.align);
            QCOMPARE(s.text.fontSize, b.text.fontSize);
        }
        QStringList shownTexts;
        for (const dss::Source &s : shown.sources) {
            if (s.kind == dss::SourceKind::Text)
                shownTexts.append(s.text.content);
        }
        QCOMPARE(shownTexts, (QStringList{QStringLiteral("Danke fürs Folgen, Testname"), QStringLiteral("Abo von Testname"),
                                          QStringLiteral("5 Geschenke von Testname"), QStringLiteral("100 Bits von Testname"),
                                          QStringLiteral("12 Leute von Testname"), QStringLiteral("Hoi Testname"),
                                          QStringLiteral("Schaut bei Gast rein")}));
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
        QVERIFY2(log.contains(QStringLiteral("Overlay «Chat-Bot» angelegt")), qPrintable(log));

        ActionsConfig file;
        QString error;
        QVERIFY2(parseActions(readFile(path), &file, &error), qPrintable(error));
        QCOMPARE(file, actionTemplate());
        QVERIFY(!file.enabled);

        // f2: Overlay «Chat-Bot» im Ordner «Chat-Bot», Verweise der Aktionen ohne Warnung
        dss::OverlayStore store(data.path());
        QVERIFY(store.load(&error, dss::OverlayStore::LoadMode::ReadOnly));
        QVERIFY(store.loadErrors().isEmpty() && store.loadWarnings().isEmpty());
        QCOMPARE(store.overlays().size(), 1);
        QVERIFY(store.overlay(chatBotId()));
        QCOMPARE(store.layout().entries.size(), 1);
        QCOMPARE(store.layout().entries.at(0).folder.name, QStringLiteral("Chat-Bot"));
        QCOMPARE(store.mediaName(store.overlay(chatBotId())->sources.at(5).file), QStringLiteral("chatbot-platzhalter.png"));
        QVERIFY(checkReferences(file, contextOf(store)).isEmpty());
    }

    // aktionen.json fehlt, das Overlay «Chat-Bot» gibt es (vom Nutzer umgestaltet): bleibt, wie es ist
    void coreKeepsExistingOverlay()
    {
        std::unique_ptr<QTemporaryDir> run = runtimeDir();
        QTemporaryDir data;
        QVERIFY(run->isValid() && data.isValid());
        QString error;
        QString warning;
        QByteArray before;
        {
            dss::OverlayStore store(data.path());
            QVERIFY(store.load(&error));
            QCOMPARE(dss::ensureChatBotOverlay(&store, &error, &warning), dss::ChatBotOverlayResult::Created);
            QVERIFY(store.renameOverlay(chatBotId(), QStringLiteral("Meine Alerts"), false, &error));
            QVERIFY(store.removeSource(chatBotId(), QUuid::fromString(QLatin1String(kChatBotElements[8].id)), &error));
            before = readFile(store.overlayFilePath(chatBotId()));
        }
        const QStringList mediaBefore = QDir(data.filePath(QStringLiteral("media"))).entryList(QDir::Files);
        QString log;
        std::unique_ptr<QProcess> core = startCore(data.path(), run->path() + QStringLiteral("/a.sock"));
        QVERIFY2(waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        core->terminate();
        QVERIFY(core->waitForFinished(10000));
        QCOMPARE(core->exitCode(), 0);
        log += QString::fromUtf8(core->readAllStandardError());
        QVERIFY2(log.contains(QStringLiteral("mitgelieferte Aktionen angelegt")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("Overlay «Chat-Bot» gibt es schon")), qPrintable(log));
        dss::OverlayStore store(data.path());
        QCOMPARE(readFile(store.overlayFilePath(chatBotId())), before);
        QCOMPARE(QDir(data.filePath(QStringLiteral("media"))).entryList(QDir::Files), mediaBefore);
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
        // f2: kein Overlay «Chat-Bot» ohne die mitgelieferten Aktionen
        QVERIFY2(!log.contains(QStringLiteral("Overlay «Chat-Bot»")), qPrintable(log));
        QVERIFY(!QFile::exists(dss::OverlayStore(data.path()).overlayFilePath(chatBotId())));
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
