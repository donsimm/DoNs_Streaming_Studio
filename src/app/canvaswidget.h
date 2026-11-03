#pragma once

#include "overlay.h"
#include "theme.h"

#include <QPoint>
#include <QSize>
#include <QWidget>

#include <optional>

class SourceRenderer;

// Fläche der Leinwand mit Linealen oben und links, Schachbrettmuster, dunklem Rand und
// Anzeige der Zeigerposition. Zeichnet nur; gerechnet wird in dss_common (canvasview.h).
// Ohne Overlay eine leere Leinwand 1920x1080 als Platzhalter, Zoom immer «Einpassen». Seit P07
// in der Auflösung des gewählten Overlays. Seit P09 zeichnet SourceRenderer die Quellen darauf
// (Schalter DSS_APP_RENDERER).
class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasWidget(const Theme &theme, QWidget *parent = nullptr);

    // Auflösung der Leinwand in Pixeln des Overlays; ungültige Grösse = Platzhalter 1920x1080.
    void setCanvasSize(const QSize &size);
    QSize canvasSize() const { return m_canvasSize; }

    // P09: Zeichner der Quellen (nullptr = keine Quellen zeichnen) und das gezeigte Overlay.
    void setRenderer(SourceRenderer *renderer);
    void setOverlay(const std::optional<dss::Overlay> &overlay);

signals:
    // Nach jedem Zeichnen (Leistungsmessung, P09).
    void painted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Theme m_theme;
    QSize m_canvasSize{1920, 1080};
    SourceRenderer *m_renderer = nullptr;
    std::optional<dss::Overlay> m_overlay;
    QPoint m_pointer;
    bool m_pointerOver = false;
};
