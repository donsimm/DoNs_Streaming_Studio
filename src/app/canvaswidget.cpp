#include "canvaswidget.h"

#include "canvasview.h"
#include "sourcegeometry.h"
#if DSS_APP_RENDERER
#include "sourcerenderer.h"
#endif

#include "dockviews.h"

#include <QContextMenuEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {

constexpr int kRulerSize = 20;        // Breite der Lineale in Bildschirm-Pixeln
constexpr int kCanvasMargin = 8;      // Abstand der eingepassten Leinwand zum Rand der Fläche
constexpr int kCheckerSize = 10;      // Kantenlänge eines Feldes im Schachbrettmuster
constexpr double kMajorSpacing = 60;  // Mindestabstand beschrifteter Striche
constexpr double kMinorSpacing = 4;   // Mindestabstand kleiner Striche

int tickLength(dss::RulerTick::Kind kind)
{
    switch (kind) {
    case dss::RulerTick::Major:
        return kRulerSize - 4;
    case dss::RulerTick::Medium:
        return 7;
    case dss::RulerTick::Minor:
        break;
    }
    return 4;
}

} // namespace

CanvasWidget::CanvasWidget(const Theme &theme, QWidget *parent)
    : QWidget(parent), m_theme(theme)
{
    setMouseTracking(true);
    setMinimumSize(200, 120);
    setAccessibleName(QStringLiteral("Leinwand"));
#if DSS_APP_CANVAS_EDIT
    // Pfeiltasten wirken, wenn die Leinwand den Fokus hat (wie die Vorschau in OBS).
    setFocusPolicy(Qt::StrongFocus);
#endif
}

QRect CanvasWidget::area() const
{
    return QRect(kRulerSize, kRulerSize, width() - kRulerSize, height() - kRulerSize);
}

dss::CanvasFit CanvasWidget::fit() const
{
    return dss::fitCanvas(m_canvasSize, area().size(), kCanvasMargin);
}

QPointF CanvasWidget::overlayPoint(QPointF widgetPoint) const
{
    return dss::areaToOverlay(fit(), widgetPoint - QPointF(area().topLeft()));
}

QPointF CanvasWidget::toWidget(QPointF overlayPoint) const
{
    return QPointF(area().topLeft()) + dss::overlayToArea(fit(), overlayPoint);
}

const dss::Source *CanvasWidget::selectedSource() const
{
    if (!m_overlay || m_selected.isNull())
        return nullptr;
    const int index = dss::sourceIndex(*m_overlay, m_selected);
    return index < 0 ? nullptr : &m_overlay->sources.at(index);
}

void CanvasWidget::setSelectedSource(const QUuid &id)
{
    if (id == m_selected)
        return;
    m_selected = id;
    if (m_drag && m_drag->start.id != id)
        m_drag.reset();
    updateCursor();
    update();
}

void CanvasWidget::setEditable(bool editable)
{
    m_editable = editable;
    if (!editable)
        m_drag.reset();
    updateCursor();
}

void CanvasWidget::setCanvasSize(const QSize &size)
{
    const QSize next = size.isValid() && !size.isEmpty() ? size : QSize(1920, 1080);
    if (next == m_canvasSize)
        return;
    m_canvasSize = next;
    update();
}

void CanvasWidget::setRenderer(SourceRenderer *renderer)
{
    m_renderer = renderer;
#if DSS_APP_RENDERER
    if (renderer)
        connect(renderer, &SourceRenderer::changed, this, qOverload<>(&QWidget::update));
#endif
    update();
}

void CanvasWidget::setOverlay(const std::optional<dss::Overlay> &overlay)
{
    m_overlay = overlay;
    update();
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_pointer = event->position().toPoint();
    m_pointerOver = true;
    if (m_drag) {
        m_drag->pointer = overlayPoint(event->position());
        updateDrag(event->modifiers() & Qt::ShiftModifier);
    }
    updateCursor();
    update();
}

