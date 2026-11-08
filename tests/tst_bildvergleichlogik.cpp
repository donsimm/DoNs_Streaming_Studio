// Vergleichslogik des Bildvergleichs (P11, K-08) ohne Fenster: Masszahlen, Kantenmaske,
// vormultipliziert, Bereich, Differenzbild. Nur künstliche Bilder mit bekanntem Ergebnis.

#include "bildvergleich.h"

#include <QTest>

using namespace bildvergleich;

namespace {

// 100 x 60, links (x < split) rot, rechts blau, deckend
QImage halves(int split)
{
    QImage image(100, 60, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixel(x, y, x < split ? qRgb(255, 0, 0) : qRgb(0, 0, 255));
    return image;
}

// Wert der Maske (Grayscale8; pixelIndex gilt nur für Bilder mit Palette)
int at(const QImage &mask, int x, int y)
{
    return mask.constScanLine(y)[x];
}

} // namespace

class TestBildvergleichLogik : public QObject
{
    Q_OBJECT

private slots:
    void identicalImagesGiveZero()
    {
        const QImage a = halves(50);
        const QImage mask = edgeMask(a, a);
        const Measure m = measure(a, a, mask, a.rect());
        QCOMPARE(m.mean, 0.0);
        QCOMPARE(m.maxOutsideEdges, 0);
        QCOMPARE(m.edgeOver32, 0.0);
        QCOMPARE(m.pixels, qint64(6000));
        QCOMPARE(m.edgePixels, qint64(4 * 60)); // Spalten 48 bis 51
    }

    // Sprung zwischen x = 49 und 50: beide Pixel, erweitert um 1 → Spalten 48 bis 51
    void edgeMaskCoversJumpPlusOnePixel()
    {
        const QImage mask = edgeMask(halves(50), halves(50));
        QCOMPARE(mask.format(), QImage::Format_Grayscale8);
        for (int x = 0; x < 100; ++x) {
            const bool edge = x >= 48 && x <= 51;
            QCOMPARE(at(mask,x, 30) == 255, edge);
            QCOMPARE(at(mask,x, 0) == 255, edge);
        }
    }

    // Abweichung im Inneren eines Feldes: ausserhalb der Kanten sichtbar
    void interiorChangeOutsideEdges()
    {
        const QImage a = halves(50);
        QImage b = a;
        for (int y = 10; y < 20; ++y)
            for (int x = 10; x < 20; ++x)
                b.setPixel(x, y, qRgb(247, 0, 0));
        const Measure m = measure(a, b, edgeMask(a, b), a.rect());
        QCOMPARE(m.maxOutsideEdges, 8);
        QCOMPARE(m.edgeOver32, 0.0);
        QCOMPARE(m.mean, 8.0 * 100 / (6000 * 4));
        // Im Bereich ohne die Änderung: nichts
        QCOMPARE(measure(a, b, edgeMask(a, b), QRect(60, 0, 40, 60)).maxOutsideEdges, 0);
    }

    // 1 Pixel verschoben: nur an der Kante, dort deutlich
    void shiftedEdgeOnlyAtEdges()
    {
        const QImage a = halves(50);
        const QImage b = halves(51);
        const QImage mask = edgeMask(a, b); // Sprünge 49/50 und 50/51 → Spalten 48 bis 52
        const Measure m = measure(a, b, mask, a.rect());
        QCOMPARE(m.maxOutsideEdges, 0);
        QCOMPARE(m.edgePixels, qint64(5 * 60));
        QCOMPARE(m.edgeOver32, 100.0 * 60 / 300); // Spalte 50 ist rot statt blau
        QCOMPARE(m.mean, 255.0 * 2 * 60 / (6000 * 4));
    }

    // Reiner Farbsprung (gleiche Helligkeit) und reiner Alphasprung zählen als Kante
    void colorAndAlphaJumpsAreEdges()
    {
        QImage color(10, 4, QImage::Format_ARGB32_Premultiplied);
        QImage alpha(10, 4, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 10; ++x) {
                color.setPixel(x, y, x < 5 ? qRgb(150, 100, 100) : qRgb(100, 125, 100));
                alpha.setPixel(x, y, x < 5 ? qRgba(0, 0, 0, 0) : qRgba(255, 255, 255, 255));
            }
        }
        for (const QImage &image : {color, alpha}) {
            const QImage mask = edgeMask(image, image);
            QCOMPARE(at(mask,4, 2), 255);
            QCOMPARE(at(mask,5, 2), 255);
            QCOMPARE(at(mask,2, 2), 0);
        }
        // Sprung von höchstens kEdgeJump ist keine Kante
        QImage soft(10, 4, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 10; ++x)
                soft.setPixel(x, y, x < 5 ? qRgb(100, 100, 100) : qRgb(100 + kEdgeJump, 100, 100));
        QCOMPARE(at(edgeMask(soft, soft), 4, 2), 0);
    }

