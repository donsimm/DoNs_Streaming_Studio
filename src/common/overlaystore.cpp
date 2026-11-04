#include "overlaystore.h"

#include "address.h"
#include "imageorientation.h"
#include "overlayjson.h"
#include "videoformat.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace dss {

QString defaultDataDir(Edition edition)
{
    // AppDataLocation hängt den Programmnamen an (<APPNAME>). Kern, Fenster und dss-cli
    // haben verschiedene Namen, sollen aber denselben Ordner nutzen; deshalb wird der Name
    // nur für diese Abfrage gesetzt. Ein Organisationsname ist nicht gesetzt.
    // https://doc.qt.io/qt-6.8/qstandardpaths.html
    const QString previous = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(appDirName(edition));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QCoreApplication::setApplicationName(previous);
    return dir;
}

OverlayStore::OverlayStore(const QString &dataDir)
    : m_dataDir(QDir(dataDir).absolutePath())
    , m_tokens(m_dataDir + QStringLiteral("/tokens.json"))
{
}

QString OverlayStore::overlaysDir() const
{
    return m_dataDir + QStringLiteral("/overlays");
}

QString OverlayStore::mediaDir() const
{
    return m_dataDir + QStringLiteral("/media");
}

QString OverlayStore::overlayFilePath(const QUuid &overlayId) const
{
    return overlaysDir() + QLatin1Char('/') + overlayId.toString(QUuid::WithoutBraces)
           + QStringLiteral(".json");
}

QString OverlayStore::mediaFilePath(const QString &reference) const
{
    return m_dataDir + QLatin1Char('/') + reference;
}

QString OverlayStore::layoutFilePath() const
{
    return m_dataDir + QLatin1Char('/') + QString::fromLatin1(kListFileName);
}

static QString relativeName(const QString &fileName)
{
    return QStringLiteral("overlays/") + fileName;
}

bool OverlayStore::load(QString *error, LoadMode mode)
{
    const bool readOnly = mode == LoadMode::ReadOnly;
    m_overlays.clear();
    m_pendingWrites.clear();
    m_loadErrors.clear();
    m_loadWarnings.clear();
    m_unreadableOverlayFiles.clear();
    for (const QString &dir : {overlaysDir(), mediaDir()}) {
        if (!readOnly && !QDir().mkpath(dir)) {
            *error = QStringLiteral("Ordner %1 kann nicht angelegt werden.").arg(dir);
            return false;
        }
    }

    QString tokenWarning;
    if (!m_tokens.load(error, &tokenWarning, readOnly))
        return false;
    if (!tokenWarning.isEmpty())
        m_loadWarnings.append(tokenWarning);

    const QDir dir(overlaysDir());
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        const QString name = relativeName(fileName);
        QFile file(dir.filePath(fileName));
        // Nicht geladene Dateien merken: Ihre Mediendateien dürfen nicht gelöscht werden.
        m_unreadableOverlayFiles.append(file.fileName());
        if (!file.open(QIODevice::ReadOnly)) {
            m_loadErrors.append(QStringLiteral("%1: kann nicht gelesen werden (%2)")
                                    .arg(name, file.errorString()));
            continue;
        }
        Overlay overlay;
        QString parseError;
        if (!parseOverlay(file.readAll(), &overlay, &parseError)) {
            m_loadErrors.append(name + QStringLiteral(": ") + parseError);
            continue;
        }
        if (fileName != overlay.id.toString(QUuid::WithoutBraces) + QStringLiteral(".json")) {
            m_loadErrors.append(QStringLiteral("%1: Dateiname passt nicht zur Kennung %2")
                                    .arg(name, overlay.id.toString(QUuid::WithoutBraces)));
            continue;
        }
        if (overlayByAddress(overlay.address)) {
            m_loadErrors.append(QStringLiteral("%1: Adresse «%2» ist schon vergeben")
                                    .arg(name, overlay.address));
            continue;
        }
        for (const Source &source : std::as_const(overlay.sources)) {
            const QString reference =
                source.kind == SourceKind::Text ? source.text.fontFile : source.file;
            if (!QFileInfo::exists(mediaFilePath(reference)))
                m_loadWarnings.append(QStringLiteral("%1: Quelle «%2» (%3): Datei %4 fehlt")
                                          .arg(name, source.name,
                                               source.id.toString(QUuid::WithoutBraces),
                                               reference));
        }
        if (!m_tokens.contains(overlay.id) && readOnly) {
            m_loadWarnings.append(QStringLiteral("%1: kein Token vorhanden (nur gelesen, keiner erzeugt)").arg(name));
        } else if (!m_tokens.contains(overlay.id)) {
            QString tokenError;
            if (m_tokens.createToken(overlay.id, &tokenError))
                m_loadWarnings.append(QStringLiteral("%1: kein Token vorhanden, neuer Token erzeugt").arg(name));
            else
                m_loadErrors.append(QStringLiteral("%1: kein Token vorhanden, %2").arg(name, tokenError));
        }
        m_unreadableOverlayFiles.removeLast(); // gelesen (auch ohne Token: Datei ist gültig)
        insertSorted(overlay);
    }
    loadLayout(); // liest nur, schreibt auch im Modus ReadWrite nicht
    loadMediaLibrary(); // ebenso
    return true;
}

