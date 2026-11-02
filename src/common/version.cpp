#include "version.h"

namespace dss {

QString version()
{
    return QStringLiteral(DSS_VERSION);
}

QString versionLine(const QString &programName)
{
    return programName + QLatin1Char(' ') + version();
}

} // namespace dss
