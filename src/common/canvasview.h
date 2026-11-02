#pragma once

#include <QList>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>

// Ansicht der Leinwand im Editor: Einpassen, Zeigerposition und Lineale (E-09).
// Nur Rechnen, nur Qt Core (CLAUDE.md Regel 9); das Fenster zeichnet damit.
// Koordinaten der Fläche: Pixel des Bildschirms, (0,0) = linke obere Ecke der Fläche ohne Lineale.
// Koordinaten der Leinwand: ganze Pixel der Overlay-Auflösung (CLAUDE.md Regel 11).

namespace dss {

struct CanvasFit
{
    double scale = 1.0; // Bildschirm-Pixel pro Leinwand-Pixel
    QPointF origin;     // Lage der Leinwand-Ecke (0,0) in der Fläche
};

// Zoom «Einpassen»: Die Leinwand passt ganz in die Fläche, mit margin Pixeln Abstand
// auf jeder Seite, und liegt in der Mitte. Ist die Fläche kleiner als 2 * margin + 1,
// wird ohne Abstand eingepasst; ist sie leer, gilt scale = 0.
CanvasFit fitCanvas(QSize canvas, QSize area, int margin);

// Leinwand-Pixel unter einem Punkt der Fläche. false, wenn der Punkt ausserhalb der
// Leinwand liegt (oder scale = 0).
bool canvasPixelAt(const CanvasFit &fit, QSize canvas, QPointF areaPoint, QPoint *pixel);

// Text der Anzeige unten links: «X: 12  Y: 34», ausserhalb «X: –  Y: –».
QString pointerPositionText(bool inside, QPoint pixel);

struct RulerTick
{
    enum Kind { Minor, Medium, Major };
    int value = 0;         // Leinwand-Pixel
    double position = 0.0; // Lage auf dem Lineal in Pixeln der Fläche
    Kind kind = Minor;
};

// Abstand der beschrifteten Striche in Leinwand-Pixeln: der kleinste Wert aus
// 1, 2, 5, 10, 20, 50, … , dessen Abstand auf dem Bildschirm mindestens minSpacing ist.
int rulerMajorStep(double scale, double minSpacing);

// Striche eines Lineals von 0 bis length (Leinwand-Pixel, beide eingeschlossen).
// origin und scale wie in CanvasFit (die passende Achse). Zwischen zwei beschrifteten
// Strichen liegen kleine Striche, wenn sie mindestens minMinorSpacing auseinander sind.
QList<RulerTick> rulerTicks(double scale, double origin, int length, double minMajorSpacing,
                            double minMinorSpacing);

} // namespace dss
