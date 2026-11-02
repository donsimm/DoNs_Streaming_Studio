#include "overlaylist.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <cmath>

namespace dss {

QList<QUuid> OverlayLayout::overlayIds() const
{
    QList<QUuid> ids;
    for (const ListEntry &e : entries) {
        if (e.isFolder)
            ids.append(e.folder.overlays);
        else
            ids.append(e.overlayId);
    }
    return ids;
}

int OverlayLayout::folderIndex(const QUuid &folderId) const
{
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (entries.at(i).isFolder && entries.at(i).folder.id == folderId)
            return int(i);
    }
    return -1;
}

const ListFolder *OverlayLayout::folder(const QUuid &folderId) const
{
    const int i = folderIndex(folderId);
    return i < 0 ? nullptr : &entries.at(i).folder;
}

bool OverlayLayout::findOverlay(const QUuid &overlayId, QUuid *folderId, int *index) const
{
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const ListEntry &e = entries.at(i);
        if (!e.isFolder && e.overlayId == overlayId) {
            *folderId = QUuid();
            *index = int(i);
            return true;
        }
        if (e.isFolder) {
            const qsizetype j = e.folder.overlays.indexOf(overlayId);
            if (j >= 0) {
                *folderId = e.folder.id;
                *index = int(j);
                return true;
            }
        }
    }
    return false;
}

OverlayLayout layoutFromIds(const QList<QUuid> &overlayIds)
{
    OverlayLayout layout;
    for (const QUuid &id : overlayIds) {
        ListEntry e;
        e.overlayId = id;
        layout.entries.append(e);
    }
    return layout;
}

bool normalizeLayout(OverlayLayout *layout, const QList<QUuid> &existing)
{
    const QSet<QUuid> known(existing.cbegin(), existing.cend());
    bool changed = false;
    for (qsizetype i = layout->entries.size() - 1; i >= 0; --i) {
        ListEntry &e = layout->entries[i];
        if (e.isFolder) {
            changed |= e.folder.overlays.removeIf([&known](const QUuid &id) {
                return !known.contains(id);
            }) > 0;
        } else if (!known.contains(e.overlayId)) {
            layout->entries.removeAt(i);
            changed = true;
        }
    }
    const QList<QUuid> listed = layout->overlayIds();
    const QSet<QUuid> present(listed.cbegin(), listed.cend());
    for (const QUuid &id : existing) {
        if (!present.contains(id)) {
            ListEntry e;
            e.overlayId = id;
            layout->entries.append(e);
            changed = true;
        }
    }
    return changed;
}

namespace {

QString idText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

// Kennung in der Schreibweise der Overlay-Dateien (Kleinbuchstaben, ohne Klammern).
bool readUuid(const QJsonValue &v, QUuid *id)
{
    if (!v.isString())
        return false;
    const QUuid parsed = QUuid::fromString(v.toString());
    if (parsed.isNull() || idText(parsed) != v.toString())
        return false;
    *id = parsed;
    return true;
}

bool onlyKeys(const QJsonObject &o, const QStringList &keys, const QString &path, QString *error)
{
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!keys.contains(it.key())) {
            *error = QStringLiteral("Feld «%1%2»: unbekanntes Feld").arg(path, it.key());
            return false;
        }
    }
    for (const QString &key : keys) {
        if (!o.contains(key)) {
            *error = QStringLiteral("Feld «%1%2»: fehlt").arg(path, key);
            return false;
        }
    }
    return true;
}

} // namespace

QJsonObject layoutToJson(const OverlayLayout &layout)
{
    QJsonArray entries;
    for (const ListEntry &e : layout.entries) {
        if (!e.isFolder) {
            entries.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("overlay")},
                                       {QStringLiteral("id"), idText(e.overlayId)}});
            continue;
        }
        QJsonArray overlays;
        for (const QUuid &id : e.folder.overlays)
            overlays.append(idText(id));
        entries.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("folder")},
                                   {QStringLiteral("id"), idText(e.folder.id)},
                                   {QStringLiteral("name"), e.folder.name},
                                   {QStringLiteral("expanded"), e.folder.expanded},
                                   {QStringLiteral("overlays"), overlays}});
    }
    return QJsonObject{{QStringLiteral("format_version"), kListFormatVersion},
                       {QStringLiteral("entries"), entries}};
}

