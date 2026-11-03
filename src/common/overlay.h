#pragma once

#include <QHash>
#include <QList>
#include <QSize>
#include <QString>
#include <QUuid>

#include <optional>

// Datenmodell eines Overlays (P04, K-01). Nur Qt Core.
// Alle Lagen und Grössen sind ganze Pixel der Overlay-Auflösung (CLAUDE.md Regel 11).
// Ungültige Werte werden mit einer Meldung abgewiesen, nie still korrigiert.
// Beschreibung des Dateiformats: docs/06-overlay-format.md

namespace dss {

// Grenzen der Werte, siehe docs/06-overlay-format.md
inline constexpr int kOverlayMinSize = 1;
inline constexpr int kOverlayMaxWidth = 7680;
inline constexpr int kOverlayMaxHeight = 4320;
inline constexpr int kSourceMinSize = 1;
inline constexpr int kRotationMin = 0;    // Zehntelgrad
inline constexpr int kRotationMax = 3599; // Zehntelgrad, also 359.9 °
inline constexpr int kFontWeightMin = 100;
inline constexpr int kFontWeightMax = 900;
inline constexpr int kFontSizeMin = 1;

enum class SourceKind { Text, Image, Video };
enum class TextAlign { Left, Center, Right };

// Namen wie in der Datei: "text", "image", "video" bzw. "left", "center", "right"
QString sourceKindName(SourceKind kind);
bool sourceKindFromName(const QString &name, SourceKind *kind);
QString textAlignName(TextAlign align);
bool textAlignFromName(const QString &name, TextAlign *align);

// Eigenschaften der Art "text" (TX-01 bis TX-05)
struct TextProperties
{
    QString content;                           // mehrzeilig, Zeilenwechsel als '\n'
    QString fontFile;                          // Verweis "media/<sha256>.<endung>"
    int weight = 400;                          // 100 bis 900
    bool italic = false;
    int fontSize = 48;                         // Pixel
    QString color = QStringLiteral("#FFFFFFFF"); // #RRGGBBAA
    TextAlign align = TextAlign::Left;

    bool operator==(const TextProperties &) const = default;
};

struct Source
{
    QUuid id;
    SourceKind kind = SourceKind::Image;
    QString name;
    int x = 0;
    int y = 0;
    int width = 1;
    int height = 1;
    int rotation = 0; // Zehntelgrad um die Mitte der Quelle
    // Gespiegelt (P09b, Entscheid 4): Der Inhalt wird im Kasten gespiegelt, an der senkrechten
    // (waagrecht gespiegelt) bzw. waagrechten Mittellinie (senkrecht gespiegelt) des ungedrehten
    // Kastens, danach gedreht. Breite und Höhe bleiben positiv, der Kasten bleibt, wo er ist.
    bool flipHorizontal = false;
    bool flipVertical = false;
    bool visible = true;
    TextProperties text; // nur bei SourceKind::Text
    QString file;        // nur bei Image und Video: Verweis "media/<sha256>.<endung>"
    bool loop = false;   // nur bei Video: am Ende wieder von vorne, Standard nicht wiederholen
    // Nur bei Video (P09, Entscheid 9): am Ende das letzte Bild stehen lassen. Ohne beides wird
    // die Quelle am Ende leer (durchsichtig). Mit loop hat holdAtEnd keine Wirkung.
    bool holdAtEnd = false;

    bool operator==(const Source &) const = default;
};

struct Overlay
{
    QUuid id;
    QString name;
    QString address;
    int width = 1920;
    int height = 1080;
    QList<Source> sources; // Zeichenreihenfolge: das erste Element liegt hinten
    // Ausgabe ein/aus (E-24, P10): aus = die Browserquelle zeigt nichts (leer, durchsichtig), der
    // Editor zeigt und bearbeitet das Overlay weiter. Neue Overlays: ein.
    bool output = true;

