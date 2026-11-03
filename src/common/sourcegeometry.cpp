#include "sourcegeometry.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
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

} // namespace dss
