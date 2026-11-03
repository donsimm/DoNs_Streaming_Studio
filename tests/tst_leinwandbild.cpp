// Bild der Leinwand (P10-vor, Teil B 2) ohne Fenster: dss_render (Qt Gui, kein Qt Widgets) mit
// QGuiApplication in der X11-Sitzung (CLAUDE.md Regel 6), ohne X11 «übersprungen» (Exit-Code 77).
// Ganz ohne Qt Gui geht es nicht: Zeichnen braucht QPainter und QImage, Text die Schriften von
// Qt Gui. Geprüft: Grösse = Auflösung des Overlays, durchsichtig, wo keine Quelle deckt (also
// kein Schachbrett, kein Auswahlrahmen, keine Lineale), Halbdurchsichtiges bleibt halb,
// Vergleich mit dem Zeichner an anderer Stelle, EXIF-Bilder (tests/daten/exif/) und gedrehte
// Videos (tests/daten/video-drehung/) wie auf der Leinwand, PNG Byte für Byte lesbar zurück,
// Name und Ordner. Daten wie in tst_leinwand und im Handtest (Regel 7).
// Mit DSS_LEINWANDBILD_ORDNER=ORDNER wird das Bild für den Bericht dort gespeichert.

#include "canvasimage.h"
#include "mediainfo.h"
#include "overlayimage.h"
#include "overlaystore.h"
#include "sourcerenderer.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace dss;

namespace {

int distance(QColor a, QColor b)
{
    return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue())});
}

// Viertel der Testbilder (64x32 nach der Ausrichtung): rot, grün oben, blau, gelb unten.
struct Quarter
{
    QPoint offset;
    QColor color;
};
const Quarter kQuarters[] = {{{16, 8}, QColor(255, 0, 0)},
                             {{48, 8}, QColor(0, 255, 0)},
                             {{16, 24}, QColor(0, 0, 255)},
                             {{48, 24}, QColor(255, 255, 0)}};

} // namespace

class TestLeinwandBild : public QObject
{
    Q_OBJECT

    QTemporaryDir m_data;
    QTemporaryDir m_work;
    Overlay m_overlay;
    QList<QPoint> m_oriented;      // 8 JPEG, dann 8 PNG
    QList<QPoint> m_rotatedVideos; // 0°, 90°, 180°, 270°
    QRect m_halfBox;               // halbdurchsichtiges Rot (Alpha 128)
    QRect m_textBox;
    std::unique_ptr<SourceRenderer> m_renderer;

    bool videosShown() const
    {
        const QList<SourceRenderer::VideoInfo> infos = m_renderer->videoInfo();
        return infos.size() == 4
               && std::all_of(infos.begin(), infos.end(), [](const auto &v) { return v.hasFrame; });
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid() && m_work.isValid());
        const QString half = m_work.filePath(QStringLiteral("halb.png"));
        {
            QImage h(100, 50, QImage::Format_ARGB32);
            h.fill(QColor(255, 0, 0, 128));
            QVERIFY(h.save(half));
        }
        OverlayStore store(m_data.path());
        QString error;
        QVERIFY(store.load(&error));
        store.setMediaSizeReader(readMediaSize);
        const QUuid id = store.createOverlay(QStringLiteral("Test Bild"), 640, 360, &error);
        QVERIFY2(!id.isNull(), qPrintable(error));
        const auto place = [&](const QUuid &s, QPoint at) {
            QVERIFY2(!s.isNull(), qPrintable(error));
            SourceChanges c;
            c.x = at.x();
            c.y = at.y();
            QVERIFY2(store.updateSource(id, s, c, &error), qPrintable(error));
        };
        for (int i = 0; i < 16; ++i) {
            const QString file = QStringLiteral(DSS_TEST_EXIF_DIR "/orientierung-%1.%2")
                                     .arg(i % 8 + 1)
                                     .arg(i < 8 ? QStringLiteral("jpg") : QStringLiteral("png"));
            const QPoint at(8 + (i % 8) * 72, 8 + (i / 8) * 40);
            place(store.addImageSource(id, file, &error), at);
            m_oriented.append(at);
        }
        for (int i = 0; i < 4; ++i) {
            const QString file = QStringLiteral(DSS_TEST_VIDEO_ROTATION_DIR "/drehung-%1.mp4").arg(i * 90);
            const QPoint at(8 + i * 72, 96);
            place(store.addVideoSource(id, file, true, &error), at);
            m_rotatedVideos.append(at);
        }
        m_halfBox = QRect(8, 140, 100, 50);
        place(store.addImageSource(id, half, &error), m_halfBox.topLeft());
        const QUuid text = store.addTextSource(id, QStringLiteral("Willkommen"), QStringLiteral(DSS_TEST_FONT), &error);
        QVERIFY2(!text.isNull(), qPrintable(error));
        m_textBox = QRect(8, 220, 600, 80);
        QVERIFY(store.setSourceGeometry(id, text, m_textBox.x(), m_textBox.y(), m_textBox.width(),
                                        m_textBox.height(), 0, &error));
        SourceChanges size;
        size.fontSize = 48;
        QVERIFY(store.updateSource(id, text, size, &error));
        m_overlay = *store.overlay(id);
        QCOMPARE(m_overlay.address, QStringLiteral("test-bild"));

