#include "gaugewidget.h"

#include <QPainter>
#include <QPainterPath>

GaugeGraph::GaugeGraph(const QColor &line, QWidget *parent)
    : QFrame(parent), m_line(line)
{
    setObjectName(QStringLiteral("gaugeGraph"));
}

void GaugeGraph::setHistory(const QList<std::optional<double>> &values, int capacity,
                            double maximum)
{
    m_values = values;
    m_capacity = std::max(2, capacity);
    m_maximum = maximum > 0 ? maximum : 1.0;
    update();
}

void GaugeGraph::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event); // Hintergrund und Rahmen aus dem Stylesheet
    const QRectF area = QRectF(contentsRect()).adjusted(1, 2, -1, -1);
    if (m_values.isEmpty() || area.width() <= 0 || area.height() <= 0)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double step = area.width() / (m_capacity - 1);
    // Neuester Wert am rechten Rand.
    const qsizetype offset = m_capacity - m_values.size();
    QPainterPath line;
    QPainterPath fill;
    bool open = false;
    QPointF first;
    QPointF last;
    const auto closeSegment = [&] {
        if (!open)
            return;
        fill.lineTo(last.x(), area.bottom());
        fill.lineTo(first.x(), area.bottom());
        fill.closeSubpath();
        open = false;
    };
    for (qsizetype i = 0; i < m_values.size(); ++i) {
        const std::optional<double> &v = m_values.at(i);
        if (!v) {
            closeSegment(); // Lücke, wo der Wert fehlt
            continue;
        }
        const double ratio = std::clamp(*v / m_maximum, 0.0, 1.0);
        const QPointF point(area.left() + (offset + i) * step,
                            area.bottom() - ratio * area.height());
        if (!open) {
            line.moveTo(point);
            fill.moveTo(point);
            first = point;
            open = true;
        } else {
            line.lineTo(point);
            fill.lineTo(point);
        }
        last = point;
    }
    closeSegment();
    QColor shade = m_line;
    shade.setAlpha(50);
    p.fillPath(fill, shade);
    p.setPen(QPen(m_line, 1.5));
    p.drawPath(line);
}
