#pragma once

#include "canvasedit.h"
#include "canvasview.h"
#include "overlay.h"
#include "theme.h"

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QUuid>
#include <QWidget>

#include <optional>

class SourceRenderer;

// Fläche der Leinwand mit Linealen oben und links, Schachbrettmuster, dunklem Rand und
// Anzeige der Zeigerposition. Zeichnet nur; gerechnet wird in dss_common (canvasview.h).
// Ohne Overlay eine leere Leinwand 1920x1080 als Platzhalter, Zoom immer «Einpassen». Seit P07
// in der Auflösung des gewählten Overlays. Seit P09 zeichnet SourceRenderer die Quellen darauf
// (Schalter DSS_APP_RENDERER). Seit P09 Teil C (Schalter DSS_APP_CANVAS_EDIT): Klick wählt die
// oberste sichtbare Quelle, die gewählte hat Rahmen, Griffe und Drehgriff; Ziehen verschiebt,
// skaliert und dreht, Pfeiltasten verschieben. Die Rechnung steht in dss_common (canvasedit.h);
// die Leinwand meldet nur, was der Nutzer will (Signale), und zeigt, was das Fenster ihr gibt.
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
    // P09 Teil C: gewählte Quelle (Rahmen und Griffe) und ob gezogen werden darf.
    void setSelectedSource(const QUuid &id);
    void setEditable(bool editable);

signals:
    // Nach jedem Zeichnen (Leistungsmessung, P09).
    void painted();
    // Klick auf die Leinwand wählt diese Quelle (null = Auswahl aufheben).
    void sourceClicked(const QUuid &id);
    // Neue Lage beim Ziehen bzw. mit den Pfeiltasten; finished beim Loslassen und bei Tasten.
    void geometryEdited(const QUuid &id, const dss::SourceGeometry &geometry, bool finished);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // Fläche ohne Lineale und eingepasste Leinwand für die aktuelle Grösse des Widgets.
    QRect area() const;
    dss::CanvasFit fit() const;
    QPointF overlayPoint(QPointF widgetPoint) const;
    const dss::Source *selectedSource() const;
    void updateDrag(bool shift);
    void updateCursor();
    void paintSelection(QPainter *painter, const QRect &area, const dss::CanvasFit &fit) const;

    Theme m_theme;
    QSize m_canvasSize{1920, 1080};
    SourceRenderer *m_renderer = nullptr;
    std::optional<dss::Overlay> m_overlay;
    QUuid m_selected;
    bool m_editable = false;
    // Ziehen (Teil C): Griff, Quelle und Zeiger beim Beginn, letzte gemeldete Lage.
    struct Drag
    {
        dss::CanvasHandle handle = dss::CanvasHandle::None;
        dss::Source start;
        QPointF startPointer; // Overlay
        QPointF pointer;      // Overlay
        dss::SourceGeometry last;
        bool changed = false;
    };
    std::optional<Drag> m_drag;
    QPoint m_pointer;
    bool m_pointerOver = false;
};