bool layoutFromJson(const QJsonObject &object, OverlayLayout *layout, QString *error)
{
    const QJsonValue version = object.value(QStringLiteral("format_version"));
    if (!version.isDouble() || std::trunc(version.toDouble()) != version.toDouble()) {
        *error = QStringLiteral("Feld «format_version»: fehlt oder ist keine ganze Zahl");
        return false;
    }
    if (version.toInt() != kListFormatVersion) {
        *error = QStringLiteral("format_version %1 ist unbekannt, erwartet %2.")
                     .arg(version.toDouble()).arg(kListFormatVersion);
        return false;
    }
    if (!onlyKeys(object, {QStringLiteral("format_version"), QStringLiteral("entries")}, QString(),
                  error))
        return false;
    const QJsonValue entriesValue = object.value(QStringLiteral("entries"));
    if (!entriesValue.isArray()) {
        *error = QStringLiteral("Feld «entries»: muss eine Liste sein");
        return false;
    }
    OverlayLayout result;
    QSet<QUuid> overlayIds;
    QSet<QUuid> folderIds;
    const QJsonArray entries = entriesValue.toArray();
    for (qsizetype i = 0; i < entries.size(); ++i) {
        const QString path = QStringLiteral("entries[%1].").arg(i);
        if (!entries.at(i).isObject()) {
            *error = QStringLiteral("Feld «entries[%1]»: muss ein Objekt sein").arg(i);
            return false;
        }
        const QJsonObject o = entries.at(i).toObject();
        const QString kind = o.value(QStringLiteral("kind")).toString();
        ListEntry entry;
        if (kind == QLatin1String("overlay")) {
            if (!onlyKeys(o, {QStringLiteral("kind"), QStringLiteral("id")}, path, error))
                return false;
            if (!readUuid(o.value(QStringLiteral("id")), &entry.overlayId)) {
                *error = QStringLiteral("Feld «%1id»: muss eine UUID in der Form xxxxxxxx-xxxx-… sein")
                             .arg(path);
                return false;
            }
            if (overlayIds.contains(entry.overlayId)) {
                *error = QStringLiteral("Feld «%1id»: Overlay kommt mehrfach vor").arg(path);
                return false;
            }
            overlayIds.insert(entry.overlayId);
        } else if (kind == QLatin1String("folder")) {
            entry.isFolder = true;
            if (!onlyKeys(o, {QStringLiteral("kind"), QStringLiteral("id"), QStringLiteral("name"),
                              QStringLiteral("expanded"), QStringLiteral("overlays")},
                          path, error))
                return false;
            ListFolder &f = entry.folder;
            if (!readUuid(o.value(QStringLiteral("id")), &f.id)) {
                *error = QStringLiteral("Feld «%1id»: muss eine UUID in der Form xxxxxxxx-xxxx-… sein")
                             .arg(path);
                return false;
            }
            if (folderIds.contains(f.id)) {
                *error = QStringLiteral("Feld «%1id»: Ordner kommt mehrfach vor").arg(path);
                return false;
            }
            folderIds.insert(f.id);
            const QJsonValue name = o.value(QStringLiteral("name"));
            if (!name.isString() || !validateFolderName(name.toString()).isEmpty()) {
                *error = QStringLiteral("Feld «%1name»: muss ein nicht leerer Text sein").arg(path);
                return false;
            }
            f.name = name.toString();
            const QJsonValue expanded = o.value(QStringLiteral("expanded"));
            if (!expanded.isBool()) {
                *error = QStringLiteral("Feld «%1expanded»: muss true oder false sein").arg(path);
                return false;
            }
            f.expanded = expanded.toBool();
            const QJsonValue overlays = o.value(QStringLiteral("overlays"));
            if (!overlays.isArray()) {
                *error = QStringLiteral("Feld «%1overlays»: muss eine Liste sein").arg(path);
                return false;
            }
            const QJsonArray list = overlays.toArray();
            for (qsizetype j = 0; j < list.size(); ++j) {
                QUuid id;
                // Nur Kennungen von Overlays: Ein Ordner im Ordner ist im Format nicht möglich.
                if (!readUuid(list.at(j), &id)) {
                    *error = QStringLiteral("Feld «%1overlays[%2]»: muss die UUID eines Overlays sein")
                                 .arg(path).arg(j);
                    return false;
                }
                if (overlayIds.contains(id)) {
                    *error = QStringLiteral("Feld «%1overlays[%2]»: Overlay kommt mehrfach vor")
                                 .arg(path).arg(j);
                    return false;
                }
                overlayIds.insert(id);
                f.overlays.append(id);
            }
        } else {
            *error = QStringLiteral("Feld «%1kind»: unbekannt «%2», erlaubt sind overlay, folder")
                         .arg(path, kind);
            return false;
        }
        result.entries.append(entry);
    }
    if (const QSet<QUuid> both = overlayIds & folderIds; !both.isEmpty()) {
        *error = QStringLiteral("Kennung %1 ist zugleich Overlay und Ordner").arg(idText(*both.begin()));
        return false;
    }
    *layout = result;
    return true;
}

