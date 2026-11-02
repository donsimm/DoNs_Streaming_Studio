// Messprototyp M1 (Prompt P02): Variante A mit eigenem Dekodierweg über FFmpeg.
// Ausgehend von prototypes/a-cpp-qt (P01). Overlay, Lage und Messausgabe unverändert; nur die
// zwei Videoquellen kommen statt aus Qt Multimedia aus VideoDecoder (decoder.cpp):
//   video.webm  Standard-Decoder von FFmpeg
//   alpha.webm  ausdrücklich "libvpx-vp9" (behält den Alphakanal, siehe P01-Bericht)
// Jeder Decoder läuft in einem eigenen Thread und liefert QImage im Format ARGB32_Premultiplied.
//
//   --mode t01                     Fenster zeigen, nach dem ersten Bild 1 s warten, beenden.
//                                  Gibt "FIRST_FRAME_NS <CLOCK_MONOTONIC ns>" aus.
//   --mode t02 --media DIR --stats DATEI --duration S
//                                  Overlay 1920x1080 im Vollbild, Ziel 30 Bilder/s,
//                                  schreibt Zeitstempel als JSON und beendet nach S Sekunden.
//
// Quellen:
//   QMetaObject::invokeMethod (Funktor, QueuedConnection):
//                              https://doc.qt.io/qt-6.8/qmetaobject.html#invokeMethod-5
//   steady_clock = CLOCK_MONOTONIC unter Linux (libstdc++):
//                              https://en.cppreference.com/w/cpp/chrono/steady_clock

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QWidget>

#include "decoder.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

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

// ---------- T-01 ----------

class StartWindow : public QLabel
{
public:
    StartWindow() : QLabel(QStringLiteral("T-01 Variante A mit libvpx"))
    {
        setAlignment(Qt::AlignCenter);
        resize(800, 450);
        setWindowTitle(QStringLiteral("P02 M1 T-01 A-libvpx"));
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        QLabel::paintEvent(e);
        if (!m_done) {
            m_done = true;
            // Nach dem Zurückkehren aus paintEvent wird der Backing-Store geleert.
            QTimer::singleShot(0, [] {
                std::printf("FIRST_FRAME_NS %lld\n", static_cast<long long>(monotonicNs()));
                std::fflush(stdout);
                QTimer::singleShot(1000, qApp, &QApplication::quit);
            });
        }
    }

private:
    bool m_done = false;
};

// ---------- T-02 ----------

class Overlay : public QWidget
{
public:
    Overlay(const QString &media, const QString &statsPath, int durationS)
        : m_statsPath(statsPath)
    {
        setWindowTitle(QStringLiteral("P02 M1 A-libvpx"));
        setFixedSize(1920, 1080);
        setAttribute(Qt::WA_OpaquePaintEvent);

        // Schachbrett 1920x1080 einmal vorberechnen
        m_checker = QPixmap(1920, 1080);
        QPainter cp(&m_checker);
        for (int y = 0; y < 1080; y += 16)
            for (int x = 0; x < 1920; x += 16)
                cp.fillRect(x, y, 16, 16, ((x / 16 + y / 16) % 2) ? QColor(204, 204, 204) : QColor(255, 255, 255));
        cp.end();

        for (int i = 1; i <= 5; ++i)
            m_images.append(QImage(media + QStringLiteral("/bild%1.png").arg(i)));

        m_video = makeDecoder(media + QStringLiteral("/video.webm"), QString(), &m_videoFrame, &m_videoTimes, QStringLiteral("video"));
        m_alpha = makeDecoder(media + QStringLiteral("/alpha.webm"), QStringLiteral("libvpx-vp9"), &m_alphaFrame, &m_alphaTimes, QStringLiteral("alpha"));

        m_t0 = monotonicNs();
        m_next = m_t0;
        scheduleNext();
        QTimer::singleShot(durationS * 1000, this, [this] { finish(); });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        m_frameTimes.append(epochMs());
        const double t = (monotonicNs() - m_t0) / 1e9;

        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawPixmap(0, 0, m_checker);

        // 5 Bilder unten
        for (int i = 0; i < m_images.size(); ++i)
            p.drawImage(QPoint(60 + 300 * i, 780), m_images[i]);
        // Video und WebM mit Alpha
        if (!m_videoFrame.isNull())
            p.drawImage(QRect(60, 60, 960, 540), m_videoFrame);
        if (!m_alphaFrame.isNull())
            p.drawImage(QRect(1340, 480, 512, 512), m_alphaFrame);

        // 3 Texte: Schweben, Atmen, beides
        QFont f(QStringLiteral("DejaVu Sans"));
        f.setPixelSize(64);
        f.setBold(true);
        const double floatY = 12.0 * std::sin(2 * M_PI * t / 3.0);
        const double breath = 1.0 + 0.06 * std::sin(2 * M_PI * t / 2.5);
        drawText(p, f, QStringLiteral("Schweben"), QPointF(1100, 140 + floatY), 1.0);
        drawText(p, f, QStringLiteral("Atmen"), QPointF(1100, 280), breath);
        drawText(p, f, QStringLiteral("DoNs Studio"), QPointF(1100, 420 + floatY), breath);
    }

private:
    // Bilder kommen im Decoder-Thread an und werden über die Ereignisschleife übergeben.
    std::unique_ptr<VideoDecoder> makeDecoder(const QString &file, const QString &decoder, QImage *target,
                                              QList<double> *times, const QString &key)
    {
        auto onFrame = [this, target, times](const QImage &img) {
            QMetaObject::invokeMethod(this, [target, times, img] {
                times->append(epochMs());
                *target = img;
            }, Qt::QueuedConnection);
        };
        auto onInfo = [this, key](const VideoDecoder::Info &i) {
            QMetaObject::invokeMethod(this, [this, key, i] {
                m_info.insert(key + QStringLiteral("_decoder"), i.decoder);
                m_info.insert(key + QStringLiteral("_pixel_format"), i.pixFmt);
                m_info.insert(key + QStringLiteral("_first_frame_alpha_mean"), i.firstAlphaMean);
                if (!i.error.isEmpty())
                    m_info.insert(key + QStringLiteral("_error"), i.error);
            }, Qt::QueuedConnection);
        };
        return std::make_unique<VideoDecoder>(file, decoder, onFrame, onInfo);
    }

