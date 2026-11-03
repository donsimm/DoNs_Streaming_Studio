#include "version.h"

#include "configpaths.h"

namespace dss {

QString version()
{
    return QStringLiteral(DSS_VERSION);
}

QString commit()
{
    return QStringLiteral(DSS_COMMIT);
}

QString buildDetails()
{
    QString details = editionLabel();
    if (!commit().isEmpty())
        details += QStringLiteral(", Commit ") + commit();
    return details;
}

QString versionLine(const QString &programName)
{
    return programName + QLatin1Char(' ') + version() + QStringLiteral(" (") + buildDetails()
           + QLatin1Char(')');
}

} // namespace dss
