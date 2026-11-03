#include "sourcerenderer.h"

#include "imageorientation.h"
#include "videoplayer.h"
#include "windowedit.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImageReader>
#include <QPainter>
#include <QRawFont>
#include <QTransform>

// Quellen:
//   QPainter::translate/rotate/scale (positiv = im Uhrzeigersinn): https://doc.qt.io/qt-6.8/qpainter.html#rotate
//   QPainter::drawImage auf ein Rechteck (gestreckt), SmoothPixmapTransform:
//     https://doc.qt.io/qt-6.8/qpainter.html#drawImage, https://doc.qt.io/qt-6.8/qpainter.html#RenderHint-enum
//   QImage über fremdem Speicher ohne Kopie: https://doc.qt.io/qt-6.8/qimage.html#QImage-5
//   QFontDatabase::addApplicationFont: https://doc.qt.io/qt-6.8/qfontdatabase.html#addApplicationFont
//   QFont::setVariableAxis (seit Qt 6.7): https://doc.qt.io/qt-6.8/qfont.html#setVariableAxis
//   QRawFont::fromFont, fontTable: https://doc.qt.io/qt-6.8/qrawfont.html
//   QThreadPool::start(std::function): https://doc.qt.io/qt-6.8/qthreadpool.html#start-1
//   QTimer::PreciseTimer: https://doc.qt.io/qt-6.8/qt.html#TimerType-enum
//   QImageReader::setAutoTransform: https://doc.qt.io/qt-6.8/qimagereader.html#setAutoTransform
//   QImage::mirrored, QImage::transformed: https://doc.qt.io/qt-6.8/qimage.html#mirrored

namespace {

// 30 Bilder pro Sekunde, solange ein Video läuft (Auftrag P09). Genau 1/30 s, nicht 33 ms:
// sonst läuft der Takt gegen die 30 Bilder/s der Videos und zeigt ab und zu ein Bild doppelt.
constexpr std::chrono::nanoseconds kFrameInterval(1'000'000'000 / 30);

bool playing(const dss::VideoPlayer &player)
{
    const dss::VideoPlayer::State s = player.state();
    return s == dss::VideoPlayer::State::Starting || s == dss::VideoPlayer::State::Playing;
}

} // namespace

SourceRenderer::SourceRenderer(QObject *parent) : QObject(parent)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &SourceRenderer::onTick);
    m_loader.setMaxThreadCount(2);
}

SourceRenderer::~SourceRenderer()
{
    // Erst die Lader fertig werden lassen; ihre Antworten an dieses Objekt verwirft Qt mit ihm.
    m_loader.waitForDone();
    m_videos.clear(); // hält die Threads der Videos an
}

void SourceRenderer::setDataDir(const QString &dir)
{
    if (dir == m_dataDir)
        return;
    m_dataDir = dir;
    m_images.clear();
    m_videos.clear();
    m_fonts.clear();
    m_overlayId = QUuid();
}

QString SourceRenderer::mediaPath(const QString &reference) const
{
    return m_dataDir + QLatin1Char('/') + reference;
}

void SourceRenderer::setOverlay(const dss::Overlay *overlay)
{
    const QUuid id = overlay ? overlay->id : QUuid();
    if (id != m_overlayId) {
        m_videos.clear();
        m_overlayId = id;
    }
    if (!overlay || m_dataDir.isEmpty()) {
        m_videos.clear();
        m_timer.stop();
        emit changed();
        return;
    }

    QSet<QUuid> keepVideos;
    QSet<QString> keepImages;
    for (const dss::Source &s : overlay->sources) {
        if (s.kind == dss::SourceKind::Image) {
            keepImages.insert(s.file);
            if (!m_images.contains(s.file))
                requestImage(s.file);
        } else if (s.kind == dss::SourceKind::Video && s.visible) {
            keepVideos.insert(s.id);
            auto it = m_videos.find(s.id);
            if (it != m_videos.end() && it->second.file == s.file && it->second.loop == s.loop
                && it->second.holdAtEnd == s.holdAtEnd)
                continue;
            VideoEntry entry;
            entry.file = s.file;
            entry.loop = s.loop;
            entry.holdAtEnd = s.holdAtEnd;
            if (it != m_videos.end())
                m_videos.erase(it); // alter Thread hält an, bevor der neue beginnt
            entry.player = std::make_unique<dss::VideoPlayer>(mediaPath(s.file), s.loop, s.holdAtEnd);
            m_videos.emplace(s.id, std::move(entry));
        }
    }
    for (auto it = m_videos.begin(); it != m_videos.end();) {
        if (keepVideos.contains(it->first))
            ++it;
        else
            it = m_videos.erase(it);
    }
    for (auto it = m_images.begin(); it != m_images.end();) {
        if (keepImages.contains(it.key()))
            ++it;
        else
            it = m_images.erase(it);
    }
    if (!m_videos.empty() && !m_timer.isActive())
        startTicks();
    emit changed();
}

