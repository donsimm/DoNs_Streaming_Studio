// Zusammenspiel Kern–Verbindung (P07, Tests): dss-core läuft als eigener Prozess mit eigenem
// Datenordner und eigener Verbindung. Eine Testverbindung (CoreClient wie im Fenster und eine
// rohe Verbindung für Fehlerfälle) meldet sich an, liest Liste und Overlay, empfängt
// Leistungswerte; der Kern wird neu geladen, beendet, nach kill -9 neu gestartet, und die
// Verbindung baut sich wieder auf. Nur Qt Core, Network und Test (kein Qt Gui).
// Daten wie im Handtest (CLAUDE.md Regel 7): Bild und Video aus prototypes/media, Schrift
// DejaVu Sans.

#include "coreclient.h"
#include "overlayjson.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#include "version.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSignalSpy>
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
        m_core = testhelp::startCore(m_data.path(), m_socket);
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
        QCOMPARE(c->coreHello().protocolVersion, 1);
        QTRY_COMPARE_WITH_TIMEOUT(c->overlays().size(), 2, 3000);
        // Sortiert nach Name wie im Kern: «Chat Ecke» vor «Überblick Start».
        QCOMPARE(c->overlays().at(0), summarizeOverlay(storedOverlay(m_chatId)));
        QCOMPARE(c->overlays().at(1), summarizeOverlay(storedOverlay(m_overviewId)));
        QCOMPARE(c->overlays().at(1).sourceCount, 3);
        QCOMPARE(c->overlays().at(0).width, 800);
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
