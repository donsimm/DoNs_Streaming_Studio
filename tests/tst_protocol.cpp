// Prüft Lesen und Schreiben aller Nachrichten der Schnittstelle Kern–Fenster ohne Qt Gui
// (P07, Teil D; docs/08-kern-schnittstelle.md).

#include "actionprotocol.h"
#include "browsersource.h"
#include "overlayjson.h"
#include "protocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace dss;

namespace {

// Durch eine Zeile und zurück, wie über die Verbindung.
QJsonObject roundTrip(const QJsonObject &message)
{
    const QByteArray line = encodeMessage(message);
    if (!line.endsWith('\n') || line.count('\n') != 1)
        return {};
    QJsonObject back;
    QString error;
    if (!decodeMessage(line.chopped(1), &back, &error))
        return {};
    return back;
}

Overlay sampleOverlay()
{
    Overlay o;
    o.id = QUuid::fromString(QStringLiteral("db7f47a9-dee3-4de5-b056-40b5b3507607"));
    o.name = QStringLiteral("Überblick Start");
    o.address = QStringLiteral("ueberblick-start");
    o.width = 1920;
    o.height = 1080;
    Source text;
    text.id = QUuid::fromString(QStringLiteral("24b4efb3-8bde-45fe-af64-75f947cbdb95"));
    text.kind = SourceKind::Text;
    text.name = QStringLiteral("Text: Willkommen");
    text.x = -40;
    text.y = 900;
    text.width = 800;
    text.height = 140;
    text.rotation = 125;
    text.visible = false;
    text.text.content = QStringLiteral("Willkommen\nZweite Zeile");
    text.text.fontFile =
        QStringLiteral("media/57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e.ttf");
    Source video;
    video.id = QUuid::fromString(QStringLiteral("b814d820-5d53-42a8-9683-ef3419cf4d7e"));
    video.kind = SourceKind::Video;
    video.name = QStringLiteral("alpha.webm");
    video.width = 512;
    video.height = 512;
    video.loop = true;
    video.holdAtEnd = true; // P09; mit loop ohne Wirkung, bleibt aber gespeichert
    video.file =
        QStringLiteral("media/09445b269e30ac5f3eddf936bc7a19a960ea82dbcd9ebfcc17ff5acace4c7bdf.webm");
    o.sources = {video, text};
    return o;
}

} // namespace

class TestProtocol : public QObject
{
    Q_OBJECT

private slots:
    void lineHasNoInnerNewline()
    {
        // Zeilenwechsel im Text eines Overlays wird im JSON als \n geschrieben, nie als Byte 0x0A.
        const QByteArray line = encodeMessage(overlayMessage(msg::kOverlay, sampleOverlay(), {}));
        QCOMPARE(line.count('\n'), 1);
        QVERIFY(line.endsWith('\n'));
        QVERIFY(line.contains("Willkommen\\nZweite Zeile"));
    }

