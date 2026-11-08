// P10, Stufe 1 der Prüfung der Browserquelle (Entscheid des Nutzers): Qt WebEngine (Chromium)
// lädt die Seite eines eigenen Kerns über die echte Netzwerkkarte (http://<Adresse im
// Heimnetz>:<Port>/o/…) und greift das Bild mit Durchsichtigkeit ab wie in P02, M2
// (QQuickWidget::grabFramebuffer auf das innere Widget des QWebEngineView,
// Format_RGBA8888_Premultiplied). Vergleich mit dem Zeichner des Editors (dss_render) mit den
// Testdaten tests/daten/exif/ und tests/daten/video-drehung/, Text, Spiegeln, Drehung.
// Weiter: Änderung ohne Neuladen (Zeit), Ausgabe aus/ein, Video beginnt beim Einblenden von vorne,
// neuer Token (alte Adresse leer, Hinweis), Wiederverbinden nach Neustart des Kerns.
// Mit DSS_BROWSERQUELLE_LAST=1 zusätzlich die Last: 10 Quellen mit 2 Videos 1080p30, CPU und
// Speicher von Kern und Testprogramm (mit den Prozessen von Chromium).
// Abweichungen werden mit Zahlen ausgegeben (feste Grenze erst mit P11); geprüft wird hier, dass
// Ausrichtung und Inhalt stimmen (Farben der Viertel der Testbilder).
// Ohne X11-Sitzung oder ohne Adresse im Heimnetz «übersprungen» (Exit-Code 77).
// Mit DSS_BROWSERQUELLE_ORDNER=ORDNER werden die Bilder für den Bericht dort gespeichert.
//
// Quellen: QWebEngineView https://doc.qt.io/qt-6.8/qwebengineview.html,
// QWebEnginePage::setBackgroundColor https://doc.qt.io/qt-6.8/qwebenginepage.html#backgroundColor-prop,
// QQuickWidget::grabFramebuffer https://doc.qt.io/qt-6.8/qquickwidget.html#grabFramebuffer,
// /proc/<pid>/stat und statm: https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html

#include "bildvergleich.h"
#include "coreclient.h"
#include "coresettings.h"
#include "overlayimage.h"
#include "overlaystore.h"
#include "mediainfo.h"
#include "prozesshilfe.h"
#include "sourcerenderer.h"
#include "version.h"
#include "windowedit.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QQuickWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <unistd.h>

using namespace dss;
using namespace testhelp;

namespace {

const QString kExif = QStringLiteral(DSS_TEST_EXIF_DIR);
const QString kVideos = QStringLiteral(DSS_TEST_VIDEO_ROTATION_DIR);
constexpr int kWidth = 800;
constexpr int kHeight = 450;

// Testbilder: angezeigt links oben rot, rechts oben grün, links unten blau, rechts unten gelb.
struct Quarter
{
    QPointF at; // Anteil im Kasten
    QColor color;
};
const Quarter kQuarters[] = {{{0.25, 0.25}, QColor(255, 0, 0)},
                             {{0.75, 0.25}, QColor(0, 255, 0)},
                             {{0.25, 0.75}, QColor(0, 0, 255)},
                             {{0.75, 0.75}, QColor(255, 255, 0)}};

int distance(const QColor &a, const QColor &b)
{
    return std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                     std::abs(a.blue() - b.blue())});
}

// Unterschied zweier Bilder (vormultipliziert) in einem Bereich: tests/bildvergleich.h (P11)
using bildvergleich::compare;
using bildvergleich::Difference;

// CPU-Zeit (utime + stime, Ticks) und RSS (KiB) eines Prozesses
struct ProcessSample
{
    qint64 ticks = 0;
    qint64 rssKiB = 0;
};
ProcessSample sampleProcess(qint64 pid)
{
    ProcessSample s;
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (stat.open(QIODevice::ReadOnly)) {
        const QByteArray line = stat.readAll();
        const QList<QByteArray> f = line.mid(line.lastIndexOf(')') + 2).split(' ');
        // nach dem Namen: Feld 3 = Index 0; utime = Feld 14, stime = Feld 15
        s.ticks = f.value(11).toLongLong() + f.value(12).toLongLong();
    }
    QFile statm(QStringLiteral("/proc/%1/statm").arg(pid));
    if (statm.open(QIODevice::ReadOnly))
        s.rssKiB = statm.readAll().split(' ').value(1).toLongLong() * (sysconf(_SC_PAGESIZE) / 1024);
    return s;
}
// Alle Nachkommen eines Prozesses (Chromium startet QtWebEngineProcess als Kindprozesse).
QList<qint64> descendants(qint64 pid)
{
    QHash<qint64, qint64> parent;
    for (const QString &e : QDir(QStringLiteral("/proc")).entryList(QDir::Dirs)) {
        bool ok = false;
        const qint64 p = e.toLongLong(&ok);
        if (!ok)
            continue;
        QFile stat(QStringLiteral("/proc/%1/stat").arg(p));
        if (!stat.open(QIODevice::ReadOnly))
            continue;
        const QByteArray line = stat.readAll();
        parent.insert(p, line.mid(line.lastIndexOf(')') + 2).split(' ').value(1).toLongLong());
    }
    QList<qint64> result;
    QList<qint64> todo = {pid};
    while (!todo.isEmpty()) {
        const qint64 cur = todo.takeFirst();
        for (auto it = parent.cbegin(); it != parent.cend(); ++it) {
            if (it.value() == cur) {
                result.append(it.key());
                todo.append(it.key());
            }
        }
    }
    return result;
}

} // namespace

