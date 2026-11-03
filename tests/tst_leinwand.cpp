// Zeichnen der Quellen auf der Leinwand (P09, Teil B) in der X11-Sitzung (CLAUDE.md Regel 6),
// ohne X11 «übersprungen» (Exit-Code 77). Leinwand und SourceRenderer wie in dss-app, Daten
// über OverlayStore in einem eigenen Datenordner (Kopien aus prototypes/media):
//   - Bild PNG (bild1.png) und gedrehtes, gestrecktes Bild (bild3.png 512x128, 90°)
//   - JPEG (aus bild1.png, deckend) mit alpha.webm darüber: wo das Video durchsichtig ist,
//     muss das JPEG zu sehen sein; Alpha-Mittel im ersten Bild als Zahl wie P02
//   - Text in zwei Zeilen in einer Schrift des Systems (DejaVu Sans), abgeschnitten am Kasten
//   - verborgenes Bild: nicht gezeichnet
//   - variable Schrift Urbanist[wght]: Gewicht 800 deckt deutlich mehr als 200 (Achse wght)
//   - gespiegelt (P09b): Bild mit roter linker und blauer rechter Hälfte, waagrecht gespiegelt,
//     und waagrecht gespiegelt plus 90° gedreht (erst spiegeln, dann drehen)
//   - EXIF-Ausrichtung (P09b): tests/daten/exif/orientierung-1 bis -8 als JPEG und PNG; jedes
//     zeigt nach der Ausrichtung 64x32 mit rot, grün (oben), blau, gelb (unten)
//   - Drehung aus den Metadaten von Videos (P10a, Punkt 65): tests/daten/video-drehung, 0°, 90°,
//     180°, 270°; jedes zeigt nach der Drehung dasselbe Bild wie die EXIF-Bilder
// Mit DSS_LEINWAND_BILD=DATEI wird das Bild der Leinwand gespeichert (Bericht).

#include "canvaswidget.h"
#include "mediainfo.h"
#include "overlaystore.h"
#include "sourcerenderer.h"
#include "theme.h"
#include "windowdata.h"

#include <algorithm>

#include <QApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace dss;

namespace {

const QString kMediaDir = QStringLiteral(DSS_TEST_MEDIA_DIR);

QString media(const char *name)
{
    return kMediaDir + QLatin1Char('/') + QLatin1String(name);
}

int distance(QColor a, QColor b)
{
    return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue())});
}

} // namespace

class TestLeinwand : public QObject
{
    Q_OBJECT

    QTemporaryDir m_data;
    QTemporaryDir m_work;
    Overlay m_overlay;
    QUuid m_image, m_rotated, m_jpeg, m_video, m_text, m_hidden, m_light, m_heavy, m_flipped, m_flippedRotated;
    // P09b: Ausrichtung 1 bis 8, erst JPEG, dann PNG; Lage links oben im Overlay
    QList<std::pair<QUuid, QPoint>> m_oriented;
    QList<std::pair<QUuid, QPoint>> m_rotatedVideos; // P10a
    std::unique_ptr<SourceRenderer> m_renderer;
    std::unique_ptr<CanvasWidget> m_canvas;
    Theme m_theme = darkTheme();

    bool videosShown() const
    {
        const QList<SourceRenderer::VideoInfo> infos = m_renderer->videoInfo();
        return infos.size() == 5
               && std::all_of(infos.begin(), infos.end(), [](const auto &v) { return v.hasFrame; });
    }

    SourceRenderer::VideoInfo videoInfo(const QUuid &id) const
    {
        for (const SourceRenderer::VideoInfo &v : m_renderer->videoInfo()) {
            if (v.sourceId == id)
                return v;
        }
        return {};
    }