void CanvasWidget::mousePressEvent(QMouseEvent *event)
{
#if DSS_APP_CANVAS_EDIT
    if (event->button() != Qt::LeftButton || !m_overlay || fit().scale <= 0) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF point = overlayPoint(event->position());
    const dss::CanvasPress press = dss::canvasPress(*m_overlay, m_selected, point, fit().scale);
    if (press.sourceId != m_selected) {
        m_selected = press.sourceId;
        emit sourceClicked(press.sourceId);
    }
    const dss::Source *source = selectedSource();
    if (m_editable && source && press.handle != dss::CanvasHandle::None) {
        Drag drag;
        drag.handle = press.handle;
        drag.start = *source;
        drag.startPointer = point;
        drag.pointer = point;
        drag.last = dss::geometryOf(*source);
        m_drag = drag;
    }
    updateCursor();
    update();
#else
    QWidget::mousePressEvent(event);
#endif
}

void CanvasWidget::contextMenuEvent(QContextMenuEvent *event)
{
#if DSS_APP_TRANSFORM
    if (!m_overlay || fit().scale <= 0 || m_drag) {
        QWidget::contextMenuEvent(event);
        return;
    }
    const dss::CanvasPress press =
        dss::canvasPress(*m_overlay, m_selected, overlayPoint(event->pos()), fit().scale);
    if (press.sourceId != m_selected) {
        m_selected = press.sourceId;
        emit sourceClicked(press.sourceId);
        update();
    }
    emit sourceMenuRequested(press.sourceId, event->globalPos());
#else
    QWidget::contextMenuEvent(event);
#endif
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_drag) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const Drag drag = *m_drag;
    m_drag.reset();
    if (drag.changed)
        emit geometryEdited(drag.start.id, drag.last, true);
    updateCursor();
}

void CanvasWidget::updateDrag(bool shift)
{
    Drag &d = *m_drag;
    dss::SourceGeometry g;
    switch (d.handle) {
    case dss::CanvasHandle::Move:
        g = dss::moveGeometry(d.start, d.startPointer, d.pointer);
        break;
    case dss::CanvasHandle::Rotate:
        g = dss::rotateGeometry(d.start, d.pointer, shift);
        break;
    default:
        g = dss::resizeGeometry(d.start, d.handle, d.pointer, shift,
                                dss::originalSize(d.start, m_mediaSizes));
        break;
    }
    if (g == d.last)
        return;
    d.last = g;
    d.changed = true;
    emit geometryEdited(d.start.id, g, false);
}