QList<QUuid> OverlayStore::idsByName() const
{
    QList<QUuid> ids;
    for (const Overlay &overlay : m_overlays)
        ids.append(overlay.id);
    return ids;
}

void OverlayStore::loadLayout()
{
    // Fehlt liste.json, gilt die Reihenfolge nach Name; geschrieben wird erst bei einer Änderung.
    // Abweichungen zu den Overlay-Dateien werden nur im Speicher ausgeglichen.
    m_layoutError.clear();
    m_layout = layoutFromIds(idsByName());
    QFile file(layoutFilePath());
    if (!file.exists())
        return;
    OverlayLayout layout;
    QString error;
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("kann nicht gelesen werden (%1)").arg(file.errorString());
    } else if (parseLayout(file.readAll(), &layout, &error)) {
        // Overlays, die fehlen (zum Beispiel mit dss-cli angelegt), kommen ans Ende der obersten
        // Ebene; Einträge zu Overlays, die es nicht gibt, fallen weg.
        normalizeLayout(&layout, idsByName());
        m_layout = layout;
        return;
    }
    // Kaputte Datei: melden, nicht überschreiben, Liste nach Name.
    m_layoutError = QString::fromLatin1(kListFileName) + QStringLiteral(": ") + error;
    m_loadErrors.append(m_layoutError
                        + QStringLiteral(" (Liste nach Name, Datei wird nicht überschrieben)"));
}

QString OverlayStore::mediaLibraryFilePath() const
{
    return m_dataDir + QLatin1Char('/') + QString::fromLatin1(kMediaLibraryFileName);
}

void OverlayStore::loadMediaLibrary()
{
    // Wie liste.json: Fehlt die Datei, gelten Ersatznamen; Abweichungen zu media/ werden nur im
    // Speicher ausgeglichen, geschrieben wird erst bei einer Änderung.
    m_mediaError.clear();
    m_media = MediaLibrary();
    QFile file(mediaLibraryFilePath());
    if (file.exists()) {
        MediaLibrary library;
        QString error;
        if (!file.open(QIODevice::ReadOnly)) {
            error = QStringLiteral("kann nicht gelesen werden (%1)").arg(file.errorString());
        } else if (parseMediaLibrary(file.readAll(), &library, &error)) {
            m_media = library;
        }
        if (!error.isEmpty()) {
            // Kaputte Datei: melden, nicht überschreiben, Ersatznamen auf der obersten Ebene.
            m_mediaError = QString::fromLatin1(kMediaLibraryFileName) + QStringLiteral(": ") + error;
            m_loadErrors.append(m_mediaError + QStringLiteral(" (Ersatznamen, Datei wird nicht "
                                                              "überschrieben)"));
        }
    }
    reconcileMedia();
}

bool OverlayStore::reconcileMedia()
{
    const QList<MediaFileInfo> present = scanMediaDir(mediaDir());
    m_mediaBytes.clear();
    for (const MediaFileInfo &f : present)
        m_mediaBytes.insert(f.reference, f.bytes);
    return reconcileMediaLibrary(&m_media, present);
}

bool OverlayStore::writeMediaLibrary(const MediaLibrary &library, QString *error)
{
    // Atomar wie liste.json. https://doc.qt.io/qt-6.8/qsavefile.html
    QSaveFile file(mediaLibraryFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("%1 kann nicht geschrieben werden (%2)")
                     .arg(file.fileName(), file.errorString());
        return false;
    }
    file.write(serializeMediaLibrary(library));
    if (!file.commit()) {
        *error = QStringLiteral("%1 kann nicht gespeichert werden (%2)")
                     .arg(file.fileName(), file.errorString());
        return false;
    }
    return true;
}

