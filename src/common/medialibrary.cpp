// Quellen:
//   QJsonDocument: https://doc.qt.io/qt-6.8/qjsondocument.html
//   QFileInfo::lastModified: https://doc.qt.io/qt-6.8/qfileinfo.html#lastModified
//   cp -a behält die Zeit der letzten Änderung (--preserve=…,timestamps):
//     https://www.gnu.org/software/coreutils/manual/html_node/cp-invocation.html

#include "medialibrary.h"

#include "mediaimport.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace dss {

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

// UUID oder null (oberste Ebene)
bool readParent(const QJsonValue &v, QUuid *id)
{
    if (v.isNull()) {
        *id = QUuid();
        return true;
    }
    return readUuid(v, id);
}

QJsonValue parentValue(const QUuid &id)
{
    return id.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(idText(id));
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

// Verweis in der Form von K-03 mit einer Endung, die das Programm übernimmt.
bool isMediaReference(const QString &reference)
{
    return validateMediaReference(reference).isEmpty() && mediaKindOf(reference).has_value();
}

QString unknownFile(const QString &file)
{
    return QStringLiteral("Datei %1 ist nicht in der Medienliste.").arg(file);
}

QString unknownFolder(const QUuid &id)
{
    return QStringLiteral("Ordner %1 nicht gefunden.").arg(idText(id));
}

int fileIndex(const MediaLibrary &library, const QString &file)
{
    for (qsizetype i = 0; i < library.files.size(); ++i) {
        if (library.files.at(i).file == file)
            return int(i);
    }
    return -1;
}

int folderIndex(const MediaLibrary &library, const QUuid &id)
{
    for (qsizetype i = 0; i < library.folders.size(); ++i) {
        if (library.folders.at(i).id == id)
            return int(i);
    }
    return -1;
}

QString checkTargetFolder(const MediaLibrary &library, const QUuid &folder)
{
    if (!folder.isNull() && !library.folder(folder))
        return unknownFolder(folder);
    return {};
}

} // namespace

std::optional<MediaKind> mediaKindOf(const QString &reference)
{
    const QString suffix = QFileInfo(reference).suffix().toLower();
    for (MediaKind kind : {MediaKind::Image, MediaKind::Video, MediaKind::Font}) {
        const MediaUse use = kind == MediaKind::Image   ? MediaUse::Image
                             : kind == MediaKind::Video ? MediaUse::Video
                                                        : MediaUse::Font;
        if (allowedMediaSuffixes(use).contains(suffix))
            return kind;
    }
    return std::nullopt;
}

QString mediaKindName(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Image:
        return QStringLiteral("Bild");
    case MediaKind::Video:
        return QStringLiteral("Video");
    case MediaKind::Font:
        return QStringLiteral("Schrift");
    }
    return {};
}

QString mediaKindKey(MediaKind kind)
{
    switch (kind) {
    case MediaKind::Image:
        return QStringLiteral("image");
    case MediaKind::Video:
        return QStringLiteral("video");
    case MediaKind::Font:
        return QStringLiteral("font");
    }
    return {};
}

std::optional<MediaKind> mediaKindFromKey(const QString &key)
{
    for (MediaKind kind : {MediaKind::Image, MediaKind::Video, MediaKind::Font}) {
        if (mediaKindKey(kind) == key)
            return kind;
    }
    return std::nullopt;
}

const MediaFolder *MediaLibrary::folder(const QUuid &id) const
{
    const int index = folderIndex(*this, id);
    return id.isNull() || index < 0 ? nullptr : &folders.at(index);
}

const MediaEntry *MediaLibrary::entry(const QString &file) const
{
    const int index = fileIndex(*this, file);
    return index < 0 ? nullptr : &files.at(index);
}

QList<QUuid> MediaLibrary::descendants(const QUuid &id) const
{
    QList<QUuid> result;
    QList<QUuid> open{id};
    while (!open.isEmpty()) {
        const QUuid current = open.takeFirst();
        for (const MediaFolder &f : folders) {
            if (f.parent == current && !f.parent.isNull() && !result.contains(f.id)) {
                result.append(f.id);
                open.append(f.id);
            }
        }
    }
    return result;
}

