#include "overlayimage.h"

#include "canvasimage.h"
#include "sourcerenderer.h"

#include <QDir>
#include <QPainter>
#include <QSaveFile>

namespace dss {

QImage renderOverlayImage(const SourceRenderer &renderer, const Overlay &overlay)
{
    QImage image(overlay.width, overlay.height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    // Wie die Leinwand, aber Ecke (0,0) des Bildes und 1 Bild-Pixel je Pixel des Overlays; der
    // Rand des Bildes beschneidet wie die Leinwand.
    renderer.paint(&painter, overlay, CanvasFit{1.0, QPointF(0, 0)});
    painter.end();
    return image;
}

bool saveOverlayImage(const SourceRenderer &renderer, const Overlay &overlay, const QString &dir,
                      const QDateTime &time, QString *path, QString *error)
{
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("Ordner %1 kann nicht angelegt werden.").arg(dir);
        return false;
    }
    const QImage image = renderOverlayImage(renderer, overlay);
    const QString target = uniqueCanvasImagePath(dir, overlay.address, time);
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
        *error = QStringLiteral("%1 kann nicht gespeichert werden (%2).").arg(target, file.errorString());
        return false;
    }
    *path = target;
    return true;
}

} // namespace dss