bool OverlayStore::changeMedia(const std::function<QString(MediaLibrary *)> &change,
                               bool structural, QString *error)
{
    if (structural && !m_mediaError.isEmpty()) {
        *error = QStringLiteral("Die Medienliste lässt sich nicht ändern: %1. Die Datei wird nicht "
                                "überschrieben; reparieren oder entfernen und den Kern neu laden.")
                     .arg(m_mediaError);
        return false;
    }
    MediaLibrary changed = m_media;
    if (const QString e = change(&changed); !e.isEmpty()) {
        *error = e;
        return false;
    }
    // Ungültige medien.json bleibt, wie sie ist (Übernehmen und Löschen gelten nur im Speicher).
    if (m_writesMedia && m_mediaError.isEmpty() && !writeMediaLibrary(changed, error))
        return false;
    m_media = changed;
    return true;
}

QString OverlayStore::mediaName(const QString &reference) const
{
    const MediaEntry *e = m_media.entry(reference);
    return e ? e->name : QString();
}

QString OverlayStore::videoCodec(const QString &reference) const
{
    if (!m_videoCodecReader || mediaKindOf(reference) != MediaKind::Video)
        return {};
    auto it = m_videoCodecCache.constFind(reference);
    if (it == m_videoCodecCache.constEnd()) {
        QString codec;
        QString ignored;
        if (!m_videoCodecReader(mediaFilePath(reference), &codec, &ignored))
            codec.clear();
        it = m_videoCodecCache.insert(reference, codec);
    }
    return *it;
}

QString OverlayStore::browserProblem(const QString &reference, const QString &name) const
{
    const std::optional<QString> unsupported = browserUnsupportedVideoCodec(videoCodec(reference));
    if (!unsupported)
        return {};
    return browserVideoWarning(name.isEmpty() ? mediaName(reference) : name, *unsupported);
}

bool OverlayStore::recordMedia(const QString &reference, const QString &name, const QUuid &folder,
                               QString *error)
{
    const QFileInfo info(mediaFilePath(reference));
    if (!info.isFile()) {
        *error = QStringLiteral("Datei %1 fehlt im Datenordner.").arg(reference);
        return false;
    }
    m_mediaBytes.insert(reference, info.size());
    const QUuid target = m_media.folder(folder) ? folder : QUuid();
    const QString entryName = name.trimmed().isEmpty() ? QFileInfo(reference).fileName() : name;
    // Gibt es die Datei schon (gleicher Inhalt), bleiben Name und Ordner (addMediaToLibrary).
    return changeMedia([&](MediaLibrary *l) {
        return addMediaToLibrary(l, reference, entryName, target);
    }, false, error);
}

bool OverlayStore::renameMedia(const QString &reference, const QString &name, QString *error)
{
    return changeMedia([&](MediaLibrary *l) { return renameMediaInLibrary(l, reference, name); },
                       true, error);
}

bool OverlayStore::moveMediaFile(const QString &reference, const QUuid &folder, QString *error)
{
    return changeMedia([&](MediaLibrary *l) { return moveMediaFileInLibrary(l, reference, folder); },
                       true, error);
}

QUuid OverlayStore::createMediaFolder(const QString &name, const QUuid &parent, QString *error)
{
    const QUuid id = QUuid::createUuid();
    if (!changeMedia([&](MediaLibrary *l) { return createMediaFolderInLibrary(l, id, name, parent); },
                     true, error))
        return {};
    return id;
}

bool OverlayStore::renameMediaFolder(const QUuid &folderId, const QString &name, QString *error)
{
    return changeMedia([&](MediaLibrary *l) { return renameMediaFolderInLibrary(l, folderId, name); },
                       true, error);
}

bool OverlayStore::removeMediaFolder(const QUuid &folderId, QString *error)
{
    return changeMedia([&](MediaLibrary *l) { return removeMediaFolderFromLibrary(l, folderId); },
                       true, error);
}

bool OverlayStore::moveMediaFolder(const QUuid &folderId, const QUuid &parent, QString *error)
{
    return changeMedia([&](MediaLibrary *l) { return moveMediaFolderInLibrary(l, folderId, parent); },
                       true, error);
}

bool OverlayStore::setMediaFolderExpanded(const QUuid &folderId, bool expanded, QString *error)
{
    return changeMedia([&](MediaLibrary *l) {
        return setMediaFolderExpandedInLibrary(l, folderId, expanded);
    }, true, error);
}

