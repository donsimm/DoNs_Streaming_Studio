// P10, Teil A: Overlay-Server ohne Fenster (Qt Core und Qt Network, CLAUDE.md Regel 9).
// Geprüft: Token (gleichbleibende Laufzeit, falsch, alt), leere Seite für alte und unbekannte
// Adressen, Protokoll ohne Token, Änderungen über Server-Sent Events, Medien nur des Overlays mit
// Bereichen und neutralisierter Ausrichtung, Port belegt (Meldung mit Programm), Adresse fehlt
// (nur 127.0.0.1, später von selbst), nur IPv4 (ss -ltn), Meldungen der Seite.
// Daten: Bilder und Videos aus tests/daten/exif/ und tests/daten/video-drehung/ (wie im Handtest),
// WebM mit Drehangabe aus tests/daten/bildvergleich/ (P11, Punkt 80).

#include "bildserver.h"
#include "browsersource.h"
#include "httpmessage.h"
#include "imageorientation.h"
#include "mediapatch.h"
#include "overlaylivestate.h"
#include "overlayserver.h"
#include "overlaystore.h"
#include "mediainfo.h"
#include "portowner.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QtEndian>

#include <chrono>

#include <unistd.h>

using namespace dss;

namespace {

const QString kExif = QStringLiteral(DSS_TEST_EXIF_DIR);
const QString kVideos = QStringLiteral(DSS_TEST_VIDEO_ROTATION_DIR);
const QString kBildvergleich = QStringLiteral(DSS_TEST_BILDVERGLEICH_DIR);
const QString kWeb = QStringLiteral(DSS_WEB_DIR);

// Protokoll des Tests mitlesen (Token darf nie darin stehen).
QStringList g_log;
QtMessageHandler g_previous = nullptr;
void collect(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    g_log.append(message);
    g_previous(type, context, message);
}

struct Response
{
    int status = 0;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

// Einfacher Client mit Ereignisschleife (der Server läuft im selben Thread).
Response request(quint16 port, const QByteArray &method, const QByteArray &path,
                 const QList<QByteArray> &headers = {}, const QByteArray &body = {},
                 const QString &host = QStringLiteral("127.0.0.1"))
{
    QTcpSocket s;
    s.connectToHost(host, port);
    Response r;
    if (!QTest::qWaitFor([&] { return s.state() == QAbstractSocket::ConnectedState; }, 3000))
        return r;
    QByteArray req = method + ' ' + path + " HTTP/1.1\r\nHost: test\r\nConnection: close\r\n";
    for (const QByteArray &h : headers)
        req += h + "\r\n";
    if (!body.isEmpty())
        req += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    req += "\r\n" + body;
    s.write(req);
    QByteArray all;
    static_cast<void>(QTest::qWaitFor([&] {
        all += s.readAll();
        return s.state() == QAbstractSocket::UnconnectedState;
    }, 5000));
    all += s.readAll();
    const qsizetype end = all.indexOf("\r\n\r\n");
    if (end < 0)
        return r;
    const QList<QByteArray> lines = all.left(end).split('\n');
    r.status = lines.first().split(' ').value(1).toInt();
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const qsizetype colon = lines.at(i).indexOf(':');
        r.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
    }
    r.body = all.mid(end + 4);
    return r;
}

// Offene Verbindung für Server-Sent Events.
class EventClient
{
public:
    EventClient(quint16 port, const QByteArray &path)
    {
        m_socket.connectToHost(QStringLiteral("127.0.0.1"), port);
        static_cast<void>(QTest::qWaitFor([&] { return m_socket.state() == QAbstractSocket::ConnectedState; }, 3000));
        m_socket.write("GET " + path + " HTTP/1.1\r\nHost: test\r\nAccept: text/event-stream\r\n\r\n");
    }
    // Nächstes Ereignis (Name, Daten); leer nach timeoutMs.
    std::pair<QByteArray, QByteArray> next(int timeoutMs = 3000)
    {
        std::pair<QByteArray, QByteArray> result;
        static_cast<void>(QTest::qWaitFor([&] {
            m_buffer += m_socket.readAll();
            for (;;) {
                const qsizetype end = m_buffer.indexOf("\n\n");
                if (end < 0)
                    return false;
                const QByteArray block = m_buffer.left(end);
                m_buffer.remove(0, end + 2);
                QByteArray name;
                QByteArray data;
                for (const QByteArray &line : block.split('\n')) {
                    if (line.startsWith("event: "))
                        name = line.mid(7);
                    else if (line.startsWith("data: "))
                        data = line.mid(6);
                }
                if (!name.isEmpty()) {
                    result = {name, data};
                    return true;
                }
            }
        }, timeoutMs));
        return result;
    }
    bool closed() const { return m_socket.state() == QAbstractSocket::UnconnectedState; }
    QByteArray head() const { return m_buffer; }

private:
    QTcpSocket m_socket;
    QByteArray m_buffer;
};

qint64 steadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Freier Port im erlaubten Bereich.
quint16 freePort()
{
    for (quint16 p = 20000 + quint16(getpid() % 5000); p < 32000; ++p) {
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, p))
            return p;
    }
    return 0;
}

} // namespace

