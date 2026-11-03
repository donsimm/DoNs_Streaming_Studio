#include "portowner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace dss {

namespace {

// Inodes der lauschenden Sockets auf port aus einer Datei im Format von /proc/net/tcp.
QSet<QByteArray> listeningInodes(const QString &path, int port)
{
    QSet<QByteArray> inodes;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return inodes;
    file.readLine(); // Kopfzeile
    while (!file.atEnd()) {
        const QList<QByteArray> f = file.readLine().simplified().split(' ');
        // sl local_address rem_address st tx:rx tr:when retrnsmt uid timeout inode
        if (f.size() < 10 || f.at(3) != "0A")
            continue;
        const qsizetype colon = f.at(1).lastIndexOf(':');
        bool ok = false;
        if (colon < 0 || f.at(1).mid(colon + 1).toInt(&ok, 16) != port || !ok)
            continue;
        if (f.at(9) != "0")
            inodes.insert(f.at(9));
    }
    return inodes;
}

} // namespace

PortOwner findPortOwner(int port, const QString &procRoot)
{
    PortOwner owner;
    QSet<QByteArray> inodes = listeningInodes(procRoot + QStringLiteral("/net/tcp"), port);
    inodes.unite(listeningInodes(procRoot + QStringLiteral("/net/tcp6"), port));
    if (inodes.isEmpty())
        return owner;
    owner.listening = true;
    const QDir proc(procRoot);
    for (const QString &entry : proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        const qint64 pid = entry.toLongLong(&isPid);
        if (!isPid)
            continue;
        const QDir fds(proc.filePath(entry + QStringLiteral("/fd")));
        for (const QString &fd : fds.entryList(QDir::Files | QDir::System | QDir::NoDotAndDotDot)) {
            // Ziel «socket:[Inode]»; symLinkTarget macht daraus einen absoluten Pfad.
            const QString name = QFileInfo(QFile::symLinkTarget(fds.filePath(fd))).fileName();
            if (!name.startsWith(QLatin1String("socket:[")))
                continue;
            const QByteArray inode = name.mid(8, name.size() - 9).toLatin1();
            if (!inodes.contains(inode))
                continue;
            owner.pid = pid;
            owner.executable = QFile::symLinkTarget(proc.filePath(entry + QStringLiteral("/exe")));
            return owner;
        }
    }
    return owner;
}

QString portBusyMessage(int port, const PortOwner &owner, const QString &installedDir)
{
    QString who;
    if (owner.pid > 0) {
        const QFileInfo exe(owner.executable);
        if (exe.fileName() == QLatin1String("dss-core")) {
            const QString dir = QDir::cleanPath(installedDir) + QLatin1Char('/');
            who = owner.executable.startsWith(dir)
                      ? QStringLiteral("vermutlich von DoNs Streaming Studio im Einsatz")
                      : QStringLiteral("vermutlich von DoNs Streaming Studio in Arbeit (%1)")
                            .arg(owner.executable);
        } else {
            who = QStringLiteral("von «%1»").arg(exe.fileName().isEmpty() ? owner.executable
                                                                           : exe.fileName());
        }
        who += QStringLiteral(" (Prozess %1)").arg(owner.pid);
    } else {
        who = QStringLiteral("von einem Programm, das sich nicht feststellen lässt (vermutlich "
                             "eines anderen Benutzers)");
    }
    return QStringLiteral("Port %1 ist belegt, %2. Die Browserquelle ist nicht erreichbar. "
                          "Anderen Port im Dialog «Einstellungen» wählen oder das Programm beenden.")
        .arg(port).arg(who);
}

} // namespace dss
