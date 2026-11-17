// P15 d1: CameraServiceClient (dss_camera) gegen den echten Kamera-Dienst in der Attrappe
// (python3 -m dss_camera --attrappe --geraeteordner, eigener Prozess auf 127.0.0.1, freier Port,
// eigene XDG-Ordner im Testordner), dazu der Leser für Server-Sent Events ohne Netz.
//
// Quellen: docs/09-kamera-dienst.md; Start und Attrappe: kamera-dienst/LIESMICH.md,
// kamera-dienst/tests/test_api.py (Service, free_port, ini).

#include "cameraserviceclient.h"
#include "sseparser.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using dss::camera::CameraServiceClient;
using dss::camera::Config;
using dss::camera::Reply;
using dss::camera::SseEvent;
using dss::camera::SseParser;

namespace {

constexpr int kStartTimeoutMs = 20000;
// Kennung der C922 in der Attrappe (kamera-dienst/dss_camera/fake.py, C922_ID).
const QString kC922 = QStringLiteral("bbbb:0002:SERIE0001");

// Im Bereich, den dienst.ini erlaubt (1024 bis 32767), wie free_port() in test_api.py.
int freePort()
{
    for (int i = 0; i < 200; ++i) {
        const int port = QRandomGenerator::global()->bounded(20000, 32000);
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, static_cast<quint16>(port)))
            return port;
    }
    return 0;
}

// Ein Dienst-Prozess mit eigenem Testordner (wie Service in test_api.py).
class Service
{
public:
    Service(const QString &folder, int port)
        : m_folder(folder)
    {
        QDir().mkpath(folder + QStringLiteral("/laufzeit"));
        QFile ini(folder + QStringLiteral("/dienst.ini"));
        if (ini.open(QIODevice::WriteOnly))
            ini.write(QStringLiteral("[dienst]\nport=%1\nlauschadressen=127.0.0.1\nerlaubte-adressen=127.0.0.1\n")
                          .arg(port)
                          .toUtf8());
    }

    ~Service() { stop(); }

    // Wartet auf «bereit» im Protokoll.
    bool start()
    {
        m_process = std::make_unique<QProcess>();
        m_process->setWorkingDirectory(QStringLiteral(DSS_CAMERA_SERVICE_DIR));
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("XDG_RUNTIME_DIR"), m_folder + QStringLiteral("/laufzeit"));
        env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_folder + QStringLiteral("/config"));
        env.insert(QStringLiteral("XDG_DATA_HOME"), m_folder + QStringLiteral("/daten"));
        env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
        m_process->setProcessEnvironment(env);
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        m_process->setStandardOutputFile(QProcess::nullDevice());
        m_process->setReadChannel(QProcess::StandardError); // für waitForReadyRead
        m_process->start(QStringLiteral(DSS_PYTHON3),
                         {QStringLiteral("-m"), QStringLiteral("dss_camera"), QStringLiteral("--konfig"),
                          m_folder + QStringLiteral("/dienst.ini"), QStringLiteral("--attrappe"),
                          QStringLiteral("--geraeteordner"), m_folder + QStringLiteral("/geraete")});
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < kStartTimeoutMs) {
            if (m_process->state() == QProcess::NotRunning)
                break;
            m_process->waitForReadyRead(200);
            m_stderr += QString::fromUtf8(m_process->readAllStandardError());
            if (m_stderr.contains(QStringLiteral("bereit")))
                return true;
        }
        return false;
    }

    // Beendet mit SIGTERM; gibt den Exit-Code zurück (-1: lief nicht oder hing).
    int stop()
    {
        if (!m_process)
            return -1;
        if (m_process->state() != QProcess::NotRunning) {
            m_process->terminate();
            if (!m_process->waitForFinished(kStartTimeoutMs)) {
                m_process->kill();
                m_process->waitForFinished();
                m_process.reset();
                return -1;
            }
        }
        m_stderr += QString::fromUtf8(m_process->readAllStandardError());
        const int code = m_process->exitStatus() == QProcess::NormalExit ? m_process->exitCode() : -1;
        m_process.reset();
        return code;
    }

    QString log() const { return m_stderr; }