void CanvasWidget::keyPressEvent(QKeyEvent *event)
{
    // Shift während des Ziehens: neu rechnen (frei skalieren bzw. einrasten).
    if (m_drag && event->key() == Qt::Key_Shift) {
        updateDrag(true);
        return;
    }
    const dss::Source *source = selectedSource();
    if (!m_editable || !source || m_drag) {
        QWidget::keyPressEvent(event);
        return;
    }
    const int step = event->modifiers() & Qt::ShiftModifier ? dss::kNudgeStepLarge : dss::kNudgeStep;
    int dx = 0;
    int dy = 0;
    switch (event->key()) {
    case Qt::Key_Left:
        dx = -step;
        break;
    case Qt::Key_Right:
        dx = step;
        break;
    case Qt::Key_Up:
        dy = -step;
        break;
    case Qt::Key_Down:
        dy = step;
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    emit geometryEdited(source->id, dss::nudgeGeometry(*source, dx, dy), true);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (m_drag && event->key() == Qt::Key_Shift) {
        updateDrag(false);
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CanvasWidget::updateCursor()
{
    dss::CanvasHandle handle = dss::CanvasHandle::None;
    int rotation = 0;
    if (m_drag) {
        handle = m_drag->handle;
        rotation = m_drag->last.rotation;
    } else if (const dss::Source *source = selectedSource();
               m_editable && source && source->visible && m_pointerOver && fit().scale > 0) {
        handle = dss::handleAt(*source, overlayPoint(m_pointer), fit().scale);
        rotation = source->rotation;
    }
    switch (dss::cursorFor(handle, rotation, m_drag.has_value())) {
    case dss::CanvasCursor::Arrow:
        unsetCursor();
        break;
    case dss::CanvasCursor::Move:
        setCursor(Qt::SizeAllCursor);
        break;
    case dss::CanvasCursor::SizeHorizontal:
        setCursor(Qt::SizeHorCursor);
        break;
    case dss::CanvasCursor::SizeVertical:
        setCursor(Qt::SizeVerCursor);
        break;
    case dss::CanvasCursor::SizeFDiagonal:
        setCursor(Qt::SizeFDiagCursor);
        break;
    case dss::CanvasCursor::SizeBDiagonal:
        setCursor(Qt::SizeBDiagCursor);
        break;
    case dss::CanvasCursor::OpenHand:
        setCursor(Qt::OpenHandCursor);
        break;
    case dss::CanvasCursor::ClosedHand:
        setCursor(Qt::ClosedHandCursor);
        break;
    }
}

void CanvasWidget::paintSelection(QPainter *p, const QRect &area, const dss::CanvasFit &fit) const
{
    const dss::Source *source = selectedSource();
    if (!source || !source->visible || fit.scale <= 0)
        return;
    // Rahmen, Griffe und Drehgriff in Bildschirm-Pixeln, mit der Quelle gedreht. Dunkler Rand
    // unter heller Linie: sichtbar auf dunklem und hellem Grund.
    const auto toWidget = [&](QPointF overlayPoint) {
        return QPointF(area.topLeft()) + dss::overlayToArea(fit, overlayPoint);
    };
    const QColor dark(0, 0, 0, 200);
    const QColor light = m_theme.primaryLighter;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QPolygonF frame;
    for (const QPointF &c : dss::sourceCorners(*source))
        frame << toWidget(c);
    const QPointF topCenter = toWidget(dss::handlePosition(*source, dss::CanvasHandle::Top, fit.scale));
    const QPointF rotateAt = toWidget(dss::handlePosition(*source, dss::CanvasHandle::Rotate, fit.scale));
    for (const auto &[color, width] : {std::pair{dark, 3.0}, std::pair{light, 1.0}}) {
        p->setPen(QPen(color, width));
        p->setBrush(Qt::NoBrush);
        p->drawPolygon(frame);
        p->drawLine(topCenter, rotateAt);
    }
    // Griffe: Quadrate mit der Kante 2 * kHandleRadius, mit der Quelle gedreht.
    p->setPen(QPen(dark, 1.0));
    p->setBrush(light);
    for (dss::CanvasHandle h : {dss::CanvasHandle::TopLeft, dss::CanvasHandle::Top,
                                dss::CanvasHandle::TopRight, dss::CanvasHandle::Right,
                                dss::CanvasHandle::BottomRight, dss::CanvasHandle::Bottom,
                                dss::CanvasHandle::BottomLeft, dss::CanvasHandle::Left}) {
        p->save();
        p->translate(toWidget(dss::handlePosition(*source, h, fit.scale)));
        p->rotate(dss::rotationDegrees(source->rotation));
        const double r = dss::kHandleRadius;
        p->drawRect(QRectF(-r, -r, 2 * r, 2 * r));
        p->restore();
    }
    p->drawEllipse(rotateAt, dss::kHandleRadius + 1, dss::kHandleRadius + 1);
    p->restore();
}

void CanvasWidget::leaveEvent(QEvent *)
{
    m_pointerOver = false;
    update();
}

void CanvasWidget::setAcceptsMedia(bool accept)
{
    m_acceptsMedia = accept;
    setAcceptDrops(accept);
}

void CanvasWidget::dragEnterEvent(QDragEnterEvent *event)
{
    dragMoveEvent(event); // QDragEnterEvent ist ein QDragMoveEvent
}

void CanvasWidget::dragMoveEvent(QDragMoveEvent *event)
{
    // Nur mit gewähltem Overlay; Schriften und anderes nicht (droppedMediaSource)
    if (m_acceptsMedia && m_overlay && !droppedMediaSource(event->mimeData()).isEmpty()) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    event->ignore();
}

void CanvasWidget::dropEvent(QDropEvent *event)
{
    const QString reference = droppedMediaSource(event->mimeData());
    if (!m_acceptsMedia || !m_overlay || reference.isEmpty()) {
        event->ignore();
        return;
    }
    const QPointF p = overlayPoint(event->position());
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit mediaDropped(reference, QPoint(qRound(p.x()), qRound(p.y())));
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), m_theme.preview);

    // Fläche ohne Lineale; darin wird die Leinwand eingepasst.
    const QRect area = this->area();
    const dss::CanvasFit fit = this->fit();
    const QRectF canvas(area.left() + fit.origin.x(), area.top() + fit.origin.y(),
                        m_canvasSize.width() * fit.scale, m_canvasSize.height() * fit.scale);

    // Schachbrettmuster für Transparenz, in Bildschirm-Pixeln, nur auf der Leinwand.
    p.save();
    p.setClipRect(canvas);
    p.fillRect(canvas, m_theme.checkerDark);
    const QPoint start = canvas.topLeft().toPoint();
    for (int y = 0; y < canvas.height(); y += kCheckerSize) {
        for (int x = (y / kCheckerSize) % 2 * kCheckerSize; x < canvas.width();
             x += 2 * kCheckerSize)
            p.fillRect(start.x() + x, start.y() + y, kCheckerSize, kCheckerSize,
                       m_theme.checkerLight);
    }
#if DSS_APP_RENDERER
    // Quellen des Overlays (P09, Teil B), beschnitten auf die Leinwand.
    if (m_renderer && m_overlay) {
        p.save();
        p.translate(area.topLeft());
        m_renderer->paint(&p, *m_overlay, fit);
        p.restore();
    }
#endif
    p.restore();
#if DSS_APP_CANVAS_EDIT
    // Rahmen der gewählten Quelle, auch über den Rand der Leinwand hinaus (Teil C).
    paintSelection(&p, area, fit);
#endif

    // Lineale oben und links, in Pixeln der Leinwand.
    p.fillRect(0, 0, width(), kRulerSize, m_theme.preview);
    p.fillRect(0, 0, kRulerSize, height(), m_theme.preview);
    QFont small = font();
    small.setPixelSize(9);
    p.setFont(small);
    p.setPen(m_theme.rulerText);
    p.save();
    p.setClipRect(kRulerSize, 0, width() - kRulerSize, kRulerSize);
    for (const dss::RulerTick &t : dss::rulerTicks(fit.scale, fit.origin.x(), m_canvasSize.width(),
                                                   kMajorSpacing, kMinorSpacing)) {
        const double x = area.left() + t.position;
        p.drawLine(QPointF(x, kRulerSize - tickLength(t.kind)), QPointF(x, kRulerSize));
        if (t.kind == dss::RulerTick::Major)
            p.drawText(QPointF(x + 2, 10), QString::number(t.value));
    }
    p.restore();
    p.save();
    p.setClipRect(0, kRulerSize, kRulerSize, height() - kRulerSize);
    for (const dss::RulerTick &t : dss::rulerTicks(fit.scale, fit.origin.y(), m_canvasSize.height(),
                                                   kMajorSpacing, kMinorSpacing)) {
        const double y = area.top() + t.position;
        p.drawLine(QPointF(kRulerSize - tickLength(t.kind), y), QPointF(kRulerSize, y));
        if (t.kind == dss::RulerTick::Major) {
            // Zahl senkrecht, von unten nach oben lesbar.
            p.save();
            p.translate(10, y + 2);
            p.rotate(-90);
            p.drawText(QPointF(-p.fontMetrics().horizontalAdvance(QString::number(t.value)), 0),
                       QString::number(t.value));
            p.restore();
        }
    }
    p.restore();

    // Zeigerposition unten links, in Pixeln der Leinwand.
    QPoint pixel;
    const bool inside = m_pointerOver
                        && dss::canvasPixelAt(fit, m_canvasSize, QPointF(m_pointer - area.topLeft()),
                                              &pixel);
    const QRect box(area.left() + 6, height() - 26, 146, 20);
    p.fillRect(box, m_theme.positionBack);
    p.setFont(font());
    p.setPen(m_theme.text);
    p.drawText(box.adjusted(6, 0, -6, 0), Qt::AlignLeft | Qt::AlignVCenter,
               dss::pointerPositionText(inside, pixel));
    emit painted();
}
