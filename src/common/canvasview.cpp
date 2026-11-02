#include "canvasview.h"

#include <algorithm>
#include <cmath>

namespace dss {

CanvasFit fitCanvas(QSize canvas, QSize area, int margin)
{
    CanvasFit fit;
    if (canvas.isEmpty() || area.isEmpty()) {
        fit.scale = 0.0;
        return fit;
    }
    if (area.width() <= 2 * margin || area.height() <= 2 * margin)
        margin = 0;
    const double sx = double(area.width() - 2 * margin) / canvas.width();
    const double sy = double(area.height() - 2 * margin) / canvas.height();
    fit.scale = std::min(sx, sy);
    fit.origin = QPointF((area.width() - canvas.width() * fit.scale) / 2.0,
                         (area.height() - canvas.height() * fit.scale) / 2.0);
    return fit;
}

bool canvasPixelAt(const CanvasFit &fit, QSize canvas, QPointF areaPoint, QPoint *pixel)
{
    if (fit.scale <= 0.0)
        return false;
    const double x = std::floor((areaPoint.x() - fit.origin.x()) / fit.scale);
    const double y = std::floor((areaPoint.y() - fit.origin.y()) / fit.scale);
    if (x < 0 || y < 0 || x >= canvas.width() || y >= canvas.height())
        return false;
    *pixel = QPoint(int(x), int(y));
    return true;
}

QString pointerPositionText(bool inside, QPoint pixel)
{
    if (!inside)
        return QStringLiteral("X: –  Y: –");
    return QStringLiteral("X: %1  Y: %2").arg(pixel.x()).arg(pixel.y());
}

int rulerMajorStep(double scale, double minSpacing)
{
    if (scale <= 0.0)
        return 0;
    for (int power = 1; power <= 100000000; power *= 10) {
        for (int factor : {1, 2, 5}) {
            const int step = factor * power;
            if (step * scale >= minSpacing)
                return step;
        }
    }
    return 1000000000;
}

QList<RulerTick> rulerTicks(double scale, double origin, int length, double minMajorSpacing,
                            double minMinorSpacing)
{
    QList<RulerTick> ticks;
    const int major = rulerMajorStep(scale, minMajorSpacing);
    if (major <= 0 || length < 0)
        return ticks;
    // Unterteilung: in 10 (mit mittlerem Strich), 5 oder 2 Teile, so fein wie möglich.
    int minor = major;
    for (int parts : {10, 5, 2}) {
        if (major % parts == 0 && (major / parts) * scale >= minMinorSpacing) {
            minor = major / parts;
            break;
        }
    }
    for (int value = 0; value <= length; value += minor) {
        RulerTick tick;
        tick.value = value;
        tick.position = origin + value * scale;
        if (value % major == 0)
            tick.kind = RulerTick::Major;
        else if (major / minor == 10 && value % (major / 2) == 0)
            tick.kind = RulerTick::Medium;
        else
            tick.kind = RulerTick::Minor;
        ticks.append(tick);
    }
    return ticks;
}

} // namespace dss
