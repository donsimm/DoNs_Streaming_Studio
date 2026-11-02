#pragma once

#include "theme.h"

#include <QPoint>
#include <QSize>
#include <QWidget>

// Fläche der Leinwand mit Linealen oben und links, Schachbrettmuster, dunklem Rand und
// Anzeige der Zeigerposition. Zeichnet nur; gerechnet wird in dss_common (canvasview.h).
// Ohne Overlay eine leere Leinwand 1920x1080 als Platzhalter, Zoom immer «Einpassen».
class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CanvasWidget(const Theme &theme, QWidget *parent = nullptr);

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
