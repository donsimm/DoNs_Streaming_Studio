// Prüft die Rechnung der Leinwand-Ansicht ohne Fenster: Einpassen, Zeigerposition, Lineale.

#include "canvasview.h"

#include <QTest>

using namespace dss;

class TestCanvasView : public QObject
{
    Q_OBJECT

private slots:
    void fitLimitedByHeight()
    {
        // Leinwand 1920x1080 in einer breiten Fläche: Höhe begrenzt, waagrecht in der Mitte.
        const CanvasFit fit = fitCanvas(QSize(1920, 1080), QSize(1236, 450), 8);
        QCOMPARE(fit.scale, 434.0 / 1080.0);
        QCOMPARE(fit.origin.y(), 8.0);
        QCOMPARE(fit.origin.x(), (1236 - 1920 * fit.scale) / 2.0);
    }

    void fitLimitedByWidth()
    {
        const CanvasFit fit = fitCanvas(QSize(1920, 1080), QSize(976, 1000), 8);
        QCOMPARE(fit.scale, 0.5);
        QCOMPARE(fit.origin.x(), 8.0);
        QCOMPARE(fit.origin.y(), (1000 - 540) / 2.0);
    }

    void fitFollowsAreaSize_data()
    {
        QTest::addColumn<QSize>("canvas");
        QTest::addColumn<QSize>("area");
        QTest::newRow("1080p klein") << QSize(1920, 1080) << QSize(300, 200);
        QTest::newRow("1080p gross") << QSize(1920, 1080) << QSize(3000, 2000);
        QTest::newRow("hochkant") << QSize(1080, 1920) << QSize(1236, 450);
        QTest::newRow("quadratisch") << QSize(512, 512) << QSize(800, 400);
        QTest::newRow("1 Pixel") << QSize(1, 1) << QSize(100, 50);
    }

    void fitFollowsAreaSize()
    {
        // Für jede Fläche: Die Leinwand liegt ganz innerhalb und berührt den Abstand
        // auf mindestens einer Achse.
        QFETCH(QSize, canvas);
        QFETCH(QSize, area);
        const int margin = 8;
        const CanvasFit fit = fitCanvas(canvas, area, margin);
        const double w = canvas.width() * fit.scale;
        const double h = canvas.height() * fit.scale;
        QVERIFY(fit.scale > 0);
        QVERIFY(fit.origin.x() >= margin - 1e-9);
        QVERIFY(fit.origin.y() >= margin - 1e-9);
        QVERIFY(fit.origin.x() + w <= area.width() - margin + 1e-9);
        QVERIFY(fit.origin.y() + h <= area.height() - margin + 1e-9);
        QVERIFY(qFuzzyCompare(w, double(area.width() - 2 * margin))
                || qFuzzyCompare(h, double(area.height() - 2 * margin)));
        QCOMPARE(fit.origin.x() * 2 + w, double(area.width()));
        QCOMPARE(fit.origin.y() * 2 + h, double(area.height()));
    }

    void fitSmallAndEmptyArea()
    {
        // Fläche kleiner als der doppelte Abstand: ohne Abstand einpassen.
        CanvasFit fit = fitCanvas(QSize(1920, 1080), QSize(10, 10), 8);
        QCOMPARE(fit.scale, 10.0 / 1920.0);
        fit = fitCanvas(QSize(1920, 1080), QSize(0, 300), 8);
        QCOMPARE(fit.scale, 0.0);
        QPoint p;
        QVERIFY(!canvasPixelAt(fit, QSize(1920, 1080), QPointF(0, 0), &p));
    }