void SourceRenderer::startTicks()
{
    m_tickStart = std::chrono::steady_clock::now();
    m_tick = 0;
    scheduleNextTick();
}

void SourceRenderer::scheduleNextTick()
{
    const auto next = m_tickStart + (m_tick + 1) * kFrameInterval;
    const auto wait = std::chrono::ceil<std::chrono::milliseconds>(next - std::chrono::steady_clock::now());
    m_timer.start(std::max<qint64>(0, wait.count()));
}

void SourceRenderer::requestImage(const QString &reference)
{
    m_images.insert(reference, ImageEntry());
    const QString path = mediaPath(reference);
    m_loader.start([this, path, reference] {
        // EXIF-Ausrichtung selbst anwenden (P09b, Entscheid 7): Qt 6.8 dreht nicht von sich aus
        // und liest eXIf in PNG nicht (geprüft 2026-11-03, P09b-Bericht).
        QImageReader reader(path);
        reader.setAutoTransform(false);
        QImage image = reader.read();
        if (image.isNull()) {
            qWarning("Bild nicht lesbar: %s", qPrintable(path));
        } else {
            const dss::OrientationSteps steps =
                dss::orientationSteps(dss::imageFileOrientation(path));
            if (steps.mirror)
                image = image.mirrored(true, false);
            if (steps.rotation != 0)
                image = image.transformed(QTransform().rotate(steps.rotation));
            image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        // Zurück in den Thread der Oberfläche. Der Destruktor wartet auf den Pool, this lebt also
        // noch; wird es danach gelöscht, verwirft Qt die Nachricht mit ihm.
        // https://doc.qt.io/qt-6.8/qmetaobject.html#invokeMethod-5
        QMetaObject::invokeMethod(this, [this, reference, image] {
            auto it = m_images.find(reference);
            if (it == m_images.end())
                return; // inzwischen nicht mehr gebraucht
            it->image = image;
            it->loading = false;
            emit changed();
        }, Qt::QueuedConnection);
    });
}

void SourceRenderer::onTick()
{
    ++m_tick;
    const auto now = std::chrono::steady_clock::now();
    // War die Oberfläche lange beschäftigt, nicht nachholen, sondern ab jetzt weiterzählen.
    if (now - (m_tickStart + m_tick * kFrameInterval) > 4 * kFrameInterval) {
        m_tickStart = now;
        m_tick = 0;
    }
    // Bild jedes Videos für den geplanten Zeitpunkt dieses Takts
    const auto sampleTime = m_tickStart + m_tick * kFrameInterval;
    bool any = false;
    for (auto &[id, entry] : m_videos) {
        entry.frame = entry.player->frameAt(sampleTime);
        any = any || playing(*entry.player);
    }
    // Noch ein Bild nach dem Ende, damit «leer» bzw. das letzte Bild sicher gezeichnet ist.
    if (any)
        scheduleNextTick();
    emit changed();
}

SourceRenderer::FontEntry *SourceRenderer::font(const QString &reference) const
{
    auto it = m_fonts.find(reference);
    if (it != m_fonts.end())
        return it->family.isEmpty() ? nullptr : &*it;
    FontEntry entry;
    const QString path = mediaPath(reference);
    const int id = QFontDatabase::addApplicationFont(path);
    const QStringList families = id < 0 ? QStringList() : QFontDatabase::applicationFontFamilies(id);
    if (families.isEmpty()) {
        qWarning("Schrift nicht lesbar: %s", qPrintable(path));
    } else {
        entry.family = families.first();
        const QRawFont raw(path, 12);
        entry.axes = dss::parseFvarAxes(raw.fontTable("fvar"));
        entry.head = raw.fontTable("head");
    }
    it = m_fonts.insert(reference, entry);
    return it->family.isEmpty() ? nullptr : &*it;
}

void SourceRenderer::paint(QPainter *painter, const dss::Overlay &overlay,
                           const dss::CanvasFit &fit) const
{
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->translate(fit.origin);
    painter->scale(fit.scale, fit.scale);
    for (int index : dss::drawnSources(overlay)) {
        const dss::Source &s = overlay.sources.at(index);
        painter->save();
        // Drehung um die Mitte, positiv im Uhrzeigersinn (Entscheid 8); gespiegelt wird um die
        // Mitte vor dem Drehen (P09b, Entscheid 4, dss::contentToSource).
        const QPointF center = dss::sourceCenter(s);
        const QPointF flip = dss::flipScale(s);
        painter->translate(center);
        painter->rotate(dss::rotationDegrees(s.rotation));
        painter->scale(flip.x(), flip.y());
        painter->translate(-s.width / 2.0, -s.height / 2.0);
        switch (s.kind) {
        case dss::SourceKind::Image: {
            const auto it = m_images.constFind(s.file);
            if (it != m_images.constEnd() && !it->image.isNull())
                drawImage(painter, it->image, s);
            break;
        }
        case dss::SourceKind::Video: {
            const auto it = m_videos.find(s.id);
            if (it == m_videos.end())
                break;
            // Bild des letzten Takts; frame hält den Puffer, solange das QImage darüber gezeichnet wird.
            const std::shared_ptr<const dss::VideoFrame> frame = it->second.frame;
            if (frame) {
                const QImage image(frame->data.data(), frame->width, frame->height, frame->stride,
                                   QImage::Format_ARGB32_Premultiplied);
                drawImage(painter, image, s);
            }
            break;
        }
        case dss::SourceKind::Text:
            drawText(painter, s);
            break;
        }
        painter->restore();
    }
    painter->restore();
}

void SourceRenderer::drawImage(QPainter *painter, const QImage &image, const dss::Source &source) const
{
    // Bild und Video füllen den Kasten, gestreckt (Entscheid 6).
    painter->drawImage(QRectF(0, 0, source.width, source.height), image);
}

void SourceRenderer::drawText(QPainter *painter, const dss::Source &source) const
{
    const dss::TextProperties &t = source.text;
    FontEntry *entry = font(t.fontFile);
    if (!entry)
        return;
    QFont f(entry->family);
    f.setPixelSize(t.fontSize);
    f.setWeight(static_cast<QFont::Weight>(t.weight));
    f.setItalic(t.italic);
    // Ohne Hinting, damit die Formen beim Einpassen nur skaliert werden.
    f.setHintingPreference(QFont::PreferNoHinting);
    double axis = 0;
    if (dss::weightAxisValue(entry->axes, t.weight, &axis))
        f.setVariableAxis(QFont::Tag("wght"), static_cast<float>(axis));
    if (!entry->checked) {
        entry->checked = true;
        if (QRawFont::fromFont(f).fontTable("head") != entry->head)
            qWarning("Schrift %s: Qt wählt für «%s» eine andere Datei derselben Familie",
                     qPrintable(t.fontFile), qPrintable(entry->family));
    }
    int r = 255, g = 255, b = 255, a = 255;
    dss::colorParts(t.color, &r, &g, &b, &a);

    // Zeilen nur an den Zeilenwechseln des Inhalts, oben beginnend; was über den Kasten
    // hinausgeht, wird abgeschnitten (Annahme P09, [OFFEN]).
    painter->setClipRect(QRectF(0, 0, source.width, source.height), Qt::IntersectClip);
    painter->setFont(f);
    painter->setPen(QColor(r, g, b, a));
    const QFontMetricsF metrics(f);
    double baseline = metrics.ascent();
    for (const QString &line : dss::textLines(t.content)) {
        const double x = dss::alignedLineX(t.align, source.width, metrics.horizontalAdvance(line));
        painter->drawText(QPointF(x, baseline), line);
        baseline += metrics.lineSpacing();
    }
}

QList<SourceRenderer::VideoInfo> SourceRenderer::videoInfo() const
{
    QList<VideoInfo> result;
    for (const auto &[id, entry] : m_videos) {
        const dss::VideoPlayer::Statistics st = entry.player->statistics();
        VideoInfo info;
        info.sourceId = id;
        info.decoderName = entry.player->decoderName();
        info.firstAlphaMean = st.firstAlphaMean;
        info.decoded = st.decoded;
        info.late = st.late;
        info.loops = st.loops;
        info.dropped = st.dropped;
        info.hasFrame = entry.frame != nullptr;
        info.state = static_cast<int>(entry.player->state());
        info.serial = entry.player->frameSerial();
        result.append(info);
    }
    return result;
}

bool SourceRenderer::imagesLoaded() const
{
    for (const ImageEntry &e : m_images) {
        if (e.loading)
            return false;
    }
    return true;
}
