// P15 d1: CameraServiceClient (dss_camera) gegen den echten Kamera-Dienst in der Attrappe
// (python3 -m dss_camera --attrappe --geraeteordner, eigener Prozess auf 127.0.0.1, freier Port,
// eigene XDG-Ordner im Testordner), dazu der Leser für Server-Sent Events ohne Netz.
// P15 f2a (Punkt 129): Reihenfolge je Ziel (CallQueue) ohne Netz und gegen den Dienst mit einer
// langsamen Attrappen-Lampe.
//
// Quellen: docs/09-kamera-dienst.md; Start und Attrappe: kamera-dienst/LIESMICH.md,
// kamera-dienst/tests/test_api.py (Service, free_port, ini; seit d2 in kameradiensthilfe.h).

#include "cameracallqueue.h"
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
    void callQueue();
    void lightOrder();
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

void TestCameraService::callQueue()
{
    using dss::camera::CallQueue;
    using dss::camera::CallTarget;
    using dss::camera::callTarget;
    using Area = CallTarget::Area;
    const auto target = [](const char *method, const char *path) {
        return callTarget(QByteArray(method), QString::fromLatin1(path));
    };
    // Ziele
    QCOMPARE(target("GET", "status"), CallTarget{});
    QCOMPARE(target("GET", "cameras/x/controls"), CallTarget{});
    QCOMPARE(target("PUT", "cameras/x/controls"), (CallTarget{Area::Camera, QStringLiteral("x")}));
    QCOMPARE(target("POST", "cameras/x/step"), (CallTarget{Area::Camera, QStringLiteral("x")}));
    QCOMPARE(target("POST", "cameras/x/move"), CallTarget{});
    QCOMPARE(target("POST", "cameras/x/stop"), CallTarget{});
    QCOMPARE(target("POST", "camera-presets/p/recall"), (CallTarget{Area::Camera, QString()}));
    QCOMPARE(target("PUT", "lights/links"), (CallTarget{Area::Light, QStringLiteral("links")}));
    QCOMPARE(target("PUT", "lights/links/config"), (CallTarget{Area::Light, QStringLiteral("links")}));
    QCOMPARE(target("DELETE", "lights/links"), (CallTarget{Area::Light, QStringLiteral("links")}));
    QCOMPARE(target("PUT", "lights/all"), (CallTarget{Area::Light, QString()}));
    QCOMPARE(target("POST", "lights"), (CallTarget{Area::Light, QString()}));
    QCOMPARE(target("POST", "lights/check"), CallTarget{});
    QCOMPARE(target("POST", "light-presets/stream/recall"), (CallTarget{Area::Light, QString()}));

    const auto ids = [](const QList<CallQueue::Call> &calls) {
        QList<quint64> list;
        for (const CallQueue::Call &c : calls)
            list.append(c.id);
        return list;
    };
    const auto value = [](const char *key, const QJsonValue &v) { return QJsonObject{{QString::fromLatin1(key), v}}; };

    // Eine Lampe nacheinander, wartende gleiche PUT zusammengefasst; frei und andere Lampe sofort
    CallQueue q;
    const quint64 a1 = q.add("PUT", QStringLiteral("lights/a"), value("brightness", 50));
    QCOMPARE(ids(q.takeReady()), QList<quint64>{a1});
    const quint64 a2 = q.add("PUT", QStringLiteral("lights/a"), value("brightness", 60));
    QCOMPARE(q.add("PUT", QStringLiteral("lights/a"), value("brightness", 70)), a2);
    QVERIFY(q.takeReady().isEmpty());
    const quint64 get = q.add("GET", QStringLiteral("status"), QJsonValue());
    const quint64 b1 = q.add("PUT", QStringLiteral("lights/b"), value("brightness", 10));
    QCOMPARE(ids(q.takeReady()), (QList<quint64>{get, b1}));
    q.finish(get);
    // «Licht» wartet auf jede Lampe; danach keine Zusammenfassung über «Licht» hinweg
    const quint64 all = q.add("PUT", QStringLiteral("lights/all"), value("brightness", 30));
    const quint64 b2 = q.add("PUT", QStringLiteral("lights/b"), value("brightness", 20));
    QVERIFY(b2 != b1);
    const quint64 a3 = q.add("PUT", QStringLiteral("lights/a"), value("brightness", 80));
    QVERIFY(a3 != a2);
    QVERIFY(q.takeReady().isEmpty());
    q.finish(a1);
    const QList<CallQueue::Call> next = q.takeReady();
    QCOMPARE(ids(next), QList<quint64>{a2});
    QCOMPARE(next.at(0).body, QJsonValue(value("brightness", 70)));
    q.finish(a2);
    QVERIFY(q.takeReady().isEmpty()); // b1 läuft noch
    q.finish(b1);
    QCOMPARE(ids(q.takeReady()), QList<quint64>{all});
    q.finish(all);
    QCOMPARE(ids(q.takeReady()), (QList<quint64>{b2, a3}));
    q.finish(b2);
    q.finish(a3);
    QCOMPARE(q.waitingCount(), qsizetype(0));
    QCOMPARE(q.runningCount(), qsizetype(0));

    // Nie zusammengefasst: andere Felder, toggle, POST; verschachtelt nur bei gleichen Feldern
    const quint64 busy = q.add("PUT", QStringLiteral("lights/a"), value("on", true));
    q.takeReady();
    const quint64 t1 = q.add("PUT", QStringLiteral("lights/a"), value("on", QStringLiteral("toggle")));
    QVERIFY(q.add("PUT", QStringLiteral("lights/a"), value("on", QStringLiteral("toggle"))) != t1);
    const quint64 f1 = q.add("PUT", QStringLiteral("lights/a"), value("brightness", 40));
    QVERIFY(q.add("PUT", QStringLiteral("lights/a"), value("temperature", 300)) != f1);
    const quint64 c1 = q.add("PUT", QStringLiteral("cameras/x/config"), value("motion", value("speed", 5)));
    q.takeReady();
    const quint64 c2 = q.add("PUT", QStringLiteral("cameras/x/config"), value("motion", value("speed", 6)));
    QCOMPARE(q.add("PUT", QStringLiteral("cameras/x/config"), value("motion", value("speed", 7))), c2);
    QVERIFY(q.add("PUT", QStringLiteral("cameras/x/config"), value("motion", value("auto_wake", true))) != c2);
    const quint64 s1 = q.add("POST", QStringLiteral("cameras/x/step"), value("pan", 1));
    QVERIFY(q.add("POST", QStringLiteral("cameras/x/step"), value("pan", 1)) != s1);
    // move und stop warten nie, auch nicht hinter einer laufenden Anfrage an dieselbe Kamera
    const quint64 move = q.add("POST", QStringLiteral("cameras/x/move"), value("pan", 1));
    const quint64 stop = q.add("POST", QStringLiteral("cameras/x/stop"), QJsonValue());
    QCOMPARE(ids(q.takeReady()), (QList<quint64>{move, stop}));
    // Kamera-Presets warten auf jede Kamera und umgekehrt
    q.finish(busy);
    q.finish(c1);
    const quint64 recall = q.add("POST", QStringLiteral("camera-presets/p/recall"), QJsonValue());
    const QList<quint64> started = ids(q.takeReady());
    QVERIFY(started.contains(t1));
    QVERIFY(started.contains(c2));
    QVERIFY(!started.contains(recall));
}

