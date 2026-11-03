#include "imageorientation.h"

#include <QFile>

namespace dss {

namespace {

// Höchstens so viele Segmente bzw. Chunks vor den Bilddaten ansehen (Schutz vor kaputten Dateien).
constexpr int kMaxBlocks = 64;
// Grösste TIFF-Daten, die gelesen werden (ein APP1-Segment ist höchstens 64 KiB gross).
constexpr qint64 kMaxTiffBytes = 1024 * 1024;

struct ByteOrder
{
    bool little = false;
    quint16 u16(QByteArrayView d, qsizetype at) const
    {
        const auto b0 = quint16(quint8(d[at]));
        const auto b1 = quint16(quint8(d[at + 1]));
        return little ? quint16(b0 | (b1 << 8)) : quint16((b0 << 8) | b1);
    }
    quint32 u32(QByteArrayView d, qsizetype at) const
    {
        const quint32 hi = u16(d, little ? at + 2 : at);
        const quint32 lo = u16(d, little ? at : at + 2);
        return (hi << 16) | lo;
    }
};

quint32 bigEndian32(const QByteArray &d)
{
    return (quint32(quint8(d[0])) << 24) | (quint32(quint8(d[1])) << 16)
           | (quint32(quint8(d[2])) << 8) | quint32(quint8(d[3]));
}

// Ausrichtung und Lage des Werts (ab Beginn der TIFF-Daten); -1 ohne gültige Angabe.
int tiffOrientationAt(QByteArrayView tiff, qint64 *valueOffset, bool *little);

OrientationLocation fromTiff(QByteArrayView tiff, qint64 tiffStart)
{
    OrientationLocation l;
    qint64 offset = -1;
    l.orientation = tiffOrientationAt(tiff, &offset, &l.littleEndian);
    if (offset >= 0)
        l.valueOffset = tiffStart + offset;
    return l;
}

OrientationLocation jpegOrientation(QFile *file)
{
    // Segmente: FF xx, dann Länge (2 Byte, mit sich selbst). Bis SOS (FF DA) bzw. EOI (FF D9).
    for (int block = 0; block < kMaxBlocks; ++block) {
        const QByteArray marker = file->read(2);
        if (marker.size() != 2 || quint8(marker[0]) != 0xFF)
            return {};
        const quint8 type = quint8(marker[1]);
        if (type == 0xDA || type == 0xD9)
            return {};
        if (type == 0xFF) { // Füllbyte, nächstes Byte ist der Marker
            file->seek(file->pos() - 1);
            continue;
        }
        const QByteArray lengthBytes = file->read(2);
        if (lengthBytes.size() != 2)
            return {};
        const int length = (quint8(lengthBytes[0]) << 8) | quint8(lengthBytes[1]);
        if (length < 2)
            return {};
        if (type == 0xE1) {
            const qint64 start = file->pos();
            const QByteArray data = file->read(length - 2);
            static const QByteArray exifId("Exif\0\0", 6);
            if (data.startsWith(exifId))
                return fromTiff(QByteArrayView(data).sliced(exifId.size()), start + exifId.size());
            continue; // anderes APP1 (zum Beispiel XMP)
        }
        if (!file->seek(file->pos() + length - 2))
            return {};
    }
    return {};
}

OrientationLocation pngOrientation(QFile *file)
{
    // Chunks: Länge (4 Byte, Big Endian), Typ (4), Daten, CRC (4). eXIf gilt nur vor IDAT.
    for (int block = 0; block < kMaxBlocks; ++block) {
        const qint64 chunk = file->pos();
        const QByteArray head = file->read(8);
        if (head.size() != 8)
            return {};
        const quint32 length = bigEndian32(head);
        const QByteArray type = head.sliced(4);
        if (type == "IDAT" || type == "IEND")
            return {};
        if (type == "eXIf") {
            if (length > kMaxTiffBytes)
                return {};
            OrientationLocation l = fromTiff(file->read(length), chunk + 8);
            l.pngChunk = chunk;
            l.pngChunkLength = length;
            return l;
        }
        if (!file->seek(file->pos() + qint64(length) + 4))
            return {};
    }
    return {};
}

int tiffOrientationAt(QByteArrayView tiff, qint64 *valueOffset, bool *little)
{
    // Kopf: «II» oder «MM», 42, Lage des ersten IFD. IFD: Anzahl, je 12 Byte (Tag, Typ, Anzahl,
    // Wert); ein SHORT steht links im Wertfeld.
    if (tiff.size() < 8)
        return kOrientationNormal;
    ByteOrder order;
    if (tiff.startsWith("II"))
        order.little = true;
    else if (!tiff.startsWith("MM"))
        return kOrientationNormal;
    if (order.u16(tiff, 2) != 42)
        return kOrientationNormal;
    const quint32 ifd = order.u32(tiff, 4);
    if (ifd < 8 || qint64(ifd) + 2 > tiff.size())
        return kOrientationNormal;
    const int count = order.u16(tiff, ifd);
    for (int i = 0; i < count; ++i) {
        const qint64 entry = qint64(ifd) + 2 + qint64(i) * 12;
        if (entry + 12 > tiff.size())
            return kOrientationNormal;
        if (order.u16(tiff, entry) != 0x0112)
            continue;
        constexpr quint16 kTypeShort = 3;
        if (order.u16(tiff, entry + 2) != kTypeShort || order.u32(tiff, entry + 4) != 1)
            return kOrientationNormal;
        const int value = order.u16(tiff, entry + 8);
        if (value < kOrientationMin || value > kOrientationMax)
            return kOrientationNormal;
        *valueOffset = entry + 8;
        *little = order.little;
        return value;
    }
    return kOrientationNormal;
}

} // namespace

int tiffOrientation(QByteArrayView tiff)
{
    qint64 offset = -1;
    bool little = false;
    return tiffOrientationAt(tiff, &offset, &little);
}

OrientationLocation imageOrientationLocation(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray start = file.peek(8);
    if (start.startsWith("\xFF\xD8")) {
        file.read(2);
        return jpegOrientation(&file);
    }
    if (start == QByteArray("\x89PNG\r\n\x1A\n", 8)) {
        file.read(8);
        return pngOrientation(&file);
    }
    return {};
}

int imageFileOrientation(const QString &path)
{
    return imageOrientationLocation(path).orientation;
}

QSize orientedSize(QSize stored, int orientation)
{
    return orientation >= 5 && orientation <= kOrientationMax ? stored.transposed() : stored;
}

QPointF orientedPoint(QPointF p, QSize storedSize, int orientation)
{
    const double w = storedSize.width();
    const double h = storedSize.height();
    switch (orientation) {
    case 2:
        return {w - p.x(), p.y()};
    case 3:
        return {w - p.x(), h - p.y()};
    case 4:
        return {p.x(), h - p.y()};
    case 5:
        return {p.y(), p.x()};
    case 6:
        return {h - p.y(), p.x()};
    case 7:
        return {h - p.y(), w - p.x()};
    case 8:
        return {p.y(), w - p.x()};
    default:
        return p;
    }
}

OrientationSteps orientationSteps(int orientation)
{
    switch (orientation) {
    case 2:
        return {true, 0};
    case 3:
        return {false, 180};
    case 4:
        return {true, 180};
    case 5:
        return {true, 270};
    case 6:
        return {false, 90};
    case 7:
        return {true, 90};
    case 8:
        return {false, 270};
    default:
        return {false, 0};
    }
}

} // namespace dss