        m_renderer = std::make_unique<SourceRenderer>();
        m_renderer->setDataDir(m_data.path());
        m_renderer->setOverlay(&m_overlay);
        QTRY_VERIFY_WITH_TIMEOUT(m_renderer->imagesLoaded(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(videosShown(), 5000);
    }

    void sizeAndTransparency()
    {
        const QImage image = renderOverlayImage(*m_renderer, m_overlay);
        QCOMPARE(image.size(), QSize(640, 360));
        QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied);
        QVERIFY(image.hasAlphaChannel());
        // Ausserhalb aller Quellen ganz durchsichtig: kein Schachbrett, kein Auswahlrahmen,
        // keine Lineale, keine Hilfslinien.
        QRegion covered;
        for (const Source &s : m_overlay.sources)
            covered += QRect(s.x, s.y, s.width, s.height);
        int inkOutside = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (!covered.contains(QPoint(x, y)) && qAlpha(image.pixel(x, y)) != 0)
                    ++inkOutside;
        QCOMPARE(inkOutside, 0);
        QCOMPARE(qAlpha(image.pixel(639, 359)), 0);
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
        // Halbdurchsichtig bleibt halb (Alpha 128 ± 1 durch Vormultiplizieren)
        const QColor h = image.pixelColor(m_halfBox.center());
        QVERIFY2(std::abs(h.alpha() - 128) <= 1, qPrintable(QString::number(h.alpha())));
        QVERIFY(distance(h, QColor(255, 0, 0)) <= 2);
        // Text gezeichnet: deckende Pixel im Kasten
        int ink = 0;
        for (int y = m_textBox.top(); y <= m_textBox.bottom(); ++y)
            for (int x = m_textBox.left(); x <= m_textBox.right(); ++x)
                ink += qAlpha(image.pixel(x, y)) > 200 ? 1 : 0;
        QVERIFY2(ink > 1000, qPrintable(QString::number(ink)));
    }

    void exifAndVideoRotation()
    {
        const QImage image = renderOverlayImage(*m_renderer, m_overlay);
        for (int i = 0; i < m_oriented.size(); ++i) {
            const int tolerance = i < 8 ? 12 : 2; // JPEG verlustbehaftet
            for (const Quarter &q : kQuarters) {
                const QColor c = image.pixelColor(m_oriented.at(i) + q.offset);
                QVERIFY2(distance(c, q.color) <= tolerance && c.alpha() == 255,
                         qPrintable(QStringLiteral("Bild %1 (%2) bei %3/%4: %5 statt %6")
                                        .arg(i % 8 + 1).arg(i < 8 ? QStringLiteral("JPEG") : QStringLiteral("PNG"))
                                        .arg(q.offset.x()).arg(q.offset.y()).arg(c.name(), q.color.name())));
            }
        }
        // Videos: das Bild, das der Zeichner gerade hält (dasselbe wie die Leinwand)
        const int count = DSS_VIDEO_ROTATION ? int(m_rotatedVideos.size()) : 1;
        for (int i = 0; i < count; ++i) {
            for (const Quarter &q : kQuarters) {
                const QColor c = image.pixelColor(m_rotatedVideos.at(i) + q.offset);
                QVERIFY2(distance(c, q.color) <= 12,
                         qPrintable(QStringLiteral("Video %1° bei %2/%3: %4 statt %5")
                                        .arg(i * 90).arg(q.offset.x()).arg(q.offset.y())
                                        .arg(c.name(), q.color.name())));
            }
        }
    }

    // Vergleich mit dem Zeichner: derselbe Zeichner, an anderer Stelle einer grösseren Fläche
    // gezeichnet (wie die Leinwand mit Abstand zum Rand), ergibt Pixel für Pixel dasselbe.
    void sameAsRenderer()
    {
        const QImage image = renderOverlayImage(*m_renderer, m_overlay);
        QImage area(700, 420, QImage::Format_ARGB32_Premultiplied);
        area.fill(Qt::transparent);
        {
            QPainter p(&area);
            p.setClipRect(QRect(30, 20, 640, 360)); // wie die Leinwand: auf das Overlay beschnitten
            m_renderer->paint(&p, m_overlay, CanvasFit{1.0, QPointF(30, 20)});
        }
        QCOMPARE(area.copy(30, 20, 640, 360), image);
    }