bool OverlayStore::removeMedia(const QString &reference, QString *error)
{
    const MediaEntry *entry = m_media.entry(reference);
    if (!entry) {
        *error = QStringLiteral("Datei %1 ist nicht in der Medienliste.").arg(reference);
        return false;
    }
    const QString name = entry->name;
    if (const QList<MediaUsage> used = mediaUsages().value(reference); !used.isEmpty()) {
        *error = QStringLiteral("«%1» lässt sich nicht löschen, %2.").arg(name, usageText(used));
        return false;
    }
    for (const QString &path : std::as_const(m_unreadableOverlayFiles)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.readAll().contains(reference.toUtf8())) {
            *error = QStringLiteral("«%1» lässt sich nicht löschen: Die Overlay-Datei %2 ist nicht "
                                    "lesbar und kann die Datei verwenden.")
                         .arg(name, QFileInfo(path).fileName());
            return false;
        }
    }
    QFile file(mediaFilePath(reference));
    if (file.exists() && !file.remove()) {
        *error = QStringLiteral("%1 kann nicht gelöscht werden (%2)")
                     .arg(file.fileName(), file.errorString());
        return false;
    }
    m_mediaSizeCache.remove(reference);
    m_videoCodecCache.remove(reference);
    m_mediaBytes.remove(reference);
    QString writeError;
    if (!changeMedia([&](MediaLibrary *l) { return removeMediaFromLibrary(l, reference); }, false,
                     &writeError)) {
        // Datei ist weg; der Eintrag fällt spätestens beim nächsten Laden weg.
        removeMediaFromLibrary(&m_media, reference);
        *error = QStringLiteral("Datei gelöscht, aber die Medienliste nicht gespeichert: ") + writeError;
        return false;
    }
    return true;
}

bool OverlayStore::writeLayout(const OverlayLayout &layout, QString *error)
{
    // Atomar wie die Overlay-Dateien. https://doc.qt.io/qt-6.8/qsavefile.html
    QSaveFile file(layoutFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("%1 kann nicht geschrieben werden (%2)")
                     .arg(file.fileName(), file.errorString());
        return false;
    }
    file.write(serializeLayout(layout));
    if (!file.commit()) {
        *error = QStringLiteral("%1 kann nicht gespeichert werden (%2)")
                     .arg(file.fileName(), file.errorString());
        return false;
    }
    return true;
}

bool OverlayStore::changeLayout(const std::function<QString(OverlayLayout *)> &change,
                                QString *error)
{
    if (!m_layoutError.isEmpty()) {
        *error = QStringLiteral("Die Liste lässt sich nicht ändern: %1. Die Datei wird nicht "
                                "überschrieben; reparieren oder entfernen und den Kern neu laden.")
                     .arg(m_layoutError);
        return false;
    }
    OverlayLayout changed = m_layout;
    if (const QString e = change(&changed); !e.isEmpty()) {
        *error = e;
        return false;
    }
    if (!writeLayout(changed, error))
        return false;
    m_layout = changed;
    return true;
}

bool OverlayStore::followLayout(const std::function<QString(OverlayLayout *)> &change,
                                const QString &done, QString *error)
{
    change(&m_layout);
    if (!m_layoutError.isEmpty())
        return true; // kaputte liste.json bleibt, wie sie ist
    QString writeError;
    if (!writeLayout(m_layout, &writeError)) {
        *error = done + QStringLiteral(", aber die Liste nicht gespeichert: ") + writeError;
        return false;
    }
    return true;
}

int OverlayStore::indexOf(const QUuid &overlayId) const
{
    for (qsizetype i = 0; i < m_overlays.size(); ++i) {
        if (m_overlays.at(i).id == overlayId)
            return int(i);
    }
    return -1;
}

const Overlay *OverlayStore::overlay(const QUuid &overlayId) const
{
    const int index = indexOf(overlayId);
    return index < 0 ? nullptr : &m_overlays.at(index);
}

const Overlay *OverlayStore::overlayByAddress(const QString &address) const
{
    for (const Overlay &overlay : m_overlays) {
        if (overlay.address == address)
            return &overlay;
    }
    return nullptr;
}

void OverlayStore::insertSorted(const Overlay &overlay)
{
    const auto less = [](const Overlay &a, const Overlay &b) {
        const int c = a.name.compare(b.name, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.address < b.address;
    };
    m_overlays.insert(std::upper_bound(m_overlays.begin(), m_overlays.end(), overlay, less),
                      overlay);
}

QSet<QString> OverlayStore::takenAddresses(const QUuid &except) const
{
    QSet<QString> taken;
    for (const Overlay &overlay : m_overlays) {
        if (overlay.id != except)
            taken.insert(overlay.address);
    }
    return taken;
}

bool OverlayStore::writeOverlay(const Overlay &overlay, QString *error)
{
    // QSaveFile schreibt in eine temporäre Datei und benennt sie erst bei commit() um;
    // ein Absturz hinterlässt keine halbe Datei. https://doc.qt.io/qt-6.8/qsavefile.html
    QSaveFile file(overlayFilePath(overlay.id));
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("%1 kann nicht geschrieben werden (%2)")
                     .arg(file.fileName(), file.errorString());
        return false;
    }
    file.write(serializeOverlay(overlay));
    if (!file.commit()) {
        *error = QStringLiteral("%1 kann nicht gespeichert werden (%2)")
                     .arg(file.fileName(), file.errorString());
        return false;
    }
    ++m_overlayWrites;
    m_pendingWrites.remove(overlay.id);
    return true;
}

