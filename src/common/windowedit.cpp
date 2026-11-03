#include "windowedit.h"

#include "address.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <map>

namespace dss {

QString newOverlayName()
{
    return QStringLiteral("Neues Overlay");
}

QString newFolderName()
{
    return QStringLiteral("Neuer Ordner");
}

QUuid firstOverlay(const OverlayLayout &layout)
{
    const QList<QUuid> ids = layout.overlayIds();
    return ids.isEmpty() ? QUuid() : ids.first();
}

bool renameChangesAddress(const QList<OverlaySummary> &overlays, const QUuid &overlayId,
                          const QString &newName, QString *newAddress)
{
    // Gleiche Rechnung wie OverlayStore::renameOverlay: Adresse aus dem Namen, eindeutig unter
    // den Adressen der anderen Overlays.
    QSet<QString> taken;
    QString current;
    for (const OverlaySummary &o : overlays) {
        if (o.id == overlayId)
            current = o.address;
        else
            taken.insert(o.address);
    }
    const QString address = uniqueAddress(addressFromName(newName), taken);
    if (newAddress)
        *newAddress = address;
    return address != current;
}

int sourceIndexForRow(const Overlay &overlay, int row)
{
    const int count = int(overlay.sources.size());
    if (row < 0 || row >= count)
        return -1;
    return count - 1 - row;
}

std::optional<int> sourceDropIndex(const Overlay &overlay, const QUuid &sourceId, int insertRow)
{
    const int count = int(overlay.sources.size());
    const int from = sourceIndex(overlay, sourceId);
    if (from < 0 || insertRow < 0 || insertRow > count)
        return std::nullopt;
    // Zeile der gezogenen Quelle; nach dem Herausnehmen rückt alles darunter eine Zeile hoch.
    const int fromRow = count - 1 - from;
    const int row = insertRow > fromRow ? insertRow - 1 : insertRow;
    // Unter den übrigen count-1 Zeilen an Zeile row: Stelle in der Zeichenreihenfolge.
    const int index = (count - 1) - row;
    if (index == from)
        return std::nullopt;
    return index;
}

std::optional<int> sourceStepIndex(const Overlay &overlay, const QUuid &sourceId, int step)
{
    const int from = sourceIndex(overlay, sourceId);
    const int to = from + step;
    if (from < 0 || step == 0 || to < 0 || to >= int(overlay.sources.size()))
        return std::nullopt;
    return to;
}

QString mediaFileFilter(MediaUse use)
{
    QStringList patterns;
    for (const QString &suffix : allowedMediaSuffixes(use))
        patterns.append(QStringLiteral("*.") + suffix);
    QString title;
    switch (use) {
    case MediaUse::Image:
        title = QStringLiteral("Bilder");
        break;
    case MediaUse::Video:
        title = QStringLiteral("Videos");
        break;
    case MediaUse::Font:
        title = QStringLiteral("Schriften");
        break;
    }
    return QStringLiteral("%1 (%2)").arg(title, patterns.join(QLatin1Char(' ')));
}

int rotationTenths(double degrees)
{
    const int tenths = int(std::lround(degrees * 10.0));
    return ((tenths % 3600) + 3600) % 3600;
}

QString colorText(int red, int green, int blue, int alpha)
{
    const auto part = [](int v) {
        return QStringLiteral("%1").arg(std::clamp(v, 0, 255), 2, 16, QLatin1Char('0')).toUpper();
    };
    return QLatin1Char('#') + part(red) + part(green) + part(blue) + part(alpha);
}

bool colorParts(const QString &color, int *red, int *green, int *blue, int *alpha)
{
    if (!validateColor(color).isEmpty())
        return false;
    const auto part = [&color](int i) { return color.mid(1 + 2 * i, 2).toInt(nullptr, 16); };
    *red = part(0);
    *green = part(1);
    *blue = part(2);
    *alpha = part(3);
    return true;
}

QString changeKey(const QUuid &sourceId, const QString &field)
{
    return sourceId.toString(QUuid::WithoutBraces) + QLatin1Char('/') + field;
}

QString overlaySizeKey(const QUuid &overlayId)
{
    return overlayId.toString(QUuid::WithoutBraces) + QStringLiteral("/size");
}

ChangeRequest sourceUpdate(const QUuid &overlayId, const QUuid &sourceId,
                           const SourceChanges &changes)
{
    ChangeRequest r;
    r.type = QString::fromLatin1(msg::kUpdateSource);
    r.overlayId = overlayId;
    r.sourceId = sourceId;
    r.changes = changes;
    return r;
}

QList<WeightChoice> weightChoices(const QList<FontFace> &faces, const QString &family)
{
    // Je Gewicht der Name des aufrechten Schnitts; gibt es nur einen kursiven, dessen Name ohne
    // den Teil für kursiv («Bold Italic» → «Bold», «Italic» → «Regular»).
    static const QRegularExpression italicWord(QStringLiteral("\\s*\\b(Italic|Oblique)\\b\\s*"),
                                               QRegularExpression::CaseInsensitiveOption);
    std::map<int, QString> upright;
    std::map<int, QString> italic;
    for (const FontFace &f : facesOfFamily(faces, family)) {
        auto &names = f.italic ? italic : upright;
        if (!names.contains(f.weight))
            names[f.weight] = f.style;
    }
    std::map<int, QString> labels = upright;
    for (const auto &[weight, style] : italic) {
        if (labels.contains(weight))
            continue;
        QString name = style;
        name.replace(italicWord, QStringLiteral(" "));
        name = name.simplified();
        labels[weight] = name.isEmpty() ? QStringLiteral("Regular") : name;
    }
    QList<WeightChoice> result;
    for (const auto &[weight, name] : labels)
        result.append({weight, QStringLiteral("%1 (%2)").arg(name).arg(weight)});
    return result;
}

bool italicSelectable(const QList<FontFace> &faces, const QString &family)
{
    bool upright = false;
    bool italic = false;
    for (const FontFace &f : facesOfFamily(faces, family)) {
        upright = upright || !f.italic;
        italic = italic || f.italic;
    }
    return upright && italic;
}

QString familyOfFont(const QList<FontFace> &faces, const QString &fontFile)
{
    const FontFace *face = fontFaceForFile(faces, fontFile);
    return face ? face->family : QString();
}

std::optional<ChangeRequest> fontChange(const QList<FontFace> &faces, const QUuid &overlayId,
                                        const QUuid &sourceId, const QString &family, int weight,
                                        bool italic)
{
    const FontFace *face = resolveFontFace(faces, family, weight, italic);
    if (!face)
        return std::nullopt;
    ChangeRequest r;
    r.type = QString::fromLatin1(msg::kSetTextFont);
    r.overlayId = overlayId;
    r.sourceId = sourceId;
    r.file = face->file;
    r.weight = face->weight;
    r.italic = face->italic;
    return r;
}

} // namespace dss
