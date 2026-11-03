// Prüft Lesen und Schreiben aller Nachrichten der Schnittstelle Kern–Fenster ohne Qt Gui
// (P07, Teil D; docs/08-kern-schnittstelle.md).

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
        QCOMPARE(o.value(QStringLiteral("protocol_version")).toInt(), 4); // seit P09b
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
             QStringLiteral("Chat Ecke"), QStringLiteral("chat-ecke"), 800, 600, 0}};
        QCOMPARE(list.first().sourceCount, 2);
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
};

QTEST_GUILESS_MAIN(TestProtocol)
#include "tst_protocol.moc"