QByteArray serializeMediaLibrary(const MediaLibrary &library)
{
    QJsonArray folders;
    for (const MediaFolder &f : library.folders)
        folders.append(QJsonObject{{QStringLiteral("id"), idText(f.id)},
                                   {QStringLiteral("name"), f.name},
                                   {QStringLiteral("parent"), parentValue(f.parent)},
                                   {QStringLiteral("expanded"), f.expanded}});
    QJsonArray files;
    for (const MediaEntry &e : library.files)
        files.append(QJsonObject{{QStringLiteral("file"), e.file},
                                 {QStringLiteral("name"), e.name},
                                 {QStringLiteral("folder"), parentValue(e.folder)}});
    const QJsonObject root{{QStringLiteral("format_version"), kMediaLibraryFormatVersion},
                           {QStringLiteral("folders"), folders},
                           {QStringLiteral("files"), files}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool parseMediaLibrary(const QByteArray &data, MediaLibrary *library, QString *error)
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
    const QJsonObject object = doc.object();
    const QJsonValue version = object.value(QStringLiteral("format_version"));
    if (!version.isDouble() || std::trunc(version.toDouble()) != version.toDouble()) {
        *error = QStringLiteral("Feld «format_version»: fehlt oder ist keine ganze Zahl");
        return false;
    }
    if (version.toInt() != kMediaLibraryFormatVersion) {
        *error = QStringLiteral("format_version %1 ist unbekannt, erwartet %2.")
                     .arg(version.toDouble()).arg(kMediaLibraryFormatVersion);
        return false;
    }
    if (!onlyKeys(object, {QStringLiteral("format_version"), QStringLiteral("folders"),
                           QStringLiteral("files")}, QString(), error))
        return false;
    const QJsonValue foldersValue = object.value(QStringLiteral("folders"));
    const QJsonValue filesValue = object.value(QStringLiteral("files"));
    if (!foldersValue.isArray()) {
        *error = QStringLiteral("Feld «folders»: muss eine Liste sein");
        return false;
    }
    if (!filesValue.isArray()) {
        *error = QStringLiteral("Feld «files»: muss eine Liste sein");
        return false;
    }

    MediaLibrary result;
    QSet<QUuid> folderIds;
    const QJsonArray folders = foldersValue.toArray();
    for (qsizetype i = 0; i < folders.size(); ++i) {
        const QString path = QStringLiteral("folders[%1].").arg(i);
        if (!folders.at(i).isObject()) {
            *error = QStringLiteral("Feld «folders[%1]»: muss ein Objekt sein").arg(i);
            return false;
        }
        const QJsonObject o = folders.at(i).toObject();
        if (!onlyKeys(o, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("parent"),
                          QStringLiteral("expanded")}, path, error))
            return false;
        MediaFolder f;
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
        if (!name.isString() || !validateMediaName(name.toString()).isEmpty()) {
            *error = QStringLiteral("Feld «%1name»: muss ein nicht leerer Text sein").arg(path);
            return false;
        }
        f.name = name.toString();
        if (!readParent(o.value(QStringLiteral("parent")), &f.parent)) {
            *error = QStringLiteral("Feld «%1parent»: muss null oder die UUID eines Ordners sein")
                         .arg(path);
            return false;
        }
        const QJsonValue expanded = o.value(QStringLiteral("expanded"));
        if (!expanded.isBool()) {
            *error = QStringLiteral("Feld «%1expanded»: muss true oder false sein").arg(path);
            return false;
        }
        f.expanded = expanded.toBool();
        result.folders.append(f);
    }
    // Eltern vorhanden und keine Schleifen: Von jedem Ordner aus führt die Kette der Eltern nach
    // höchstens so vielen Schritten, wie es Ordner gibt, auf die oberste Ebene.
    for (qsizetype i = 0; i < result.folders.size(); ++i) {
        QUuid current = result.folders.at(i).parent;
        for (qsizetype steps = 0; !current.isNull(); ++steps) {
            const MediaFolder *parent = result.folder(current);
            if (!parent) {
                *error = QStringLiteral("Feld «folders[%1].parent»: Ordner %2 gibt es nicht")
                             .arg(i).arg(idText(current));
                return false;
            }
            if (steps >= result.folders.size()) {
                *error = QStringLiteral("Feld «folders[%1].parent»: Ordner enthalten sich im Kreis")
                             .arg(i);
                return false;
            }
            current = parent->parent;
        }
    }

    QSet<QString> fileNames;
    const QJsonArray files = filesValue.toArray();
    for (qsizetype i = 0; i < files.size(); ++i) {
        const QString path = QStringLiteral("files[%1].").arg(i);
        if (!files.at(i).isObject()) {
            *error = QStringLiteral("Feld «files[%1]»: muss ein Objekt sein").arg(i);
            return false;
        }
        const QJsonObject o = files.at(i).toObject();
        if (!onlyKeys(o, {QStringLiteral("file"), QStringLiteral("name"), QStringLiteral("folder")},
                      path, error))
            return false;
        MediaEntry e;
        const QJsonValue file = o.value(QStringLiteral("file"));
        if (!file.isString() || !isMediaReference(file.toString())) {
            *error = QStringLiteral("Feld «%1file»: muss ein Verweis media/<SHA-256>.<Endung> sein")
                         .arg(path);
            return false;
        }
        e.file = file.toString();
        if (fileNames.contains(e.file)) {
            *error = QStringLiteral("Feld «%1file»: Datei kommt mehrfach vor").arg(path);
            return false;
        }
        fileNames.insert(e.file);
        const QJsonValue name = o.value(QStringLiteral("name"));
        if (!name.isString() || !validateMediaName(name.toString()).isEmpty()) {
            *error = QStringLiteral("Feld «%1name»: muss ein nicht leerer Text sein").arg(path);
            return false;
        }
        e.name = name.toString();
        if (!readParent(o.value(QStringLiteral("folder")), &e.folder)) {
            *error = QStringLiteral("Feld «%1folder»: muss null oder die UUID eines Ordners sein")
                         .arg(path);
            return false;
        }
        if (!e.folder.isNull() && !result.folder(e.folder)) {
            *error = QStringLiteral("Feld «%1folder»: Ordner %2 gibt es nicht")
                         .arg(path, idText(e.folder));
            return false;
        }
        result.files.append(e);
    }
    *library = result;
    return true;
}