private:
    QString m_folder;
    std::unique_ptr<QProcess> m_process;
    QString m_stderr;
};

// Antwort von request() abwarten.
Reply call(CameraServiceClient &client, const QByteArray &method, const QString &path,
           const QJsonValue &body = QJsonValue::Undefined)
{
    Reply result;
    bool done = false;
    client.request(method, path, body, [&](const Reply &reply) {
        result = reply;
        done = true;
    });
    if (!QTest::qWaitFor([&] { return done; }, 20000))
        result.error = QStringLiteral("Test: keine Antwort von request()");
    return result;
}

QStringList models(const QJsonObject &status)
{
    QStringList list;
    for (const QJsonValue &camera : status.value(QStringLiteral("cameras")).toArray())
        list.append(camera.toObject().value(QStringLiteral("model")).toString());
    list.sort();
    return list;
}

} // namespace

class TestCameraService : public QObject
{
    Q_OBJECT

private slots:
    void sseParser();
    void sseParserLimit();
    void validPath();
    void offWithoutService();
    void againstFakeService();
};

void TestCameraService::sseParser()
{
    SseParser parser;
    // BOM, Kommentar, retry, Ereignis in Stücken, CRLF und CR, mehrere data-Zeilen.
    QList<SseEvent> events = parser.feed(QByteArray("\xEF\xBB"));
    QVERIFY(events.isEmpty());
    events = parser.feed(QByteArray("\xBFretry: 2000\n\n: \n\nid: 3\nevent: lig"));
    QVERIFY(events.isEmpty());
    events = parser.feed("hts\r\ndata: {\"lights\":[]}\r\n\r");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0), (SseEvent{QStringLiteral("3"), QStringLiteral("lights"), "{\"lights\":[]}"}));
    // LF direkt nach dem CR des letzten Stücks gehört zur selben Zeile: keine zweite Leerzeile.
    events = parser.feed("\ndata:a\rdata:  b\r\r");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0), (SseEvent{QStringLiteral("3"), QStringLiteral("message"), "a\n b"}));
    // Ohne data kein Ereignis; Feld ohne Doppelpunkt; unbekanntes Feld.
    events = parser.feed("event: status\nfoo: 1\n\ndata\n\n");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0), (SseEvent{QStringLiteral("3"), QStringLiteral("message"), QByteArray()}));
    // Unvollständiges Ereignis am Ende des Stroms fällt weg; reset() vergisst alles.
    QVERIFY(parser.feed("event: status\ndata: x\n").isEmpty());
    parser.reset();
    events = parser.feed("data: y\n\n");
    QCOMPARE(events.size(), 1);
    QCOMPARE(events.at(0), (SseEvent{QString(), QStringLiteral("message"), "y"}));
}

void TestCameraService::sseParserLimit()
{
    SseParser parser;
    bool ok = true;
    parser.feed(QByteArray(SseParser::kMaxBytes + 1, 'x'), &ok);
    QVERIFY(!ok);
    QVERIFY(parser.feed("data: y\n\n", &ok).isEmpty());
    QVERIFY(!ok);
    parser.reset();
    QCOMPARE(parser.feed("data: y\n\n", &ok).size(), 1);
    QVERIFY(ok);
}

void TestCameraService::validPath()
{
    QVERIFY(CameraServiceClient::isValidPath(QStringLiteral("status")));
    QVERIFY(CameraServiceClient::isValidPath(QStringLiteral("camera-presets/totale/recall")));
    QVERIFY(CameraServiceClient::isValidPath(QStringLiteral("cameras/bbbb%3A0002%3ASERIE0001/controls")));
    QVERIFY(CameraServiceClient::isValidPath(QStringLiteral("cameras/bbbb:0002:SERIE0001/controls")));
    for (const char *path : {"", "/status", "status/", "../status", "a/../status", "a//b", "./status",
                             "status?x=1", "status#x", "a b", "a\\b", "a\nb"})
        QVERIFY2(!CameraServiceClient::isValidPath(QString::fromUtf8(path)), path);
}