    QImage grabCanvas()
    {
        m_canvas->repaint();
        return m_canvas->grab().toImage().convertToFormat(QImage::Format_RGB32);
    }
    QColor at(const QImage &image, QPointF overlayPoint)
    {
        return image.pixelColor(m_canvas->toWidget(overlayPoint).toPoint());
    }
    bool isChecker(QColor c)
    {
        return distance(c, m_theme.checkerLight) <= 1 || distance(c, m_theme.checkerDark) <= 1;
    }
    // Pixel im Kasten (Overlay), die nicht Schachbrett sind
    int inkIn(const QImage &image, QRect overlayBox)
    {
        const QPoint a = m_canvas->toWidget(overlayBox.topLeft()).toPoint();
        const QPoint b = m_canvas->toWidget(overlayBox.bottomRight()).toPoint();
        int ink = 0;
        for (int y = a.y(); y <= b.y(); ++y)
            for (int x = a.x(); x <= b.x(); ++x)
                ink += isChecker(image.pixelColor(x, y)) ? 0 : 1;
        return ink;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid() && m_work.isValid());
        // JPEG aus einer Kopie von bild1.png (deckend, Farbe 255/80/80 überall).
        const QString jpg = m_work.filePath(QStringLiteral("bild1.jpg"));
        QProcess ff;
        ff.start(QStringLiteral("ffmpeg"), {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                                            QStringLiteral("error"), QStringLiteral("-i"), media("bild1.png"),
                                            QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-q:v"),
                                            QStringLiteral("2"), jpg});
        QVERIFY(ff.waitForFinished(30000) && ff.exitCode() == 0);

        // Links rot, rechts blau (P09b, Spiegeln)
        const QString halves = m_work.filePath(QStringLiteral("haelften.png"));
        {
            QImage h(200, 100, QImage::Format_RGB32);
            h.fill(QColor(255, 0, 0));
            for (int y = 0; y < 100; ++y)
                for (int x = 100; x < 200; ++x)
                    h.setPixelColor(x, y, QColor(0, 0, 255));
            QVERIFY(h.save(halves));
        }

