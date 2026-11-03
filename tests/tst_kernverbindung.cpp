// Zusammenspiel Kern–Verbindung (P07, Tests): dss-core läuft als eigener Prozess mit eigenem
// Datenordner und eigener Verbindung. Eine Testverbindung (CoreClient wie im Fenster und eine
// rohe Verbindung für Fehlerfälle) meldet sich an, liest Liste und Overlay, empfängt
// Leistungswerte; der Kern wird neu geladen, beendet, nach kill -9 neu gestartet, und die
// Verbindung baut sich wieder auf. Nur Qt Core, Network und Test (kein Qt Gui).
// Daten wie im Handtest (CLAUDE.md Regel 7): Bild und Video aus prototypes/media, Schrift
// DejaVu Sans.

#include "browsersource.h"
#include "coreclient.h"
#include "coresettings.h"
#include "overlayjson.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#include "version.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <memory>

using namespace dss;
using namespace testhelp;

namespace {

const QString kMediaDir = QStringLiteral(DSS_TEST_MEDIA_DIR);
const QString kFont = QStringLiteral(DSS_TEST_FONT);

// Rohe Verbindung, um einzelne Zeilen zu schicken und alle Antworten zu sehen.
class RawConnection
{
public:
    explicit RawConnection(const QString &path)
    {
        m_socket.connectToServer(path);
        m_connected = m_socket.waitForConnected(3000);
    }
    bool isConnected() const { return m_connected; }
    void sendLine(const QByteArray &line)
    {
        m_socket.write(line + '\n');
        m_socket.waitForBytesWritten(1000);
    }
    void hello(const QString &program, int version = kProtocolVersion)
    {
        m_socket.write(encodeMessage(helloMessage({program, QStringLiteral("0"), version})));
        m_socket.waitForBytesWritten(1000);
    }
    // Nächste Nachricht vom Typ type (andere, zum Beispiel stats, werden übersprungen).
    QJsonObject waitFor(const QString &type, int timeoutMs = 3000)
    {
        const QDeadlineTimer deadline(timeoutMs);
        for (;;) {
            QByteArray line;
            while (m_lines.next(&line) == LineSplitter::Result::Line) {
                m_all.append(line);
                QJsonObject o;
                QString error;
                if (decodeMessage(line, &o, &error) && messageType(o) == type)
                    return o;
            }
            if (deadline.hasExpired() || m_socket.state() != QLocalSocket::ConnectedState)
                return {};
            if (m_socket.waitForReadyRead(int(deadline.remainingTime())))
                m_lines.append(m_socket.readAll());
        }
    }
    bool waitForDisconnected(int timeoutMs = 3000)
    {
        if (m_socket.state() == QLocalSocket::UnconnectedState)
            return true;
        // Restliche Daten lesen, bis die Gegenseite trennt.
        const QDeadlineTimer deadline(timeoutMs);
        while (m_socket.state() != QLocalSocket::UnconnectedState && !deadline.hasExpired()) {
            if (m_socket.waitForReadyRead(int(deadline.remainingTime())))
                m_lines.append(m_socket.readAll());
        }
        return m_socket.state() == QLocalSocket::UnconnectedState;
    }
    QByteArray allLines() const { return m_all.join('\n'); }

private:
    QLocalSocket m_socket;
    LineSplitter m_lines;
    QList<QByteArray> m_all;
    bool m_connected = false;
};

} // namespace

