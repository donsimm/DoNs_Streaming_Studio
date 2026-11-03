#include "translation.h"

#include <QCoreApplication>
#include <QTranslator>

QTranslator *installQtTranslation(QCoreApplication *app)
{
    // DSS_TRANSLATION_DIR kommt aus src/app/CMakeLists.txt (Ordner im Build-Ordner).
    auto *translator = new QTranslator(app);
    const QString dir = QStringLiteral(DSS_TRANSLATION_DIR);
    if (!translator->load(QStringLiteral("qtbase_de.qm"), dir)) {
        qWarning("Übersetzung von Qt nicht geladen: %s/qtbase_de.qm", qPrintable(dir));
        delete translator;
        return nullptr;
    }
    QCoreApplication::installTranslator(translator);
    return translator;
}