class TestOverlayServer : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    std::unique_ptr<OverlayStore> m_store;
    std::unique_ptr<OverlayServer> m_server;
    quint16 m_port = 0;
    QUuid m_id;
    QUuid m_textId;
    QList<QHostAddress> m_local = {QHostAddress(QHostAddress::LocalHost), QHostAddress(QStringLiteral("127.0.0.2"))};

    QByteArray base() const
    {
        const Overlay *o = m_store->overlay(m_id);
        return browserSourcePath(o->address, m_store->token(m_id)).toUtf8();
    }
    QString token() const { return m_store->token(m_id); }

    // P12b b4: Videoquelle des Overlays und eine gültige Meldung der Uhr (clockreport.h)
    Source videoSource() const { return m_store->overlay(m_id)->sources.at(2); }
    QJsonObject clockMessage(const char *page, qint64 seq, const char *event = "frame") const
    {
        const Source video = videoSource();
        return {{QStringLiteral("v"), 1},
                {QStringLiteral("page"), QLatin1String(page)},
                {QStringLiteral("seq"), seq},
                {QStringLiteral("event"), QLatin1String(event)},
                {QStringLiteral("run"), 2},
                {QStringLiteral("source"), video.id.toString(QUuid::WithoutBraces)},
                {QStringLiteral("file"), video.file},
                {QStringLiteral("loop"), 1},
                {QStringLiteral("media_s"), 1.25},
                {QStringLiteral("display_ms"), 81234.5},
                {QStringLiteral("now_ms"), 81236.25},
                {QStringLiteral("duration_s"), 3.0}};
    }
    static QByteArray clockBody(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
    bool logHasToken() const
    {
        for (const QString &line : g_log) {
            for (const Overlay &o : m_store->overlays()) {
                if (line.contains(m_store->token(o.id)))
                    return true;
            }
        }
        return false;
    }

    std::unique_ptr<OverlayServer> makeServer(const std::function<QList<QHostAddress>()> &local)
    {
        OverlayServer::Options o;
        o.webDir = kWeb;
        o.installedDir = QDir::homePath() + QStringLiteral("/.local/opt/dons-streaming-studio");
        o.retryIntervalMs = 100;
        o.heartbeatMs = 200;
        o.invalidHintMs = 1000;
        o.localAddresses = local;
        o.videoOrientation = readVideoOrientation; // wie im Kern (dss_media)
        return std::make_unique<OverlayServer>(m_store.get(), o);
    }

private slots:
    void initTestCase()
    {
        g_previous = qInstallMessageHandler(collect);
        m_store = std::make_unique<OverlayStore>(m_data.path());
        QString error;
        QVERIFY(m_store->load(&error));
        m_id = m_store->createOverlay(QStringLiteral("Probe Seite"), 640, 360, &error);
        QVERIFY2(!m_id.isNull(), qPrintable(error));
        QVERIFY(!m_store->addImageSource(m_id, kExif + QStringLiteral("/orientierung-6.jpg"), &error).isNull());
        QVERIFY(!m_store->addImageSource(m_id, kExif + QStringLiteral("/orientierung-6.png"), &error).isNull());
        QVERIFY(!m_store->addVideoSource(m_id, kVideos + QStringLiteral("/drehung-90.mp4"), true, &error).isNull());
        m_textId = m_store->addTextSource(m_id, QStringLiteral("Hallo"), QStringLiteral(DSS_TEST_FONT), &error);
        QVERIFY(!m_textId.isNull());
        // zweites Overlay: seine Dateien darf die Seite des ersten nicht laden
        const QUuid other = m_store->createOverlay(QStringLiteral("Andere"), 100, 100, &error);
        QVERIFY(!m_store->addImageSource(other, kExif + QStringLiteral("/ohne-exif.jpg"), &error).isNull());
        m_port = freePort();
        QVERIFY(m_port > 0);
        m_server = makeServer([this] { return m_local; });
        m_server->configure(m_port, QStringLiteral("127.0.0.2"));
        QCOMPARE(m_server->status().listening,
                 (QStringList{QStringLiteral("127.0.0.1:%1").arg(m_port), QStringLiteral("127.0.0.2:%1").arg(m_port)}));
        QVERIFY(m_server->status().problems.isEmpty());
    }

    void cleanupTestCase()
    {
        m_server.reset();
        qInstallMessageHandler(g_previous);
    }

    void constantTimeCompare()
    {
        QVERIFY(constantTimeEquals("abc", "abc"));
        QVERIFY(!constantTimeEquals("abc", "abd"));
        QVERIFY(!constantTimeEquals("abc", "ab"));
        QVERIFY(!constantTimeEquals("", "a"));
        QVERIFY(constantTimeEquals("", ""));
    }

    void httpParts()
    {
        QByteArray buffer = "GET /o/a/b/?x=1 HTTP/1.1\r\nHost: h\r\nRange: bytes=0-9\r\n\r\nGET / HTTP/1.1\r\n";
        HttpRequest r;
        QCOMPARE(takeHttpRequest(&buffer, &r), ParseResult::Complete);
        QCOMPARE(r.method, QByteArray("GET"));
        QCOMPARE(r.target, QByteArray("/o/a/b/"));
        QCOMPARE(r.headers.value("range"), QByteArray("bytes=0-9"));
        QVERIFY(r.keepAlive());
        QCOMPARE(takeHttpRequest(&buffer, &r), ParseResult::Incomplete);
        QByteArray bad = "kaputt\r\n\r\n";
        QCOMPARE(takeHttpRequest(&bad, &r), ParseResult::Invalid);
        QByteArray huge(kMaxHttpHeaderBytes + 10, 'a');
        QCOMPARE(takeHttpRequest(&huge, &r), ParseResult::Invalid);

        QCOMPARE(parseRange("", 100), std::nullopt);
        QCOMPARE(parseRange("bytes=0-9", 100), (ByteRange{0, 9}));
        QCOMPARE(parseRange("bytes=90-", 100), (ByteRange{90, 99}));
        QCOMPARE(parseRange("bytes=-10", 100), (ByteRange{90, 99}));
        QCOMPARE(parseRange("bytes=50-500", 100), (ByteRange{50, 99}));
        QCOMPARE(parseRange("bytes=100-", 100), (ByteRange{-1, -1}));
        QCOMPARE(parseRange("bytes=0-1,5-6", 100), (ByteRange{-1, -1}));
        QCOMPARE(parseRange("items=0-1", 100), (ByteRange{-1, -1}));
    }

    void pageWithValidToken()
    {
        const Response r = request(m_port, "GET", base());
        QCOMPARE(r.status, 200);
        QFile html(kWeb + QStringLiteral("/overlay.html"));
        QVERIFY(html.open(QIODevice::ReadOnly));
        QCOMPARE(r.body, html.readAll());
        QCOMPARE(r.headers.value("content-type"), QByteArray("text/html; charset=utf-8"));
        QCOMPARE(request(m_port, "GET", base() + "overlay.js").status, 200);
        QCOMPARE(request(m_port, "HEAD", base()).body, QByteArray());
        // ohne «/» am Ende: weiter auf die Adresse mit «/»
        const Response redirect = request(m_port, "GET", base().chopped(1));
        QCOMPARE(redirect.status, 302);
        QCOMPARE(redirect.headers.value("location"), base());
        // auch über die Adresse im Heimnetz (hier 127.0.0.2)
        QCOMPARE(request(m_port, "GET", base(), {}, {}, QStringLiteral("127.0.0.2")).status, 200);
    }

    void invalidAddressesGiveEmptyPage()
    {
        g_log.clear();
        const QByteArray address = m_store->overlay(m_id)->address.toUtf8();
        QByteArray wrong = token().toUtf8();
        wrong[10] = wrong[10] == 'A' ? 'B' : 'A';
        const QList<QByteArray> paths = {
            "/o/" + address + "/" + wrong + "/",                      // ein Zeichen falsch
            "/o/" + address + "/" + token().toUtf8().chopped(1) + "/", // zu kurz
            "/o/" + address + "/" + token().toUtf8() + "x/",          // zu lang
            "/o/" + address + "//",                                    // leer
            "/o/gibt-es-nicht/" + token().toUtf8() + "/",              // unbekannte Adresse
        };
        for (const QByteArray &p : paths) {
            const Response r = request(m_port, "GET", p);
            QCOMPARE(r.status, 200);
            QVERIFY2(r.body.contains("background:transparent") && r.body.contains("<body></body>"),
                     r.body.constData());
            QVERIFY(!r.body.contains("overlay.js"));
            QCOMPARE(r.headers.value("cache-control"), QByteArray("no-store"));
        }
        // Unterseiten mit falschem Token: nichts
        QCOMPARE(request(m_port, "GET", paths.first() + "overlay.js").status, 404);
        // Protokoll: «unbekannte Adresse» mit lesbarer Adresse, ohne Token
        const QString joined = g_log.join(QLatin1Char('\n'));
        QVERIFY2(joined.contains(QStringLiteral("unbekannte Adresse /o/%1/… von 127.0.0.1").arg(QString::fromUtf8(address))),
                 qPrintable(joined));
        QVERIFY(joined.contains(QStringLiteral("unbekannte Adresse /o/gibt-es-nicht/…")));
        QVERIFY(!logHasToken());
        QVERIFY(!joined.contains(QString::fromUtf8(wrong)));
        // Hinweis fürs Fenster, ohne Token; verschwindet nach invalidHintMs
        QVERIFY2(m_server->status().invalidUse.contains(QStringLiteral("ungültige Adresse")),
                 qPrintable(m_server->status().invalidUse));
        QVERIFY(!m_server->status().invalidUse.contains(token()));
        QTRY_VERIFY_WITH_TIMEOUT(m_server->status().invalidUse.isEmpty(), 3000);
        // Andere Pfade: / leer ohne Hinweis, /favicon.ico 404
        QCOMPARE(request(m_port, "GET", "/").status, 200);
        QCOMPARE(request(m_port, "GET", "/favicon.ico").status, 404);
        QVERIFY(m_server->status().invalidUse.isEmpty());
    }

    void mediaOnlyOfOverlayWithRanges()
    {
        const Overlay o = *m_store->overlay(m_id);
        const QString jpg = o.sources.at(0).file;
        const QString png = o.sources.at(1).file;
        const QString mp4 = o.sources.at(2).file;
        const QString font = o.sources.at(3).text.fontFile;
        for (const QString &f : {jpg, png, mp4, font}) {
            const Response r = request(m_port, "GET", base() + f.toUtf8());
            QCOMPARE(r.status, 200);
            QCOMPARE(r.body.size(), QFileInfo(m_store->dataDir() + QLatin1Char('/') + f).size());
        }
        // Datei eines anderen Overlays, Pfad ausserhalb: 404
        const QString otherFile = m_store->overlayByAddress(QStringLiteral("andere"))->sources.value(0).file;
        QVERIFY(!otherFile.isEmpty());
        QCOMPARE(request(m_port, "GET", base() + otherFile.toUtf8()).status, 404);
        QCOMPARE(request(m_port, "GET", base() + "media/../tokens.json").status, 404);
        QCOMPARE(request(m_port, "GET", base() + "../../tokens.json").status, 404);

        // Ausrichtung in den gelieferten Bytes neutralisiert, Datei im Datenordner unverändert
        QTemporaryDir out;
        const auto served = [&](const QString &ref) {
            const QString path = out.filePath(QFileInfo(ref).fileName());
            QFile f(path);
            f.open(QIODevice::WriteOnly);
            f.write(request(m_port, "GET", base() + ref.toUtf8()).body);
            return path;
        };
        QCOMPARE(imageFileOrientation(m_store->dataDir() + QLatin1Char('/') + jpg), 6);
        QCOMPARE(imageFileOrientation(served(jpg)), 1);
        QCOMPARE(imageFileOrientation(m_store->dataDir() + QLatin1Char('/') + png), 6);
        const QString servedPng = served(png);
        QCOMPARE(imageFileOrientation(servedPng), 1);
        {
            // CRC des Chunks eXIf stimmt wieder
            const OrientationLocation l = imageOrientationLocation(servedPng);
            QFile f(servedPng);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray all = f.readAll();
            const QByteArray typeAndData = all.mid(l.pngChunk + 4, 4 + l.pngChunkLength);
            const QByteArray crc = all.mid(l.pngChunk + 8 + l.pngChunkLength, 4);
            QCOMPARE(qFromBigEndian<quint32>(crc.constData()), pngCrc32(typeAndData));
        }
        // MP4: nur die Matrix anders (Einheitsmatrix), Länge gleich
        const BytePatches patches = orientationPatches(m_store->dataDir() + QLatin1Char('/') + mp4);
        QCOMPARE(patches.size(), 1); // mvhd hat schon die Einheitsmatrix, tkhd nicht
        QCOMPARE(patches.first().bytes.size(), 36);
        QVERIFY(orientationPatches(kVideos + QStringLiteral("/drehung-0.mp4")).isEmpty());
        QVERIFY(orientationPatches(kExif + QStringLiteral("/orientierung-1.jpg")).isEmpty());

        // Bereiche (Video)
        Response r = request(m_port, "GET", base() + mp4.toUtf8(), {"Range: bytes=10-19"});
        QCOMPARE(r.status, 206);
        QCOMPARE(r.body.size(), 10);
        const qint64 size = QFileInfo(m_store->dataDir() + QLatin1Char('/') + mp4).size();
        QCOMPARE(r.headers.value("content-range"), QByteArray("bytes 10-19/") + QByteArray::number(size));
        // Bereich über die Ersetzung hinweg = dieselben Bytes wie im Ganzen
        const QByteArray whole = request(m_port, "GET", base() + mp4.toUtf8()).body;
        const qint64 at = patches.first().offset - 5;
        r = request(m_port, "GET", base() + mp4.toUtf8(),
                    {"Range: bytes=" + QByteArray::number(at) + "-" + QByteArray::number(at + 49)});
        QCOMPARE(r.body, whole.mid(at, 50));
        QCOMPARE(whole.mid(patches.first().offset, 36), patches.first().bytes);
        r = request(m_port, "GET", base() + mp4.toUtf8(), {"Range: bytes=999999-"});
        QCOMPARE(r.status, 416);
    }

    // P11, Punkt 80: WebM mit Drehangabe (Projection) wird neutralisiert, gleiche Länge. Gespiegelt
    // (Yaw 180) wird zur Laufzeit mit ffmpeg erzeugt (-display_hflip).
    void webmRotationNeutralized_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("orientation"); // vor dem Neutralisieren (mit DSS_VIDEO_ROTATION)
        QTest::addColumn<int>("patches");
        QTest::newRow("90") << kBildvergleich + QStringLiteral("/drehung-90.webm") << 6 << 1;
        QTest::newRow("180") << kBildvergleich + QStringLiteral("/drehung-180.webm") << 3 << 1;
        QTest::newRow("270") << kBildvergleich + QStringLiteral("/drehung-270.webm") << 8 << 1;
        QTest::newRow("gespiegelt 90") << QStringLiteral("gespiegelt") << 7 << 2;
        QTest::newRow("ohne Drehung") << kBildvergleich + QStringLiteral("/vp9-640x360-ohne.webm") << 1 << 0;
        QTest::newRow("Alpha ohne Drehung") << kBildvergleich + QStringLiteral("/alpha.webm") << 1 << 0;
    }

    void webmRotationNeutralized()
    {
        QFETCH(QString, file);
        QFETCH(int, orientation);
        QFETCH(int, patches);
        QTemporaryDir dir;
        if (file == QLatin1String("gespiegelt")) {
            file = dir.filePath(QStringLiteral("gespiegelt.webm"));
            QProcess ff;
            ff.start(QStringLiteral("ffmpeg"), {QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-nostdin"),
                                                QStringLiteral("-display_rotation"), QStringLiteral("90"),
                                                QStringLiteral("-display_hflip"), QStringLiteral("-i"),
                                                kBildvergleich + QStringLiteral("/drehung-90.webm"), QStringLiteral("-c"),
                                                QStringLiteral("copy"), file});
            QVERIFY(ff.waitForFinished(30000) && ff.exitCode() == 0);
        }
#if DSS_VIDEO_ROTATION
        QCOMPARE(readVideoOrientation(file), orientation);
#endif
        const BytePatches p = orientationPatches(file);
        QCOMPARE(p.size(), patches);
        for (const BytePatch &one : p)
            QCOMPARE(one.bytes, QByteArray(8, '\0')); // FFmpeg schreibt 8 Byte
        QFile in(file);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QByteArray all = in.readAll();
        const qint64 size = all.size();
        applyPatches(&all, 0, p);
        QCOMPARE(all.size(), size);
        const QString out = dir.filePath(QStringLiteral("geliefert.webm"));
        QFile f(out);
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(all) == size);
        f.close();
        // Nach dem Neutralisieren ohne Drehung; Bild unverändert lesbar
        QCOMPARE(readVideoOrientation(out), 1);
        int w = 0, h = 0;
        QString error;
        QVERIFY2(readMediaSize(out, &w, &h, &error), qPrintable(error));
        QVERIFY(orientationPatches(out).isEmpty());
        Q_UNUSED(orientation);
    }

    void pageDataForThePage()
    {
        const QJsonObject d = m_server->pageData(m_id);
        const Overlay o = *m_store->overlay(m_id);
        QCOMPARE(d.value(QStringLiteral("media")).toObject().value(o.sources.at(0).file).toObject()
                     .value(QStringLiteral("orientation")).toInt(), 6);
        QCOMPARE(d.value(QStringLiteral("media")).toObject().value(o.sources.at(1).file).toObject()
                     .value(QStringLiteral("orientation")).toInt(), 6);
#if DSS_VIDEO_ROTATION
        // drehung-90.mp4: 90° im Uhrzeigersinn = Ausrichtung 6 (wie der Zeichner des Editors)
        QCOMPARE(d.value(QStringLiteral("media")).toObject().value(o.sources.at(2).file).toObject()
                     .value(QStringLiteral("orientation")).toInt(), 6);
#endif
        const QJsonObject font = d.value(QStringLiteral("fonts")).toObject().value(o.sources.at(3).text.fontFile).toObject();
        QCOMPARE(font.value(QStringLiteral("units_per_em")).toInt(), 2048); // DejaVu Sans
        QCOMPARE(font.value(QStringLiteral("ascent")).toInt(), 1901);
        QCOMPARE(font.value(QStringLiteral("descent")).toInt(), 483);
        QVERIFY(!QJsonDocument(d).toJson().contains(token().toUtf8()));
    }

    void eventsWithChanges()
    {
        EventClient events(m_port, base() + "events");
        auto e = events.next();
        QCOMPARE(e.first, QByteArray("overlay"));
        QCOMPARE(QJsonDocument::fromJson(e.second).object().value(QStringLiteral("overlay")).toObject()
                     .value(QStringLiteral("id")).toString(), m_id.toString(QUuid::WithoutBraces));
        QTRY_COMPARE_WITH_TIMEOUT(m_server->status().pages.value(m_id), 1, 2000);
        // Änderung: das ganze Overlay neu
        QString error;
        QElapsedTimer t;
        t.start();
        SourceChanges c;
        c.x = 123;
        QVERIFY(m_store->updateSource(m_id, m_textId, c, &error));
        m_server->overlayChanged(m_id);
        e = events.next();
        QCOMPARE(e.first, QByteArray("overlay"));
        qInfo("Änderung bis zum Ereignis: %lld ms", static_cast<long long>(t.elapsed()));
        const QJsonArray sources = QJsonDocument::fromJson(e.second).object().value(QStringLiteral("overlay"))
                                       .toObject().value(QStringLiteral("sources")).toArray();
        QCOMPARE(sources.at(3).toObject().value(QStringLiteral("x")).toInt(), 123);
        // Ausgabe aus: gleiche Verbindung, output false
        QVERIFY(m_store->setOverlayOutput(m_id, false, &error));
        m_server->overlayChanged(m_id);
        e = events.next();
        QCOMPARE(QJsonDocument::fromJson(e.second).object().value(QStringLiteral("overlay")).toObject()
                     .value(QStringLiteral("output")).toBool(true), false);
        QVERIFY(m_store->setOverlayOutput(m_id, true, &error));
        m_server->overlayChanged(m_id);
        QCOMPARE(events.next().first, QByteArray("overlay"));
        // Gleiches Overlay noch einmal: kein Ereignis
        m_server->overlayChanged(m_id);
        QCOMPARE(events.next(300).first, QByteArray());
    }

    // P14 d: Laufzustand als «event: live», Bilder des Laufzustands nur für dieses Overlay
    void liveEventsAndImages()
    {
        testhelp::ImageServer images;
        const QByteArray png = [] {
            QFile f(kExif + QStringLiteral("/orientierung-6.png"));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        images.bodies.insert("/profil.png", png);
        OverlayLiveState::Options options;
        options.imageDir = m_store->dataDir() + QStringLiteral("/live-bilder");
        options.imageHosts = {QStringLiteral("127.0.0.1")};
        options.allowHttp = true;
        OverlayLiveState live(m_store.get(), options);
        m_server->setLiveState(&live);
        connect(&live, &OverlayLiveState::changed, m_server.get(), &OverlayServer::liveChanged);
        const auto setAndWait = [&live](const LiveSetRequest &r) {
            std::optional<OverlayLiveState::Result> result;
            live.set(r, [&result](const OverlayLiveState::Result &x) { result = x; });
            static_cast<void>(QTest::qWaitFor([&result] { return result.has_value(); }, 5000));
            return result.value_or(OverlayLiveState::Result{});
        };
        const Overlay o = *m_store->overlay(m_id);
        const QUuid imageId = o.sources.at(0).id;
        const QString otherFile = m_store->overlayByAddress(QStringLiteral("andere"))->sources.value(0).file;

        // Ohne Laufzustand: nach «overlay» kein «live»
        EventClient events(m_port, base() + "events");
        QCOMPARE(events.next().first, QByteArray("overlay"));
        QCOMPARE(events.next(300).first, QByteArray());
        QCOMPARE(request(m_port, "GET", base() + otherFile.toUtf8()).status, 404);

        // Datei eines anderen Overlays über den Laufzustand: jetzt auch für dieses Overlay
        LiveSetRequest r;
        r.overlayId = m_id;
        r.elements = {{imageId, std::nullopt, otherFile}, {m_textId, QStringLiteral("Hallo %user%"), QString()}};
        r.arguments = {{QStringLiteral("user"), QStringLiteral("Zuschauer42")}};
        QVERIFY(setAndWait(r).ok);
        auto e = events.next();
        QCOMPARE(e.first, QByteArray("live"));
        QJsonObject data = QJsonDocument::fromJson(e.second).object();
        QJsonObject element = data.value(QStringLiteral("elements")).toObject()
                                  .value(imageId.toString(QUuid::WithoutBraces)).toObject();
        QCOMPARE(element.value(QStringLiteral("visible")).toBool(), true);
        QCOMPARE(element.value(QStringLiteral("image")).toString(), otherFile);
        QVERIFY(element.value(QStringLiteral("text")).isNull());
        QCOMPARE(data.value(QStringLiteral("media")).toObject().value(otherFile).toObject()
                     .value(QStringLiteral("orientation")).toInt(), 1);
        QCOMPARE(data.value(QStringLiteral("elements")).toObject().value(m_textId.toString(QUuid::WithoutBraces))
                     .toObject().value(QStringLiteral("text")).toString(), QStringLiteral("Hallo Zuschauer42"));
        QCOMPARE(request(m_port, "GET", base() + otherFile.toUtf8()).status, 200);

        // Profilbild: …/live/<sha256>.png mit neutralisierter Ausrichtung, Orientierung 6 für die Seite
        r.elements = {{imageId, std::nullopt, images.url("/profil.png")}};
        const OverlayLiveState::Result loaded = setAndWait(r);
        QVERIFY2(loaded.ok && loaded.warning.isEmpty(), qPrintable(loaded.message + loaded.warning));
        e = events.next();
        QCOMPARE(e.first, QByteArray("live"));
        data = QJsonDocument::fromJson(e.second).object();
        const QString livePath = data.value(QStringLiteral("elements")).toObject()
                                     .value(imageId.toString(QUuid::WithoutBraces)).toObject()
                                     .value(QStringLiteral("image")).toString();
        QVERIFY2(livePath.startsWith(QStringLiteral("live/")) && livePath.endsWith(QStringLiteral(".png")), qPrintable(livePath));
        QCOMPARE(data.value(QStringLiteral("media")).toObject().value(livePath).toObject()
                     .value(QStringLiteral("orientation")).toInt(), 6);
        Response served = request(m_port, "GET", base() + livePath.toUtf8());
        QCOMPARE(served.status, 200);
        QCOMPARE(served.body.size(), png.size());
        QCOMPARE(served.headers.value("content-type"), QByteArray("image/png"));
        QTemporaryDir out;
        {
            QFile f(out.filePath(QStringLiteral("geliefert.png")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(served.body);
        }
        QCOMPARE(imageFileOrientation(out.filePath(QStringLiteral("geliefert.png"))), 1);
        // Die vorige Datei gilt nicht mehr, ein anderes Overlay bekommt das Profilbild nicht
        QCOMPARE(request(m_port, "GET", base() + otherFile.toUtf8()).status, 404);
        const Overlay *other = m_store->overlayByAddress(QStringLiteral("andere"));
        const QByteArray otherBase = browserSourcePath(other->address, m_store->token(other->id)).toUtf8();
        QCOMPARE(request(m_port, "GET", otherBase + livePath.toUtf8()).status, 404);
        QCOMPARE(request(m_port, "GET", base() + "live/../tokens.json").status, 404);
        QCOMPARE(request(m_port, "GET", base() + "live/" + QByteArray(64, 'a') + ".png").status, 404);

        // Neue Verbindung mit Laufzustand: zuerst «overlay», dann «live»
        EventClient second(m_port, base() + "events");
        QCOMPARE(second.next().first, QByteArray("overlay"));
        QCOMPARE(second.next().first, QByteArray("live"));

        // Zurücksetzen: leerer Laufzustand an beide, Bild nicht mehr erreichbar
        QVERIFY(live.reset({0, m_id, {}}).isEmpty());
        for (EventClient *c : {&events, &second}) {
            e = c->next();
            QCOMPARE(e.first, QByteArray("live"));
            QVERIFY(QJsonDocument::fromJson(e.second).object().value(QStringLiteral("elements")).toObject().isEmpty());
        }
        QCOMPARE(request(m_port, "GET", base() + livePath.toUtf8()).status, 404);
        QVERIFY(!logHasToken());
        m_server->setLiveState(nullptr);
    }

    void oldAddressesBecomeInvalid()
    {
        QString error;
        // Neuer Token: Seite der alten Adresse bekommt «gone», alte Adresse leer
        const QByteArray oldBase = base();
        {
            EventClient events(m_port, oldBase + "events");
            QCOMPARE(events.next().first, QByteArray("overlay"));
            QVERIFY(m_store->newToken(m_id, &error));
            m_server->tokenChanged(m_id);
            QCOMPARE(events.next().first, QByteArray("gone"));
            QTRY_VERIFY_WITH_TIMEOUT(events.closed(), 2000);
        }
        QVERIFY(oldBase != base());
        QVERIFY(!request(m_port, "GET", oldBase).body.contains("overlay.js"));
        QVERIFY(request(m_port, "GET", base()).body.contains("overlay.js"));
        // Ereignisse mit alter Adresse: sofort «gone» (die Seite lädt neu und ist leer)
        {
            EventClient events(m_port, oldBase + "events");
            QCOMPARE(events.next().first, QByteArray("gone"));
        }
        // dasselbe mit Kennung der Seite (Seite in OBS, P12b b4)
        {
            EventClient events(m_port, oldBase + "events/seitealt1");
            QCOMPARE(events.next().first, QByteArray("gone"));
        }
        // Neue Adresse beim Umbenennen
        const QByteArray beforeRename = base();
        {
            EventClient events(m_port, beforeRename + "events");
            QCOMPARE(events.next().first, QByteArray("overlay"));
            QVERIFY(m_store->renameOverlay(m_id, QStringLiteral("Probe Neu"), true, &error));
            m_server->overlayChanged(m_id);
            QCOMPARE(events.next().first, QByteArray("gone"));
        }
        QVERIFY(!request(m_port, "GET", beforeRename).body.contains("overlay.js"));
        QVERIFY(request(m_port, "GET", base()).body.contains("overlay.js"));
        // Gelöschtes Overlay
        const QUuid gone = m_store->createOverlay(QStringLiteral("Weg"), 10, 10, &error);
        const QByteArray gonePath = browserSourcePath(m_store->overlay(gone)->address, m_store->token(gone)).toUtf8();
        QVERIFY(request(m_port, "GET", gonePath).body.contains("overlay.js"));
        {
            EventClient events(m_port, gonePath + "events");
            QCOMPARE(events.next().first, QByteArray("overlay"));
            QVERIFY(m_store->removeOverlay(gone, &error));
            m_server->overlayRemoved(gone);
            QCOMPARE(events.next().first, QByteArray("gone"));
        }
        QVERIFY(!request(m_port, "GET", gonePath).body.contains("overlay.js"));
        QVERIFY(!logHasToken());
    }

    void reportFromPage()
    {
        g_log.clear();
        const Response r = request(m_port, "POST", base() + "report", {},
                                   "Video nicht abspielbar: media/x.mp4 (Fehler 4)\nzweite Zeile");
        QCOMPARE(r.status, 204);
        QVERIFY2(g_log.join(QLatin1Char('\n')).contains(QStringLiteral("meldet: Video nicht abspielbar: media/x.mp4 (Fehler 4) zweite Zeile")),
                 qPrintable(g_log.join(QLatin1Char('\n'))));
        QCOMPARE(request(m_port, "POST", base() + "events").status, 405);
        QCOMPARE(request(m_port, "PUT", base()).status, 405);
        QVERIFY(!logHasToken());
    }

    // P12b b4: Meldungen der Uhr (…/clock), Format clockreport.h
    void clockFromPage()
    {
        QList<std::pair<QUuid, ClockReport>> reports;
        const QMetaObject::Connection connection = connect(
            m_server.get(), &OverlayServer::clockReported, this,
            [&reports](const QUuid &id, const ClockReport &r) { reports.append({id, r}); });
        const Source video = videoSource();
        const qint64 before = steadyNs();
        Response r = request(m_port, "POST", base() + "clock", {}, clockBody(clockMessage("seitea01", 7)));
        const qint64 after = steadyNs();
        QCOMPARE(r.status, 204);
        QCOMPARE(reports.size(), 1);
        QCOMPARE(reports.at(0).first, m_id);
        const ClockReport &c = reports.at(0).second;
        QCOMPARE(c.page, QStringLiteral("seitea01"));
        QCOMPARE(c.seq, 7);
        QCOMPARE(c.event, ClockReport::Event::Frame);
        QCOMPARE(c.run, 2);
        QCOMPARE(c.source, video.id);
        QCOMPARE(c.file, video.file);
        QCOMPARE(c.loop, 1);
        QCOMPARE(c.mediaNs, 1'250'000'000);
        QCOMPARE(c.displayMs, 81234.5);
        QCOMPARE(c.nowMs, 81236.25);
        QCOMPARE(c.durationNs, 3'000'000'000);
        QVERIFY2(c.arrivalNs >= before && c.arrivalNs <= after, "Eintreffen in steady_clock");

        // Alle Ereignisse; stop ohne Quelle, end ohne Lage, duration_s darf fehlen
        for (const char *event : {"start", "loop", "end", "stop"}) {
            QJsonObject m = clockMessage("seitea01", 8, event);
            m.remove(QStringLiteral("duration_s"));
            if (QByteArray(event) == "stop")
                m.remove(QStringLiteral("source"));
            if (QByteArray(event) == "end" || QByteArray(event) == "stop") {
                m.remove(QStringLiteral("file"));
                m.remove(QStringLiteral("loop"));
                m.remove(QStringLiteral("media_s"));
                m.remove(QStringLiteral("display_ms"));
            }
            QCOMPARE(request(m_port, "POST", base() + "clock", {}, clockBody(m)).status, 204);
            QCOMPARE(QByteArray(clockEventName(reports.last().second.event)), QByteArray(event));
            QCOMPARE(reports.last().second.durationNs, 0);
        }
        QVERIFY(reports.last().second.source.isNull());
        QCOMPARE(reports.size(), 5);

        // Nur mit gültigem Token, nur POST
        const QByteArray wrong = browserSourcePath(m_store->overlay(m_id)->address, QString(43, QLatin1Char('x'))).toUtf8();
        QCOMPARE(request(m_port, "POST", wrong + "clock", {}, clockBody(clockMessage("seitea01", 9))).status, 404);
        QCOMPARE(request(m_port, "GET", base() + "clock").status, 405);
        QCOMPARE(reports.size(), 5);
        disconnect(connection);
        QVERIFY(!logHasToken());
    }

    void clockRejectsWrongData_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        const auto without = [this](const char *key) {
            QJsonObject m = clockMessage("seiteb01", 1);
            m.remove(QLatin1String(key));
            return clockBody(m);
        };
        const auto with = [this](const char *key, const QJsonValue &value) {
            QJsonObject m = clockMessage("seiteb01", 1);
            m.insert(QLatin1String(key), value);
            return clockBody(m);
        };
        QTest::newRow("kein JSON") << QByteArray("kaputt") << 400;
        QTest::newRow("Liste") << QByteArray("[]") << 400;
        QTest::newRow("ohne Version") << without("v") << 400;
        QTest::newRow("Version 2") << with("v", 2) << 400;
        QTest::newRow("Seite zu kurz") << with("page", QStringLiteral("abc")) << 400;
        QTest::newRow("Seite gross") << with("page", QStringLiteral("SEITEB01")) << 400;
        QTest::newRow("Ereignis unbekannt") << with("event", QStringLiteral("pause")) << 400;
        QTest::newRow("seq negativ") << with("seq", -1) << 400;
        QTest::newRow("seq gebrochen") << with("seq", 1.5) << 400;
        QTest::newRow("seq Text") << with("seq", QStringLiteral("1")) << 400;
        QTest::newRow("ohne run") << without("run") << 400;
        QTest::newRow("ohne now_ms") << without("now_ms") << 400;
        QTest::newRow("now_ms negativ") << with("now_ms", -5.0) << 400;
        QTest::newRow("ohne Quelle") << without("source") << 400;
        QTest::newRow("Quelle keine Kennung") << with("source", QStringLiteral("x")) << 400;
        QTest::newRow("Quelle unbekannt") << with("source", QUuid::createUuid().toString(QUuid::WithoutBraces)) << 400;
        QTest::newRow("Quelle Bild") << with("source", m_store->overlay(m_id)->sources.at(0).id.toString(QUuid::WithoutBraces)) << 400;
        QTest::newRow("ohne Datei") << without("file") << 400;
        QTest::newRow("Datei ausserhalb") << with("file", QStringLiteral("../kern.ini")) << 400;
        QTest::newRow("ohne loop") << without("loop") << 400;
        QTest::newRow("ohne media_s") << without("media_s") << 400;
        QTest::newRow("media_s negativ") << with("media_s", -0.1) << 400;
        QTest::newRow("ohne display_ms") << without("display_ms") << 400;
        QTest::newRow("duration_s negativ") << with("duration_s", -1.0) << 400;
        QTest::newRow("zu gross") << with("pad", QString(kMaxClockBytes, QLatin1Char('a'))) << 413;
    }

    void clockRejectsWrongData()
    {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        int reports = 0;
        const QMetaObject::Connection connection =
            connect(m_server.get(), &OverlayServer::clockReported, this, [&reports] { ++reports; });
        QCOMPARE(request(m_port, "POST", base() + "clock", {}, body).status, status);
        disconnect(connection);
        QCOMPARE(reports, 0);
        QVERIFY(!logHasToken());
    }

    // Grenze je Seite: 60 Meldungen in einem Paket (gleiches Eintreffen), davon 40 angenommen;
    // eine andere Seite ist davon nicht betroffen.
    void clockLimitPerPage()
    {
        int reports = 0;
        const QMetaObject::Connection connection =
            connect(m_server.get(), &OverlayServer::clockReported, this, [&reports] { ++reports; });
        QTcpSocket s;
        s.connectToHost(QStringLiteral("127.0.0.1"), m_port);
        QVERIFY(QTest::qWaitFor([&] { return s.state() == QAbstractSocket::ConnectedState; }, 3000));
        QByteArray all;
        constexpr int kCount = 60;
        for (int i = 0; i < kCount; ++i) {
            const QByteArray body = clockBody(clockMessage("seitec01", i + 1));
            all += "POST " + base() + "clock HTTP/1.1\r\nHost: test\r\n"
                   + (i == kCount - 1 ? "Connection: close\r\n" : "") + "Content-Length: "
                   + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        }
        s.write(all);
        QByteArray answers;
        QVERIFY(QTest::qWaitFor([&] {
            answers += s.readAll();
            return s.state() == QAbstractSocket::UnconnectedState;
        }, 5000));
        answers += s.readAll();
        const int accepted = int(answers.count("HTTP/1.1 204"));
        const int limited = int(answers.count("HTTP/1.1 429"));
        QCOMPARE(accepted + limited, kCount);
        // Eimer mit 40 Marken; kommt das Paket in Teilen, höchstens eine Marke dazu
        QVERIFY2(accepted >= ClockLimiter::kBurst && accepted <= ClockLimiter::kBurst + 1, qPrintable(QString::number(accepted)));
        QCOMPARE(reports, accepted);
        QCOMPARE(request(m_port, "POST", base() + "clock", {}, clockBody(clockMessage("seitec02", 1))).status, 204);
        QCOMPARE(reports, accepted + 1);
        disconnect(connection);

        // Eimer füllt sich wieder: 20 Marken je Sekunde
        ClockLimiter limiter;
        for (int i = 0; i < ClockLimiter::kBurst; ++i)
            QVERIFY(limiter.allow(QStringLiteral("seited01"), 1000));
        QVERIFY(!limiter.allow(QStringLiteral("seited01"), 1000));
        QVERIFY(limiter.allow(QStringLiteral("seited01"), 1000 + 50'000'000));
        QVERIFY(!limiter.allow(QStringLiteral("seited01"), 1000 + 50'000'000));
        // Höchstens kMaxPages Seiten; ruhende fallen nach kIdleNs heraus
        for (int i = 1; i < ClockLimiter::kMaxPages; ++i)
            QVERIFY(limiter.allow(QStringLiteral("seite%1").arg(i, 3, 10, QLatin1Char('0')), 2000));
        QVERIFY(!limiter.allow(QStringLiteral("seitevoll"), 3000));
        QVERIFY(limiter.allow(QStringLiteral("seitevoll"), 3000 + ClockLimiter::kIdleNs + 1));
    }

    // Ereignisse mit Kennung der Seite: pageOpened beim Verbinden, pageClosed beim Trennen.
    void eventsWithPage()
    {
        QList<std::pair<QUuid, QString>> opened;
        QList<std::pair<QUuid, QString>> closed;
        const auto c1 = connect(m_server.get(), &OverlayServer::pageOpened, this,
                                [&opened](const QUuid &id, const QString &page) { opened.append({id, page}); });
        const auto c2 = connect(m_server.get(), &OverlayServer::pageClosed, this,
                                [&closed](const QUuid &id, const QString &page) { closed.append({id, page}); });
        {
            EventClient events(m_port, base() + "events/seitee01");
            QCOMPARE(events.next().first, QByteArray("overlay"));
            QCOMPARE(opened, (QList<std::pair<QUuid, QString>>{{m_id, QStringLiteral("seitee01")}}));
            QVERIFY(closed.isEmpty());
        }
        QTRY_COMPARE_WITH_TIMEOUT(closed, (QList<std::pair<QUuid, QString>>{{m_id, QStringLiteral("seitee01")}}), 2000);
        // Ohne Kennung kein Signal; ungültige Kennung: 404
        {
            EventClient events(m_port, base() + "events");
            QCOMPARE(events.next().first, QByteArray("overlay"));
        }
        QCOMPARE(request(m_port, "GET", base() + "events/SEITE").status, 404);
        QTest::qWait(200);
        QCOMPARE(opened.size(), 1);
        QCOMPARE(closed.size(), 1);
        disconnect(c1);
        disconnect(c2);
    }

    // P12b, Rückfall nach M3: «obs_active» nur an die Seite mit dieser Kennung (sendPageEvent).
    void pageEventOnlyToThatPage()
    {
        EventClient one(m_port, base() + "events/seiteg01");
        EventClient two(m_port, base() + "events/seiteg02");
        EventClient plain(m_port, base() + "events");
        QCOMPARE(one.next().first, QByteArray("overlay"));
        QCOMPARE(two.next().first, QByteArray("overlay"));
        QCOMPARE(plain.next().first, QByteArray("overlay"));
        const QJsonObject data{{QStringLiteral("active"), true}};
        QVERIFY(m_server->sendPageEvent(m_id, QStringLiteral("seiteg02"), "obs_active", data));
        const auto e = two.next();
        QCOMPARE(e.first, QByteArray("obs_active"));
        QCOMPARE(QJsonDocument::fromJson(e.second).object(), data);
        QCOMPARE(one.next(300).first, QByteArray());
        QCOMPARE(plain.next(100).first, QByteArray());
        // Unbekannte Seite, anderes Overlay, leere Kennung: nichts geschickt
        QVERIFY(!m_server->sendPageEvent(m_id, QStringLiteral("seiteg09"), "obs_active", data));
        QVERIFY(!m_server->sendPageEvent(QUuid::createUuid(), QStringLiteral("seiteg01"), "obs_active", data));
        QVERIFY(!m_server->sendPageEvent(m_id, QString(), "obs_active", data));
        QCOMPARE(one.next(200).first, QByteArray());
    }

    // Nur IPv4: ss zeigt 127.0.0.1 und 127.0.0.2, kein [::] und kein *.
    void listensOnlyOnIpv4()
    {
        QProcess ss;
        ss.start(QStringLiteral("ss"), {QStringLiteral("-ltnH")});
        QVERIFY(ss.waitForFinished(5000));
        QStringList lines;
        for (const QString &line : QString::fromUtf8(ss.readAllStandardOutput()).split(QLatin1Char('\n'))) {
            if (line.contains(QStringLiteral(":%1 ").arg(m_port)))
                lines.append(line.simplified());
        }
        qInfo("ss -ltnH: %s", qPrintable(lines.join(QStringLiteral(" | "))));
        QCOMPARE(lines.size(), 2);
        QVERIFY(lines.join(QLatin1Char(' ')).contains(QStringLiteral("127.0.0.1:%1").arg(m_port)));
        QVERIFY(lines.join(QLatin1Char(' ')).contains(QStringLiteral("127.0.0.2:%1").arg(m_port)));
        QVERIFY(!lines.join(QLatin1Char(' ')).contains(QStringLiteral("[")));
        QVERIFY(!lines.join(QLatin1Char(' ')).contains(QStringLiteral("*:%1").arg(m_port)));
        QTcpSocket v6;
        v6.connectToHost(QHostAddress(QHostAddress::LocalHostIPv6), m_port);
        QVERIFY(!v6.waitForConnected(1000));
    }

    // Port belegt: Meldung mit dem Programm, das ihn hält (hier dieser Test); frei → Server startet.
    void portBusy()
    {
        const quint16 port = freePort();
        auto blocker = std::make_unique<QTcpServer>();
        QVERIFY(blocker->listen(QHostAddress::LocalHost, port));
        const PortOwner owner = findPortOwner(port);
        QVERIFY(owner.listening);
        QCOMPARE(owner.pid, qint64(getpid()));
        QVERIFY2(owner.executable.endsWith(QStringLiteral("tst_overlayserver")), qPrintable(owner.executable));

        auto server = makeServer([] { return QList<QHostAddress>{QHostAddress(QHostAddress::LocalHost)}; });
        server->configure(port, QString());
        const QString problems = server->status().problems.join(QLatin1Char('\n'));
        QVERIFY2(problems.contains(QStringLiteral("Port %1 ist belegt, von «tst_overlayserver» (Prozess %2)")
                                       .arg(port).arg(getpid())),
                 qPrintable(problems));
        QVERIFY(problems.contains(QStringLiteral("Keine Adresse im Heimnetz eingestellt")));
        QVERIFY(server->status().listening.isEmpty());
        blocker.reset();
        QTRY_COMPARE_WITH_TIMEOUT(server->status().listening, QStringList{QStringLiteral("127.0.0.1:%1").arg(port)}, 3000);
        QVERIFY(!server->status().problems.join(QLatin1Char(' ')).contains(QStringLiteral("belegt")));
    }

    // Meldung «vermutlich DoNs Streaming Studio im Einsatz» (nachgebautes /proc, Punkt 77).
    void portOwnerFromProc()
    {
        QTemporaryDir proc;
        QVERIFY(QDir(proc.path()).mkpath(QStringLiteral("net")));
        QVERIFY(QDir(proc.path()).mkpath(QStringLiteral("4242/fd")));
        QFile tcp(proc.filePath(QStringLiteral("net/tcp")));
        QVERIFY(tcp.open(QIODevice::WriteOnly));
        tcp.write("  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid  timeout inode\n"
                  "   0: 090200C0:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 777001 1\n"
                  "   1: 0100007F:1F91 00000000:0000 01 00000000:00000000 00:00000000 00000000  1000        0 777002 1\n");
        tcp.close();
        const QString installed = proc.filePath(QStringLiteral("opt/dons-streaming-studio"));
        QVERIFY(QFile::link(QStringLiteral("socket:[777001]"), proc.filePath(QStringLiteral("4242/fd/7"))));
        QVERIFY(QFile::link(installed + QStringLiteral("/bin/dss-core"), proc.filePath(QStringLiteral("4242/exe"))));
        PortOwner owner = findPortOwner(8080, proc.path());
        QCOMPARE(owner.pid, qint64(4242));
        QCOMPARE(portBusyMessage(8080, owner, installed),
                 QStringLiteral("Port 8080 ist belegt, vermutlich von DoNs Streaming Studio im Einsatz (Prozess 4242). "
                                "Die Browserquelle ist nicht erreichbar. Anderen Port im Dialog «Einstellungen» "
                                "wählen oder das Programm beenden."));
        // 8081 nur verbunden (01), nicht lauschend
        QVERIFY(!findPortOwner(8081, proc.path()).listening);
        // ohne lesbaren Prozess
        owner.pid = 0;
        QVERIFY(portBusyMessage(8080, owner, installed).contains(QStringLiteral("nicht feststellen")));
    }

    // Adresse fehlt beim Start: nur 127.0.0.1 mit Meldung; kommt sie, lauscht der Server von selbst.
    void addressAppearsLater()
    {
        QList<QHostAddress> local = {QHostAddress(QHostAddress::LocalHost)};
        const quint16 port = freePort();
        auto server = makeServer([&local] { return local; });
        server->configure(port, QStringLiteral("127.0.0.3"));
        QCOMPARE(server->status().listening, QStringList{QStringLiteral("127.0.0.1:%1").arg(port)});
        QVERIFY2(server->status().problems.join(QLatin1Char(' ')).contains(
                     QStringLiteral("Die Adresse im Heimnetz 127.0.0.3 ist auf diesem PC gerade nicht vorhanden")),
                 qPrintable(server->status().problems.join(QLatin1Char(' '))));
        QSignalSpy changed(server.get(), &OverlayServer::statusChanged);
        local.append(QHostAddress(QStringLiteral("127.0.0.3")));
        QTRY_COMPARE_WITH_TIMEOUT(server->status().listening.size(), 2, 3000);
        QVERIFY(server->status().problems.isEmpty());
        QVERIFY(changed.count() >= 1);
        QCOMPARE(request(port, "GET", base(), {}, {}, QStringLiteral("127.0.0.3")).status, 200);
        // Port ändern: Server startet neu, alter Port zu
        const quint16 port2 = freePort();
        server->configure(port2, QStringLiteral("127.0.0.3"));
        QCOMPARE(server->status().listening.size(), 2);
        QCOMPARE(request(port, "GET", base()).status, 0);
        QCOMPARE(request(port2, "GET", base()).status, 200);
    }
};

QTEST_GUILESS_MAIN(TestOverlayServer)
#include "tst_overlayserver.moc"
