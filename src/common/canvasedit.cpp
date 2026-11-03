#include "canvasedit.h"

#include "sourcegeometry.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dss {

namespace {

// Seiten eines Griffs: -1 links/oben, +1 rechts/unten, 0 keine.
struct Sides
{
    int x = 0;
    int y = 0;
};

Sides sidesOf(CanvasHandle handle)
{
    switch (handle) {
    case CanvasHandle::TopLeft:
        return {-1, -1};
    case CanvasHandle::Top:
        return {0, -1};
    case CanvasHandle::TopRight:
        return {1, -1};
    case CanvasHandle::Right:
        return {1, 0};
    case CanvasHandle::BottomRight:
        return {1, 1};
    case CanvasHandle::Bottom:
        return {0, 1};
    case CanvasHandle::BottomLeft:
        return {-1, 1};
    case CanvasHandle::Left:
        return {-1, 0};
    case CanvasHandle::None:
    case CanvasHandle::Move:
    case CanvasHandle::Rotate:
        break;
    }
    return {};
}

double distance(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

int normalizedTenths(double degrees)
{
    int tenths = static_cast<int>(std::lround(degrees * 10.0)) % 3600;
    if (tenths < 0)
        tenths += 3600;
    return tenths;
}

} // namespace

bool isCornerHandle(CanvasHandle handle)
{
    const Sides s = sidesOf(handle);
    return s.x != 0 && s.y != 0;
}

bool isEdgeHandle(CanvasHandle handle)
{
    const Sides s = sidesOf(handle);
    return (s.x != 0) != (s.y != 0);
}

QPointF handleLocal(const Source &source, CanvasHandle handle, double scale)
{
    const double w = source.width;
    const double h = source.height;
    if (handle == CanvasHandle::Rotate)
        return QPointF(w / 2.0, -kRotateHandleDistance / scale);
    const Sides s = sidesOf(handle);
    return QPointF((s.x + 1) * w / 2.0, (s.y + 1) * h / 2.0);
}

QPointF handlePosition(const Source &source, CanvasHandle handle, double scale)
{
    return sourceToOverlay(source, handleLocal(source, handle, scale));
}

CanvasHandle handleAt(const Source &source, QPointF overlayPoint, double scale)
{
    if (scale <= 0)
        return CanvasHandle::None;
    const double radius = kHandleHitRadius / scale;
    const auto near = [&](CanvasHandle h) {
        return distance(handlePosition(source, h, scale), overlayPoint) <= radius;
    };
    if (near(CanvasHandle::Rotate))
        return CanvasHandle::Rotate;
    for (CanvasHandle h : {CanvasHandle::TopLeft, CanvasHandle::TopRight, CanvasHandle::BottomRight,
                           CanvasHandle::BottomLeft}) {
        if (near(h))
            return h;
    }
    for (CanvasHandle h : {CanvasHandle::Top, CanvasHandle::Right, CanvasHandle::Bottom,
                           CanvasHandle::Left}) {
        if (near(h))
            return h;
    }
    if (sourceContains(source, overlayPoint))
        return CanvasHandle::Move;
    return CanvasHandle::None;
}

CanvasPress canvasPress(const Overlay &overlay, const QUuid &selectedId, QPointF overlayPoint,
                        double scale)
{
    if (!selectedId.isNull()) {
        const int index = sourceIndex(overlay, selectedId);
        if (index >= 0 && overlay.sources.at(index).visible) {
            const CanvasHandle h = handleAt(overlay.sources.at(index), overlayPoint, scale);
            if (h != CanvasHandle::None)
                return {selectedId, h};
        }
    }
    const int top = topmostSourceAt(overlay, overlayPoint);
    if (top < 0)
        return {};
    return {overlay.sources.at(top).id, CanvasHandle::Move};
}

SourceGeometry geometryOf(const Source &source)
{
    return {source.x, source.y, source.width, source.height, source.rotation};
}

bool applyGeometry(Overlay *overlay, const QUuid &sourceId, const SourceGeometry &geometry)
{
    const int index = sourceIndex(*overlay, sourceId);
    if (index < 0)
        return false;
    Source &s = overlay->sources[index];
    s.x = geometry.x;
    s.y = geometry.y;
    s.width = geometry.width;
    s.height = geometry.height;
    s.rotation = geometry.rotation;
    return true;
}

SourceGeometry moveGeometry(const Source &start, QPointF startPointer, QPointF pointer)
{
    SourceGeometry g = geometryOf(start);
    g.x = start.x + static_cast<int>(std::lround(pointer.x() - startPointer.x()));
    g.y = start.y + static_cast<int>(std::lround(pointer.y() - startPointer.y()));
    return g;
}

SourceGeometry resizeGeometry(const Source &start, CanvasHandle handle, QPointF pointer,
                              bool freeAspect)
{
    SourceGeometry g = geometryOf(start);
    const Sides s = sidesOf(handle);
    if (s.x == 0 && s.y == 0)
        return g;
    const double w = start.width;
    const double h = start.height;
    // Zeiger im Rahmen der Quelle beim Beginn; der Gegengriff liegt auf der anderen Seite.
    const QPointF p = overlayToSource(start, pointer);
    double newW = w;
    double newH = h;
    if (s.x > 0)
        newW = p.x();
    else if (s.x < 0)
        newW = w - p.x();
    if (s.y > 0)
        newH = p.y();
    else if (s.y < 0)
        newH = h - p.y();
    newW = std::max(newW, double(kSourceMinSize));
    newH = std::max(newH, double(kSourceMinSize));
    if (isCornerHandle(handle) && !freeAspect) {
        // Wie ClampAspect in OBS: Ist das neue Verhältnis kleiner, folgt die Breite der Höhe.
        const double aspect = w / h;
        if (newW / newH < aspect)
            newW = newH * aspect;
        else
            newH = newW / aspect;
    }
    g.width = std::max(kSourceMinSize, static_cast<int>(std::lround(newW)));
    g.height = std::max(kSourceMinSize, static_cast<int>(std::lround(newH)));
    // Neuer Kasten im Rahmen des Beginns: Der Gegengriff bleibt, ohne Seite bleibt die Mitte.
    const double x0 = s.x > 0 ? 0.0 : (s.x < 0 ? w - g.width : (w - g.width) / 2.0);
    const double y0 = s.y > 0 ? 0.0 : (s.y < 0 ? h - g.height : (h - g.height) / 2.0);
    const QPointF center = sourceToOverlay(start, QPointF(x0 + g.width / 2.0, y0 + g.height / 2.0));
    g.x = static_cast<int>(std::lround(center.x() - g.width / 2.0));
    g.y = static_cast<int>(std::lround(center.y() - g.height / 2.0));
    return g;
}

SourceGeometry rotateGeometry(const Source &start, QPointF pointer, bool snap)
{
    SourceGeometry g = geometryOf(start);
    const QPointF c = sourceCenter(start);
    if (distance(c, pointer) == 0.0)
        return g;
    double degrees = std::atan2(pointer.y() - c.y(), pointer.x() - c.x()) * 180.0 / std::numbers::pi
                     + 90.0;
    if (snap)
        degrees = std::round(degrees / 15.0) * 15.0;
    g.rotation = normalizedTenths(degrees);
    return g;
}

SourceGeometry nudgeGeometry(const Source &source, int dx, int dy)
{
    SourceGeometry g = geometryOf(source);
    g.x += dx;
    g.y += dy;
    return g;
}

CanvasCursor cursorFor(CanvasHandle handle, int rotation, bool dragging)
{
    switch (handle) {
    case CanvasHandle::None:
        return CanvasCursor::Arrow;
    case CanvasHandle::Move:
        return CanvasCursor::Move;
    case CanvasHandle::Rotate:
        return dragging ? CanvasCursor::ClosedHand : CanvasCursor::OpenHand;
    default:
        break;
    }
    // Richtung des Griffs von der Mitte aus, in Achteln: Kanten 0°/90°, Ecken 45°-Schritte,
    // dazu die Drehung, auf das nächste Achtel gerundet (wie UpdateCursor in OBS).
    const Sides s = sidesOf(handle);
    const double base = std::atan2(double(s.y), double(s.x)) * 180.0 / std::numbers::pi;
    const double direction = base + rotation / 10.0;
    int octant = static_cast<int>(std::lround(direction / 45.0)) % 4;
    if (octant < 0)
        octant += 4;
    switch (octant) {
    case 0:
        return CanvasCursor::SizeHorizontal;
    case 1:
        return CanvasCursor::SizeFDiagonal; // «\»: links oben – rechts unten
    case 2:
        return CanvasCursor::SizeVertical;
    default:
        return CanvasCursor::SizeBDiagonal; // «/»
    }
}

} // namespace dss
