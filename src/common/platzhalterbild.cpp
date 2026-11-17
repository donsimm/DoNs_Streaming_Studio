// Zeichnet das neutrale Platzhalterbild des Overlays «Chat-Bot» (P14 f2, Plan 7) beim Bau: grauer
// Kreis mit einer hellen Figur (Kopf und Schultern), aussen durchsichtig, 256 × 256 Pixel. Kein
// Markenlogo, keine Schrift. Aufruf: dss-platzhalterbild <Datei.png>
// Ohne QGuiApplication: Zeichnen auf ein QImage braucht keine Plattform.
// https://doc.qt.io/qt-6.8/qpainter.html, https://doc.qt.io/qt-6.8/qimage.html

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <cstdio>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        std::fprintf(stderr, "Aufruf: dss-platzhalterbild <Datei.png>\n");
        return 2;
    }
    constexpr int size = 256;
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        QPainterPath circle;
        circle.addEllipse(QRectF(0, 0, size, size));
        p.fillPath(circle, QColor(0x5F, 0x66, 0x70));
        p.setClipPath(circle);
        p.setBrush(QColor(0xC9, 0xCD, 0xD2));
        p.drawEllipse(QPointF(128, 100), 46, 46);  // Kopf
        p.drawEllipse(QPointF(128, 250), 88, 84);  // Schultern
    }
    if (!image.save(QString::fromLocal8Bit(argv[1]), "PNG")) {
        std::fprintf(stderr, "%s kann nicht geschrieben werden\n", argv[1]);
        return 1;
    }
    return 0;
}