class TestBrowserquelle : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    QTemporaryDir m_config;
    std::unique_ptr<QTemporaryDir> m_run;
    QString m_socket;
    QString m_home;
    int m_port = 0;
    std::unique_ptr<QProcess> m_core;
    QString m_coreLog;
    QString m_allLog; // Protokolle früherer Läufe des Kerns
    std::unique_ptr<CoreClient> m_client;
    QUuid m_id;
    QList<QRect> m_oriented; // 8 JPEG, 8 PNG, 4 Videos
    QRect m_textBox;
    QRect m_mirrored;
    QRect m_rotated;
    QRect m_videoTurned;
    QUuid m_videoSource;
    QUuid m_moveSource;
    std::unique_ptr<QWebEngineView> m_view;
    QString m_saveDir;
    QString m_oldToken;

    bool startCore()
    {
        if (m_core)
            m_allLog += m_coreLog + QString::fromUtf8(m_core->readAllStandardError());
        m_core = testhelp::startCore(m_data.path(), m_socket, m_config.path());
        m_coreLog.clear();
        return waitForLog(m_core.get(), QStringLiteral("Overlay-Server lauscht auf %1:%2").arg(m_home).arg(m_port),
                          &m_coreLog, 8000);
    }

    QVariant js(const QString &code)
    {
        QVariant result;
        bool done = false;
        m_view->page()->runJavaScript(code, [&](const QVariant &v) {
            result = v;
            done = true;
        });
        static_cast<void>(QTest::qWaitFor([&] { return done; }, 5000));
        return result;
    }

    QImage grab()
    {
        auto *quick = m_view->findChild<QQuickWidget *>();
        if (!quick)
            return {};
        return quick->grabFramebuffer().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    void save(const QImage &image, const QString &name)
    {
        if (!m_saveDir.isEmpty())
            image.save(m_saveDir + QLatin1Char('/') + name);
    }

    bool waitForPage(int applied, int timeoutMs = 15000)
    {
        return QTest::qWaitFor([&] { return js(QStringLiteral("window.dssState ? window.dssState.applied : -1")).toInt() >= applied; },
                               timeoutMs);
    }

    // Seite fertig: Schriften geladen, Videos mit Bild, Bilder geladen.
    bool pageReady(int videos)
    {
        const QString code = QStringLiteral(
            "(() => { const f=[...document.fonts].every(x=>x.status==='loaded');"
            " const v=[...document.querySelectorAll('video')]; const i=[...document.querySelectorAll('img')];"
            " return f && document.fonts.size>0 && v.length===%1 && v.every(x=>x.readyState>=2)"
            " && i.every(x=>x.complete && x.naturalWidth>0); })()").arg(videos);
        return QTest::qWaitFor([&] { return js(code).toBool(); }, 15000);
    }

    QString url()
    {
        const auto b = m_client->browserSource();
        return b ? b->url : QString();
    }

    ChangeResult change(ChangeRequest r)
    {
        QSignalSpy spy(m_client.get(), &CoreClient::changeFinished);
        const qint64 id = m_client->sendChange(r);
        for (int i = 0; i < 50; ++i) {
            for (const QList<QVariant> &args : std::as_const(spy)) {
                const ChangeResult result = args.at(0).value<ChangeResult>();
                if (result.requestId == id)
                    return result;
            }
            spy.wait(100);
        }
        return {};
    }

    bool allTransparent(const QImage &image)
    {
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(image.pixel(x, y)) != 0)
                    return false;
        return true;
    }

    void checkQuarters(const QImage &image, const QString &what)
    {
        for (int i = 0; i < m_oriented.size(); ++i) {
            const QRect box = m_oriented.at(i);
            // JPEG verlustbehaftet. Videos: Chromium rechnet H.264 ohne Farbangaben wie BT.601 mit
            // Umrechnung der Grundfarben auf sRGB (Rot 248/36/0, Grün 64/252/0), der Editor
            // (swscale) ohne; geprüft wird dort nur die Ausrichtung (nächstliegende Farbe).
            const bool video = i >= 16;
            const int tolerance = video ? 80 : i < 8 ? 40 : 4;
            for (const Quarter &q : kQuarters) {
                const QPoint p(box.x() + int(box.width() * q.at.x()), box.y() + int(box.height() * q.at.y()));
                const QColor c = image.pixelColor(p);
                const Quarter *nearest = std::min_element(std::begin(kQuarters), std::end(kQuarters),
                                                          [&](const Quarter &x, const Quarter &y) {
                                                              return distance(c, x.color) < distance(c, y.color);
                                                          });
                QVERIFY2(nearest->color == q.color, qPrintable(QStringLiteral("%1: Ausrichtung Quelle %2 bei %3,%4")
                                                                   .arg(what).arg(i).arg(p.x()).arg(p.y())));
                const QString name = i < 8 ? QStringLiteral("JPEG %1").arg(i + 1)
                                     : i < 16 ? QStringLiteral("PNG %1").arg(i - 7)
                                              : QStringLiteral("Video %1°").arg((i - 16) * 90);
                QVERIFY2(distance(c, q.color) <= tolerance && c.alpha() >= 250,
                         qPrintable(QStringLiteral("%1: %2 bei %3,%4: %5 statt %6")
                                        .arg(what, name).arg(p.x()).arg(p.y()).arg(c.name(QColor::HexArgb), q.color.name())));
            }
        }
    }