bool OverlayStore::flushPendingWrites(QString *error)
{
    QStringList errors;
    const QSet<QUuid> pending = m_pendingWrites;
    for (const QUuid &id : pending) {
        const Overlay *o = overlay(id);
        if (!o) {
            m_pendingWrites.remove(id);
            continue;
        }
        QString e;
        if (!writeOverlay(*o, &e))
            errors.append(e);
    }
    if (!errors.isEmpty()) {
        *error = errors.join(QStringLiteral("; "));
        return false;
    }
    return true;
}

bool OverlayStore::addNewOverlay(const Overlay &overlay, QString *error)
{
    if (!writeOverlay(overlay, error))
        return false;
    if (!m_tokens.createToken(overlay.id, error)) {
        QFile::remove(overlayFilePath(overlay.id));
        return false;
    }
    insertSorted(overlay);
    return true;
}

static QString unknownOverlay(const QUuid &overlayId)
{
    return QStringLiteral("Overlay %1 nicht gefunden.").arg(overlayId.toString(QUuid::WithoutBraces));
}

bool OverlayStore::modify(const QUuid &overlayId, const std::function<QString(Overlay *)> &change,
                          QString *error)
{
    const int index = indexOf(overlayId);
    if (index < 0) {
        *error = unknownOverlay(overlayId);
        return false;
    }
    Overlay changed = m_overlays.at(index);
    if (const QString e = change(&changed); !e.isEmpty()) {
        *error = e;
        return false;
    }
    if (const QString e = validateOverlay(changed); !e.isEmpty()) {
        *error = e;
        return false;
    }
    if (m_deferredWrites)
        m_pendingWrites.insert(overlayId); // geschrieben von flushPendingWrites()
    else if (!writeOverlay(changed, error))
        return false;
    m_overlays.removeAt(index);
    insertSorted(changed);
    return true;
}

QUuid OverlayStore::createOverlay(const QString &name, int width, int height, QString *error)
{
    Overlay overlay;
    overlay.id = QUuid::createUuid();
    overlay.name = name;
    overlay.address = uniqueAddress(addressFromName(name), takenAddresses());
    overlay.width = width;
    overlay.height = height;
    if (const QString e = validateOverlay(overlay); !e.isEmpty()) {
        *error = e;
        return {};
    }
    if (!addNewOverlay(overlay, error))
        return {};
    if (!followLayout([&](OverlayLayout *l) { return addOverlayToLayout(l, overlay.id); },
                      QStringLiteral("Overlay angelegt"), error))
        return {};
    return overlay.id;
}

bool OverlayStore::renameOverlay(const QUuid &overlayId, const QString &newName,
                                 bool changeAddress, QString *error)
{
    const QSet<QString> taken = takenAddresses(overlayId);
    return modify(overlayId, [&](Overlay *overlay) {
        if (const QString e = validateOverlayName(newName); !e.isEmpty())
            return e;
        overlay->name = newName;
        if (changeAddress)
            overlay->address = uniqueAddress(addressFromName(newName), taken);
        return QString();
    }, error);
}

QUuid OverlayStore::copyOverlay(const QUuid &overlayId, QString *error)
{
    const Overlay *original = overlay(overlayId);
    if (!original) {
        *error = unknownOverlay(overlayId);
        return {};
    }
    Overlay copy = *original;
    copy.id = QUuid::createUuid();
    copy.name = original->name + QStringLiteral(" Kopie");
    copy.address = uniqueAddress(addressFromName(copy.name), takenAddresses());
    for (Source &source : copy.sources)
        source.id = QUuid::createUuid();
    if (const QString e = validateOverlay(copy); !e.isEmpty()) {
        *error = e;
        return {};
    }
    if (!addNewOverlay(copy, error))
        return {};
    if (!followLayout([&](OverlayLayout *l) { return addOverlayToLayout(l, copy.id, overlayId); },
                      QStringLiteral("Kopie angelegt"), error))
        return {};
    return copy.id;
}

