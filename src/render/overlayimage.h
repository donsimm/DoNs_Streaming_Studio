#pragma once

#include "overlay.h"

#include <QDateTime>
#include <QImage>
#include <QString>

class SourceRenderer;

// Bild eines Overlays in seiner Auflösung (P10-vor, Teil B 2), gezeichnet vom Zeichner der
// Leinwand: nur die Quellen, ohne Schachbrett, Auswahlrahmen, Lineale, Hilfslinien und Zoom.
// Durchsichtig, wo keine Quelle deckt. Videos mit dem Bild, das der Zeichner gerade hält (das
// Bild der Leinwand). Ohne Fenster nutzbar (Qt Gui, kein Qt Widgets), für P11.

namespace dss {

// QImage::Format_ARGB32_Premultiplied, overlay.width x overlay.height, Massstab 1.
QImage renderOverlayImage(const SourceRenderer &renderer, const Overlay &overlay);

// Zeichnet und speichert als PNG in dir (Ordner wird bei Bedarf angelegt), Name aus
// uniqueCanvasImagePath (canvasimage.h). Atomar (QSaveFile). false mit Meldung auf Deutsch.
bool saveOverlayImage(const SourceRenderer &renderer, const Overlay &overlay, const QString &dir,
                      const QDateTime &time, QString *path, QString *error);

} // namespace dss