QList<MediaFileInfo> scanMediaDir(const QString &mediaDir)
{
    QList<MediaFileInfo> list;
    const QDir dir(mediaDir);
    const QFileInfoList entries =
        dir.entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const QFileInfo &info : entries) {
        const QString reference = QStringLiteral("media/") + info.fileName();
        if (!isMediaReference(reference))
            continue;
        list.append({reference, info.lastModified(), info.size()});
    }
    return list;
}

QString substituteMediaName(MediaKind kind, const QDate &date)
{
    return QStringLiteral("%1 vom %2").arg(mediaKindName(kind), date.toString(Qt::ISODate));
}

bool reconcileMediaLibrary(MediaLibrary *library, const QList<MediaFileInfo> &present,
                           QStringList *added, QStringList *dropped)
{
    bool changed = false;
    QSet<QString> presentFiles;
    for (const MediaFileInfo &f : present)
        presentFiles.insert(f.reference);
    for (qsizetype i = library->files.size() - 1; i >= 0; --i) {
        if (!presentFiles.contains(library->files.at(i).file)) {
            if (dropped)
                dropped->prepend(library->files.at(i).file);
            library->files.removeAt(i);
            changed = true;
        }
    }
    // Neue Dateien in der Reihenfolge des Datums, bei gleichem Datum nach Verweis (stabil).
    QList<MediaFileInfo> missing;
    for (const MediaFileInfo &f : present) {
        if (!library->entry(f.reference))
            missing.append(f);
    }
    std::sort(missing.begin(), missing.end(), [](const MediaFileInfo &a, const MediaFileInfo &b) {
        return a.modified != b.modified ? a.modified < b.modified : a.reference < b.reference;
    });
    QSet<QString> takenNames;
    for (const MediaEntry &e : library->files)
        takenNames.insert(e.name);
    for (const MediaFileInfo &f : missing) {
        const QString base = substituteMediaName(*mediaKindOf(f.reference),
                                                 f.modified.toLocalTime().date());
        QString name = base;
        for (int n = 2; takenNames.contains(name); ++n)
            name = QStringLiteral("%1 (%2)").arg(base).arg(n);
        takenNames.insert(name);
        library->files.append({f.reference, name, QUuid()});
        if (added)
            added->append(f.reference);
        changed = true;
    }
    return changed;
}

QString validateMediaName(const QString &name)
{
    if (name.trimmed().isEmpty())
        return QStringLiteral("Der Name darf nicht leer sein.");
    return {};
}

