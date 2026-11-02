#pragma once

#include "theme.h"

#include <QPoint>
#include <QSize>
#include <QWidget>

// Fläche der Leinwand mit Linealen oben und links, Schachbrettmuster, dunklem Rand und
// Anzeige der Zeigerposition. Zeichnet nur; gerechnet wird in dss_common (canvasview.h).
// Ohne Overlay eine leere Leinwand 1920x1080 als Platzhalter, Zoom immer «Einpassen». Seit P07
// in der Auflösung des gewählten Overlays; Quellen werden noch nicht gezeichnet (P09).
class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasWidget(const Theme &theme, QWidget *parent = nullptr);

    // Auflösung der Leinwand in Pixeln des Overlays; ungültige Grösse = Platzhalter 1920x1080.
    void setCanvasSize(const QSize &size);
    QSize canvasSize() const { return m_canvasSize; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    Theme m_theme;
    QSize m_canvasSize{1920, 1080};
    QPoint m_pointer;
    bool m_pointerOver = false;
};
