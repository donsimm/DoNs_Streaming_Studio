// Ton der Videos im Kern (P12b b4, Commit 3, Plan P12 Abschnitt «b4»): dss-core läuft als eigener
// Prozess mit dem Ersatzausgang ohne Gerät (--ersatzausgang, src/audio/nulloutput.h): kein
// PipeWire, kein Ton. Der Test bildet eine Seite in OBS nach (Ereignisse …/events/<page> und
// Meldungen …/clock, Format src/server/clockreport.h) und prüft am Protokoll des Kerns: Start,
// Nachführen, Glätten, Wiederholen, Stopp in allen Fällen (Seite, Ende, verborgen, Ausgabe aus,
// 3 s ohne Meldung, Ende der Ereignisse, geändert, gelöscht), Einstieg danach und «die erste Seite
// führt». Dazu Abbilden und Glätten der Uhr der Seite ohne Prozess (src/audio/pageclock.h).
// Daten wie im Handtest (Regel 7): tests/daten/ton/blitz-piep.mp4 und .webm.
//
// P14 t (Plan P14, 4.3): Zweck «aktionen» mit eigenem Ersatzausgang im Kern (audio_outputs,
// [ton-aktionen]) und Töne des Chat-Bots (dss::ActionSound) im Prozess mit dem Ersatzausgang:
// Ende nach der Länge, ohne Ausgang, falsche Lautstärke, fehlende Datei, 8 gleichzeitig und der
// 9. abgewiesen, Abbruch, Ausgang weg, Beenden. Daten: tests/daten/ton/ton-piep.wav (1 s).

#include "actionsound.h"
#include "browsersource.h"
#include "coresettings.h"
#include "mixer.h"
#include "nulloutput.h"
#include "overlaystore.h"
#include "pageclock.h"
#include "protocol.h"
#include "prozesshilfe.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <chrono>
#include <cmath>
#include <memory>

using namespace dss;
using namespace testhelp;

namespace {

const QString kTon = QStringLiteral(DSS_TEST_TON_DIR);
// Pause des Videos beim Wiederholen wie der Startwert des Mischers (Mixer::kLoopGapStartNs, M2)
constexpr double kGapMs = 58.6;
constexpr int kTickMs = 250; // Lage etwa viermal je Sekunde (die Seite darf jede Sekunde)

double steadyMs()
{
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count())
           / 1e6;
}

// Seite in OBS, nachgebildet: hält …/events/<page> offen und schickt Meldungen über eine Verbindung
// mit Keep-Alive (Reihenfolge bleibt). performance.now() der Seite = steady_clock dieses Tests.
class FakePage : public QObject
{
public:
    FakePage(quint16 port, const QByteArray &base, const QString &page, const QUuid &source, const QString &file)
        : m_port(port), m_base(base), m_page(page), m_source(source), m_file(file)
    {
        m_timer.setInterval(kTickMs);
        connect(&m_timer, &QTimer::timeout, this, [this] { tick(); });
        m_clock.connectToHost(QStringLiteral("127.0.0.1"), m_port);
        connect(&m_clock, &QTcpSocket::readyRead, this, [this] { m_answers += m_clock.readAll(); });
    }

    void openEvents()
    {
        m_events = std::make_unique<QTcpSocket>();
        m_events->connectToHost(QStringLiteral("127.0.0.1"), m_port);
        connect(m_events.get(), &QTcpSocket::readyRead, m_events.get(), [s = m_events.get()] { s->readAll(); });
        static_cast<void>(QTest::qWaitFor([this] { return m_events->state() == QAbstractSocket::ConnectedState; }, 3000));
        m_events->write("GET " + m_base + "events/" + m_page.toUtf8() + " HTTP/1.1\r\nHost: test\r\n"
                        "Accept: text/event-stream\r\n\r\n");
    }
    void closeEvents()
    {
        if (m_events)
            m_events->abort();
        m_events.reset();
    }

    // Video beginnt von vorne (aktiv): «start», danach Lage alle kTickMs.
    void start(int run, double durationS, bool loop)
    {
        m_run = run;
        m_durationS = durationS;
        m_loop = loop;
        m_lastLoop = 0;
        m_t0 = steadyMs();
        send("start", 0.0, m_t0, 0, m_t0);
        m_timer.start();
    }
    void stop() // inaktiv: «stop» ohne Quelle
    {
        m_timer.stop();
        send("stop", 0, 0, 0, steadyMs(), false);
    }
    void end() // Ende des Videos ohne Wiederholen
    {
        m_timer.stop();
        send("end", 0, 0, 0, steadyMs(), true, false);
    }
    void silence() { m_timer.stop(); } // keine Meldungen mehr (Netz weg)
    void resume() { m_timer.start(); } // Video lief weiter, Meldungen kommen wieder

