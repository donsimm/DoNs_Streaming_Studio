#include "bildvergleich.h"

#include <algorithm>
#include <cstdlib>

namespace bildvergleich {

namespace {

QImage premultiplied(const QImage &image)
{
    return image.format() == QImage::Format_ARGB32_Premultiplied
               ? image
               : image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

const QRgb *line(const QImage &image, int y)
{
    return reinterpret_cast<const QRgb *>(image.constScanLine(y));
}

// Grösste Abweichung eines Kanals (R, G, B, A)
int channelMax(QRgb p, QRgb q)
{
    return std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                     std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
}

int channelSum(QRgb p, QRgb q)
{
    return std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q))
           + std::abs(qBlue(p) - qBlue(q)) + std::abs(qAlpha(p) - qAlpha(q));
}

// Sprünge eines Bildes in die Maske eintragen (beide Pixel des Sprungs)
void markJumps(const QImage &image, int jump, QImage *mask)
{
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *row = line(image, y);
        const QRgb *below = y + 1 < image.height() ? line(image, y + 1) : nullptr;
        uchar *m = mask->scanLine(y);
        uchar *mBelow = below ? mask->scanLine(y + 1) : nullptr;
        for (int x = 0; x < image.width(); ++x) {
            if (x + 1 < image.width() && channelMax(row[x], row[x + 1]) > jump) {
                m[x] = 255;
                m[x + 1] = 255;
            }
            if (below && channelMax(row[x], below[x]) > jump) {
                m[x] = 255;
                mBelow[x] = 255;
            }
        }
    }
}

} // namespace

Difference compare(const QImage &a, const QImage &b, const QRect &area)
{
    Difference d;
    if (a.size() != b.size())
        return d;
    const QImage pa = premultiplied(a);
    const QImage pb = premultiplied(b);
    qint64 sum = 0;
    qint64 count = 0;
    qint64 over = 0;
    const QRect r = area.intersected(pa.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const QRgb *la = line(pa, y);
        const QRgb *lb = line(pb, y);
        for (int x = r.left(); x <= r.right(); ++x) {
            const int m = channelMax(la[x], lb[x]);
            sum += channelSum(la[x], lb[x]);
            d.max = std::max(d.max, m);
            over += m > kLargeDeviation ? 1 : 0;
            ++count;
        }
    }
    if (count > 0) {
        d.mean = double(sum) / double(count * 4);
        d.over32 = 100.0 * double(over) / double(count);
    }
    return d;
}

QImage edgeMask(const QImage &a, const QImage &b, int jump)
{
    if (a.size() != b.size())
        return {};
    QImage jumps(a.size(), QImage::Format_Grayscale8);
    jumps.fill(0);
    markJumps(premultiplied(a), jump, &jumps);
    markJumps(premultiplied(b), jump, &jumps);
    // um 1 Pixel erweitern (3 x 3)
    QImage mask(a.size(), QImage::Format_Grayscale8);
    mask.fill(0);
    const int w = a.width();
    const int h = a.height();
    for (int y = 0; y < h; ++y) {
        const uchar *j = jumps.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            if (!j[x])
                continue;
            for (int yy = std::max(0, y - 1); yy <= std::min(h - 1, y + 1); ++yy) {
                uchar *m = mask.scanLine(yy);
                for (int xx = std::max(0, x - 1); xx <= std::min(w - 1, x + 1); ++xx)
                    m[xx] = 255;
            }
        }
    }
    return mask;
}

Measure measure(const QImage &a, const QImage &b, const QImage &mask, const QRect &area)
{
    Measure d;
    if (a.size() != b.size() || mask.size() != a.size())
        return d;
    const QImage pa = premultiplied(a);
    const QImage pb = premultiplied(b);
    qint64 sum = 0;
    qint64 edgeOver = 0;
    const QRect r = area.intersected(pa.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        const QRgb *la = line(pa, y);
        const QRgb *lb = line(pb, y);
        const uchar *m = mask.constScanLine(y);
        for (int x = r.left(); x <= r.right(); ++x) {
            const int dev = channelMax(la[x], lb[x]);
            sum += channelSum(la[x], lb[x]);
            ++d.pixels;
            if (m[x]) {
                ++d.edgePixels;
                edgeOver += dev > kLargeDeviation ? 1 : 0;
            } else {
                d.maxOutsideEdges = std::max(d.maxOutsideEdges, dev);
            }
        }
    }
    if (d.pixels > 0)
        d.mean = double(sum) / double(d.pixels * 4);
    if (d.edgePixels > 0)
        d.edgeOver32 = 100.0 * double(edgeOver) / double(d.edgePixels);
    return d;
}

Measure combine(const QList<Measure> &parts)
{
    Measure all;
    double meanSum = 0;
    double edgeSum = 0;
    for (const Measure &m : parts) {
        all.pixels += m.pixels;
        all.edgePixels += m.edgePixels;
        all.maxOutsideEdges = std::max(all.maxOutsideEdges, m.maxOutsideEdges);
        meanSum += m.mean * double(m.pixels);
        edgeSum += m.edgeOver32 * double(m.edgePixels);
    }
    if (all.pixels > 0)
        all.mean = meanSum / double(all.pixels);
    if (all.edgePixels > 0)
        all.edgeOver32 = edgeSum / double(all.edgePixels);
    return all;
}

QImage differenceImage(const QImage &a, const QImage &b, const QImage &mask, int gain)
{
    if (a.size() != b.size())
        return {};
    const QImage pa = premultiplied(a);
    const QImage pb = premultiplied(b);
    const bool withMask = mask.size() == a.size();
    QImage out(a.size(), QImage::Format_ARGB32);
    for (int y = 0; y < out.height(); ++y) {
        const QRgb *la = line(pa, y);
        const QRgb *lb = line(pb, y);
        const uchar *m = withMask ? mask.constScanLine(y) : nullptr;
        QRgb *o = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            const int v = std::min(255, channelMax(la[x], lb[x]) * gain);
            o[x] = m && m[x] ? qRgb(v, v, std::max(v, 96)) : qRgb(v, v, v);
        }
    }
    return out;
}

QImage onBlack(const QImage &image)
{
    QImage out = premultiplied(image).copy();
    for (int y = 0; y < out.height(); ++y) {
        QRgb *l = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
            l[x] = qRgb(qRed(l[x]), qGreen(l[x]), qBlue(l[x])); // vormultipliziert = auf Schwarz
    }
    return out;
}

bool fullyOpaque(const QImage &image)
{
    const QImage p = premultiplied(image);
    for (int y = 0; y < p.height(); ++y) {
        const QRgb *l = line(p, y);
        for (int x = 0; x < p.width(); ++x)
            if (qAlpha(l[x]) != 255)
                return false;
    }
    return true;
}

} // namespace bildvergleich
