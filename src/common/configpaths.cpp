#include "configpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace dss {

QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/dons-streaming-studio");
}

QString userSymbolDir()
{
    return configDir() + QStringLiteral("/symbole");
}

QString defaultConnectionPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
           + QStringLiteral("/dons-streaming-studio/kern.sock");
}

QString findSymbolFile(const QString &fileName, const QStringList &dirs)
{
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/')))
        return {};
    for (const QString &dir : dirs) {
        const QString path = QDir(dir).filePath(fileName);
        if (QFileInfo(path).isFile())
            return path;
    }
    return {};
}

QString ndiTrademarkNotice()
{
    return QStringLiteral("NDI® is a registered trademark of Vizrt NDI AB");
}

} // namespace dss