QString addMediaToLibrary(MediaLibrary *library, const QString &file, const QString &name,
                          const QUuid &folder)
{
    if (!isMediaReference(file))
        return QStringLiteral("Verweis «%1» ungültig.").arg(file);
    if (library->entry(file))
        return {}; // gleicher Inhalt: Name und Ordner bleiben
    if (const QString e = validateMediaName(name); !e.isEmpty())
        return e;
    if (const QString e = checkTargetFolder(*library, folder); !e.isEmpty())
        return e;
    library->files.append({file, name, folder});
    return {};
}

QString renameMediaInLibrary(MediaLibrary *library, const QString &file, const QString &name)
{
    const int index = fileIndex(*library, file);
    if (index < 0)
        return unknownFile(file);
    if (const QString e = validateMediaName(name); !e.isEmpty())
        return e;
    library->files[index].name = name;
    return {};
}

QString moveMediaFileInLibrary(MediaLibrary *library, const QString &file, const QUuid &folder)
{
    const int index = fileIndex(*library, file);
    if (index < 0)
        return unknownFile(file);
    if (const QString e = checkTargetFolder(*library, folder); !e.isEmpty())
        return e;
    library->files[index].folder = folder;
    return {};
}

QString removeMediaFromLibrary(MediaLibrary *library, const QString &file)
{
    const int index = fileIndex(*library, file);
    if (index < 0)
        return unknownFile(file);
    library->files.removeAt(index);
    return {};
}

QString createMediaFolderInLibrary(MediaLibrary *library, const QUuid &id, const QString &name,
                                   const QUuid &parent)
{
    if (id.isNull() || folderIndex(*library, id) >= 0)
        return QStringLiteral("Kennung %1 für den neuen Ordner ungültig.").arg(idText(id));
    if (const QString e = validateMediaName(name); !e.isEmpty())
        return e;
    if (const QString e = checkTargetFolder(*library, parent); !e.isEmpty())
        return e;
    library->folders.append({id, name, parent, true});
    return {};
}

QString renameMediaFolderInLibrary(MediaLibrary *library, const QUuid &id, const QString &name)
{
    const int index = folderIndex(*library, id);
    if (id.isNull() || index < 0)
        return unknownFolder(id);
    if (const QString e = validateMediaName(name); !e.isEmpty())
        return e;
    library->folders[index].name = name;
    return {};
}

QString removeMediaFolderFromLibrary(MediaLibrary *library, const QUuid &id)
{
    const int index = folderIndex(*library, id);
    if (id.isNull() || index < 0)
        return unknownFolder(id);
    const QUuid parent = library->folders.at(index).parent;
    for (MediaFolder &f : library->folders) {
        if (f.parent == id)
            f.parent = parent;
    }
    for (MediaEntry &e : library->files) {
        if (e.folder == id)
            e.folder = parent;
    }
    library->folders.removeAt(index);
    return {};
}

QString moveMediaFolderInLibrary(MediaLibrary *library, const QUuid &id, const QUuid &parent)
{
    const int index = folderIndex(*library, id);
    if (id.isNull() || index < 0)
        return unknownFolder(id);
    if (const QString e = checkTargetFolder(*library, parent); !e.isEmpty())
        return e;
    if (parent == id || library->descendants(id).contains(parent))
        return QStringLiteral("Ein Ordner lässt sich nicht in sich selbst oder einen seiner "
                              "Unterordner verschieben.");
    library->folders[index].parent = parent;
    return {};
}

QString setMediaFolderExpandedInLibrary(MediaLibrary *library, const QUuid &id, bool expanded)
{
    const int index = folderIndex(*library, id);
    if (id.isNull() || index < 0)
        return unknownFolder(id);
    library->folders[index].expanded = expanded;
    return {};
}

