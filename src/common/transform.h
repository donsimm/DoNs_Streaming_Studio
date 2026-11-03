#pragma once

#include "canvasedit.h"
#include "overlay.h"

#include <QSize>

#include <optional>

// Befehle «Transformieren» wie OBS (P09b, Entscheid 5, E-25): im Menü «Bearbeiten» und per
// Rechtsklick auf eine Quelle (Leinwand und Dock «Quellen»). Nur Qt Core (CLAUDE.md Regel 9);
// das Fenster schickt das Ergebnis wie jede Änderung der Lage an den Kern.
//
// Vorbild OBS Studio, Commit 65c320b46e1af5c7a555161aea6182cdf7a8c524 (2026-10-29):
//   frontend/widgets/OBSBasic_SceneItems.cpp
//     https://github.com/obsproject/obs-studio/blob/65c320b46e1af5c7a555161aea6182cdf7a8c524/frontend/widgets/OBSBasic_SceneItems.cpp
//     - GetItemBox (Zeilen 1048-1068): umschliessender Kasten der gedrehten Quelle
//     - RotateSelectedSources (1089-1118), on_actionRotate90CW/90CCW/180 (1120-1160): Drehung
//       += 90/-90/180, die linke obere Ecke des umschliessenden Kastens bleibt
//     - MultiplySelectedItemScale (1162-1188), on_actionFlipHorizontal/Vertical (1190-1218):
//       Massstab * -1 in einer Richtung, linke obere Ecke des umschliessenden Kastens bleibt
//     - CenterAlignSelectedItems (1220-1251), on_actionFitToScreen (1253, OBS_BOUNDS_SCALE_INNER),
//       on_actionStretchToScreen (1267, OBS_BOUNDS_STRETCH): Lage 0/0, Massstab 1 (nicht mehr
//       gespiegelt), Drehung 0, Begrenzung = Leinwand, mittig ausgerichtet
//     - CenterSelectedSceneItems (1281-1366), on_actionCenterToScreen/VerticalCenter/
//       HorizontalCenter (1368-1408): Mitte des umschliessenden Kastens auf die Mitte der
//       Leinwand; «senkrecht» verschiebt nur in y, «waagrecht» nur in x
//   frontend/widgets/OBSBasic_Scenes.cpp, reset_tr (Zeilen 952-983): Lage 0/0, Massstab 1,
//     Drehung 0, keine Begrenzung, kein Zuschnitt
//   frontend/widgets/OBSBasic_Clipboard.cpp (Zeilen 28-68): kopiert und setzt obs_transform_info
//   libobs/obs-scene.c, calculate_bounds_data (Zeilen 446-558, Massstab 466-497): SCALE_INNER
//     mit dem Verhältnis der Quelle, Abstand mittig (add_alignment, Zeilen 325-336, ganzzahlig
//     halbiert)
//
// Abweichungen (P09b-Bericht): Einpassen und Strecken setzen feste Pixel, OBS behält eine
// Begrenzung (bounds), die auch später gilt. Einfügen übernimmt die Grösse in Pixeln, OBS den
// Massstab. Zuschneiden gibt es nicht. Zentrieren rechnet mit dem Kasten auch ganz links oder
// oben ausserhalb der Leinwand (OBS beginnt rechts und unten bei 0).

namespace dss {

enum class TransformCommand {
    Reset,
    Rotate90Clockwise,
    Rotate90CounterClockwise,
    Rotate180,
    FlipHorizontal,
    FlipVertical,
    FitToCanvas,
    StretchToCanvas,
    CenterOnCanvas,
    CenterVertically,   // nur y
    CenterHorizontally, // nur x
};

// Umschliessender Kasten der gedrehten Quelle in Pixeln des Overlays (links, oben, Breite, Höhe).
struct BoundingBox
{
    double left = 0;
    double top = 0;
    double width = 0;
    double height = 0;
};
BoundingBox boundingBox(const SourceGeometry &geometry);

// Neue Lage nach einem Befehl. canvas = Auflösung des Overlays. originalSize = Grösse der Datei
// (originalSize() aus overlay.h); ohne (Text, Datei unbekannt) behält Zurücksetzen die Grösse
// und Einpassen nimmt das Verhältnis des Kastens.
SourceGeometry transformGeometry(const Source &source, TransformCommand command, QSize canvas,
                                 std::optional<QSize> originalSize);

} // namespace dss
