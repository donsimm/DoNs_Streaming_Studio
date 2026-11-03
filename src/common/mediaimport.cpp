#include "mediaimport.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QWaitCondition>

namespace dss {

namespace {

constexpr qint64 kChunkBytes = 1 << 20; // 1 MiB: Abbruch und Fortschritt je Stück
constexpr unsigned long kWaitSliceMs = 100; // Warten auf ein gleiches Ziel, dabei Abbruch prüfen

// Ziele, die gerade ein Thread schreibt. Gleicher Inhalt heisst gleiches Ziel; der zweite Auftrag
// wartet, bis der erste fertig ist, und findet dann die Kopie vor.
class TargetLocks
{
public:
    // false bei Abbruch während des Wartens.
    bool acquire(const QString &target, const std::atomic<bool> *cancel)
    {
        QMutexLocker locker(&m_mutex);
        while (m_busy.contains(target)) {
            if (cancel && cancel->load())
                return false;
            m_released.wait(&m_mutex, kWaitSliceMs);
        }
        m_busy.insert(target);
        return true;
    }

    void release(const QString &target)
    {
        QMutexLocker locker(&m_mutex);
        m_busy.remove(target);
        m_released.wakeAll();
    }

private:
    QMutex m_mutex;
    QWaitCondition m_released;
    QSet<QString> m_busy;
};

TargetLocks &targetLocks()
{
    static TargetLocks locks;
    return locks;
}

bool canceled(const std::atomic<bool> *cancel)
{
    return cancel && cancel->load();
}

MediaImportResult canceledResult(const QString &filePath)
{
    MediaImportResult r;
    r.canceled = true;
    r.error = QStringLiteral("Übernehmen von %1 abgebrochen.").arg(filePath);
    return r;
}

} // namespace

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

MediaImportResult importMediaFile(const QString &mediaDir, const QString &filePath, MediaUse use,
                                  const std::atomic<bool> *cancel,
                                  const std::function<void()> &progress)
{
    MediaImportResult result;
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    const QStringList allowed = allowedMediaSuffixes(use);
    if (!allowed.contains(suffix)) {
        result.error = QStringLiteral("Endung «%1» nicht erlaubt, erlaubt sind: %2.")
                           .arg(info.suffix(), allowed.join(QStringLiteral(", ")));
        return result;
    }
    QFile source(filePath);
    if (!source.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("%1 kann nicht gelesen werden (%2)").arg(filePath, source.errorString());
        return result;
    }
    result.bytes = source.size();
    // Stückweise statt addData(QIODevice *), damit Abbruch und Fortschritt dazwischen laufen.
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!source.atEnd()) {
        if (canceled(cancel))
            return canceledResult(filePath);
        const QByteArray chunk = source.read(kChunkBytes);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
            result.error = QStringLiteral("%1 kann nicht gelesen werden (%2)").arg(filePath, source.errorString());
            return result;
        }
        hash.addData(chunk);
        if (progress)
            progress();
    }
    const QString reference = QStringLiteral("media/") + QString::fromLatin1(hash.result().toHex())
                              + QLatin1Char('.') + suffix;
    const QString target = QDir(mediaDir).filePath(QFileInfo(reference).fileName());

    if (!targetLocks().acquire(target, cancel))
        return canceledResult(filePath);
    struct Release
    {
        QString target;
        ~Release() { targetLocks().release(target); }
    } release{target};

    if (QFileInfo::exists(target)) {
        result.reference = reference; // gleicher Inhalt schon vorhanden: keine zweite Kopie
        result.existed = true;
        return result;
    }
    if (!QDir().mkpath(mediaDir)) {
        result.error = QStringLiteral("Ordner %1 kann nicht angelegt werden.").arg(mediaDir);
        return result;
    }
    source.seek(0);
    QSaveFile copy(target);
    if (!copy.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("%1 kann nicht geschrieben werden (%2)").arg(target, copy.errorString());
        return result;
    }
    // Inhalt beim Kopieren nochmals prüfen, falls sich die Datei seit dem Hashen geändert hat.
    QCryptographicHash check(QCryptographicHash::Sha256);
    while (!source.atEnd()) {
        if (canceled(cancel)) {
            copy.cancelWriting(); // QSaveFile verwirft die vorläufige Datei
            return canceledResult(filePath);
        }
        const QByteArray chunk = source.read(kChunkBytes);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError)
            break;
        check.addData(chunk);
        copy.write(chunk);
        if (progress)
            progress();
    }
    if (source.error() != QFileDevice::NoError || check.result() != hash.result()) {
        copy.cancelWriting();
        result.error = QStringLiteral("%1 hat sich beim Kopieren geändert oder ist nicht lesbar.").arg(filePath);
        return result;
    }
    if (!copy.commit()) {
        result.error = QStringLiteral("%1 kann nicht gespeichert werden (%2)").arg(target, copy.errorString());
        return result;
    }
    result.reference = reference;
    return result;
}

