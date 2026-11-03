// Rechnungen des Zeichners (P09, Teil B) ohne Qt Gui: Overlay → Leinwand, Drehung um die Mitte,
// Treffer (gedreht, überlappend, verborgen), Textzeilen und Ausrichtung, Achsen variabler
// Schriften aus einer echten Datei (Urbanist[wght].ttf, tests/daten/schriften/urbanist).

#include "canvasview.h"
#include "sourcegeometry.h"

#include <QFile>
#include <QTest>

using namespace dss;

namespace {

Source box(int x, int y, int w, int h, int rotation = 0, bool visible = true)
{
    Source s;
    s.id = QUuid::createUuid();
    s.kind = SourceKind::Image;
    s.name = QStringLiteral("q");
    s.file = QStringLiteral("media/x.png");
    s.x = x;
    s.y = y;
    s.width = w;
    s.height = h;
    s.rotation = rotation;
    s.visible = visible;
    return s;
}

bool near(QPointF a, QPointF b, double eps = 1e-9)
{
    return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps;
}

} // namespace

class TestSourceGeometry : public QObject
{
    Q_OBJECT

private slots:
    // P09b, Entscheid 4: Inhalt im Kasten spiegeln; Kasten, Ecken und Treffer bleiben.
    void flipContent()
    {
        Source s;
        s.id = QUuid::createUuid();
        s.x = 100;
        s.y = 50;
        s.width = 200;
        s.height = 100;
        QCOMPARE(contentToSource(s, QPointF(10, 20)), QPointF(10, 20));
        QCOMPARE(flipScale(s), QPointF(1, 1));
        s.flipHorizontal = true;
        QCOMPARE(contentToSource(s, QPointF(10, 20)), QPointF(190, 20));
        QCOMPARE(flipScale(s), QPointF(-1, 1));
        s.flipVertical = true;
        QCOMPARE(contentToSource(s, QPointF(10, 20)), QPointF(190, 80));
        QCOMPARE(flipScale(s), QPointF(-1, -1));
        s.flipHorizontal = false;
        QCOMPARE(contentToSource(s, QPointF(0, 0)), QPointF(0, 100));
        // Kasten bleibt: Ecken und Treffer wie ohne Spiegeln, auch gedreht
        Source plain = s;
        plain.flipVertical = false;
        s.rotation = plain.rotation = 300;
        QCOMPARE(sourceCorners(s), sourceCorners(plain));
        QCOMPARE(sourceContains(s, QPointF(200, 100)), sourceContains(plain, QPointF(200, 100)));
        // Inhalt links oben, gespiegelt und gedreht: liegt an der gedrehten Ecke links unten
        const QPointF p = sourceToOverlay(s, contentToSource(s, QPointF(0, 0)));
        const QPointF expected = sourceCorners(plain)[3];
        QVERIFY(std::abs(p.x() - expected.x()) < 1e-9 && std::abs(p.y() - expected.y()) < 1e-9);
    }

    void overlayToCanvas()
    {
        // 1920x1080 in eine Fläche 976x556 mit Rand 8: Massstab 0.5, Leinwand in der Mitte.
        const CanvasFit fit = fitCanvas(QSize(1920, 1080), QSize(976, 556), 8);
        QCOMPARE(fit.scale, 0.5);
        QCOMPARE(overlayToArea(fit, QPointF(0, 0)), fit.origin);
        QCOMPARE(overlayToArea(fit, QPointF(1920, 1080)), fit.origin + QPointF(960, 540));
        QCOMPARE(areaToOverlay(fit, overlayToArea(fit, QPointF(123, 456))), QPointF(123, 456));
    }

    void rotationAroundCenter()
    {
        const Source s = box(100, 200, 400, 100, 900); // 90° im Uhrzeigersinn
        QCOMPARE(sourceCenter(s), QPointF(300, 250));
        const std::array<QPointF, 4> c = sourceCorners(s);
        // Links oben (100,200) dreht um (300,250) nach rechts oben: (350, 50)
        QVERIFY(near(c[0], QPointF(350, 50)));
        QVERIFY(near(c[1], QPointF(350, 450))); // rechts oben → rechts unten
        QVERIFY(near(c[2], QPointF(250, 450)));
        QVERIFY(near(c[3], QPointF(250, 50)));
        // Hin und zurück
        const QPointF local(37.5, 12.25);
        QVERIFY(near(overlayToSource(s, sourceToOverlay(s, local)), local));
        // Ohne Drehung: lokal = verschoben
        const Source flat = box(-20, 30, 256, 128);
        QVERIFY(near(sourceToOverlay(flat, QPointF(0, 0)), QPointF(-20, 30)));
        QVERIFY(near(sourceToOverlay(flat, QPointF(256, 128)), QPointF(236, 158)));
    }

