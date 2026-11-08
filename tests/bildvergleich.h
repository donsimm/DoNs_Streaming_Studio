#pragma once

// Vergleichslogik der zwei Zeichner (P11, K-08): Unterschied zweier Bilder in einem Bereich, mit
// Kantenmaske. Rechnet vormultipliziert (QImage::Format_ARGB32_Premultiplied), Kanäle 0 bis 255,
// Alpha zählt als vierter Kanal. Nur Qt Gui für QImage, ohne Fenster (Regel 9). Bilder anderer
// Formate werden vorher umgewandelt. Beide Bilder müssen gleich gross sein; der Bereich wird auf
// das Bild beschnitten.
//
// Kantenmaske: Ein Pixel liegt an einer Kante, wenn in einem der beiden Bilder ein Kanal (R, G, B
// oder Alpha) zum rechten oder unteren Nachbarn um mehr als kEdgeJump springt (beide Pixel des
// Sprungs). Die Maske wird danach um 1 Pixel erweitert (3 x 3). So fallen Helligkeits-,
// Farb- und Alphasprünge darunter, auch reine Farbsprünge bei gleicher Helligkeit (4:2:0).
//
// Quellen: QImage::scanLine, Format_ARGB32_Premultiplied
//   https://doc.qt.io/qt-6.8/qimage.html#scanLine, https://doc.qt.io/qt-6.8/qimage.html#Format-enum

#include <QImage>
#include <QRect>

namespace bildvergleich {

inline constexpr int kEdgeJump = 32;
inline constexpr int kLargeDeviation = 32;

// Unterschied ohne Kantenmaske (seit P10 in tst_browserquelle).
struct Difference
{
    double mean = 0;   // mittlere Abweichung je Kanal (0 bis 255)
    int max = 0;       // grösste Abweichung eines Kanals
    double over32 = 0; // Anteil der Pixel mit einer Abweichung > 32 (Prozent)
};
Difference compare(const QImage &a, const QImage &b, const QRect &area);

// Masszahlen eines Bereichs mit Kantenmaske (Plan P11, Abschnitt 4).
struct Measure
{
    double mean = 0;         // mittlere Abweichung je Kanal über alle Pixel des Bereichs
    int maxOutsideEdges = 0; // grösste Abweichung eines Kanals ausserhalb der Kanten
    double edgeOver32 = 0;   // Anteil der Kantenpixel mit einer Abweichung > 32 (Prozent)
    qint64 pixels = 0;
    qint64 edgePixels = 0;
};

// Maske im Format Grayscale8, 255 = Kante, 0 = keine. Leer, wenn die Grössen verschieden sind.
QImage edgeMask(const QImage &a, const QImage &b, int jump = kEdgeJump);
// mask aus edgeMask(a, b). Leere Masszahlen, wenn die Grössen verschieden sind.
Measure measure(const QImage &a, const QImage &b, const QImage &mask, const QRect &area);

// Grösste Abweichung eines Kanals je Pixel mal gain (höchstens 255), deckend auf Schwarz; zum
// Speichern bei einer Abweichung. Kanten der Maske (falls nicht leer) blau eingefärbt.
QImage differenceImage(const QImage &a, const QImage &b, const QImage &mask = QImage(), int gain = 4);

// Bild auf Schwarz gelegt (deckend). Für den Vergleich mit einem Bild ohne Alpha (OBS).
QImage onBlack(const QImage &image);
// Alle Pixel deckend.
bool fullyOpaque(const QImage &image);

} // namespace bildvergleich
