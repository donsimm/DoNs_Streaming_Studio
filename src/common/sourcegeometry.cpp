#include "sourcegeometry.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace dss {

QPointF sourceCenter(const Source &source)
{
    return QPointF(source.x + source.width / 2.0, source.y + source.height / 2.0);
}

namespace {

// Drehung um angle Grad, positiv im Uhrzeigersinn bei y nach unten.
QPointF rotated(QPointF v, double degrees)
{
    const double r = degrees * std::numbers::pi / 180.0;
    const double c = std::cos(r);
    const double s = std::sin(r);
    return QPointF(v.x() * c - v.y() * s, v.x() * s + v.y() * c);
}

} // namespace

QPointF sourceToOverlay(const Source &source, QPointF local)
{
    const QPointF half(source.width / 2.0, source.height / 2.0);
    return sourceCenter(source) + rotated(local - half, rotationDegrees(source.rotation));
}

QPointF overlayToSource(const Source &source, QPointF overlayPoint)
{
    const QPointF half(source.width / 2.0, source.height / 2.0);
    return rotated(overlayPoint - sourceCenter(source), -rotationDegrees(source.rotation)) + half;
}

QPointF contentToSource(const Source &source, QPointF contentPoint)
{
    return QPointF(source.flipHorizontal ? source.width - contentPoint.x() : contentPoint.x(),
                   source.flipVertical ? source.height - contentPoint.y() : contentPoint.y());
}

QPointF flipScale(const Source &source)
{
    return QPointF(source.flipHorizontal ? -1.0 : 1.0, source.flipVertical ? -1.0 : 1.0);
}

std::array<QPointF, 4> sourceCorners(const Source &source)
{
    const double w = source.width;
    const double h = source.height;
    return {sourceToOverlay(source, QPointF(0, 0)), sourceToOverlay(source, QPointF(w, 0)),
            sourceToOverlay(source, QPointF(w, h)), sourceToOverlay(source, QPointF(0, h))};
}

QPointF overlayToArea(const CanvasFit &fit, QPointF overlayPoint)
{
    return fit.origin + overlayPoint * fit.scale;
}

QPointF areaToOverlay(const CanvasFit &fit, QPointF areaPoint)
{
    return (areaPoint - fit.origin) / fit.scale;
}

bool sourceContains(const Source &source, QPointF overlayPoint)
{
    // Kleine Toleranz gegen Rundung der Drehung, damit Punkte auf dem Rand dazugehören.
    constexpr double kEpsilon = 1e-9;
    const QPointF p = overlayToSource(source, overlayPoint);
    return p.x() >= -kEpsilon && p.y() >= -kEpsilon && p.x() <= source.width + kEpsilon
           && p.y() <= source.height + kEpsilon;
}

int topmostSourceAt(const Overlay &overlay, QPointF overlayPoint)
{
    for (qsizetype i = overlay.sources.size() - 1; i >= 0; --i) {
        const Source &s = overlay.sources.at(i);
        if (s.visible && sourceContains(s, overlayPoint))
            return static_cast<int>(i);
    }
    return -1;
}

QList<int> drawnSources(const Overlay &overlay)
{
    QList<int> result;
    for (qsizetype i = 0; i < overlay.sources.size(); ++i) {
        if (overlay.sources.at(i).visible)
            result.append(static_cast<int>(i));
    }
    return result;
}

QStringList textLines(const QString &content)
{
    return content.split(QLatin1Char('\n'));
}

double alignedLineX(TextAlign align, double boxWidth, double lineWidth)
{
    switch (align) {
    case TextAlign::Center:
        return (boxWidth - lineWidth) / 2.0;
    case TextAlign::Right:
        return boxWidth - lineWidth;
    case TextAlign::Left:
        break;
    }
    return 0.0;
}

QByteArray sfntTable(const QByteArray &fontFile, const char *tag)
{
    // Kopf: sfntVersion u32, numTables u16, searchRange u16, entrySelector u16, rangeShift u16;
    // danach je Tabelle: tableTag Tag(4), checksum u32, offset u32, length u32 (16 Bytes).
    if (fontFile.size() < 12)
        return {};
    const auto *d = reinterpret_cast<const uchar *>(fontFile.constData());
    const quint16 count = qFromBigEndian<quint16>(d + 4);
    if (12 + static_cast<qsizetype>(count) * 16 > fontFile.size())
        return {};
    for (int i = 0; i < count; ++i) {
        const uchar *record = d + 12 + i * 16;
        if (std::memcmp(record, tag, 4) != 0)
            continue;
        const quint32 offset = qFromBigEndian<quint32>(record + 8);
        const quint32 length = qFromBigEndian<quint32>(record + 12);
        if (static_cast<qsizetype>(offset) + length > fontFile.size())
            return {};
        return fontFile.mid(offset, length);
    }
    return {};
}

