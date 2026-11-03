#include "canvasimage.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace dss {

QString canvasImageDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
           + QStringLiteral("/DoNs Streaming Studio");
}

QString canvasImageFileName(const QString &address, const QDateTime &time)
{
    return address + QLatin1Char('_') + time.toString(QStringLiteral("yyyy-MM-dd_hh-mm-ss"))
           + QStringLiteral(".png");
}

QString uniqueCanvasImagePath(const QString &dir, const QString &address, const QDateTime &time)
{
    const QString name = canvasImageFileName(address, time);
    QString path = QDir(dir).filePath(name);
    const QString stem = name.chopped(4); // ohne «.png»
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QDir(dir).filePath(QStringLiteral("%1-%2.png").arg(stem).arg(n));
    return path;
}

} // namespace dss
