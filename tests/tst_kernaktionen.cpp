// Aktionssystem im Kern (P14 b3, Plan 3 und 5) mit dss-core und dss-cli als eigenen Prozessen:
// Änderung übersteht Neustart, ungültige aktionen.json wird nicht überschrieben, stumm bei
// zweitem Kern (Version im Einsatz), Ereignis über den Fluss löst eine Aktion aus, keine
// Chat-Texte und keine Zuschauernamen im Protokoll. Erfundene Daten, eigener Datenordner.
// Ereignisse kommen über test_event (dss-core --testereignisse), weil Twitch erst mit c1
// im Kern angebunden ist.
//
// P14 c2 (Plan 4.3, 4.4): «Ton abspielen» über den Zweck «aktionen» mit dem Ersatzausgang
// (--ersatzausgang, ohne Gerät; tests/daten/ton/ton-piep.wav, 1 s) und «Overlay-Element zeigen/
// verstecken» über den Laufzustand des Kerns (overlay_live), Overlay-Datei bleibt unverändert.

#include "actionjson.h"
#include "actionprotocol.h"
#include "coresettings.h"
#include "eventbus.h"
#include "eventtypes.h"
#include "overlaylive.h"
#include "overlaystore.h"
#include "protocol.h"
#include "prozesshilfe.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <functional>
#include <memory>

using namespace dss;
using namespace dss::actions;
using namespace testhelp;

namespace {

// Erfundene Texte, die nie im Protokoll des Kerns stehen dürfen
const QString kChatText = QStringLiteral("!dsstest Geheimtext-Zuschauer-7319");
const QString kViewerLogin = QStringLiteral("zuschauer_geheim_4711");
const QString kViewerName = QStringLiteral("Zuschauer_Geheim_4711");
const QString kReplyText = QStringLiteral("Antwort-Geheimtext-5521");

class CoreLink
{
public:
    explicit CoreLink(const QString &path)
    {
        m_socket.connectToServer(path);
        m_connected = m_socket.waitForConnected(3000);
        if (m_connected) {
            m_socket.write(encodeMessage(helloMessage({QStringLiteral("tst_kernaktionen"), QStringLiteral("0"),
                                                       kProtocolVersion})));
            m_socket.waitForBytesWritten(1000);
        }
    }
    bool isConnected() const { return m_connected; }

    // Nächste Nachricht vom Typ type in der Reihenfolge des Eintreffens; andere bleiben für
    // spätere Aufrufe liegen.
    QJsonObject waitFor(const QString &type, int timeoutMs = 5000)
    {
        return next([&type](const QJsonObject &o) { return messageType(o) == type; }, timeoutMs);
    }

    // Anfrage schicken; Antwort vom Typ replyType oder change_result mit derselben request_id
    QJsonObject request(const QString &type, const QJsonObject &fields, const QString &replyType = QString::fromLatin1(msg::kChangeResult))
    {
        const qint64 id = m_next++;
        send(actionsRequestMessage({id, type, fields}));
        return next([&](const QJsonObject &o) {
            return messageRequestId(o) == id
                   && (messageType(o) == replyType || messageType(o) == QLatin1String(msg::kChangeResult));
        }, 5000);
    }

    void send(const QJsonObject &message)
    {
        m_socket.write(encodeMessage(message));
        m_socket.waitForBytesWritten(1000);
    }

private:
    QJsonObject next(const std::function<bool(const QJsonObject &)> &match, int timeoutMs)
    {
        const QDeadlineTimer deadline(timeoutMs);
        for (;;) {
            for (qsizetype i = 0; i < m_pending.size(); ++i) {
                if (match(m_pending.at(i)))
                    return m_pending.takeAt(i);
            }
            QByteArray line;
            while (m_lines.next(&line) == LineSplitter::Result::Line) {
                QJsonObject o;
                QString error;
                if (!decodeMessage(line, &o, &error) || messageType(o) == QLatin1String(msg::kStats))
                    continue;
                if (match(o))
                    return o;
                m_pending.append(o);
            }
            if (deadline.hasExpired() || m_socket.state() != QLocalSocket::ConnectedState)
                return {};
            if (m_socket.bytesAvailable() > 0 || m_socket.waitForReadyRead(int(qMin<qint64>(100, deadline.remainingTime()))))
                m_lines.append(m_socket.readAll());
        }
    }