private slots:
    void initTestCase()
    {
        m_saveDir = qEnvironmentVariable("DSS_BROWSERQUELLE_ORDNER");
        if (!m_saveDir.isEmpty())
            QVERIFY(QDir().mkpath(m_saveDir));
        m_home = detectHomeAddress();
        QVERIFY(!m_home.isEmpty());
        m_port = 24000 + int(QCoreApplication::applicationPid() % 2000) * 4 + 2;
        QVERIFY(writeCoreSettings(m_config.path(), m_port, m_home));
        m_run = runtimeDir();
        m_socket = m_run->filePath(QStringLiteral("k.sock"));

        OverlayStore store(m_data.path());
        store.setMediaSizeReader(readMediaSize);
        QString error;
        QVERIFY(store.load(&error));
        m_id = store.createOverlay(QStringLiteral("Prüfung Browserquelle"), kWidth, kHeight, &error);
        QVERIFY2(!m_id.isNull(), qPrintable(error));
        const auto place = [&](const QUuid &s, const QRect &r, int rotation = 0, bool flipH = false,
                               bool flipV = false) {
            QVERIFY2(!s.isNull(), qPrintable(error));
            SourceChanges c;
            c.x = r.x();
            c.y = r.y();
            c.width = r.width();
            c.height = r.height();
            c.rotation = rotation;
            c.flipHorizontal = flipH;
            c.flipVertical = flipV;
            QVERIFY2(store.updateSource(m_id, s, c, &error), qPrintable(error));
        };
        for (int row = 0; row < 2; ++row) {
            for (int n = 1; n <= 8; ++n) {
                const QRect r(10 + (n - 1) * 96, 10 + row * 64, 88, 44);
                place(store.addImageSource(m_id, kExif + QStringLiteral("/orientierung-%1.%2").arg(n).arg(row ? "png" : "jpg"), &error), r);
                m_oriented.append(r);
            }
        }
        for (int i = 0; i < 4; ++i) {
            const QRect r(10 + i * 96, 140, 88, 44);
            const QUuid v = store.addVideoSource(m_id, kVideos + QStringLiteral("/drehung-%1.mp4").arg(i * 90), true, &error);
            place(v, r);
            m_oriented.append(r);
            if (i == 1)
                m_videoSource = v;
        }
        m_textBox = QRect(10, 200, 380, 100);
        const QUuid text = store.addTextSource(m_id, QStringLiteral("Hallo Welt\nZweite Zeile ÄÖÜ"), QStringLiteral(DSS_TEST_FONT), &error);
        place(text, m_textBox);
        {
            SourceChanges c;
            c.fontSize = 36;
            c.align = TextAlign::Center;
            c.color = QStringLiteral("#FFE040FF");
            QVERIFY(store.updateSource(m_id, text, c, &error));
        }
        const QUuid text2 = store.addTextSource(m_id, QStringLiteral("Be Vietnam Pro\nrechts"),
                                                QStringLiteral(DSS_TEST_BUILTIN_FONT_DIR "/BeVietnamPro-Bold.ttf"), &error);
        place(text2, QRect(410, 200, 370, 100), 0);
        {
            SourceChanges c;
            c.fontSize = 30;
            c.align = TextAlign::Right;
            QVERIFY(store.updateSource(m_id, text2, c, &error));
        }
        m_mirrored = QRect(10, 330, 160, 80);
        place(store.addImageSource(m_id, kExif + QStringLiteral("/orientierung-1.png"), &error), m_mirrored, 0, true, false);
        m_rotated = QRect(220, 330, 160, 80);
        m_moveSource = store.addImageSource(m_id, kExif + QStringLiteral("/orientierung-1.png"), &error);
        place(m_moveSource, m_rotated, 125);
        m_videoTurned = QRect(440, 320, 160, 80);
        place(store.addVideoSource(m_id, kVideos + QStringLiteral("/drehung-90.mp4"), true, &error), m_videoTurned, 300, false, true);

        QVERIFY2(startCore(), qPrintable(m_coreLog));
        CoreClient::Options o;
        o.socketPath = m_socket;
        o.program = QStringLiteral("tst_browserquelle");
        o.version = version();
        o.reconnectIntervalMs = 200;
        m_client = std::make_unique<CoreClient>(o);
        m_client->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isConnected(), 5000);
        m_client->requestBrowserSource(m_id);
        m_client->requestOverlay(m_id);
        QTRY_VERIFY_WITH_TIMEOUT(!url().isEmpty() && m_client->overlay(m_id), 5000);
        QVERIFY(url().startsWith(QStringLiteral("http://%1:%2/o/").arg(m_home).arg(m_port)));

        m_view = std::make_unique<QWebEngineView>();
        m_view->page()->setBackgroundColor(Qt::transparent);
        m_view->page()->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
        m_view->resize(kWidth, kHeight);
        m_view->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
        m_view->load(QUrl(url()));
        QVERIFY(waitForPage(1));
        QVERIFY(pageReady(5));
        qInfo("Chromium: %s", qPrintable(js(QStringLiteral("navigator.userAgent")).toString()));
    }

    void cleanupTestCase()
    {
        m_view.reset();
        m_client.reset();
        if (m_core && m_core->state() != QProcess::NotRunning) {
            m_core->terminate();
            m_core->waitForFinished(5000);
        }
        // Protokoll des Kerns ohne Token (Regel 12)
        OverlayStore store(m_data.path());
        QString error;
        store.load(&error, OverlayStore::LoadMode::ReadOnly);
        m_allLog += m_coreLog + QString::fromUtf8(m_core ? m_core->readAllStandardError() : QByteArray());
        QVERIFY(!store.token(m_id).isEmpty());
        QVERIFY(!m_allLog.contains(store.token(m_id)));
        if (!m_oldToken.isEmpty()) // nur, wenn newToken lief
            QVERIFY(!m_allLog.contains(m_oldToken));
    }

    // Bild der Seite gegen das Bild des Editors (dss_render)
    void sameAsEditor()
    {
        QTest::qWait(500); // Videos laufen (10 Bilder je Durchgang, alle gleich)
        const QImage page = grab();
        QCOMPARE(page.size(), QSize(kWidth, kHeight));
        save(page, QStringLiteral("seite.png"));

        SourceRenderer renderer;
        renderer.setDataDir(m_client->coreHello().dataDir);
        const Overlay overlay = *m_client->overlay(m_id);
        renderer.setOverlay(&overlay);
        QTRY_VERIFY_WITH_TIMEOUT(renderer.imagesLoaded(), 5000);
        const auto videosShown = [&] {
            const QList<SourceRenderer::VideoInfo> infos = renderer.videoInfo();
            return infos.size() == 5 && std::all_of(infos.begin(), infos.end(), [](const auto &v) { return v.hasFrame; });
        };
        QTRY_VERIFY_WITH_TIMEOUT(videosShown(), 5000);
        const QImage editor = renderOverlayImage(renderer, overlay);
        save(editor, QStringLiteral("editor.png"));

        checkQuarters(editor, QStringLiteral("Editor"));
        checkQuarters(page, QStringLiteral("Seite"));
        // Abweichungen mit Zahlen (Grenze kommt mit P11)
        const auto line = [&](const QString &name, const QRect &r) {
            const Difference d = compare(page, editor, r);
            qInfo("Abweichung %-26s mittel %6.2f, grösste %3d, Pixel > 32: %5.2f %%", qPrintable(name), d.mean, d.max, d.over32);
            return d;
        };
        line(QStringLiteral("ganzes Bild"), QRect(0, 0, kWidth, kHeight));
        line(QStringLiteral("JPEG mit EXIF (8)"), QRect(10, 10, 8 * 96, 44));
        line(QStringLiteral("PNG mit eXIf (8)"), QRect(10, 74, 8 * 96, 44));
        line(QStringLiteral("Videos 0/90/180/270°"), QRect(10, 140, 4 * 96, 44));
        line(QStringLiteral("Text DejaVu, Mitte"), m_textBox);
        line(QStringLiteral("Text Be Vietnam Pro, rechts"), QRect(410, 200, 370, 100));
        line(QStringLiteral("gespiegelt"), m_mirrored);
        line(QStringLiteral("gedreht 12.5°"), m_rotated.adjusted(-20, -40, 20, 40));
        line(QStringLiteral("Video gedreht+gespiegelt"), m_videoTurned.adjusted(-30, -60, 30, 60));
        // Gespiegelt: links oben grün statt rot (an der senkrechten Mittellinie)
        QVERIFY(distance(page.pixelColor(m_mirrored.x() + 40, m_mirrored.y() + 20), QColor(0, 255, 0)) <= 4);
        QVERIFY(distance(editor.pixelColor(m_mirrored.x() + 40, m_mirrored.y() + 20), QColor(0, 255, 0)) <= 4);
        // Ausserhalb der Quellen durchsichtig
        QCOMPARE(qAlpha(page.pixel(kWidth - 1, kHeight - 1)), 0);
        QCOMPARE(qAlpha(page.pixel(kWidth - 1, 0)), 0);
        // Text gezeichnet: Farbe #FFE040 im Kasten, auf Seite und im Editor ähnlich viel
        const auto ink = [&](const QImage &img) {
            int n = 0;
            for (int y = m_textBox.top(); y <= m_textBox.bottom(); ++y)
                for (int x = m_textBox.left(); x <= m_textBox.right(); ++x)
                    n += qAlpha(img.pixel(x, y)) > 128 ? 1 : 0;
            return n;
        };
        qInfo("Text DejaVu: deckende Pixel Seite %d, Editor %d", ink(page), ink(editor));
        QVERIFY(ink(page) > 1000);
        QVERIFY(std::abs(ink(page) - ink(editor)) < ink(editor) / 5);
    }

    // Änderung im Editor (über den Kern) erscheint ohne Neuladen; Zeit bis zur Seite und bis zum Bild
    void changeWithoutReload()
    {
        js(QStringLiteral("window.dssMarker = 4711"));
        QList<qint64> toDom;
        QList<qint64> toImage;
        for (int i = 0; i < 10; ++i) {
            const int applied = js(QStringLiteral("window.dssState.applied")).toInt();
            // abwechselnd rechts (620, frei) und zurück (220); geprüft wird die Mitte rechts
            const bool right = i % 2 == 0;
            const int x = right ? 620 : m_rotated.x();
            QElapsedTimer t;
            t.start();
            m_client->sendChange(sourceUpdate(m_id, m_moveSource, [&] {
                SourceChanges c;
                c.x = x;
                c.rotation = 0;
                return c;
            }()));
            QVERIFY(QTest::qWaitFor([&] { return js(QStringLiteral("window.dssState.applied")).toInt() > applied; }, 5000));
            toDom.append(t.elapsed());
            // Bild: Mitte des Kastens rechts deckend bzw. wieder durchsichtig
            const QPoint center(620 + m_rotated.width() / 2, m_rotated.y() + m_rotated.height() / 2);
            QVERIFY(QTest::qWaitFor([&] { return qAlpha(grab().pixel(center)) == (right ? 255 : 0); }, 5000));
            toImage.append(t.elapsed());
        }
        QCOMPARE(js(QStringLiteral("window.dssMarker")).toInt(), 4711); // nicht neu geladen
        std::sort(toDom.begin(), toDom.end());
        std::sort(toImage.begin(), toImage.end());
        qInfo("Änderung bis zur Seite (10 Mal): Median %lld ms, grösste %lld ms; bis zum abgegriffenen Bild: Median %lld ms, grösste %lld ms",
              static_cast<long long>(toDom.at(5)), static_cast<long long>(toDom.last()),
              static_cast<long long>(toImage.at(5)), static_cast<long long>(toImage.last()));
    }

    // Ausgabe aus: leer, bleibt verbunden; ein: Inhalt zurück, Videos von vorne
    void outputOffAndOn()
    {
        ChangeRequest r;
        r.type = QString::fromLatin1(msg::kSetOverlayOutput);
        r.overlayId = m_id;
        r.output = false;
        QVERIFY(change(r).ok);
        QVERIFY(QTest::qWaitFor([&] { return allTransparent(grab()); }, 5000));
        save(grab(), QStringLiteral("ausgabe-aus.png"));
        QCOMPARE(js(QStringLiteral("window.dssState.connected")).toBool(), true);
        QCOMPARE(js(QStringLiteral("window.dssState.output")).toBool(), false);
        QCOMPARE(js(QStringLiteral("document.querySelectorAll('video').length")).toInt(), 0);
        QTest::qWait(300);
        r.output = true;
        QVERIFY(change(r).ok);
        QVERIFY(QTest::qWaitFor([&] { return js(QStringLiteral("document.querySelectorAll('video').length")).toInt() == 5; }, 5000));
        const double t = js(QStringLiteral("Math.max(...[...document.querySelectorAll('video')].map(v=>v.currentTime))")).toDouble();
        qInfo("Ausgabe ein: Zeit der Videos danach höchstens %.3f s", t);
        QVERIFY(t < 0.5);
        QVERIFY(pageReady(5));
        QTest::qWait(300);
        checkQuarters(grab(), QStringLiteral("nach Ausgabe ein"));
    }

    // Video beginnt beim Einblenden der Quelle von vorne (Teil B 2)
    void videoRestartsWhenShown()
    {
        QTest::qWait(400); // Video läuft (Länge 10 Bilder = 0.33 s, wiederholt)
        const auto hide = [&](bool visible) {
            return change(sourceUpdate(m_id, m_videoSource, [&] {
                SourceChanges c;
                c.visible = visible;
                return c;
            }()));
        };
        QVERIFY(hide(false).ok);
        QVERIFY(QTest::qWaitFor([&] { return js(QStringLiteral("document.querySelectorAll('video').length")).toInt() == 4; }, 5000));
        QVERIFY(hide(true).ok);
        QVERIFY(QTest::qWaitFor([&] { return js(QStringLiteral("document.querySelectorAll('video').length")).toInt() == 5; }, 5000));
        // neues Element (Neubeginn), Zeit nahe 0
        const double t = js(QStringLiteral("[...document.querySelectorAll('video')].map(v=>v.currentTime).sort((a,b)=>a-b)[0]")).toDouble();
        qInfo("Video nach dem Einblenden bei %.3f s", t);
        QVERIFY(t < 0.2);
    }

    // Teil B 3: Der Editor wechselt auf ein anderes Overlay und ändert es; die Seite spielt
    // unabhängig davon weiter (gleiche Videoelemente, kein neues Overlay angewendet).
    void otherOverlayLeavesPageAlone()
    {
        js(QStringLiteral("document.querySelectorAll('video').forEach(v => v.dssMark = 1)"));
        const int applied = js(QStringLiteral("window.dssState.applied")).toInt();
        ChangeRequest c;
        c.type = QString::fromLatin1(msg::kCreateOverlay);
        c.name = QStringLiteral("Anderes");
        c.width = 640;
        c.height = 360;
        const ChangeResult created = change(c);
        QVERIFY(created.ok);
        m_client->requestOverlay(created.id);
        ChangeRequest t;
        t.type = QString::fromLatin1(msg::kAddTextSource);
        t.overlayId = created.id;
        QVERIFY(change(t).ok);
        m_client->requestOverlay(m_id);
        QTest::qWait(500);
        QCOMPARE(js(QStringLiteral("window.dssState.applied")).toInt(), applied);
        QCOMPARE(js(QStringLiteral("[...document.querySelectorAll('video')].filter(v => v.dssMark === 1).length")).toInt(), 5);
    }

    // Neuer Token: alte Adresse leer und durchsichtig, Hinweis im Fenster; neue Adresse geht
    void newToken()
    {
        const QString oldUrl = url();
        m_oldToken = oldUrl.section(QLatin1Char('/'), 5, 5);
        ChangeRequest r;
        r.type = QString::fromLatin1(msg::kNewToken);
        r.overlayId = m_id;
        QVERIFY(change(r).ok);
        QTRY_VERIFY_WITH_TIMEOUT(url() != oldUrl && !url().isEmpty(), 3000);
        // Die Seite bekommt «gone», lädt neu und ist leer.
        QVERIFY(QTest::qWaitFor([&] { return js(QStringLiteral("document.getElementById('overlay') === null")).toBool(); }, 8000));
        QCOMPARE(m_view->url().toString(), oldUrl);
        QVERIFY(QTest::qWaitFor([&] { return allTransparent(grab()); }, 5000));
        save(grab(), QStringLiteral("alte-adresse.png"));
        QTRY_VERIFY_WITH_TIMEOUT(m_client->serverStatus() && !m_client->serverStatus()->invalidUse.isEmpty(), 12000);
        qInfo("Hinweis im Fenster: %s", qPrintable(m_client->serverStatus()->invalidUse));
        QVERIFY(m_client->serverStatus()->invalidUse.contains(m_home));
        QVERIFY(!m_client->serverStatus()->invalidUse.contains(oldUrl.section(QLatin1Char('/'), 5, 5)));
        // Neue Adresse
        m_view->load(QUrl(url()));
        QVERIFY(waitForPage(1));
        QVERIFY(pageReady(5));
        QTest::qWait(300);
        checkQuarters(grab(), QStringLiteral("neue Adresse"));
    }

    // Neustart des Kerns: letztes Bild bleibt stehen, die Seite verbindet sich von selbst wieder
    void reconnectAfterCoreRestart()
    {
        js(QStringLiteral("window.dssMarker = 99"));
        const QImage before = grab();
        m_core->terminate();
        QVERIFY(m_core->waitForFinished(5000));
        QTRY_VERIFY_WITH_TIMEOUT(!js(QStringLiteral("window.dssState.connected")).toBool(), 5000);
        QTest::qWait(1500);
        const QImage during = grab();
        save(during, QStringLiteral("kern-weg.png"));
        // Bilder (ohne Videos) unverändert
        const Difference d = compare(before, during, QRect(10, 10, 8 * 96, 108));
        qInfo("Kern beendet: Abweichung der Bilder zum Bild davor: grösste %d", d.max);
        QCOMPARE(d.max, 0);
        QElapsedTimer t;
        t.start();
        const int applied = js(QStringLiteral("window.dssState.applied")).toInt();
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QVERIFY(QTest::qWaitFor([&] {
            return js(QStringLiteral("window.dssState.connected && window.dssState.applied > %1").arg(applied)).toBool();
        }, 15000));
        qInfo("Wiederverbunden %lld ms nach dem Start des Kerns", static_cast<long long>(t.elapsed()));
        QCOMPARE(js(QStringLiteral("window.dssMarker")).toInt(), 99); // ohne Neuladen
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isConnected(), 5000);
        // Änderung kommt wieder an
        const int now = js(QStringLiteral("window.dssState.applied")).toInt();
        m_client->sendChange(sourceUpdate(m_id, m_moveSource, [] {
            SourceChanges c;
            c.y = 300;
            return c;
        }()));
        QVERIFY(QTest::qWaitFor([&] { return js(QStringLiteral("window.dssState.applied")).toInt() > now; }, 5000));
    }

    // Teil B 4: WebM VP9 mit Alpha (prototypes/media/alpha.webm). Alpha-Mittel des ersten Bildes
    // in der Seite (angehalten bei 0 s) gegen den Zeichner des Editors (VideoInfo::firstAlphaMean).
    void webmAlpha()
    {
        ChangeRequest a;
        a.type = QString::fromLatin1(msg::kAddVideoSource);
        a.overlayId = m_id;
        a.file = QStringLiteral(DSS_TEST_MEDIA_DIR "/alpha.webm");
        const ChangeResult added = change(a);
        QVERIFY2(added.ok, qPrintable(added.message));
        const QRect box(630, 330, 110, 110);
        SourceChanges c;
        c.x = box.x();
        c.y = box.y();
        c.width = box.width();
        c.height = box.height();
        QVERIFY(change(sourceUpdate(m_id, added.id, c)).ok);
        QVERIFY(QTest::qWaitFor([&] { return js(QStringLiteral("document.querySelectorAll('video').length")).toInt() == 6; }, 5000));
        QVERIFY(pageReady(6));
        js(QStringLiteral("(() => { const v = [...document.querySelectorAll('video')].pop(); window.dssSeeked = false;"
                          " v.addEventListener('seeked', () => { window.dssSeeked = true; }, {once: true});"
                          " v.pause(); v.currentTime = 0; })()"));
        QVERIFY(QTest::qWaitFor([&] { return js(QStringLiteral("window.dssSeeked")).toBool(); }, 5000));
        QTest::qWait(200);
        const QImage page = grab();
        save(page, QStringLiteral("alpha-webm.png"));
        double sum = 0;
        int partial = 0;
        for (int y = box.top(); y <= box.bottom(); ++y) {
            for (int x = box.left(); x <= box.right(); ++x) {
                const int alpha = qAlpha(page.pixel(x, y));
                sum += alpha;
                partial += alpha > 0 && alpha < 255 ? 1 : 0;
            }
        }
        const double pageMean = sum / double(box.width() * box.height());

        SourceRenderer renderer;
        renderer.setDataDir(m_client->coreHello().dataDir);
        QTRY_VERIFY_WITH_TIMEOUT(m_client->overlay(m_id) && m_client->overlay(m_id)->sources.size() == 26, 5000);
        const Overlay overlay = *m_client->overlay(m_id);
        renderer.setOverlay(&overlay);
        double editorMean = -1;
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const SourceRenderer::VideoInfo &v : renderer.videoInfo())
                if (v.sourceId == added.id && v.firstAlphaMean >= 0)
                    editorMean = v.firstAlphaMean;
            return editorMean >= 0;
        }(), 5000);
        qInfo("WebM mit Alpha, erstes Bild: Alpha-Mittel Seite %.2f (Kasten %dx%d, teilweise durchsichtige Pixel %d),"
              " Editor %.4f (512x512)", pageMean, box.width(), box.height(), partial, editorMean);
        QVERIFY(partial > 0);
        QVERIFY(std::abs(pageMean - editorMean) < 3.0);
    }

    // Teil B 4: Video, das Chromium nicht abspielt (MPEG-4 Part 2, mit ffmpeg erzeugt): leer und
    // Meldung im Protokoll des Kerns (ohne Token).
    void unplayableVideo()
    {
        QTemporaryDir work;
        const QString file = work.filePath(QStringLiteral("mpeg4-part2.mp4"));
        QProcess ff;
        ff.start(QStringLiteral("ffmpeg"), {QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                                            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                            QStringLiteral("testsrc2=s=160x90:r=30:d=1"), QStringLiteral("-c:v"),
                                            QStringLiteral("mpeg4"), file});
        QVERIFY(ff.waitForFinished(60000) && ff.exitCode() == 0);
        ChangeRequest a;
        a.type = QString::fromLatin1(msg::kAddVideoSource);
        a.overlayId = m_id;
        a.file = file;
        const ChangeResult added = change(a);
        QVERIFY2(added.ok, qPrintable(added.message));
        const QRect box(750, 330, 40, 40);
        SourceChanges c;
        c.x = box.x();
        c.y = box.y();
        c.width = box.width();
        c.height = box.height();
        QVERIFY(change(sourceUpdate(m_id, added.id, c)).ok);
        QVERIFY(QTest::qWaitFor([&] {
            return js(QStringLiteral("window.dssState.reports.some(r => r.startsWith('Video nicht abspielbar'))")).toBool();
        }, 8000));
        qInfo("Meldung der Seite: %s", qPrintable(js(QStringLiteral("window.dssState.reports.join(' | ')")).toString()));
        QVERIFY(waitForLog(m_core.get(), QStringLiteral("meldet: Video nicht abspielbar"), &m_coreLog, 5000));
        QVERIFY(m_coreLog.contains(QStringLiteral("Browserquelle /o/pruefung-browserquelle/… meldet:")));
        const QImage page = grab();
        for (int y = box.top(); y <= box.bottom(); ++y)
            for (int x = box.left(); x <= box.right(); ++x)
                QCOMPARE(qAlpha(page.pixel(x, y)), 0);
    }

    // Last (nur mit DSS_BROWSERQUELLE_LAST=1): 10 Quellen, 2 Videos 1080p30
    void load()
    {
        if (qEnvironmentVariableIntValue("DSS_BROWSERQUELLE_LAST") != 1)
            QSKIP("Last nur mit DSS_BROWSERQUELLE_LAST=1");
        const QString dir = QStringLiteral(DSS_TEST_LARGE_DIR);
        QVERIFY(QDir().mkpath(dir));
        const QString webm = dir + QStringLiteral("/last-1080p30.webm");
        const QString mp4 = dir + QStringLiteral("/last-1080p30.mp4");
        const auto make = [](const QStringList &args) {
            QProcess ff;
            ff.start(QStringLiteral("ffmpeg"), args);
            return ff.waitForFinished(600000) && ff.exitCode() == 0;
        };
        if (!QFile::exists(webm))
            QVERIFY(make({QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                          QStringLiteral("-i"), QStringLiteral("testsrc2=s=1920x1080:r=30:d=20"), QStringLiteral("-c:v"), QStringLiteral("libvpx-vp9"),
                          QStringLiteral("-b:v"), QStringLiteral("4M"), QStringLiteral("-deadline"), QStringLiteral("realtime"),
                          QStringLiteral("-cpu-used"), QStringLiteral("8"), webm}));
        if (!QFile::exists(mp4))
            QVERIFY(make({QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"),
                          QStringLiteral("-i"), QStringLiteral("testsrc2=s=1920x1080:r=30:d=20"), QStringLiteral("-c:v"), QStringLiteral("libx264"),
                          QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-b:v"), QStringLiteral("4M"), mp4}));
        // Overlay 1920x1080: 2 Videos, 6 Bilder, 2 Texte (über den Kern angelegt)
        ChangeRequest c;
        c.type = QString::fromLatin1(msg::kCreateOverlay);
        c.name = QStringLiteral("Last");
        c.width = 1920;
        c.height = 1080;
        ChangeResult res = change(c);
        QVERIFY(res.ok);
        const QUuid id = res.id;
        const auto add = [&](const char *type, const QString &file) {
            ChangeRequest a;
            a.type = QString::fromLatin1(type);
            a.overlayId = id;
            a.file = file;
            const ChangeResult r = change(a);
            return r.ok ? r.id : QUuid();
        };
        const QList<QUuid> videos = {add(msg::kAddVideoSource, webm), add(msg::kAddVideoSource, mp4)};
        const QString media = QStringLiteral(DSS_TEST_MEDIA_DIR);
        QList<QUuid> sources = videos;
        for (int i = 1; i <= 5; ++i)
            sources.append(add(msg::kAddImageSource, media + QStringLiteral("/bild%1.png").arg(i)));
        sources.append(add(msg::kAddImageSource, kExif + QStringLiteral("/orientierung-6.jpg")));
        sources.append(add(msg::kAddTextSource, QString()));
        sources.append(add(msg::kAddTextSource, QString()));
        QCOMPARE(sources.size(), 10);
        for (const QUuid &s : sources)
            QVERIFY(!s.isNull());
        // Videos: halbe Fläche je, wiederholen
        for (int i = 0; i < 2; ++i) {
            SourceChanges ch;
            ch.x = i * 960;
            ch.y = 0;
            ch.width = 960;
            ch.height = 540;
            ch.loop = true;
            QVERIFY(change(sourceUpdate(id, videos.at(i), ch)).ok);
        }
        m_client->requestBrowserSource(id);
        QTRY_VERIFY_WITH_TIMEOUT(url().contains(QStringLiteral("/o/last/")), 3000);
        auto view = std::make_unique<QWebEngineView>();
        view->page()->setBackgroundColor(Qt::transparent);
        view->page()->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
        view->resize(1920, 1080);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        view->load(QUrl(url()));
        QTest::qWait(5000); // Laden, Videos anlaufen

        const qint64 corePid = m_core->processId();
        const qint64 self = QCoreApplication::applicationPid();
        const auto sampleAll = [&] {
            QList<std::pair<QString, ProcessSample>> s;
            s.append({QStringLiteral("dss-core"), sampleProcess(corePid)});
            s.append({QStringLiteral("Testprogramm"), sampleProcess(self)});
            ProcessSample children;
            for (qint64 p : descendants(self)) {
                const ProcessSample c = sampleProcess(p);
                children.ticks += c.ticks;
                children.rssKiB += c.rssKiB;
            }
            s.append({QStringLiteral("QtWebEngineProcess (alle)"), children});
            return s;
        };
        const qint64 hz = sysconf(_SC_CLK_TCK);
        const auto a = sampleAll();
        QElapsedTimer t;
        t.start();
        QTest::qWait(30000);
        const auto b = sampleAll();
        const double seconds = t.elapsed() / 1000.0;
        for (int i = 0; i < a.size(); ++i) {
            const double cpu = 100.0 * double(b.at(i).second.ticks - a.at(i).second.ticks) / double(hz) / seconds;
            qInfo("Last %-28s CPU %6.1f %% eines Kerns, RSS %lld MiB", qPrintable(a.at(i).first), cpu,
                  static_cast<long long>(b.at(i).second.rssKiB / 1024));
        }
        // Videos laufen noch: Zeit und verworfene Bilder nach Chromium (getVideoPlaybackQuality,
        // https://developer.mozilla.org/en-US/docs/Web/API/HTMLVideoElement/getVideoPlaybackQuality)
        bool done = false;
        QVariant state;
        view->page()->runJavaScript(QStringLiteral(
            "JSON.stringify([...document.querySelectorAll('video')].map(v=>{const q=v.getVideoPlaybackQuality();"
            "return {t:v.currentTime,r:v.readyState,total:q.totalVideoFrames,dropped:q.droppedVideoFrames};}))"),
            [&](const QVariant &v) { state = v; done = true; });
        QVERIFY(QTest::qWaitFor([&] { return done; }, 5000));
        qInfo("Last Videos: %s", qPrintable(state.toString()));
        view.reset();
        m_client->requestBrowserSource(m_id);
    }
};

int main(int argc, char *argv[])
{
    if (qgetenv("XDG_SESSION_TYPE") != "x11" || qEnvironmentVariableIsEmpty("DISPLAY")) {
        qInfo("übersprungen: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)");
        return 77;
    }
    if (detectHomeAddress().isEmpty()) {
        qInfo("übersprungen: keine Adresse im Heimnetz (Standardroute fehlt)");
        return 77;
    }
    QApplication app(argc, argv);
    TestBrowserquelle test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_browserquelle.moc"