    void saveAsPng()
    {
        QTemporaryDir pictures;
        const QString dir = pictures.filePath(QStringLiteral("DoNs Streaming Studio")); // fehlt noch
        const QDateTime time(QDate(2026, 11, 3), QTime(21, 5, 9));
        QString path;
        QString error;
        QVERIFY2(saveOverlayImage(*m_renderer, m_overlay, dir, time, &path, &error), qPrintable(error));
        QCOMPARE(path, dir + QStringLiteral("/test-bild_2026-11-03_21-05-09.png"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.read(8) == QByteArray("\x89PNG\r\n\x1a\n", 8));
        const QImage back(path);
        QCOMPARE(back.size(), QSize(640, 360));
        QVERIFY(back.hasAlphaChannel());
        // Videos laufen weiter; zum Vergleich dasselbe Bild nochmals speichern lassen ist nicht
        // nötig: PNG ist verlustfrei, verglichen wird ohne Vormultiplizieren.
        const QImage now = renderOverlayImage(*m_renderer, m_overlay).convertToFormat(QImage::Format_ARGB32);
        const QImage read = back.convertToFormat(QImage::Format_ARGB32);
        int differing = 0;
        for (int y = 0; y < now.height(); ++y)
            for (int x = 0; x < now.width(); ++x)
                differing += now.pixel(x, y) != read.pixel(x, y) ? 1 : 0;
        // Erlaubt: nur die Videos (ein neueres Bild seit dem Speichern), 4 x 64 x 32 Pixel
        QVERIFY2(differing <= 4 * 64 * 32, qPrintable(QString::number(differing)));
        QCOMPARE(qAlpha(read.pixel(0, 0)), 0);
        // Zweites Bild in derselben Sekunde überschreibt nicht
        QString second;
        QVERIFY(saveOverlayImage(*m_renderer, m_overlay, dir, time, &second, &error));
        QCOMPARE(second, dir + QStringLiteral("/test-bild_2026-11-03_21-05-09-2.png"));
        QCOMPARE(QDir(dir).entryList(QDir::Files | QDir::Hidden).size(), 2); // keine vorläufige Datei
        if (const QString keep = qEnvironmentVariable("DSS_LEINWANDBILD_ORDNER"); !keep.isEmpty()) {
            QFile::remove(keep + QStringLiteral("/leinwandbild.png"));
            QVERIFY(QFile::copy(path, keep + QStringLiteral("/leinwandbild.png")));
        }
    }

    void saveFailsWithMessage()
    {
        QString path;
        QString error;
        QVERIFY(!saveOverlayImage(*m_renderer, m_overlay, QStringLiteral("/proc/gibt-es-nicht"),
                                  QDateTime::currentDateTime(), &path, &error));
        QVERIFY2(error.contains(QStringLiteral("kann nicht angelegt werden")), qPrintable(error));
    }

    void fileNameAndDir()
    {
        QCOMPARE(canvasImageFileName(QStringLiteral("chat-ecke"), QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5))),
                 QStringLiteral("chat-ecke_2026-01-02_03-04-05.png"));
        // Ordner aus user-dirs.dirs wie xdg-user-dir (beide lesen $XDG_CONFIG_HOME/user-dirs.dirs)
        QTemporaryDir config;
        {
            QFile f(config.filePath(QStringLiteral("user-dirs.dirs")));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("XDG_PICTURES_DIR=\"$HOME/Testbilder\"\n");
        }
        const QByteArray old = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", config.path().toLocal8Bit());
        const QString dir = canvasImageDir();
        QProcess xdg;
        xdg.start(QStringLiteral("xdg-user-dir"), {QStringLiteral("PICTURES")});
        const bool ran = xdg.waitForFinished(5000) && xdg.exitCode() == 0;
        const QString xdgDir = QString::fromLocal8Bit(xdg.readAllStandardOutput()).trimmed();
        qputenv("XDG_CONFIG_HOME", old);
        if (old.isEmpty())
            qunsetenv("XDG_CONFIG_HOME");
        QCOMPARE(dir, QDir::homePath() + QStringLiteral("/Testbilder/DoNs Streaming Studio"));
        if (ran)
            QCOMPARE(dir, xdgDir + QStringLiteral("/DoNs Streaming Studio"));
        else
            qWarning("xdg-user-dir nicht gefunden, Vergleich übersprungen");
    }
};

int main(int argc, char *argv[])
{
    if (qgetenv("XDG_SESSION_TYPE") != "x11" || qEnvironmentVariableIsEmpty("DISPLAY")) {
        qInfo("übersprungen: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)");
        return 77;
    }
    QGuiApplication app(argc, argv);
    TestLeinwandBild test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_leinwandbild.moc"