void TestCameraService::offWithoutService()
{
    CameraServiceClient client;
    QCOMPARE(client.state(), CameraServiceClient::State::Off);
    QVERIFY(!client.message().isEmpty());
    // Off: Antwort ohne Netz, aber erst nach der Rückkehr von request().
    bool returned = false;
    Reply result;
    bool done = false;
    client.request("GET", QStringLiteral("status"), QJsonValue(), [&](const Reply &reply) {
        QVERIFY(returned);
        result = reply;
        done = true;
    });
    returned = true;
    QVERIFY(!done);
    QTRY_VERIFY(done);
    QCOMPARE(result.status, 0);
    QVERIFY(!result.error.isEmpty());

    // Kein Dienst am Port: Connecting nur beim ersten Versuch, danach Error.
    const int port = freePort();
    QVERIFY(port > 0);
    QSignalSpy states(&client, &CameraServiceClient::stateChanged);
    client.configure(Config{true, QStringLiteral("127.0.0.1"), port});
    QCOMPARE(client.state(), CameraServiceClient::State::Connecting);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), CameraServiceClient::State::Error, 10000);
    QVERIFY(client.message().contains(QStringLiteral("nicht erreichbar")));
    QTest::qWait(2500); // ein neuer Versuch
    QCOMPARE(client.state(), CameraServiceClient::State::Error);
    for (const QList<QVariant> &args : std::as_const(states))
        QVERIFY(args.at(0).value<CameraServiceClient::State>() != CameraServiceClient::State::Connected);
    QCOMPARE(states.size(), 2); // Connecting, Error (Meldung bleibt gleich)

    // Ungültiger Pfad: keine Anfrage.
    result = call(client, "GET", QStringLiteral("../status"));
    QCOMPARE(result.status, 0);
    QVERIFY(result.error.contains(QStringLiteral("Pfad")));

    client.configure(Config{});
    QCOMPARE(client.state(), CameraServiceClient::State::Off);
}

