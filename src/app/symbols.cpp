#include "symbols.h"

#include "configpaths.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

QString symbolDir()
{
    // DSS_SYMBOL_DIR kommt aus src/app/CMakeLists.txt (Ordner im Quellbaum).
    return QStringLiteral(DSS_SYMBOL_DIR);
}

QString symbolPath(const QString &fileName)
{
    const QString found = dss::findSymbolFile(fileName, {dss::userSymbolDir(), symbolDir()});
    return found.isEmpty() ? symbolDir() + QLatin1Char('/') + fileName : found;
}

bool hasUserSymbol(const QString &fileName)
{
    return !dss::findSymbolFile(fileName, {dss::userSymbolDir()}).isEmpty();
}

QList<int> appIconSizes()
{
    return {16, 22, 24, 32, 48, 64, 128, 256, 512};
}

QIcon appIcon(const QString &dir)
{
    QIcon icon;
    for (const int size : appIconSizes()) {
        const QString path = dir + QStringLiteral("/dons-streaming-studio-%1.png").arg(size);
        if (QFileInfo(path).isFile())
            icon.addFile(path, QSize(size, size));
    }
    if (icon.isNull())
        qWarning("Programm-Symbol fehlt: %s", qPrintable(dir));
    return icon;
}

QImage tintedSymbol(const QString &path, const QSize &size, const QColor &color)
{
    // SVG über QIcon (Paket qt6-svg-plugins). CompositionMode_SourceIn: Farbe von color,
    // Deckkraft vom Ziel, also vom gezeichneten Symbol.
    // https://doc.qt.io/qt-6.8/qpainter.html#CompositionMode-enum
    QImage image = QIcon(path).pixmap(size).toImage();
    if (image.isNull())
        return {};
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(image.rect(), color);
    painter.end();
    return image;
}

QIcon loadSymbol(const QString &fileName, const QSize &size)
{
    const QString path = symbolPath(fileName);
    if (!QFileInfo::exists(path)) {
        qWarning("Symbol fehlt: %s", qPrintable(path));
        return {};
    }
    // Farben aus der Palette, die aus dem Theme kommt (src/app/theme.cpp).
    const QPalette palette = QGuiApplication::palette();
    QIcon icon;
    icon.addPixmap(QPixmap::fromImage(
                       tintedSymbol(path, size, palette.color(QPalette::Normal, QPalette::ButtonText))),
                   QIcon::Normal);
    icon.addPixmap(QPixmap::fromImage(tintedSymbol(
                       path, size, palette.color(QPalette::Disabled, QPalette::ButtonText))),
                   QIcon::Disabled);
    if (icon.isNull())
        qWarning("Symbol nicht lesbar: %s", qPrintable(path));
    return icon;
}
