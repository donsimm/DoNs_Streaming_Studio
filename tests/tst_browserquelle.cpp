// P10, Stufe 1 der Prüfung der Browserquelle (Entscheid des Nutzers): Qt WebEngine (Chromium)
// lädt die Seite eines eigenen Kerns über die echte Netzwerkkarte (http://<Adresse im
// Heimnetz>:<Port>/o/…) und greift das Bild mit Durchsichtigkeit ab wie in P02, M2
// (QQuickWidget::grabFramebuffer auf das innere Widget des QWebEngineView,
// Format_RGBA8888_Premultiplied). Vergleich mit dem Zeichner des Editors (dss_render) mit den
// Testdaten tests/daten/exif/ und tests/daten/video-drehung/, Text, Spiegeln, Drehung.
// Weiter: Änderung ohne Neuladen (Zeit), Ausgabe aus/ein, Video beginnt beim Einblenden von vorne,
// neuer Token (alte Adresse leer, Hinweis), Wiederverbinden nach Neustart des Kerns.
// P12b b4: Seite in OBS mit nachgebildetem window.obsstudio und CustomEvent
// «obsSourceActiveChanged»: Meldungen an …/clock (Inhalt, Folge, Antwort des Kerns), anhalten und
// zurück bei «inaktiv», neue Datei von vorne; ohne window.obsstudio keine Meldungen. Rückfall
// «obs_active» des Kerns (GetSourceActive gegen obstestserver.h, kein echtes OBS): aktiv, inaktiv,
// Ereignis aus OBS hat Vorrang; ohne OBS kein «obs_active».
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
// QWebEngineScript (DocumentCreation, MainWorld) https://doc.qt.io/qt-6.8/qwebenginescript.html,
// obs-browser (window.obsstudio, obsSourceActiveChanged auf window, event.detail.active)
// https://github.com/obsproject/obs-browser/blob/master/README.md,
// /proc/<pid>/stat und statm: https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html

#include "bildvergleich.h"
#include "coreclient.h"
#include "coresession.h"
#include "coresettings.h"
#include "overlaylive.h"
#include "overlayimage.h"
#include "overlaystore.h"
#include "mediainfo.h"
#include "obstestserver.h"
#include "prozesshilfe.h"
#include "sourcerenderer.h"
#include "version.h"
#include "windowedit.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQuickWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QRegularExpression>
#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
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

// Toleranz der Videofarben in checkQuarters (grösster Kanal), siehe dort. Gemessen 2026-11-08 (P11):
// Editor höchstens 2, Seite höchstens 64. Editor: Reserve wie die Grenze in tst_bildvergleich
// (× 1.25, mindestens + 2).
constexpr int kEditorVideoTolerance = 4;
constexpr int kPageVideoTolerance = 80;

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

