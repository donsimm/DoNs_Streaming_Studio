// Messprototyp M2 (Prompt P02): Variante B, Bilder aus Qt WebEngine in den Speicher abgreifen.
// Ausgehend von prototypes/b-web/host; Seite und Kern bleiben die aus prototypes/b-web.
//
//   --url URL --count 1|2 --visibility visible|covered|minimized --grab widget|framebuffer
//   --duration S --stats DATEI
//
// Je Overlay ein QWebEngineView mit 1920x1080. Ein Taktgeber holt alle 1/30 s von jedem
// Overlay ein Bild in den Speicher (QImage) und gibt es an einen eigenen Arbeits-Thread,
// der es nach UYVY und UYVA umwandelt (Schätzung des NDI-Vorbereitungsaufwands, ohne NDI).
//
// Sichtbarkeit:
//   visible    Fenster im Vollbild; bei zwei Overlays liegt das zweite über dem ersten.
//   covered    wie visible, nach 1 s legt sich ein schwarzes Vollbildfenster darüber.
//   minimized  wie visible, nach 3 s werden die Overlay-Fenster minimiert.
//   offscreen  Fenster ohne Rahmen und am Fenstermanager vorbei ganz ausserhalb des Bildschirms (x = 3840, y = 1080 * Nummer).
//
// Abgriff:
//   widget       QWidget::grab() auf den QWebEngineView (rendert das Widget mit Kindern).
//   framebuffer  QQuickWidget::grabFramebuffer() auf das innere Darstellungs-Widget.
//
// Am Ende holt das Programm die Zeitstempel der Seite (Objekt `stats` aus overlay.html) per
// runJavaScript, weil die Seite ihre Meldung nur aus requestAnimationFrame sendet und rAF bei
// verdecktem oder minimiertem Fenster ausbleiben kann.
//
// Quellen:
//   QWidget::grab:                https://doc.qt.io/qt-6.8/qwidget.html#grab
//   QQuickWidget::grabFramebuffer: https://doc.qt.io/qt-6.8/qquickwidget.html
//   QWebEnginePage::runJavaScript: https://doc.qt.io/qt-6.8/qwebenginepage.html#runJavaScript
//   QWebEngineSettings:           https://doc.qt.io/qt-6.8/qwebenginesettings.html

#include "convert.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWidget>
#include <QTimer>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

static qint64 monotonicNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

static double epochMs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count() / 1000.0;
}

static QJsonArray toJson(const std::vector<double> &v)
{
    QJsonArray a;
    for (double d : v)
        a.append(d);
    return a;
}

// Arbeits-Thread je Overlay: nimmt das jeweils neueste Bild, wandelt nach BGRA (falls nötig),
// dann nach UYVY und UYVA. Ist er noch beschäftigt, wird ein neues Bild verworfen und gezählt.
class Converter
{
public:
    Converter() : m_thread([this] { run(); }) {}
    ~Converter() { stop(); }