bool OverlayStore::removeOverlay(const QUuid &overlayId, QString *error)
{
    const int index = indexOf(overlayId);
    if (index < 0) {
        *error = unknownOverlay(overlayId);
        return false;
    }
    QFile file(overlayFilePath(overlayId));
    if (!file.remove()) {
        *error = QStringLiteral("%1 kann nicht gelöscht werden (%2)")
                     .arg(file.fileName(), file.errorString());
        return false;
    }
    m_overlays.removeAt(index);
    m_pendingWrites.remove(overlayId);
    if (!m_tokens.removeToken(overlayId, error)) {
        *error = QStringLiteral("Overlay gelöscht, aber Token nicht entfernt: ") + *error;
        return false;
    }
    return followLayout([&](OverlayLayout *l) { return removeOverlayFromLayout(l, overlayId); },
                        QStringLiteral("Overlay gelöscht"), error);
}

bool OverlayStore::setOverlaySize(const QUuid &overlayId, int width, int height, QString *error)
{
    return modify(overlayId, [&](Overlay *o) { return dss::setOverlaySize(o, width, height); },
                  error);
}

bool OverlayStore::setOverlayOutput(const QUuid &overlayId, bool output, QString *error)
{
    return modify(overlayId, [&](Overlay *o) {
        o->output = output;
        return QString();
    }, error);
}

bool OverlayStore::newToken(const QUuid &overlayId, QString *error)
{
    if (indexOf(overlayId) < 0) {
        *error = unknownOverlay(overlayId);
        return false;
    }
    return m_tokens.createToken(overlayId, error);
}

bool OverlayStore::moveOverlayInList(const QUuid &overlayId, const QUuid &folderId, int index,
                                     QString *error)
{
    return changeLayout([&](OverlayLayout *l) {
        return moveOverlayInLayout(l, overlayId, folderId, index);
    }, error);
}

bool OverlayStore::moveFolderInList(const QUuid &folderId, int index, QString *error)
{
    return changeLayout([&](OverlayLayout *l) { return moveFolderInLayout(l, folderId, index); },
                        error);
}

QUuid OverlayStore::createFolder(const QString &name, QString *error)
{
    const QUuid id = QUuid::createUuid();
    if (!changeLayout([&](OverlayLayout *l) { return createFolderInLayout(l, id, name); }, error))
        return {};
    return id;
}

bool OverlayStore::renameFolder(const QUuid &folderId, const QString &name, QString *error)
{
    return changeLayout([&](OverlayLayout *l) { return renameFolderInLayout(l, folderId, name); },
                        error);
}

bool OverlayStore::removeFolder(const QUuid &folderId, QString *error)
{
    return changeLayout([&](OverlayLayout *l) { return removeFolderFromLayout(l, folderId); },
                        error);
}

bool OverlayStore::setFolderExpanded(const QUuid &folderId, bool expanded, QString *error)
{
    return changeLayout([&](OverlayLayout *l) {
        return setFolderExpandedInLayout(l, folderId, expanded);
    }, error);
}

QString OverlayStore::importMedia(const QString &filePath, MediaUse use, QString *error,
                                  const QUuid &folder)
{
    const MediaImportResult r = importMediaFile(mediaDir(), filePath, use, nullptr, m_progress);
    if (r.reference.isEmpty()) {
        *error = r.error;
        return {};
    }
    // Ursprünglicher Dateiname in die Medienliste (P10b). Scheitert das Speichern der Liste, gilt
    // die Übernahme trotzdem; die Datei bekommt dann beim nächsten Laden einen Ersatznamen.
    QString ignored;
    recordMedia(r.reference, QFileInfo(filePath).fileName(), folder, &ignored);
    return r.reference;
}

QString OverlayStore::resolveMedia(const QString &file, MediaUse use, QString *error)
{
    if (!validateMediaReference(file).isEmpty())
        return importMedia(file, use, error);
    // Verweis auf eine Kopie, die schon im Datenordner liegt.
    if (!allowedMediaSuffixes(use).contains(QFileInfo(file).suffix())
        || !QFileInfo::exists(mediaFilePath(file))) {
        *error = QStringLiteral("Datei %1 fehlt im Datenordner.").arg(file);
        return {};
    }
    return file;
}

QUuid OverlayStore::addPreparedSource(const QUuid &overlayId, Source source, QString *error,
                                      std::optional<std::pair<int, int>> size)
{
    const Overlay *target = overlay(overlayId);
    if (!target) {
        *error = unknownOverlay(overlayId);
        return {};
    }
    source.id = QUuid::createUuid();
    source.x = 0;
    source.y = 0;
    source.width = size ? size->first : target->width;
    source.height = size ? size->second : target->height;
    if (!modify(overlayId, [&](Overlay *o) { return dss::addSource(o, source); }, error))
        return {};
    return source.id;
}

