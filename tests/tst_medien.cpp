// Bilder und Videos über dss_media (P09) ohne Qt Gui:
// - Grösse neuer Bild- und Videoquellen aus der Datei (Entscheid 6), PNG, JPEG, WebM, MP4, und
//   die Grösse des Overlays mit Warnung, wenn die Datei nicht lesbar ist
// - dss::VideoPlayer: Decoder (libvpx-vp9 für VP9 mit Alpha), Alpha im ersten Bild als Zahl wie
//   P02 (Gegenprobe mit ffmpeg … alphaextract,signalstats), Takt, Ende ohne Einstellung (leer),
//   «Am Ende anhalten» (letztes Bild bleibt) und «Wiederholen»
// - dss-cli quelle-video --am-ende-anhalten und die Spalte in quelle-liste
// Daten wie im Handtest: prototypes/media (alpha.webm, video.webm, bild1.png); JPEG und MP4
// werden mit ffmpeg aus Kopien davon erzeugt.

#include "mediainfo.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#include "videoplayer.h"
#include "windowdata.h"

#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

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

#ifdef DSS_CLI_BIN
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
#endif
};

QTEST_GUILESS_MAIN(TestMedien)
#include "tst_medien.moc"