    void decodeErrors_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::newRow("kein JSON") << QByteArray("hallo");
        QTest::newRow("Liste") << QByteArray("[1,2]");
        QTest::newRow("ohne type") << QByteArray("{\"id\":1}");
        QTest::newRow("type keine Zahl") << QByteArray("{\"type\":5}");
        QTest::newRow("type leer") << QByteArray("{\"type\":\"\"}");
        QTest::newRow("leer") << QByteArray();
    }

    void decodeErrors()
    {
        QFETCH(QByteArray, line);
        QJsonObject o;
        QString error;
        QVERIFY(!decodeMessage(line, &o, &error));
        QVERIFY(!error.isEmpty());
    }

    void lineSplitter()
    {
        LineSplitter s;
        QByteArray line;
        s.append("{\"type\":\"a\"}\n{\"ty");
        QCOMPARE(s.next(&line), LineSplitter::Result::Line);
        QCOMPARE(line, QByteArray("{\"type\":\"a\"}"));
        QCOMPARE(s.next(&line), LineSplitter::Result::None);
        s.append("pe\":\"b\"}\n\n");
        QCOMPARE(s.next(&line), LineSplitter::Result::Line);
        QCOMPARE(line, QByteArray("{\"type\":\"b\"}"));
        QCOMPARE(s.next(&line), LineSplitter::Result::Line);
        QCOMPARE(line, QByteArray());
        QCOMPARE(s.next(&line), LineSplitter::Result::None);
    }

    void lineSplitterTooLong()
    {
        LineSplitter s;
        QByteArray line;
        s.append(QByteArray(kMaxMessageBytes, 'x'));
        QCOMPARE(s.next(&line), LineSplitter::Result::None); // genau die Grenze, noch offen
        s.append("x");
        QCOMPARE(s.next(&line), LineSplitter::Result::TooLong);

        LineSplitter t;
        t.append(QByteArray(kMaxMessageBytes, 'y') + '\n');
        QCOMPARE(t.next(&line), LineSplitter::Result::Line); // genau 16 MiB sind erlaubt
        QCOMPARE(line.size(), kMaxMessageBytes);
    }

    void hello()
    {
        const Hello h{QStringLiteral("dss-core"), QStringLiteral("0.1.0"), kProtocolVersion};
        const QJsonObject o = roundTrip(helloMessage(h));
        QCOMPARE(messageType(o), QStringLiteral("hello"));
        Hello back;
        QString error;
        QVERIFY2(parseHello(o, &back, &error), qPrintable(error));
        QCOMPARE(back, h);
        QCOMPARE(o.value(QStringLiteral("protocol_version")).toInt(), 19); // seit P15 d2 (Kamera-Dienst)
        QVERIFY(!o.contains(QStringLiteral("data_dir")));

        // P09: Datenordner des Kerns für die Dateien der Quellen (nur vom Kern)
        const Hello core{QStringLiteral("dss-core"), QStringLiteral("0.1.0"), kProtocolVersion,
                         QStringLiteral("/home/nutzer/.local/share/dons-streaming-studio")};
        const QJsonObject withDir = roundTrip(helloMessage(core));
        QCOMPARE(withDir.value(QStringLiteral("data_dir")).toString(), core.dataDir);
        QVERIFY2(parseHello(withDir, &back, &error), qPrintable(error));
        QCOMPARE(back, core);
        QJsonObject bad = withDir;
        bad.insert(QStringLiteral("data_dir"), 5);
        QVERIFY(!parseHello(bad, &back, &error));
    }

    void helloInvalid()
    {
        Hello back;
        QString error;
        QJsonObject o = helloMessage({QStringLiteral("x"), QStringLiteral("1"), 1});
        o.remove(QStringLiteral("program"));
        QVERIFY(!parseHello(o, &back, &error));
        o = helloMessage({QStringLiteral("x"), QStringLiteral("1"), 1});
        o.insert(QStringLiteral("protocol_version"), 1.5);
        QVERIFY(!parseHello(o, &back, &error));
        o.insert(QStringLiteral("protocol_version"), 0);
        QVERIFY(!parseHello(o, &back, &error));
        // Unbekannte Felder werden übergangen.
        o.insert(QStringLiteral("protocol_version"), 3);
        o.insert(QStringLiteral("neu"), true);
        QVERIFY(parseHello(o, &back, &error));
        QCOMPARE(back.protocolVersion, 3);
    }

    void overlayList()
    {
        // Seit P08 (Version 2): Einträge in der Reihenfolge der Liste, mit Ordnern.
        const QList<OverlaySummary> list = {
            summarizeOverlay(sampleOverlay()),
            {QUuid::fromString(QStringLiteral("7be08f35-6885-4a95-9c32-939c45a688ea")),
             QStringLiteral("Chat Ecke"), QStringLiteral("chat-ecke"), 800, 600, 0, false}};
        QCOMPARE(list.first().sourceCount, 2);
        QCOMPARE(list.first().output, true); // P10 (E-24)
        OverlayLayout layout = layoutFromIds({list.at(0).id});
        ListEntry folder;
        folder.isFolder = true;
        folder.folder = {QUuid::fromString(QStringLiteral("5c0f9a1e-2b7d-4e43-9d5a-8f1e2c3b4a50")),
                         QStringLiteral("Szenen"), false, {list.at(1).id}};
        layout.entries.append(folder);
        const QJsonObject o = roundTrip(overlayListMessage(layout, list));
        QCOMPARE(messageType(o), QStringLiteral("overlay_list"));
        QVERIFY(o.value(QStringLiteral("list_error")).isNull());
        OverlayListData back;
        QString error;
        QVERIFY2(parseOverlayList(o, &back, &error), qPrintable(error));
        QCOMPARE(back.overlays, list);
        QCOMPARE(back.layout, layout);
        QCOMPARE(back.summary(list.at(1).id)->name, QStringLiteral("Chat Ecke"));
        QVERIFY(back.listError.isEmpty());
        // Meldung bei kaputter liste.json
        QVERIFY(parseOverlayList(roundTrip(overlayListMessage(layout, list, QStringLiteral("liste.json: kaputt"))),
                                 &back, &error));
        QCOMPARE(back.listError, QStringLiteral("liste.json: kaputt"));
        // Leere Liste
        QVERIFY(parseOverlayList(roundTrip(overlayListMessage({}, {})), &back, &error));
        QVERIFY(back.overlays.isEmpty());
        QVERIFY(back.layout.entries.isEmpty());
    }

    void overlayListInvalid()
    {
        OverlayListData back;
        QString error;
        const OverlaySummary summary = summarizeOverlay(sampleOverlay());
        QJsonObject o = overlayListMessage(layoutFromIds({summary.id}), {summary});
        QJsonArray a = o.value(QStringLiteral("entries")).toArray();
        QJsonObject entry = a.at(0).toObject();
        QJsonObject e = entry.value(QStringLiteral("overlay")).toObject();
        e.insert(QStringLiteral("width"), 0);
        entry.insert(QStringLiteral("overlay"), e);
        a.replace(0, entry);
        o.insert(QStringLiteral("entries"), a);
        QVERIFY(!parseOverlayList(o, &back, &error));
        QVERIFY(error.contains(QStringLiteral("entries[0]")));
        e.insert(QStringLiteral("width"), 10);
        e.insert(QStringLiteral("id"), QStringLiteral("{DB7F47A9-DEE3-4DE5-B056-40B5B3507607}"));
        entry.insert(QStringLiteral("overlay"), e);
        a.replace(0, entry);
        o.insert(QStringLiteral("entries"), a);
        QVERIFY(!parseOverlayList(o, &back, &error));
        // P10: output ist Pflicht und true/false
        e.insert(QStringLiteral("id"), summary.id.toString(QUuid::WithoutBraces));
        e.remove(QStringLiteral("output"));
        entry.insert(QStringLiteral("overlay"), e);
        a.replace(0, entry);
        o.insert(QStringLiteral("entries"), a);
        QVERIFY(!parseOverlayList(o, &back, &error));
        QVERIFY2(error.contains(QStringLiteral("output")), qPrintable(error));
        QVERIFY(!parseOverlayList(simpleMessage(msg::kOverlayList), &back, &error));
    }

    void overlayAndChanged_data()
    {
        QTest::addColumn<QString>("type");
        QTest::newRow("overlay") << QString::fromLatin1(msg::kOverlay);
        QTest::newRow("overlay_changed") << QString::fromLatin1(msg::kOverlayChanged);
    }

    void overlayAndChanged()
    {
        QFETCH(QString, type);
        const Overlay overlay = sampleOverlay();
        const QByteArray typeLatin = type.toLatin1();
        const MediaSizes sizes{{QStringLiteral("media/a.png"), QSize(640, 480)},
                               {QStringLiteral("media/b.webm"), QSize(1920, 1080)}};
        const QJsonObject o = roundTrip(overlayMessage(typeLatin.constData(), overlay, sizes));
        QCOMPARE(messageType(o), type);
        // Im Format der Overlay-Datei, also mit format_version.
        QCOMPARE(o.value(QStringLiteral("overlay")).toObject(), overlayToJson(overlay));
        QCOMPARE(o.value(QStringLiteral("media_sizes")).toObject().value(QStringLiteral("media/a.png")),
                 QJsonValue(QJsonObject{{QStringLiteral("width"), 640}, {QStringLiteral("height"), 480}}));
        Overlay back;
        MediaSizes backSizes;
        QString error;
        QVERIFY2(parseOverlayMessage(o, &back, &backSizes, &error), qPrintable(error));
        QCOMPARE(back, overlay);
        QCOMPARE(backSizes, sizes);
        // Leer ist erlaubt (Kern ohne MediaSizeReader, nur Text).
        QVERIFY2(parseOverlayMessage(roundTrip(overlayMessage(typeLatin.constData(), overlay, {})),
                                     &back, &backSizes, &error),
                 qPrintable(error));
        QVERIFY(backSizes.isEmpty());
    }

    void overlayMediaSizesInvalid_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::newRow("fehlt") << QJsonValue(QJsonValue::Undefined);
        QTest::newRow("Liste") << QJsonValue(QJsonArray());
        const auto entry = [](const QJsonValue &width, const QJsonValue &height) {
            return QJsonValue(QJsonObject{
                {QStringLiteral("media/a.png"),
                 QJsonObject{{QStringLiteral("width"), width}, {QStringLiteral("height"), height}}}});
        };
        QTest::newRow("Breite 0") << entry(0, 10);
        QTest::newRow("Höhe negativ") << entry(10, -1);
        QTest::newRow("Bruchteil") << entry(10.5, 10);
        QTest::newRow("Text") << entry(QStringLiteral("10"), 10);
        QTest::newRow("kein Objekt")
            << QJsonValue(QJsonObject{{QStringLiteral("media/a.png"), 10}});
        QTest::newRow("Feld zu viel") << QJsonValue(QJsonObject{
            {QStringLiteral("media/a.png"),
             QJsonObject{{QStringLiteral("width"), 1}, {QStringLiteral("height"), 1},
                         {QStringLiteral("depth"), 1}}}});
    }

    void overlayMediaSizesInvalid()
    {
        QFETCH(QJsonValue, value);
        QJsonObject o = overlayMessage(msg::kOverlay, sampleOverlay(), {});
        if (value.isUndefined())
            o.remove(QStringLiteral("media_sizes"));
        else
            o.insert(QStringLiteral("media_sizes"), value);
        Overlay back;
        MediaSizes sizes;
        QString error;
        QVERIFY(!parseOverlayMessage(o, &back, &sizes, &error));
        QVERIFY(error.contains(QStringLiteral("media_sizes")));
    }

    void overlayHasNoToken()
    {
        // Regel 12: kein Feld für einen Token, an keiner Stelle des Overlays.
        const QByteArray line = encodeMessage(overlayMessage(msg::kOverlay, sampleOverlay(), {}));
        QVERIFY(!line.contains("token"));
    }

    void overlayInvalid()
    {
        Overlay back;
        QString error;
        MediaSizes sizes;
        QJsonObject o = overlayMessage(msg::kOverlay, sampleOverlay(), {});
        QJsonObject inner = o.value(QStringLiteral("overlay")).toObject();
        inner.insert(QStringLiteral("token"), QStringLiteral("x")); // unbekanntes Feld der Datei
        o.insert(QStringLiteral("overlay"), inner);
        QVERIFY(!parseOverlayMessage(o, &back, &sizes, &error));
        QVERIFY(!parseOverlayMessage(simpleMessage(msg::kOverlay), &back, &sizes, &error));
    }

    void overlayIdMessages_data()
    {
        QTest::addColumn<QString>("type");
        QTest::newRow("overlay_removed") << QString::fromLatin1(msg::kOverlayRemoved);
        QTest::newRow("get_overlay") << QString::fromLatin1(msg::kGetOverlay);
    }

    void overlayIdMessages()
    {
        QFETCH(QString, type);
        const QUuid id = sampleOverlay().id;
        const QByteArray typeLatin = type.toLatin1();
        const QJsonObject o = roundTrip(overlayIdMessage(typeLatin.constData(), id));
        QCOMPARE(messageType(o), type);
        QCOMPARE(o.value(QStringLiteral("id")).toString(),
                 QStringLiteral("db7f47a9-dee3-4de5-b056-40b5b3507607"));
        QUuid back;
        QString error;
        QVERIFY(parseOverlayId(o, &back, &error));
        QCOMPARE(back, id);
        QVERIFY(!parseOverlayId(simpleMessage(typeLatin.constData()), &back, &error));
    }

    void stats()
    {
        SystemStats s;
        s.cpuPercent = 4.104477611940299;
        s.ramUsedKiB = 3713872;
        s.ramTotalKiB = 32809248;
        s.lan = LanStats{QStringLiteral("enp3s0"), 1.056, 0.25};
        QJsonObject o = roundTrip(statsMessage(s));
        QCOMPARE(messageType(o), QStringLiteral("stats"));
        SystemStats back;
        QString error;
        QVERIFY2(parseStats(o, &back, &error), qPrintable(error));
        QCOMPARE(back, s);

        // Unbekannte Werte als null, das Feld ist trotzdem da.
        const SystemStats empty;
        o = roundTrip(statsMessage(empty));
        QVERIFY(o.contains(QStringLiteral("lan")) && o.value(QStringLiteral("lan")).isNull());
        QVERIFY(o.value(QStringLiteral("cpu_percent")).isNull());
        QVERIFY(parseStats(o, &back, &error));
        QCOMPARE(back, empty);
    }

    void statsInvalid()
    {
        SystemStats back;
        QString error;
        SystemStats s;
        s.cpuPercent = 50;
        QJsonObject o = statsMessage(s);
        o.remove(QStringLiteral("lan"));
        QVERIFY(!parseStats(o, &back, &error)); // Feld fehlt
        o = statsMessage(s);
        o.insert(QStringLiteral("cpu_percent"), 100.5);
        QVERIFY(!parseStats(o, &back, &error));
        o.insert(QStringLiteral("cpu_percent"), -1);
        QVERIFY(!parseStats(o, &back, &error));
        o = statsMessage(s);
        o.insert(QStringLiteral("ram_used_kib"), 1.5);
        QVERIFY(!parseStats(o, &back, &error));
        o = statsMessage(s);
        o.insert(QStringLiteral("lan"), QJsonObject{{QStringLiteral("interface"), QStringLiteral("x")}});
        QVERIFY(!parseStats(o, &back, &error));
        o.insert(QStringLiteral("lan"), 5);
        QVERIFY(!parseStats(o, &back, &error));
    }

    void connections()
    {
        ConnectionStates states;
        states.insert(Connection::Twitch, ConnectionState::Connected);
        states.insert(Connection::YouTube, ConnectionState::Connecting);
        states.insert(Connection::Obs, ConnectionState::Error);
        states.insert(Connection::Ndi, ConnectionState::Off);
        const QJsonObject o = roundTrip(connectionsMessage(states));
        QCOMPARE(messageType(o), QStringLiteral("connections"));
        QCOMPARE(o.value(QStringLiteral("twitch")).toString(), QStringLiteral("connected"));
        QCOMPARE(o.value(QStringLiteral("youtube")).toString(), QStringLiteral("connecting"));
        QCOMPARE(o.value(QStringLiteral("obs")).toString(), QStringLiteral("error"));
        QCOMPARE(o.value(QStringLiteral("ndi")).toString(), QStringLiteral("off"));
        ConnectionStates back;
        QString error;
        QVERIFY2(parseConnections(o, &back, &error), qPrintable(error));
        QCOMPARE(back, states);
        // Fehlende Verbindung im Hash: «off»
        QVERIFY(parseConnections(roundTrip(connectionsMessage({})), &back, &error));
        QCOMPARE(back.value(Connection::Obs), ConnectionState::Off);
        QJsonObject bad = o;
        bad.insert(QStringLiteral("obs"), QStringLiteral("grün"));
        QVERIFY(!parseConnections(bad, &back, &error));
        bad.remove(QStringLiteral("obs"));
        QVERIFY(!parseConnections(bad, &back, &error));
    }

    void quitRequestAndReply()
    {
        QCOMPARE(messageType(roundTrip(simpleMessage(msg::kQuitRequest))),
                 QStringLiteral("quit_request"));
        QCOMPARE(messageType(roundTrip(simpleMessage(msg::kGetOverlayList))),
                 QStringLiteral("get_overlay_list"));
        for (const QuitReply &r : {QuitReply{true, 0}, QuitReply{false, 2}}) {
            const QJsonObject o = roundTrip(quitReplyMessage(r));
            QCOMPARE(messageType(o), QStringLiteral("quit_reply"));
            QuitReply back;
            QString error;
            QVERIFY(parseQuitReply(o, &back, &error));
            QCOMPARE(back, r);
        }
        QuitReply back;
        QString error;
        QJsonObject o = quitReplyMessage({true, 0});
        o.insert(QStringLiteral("accepted"), 1);
        QVERIFY(!parseQuitReply(o, &back, &error));
    }

    void error()
    {
        for (const ErrorInfo &e : {ErrorInfo{QString::fromLatin1(err::kUnknownOverlay),
                                             QStringLiteral("Kein Overlay"),
                                             QStringLiteral("get_overlay")},
                                   ErrorInfo{QString::fromLatin1(err::kInvalidJson),
                                             QStringLiteral("kein JSON"), QString()}}) {
            const QJsonObject o = roundTrip(errorMessage(e));
            QCOMPARE(messageType(o), QStringLiteral("error"));
            QVERIFY(o.contains(QStringLiteral("request")));
            ErrorInfo back;
            QString error;
            QVERIFY(parseError(o, &back, &error));
            QCOMPARE(back, e);
        }
    }

    // P10c: Einstellungen und Zustand von OBS
    void obsMessages()
    {
        SettingsInfo info{{8081, QString()}, 1024, 32767, QStringLiteral("/tmp/kern.ini"), QString()};
        info.settings.obsEnabled = true;
        info.settings.obsHost = QStringLiteral("192.168.1.30");
        info.settings.obsPort = 4456;
        info.obsPasswordSet = true;
        SettingsInfo backInfo;
        QString error;
        const QJsonObject settings = settingsMessage(info);
        QVERIFY2(parseSettings(settings, &backInfo, &error), qPrintable(error));
        QCOMPARE(backInfo, info);
        for (const char *key : {"obs_enabled", "obs_host", "obs_port", "obs_password_set"}) {
            QJsonObject bad = settings;
            bad.remove(QLatin1String(key));
            QVERIFY2(!parseSettings(bad, &backInfo, &error), key);
        }

        SettingsUpdate update;
        update.requestId = 3;
        update.obsEnabled = false;
        update.obsHost = QStringLiteral("obs.local");
        update.obsPort = 4455;
        update.obsPassword = QString(); // leer = löschen, fehlt = behalten
        SettingsUpdate parsed;
        const QJsonObject sent = settingsUpdateMessage(update);
        QVERIFY2(parseSettingsUpdate(sent, &parsed, &error), qPrintable(error));
        QCOMPARE(parsed, update);
        QVERIFY(sent.contains(QStringLiteral("obs_password")));
        update = SettingsUpdate();
        update.requestId = 4;
        update.obsPort = 4455;
        QVERIFY(!settingsUpdateMessage(update).contains(QStringLiteral("obs_password")));
        QVERIFY(!parseSettingsUpdate(QJsonObject{{QStringLiteral("request_id"), 1},
                                                 {QStringLiteral("obs_enabled"), QStringLiteral("ja")}},
                                     &parsed, &error));
        QVERIFY(!parseSettingsUpdate(QJsonObject{{QStringLiteral("request_id"), 1},
                                                 {QStringLiteral("obs_password"), 5}},
                                     &parsed, &error));

        ObsStatusInfo status;
        ObsStatusInfo backStatus;
        QCOMPARE(QString::fromUtf8(encodeMessage(obsStatusMessage(status))),
                 QStringLiteral("{\"current_scene\":null,\"message\":null,\"obs_version\":null,\"scenes\":[],"
                                "\"type\":\"obs_status\",\"websocket_version\":null}\n"));
        status.message = QStringLiteral("OBS nicht erreichbar");
        QVERIFY2(parseObsStatus(obsStatusMessage(status), &backStatus, &error), qPrintable(error));
        QCOMPARE(backStatus, status);
        status = ObsStatusInfo();
        status.obsVersion = QStringLiteral("33.0.1");
        status.websocketVersion = QStringLiteral("5.7.6");
        status.currentScene = {QStringLiteral("a"), QStringLiteral("Live")};
        status.scenes = {{QStringLiteral("a"), QStringLiteral("Live")}, {QStringLiteral("b"), QStringLiteral("Pause")}};
        QVERIFY2(parseObsStatus(obsStatusMessage(status), &backStatus, &error), qPrintable(error));
        QCOMPARE(backStatus, status);
        QJsonObject bad = obsStatusMessage(status);
        bad.remove(QStringLiteral("scenes"));
        QVERIFY(!parseObsStatus(bad, &backStatus, &error));
        bad = obsStatusMessage(status);
        bad.insert(QStringLiteral("current_scene"), 3);
        QVERIFY(!parseObsStatus(bad, &backStatus, &error));
        bad = obsStatusMessage(status);
        bad.insert(QStringLiteral("message"), 3);
        QVERIFY(!parseObsStatus(bad, &backStatus, &error));
    }

    // P10c, E-29: Quellen in OBS, ohne URL und ohne Token
    void obsSourceMessages()
    {
        const QUuid a = QUuid::fromString(QStringLiteral("aaaaaaaa-0000-4000-8000-000000000001"));
        const QUuid b = QUuid::fromString(QStringLiteral("bbbbbbbb-0000-4000-8000-000000000002"));
        const QUuid c = QUuid::fromString(QStringLiteral("cccccccc-0000-4000-8000-000000000003"));
        const QList<ObsSourceInfo> sources = {{a, ObsSourceState::None, QString()},
                                              {b, ObsSourceState::Present, QStringLiteral("Quelle")},
                                              {c, ObsSourceState::Missing, QStringLiteral("Weg")}};
        QCOMPARE(QString::fromUtf8(encodeMessage(obsSourcesMessage({sources.at(1)}))),
                 QStringLiteral("{\"sources\":[{\"input_name\":\"Quelle\",\"overlay_id\":\"bbbbbbbb-0000-4000-8000-000000000002\","
                                "\"state\":\"present\"}],\"type\":\"obs_sources\"}\n"));
        QList<ObsSourceInfo> back;
        QString error;
        QVERIFY2(parseObsSources(obsSourcesMessage(sources), &back, &error), qPrintable(error));
        QCOMPARE(back, sources);
        QVERIFY(!parseObsSources(QJsonObject{}, &back, &error));
        QJsonObject bad = obsSourcesMessage(sources);
        QJsonArray list = bad.value(QStringLiteral("sources")).toArray();
        QJsonObject first = list.at(0).toObject();
        first.insert(QStringLiteral("state"), QStringLiteral("vielleicht"));
        list[0] = first;
        bad.insert(QStringLiteral("sources"), list);
        QVERIFY(!parseObsSources(bad, &back, &error));

        const ObsSourceRequest request{5, a, QStringLiteral("szene"), QStringLiteral("Name")};
        ObsSourceRequest parsed;
        QVERIFY2(parseObsCreateSource(obsCreateSourceMessage(request), &parsed, &error), qPrintable(error));
        QCOMPARE(parsed, request);
        QCOMPARE(messageType(obsCreateSourceMessage(request)), QStringLiteral("obs_create_source"));
        QJsonObject noName = obsCreateSourceMessage(request);
        noName.remove(QStringLiteral("name"));
        QVERIFY(!parseObsCreateSource(noName, &parsed, &error));
        QVERIFY2(parseObsAddToScene(obsAddToSceneMessage(request), &parsed, &error), qPrintable(error));
        QCOMPARE(parsed, (ObsSourceRequest{5, a, QStringLiteral("szene"), QString()}));
        QCOMPARE(messageType(obsAddToSceneMessage(request)), QStringLiteral("obs_add_to_scene"));
        QVERIFY(!obsAddToSceneMessage(request).contains(QStringLiteral("name")));
        QJsonObject noScene = obsAddToSceneMessage(request);
        noScene.remove(QStringLiteral("scene_uuid"));
        QVERIFY(!parseObsAddToScene(noScene, &parsed, &error));
    }

    // P13: twitch_status und die Anfragen zu den Konten
    void twitchMessages()
    {
        TwitchStatusInfo info;
        info.enabled = true;
        info.message = QStringLiteral("Rechte fehlen");
        info.live = true;
        info.subscriptions = {{QStringLiteral("channel.follow"), true, QString()},
                              {QStringLiteral("channel.raid"), false, QStringLiteral("HTTP 403")}};
        info.channel.state = QStringLiteral("ok");
        info.channel.login = QStringLiteral("testkanal");
        info.channel.userId = QStringLiteral("1");
        info.channel.missingScopes = {QStringLiteral("bits:read")};
        info.bot.state = QStringLiteral("off");
        info.bot.loginPending = TwitchLoginPending{QStringLiteral("ABCD1234"), QStringLiteral("https://www.twitch.tv/activate"),
                                                   QDateTime(QDate(2026, 11, 15), QTime(12, 0), QTimeZone::UTC)};
        info.badgeProblem = QStringLiteral("Abzeichen nicht aktiv: Bot nicht angemeldet.");
        const QJsonObject o = twitchStatusMessage(info);
        QCOMPARE(messageType(o), QStringLiteral("twitch_status"));
        TwitchStatusInfo back;
        QString error;
        QVERIFY2(parseTwitchStatus(o, &back, &error), qPrintable(error));
        QCOMPARE(back, info);
        QCOMPARE(o.value(QStringLiteral("accounts")).toObject().value(QStringLiteral("channel")).toObject()
                     .value(QStringLiteral("login_pending")), QJsonValue(QJsonValue::Null));
        QVERIFY2(parseTwitchStatus(twitchStatusMessage({}), &back, &error), qPrintable(error));
        QCOMPARE(back, TwitchStatusInfo());
        QJsonObject bad = o;
        QJsonObject accounts = bad.value(QStringLiteral("accounts")).toObject();
        QJsonObject channel = accounts.value(QStringLiteral("channel")).toObject();
        channel.insert(QStringLiteral("state"), QStringLiteral("gut"));
        accounts.insert(QStringLiteral("channel"), channel);
        bad.insert(QStringLiteral("accounts"), accounts);
        QVERIFY(!parseTwitchStatus(bad, &back, &error));

        const TwitchAccountRequest request{4, QStringLiteral("bot")};
        TwitchAccountRequest parsed;
        for (const char *type : {msg::kTwitchLoginStart, msg::kTwitchLoginCancel, msg::kTwitchLogout}) {
            const QJsonObject m = twitchAccountRequestMessage(type, request);
            QCOMPARE(messageType(m), QLatin1String(type));
            QVERIFY2(parseTwitchAccountRequest(m, &parsed, &error), qPrintable(error));
            QCOMPARE(parsed, request);
        }
        QVERIFY(!parseTwitchAccountRequest(twitchAccountRequestMessage(msg::kTwitchLogout, {4, QStringLiteral("kanal")}),
                                           &parsed, &error));
    }

    // P10-vor: Einstellungen des Kerns
    void settingsMessages()
    {
        const SettingsInfo info{{8081, QStringLiteral("192.168.1.20")}, 1024, 32767,
                                QStringLiteral("/tmp/kern.ini"), QString()};
        const QJsonObject o = settingsMessage(info);
        QCOMPARE(QString::fromUtf8(encodeMessage(o)),
                 QStringLiteral("{\"camera_enabled\":false,\"camera_host\":\"\",\"camera_port\":8093," // P15 d2
                                "\"file\":\"/tmp/kern.ini\",\"home_address\":\"192.168.1.20\","
                                "\"obs_enabled\":false,\"obs_host\":\"\",\"obs_password_set\":false,"
                                "\"obs_port\":4455,\"port\":8081,"
                                "\"port_max\":32767,\"port_min\":1024,\"settings_error\":null,"
                                // P13
                                "\"twitch_client_id\":\"\",\"twitch_client_secret_set\":false,\"twitch_enabled\":false,"
                                "\"type\":\"settings\"}\n")
                     .insert(1, QStringLiteral("\"audio_offset_max_ms\":500,\"audio_offset_min_ms\":-500,"
                                               "\"audio_routes\":{\"aktionen\":{\"offset_ms\":0,\"output\":\"\"},"
                                               "\"video\":{\"offset_ms\":0,\"output\":\"\"}},")));
        SettingsInfo back;
        QString error;
        QVERIFY2(parseSettings(o, &back, &error), qPrintable(error));
        QCOMPARE(back, info);
        QJsonObject bad = o;
        bad.insert(QStringLiteral("settings_error"), 3);
        QVERIFY(!parseSettings(bad, &back, &error));
        bad = o;
        bad.remove(QStringLiteral("home_address"));
        QVERIFY(!parseSettings(bad, &back, &error));

        // P12b b3: Ausgang und Versatz je Zweck, Bereich des Versatzes
        SettingsInfo audio = info;
        audio.settings.audioRoutes[QStringLiteral("video")] = {QStringLiteral("test_ausgang"), -120};
        QVERIFY2(parseSettings(settingsMessage(audio), &back, &error), qPrintable(error));
        QCOMPARE(back, audio);
        QCOMPARE(settingsMessage(audio).value(QStringLiteral("audio_routes")).toObject(),
                 (QJsonObject{{QStringLiteral("aktionen"), QJsonObject{{QStringLiteral("output"), QString()},
                                                                       {QStringLiteral("offset_ms"), 0}}},
                              {QStringLiteral("video"), QJsonObject{{QStringLiteral("output"), QStringLiteral("test_ausgang")},
                                                                    {QStringLiteral("offset_ms"), -120}}}}));
        for (const QString &field : {QStringLiteral("audio_routes"), QStringLiteral("audio_offset_min_ms"),
                                     QStringLiteral("audio_offset_max_ms")}) {
            bad = o;
            bad.remove(field);
            QVERIFY2(!parseSettings(bad, &back, &error), qPrintable(field));
            QVERIFY2(error.contains(field), qPrintable(error));
        }
        bad = o;
        bad.insert(QStringLiteral("audio_offset_min_ms"), 501);
        QVERIFY(!parseSettings(bad, &back, &error));
        bad = o;
        bad.insert(QStringLiteral("audio_routes"), QJsonObject{{QStringLiteral("video"), 3}});
        QVERIFY(!parseSettings(bad, &back, &error));
        bad.insert(QStringLiteral("audio_routes"),
                   QJsonObject{{QStringLiteral("video"), QJsonObject{{QStringLiteral("output"), QStringLiteral("a")}}}});
        QVERIFY(!parseSettings(bad, &back, &error)); // offset_ms fehlt
        bad.insert(QStringLiteral("audio_routes"),
                   QJsonObject{{QStringLiteral("video"), QJsonObject{{QStringLiteral("output"), 1},
                                                                     {QStringLiteral("offset_ms"), 0}}}});
        QVERIFY(!parseSettings(bad, &back, &error));

        // P13: Twitch; das Secret geht nur hinein, zurück kommt «gesetzt»
        SettingsInfo twitch = info;
        twitch.settings.twitchEnabled = true;
        twitch.settings.twitchClientId = QStringLiteral("testclient");
        twitch.twitchClientSecretSet = true;
        QVERIFY2(parseSettings(settingsMessage(twitch), &back, &error), qPrintable(error));
        QCOMPARE(back, twitch);
        for (const QString &field : {QStringLiteral("twitch_enabled"), QStringLiteral("twitch_client_id"),
                                     QStringLiteral("twitch_client_secret_set")}) {
            bad = o;
            bad.remove(field);
            QVERIFY2(!parseSettings(bad, &back, &error), qPrintable(field));
        }
        SettingsUpdate twitchUpdate;
        twitchUpdate.requestId = 8;
        twitchUpdate.twitchEnabled = false;
        twitchUpdate.twitchClientId = QStringLiteral("abc");
        twitchUpdate.twitchClientSecret = QString();
        SettingsUpdate twitchParsed;
        QVERIFY2(parseSettingsUpdate(settingsUpdateMessage(twitchUpdate), &twitchParsed, &error), qPrintable(error));
        QCOMPARE(twitchParsed, twitchUpdate);
        twitchUpdate.twitchEnabled.reset();
        twitchUpdate.twitchClientId.reset();
        QVERIFY2(parseSettingsUpdate(settingsUpdateMessage(twitchUpdate), &twitchParsed, &error), qPrintable(error));
        QCOMPARE(twitchParsed, twitchUpdate);

        // P15 d2: Kamera-Dienst
        SettingsInfo camera = info;
        camera.settings.cameraEnabled = true;
        camera.settings.cameraHost = QStringLiteral("192.168.1.30");
        camera.settings.cameraPort = 9000;
        QVERIFY2(parseSettings(settingsMessage(camera), &back, &error), qPrintable(error));
        QCOMPARE(back, camera);
        for (const QString &field : {QStringLiteral("camera_enabled"), QStringLiteral("camera_host"),
                                     QStringLiteral("camera_port")}) {
            bad = o;
            bad.remove(field);
            QVERIFY2(!parseSettings(bad, &back, &error), qPrintable(field));
        }
        SettingsUpdate cameraUpdate;
        cameraUpdate.requestId = 9;
        cameraUpdate.cameraEnabled = true;
        cameraUpdate.cameraHost = QStringLiteral("192.168.1.30");
        cameraUpdate.cameraPort = 8093;
        SettingsUpdate cameraParsed;
        QVERIFY2(parseSettingsUpdate(settingsUpdateMessage(cameraUpdate), &cameraParsed, &error), qPrintable(error));
        QCOMPARE(cameraParsed, cameraUpdate);
        cameraUpdate.cameraEnabled.reset();
        cameraUpdate.cameraHost.reset();
        QVERIFY2(parseSettingsUpdate(settingsUpdateMessage(cameraUpdate), &cameraParsed, &error), qPrintable(error));
        QCOMPARE(cameraParsed, cameraUpdate);
        QJsonObject badUpdate = settingsUpdateMessage(cameraUpdate);
        badUpdate.insert(QStringLiteral("camera_port"), QStringLiteral("8093"));
        QVERIFY(!parseSettingsUpdate(badUpdate, &cameraParsed, &error));

        SettingsUpdate update;
        update.requestId = 7;
        update.port = 8090;
        QCOMPARE(QString::fromUtf8(encodeMessage(settingsUpdateMessage(update))),
                 QStringLiteral("{\"port\":8090,\"request_id\":7,\"type\":\"update_settings\"}\n"));
        SettingsUpdate parsed;
        QVERIFY2(parseSettingsUpdate(settingsUpdateMessage(update), &parsed, &error), qPrintable(error));
        QCOMPARE(parsed, update);
        update.port.reset();
        update.homeAddress = QString();
        QVERIFY(parseSettingsUpdate(settingsUpdateMessage(update), &parsed, &error));
        QCOMPARE(parsed.homeAddress, std::optional<QString>(QString()));
        // ohne Wert, Port keine ganze Zahl
        QVERIFY(!parseSettingsUpdate(QJsonObject{{QStringLiteral("type"), QStringLiteral("update_settings")},
                                                 {QStringLiteral("request_id"), 1}},
                                     &parsed, &error));
        QVERIFY(!parseSettingsUpdate(QJsonObject{{QStringLiteral("request_id"), 1},
                                                 {QStringLiteral("port"), 80.5}},
                                     &parsed, &error));
        QVERIFY(!parseSettingsUpdate(QJsonObject{{QStringLiteral("request_id"), 1},
                                                 {QStringLiteral("home_address"), 1}},
                                     &parsed, &error));
        // Bereich prüft erst der Kern
        QVERIFY(parseSettingsUpdate(QJsonObject{{QStringLiteral("request_id"), 1},
                                                {QStringLiteral("port"), 80}},
                                    &parsed, &error));

        // P12b b3: audio_routes, je Zweck output und/oder offset_ms
        SettingsUpdate audioUpdate;
        audioUpdate.requestId = 8;
        audioUpdate.audioRoutes.insert(QStringLiteral("video"), AudioRouteChange{QStringLiteral("test_ausgang"), std::nullopt});
        QCOMPARE(QString::fromUtf8(encodeMessage(settingsUpdateMessage(audioUpdate))),
                 QStringLiteral("{\"audio_routes\":{\"video\":{\"output\":\"test_ausgang\"}},\"request_id\":8,"
                                "\"type\":\"update_settings\"}\n"));
        QVERIFY2(parseSettingsUpdate(settingsUpdateMessage(audioUpdate), &parsed, &error), qPrintable(error));
        QCOMPARE(parsed, audioUpdate);
        audioUpdate.audioRoutes[QStringLiteral("video")] = AudioRouteChange{std::nullopt, -500};
        QVERIFY2(parseSettingsUpdate(settingsUpdateMessage(audioUpdate), &parsed, &error), qPrintable(error));
        QCOMPARE(parsed, audioUpdate);
        audioUpdate.audioRoutes[QStringLiteral("video")] = AudioRouteChange{QString(), 0};
        audioUpdate.port = 9000;
        QVERIFY2(parseSettingsUpdate(settingsUpdateMessage(audioUpdate), &parsed, &error), qPrintable(error));
        QCOMPARE(parsed, audioUpdate);
        // Unbekannte Zwecke und Bereich prüft erst der Kern
        QVERIFY(parseSettingsUpdate(QJsonObject{{QStringLiteral("request_id"), 1},
                                                {QStringLiteral("audio_routes"),
                                                 QJsonObject{{QStringLiteral("musik"),
                                                              QJsonObject{{QStringLiteral("offset_ms"), 9999}}}}}},
                                    &parsed, &error));
        const QList<QJsonValue> badRoutes = {
            QJsonValue(3),
            QJsonValue(QJsonObject()), // leer
            QJsonObject{{QStringLiteral("video"), 3}},
            QJsonObject{{QStringLiteral("video"), QJsonObject()}}, // ohne output und offset_ms
            QJsonObject{{QStringLiteral("video"), QJsonObject{{QStringLiteral("output"), 1}}}},
            QJsonObject{{QStringLiteral("video"), QJsonObject{{QStringLiteral("offset_ms"), 1.5}}}},
            QJsonObject{{QStringLiteral("video"), QJsonObject{{QStringLiteral("offset_ms"), QStringLiteral("1")}}}},
        };
        for (const QJsonValue &routes : badRoutes) {
            QVERIFY2(!parseSettingsUpdate(QJsonObject{{QStringLiteral("request_id"), 1},
                                                      {QStringLiteral("audio_routes"), routes}},
                                          &parsed, &error),
                     QJsonDocument(QJsonObject{{QStringLiteral("r"), routes}}).toJson(QJsonDocument::Compact).constData());
        }
    }

    // P12b b3: Ausgänge für den Ton und Zustand je Zweck
    void audioOutputsMessages()
    {
        AudioOutputsInfo info;
        info.enabled = true;
        info.devices = {{QStringLiteral("test_ausgang.analog-stereo"), QStringLiteral("Testausgang"), true},
                        {QStringLiteral("test_pult"), QStringLiteral("Pult für Tests"), false}};
        info.routes.insert(QStringLiteral("video"), {QStringLiteral("missing"), QString()});
        const QJsonObject o = audioOutputsMessage(info);
        QCOMPARE(QString::fromUtf8(encodeMessage(o)),
                 QStringLiteral("{\"devices\":[{\"default\":true,\"description\":\"Testausgang\","
                                "\"name\":\"test_ausgang.analog-stereo\"},{\"default\":false,"
                                "\"description\":\"Pult für Tests\",\"name\":\"test_pult\"}],\"enabled\":true,"
                                "\"routes\":{\"video\":{\"detail\":null,\"state\":\"missing\"}},"
                                "\"type\":\"audio_outputs\"}\n"));
        AudioOutputsInfo back;
        QString error;
        QVERIFY2(parseAudioOutputs(o, &back, &error), qPrintable(error));
        QCOMPARE(back, info);
        // Jeder Zustand hin und zurück, mit Grund
        QCOMPARE(audioRouteStates(), (QStringList{QStringLiteral("no_pipewire"), QStringLiteral("no_target"),
                                                  QStringLiteral("missing"), QStringLiteral("connecting"),
                                                  QStringLiteral("playing"), QStringLiteral("error")}));
        for (const QString &state : audioRouteStates()) {
            info.routes[QStringLiteral("video")] = {state, QStringLiteral("Grund %1").arg(state)};
            QVERIFY2(parseAudioOutputs(audioOutputsMessage(info), &back, &error), qPrintable(error));
            QCOMPARE(back, info);
        }
        // Kern ohne Ton (DSS_CORE_AUDIO aus)
        QCOMPARE(QString::fromUtf8(encodeMessage(audioOutputsMessage({}))),
                 QStringLiteral("{\"devices\":[],\"enabled\":false,\"routes\":{},\"type\":\"audio_outputs\"}\n"));
        QVERIFY(parseAudioOutputs(audioOutputsMessage({}), &back, &error));
        QCOMPARE(back, AudioOutputsInfo());

        const auto rejected = [&](const char *field, const QJsonValue &value) {
            QJsonObject bad = o;
            bad.insert(QLatin1String(field), value);
            return !parseAudioOutputs(bad, &back, &error);
        };
        QVERIFY(rejected("enabled", 1));
        QVERIFY(rejected("devices", QJsonObject()));
        QVERIFY(rejected("devices", QJsonArray{3}));
        QVERIFY(rejected("devices", QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("a")},
                                                           {QStringLiteral("description"), QStringLiteral("b")}}}));
        QVERIFY(rejected("routes", QJsonArray()));
        QVERIFY(rejected("routes", QJsonObject{{QStringLiteral("video"), QJsonObject{{QStringLiteral("state"),
                                                                                     QStringLiteral("laut")},
                                                                                    {QStringLiteral("detail"), QJsonValue::Null}}}}));
        QVERIFY(error.contains(QStringLiteral("laut")));
        QVERIFY(rejected("routes", QJsonObject{{QStringLiteral("video"), QJsonObject{{QStringLiteral("state"),
                                                                                     QStringLiteral("playing")}}}}));
        QVERIFY(rejected("routes", QJsonObject{{QStringLiteral("video"), QJsonObject{{QStringLiteral("state"),
                                                                                     QStringLiteral("playing")},
                                                                                    {QStringLiteral("detail"), 1}}}}));
        QVERIFY(rejected("routes", QJsonObject{{QStringLiteral("video"), 1}}));
        QJsonObject missing = o;
        missing.remove(QStringLiteral("routes"));
        QVERIFY(!parseAudioOutputs(missing, &back, &error));
    }

    // P10: Adresse der Browserquelle
    void browserSourceMessages()
    {
        QCOMPARE(browserSourceUrl(QStringLiteral("192.168.1.20"), 8081, QStringLiteral("ueberblick"),
                                  QStringLiteral("abc")),
                 QStringLiteral("http://192.168.1.20:8081/o/ueberblick/abc/"));
        QCOMPARE(browserSourceLogPath(QStringLiteral("ueberblick")), QStringLiteral("/o/ueberblick/…"));

        const QUuid id = QUuid::fromString(QStringLiteral("2b0b3c39-4d0c-4c39-9a0e-6f1f0a3c1d11"));
        const BrowserSourceInfo info{id, QStringLiteral("http://192.168.1.20:8081/o/a/b/"), QString()};
        QJsonObject o = browserSourceMessage(info);
        QCOMPARE(QString::fromUtf8(encodeMessage(o)),
                 QStringLiteral("{\"overlay_id\":\"2b0b3c39-4d0c-4c39-9a0e-6f1f0a3c1d11\",\"problem\":null,"
                                "\"type\":\"browser_source\",\"url\":\"http://192.168.1.20:8081/o/a/b/\"}\n"));
        BrowserSourceInfo back;
        QString error;
        QVERIFY2(parseBrowserSource(o, &back, &error), qPrintable(error));
        QCOMPARE(back, info);
        const BrowserSourceInfo problem{id, QString(), QStringLiteral("Keine Adresse")};
        QVERIFY(parseBrowserSource(browserSourceMessage(problem), &back, &error));
        QCOMPARE(back, problem);
        // genau eines von url und problem
        o.insert(QStringLiteral("problem"), QStringLiteral("x"));
        QVERIFY(!parseBrowserSource(o, &back, &error));
        o.insert(QStringLiteral("url"), QJsonValue::Null);
        o.insert(QStringLiteral("problem"), QJsonValue::Null);
        QVERIFY(!parseBrowserSource(o, &back, &error));
        o.remove(QStringLiteral("problem"));
        QVERIFY(!parseBrowserSource(o, &back, &error));

        QUuid parsed;
        QVERIFY(parseGetBrowserSource(getBrowserSourceMessage(id), &parsed, &error));
        QCOMPARE(parsed, id);
        QCOMPARE(QString::fromUtf8(encodeMessage(getBrowserSourceMessage(QUuid()))),
                 QStringLiteral("{\"overlay_id\":null,\"type\":\"get_browser_source\"}\n"));
        QVERIFY(parseGetBrowserSource(getBrowserSourceMessage(QUuid()), &parsed, &error));
        QVERIFY(parsed.isNull());
        QVERIFY(!parseGetBrowserSource(simpleMessage(msg::kGetBrowserSource), &parsed, &error));
    }

    // P13: Ereignisfluss (subscribe_events, event_history, event, events_dropped)
    void eventMessages()
    {
        const SubscribeEvents request{{QStringLiteral("twitch."), QStringLiteral("obs.scene_changed")}, true};
        QJsonObject o = roundTrip(subscribeEventsMessage(request));
        QCOMPARE(messageType(o), QStringLiteral("subscribe_events"));
        SubscribeEvents backRequest;
        QString error;
        QVERIFY2(parseSubscribeEvents(o, &backRequest, &error), qPrintable(error));
        QCOMPARE(backRequest, request);
        // Leer = abbestellen
        QVERIFY(parseSubscribeEvents(subscribeEventsMessage({}), &backRequest, &error));
        QCOMPARE(backRequest, SubscribeEvents());
        // Filter gegen die Typenliste
        for (const QString &bad : {QStringLiteral("twitch"), QStringLiteral("twitch.gibtsnicht"),
                                   QStringLiteral("youtube."), QString()}) {
            QVERIFY2(!parseSubscribeEvents(subscribeEventsMessage({{bad}, false}), &backRequest, &error),
                     qPrintable(bad));
        }
        QVERIFY(error.contains(QStringLiteral("Unbekannter Ereignistyp")));
        o.remove(QStringLiteral("history"));
        QVERIFY(!parseSubscribeEvents(o, &backRequest, &error));
        o = subscribeEventsMessage(request);
        o.insert(QStringLiteral("types"), QJsonArray{1});
        QVERIFY(!parseSubscribeEvents(o, &backRequest, &error));

        events::Event event;
        event.id = QUuid::fromString(QStringLiteral("3f1c2a4e-5b6d-4e7f-8a9b-0c1d2e3f4a5b"));
        event.type = QStringLiteral("obs.scene_changed");
        event.origin = QStringLiteral("obs");
        event.time = QDateTime(QDate(2026, 11, 15), QTime(14, 3, 7, 125), QTimeZone::UTC);
        event.data = QJsonObject{{QStringLiteral("scene_uuid"), QStringLiteral("u-1")},
                                 {QStringLiteral("scene_name"), QStringLiteral("Szene «test 2»")}};
        o = roundTrip(eventMessage(event));
        QCOMPARE(messageType(o), QStringLiteral("event"));
        QCOMPARE(QJsonDocument(o.value(QStringLiteral("event")).toObject()).toJson(QJsonDocument::Compact),
                 QByteArray("{\"data\":{\"scene_name\":\"Szene «test 2»\",\"scene_uuid\":\"u-1\"},"
                            "\"id\":\"3f1c2a4e-5b6d-4e7f-8a9b-0c1d2e3f4a5b\",\"origin\":\"obs\","
                            "\"origin_id\":\"\",\"time\":\"2026-11-15T14:03:07.125Z\",\"type\":\"obs.scene_changed\"}"));
        events::Event back;
        QVERIFY2(parseEventMessage(o, &back, &error), qPrintable(error));
        QCOMPARE(back, event);
        QVERIFY(!parseEventMessage(simpleMessage(msg::kEvent), &back, &error));

        events::Event second = event;
        second.id = QUuid::createUuid();
        second.originId = QStringLiteral("abc");
        o = roundTrip(eventHistoryMessage({event, second}));
        QCOMPARE(messageType(o), QStringLiteral("event_history"));
        QList<events::Event> history;
        QVERIFY2(parseEventHistory(o, &history, &error), qPrintable(error));
        QCOMPARE(history, (QList<events::Event>{event, second}));
        QVERIFY(parseEventHistory(eventHistoryMessage({}), &history, &error));
        QVERIFY(history.isEmpty());
        o.insert(QStringLiteral("events"), QJsonArray{1});
        QVERIFY(!parseEventHistory(o, &history, &error));

        o = roundTrip(eventsDroppedMessage(17));
        QCOMPARE(messageType(o), QStringLiteral("events_dropped"));
        qint64 count = 0;
        QVERIFY2(parseEventsDropped(o, &count, &error), qPrintable(error));
        QCOMPARE(count, 17);
        QVERIFY(!parseEventsDropped(eventsDroppedMessage(0), &count, &error));
        QCOMPARE(kEventBacklogBytes, 1024 * 1024);
    }

    // P10, Punkt 77
    void resetSettingsMessage()
    {
        const QJsonObject o = dss::resetSettingsMessage(12);
        QCOMPARE(messageType(o), QStringLiteral("reset_settings"));
        qint64 id = 0;
        QString error;
        QVERIFY(parseResetSettings(o, &id, &error));
        QCOMPARE(id, 12);
        QVERIFY(!parseResetSettings(QJsonObject{{QStringLiteral("type"), QStringLiteral("reset_settings")}}, &id, &error));
        QVERIFY(error.contains(QStringLiteral("request_id")));
    }

    void serverStatusMessages()
    {
        ServerStatusInfo info;
        info.enabled = true;
        info.listening = {QStringLiteral("127.0.0.1:8081"), QStringLiteral("192.168.1.20:8081")};
        info.problems = {QStringLiteral("Port 8081 ist belegt")};
        info.invalidUse = QStringLiteral("Eine Browserquelle benutzt eine ungültige Adresse");
        info.pages.insert(QUuid::fromString(QStringLiteral("2b0b3c39-4d0c-4c39-9a0e-6f1f0a3c1d11")), 2);
        const QJsonObject o = serverStatusMessage(info);
        ServerStatusInfo back;
        QString error;
        QVERIFY2(parseServerStatus(o, &back, &error), qPrintable(error));
        QCOMPARE(back, info);
        QVERIFY(parseServerStatus(serverStatusMessage(ServerStatusInfo()), &back, &error));
        QCOMPARE(back, ServerStatusInfo());
        QCOMPARE(serverStatusMessage(ServerStatusInfo()).value(QStringLiteral("invalid_use")), QJsonValue(QJsonValue::Null));
        QJsonObject bad = o;
        bad.insert(QStringLiteral("problems"), QJsonArray{1});
        QVERIFY(!parseServerStatus(bad, &back, &error));
        bad = o;
        bad.insert(QStringLiteral("pages"), QJsonObject{{QStringLiteral("keine-kennung"), 1}});
        QVERIFY(!parseServerStatus(bad, &back, &error));
        bad = o;
        bad.remove(QStringLiteral("enabled"));
        QVERIFY(!parseServerStatus(bad, &back, &error));
    }

    // P10b: media_list und import_progress hin und zurück, null für leere Angaben
    void mediaListMessages()
    {
        MediaListData d;
        d.folders = {{QUuid::createUuid(), QStringLiteral("Logos"), QUuid(), true}};
        d.folders.append({QUuid::createUuid(), QStringLiteral("Alt"), d.folders.first().id, false});
        MediaFileData image;
        image.entry = {QStringLiteral("media/") + QString(64, QLatin1Char('a')) + QStringLiteral(".png"),
                       QStringLiteral("logo «neu».png"), d.folders.at(1).id};
        image.kind = MediaKind::Image;
        image.bytes = 19776;
        image.usedIn = {{QUuid::createUuid(), QStringLiteral("Überblick"), QUuid::createUuid(),
                         QStringLiteral("logo.png")}};
        MediaFileData video;
        video.entry = {QStringLiteral("media/") + QString(64, QLatin1Char('b')) + QStringLiteral(".mp4"),
                       QStringLiteral("zaehler.mp4"), QUuid()};
        video.kind = MediaKind::Video;
        video.bytes = qint64(3) << 32; // über 4 GiB
        video.codec = QStringLiteral("hevc");
        video.browserProblem = QStringLiteral("zaehler.mp4 (HEVC) wird in der Browserquelle nicht angezeigt.");
        // P14 t: Ton mit Länge
        MediaFileData sound;
        sound.entry = {QStringLiteral("media/") + QString(64, QLatin1Char('c')) + QStringLiteral(".mp3"),
                       QStringLiteral("glocke.mp3"), QUuid()};
        sound.kind = MediaKind::Audio;
        sound.bytes = 4096;
        sound.durationMs = 2450;
        d.files = {image, video, sound};
        d.error = QStringLiteral("medien.json: kaputt");
        const QJsonObject o = roundTrip(mediaListMessage(d));
        QCOMPARE(messageType(o), QStringLiteral("media_list"));
        MediaListData back;
        QString error;
        QVERIFY2(parseMediaList(o, &back, &error), qPrintable(error));
        QCOMPARE(back, d);
        QVERIFY(back.file(video.entry.file));
        QCOMPARE(back.file(video.entry.file)->codec, QStringLiteral("hevc"));
        // Leere Angaben als null
        const QJsonArray files = o.value(QStringLiteral("files")).toArray();
        QVERIFY(files.at(0).toObject().value(QStringLiteral("codec")).isNull());
        QVERIFY(files.at(0).toObject().value(QStringLiteral("browser_problem")).isNull());
        QVERIFY(files.at(1).toObject().value(QStringLiteral("folder")).isNull());
        QVERIFY(files.at(0).toObject().value(QStringLiteral("duration_ms")).isNull());
        QCOMPARE(files.at(2).toObject().value(QStringLiteral("kind")).toString(), QStringLiteral("audio"));
        QCOMPARE(files.at(2).toObject().value(QStringLiteral("duration_ms")).toInt(), 2450);
        d.error.clear();
        QVERIFY(mediaListMessage(d).value(QStringLiteral("error")).isNull());
        // Unbekannte Art
        QJsonObject bad = o;
        QJsonArray badFiles = files;
        QJsonObject f = badFiles.at(0).toObject();
        f.insert(QStringLiteral("kind"), QStringLiteral("musik"));
        badFiles[0] = f;
        bad.insert(QStringLiteral("files"), badFiles);
        QVERIFY(!parseMediaList(bad, &back, &error));
        // Länge fehlt oder ist 0
        for (const QJsonValue &duration : {QJsonValue(QJsonValue::Undefined), QJsonValue(0)}) {
            QJsonObject s = files.at(2).toObject();
            if (duration.isUndefined())
                s.remove(QStringLiteral("duration_ms"));
            else
                s.insert(QStringLiteral("duration_ms"), duration);
            badFiles = files;
            badFiles[2] = s;
            bad.insert(QStringLiteral("files"), badFiles);
            QVERIFY(!parseMediaList(bad, &back, &error));
        }

        const QList<ImportProgressData> imports = {
            {1, QStringLiteral("gross.webm"), d.folders.first().id, 42},
            {2, QStringLiteral("klein.png"), QUuid(), 100}};
        const QJsonObject p = roundTrip(importProgressMessage(imports));
        QCOMPARE(messageType(p), QStringLiteral("import_progress"));
        QList<ImportProgressData> backImports;
        QVERIFY2(parseImportProgress(p, &backImports, &error), qPrintable(error));
        QCOMPARE(backImports, imports);
        QJsonObject over = importProgressMessage({{1, QStringLiteral("x.png"), QUuid(), 100}});
        QJsonArray list = over.value(QStringLiteral("imports")).toArray();
        QJsonObject entry = list.at(0).toObject();
        entry.insert(QStringLiteral("percent"), 101);
        list[0] = entry;
        over.insert(QStringLiteral("imports"), list);
        QVERIFY(!parseImportProgress(over, &backImports, &error));
    }

    // e2-4b: actions_status meldet den Hauptschalter (enabled) in jedem Zustand, auch bei «stumm»
    void actionsStatusEnabled()
    {
        for (const bool enabled : {true, false}) {
            ActionsStatus status;
            status.state = QStringLiteral("muted");
            status.message = QStringLiteral("stumm: Version im Einsatz läuft");
            status.enabled = enabled;
            const QJsonObject message = roundTrip(actionsStatusMessage(status));
            QCOMPARE(message.value(QStringLiteral("enabled")), QJsonValue(enabled));
            ActionsStatus back;
            QString error;
            QVERIFY2(parseActionsStatus(message, &back, &error), qPrintable(error));
            QCOMPARE(back, status);
        }
    }
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "tst_protocol.moc"