class TestKernVerbindung : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    QTemporaryDir m_config; // XDG_CONFIG_HOME des Kerns (kern.ini), bleibt über Neustarts
    std::unique_ptr<QTemporaryDir> m_run;
    QString m_socket;
    std::unique_ptr<QProcess> m_core;
    QString m_coreLog;
    QUuid m_overviewId;
    QUuid m_chatId;
    QString m_overviewToken;

    std::unique_ptr<CoreClient> client(const QString &program = QStringLiteral("tst_kernverbindung"))
    {
        CoreClient::Options o;
        o.socketPath = m_socket;
        o.program = program;
        o.version = version();
        o.reconnectIntervalMs = 200;
        return std::make_unique<CoreClient>(o);
    }

    bool startCore()
    {
        m_core = testhelp::startCore(m_data.path(), m_socket, m_config.path());
        m_coreLog.clear();
        return waitForLog(m_core.get(), QStringLiteral("Verbindung offen"), &m_coreLog);
    }

    // Overlay so verändern, wie es der Nutzer von Hand in der Datei täte.
    void writeOverlayFile(const Overlay &o)
    {
        QFile f(m_data.path() + QStringLiteral("/overlays/") + o.id.toString(QUuid::WithoutBraces)
                + QStringLiteral(".json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(serializeOverlay(o));
    }

    Overlay storedOverlay(const QUuid &id)
    {
        OverlayStore store(m_data.path());
        QString error;
        store.load(&error, OverlayStore::LoadMode::ReadOnly);
        return store.overlay(id) ? *store.overlay(id) : Overlay();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        m_run = runtimeDir();
        QVERIFY(m_run->isValid());
        m_socket = m_run->filePath(QStringLiteral("kern.sock"));
        {
            OverlayStore store(m_data.path());
            QString error;
            QVERIFY(store.load(&error));
            m_overviewId = store.createOverlay(QStringLiteral("Überblick Start"), 1920, 1080, &error);
            m_chatId = store.createOverlay(QStringLiteral("Chat Ecke"), 800, 600, &error);
            QVERIFY2(!m_overviewId.isNull() && !m_chatId.isNull(), qPrintable(error));
            QVERIFY2(!store.addImageSource(m_overviewId, kMediaDir + QStringLiteral("/bild1.png"), &error).isNull(),
                     qPrintable(error));
            QVERIFY(!store.addVideoSource(m_overviewId, kMediaDir + QStringLiteral("/alpha.webm"), true, &error).isNull());
            const QUuid text = store.addTextSource(m_overviewId, QStringLiteral("Willkommen\nZweite Zeile"), kFont, &error);
            QVERIFY(!text.isNull());
            QVERIFY(store.setSourceVisible(m_overviewId, text, false, &error));
            m_overviewToken = store.token(m_overviewId);
            QCOMPARE(m_overviewToken.size(), 43);
        }
        QVERIFY2(startCore(), qPrintable(m_coreLog));
    }

    void cleanupTestCase()
    {
        if (m_core && m_core->state() != QProcess::NotRunning) {
            m_core->terminate();
            m_core->waitForFinished(5000);
        }
    }

    void helloListAndConnections()
    {
        auto c = client();
        QSignalSpy listSpy(c.get(), &CoreClient::overlayListChanged);
        c->start();
        QTRY_VERIFY_WITH_TIMEOUT(c->isConnected(), 3000);
        QCOMPARE(c->coreHello().program, QStringLiteral("dss-core"));
        QCOMPARE(c->coreHello().version, version());
        QCOMPARE(c->coreHello().protocolVersion, 9); // seit P10 (reset_settings)
        QTRY_COMPARE_WITH_TIMEOUT(c->overlays().size(), 2, 3000);
        // Seit P08 in der Reihenfolge der Liste (liste.json): angelegt «Überblick Start», dann
        // «Chat Ecke».
        QCOMPARE(c->overlays().at(0), summarizeOverlay(storedOverlay(m_overviewId)));
        QCOMPARE(c->overlays().at(1), summarizeOverlay(storedOverlay(m_chatId)));
        QCOMPARE(c->overlays().at(0).sourceCount, 3);
        QCOMPARE(c->overlays().at(1).width, 800);
        QCOMPARE(c->listData().layout.entries.size(), 2);
        QCOMPARE(c->statusText(), QStringLiteral("Mit dem Kern verbunden (dss-core %1)").arg(version()));
        for (Connection con : kAllConnections)
            QCOMPARE(c->connectionStates().value(con), ConnectionState::Off);
    }

    void wholeOverlayOnRequest()
    {
        auto c = client();
        c->start();
        QTRY_VERIFY_WITH_TIMEOUT(c->isConnected(), 3000);
        QSignalSpy spy(c.get(), &CoreClient::overlayChanged);
        c->requestOverlay(m_overviewId);
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 3000);
        QCOMPARE(spy.at(0).at(0).toUuid(), m_overviewId);
        const std::optional<Overlay> o = c->overlay(m_overviewId);
        QVERIFY(o);
        QCOMPARE(*o, storedOverlay(m_overviewId));
        QCOMPARE(o->sources.size(), 3);
        QCOMPARE(o->sources.at(2).visible, false);
        QCOMPARE(o->sources.at(1).loop, true);
    }

    void statsOncePerSecond()
    {
        auto c = client();
        QSignalSpy spy(c.get(), &CoreClient::statsReceived);
        c->start();
        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 3, 4000);
        QVERIFY(c->lastStats());
        const SystemStats last = *c->lastStats();
        QVERIFY(last.cpuPercent && *last.cpuPercent >= 0 && *last.cpuPercent <= 100);
        QVERIFY(last.ramTotalKiB && *last.ramTotalKiB > 0);
        QVERIFY(last.ramUsedKiB && *last.ramUsedKiB <= *last.ramTotalKiB);
        // Abstand etwa 1 s: in 2.5 s kommen 2 bis 4 Werte.
        spy.clear();
        QTest::qWait(2500);
        QVERIFY2(spy.count() >= 2 && spy.count() <= 4, qPrintable(QString::number(spy.count())));
    }

    void tokenNeverSent()
    {
        RawConnection raw(m_socket);
        QVERIFY(raw.isConnected());
        raw.hello(QStringLiteral("tst"));
        QVERIFY(!raw.waitFor(QStringLiteral("overlay_list")).isEmpty());
        raw.sendLine(encodeMessage(overlayIdMessage(msg::kGetOverlay, m_overviewId)).chopped(1));
        QVERIFY(!raw.waitFor(QStringLiteral("overlay")).isEmpty());
        QVERIFY(!raw.waitFor(QStringLiteral("stats")).isEmpty());
        QVERIFY(!raw.allLines().isEmpty());
        QVERIFY(!raw.allLines().contains(m_overviewToken.toLatin1()));
        QVERIFY(!raw.allLines().contains("token"));
    }

    void errorCases()
    {
        RawConnection raw(m_socket);
        QVERIFY(raw.isConnected());
        QCOMPARE(raw.waitFor(QStringLiteral("hello")).value(QStringLiteral("program")).toString(),
                 QStringLiteral("dss-core"));
        raw.sendLine("{\"type\":\"get_overlay_list\"}");
        QJsonObject e = raw.waitFor(QStringLiteral("error"));
        QCOMPARE(e.value(QStringLiteral("code")).toString(), QStringLiteral("hello_missing"));
        QCOMPARE(e.value(QStringLiteral("request")).toString(), QStringLiteral("get_overlay_list"));
        raw.sendLine("das ist kein JSON");
        e = raw.waitFor(QStringLiteral("error"));
        QCOMPARE(e.value(QStringLiteral("code")).toString(), QStringLiteral("invalid_json"));
        QVERIFY(e.value(QStringLiteral("request")).isNull());
        raw.hello(QStringLiteral("tst"));
        QVERIFY(!raw.waitFor(QStringLiteral("overlay_list")).isEmpty());
        raw.sendLine("{\"type\":\"get_overlay\",\"id\":\"00000000-0000-4000-8000-000000000000\"}");
        e = raw.waitFor(QStringLiteral("error"));
        QCOMPARE(e.value(QStringLiteral("code")).toString(), QStringLiteral("unknown_overlay"));
        raw.sendLine("{\"type\":\"get_overlay\"}");
        e = raw.waitFor(QStringLiteral("error"));
        QCOMPARE(e.value(QStringLiteral("code")).toString(), QStringLiteral("invalid_message"));
        raw.sendLine("{\"type\":\"overlay_bearbeiten\"}");
        e = raw.waitFor(QStringLiteral("error"));
        QCOMPARE(e.value(QStringLiteral("code")).toString(), QStringLiteral("unknown_type"));
        // Die Verbindung bleibt nach diesen Fehlern bestehen.
        raw.sendLine("{\"type\":\"get_overlay_list\"}");
        QVERIFY(!raw.waitFor(QStringLiteral("overlay_list")).isEmpty());
    }

    void otherProtocolVersionIsRejected()
    {
        RawConnection raw(m_socket);
        QVERIFY(raw.isConnected());
        raw.hello(QStringLiteral("tst"), 99);
        const QJsonObject e = raw.waitFor(QStringLiteral("error"));
        QCOMPARE(e.value(QStringLiteral("code")).toString(), QStringLiteral("protocol_version"));
        QVERIFY(raw.waitForDisconnected());
    }

    void tooLongLineCloses()
    {
        RawConnection raw(m_socket);
        QVERIFY(raw.isConnected());
        raw.sendLine(QByteArray(kMaxMessageBytes + 1, 'x'));
        const QJsonObject e = raw.waitFor(QStringLiteral("error"), 5000);
        QCOMPARE(e.value(QStringLiteral("code")).toString(), QStringLiteral("message_too_long"));
        QVERIFY(raw.waitForDisconnected(5000));
    }

    void reloadSendsWholeChangedOverlayToAll()
    {
        // K-02: Bei jeder Änderung bekommt jedes verbundene Fenster das ganze Overlay, auch
        // wenn es dieses Overlay nie angefragt hat. In P07 ändert sich nur etwas beim Neu laden.
        auto a = client();
        auto b = client();
        a->start();
        b->start();
        QTRY_VERIFY_WITH_TIMEOUT(a->isConnected() && b->isConnected(), 3000);
        QSignalSpy changedA(a.get(), &CoreClient::overlayChanged);
        QSignalSpy changedB(b.get(), &CoreClient::overlayChanged);
        QSignalSpy removedB(b.get(), &CoreClient::overlayRemoved);

        Overlay chat = storedOverlay(m_chatId);
        chat.name = QStringLiteral("Chat Ecke neu");
        chat.width = 1280;
        writeOverlayFile(chat);
        Overlay added;
        added.id = QUuid::fromString(QStringLiteral("5f1c2d3e-4b5a-4c6d-8e7f-901234567890"));
        added.name = QStringLiteral("Von Hand");
        added.address = QStringLiteral("von-hand");
        writeOverlayFile(added);
        QCOMPARE(::kill(pid_t(m_core->processId()), SIGHUP), 0);

        QTRY_COMPARE_WITH_TIMEOUT(changedB.count(), 2, 3000);
        QCOMPARE(changedA.count(), 2);
        QVERIFY(b->overlay(m_chatId));
        QCOMPARE(*b->overlay(m_chatId), chat);
        QCOMPARE(*a->overlay(added.id), added);
        QTRY_COMPARE_WITH_TIMEOUT(b->overlays().size(), 3, 3000);
        bool found = false;
        for (const OverlaySummary &s : b->overlays())
            found |= s.id == m_chatId && s.name == QStringLiteral("Chat Ecke neu") && s.width == 1280;
        QVERIFY(found);
        QVERIFY(waitForLog(m_core.get(), QStringLiteral("2 geändert oder neu, 0 entfernt"), &m_coreLog));

        // Entfernte Datei: overlay_removed und neue Liste.
        QVERIFY(QFile::remove(m_data.path() + QStringLiteral("/overlays/")
                              + added.id.toString(QUuid::WithoutBraces) + QStringLiteral(".json")));
        changedB.clear();
        QCOMPARE(::kill(pid_t(m_core->processId()), SIGHUP), 0);
        QTRY_COMPARE_WITH_TIMEOUT(removedB.count(), 1, 3000);
        QCOMPARE(removedB.at(0).at(0).toUuid(), added.id);
        QTRY_COMPARE_WITH_TIMEOUT(b->overlays().size(), 2, 3000);
        QVERIFY(!b->overlay(added.id));
        QCOMPARE(changedB.count(), 0); // nichts anderes hat sich geändert

        // Neu laden ohne Änderung schickt nichts.
        QSignalSpy listB(b.get(), &CoreClient::overlayListChanged);
        QCOMPARE(::kill(pid_t(m_core->processId()), SIGHUP), 0);
        QVERIFY(waitForLog(m_core.get(), QStringLiteral("0 geändert oder neu, 0 entfernt"), &m_coreLog));
        QTest::qWait(300);
        QCOMPARE(listB.count(), 0);
        QCOMPARE(changedB.count(), 0);
    }

    // P10-vor, Teil A 3: Einstellungen über die Schnittstelle lesen und ändern. Der Kern hat
    // kern.ini beim Start im eigenen XDG_CONFIG_HOME angelegt und die Adresse eingetragen.
    void settingsReadAndChange()
    {
        const QString path = m_config.filePath(QFileInfo(coreSettingsPath()).dir().dirName()
                                               + QStringLiteral("/kern.ini"));
        QVERIFY2(QFile::exists(path), qPrintable(path));
        const QString detected = detectHomeAddress();
        QVERIFY(m_coreLog.contains(QStringLiteral("Einstellungen: %1 angelegt").arg(path)));
        if (!detected.isEmpty())
            QVERIFY(m_coreLog.contains(QStringLiteral("Einstellungen: Adresse im Heimnetz %1 eingetragen")
                                           .arg(detected)));

        RawConnection one(m_socket);
        RawConnection two(m_socket);
        one.hello(QStringLiteral("dss-app"));
        two.hello(QStringLiteral("dss-cli"));
        QVERIFY(!one.waitFor(QStringLiteral("overlay_list")).isEmpty());
        QVERIFY(!two.waitFor(QStringLiteral("overlay_list")).isEmpty());
        one.sendLine("{\"type\":\"get_settings\"}");
        SettingsInfo info;
        QString error;
        QVERIFY2(parseSettings(one.waitFor(QStringLiteral("settings")), &info, &error), qPrintable(error));
        QCOMPARE(info, (SettingsInfo{{defaultCoreSettings().port, detected}, 1024, 32767, path, QString()}));

        // Ungültige Werte: abgewiesen mit Meldung, Datei unverändert, keine Nachricht an andere.
        const QByteArray before = [&] {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        const QList<QPair<QByteArray, QString>> invalid = {
            {"{\"type\":\"update_settings\",\"request_id\":1,\"port\":80}", QStringLiteral("Port 80 ungültig")},
            {"{\"type\":\"update_settings\",\"request_id\":2,\"port\":40000}", QStringLiteral("Port 40000 ungültig")},
            {"{\"type\":\"update_settings\",\"request_id\":3,\"home_address\":\"192.168.1\"}",
             QStringLiteral("Adresse «192.168.1» ungültig")},
            {"{\"type\":\"update_settings\",\"request_id\":4,\"port\":9000,\"home_address\":\"127.0.0.1\"}",
             QStringLiteral("keine Adresse im Heimnetz")},
            {"{\"type\":\"update_settings\",\"request_id\":5}", QStringLiteral("ohne port und home_address")},
            // P10, Punkt 78: 192.0.2.1 (Beispielnetz nach RFC 5737) gehört nie zu diesem PC
            {"{\"type\":\"update_settings\",\"request_id\":7,\"port\":9000,\"home_address\":\"192.0.2.1\"}",
             QStringLiteral("192.0.2.1 gehört nicht zu diesem PC. Adressen dieses PCs: ")},
            // P10, Punkt 77: gültige Datei wird nicht zurückgesetzt
            {"{\"type\":\"reset_settings\",\"request_id\":8}", QStringLiteral("ist gültig; nichts zurückgesetzt")},
        };
        for (const auto &[line, text] : invalid) {
            one.sendLine(line);
            ChangeResult r;
            QVERIFY(parseChangeResult(one.waitFor(QStringLiteral("change_result")), &r, &error));
            QVERIFY2(!r.ok, line.constData());
            QVERIFY2(r.message.contains(text), qPrintable(r.message));
        }
        QCOMPARE([&] { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }(), before);
        QVERIFY(two.waitFor(QStringLiteral("settings"), 300).isEmpty());

        // Gültig: Antwort an den Absender, neue Werte an alle, Datei geschrieben. Leere Adresse (seit
        // P10 nimmt der Kern nur Adressen dieses PCs an, Punkt 78; die erkannte gilt schon).
        one.sendLine("{\"type\":\"update_settings\",\"request_id\":6,\"port\":9000,\"home_address\":\"\"}");
        ChangeResult r;
        QVERIFY(parseChangeResult(one.waitFor(QStringLiteral("change_result")), &r, &error));
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(r.requestId, 6);
        SettingsInfo broadcast;
        QVERIFY(parseSettings(two.waitFor(QStringLiteral("settings")), &broadcast, &error));
        QCOMPARE(broadcast.settings, (CoreSettings{9000, QString()}));
        // Der Absender bekommt die neuen Werte wie alle, vor change_result (wie bei Änderungen an
        // Overlays, docs/08 «Einstellungen»).
        QVERIFY2(one.allLines().contains("\"port\":9000,\"port_max\":32767"), one.allLines().constData());
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            CoreSettings stored;
            QVERIFY(parseCoreSettings(f.readAll(), defaultCoreSettings(), &stored, &error));
            QCOMPARE(stored, (CoreSettings{9000, QString()}));
        }
        QVERIFY(waitForLog(m_core.get(), QStringLiteral("Einstellungen geändert: Port 9000, Adresse im Heimnetz leer"),
                           &m_coreLog));

#ifdef DSS_CLI_BIN
        // dss-cli über dieselbe Verbindung
        CliResult cli = runCli(m_data.path(), {QStringLiteral("--verbindung"), m_socket, QStringLiteral("einstellungen")});
        QCOMPARE(cli.exitCode, 0);
        QVERIFY2(cli.out.contains(QStringLiteral("9000 (erlaubt 1024 bis 32767)")), qPrintable(cli.out));
        QVERIFY2(cli.out.contains(QRegularExpression(QStringLiteral("Adresse im Heimnetz +leer"))), qPrintable(cli.out));
        QVERIFY(cli.out.contains(QStringLiteral("Kern")));
        cli = runCli(m_data.path(), {QStringLiteral("--verbindung"), m_socket, QStringLiteral("einstellung-port"),
                                     QStringLiteral("1023")});
        QCOMPARE(cli.exitCode, 1);
        QVERIFY2(cli.err.contains(QStringLiteral("Port 1023 ungültig")), qPrintable(cli.err));
        cli = runCli(m_data.path(), {QStringLiteral("--verbindung"), m_socket, QStringLiteral("einstellung-port"),
                                     QStringLiteral("acht")});
        QCOMPARE(cli.exitCode, 1);
        cli = runCli(m_data.path(), {QStringLiteral("--verbindung"), m_socket, QStringLiteral("einstellung-port"),
                                     QString::number(defaultCoreSettings().port)});
        QCOMPARE(cli.exitCode, 0);
        QVERIFY(cli.out.startsWith(QStringLiteral("Port: %1\n").arg(defaultCoreSettings().port)));
        cli = runCli(m_data.path(), {QStringLiteral("--verbindung"), m_socket, QStringLiteral("einstellung-adresse"),
                                     detected});
        QCOMPARE(cli.exitCode, 0);
        QVERIFY(parseSettings(two.waitFor(QStringLiteral("settings")), &broadcast, &error));
        QVERIFY(parseSettings(two.waitFor(QStringLiteral("settings")), &broadcast, &error));
        QCOMPARE(broadcast.settings, (CoreSettings{defaultCoreSettings().port, detected}));
        // Ohne Kern: ändern geht nicht (Exit 4)
        cli = runCli(m_data.path(), {QStringLiteral("--verbindung"), m_run->filePath(QStringLiteral("keiner.sock")),
                                     QStringLiteral("einstellung-port"), QStringLiteral("9000")});
        QCOMPARE(cli.exitCode, 4);
        QVERIFY2(cli.err.contains(QStringLiteral("ändert nur der Kern")), qPrintable(cli.err));
#endif
    }

    // P10, Punkt 77: ungültige kern.ini über reset_settings zurücksetzen (eigener Kern mit eigener
    // kaputter Datei). Die alte Datei bleibt unter kern.ini.ungueltig-<Zeit> erhalten.
    void settingsResetInvalid()
    {
        QTemporaryDir data;
        QTemporaryDir config;
        std::unique_ptr<QTemporaryDir> run = runtimeDir();
        const QString socket = run->filePath(QStringLiteral("r.sock"));
        const QString dir = config.filePath(QFileInfo(coreSettingsPath()).dir().dirName());
        const QString path = dir + QStringLiteral("/kern.ini");
        QVERIFY(QDir().mkpath(dir));
        const QByteArray broken("[overlay-server]\nport=99999\n");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write(broken) == broken.size());
        }
        std::unique_ptr<QProcess> core = testhelp::startCore(data.path(), socket, config.path());
        QString log;
        QVERIFY2(waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        QVERIFY(log.contains(QStringLiteral("Port 99999 ungültig")));
        RawConnection one(socket);
        RawConnection two(socket);
        one.hello(QStringLiteral("dss-app"));
        two.hello(QStringLiteral("dss-app"));
        QVERIFY(!one.waitFor(QStringLiteral("overlay_list")).isEmpty());
        QVERIFY(!two.waitFor(QStringLiteral("overlay_list")).isEmpty());
        one.sendLine("{\"type\":\"get_settings\"}");
        SettingsInfo info;
        QString error;
        QVERIFY(parseSettings(one.waitFor(QStringLiteral("settings")), &info, &error));
        QVERIFY(!info.error.isEmpty());
        one.sendLine("{\"type\":\"reset_settings\",\"request_id\":1}");
        ChangeResult r;
        QVERIFY(parseChangeResult(one.waitFor(QStringLiteral("change_result")), &r, &error));
        QVERIFY2(r.ok, qPrintable(r.message));
        QVERIFY2(r.warning.startsWith(QStringLiteral("Auf Standardwerte zurückgesetzt. Die ungültige Datei heisst jetzt "
                                                     "kern.ini.ungueltig-")),
                 qPrintable(r.warning));
        const QString renamed = r.warning.section(QStringLiteral("heisst jetzt "), 1).chopped(1);
        QVERIFY(QRegularExpression(QStringLiteral("^kern\\.ini\\.ungueltig-\\d{4}-\\d\\d-\\d\\d_\\d\\d-\\d\\d-\\d\\d$"))
                    .match(renamed).hasMatch());
        {
            QFile old(dir + QLatin1Char('/') + renamed);
            QVERIFY(old.open(QIODevice::ReadOnly));
            QCOMPARE(old.readAll(), broken);
        }
        // Neue Werte an alle, ohne Meldung; die Datei hat die Standardwerte mit der erkannten Adresse.
        QVERIFY(parseSettings(two.waitFor(QStringLiteral("settings")), &info, &error));
        const CoreSettings expected{defaultCoreSettings().port, detectHomeAddress()};
        QCOMPARE(info.settings, expected);
        QVERIFY(info.error.isEmpty());
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::ReadOnly));
            CoreSettings stored;
            QVERIFY(parseCoreSettings(f.readAll(), defaultCoreSettings(), &stored, &error));
            QCOMPARE(stored, expected);
        }
        QVERIFY(waitForLog(core.get(), QStringLiteral("ungültige Datei umbenannt in"), &log));
        // Danach wieder änderbar
        one.sendLine("{\"type\":\"update_settings\",\"request_id\":2,\"port\":9002}");
        QVERIFY(parseChangeResult(one.waitFor(QStringLiteral("change_result")), &r, &error));
        QVERIFY2(r.ok, qPrintable(r.message));
        core->terminate();
        QVERIFY(core->waitForFinished(5000));
    }

    // P10: Adresse der Browserquelle mit Token nur an die Verbindung, die danach fragt; nach
    // neuem Token und neuem Port von selbst neu; nie im Protokoll.
    void browserSource()
    {
        const QString detected = detectHomeAddress();
        if (detected.isEmpty())
            QSKIP("Keine Adresse im Heimnetz auf diesem PC (Standardroute fehlt)");
        const QString address = storedOverlay(m_overviewId).address;
        const auto storedToken = [&] {
            OverlayStore store(m_data.path());
            QString error;
            store.load(&error, OverlayStore::LoadMode::ReadOnly);
            return store.token(m_overviewId);
        };
        RawConnection one(m_socket);
        RawConnection two(m_socket);
        one.hello(QStringLiteral("dss-app"));
        two.hello(QStringLiteral("dss-app"));
        QVERIFY(!one.waitFor(QStringLiteral("overlay_list")).isEmpty());
        QVERIFY(!two.waitFor(QStringLiteral("overlay_list")).isEmpty());
        one.sendLine(encodeMessage(getBrowserSourceMessage(m_overviewId)).chopped(1));
        BrowserSourceInfo info;
        QString error;
        QVERIFY2(parseBrowserSource(one.waitFor(QStringLiteral("browser_source")), &info, &error),
                 qPrintable(error));
        const QString oldToken = storedToken();
        QCOMPARE(info.url, browserSourceUrl(detected, defaultCoreSettings().port, address, oldToken));
        QVERIFY(info.problem.isEmpty());

        // Neuer Token: neue Adresse an one, nichts an two, Protokoll ohne Token
        ChangeRequest r;
        r.type = QString::fromLatin1(msg::kNewToken);
        r.requestId = 11;
        r.overlayId = m_overviewId;
        one.sendLine(encodeMessage(changeRequestMessage(r)).chopped(1));
        QVERIFY(parseBrowserSource(one.waitFor(QStringLiteral("browser_source")), &info, &error));
        ChangeResult result;
        QVERIFY(parseChangeResult(one.waitFor(QStringLiteral("change_result")), &result, &error));
        QVERIFY2(result.ok, qPrintable(result.message));
        const QString newToken = storedToken();
        QVERIFY(newToken != oldToken);
        QCOMPARE(info.url, browserSourceUrl(detected, defaultCoreSettings().port, address, newToken));
        QVERIFY(waitForLog(m_core.get(), QStringLiteral("Neuer Token für Overlay «Überblick Start» (/o/%1/…): "
                                                        "die alte Adresse gilt nicht mehr").arg(address),
                           &m_coreLog));
        QVERIFY(two.waitFor(QStringLiteral("browser_source"), 300).isEmpty());
        QVERIFY(!two.allLines().contains(newToken.toLatin1()));
        QVERIFY(!two.allLines().contains(oldToken.toLatin1()));

        // Neuer Port: neue Adresse von selbst
        one.sendLine("{\"type\":\"update_settings\",\"request_id\":12,\"port\":9001}");
        QVERIFY(parseBrowserSource(one.waitFor(QStringLiteral("browser_source")), &info, &error));
        QCOMPARE(info.url, browserSourceUrl(detected, 9001, address, newToken));
        one.sendLine(QStringLiteral("{\"type\":\"update_settings\",\"request_id\":13,\"port\":%1}")
                         .arg(defaultCoreSettings().port).toUtf8());
        QVERIFY(parseBrowserSource(one.waitFor(QStringLiteral("browser_source")), &info, &error));
        QCOMPARE(info.url, browserSourceUrl(detected, defaultCoreSettings().port, address, newToken));

        // Unbekanntes Overlay: problem; null: keine Adresse mehr
        one.sendLine(encodeMessage(getBrowserSourceMessage(QUuid::createUuid())).chopped(1));
        QVERIFY(parseBrowserSource(one.waitFor(QStringLiteral("browser_source")), &info, &error));
        QVERIFY(info.url.isEmpty());
        QCOMPARE(info.problem, QStringLiteral("Kein Overlay mit dieser Kennung."));
        one.sendLine(encodeMessage(getBrowserSourceMessage(QUuid())).chopped(1));
        r.requestId = 14;
        one.sendLine(encodeMessage(changeRequestMessage(r)).chopped(1));
        const qsizetype seen = one.allLines().size();
        QVERIFY(parseChangeResult(one.waitFor(QStringLiteral("change_result")), &result, &error));
        QVERIFY(result.ok);
        QVERIFY(one.waitFor(QStringLiteral("browser_source"), 300).isEmpty());
        QVERIFY(!one.allLines().mid(seen).contains("browser_source"));

        m_overviewToken = storedToken();
        QVERIFY(!m_coreLog.contains(oldToken) && !m_coreLog.contains(newToken)
                && !m_coreLog.contains(m_overviewToken));
    }