std::optional<QSize> OverlayStore::mediaSize(const QString &reference, QString *reason) const
{
    auto it = m_mediaSizeCache.constFind(reference);
    if (it == m_mediaSizeCache.constEnd()) {
        CachedSize cached;
        int width = 0;
        int height = 0;
        const QString path = mediaFilePath(reference);
        if (m_mediaSizeReader(path, &width, &height, &cached.reason)
            && width >= kSourceMinSize && height >= kSourceMinSize) {
            // Bilder so gross, wie sie angezeigt werden (P09b, Entscheid 7): Der Leser gibt die
            // gespeicherte Grösse, die EXIF-Ausrichtung 5 bis 8 vertauscht Breite und Höhe.
            const bool image = allowedMediaSuffixes(MediaUse::Image)
                                   .contains(QFileInfo(reference).suffix().toLower());
            cached.size = QSize(width, height);
            if (image)
                cached.size = orientedSize(*cached.size, imageFileOrientation(path));
        } else if (cached.reason.isEmpty()) {
            cached.reason = QStringLiteral("Grösse %1x%2").arg(width).arg(height);
        }
        it = m_mediaSizeCache.insert(reference, cached);
    }
    if (reason)
        *reason = it->reason;
    return it->size;
}

MediaSizes OverlayStore::mediaSizes(const Overlay &overlay) const
{
    MediaSizes sizes;
    if (!m_mediaSizeReader)
        return sizes;
    for (const Source &source : overlay.sources) {
        if (source.kind == SourceKind::Text || sizes.contains(source.file))
            continue;
        if (const std::optional<QSize> size = mediaSize(source.file, nullptr))
            sizes.insert(source.file, *size);
    }
    return sizes;
}

QUuid OverlayStore::addMediaSource(const QUuid &overlayId, const QString &filePath, SourceKind kind,
                                   bool loop, bool holdAtEnd, QString *error, QString *warning,
                                   const QString &sourceName)
{
    if (!overlay(overlayId)) {
        *error = unknownOverlay(overlayId);
        return {};
    }
    const QString reference =
        resolveMedia(filePath, kind == SourceKind::Image ? MediaUse::Image : MediaUse::Video, error);
    if (reference.isEmpty())
        return {};
    Source source;
    source.kind = kind;
    // ursprünglicher Dateiname als Vorschlag
    source.name = sourceName.isEmpty() ? QFileInfo(filePath).fileName() : sourceName;
    source.file = reference;
    source.loop = loop;
    source.holdAtEnd = holdAtEnd;
    // Grösse der Datei (Entscheid 6), gelesen aus der Kopie im Datenordner.
    std::optional<std::pair<int, int>> size;
    if (m_mediaSizeReader) {
        // Frisch kopiert: neu lesen, falls die Datei vorher fehlte oder unlesbar war.
        m_mediaSizeCache.remove(reference);
        QString reason;
        if (const std::optional<QSize> read = mediaSize(reference, &reason)) {
            size = std::pair(read->width(), read->height());
        } else {
            const Overlay *target = overlay(overlayId);
            const QString text = QStringLiteral("Grösse von «%1» nicht lesbar (%2), die Quelle "
                                                "bekommt die Grösse des Overlays %3x%4.")
                                     .arg(source.name, reason).arg(target->width).arg(target->height);
            if (warning)
                *warning = text;
        }
    }
    return addPreparedSource(overlayId, source, error, size);
}

QUuid OverlayStore::addImageSource(const QUuid &overlayId, const QString &filePath, QString *error,
                                   QString *warning, const QString &sourceName)
{
    return addMediaSource(overlayId, filePath, SourceKind::Image, false, false, error, warning,
                          sourceName);
}

QUuid OverlayStore::addVideoSource(const QUuid &overlayId, const QString &filePath, bool loop,
                                   QString *error, QString *warning, bool holdAtEnd,
                                   const QString &sourceName)
{
    return addMediaSource(overlayId, filePath, SourceKind::Video, loop, holdAtEnd, error, warning,
                          sourceName);
}