    QLocalSocket m_socket;
    LineSplitter m_lines;
    QList<QJsonObject> m_pending;
    qint64 m_next = 1;
    bool m_connected = false;
};

QString idText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

// Befehl «!dsstest» mit Aktion: Variable setzen (dsstest_zaehler +1) und Chat senden
struct TestSetup
{
    Command command;
    Action action;
};

TestSetup testSetup()
{
    TestSetup s;
    s.command.id = QUuid::createUuid();
    s.command.name = QStringLiteral("DSS-Test");
    s.command.commands = {QStringLiteral("!dsstest")};
    s.action.id = QUuid::createUuid();
    s.action.name = QStringLiteral("DSS-Test Befehl");
    Trigger trigger = newTrigger(QString::fromLatin1(kTriggerCommand));
    trigger.fields.insert(QStringLiteral("command_id"), idText(s.command.id));
    s.action.triggers.append(trigger);
    Step counter = newStep(QString::fromLatin1(kStepGlobalSet));
    counter.fields.insert(QStringLiteral("name"), QStringLiteral("dsstest_zaehler"));
    counter.fields.insert(QStringLiteral("operation"), QStringLiteral("increment"));
    counter.fields.insert(QStringLiteral("value"), QStringLiteral("1"));
    s.action.steps.append(counter);
    Step chat = newStep(QString::fromLatin1(kStepChatSend));
    chat.fields.insert(QStringLiteral("text"), kReplyText + QStringLiteral(" %user%"));
    s.action.steps.append(chat);
    return s;
}

// twitch.chat_message mit allen Feldern der Typenliste (eventtypes.h), erfundener Zuschauer
QJsonObject chatEvent(const QString &text)
{
    QJsonObject data{{QStringLiteral("message_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                     {QStringLiteral("user_id"), QStringLiteral("900000001")},
                     {QStringLiteral("user_login"), kViewerLogin},
                     {QStringLiteral("user_name"), kViewerName},
                     {QStringLiteral("text"), text},
                     {QStringLiteral("fragments"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                                           {QStringLiteral("text"), text},
                                                                           {QStringLiteral("emote_id"), QJsonValue::Null}}}},
                     {QStringLiteral("color"), QString()},
                     {QStringLiteral("badges"), QJsonArray()},
                     {QStringLiteral("role"), 1},
                     {QStringLiteral("is_subscriber"), false},
                     {QStringLiteral("is_vip"), false},
                     {QStringLiteral("is_moderator"), false},
                     {QStringLiteral("is_broadcaster"), false},
                     {QStringLiteral("is_bot_account"), false},
                     {QStringLiteral("bits"), 0},
                     {QStringLiteral("reply_to"), QJsonValue::Null},
                     {QStringLiteral("reward_id"), QJsonValue::Null},
                     {QStringLiteral("message_type"), QStringLiteral("text")},
                     {QStringLiteral("is_highlight"), false},
                     {QStringLiteral("source_broadcaster_id"), QJsonValue::Null}};
    return QJsonObject{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                       {QStringLiteral("type"), QStringLiteral("twitch.chat_message")},
                       {QStringLiteral("origin"), QStringLiteral("twitch")},
                       {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                       {QStringLiteral("origin_id"), QString()},
                       {QStringLiteral("data"), data}};
}

bool ok(const QJsonObject &reply)
{
    return messageType(reply) == QLatin1String(msg::kChangeResult) && reply.value(QStringLiteral("ok")).toBool();
}

// c2: Aktion ohne Auslöser (gestartet mit action_test)
Action newAction(const QString &name, const QList<Step> &steps)
{
    Action a;
    a.id = QUuid::createUuid();
    a.name = name;
    a.steps = steps;
    return a;
}

Step soundStep(const QString &file, bool wait)
{
    Step s = newStep(QString::fromLatin1(kStepPlaySound));
    s.fields.insert(QStringLiteral("file"), file);
    s.fields.insert(QStringLiteral("wait"), wait);
    return s;
}

struct ElementChoice
{
    QUuid source;
    std::optional<QString> text;
    std::optional<QString> image;
};

Step overlayStep(const QUuid &overlay, const QString &mode, const QList<ElementChoice> &elements, int durationMs,
                 bool wait)
{
    Step s = newStep(QString::fromLatin1(kStepOverlaySource));
    s.fields.insert(QStringLiteral("overlay"), idText(overlay));
    s.fields.insert(QStringLiteral("mode"), mode);
    QJsonArray list;
    for (const ElementChoice &e : elements) {
        // Format (fieldspec.cpp): text und image immer, null = Text bzw. Bild des Elements
        list.append(QJsonObject{{QStringLiteral("source"), idText(e.source)},
                                {QStringLiteral("text"), e.text ? QJsonValue(*e.text) : QJsonValue()},
                                {QStringLiteral("image"), e.image ? QJsonValue(*e.image) : QJsonValue()}});
    }
    s.fields.insert(QStringLiteral("elements"), list);
    s.fields.insert(QStringLiteral("duration_ms"), durationMs);
    s.fields.insert(QStringLiteral("wait"), wait);
    return s;
}

} // namespace

class TestKernAktionen : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_run;
    QString m_inUseSocket; // Verbindung der «Version im Einsatz» (nur in mutedWithSecondCore belegt)

    struct Core
    {
        std::unique_ptr<QProcess> process;
        QString socket;
        QString log;
    };

    Core start(const QString &dataDir, const QString &name, const QStringList &extra = {})
    {
        Core c;
        c.socket = m_run->path() + QLatin1Char('/') + name + QStringLiteral(".sock");
        c.process = startCore(dataDir, c.socket, testConfigHome(),
                              QStringList{QStringLiteral("--einsatz-verbindung"), m_inUseSocket,
                                          QStringLiteral("--testereignisse")}
                                  + extra);
        if (!waitForLog(c.process.get(), QStringLiteral("Verbindung offen"), &c.log))
            qWarning("Kern startet nicht: %s", qPrintable(c.log));
        return c;
    }

    void stop(Core &c)
    {
        c.process->terminate();
        QVERIFY(c.process->waitForFinished(10000));
        c.log += QString::fromUtf8(c.process->readAllStandardError());
        QCOMPARE(c.process->exitStatus(), QProcess::NormalExit);
        QCOMPARE(c.process->exitCode(), 0);
    }

    static QByteArray readFile(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    // Legt Befehl und Aktion an und schaltet den Chat-Bot ein
    void putSetup(CoreLink &c, const TestSetup &s)
    {
        QVERIFY(ok(c.request(QString::fromLatin1(msg::kCommandPut), {{QStringLiteral("command"), commandToJson(s.command)}})));
        const QJsonObject put = c.request(QString::fromLatin1(msg::kActionPut), {{QStringLiteral("action"), actionToJson(s.action)}});
        QVERIFY2(ok(put), QJsonDocument(put).toJson(QJsonDocument::Compact).constData());
        QCOMPARE(put.value(QStringLiteral("id")).toString(), idText(s.action.id)); // neu angelegt
        QVERIFY(ok(c.request(QString::fromLatin1(msg::kActionsSetEnabled), {{QStringLiteral("enabled"), true}})));
    }

    QList<RunRecord> runs(CoreLink &c)
    {
        QList<RunRecord> list;
        QString error;
        const QJsonObject reply = c.request(QString::fromLatin1(msg::kGetActionRuns), {}, QString::fromLatin1(msg::kActionRuns));
        if (!parseActionRuns(reply, &list, &error))
            qWarning("%s", qPrintable(error));
        return list;
    }

    // c2: legt die Aktion an, startet sie echt (action_test) und wartet auf das Ende des Laufs.
    // tookMs: vom Start bis zum gemeldeten Ende (Abfrage alle 20 ms).
    RunRecord runAction(CoreLink &c, const Action &action, const QJsonObject &arguments = {}, qint64 *tookMs = nullptr)
    {
        RunRecord run;
        const QJsonObject put = c.request(QString::fromLatin1(msg::kActionPut), {{QStringLiteral("action"), actionToJson(action)}});
        if (!ok(put)) {
            qWarning("action_put: %s", QJsonDocument(put).toJson(QJsonDocument::Compact).constData());
            return run;
        }
        QElapsedTimer timer;
        timer.start();
        const QJsonObject started = c.request(QString::fromLatin1(msg::kActionTest),
                                              {{QStringLiteral("action_id"), idText(action.id)},
                                               {QStringLiteral("dry_run"), false},
                                               {QStringLiteral("arguments"), arguments}});
        if (!ok(started)) {
            qWarning("action_test: %s", QJsonDocument(started).toJson(QJsonDocument::Compact).constData());
            return run;
        }
        QString error;
        const QDeadlineTimer deadline(15000);
        while (!deadline.hasExpired()) {
            if (parseActionRun(c.request(QString::fromLatin1(msg::kActionRunDetails),
                                         {{QStringLiteral("run_id"), started.value(QStringLiteral("id"))}},
                                         QString::fromLatin1(msg::kActionRun)),
                               &run, &error)
                && isFinished(run.state))
                break;
            QTest::qWait(20);
        }
        if (tookMs)
            *tookMs = timer.elapsed();
        return run;
    }

    // c2: Laufzustand eines Overlays, wie ihn ein neues Fenster nach hello bekommt
    static LiveElements liveElements(const QString &socket, const QUuid &overlay)
    {
        CoreLink link(socket);
        QList<OverlayLiveInfo> infos;
        QString error;
        if (!parseOverlayLive(link.waitFor(QString::fromLatin1(msg::kOverlayLive)), &infos, &error))
            qWarning("overlay_live: %s", qPrintable(error));
        for (const OverlayLiveInfo &info : std::as_const(infos)) {
            if (info.overlayId == overlay)
                return info.elements;
        }
        return {};
    }

private slots:
    void init()
    {
        m_run = runtimeDir();
        QVERIFY(m_run->isValid());
        m_inUseSocket = m_run->path() + QStringLiteral("/einsatz.sock"); // ohne Kern
    }

    void changeSurvivesRestart()
    {
        QTemporaryDir data;
        Core core = start(data.path(), QStringLiteral("a"));
        const TestSetup s = testSetup();
        {
            CoreLink c(core.socket);
            QVERIFY(c.isConnected());
            ActionsConfigInfo info;
            QString error;
            QVERIFY(parseActionsConfig(c.waitFor(QString::fromLatin1(msg::kActionsConfig)), &info, &error));
            QVERIFY(info.config);
            QVERIFY(info.config->actions.isEmpty());
            QVERIFY(!info.config->enabled); // beim ersten Start aus
            putSetup(c, s);
            // Änderung an alle: actions_config mit der Aktion
            ActionsConfigInfo after;
            bool seen = false;
            for (int i = 0; i < 5 && !seen; ++i) {
                QVERIFY(parseActionsConfig(c.waitFor(QString::fromLatin1(msg::kActionsConfig)), &after, &error));
                seen = after.config && after.config->enabled && after.config->action(s.action.id);
            }
            QVERIFY(seen);
            // Ungültige Aktion wird abgewiesen, die Datei bleibt
            const QByteArray before = readFile(data.filePath(QString::fromLatin1(kActionsFileName)));
            QJsonObject broken = actionToJson(s.action);
            broken.insert(QStringLiteral("enabled"), QStringLiteral("ja"));
            const QJsonObject rejected = c.request(QString::fromLatin1(msg::kActionPut), {{QStringLiteral("action"), broken}});
            QVERIFY(!rejected.value(QStringLiteral("ok")).toBool());
            QCOMPARE(rejected.value(QStringLiteral("code")).toString(), QString::fromLatin1(err::kRejected));
            QCOMPARE(readFile(data.filePath(QString::fromLatin1(kActionsFileName))), before);
            // Kopie und dss-cli
            const QJsonObject copy = c.request(QString::fromLatin1(msg::kActionCopy), {{QStringLiteral("action_id"), idText(s.action.id)}});
            QVERIFY(ok(copy));
            QVERIFY(!QUuid::fromString(copy.value(QStringLiteral("id")).toString()).isNull());
        }
        const CliResult list = runCliOnly({QStringLiteral("--verbindung"), core.socket, QStringLiteral("aktionen")});
        QCOMPARE(list.exitCode, 0);
        QVERIFY2(list.out.contains(QStringLiteral("DSS-Test Befehl (Kopie)")), qPrintable(list.out + list.err));
        QVERIFY(list.out.contains(QStringLiteral("Chat-Bot: an")));
        const CliResult removed = runCliOnly({QStringLiteral("--verbindung"), core.socket, QStringLiteral("aktion-loeschen"),
                                              QStringLiteral("DSS-Test Befehl (Kopie)")});
        QCOMPARE(removed.exitCode, 0);
        stop(core);

        // Datei gültig und nach dem Neustart derselbe Stand
        ActionsConfig file;
        QString error;
        QVERIFY2(parseActions(readFile(data.filePath(QString::fromLatin1(kActionsFileName))), &file, &error), qPrintable(error));
        QCOMPARE(file.actions.size(), 1);
        Core again = start(data.path(), QStringLiteral("b"));
        CoreLink c(again.socket);
        ActionsConfigInfo info;
        QVERIFY(parseActionsConfig(c.waitFor(QString::fromLatin1(msg::kActionsConfig)), &info, &error));
        QVERIFY(info.config);
        QCOMPARE(*info.config, file);
        QVERIFY(info.config->enabled);
        QCOMPARE(info.config->action(s.action.id)->name, s.action.name);
        stop(again);
    }

    void invalidFileNotOverwritten()
    {
        QTemporaryDir data;
        const QByteArray broken = "{\"format_version\": 1, \"kaputt\": ";
        {
            QFile f(data.filePath(QString::fromLatin1(kActionsFileName)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(broken);
        }
        Core core = start(data.path(), QStringLiteral("a"));
        {
            CoreLink c(core.socket);
            ActionsConfigInfo info;
            QString error;
            QVERIFY(parseActionsConfig(c.waitFor(QString::fromLatin1(msg::kActionsConfig)), &info, &error));
            QVERIFY(!info.config);
            QVERIFY(!info.fileError.isEmpty());
            ActionsStatus status;
            QVERIFY(parseActionsStatus(c.waitFor(QString::fromLatin1(msg::kActionsStatus)), &status, &error));
            QCOMPARE(status.state, QStringLiteral("file_error"));
            const TestSetup s = testSetup();
            const QJsonObject reply = c.request(QString::fromLatin1(msg::kActionPut), {{QStringLiteral("action"), actionToJson(s.action)}});
            QVERIFY(!reply.value(QStringLiteral("ok")).toBool());
            QCOMPARE(reply.value(QStringLiteral("code")).toString(), QString::fromLatin1(err::kActionsFileError));
            QVERIFY(!ok(c.request(QString::fromLatin1(msg::kActionsSetEnabled), {{QStringLiteral("enabled"), true}})));
        }
        stop(core);
        QCOMPARE(readFile(data.filePath(QString::fromLatin1(kActionsFileName))), broken);
        QVERIFY2(core.log.contains(QStringLiteral("aktionen.json")), qPrintable(core.log));
    }

    void eventTriggersAction()
    {
        QTemporaryDir data;
        Core core = start(data.path(), QStringLiteral("a"));
        const TestSetup s = testSetup();
        {
            CoreLink c(core.socket);
            putSetup(c, s);
            c.send(subscribeEventsMessage({{QStringLiteral("actions.")}, true}));
            QVERIFY(!c.waitFor(QString::fromLatin1(msg::kEventHistory)).isEmpty());
            QVERIFY(ok(c.request(QString::fromLatin1(msg::kTestEvent), {{QStringLiteral("event"), chatEvent(kChatText)}})));
            // actions.run_finished auf dem Fluss
            events::Event finished;
            QString error;
            for (int i = 0; i < 10; ++i) {
                const QJsonObject message = c.waitFor(QString::fromLatin1(msg::kEvent));
                QVERIFY(parseEventMessage(message, &finished, &error));
                if (finished.type == QLatin1String("actions.run_finished"))
                    break;
            }
            QCOMPARE(finished.type, QStringLiteral("actions.run_finished"));
            QCOMPARE(finished.data.value(QStringLiteral("action_name")).toString(), s.action.name);
            QCOMPARE(finished.data.value(QStringLiteral("trigger_type")).toString(), QStringLiteral("command"));
            // Chat senden scheitert ohne Twitch, der Lauf geht weiter (Plan 3.1)
            QCOMPARE(finished.data.value(QStringLiteral("state")).toString(), QStringLiteral("error"));
            // Einzelheiten mit dem eingesetzten Text nur auf Anfrage
            RunRecord run;
            QVERIFY(parseActionRun(c.request(QString::fromLatin1(msg::kActionRunDetails),
                                             {{QStringLiteral("run_id"), finished.data.value(QStringLiteral("run_id"))}},
                                             QString::fromLatin1(msg::kActionRun)),
                                   &run, &error));
            QCOMPARE(run.log.size(), 2);
            QCOMPARE(run.log.at(1).message, QStringLiteral("Twitch nicht verbunden"));
            // Variable gesetzt, Zuschauer nachgeführt
            VariablesInfo vars;
            QVERIFY(parseVariablesMessage(c.request(QString::fromLatin1(msg::kGetVariables), {}, QString::fromLatin1(msg::kVariables)),
                                          &vars, &error));
            QCOMPARE(vars.persisted.global.value(QStringLiteral("dsstest_zaehler")).value.toDouble(), 1.0);
            ViewersData viewers;
            QVERIFY(parseViewersMessage(c.request(QString::fromLatin1(msg::kGetViewers),
                                                  {{QStringLiteral("user_ids"), QJsonArray{QStringLiteral("900000001")}}},
                                                  QString::fromLatin1(msg::kViewers)),
                                        &viewers, &error));
            QCOMPARE(viewers.viewers.value(QStringLiteral("900000001")).login, kViewerLogin);

            // Hauptschalter aus: abgewiesen, kein Lauf
            QVERIFY(ok(c.request(QString::fromLatin1(msg::kActionsSetEnabled), {{QStringLiteral("enabled"), false}})));
            QVERIFY(ok(c.request(QString::fromLatin1(msg::kTestEvent), {{QStringLiteral("event"), chatEvent(kChatText)}})));
            QTRY_COMPARE_WITH_TIMEOUT(runs(c).size(), 2, 5000);
            QCOMPARE(runs(c).last().state, RunState::Rejected);
        }

        // dss-cli: Probelauf meldet den Text nur, Warteschlange, Variablen, Läufe
        const QStringList base{QStringLiteral("--verbindung"), core.socket};
        const CliResult test = runCliOnly(base + QStringList{QStringLiteral("aktion-test"), s.action.name, QStringLiteral("user=Testname")});
        QCOMPARE(test.exitCode, 0);
        QVERIFY2(test.out.contains(QStringLiteral("Probelauf: würde senden")), qPrintable(test.out + test.err));
        QVERIFY(test.out.contains(QStringLiteral("Testname")));
        const CliResult real = runCliOnly(base + QStringList{QStringLiteral("aktion-test"), s.action.name, QStringLiteral("--echt")});
        QCOMPARE(real.exitCode, 1); // Chat senden: Twitch nicht verbunden
        QVERIFY(real.out.contains(QStringLiteral("Twitch nicht verbunden")));
        QCOMPARE(runCliOnly(base + QStringList{QStringLiteral("warteschlange"), QStringLiteral("Standard"), QStringLiteral("anhalten")}).exitCode, 0);
        QCOMPARE(runCliOnly(base + QStringList{QStringLiteral("warteschlange"), QStringLiteral("Standard"), QStringLiteral("fortsetzen")}).exitCode, 0);
        const CliResult counts = runCliOnly(base + QStringList{QStringLiteral("variablen"), QStringLiteral("--zaehlen")});
        QCOMPARE(counts.exitCode, 0);
        QVERIFY2(counts.out.contains(QStringLiteral("dsstest_zaehler")), qPrintable(counts.out + counts.err));
        const CliResult list = runCliOnly(base + QStringList{QStringLiteral("laeufe")});
        QCOMPARE(list.exitCode, 0);
        QVERIFY(list.out.contains(QStringLiteral("rejected: ")));
        QVERIFY(list.out.contains(QStringLiteral("(Probelauf)")));
        stop(core);

        // Variablen und Zuschauer gespeichert
        VariablesData stored;
        QString error;
        QVERIFY(parseVariables(readFile(data.filePath(QString::fromLatin1(kVariablesFileName))), &stored, &error));
        // Befehl aus dem Chat und aktion-test --echt; der Probelauf zählt im eigenen Satz (Plan 3.6)
        QCOMPARE(stored.global.value(QStringLiteral("dsstest_zaehler")).value.toDouble(), 2.0);
        QVERIFY(QFileInfo::exists(data.filePath(QString::fromLatin1(kViewersFileName))));

        // Journal: Name der Aktion und Ergebnis, keine Chat-Texte, keine Zuschauernamen (Plan 3.5)
        QVERIFY2(core.log.contains(QStringLiteral("Lauf «DSS-Test Befehl»: error")), qPrintable(core.log));
        for (const QString &secret : {kChatText, QStringLiteral("Geheimtext"), kViewerLogin, kViewerName, kReplyText,
                                      QStringLiteral("Testname")})
            QVERIFY2(!core.log.contains(secret), qPrintable(secret));
    }

    // e2-2 (Version 18): chat_send ohne Twitch. Abgewiesen mit Grund: leer, länger als 500 UTF-16-Einheiten,
    // Twitch-Befehl, unbekanntes Konto, Konto kann nicht senden (Grund wie in actions_status.accounts),
    // falsche Felder. Senden selbst prüft tst_kerntwitch (chatSendFromWindow).
    void chatSendRejected()
    {
        QTemporaryDir data;
        Core core = start(data.path(), QStringLiteral("a"));
        {
            CoreLink c(core.socket);
            ActionsStatus status;
            QString error;
            QVERIFY(parseActionsStatus(c.waitFor(QString::fromLatin1(msg::kActionsStatus)), &status, &error));
            QCOMPARE(status.state, QStringLiteral("off")); // Chat-Bot aus, Senden trotzdem geprüft
            QCOMPARE(status.accounts.size(), 2);
            const auto send = [&](const QJsonValue &text, const QJsonValue &account, const QJsonValue &replyTo = QJsonValue::Null) {
                QJsonObject fields{{QStringLiteral("account"), account}, {QStringLiteral("reply_to"), replyTo}};
                if (!text.isUndefined())
                    fields.insert(QStringLiteral("text"), text);
                const QJsonObject reply = c.request(QString::fromLatin1(msg::kChatSend), fields);
                return ok(reply) ? QStringLiteral("ok") : reply.value(QStringLiteral("message")).toString();
            };
            const QString bot = QStringLiteral("bot");
            QCOMPARE(send(QString(), bot), QStringLiteral("Die Nachricht ist leer"));
            QCOMPARE(send(QStringLiteral("  \n "), bot), QStringLiteral("Die Nachricht ist leer"));
            QCOMPARE(send(QString(501, QLatin1Char('a')), bot), QStringLiteral("Die Nachricht ist länger als 500 Zeichen"));
            // 250 Emoji ausserhalb der BMP = 500 UTF-16-Einheiten: erlaubt, scheitert erst am Konto
            QString emoji;
            for (int i = 0; i < 251; ++i)
                emoji += QString::fromUtf8("\xF0\x9F\x98\x80");
            QCOMPARE(send(emoji, bot), QStringLiteral("Die Nachricht ist länger als 500 Zeichen"));
            emoji.chop(2);
            QCOMPARE(send(emoji, bot), status.accounts.at(0).message);
            for (const QString &command : {QStringLiteral("/ban x"), QStringLiteral(".ban x"), QStringLiteral("  /me x")})
                QCOMPARE(send(command, bot), QStringLiteral("Twitch-Befehle sind nicht möglich"));
            QCOMPARE(send(kReplyText, QStringLiteral("kanal")), QStringLiteral("account muss bot oder broadcaster sein"));
            // Ohne Twitch kann kein Konto senden: Grund wie in actions_status
            for (const ActionsAccountStatus &a : std::as_const(status.accounts)) {
                QVERIFY(!a.canSend);
                QVERIFY(!a.message.isEmpty());
                QCOMPARE(send(kReplyText, a.account, QStringLiteral("msg-1")), a.message);
            }
            // Falsche Felder: Text fehlt, reply_to keine Zeichenkette
            QVERIFY(send(QJsonValue::Undefined, bot).contains(QStringLiteral("text")));
            QVERIFY(send(kReplyText, bot, 5).contains(QStringLiteral("reply_to")));
        }
        stop(core);
        QVERIFY2(!core.log.contains(QStringLiteral("Geheimtext")), qPrintable(core.log));
    }

    void mutedWithSecondCore()
    {
        // Zweiter Kern unter der Verbindung der «Version im Einsatz»
        QTemporaryDir inUseData;
        QTemporaryDir data;
        Core inUse = start(inUseData.path(), QStringLiteral("einsatz"));
        QCOMPARE(inUse.socket, m_inUseSocket);
        Core core = start(data.path(), QStringLiteral("a"));
        const TestSetup s = testSetup();
        {
            CoreLink c(core.socket);
            putSetup(c, s);
            // Zustand stumm (Prüfung beim Start und alle 2 s)
            ActionsStatus status;
            QString error;
            for (int i = 0; i < 20 && status.state != QLatin1String("muted"); ++i) {
                const QJsonObject message = c.waitFor(QString::fromLatin1(msg::kActionsStatus), 500);
                if (!message.isEmpty())
                    QVERIFY(parseActionsStatus(message, &status, &error));
            }
            QCOMPARE(status.state, QStringLiteral("muted"));
            QCOMPARE(status.message, QStringLiteral("stumm: Version im Einsatz läuft"));
            // Befehl aus dem Chat: kein Lauf, nur «stumm»
            QVERIFY(ok(c.request(QString::fromLatin1(msg::kTestEvent), {{QStringLiteral("event"), chatEvent(kChatText)}})));
            QTRY_COMPARE_WITH_TIMEOUT(runs(c).size(), 1, 5000);
            QCOMPARE(runs(c).first().state, RunState::Muted);
            // Probelauf geht weiter, echt nicht
            QVERIFY(ok(c.request(QString::fromLatin1(msg::kActionTest), {{QStringLiteral("action_id"), idText(s.action.id)},
                                                                         {QStringLiteral("arguments"), QJsonObject()},
                                                                         {QStringLiteral("dry_run"), true}})));
            QVERIFY(!ok(c.request(QString::fromLatin1(msg::kActionTest), {{QStringLiteral("action_id"), idText(s.action.id)},
                                                                          {QStringLiteral("arguments"), QJsonObject()},
                                                                          {QStringLiteral("dry_run"), false}})));
            // e2-2: Chat senden aus dem Fenster abgewiesen
            const QJsonObject chat = c.request(QString::fromLatin1(msg::kChatSend), {{QStringLiteral("text"), kReplyText},
                                                                                     {QStringLiteral("account"), QStringLiteral("broadcaster")},
                                                                                     {QStringLiteral("reply_to"), QJsonValue::Null}});
            QVERIFY(!ok(chat));
            QCOMPARE(chat.value(QStringLiteral("message")).toString(), QStringLiteral("stumm: Version im Einsatz läuft"));
            // Version im Einsatz beendet: nicht mehr stumm
            stop(inUse);
            for (int i = 0; i < 20 && status.state == QLatin1String("muted"); ++i) {
                const QJsonObject message = c.waitFor(QString::fromLatin1(msg::kActionsStatus), 500);
                if (!message.isEmpty())
                    QVERIFY(parseActionsStatus(message, &status, &error));
            }
            QCOMPARE(status.state, QStringLiteral("on"));
        }
        stop(core);
        QVERIFY2(core.log.contains(QStringLiteral("stumm, die Version im Einsatz läuft")), qPrintable(core.log));
    }

    // c2 (Plan 4.3): Ton abspielen über den Zweck «aktionen». Ohne gewählten Ausgang meldet der
    // Schritt das; mit wait wartet der Lauf die Länge ab (1 s), ohne nicht; der 9. gleichzeitige Ton
    // wird abgewiesen, der Lauf geht weiter. Ohne DSS_CORE_AUDIO: «ohne Ton gebaut».
    void soundStepPlays()
    {
        QTemporaryDir data;
        QVERIFY(QDir(data.path()).mkpath(QStringLiteral("media")));
        const QString file = QStringLiteral("media/piep.wav");
        QVERIFY(QFile::copy(QStringLiteral(DSS_TEST_TON_DIR "/ton-piep.wav"), data.filePath(file)));
        Core core = start(data.path(), QStringLiteral("ton"), {QStringLiteral("--ersatzausgang")});
        CoreLink c(core.socket);
        QVERIFY(c.isConnected());
#if DSS_CORE_AUDIO
        // Standard: kein Ausgang gewählt
        RunRecord run = runAction(c, newAction(QStringLiteral("DSS-Test Ton ohne Ausgang"), {soundStep(file, true)}));
        QCOMPARE(run.state, RunState::Error);
        QCOMPARE(run.log.size(), 1);
        QVERIFY2(run.log.at(0).message.contains(QStringLiteral("Kein Ausgang für «Ton Chat-Bot»")),
                 qPrintable(run.log.at(0).message));
        // Meldung des Laufs über das Protokoll: die erste Fehlermeldung (Entscheid Nutzer 2026-11-16)
        QCOMPARE(run.message, run.log.at(0).message);

        // Ausgang wählen: der Ersatzausgang
        SettingsUpdate update;
        update.requestId = 9001;
        update.audioRoutes.insert(QStringLiteral("aktionen"), AudioRouteChange{QStringLiteral("dss_ersatzausgang"), std::nullopt});
        c.send(settingsUpdateMessage(update));
        QVERIFY(ok(c.waitFor(QString::fromLatin1(msg::kChangeResult))));

        // wait: der Lauf wartet bis zum Ende des Tons
        qint64 tookMs = 0;
        run = runAction(c, newAction(QStringLiteral("DSS-Test Ton warten"), {soundStep(file, true)}), {}, &tookMs);
        QVERIFY2(run.state == RunState::Ok, qPrintable(run.log.isEmpty() ? run.message : run.log.at(0).message));
        QVERIFY2(tookMs >= 950 && tookMs <= 3000, qPrintable(QString::number(tookMs)));

        // 9 Töne ohne wait: 8 spielen, der 9. wird abgewiesen
        QList<Step> nine;
        for (int i = 0; i < 9; ++i)
            nine.append(soundStep(file, false));
        run = runAction(c, newAction(QStringLiteral("DSS-Test neun Töne"), nine), {}, &tookMs);
        QCOMPARE(run.state, RunState::Error);
        QCOMPARE(run.log.size(), 9);
        for (int i = 0; i < 8; ++i)
            QCOMPARE(run.log.at(i).result, StepLogEntry::Result::Ok);
        QCOMPARE(run.log.at(8).result, StepLogEntry::Result::Error);
        QVERIFY2(run.log.at(8).message.contains(QStringLiteral("Schon 8 Töne")), qPrintable(run.log.at(8).message));
        QVERIFY2(tookMs < 900, qPrintable(QString::number(tookMs))); // ohne wait: wartet nicht

        // Fehlende Datei: Fehler im Lauf
        QTest::qWait(1500); // die 8 Töne sind zu Ende
        run = runAction(c, newAction(QStringLiteral("DSS-Test Ton fehlt"), {soundStep(QStringLiteral("media/fehlt.wav"), true)}));
        QCOMPARE(run.state, RunState::Error);
        QVERIFY2(run.log.at(0).message.contains(QStringLiteral("fehlt im Datenordner")), qPrintable(run.log.at(0).message));

        // Pfad ausserhalb der Mediathek: abgewiesen, bevor eine Datei geöffnet wird
        for (const QString &file : {QStringLiteral("media/../kern.ini"), QStringLiteral("/etc/hostname")}) {
            run = runAction(c, newAction(QStringLiteral("DSS-Test Ton Pfad"), {soundStep(file, true)}));
            QCOMPARE(run.state, RunState::Error);
            QVERIFY2(run.log.at(0).message.contains(QStringLiteral("ungültig")), qPrintable(run.log.at(0).message));
        }
        stop(core);
        QVERIFY2(core.log.contains(QStringLiteral("Ton Chat-Bot: Start")), qPrintable(core.log));
#else
        const RunRecord run = runAction(c, newAction(QStringLiteral("DSS-Test Ton"), {soundStep(file, true)}));
        QCOMPARE(run.state, RunState::Error);
        QCOMPARE(run.log.at(0).message, QStringLiteral("ohne Ton gebaut"));
        QCOMPARE(run.message, QStringLiteral("ohne Ton gebaut"));
        stop(core);
#endif
    }

    // c4 (Entscheid Nutzer 2026-11-16): «Lauf abbrechen» beendet die Töne des Laufs sofort, mit und
    // ohne wait; das normale Ende lässt Töne ohne wait zu Ende spielen. Zwei Läufe: nur der Ton des
    // abgebrochenen endet. Je Fall eine eigene Datei, damit das Kernprotokoll den Ton zuordnet.
    void cancelStopsSounds()
    {
#if !DSS_CORE_AUDIO
        QSKIP("ohne Ton gebaut");
#else
        QTemporaryDir data;
        QVERIFY(QDir(data.path()).mkpath(QStringLiteral("media")));
        const QStringList names{QStringLiteral("warten"), QStringLiteral("ohne"), QStringLiteral("normal"),
                                QStringLiteral("a"), QStringLiteral("b")};
        for (const QString &name : names)
            QVERIFY(QFile::copy(QStringLiteral(DSS_TEST_TON_DIR "/ton-piep.wav"),
                                data.filePath(QStringLiteral("media/%1.wav").arg(name))));
        Core core = start(data.path(), QStringLiteral("tonabbruch"), {QStringLiteral("--ersatzausgang")});
        CoreLink c(core.socket);
        QVERIFY(c.isConnected());
        SettingsUpdate update;
        update.requestId = 9002;
        update.audioRoutes.insert(QStringLiteral("aktionen"), AudioRouteChange{QStringLiteral("dss_ersatzausgang"), std::nullopt});
        c.send(settingsUpdateMessage(update));
        QVERIFY(ok(c.waitFor(QString::fromLatin1(msg::kChangeResult))));

        // Startet die Aktion, ohne auf das Ende zu warten; Ergebnis: Kennung des Laufs
        const auto launch = [&](const Action &action) {
            if (!ok(c.request(QString::fromLatin1(msg::kActionPut), {{QStringLiteral("action"), actionToJson(action)}})))
                return QJsonValue();
            const QJsonObject started = c.request(QString::fromLatin1(msg::kActionTest),
                                                  {{QStringLiteral("action_id"), idText(action.id)},
                                                   {QStringLiteral("dry_run"), false},
                                                   {QStringLiteral("arguments"), QJsonObject()}});
            return ok(started) ? started.value(QStringLiteral("id")) : QJsonValue();
        };
        const auto cancel = [&](const QJsonValue &run) {
            return ok(c.request(QString::fromLatin1(msg::kActionRunCancel), {{QStringLiteral("run_id"), run}}));
        };
        const auto file = [](const QString &name) { return QStringLiteral("media/%1.wav").arg(name); };
        // je Aktion ein neuer Schritt: Schrittkennungen sind in aktionen.json eindeutig
        const auto longDelay = [] {
            Step s = newStep(QString::fromLatin1(kStepDelay));
            s.fields.insert(QStringLiteral("value_ms"), 5000);
            return s;
        };

        // mit wait: abbrechen, während der Lauf auf das Ende des Tons wartet
        QJsonValue run = launch(newAction(QStringLiteral("DSS-Test Ton warten"), {soundStep(file(QStringLiteral("warten")), true)}));
        QVERIFY(!run.isUndefined() && !run.isNull());
        QTest::qWait(200);
        QVERIFY(cancel(run));

        // ohne wait: der Lauf wartet danach, der Ton spielt noch
        run = launch(newAction(QStringLiteral("DSS-Test Ton ohne"), {soundStep(file(QStringLiteral("ohne")), false), longDelay()}));
        QVERIFY(!run.isUndefined() && !run.isNull());
        QTest::qWait(200);
        QVERIFY(cancel(run));

        // normales Ende mit einem Ton ohne wait: der Ton spielt zu Ende
        const RunRecord normal = runAction(c, newAction(QStringLiteral("DSS-Test Ton normal"), {soundStep(file(QStringLiteral("normal")), false)}));
        QCOMPARE(normal.state, RunState::Ok);

        // zwei Läufe, nur a wird abgebrochen
        const QJsonValue runA = launch(newAction(QStringLiteral("DSS-Test Ton a"), {soundStep(file(QStringLiteral("a")), false), longDelay()}));
        const QJsonValue runB = launch(newAction(QStringLiteral("DSS-Test Ton b"), {soundStep(file(QStringLiteral("b")), false), longDelay()}));
        QVERIFY(!runA.isNull() && !runB.isNull());
        QTest::qWait(200);
        QVERIFY(cancel(runA));
        QTest::qWait(1500); // b (1 s) spielt zu Ende
        QVERIFY(cancel(runB)); // b ist schon zu Ende: nichts mehr zu beenden

        stop(core);
        for (const QString &name : {QStringLiteral("warten"), QStringLiteral("ohne"), QStringLiteral("a")})
            QVERIFY2(core.log.contains(QStringLiteral("Ton Chat-Bot: Ende %1: abgebrochen").arg(file(name))), qPrintable(core.log));
        for (const QString &name : {QStringLiteral("normal"), QStringLiteral("b")})
            QVERIFY2(core.log.contains(QStringLiteral("Ton Chat-Bot: Ende %1: gespielt").arg(file(name))), qPrintable(core.log));
#endif
    }

    // e2-1 (Entscheid Nutzer 2026-11-16): Aktion abschalten oder löschen bricht ihre Läufe ab (auch
    // Probeläufe), andere laufen weiter; Hauptschalter aus bricht alle ab. actions.run_finished
    // meldet cancelled mit dem Grund.
    void editCancelsRuns()
    {
        QTemporaryDir data;
        Core core = start(data.path(), QStringLiteral("abbruch"));
        CoreLink c(core.socket);
        QVERIFY(c.isConnected());
        QVERIFY(ok(c.request(QString::fromLatin1(msg::kActionsSetEnabled), {{QStringLiteral("enabled"), true}})));
        c.send(subscribeEventsMessage({{QStringLiteral("actions.")}, true}));
        QVERIFY(!c.waitFor(QString::fromLatin1(msg::kEventHistory)).isEmpty());

        // je Aktion ein neuer Schritt: Schrittkennungen sind in aktionen.json eindeutig
        const auto longAction = [](const QString &name) {
            Step s = newStep(QString::fromLatin1(kStepDelay));
            s.fields.insert(QStringLiteral("value_ms"), 30000);
            return newAction(name, {s});
        };
        const auto put = [&](const Action &action) {
            return ok(c.request(QString::fromLatin1(msg::kActionPut), {{QStringLiteral("action"), actionToJson(action)}}));
        };
        const auto launch = [&](const Action &action, bool dryRun) {
            const QJsonObject started = c.request(QString::fromLatin1(msg::kActionTest),
                                                  {{QStringLiteral("action_id"), idText(action.id)},
                                                   {QStringLiteral("dry_run"), dryRun},
                                                   {QStringLiteral("arguments"), QJsonObject()}});
            return ok(started) ? started.value(QStringLiteral("id")).toString() : QString();
        };
        // Wartet auf actions.run_finished der genannten Läufe; Ergebnis: Kennung → Daten
        const auto finished = [&](const QStringList &ids) {
            QHash<QString, QJsonObject> result;
            for (int i = 0; i < 50 && result.size() < ids.size(); ++i) {
                const QJsonObject message = c.waitFor(QString::fromLatin1(msg::kEvent), 2000);
                events::Event event;
                QString error;
                if (message.isEmpty() || !parseEventMessage(message, &event, &error))
                    break;
                const QString run = event.data.value(QStringLiteral("run_id")).toString();
                if (event.type == QLatin1String("actions.run_finished") && ids.contains(run))
                    result.insert(run, event.data);
            }
            return result;
        };
        const auto checkCancelled = [](const QJsonObject &data, const QString &message) {
            return data.value(QStringLiteral("state")).toString() == QLatin1String("cancelled")
                && data.value(QStringLiteral("message")).toString() == message;
        };

        Action a = longAction(QStringLiteral("DSS-Test Abbruch a"));
        const Action b = longAction(QStringLiteral("DSS-Test Abbruch b"));
        QVERIFY(put(a));
        QVERIFY(put(b));

        // Abschalten: echter Lauf und Probelauf von a enden, b läuft weiter
        const QString a1 = launch(a, false);
        const QString a2 = launch(a, true);
        const QString b1 = launch(b, false);
        QVERIFY(!a1.isEmpty() && !a2.isEmpty() && !b1.isEmpty());
        a.enabled = false;
        QVERIFY(put(a));
        QHash<QString, QJsonObject> done = finished({a1, a2});
        QCOMPARE(done.size(), 2);
        for (const QJsonObject &d : std::as_const(done))
            QVERIFY2(checkCancelled(d, QStringLiteral("abgebrochen: Aktion abgeschaltet")),
                     qPrintable(QJsonDocument(d).toJson()));
        const QList<RunRecord> afterDisable = runs(c);
        QCOMPARE(afterDisable.size(), 3);
        QCOMPARE(std::count_if(afterDisable.cbegin(), afterDisable.cend(),
                               [](const RunRecord &r) { return r.state == RunState::Running; }),
                 1);

        // Löschen
        a.enabled = true;
        QVERIFY(put(a));
        const QString a3 = launch(a, false);
        QVERIFY(!a3.isEmpty());
        QVERIFY(ok(c.request(QString::fromLatin1(msg::kActionRemove), {{QStringLiteral("action_id"), idText(a.id)}})));
        done = finished({a3});
        QCOMPARE(done.size(), 1);
        QVERIFY(checkCancelled(done.value(a3), QStringLiteral("abgebrochen: Aktion gelöscht")));

        // Hauptschalter aus: auch b und ein Probelauf von b
        const QString b2 = launch(b, true);
        QVERIFY(!b2.isEmpty());
        QVERIFY(ok(c.request(QString::fromLatin1(msg::kActionsSetEnabled), {{QStringLiteral("enabled"), false}})));
        done = finished({b1, b2});
        QCOMPARE(done.size(), 2);
        for (const QJsonObject &d : std::as_const(done))
            QVERIFY(checkCancelled(d, QStringLiteral("abgebrochen: Chat-Bot aus")));
        stop(core);
        QVERIFY2(core.log.contains(QStringLiteral("Lauf «DSS-Test Abbruch b»: cancelled, abgebrochen: Chat-Bot aus")),
                 qPrintable(core.log));
    }

    // c2 (Plan 4.4): Overlay-Element zeigen/verstecken über den Laufzustand des Kerns
    void overlayStepShowsElements()
    {
        QTemporaryDir data;
        QUuid overlayId;
        QUuid greeting;
        QUuid thanks;
        QUuid counterText;
        QUuid picture;
        QString otherImage;
        {
            OverlayStore store(data.path());
            QString error;
            QVERIFY2(store.load(&error), qPrintable(error));
            overlayId = store.createOverlay(QStringLiteral("DSS-Test Begrüssung"), 640, 360, &error);
            QVERIFY2(!overlayId.isNull(), qPrintable(error));
            greeting = store.addTextSource(overlayId, QStringLiteral("Hallo %user%!"), QStringLiteral(DSS_TEST_FONT), &error);
            thanks = store.addTextSource(overlayId, QStringLiteral("Danke"), QStringLiteral(DSS_TEST_FONT), &error);
            counterText = store.addTextSource(overlayId, QStringLiteral("~dsstest_zaehler~ von %USER% (%user%)"),
                                              QStringLiteral(DSS_TEST_FONT), &error);
            QVERIFY2(!counterText.isNull(), qPrintable(error));
            picture = store.addImageSource(overlayId, QStringLiteral(DSS_TEST_EXIF_DIR "/orientierung-1.png"), &error);
            const QUuid other = store.addImageSource(overlayId, QStringLiteral(DSS_TEST_EXIF_DIR "/orientierung-2.png"), &error);
            QVERIFY2(!greeting.isNull() && !thanks.isNull() && !picture.isNull() && !other.isNull(), qPrintable(error));
            for (const Source &s : store.overlay(overlayId)->sources) {
                if (s.id == other)
                    otherImage = s.file;
            }
            QVERIFY(otherImage.startsWith(QStringLiteral("media/")));
        }
        const QString overlayFile = data.filePath(QStringLiteral("overlays/") + idText(overlayId) + QStringLiteral(".json"));
        const QByteArray before = readFile(overlayFile);
        QVERIFY(!before.isEmpty());

        Core core = start(data.path(), QStringLiteral("overlay"));
        CoreLink c(core.socket);
        QVERIFY(c.isConnected());
        const QJsonObject args{{QStringLiteral("user"), QStringLiteral("Zuschauer42")},
                               {QStringLiteral("amount"), 100},
                               {QStringLiteral("bild"), otherImage}};

        // Drei Elemente gemeinsam: Platzhalter im eigenen Text, text ersetzt, Bild aus einem Argument
        qint64 tookMs = 0;
        RunRecord run = runAction(c, newAction(QStringLiteral("DSS-Test zeigen"),
                                               {overlayStep(overlayId, QStringLiteral("show"),
                                                            {{greeting, std::nullopt, std::nullopt},
                                                             {thanks, QStringLiteral("Danke %user% für %amount% Bits"), std::nullopt},
                                                             {picture, std::nullopt, QStringLiteral("%bild%")}},
                                                            3000, false)}),
                                  args, &tookMs);
        QVERIFY2(run.state == RunState::Ok, qPrintable(run.log.isEmpty() ? run.message : run.log.at(0).message));
        QVERIFY2(tookMs < 2000, qPrintable(QString::number(tookMs))); // ohne wait
        LiveElements live = liveElements(core.socket, overlayId);
        QCOMPARE(live.size(), 3);
        QCOMPARE(live.value(greeting).visible, std::optional<bool>(true));
        QCOMPARE(live.value(greeting).text, std::optional<QString>(QStringLiteral("Hallo Zuschauer42!")));
        QCOMPARE(live.value(thanks).text, std::optional<QString>(QStringLiteral("Danke Zuschauer42 für 100 Bits")));
        QCOMPARE(live.value(picture).visible, std::optional<bool>(true));
        QCOMPARE(live.value(picture).image, otherImage);
        QVERIFY(live.value(picture).remainingMs > 0 && live.value(picture).remainingMs <= 3000);
        // Nach der Dauer fällt der ganze Laufzustand der Elemente weg
        QTRY_VERIFY_WITH_TIMEOUT(liveElements(core.socket, overlayId).isEmpty(), 6000);

        // Ein eingesetzter Wert wird nie nochmals ersetzt (Plan 2.9): «%…%» in einem Wert bleibt wörtlich,
        // im Text aus dem Schritt, im eigenen Text des Elements und im Bild
        const QJsonObject tricky{{QStringLiteral("user"), QStringLiteral("%geheim%")},
                                 {QStringLiteral("geheim"), QStringLiteral("Wert-X")},
                                 {QStringLiteral("bild"), QStringLiteral("%anderesBild%")},
                                 {QStringLiteral("anderesBild"), otherImage}};
        run = runAction(c, newAction(QStringLiteral("DSS-Test einmal ersetzen"),
                                     {overlayStep(overlayId, QStringLiteral("show"),
                                                  {{greeting, std::nullopt, std::nullopt},
                                                   {thanks, QStringLiteral("Danke %user%"), std::nullopt},
                                                   {picture, std::nullopt, QStringLiteral("%bild%")}},
                                                  300, false)}),
                        tricky, &tookMs);
        QVERIFY2(run.state == RunState::Ok, qPrintable(run.log.isEmpty() ? run.message : run.log.at(0).message));
        live = liveElements(core.socket, overlayId);
        QCOMPARE(live.value(greeting).text, std::optional<QString>(QStringLiteral("Hallo %geheim%!")));
        QCOMPARE(live.value(thanks).text, std::optional<QString>(QStringLiteral("Danke %geheim%")));
        QCOMPARE(live.value(picture).image, QString()); // «%anderesBild%» ist kein Bild: eigenes Bild
        QTRY_VERIFY_WITH_TIMEOUT(liveElements(core.socket, overlayId).isEmpty(), 6000);

        // c5: Eigener Text des Elements wie Plan 2.9: «~name~» setzt die dauerhafte globale
        // Variable ein, Gross- und Kleinschreibung des Arguments zählt nicht, ein Wert mit «%…%»
        // wird nicht nochmals ersetzt
        Step counter = newStep(QString::fromLatin1(kStepGlobalSet));
        counter.fields.insert(QStringLiteral("name"), QStringLiteral("dsstest_zaehler"));
        counter.fields.insert(QStringLiteral("operation"), QStringLiteral("increment"));
        counter.fields.insert(QStringLiteral("value"), QStringLiteral("7"));
        run = runAction(c, newAction(QStringLiteral("DSS-Test Zähler zeigen"),
                                     {counter, overlayStep(overlayId, QStringLiteral("show"),
                                                           {{counterText, std::nullopt, std::nullopt}}, 300, false)}),
                        tricky, &tookMs);
        QVERIFY2(run.state == RunState::Ok, qPrintable(run.log.isEmpty() ? run.message : run.log.last().message));
        live = liveElements(core.socket, overlayId);
        QCOMPARE(live.value(counterText).text, std::optional<QString>(QStringLiteral("7 von %geheim% (%geheim%)")));
        QTRY_VERIFY_WITH_TIMEOUT(liveElements(core.socket, overlayId).isEmpty(), 6000);

        // wait: der Lauf wartet die Dauer ab; «weich» wirkt in P14-1 wie ohne Übergang
        Step soft = overlayStep(overlayId, QStringLiteral("show"), {{greeting, std::nullopt, std::nullopt}}, 600, true);
        soft.fields.insert(QStringLiteral("transition_in"), QStringLiteral("fade"));
        run = runAction(c, newAction(QStringLiteral("DSS-Test warten"), {soft}), args, &tookMs);
        QCOMPARE(run.state, RunState::Ok);
        QVERIFY2(tookMs >= 590, qPrintable(QString::number(tookMs)));
        QVERIFY2(run.log.at(0).message.contains(QStringLiteral("Übergang «weich»")), qPrintable(run.log.at(0).message));

        // Verstecken ohne Dauer bleibt bis zum Neustart
        run = runAction(c, newAction(QStringLiteral("DSS-Test verstecken"),
                                     {overlayStep(overlayId, QStringLiteral("hide"), {{greeting, std::nullopt, std::nullopt}}, 0, true)}));
        QCOMPARE(run.state, RunState::Ok);
        live = liveElements(core.socket, overlayId);
        QCOMPARE(live.value(greeting).visible, std::optional<bool>(false));
        QCOMPARE(live.value(greeting).remainingMs, qint64(-1));

        // Unbekanntes Element und unbekanntes Overlay: Fehler im Lauf, der Lauf geht weiter
        run = runAction(c, newAction(QStringLiteral("DSS-Test unbekannt"),
                                     {overlayStep(overlayId, QStringLiteral("show"), {{QUuid::createUuid(), std::nullopt, std::nullopt}}, 0, false),
                                      overlayStep(QUuid::createUuid(), QStringLiteral("show"), {{greeting, std::nullopt, std::nullopt}}, 0, false)}));
        QCOMPARE(run.state, RunState::Error);
        QCOMPARE(run.log.size(), 2);
        QVERIFY2(run.log.at(0).message.contains(QStringLiteral("gibt es im Overlay")), qPrintable(run.log.at(0).message));
        QVERIFY2(run.log.at(1).message.contains(QStringLiteral("Kein Overlay")), qPrintable(run.log.at(1).message));

        stop(core);
        // Laufzustand nie in die Overlay-Datei (Plan 4.4)
        QCOMPARE(readFile(overlayFile), before);
        QVERIFY2(!core.log.contains(QStringLiteral("Zuschauer42")), qPrintable(core.log));
    }

    void protocolMessages()
    {
        // Anfragen: Art der Felder, request_id, Vorschlag der Ansichtslogik (actionview.h)
        ActionsRequest r;
        QString error;
        QVERIFY(parseActionsRequest(actionsEditMessage({QStringLiteral("action_remove"),
                                                        {{QStringLiteral("action_id"), idText(QUuid::createUuid())}}},
                                                       7),
                                    &r, &error));
        QCOMPARE(r.requestId, 7);
        QVERIFY(!parseActionsRequest(actionsRequestMessage({1, QStringLiteral("action_remove"), {}}), &r, &error));
        QVERIFY(!parseActionsRequest(actionsRequestMessage({1, QStringLiteral("action_put"),
                                                            {{QStringLiteral("action"), 3}}}),
                                     &r, &error));
        QVERIFY(parseActionsRequest(actionsRequestMessage({1, QStringLiteral("queue_control"),
                                                           {{QStringLiteral("queue_id"), QJsonValue::Null},
                                                            {QStringLiteral("control"), QStringLiteral("pause")}}}),
                                    &r, &error));
        QVERIFY(parseActionsRequest(actionsRequestMessage({1, QStringLiteral("variable_set"),
                                                           {{QStringLiteral("scope"), QStringLiteral("global")},
                                                            {QStringLiteral("name"), QStringLiteral("x")},
                                                            {QStringLiteral("persisted"), true},
                                                            {QStringLiteral("value"), 3}}}),
                                    &r, &error));
        QVERIFY(!r.fields.contains(QStringLiteral("user_id")));
        // chat_send (Version 18): reply_to Text oder null, Pflicht
        QVERIFY(actionsRequestTypes().contains(QStringLiteral("chat_send")));
        const auto chatSend = [&](const QJsonValue &replyTo) {
            QJsonObject fields{{QStringLiteral("text"), QStringLiteral("x")}, {QStringLiteral("account"), QStringLiteral("bot")}};
            if (!replyTo.isUndefined())
                fields.insert(QStringLiteral("reply_to"), replyTo);
            return parseActionsRequest(actionsRequestMessage({2, QString::fromLatin1(msg::kChatSend), fields}), &r, &error);
        };
        QVERIFY(chatSend(QJsonValue::Null));
        QVERIFY(r.fields.value(QStringLiteral("reply_to")).isNull());
        QVERIFY(chatSend(QStringLiteral("m-1")));
        QCOMPARE(r.fields.value(QStringLiteral("reply_to")).toString(), QStringLiteral("m-1"));
        QVERIFY(!chatSend(QJsonValue::Undefined));
        QVERIFY(!chatSend(1));
        QVERIFY(!parseActionsRequest(actionsRequestMessage({2, QString::fromLatin1(msg::kChatSend),
                                                            {{QStringLiteral("text"), 1}, {QStringLiteral("account"), QStringLiteral("bot")},
                                                             {QStringLiteral("reply_to"), QJsonValue::Null}}}),
                                     &r, &error));
        // Lauf hin und zurück
        RunRecord run;
        run.runId = QUuid::createUuid();
        run.actionId = QUuid::createUuid();
        run.actionName = QStringLiteral("A");
        run.triggerType = QStringLiteral("test");
        run.state = RunState::Running;
        run.stepIndex = 2;
        run.stepCount = 5;
        run.queuedAt = QDateTime::fromString(QStringLiteral("2026-11-15T10:00:00.000Z"), Qt::ISODateWithMs);
        run.log.append({run.queuedAt, QUuid::createUuid(), QStringLiteral("chat_send"), {1, 2}, 0,
                        StepLogEntry::Result::DryRun, QStringLiteral("x")});
        RunRecord back;
        QVERIFY(parseActionRun(actionRunMessage(3, run), &back, &error));
        QCOMPARE(back.runId, run.runId);
        QCOMPARE(back.stepIndex, 2);
        QCOMPARE(back.log.size(), 1);
        QCOMPARE(back.log.first().path, (QList<int>{1, 2}));
        QCOMPARE(back.log.first().result, StepLogEntry::Result::DryRun);
        // Ereignis passt zur Typenliste
        events::Event e;
        e.type = QStringLiteral("actions.run_changed");
        e.data = runEventData(run);
        QVERIFY2(events::EventBus::validate(e).isEmpty(), qPrintable(events::EventBus::validate(e)));
    }
};

QTEST_GUILESS_MAIN(TestKernAktionen)
#include "tst_kernaktionen.moc"