    double mediaShiftMs = 0.0; // Lage der Seite verschoben (Nachführen)
    double jitterMs = 0.0;     // Streuung der gemeldeten Lage (gleichverteilt ±)
    double delayMaxMs = 0.0;   // Laufzeit im Netz (gleichverteilt 0 bis max), über now_ms nachgebildet
    double lastSentMs = 0.0;

    int accepted() const { return int(m_answers.count("HTTP/1.1 204")); }
    int rejected() const { return int(m_answers.count("HTTP/1.1 ")) - accepted(); }

private:
    void tick()
    {
        const double now = steadyMs();
        const double t = now - m_t0;
        int loop = 0;
        double media = t;
        if (m_loop && m_durationS > 0) {
            const double period = m_durationS * 1000.0 + kGapMs;
            loop = int(t / period);
            media = t - loop * period;
            if (media > m_durationS * 1000.0)
                return; // Pause beim Wiederholen: kein Bild
        }
        const char *event = loop != m_lastLoop ? "loop" : "frame";
        m_lastLoop = loop;
        QRandomGenerator *r = QRandomGenerator::global();
        const double jitter = jitterMs > 0 ? (r->generateDouble() * 2.0 - 1.0) * jitterMs : 0.0;
        const double delay = delayMaxMs > 0 ? r->generateDouble() * delayMaxMs : 0.0;
        send(event, (media + mediaShiftMs + jitter) / 1000.0, now, loop, now - delay);
    }

    void send(const char *event, double mediaS, double displayMs, int loop, double nowMs, bool withSource = true,
              bool withPosition = true)
    {
        QJsonObject m{{QStringLiteral("v"), 1},
                      {QStringLiteral("page"), m_page},
                      {QStringLiteral("seq"), ++m_seq},
                      {QStringLiteral("event"), QLatin1String(event)},
                      {QStringLiteral("run"), m_run},
                      {QStringLiteral("now_ms"), nowMs}};
        if (withSource)
            m.insert(QStringLiteral("source"), m_source.toString(QUuid::WithoutBraces));
        if (withSource && withPosition) {
            m.insert(QStringLiteral("file"), m_file);
            m.insert(QStringLiteral("loop"), loop);
            m.insert(QStringLiteral("media_s"), std::max(0.0, mediaS));
            m.insert(QStringLiteral("display_ms"), displayMs);
            m.insert(QStringLiteral("duration_s"), m_durationS);
        }
        const QByteArray body = QJsonDocument(m).toJson(QJsonDocument::Compact);
        m_clock.write("POST " + m_base + "clock HTTP/1.1\r\nHost: test\r\nContent-Type: text/plain\r\n"
                      "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        m_clock.flush();
        lastSentMs = steadyMs();
    }

    quint16 m_port;
    QByteArray m_base;
    QString m_page;
    QUuid m_source;
    QString m_file;
    QTcpSocket m_clock;
    std::unique_ptr<QTcpSocket> m_events;
    QTimer m_timer;
    QByteArray m_answers;
    qint64 m_seq = 0;
    int m_run = 0;
    double m_durationS = 0.0;
    bool m_loop = false;
    int m_lastLoop = 0;
    double m_t0 = 0.0;
};

// Zahlen einer Zeile «Ton: Stopp …» bzw. «Ton: Nachführen …»
int number(const QString &line, const QString &name)
{
    const QRegularExpression re(QRegularExpression::escape(name) + QStringLiteral(" (\\d+)"));
    const QRegularExpressionMatch m = re.match(line);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

double played(const QString &line)
{
    const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("gespielt ([0-9.]+) s")).match(line);
    return m.hasMatch() ? m.captured(1).toDouble() : -1.0;
}

} // namespace

