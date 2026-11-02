#include "overlay.h"

#include <QRegularExpression>
#include <QSet>

namespace dss {

QString sourceKindName(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Text:
        return QStringLiteral("text");
    case SourceKind::Image:
        return QStringLiteral("image");
    case SourceKind::Video:
        return QStringLiteral("video");
    }
    return {};
}

bool sourceKindFromName(const QString &name, SourceKind *kind)
{
    for (SourceKind k : {SourceKind::Text, SourceKind::Image, SourceKind::Video}) {
        if (name == sourceKindName(k)) {
            *kind = k;
            return true;
        }
    }
    return false;
}

QString textAlignName(TextAlign align)
{
    switch (align) {
    case TextAlign::Left:
        return QStringLiteral("left");
    case TextAlign::Center:
        return QStringLiteral("center");
    case TextAlign::Right:
        return QStringLiteral("right");
    }
    return {};
}

bool textAlignFromName(const QString &name, TextAlign *align)
{
    for (TextAlign a : {TextAlign::Left, TextAlign::Center, TextAlign::Right}) {
        if (name == textAlignName(a)) {
            *align = a;
            return true;
        }
    }
    return false;
}

QString validateOverlayName(const QString &name)
{
    if (name.trimmed().isEmpty())
        return QStringLiteral("Der Name des Overlays darf nicht leer sein.");
    return {};
}

QString validateOverlaySize(int width, int height)
{
    if (width < kOverlayMinSize || width > kOverlayMaxWidth)
        return QStringLiteral("Breite %1 ungültig, erlaubt sind %2 bis %3 Pixel.")
            .arg(width).arg(kOverlayMinSize).arg(kOverlayMaxWidth);
    if (height < kOverlayMinSize || height > kOverlayMaxHeight)
        return QStringLiteral("Höhe %1 ungültig, erlaubt sind %2 bis %3 Pixel.")
            .arg(height).arg(kOverlayMinSize).arg(kOverlayMaxHeight);
    return {};
}

QString validateAddress(const QString &address)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]+(-[a-z0-9]+)*$"));
    if (!pattern.match(address).hasMatch())
        return QStringLiteral("Adresse «%1» ungültig, erlaubt sind a-z, 0-9 und einzelne «-» "
                              "zwischen diesen Zeichen.").arg(address);
    return {};
}

QString validateSourceGeometry(int width, int height, int rotation)
{
    if (width < kSourceMinSize)
        return QStringLiteral("Breite %1 ungültig, mindestens %2 Pixel.").arg(width).arg(kSourceMinSize);
    if (height < kSourceMinSize)
        return QStringLiteral("Höhe %1 ungültig, mindestens %2 Pixel.").arg(height).arg(kSourceMinSize);
    if (rotation < kRotationMin || rotation > kRotationMax)
        return QStringLiteral("Drehung %1 ungültig, erlaubt sind %2 bis %3 Zehntelgrad.")
            .arg(rotation).arg(kRotationMin).arg(kRotationMax);
    return {};
}

QString validateMediaReference(const QString &reference)
{
    // Form siehe docs/06-overlay-format.md: media/<SHA-256 in Kleinbuchstaben>[.<endung>]
    static const QRegularExpression pattern(
        QStringLiteral("^media/[0-9a-f]{64}(\\.[a-z0-9]{1,10})?$"));
    if (!pattern.match(reference).hasMatch())
        return QStringLiteral("Dateiverweis «%1» ungültig, erwartet media/<SHA-256>.<Endung>.")
            .arg(reference);
    return {};
}

QString validateColor(const QString &color)
{
    static const QRegularExpression pattern(QStringLiteral("^#[0-9A-Fa-f]{8}$"));
    if (!pattern.match(color).hasMatch())
        return QStringLiteral("Farbe «%1» ungültig, erwartet #RRGGBBAA.").arg(color);
    return {};
}

QString validateTextProperties(const TextProperties &text)
{
    if (const QString e = validateMediaReference(text.fontFile); !e.isEmpty())
        return QStringLiteral("Schriftdatei: ") + e;
    if (text.weight < kFontWeightMin || text.weight > kFontWeightMax)
        return QStringLiteral("Gewicht %1 ungültig, erlaubt sind %2 bis %3.")
            .arg(text.weight).arg(kFontWeightMin).arg(kFontWeightMax);
    if (text.fontSize < kFontSizeMin)
        return QStringLiteral("Schriftgrösse %1 ungültig, mindestens %2 Pixel.")
            .arg(text.fontSize).arg(kFontSizeMin);
    if (const QString e = validateColor(text.color); !e.isEmpty())
        return e;
    return {};
}