#if DSS_CORE_OVERLAY_SERVER
    // P10 (Teil A 2, Punkt 77): Zwei Kerne mit demselben Port. Der zweite meldet im Fenster
    // (server_status), wer den Port belegt, und lauscht von selbst, sobald er frei ist.
    void serverStatusPortBusy()
    {
        QTemporaryDir data1;
        QTemporaryDir data2;
        QTemporaryDir config1;
        QTemporaryDir config2;
        const int port = 24000 + int(QCoreApplication::applicationPid() % 2000) * 4 + 3;
        QVERIFY(writeCoreSettings(config1.path(), port));
        QVERIFY(writeCoreSettings(config2.path(), port));
        const QString socket1 = m_run->filePath(QStringLiteral("eins.sock"));
        const QString socket2 = m_run->filePath(QStringLiteral("zwei.sock"));
        auto core1 = testhelp::startCore(data1.path(), socket1, config1.path());
        QString log1;
        QVERIFY2(waitForLog(core1.get(), QStringLiteral("Overlay-Server lauscht auf 127.0.0.1:%1").arg(port), &log1),
                 qPrintable(log1));
        auto core2 = testhelp::startCore(data2.path(), socket2, config2.path());
        QString log2;
        QVERIFY2(waitForLog(core2.get(), QStringLiteral("ist belegt"), &log2), qPrintable(log2));

        CoreClient::Options o;
        o.socketPath = socket2;
        o.program = QStringLiteral("tst_kernverbindung");
        o.version = version();
        o.reconnectIntervalMs = 200;
        CoreClient c(o);
        c.start();
        QTRY_VERIFY_WITH_TIMEOUT(c.serverStatus().has_value(), 3000);
        QVERIFY(c.serverStatus()->enabled);
        const QString problems = c.serverStatus()->problems.join(QLatin1Char('\n'));
        QVERIFY2(problems.contains(QStringLiteral("Port %1 ist belegt, vermutlich von DoNs Streaming Studio in Arbeit (%2) (Prozess %3)")
                                       .arg(port).arg(QFileInfo(QStringLiteral(DSS_CORE_BIN)).canonicalFilePath())
                                       .arg(core1->processId())),
                 qPrintable(problems));
        QVERIFY(c.serverStatus()->listening.isEmpty() || !c.serverStatus()->listening.join(QLatin1Char(' ')).contains(QStringLiteral("127.0.0.1")));

        core1->terminate();
        QVERIFY(core1->waitForFinished(5000));
        QTRY_VERIFY_WITH_TIMEOUT(c.serverStatus()->listening.contains(QStringLiteral("127.0.0.1:%1").arg(port)), 6000);
        QVERIFY(!c.serverStatus()->problems.join(QLatin1Char(' ')).contains(QStringLiteral("belegt")));
        const QString detected = detectHomeAddress();
        if (!detected.isEmpty())
            QVERIFY(c.serverStatus()->listening.contains(QStringLiteral("%1:%2").arg(detected).arg(port)));
        c.stop();
        core2->terminate();
        QVERIFY(core2->waitForFinished(5000));
        QCOMPARE(core2->exitCode(), 0);
    }
