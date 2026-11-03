#pragma once

#include "overlay.h"

#include <QPointF>
#include <QUuid>

#include <optional>

// Bearbeiten auf der Leinwand (P09, Teil C): Wählen, Griffe, Verschieben, Skalieren, Drehen und
// Pfeiltasten. Nur Rechnen, ohne Qt Gui (CLAUDE.md Regel 9), getestet in tst_canvasedit.
// Punkte sind Pixel des Overlays (double, wie sie aus der Maus kommen); Ergebnisse sind ganze
// Pixel und Zehntelgrad (CLAUDE.md Regel 11). scale = Bildschirm-Pixel pro Overlay-Pixel
// (CanvasFit::scale): Griffe haben eine feste Grösse auf dem Bildschirm.
//
// Vorbild OBS Studio, Commit 65c320b46e1af5c7a555161aea6182cdf7a8c524 (2026-10-29),
// frontend/widgets/OBSBasicPreview.cpp:
//   https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/widgets/OBSBasicPreview.cpp
//   - Griffe: HANDLE_RADIUS 4, Fangradius HANDLE_SEL_RADIUS = 1.5 * 4 = 6 Bildschirm-Pixel
//     (Zeilen 8 f., 373)
//   - Drehgriff: HANDLE_RADIUS * HANDLE_SEL_RADIUS * 1.5 - HANDLE_SEL_RADIUS = 30 Bildschirm-Pixel
//     über der Mitte der oberen Kante, mit der Quelle gedreht; gedreht wird um die Mitte
//     (Zeilen 446-452)
//   - Ecken: Seitenverhältnis bleibt, mit Shift frei; ClampAspect (Zeile 1362): Ist das neue
//     Verhältnis kleiner als das alte, folgt die Breite der Höhe, sonst die Höhe der Breite
//   - Drehen (RotateItem, Zeile 1668): Winkel = atan2 zur Mitte + 90°; Shift rastet auf 15° ein
//   - Zeiger (UpdateCursor, Zeile 688): Drehgriff OpenHandCursor, beim Drehen ClosedHandCursor,
//     Verschieben SizeAllCursor, Griffe Size*Cursor nach Achtel der Drehung
// Abweichungen von OBS (P09-Bericht): Kanten ändern nur Breite oder Höhe (Entscheid 7; OBS hält
// auch dort das Verhältnis); Verhältnis = Kasten beim Beginn des Ziehens (OBS: Grösse der
// Quelle); kein Einrasten ohne Shift (OBS: 5° um 45°-Schritte) und kein Einrasten an Rändern
// (E-10); kein Spiegeln über den Gegengriff hinaus (Mindestgrösse 1 Pixel).

namespace dss {

// Griff: Ecke oder Kante (nach Lage vor dem Drehen), Drehgriff, Verschieben (in der Quelle).
enum class CanvasHandle {
    None,
    Move,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left,
    Rotate
};

inline constexpr double kHandleRadius = 4.0;         // gezeichnete Grösse (halbe Kante), Bildschirm
inline constexpr double kHandleHitRadius = 6.0;      // Fangradius, Bildschirm
inline constexpr double kRotateHandleDistance = 30.0; // Drehgriff über der oberen Kante, Bildschirm

bool isCornerHandle(CanvasHandle handle);
bool isEdgeHandle(CanvasHandle handle);

// Lage eines Griffs in lokalen Koordinaten der Quelle (sourcegeometry.h) bzw. im Overlay.
QPointF handleLocal(const Source &source, CanvasHandle handle, double scale);
QPointF handlePosition(const Source &source, CanvasHandle handle, double scale);

// Griff der gewählten Quelle unter dem Punkt: zuerst Drehgriff, dann Ecken, dann Kanten, dann
// das Innere (Move); None, wenn nichts davon getroffen ist.
CanvasHandle handleAt(const Source &source, QPointF overlayPoint, double scale);

// Was ein Klick bewirkt (Teil C): Griffe und Inneres der gewählten Quelle zuerst, sonst die
// oberste sichtbare Quelle unter dem Zeiger (verschieben), sonst Auswahl aufheben.
struct CanvasPress
{
    QUuid sourceId;              // null = Auswahl aufheben
    CanvasHandle handle = CanvasHandle::None;
};
CanvasPress canvasPress(const Overlay &overlay, const QUuid &selectedId, QPointF overlayPoint,
                        double scale);

struct SourceGeometry
{
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    int rotation = 0; // Zehntelgrad 0 bis 3599
    bool operator==(const SourceGeometry &) const = default;
};
SourceGeometry geometryOf(const Source &source);
// Setzt die Lage einer Quelle im Overlay (für die Vorschau beim Ziehen); false, wenn es die
// Quelle nicht gibt.
bool applyGeometry(Overlay *overlay, const QUuid &sourceId, const SourceGeometry &geometry);

// Verschieben um den Weg des Zeigers seit dem Beginn, auf ganze Pixel gerundet.
SourceGeometry moveGeometry(const Source &start, QPointF startPointer, QPointF pointer);

// Skalieren an einem Ecken- oder Kantengriff. Der Gegengriff bleibt stehen (auch gedreht).
// Ecken: Seitenverhältnis des Kastens beim Beginn bleibt, freeAspect (Shift) = frei.
// Kanten: nur Breite bzw. Höhe (Entscheid 7). Mindestgrösse kSourceMinSize.
SourceGeometry resizeGeometry(const Source &start, CanvasHandle handle, QPointF pointer,
                              bool freeAspect);

// Drehen am Drehgriff: Winkel des Zeigers um die Mitte, 0° = Zeiger über der Mitte, positiv im
// Uhrzeigersinn; snap (Shift) = auf 15° einrasten. Ergebnis in Zehntelgrad 0 bis 3599.
SourceGeometry rotateGeometry(const Source &start, QPointF pointer, bool snap);

// Pfeiltasten: 1 Pixel, mit Shift 10 Pixel (OBS: frontend/OBSBasic.cpp Zeilen 390-406,
// https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/OBSBasic.cpp).
inline constexpr int kNudgeStep = 1;
inline constexpr int kNudgeStepLarge = 10;
SourceGeometry nudgeGeometry(const Source &source, int dx, int dy);

// Zeigerform über einem Griff (Qt::CursorShape wird im Fenster daraus).
enum class CanvasCursor { Arrow, Move, SizeHorizontal, SizeVertical, SizeFDiagonal, SizeBDiagonal,
                          OpenHand, ClosedHand };
// rotation in Zehntelgrad; dragging = Griff wird gerade gezogen (Drehgriff: ClosedHand).
CanvasCursor cursorFor(CanvasHandle handle, int rotation, bool dragging);

} // namespace dss
