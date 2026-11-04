#include "mediapreview.h"

#include "imageorientation.h"
#include "videoplayer.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QGlyphRun>
#include <QImageReader>
#include <QPainter>
#include <QRawFont>
#include <QThread>
#include <QTransform>

namespace dss {

namespace {

QImage oriented(QImage image, int orientation)
{
    // Wie SourceRenderer: zuerst waagrecht spiegeln, dann drehen (imageorientation.h)
    const OrientationSteps steps = orientationSteps(orientation);
    if (steps.mirror)
        image = image.mirrored(true, false);
    if (steps.rotation != 0)
        image = image.transformed(QTransform().rotate(steps.rotation));
    return image;
}

QImage fitted(const QImage &image, const QSize &box)
{
    QImage out(box, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (image.isNull())
        return out;
    QSize size = image.size();
    if (size.width() > box.width() || size.height() > box.height())
        size = size.scaled(box, Qt::KeepAspectRatio);
    size = size.expandedTo(QSize(1, 1));
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(QRect(QPoint((box.width() - size.width()) / 2, (box.height() - size.height()) / 2), size), image);
    return out;
}

QImage imagePreview(const QString &path, const QSize &box, QString *error)
{
    QImageReader reader(path);
    reader.setAutoTransform(false); // Ausrichtung selbst, wie auf der Leinwand (P09b)
    QImage image = reader.read();
    if (image.isNull()) {
        *error = QStringLiteral("Bild nicht lesbar: %1 (%2)").arg(path, reader.errorString());
        return {};
    }
    return fitted(oriented(image, imageFileOrientation(path)), box);
}

QImage videoPreview(const QString &path, const QSize &box, QString *error, int timeoutMs)
{
    // Weder wiederholen noch leer am Ende: das erste Bild bleibt, auch bei sehr kurzen Videos
    VideoPlayer player(path, false, true);
    QDeadlineTimer deadline(timeoutMs);
    std::shared_ptr<const VideoFrame> frame;
    while (!frame && !deadline.hasExpired()) {
        if (player.state() == VideoPlayer::State::Failed)
            break;
        frame = player.currentFrame();
        if (!frame)
            QThread::msleep(5);
    }
    if (!frame) {
        *error = QStringLiteral("Video nicht lesbar: %1").arg(path);
        return {};
    }
    const QImage image(frame->data.data(), frame->width, frame->height, frame->stride,
                       QImage::Format_ARGB32_Premultiplied);
    return fitted(oriented(image.copy(), frame->orientation), box);
}

QImage fontPreview(const QString &path, const QSize &box, const QColor &color, QString *error)
{
    const QString text = QStringLiteral("Aa");
    QRawFont font(path, box.height() * 0.7);
    if (!font.isValid()) {
        *error = QStringLiteral("Schrift nicht lesbar: %1").arg(path);
        return {};
    }
    const QList<quint32> glyphs = font.glyphIndexesForString(text);
    QList<QPointF> advances = font.advancesForGlyphIndexes(glyphs);
    QList<QPointF> positions;
    qreal x = 0;
    for (const QPointF &a : advances) {
        positions.append(QPointF(x, 0));
        x += a.x();
    }
    QGlyphRun run;
    run.setRawFont(font);
    run.setGlyphIndexes(glyphs);
    run.setPositions(positions);
    const QRectF bounds = run.boundingRect();
    QImage out(box, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    if (bounds.isEmpty())
        return out; // Schrift ohne Zeichen für «Aa»: leer
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(color);
    // Grösser als box (breite Schrift): verkleinern
    const qreal scale = qMin(1.0, qMin(box.width() / bounds.width(), box.height() / bounds.height()));
    p.translate(box.width() / 2.0, box.height() / 2.0);
    p.scale(scale, scale);
    p.translate(-bounds.center());
    p.drawGlyphRun(QPointF(0, 0), run);
    return out;
}

} // namespace

QImage mediaPreview(const QString &path, MediaKind kind, const QSize &box, const QColor &textColor,
                    QString *error, int timeoutMs)
{
    switch (kind) {
    case MediaKind::Image:
        return imagePreview(path, box, error);
    case MediaKind::Video:
        return videoPreview(path, box, error, timeoutMs);
    case MediaKind::Font:
        return fontPreview(path, box, textColor, error);
    }
    return {};
}

MediaPreviewLoader::MediaPreviewLoader(const QSize &box, const QColor &textColor, QObject *parent)
    : QObject(parent), m_box(box), m_textColor(textColor)
{
    // Höchstens zwei zugleich: Videos dekodieren in eigenen Threads, die Oberfläche bleibt frei.
    m_pool.setMaxThreadCount(2);
}

MediaPreviewLoader::~MediaPreviewLoader()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void MediaPreviewLoader::setDataDir(const QString &dir)
{
    if (dir == m_dataDir)
        return;
    m_dataDir = dir;
    ++m_generation;
    m_cache.clear();
    m_loading.clear();
    m_pool.clear();
}

QImage MediaPreviewLoader::preview(const QString &reference)
{
    if (const auto it = m_cache.constFind(reference); it != m_cache.cend())
        return *it;
    const std::optional<MediaKind> kind = mediaKindOf(reference);
    if (m_dataDir.isEmpty() || !kind || m_loading.contains(reference))
        return {};
    m_loading.insert(reference);
    const QString path = QDir(m_dataDir).filePath(reference);
    const quint64 generation = m_generation;
    const QSize box = m_box;
    const QColor color = m_textColor;
    m_pool.start([this, path, reference, generation, box, color, kind] {
        QString error;
        const QImage image = mediaPreview(path, *kind, box, color, &error);
        if (image.isNull())
            qWarning("Vorschau: %s", qPrintable(error));
        // Zurück in den Thread der Oberfläche; der Destruktor wartet auf den Pool.
        QMetaObject::invokeMethod(this, [this, reference, generation, image] {
            if (generation != m_generation)
                return;
            m_loading.remove(reference);
            // Auch eine leere Vorschau merken: nicht bei jedem Füllen neu versuchen
            m_cache.insert(reference, image);
            emit previewReady(reference);
        }, Qt::QueuedConnection);
    });
    return {};
}

} // namespace dss
