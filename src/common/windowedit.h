#pragma once

#include "fontcatalog.h"
#include "overlay.h"
#include "overlaylist.h"
#include "overlaystore.h"
#include "protocol.h"

#include <QList>
#include <QString>
#include <QUuid>

#include <optional>

// Bearbeiten im Fenster (P08, Teile C bis E): alles, was die Docks rechnen müssen, ohne Qt Gui
// (CLAUDE.md Regel 9). Das Fenster zeigt nur an, öffnet Dialoge und schickt die Änderungen,
// die hier entstehen.

namespace dss {

// --- Overlays (Teil C) ---

// Vorgaben für «Overlay anlegen» (Entscheid: Standard 1920x1080).
inline constexpr int kNewOverlayWidth = 1920;
inline constexpr int kNewOverlayHeight = 1080;
QString newOverlayName();  // «Neues Overlay»
QString newFolderName();   // «Neuer Ordner»

// Erstes Overlay in der Reihenfolge der Liste (auch in einem Ordner), null ohne Overlays.
QUuid firstOverlay(const OverlayLayout &layout);

// Würde sich die Adresse ändern, wenn das Overlay newName heisst und die Adresse neu aus dem
// Namen entsteht (wie OverlayStore::renameOverlay)? Nur dann fragt das Fenster nach.
// *newAddress (falls nicht nullptr) bekommt die neue Adresse.
bool renameChangesAddress(const QList<OverlaySummary> &overlays, const QUuid &overlayId,
                          const QString &newName, QString *newAddress = nullptr);

// --- Quellen (Teil D) ---

// Zeile im Dock «Quellen» (0 = vorderste Quelle oben, wie sourceRows) → Stelle in der
// Zeichenreihenfolge (Overlay::sources, 0 = ganz hinten). -1 bei ungültiger Zeile.
int sourceIndexForRow(const Overlay &overlay, int row);

// Gezogene Quelle an der Einfügestelle insertRow losgelassen (0 = über der obersten Zeile,
// Anzahl Zeilen = unter der untersten; gezählt mit der gezogenen Zeile). Ergebnis: index für
// move_source, leer wenn sich nichts ändert.
std::optional<int> sourceDropIndex(const Overlay &overlay, const QUuid &sourceId, int insertRow);

// «Nach vorne» (step +1) bzw. «nach hinten» (step -1): index für move_source, leer wenn die
// Quelle schon ganz vorne bzw. hinten liegt.
std::optional<int> sourceStepIndex(const Overlay &overlay, const QUuid &sourceId, int step);

// Filter für den Dateidialog, aus den erlaubten Endungen (allowedMediaSuffixes):
// «Bilder (*.png *.jpg *.jpeg)».
QString mediaFileFilter(MediaUse use);

// --- Eigenschaften (Teil E) ---

// Drehung aus dem Zahlenfeld (Grad, eine Nachkommastelle) in Zehntelgrad 0 bis 3599.
// 360.0 wird 0 (das Feld läuft rundum).
int rotationTenths(double degrees);

// Farbe #RRGGBBAA ↔ Anteile 0 bis 255. Lesen gelingt nur mit gültiger Farbe (validateColor).
QString colorText(int red, int green, int blue, int alpha);
bool colorParts(const QString &color, int *red, int *green, int *blue, int *alpha);

// Schlüssel für das Zusammenfassen häufiger Änderungen (CoreClient::sendChange): je Quelle
// und Feld, zum Beispiel «<Kennung>/x».
QString changeKey(const QUuid &sourceId, const QString &field);

// Einzelne Änderung einer Quelle als Nachricht update_source.
ChangeRequest sourceUpdate(const QUuid &overlayId, const QUuid &sourceId,
                           const SourceChanges &changes);

// Schrift: Auswahl im Dock «Eigenschaften».
struct WeightChoice
{
    int weight = 400;
    QString label; // «Bold (700)»
    bool operator==(const WeightChoice &) const = default;
};
// Gewichte einer Familie, aufsteigend, jedes einmal. Bezeichnung: Name des aufrechten Schnitts,
// sonst des kursiven (ohne «Italic»/«Oblique»), dazu das Gewicht.
QList<WeightChoice> weightChoices(const QList<FontFace> &faces, const QString &family);
// Gibt es in der Familie aufrechte und kursive Schnitte? Nur dann lässt sich «kursiv» wählen.
bool italicSelectable(const QList<FontFace> &faces, const QString &family);
// Familie der Schriftdatei einer Textquelle, leer wenn unbekannt.
QString familyOfFont(const QList<FontFace> &faces, const QString &fontFile);
// Änderung set_text_font für eine Auswahl Familie/Gewicht/kursiv (resolveFontFace). Leer,
// wenn es die Familie nicht gibt.
std::optional<ChangeRequest> fontChange(const QList<FontFace> &faces, const QUuid &overlayId,
                                        const QUuid &sourceId, const QString &family, int weight,
                                        bool italic);

} // namespace dss