// Vor den Skripten der Seite: Meldungen an «clock» aufzeichnen (Inhalt, keepalive, Content-Type,
// Antwort des Kerns) und mit obs window.obsstudio nachbilden (pluginVersion wie in M3).
void addClockRecorder(QWebEngineView *view, bool obs)
{
    QWebEngineScript script;
    script.setName(QStringLiteral("dss-clock-recorder"));
    script.setInjectionPoint(QWebEngineScript::DocumentCreation);
    script.setWorldId(QWebEngineScript::MainWorld);
    script.setRunsOnSubFrames(false);
    script.setSourceCode(QStringLiteral(
        "(() => {"
        " window.dssClock = [];"
        " const original = window.fetch.bind(window);"
        " window.fetch = (url, init) => {"
        "  const result = original(url, init);"
        "  if (String(url) === 'clock') {"
        "   const e = { body: JSON.parse(init.body), keepalive: init.keepalive === true,"
        "               type: init.headers ? init.headers['Content-Type'] : '', status: 0 };"
        "   window.dssClock.push(e);"
        "   result.then((r) => { e.status = r.status; }, () => { e.status = -1; });"
        "  }"
        "  return result;"
        " };"
        " %1"
        "})();").arg(obs ? QStringLiteral("window.obsstudio = { pluginVersion: '2.26.9' };") : QString()));
    view->page()->scripts().insert(script);
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
    QUuid m_textSource; // «Hallo Welt …», P14 d Laufzustand
    std::unique_ptr<QWebEngineView> m_view;
    QString m_saveDir;
    QString m_oldToken;
    QUuid m_obsId;    // Overlay für die Seite in OBS (P12b b4)
    QUuid m_obsVideo; // tests/daten/ton/blitz-piep.mp4, 60 s, ohne Wiederholen
    QUuid m_obsLoop;  // drehung-0.mp4, 10 Bilder, wiederholt

    bool startCore()
    {
        if (m_core)
            m_allLog += m_coreLog + QString::fromUtf8(m_core->readAllStandardError());
        m_core = testhelp::startCore(m_data.path(), m_socket, m_config.path());
        m_coreLog.clear();
        return waitForLog(m_core.get(), QStringLiteral("Overlay-Server lauscht auf %1:%2").arg(m_home).arg(m_port),
                          &m_coreLog, 8000);
    }

    // Wie waitForLog, aber mit Ereignisschleife: Die Attrappe von OBS (obstestserver.h) läuft im
    // selben Prozess und antwortet sonst nicht.
    bool waitForCoreLog(const QString &text, int timeoutMs)
    {
        return QTest::qWaitFor([&] {
            m_coreLog += QString::fromUtf8(m_core->readAllStandardError());
            return m_coreLog.contains(text);
        }, timeoutMs);
    }

    QVariant js(const QString &code) { return js(m_view.get(), code); }

    QVariant js(QWebEngineView *view, const QString &code)
    {
        QVariant result;
        bool done = false;
        view->page()->runJavaScript(code, [&](const QVariant &v) {
            result = v;
            done = true;
        });
        static_cast<void>(QTest::qWaitFor([&] { return done; }, 5000));
        return result;
    }

    // Aufgezeichnete Meldungen an «clock» (addClockRecorder), bis vor die erste ohne Antwort (die
    // Seite meldet weiter, während der Test liest)
    QJsonArray clockReports(QWebEngineView *view)
    {
        return QJsonDocument::fromJson(js(view, QStringLiteral("(() => { const i = window.dssClock.findIndex(e => e.status === 0);"
                                                               " return JSON.stringify(i < 0 ? window.dssClock : window.dssClock.slice(0, i)); })()"))
                                           .toString()
                                           .toUtf8())
            .array();
    }

    void setObsActive(QWebEngineView *view, bool active)
    {
        js(view, QStringLiteral("window.dispatchEvent(new CustomEvent('obsSourceActiveChanged', {detail: {active: %1}}))")
                     .arg(active ? QStringLiteral("true") : QStringLiteral("false")));
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

    // videoTolerance: JPEG ist verlustbehaftet (40), PNG 4. Videos (H.264 ohne Farbangabe, kleiner
    // als 720 Pixel): Der Editor rechnet seit P11 (Punkt 79) wie die Browserquelle in OBS (BT.601
    // ohne Umrechnung der Grundfarben). Qt WebEngine (Chromium 122) rechnet die Grundfarben um (Rot
    // 248/36/0, Grün 64/252/0, Bericht P10); OBS (Chromium 146) tut das nicht (Prüfung V, Bericht
    // P11). Für die Seite gilt deshalb weiter 80 als bekannter Unterschied.
    void checkQuarters(const QImage &image, const QString &what, int videoTolerance)
    {
        int worstVideo = 0;
        for (int i = 0; i < m_oriented.size(); ++i) {
            const QRect box = m_oriented.at(i);
            const bool video = i >= 16;
            const int tolerance = video ? videoTolerance : i < 8 ? 40 : 4;
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
                if (video)
                    worstVideo = std::max(worstVideo, distance(c, q.color));
            }
        }
        qInfo("%s: Videos höchstens %d neben den Farben der Viertel (Toleranz %d)", qPrintable(what), worstVideo,
              videoTolerance);
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
        m_textSource = text;
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

        // Overlay für die Seite in OBS (P12b b4): ein langes Video mit Ton, ein kurzes wiederholtes
        m_obsId = store.createOverlay(QStringLiteral("Prüfung Seite in OBS"), 640, 360, &error);
        QVERIFY2(!m_obsId.isNull(), qPrintable(error));
        m_obsVideo = store.addVideoSource(m_obsId, QStringLiteral(DSS_TEST_TON_DIR "/blitz-piep.mp4"), false, &error);
        QVERIFY2(!m_obsVideo.isNull(), qPrintable(error));
        m_obsLoop = store.addVideoSource(m_obsId, kVideos + QStringLiteral("/drehung-0.mp4"), true, &error);
        QVERIFY2(!m_obsLoop.isNull(), qPrintable(error));
        {
            SourceChanges c;
            c.x = 0;
            c.y = 0;
            c.width = 320;
            c.height = 180;
            QVERIFY2(store.updateSource(m_obsId, m_obsVideo, c, &error), qPrintable(error));
            c.x = 400;
            c.width = 160;
            c.height = 90;
            QVERIFY2(store.updateSource(m_obsId, m_obsLoop, c, &error), qPrintable(error));
        }

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
        addClockRecorder(m_view.get(), false); // ohne window.obsstudio: keine Meldungen (obsPage)
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

        checkQuarters(editor, QStringLiteral("Editor"), kEditorVideoTolerance);
        checkQuarters(page, QStringLiteral("Seite"), kPageVideoTolerance);
        // Abweichungen mit Zahlen (nur Protokoll; die feste Grenze prüft tst_bildvergleich, P11)
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
        checkQuarters(grab(), QStringLiteral("nach Ausgabe ein"), kPageVideoTolerance);
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

    // P14 d: Laufzustand über «event: live» ohne Neuladen; Platzhalter im Text, Ausblenden,
    // Zurücksetzen; eine neu geladene Seite bekommt den Laufzustand sofort. Die Overlay-Datei bleibt.
    void liveState()
    {
        CoreSession session;
        QString error;
        QVERIFY2(session.open(m_socket, QStringLiteral("tst_browserquelle"), &error), qPrintable(error));
        const auto send = [&](const QJsonObject &message) {
            const auto result = session.request(message, &error);
            return result && result->ok;
        };
        const auto pageText = [&] { return js(QStringLiteral("document.body.innerText")).toString(); };
        const auto liveCount = [&] { return js(QStringLiteral("window.dssState.live")).toInt(); };
        js(QStringLiteral("window.dssMarker = 4712"));

        LiveSetRequest set;
        set.overlayId = m_id;
        set.mode = LiveMode::Show;
        set.elements = {{m_textSource, QStringLiteral("Neuer Follower: %user%"), QString()}};
        set.arguments = {{QStringLiteral("user"), QStringLiteral("Zuschauer42")}};
        int count = liveCount();
        QVERIFY2(send(liveSetMessage(set)), qPrintable(error));
        QVERIFY(QTest::qWaitFor([&] { return liveCount() > count && pageText().contains(QStringLiteral("Neuer Follower: Zuschauer42")); }, 5000));
        QVERIFY(!pageText().contains(QStringLiteral("Hallo Welt")));
        QCOMPARE(js(QStringLiteral("window.dssMarker")).toInt(), 4712);
        save(grab(), QStringLiteral("laufzustand-text.png"));

        // Neu laden: zuerst «overlay», dann «live»
        m_view->load(QUrl(url()));
        QVERIFY(QTest::qWaitFor([&] { return pageText().contains(QStringLiteral("Neuer Follower: Zuschauer42")); }, 15000));

        // Ausblenden: Text weg, im Textkasten nichts Deckendes
        set.mode = LiveMode::Hide;
        set.elements = {{m_textSource, std::nullopt, QString()}};
        QVERIFY2(send(liveSetMessage(set)), qPrintable(error));
        QVERIFY(QTest::qWaitFor([&] { return !pageText().contains(QStringLiteral("Neuer Follower")); }, 5000));
        QVERIFY(QTest::qWaitFor([&] { return allTransparent(grab().copy(m_textBox)); }, 5000));

        // Zurücksetzen: wieder wie im Overlay
        LiveResetRequest reset;
        reset.overlayId = m_id;
        QVERIFY2(send(liveResetMessage(reset)), qPrintable(error));
        QVERIFY(QTest::qWaitFor([&] { return pageText().contains(QStringLiteral("Hallo Welt")); }, 5000));
        QVERIFY(pageReady(5));
        const auto overlay = session.getOverlay(m_id, &error);
        QVERIFY2(overlay, qPrintable(error));
        const auto it = std::find_if(overlay->sources.begin(), overlay->sources.end(),
                                     [&](const Source &s) { return s.id == m_textSource; });
        QVERIFY(it != overlay->sources.end());
        QCOMPARE(it->text.content,QStringLiteral("Hallo Welt\nZweite Zeile ÄÖÜ"));
        QVERIFY(it->visible);
    }

    // P12b b4: Seite in OBS (window.obsstudio und «obsSourceActiveChanged» nachgebildet). Meldungen
    // an …/clock: Inhalt, Folge, Antwort 204; «inaktiv» hält an und setzt zurück; neue Datei von
    // vorne. Ohne window.obsstudio (m_view) keine Meldungen. Zum Schluss neuer Token: auch die Seite
    // mit …/events/<page> lädt neu und ist leer.
    void obsPage()
    {
        const QString mainUrl = url();
        m_client->requestBrowserSource(m_obsId);
        QTRY_VERIFY_WITH_TIMEOUT(!url().isEmpty() && url() != mainUrl, 3000);
        const QString obsUrl = url();
        m_client->requestBrowserSource(m_id);
        QTRY_VERIFY_WITH_TIMEOUT(url() == mainUrl, 3000);

        auto view = std::make_unique<QWebEngineView>();
        view->page()->setBackgroundColor(Qt::transparent);
        view->page()->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
        addClockRecorder(view.get(), true);
        view->resize(640, 360);
        view->show();
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        view->load(QUrl(obsUrl));
        QVERIFY(QTest::qWaitFor([&] { return js(view.get(), QStringLiteral("window.dssState ? window.dssState.applied : 0")).toInt() >= 1; }, 15000));
        const QString videos = QStringLiteral("[...document.querySelectorAll('video')]");
        QVERIFY(QTest::qWaitFor([&] {
            return js(view.get(), videos + QStringLiteral(".filter(v => v.readyState >= 2).length")).toInt() == 2;
        }, 15000));
        const QString page = js(view.get(), QStringLiteral("window.dssState.page")).toString();
        QVERIFY2(QRegularExpression(QStringLiteral("^[a-z0-9]{8,32}$")).match(page).hasMatch(), qPrintable(page));
        const QString longFile = js(view.get(), videos + QStringLiteral("[0].dssFile")).toString();
        const QString loopFile = js(view.get(), videos + QStringLiteral("[1].dssFile")).toString();
        QVERIFY(longFile.startsWith(QStringLiteral("media/")) && loopFile.startsWith(QStringLiteral("media/")));

        // Zustand unbekannt (wie nach dem Laden einer schon aktiven Quelle, M3): Videos laufen, keine Meldungen.
        // Ohne OBS schickt der Kern kein «obs_active» (Rückfall, obsActiveFallback).
        QTest::qWait(1200);
        QCOMPARE(js(view.get(), QStringLiteral("window.dssClock.length")).toInt(), 0);
        QVERIFY(js(view.get(), videos + QStringLiteral("[0].currentTime")).toDouble() > 0.5);
        QVERIFY(js(view.get(), QStringLiteral("window.dssState.active === null")).toBool());
        QVERIFY2(waitForCoreLog(QStringLiteral("OBS: Seite %1 von «Prüfung Seite in OBS»: Zustand im Programm "
                                               "unbekannt (OBS nicht verbunden), kein obs_active").arg(page),
                                3000),
                 qPrintable(m_coreLog));

        // Folge einer Quelle: Ereignisse mit Lauf, Wiederholung und Lage
        const auto bySource = [](const QJsonArray &all, const QUuid &source, int from = 0) {
            QList<QJsonObject> out;
            for (int i = from; i < all.size(); ++i) {
                const QJsonObject b = all.at(i).toObject().value(QStringLiteral("body")).toObject();
                if (QUuid::fromString(b.value(QStringLiteral("source")).toString()) == source)
                    out.append(b);
            }
            return out;
        };
        const auto ev = [](const QJsonObject &b) { return b.value(QStringLiteral("event")).toString(); };
        const auto num = [](const QJsonObject &b, const char *key) { return b.value(QLatin1String(key)).toDouble(-1); };
        const auto allAnswered = [&] {
            if (QTest::qWaitFor([&] { return js(view.get(), QStringLiteral("window.dssClock.every(e => e.status !== 0)")).toBool(); }, 3000))
                return true;
            // Diagnose: Meldungen ohne Antwort und Zeilen des Kerns zum Ton
            m_coreLog += QString::fromUtf8(m_core->readAllStandardError());
            qWarning("ohne Antwort: %s", qPrintable(js(view.get(), QStringLiteral("JSON.stringify(window.dssClock.filter(e => e.status === 0))")).toString()));
            for (const QString &line : m_coreLog.split(QLatin1Char('\n')))
                if (line.contains(QStringLiteral("Ton")) || line.contains(QStringLiteral("Uhr")))
                    qWarning("Kern: %s", qPrintable(line));
            return false;
        };
        // Für alle Meldungen: Version, Seite, laufende Nummer ohne Lücke, keepalive, text/plain, 204
        const auto checkAll = [&](const QJsonArray &all) {
            double lastNow = -1;
            for (int i = 0; i < all.size(); ++i) {
                const QJsonObject e = all.at(i).toObject();
                const QJsonObject b = e.value(QStringLiteral("body")).toObject();
                const QString what = QString::fromUtf8(QJsonDocument(e).toJson(QJsonDocument::Compact));
                QVERIFY2(b.value(QStringLiteral("v")).toInt() == 1 && b.value(QStringLiteral("page")).toString() == page
                             && b.value(QStringLiteral("seq")).toInt(-1) == i,
                         qPrintable(what));
                QVERIFY2(e.value(QStringLiteral("keepalive")).toBool() && e.value(QStringLiteral("type")).toString() == QStringLiteral("text/plain"),
                         qPrintable(what));
                QVERIFY2(e.value(QStringLiteral("status")).toInt() == 204, qPrintable(what));
                QVERIFY2(num(b, "now_ms") >= lastNow, qPrintable(what));
                lastNow = num(b, "now_ms");
                if (ev(b) != QStringLiteral("stop") && ev(b) != QStringLiteral("end"))
                    QVERIFY2(std::abs(num(b, "now_ms") - num(b, "display_ms")) < 200, qPrintable(what));
            }
        };

        // «aktiv»: Lauf 1, beide Videos von vorne
        setObsActive(view.get(), true);
        QTest::qWait(2600);
        QVERIFY(allAnswered());
        QJsonArray all = clockReports(view.get());
        checkAll(all);
        if (QTest::currentTestFailed())
            return;
        const QList<QJsonObject> longRun = bySource(all, m_obsVideo);
        const QList<QJsonObject> loopRun = bySource(all, m_obsLoop);
        QCOMPARE(int(longRun.size() + loopRun.size()), int(all.size()));
        // Langes Video: start bei 0, dann etwa jede Sekunde frame, Länge 60 s
        QVERIFY(longRun.size() >= 3);
        QCOMPARE(ev(longRun.first()), QStringLiteral("start"));
        for (int i = 0; i < longRun.size(); ++i) {
            const QJsonObject &b = longRun.at(i);
            QCOMPARE(b.value(QStringLiteral("run")).toInt(), 1);
            QCOMPARE(b.value(QStringLiteral("loop")).toInt(-1), 0);
            QCOMPARE(b.value(QStringLiteral("file")).toString(), longFile);
            QVERIFY(std::abs(num(b, "duration_s") - 60.0) < 0.1);
            if (i == 0) {
                QVERIFY2(num(b, "media_s") >= 0 && num(b, "media_s") < 0.2, qPrintable(QString::number(num(b, "media_s"))));
                continue;
            }
            QCOMPARE(ev(b), QStringLiteral("frame"));
            const double gap = num(b, "display_ms") - num(longRun.at(i - 1), "display_ms");
            const double media = num(b, "media_s") - num(longRun.at(i - 1), "media_s");
            qInfo("frame nach %.1f ms, Lage + %.3f s", gap, media);
            QVERIFY(gap >= 1000 && gap < 1500);
            QVERIFY(media > 0.9 && media < 1.6);
        }
        // Kurzes Video (0,33 s, wiederholt): start, danach loop mit Durchgang 1, 2, 3 …
        QVERIFY(loopRun.size() >= 5);
        QCOMPARE(ev(loopRun.first()), QStringLiteral("start"));
        int loops = 0;
        for (const QJsonObject &b : loopRun) {
            QCOMPARE(b.value(QStringLiteral("run")).toInt(), 1);
            QCOMPARE(b.value(QStringLiteral("file")).toString(), loopFile);
            QVERIFY(num(b, "duration_s") > 0.2 && num(b, "duration_s") < 0.5);
            if (ev(b) == QStringLiteral("loop")) {
                QCOMPARE(b.value(QStringLiteral("loop")).toInt(), ++loops);
                QVERIFY2(num(b, "media_s") < 0.2, qPrintable(QString::number(num(b, "media_s"))));
            } else {
                QCOMPARE(b.value(QStringLiteral("loop")).toInt(), loops);
            }
        }
        qInfo("Lauf 1: %d Meldungen, langes Video %d, kurzes %d (Wiederholungen %d)", int(all.size()),
              int(longRun.size()), int(loopRun.size()), loops);
        QVERIFY(loops >= 4);

        // «inaktiv»: stop ohne Quelle, Videos angehalten am Anfang, danach keine Meldungen
        setObsActive(view.get(), false);
        QTRY_VERIFY_WITH_TIMEOUT(js(view.get(), videos + QStringLiteral(".every(v => v.paused && v.currentTime === 0 && !v.seeking)")).toBool(), 3000);
        QTest::qWait(1200);
        QVERIFY(allAnswered());
        all = clockReports(view.get());
        checkAll(all);
        if (QTest::currentTestFailed())
            return;
        const QJsonObject stop =all.last().toObject().value(QStringLiteral("body")).toObject();
        QCOMPARE(ev(stop), QStringLiteral("stop"));
        QVERIFY(!stop.contains(QStringLiteral("source")));
        QCOMPARE(stop.value(QStringLiteral("run")).toInt(), 1);
        QVERIFY(js(view.get(), videos + QStringLiteral(".every(v => v.paused && v.currentTime === 0)")).toBool());
        const int afterStop = int(all.size());
        QCOMPARE(js(view.get(), QStringLiteral("window.dssClock.length")).toInt(), afterStop);

        // wieder «aktiv»: Lauf 2, beide von vorne mit start
        setObsActive(view.get(), true);
        QTest::qWait(600);
        QVERIFY(allAnswered());
        all = clockReports(view.get());
        checkAll(all);
        if (QTest::currentTestFailed())
            return;
        for (const QUuid &s : {m_obsVideo, m_obsLoop}) {
            const QList<QJsonObject> run2 = bySource(all, s, afterStop);
            QVERIFY(!run2.isEmpty());
            QCOMPARE(ev(run2.first()), QStringLiteral("start"));
            QCOMPARE(run2.first().value(QStringLiteral("run")).toInt(), 2);
            QCOMPARE(run2.first().value(QStringLiteral("loop")).toInt(-1), 0);
            QVERIFY(num(run2.first(), "media_s") < 0.2);
        }

        // Neue Datei (nachgebildet, der Kern ändert die Datei eines Videos heute nicht): das lange
        // Video bekommt die Datei des kurzen → neues Element von vorne, start; ohne Wiederholen
        // danach end.
        const int beforeFile = int(clockReports(view.get()).size());
        js(view.get(), QStringLiteral("(() => { const d = JSON.parse(JSON.stringify(lastData));"
                                      " const s = d.overlay.sources.find(x => x.kind === 'video' && !x.video.loop);"
                                      " s.video.file = '%1'; apply(d); })()").arg(loopFile));
        QVERIFY(QTest::qWaitFor([&] {
            const QList<QJsonObject> r = bySource(clockReports(view.get()), m_obsVideo, beforeFile);
            return std::any_of(r.cbegin(), r.cend(), [&](const QJsonObject &b) { return ev(b) == QStringLiteral("end"); });
        }, 5000));
        QVERIFY(allAnswered());
        all = clockReports(view.get());
        checkAll(all);
        if (QTest::currentTestFailed())
            return;
        const QList<QJsonObject> newFile = bySource(all, m_obsVideo, beforeFile);
        QCOMPARE(ev(newFile.first()), QStringLiteral("start"));
        QCOMPARE(newFile.first().value(QStringLiteral("file")).toString(), loopFile);
        QCOMPARE(newFile.first().value(QStringLiteral("run")).toInt(), 2);
        QCOMPARE(newFile.first().value(QStringLiteral("loop")).toInt(-1), 0);
        QVERIFY(num(newFile.first(), "media_s") < 0.2);
        QCOMPARE(ev(newFile.last()), QStringLiteral("end"));
        QCOMPARE(newFile.last().value(QStringLiteral("run")).toInt(), 2);
        QVERIFY(!newFile.last().contains(QStringLiteral("file")));

        // Kern hat nichts abgewiesen; die Seite ohne window.obsstudio hat nichts gemeldet
        m_coreLog += QString::fromUtf8(m_core->readAllStandardError());
        QVERIFY(!m_coreLog.contains(QStringLiteral("Meldung der Uhr abgewiesen")));
        QCOMPARE(js(QStringLiteral("window.dssClock.length")).toInt(), 0);
        QCOMPARE(js(QStringLiteral("window.dssState.page")).toString(), QString());

        // Neuer Token: Die Seite mit …/events/<page> bekommt «gone», lädt neu und ist leer
        ChangeRequest r;
        r.type = QString::fromLatin1(msg::kNewToken);
        r.overlayId = m_obsId;
        QVERIFY(change(r).ok);
        QVERIFY(QTest::qWaitFor([&] { return js(view.get(), QStringLiteral("document.getElementById('overlay') === null")).toBool(); }, 8000));
        m_client->requestBrowserSource(m_id);
        QTRY_VERIFY_WITH_TIMEOUT(url() == mainUrl, 3000);
    }

    // P12b, Rückfall nach M3: Beim Laden einer schon aktiven Quelle meldet OBS kein «aktiv». Der
    // Kern fragt OBS (hier obstestserver.h, GetSourceActive) beim Beginn von …/events/<page> und
    // schickt «obs_active»; die Seite übernimmt es nur bei unbekanntem Zustand.
    void obsActiveFallback()
    {
        testhelp::ObsTestServer server;
        QVERIFY(server.start());
        SettingsUpdate update;
        update.obsEnabled = true;
        update.obsHost = QStringLiteral("127.0.0.1");
        update.obsPort = server.port();
        QSignalSpy settingsAnswered(m_client.get(), &CoreClient::settingsUpdateFinished);
        m_client->sendSettingsUpdate(update);
        QTRY_COMPARE(settingsAnswered.size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(m_client->connectionStates().value(Connection::Obs), ConnectionState::Connected, 10000);
        // Quelle in OBS über den Kern anlegen: Adresse wie im Betrieb
        QSignalSpy answered(m_client.get(), &CoreClient::obsRequestFinished);
        QVERIFY(m_client->sendObsCreateSource(m_obsId, QStringLiteral("scene-live"), QStringLiteral("Seite in OBS")) > 0);
        QTRY_COMPARE(answered.size(), 1);
        QVERIFY(answered.takeFirst().first().value<ChangeResult>().ok);
        const QString input = server.inputs.constLast().uuid;

        const QString mainUrl = url();
        m_client->requestBrowserSource(m_obsId);
        QTRY_VERIFY_WITH_TIMEOUT(!url().isEmpty() && url() != mainUrl, 3000);
        const QString obsUrl = url();
        QCOMPARE(server.input(input)->settings.value(QStringLiteral("url")).toString(), obsUrl);
        m_client->requestBrowserSource(m_id);
        QTRY_VERIFY_WITH_TIMEOUT(url() == mainUrl, 3000);

        const QString videos = QStringLiteral("[...document.querySelectorAll('video')]");
        const auto open = [&] {
            auto view = std::make_unique<QWebEngineView>();
            view->page()->setBackgroundColor(Qt::transparent);
            view->page()->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
            addClockRecorder(view.get(), true);
            view->resize(640, 360);
            view->show();
            static_cast<void>(QTest::qWaitForWindowExposed(view.get()));
            view->load(QUrl(obsUrl));
            static_cast<void>(QTest::qWaitFor([&] {
                return js(view.get(), QStringLiteral("window.dssState ? window.dssState.applied : 0")).toInt() >= 1;
            }, 15000));
            return view;
        };
        const auto events = [&](QWebEngineView *view) {
            QStringList out;
            const QJsonArray all = clockReports(view);
            for (const QJsonValue &e : all)
                out.append(e.toObject().value(QStringLiteral("body")).toObject().value(QStringLiteral("event")).toString());
            return out;
        };
        const auto sentLine = [](const QString &page, bool active) {
            return QStringLiteral("OBS: Seite %1 von «Prüfung Seite in OBS»: Quelle %2 (1 Quellen gefragt), obs_active geschickt")
                .arg(page, active ? QStringLiteral("im Programm") : QStringLiteral("nicht im Programm"));
        };

        // Quelle im Programm: «aktiv» ohne Ereignis aus OBS, Lauf 1 von vorne mit «start»
        server.activeInputs.insert(input);
        {
            auto view = open();
            QVERIFY(QTest::qWaitFor([&] { return js(view.get(), QStringLiteral("window.dssState.active === true")).toBool(); }, 5000));
            const QString page = js(view.get(), QStringLiteral("window.dssState.page")).toString();
            QVERIFY(QTest::qWaitFor([&] { return events(view.get()).count(QStringLiteral("start")) == 2; }, 8000));
            const QJsonObject first = clockReports(view.get()).first().toObject().value(QStringLiteral("body")).toObject();
            QCOMPARE(first.value(QStringLiteral("run")).toInt(), 1);
            QVERIFY(first.value(QStringLiteral("media_s")).toDouble(-1) < 0.2);
            QVERIFY2(waitForCoreLog(sentLine(page, true), 3000), qPrintable(m_coreLog));
            // Ein echtes Ereignis aus OBS gilt danach wie gewohnt
            setObsActive(view.get(), false);
            QTRY_VERIFY_WITH_TIMEOUT(events(view.get()).endsWith(QStringLiteral("stop")), 3000);
        }

        // Quelle nicht im Programm: «inaktiv», Videos am Anfang angehalten, «stop»
        server.activeInputs.clear();
        {
            auto view = open();
            QVERIFY(QTest::qWaitFor([&] { return js(view.get(), QStringLiteral("window.dssState.active === false")).toBool(); }, 5000));
            const QString page = js(view.get(), QStringLiteral("window.dssState.page")).toString();
            QTRY_VERIFY_WITH_TIMEOUT(js(view.get(), videos + QStringLiteral(".every(v => v.paused && v.currentTime === 0 && !v.seeking)")).toBool(), 3000);
            QTRY_COMPARE_WITH_TIMEOUT(events(view.get()), QStringList{QStringLiteral("stop")}, 3000);
            QVERIFY2(waitForCoreLog(sentLine(page, false), 3000), qPrintable(m_coreLog));
        }

        // Ereignis aus OBS vor der Antwort: hat Vorrang, «obs_active» (inaktiv) wird übergangen
        server.delayedRequests.insert(QStringLiteral("GetSourceActive"), 1500);
        {
            auto view = open();
            setObsActive(view.get(), true);
            const QString page = js(view.get(), QStringLiteral("window.dssState.page")).toString();
            QVERIFY2(waitForCoreLog(sentLine(page, false), 5000), qPrintable(m_coreLog));
            QTest::qWait(500);
            QVERIFY(js(view.get(), QStringLiteral("window.dssState.active === true")).toBool());
            const QStringList seen = events(view.get());
            QVERIFY2(!seen.contains(QStringLiteral("stop")) && seen.count(QStringLiteral("start")) == 2, qPrintable(seen.join(u' ')));
            QVERIFY(js(view.get(), videos + QStringLiteral(".every(v => !v.paused)")).toBool());
        }
        server.delayedRequests.clear();

        // OBS wieder aus (die weiteren Tests laufen ohne)
        update = SettingsUpdate();
        update.obsEnabled = false;
        settingsAnswered.clear();
        m_client->sendSettingsUpdate(update);
        QTRY_COMPARE(settingsAnswered.size(), 1);
        m_coreLog += QString::fromUtf8(m_core->readAllStandardError());
        QVERIFY(!m_coreLog.contains(QStringLiteral("Meldung der Uhr abgewiesen")));
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
        checkQuarters(grab(), QStringLiteral("neue Adresse"), kPageVideoTolerance);
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