void TestCameraService::againstFakeService()
{
    QTemporaryDir tmp(QDir::tempPath() + QStringLiteral("/tst_kameradienst-XXXXXX"));
    QVERIFY(tmp.isValid());
    // Geräteordner der Attrappe mit Tiny 2 und C922 (FakeDeviceFolder.add_standard_cameras).
    QProcess folder;
    folder.setWorkingDirectory(QStringLiteral(DSS_CAMERA_SERVICE_DIR));
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
    folder.setProcessEnvironment(env);
    folder.start(QStringLiteral(DSS_PYTHON3),
                 {QStringLiteral("-c"),
                  QStringLiteral("import sys; from pathlib import Path; from dss_camera.fake import FakeDeviceFolder; "
                                 "FakeDeviceFolder(Path(sys.argv[1])).add_standard_cameras()"),
                  tmp.filePath(QStringLiteral("geraete"))});
    QVERIFY(folder.waitForFinished(kStartTimeoutMs));
    QVERIFY2(folder.exitStatus() == QProcess::NormalExit && folder.exitCode() == 0,
             folder.readAllStandardError().constData());

    const int port = freePort();
    QVERIFY(port > 0);
    Service service(tmp.path(), port);
    QVERIFY2(service.start(), qPrintable(service.log()));

    CameraServiceClient client;
    QSignalSpy events(&client, &CameraServiceClient::eventReceived);
    client.configure(Config{true, QStringLiteral("127.0.0.1"), port});
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), CameraServiceClient::State::Connected, 10000);
    QVERIFY(client.message().isEmpty());

    // Status lesen.
    QJsonObject status = client.status();
    QCOMPARE(status.value(QStringLiteral("api_version")).toInt(), 1);
    QCOMPARE(status.value(QStringLiteral("fake")).toBool(), true);
    // Modelle aus dem Geräteordner (product im USB-Ordner der Attrappe).
    QCOMPARE(models(status), (QStringList{QStringLiteral("C922 Pro Stream Webcam"), QStringLiteral("OBSBOT Tiny 2")}));
    QCOMPARE(status.value(QStringLiteral("camera_presets")).toArray(), QJsonArray());

    // Beim Anmelden schickt der Dienst status, presets und lights.
    const auto names = [&] {
        QStringList list;
        for (const QList<QVariant> &args : std::as_const(events))
            list.append(args.at(0).toString());
        return list;
    };
    QTRY_VERIFY_WITH_TIMEOUT(names().contains(QStringLiteral("lights")), 5000);
    QVERIFY(names().contains(QStringLiteral("status")));
    QVERIFY(names().contains(QStringLiteral("presets")));

    // Anfrage: Preset anlegen (die erste Anfrage wartet 3 s nach dem Erscheinen der Kamera) und
    // abrufen; Ereignisse presets und controls kommen an, status() führt die Liste nach.
    events.clear();
    Reply reply = call(client, "POST", QStringLiteral("camera-presets"),
                       QJsonObject{{QStringLiteral("name"), QStringLiteral("Totale")}, {QStringLiteral("camera"), kC922}});
    QVERIFY2(reply.status == 200, qPrintable(reply.error + QString::number(reply.status)));
    QCOMPARE(reply.body.value(QStringLiteral("preset")).toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("totale"));
    const QJsonArray presets{QJsonObject{{QStringLiteral("id"), QStringLiteral("totale")},
                                         {QStringLiteral("name"), QStringLiteral("Totale")}}};
    QTRY_COMPARE_WITH_TIMEOUT(client.status().value(QStringLiteral("camera_presets")).toArray(), presets, 5000);
    QVERIFY(names().contains(QStringLiteral("presets")));

    reply = call(client, "POST", QStringLiteral("camera-presets/totale/recall"));
    QVERIFY2(reply.status == 200, qPrintable(reply.error + QString::number(reply.status)));
    QCOMPARE(reply.body.value(QStringLiteral("ok")).toBool(), true);
    QTRY_VERIFY_WITH_TIMEOUT(names().contains(QStringLiteral("controls")), 5000);
    for (const QList<QVariant> &args : std::as_const(events)) {
        if (args.at(0).toString() == QLatin1String("controls"))
            QCOMPARE(args.at(1).toJsonObject().value(QStringLiteral("camera")).toString(), kC922);
    }

    // Fehler des Dienstes kommen mit Status und Code.
    reply = call(client, "GET", QStringLiteral("camera-presets/gibt-es-nicht"));
    QCOMPARE(reply.status, 404);
    QCOMPARE(reply.body.value(QStringLiteral("code")).toString(), QStringLiteral("not_found"));
    QVERIFY(reply.error.isEmpty());

    // Dienst beenden: Error, der letzte Stand bleibt; Anfragen scheitern ohne Status.
    QCOMPARE(service.stop(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), CameraServiceClient::State::Error, 10000);
    QVERIFY(!client.message().isEmpty());
    QCOMPARE(client.status().value(QStringLiteral("camera_presets")).toArray(), presets);
    reply = call(client, "GET", QStringLiteral("status"));
    QCOMPARE(reply.status, 0);
    QVERIFY(!reply.error.isEmpty());

    // Neu starten: Der Client verbindet von selbst wieder (nach 2 s), das Preset ist noch da.
    QVERIFY2(service.start(), qPrintable(service.log()));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), CameraServiceClient::State::Connected, 10000);
    QCOMPARE(client.status().value(QStringLiteral("camera_presets")).toArray(), presets);
    reply = call(client, "GET", QStringLiteral("camera-presets/totale"));
    QCOMPARE(reply.status, 200);

    // Abschalten: Off, Stand leer.
    client.configure(Config{});
    QCOMPARE(client.state(), CameraServiceClient::State::Off);
    QVERIFY(client.status().isEmpty());
    QCOMPARE(service.stop(), 0);
}

QTEST_GUILESS_MAIN(TestCameraService)
#include "tst_kameradienst.moc"