    void submit(const QImage &img)
    {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            if (m_hasJob) {
                ++m_dropped;
                return;
            }
            m_job = img;
            m_hasJob = true;
        }
        m_cv.notify_one();
    }

    void stop()
    {
        {
            std::lock_guard<std::mutex> l(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        if (m_thread.joinable())
            m_thread.join();
    }

    QJsonObject result() const
    {
        QJsonObject o;
        o.insert(QStringLiteral("format_ms"), toJson(m_formatMs));
        o.insert(QStringLiteral("uyvy_ms"), toJson(m_uyvyMs));
        o.insert(QStringLiteral("uyva_ms"), toJson(m_uyvaMs));
        o.insert(QStringLiteral("done_epoch_ms"), toJson(m_doneAt));
        o.insert(QStringLiteral("dropped"), m_dropped);
        o.insert(QStringLiteral("input_format"), m_inputFormat);
        return o;
    }

private:
    void run()
    {
        for (;;) {
            QImage img;
            {
                std::unique_lock<std::mutex> l(m_mutex);
                m_cv.wait(l, [this] { return m_hasJob || m_stop; });
                if (m_stop)
                    return;
                img = m_job;
                m_job = QImage();
            }
            if (m_inputFormat < 0)
                m_inputFormat = static_cast<int>(img.format());
            // ARGB32_Premultiplied liegt auf x86 (little endian) im Speicher als B, G, R, A.
            qint64 t0 = monotonicNs();
            if (img.format() != QImage::Format_ARGB32_Premultiplied)
                img.convertTo(QImage::Format_ARGB32_Premultiplied);
            qint64 t1 = monotonicNs();
            const int w = img.width(), h = img.height();
            m_uyvy.resize(static_cast<size_t>(w) * h * 2);
            m_alpha.resize(static_cast<size_t>(w) * h);
            bgraToUyvy(img.constBits(), img.bytesPerLine(), w, h, m_uyvy.data());
            qint64 t2 = monotonicNs();
            bgraToUyva(img.constBits(), img.bytesPerLine(), w, h, m_uyvy.data(), m_alpha.data());
            qint64 t3 = monotonicNs();
            m_formatMs.push_back((t1 - t0) / 1e6);
            m_uyvyMs.push_back((t2 - t1) / 1e6);
            m_uyvaMs.push_back((t3 - t2) / 1e6);
            m_doneAt.push_back(epochMs());
            {
                std::lock_guard<std::mutex> l(m_mutex);
                m_hasJob = false;
            }
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    QImage m_job;
    bool m_hasJob = false;
    bool m_stop = false;
    int m_dropped = 0;
    int m_inputFormat = -1;
    std::vector<uint8_t> m_uyvy, m_alpha;
    std::vector<double> m_formatMs, m_uyvyMs, m_uyvaMs, m_doneAt;
    std::thread m_thread;
};

struct Overlay
{
    QWebEngineView *view = nullptr;
    std::unique_ptr<Converter> converter;
    QImage last;
    std::vector<double> grabAt, newAt, grabMs, compareMs;
    QJsonObject info;
    QJsonObject pageStats;
};

class Capture : public QObject
{
public:
    Capture(const QCommandLineParser &p)
        : m_statsPath(p.value(QStringLiteral("stats"))),
          m_visibility(p.value(QStringLiteral("visibility"))),
          m_useFramebuffer(p.value(QStringLiteral("grab")) == QStringLiteral("framebuffer"))
    {
        const int count = p.value(QStringLiteral("count")).toInt();
        const int durationS = p.value(QStringLiteral("duration")).toInt();
        for (int i = 0; i < count; ++i) {
            auto ov = std::make_unique<Overlay>();
            ov->view = new QWebEngineView;
            ov->view->setAttribute(Qt::WA_DeleteOnClose, false);
            ov->view->page()->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
            ov->view->setWindowTitle(QStringLiteral("P02 M2 Overlay %1").arg(i + 1));
            ov->view->resize(1920, 1080);
            // Die Seite beendet ihre rAF-Schleife nach "duration"; deshalb länger als die Laufzeit.
            ov->view->setUrl(QUrl(p.value(QStringLiteral("url")) + QStringLiteral("?duration=%1").arg(durationS + 60)));
            if (m_visibility == QStringLiteral("offscreen")) {
                // Ohne Fenstermanager platzieren, sonst schiebt xfwm4 das Fenster auf den Bildschirm zurück.
                ov->view->setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::X11BypassWindowManagerHint);
                ov->view->setGeometry(3840, 1080 * i, 1920, 1080);
                ov->view->show();
            } else {
                ov->view->showFullScreen();
            }
            ov->converter = std::make_unique<Converter>();
            m_overlays.push_back(std::move(ov));
        }

        if (m_visibility == QStringLiteral("covered")) {
            QTimer::singleShot(1000, this, [this] {
                m_cover = new QWidget;
                m_cover->setWindowTitle(QStringLiteral("P02 M2 Abdeckung"));
                m_cover->setStyleSheet(QStringLiteral("background: black;"));
                m_cover->showFullScreen();
                m_cover->raise();
                m_cover->activateWindow();
            });
        } else if (m_visibility == QStringLiteral("offscreen")) {
            QTimer::singleShot(3000, this, [this] {
                for (int i = 0; i < static_cast<int>(m_overlays.size()); ++i)
                    m_info.insert(QStringLiteral("offscreen_geometry_%1").arg(i), QStringLiteral("%1,%2 %3x%4")
                                  .arg(m_overlays[i]->view->x()).arg(m_overlays[i]->view->y())
                                  .arg(m_overlays[i]->view->width()).arg(m_overlays[i]->view->height()));
            });
        } else if (m_visibility == QStringLiteral("minimized")) {
            QTimer::singleShot(3000, this, [this] {
                for (auto &ov : m_overlays)
                    ov->view->showMinimized();
            });
        }

        m_t0 = monotonicNs();
        m_next = m_t0;
        scheduleNext();
        QTimer::singleShot(durationS * 1000, this, [this] { finish(); });
    }

private:
    QImage grabOne(Overlay &ov)
    {
        if (m_useFramebuffer) {
            auto *qw = ov.view->findChild<QQuickWidget *>();
            if (!qw) {
                ov.info.insert(QStringLiteral("grab_error"), QStringLiteral("kein QQuickWidget gefunden"));
                return QImage();
            }
            return qw->grabFramebuffer();
        }
        return ov.view->grab().toImage();
    }

    void tick()
    {
        for (auto &ovp : m_overlays) {
            Overlay &ov = *ovp;
            const double at = epochMs();
            qint64 t0 = monotonicNs();
            QImage img = grabOne(ov);
            qint64 t1 = monotonicNs();
            if (img.isNull())
                continue;
            ov.grabAt.push_back(at);
            ov.grabMs.push_back((t1 - t0) / 1e6);
            if (!ov.info.contains(QStringLiteral("grab_format"))) {
                ov.info.insert(QStringLiteral("grab_format"), static_cast<int>(img.format()));
                ov.info.insert(QStringLiteral("grab_size"), QStringLiteral("%1x%2").arg(img.width()).arg(img.height()));
                QStringList children;
                for (QObject *c : ov.view->findChildren<QObject *>())
                    children << QString::fromLatin1(c->metaObject()->className());
                ov.info.insert(QStringLiteral("view_children"), children.join(QLatin1Char(',')));
            }
            // Neues Bild = Inhalt unterscheidet sich vom vorigen Abgriff.
            qint64 t2 = monotonicNs();
            const bool changed = ov.last.isNull() || ov.last.sizeInBytes() != img.sizeInBytes()
                || std::memcmp(ov.last.constBits(), img.constBits(), img.sizeInBytes()) != 0;
            qint64 t3 = monotonicNs();
            ov.compareMs.push_back((t3 - t2) / 1e6);
            if (changed)
                ov.newAt.push_back(at);
            ov.last = img;
            ov.converter->submit(img);
        }
    }

    // Feste Zeitpunkte alle 1/30 s, ohne Aufsummieren von Fehlern (wie Variante A in P01).
    void scheduleNext()
    {
        m_next += 1'000'000'000LL / 30;
        const qint64 waitMs = std::max<qint64>(0, (m_next - monotonicNs()) / 1'000'000);
        QTimer::singleShot(static_cast<int>(waitMs), Qt::PreciseTimer, this, [this] {
            if (m_finishing)
                return;
            tick();
            scheduleNext();
        });
    }

    void finish()
    {
        m_finishing = true;
        m_pending = static_cast<int>(m_overlays.size());
        for (auto &ovp : m_overlays) {
            Overlay *ov = ovp.get();
            ov->view->page()->runJavaScript(QStringLiteral("JSON.stringify(stats)"), [this, ov](const QVariant &v) {
                ov->pageStats = QJsonDocument::fromJson(v.toString().toUtf8()).object();
                if (--m_pending == 0)
                    write();
            });
        }
        // Falls die Seite nicht antwortet
        QTimer::singleShot(5000, this, [this] {
            if (m_pending > 0) {
                m_pending = -1;
                write();
            }
        });
    }

    void write()
    {
        QJsonArray arr;
        for (auto &ovp : m_overlays) {
            Overlay &ov = *ovp;
            ov.converter->stop();
            QJsonObject o;
            o.insert(QStringLiteral("grabs"), toJson(ov.grabAt));
            o.insert(QStringLiteral("new_frames"), toJson(ov.newAt));
            o.insert(QStringLiteral("grab_ms"), toJson(ov.grabMs));
            o.insert(QStringLiteral("compare_ms"), toJson(ov.compareMs));
            o.insert(QStringLiteral("convert"), ov.converter->result());
            o.insert(QStringLiteral("info"), ov.info);
            o.insert(QStringLiteral("page"), ov.pageStats);
            arr.append(o);
        }
        QJsonObject root;
        root.insert(QStringLiteral("visibility"), m_visibility);
        root.insert(QStringLiteral("grab"), m_useFramebuffer ? QStringLiteral("framebuffer") : QStringLiteral("widget"));
        root.insert(QStringLiteral("overlays"), arr);
        root.insert(QStringLiteral("info"), m_info);
        QFile out(m_statsPath);
        if (out.open(QIODevice::WriteOnly))
            out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        QTimer::singleShot(0, qApp, &QApplication::quit);
    }

    QString m_statsPath, m_visibility;
    bool m_useFramebuffer;
    std::vector<std::unique_ptr<Overlay>> m_overlays;
    QWidget *m_cover = nullptr;
    QJsonObject m_info;
    qint64 m_t0 = 0, m_next = 0;
    bool m_finishing = false;
    int m_pending = 0;

public:
    ~Capture() override
    {
        for (auto &ov : m_overlays)
            delete ov->view;
        delete m_cover;
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOptions({
        {QStringLiteral("url"), QStringLiteral("Seite des Kerns, ohne Parameter"), QStringLiteral("url")},
        {QStringLiteral("count"), QStringLiteral("Anzahl Overlays (1 oder 2)"), QStringLiteral("n"), QStringLiteral("1")},
        {QStringLiteral("visibility"), QStringLiteral("visible, covered, minimized oder offscreen"), QStringLiteral("v"), QStringLiteral("visible")},
        {QStringLiteral("grab"), QStringLiteral("widget oder framebuffer"), QStringLiteral("g"), QStringLiteral("widget")},
        {QStringLiteral("stats"), QStringLiteral("Ausgabedatei JSON"), QStringLiteral("file")},
        {QStringLiteral("duration"), QStringLiteral("Laufzeit in s"), QStringLiteral("s"), QStringLiteral("75")},
    });
    parser.process(app);
    Capture c(parser);
    return app.exec();
}
