// P15 d2: Kern als eigener Prozess gegen den echten Kamera-Dienst in der Attrappe (eigener Prozess auf
// 127.0.0.1, freier Port): kern.ini [kamera-dienst], camera_service nach hello und bei Änderungen,
// camera_call → camera_result (Preset anlegen und abrufen), feste Liste (nicht erlaubte Anfragen gehen
// nicht an den Dienst), Ereignisse camera.state_changed und light.changed auf dem Fluss, Dienst
// beenden → error, neu starten → connected, dss-cli kamera-status, kamera-preset, kamera-aufruf.
// Dazu die Nachrichten und die feste Liste ohne Kern.
//
// Quellen: docs/08-kern-schnittstelle.md («Kamera-Dienst»), docs/09-kamera-dienst.md.

#include "cameraprotocol.h"
#include "coresession.h"
#include "coresettings.h"
#include "kameradiensthilfe.h"
#include "prozesshilfe.h"
#include "protocol.h"

#include <QDeadlineTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using kameradienst::kC922;

namespace {

constexpr int kSessionTimeoutMs = 20000;

// Nächstes camera_service mit diesem Zustand; frühere werden übergangen.
bool waitForState(dss::CoreSession &session, const QString &state, dss::CameraServiceInfo *info, QString *error)
{
    const QDeadlineTimer deadline(kSessionTimeoutMs);
    while (!deadline.hasExpired()) {
        const std::optional<QJsonObject> message = session.waitForMessage(QString::fromLatin1(dss::msg::kCameraService), error);
        if (!message || !dss::parseCameraService(*message, info, error))
            return false;
        if (info->state == state)
            return true;
    }
    *error = QStringLiteral("camera_service «%1» nicht innerhalb von %2 ms").arg(state).arg(kSessionTimeoutMs);
    return false;
}

// camera_call über eine eigene Verbindung; Antwort camera_result.
dss::CameraResult call(const QString &socket, const QString &method, const QString &path,
                       const std::optional<QJsonObject> &body = std::nullopt)
{
    dss::CameraResult result;
    dss::CoreSession session(kSessionTimeoutMs);
    QString error;
    if (!session.open(socket, QStringLiteral("tst_kernkamera"), &error)) {
        result.error = QStringLiteral("Test: ") + error;
        return result;
    }
    QJsonObject fields{{QStringLiteral("method"), method}, {QStringLiteral("path"), path}};
    if (body)
        fields.insert(QStringLiteral("body"), *body);
    const std::optional<QJsonObject> reply = session.actionsRequest(QString::fromLatin1(dss::msg::kCameraCall), fields,
                                                                    QString::fromLatin1(dss::msg::kCameraResult), &error);
    if (!reply || !dss::parseCameraResult(*reply, &result, &error))
        result.error = QStringLiteral("Test: ") + error;
    return result;
}

// Typen im Verlauf des Ereignisflusses (nur camera. und light.)
QStringList eventHistory(const QString &socket, QList<dss::events::Event> *events = nullptr)
{
    dss::CoreSession session;
    QString error;
    QStringList types;
    if (!session.open(socket, QStringLiteral("tst_kernkamera"), &error))
        return types;
    const std::optional<QList<dss::events::Event>> history =
        session.subscribeEvents({QStringLiteral("camera."), QStringLiteral("light.")}, &error);
    if (!history)
        return types;
    for (const dss::events::Event &e : *history)
        types.append(e.type);
    if (events)
        *events = *history;
    return types;
}

} // namespace

class TestKernKamera : public QObject
{
    Q_OBJECT

private slots:
    void messages();
    void allowedCalls();
    void coreAgainstFakeService();
};