    void hitTestRotated()
    {
        const Source s = box(0, 0, 200, 20, 450); // 45°
        QVERIFY(sourceContains(s, QPointF(100, 10)));        // Mitte
        QVERIFY(!sourceContains(s, QPointF(5, 5)));          // Ecke des ungedrehten Kastens
        QVERIFY(sourceContains(s, QPointF(100 + 60, 10 + 60))); // entlang der gedrehten Achse
        QVERIFY(!sourceContains(s, QPointF(190, 10)));       // ungedreht drin, gedreht draussen
        // Ränder gehören dazu
        const Source flat = box(10, 10, 5, 5);
        QVERIFY(sourceContains(flat, QPointF(10, 10)));
        QVERIFY(sourceContains(flat, QPointF(15, 15)));
        QVERIFY(!sourceContains(flat, QPointF(15.01, 15)));
    }

    void topmostOverlapping()
    {
        Overlay o;
        o.sources = {box(0, 0, 100, 100), box(50, 50, 100, 100), box(60, 60, 10, 10, 0, false)};
        QCOMPARE(topmostSourceAt(o, QPointF(10, 10)), 0);
        QCOMPARE(topmostSourceAt(o, QPointF(75, 75)), 1);   // 1 liegt über 0
        QCOMPARE(topmostSourceAt(o, QPointF(65, 65)), 1);   // 2 ist verborgen
        QCOMPARE(topmostSourceAt(o, QPointF(500, 500)), -1);
        QCOMPARE(drawnSources(o), (QList<int>{0, 1}));
        // Gedreht über einer anderen: nur wo die gedrehte wirklich liegt
        o.sources[1].rotation = 450;
        QCOMPARE(topmostSourceAt(o, QPointF(52, 52)), 0); // Ecke von 1 ist weggedreht
        QCOMPARE(topmostSourceAt(o, QPointF(100, 100)), 1);
    }

    void textLinesAndAlign()
    {
        QCOMPARE(textLines(QStringLiteral("Willkommen\nim Stream")),
                 (QStringList{QStringLiteral("Willkommen"), QStringLiteral("im Stream")}));
        QCOMPARE(textLines(QStringLiteral("eine sehr lange Zeile ohne Umbruch")).size(), 1);
        QCOMPARE(textLines(QStringLiteral("a\n\nb")).size(), 3); // leere Zeile bleibt
        QCOMPARE(alignedLineX(TextAlign::Left, 800, 300), 0.0);
        QCOMPARE(alignedLineX(TextAlign::Center, 800, 300), 250.0);
        QCOMPARE(alignedLineX(TextAlign::Right, 800, 300), 500.0);
    }

    void variableFontAxes()
    {
        QFile f(QStringLiteral(DSS_TEST_FONT_DATA "/urbanist/Urbanist[wght].ttf"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray file = f.readAll();
        const QList<FontAxis> axes = parseFvarAxes(sfntTable(file, "fvar"));
        QCOMPARE(axes.size(), 1);
        QCOMPARE(axes.first().tag, QStringLiteral("wght"));
        QCOMPARE(axes.first().minimum, 100.0);
        QCOMPARE(axes.first().maximum, 900.0);
        double value = 0;
        QVERIFY(weightAxisValue(axes, 700, &value));
        QCOMPARE(value, 700.0);
        QVERIFY(weightAxisValue(axes, 50, &value));
        QCOMPARE(value, 100.0); // begrenzt auf die Achse
        // Feste Schrift: keine Tabelle fvar
        QFile fixed(QStringLiteral(DSS_TEST_FONT));
        QVERIFY(fixed.open(QIODevice::ReadOnly));
        const QByteArray fixedFile = fixed.readAll();
        QVERIFY(sfntTable(fixedFile, "fvar").isEmpty());
        QVERIFY(!sfntTable(fixedFile, "head").isEmpty());
        QVERIFY(!weightAxisValue(parseFvarAxes(sfntTable(fixedFile, "fvar")), 700, &value));
        // Kaputte Daten
        QVERIFY(parseFvarAxes(QByteArray(10, '\0')).isEmpty());
        QVERIFY(sfntTable(QByteArray("kein Font"), "fvar").isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestSourceGeometry)
#include "tst_sourcegeometry.moc"