QByteArray serializeLayout(const OverlayLayout &layout)
{
    return QJsonDocument(layoutToJson(layout)).toJson(QJsonDocument::Indented);
}

bool parseLayout(const QByteArray &data, OverlayLayout *layout, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("kein gültiges JSON (%1 bei Zeichen %2)")
                     .arg(parseError.errorString()).arg(parseError.offset);
        return false;
    }
    if (!doc.isObject()) {
        *error = QStringLiteral("kein JSON-Objekt");
        return false;
    }
    return layoutFromJson(doc.object(), layout, error);
}

QString validateFolderName(const QString &name)
{
    if (name.trimmed().isEmpty())
        return QStringLiteral("Der Name des Ordners darf nicht leer sein.");
    return {};
}

namespace {

QString unknownOverlay(const QUuid &id)
{
    return QStringLiteral("Overlay %1 nicht in der Liste.").arg(idText(id));
}

QString unknownFolder(const QUuid &id)
{
    return QStringLiteral("Ordner %1 nicht gefunden.").arg(idText(id));
}

ListEntry overlayEntry(const QUuid &id)
{
    ListEntry e;
    e.overlayId = id;
    return e;
}

} // namespace

QString addOverlayToLayout(OverlayLayout *layout, const QUuid &overlayId, const QUuid &after)
{
    QUuid folder;
    int index = 0;
    if (layout->findOverlay(overlayId, &folder, &index))
        return QStringLiteral("Overlay %1 ist schon in der Liste.").arg(idText(overlayId));
    if (!after.isNull() && layout->findOverlay(after, &folder, &index)) {
        if (folder.isNull())
            layout->entries.insert(index + 1, overlayEntry(overlayId));
        else
            layout->entries[layout->folderIndex(folder)].folder.overlays.insert(index + 1, overlayId);
        return {};
    }
    layout->entries.append(overlayEntry(overlayId));
    return {};
}

QString removeOverlayFromLayout(OverlayLayout *layout, const QUuid &overlayId)
{
    QUuid folder;
    int index = 0;
    if (!layout->findOverlay(overlayId, &folder, &index))
        return unknownOverlay(overlayId);
    if (folder.isNull())
        layout->entries.removeAt(index);
    else
        layout->entries[layout->folderIndex(folder)].folder.overlays.removeAt(index);
    return {};
}

QString moveOverlayInLayout(OverlayLayout *layout, const QUuid &overlayId, const QUuid &folderId,
                            int index)
{
    OverlayLayout changed = *layout;
    if (const QString e = removeOverlayFromLayout(&changed, overlayId); !e.isEmpty())
        return e;
    if (folderId.isNull()) {
        if (index < 0 || index > changed.entries.size())
            return QStringLiteral("Stelle %1 ungültig, erlaubt sind 0 bis %2.")
                .arg(index).arg(changed.entries.size());
        changed.entries.insert(index, overlayEntry(overlayId));
    } else {
        const int fi = changed.folderIndex(folderId);
        if (fi < 0)
            return unknownFolder(folderId);
        QList<QUuid> &overlays = changed.entries[fi].folder.overlays;
        if (index < 0 || index > overlays.size())
            return QStringLiteral("Stelle %1 ungültig, erlaubt sind 0 bis %2.")
                .arg(index).arg(overlays.size());
        overlays.insert(index, overlayId);
    }
    *layout = changed;
    return {};
}