    bool operator==(const Overlay &) const = default;
};

// Prüfungen: leerer String = gültig, sonst eine Meldung auf Deutsch.
QString validateOverlayName(const QString &name);
QString validateOverlaySize(int width, int height);
QString validateAddress(const QString &address);
QString validateSourceGeometry(int width, int height, int rotation);
QString validateMediaReference(const QString &reference);
QString validateColor(const QString &color);
QString validateTextProperties(const TextProperties &text);
QString validateSource(const Source &source);
// Prüft das ganze Overlay einschliesslich aller Quellen und eindeutiger Kennungen.
QString validateOverlay(const Overlay &overlay);

// Auflösung eines bestehenden Overlays ändern (P09, Entscheid 5). Die Quellen behalten ihre
// Pixelwerte; was danach ausserhalb liegt, ist teilweise ausserhalb der Leinwand.
QString setOverlaySize(Overlay *overlay, int width, int height);

// Bearbeiten der Quellen eines Overlays (E-07, Datenteil von E-08).
// Bei einem Fehler bleibt das Overlay unverändert und die Meldung wird zurückgegeben.
int sourceIndex(const Overlay &overlay, const QUuid &sourceId); // -1, wenn nicht vorhanden
QString addSource(Overlay *overlay, const Source &source);
QString removeSource(Overlay *overlay, const QUuid &sourceId);
QString moveSourceForward(Overlay *overlay, const QUuid &sourceId);  // eine Stufe nach vorne
QString moveSourceBackward(Overlay *overlay, const QUuid &sourceId); // eine Stufe nach hinten
QString setSourceVisible(Overlay *overlay, const QUuid &sourceId, bool visible);
QString setSourceGeometry(Overlay *overlay, const QUuid &sourceId, int x, int y, int width,
                          int height, int rotation);
// Setzt die Quelle an die Stelle index der Zeichenreihenfolge (0 = ganz hinten), P08.
QString moveSource(Overlay *overlay, const QUuid &sourceId, int index);

// Änderung einzelner Eigenschaften einer Quelle (P08, Teil E). Nur gesetzte Werte ändern sich.
// Schriftdatei, Gewicht und kursiv ändern sich nur zusammen (OverlayStore::setTextFont), die
// Datei von Bild und Video nur mit einer Kopie im Datenordner.
struct SourceChanges
{
    std::optional<QString> name;
    std::optional<int> x;
    std::optional<int> y;
    std::optional<int> width;
    std::optional<int> height;
    std::optional<int> rotation; // Zehntelgrad
    std::optional<bool> flipHorizontal; // P09b
    std::optional<bool> flipVertical;   // P09b
    std::optional<bool> visible;
    std::optional<bool> loop;        // nur Video
    std::optional<bool> holdAtEnd;   // nur Video (P09)
    std::optional<QString> content;  // nur Text
    std::optional<int> fontSize;     // nur Text
    std::optional<QString> color;    // nur Text, #RRGGBBAA
    std::optional<TextAlign> align;  // nur Text

    bool isEmpty() const;
    bool operator==(const SourceChanges &) const = default;
};
// Wendet die Änderungen an und prüft die Quelle danach (validateSource). Eine Eigenschaft, die
// es bei der Art der Quelle nicht gibt (zum Beispiel loop bei Text), ist ein Fehler.
QString applySourceChanges(Source *source, const SourceChanges &changes);
QString updateSource(Overlay *overlay, const QUuid &sourceId, const SourceChanges &changes);

// Drehung als Text mit einer Nachkommastelle, zum Beispiel 125 → "12.5 °"
QString rotationDisplay(int tenthDegrees);

// Originalgrösse der Dateien (P09b, Entscheid 3): Verweis "media/…" → Grösse in Pixeln, so wie
// die Datei angezeigt wird (Bild mit EXIF-Ausrichtung). Liest der Kern und schickt sie mit dem
// Overlay mit (docs/08-kern-schnittstelle.md); nicht Teil der Overlay-Datei.
using MediaSizes = QHash<QString, QSize>;
// Originalgrösse einer Quelle: Bild und Video aus sizes, Text und unbekannte Dateien nullopt.
std::optional<QSize> originalSize(const Source &source, const MediaSizes &sizes);

} // namespace dss
