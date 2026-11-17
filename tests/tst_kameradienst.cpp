// P15 d1: CameraServiceClient (dss_camera) gegen den echten Kamera-Dienst in der Attrappe
// (python3 -m dss_camera --attrappe --geraeteordner, eigener Prozess auf 127.0.0.1, freier Port,
// eigene XDG-Ordner im Testordner), dazu der Leser für Server-Sent Events ohne Netz.
//
// Quellen: docs/09-kamera-dienst.md; Start und Attrappe: kamera-dienst/LIESMICH.md,
// kamera-dienst/tests/test_api.py (Service, free_port, ini; seit d2 in kameradiensthilfe.h).

#include "cameraserviceclient.h"
#include "kameradiensthilfe.h"
#include "sseparser.h"

#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using dss::camera::CameraServiceClient;
using dss::camera::Config;
using dss::camera::Reply;
using dss::camera::SseEvent;
using dss::camera::SseParser;
using kameradienst::freePort;
using kameradienst::kC922;
using kameradienst::Service;

namespace {

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
    const QString devices = kameradienst::createFakeDevices(tmp.filePath(QStringLiteral("geraete")));
    QVERIFY2(devices.isEmpty(), qPrintable(devices));

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