void TestCameraService::lightOrder()
{
    // Punkt 129: Werte für eine Lampe, die langsam antwortet (0,3 s je Anfrage an die Lampe, Setzen
    // mit Zurücklesen also rund 0,6 s), ohne zu warten nacheinander geschickt.
    QTemporaryDir tmp(QDir::tempPath() + QStringLiteral("/tst_kameradienst-XXXXXX"));
    QVERIFY(tmp.isValid());
    const QString devices = kameradienst::createFakeDevices(tmp.filePath(QStringLiteral("geraete")));
    QVERIFY2(devices.isEmpty(), qPrintable(devices));
    const int port = freePort();
    QVERIFY(port > 0);
    Service service(tmp.path(), port);
    QVERIFY2(service.start(), qPrintable(service.log()));
    kameradienst::FakeLights lamps;
    QVERIFY(lamps.start(2, 0.3));

    CameraServiceClient client;
    client.configure(Config{true, QStringLiteral("127.0.0.1"), port});
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), CameraServiceClient::State::Connected, 10000);
    for (const auto &[name, host] : {std::pair{QStringLiteral("Links"), lamps.hosts().at(0)},
                                     std::pair{QStringLiteral("Rechts"), lamps.hosts().at(1)}}) {
        const Reply reply = call(client, "POST", QStringLiteral("lights"),
                                 QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("host"), host}});
        QVERIFY2(reply.status == 200, qPrintable(reply.error + QString::number(reply.status)));
    }

    const auto value = [](const char *key, int v) { return QJsonObject{{QString::fromLatin1(key), v}}; };
    const QList<std::pair<QString, QJsonObject>> sent = {
        {QStringLiteral("lights/links"), value("brightness", 50)},
        {QStringLiteral("lights/links"), value("brightness", 60)},  // ersetzt durch 70
        {QStringLiteral("lights/links"), value("brightness", 70)},
        {QStringLiteral("lights/links"), value("temperature", 300)}, // andere Felder: eigene Anfrage
        {QStringLiteral("lights/links"), value("brightness", 80)},
        {QStringLiteral("lights/all"), value("brightness", 30)},
        {QStringLiteral("lights/links"), value("brightness", 90)},
    };
    QList<int> order;
    QList<Reply> replies(sent.size());
    for (int i = 0; i < sent.size(); ++i) {
        client.request("PUT", sent.at(i).first, sent.at(i).second, [&order, &replies, i](const Reply &reply) {
            order.append(i);
            replies[i] = reply;
        });
    }
    // Lesen wartet nicht auf die Lampe
    bool statusRead = false;
    client.request("GET", QStringLiteral("status"), QJsonValue(), [&](const Reply &reply) {
        QCOMPARE(reply.status, 200);
        QVERIFY(order.size() < sent.size());
        statusRead = true;
    });
    QTRY_COMPARE_WITH_TIMEOUT(order.size(), sent.size(), 20000);
    QVERIFY(statusRead);

    // Antworten in der Reihenfolge des Sendens, ohne Fehler; die ersetzte hat die Antwort von 70
    QCOMPARE(order, (QList<int>{0, 1, 2, 3, 4, 5, 6}));
    for (int i = 0; i < replies.size(); ++i) {
        QVERIFY2(replies.at(i).status == 200, qPrintable(QString::number(i) + replies.at(i).error));
        QCOMPARE(replies.at(i).body.value(QStringLiteral("errors")).toArray(), QJsonArray());
    }
    QCOMPARE(replies.at(1).body, replies.at(2).body);
    QVERIFY(replies.at(0).body != replies.at(2).body);

    // An der Lampe angekommen: in der Reihenfolge, 60 nie, der letzte Wert gewinnt
    client.configure(Config{});
    lamps.stop();
    const QJsonArray puts = lamps.puts();
    QCOMPARE(puts.size(), 2);
    const QJsonArray expected{value("brightness", 50), value("brightness", 70), value("temperature", 300),
                              value("brightness", 80), value("brightness", 30), value("brightness", 90)};
    QCOMPARE(puts.at(0).toArray(), expected);
    QCOMPARE(puts.at(1).toArray(), QJsonArray{value("brightness", 30)});
    QCOMPARE(service.stop(), 0);
}

QTEST_GUILESS_MAIN(TestCameraService)
#include "tst_kameradienst.moc"