QString validateSource(const Source &source)
{
    if (source.id.isNull())
        return QStringLiteral("Die Quelle hat keine Kennung.");
    if (source.name.trimmed().isEmpty())
        return QStringLiteral("Der Name der Quelle darf nicht leer sein.");
    if (const QString e = validateSourceGeometry(source.width, source.height, source.rotation);
        !e.isEmpty())
        return e;
    // Wiederholen wird nur bei Video gespeichert; anderswo ginge der Wert beim Speichern verloren.
    if (source.loop && source.kind != SourceKind::Video)
        return QStringLiteral("Wiederholen gibt es nur bei Quellen der Art Video.");
    switch (source.kind) {
    case SourceKind::Text:
        return validateTextProperties(source.text);
    case SourceKind::Image:
    case SourceKind::Video:
        return validateMediaReference(source.file);
    }
    return QStringLiteral("Unbekannte Art der Quelle.");
}

QString validateOverlay(const Overlay &overlay)
{
    if (overlay.id.isNull())
        return QStringLiteral("Das Overlay hat keine Kennung.");
    if (const QString e = validateOverlayName(overlay.name); !e.isEmpty())
        return e;
    if (const QString e = validateAddress(overlay.address); !e.isEmpty())
        return e;
    if (const QString e = validateOverlaySize(overlay.width, overlay.height); !e.isEmpty())
        return e;
    QSet<QUuid> ids;
    for (qsizetype i = 0; i < overlay.sources.size(); ++i) {
        const Source &source = overlay.sources.at(i);
        if (const QString e = validateSource(source); !e.isEmpty())
            return QStringLiteral("Quelle %1: %2").arg(i + 1).arg(e);
        if (ids.contains(source.id))
            return QStringLiteral("Quelle %1: Kennung %2 kommt mehrfach vor.")
                .arg(i + 1).arg(source.id.toString(QUuid::WithoutBraces));
        ids.insert(source.id);
    }
    return {};
}

int sourceIndex(const Overlay &overlay, const QUuid &sourceId)
{
    for (qsizetype i = 0; i < overlay.sources.size(); ++i) {
        if (overlay.sources.at(i).id == sourceId)
            return int(i);
    }
    return -1;
}

static QString unknownSource(const QUuid &sourceId)
{
    return QStringLiteral("Quelle %1 nicht gefunden.").arg(sourceId.toString(QUuid::WithoutBraces));
}

QString addSource(Overlay *overlay, const Source &source)
{
    if (const QString e = validateSource(source); !e.isEmpty())
        return e;
    if (sourceIndex(*overlay, source.id) >= 0)
        return QStringLiteral("Kennung %1 ist schon vergeben.")
            .arg(source.id.toString(QUuid::WithoutBraces));
    overlay->sources.append(source); // neue Quelle liegt zuoberst
    return {};
}

QString removeSource(Overlay *overlay, const QUuid &sourceId)
{
    const int index = sourceIndex(*overlay, sourceId);
    if (index < 0)
        return unknownSource(sourceId);
    overlay->sources.removeAt(index);
    return {};
}

QString moveSourceForward(Overlay *overlay, const QUuid &sourceId)
{
    const int index = sourceIndex(*overlay, sourceId);
    if (index < 0)
        return unknownSource(sourceId);
    if (index == overlay->sources.size() - 1)
        return QStringLiteral("Die Quelle liegt bereits ganz vorne.");
    overlay->sources.swapItemsAt(index, index + 1);
    return {};
}

QString moveSourceBackward(Overlay *overlay, const QUuid &sourceId)
{
    const int index = sourceIndex(*overlay, sourceId);
    if (index < 0)
        return unknownSource(sourceId);
    if (index == 0)
        return QStringLiteral("Die Quelle liegt bereits ganz hinten.");
    overlay->sources.swapItemsAt(index, index - 1);
    return {};
}

QString setSourceVisible(Overlay *overlay, const QUuid &sourceId, bool visible)
{
    const int index = sourceIndex(*overlay, sourceId);
    if (index < 0)
        return unknownSource(sourceId);
    overlay->sources[index].visible = visible;
    return {};
}

QString setSourceGeometry(Overlay *overlay, const QUuid &sourceId, int x, int y, int width,
                          int height, int rotation)
{
    const int index = sourceIndex(*overlay, sourceId);
    if (index < 0)
        return unknownSource(sourceId);
    if (const QString e = validateSourceGeometry(width, height, rotation); !e.isEmpty())
        return e;
    Source &source = overlay->sources[index];
    source.x = x;
    source.y = y;
    source.width = width;
    source.height = height;
    source.rotation = rotation;
    return {};
}

QString rotationDisplay(int tenthDegrees)
{
    return QStringLiteral("%1.%2 °").arg(tenthDegrees / 10).arg(tenthDegrees % 10);
}

} // namespace dss