bool isSaveFileLeftover(const QString &fileName)
{
    // <SHA-256>.<Endung> wie in importMediaFile, dazu «.XXXXXX» von QTemporaryFileName
    // (sechs Zeichen aus A–Z, a–z; Quellen oben in mediaimport.h).
    static const QRegularExpression pattern(QStringLiteral(
        "\\A[0-9a-f]{64}\\.(png|jpg|jpeg|webm|mp4|ttf|otf)\\.[A-Za-z]{6}\\z"));
    return pattern.match(fileName).hasMatch();
}

LeftoverCleanup removeSaveFileLeftovers(const QString &mediaDir)
{
    LeftoverCleanup cleanup;
    const QDir dir(mediaDir);
    // Nur reguläre Dateien, keine Verweise (QDir::NoSymLinks), auch versteckte nicht nötig.
    const QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::Hidden);
    for (const QFileInfo &entry : entries) {
        if (!isSaveFileLeftover(entry.fileName()))
            continue;
        const qint64 size = entry.size();
        QFile file(entry.absoluteFilePath());
        if (file.remove()) {
            ++cleanup.count;
            cleanup.bytes += size;
            cleanup.removed.append(entry.fileName());
        } else {
            cleanup.failed.append(QStringLiteral("%1 (%2)").arg(entry.fileName(), file.errorString()));
        }
    }
    return cleanup;
}

MediaImporter::MediaImporter(const QString &mediaDir, int maxThreads, QObject *parent)
    : QObject(parent)
    , m_mediaDir(mediaDir)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    qRegisterMetaType<dss::MediaImportResult>();
    m_pool.setMaxThreadCount(maxThreads);
}

MediaImporter::~MediaImporter()
{
    cancelAll();
}

quint64 MediaImporter::start(const QString &filePath, MediaUse use)
{
    const quint64 job = m_nextJob++;
    m_pending.insert(job, filePath);
    const QString mediaDir = m_mediaDir;
    const std::shared_ptr<std::atomic<bool>> cancel = m_cancel;
    m_pool.start([this, job, mediaDir, filePath, use, cancel] {
        const MediaImportResult result = importMediaFile(mediaDir, filePath, use, cancel.get());
        if (cancel->load())
            return;
        // Zurück in den Thread dieses Objekts; nach cancelAll() steht der Auftrag nicht mehr in
        // m_pending und wird übergangen.
        QMetaObject::invokeMethod(this, [this, job, result] {
            if (m_pending.remove(job) == 0)
                return;
            emit finished(job, result);
        }, Qt::QueuedConnection);
    });
    return job;
}

int MediaImporter::cancelAll()
{
    const int count = int(m_pending.size());
    m_cancel->store(true);
    m_pool.waitForDone();
    m_pending.clear();
    // Neue Aufträge nach dem Abbrechen (gibt es beim Beenden nicht) bekämen ein neues Zeichen.
    m_cancel = std::make_shared<std::atomic<bool>>(false);
    return count;
}

} // namespace dss
