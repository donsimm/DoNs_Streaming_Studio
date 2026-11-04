// Bilder und Videos über dss_media (P09) ohne Qt Gui:
// - Grösse neuer Bild- und Videoquellen aus der Datei (Entscheid 6), PNG, JPEG, WebM, MP4, und
//   die Grösse des Overlays mit Warnung, wenn die Datei nicht lesbar ist
// - dss::VideoPlayer: Decoder (libvpx-vp9 für VP9 mit Alpha), Alpha im ersten Bild als Zahl wie
//   P02 (Gegenprobe mit ffmpeg … alphaextract,signalstats), Takt, Ende ohne Einstellung (leer),
//   «Am Ende anhalten» (letztes Bild bleibt) und «Wiederholen»
// - dss-cli quelle-video --am-ende-anhalten und die Spalte in quelle-liste
// - P10a: Toleranz der Zeitpunkte (Punkt 66), Drehung aus der Display-Matrix von Videos (Punkt 65)
// - P10b, Punkt 81: Format der Videos (readVideoCodec), Hinweis beim Hinzufügen und in der
//   Medienliste, wenn die Browserquelle es nicht abspielt (HEVC, MPEG-4 Part 2, ProRes); neue
//   Quelle aus der Medienliste an einer Stelle in Originalgrösse (Teil B 6); dss-cli Medien
// Daten wie im Handtest: prototypes/media (alpha.webm, video.webm, bild1.png); JPEG und MP4
// werden mit ffmpeg aus Kopien davon erzeugt, ebenso die Videos in HEVC, MPEG-4 Part 2 und ProRes.

#include "coreclient.h"
#include "imageorientation.h"
#include "mediainfo.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#include "version.h"
#include "videoformat.h"
#include "videoplayer.h"
#include "windowdata.h"

#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <array>
#include <memory>
#include <thread>

using namespace dss;

namespace {

const QString kMediaDir = QStringLiteral(DSS_TEST_MEDIA_DIR);

QString media(const char *name)
{
    return kMediaDir + QLatin1Char('/') + QLatin1String(name);
}

// ffmpeg mit Argumenten; Ausgabe (stderr) zurück, leer bei Fehler.
QString runFfmpeg(const QStringList &args, bool *ok)
{
    QProcess p;
    p.start(QStringLiteral("ffmpeg"), QStringList{QStringLiteral("-hide_banner"), QStringLiteral("-nostats")} + args);
    *ok = p.waitForFinished(60000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return QString::fromUtf8(p.readAllStandardError());
}

QString rotationVideo(int degrees)
{
    return QStringLiteral(DSS_TEST_VIDEO_ROTATION_DIR "/drehung-%1.mp4").arg(degrees);
}

// Farbe des angezeigten Bildes eines Videos an (x, y): Pixel im gespeicherten Bild suchen, das die
// Ausrichtung dorthin bringt (Pixelmitten, orientedPoint).
std::array<int, 3> shownPixel(const VideoFrame &f, int x, int y)
{
    for (int sy = 0; sy < f.height; ++sy) {
        for (int sx = 0; sx < f.width; ++sx) {
            const QPointF p = orientedPoint(QPointF(sx + 0.5, sy + 0.5), QSize(f.width, f.height),
                                            f.orientation);
            if (int(p.x()) == x && int(p.y()) == y) {
                const uint8_t *b = f.data.data() + size_t(sy) * f.stride + 4 * sx; // BGRA
                return {b[2], b[1], b[0]};
            }
        }
    }
    return {-1, -1, -1};
}

bool waitFor(const std::function<bool()> &condition, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!condition()) {
        if (t.elapsed() > timeoutMs)
            return false;
        QThread::msleep(10);
    }
    return true;
}

} // namespace

class TestMedien : public QObject
{
    Q_OBJECT

    QTemporaryDir m_work;
    QString m_jpg;
    QString m_mp4;
    // P10b, Punkt 81: Formate, die die Browserquelle nicht abspielt (Bericht P10)
    QString m_hevc;
    QString m_mpeg4;
    QString m_prores; // ProRes in MOV, als .mp4 benannt (ffmpeg schreibt ProRes nicht in MP4)

    // Kern mit eigenem Datenordner, eigener Verbindung und eigenem Port (prozesshilfe.h)
    struct Core
    {
        QTemporaryDir data;
        std::unique_ptr<QTemporaryDir> run = testhelp::runtimeDir();
        QString socket = run->filePath(QStringLiteral("k.sock"));
        std::unique_ptr<QProcess> process;
        QString log;
        std::unique_ptr<CoreClient> client;

        bool start()
        {
            process = testhelp::startCore(data.path(), socket);
            if (!testhelp::waitForLog(process.get(), QStringLiteral("Verbindung offen"), &log))
                return false;
            CoreClient::Options o;
            o.socketPath = socket;
            o.program = QStringLiteral("tst_medien");
            o.version = version();
            client = std::make_unique<CoreClient>(o);
            QSignalSpy spy(client.get(), &CoreClient::overlayListChanged);
            client->start();
            return spy.wait(5000);
        }

        ChangeResult change(const ChangeRequest &r)
        {
            QSignalSpy spy(client.get(), &CoreClient::changeFinished);
            const qint64 id = client->sendChange(r);
            QDeadlineTimer deadline(20000);
            while (!deadline.hasExpired()) {
                for (const QList<QVariant> &args : std::as_const(spy)) {
                    const ChangeResult result = args.at(0).value<ChangeResult>();
                    if (result.requestId == id)
                        return result;
                }
                spy.wait(50);
            }
            return {};
        }

        ~Core()
        {
            if (client)
                client->stop();
            if (process) {
                process->terminate();
                process->waitForFinished(5000);
            }
        }
    };

