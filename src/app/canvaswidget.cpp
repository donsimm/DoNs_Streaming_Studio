#include "canvaswidget.h"

#include "canvasview.h"

#include <QMouseEvent>
#include <QPainter>

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
}

void CanvasWidget::setCanvasSize(const QSize &size)
{
    const QSize next = size.isValid() && !size.isEmpty() ? size : QSize(1920, 1080);
    if (next == m_canvasSize)
        return;
    m_canvasSize = next;
    update();
}

void CanvasWidget::mouseMoveEvent(QMouseEvent *event)
{
    m_pointer = event->position().toPoint();
    m_pointerOver = true;
    update();
}

void CanvasWidget::leaveEvent(QEvent *)
{
    m_pointerOver = false;
    update();
}

void CanvasWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), m_theme.preview);

    // Fläche ohne Lineale; darin wird die Leinwand eingepasst.
    const QRect area(kRulerSize, kRulerSize, width() - kRulerSize, height() - kRulerSize);
    const dss::CanvasFit fit = dss::fitCanvas(m_canvasSize, area.size(), kCanvasMargin);
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
    p.restore();

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
}
