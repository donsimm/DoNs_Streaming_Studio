#include "datalock.h"

#include <QDir>
#include <QLockFile>

namespace dss {

QString lockFilePath(const QString &dataDir)
{
    return QDir(dataDir).filePath(QString::fromLatin1(kLockFileName));
}

DataLock::DataLock(const QString &dataDir)
    : m_dataDir(dataDir)
    , m_lock(std::make_unique<QLockFile>(lockFilePath(dataDir)))
{
    // Dauerhafte Sperre: nie wegen ihres Alters als verwaist ansehen.
    // https://doc.qt.io/qt-6.8/qlockfile.html#setStaleLockTime
    m_lock->setStaleLockTime(0);
}

DataLock::~DataLock() = default;

bool DataLock::tryLock(QString *error, qint64 *holderPid)
{
    if (holderPid)
        *holderPid = 0;
    if (!QDir().mkpath(m_dataDir)) {
        *error = QStringLiteral("Datenordner %1 kann nicht angelegt werden").arg(m_dataDir);
        return false;
    }
    if (m_lock->tryLock(0))
        return true;
    switch (m_lock->error()) {
    case QLockFile::LockFailedError: {
        qint64 pid = 0;
        QString host;
        QString app;
        if (m_lock->getLockInfo(&pid, &host, &app)) {
            if (holderPid)
                *holderPid = pid;
            *error = QStringLiteral("Datenordner ist gesperrt von %1 (PID %2)")
                         .arg(app.isEmpty() ? QStringLiteral("einem anderen Programm") : app)
                         .arg(pid);
        } else {
            *error = QStringLiteral("Datenordner ist gesperrt (%1)").arg(lockFilePath(m_dataDir));
        }
        return false;
    }
    case QLockFile::PermissionError:
        *error = QStringLiteral("Sperrdatei %1 kann nicht angelegt werden (keine Rechte)")
                     .arg(lockFilePath(m_dataDir));
        return false;
    case QLockFile::NoError:
    case QLockFile::UnknownError:
        break;
    }
    *error = QStringLiteral("Sperrdatei %1: unbekannter Fehler").arg(lockFilePath(m_dataDir));
    return false;
}

bool DataLock::isLocked() const
{
    return m_lock->isLocked();
}

void DataLock::unlock()
{
    m_lock->unlock();
}

} // namespace dss
