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

// Spiegeln (P09b): Punkt des Inhalts (Bild, Video, Text, in Pixeln des Kastens, ungespiegelt) →
// lokal im Kasten. Waagrecht gespiegelt: x → Breite − x; senkrecht gespiegelt: y → Höhe − y.
// Der Zeichner spiegelt um die Mitte, nach dem Drehen gerechnet also vor dem Drehen
// (Reihenfolge wie OBS: erst Massstab mit Vorzeichen, dann Drehung, libobs/obs-scene.c
// update_item_transform; im Browser «rotate(…) scale(-1, 1)» um die Mitte, P10).
QPointF contentToSource(const Source &source, QPointF contentPoint);
// Faktoren für QPainter::scale bzw. CSS scale(): -1 = gespiegelt, 1 = nicht.
QPointF flipScale(const Source &source);

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

// Tabelle tag (zum Beispiel "fvar") aus einer OpenType-/TrueType-Datei, leer ohne sie.
// Aufbau des Tabellenverzeichnisses: https://learn.microsoft.com/en-us/typography/opentype/spec/otff
QByteArray sfntTable(const QByteArray &fontFile, const char *tag);
// Achsen aus der Tabelle 'fvar' einer OpenType-Datei (leer, wenn die Tabelle fehlt oder
// ungültig ist). Aufbau: https://learn.microsoft.com/en-us/typography/opentype/spec/fvar
QList<FontAxis> parseFvarAxes(const QByteArray &fvar);
// Wert für die Achse wght: das gewählte Gewicht, begrenzt auf die Achse der Datei.
// false, wenn die Datei keine Achse wght hat.
bool weightAxisValue(const QList<FontAxis> &axes, int weight, double *value);

// --- Zeilenmasse (P10, zweiter Zeichner) ---

// Oberlänge, Unterlänge und Zeilenabstand einer Schrift in Einheiten der Schrift, so wie Qt 6.8
// sie für QFontMetricsF wählt (QFontEngine::initializeHeightMetrics): zuerst hhea, dann OS/2
// darüber, mit USE_TYPO_METRICS (fsSelection Bit 7) die typo-Werte, sonst winAscent/winDescent
// und Zeilenabstand 0. Die Webseite rechnet daraus dieselben Zeilen wie der Editor.
// https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/text/qfontengine.cpp?h=v6.8.2
// Tabellen: https://learn.microsoft.com/en-us/typography/opentype/spec/hhea,
// https://learn.microsoft.com/en-us/typography/opentype/spec/os2,
// https://learn.microsoft.com/en-us/typography/opentype/spec/head
struct FontLineMetrics
{
    int unitsPerEm = 0;
    int ascent = 0;
    int descent = 0; // positiv = unter der Grundlinie
    int leading = 0;
    bool operator==(const FontLineMetrics &) const = default;
};
// false, wenn head oder hhea fehlen oder ungültig sind, oder bei eingebetteten Bitmaps (EBLC,
// CBLC, bdat: dann nimmt Qt die Werte von FreeType, hier nicht nachgebaut).
bool fontLineMetrics(const QByteArray &fontFile, FontLineMetrics *metrics);
// Wert in Pixeln wie Qt: QFixed (1/64 Pixel), QFixed::fromReal(units * pixelSize) / unitsPerEm
// mit gerundeter Division.
double fontMetricPixels(int units, int pixelSize, int unitsPerEm);

} // namespace dss
