#include "symbols.h"

#include <QFileInfo>

QIcon loadSymbol(const QString &fileName)
{
    // DSS_SYMBOL_DIR kommt aus src/app/CMakeLists.txt (Ordner im Quellbaum).
    const QString path = QStringLiteral(DSS_SYMBOL_DIR "/") + fileName;
    if (!QFileInfo::exists(path)) {
        qWarning("Symbol fehlt: %s", qPrintable(path));
        return {};
    }
    return QIcon(path);
}
