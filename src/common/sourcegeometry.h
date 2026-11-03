#pragma once

#include "canvasview.h"
#include "overlay.h"
#include "windowdata.h" // rotationDegrees

#include <QByteArray>
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <array>

// Zeichnen der Quellen auf der Leinwand (P09, Teil B): Rechnungen ohne Qt Gui
// (CLAUDE.md Regel 9), getestet in tst_sourcegeometry.
// Lage einer Quelle: Ecke links oben (x, y), Breite, Höhe in Pixeln des Overlays; gedreht um
// ihre Mitte, positiv im Uhrzeigersinn (Entscheid 8, wie QPainter::rotate bei y nach unten).
// Lokale Koordinaten einer Quelle: (0,0) = ihre Ecke links oben vor dem Drehen, (w,h) unten rechts.

namespace dss {

// Mitte der Quelle in Pixeln des Overlays.
QPointF sourceCenter(const Source &source);

// Punkt der Quelle (lokal) → Overlay, mit Drehung um die Mitte.
QPointF sourceToOverlay(const Source &source, QPointF local);
// Punkt im Overlay → lokal in der Quelle (Umkehrung von sourceToOverlay).
QPointF overlayToSource(const Source &source, QPointF overlayPoint);

// Ecken der gedrehten Quelle im Overlay: links oben, rechts oben, rechts unten, links unten.
std::array<QPointF, 4> sourceCorners(const Source &source);

// Pixel des Overlays ↔ Fläche der Leinwand (Bildschirm-Pixel, wie CanvasFit).
QPointF overlayToArea(const CanvasFit &fit, QPointF overlayPoint);
QPointF areaToOverlay(const CanvasFit &fit, QPointF areaPoint); // fit.scale > 0

// Liegt der Punkt (Overlay) in der gedrehten Quelle? Ränder gehören dazu.
bool sourceContains(const Source &source, QPointF overlayPoint);

// Oberste sichtbare Quelle unter dem Punkt (Overlay): Stelle in Overlay::sources, -1 ohne.
// Verborgene Quellen zählen nicht (Teil C).
int topmostSourceAt(const Overlay &overlay, QPointF overlayPoint);

// Quellen, die gezeichnet werden, in Zeichenreihenfolge (hinten zuerst): nur sichtbare.
QList<int> drawnSources(const Overlay &overlay);

// --- Text (TX-01 bis TX-05) ---

// Zeilen des Inhalts: nur an den Zeilenwechseln des Inhalts ('\n'), kein Umbruch.
QStringList textLines(const QString &content);
// Linker Rand einer Zeile der Breite lineWidth in einem Kasten der Breite boxWidth.
double alignedLineX(TextAlign align, double boxWidth, double lineWidth);

// --- Variable Schriften (Punkt 45) ---

struct FontAxis
{
    QString tag; // zum Beispiel "wght"
    double minimum = 0;
    double defaultValue = 0;
    double maximum = 0;
    bool operator==(const FontAxis &) const = default;
};

// Achsen aus der Tabelle 'fvar' einer OpenType-Datei (leer, wenn die Tabelle fehlt oder
// ungültig ist). Aufbau: https://learn.microsoft.com/en-us/typography/opentype/spec/fvar
QList<FontAxis> parseFvarAxes(const QByteArray &fvar);
// Wert für die Achse wght: das gewählte Gewicht, begrenzt auf die Achse der Datei.
// false, wenn die Datei keine Achse wght hat.
bool weightAxisValue(const QList<FontAxis> &axes, int weight, double *value);

} // namespace dss
