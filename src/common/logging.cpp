#include "logging.h"

#include "version.h"

#include <QtGlobal>

namespace dss {

void setupLogging()
{
    // https://doc.qt.io/qt-6.8/qtlogging.html#qSetMessagePattern
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd hh:mm:ss.zzz} %{type} %{message}"));
}

QString startLine(const QString &programName)
{
    return QStringLiteral("%1 %2 gestartet, Qt %3")
        .arg(programName, version(), QString::fromLatin1(qVersion()));
}

} // namespace dss
