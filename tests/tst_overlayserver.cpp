// P10, Teil A: Overlay-Server ohne Fenster (Qt Core und Qt Network, CLAUDE.md Regel 9).
// Geprüft: Token (gleichbleibende Laufzeit, falsch, alt), leere Seite für alte und unbekannte
// Adressen, Protokoll ohne Token, Änderungen über Server-Sent Events, Medien nur des Overlays mit
// Bereichen und neutralisierter Ausrichtung, Port belegt (Meldung mit Programm), Adresse fehlt
// (nur 127.0.0.1, später von selbst), nur IPv4 (ss -ltn), Meldungen der Seite.
// Daten: Bilder und Videos aus tests/daten/exif/ und tests/daten/video-drehung/ (wie im Handtest),
// WebM mit Drehangabe aus tests/daten/bildvergleich/ (P11, Punkt 80).

#include "browsersource.h"
#include "httpmessage.h"
#include "imageorientation.h"
#include "mediapatch.h"
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
                  "   0: 0901A8C0:1F90 00000000:0000 0A 00000000:00000000 00:00000000 00000000  1000        0 777001 1\n"
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
