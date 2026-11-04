#include "windowedit.h"

#include "address.h"
#include "medialibrary.h"

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

int newSourceIndex(const Overlay &overlay, int insertRow)
{
    const int count = int(overlay.sources.size());
    return count - std::clamp(insertRow, 0, count);
}

QString mediaLibraryFileFilter()
{
    QStringList all;
    for (MediaUse use : {MediaUse::Image, MediaUse::Video, MediaUse::Font}) {
        for (const QString &suffix : allowedMediaSuffixes(use))
            all.append(QStringLiteral("*.") + suffix);
    }
    return QStringLiteral("Mediendateien (%1);;%2;;%3;;%4")
        .arg(all.join(QLatin1Char(' ')), mediaFileFilter(MediaUse::Image),
             mediaFileFilter(MediaUse::Video), mediaFileFilter(MediaUse::Font));
}

bool mediaRowBefore(const MediaSortKey &a, const MediaSortKey &b, MediaColumn column, bool ascending)
{
    if (a.folder != b.folder)
        return a.folder;
    const auto names = [&] { return QString::compare(a.name, b.name, Qt::CaseInsensitive); };
    int c = 0;
    switch (column) {
    case MediaColumn::Name:
        c = names();
        break;
    case MediaColumn::Kind:
        c = QString::compare(a.kind, b.kind, Qt::CaseInsensitive);
        if (c == 0)
            c = names();
        break;
    case MediaColumn::Size:
        c = a.bytes < b.bytes ? -1 : a.bytes > b.bytes ? 1 : 0;
        if (c == 0)
            c = names();
        break;
    }
    if (c != 0)
        return ascending ? c < 0 : c > 0;
    // Gleiche Werte: immer in derselben Folge, unabhängig von der Richtung
    return a.tieBreak < b.tieBreak;
}

std::optional<QUuid> mediaDropFolder(const MediaListData &media, const QUuid &draggedFolder,
                                     const QString &draggedFile, bool targetIsFolder,
                                     const QUuid &targetFolder, const QString &targetFile,
                                     DropPosition position)
{
    MediaLibrary library;
    library.folders = media.folders;
    const auto folderOfFile = [&](const QString &file) -> std::optional<QUuid> {
        const MediaFileData *f = media.file(file);
        if (!f)
            return std::nullopt;
        return f->entry.folder;
    };
    QUuid destination;
    if (position != DropPosition::EndOfList) {
        if (targetIsFolder) {
            const MediaFolder *f = library.folder(targetFolder);
            if (!f)
                return std::nullopt;
            destination = position == DropPosition::OnItem ? f->id : f->parent;
        } else {
            const std::optional<QUuid> folder = folderOfFile(targetFile);
            if (!folder)
                return std::nullopt;
            destination = *folder;
        }
    }
    if (!draggedFolder.isNull()) {
        const MediaFolder *f = library.folder(draggedFolder);
        if (!f || f->parent == destination || destination == f->id
            || library.descendants(f->id).contains(destination))
            return std::nullopt;
        return destination;
    }
    const std::optional<QUuid> current = folderOfFile(draggedFile);
    if (!current || *current == destination)
        return std::nullopt;
    return destination;
}

namespace {

QString capitalized(QString text)
{
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

} // namespace

MediaSelectionInfo mediaSelectionInfo(const MediaListData &media,
                                      const QList<ImportProgressData> &imports,
                                      const QString &file, const QUuid &folder, qint64 importId)
{
    MediaSelectionInfo info;
    info.removeToolTip = QStringLiteral("Datei oder Ordner löschen");
    if (importId > 0) {
        for (const ImportProgressData &i : imports) {
            if (i.importId != importId)
                continue;
            info.removable = true;
            info.removeToolTip = QStringLiteral("Übernehmen von «%1» abbrechen").arg(i.name);
            info.line = QStringLiteral("«%1» wird übernommen (%2 %).").arg(i.name).arg(i.percent);
        }
        return info;
    }
    MediaLibrary library;
    library.folders = media.folders;
    if (const MediaFolder *f = library.folder(folder)) {
        const QList<QUuid> inside = library.descendants(f->id) + QList<QUuid>{f->id};
        int files = 0;
        for (const MediaFileData &d : media.files)
            files += inside.contains(d.entry.folder) ? 1 : 0;
        info.removable = media.error.isEmpty();
        info.removeToolTip = QStringLiteral("Ordner «%1» löschen; der Inhalt kommt eine Ebene höher")
                                 .arg(f->name);
        info.line = QStringLiteral("Ordner «%1»: %2 Datei(en)").arg(mediaFolderPath(library, f->id)).arg(files);
        return info;
    }
    const MediaFileData *d = media.file(file);
    if (!d)
        return info;
    if (d->usedIn.isEmpty()) {
        info.removable = true;
        info.removeToolTip = QStringLiteral("«%1» löschen").arg(d->entry.name);
        info.line = QStringLiteral("Ungenutzt");
    } else {
        // Wie die Meldung des Kerns bei remove_media (Code in_use)
        info.removeToolTip = QStringLiteral("«%1» lässt sich nicht löschen, %2.")
                                 .arg(d->entry.name, usageText(d->usedIn));
        info.line = capitalized(usageText(d->usedIn));
    }
    if (!d->browserProblem.isEmpty())
        info.line += QStringLiteral(" · ") + d->browserProblem;
    return info;
}

QString mediaFileToolTip(const MediaFileData &file)
{
    QStringList lines{file.entry.name};
    QString kind = mediaKindName(file.kind) + QStringLiteral(" · ") + mediaSizeText(file.bytes);
    if (!file.codec.isEmpty())
        kind += QStringLiteral(" · Format ") + file.codec;
    lines << kind << file.entry.file;
    lines << (file.usedIn.isEmpty() ? QStringLiteral("Ungenutzt") : capitalized(usageText(file.usedIn)));
    if (!file.browserProblem.isEmpty())
        lines << file.browserProblem;
    return lines.join(QLatin1Char('\n'));
}

QString importProgressText(const ImportProgressData &import)
{
    return QStringLiteral("übernehme … %1 %").arg(import.percent);
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