        OverlayStore store(m_data.path());
        QString error;
        QVERIFY(store.load(&error));
        store.setMediaSizeReader(readMediaSize);
        const QUuid id = store.createOverlay(QStringLiteral("Test P09"), 1920, 1080, &error);
        m_image = store.addImageSource(id, media("bild1.png"), &error);
        m_rotated = store.addImageSource(id, media("bild3.png"), &error);
        m_jpeg = store.addImageSource(id, jpg, &error);
        m_video = store.addVideoSource(id, media("alpha.webm"), true, &error);
        m_text = store.addTextSource(id, QStringLiteral("Erste Zeile\nZweite Zeile"),
                                     QStringLiteral(DSS_TEST_FONT), &error);
        m_hidden = store.addImageSource(id, media("bild2.png"), &error);
        m_light = store.addTextSource(id, QStringLiteral("Gewicht"), QStringLiteral(DSS_TEST_VARIABLE_FONT), &error);
        m_heavy = store.addTextSource(id, QStringLiteral("Gewicht"), QStringLiteral(DSS_TEST_VARIABLE_FONT), &error);
        QVERIFY2(!m_heavy.isNull(), qPrintable(error));
        m_flipped = store.addImageSource(id, halves, &error);
        m_flippedRotated = store.addImageSource(id, halves, &error);
        QVERIFY2(!m_flippedRotated.isNull(), qPrintable(error));
        // 4 x 4 Bilder rechts in der freien Fläche (1650, 380) bis (1918, 532)
        for (int i = 0; i < 16; ++i) {
            const QString file = QStringLiteral(DSS_TEST_EXIF_DIR "/orientierung-%1.%2")
                                     .arg(i % 8 + 1)
                                     .arg(i < 8 ? QStringLiteral("jpg") : QStringLiteral("png"));
            const QUuid s = store.addImageSource(id, file, &error);
            QVERIFY2(!s.isNull(), qPrintable(error));
            const QPoint at(1650 + (i % 4) * 68, 380 + (i / 4) * 40);
            m_oriented.append({s, at});
            // Grösse der neuen Quelle: so, wie das Bild angezeigt wird (Kern, mit EXIF)
            const Source *added = findSource(*store.overlay(id), s);
            QCOMPARE(QSize(added->width, added->height), QSize(64, 32));
            SourceChanges c;
            c.x = at.x();
            c.y = at.y();
            QVERIFY2(store.updateSource(id, s, c, &error), qPrintable(error));
        }
        // P10a: 4 gedrehte Videos darunter in der freien Fläche (1650, 560) bis (1918, 592)
        for (int i = 0; i < 4; ++i) {
            const QString file = QStringLiteral(DSS_TEST_VIDEO_ROTATION_DIR "/drehung-%1.mp4").arg(i * 90);
            const QUuid s = store.addVideoSource(id, file, true, &error);
            QVERIFY2(!s.isNull(), qPrintable(error));
            const QPoint at(1650 + i * 68, 560);
            m_rotatedVideos.append({s, at});
            const Source *added = findSource(*store.overlay(id), s);
            QCOMPARE(QSize(added->width, added->height), QSize(64, 32));
            SourceChanges c;
            c.x = at.x();
            c.y = at.y();
            QVERIFY2(store.updateSource(id, s, c, &error), qPrintable(error));
        }
        const auto geometry = [&](const QUuid &s, int x, int y, int w, int h, int r) {
            QVERIFY2(store.setSourceGeometry(id, s, x, y, w, h, r, &error), qPrintable(error));
        };
        geometry(m_image, 100, 100, 256, 256, 0);
        geometry(m_rotated, 1300, 300, 512, 128, 900);
        geometry(m_jpeg, 600, 100, 512, 512, 0);
        geometry(m_video, 600, 100, 512, 512, 0);
        geometry(m_text, 100, 650, 1700, 230, 0);
        geometry(m_hidden, 100, 400, 200, 200, 0);
        geometry(m_light, 100, 900, 800, 160, 0);
        geometry(m_heavy, 1000, 900, 800, 160, 0);
        QVERIFY(store.setSourceVisible(id, m_hidden, false, &error));
        for (const auto &[s, y, rotation] : {std::tuple{m_flipped, 0, 0}, std::tuple{m_flippedRotated, 170, 900}}) {
            SourceChanges c;
            c.x = 1650;
            c.y = y;
            c.rotation = rotation;
            c.flipHorizontal = true;
            QVERIFY2(store.updateSource(id, s, c, &error), qPrintable(error));
        }
        SourceChanges text;
        text.fontSize = 100;
        QVERIFY(store.updateSource(id, m_text, text, &error));
        for (const auto &[s, w] : {std::pair{m_light, 200}, std::pair{m_heavy, 800}}) {
            SourceChanges c;
            c.fontSize = 140;
            QVERIFY(store.updateSource(id, s, c, &error));
            QVERIFY2(store.setTextFont(id, s, QStringLiteral(DSS_TEST_VARIABLE_FONT), w, false, &error),
                     qPrintable(error));
        }
        m_overlay = *store.overlay(id);

