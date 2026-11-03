#include "configpaths.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace dss {

Edition buildEdition()
{
#if DSS_INSTALLED_EDITION
    return Edition::Installed;
#else
    return Edition::Development;
#endif
}

QString editionLabel(Edition edition)
{
    return edition == Edition::Installed ? QStringLiteral("Einsatz") : QStringLiteral("Entwicklung");
}

QString appDirName(Edition edition)
{
    return edition == Edition::Installed ? QStringLiteral("dons-streaming-studio")
                                         : QStringLiteral("dons-streaming-studio-entwicklung");
}

QString configDir(Edition edition)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QLatin1Char('/') + appDirName(edition);
}

QString userSymbolDir()
{
    return configDir(Edition::Installed) + QStringLiteral("/symbole");
}

QString defaultConnectionPath(Edition edition)
{
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QLatin1Char('/')
           + appDirName(edition) + QStringLiteral("/kern.sock");
}

QString coreUnitName(Edition edition)
{
    return edition == Edition::Installed ? QStringLiteral("dss-core.service")
                                         : QStringLiteral("dss-core-entwicklung.service");
}

QString windowTitle(Edition edition)
{
    return edition == Edition::Installed ? QStringLiteral("DoNs Streaming Studio")
                                         : QStringLiteral("DoNs Streaming Studio – Entwicklung");
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