QUuid OverlayStore::addTextSource(const QUuid &overlayId, const QString &content,
                                  const QString &fontFilePath, QString *error)
{
    if (!overlay(overlayId)) {
        *error = unknownOverlay(overlayId);
        return {};
    }
    const QString reference = resolveMedia(fontFilePath, MediaUse::Font, error);
    if (reference.isEmpty())
        return {};
    Source source;
    source.kind = SourceKind::Text;
    const QString firstLine = content.section(QLatin1Char('\n'), 0, 0).trimmed();
    source.name = firstLine.isEmpty() ? QStringLiteral("Text")
                                      : QStringLiteral("Text: ") + firstLine.left(40);
    source.text.content = content;
    source.text.fontFile = reference;
    return addPreparedSource(overlayId, source, error);
}

bool OverlayStore::removeSource(const QUuid &overlayId, const QUuid &sourceId, QString *error)
{
    return modify(overlayId, [&](Overlay *o) { return dss::removeSource(o, sourceId); }, error);
}

bool OverlayStore::moveSourceForward(const QUuid &overlayId, const QUuid &sourceId, QString *error)
{
    return modify(overlayId, [&](Overlay *o) { return dss::moveSourceForward(o, sourceId); }, error);
}

bool OverlayStore::moveSourceBackward(const QUuid &overlayId, const QUuid &sourceId, QString *error)
{
    return modify(overlayId, [&](Overlay *o) { return dss::moveSourceBackward(o, sourceId); }, error);
}

bool OverlayStore::setSourceVisible(const QUuid &overlayId, const QUuid &sourceId, bool visible,
                                    QString *error)
{
    return modify(overlayId,
                  [&](Overlay *o) { return dss::setSourceVisible(o, sourceId, visible); }, error);
}

bool OverlayStore::setSourceGeometry(const QUuid &overlayId, const QUuid &sourceId, int x, int y,
                                     int width, int height, int rotation, QString *error)
{
    return modify(overlayId, [&](Overlay *o) {
        return dss::setSourceGeometry(o, sourceId, x, y, width, height, rotation);
    }, error);
}

bool OverlayStore::moveSource(const QUuid &overlayId, const QUuid &sourceId, int index,
                              QString *error)
{
    return modify(overlayId, [&](Overlay *o) { return dss::moveSource(o, sourceId, index); }, error);
}

bool OverlayStore::updateSource(const QUuid &overlayId, const QUuid &sourceId,
                                const SourceChanges &changes, QString *error)
{
    return modify(overlayId, [&](Overlay *o) { return dss::updateSource(o, sourceId, changes); },
                  error);
}

static QString findSourceOfKind(const Overlay *overlay, const QUuid &overlayId,
                                const QUuid &sourceId, SourceKind kind, const QString &what)
{
    if (!overlay)
        return unknownOverlay(overlayId);
    const int index = sourceIndex(*overlay, sourceId);
    if (index < 0)
        return QStringLiteral("Quelle %1 nicht gefunden.").arg(sourceId.toString(QUuid::WithoutBraces));
    if (overlay->sources.at(index).kind != kind)
        return what;
    return {};
}

bool OverlayStore::setTextFont(const QUuid &overlayId, const QUuid &sourceId,
                               const QString &fontFile, int weight, bool italic, QString *error,
                               QString *reference)
{
    if (const QString e = findSourceOfKind(overlay(overlayId), overlayId, sourceId,
                                           SourceKind::Text,
                                           QStringLiteral("Eine Schrift gibt es nur bei Quellen der Art Text."));
        !e.isEmpty()) {
        *error = e;
        return false;
    }
    const QString ref = resolveMedia(fontFile, MediaUse::Font, error);
    if (ref.isEmpty())
        return false;
    const bool ok = modify(overlayId, [&](Overlay *o) {
        const int index = sourceIndex(*o, sourceId);
        Source changed = o->sources.at(index);
        changed.text.fontFile = ref;
        changed.text.weight = weight;
        changed.text.italic = italic;
        if (const QString e = validateSource(changed); !e.isEmpty())
            return e;
        o->sources[index] = changed;
        return QString();
    }, error);
    if (ok && reference)
        *reference = ref;
    return ok;
}

bool OverlayStore::replaceImageFile(const QUuid &overlayId, const QUuid &sourceId,
                                    const QString &filePath, QString *error)
{
    if (const QString e = findSourceOfKind(overlay(overlayId), overlayId, sourceId,
                                           SourceKind::Image,
                                           QStringLiteral("Datei ersetzen gibt es nur bei Quellen der Art Bild."));
        !e.isEmpty()) {
        *error = e;
        return false;
    }
    const QString ref = resolveMedia(filePath, MediaUse::Image, error);
    if (ref.isEmpty())
        return false;
    return modify(overlayId, [&](Overlay *o) {
        o->sources[sourceIndex(*o, sourceId)].file = ref;
        return QString();
    }, error);
}

} // namespace dss
