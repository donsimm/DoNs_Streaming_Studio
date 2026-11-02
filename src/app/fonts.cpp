#include "fonts.h"

#include <QDir>
#include <QFontDatabase>

QString fontDir()
{
    // DSS_FONT_DIR kommt aus src/app/CMakeLists.txt (Ordner im Quellbaum).
    return QStringLiteral(DSS_FONT_DIR);
}

QStringList loadWindowFonts(const QString &dir)
{
    QStringList families;
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")},
                                                  QDir::Files, QDir::Name);
    if (files.isEmpty())
        qWarning("Keine Schriftdatei in %s", qPrintable(dir));
    for (const QString &file : files) {
        const QString path = dir + QLatin1Char('/') + file;
        const int id = QFontDatabase::addApplicationFont(path);
        if (id < 0) {
            qWarning("Schrift nicht geladen: %s", qPrintable(path));
            continue;
        }
        for (const QString &family : QFontDatabase::applicationFontFamilies(id)) {
            if (!families.contains(family))
                families << family;
        }
    }
    return families;
}
