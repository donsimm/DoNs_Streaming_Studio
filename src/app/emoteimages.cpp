#include "emoteimages.h"

#include <QBuffer>
#include <QImageReader>
#include <QtGlobal>

#include <memory>

namespace dss {

namespace {

void reject(const QString &key, const char *reason)
{
    qWarning("Emotes: Bild %s nicht angezeigt: %s", qPrintable(key), reason);
}

} // namespace

EmoteImages::EmoteImages(qint64 memoryBytes)
{
    cache_.setMaxCost(memoryBytes);
}

bool EmoteImages::webpAvailable()
{
    static const bool available = QImageReader::supportedImageFormats().contains("webp");
    return available;
}

bool EmoteImages::add(const QString &key, const QByteArray &bytes, const EmoteImageLimits &limits)
{
    cache_.remove(key);
    if (bytes.isEmpty()) {
        reject(key, "keine Daten");
        return false;
    }
    if (bytes.size() > limits.maxBytes) {
        reject(key, "zu viele Bytes");
        return false;
    }

    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    // Grösse vor dem Dekodieren prüfen. QImageReader::setAllocationLimit ist statisch und gälte
    // für alle Bilder des Fensters (auch die Medien der Leinwand); die Seitengrenze hier und je
    // Bild unten hält ein Bild unter maxSide × maxSide × 4 Bytes (Emotes 512, GIFs 1024).
    const QSize declared = reader.size();
    if (declared.width() > limits.maxSide || declared.height() > limits.maxSide) {
        reject(key, "Bild zu gross");
        return false;
    }

    const QSize box(limits.height * limits.maxWidthFactor, limits.height);
    auto entry = std::make_unique<Entry>();
    qint64 cost = 0;
    qint64 end = 0;
    while (entry->frames.isEmpty() || reader.canRead()) {
        const QImage source = reader.read();
        if (source.isNull())
            break;
        if (source.width() > limits.maxSide || source.height() > limits.maxSide) {
            reject(key, "Bild zu gross");
            return false;
        }
        if (entry->frames.size() >= limits.maxFrames) {
            reject(key, "zu viele Bilder in der Animation");
            return false;
        }
        int delay = reader.nextImageDelay();
        if (delay <= kEmoteFrameMinMs)
            delay = kEmoteFrameDefaultMs;
        end += delay;
        // Auf die Höhe skaliert, Breite nach Seitenverhältnis, höchstens box.width()
        QImage scaled = source.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                            .convertToFormat(QImage::Format_ARGB32_Premultiplied);
        cost += scaled.sizeInBytes();
        entry->frames.append(std::move(scaled));
        entry->ends.append(end);
    }
    if (entry->frames.isEmpty()) {
        reject(key, "Daten nicht lesbar");
        return false;
    }
    if (cost > cache_.maxCost()) {
        reject(key, "grösser als der Speicher für Bilder");
        return false;
    }
    cache_.insert(key, entry.release(), cost);
    return true;
}

bool EmoteImages::contains(const QString &key) const
{
    return cache_.contains(key);
}

void EmoteImages::clear()
{
    cache_.clear();
}

const QImage *EmoteImages::frame(const QString &key, qint64 elapsedMs, bool animate) const
{
    const Entry *entry = cache_.object(key);
    if (!entry)
        return nullptr;
    if (!animate || entry->frames.size() == 1)
        return &entry->frames.constFirst();
    const qint64 at = qMax<qint64>(0, elapsedMs) % entry->ends.constLast();
    for (qsizetype i = 0; i < entry->ends.size(); ++i) {
        if (at < entry->ends.at(i))
            return &entry->frames.at(i);
    }
    return &entry->frames.constLast();
}

int EmoteImages::frameCount(const QString &key) const
{
    const Entry *entry = cache_.object(key);
    return entry ? int(entry->frames.size()) : 0;
}

qint64 EmoteImages::duration(const QString &key) const
{
    const Entry *entry = cache_.object(key);
    return entry && entry->frames.size() > 1 ? entry->ends.constLast() : 0;
}

bool EmoteImages::hasAnimated() const
{
    const QList<QString> keys = cache_.keys();
    for (const QString &key : keys) {
        if (frameCount(key) > 1)
            return true;
    }
    return false;
}

} // namespace dss