void TestKernKamera::messages()
{
    // Hin und zurück, ohne Kern
    const dss::CameraServiceInfo info{QStringLiteral("error"), QStringLiteral("nicht erreichbar"),
                                      QJsonObject{{QStringLiteral("api_version"), 1}}};
    dss::CameraServiceInfo info2;
    QString error;
    QVERIFY2(dss::parseCameraService(dss::cameraServiceMessage(info), &info2, &error), qPrintable(error));
    QCOMPARE(info2, info);
    QJsonObject o = dss::cameraServiceMessage({QStringLiteral("connected"), QString(), {}});
    QVERIFY(o.value(QStringLiteral("message")).isNull());
    o.insert(QStringLiteral("state"), QStringLiteral("kaputt"));
    QVERIFY(!dss::parseCameraService(o, &info2, &error));

    const dss::CameraEventInfo event{QStringLiteral("lights"), QJsonObject{{QStringLiteral("lights"), QJsonArray()}}};
    dss::CameraEventInfo event2;
    QVERIFY2(dss::parseCameraEvent(dss::cameraEventMessage(event), &event2, &error), qPrintable(error));
    QCOMPARE(event2, event);

    dss::CameraCall c{7, QStringLiteral("PUT"), QStringLiteral("lights/all"), QJsonObject{{QStringLiteral("on"), QStringLiteral("toggle")}}};
    dss::CameraCall c2;
    QVERIFY2(dss::parseCameraCall(dss::cameraCallMessage(c), &c2, &error), qPrintable(error));
    QCOMPARE(c2, c);
    c.body.reset();
    QVERIFY2(dss::parseCameraCall(dss::cameraCallMessage(c), &c2, &error), qPrintable(error));
    QCOMPARE(c2, c);
    o = dss::cameraCallMessage(c);
    o.insert(QStringLiteral("body"), QJsonArray());
    QVERIFY(!dss::parseCameraCall(o, &c2, &error));
    o = dss::cameraCallMessage(c);
    o.remove(QStringLiteral("request_id"));
    QVERIFY(!dss::parseCameraCall(o, &c2, &error));

    const dss::CameraResult r{7, 404, QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("code"), QStringLiteral("not_found")}},
                              QString()};
    dss::CameraResult r2;
    QVERIFY2(dss::parseCameraResult(dss::cameraResultMessage(r), &r2, &error), qPrintable(error));
    QCOMPARE(r2, r);
    const dss::CameraResult none{8, 0, {}, QStringLiteral("nicht verbunden")};
    QVERIFY2(dss::parseCameraResult(dss::cameraResultMessage(none), &r2, &error), qPrintable(error));
    QCOMPARE(r2, none);
}

void TestKernKamera::allowedCalls()
{
    for (const auto &[method, path] : std::initializer_list<std::pair<const char *, const char *>>{
             {"GET", "status"},
             {"POST", "camera-presets/totale/recall"},
             {"POST", "light-presets/stream/recall"},
             {"PUT", "lights/all"},
             {"PUT", "cameras/bbbb:0002:SERIE0001/controls"},
             {"GET", "cameras/bbbb%3A0002%3ASERIE0001/config"},
             {"POST", "cameras/046d:0893/move"},
             {"POST", "camera-presets"},
             {"DELETE", "light-presets/stream"},
             {"GET", "export"}}) {
        const QString problem = dss::checkCameraCall(QString::fromLatin1(method), QString::fromLatin1(path));
        QVERIFY2(problem.isEmpty(), qPrintable(problem));
    }
    for (const auto &[method, path] : std::initializer_list<std::pair<const char *, const char *>>{
             {"GET", "events"},             // Strom hält der Kern
             {"POST", "status"},            // falsche Methode
             {"get", "status"},             // nur Grossbuchstaben
             {"GET", "/status"},
             {"GET", "status/"},
             {"GET", "camera-presets/../status"},
             {"GET", "camera-presets/./x"},
             {"POST", "camera-presets/a/b/recall"},
             {"POST", "camera-presets/a b/recall"},
             {"POST", "camera-presets/a?x=1/recall"},
             {"GET", "api/v1/status"},
             {"GET", "gibt-es-nicht"},
             {"PATCH", "lights/all"}}) {
        QVERIFY2(!dss::checkCameraCall(QString::fromLatin1(method), QString::fromLatin1(path)).isEmpty(), path);
    }
    QVERIFY(!dss::checkCameraCall(QStringLiteral("GET"), QStringLiteral("camera-presets/") + QString(600, QLatin1Char('a'))).isEmpty());
    QCOMPARE(dss::allowedCameraCalls().size(), 28);
    QCOMPARE(dss::cameraApiPath(QStringLiteral("/api/v1/status")), QStringLiteral("status"));
    QCOMPARE(dss::cameraApiPath(QStringLiteral("status")), QStringLiteral("status"));
}

