#include "canvasmeasurement.h"

#include "canvaswidget.h"
#include "mainwindow.h"
#include "sourcerenderer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include <chrono>

namespace {

// Wanduhr in Millisekunden mit Bruchteil, wie time.time() * 1000 in t02.py.
double nowMs()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count() / 1000.0;
}

} // namespace

CanvasMeasurement::CanvasMeasurement(MainWindow *window, const QString &file, int seconds)
    : QObject(window), m_window(window), m_file(file)
{
    connect(window->canvas(), &CanvasWidget::painted, this, &CanvasMeasurement::onPainted);
    QTimer::singleShot(seconds * 1000, this, &CanvasMeasurement::finish);
}

void CanvasMeasurement::onPainted()
{
    if (m_finished)
        return;
    const double t = nowMs();
    m_frames.append(t);
    if (!m_window->renderer())
        return;
    for (const SourceRenderer::VideoInfo &v : m_window->renderer()->videoInfo()) {
        m_decoder[v.sourceId] = v.decoderName;
        auto it = m_lastSerial.find(v.sourceId);
        if (it == m_lastSerial.end() || it->second != v.serial) {
            m_lastSerial[v.sourceId] = v.serial;
            if (v.hasFrame)
                m_videoFrames[v.sourceId].append(t);
        }
    }
}

void CanvasMeasurement::finish()
{
    m_finished = true;
    QJsonObject info;
    QJsonArray videos;
    QJsonArray videoFrames;
    QJsonArray alphaFrames;
    if (m_window->renderer()) {
        for (const SourceRenderer::VideoInfo &v : m_window->renderer()->videoInfo()) {
            videos.append(QJsonObject{{QStringLiteral("quelle"), v.sourceId.toString(QUuid::WithoutBraces)},
                                      {QStringLiteral("decoder"), v.decoderName},
                                      {QStringLiteral("alpha_mittel_erstes_bild"), v.firstAlphaMean},
                                      {QStringLiteral("dekodiert"), double(v.decoded)},
                                      {QStringLiteral("verspaetet_ueber_50ms"), double(v.late)},
                                      {QStringLiteral("verworfen"), double(v.dropped)},
                                      {QStringLiteral("wiederholungen"), double(v.loops)}});
            // Erstes Video ohne Alpha: video_frames, das mit libvpx-vp9: alpha_frames (wie T-02)
            const QJsonArray &frames = m_videoFrames[v.sourceId];
            if (v.decoderName == QLatin1String("libvpx-vp9"))
                alphaFrames = frames;
            else if (videoFrames.isEmpty())
                videoFrames = frames;
        }
    }
    info.insert(QStringLiteral("videos"), videos);
    const QSize canvas = m_window->canvas()->size();
    info.insert(QStringLiteral("leinwand_widget"),
                QStringLiteral("%1x%2").arg(canvas.width()).arg(canvas.height()));
    const QSize windowSize = m_window->size();
    info.insert(QStringLiteral("fenster"),
                QStringLiteral("%1x%2").arg(windowSize.width()).arg(windowSize.height()));
    const QJsonObject root{{QStringLiteral("frames"), m_frames},
                           {QStringLiteral("video_frames"), videoFrames},
                           {QStringLiteral("alpha_frames"), alphaFrames},
                           {QStringLiteral("info"), info}};
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0
        || !f.commit())
        qCritical("Messung nicht geschrieben: %s", qPrintable(m_file));
    else
        qInfo("Messung geschrieben: %s (%lld Bilder der Leinwand)", qPrintable(m_file),
              static_cast<long long>(m_frames.size()));
    m_window->close();
}
