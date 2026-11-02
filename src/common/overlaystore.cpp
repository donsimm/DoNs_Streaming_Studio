#include "overlaystore.h"

#include "address.h"
#include "overlayjson.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace dss {

QStringList allowedMediaSuffixes(MediaUse use)
{
    switch (use) {
    case MediaUse::Image:
        return {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg")};
    case MediaUse::Video:
        return {QStringLiteral("webm"), QStringLiteral("mp4")};
    case MediaUse::Font:
        return {QStringLiteral("ttf"), QStringLiteral("otf")};
    }
    return {};
}

QString defaultDataDir()
{
    // AppDataLocation hängt den Programmnamen an (<APPNAME>). Kern, Fenster und dss-cli
    // haben verschiedene Namen, sollen aber denselben Ordner nutzen; deshalb wird der Name
    // nur für diese Abfrage gesetzt. Ein Organisationsname ist nicht gesetzt.
    // https://doc.qt.io/qt-6.8/qstandardpaths.html
    const QString previous = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QString::fromLatin1(kDataDirName));
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

static QString relativeName(const QString &fileName)
{
    return QStringLiteral("overlays/") + fileName;
}

bool OverlayStore::load(QString *error, LoadMode mode)
{
    const bool readOnly = mode == LoadMode::ReadOnly;
    m_overlays.clear();
    m_loadErrors.clear();
    m_loadWarnings.clear();
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
        insertSorted(overlay);
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
    if (!writeOverlay(changed, error))
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
    if (!m_tokens.removeToken(overlayId, error)) {
        *error = QStringLiteral("Overlay gelöscht, aber Token nicht entfernt: ") + *error;
        return false;
    }
    return true;
}

QString OverlayStore::importMedia(const QString &filePath, MediaUse use, QString *error)
{
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    const QStringList allowed = allowedMediaSuffixes(use);
    if (!allowed.contains(suffix)) {
        *error = QStringLiteral("Endung «%1» nicht erlaubt, erlaubt sind: %2.")
                     .arg(info.suffix(), allowed.join(QStringLiteral(", ")));
        return {};
    }
    QFile source(filePath);
    if (!source.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1 kann nicht gelesen werden (%2)").arg(filePath, source.errorString());
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&source)) {
        *error = QStringLiteral("%1 kann nicht gelesen werden (%2)").arg(filePath, source.errorString());
        return {};
    }
    const QString reference = QStringLiteral("media/") + QString::fromLatin1(hash.result().toHex())
                              + QLatin1Char('.') + suffix;
    const QString target = mediaFilePath(reference);
    if (QFileInfo::exists(target))
        return reference; // gleicher Inhalt schon vorhanden: keine zweite Kopie

    if (!QDir().mkpath(mediaDir())) {
        *error = QStringLiteral("Ordner %1 kann nicht angelegt werden.").arg(mediaDir());
        return {};
    }
    source.seek(0);
    QSaveFile copy(target);
    if (!copy.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("%1 kann nicht geschrieben werden (%2)").arg(target, copy.errorString());
        return {};
    }
    // Inhalt beim Kopieren nochmals prüfen, falls sich die Datei seit dem Hashen geändert hat.
    QCryptographicHash check(QCryptographicHash::Sha256);
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1 << 20);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError)
            break;
        check.addData(chunk);
        copy.write(chunk);
    }
    if (source.error() != QFileDevice::NoError || check.result() != hash.result()) {
        copy.cancelWriting();
        *error = QStringLiteral("%1 hat sich beim Kopieren geändert oder ist nicht lesbar.").arg(filePath);
        return {};
    }
    if (!copy.commit()) {
        *error = QStringLiteral("%1 kann nicht gespeichert werden (%2)").arg(target, copy.errorString());
        return {};
    }
    return reference;
}

QUuid OverlayStore::addPreparedSource(const QUuid &overlayId, Source source, QString *error)
{
    const Overlay *target = overlay(overlayId);
    if (!target) {
        *error = unknownOverlay(overlayId);
        return {};
    }
    source.id = QUuid::createUuid();
    source.x = 0;
    source.y = 0;
    source.width = target->width;
    source.height = target->height;
    if (!modify(overlayId, [&](Overlay *o) { return dss::addSource(o, source); }, error))
        return {};
    return source.id;
}

QUuid OverlayStore::addMediaSource(const QUuid &overlayId, const QString &filePath, SourceKind kind,
                                   bool loop, QString *error)
{
    if (!overlay(overlayId)) {
        *error = unknownOverlay(overlayId);
        return {};
    }
    const QString reference =
        importMedia(filePath, kind == SourceKind::Image ? MediaUse::Image : MediaUse::Video, error);
    if (reference.isEmpty())
        return {};
    Source source;
    source.kind = kind;
    source.name = QFileInfo(filePath).fileName(); // ursprünglicher Dateiname als Vorschlag
    source.file = reference;
    source.loop = loop;
    return addPreparedSource(overlayId, source, error);
}

QUuid OverlayStore::addImageSource(const QUuid &overlayId, const QString &filePath, QString *error)
{
    return addMediaSource(overlayId, filePath, SourceKind::Image, false, error);
}

QUuid OverlayStore::addVideoSource(const QUuid &overlayId, const QString &filePath, bool loop,
                                   QString *error)
{
    return addMediaSource(overlayId, filePath, SourceKind::Video, loop, error);
}

QUuid OverlayStore::addTextSource(const QUuid &overlayId, const QString &content,
                                  const QString &fontFilePath, QString *error)
{
    if (!overlay(overlayId)) {
        *error = unknownOverlay(overlayId);
        return {};
    }
    const QString reference = importMedia(fontFilePath, MediaUse::Font, error);
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

} // namespace dss