    void pixelAt()
    {
        const QSize canvas(1920, 1080);
        const CanvasFit fit = fitCanvas(canvas, QSize(976, 1000), 8); // scale 0.5, origin (8, 230)
        QPoint p;
        QVERIFY(canvasPixelAt(fit, canvas, QPointF(8, 230), &p));
        QCOMPARE(p, QPoint(0, 0));
        QVERIFY(canvasPixelAt(fit, canvas, QPointF(8.49, 230.49), &p));
        QCOMPARE(p, QPoint(0, 0));
        QVERIFY(canvasPixelAt(fit, canvas, QPointF(8.5, 230.5), &p));
        QCOMPARE(p, QPoint(1, 1));
        QVERIFY(canvasPixelAt(fit, canvas, QPointF(8 + 480, 230 + 270), &p));
        QCOMPARE(p, QPoint(960, 540));
        QVERIFY(canvasPixelAt(fit, canvas, QPointF(967.9, 769.9), &p));
        QCOMPARE(p, QPoint(1919, 1079));
        // ausserhalb: links, oben, rechts, unten
        QVERIFY(!canvasPixelAt(fit, canvas, QPointF(7.9, 300), &p));
        QVERIFY(!canvasPixelAt(fit, canvas, QPointF(100, 229.9), &p));
        QVERIFY(!canvasPixelAt(fit, canvas, QPointF(968, 300), &p));
        QVERIFY(!canvasPixelAt(fit, canvas, QPointF(100, 770), &p));
    }

    void positionText()
    {
        QCOMPARE(pointerPositionText(true, QPoint(12, 1079)), QStringLiteral("X: 12  Y: 1079"));
        QCOMPARE(pointerPositionText(false, QPoint(12, 34)), QStringLiteral("X: –  Y: –"));
    }

    void majorStep_data()
    {
        QTest::addColumn<double>("scale");
        QTest::addColumn<int>("expected");
        QTest::newRow("Bild des Nutzers") << 0.39 << 200; // 100 → 39 px, 200 → 78 px
        QTest::newRow("1:1") << 1.0 << 100;
        QTest::newRow("klein") << 0.05 << 2000;
        QTest::newRow("gross") << 20.0 << 5;
        QTest::newRow("sehr gross") << 100.0 << 1;
        QTest::newRow("genau 60") << 0.6 << 100;
        QTest::newRow("leer") << 0.0 << 0;
    }

    void majorStep()
    {
        QFETCH(double, scale);
        QFETCH(int, expected);
        QCOMPARE(rulerMajorStep(scale, 60.0), expected);
    }

    void ticks()
    {
        // Wie auf dem Bild des Nutzers: alle 200 Pixel beschriftet, kleine Striche alle 20.
        const QList<RulerTick> t = rulerTicks(0.39, 10.0, 1920, 60.0, 4.0);
        QCOMPARE(t.size(), 1920 / 20 + 1);
        QCOMPARE(t.first().value, 0);
        QCOMPARE(t.first().kind, RulerTick::Major);
        QCOMPARE(t.first().position, 10.0);
        QCOMPARE(t.at(5).value, 100);
        QCOMPARE(t.at(5).kind, RulerTick::Medium);
        QCOMPARE(t.at(1).kind, RulerTick::Minor);
        QCOMPARE(t.at(10).value, 200);
        QCOMPARE(t.at(10).kind, RulerTick::Major);
        QCOMPARE(t.at(10).position, 10.0 + 200 * 0.39);
        QCOMPARE(t.last().value, 1920);
        int majors = 0;
        for (const RulerTick &tick : t)
            majors += tick.kind == RulerTick::Major;
        QCOMPARE(majors, 10); // 0, 200, …, 1800
    }

    void ticksCoarse()
    {
        // Kleine Striche zu eng für 10 Teile: dann 5 oder 2 Teile, ohne mittleren Strich.
        // Hauptschritt 100 bei scale 0.6: 10 Teile = 6 px, 5 Teile = 12 px (beide < 15),
        // 2 Teile = 30 px → Striche bei 0, 50, …, 300.
        QList<RulerTick> t = rulerTicks(0.6, 0.0, 300, 60.0, 15.0);
        QCOMPARE(t.size(), 7);
        QCOMPARE(t.at(1).value, 50);
        QCOMPARE(t.at(1).kind, RulerTick::Minor);
        // Hauptschritt 1: keine Unterteilung möglich
        t = rulerTicks(100.0, 0.0, 3, 60.0, 4.0);
        QCOMPARE(t.size(), 4);
        for (const RulerTick &tick : t)
            QCOMPARE(tick.kind, RulerTick::Major);
    }
};

QTEST_GUILESS_MAIN(TestCanvasView)
#include "tst_canvasview.moc"