QList<FontAxis> parseFvarAxes(const QByteArray &fvar)
{
    // Kopf: majorVersion u16, minorVersion u16, axesArrayOffset u16, reserved u16,
    // axisCount u16, axisSize u16, instanceCount u16, instanceSize u16 (16 Bytes).
    // Achse: axisTag Tag(4), minValue Fixed, defaultValue Fixed, maxValue Fixed, flags u16,
    // axisNameID u16 (20 Bytes). Fixed = 16.16 mit Vorzeichen, alles Big Endian.
    QList<FontAxis> axes;
    if (fvar.size() < 16)
        return axes;
    const auto *d = reinterpret_cast<const uchar *>(fvar.constData());
    if (qFromBigEndian<quint16>(d) != 1)
        return axes;
    const quint16 offset = qFromBigEndian<quint16>(d + 4);
    const quint16 count = qFromBigEndian<quint16>(d + 8);
    const quint16 size = qFromBigEndian<quint16>(d + 10);
    if (size < 20 || offset + static_cast<qsizetype>(count) * size > fvar.size())
        return axes;
    const auto fixed = [](const uchar *p) { return qFromBigEndian<qint32>(p) / 65536.0; };
    for (int i = 0; i < count; ++i) {
        const uchar *a = d + offset + i * size;
        FontAxis axis;
        axis.tag = QString::fromLatin1(reinterpret_cast<const char *>(a), 4);
        axis.minimum = fixed(a + 4);
        axis.defaultValue = fixed(a + 8);
        axis.maximum = fixed(a + 12);
        axes.append(axis);
    }
    return axes;
}

bool weightAxisValue(const QList<FontAxis> &axes, int weight, double *value)
{
    for (const FontAxis &a : axes) {
        if (a.tag == QLatin1String("wght")) {
            *value = std::clamp(static_cast<double>(weight), a.minimum, a.maximum);
            return true;
        }
    }
    return false;
}

bool fontLineMetrics(const QByteArray &fontFile, FontLineMetrics *metrics)
{
    if (!sfntTable(fontFile, "EBLC").isEmpty() || !sfntTable(fontFile, "CBLC").isEmpty()
        || !sfntTable(fontFile, "bdat").isEmpty())
        return false;
    const QByteArray head = sfntTable(fontFile, "head");
    const QByteArray hhea = sfntTable(fontFile, "hhea");
    if (head.size() < 54 || hhea.size() < 10)
        return false;
    const auto u16 = [](const QByteArray &t, int at) {
        return int(qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(t.constData()) + at));
    };
    const auto s16 = [](const QByteArray &t, int at) {
        return int(qFromBigEndian<qint16>(reinterpret_cast<const uchar *>(t.constData()) + at));
    };
    FontLineMetrics m;
    m.unitsPerEm = u16(head, 18);
    if (m.unitsPerEm <= 0)
        return false;
    // hhea: ascender s16 @4, descender s16 @6, lineGap s16 @8
    if (s16(hhea, 4) != 0 || s16(hhea, 6) != 0) {
        m.ascent = s16(hhea, 4);
        m.descent = -s16(hhea, 6);
        m.leading = s16(hhea, 8);
    }
    // OS/2 (mindestens 78 Bytes): fsSelection u16 @62, sTypoAscender s16 @68,
    // sTypoDescender s16 @70, sTypoLineGap s16 @72, usWinAscent u16 @74, usWinDescent u16 @76
    const QByteArray os2 = sfntTable(fontFile, "OS/2");
    if (os2.size() >= 78) {
        constexpr int kUseTypoMetrics = 0x80;
        if (u16(os2, 62) & kUseTypoMetrics) {
            if (s16(os2, 68) != 0 || s16(os2, 70) != 0) {
                m.ascent = s16(os2, 68);
                m.descent = -s16(os2, 70);
                m.leading = s16(os2, 72);
            }
        } else if (u16(os2, 74) != 0 || u16(os2, 76) != 0) {
            m.ascent = u16(os2, 74);
            m.descent = u16(os2, 76);
            m.leading = 0;
        }
    }
    *metrics = m;
    return true;
}

double fontMetricPixels(int units, int pixelSize, int unitsPerEm)
{
    // QFixed: val in 1/64. fromReal(units * px) = units * px * 64; Division durch QFixed b:
    // (val * 64 + b.val / 2) / b.val (qfixed_p.h), b.val = unitsPerEm * 64.
    const qint64 val = qint64(units) * pixelSize * 64;
    const qint64 b = qint64(unitsPerEm) * 64;
    return double((val * 64 + b / 2) / b) / 64.0;
}

} // namespace dss