QString moveFolderInLayout(OverlayLayout *layout, const QUuid &folderId, int index)
{
    const int fi = layout->folderIndex(folderId);
    if (fi < 0)
        return unknownFolder(folderId);
    if (index < 0 || index >= layout->entries.size())
        return QStringLiteral("Stelle %1 ungültig, erlaubt sind 0 bis %2.")
            .arg(index).arg(layout->entries.size() - 1);
    layout->entries.move(fi, index);
    return {};
}

QString createFolderInLayout(OverlayLayout *layout, const QUuid &folderId, const QString &name)
{
    if (const QString e = validateFolderName(name); !e.isEmpty())
        return e;
    if (folderId.isNull() || layout->folderIndex(folderId) >= 0)
        return QStringLiteral("Kennung des Ordners fehlt oder ist schon vergeben.");
    ListEntry e;
    e.isFolder = true;
    e.folder.id = folderId;
    e.folder.name = name;
    e.folder.expanded = true;
    layout->entries.append(e);
    return {};
}

QString renameFolderInLayout(OverlayLayout *layout, const QUuid &folderId, const QString &name)
{
    const int fi = layout->folderIndex(folderId);
    if (fi < 0)
        return unknownFolder(folderId);
    if (const QString e = validateFolderName(name); !e.isEmpty())
        return e;
    layout->entries[fi].folder.name = name;
    return {};
}

QString removeFolderFromLayout(OverlayLayout *layout, const QUuid &folderId)
{
    const int fi = layout->folderIndex(folderId);
    if (fi < 0)
        return unknownFolder(folderId);
    const QList<QUuid> overlays = layout->entries.at(fi).folder.overlays;
    layout->entries.removeAt(fi);
    for (qsizetype i = 0; i < overlays.size(); ++i)
        layout->entries.insert(fi + i, overlayEntry(overlays.at(i)));
    return {};
}

QString setFolderExpandedInLayout(OverlayLayout *layout, const QUuid &folderId, bool expanded)
{
    const int fi = layout->folderIndex(folderId);
    if (fi < 0)
        return unknownFolder(folderId);
    layout->entries[fi].folder.expanded = expanded;
    return {};
}

bool overlayDropTarget(const OverlayLayout &layout, const QUuid &overlayId, const QUuid &target,
                       bool targetIsFolder, DropPosition position, QUuid *folderId, int *index)
{
    QUuid oldFolder;
    int oldIndex = 0;
    if (!layout.findOverlay(overlayId, &oldFolder, &oldIndex))
        return false;
    // Stellen zählen ohne das gezogene Overlay (wie moveOverlayInLayout).
    OverlayLayout rest = layout;
    removeOverlayFromLayout(&rest, overlayId);
    QUuid folder;
    int to = 0;
    if (position == DropPosition::EndOfList) {
        to = int(rest.entries.size());
    } else if (targetIsFolder) {
        const int fi = rest.folderIndex(target);
        if (fi < 0)
            return false;
        if (position == DropPosition::OnItem) {
            folder = target;
            to = int(rest.entries.at(fi).folder.overlays.size());
        } else {
            to = position == DropPosition::Above ? fi : fi + 1;
        }
    } else {
        if (target == overlayId)
            return false;
        int ti = 0;
        if (!rest.findOverlay(target, &folder, &ti))
            return false;
        to = position == DropPosition::Above ? ti : ti + 1;
    }
    if (folder == oldFolder && to == oldIndex)
        return false;
    *folderId = folder;
    *index = to;
    return true;
}

bool folderDropTarget(const OverlayLayout &layout, const QUuid &folderId, const QUuid &target,
                      bool targetIsFolder, DropPosition position, int *index)
{
    const int oldIndex = layout.folderIndex(folderId);
    if (oldIndex < 0)
        return false;
    OverlayLayout rest = layout;
    rest.entries.removeAt(oldIndex);
    int to = 0;
    if (position == DropPosition::EndOfList) {
        to = int(rest.entries.size());
    } else if (targetIsFolder) {
        if (target == folderId)
            return false;
        const int fi = rest.folderIndex(target);
        if (fi < 0)
            return false;
        to = position == DropPosition::Above ? fi : fi + 1;
    } else {
        QUuid parent;
        int ti = 0;
        if (!rest.findOverlay(target, &parent, &ti) || !parent.isNull())
            return false; // Ordner in Ordner ist nicht möglich
        to = position == DropPosition::Above ? ti : ti + 1;
    }
    if (to == oldIndex)
        return false;
    *index = to;
    return true;
}

} // namespace dss