void TestKernKamera::coreAgainstFakeService()
{
    QTemporaryDir tmp(QDir::tempPath() + QStringLiteral("/tst_kernkamera-XXXXXX"));
    QVERIFY(tmp.isValid());
    const QString devices = kameradienst::createFakeDevices(tmp.filePath(QStringLiteral("geraete")));
    QVERIFY2(devices.isEmpty(), qPrintable(devices));
    const int port = kameradienst::freePort();
    QVERIFY(port > 0);
    kameradienst::Service service(tmp.path(), port);
    QVERIFY2(service.start(), qPrintable(service.log()));

    // Kern mit eigenem Datenordner und kern.ini mit [kamera-dienst]
    QTemporaryDir data;
    QVERIFY(testhelp::writeEmptyActions(data.path()));
    const QString configHome = testhelp::testConfigHome();
    {
        QFile ini(configHome + QStringLiteral("/dons-streaming-studio-entwicklung/kern.ini"));
        QVERIFY(ini.open(QIODevice::Append));
        ini.write(QStringLiteral("[kamera-dienst]\naktiv=ja\nadresse=127.0.0.1\nport=%1\n").arg(port).toUtf8());
    }
    const auto runtime = testhelp::runtimeDir();
    const QString socket = runtime->path() + QStringLiteral("/kern.sock");
    std::unique_ptr<QProcess> core = testhelp::startCore(data.path(), socket, configHome);
    QString log;
    QVERIFY2(testhelp::waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));

    // Einstellungen
    dss::CoreSession watcher(kSessionTimeoutMs);
    QString error;
    QVERIFY2(watcher.open(socket, QStringLiteral("tst_kernkamera"), &error), qPrintable(error));
    {
        dss::CoreSession s;
        QVERIFY2(s.open(socket, QStringLiteral("tst_kernkamera"), &error), qPrintable(error));
        const std::optional<dss::SettingsInfo> settings = s.getSettings(&error);
        QVERIFY2(settings, qPrintable(error));
        QVERIFY(settings->settings.cameraEnabled);
        QCOMPARE(settings->settings.cameraHost, QStringLiteral("127.0.0.1"));
        QCOMPARE(settings->settings.cameraPort, port);
    }

    // camera_service nach hello, dann connected mit dem Stand des Dienstes
    dss::CameraServiceInfo info;
    QVERIFY2(waitForState(watcher, QStringLiteral("connected"), &info, &error), qPrintable(error + log));
    QVERIFY(info.message.isEmpty());
    QCOMPARE(info.status.value(QStringLiteral("fake")).toBool(), true);
    QCOMPARE(info.status.value(QStringLiteral("cameras")).toArray().size(), 2);

    const QStringList connection = {QStringLiteral("--verbindung"), socket};
    testhelp::CliResult c = testhelp::runCliOnly(connection + QStringList{QStringLiteral("kamera-status")});
    QVERIFY2(c.exitCode == 0, qPrintable(c.out + c.err));
    QVERIFY2(c.out.contains(QStringLiteral("Zustand: connected")), qPrintable(c.out));
    QVERIFY2(c.out.contains(QStringLiteral("OBSBOT Tiny 2")), qPrintable(c.out));

    // Ereignisfluss: Stand beim Verbinden als camera.state_changed und light.changed
    QTRY_VERIFY_WITH_TIMEOUT(eventHistory(socket).contains(QStringLiteral("camera.state_changed")), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(eventHistory(socket).contains(QStringLiteral("light.changed")), 10000);
    QList<dss::events::Event> events;
    eventHistory(socket, &events);
    for (const dss::events::Event &e : std::as_const(events)) {
        if (e.type == QLatin1String("camera.state_changed")) {
            QCOMPARE(e.origin, QStringLiteral("camera"));
            QCOMPARE(e.data.value(QStringLiteral("cameras")).toArray().size(), 2);
        }
    }

    // Preset über den Socket anlegen und abrufen
    dss::CameraResult r = call(socket, QStringLiteral("POST"), QStringLiteral("camera-presets"),
                               QJsonObject{{QStringLiteral("name"), QStringLiteral("Totale")}, {QStringLiteral("camera"), kC922}});
    QVERIFY2(r.status == 200, qPrintable(r.error + QString::number(r.status)));
    QCOMPARE(r.body.value(QStringLiteral("preset")).toObject().value(QStringLiteral("id")).toString(), QStringLiteral("totale"));
    r = call(socket, QStringLiteral("POST"), QStringLiteral("camera-presets/totale/recall"));
    QVERIFY2(r.status == 200, qPrintable(r.error + QString::number(r.status)));
    QCOMPARE(r.body.value(QStringLiteral("ok")).toBool(), true);
    QVERIFY(r.error.isEmpty());
    // Fehler des Dienstes kommen mit Status und body
    r = call(socket, QStringLiteral("GET"), QStringLiteral("camera-presets/gibt-es-nicht"));
    QCOMPARE(r.status, 404);
    QCOMPARE(r.body.value(QStringLiteral("code")).toString(), QStringLiteral("not_found"));
    // camera_service führt die Liste der Presets nach (an alle, also auch an watcher)
    bool presetListed = false;
    for (int i = 0; i < 50 && !presetListed; ++i) {
        QVERIFY2(waitForState(watcher, QStringLiteral("connected"), &info, &error), qPrintable(error));
        presetListed = info.status.value(QStringLiteral("camera_presets")).toArray().size() == 1;
    }
    QVERIFY(presetListed);

    // dss-cli
    c = testhelp::runCliOnly(connection + QStringList{QStringLiteral("kamera-preset"), QStringLiteral("totale")});
    QVERIFY2(c.exitCode == 0, qPrintable(c.out + c.err));
    c = testhelp::runCliOnly(connection + QStringList{QStringLiteral("kamera-aufruf"), QStringLiteral("get"),
                                                      QStringLiteral("/api/v1/camera-presets/totale")});
    QVERIFY2(c.exitCode == 0, qPrintable(c.out + c.err));
    QVERIFY2(c.out.contains(kC922), qPrintable(c.out));
    c = testhelp::runCliOnly(connection + QStringList{QStringLiteral("licht-preset"), QStringLiteral("gibt-es-nicht")});
    QVERIFY2(c.exitCode == 1, qPrintable(c.out + c.err));
    QVERIFY2(c.err.contains(QStringLiteral("404")), qPrintable(c.err));
    // Nicht erlaubt: dss-cli prüft selbst (Exit-Code 2) ...
    c = testhelp::runCliOnly(connection + QStringList{QStringLiteral("kamera-aufruf"), QStringLiteral("GET"), QStringLiteral("events")});
    QVERIFY2(c.exitCode == 2, qPrintable(c.out + c.err));
    c = testhelp::runCliOnly(connection + QStringList{QStringLiteral("kamera-aufruf"), QStringLiteral("PUT"), QStringLiteral("lights/all"),
                                                      QStringLiteral("[1]")});
    QVERIFY2(c.exitCode == 2, qPrintable(c.out + c.err));

    // ... und der Kern weist ab, ohne den Dienst zu fragen: Status 0 (der Dienst hätte mit einem
    // HTTP-Status geantwortet, auf POST status mit 404 oder 405).
    for (const auto &[method, path] : std::initializer_list<std::pair<const char *, const char *>>{
             {"GET", "events"}, {"POST", "status"}, {"GET", "camera-presets/../status"}, {"DELETE", "export"}}) {
        r = call(socket, QString::fromLatin1(method), QString::fromLatin1(path));
        QCOMPARE(r.status, 0);
        QVERIFY2(r.error.contains(QStringLiteral("nicht erlaubt")), qPrintable(r.error));
        QVERIFY(r.body.isEmpty());
    }

    // Dienst beenden: error mit Meldung, der letzte Stand bleibt; Anfragen ohne Antwort
    QCOMPARE(service.stop(), 0);
    QVERIFY2(waitForState(watcher, QStringLiteral("error"), &info, &error), qPrintable(error));
    QVERIFY(!info.message.isEmpty());
    QCOMPARE(info.status.value(QStringLiteral("camera_presets")).toArray().size(), 1);
    r = call(socket, QStringLiteral("GET"), QStringLiteral("status"));
    QCOMPARE(r.status, 0);
    QVERIFY(!r.error.isEmpty());
    c = testhelp::runCliOnly(connection + QStringList{QStringLiteral("kamera-status")});
    QVERIFY2(c.exitCode == 0 && c.out.contains(QStringLiteral("Zustand: error")), qPrintable(c.out + c.err));

    // Neu starten: Der Kern verbindet von selbst wieder, das Preset ist noch da
    QVERIFY2(service.start(), qPrintable(service.log()));
    QVERIFY2(waitForState(watcher, QStringLiteral("connected"), &info, &error), qPrintable(error));
    r = call(socket, QStringLiteral("GET"), QStringLiteral("camera-presets/totale"));
    QCOMPARE(r.status, 200);

    // Protokoll des Kerns ohne Inhalt der Anfragen
    core->terminate();
    QVERIFY(core->waitForFinished(10000));
    QCOMPARE(core->exitStatus(), QProcess::NormalExit);
    QCOMPARE(core->exitCode(), 0);
    log += QString::fromUtf8(core->readAllStandardError());
    QVERIFY2(log.contains(QStringLiteral("nicht erlaubt")), qPrintable(log));
    QVERIFY2(log.contains(QStringLiteral("Kamera-Dienst: verbunden")), qPrintable(log));
    QVERIFY2(!log.contains(QStringLiteral("Totale")), qPrintable(log));
    QCOMPARE(service.stop(), 0);
}

QTEST_GUILESS_MAIN(TestKernKamera)
#include "tst_kernkamera.moc"