#endif

    // Ungültige kern.ini: Kern startet mit Standardwerten, meldet den Fehler, schreibt die Datei
    // nicht und weist Änderungen ab.
    void invalidSettingsFile()
    {
        QTemporaryDir data;
        QTemporaryDir config;
        const QString dir = config.filePath(QFileInfo(coreSettingsPath()).dir().dirName());
        QVERIFY(QDir().mkpath(dir));
        const QString path = dir + QStringLiteral("/kern.ini");
        const QByteArray broken("[overlay-server]\nport=99999\n");
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(broken);
        }
        const QString socket = m_run->filePath(QStringLiteral("ungueltig.sock"));
        auto core = testhelp::startCore(data.path(), socket, config.path());
        QString log;
        QVERIFY2(waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("Port 99999 ungültig")), qPrintable(log));
        QVERIFY(!log.contains(QStringLiteral("eingetragen")));
        RawConnection c(socket);
        c.hello(QStringLiteral("dss-app"));
        c.sendLine("{\"type\":\"get_settings\"}");
        SettingsInfo info;
        QString error;
        QVERIFY(parseSettings(c.waitFor(QStringLiteral("settings")), &info, &error));
        QCOMPARE(info.settings, defaultCoreSettings());
        QVERIFY2(info.error.contains(QStringLiteral("Port 99999 ungültig")), qPrintable(info.error));
        c.sendLine("{\"type\":\"update_settings\",\"request_id\":1,\"port\":9000}");
        ChangeResult r;
        QVERIFY(parseChangeResult(c.waitFor(QStringLiteral("change_result")), &r, &error));
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains(QStringLiteral("von Hand reparieren")), qPrintable(r.message));
        core->terminate();
        QVERIFY(core->waitForFinished(5000));
        QCOMPARE(core->exitCode(), 0);
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), broken);
    }

    void quitRefusedWhileOtherWindowConnected()
    {
        RawConnection one(m_socket);
        RawConnection two(m_socket);
        one.hello(QStringLiteral("dss-app"));
        two.hello(QStringLiteral("dss-app"));
        QVERIFY(!one.waitFor(QStringLiteral("overlay_list")).isEmpty());
        QVERIFY(!two.waitFor(QStringLiteral("overlay_list")).isEmpty());
        one.sendLine("{\"type\":\"quit_request\"}");
        const QJsonObject r = one.waitFor(QStringLiteral("quit_reply"));
        QCOMPARE(r.value(QStringLiteral("accepted")).toBool(), false);
        QCOMPARE(r.value(QStringLiteral("other_windows")).toInt(), 1);
        QTest::qWait(300);
        QCOMPARE(m_core->state(), QProcess::Running);
    }

    void reconnectAfterKillAndRestart()
    {
        auto c = client();
        QSignalSpy connectedSpy(c.get(), &CoreClient::connectedChanged);
        c->start();
        QTRY_VERIFY_WITH_TIMEOUT(c->isConnected(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(c->overlays().size(), 2, 3000);

        m_core->kill(); // SIGKILL, wie ein Absturz
        QVERIFY(m_core->waitForFinished(5000));
        QTRY_VERIFY_WITH_TIMEOUT(!c->isConnected(), 3000);
        QVERIFY(c->overlays().isEmpty()); // ohne Verbindung keine Daten
        QVERIFY(!c->lastStats());
        QVERIFY(c->statusText().contains(QStringLiteral("Verbindung zum Kern verloren")));

        QVERIFY2(startCore(), qPrintable(m_coreLog)); // übrig gebliebener Socket und Sperre
        // kern.ini steht schon: nicht neu anlegen, Adresse nicht neu ermitteln (P10-vor)
        QVERIFY2(m_coreLog.contains(QStringLiteral("Einstellungen: Port ")), qPrintable(m_coreLog));
        QVERIFY(!m_coreLog.contains(QStringLiteral("angelegt")));
        QVERIFY(!m_coreLog.contains(QStringLiteral("eingetragen")));
        QTRY_VERIFY_WITH_TIMEOUT(c->isConnected(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(c->overlays().size(), 2, 3000);
        QCOMPARE(connectedSpy.count(), 3); // verbunden, getrennt, wieder verbunden
    }

    void quitAcceptedWhenLastWindow()
    {
        // Normales Schliessen des letzten Fensters: Kern beendet sich mit Exit-Code 0.
        auto observer = client(); // anderes Programm, zählt nicht als Fenster
        observer->start();
        auto window = client(QStringLiteral("dss-app"));
        window->start();
        QTRY_VERIFY_WITH_TIMEOUT(window->isConnected() && observer->isConnected(), 3000);
        const std::optional<QuitReply> reply = window->requestQuit(2000);
        QVERIFY(reply);
        QCOMPARE(reply->accepted, true);
        QCOMPARE(reply->otherWindows, 0);
        QVERIFY(m_core->waitForFinished(5000));
        QCOMPARE(m_core->exitStatus(), QProcess::NormalExit);
        QCOMPARE(m_core->exitCode(), 0);
        QVERIFY(waitForLog(m_core.get(), QStringLiteral("Beenden erbeten von dss-app"), &m_coreLog));
        QVERIFY(!QFile::exists(m_socket)); // Socket beim Beenden entfernt
        QTRY_VERIFY_WITH_TIMEOUT(!observer->isConnected(), 3000);
    }

    void missingConnectionIsNamed()
    {
        // Ohne Kern und ohne Einheit: Grund auf Deutsch statt Qts «Invalid name».
        CoreClient::Options o;
        o.socketPath = m_run->filePath(QStringLiteral("fehlt.sock"));
        o.program = QStringLiteral("tst_kernverbindung");
        o.version = version();
        o.reconnectIntervalMs = 200;
        CoreClient c(o);
        c.start();
        QTRY_COMPARE_WITH_TIMEOUT(c.statusText(),
                                  QStringLiteral("Kern nicht erreichbar (keine Verbindung unter diesem "
                                                 "Pfad), neuer Versuch alle 0.2 s"),
                                  3000);
        QCOMPARE(socketErrorText(QLocalSocket::ConnectionRefusedError),
                 QStringLiteral("Verbindung abgelehnt, Kern läuft nicht"));
    }

    void unitStartFailureIsShown()
    {
        // Lässt sich der Kern nicht starten (Einheit fehlt), steht der Grund im Status.
        CoreClient::Options o;
        o.socketPath = m_run->filePath(QStringLiteral("niemand.sock"));
        o.program = QStringLiteral("dss-app");
        o.version = version();
        o.unitName = QStringLiteral("dss-test-gibt-es-nicht.service");
        o.startUnit = true;
        o.reconnectIntervalMs = 200;
        CoreClient c(o);
        c.start();
        QTRY_VERIFY_WITH_TIMEOUT(c.statusText().contains(QStringLiteral("konnte nicht gestartet werden")), 10000);
        QVERIFY2(c.statusText().contains(QStringLiteral("dss-test-gibt-es-nicht.service")),
                 qPrintable(c.statusText()));
        QTest::qWait(500); // weitere Versuche überschreiben den Grund nicht
        QVERIFY(c.statusText().contains(QStringLiteral("konnte nicht gestartet werden")));
        QVERIFY(!c.isConnected());
    }
};

QTEST_GUILESS_MAIN(TestKernVerbindung)
#include "tst_kernverbindung.moc"