    // Eine Kante nur im zweiten Bild zählt ebenso
    void edgeOfEitherImage()
    {
        QImage flat(20, 4, QImage::Format_ARGB32_Premultiplied);
        flat.fill(qRgb(0, 0, 0));
        QImage step = flat;
        for (int y = 0; y < 4; ++y)
            for (int x = 10; x < 20; ++x)
                step.setPixel(x, y, qRgb(200, 200, 200));
        const QImage mask = edgeMask(flat, step);
        QCOMPARE(at(mask,9, 1), 255);
        QCOMPARE(at(mask,10, 1), 255);
        const Measure m = measure(flat, step, mask, flat.rect());
        QCOMPARE(m.maxOutsideEdges, 200); // Fläche rechts weicht ab
    }

    // Vormultipliziert: Alpha 128 gegen 136 bei Weiss weicht in allen Kanälen um 8 ab;
    // dasselbe Bild in einem anderen Format weicht nicht ab.
    void premultipliedChannels()
    {
        QImage a(8, 8, QImage::Format_ARGB32);
        a.fill(qRgba(255, 255, 255, 128));
        QImage b(8, 8, QImage::Format_ARGB32);
        b.fill(qRgba(255, 255, 255, 136));
        const Measure m = measure(a, b, edgeMask(a, b), a.rect());
        QCOMPARE(m.maxOutsideEdges, 8);
        QCOMPARE(m.mean, 8.0);
        const QImage converted = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(measure(a, converted, edgeMask(a, converted), a.rect()).mean, 0.0);
    }

    void areaIsClippedToImage()
    {
        const QImage a = halves(50);
        const QImage b = halves(51);
        const QImage mask = edgeMask(a, b);
        const Measure inside = measure(a, b, mask, QRect(40, 0, 20, 60));
        const Measure beyond = measure(a, b, mask, QRect(40, -10, 200, 200));
        QCOMPARE(beyond.pixels, qint64(60 * 60));
        QCOMPARE(beyond.edgePixels, inside.edgePixels);
        const Measure none = measure(a, b, mask, QRect(200, 200, 10, 10));
        QCOMPARE(none.pixels, qint64(0));
        QCOMPARE(none.mean, 0.0);
    }

    // Zusammenfassen je Quellenart
    void combineWeighted()
    {
        Measure a;
        a.mean = 1.0;
        a.maxOutsideEdges = 5;
        a.edgeOver32 = 10.0;
        a.pixels = 100;
        a.edgePixels = 10;
        Measure b;
        b.mean = 4.0;
        b.maxOutsideEdges = 3;
        b.edgeOver32 = 40.0;
        b.pixels = 300;
        b.edgePixels = 30;
        const Measure all = combine({a, b});
        QCOMPARE(all.pixels, qint64(400));
        QCOMPARE(all.edgePixels, qint64(40));
        QCOMPARE(all.maxOutsideEdges, 5);
        QCOMPARE(all.mean, (1.0 * 100 + 4.0 * 300) / 400);
        QCOMPARE(all.edgeOver32, (10.0 * 10 + 40.0 * 30) / 40);
        QCOMPARE(combine({}).pixels, qint64(0));
        QCOMPARE(combine({}).mean, 0.0);
    }

    void differentSizesGiveNothing()
    {
        const QImage a = halves(50);
        const QImage small(10, 10, QImage::Format_ARGB32_Premultiplied);
        QVERIFY(edgeMask(a, small).isNull());
        QCOMPARE(measure(a, small, edgeMask(a, a), a.rect()).pixels, qint64(0));
        QCOMPARE(compare(a, small, a.rect()).max, 0);
        QVERIFY(differenceImage(a, small).isNull());
    }

    // Masszahl ohne Maske wie seit P10 in tst_browserquelle
    void compareWithoutMask()
    {
        const Difference d = compare(halves(50), halves(51), QRect(0, 0, 100, 60));
        QCOMPARE(d.max, 255);
        QCOMPARE(d.over32, 100.0 * 60 / 6000);
        QCOMPARE(d.mean, 255.0 * 2 * 60 / (6000 * 4));
    }

    void differenceImageShowsDeviation()
    {
        const QImage a = halves(50);
        QImage b = a;
        b.setPixel(10, 10, qRgb(245, 0, 0));
        const QImage mask = edgeMask(a, b);
        const QImage diff = differenceImage(a, b, mask, 4);
        QCOMPARE(diff.size(), a.size());
        QCOMPARE(diff.pixel(10, 10), qRgb(40, 40, 40));
        QCOMPARE(diff.pixel(20, 20), qRgb(0, 0, 0));
        QCOMPARE(diff.pixel(49, 20), qRgb(0, 0, 96)); // Kante ohne Abweichung: blau
    }

    void onBlackAndOpaque()
    {
        QImage a(4, 4, QImage::Format_ARGB32);
        a.fill(qRgba(200, 100, 50, 128));
        QVERIFY(!fullyOpaque(a));
        const QImage black = onBlack(a);
        QVERIFY(fullyOpaque(black));
        const QRgb p = black.pixel(1, 1);
        QCOMPARE(qAlpha(p), 255);
        QVERIFY(std::abs(qRed(p) - 100) <= 1);
        QVERIFY(std::abs(qGreen(p) - 50) <= 1);
        QVERIFY(std::abs(qBlue(p) - 25) <= 1);
    }
};

QTEST_GUILESS_MAIN(TestBildvergleichLogik)
#include "tst_bildvergleichlogik.moc"