QString mediaSizeText(qint64 bytes)
{
    if (bytes < 1024)
        return QStringLiteral("%1 Byte").arg(bytes);
    const char *units[] = {"KiB", "MiB", "GiB", "TiB"};
    double value = double(bytes);
    int unit = -1;
    while (value >= 1024 && unit < 3) {
        value /= 1024;
        ++unit;
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(QLatin1String(units[unit]));
}

QString mediaFolderPath(const MediaLibrary &library, const QUuid &folder)
{
    QStringList parts;
    QUuid current = folder;
    // Höchstens so viele Schritte, wie es Ordner gibt (gültige Dateien haben keine Schleifen).
    for (qsizetype steps = 0; !current.isNull() && steps <= library.folders.size(); ++steps) {
        const MediaFolder *f = library.folder(current);
        if (!f)
            break;
        parts.prepend(f->name);
        current = f->parent;
    }
    return parts.join(QLatin1Char('/'));
}

std::optional<QUuid> findMediaFolder(const MediaLibrary &library, const QString &path,
                                     QString *error)
{
    const QString wanted = path.trimmed();
    if (wanted.isEmpty() || wanted == QLatin1String("/"))
        return QUuid();
    QList<QUuid> found;
    for (const MediaFolder &f : library.folders) {
        if (mediaFolderPath(library, f.id) == wanted)
            found.append(f.id);
    }
    if (found.size() == 1)
        return found.first();
    *error = found.isEmpty() ? QStringLiteral("Ordner «%1» gibt es nicht.").arg(wanted)
                             : QStringLiteral("Ordner «%1» gibt es %2 Mal; zuerst einen umbenennen.")
                                   .arg(wanted).arg(found.size());
    return std::nullopt;
}

QList<MediaEntry> mediaFilesInOrder(const MediaLibrary &library)
{
    QList<MediaEntry> files = library.files;
    QHash<QUuid, QString> paths;
    for (const MediaEntry &e : files)
        paths.insert(e.folder, mediaFolderPath(library, e.folder));
    std::stable_sort(files.begin(), files.end(), [&paths](const MediaEntry &a, const MediaEntry &b) {
        const int folder = paths.value(a.folder).compare(paths.value(b.folder), Qt::CaseInsensitive);
        if (folder != 0)
            return folder < 0;
        const int name = a.name.compare(b.name, Qt::CaseInsensitive);
        return name != 0 ? name < 0 : a.file < b.file;
    });
    return files;
}

QString findMediaFile(const MediaLibrary &library, const QString &text, QString *error)
{
    const QList<MediaEntry> files = mediaFilesInOrder(library);
    bool isNumber = false;
    const int number = text.toInt(&isNumber);
    if (isNumber) {
        if (number >= 1 && number <= files.size())
            return files.at(number - 1).file;
        *error = QStringLiteral("Datei Nr. %1 gibt es nicht, die Medienliste hat %2 Datei(en).")
                     .arg(number).arg(files.size());
        return {};
    }
    if (text.startsWith(QLatin1String("media/"))) {
        if (library.entry(text))
            return text;
        *error = QStringLiteral("Datei %1 ist nicht in der Medienliste.").arg(text);
        return {};
    }
    QStringList found;
    for (const MediaEntry &e : files) {
        if (e.name == text)
            found.append(e.file);
    }
    if (found.size() == 1)
        return found.first();
    *error = found.isEmpty()
                 ? QStringLiteral("Keine Datei mit dem Namen «%1».").arg(text)
                 : QStringLiteral("Den Namen «%1» haben %2 Dateien; Nummer aus medien-liste oder "
                                  "Verweis media/… angeben.").arg(text).arg(found.size());
    return {};
}

MediaUsages mediaUsages(const QList<Overlay> &overlays)
{
    MediaUsages usages;
    for (const Overlay &o : overlays) {
        for (const Source &s : o.sources) {
            const QString file = s.kind == SourceKind::Text ? s.text.fontFile : s.file;
            usages[file].append({o.id, o.name, s.id, s.name});
        }
    }
    return usages;
}

QString usageText(const QList<MediaUsage> &usages)
{
    if (usages.isEmpty())
        return {};
    QStringList parts;
    QList<QUuid> order;
    QHash<QUuid, QStringList> sources;
    QHash<QUuid, QString> names;
    for (const MediaUsage &u : usages) {
        if (!order.contains(u.overlayId))
            order.append(u.overlayId);
        names.insert(u.overlayId, u.overlayName);
        sources[u.overlayId].append(QStringLiteral("«%1»").arg(u.sourceName));
    }
    for (const QUuid &id : order)
        parts.append(QStringLiteral("Overlay «%1» (Quelle %2)")
                         .arg(names.value(id), sources.value(id).join(QStringLiteral(", "))));
    return QStringLiteral("verwendet in ") + parts.join(QStringLiteral(", "));
}

} // namespace dss
