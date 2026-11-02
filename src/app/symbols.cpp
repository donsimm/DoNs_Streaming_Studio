#include "symbols.h"

#include <QFileInfo>

QString symbolDir()
{
    // DSS_SYMBOL_DIR kommt aus src/app/CMakeLists.txt (Ordner im Quellbaum).
    return QStringLiteral(DSS_SYMBOL_DIR);
}

QIcon loadSymbol(const QString &fileName)
{
    const QString path = symbolDir() + QLatin1Char('/') + fileName;
    if (!QFileInfo::exists(path)) {
        qWarning("Symbol fehlt: %s", qPrintable(path));
        return {};
    }
    return QIcon(path);
}