    static ChangeRequest request(const char *type, const QUuid &overlay = QUuid())
    {
        ChangeRequest r;
        r.type = QString::fromLatin1(type);
        r.overlayId = overlay;
        return r;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_work.isValid());
        bool ok = false;
        m_jpg = m_work.filePath(QStringLiteral("bild1.jpg"));
        runFfmpeg({QStringLiteral("-i"), media("bild1.png"), QStringLiteral("-frames:v"), QStringLiteral("1"), m_jpg}, &ok);
        QVERIFY2(ok, "ffmpeg: JPEG nicht erzeugt");
        m_mp4 = m_work.filePath(QStringLiteral("video.mp4"));
        runFfmpeg({QStringLiteral("-i"), media("video.webm"), QStringLiteral("-t"), QStringLiteral("2"),
                   QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-pix_fmt"),
                   QStringLiteral("yuv420p"), m_mp4}, &ok);
        QVERIFY2(ok, "ffmpeg: MP4 nicht erzeugt");
        const QStringList input{QStringLiteral("-i"), media("video.webm"), QStringLiteral("-t"), QStringLiteral("1")};
        m_hevc = m_work.filePath(QStringLiteral("hevc.mp4"));
        runFfmpeg(input + QStringList{QStringLiteral("-c:v"), QStringLiteral("libx265"), QStringLiteral("-pix_fmt"),
                                      QStringLiteral("yuv420p"), QStringLiteral("-x265-params"),
                                      QStringLiteral("log-level=error"), m_hevc}, &ok);
        QVERIFY2(ok, "ffmpeg: HEVC nicht erzeugt");
        m_mpeg4 = m_work.filePath(QStringLiteral("mpeg4.mp4"));
        runFfmpeg(input + QStringList{QStringLiteral("-c:v"), QStringLiteral("mpeg4"), m_mpeg4}, &ok);
        QVERIFY2(ok, "ffmpeg: MPEG-4 Part 2 nicht erzeugt");
        const QString mov = m_work.filePath(QStringLiteral("zaehler.mov"));
        runFfmpeg(input + QStringList{QStringLiteral("-c:v"), QStringLiteral("prores_ks"), mov}, &ok);
        QVERIFY2(ok, "ffmpeg: ProRes nicht erzeugt");
        m_prores = m_work.filePath(QStringLiteral("zaehler.mp4"));
        QVERIFY(QFile::copy(mov, m_prores));
    }

    // P10b, Punkt 81: Format des Bildstroms als Name in FFmpeg
    void videoCodec_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QString>("codec");
        QTest::addColumn<QString>("unsupported");
        QTest::newRow("VP9 WebM") << media("video.webm") << QStringLiteral("vp9") << QString();
        QTest::newRow("VP9 mit Alpha") << media("alpha.webm") << QStringLiteral("vp9") << QString();
        QTest::newRow("H.264 MP4") << QStringLiteral("mp4") << QStringLiteral("h264") << QString();
        QTest::newRow("HEVC MP4") << QStringLiteral("hevc") << QStringLiteral("hevc") << QStringLiteral("HEVC");
        QTest::newRow("MPEG-4 Part 2") << QStringLiteral("mpeg4") << QStringLiteral("mpeg4")
                                       << QStringLiteral("MPEG-4 Part 2");
        QTest::newRow("ProRes") << QStringLiteral("prores") << QStringLiteral("prores") << QStringLiteral("ProRes");
    }

    void videoCodec()
    {
        QFETCH(QString, file);
        QFETCH(QString, codec);
        QFETCH(QString, unsupported);
        const QString path = file == QLatin1String("mp4")     ? m_mp4
                             : file == QLatin1String("hevc")  ? m_hevc
                             : file == QLatin1String("mpeg4") ? m_mpeg4
                             : file == QLatin1String("prores") ? m_prores
                                                               : file;
        QString got;
        QString error;
        QVERIFY2(readVideoCodec(path, &got, &error), qPrintable(error));
        QCOMPARE(got, codec);
        QCOMPARE(browserUnsupportedVideoCodec(got).value_or(QString()), unsupported);
        QVERIFY(!readVideoCodec(media("bild1.png") + QStringLiteral(".fehlt"), &got, &error));
    }

    // P10b, Punkt 81 und Teil B 6 im Kern: Hinweis beim Hinzufügen (ohne und mit Quelle) und in der
    // Medienliste; neue Quelle aus der Medienliste mit Mitte an einer Stelle, in Originalgrösse, an
    // einer Stelle der Zeichenreihenfolge; nichts wird kopiert.
    void coreFormatsAndPlacement()
    {
        if (!DSS_MEDIA_SIZE)
            QSKIP("Der Kern liest Format und Grösse nur mit DSS_MEDIA_SIZE");
        Core core;
        QVERIFY2(core.start(), qPrintable(core.log));
        ChangeRequest add = request(msg::kAddMedia);
        add.file = m_hevc;
        ChangeResult r = core.change(add);
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(r.warning, QStringLiteral("hevc.mp4 (HEVC) wird in der Browserquelle nicht angezeigt. In H.264 "
                                           "oder VP9 umwandeln."));
        add.file = m_mp4;
        r = core.change(add);
        QVERIFY(r.ok);
        QCOMPARE(r.warning, QString());
        add.file = m_prores;
        r = core.change(add);
        QVERIFY(r.ok);
        QCOMPARE(r.warning, browserVideoWarning(QStringLiteral("zaehler.mp4"), QStringLiteral("ProRes")));
        QTRY_COMPARE_WITH_TIMEOUT(core.client->mediaList().files.size(), qsizetype(3), 3000);
        for (const MediaFileData &f : core.client->mediaList().files) {
            QCOMPARE(f.kind, MediaKind::Video);
            if (f.entry.name == QLatin1String("video.mp4")) {
                QCOMPARE(f.codec, QStringLiteral("h264"));
                QVERIFY(f.browserProblem.isEmpty());
            } else {
                QVERIFY2(f.browserProblem.startsWith(f.entry.name + QStringLiteral(" (")), qPrintable(f.browserProblem));
            }
            QVERIFY(f.usedIn.isEmpty());
            QCOMPARE(f.bytes, QFileInfo(f.entry.name == QLatin1String("hevc.mp4") ? m_hevc
                                        : f.entry.name == QLatin1String("video.mp4") ? m_mp4 : m_prores).size());
        }
        QVERIFY(testhelp::waitForLog(core.process.get(), QStringLiteral("hevc.mp4 (HEVC) wird in der Browserquelle"),
                                     &core.log));

        // Mit Quelle (Knopf im Dock «Quellen»): Hinweis ebenso
        ChangeRequest create = request(msg::kCreateOverlay);
        create.name = QStringLiteral("Lage");
        create.width = 1920;
        create.height = 1080;
        const QUuid overlay = core.change(create).id;
        QVERIFY(!overlay.isNull());
        ChangeRequest video = request(msg::kAddVideoSource, overlay);
        video.file = m_mpeg4;
        r = core.change(video);
        QVERIFY(r.ok);
        QVERIFY2(r.warning.contains(QStringLiteral("mpeg4.mp4 (MPEG-4 Part 2) wird in der Browserquelle nicht angezeigt")),
                 qPrintable(r.warning));

        // Aus der Medienliste: Bild 256x256 mit Mitte bei 100/200 (wie an der Maus abgelegt)
        add.file = media("bild1.png");
        QVERIFY(core.change(add).ok);
        QTRY_COMPARE_WITH_TIMEOUT(core.client->mediaList().files.size(), qsizetype(5), 3000);
        QString image;
        QString webm;
        for (const MediaFileData &f : core.client->mediaList().files) {
            if (f.entry.name == QLatin1String("bild1.png"))
                image = f.entry.file;
        }
        add.file = media("video.webm");
        QVERIFY(core.change(add).ok);
        QTRY_COMPARE_WITH_TIMEOUT(core.client->mediaList().files.size(), qsizetype(6), 3000);
        for (const MediaFileData &f : core.client->mediaList().files) {
            if (f.entry.name == QLatin1String("video.webm"))
                webm = f.entry.file;
        }
        const QStringList filesBefore = QDir(core.data.filePath(QStringLiteral("media"))).entryList(QDir::Files);
        ChangeRequest placed = request(msg::kAddImageSource, overlay);
        placed.file = image;
        placed.centerX = 100;
        placed.centerY = 200;
        r = core.change(placed);
        QVERIFY2(r.ok, qPrintable(r.message));
        QTRY_VERIFY_WITH_TIMEOUT(core.client->overlay(overlay) && core.client->overlay(overlay)->sources.size() == 2, 3000);
        Source s = core.client->overlay(overlay)->sources.last();
        QCOMPARE(s.id, r.id);
        QCOMPARE(s.name, QStringLiteral("bild1.png"));
        QCOMPARE(QRect(s.x, s.y, s.width, s.height), QRect(-28, 72, 256, 256));
        // Video 960x540 in die Liste «Quellen» an Stelle 0 (ganz hinten), Mitte der Leinwand
        ChangeRequest listed = request(msg::kAddVideoSource, overlay);
        listed.file = webm;
        listed.centerX = 960;
        listed.centerY = 540;
        listed.insertIndex = 0;
        r = core.change(listed);
        QVERIFY2(r.ok, qPrintable(r.message));
        QTRY_VERIFY_WITH_TIMEOUT(core.client->overlay(overlay)->sources.size() == 3, 3000);
        s = core.client->overlay(overlay)->sources.first();
        QCOMPARE(s.id, r.id);
        QCOMPARE(QRect(s.x, s.y, s.width, s.height), QRect(480, 270, 960, 540));
        QCOMPARE(s.kind, SourceKind::Video);
        QVERIFY(!s.loop); // Standard wie beim Knopf
        // Nichts kopiert; jetzt verwendet
        QCOMPARE(QDir(core.data.filePath(QStringLiteral("media"))).entryList(QDir::Files), filesBefore);
        QTRY_VERIFY_WITH_TIMEOUT(core.client->mediaList().file(webm)->usedIn.size() == 1, 3000);
        QCOMPARE(core.client->mediaList().file(webm)->usedIn.first().overlayName, QStringLiteral("Lage"));
        // Verweis, den es nicht gibt: abgewiesen
        placed.file = QStringLiteral("media/") + QString(64, QLatin1Char('0')) + QStringLiteral(".png");
        r = core.change(placed);
        QVERIFY(!r.ok);
        QVERIFY2(r.message.contains(QStringLiteral("fehlt im Datenordner")), qPrintable(r.message));
    }

    void mediaSize_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("height");
        QTest::newRow("PNG") << media("bild1.png") << 256 << 256;
        QTest::newRow("JPEG") << QString() << 256 << 256;
        QTest::newRow("WebM mit Alpha") << media("alpha.webm") << 512 << 512;
        QTest::newRow("WebM") << media("video.webm") << 960 << 540;
        QTest::newRow("MP4") << QStringLiteral("mp4") << 960 << 540;
    }

    void mediaSize()
    {
        QFETCH(QString, file);
        QFETCH(int, width);
        QFETCH(int, height);
        if (file.isEmpty())
            file = m_jpg;
        else if (file == QLatin1String("mp4"))
            file = m_mp4;
        int w = 0;
        int h = 0;
        QString error;
        QVERIFY2(readMediaSize(file, &w, &h, &error), qPrintable(error));
        QCOMPARE(w, width);
        QCOMPARE(h, height);
    }

    void mediaSizeUnreadable()
    {
        const QString broken = m_work.filePath(QStringLiteral("kaputt.png"));
        QFile f(broken);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("kein Bild");
        f.close();
        int w = 0;
        int h = 0;
        QString error;
        QVERIFY(!readMediaSize(broken, &w, &h, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!readMediaSize(m_work.filePath(QStringLiteral("fehlt.webm")), &w, &h, &error));
    }

    // Neue Quellen im Speicher (wie Kern und dss-cli): Grösse der Datei, sonst des Overlays.
    void storeUsesFileSize()
    {
        QTemporaryDir data;
        OverlayStore store(data.path());
        QString error;
        QVERIFY(store.load(&error));
        store.setMediaSizeReader(readMediaSize);
        const QUuid id = store.createOverlay(QStringLiteral("Test"), 1280, 720, &error);
        QVERIFY(!id.isNull());
        QString warning;
        const QUuid image = store.addImageSource(id, m_jpg, &error, &warning);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QVERIFY(warning.isEmpty());
        const QUuid video = store.addVideoSource(id, media("alpha.webm"), false, &error, &warning, true);
        QVERIFY2(!video.isNull(), qPrintable(error));
        const QUuid mp4 = store.addVideoSource(id, m_mp4, true, &error, &warning);
        QVERIFY2(!mp4.isNull(), qPrintable(error));
        const QString broken = m_work.filePath(QStringLiteral("kaputt2.png"));
        QFile f(broken);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("kein Bild");
        f.close();
        const QUuid bad = store.addImageSource(id, broken, &error, &warning);
        QVERIFY2(!bad.isNull(), qPrintable(error));
        QVERIFY2(warning.contains(QStringLiteral("nicht lesbar")) && warning.contains(QStringLiteral("1280x720")),
                 qPrintable(warning));

        const Overlay o = *store.overlay(id);
        const auto sizeOf = [&o](const QUuid &s) {
            const Source *src = findSource(o, s);
            return src ? QSize(src->width, src->height) : QSize();
        };
        QCOMPARE(sizeOf(image), QSize(256, 256));
        QCOMPARE(sizeOf(video), QSize(512, 512));
        QCOMPARE(sizeOf(mp4), QSize(960, 540));
        QCOMPARE(sizeOf(bad), QSize(1280, 720));
        QCOMPARE(findSource(o, video)->holdAtEnd, true);
        QCOMPARE(findSource(o, video)->loop, false);
        QCOMPARE(findSource(o, mp4)->holdAtEnd, false);
        // Neue Quellen liegen bei (0, 0) ohne Drehung
        QCOMPARE(findSource(o, image)->x, 0);
        QCOMPARE(findSource(o, image)->rotation, 0);

        // P09b, Entscheid 7: gespeichert 32x64, mit EXIF-Ausrichtung 5 bis 8 angezeigt 64x32
        // (libav liefert die gespeicherte Grösse, der Store vertauscht).
        for (const QString &name : {QStringLiteral("orientierung-6.jpg"), QStringLiteral("orientierung-5.png"),
                                   QStringLiteral("orientierung-3.jpg")}) {
            int w = 0;
            int h = 0;
            const QString file = QStringLiteral(DSS_TEST_EXIF_DIR "/") + name;
            QVERIFY2(readMediaSize(file, &w, &h, &error), qPrintable(error));
            QCOMPARE(QSize(w, h), name.contains(QLatin1Char('3')) ? QSize(64, 32) : QSize(32, 64));
            const QUuid s = store.addImageSource(id, file, &error, &warning);
            QVERIFY2(!s.isNull(), qPrintable(error));
            const Source *added = findSource(*store.overlay(id), s);
            QCOMPARE(QSize(added->width, added->height), QSize(64, 32));
            QCOMPARE(store.mediaSizes(*store.overlay(id)).value(added->file), QSize(64, 32));
            QVERIFY(store.removeSource(id, s, &error));
        }

        // P09b: Originalgrösse für die Nachrichten an die Fenster (media_sizes). Jede Datei wird
        // nur einmal gelesen, auch eine unlesbare; unlesbare Dateien fehlen.
        {
            OverlayStore again(data.path());
            QVERIFY(again.load(&error));
            int calls = 0;
            again.setMediaSizeReader([&calls](const QString &path, int *w, int *h, QString *e) {
                ++calls;
                return readMediaSize(path, w, h, e);
            });
            const MediaSizes sizes = again.mediaSizes(*again.overlay(id));
            QCOMPARE(calls, 4);
            QCOMPARE(sizes.size(), 3);
            QCOMPARE(sizes.value(findSource(o, image)->file), QSize(256, 256));
            QCOMPARE(sizes.value(findSource(o, video)->file), QSize(512, 512));
            QCOMPARE(sizes.value(findSource(o, mp4)->file), QSize(960, 540));
            QVERIFY(!sizes.contains(findSource(o, bad)->file));
            QCOMPARE(again.mediaSizes(*again.overlay(id)), sizes);
            QCOMPARE(calls, 4);
            again.setMediaSizeReader({});
            QVERIFY(again.mediaSizes(*again.overlay(id)).isEmpty());
        }

        // Ohne Leser (Schalter DSS_MEDIA_SIZE aus): Grösse des Overlays wie in P08
        store.setMediaSizeReader({});
        warning.clear();
        const QUuid plain = store.addImageSource(id, media("bild1.png"), &error, &warning);
        QCOMPARE(sizeOf(plain), QSize()); // o ist eine Kopie von vorher
        QCOMPARE(findSource(*store.overlay(id), plain)->width, 1280);
        QVERIFY(warning.isEmpty());
    }

    void alphaDecoderAndNumber()
    {
        // Gegenprobe wie P01/P02: Mittel des Alphakanals im ersten Bild mit ffmpeg.
        bool ok = false;
        const QString out = runFfmpeg({QStringLiteral("-c:v"), QStringLiteral("libvpx-vp9"), QStringLiteral("-i"),
                                       media("alpha.webm"), QStringLiteral("-frames:v"), QStringLiteral("1"),
                                       QStringLiteral("-vf"),
                                       QStringLiteral("alphaextract,signalstats,metadata=print:key=lavfi.signalstats.YAVG"),
                                       QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")}, &ok);
        QVERIFY2(ok, qPrintable(out));
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("YAVG=([0-9.]+)")).match(out);
        QVERIFY(m.hasMatch());
        const double reference = m.captured(1).toDouble();
        qInfo("Alpha-Mittel erstes Bild, ffmpeg: %.4f", reference);

        VideoPlayer alpha(media("alpha.webm"), true, false);
        QVERIFY(waitFor([&] { return alpha.currentFrame() != nullptr; }, 3000));
        QCOMPARE(alpha.decoderName(), QStringLiteral("libvpx-vp9"));
        const double mean = alpha.statistics().firstAlphaMean;
        qInfo("Alpha-Mittel erstes Bild, VideoPlayer: %.4f", mean);
        QVERIFY2(std::abs(mean - reference) < 0.001, qPrintable(QString::number(mean)));
        const auto frame = alpha.currentFrame();
        QCOMPARE(frame->width, 512);
        QCOMPARE(frame->height, 512);
        // Vormultipliziert: keine Farbe grösser als Alpha
        bool premultiplied = true;
        for (int i = 0; i < frame->width * frame->height && premultiplied; ++i) {
            const uint8_t *p = frame->data.data() + 4 * i;
            premultiplied = p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3];
        }
        QVERIFY(premultiplied);

        VideoPlayer plain(media("video.webm"), true, false);
        QVERIFY(waitFor([&] { return plain.currentFrame() != nullptr; }, 3000));
        QCOMPARE(plain.decoderName(), QStringLiteral("vp9"));
        QCOMPARE(plain.statistics().firstAlphaMean, -1.0);
    }

    void unreadableVideo()
    {
        VideoPlayer p(m_work.filePath(QStringLiteral("fehlt.webm")), false, false);
        QVERIFY(waitFor([&] { return p.state() == VideoPlayer::State::Failed; }, 3000));
        QVERIFY(!p.error().isEmpty());
        QVERIFY(p.currentFrame() == nullptr);
    }

    void stopsQuickly()
    {
        QElapsedTimer t;
        t.start();
        {
            VideoPlayer p(media("alpha.webm"), true, false);
            QVERIFY(waitFor([&] { return p.currentFrame() != nullptr; }, 3000));
            t.restart();
        }
        QVERIFY2(t.elapsed() < 500, qPrintable(QString::number(t.elapsed())));
    }

    // Entscheid 9, mit alpha.webm (10 s, 30 Bilder/s): drei Spieler gleichzeitig.
    void endBehaviour()
    {
        VideoPlayer empty(media("alpha.webm"), false, false);
        VideoPlayer hold(media("alpha.webm"), false, true);
        VideoPlayer loop(media("alpha.webm"), true, true); // mit loop hat hold keine Wirkung
        QElapsedTimer t;
        t.start();
        QVERIFY(waitFor([&] { return empty.currentFrame() && hold.currentFrame() && loop.currentFrame(); }, 3000));
        // Takt: nach 5 s steht das Bild um 5 s (±0.3 s)
        QThread::msleep(5000);
        const double at5 = hold.currentFrame()->pts;
        qInfo("nach %lld ms: pts %.3f s", t.elapsed(), at5);
        QVERIFY2(std::abs(at5 - t.elapsed() / 1000.0) < 0.3, qPrintable(QString::number(at5)));
        // Abholen wie die Leinwand; der Zustand beschreibt, was gezeigt wird.
        QVERIFY(waitFor([&] {
            empty.currentFrame();
            hold.currentFrame();
            loop.currentFrame();
            return empty.state() == VideoPlayer::State::Ended && hold.state() == VideoPlayer::State::Holding;
        }, 8000));
        qInfo("Ende nach %lld ms", t.elapsed());
        QVERIFY(t.elapsed() >= 9800);
        // Ohne Einstellung: leer
        QVERIFY(empty.currentFrame() == nullptr);
        // Am Ende anhalten: letztes Bild (pts 9.967 s) bleibt
        QVERIFY(hold.currentFrame() != nullptr);
        QVERIFY2(hold.currentFrame()->pts > 9.9, qPrintable(QString::number(hold.currentFrame()->pts)));
        // Wiederholen: läuft weiter, wieder am Anfang
        QVERIFY(waitFor([&] { loop.currentFrame(); return loop.statistics().loops >= 1; }, 2000));
        QThread::msleep(500);
        QCOMPARE(loop.state(), VideoPlayer::State::Playing);
        QVERIFY(loop.currentFrame() != nullptr);
        QVERIFY2(loop.currentFrame()->pts < 2.0, qPrintable(QString::number(loop.currentFrame()->pts)));
        for (VideoPlayer *p : {&empty, &hold, &loop}) {
            const VideoPlayer::Statistics st = p->statistics();
            qInfo("dekodiert %llu, verspätet (> 50 ms) %llu, verworfen %llu, Wiederholungen %llu",
                  static_cast<unsigned long long>(st.decoded), static_cast<unsigned long long>(st.late),
                  static_cast<unsigned long long>(st.dropped), static_cast<unsigned long long>(st.loops));
            QVERIFY(st.decoded >= 300);
            QCOMPARE(st.late, 0ULL);
        }
        QCOMPARE(empty.statistics().decoded, 300ULL);
    }

    // Takt wie die Leinwand (geplante Zeitpunkte n/30 s, Abfrage etwas später wie beim Zeichnen):
    // jeder Takt bekommt genau ein neues Bild, keines doppelt, keines verworfen.
    void oneFramePerTick()
    {
        using Clock = VideoPlayer::Clock;
        VideoPlayer p(media("alpha.webm"), true, false);
        QVERIFY(waitFor([&] { return p.currentFrame() != nullptr; }, 3000));
        const Clock::time_point start = Clock::now();
        const auto period = std::chrono::nanoseconds(1'000'000'000 / 30);
        uint64_t last = p.frameSerial();
        int same = 0;
        int skipped = 0;
        for (int n = 1; n <= 150; ++n) {
            const Clock::time_point due = start + n * period;
            std::this_thread::sleep_until(due + std::chrono::milliseconds(n % 7)); // Schwanken
            p.frameAt(due);
            const uint64_t serial = p.frameSerial();
            if (serial == last)
                ++same;
            else if (serial > last + 1)
                ++skipped;
            last = serial;
        }
        qInfo("150 Takte: %d ohne neues Bild, %d mit mehr als einem", same, skipped);
        QCOMPARE(same, 0);
        QCOMPARE(skipped, 0);
        QCOMPARE(p.statistics().dropped, 0ULL);
    }

    // P10a, Punkt 66: wie die Leinwand, schon die erste Abfrage mit dem geplanten Zeitpunkt eines
    // Takts, der irgendwann nach dem Öffnen beginnt. Die Versätze 7,9, 8,7 und 9,0 ms waren vor
    // P10a ungleichmässig (Messung im Bericht P10a). Der Anker legt die Bilder auf das Raster.
    void anchorOnTick()
    {
        using Clock = VideoPlayer::Clock;
        const auto period = std::chrono::nanoseconds(1'000'000'000 / 30);
        for (const int offsetUs : {7900, 8700, 9000}) {
            VideoPlayer p(media("alpha.webm"), true, false);
            const Clock::time_point start = Clock::now() + std::chrono::microseconds(offsetUs);
            uint64_t last = 0;
            bool seen = false;
            int fresh = 0;
            int bad = 0;
            for (int n = 1; fresh + bad < 45 && n < 200; ++n) {
                const Clock::time_point due = start + n * period;
                std::this_thread::sleep_until(due + std::chrono::milliseconds(n % 7)); // Schwanken
                const bool has = p.frameAt(due) != nullptr;
                const uint64_t serial = p.frameSerial();
                if (seen)
                    ++(serial == last + 1 ? fresh : bad);
                seen = seen || has;
                last = serial;
            }
            qInfo("Versatz %.1f ms: %d Takte mit genau einem neuen Bild, %d ungleichmässig",
                  offsetUs / 1000.0, fresh, bad);
            QCOMPARE(bad, 0);
            QCOMPARE(fresh, 45);
        }
    }

    // P10a, Punkt 66: Toleranz der Zeitpunkte ohne Takt in Echtzeit. Zeitstempel von alpha.webm
    // (ganze Millisekunden, 0, 33, 67, 100 …) gegen einen Takt n/30 s, der genau beim ersten Bild
    // beginnt (wie die Leinwand, deren erste Abfrage den Beginn setzt): Ohne Toleranz kommt jedes
    // dritte Bild einen Takt zu spät, mit kDueTolerance jeder Takt genau ein neues Bild.
    void dueTolerance()
    {
        using Clock = VideoPlayer::Clock;
        QProcess probe;
        probe.start(QStringLiteral("ffprobe"),
                    {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"),
                     QStringLiteral("v:0"), QStringLiteral("-show_entries"), QStringLiteral("packet=pts"),
                     QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"), media("alpha.webm")});
        QVERIFY(probe.waitForFinished(30000) && probe.exitCode() == 0);
        QList<int> ptsMs;
        for (const QByteArray &line : probe.readAllStandardOutput().split('\n')) {
            if (!line.trimmed().isEmpty())
                ptsMs.append(line.trimmed().toInt());
        }
        std::sort(ptsMs.begin(), ptsMs.end());
        QCOMPARE(ptsMs.size(), qsizetype(300));
        QCOMPARE(ptsMs.mid(0, 4), (QList<int>{0, 33, 67, 100})); // Zeitbasis 1/1000
        const Clock::time_point base{};
        const auto period = std::chrono::nanoseconds(1'000'000'000 / 30);
        // Takte ohne neues Bild (beim nächsten kommen dann zwei)
        const auto irregular = [&](auto isDue) {
            int bad = 0;
            qsizetype next = 1;
            for (int n = 1; n < ptsMs.size(); ++n) {
                int fresh = 0;
                while (next < ptsMs.size() && isDue(base + std::chrono::milliseconds(ptsMs.at(next)),
                                                    base + n * period)) {
                    ++fresh;
                    ++next;
                }
                bad += fresh != 1;
            }
            return bad;
        };
        const int without = irregular([](Clock::time_point due, Clock::time_point t) { return due <= t; });
        const int with = irregular(&VideoPlayer::isDue);
        qInfo("299 Takte: ohne Toleranz %d ungleichmässig, mit %lld ms %d", without,
              static_cast<long long>(VideoPlayer::kDueTolerance.count()), with);
        QVERIFY(without > 100); // jedes dritte Bild zu spät, das nächste doppelt
        QCOMPARE(with, 0);
        // Grenze: genau 1 ms nach dem gefragten Zeitpunkt noch fällig, 1 ns später nicht
        QVERIFY(VideoPlayer::isDue(base + std::chrono::milliseconds(1), base));
        QVERIFY(!VideoPlayer::isDue(base + std::chrono::milliseconds(1) + std::chrono::nanoseconds(1), base));
    }

    // P10a, Punkt 65: Display-Matrix → EXIF-Ausrichtung wie ffmpeg mit autorotate. Matrizen
    // [a b 0; c d 0; 0 0 1] in 16.16 Festkomma wie libavutil/display.h.
    void displayMatrix_data()
    {
        QTest::addColumn<int>("a");
        QTest::addColumn<int>("b");
        QTest::addColumn<int>("c");
        QTest::addColumn<int>("d");
        QTest::addColumn<int>("orientation");
        QTest::newRow("ohne Drehung") << 1 << 0 << 0 << 1 << 1;
        QTest::newRow("90° im Uhrzeigersinn (clock)") << 0 << 1 << -1 << 0 << 6;
        QTest::newRow("180°") << -1 << 0 << 0 << -1 << 3;
        QTest::newRow("270° im Uhrzeigersinn (cclock)") << 0 << -1 << 1 << 0 << 8;
        QTest::newRow("hflip") << -1 << 0 << 0 << 1 << 2;
        QTest::newRow("vflip") << 1 << 0 << 0 << -1 << 4;
        QTest::newRow("transpose (cclock_flip)") << 0 << 1 << 1 << 0 << 5;
        QTest::newRow("transverse (clock_flip)") << 0 << -1 << -1 << 0 << 7;
        QTest::newRow("keine Matrix (alles 0)") << 0 << 0 << 0 << 0 << 1;
    }

    void displayMatrix()
    {
        QFETCH(int, a);
        QFETCH(int, b);
        QFETCH(int, c);
        QFETCH(int, d);
        QFETCH(int, orientation);
        const int32_t m[9] = {a * 65536, b * 65536, 0, c * 65536, d * 65536, 0, 0, 0, 1 << 30};
        QCOMPARE(displayMatrixOrientation(m), orientation);
        // Massstab ändert nichts (Matrix mit doppelter Grösse)
        const int32_t m2[9] = {2 * m[0], 2 * m[1], 0, 2 * m[3], 2 * m[4], 0, 0, 0, 1 << 30};
        QCOMPARE(displayMatrixOrientation(m2), orientation);
        QCOMPARE(displayMatrixOrientation(nullptr), 1);
    }

    // P10a, Punkt 65: Testvideos aus tests/daten/video-drehung (erzeugen.sh). Alle zeigen nach der
    // Drehung das Bild 64x32 mit den Vierteln rot, grün / blau, gelb: Originalgrösse, Ausrichtung
    // der Bilder des Players und die Farben, gegengeprüft mit ffmpeg (dreht von sich aus).
    void videoRotation_data()
    {
        QTest::addColumn<int>("degrees");
        QTest::addColumn<int>("orientation");
        QTest::addColumn<QSize>("stored");
        QTest::newRow("0") << 0 << 1 << QSize(64, 32);
        QTest::newRow("90") << 90 << 6 << QSize(32, 64);
        QTest::newRow("180") << 180 << 3 << QSize(64, 32);
        QTest::newRow("270") << 270 << 8 << QSize(32, 64);
    }

    void videoRotation()
    {
        QFETCH(int, degrees);
        QFETCH(int, orientation);
        QFETCH(QSize, stored);
        const QString file = rotationVideo(degrees);
        int w = 0;
        int h = 0;
        QString error;
        QVERIFY2(readMediaSize(file, &w, &h, &error), qPrintable(error));
#if DSS_VIDEO_ROTATION
        QCOMPARE(QSize(w, h), QSize(64, 32));
#else
        QCOMPARE(QSize(w, h), stored);
        orientation = 1;
#endif
        VideoPlayer p(file, true, false);
        QVERIFY(waitFor([&] { return p.currentFrame() != nullptr; }, 3000));
        const std::shared_ptr<const VideoFrame> f = p.currentFrame();
        QCOMPARE(QSize(f->width, f->height), stored);
        QCOMPARE(f->orientation, orientation);
        if (!DSS_VIDEO_ROTATION)
            return;
        // Gegenprobe: ffmpeg dreht beim Dekodieren selbst (autorotate), Ausgabe RGB 64x32
        QProcess ff;
        ff.start(QStringLiteral("ffmpeg"), {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), file,
                                            QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-f"),
                                            QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"),
                                            QStringLiteral("rgb24"), QStringLiteral("-")});
        QVERIFY(ff.waitForFinished(30000) && ff.exitCode() == 0);
        const QByteArray reference = ff.readAllStandardOutput();
        QCOMPARE(reference.size(), qsizetype(64 * 32 * 3));
        const struct { QPoint at; std::array<int, 3> color; } quarters[] = {
            {{16, 8}, {255, 0, 0}}, {{48, 8}, {0, 255, 0}}, {{16, 24}, {0, 0, 255}}, {{48, 24}, {255, 255, 0}}};
        for (const auto &q : quarters) {
            const std::array<int, 3> mine = shownPixel(*f, q.at.x(), q.at.y());
            for (int i = 0; i < 3; ++i) {
                const int ref = uint8_t(reference.at((q.at.y() * 64 + q.at.x()) * 3 + i));
                QVERIFY2(std::abs(mine[i] - q.color[i]) <= 12 && std::abs(mine[i] - ref) <= 4,
                         qPrintable(QStringLiteral("%1° bei %2/%3 Kanal %4: %5, ffmpeg %6, erwartet %7")
                                        .arg(degrees).arg(q.at.x()).arg(q.at.y()).arg(i)
                                        .arg(mine[i]).arg(ref).arg(q.color[i])));
            }
        }
    }

    // P10a, Punkt 65: Grösse einer neuen Videoquelle folgt der Drehung (Kern, OverlayStore)
    void storeVideoRotation()
    {
        QTemporaryDir data;
        OverlayStore store(data.path());
        QString error;
        QVERIFY(store.load(&error));
        store.setMediaSizeReader(readMediaSize);
        const QUuid id = store.createOverlay(QStringLiteral("Drehung"), 1920, 1080, &error);
        for (int degrees : {0, 90, 180, 270}) {
            const QUuid s = store.addVideoSource(id, rotationVideo(degrees), true, &error);
            QVERIFY2(!s.isNull(), qPrintable(error));
            const Source *added = findSource(*store.overlay(id), s);
            const QSize expected = DSS_VIDEO_ROTATION || degrees % 180 == 0 ? QSize(64, 32) : QSize(32, 64);
            QCOMPARE(QSize(added->width, added->height), expected);
            QCOMPARE(store.mediaSizes(*store.overlay(id)).value(added->file), expected);
        }
    }

#ifdef DSS_CLI_BIN
    // P10b, Teil A 6: dss-cli zeigt die Medienliste (mit Name, Ordner, Typ, Grösse, «verwendet in»
    // und Hinweis) und ändert sie über den laufenden Kern; ohne Kern ändert es nichts (Exit 4).
    void cliMedia()
    {
        using testhelp::runCli;
        using testhelp::runCliOnly;
        Core core;
        QVERIFY2(core.start(), qPrintable(core.log));
        const QStringList via{QStringLiteral("--verbindung"), core.socket};
        ChangeRequest add = request(msg::kAddMedia);
        add.file = media("bild1.png");
        QVERIFY(core.change(add).ok);
        add.file = m_hevc;
        QVERIFY(core.change(add).ok);
        ChangeRequest create = request(msg::kCreateOverlay);
        create.name = QStringLiteral("Szene");
        create.width = 1920;
        create.height = 1080;
        const QUuid overlay = core.change(create).id;
        ChangeRequest source = request(msg::kAddImageSource, overlay);
        source.file = media("bild1.png");
        QVERIFY(core.change(source).ok);

        // medien-liste liest den Datenordner; der Kern speichert das Overlay zusammengefasst
        testhelp::CliResult r;
        QTRY_VERIFY_WITH_TIMEOUT((r = runCli(core.data.path(), {QStringLiteral("medien-liste")})).out.contains(
                                     QStringLiteral("Overlay «Szene»")),
                                 10000);
        QCOMPARE(r.exitCode, 0);
        const QStringList lines = r.out.split(QLatin1Char('\n'));
        QVERIFY2(lines.first().contains(QRegularExpression(
                     QStringLiteral("^Nr +Name +Ordner +Typ +Grösse +Format +Datei +Verwendet in$"))),
                 qPrintable(r.out));
        QVERIFY2(r.out.contains(QRegularExpression(QStringLiteral("1 +bild1\\.png +/ +Bild +19\\.3 KiB +– +media/[0-9a-f]{64}\\.png +Overlay «Szene» \\(Quelle «bild1\\.png»\\)"))),
                 qPrintable(r.out));
        QVERIFY2(r.out.contains(QRegularExpression(QStringLiteral("2 +hevc\\.mp4 +/ +Video +[0-9.]+ KiB +hevc +media/[0-9a-f]{64}\\.mp4 +ungenutzt"))),
                 qPrintable(r.out));
        QVERIFY2(r.out.contains(QStringLiteral("Hinweis: Nr 2: hevc.mp4 (HEVC) wird in der Browserquelle nicht angezeigt.")),
                 qPrintable(r.out));

        r = runCliOnly(via + QStringList{QStringLiteral("medien-ordner-anlegen"), QStringLiteral("Clips")});
        QCOMPARE(r.exitCode, 0);
        r = runCliOnly(via + QStringList{QStringLiteral("medien-ordner-anlegen"), QStringLiteral("Clips/Alt")});
        QCOMPARE(r.exitCode, 0);
        r = runCliOnly(via + QStringList{QStringLiteral("medien-ordner-anlegen"), QStringLiteral("Fehlt/Neu")});
        QCOMPARE(r.exitCode, 1);
        QVERIFY2(r.err.contains(QStringLiteral("Ordner «Fehlt» gibt es nicht.")), qPrintable(r.err));
        r = runCliOnly(via + QStringList{QStringLiteral("medium-verschieben"), QStringLiteral("hevc.mp4"), QStringLiteral("Clips/Alt")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.out, QStringLiteral("«hevc.mp4» liegt jetzt in «Clips/Alt».\n"));
        r = runCliOnly(via + QStringList{QStringLiteral("medium-umbenennen"), QStringLiteral("1"), QStringLiteral("Logo")});
        QCOMPARE(r.exitCode, 0);
        QCOMPARE(r.out, QStringLiteral("«bild1.png» heisst jetzt «Logo».\n"));
        r = runCliOnly(via + QStringList{QStringLiteral("medien-ordner-umbenennen"), QStringLiteral("Clips/Alt"), QStringLiteral("Früher")});
        QCOMPARE(r.exitCode, 0);
        r = runCliOnly(via + QStringList{QStringLiteral("medien-ordner-verschieben"), QStringLiteral("Clips/Früher"), QStringLiteral("/")});
        QCOMPARE(r.exitCode, 0);
        r = runCliOnly(via + QStringList{QStringLiteral("medien-ordner-verschieben"), QStringLiteral("Clips"), QStringLiteral("Clips")});
        QCOMPARE(r.exitCode, 1);
        r = runCli(core.data.path(), {QStringLiteral("medien-liste")});
        QVERIFY2(r.out.contains(QRegularExpression(QStringLiteral("hevc\\.mp4 +Früher +Video"))), qPrintable(r.out));
        QVERIFY2(r.out.contains(QRegularExpression(QStringLiteral("Logo +/ +Bild"))), qPrintable(r.out));
        QVERIFY2(r.out.contains(QStringLiteral("Ordner ohne Dateien: Clips")), qPrintable(r.out));
        // Löschen: verwendet abgewiesen, ungenutzt gelöscht (Datei weg)
        r = runCliOnly(via + QStringList{QStringLiteral("medium-loeschen"), QStringLiteral("Logo")});
        QCOMPARE(r.exitCode, 1);
        QVERIFY2(r.err.contains(QStringLiteral("«Logo» lässt sich nicht löschen, verwendet in Overlay «Szene»")), qPrintable(r.err));
        QString hevcRef;
        for (const MediaFileData &f : core.client->mediaList().files) {
            if (f.kind == MediaKind::Video)
                hevcRef = f.entry.file;
        }
        QVERIFY(!hevcRef.isEmpty());
        r = runCliOnly(via + QStringList{QStringLiteral("medium-loeschen"), hevcRef});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(!QFile::exists(core.data.filePath(hevcRef)));
        r = runCliOnly(via + QStringList{QStringLiteral("medien-ordner-loeschen"), QStringLiteral("Clips")});
        QCOMPARE(r.exitCode, 0);
        // medien.json schreibt nur der Kern; ohne Kern: Exit 4, Datei unverändert
        const QByteArray json = [&] {
            QFile f(core.data.filePath(QStringLiteral("medien.json")));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        QVERIFY(json.contains("\"Logo\""));
        core.client->stop();
        core.process->terminate();
        QVERIFY(core.process->waitForFinished(5000));
        r = runCliOnly(via + QStringList{QStringLiteral("medium-umbenennen"), QStringLiteral("1"), QStringLiteral("x")});
        QCOMPARE(r.exitCode, 4);
        QVERIFY2(r.err.contains(QStringLiteral("ändert nur der Kern")), qPrintable(r.err));
        r = runCli(core.data.path(), {QStringLiteral("medium-umbenennen"), QStringLiteral("1"), QStringLiteral("x")});
        QCOMPARE(r.exitCode, 2); // --datenordner gilt hier nicht
        QFile f(core.data.filePath(QStringLiteral("medien.json")));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), json);
    }

    void cliHoldAtEnd()
    {
        QTemporaryDir data;
        using testhelp::runCli;
        QCOMPARE(runCli(data.path(), {QStringLiteral("overlay-neu"), QStringLiteral("Video Test"),
                                      QStringLiteral("1920"), QStringLiteral("1080")}).exitCode, 0);
        testhelp::CliResult r = runCli(data.path(), {QStringLiteral("quelle-video"), QStringLiteral("video-test"),
                                                     media("alpha.webm"), QStringLiteral("--am-ende-anhalten")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(r.out.contains(QStringLiteral("Am Ende anhalten ja")), qPrintable(r.out));
        QVERIFY2(r.out.contains(QStringLiteral("512x512")), qPrintable(r.out));
        r = runCli(data.path(), {QStringLiteral("quelle-liste"), QStringLiteral("video-test")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(r.out.contains(QStringLiteral("Am Ende anhalten")), qPrintable(r.out));
        // Beides zusammen geht (Wiederholen hat Vorrang), ohne Datei nicht
        r = runCli(data.path(), {QStringLiteral("quelle-video"), QStringLiteral("video-test"), media("alpha.webm"),
                                 QStringLiteral("--wiederholen"), QStringLiteral("--am-ende-anhalten")});
        QCOMPARE(r.exitCode, 0);
        r = runCli(data.path(), {QStringLiteral("quelle-bild"), QStringLiteral("video-test"), media("bild1.png"),
                                 QStringLiteral("--am-ende-anhalten")});
        QVERIFY(r.exitCode != 0);
    }

    // P09b, Entscheid 4: dss-cli quelle-geometrie mit Spiegeln und Spalte «Gespiegelt».
    void cliFlip()
    {
        QTemporaryDir data;
        using testhelp::runCli;
        QCOMPARE(runCli(data.path(), {QStringLiteral("overlay-neu"), QStringLiteral("Spiegel"),
                                      QStringLiteral("1920"), QStringLiteral("1080")}).exitCode, 0);
        QCOMPARE(runCli(data.path(), {QStringLiteral("quelle-bild"), QStringLiteral("spiegel"),
                                      media("bild1.png")}).exitCode, 0);
        testhelp::CliResult r = runCli(data.path(), {QStringLiteral("quelle-geometrie"), QStringLiteral("spiegel"),
                                                     QStringLiteral("1"), QStringLiteral("-10"), QStringLiteral("20"),
                                                     QStringLiteral("300"), QStringLiteral("200"), QStringLiteral("12.5"),
                                                     QStringLiteral("--waagrecht-gespiegelt")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(r.out.contains(QStringLiteral("gespiegelt waagrecht")), qPrintable(r.out));
        r = runCli(data.path(), {QStringLiteral("quelle-liste"), QStringLiteral("spiegel")});
        QVERIFY2(r.out.contains(QStringLiteral("Gespiegelt")) && r.out.contains(QStringLiteral("waagrecht")),
                 qPrintable(r.out));
        r = runCli(data.path(), {QStringLiteral("quelle-geometrie"), QStringLiteral("spiegel"), QStringLiteral("1"),
                                 QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("300"),
                                 QStringLiteral("200"), QStringLiteral("0"), QStringLiteral("--waagrecht-gespiegelt"),
                                 QStringLiteral("--senkrecht-gespiegelt")});
        QVERIFY2(r.out.contains(QStringLiteral("gespiegelt beide")), qPrintable(r.out));
        // Ohne Option: nicht gespiegelt
        r = runCli(data.path(), {QStringLiteral("quelle-geometrie"), QStringLiteral("spiegel"), QStringLiteral("1"),
                                 QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("300"),
                                 QStringLiteral("200"), QStringLiteral("0")});
        QVERIFY2(r.out.contains(QStringLiteral("gespiegelt nein")), qPrintable(r.out));
        // Datei: Format 3 mit beiden Feldern
        OverlayStore store(data.path());
        QString error;
        QVERIFY(store.load(&error, OverlayStore::LoadMode::ReadOnly));
        QCOMPARE(store.overlays().size(), 1);
        QCOMPARE(store.overlays().first().sources.first().flipHorizontal, false);
        // Nur bei quelle-geometrie erlaubt
        r = runCli(data.path(), {QStringLiteral("quelle-liste"), QStringLiteral("spiegel"),
                                 QStringLiteral("--senkrecht-gespiegelt")});
        QCOMPARE(r.exitCode, 2);
    }
#endif
};

QTEST_GUILESS_MAIN(TestMedien)
#include "tst_medien.moc"
