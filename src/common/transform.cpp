#include "transform.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dss {

namespace {

int rounded(double value)
{
    return static_cast<int>(std::lround(value));
}

// Lage so setzen, dass die linke obere Ecke des umschliessenden Kastens bei (left, top) liegt
// (SetItemTL in OBS).
SourceGeometry withBoxTopLeft(SourceGeometry g, double left, double top)
{
    const BoundingBox b = boundingBox(g);
    const double centerX = left + b.width / 2.0;
    const double centerY = top + b.height / 2.0;
    g.x = rounded(centerX - g.width / 2.0);
    g.y = rounded(centerY - g.height / 2.0);
    return g;
}

SourceGeometry rotatedBy(const SourceGeometry &start, int tenths)
{
    const BoundingBox before = boundingBox(start);
    SourceGeometry g = start;
    g.rotation = ((start.rotation + tenths) % 3600 + 3600) % 3600;
    return withBoxTopLeft(g, before.left, before.top);
}

// Abstand mittig wie add_alignment in OBS: ganzzahlig halbiert (Richtung 0).
int centered(int canvas, int size)
{
    return (canvas - size) / 2;
}

} // namespace

BoundingBox boundingBox(const SourceGeometry &g)
{
    const double angle = g.rotation / 10.0 * std::numbers::pi / 180.0;
    const double c = std::abs(std::cos(angle));
    const double s = std::abs(std::sin(angle));
    // Bei 90°, 180°, 270° genau (cos und sin sind dort nicht genau 0).
    const auto exact = [](double v) { return std::abs(v) < 1e-12 ? 0.0 : v; };
    const double width = g.width * exact(c) + g.height * exact(s);
    const double height = g.width * exact(s) + g.height * exact(c);
    const double centerX = g.x + g.width / 2.0;
    const double centerY = g.y + g.height / 2.0;
    return {centerX - width / 2.0, centerY - height / 2.0, width, height};
}

SourceGeometry transformGeometry(const Source &source, TransformCommand command, QSize canvas,
                                 std::optional<QSize> originalSize)
{
    const SourceGeometry start = geometryOf(source);
    SourceGeometry g = start;
    switch (command) {
    case TransformCommand::Reset:
        // reset_tr: Lage 0/0, Grösse der Datei, Drehung 0, nicht gespiegelt; Text behält die Grösse.
        g = SourceGeometry{0, 0, start.width, start.height, 0, false, false};
        if (originalSize) {
            g.width = originalSize->width();
            g.height = originalSize->height();
        }
        return g;
    case TransformCommand::Rotate90Clockwise:
        return rotatedBy(start, 900);
    case TransformCommand::Rotate90CounterClockwise:
        return rotatedBy(start, -900);
    case TransformCommand::Rotate180:
        return rotatedBy(start, 1800);
    case TransformCommand::FlipHorizontal:
        // Der Kasten bleibt (gespiegelt wird der Inhalt in ihm), also auch der umschliessende.
        g.flipHorizontal = !start.flipHorizontal;
        return g;
    case TransformCommand::FlipVertical:
        g.flipVertical = !start.flipVertical;
        return g;
    case TransformCommand::FitToCanvas: {
        // So gross wie möglich mit dem Verhältnis der Datei (Text: des Kastens), mittig.
        const double w = originalSize ? originalSize->width() : start.width;
        const double h = originalSize ? originalSize->height() : start.height;
        const double canvasAspect = double(canvas.width()) / canvas.height();
        g = SourceGeometry{0, 0, canvas.width(), canvas.height(), 0, false, false};
        if (canvasAspect < w / h)
            g.height = std::max(kSourceMinSize, rounded(canvas.width() * h / w));
        else
            g.width = std::max(kSourceMinSize, rounded(canvas.height() * w / h));
        g.x = centered(canvas.width(), g.width);
        g.y = centered(canvas.height(), g.height);
        return g;
    }
    case TransformCommand::StretchToCanvas:
        return SourceGeometry{0, 0, canvas.width(), canvas.height(), 0, false, false};
    case TransformCommand::CenterOnCanvas:
        // Mitte des umschliessenden Kastens = Mitte der Quelle (gedreht wird um die Mitte),
        // also gilt die Drehung hier nicht; ganzzahlig halbiert wie beim Einpassen.
        g.x = centered(canvas.width(), start.width);
        g.y = centered(canvas.height(), start.height);
        return g;
    case TransformCommand::CenterVertically:
        g.y = centered(canvas.height(), start.height);
        return g;
    case TransformCommand::CenterHorizontally:
        g.x = centered(canvas.width(), start.width);
        return g;
    }
    return g;
}

} // namespace dss