    static void drawText(QPainter &p, const QFont &f, const QString &s, QPointF pos, double scale)
    {
        p.save();
        p.translate(pos);
        p.scale(scale, scale);
        QPainterPath path;
        path.addText(0, 0, f, s);
        p.setPen(QPen(QColor(0, 0, 0, 200), 6));
        p.drawPath(path);
        p.fillPath(path, QColor(255, 255, 255));
        p.restore();
    }

    // Feste Bildzeitpunkte alle 1/30 s, ohne Aufsummieren von Fehlern
    void scheduleNext()
    {
        m_next += 1'000'000'000LL / 30;
        const qint64 waitMs = std::max<qint64>(0, (m_next - monotonicNs()) / 1'000'000);
        QTimer::singleShot(static_cast<int>(waitMs), Qt::PreciseTimer, this, [this] {
            update();
            scheduleNext();
        });
    }

    void finish()
    {
        QJsonObject o;
        // Decoder-Threads beenden, bevor das Fenster abgebaut wird
        m_video.reset();
        m_alpha.reset();
        o.insert(QStringLiteral("variant"), QStringLiteral("A-libvpx"));
        o.insert(QStringLiteral("frames"), QJsonArray::fromVariantList(toVariant(m_frameTimes)));
        o.insert(QStringLiteral("video_frames"), QJsonArray::fromVariantList(toVariant(m_videoTimes)));
        o.insert(QStringLiteral("alpha_frames"), QJsonArray::fromVariantList(toVariant(m_alphaTimes)));
        o.insert(QStringLiteral("info"), m_info);
        QFile out(m_statsPath);
        if (out.open(QIODevice::WriteOnly))
            out.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        qApp->quit();
    }

    static QVariantList toVariant(const QList<double> &l)
    {
        QVariantList v;
        v.reserve(l.size());
        for (double d : l)
            v.append(d);
        return v;
    }

    QString m_statsPath;
    QPixmap m_checker;
    QList<QImage> m_images;
    QImage m_videoFrame, m_alphaFrame;
    QList<double> m_frameTimes, m_videoTimes, m_alphaTimes;
    QJsonObject m_info;
    qint64 m_t0 = 0, m_next = 0;
    // Zuletzt deklariert, damit die Decoder-Threads zuerst beendet werden.
    std::unique_ptr<VideoDecoder> m_video, m_alpha;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addOptions({
        {QStringLiteral("mode"), QStringLiteral("t01 oder t02"), QStringLiteral("mode"), QStringLiteral("t01")},
        {QStringLiteral("media"), QStringLiteral("Ordner mit Testdateien"), QStringLiteral("dir")},
        {QStringLiteral("stats"), QStringLiteral("Ausgabedatei JSON"), QStringLiteral("file")},
        {QStringLiteral("duration"), QStringLiteral("Laufzeit in s"), QStringLiteral("s"), QStringLiteral("70")},
    });
    parser.process(app);

    if (parser.value(QStringLiteral("mode")) == QStringLiteral("t02")) {
        Overlay w(parser.value(QStringLiteral("media")), parser.value(QStringLiteral("stats")),
                  parser.value(QStringLiteral("duration")).toInt());
        w.showFullScreen();
        return app.exec();
    }
    StartWindow w;
    w.show();
    return app.exec();
}