class TestKernTon : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    std::unique_ptr<QTemporaryDir> m_run;
    QString m_socket;
    QString m_config;
    quint16 m_port = 0;
    std::unique_ptr<QProcess> m_core;
    QString m_log;
    qsizetype m_logPos = 0;
    QUuid m_overlayId;
    QUuid m_videoId;  // blitz-piep.mp4, wiederholt
    QUuid m_secondId; // blitz-piep.webm, ohne Wiederholen (wird gelöscht)
    QString m_videoFile;
    QString m_secondFile;
    QString m_videoLabel;
    QString m_secondLabel;
    QByteArray m_base;
    QLocalSocket m_window;
    LineSplitter m_lines;
    qint64 m_requestId = 0;

    // Wartet (mit Ereignisschleife: die Seiten melden weiter) auf eine ganze Zeile mit text nach der
    // zuletzt gefundenen; gibt sie in line zurück.
    bool expectLog(const QString &text, int timeoutMs, QString *line = nullptr)
    {
        qsizetype at = -1;
        const bool found = QTest::qWaitFor([&] {
            m_log += QString::fromUtf8(m_core->readAllStandardError());
            at = m_log.indexOf(text, m_logPos);
            return at >= 0 && m_log.indexOf(QLatin1Char('\n'), at) >= 0;
        }, timeoutMs);
        if (!found)
            return false;
        const qsizetype begin = m_log.lastIndexOf(QLatin1Char('\n'), at) + 1;
        const qsizetype end = m_log.indexOf(QLatin1Char('\n'), at);
        if (line)
            *line = m_log.mid(begin, end - begin);
        m_logPos = end;
        return true;
    }
    // text erscheint in ms nicht (nach der zuletzt gefundenen Zeile)
    bool noLog(const QString &text, int ms)
    {
        QTest::qWait(ms);
        m_log += QString::fromUtf8(m_core->readAllStandardError());
        return m_log.indexOf(text, m_logPos) < 0;
    }
    QString logTail() const { return m_log.mid(std::max<qsizetype>(0, m_logPos - 2000)); }

    // Änderung wie aus dem Fenster; wartet auf change_result mit ok
    bool change(ChangeRequest r)
    {
        r.requestId = ++m_requestId;
        m_window.write(encodeMessage(changeRequestMessage(r)));
        bool ok = false;
        const bool answered = QTest::qWaitFor([&] {
            m_lines.append(m_window.readAll());
            QByteArray line;
            while (m_lines.next(&line) == LineSplitter::Result::Line) {
                QJsonObject o;
                QString error;
                if (decodeMessage(line, &o, &error) && messageType(o) == QLatin1String(msg::kChangeResult)
                    && o.value(QStringLiteral("request_id")).toInteger() == r.requestId) {
                    ok = o.value(QStringLiteral("ok")).toBool();
                    return true;
                }
            }
            return false;
        }, 5000);
        return answered && ok;
    }
    bool setVisible(const QUuid &source, bool visible)
    {
        ChangeRequest r;
        r.type = QString::fromLatin1(msg::kUpdateSource);
        r.overlayId = m_overlayId;
        r.sourceId = source;
        r.changes.visible = visible;
        return change(r);
    }
    bool setLoop(const QUuid &source, bool loop)
    {
        ChangeRequest r;
        r.type = QString::fromLatin1(msg::kUpdateSource);
        r.overlayId = m_overlayId;
        r.sourceId = source;
        r.changes.loop = loop;
        return change(r);
    }
    bool setVolume(const QUuid &source, int volume, bool muted)
    {
        ChangeRequest r;
        r.type = QString::fromLatin1(msg::kUpdateSource);
        r.overlayId = m_overlayId;
        r.sourceId = source;
        r.changes.volume = volume;
        r.changes.muted = muted;
        return change(r);
    }
    bool setOutput(bool output)
    {
        ChangeRequest r;
        r.type = QString::fromLatin1(msg::kSetOverlayOutput);
        r.overlayId = m_overlayId;
        r.output = output;
        return change(r);
    }

    std::unique_ptr<FakePage> page(const char *id, bool second = false)
    {
        return std::make_unique<FakePage>(m_port, m_base, QLatin1String(id), second ? m_secondId : m_videoId,
                                          second ? m_secondFile : m_videoFile);
    }
    QString started(const char *page, int run) const
    {
        return QStringLiteral("Ton: Start %1 (Seite %2, Lauf %3)").arg(m_videoLabel, QLatin1String(page)).arg(run);
    }
    QString stopped(const QString &reason) const
    {
        return QStringLiteral("Ton: Stopp %1: %2 (").arg(m_videoLabel, reason);
    }
    // Einstieg mit Seite (Zeile enthält beides)
    bool expectJoin(const char *page, int timeoutMs)
    {
        QString line;
        if (!expectLog(QStringLiteral("Ton: Einstieg %1 bei").arg(m_videoLabel), timeoutMs, &line))
            return false;
        return line.contains(QStringLiteral("(Seite %1,").arg(QLatin1String(page)));
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
            m_overlayId = store.createOverlay(QStringLiteral("Ton Probe"), 1280, 720, &error);
            QVERIFY2(!m_overlayId.isNull(), qPrintable(error));
            m_videoId = store.addVideoSource(m_overlayId, kTon + QStringLiteral("/blitz-piep.mp4"), true, &error);
            m_secondId = store.addVideoSource(m_overlayId, kTon + QStringLiteral("/blitz-piep.webm"), false, &error);
            QVERIFY2(!m_videoId.isNull() && !m_secondId.isNull(), qPrintable(error));
            const Overlay *o = store.overlay(m_overlayId);
            for (const Source &s : o->sources) {
                if (s.id == m_videoId) {
                    m_videoFile = s.file;
                    m_videoLabel = QStringLiteral("«%1» in «Ton Probe»").arg(s.name);
                } else if (s.id == m_secondId) {
                    m_secondFile = s.file;
                    m_secondLabel = QStringLiteral("«%1» in «Ton Probe»").arg(s.name);
                }
            }
            m_base = browserSourcePath(o->address, store.token(m_overlayId)).toUtf8();
        }
        // Ausgang für den Ton der Videos: der Ersatzausgang
        m_config = testConfigHome();
        m_port = quint16(testPort(m_config));
        QFile ini(m_config + QStringLiteral("/dons-streaming-studio-entwicklung/kern.ini"));
        QVERIFY(ini.open(QIODevice::Append));
        ini.write("\n[ton-video]\nausgang=dss_ersatzausgang\nversatz_ms=0\n");
        // P14 t: Töne des Chat-Bots über einen zweiten Ersatzausgang
        ini.write("\n[ton-aktionen]\nausgang=dss_ersatzausgang\n");
        ini.close();

        m_core = startCore(m_data.path(), m_socket, m_config, {QStringLiteral("--ersatzausgang")});
        QVERIFY2(expectLog(QStringLiteral("Verbindung offen"), 5000), qPrintable(m_log));
        QVERIFY2(expectLog(QStringLiteral("Overlay-Server lauscht auf 127.0.0.1:%1").arg(m_port), 5000), qPrintable(m_log));
        QVERIFY2(QTest::qWaitFor([this] {
                     m_log += QString::fromUtf8(m_core->readAllStandardError());
                     return m_log.contains(QStringLiteral("Ton: Ersatzausgang: Zustand playing"))
                            && m_log.contains(QStringLiteral("Ton Chat-Bot: Ersatzausgang: Zustand playing"));
                 }, 5000),
                 qPrintable(m_log));
        m_logPos = m_log.size();
        m_window.connectToServer(m_socket);
        QVERIFY(m_window.waitForConnected(3000));
        m_window.write(encodeMessage(helloMessage({QStringLiteral("tst_kernton"), QStringLiteral("0"), kProtocolVersion})));
    }

    void cleanupTestCase()
    {
        if (!m_core)
            return;
        m_window.abort();
        m_core->terminate();
        QVERIFY(m_core->waitForFinished(10000));
        m_log += QString::fromUtf8(m_core->readAllStandardError());
        QCOMPARE(m_core->exitStatus(), QProcess::NormalExit);
        QCOMPARE(m_core->exitCode(), 0);
        QVERIFY2(m_log.contains(QStringLiteral("beendet, Exit-Code 0")), qPrintable(logTail()));
        // Zeilen zum Ton im Testprotokoll (Werte für den Bericht)
        for (const QString &line : m_log.split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral("Ton: ")))
                qInfo("%s", qPrintable(line));
        }
    }

    // Uhr der Seite abbilden: kleinster Abstand der letzten 10 s; glätten bis 4 ms, darüber sofort.
    void clockMapping()
    {
        PageClockMapper m;
        QVERIFY(!m.valid());
        m.add(1'000'000'000, 900.0); // Laufzeit 100 ms
        QCOMPARE(m.offsetNs(), 100'000'000);
        m.add(2'000'000'000, 1999.5); // Laufzeit 0,5 ms: neues Minimum
        QCOMPARE(m.offsetNs(), 500'000);
        m.add(3'000'000'000, 2990.0); // langsamer: Minimum bleibt
        QCOMPARE(m.offsetNs(), 500'000);
        QCOMPARE(m.toSteadyNs(5000.0), 5'000'500'000);
        // Nach 10 s fällt die alte Meldung heraus
        m.add(12'500'000'000, 12'498.0);
        QCOMPARE(m.offsetNs(), 2'000'000);

        PageClockSmoother s;
        QCOMPARE(s.apply({1'000'000'000, 5'000'000'000, 0}).mediaNs, 1'000'000'000); // erste Lage
        // 1 s später, 3 ms zu weit: nur ein Achtel zählt
        PageClock c = s.apply({2'003'000'000, 6'000'000'000, 0});
        QCOMPARE(c.mediaNs, 2'000'375'000);
        QCOMPARE(c.steadyNs, 6'000'000'000);
        // 10 ms: gilt sofort
        QCOMPARE(s.apply({3'010'375'000, 7'000'000'000, 0}).mediaNs, 3'010'375'000);
        // Neuer Durchgang: beginnt neu
        QCOMPARE(s.apply({1'000'000, 7'100'000'000, 1}).mediaNs, 1'000'000);
    }

    // Start, Nachführen (weich ab 2 ms, Sprung ab 20 ms) und Stopp durch die Seite.
    void startFollowStop()
    {
        auto p = page("seitea01");
        p->openEvents();
        QTest::qWait(300); // Vorrat des Decoders
        p->start(1, 60.0, true);
        QVERIFY2(expectLog(started("seitea01", 1), 3000), qPrintable(logTail()));
        QTest::qWait(1500);
        p->mediaShiftMs = 5.0; // Seite 5 ms weiter: weich nachführen
        QString line;
        QVERIFY2(expectLog(QStringLiteral("Ton: Nachführen %1: Sprünge 0, weich 1").arg(m_videoLabel), 3000, &line),
                 qPrintable(logTail()));
        p->mediaShiftMs = 45.0; // 40 ms mehr: Sprung
        QVERIFY2(expectLog(QStringLiteral("Ton: Nachführen %1: Sprünge 1,").arg(m_videoLabel), 3000, &line),
                 qPrintable(logTail()));
        p->stop();
        QVERIFY2(expectLog(stopped(QStringLiteral("Seite inaktiv")), 2000, &line), qPrintable(logTail()));
        QVERIFY2(played(line) >= 2.5, qPrintable(line));
        QCOMPARE(number(line, QStringLiteral("Sprünge")), 1);
        QVERIFY2(number(line, QStringLiteral("weich")) >= 1, qPrintable(line));
        QCOMPARE(p->rejected(), 0);
    }

    // Streuung der Lage (±3 ms) und der Laufzeit (0 bis 10 ms): kein Nachführen (Glätten).
    void smoothingAgainstJitter()
    {
        auto p = page("seiteb01");
        p->openEvents();
        QTest::qWait(300);
        p->jitterMs = 3.0;
        p->delayMaxMs = 10.0;
        p->start(1, 60.0, true);
        QVERIFY2(expectLog(started("seiteb01", 1), 3000), qPrintable(logTail()));
        const qsizetype from = m_logPos;
        QTest::qWait(4000);
        p->stop();
        QString line;
        QVERIFY2(expectLog(stopped(QStringLiteral("Seite inaktiv")), 2000, &line), qPrintable(logTail()));
        QVERIFY2(played(line) >= 3.5, qPrintable(line));
        QCOMPARE(number(line, QStringLiteral("Sprünge")), 0);
        QCOMPARE(number(line, QStringLiteral("weich")), 0);
        QVERIFY2(!m_log.mid(from, m_logPos - from).contains(QStringLiteral("Ton: Nachführen")), qPrintable(logTail()));
    }

    // Wiederholen nach der Länge, die die Seite meldet (1 s statt 60 s), mit der Pause aus M2.
    void loopsFollowPage()
    {
        auto p = page("seitec01");
        p->openEvents();
        QTest::qWait(300);
        p->start(1, 1.0, true);
        QVERIFY2(expectLog(started("seitec01", 1), 3000), qPrintable(logTail()));
        QTest::qWait(3400);
        p->stop();
        QString line;
        QVERIFY2(expectLog(stopped(QStringLiteral("Seite inaktiv")), 2000, &line), qPrintable(logTail()));
        const int loops = number(line, QStringLiteral("Wiederholungen"));
        QVERIFY2(loops >= 3 && loops <= 4, qPrintable(line));
        QCOMPARE(number(line, QStringLiteral("Sprünge")), 0);
        QCOMPARE(number(line, QStringLiteral("weich")), 0);
        // Gespielt: Ton nur innerhalb der Länge, ohne die Pausen
        QVERIFY2(played(line) >= 3.0 && played(line) <= 3.6, qPrintable(line));
    }

    // Lautstärke und Stumm (Format 5, P12b b5): gehen an den Mischer, der Ton läuft weiter.
    void volumeKeepsPlaying()
    {
        auto p = page("seiteg01");
        p->openEvents();
        QTest::qWait(300);
        p->start(1, 60.0, true);
        QVERIFY2(expectLog(started("seiteg01", 1), 3000), qPrintable(logTail()));
        const qsizetype from = m_logPos;
        QVERIFY(setVolume(m_videoId, 40, true));
        QVERIFY2(expectLog(QStringLiteral("Ton: Lautstärke %1: 40 %, stumm").arg(m_videoLabel), 2000),
                 qPrintable(logTail()));
        QVERIFY(setVolume(m_videoId, 100, false));
        QString line;
        QVERIFY2(expectLog(QStringLiteral("Ton: Lautstärke %1: 100 %").arg(m_videoLabel), 2000, &line),
                 qPrintable(logTail()));
        QVERIFY2(!line.contains(QStringLiteral("stumm")), qPrintable(line));
        QVERIFY2(!m_log.mid(from, m_logPos - from).contains(QStringLiteral("Ton: Stopp")), qPrintable(logTail()));
        p->stop();
        QVERIFY2(expectLog(stopped(QStringLiteral("Seite inaktiv")), 2000), qPrintable(logTail()));
    }

    // Stopp in allen Fällen; nach verborgen, Ausgabe aus, 3 s ohne Meldung und Ende der Ereignisse
    // steigt der Ton bei der nächsten Lage wieder ein.
    void stopRules()
    {
        auto p = page("seited01");
        p->openEvents();
        QTest::qWait(300);
        QString line;

        // Ende des Videos
        p->start(1, 60.0, true);
        QVERIFY2(expectLog(started("seited01", 1), 3000), qPrintable(logTail()));
        QTest::qWait(500);
        p->end();
        QVERIFY2(expectLog(stopped(QStringLiteral("Ende des Videos")), 2000), qPrintable(logTail()));

        // Quelle verborgen: Stopp, kein Einstieg, solange verborgen; danach Einstieg
        p->start(2, 60.0, true);
        QVERIFY2(expectLog(started("seited01", 2), 3000), qPrintable(logTail()));
        QVERIFY(setVisible(m_videoId, false));
        QVERIFY2(expectLog(stopped(QStringLiteral("Quelle verborgen")), 2000), qPrintable(logTail()));
        QVERIFY2(noLog(QStringLiteral("Ton: Einstieg"), 700), qPrintable(logTail()));
        QVERIFY(setVisible(m_videoId, true));
        QVERIFY2(expectJoin("seited01", 2000), qPrintable(logTail()));

        // Ausgabe des Overlays aus
        QVERIFY(setOutput(false));
        QVERIFY2(expectLog(stopped(QStringLiteral("Ausgabe aus")), 2000), qPrintable(logTail()));
        QVERIFY2(noLog(QStringLiteral("Ton: Einstieg"), 700), qPrintable(logTail()));
        QVERIFY(setOutput(true));
        QVERIFY2(expectJoin("seited01", 2000), qPrintable(logTail()));

        // 3 s ohne Meldung
        QTest::qWait(300);
        p->silence();
        QVERIFY2(expectLog(stopped(QStringLiteral("3 s ohne Meldung")), 5000), qPrintable(logTail()));
        const double silentMs = steadyMs() - p->lastSentMs;
        QVERIFY2(silentMs >= 2950 && silentMs <= 3700, qPrintable(QString::number(silentMs)));
        p->resume();
        QVERIFY2(expectJoin("seited01", 2000), qPrintable(logTail()));

        // Ende der Ereignisse der Seite (SSE): Stopp, nichts mehr annehmen, bis sie wieder verbunden ist
        p->closeEvents();
        QVERIFY2(expectLog(stopped(QStringLiteral("Seite getrennt")), 2000), qPrintable(logTail()));
        QVERIFY2(noLog(QStringLiteral("Ton: Einstieg"), 700), qPrintable(logTail()));
        p->openEvents();
        QVERIFY2(expectJoin("seited01", 2000), qPrintable(logTail()));

        // Quelle geändert (Wiederholen aus): Stimme neu, Einstieg mit der neuen Einstellung
        QVERIFY(setLoop(m_videoId, false));
        QVERIFY2(expectLog(stopped(QStringLiteral("Quelle geändert")), 2000), qPrintable(logTail()));
        QVERIFY2(expectJoin("seited01", 2000), qPrintable(logTail()));
        p->stop();
        QVERIFY2(expectLog(stopped(QStringLiteral("Seite inaktiv")), 2000), qPrintable(logTail()));
        QVERIFY(setLoop(m_videoId, true));

        // Quelle gelöscht (zweites Video)
        auto second = page("seitee01", true);
        second->openEvents();
        QTest::qWait(300);
        second->start(1, 60.0, false);
        QVERIFY2(expectLog(QStringLiteral("Ton: Start %1 (Seite seitee01, Lauf 1)").arg(m_secondLabel), 3000),
                 qPrintable(logTail()));
        ChangeRequest remove;
        remove.type = QString::fromLatin1(msg::kRemoveSource);
        remove.overlayId = m_overlayId;
        remove.sourceId = m_secondId;
        QVERIFY(change(remove));
        QVERIFY2(expectLog(QStringLiteral("Ton: Stopp %1: Quelle gelöscht (").arg(m_secondLabel), 2000), qPrintable(logTail()));
        // Meldungen für die gelöschte Quelle weist der Server ab
        QVERIFY(QTest::qWaitFor([&] { return second->rejected() > 0; }, 2000));
        second->silence();
        QCOMPARE(p->rejected(), 0);
    }

    // Zwei Seiten auf dasselbe Overlay: Die erste, die startet, führt; die zweite steigt erst ein,
    // wenn die erste stoppt.
    void firstPageLeads()
    {
        auto a = page("seitef01");
        auto b = page("seitef02");
        a->openEvents();
        b->openEvents();
        QTest::qWait(300);
        a->start(1, 60.0, true);
        QVERIFY2(expectLog(started("seitef01", 1), 3000), qPrintable(logTail()));
        b->start(1, 60.0, true);
        QVERIFY2(noLog(QStringLiteral("(Seite seitef02,"), 800), qPrintable(logTail()));
        a->stop();
        QVERIFY2(expectLog(stopped(QStringLiteral("Seite inaktiv")), 2000), qPrintable(logTail()));
        QVERIFY2(expectJoin("seitef02", 2000), qPrintable(logTail()));
        b->stop();
        QVERIFY2(expectLog(stopped(QStringLiteral("Seite inaktiv")), 2000), qPrintable(logTail()));
    }

    // P14 t: audio_outputs meldet beide Zwecke mit ihrem Zustand, ein Gerät (dieselbe Liste).
    void actionsRoute()
    {
        QLocalSocket window;
        window.connectToServer(m_socket);
        QVERIFY(window.waitForConnected(3000));
        window.write(encodeMessage(helloMessage({QStringLiteral("tst_kernton"), QStringLiteral("0"), kProtocolVersion})));
        LineSplitter lines;
        AudioOutputsInfo info;
        const bool got = QTest::qWaitFor([&] {
            lines.append(window.readAll());
            QByteArray line;
            while (lines.next(&line) == LineSplitter::Result::Line) {
                QJsonObject o;
                QString error;
                if (decodeMessage(line, &o, &error) && messageType(o) == QLatin1String(msg::kAudioOutputs))
                    return parseAudioOutputs(o, &info, &error);
            }
            return false;
        }, 3000);
        QVERIFY(got);
        QVERIFY(info.enabled);
        QCOMPARE(info.devices.size(), 1);
        QCOMPARE(info.devices.at(0).name, QStringLiteral("dss_ersatzausgang"));
        QCOMPARE(info.routes.keys(), (QStringList{QStringLiteral("aktionen"), QStringLiteral("video")}));
        QCOMPARE(info.routes.value(QStringLiteral("video")).state, QStringLiteral("playing"));
        QCOMPARE(info.routes.value(QStringLiteral("aktionen")).state, QStringLiteral("playing"));
        window.abort();
    }

    // P14 t: Töne des Chat-Bots im Prozess mit dem Ersatzausgang (gleicher Takt wie im Kern).
    void actionSound()
    {
        QTemporaryDir data;
        QVERIFY(data.isValid());
        QVERIFY(QFile::copy(kTon + QStringLiteral("/ton-piep.wav"), data.filePath(QStringLiteral("piep.wav"))));
        Mixer mixer(ActionSound::kMaxSounds);
        NullOutput output(&mixer);
        QString error;
        QVERIFY2(output.start(&error), qPrintable(error));
        ActionSound sound(data.path(), &mixer, &output);
        QSignalSpy finished(&sound, &ActionSound::finished);

        // Ohne Ausgang: Fehler mit Hinweis auf das Dock, keine Stelle belegt
        QCOMPARE(sound.play(QStringLiteral("piep.wav"), 100, &error), quint64(0));
        QVERIFY2(error.contains(QStringLiteral("Kein Ausgang für «Ton Chat-Bot»")), qPrintable(error));
        output.setTarget(QString::fromLatin1(NullOutput::kDeviceName));
        QTRY_COMPARE(output.state(), AudioOutput::State::Playing);
        QCOMPARE(sound.play(QStringLiteral("piep.wav"), 201, &error), quint64(0));
        QVERIFY2(error.contains(QStringLiteral("0 bis 200 %")), qPrintable(error));
        QCOMPARE(sound.play(QStringLiteral("fehlt.wav"), 100, &error), quint64(0));
        QVERIFY2(error.contains(QStringLiteral("fehlt.wav")), qPrintable(error));
        QCOMPARE(finished.count(), 0);

        // Ein Ton: endet nach seiner Länge (1 s) mit ok
        QElapsedTimer timer;
        timer.start();
        const quint64 one = sound.play(QStringLiteral("piep.wav"), 200, &error);
        QVERIFY2(one != 0, qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        const qint64 tookMs = timer.elapsed();
        QCOMPARE(finished.at(0).at(0).toULongLong(), one);
        QVERIFY2(finished.at(0).at(1).toBool(), qPrintable(finished.at(0).at(2).toString()));
        QVERIFY2(tookMs >= 950 && tookMs <= 2000, qPrintable(QString::number(tookMs)));

        // 8 gleichzeitig, der 9. abgewiesen; alle 8 enden mit ok
        finished.clear();
        QList<quint64> ids;
        for (int i = 0; i < ActionSound::kMaxSounds; ++i) {
            const quint64 id = sound.play(QStringLiteral("piep.wav"), 100, &error);
            QVERIFY2(id != 0, qPrintable(error));
            ids.append(id);
        }
        QCOMPARE(sound.play(QStringLiteral("piep.wav"), 100, &error), quint64(0));
        QVERIFY2(error.contains(QStringLiteral("Schon 8 Töne")), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), ActionSound::kMaxSounds, 5000);
        for (const QList<QVariant> &f : std::as_const(finished)) {
            QVERIFY(ids.contains(f.at(0).toULongLong()));
            QVERIFY2(f.at(1).toBool(), qPrintable(f.at(2).toString()));
        }

        // Abbruch
        finished.clear();
        const quint64 aborted = sound.play(QStringLiteral("piep.wav"), 100, &error);
        QVERIFY2(aborted != 0, qPrintable(error));
        QTest::qWait(100);
        sound.stop(aborted);
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.at(0).at(1).toBool());
        QCOMPARE(finished.at(0).at(2).toString(), QStringLiteral("abgebrochen"));

        // Ausgang weg: der Ton endet nach 5 s ohne Fortschritt mit Fehler
        finished.clear();
        const quint64 lost = sound.play(QStringLiteral("piep.wav"), 100, &error);
        QVERIFY2(lost != 0, qPrintable(error));
        QTest::qWait(100);
        output.setTarget(QString());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 8000);
        QVERIFY(!finished.at(0).at(1).toBool());
        QVERIFY2(finished.at(0).at(2).toString().contains(QStringLiteral("Kein Ausgang")),
                 qPrintable(finished.at(0).at(2).toString()));

        // Beenden: laufende Töne enden, danach nimmt es keinen mehr an
        output.setTarget(QString::fromLatin1(NullOutput::kDeviceName));
        QTRY_COMPARE(output.state(), AudioOutput::State::Playing);
        finished.clear();
        QVERIFY(sound.play(QStringLiteral("piep.wav"), 100, &error) != 0);
        sound.shutdown();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(2).toString(), QStringLiteral("Kern beendet"));
        QCOMPARE(sound.play(QStringLiteral("piep.wav"), 100, &error), quint64(0));
    }
};

QTEST_GUILESS_MAIN(TestKernTon)
#include "tst_kernton.moc"
