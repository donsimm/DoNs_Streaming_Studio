#pragma once

#include <QByteArrayView>
#include <QPointF>
#include <QSize>
#include <QString>

// EXIF-Ausrichtung von Bildern (P09b, Entscheid 7). Wie der Browser mit
// «image-orientation: from-image» (Anfangswert der CSS-Eigenschaft) zeigen Editor und Kern ein Bild so,
// wie die Ausrichtung es verlangt; die Originalgrösse ist die angezeigte Grösse. Nur Qt Core
// (CLAUDE.md Regel 9): Lesen der Ausrichtung und alle Rechnungen; die Oberfläche wendet die
// Drehung und Spiegelung nur an.
//
// Quellen:
//   MDN image-orientation: https://developer.mozilla.org/en-US/docs/Web/CSS/image-orientation
//   EXIF 2.32 (CIPA DC-008-Translation-2019):
//     https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf
//     Tag Orientation 274 (0x0112), Typ SHORT, Anzahl 1, Standard 1, Werte 1 bis 8 (Abschnitt
//     4.6.4); APP1-Segment mit «Exif» 00 00 und TIFF-Kopf (4.5.4, 4.5.5); IFD (4.6.2)
//   PNG: Chunk eXIf mit den TIFF-Daten ohne «Exif» 00 00, nur gültig vor dem ersten IDAT
//     (PNG Third Edition, W3C Recommendation 2025-06-24, Abschnitt 11.3.4.5 und Tabelle 7):
//     https://www.w3.org/TR/png-3/#eXIf

namespace dss {

// Werte des EXIF-Tags Orientation. 1 = keine Änderung, auch wenn die Angabe fehlt oder ungültig ist.
inline constexpr int kOrientationNormal = 1;
inline constexpr int kOrientationMin = 1;
inline constexpr int kOrientationMax = 8;

// Ausrichtung aus TIFF-Daten (ab dem Kopf «II*\0» oder «MM\0*»): Tag 0x0112 im ersten IFD.
int tiffOrientation(QByteArrayView tiff);
// Ausrichtung einer JPEG- oder PNG-Datei; 1 bei anderen Formaten, ohne Angabe oder bei Fehlern.
// Liest nur die Segmente bzw. Chunks vor den Bilddaten.
int imageFileOrientation(const QString &path);

// Angezeigte Grösse: Bei 5 bis 8 sind Breite und Höhe vertauscht.
QSize orientedSize(QSize stored, int orientation);

// Punkt im gespeicherten Bild (Grösse stored, Pixelkanten) → Punkt im angezeigten Bild.
// 1 (x, y); 2 (w−x, y); 3 (w−x, h−y); 4 (x, h−y); 5 (y, x); 6 (h−y, x); 7 (h−y, w−x); 8 (y, w−x)
QPointF orientedPoint(QPointF stored, QSize storedSize, int orientation);

// Dieselbe Abbildung als Schritte für die Oberfläche: zuerst waagrecht spiegeln (mirror), dann um
// rotation Grad im Uhrzeigersinn drehen (0, 90, 180, 270).
struct OrientationSteps
{
    bool mirror = false;
    int rotation = 0;
    bool operator==(const OrientationSteps &) const = default;
};
OrientationSteps orientationSteps(int orientation);

} // namespace dss