        m_renderer = std::make_unique<SourceRenderer>();
        m_renderer->setDataDir(m_data.path());
        m_canvas = std::make_unique<CanvasWidget>(m_theme);
        m_canvas->setRenderer(m_renderer.get());
        // Massstab 0.5: Fläche 976x556 ohne Lineale (20) mit Rand 8.
        m_canvas->resize(996, 576);
        m_canvas->setCanvasSize(QSize(1920, 1080));
        m_canvas->setOverlay(m_overlay);
        m_renderer->setOverlay(&m_overlay);
        m_canvas->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_canvas.get()));
        QTRY_VERIFY_WITH_TIMEOUT(m_renderer->imagesLoaded(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(videosShown(), 5000);
        QCOMPARE(m_canvas->toWidget(QPointF(0, 0)), QPointF(28, 28));
    }

    void imageAndHidden()
    {
        const QImage image = grabCanvas();
        // Mitte von bild1.png: deckend 255/80/80 (prototypes/messung/media_erzeugen.sh)
        QVERIFY(distance(at(image, QPointF(228, 228)), QColor(255, 80, 80)) <= 2);
        // Verborgenes bild2.png (grün) bei (100, 400) bis (300, 600): Schachbrett
        QVERIFY(isChecker(at(image, QPointF(200, 500))));
        QCOMPARE(inkIn(image, QRect(110, 410, 180, 180)), 0);
    }

    void rotatedImage()
    {
        // bild3.png gestreckt auf 512x128, 90° gedreht um die Mitte (1556, 364): steht senkrecht.
        const QImage image = grabCanvas();
        QVERIFY(!isChecker(at(image, QPointF(1556, 364))));
        QVERIFY(!isChecker(at(image, QPointF(1556, 364 + 120)))); // entlang der langen Achse
        QVERIFY(isChecker(at(image, QPointF(1556 + 120, 364))));  // quer dazu, ungedreht noch drin
    }

    void videoAlphaOverJpeg()
    {
        const QImage image = grabCanvas();
        // Ecke unten rechts des Videos: dort ist alpha.webm ganz durchsichtig (die Scheibe kommt
        // höchstens 250 px an die Mitte heran), also muss das JPEG darunter zu sehen sein.
        QVERIFY2(distance(at(image, QPointF(600 + 490, 100 + 490)), QColor(255, 80, 80)) <= 6,
                 qPrintable(at(image, QPointF(1090, 590)).name()));
        QVERIFY(distance(at(image, QPointF(600 + 20, 100 + 490)), QColor(255, 80, 80)) <= 6);
        // Streifen oben links: Alpha X/2 ≈ 5 bei X = 10, fast nur JPEG
        QVERIFY(distance(at(image, QPointF(600 + 10, 100 + 30)), QColor(255, 80, 80)) <= 12);
        // Alpha-Mittel des ersten Bildes wie P02 (Gegenprobe ffmpeg: tst_medien)
        const SourceRenderer::VideoInfo v = videoInfo(m_video);
        QCOMPARE(v.decoderName, QStringLiteral("libvpx-vp9"));
        qInfo("Alpha-Mittel erstes Bild: %.4f", v.firstAlphaMean);
        QVERIFY(std::abs(v.firstAlphaMean - 52.8726) < 0.001);
    }

    // P09b, Entscheid 4: Spiegeln im Kasten, erst spiegeln, dann drehen.
    void flipped()
    {
        const QImage image = grabCanvas();
        // (1650, 0) 200x100, waagrecht gespiegelt: links blau, rechts rot
        QVERIFY2(distance(at(image, QPointF(1700, 50)), QColor(0, 0, 255)) <= 2,
                 qPrintable(at(image, QPointF(1700, 50)).name()));
        QVERIFY(distance(at(image, QPointF(1800, 50)), QColor(255, 0, 0)) <= 2);
        // (1650, 170) 200x100, gespiegelt und 90° im Uhrzeigersinn gedreht um die Mitte
        // (1750, 220): lokal links (nach dem Spiegeln blau) zeigt nach oben.
        QVERIFY2(distance(at(image, QPointF(1750, 150)), QColor(0, 0, 255)) <= 2,
                 qPrintable(at(image, QPointF(1750, 150)).name()));
        QVERIFY(distance(at(image, QPointF(1750, 290)), QColor(255, 0, 0)) <= 2);
        // Kasten bleibt: neben dem gedrehten Kasten (x 1700 bis 1800) Schachbrett
        QVERIFY(isChecker(at(image, QPointF(1680, 220))));
    }

    void exifOrientation()
    {
        // P09b, Entscheid 7: alle 16 Bilder sehen nach der Ausrichtung gleich aus (wie der
        // Browser mit image-orientation: from-image). JPEG mit etwas Spielraum (verlustbehaftet).
        const QImage image = grabCanvas();
        for (int i = 0; i < m_oriented.size(); ++i) {
            const QPoint o = m_oriented.at(i).second;
            const int tolerance = i < 8 ? 12 : 2;
            const struct { QPoint offset; QColor color; } quarters[] = {
                {{16, 8}, QColor(255, 0, 0)}, {{48, 8}, QColor(0, 255, 0)},
                {{16, 24}, QColor(0, 0, 255)}, {{48, 24}, QColor(255, 255, 0)}};
            for (const auto &q : quarters) {
                const QColor c = at(image, QPointF(o + q.offset));
                QVERIFY2(distance(c, q.color) <= tolerance,
                         qPrintable(QStringLiteral("Bild %1 (%2) bei %3/%4: %5 statt %6")
                                        .arg(i % 8 + 1)
                                        .arg(i < 8 ? QStringLiteral("JPEG") : QStringLiteral("PNG"))
                                        .arg(q.offset.x())
                                        .arg(q.offset.y())
                                        .arg(c.name(), q.color.name())));
            }
        }
    }

    void videoRotation()
    {
        // P10a, Punkt 65: alle vier Videos sehen nach der Drehung gleich aus (H.264, verlustbehaftet)
        const QImage image = grabCanvas();
        // Ohne DSS_VIDEO_ROTATION nur das ungedrehte Video prüfen
        const int count = DSS_VIDEO_ROTATION ? int(m_rotatedVideos.size()) : 1;
        for (int i = 0; i < count; ++i) {
            const QPoint o = m_rotatedVideos.at(i).second;
            const struct { QPoint offset; QColor color; } quarters[] = {
                {{16, 8}, QColor(255, 0, 0)}, {{48, 8}, QColor(0, 255, 0)},
                {{16, 24}, QColor(0, 0, 255)}, {{48, 24}, QColor(255, 255, 0)}};
            for (const auto &q : quarters) {
                const QColor c = at(image, QPointF(o + q.offset));
                QVERIFY2(distance(c, q.color) <= 12,
                         qPrintable(QStringLiteral("Video %1° bei %2/%3: %4 statt %5")
                                        .arg(i * 90).arg(q.offset.x()).arg(q.offset.y())
                                        .arg(c.name(), q.color.name())));
            }
        }
    }

    void twoLineText()
    {
        const QImage image = grabCanvas();
        // Kasten (100, 650) 1700x230, Grösse 100 px: zwei Zeilen untereinander, oben beginnend
        const int first = inkIn(image, QRect(100, 650, 1700, 110));
        const int second = inkIn(image, QRect(100, 762, 1700, 110));
        qInfo("Text: Zeile 1 %d, Zeile 2 %d Pixel", first, second);
        QVERIFY(first > 500);
        QVERIFY(second > 500);
        // Kein Umbruch: rechts von «Erste Zeile» bleibt leer
        QCOMPARE(inkIn(image, QRect(1000, 655, 790, 90)), 0);
    }

    void variableWeight()
    {
        const QImage image = grabCanvas();
        const int light = inkIn(image, QRect(100, 900, 800, 160));
        const int heavy = inkIn(image, QRect(1000, 900, 800, 160));
        qInfo("Urbanist «Gewicht»: 200 → %d Pixel, 800 → %d Pixel", light, heavy);
        QVERIFY(light > 200);
        QVERIFY2(heavy > light * 1.4, "Achse wght nicht gesetzt?");
    }

    void saveImage()
    {
        const QString file = qEnvironmentVariable("DSS_LEINWAND_BILD");
        if (file.isEmpty())
            QSKIP("DSS_LEINWAND_BILD nicht gesetzt");
        QVERIFY(m_canvas->grab().save(file));
    }

    void cleanupTestCase()
    {
        m_canvas.reset();
        m_renderer.reset();
    }
};

int main(int argc, char *argv[])
{
    if (qgetenv("XDG_SESSION_TYPE") != "x11" || qEnvironmentVariableIsEmpty("DISPLAY")) {
        qInfo("übersprungen: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)");
        return 77;
    }
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));
    TestLeinwand test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_leinwand.moc"
