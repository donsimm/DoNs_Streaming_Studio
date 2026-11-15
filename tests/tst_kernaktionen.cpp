// Aktionssystem im Kern (P14 b3, Plan 3 und 5) mit dss-core und dss-cli als eigenen Prozessen:
// Änderung übersteht Neustart, ungültige aktionen.json wird nicht überschrieben, stumm bei
// zweitem Kern (Version im Einsatz), Ereignis über den Fluss löst eine Aktion aus, keine
// Chat-Texte und keine Zuschauernamen im Protokoll. Erfundene Daten, eigener Datenordner.
// Ereignisse kommen über test_event (dss-core --testereignisse), weil Twitch erst mit c1
// im Kern angebunden ist.

#include "actionjson.h"
#include "actionprotocol.h"
#include "eventbus.h"
#include "eventtypes.h"
#include "protocol.h"
#include "prozesshilfe.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>

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
