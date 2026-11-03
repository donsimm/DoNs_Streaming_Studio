#pragma once

#include "overlay.h"

#include <QByteArray>
#include <QJsonObject>

// Umwandlung Overlay ↔ JSON (K-01). Beschreibung: docs/06-overlay-format.md

namespace dss {

// Version des Dateiformats, die dieses Programm schreibt. 2 seit P09: Feld video.hold_at_end
// (Entscheid 9). 3 seit P09b: Felder flip_horizontal und flip_vertical je Quelle (Entscheid 4).
// Gelesen werden 1 bis 3; fehlt ein Feld in einer älteren Version, gilt es als false.
// Begründung: docs/06-overlay-format.md, Feld format_version.
inline constexpr int kOverlayFormatVersion = 3;
inline constexpr int kOverlayFormatVersionMin = 1;

QJsonObject overlayToJson(const Overlay &overlay);
// Liest streng: fehlende, überzählige oder ungültige Felder und eine unbekannte
// format_version führen zu false mit Meldung in *error.
bool overlayFromJson(const QJsonObject &object, Overlay *overlay, QString *error);

// Ganze Datei (JSON, eingerückt, UTF-8).
QByteArray serializeOverlay(const Overlay &overlay);
bool parseOverlay(const QByteArray &data, Overlay *overlay, QString *error);

} // namespace dss
