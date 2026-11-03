// Bearbeiten auf der Leinwand (P09, Teil C) ohne Qt Gui: Griffe, Klick, Verschieben, Skalieren
// mit und ohne Seitenverhältnis, Kanten, gedrehtes Skalieren, Drehen mit und ohne Einrasten,
// ganze Pixel, Mindestgrösse, Pfeiltasten, Zeigerformen.

#include "canvasedit.h"
#include "sourcegeometry.h"

#include <QTest>

using namespace dss;

namespace {

Source box(int x, int y, int w, int h, int rotation = 0)
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
    return s;
}

SourceGeometry geo(int x, int y, int w, int h, int r = 0)
{
    return {x, y, w, h, r};
}

} // namespace

namespace QTest {
template <>
char *toString(const dss::SourceGeometry &g)
{
    return qstrdup(qPrintable(QStringLiteral("x=%1 y=%2 w=%3 h=%4 r=%5")
                                  .arg(g.x).arg(g.y).arg(g.width).arg(g.height).arg(g.rotation)));
}
} // namespace QTest

class TestCanvasEdit : public QObject
{
    Q_OBJECT

private slots:
    void handles()
    {
        const Source s = box(100, 100, 200, 100);
        QCOMPARE(handlePosition(s, CanvasHandle::TopLeft, 1.0), QPointF(100, 100));
        QCOMPARE(handlePosition(s, CanvasHandle::Right, 1.0), QPointF(300, 150));
        QCOMPARE(handlePosition(s, CanvasHandle::Bottom, 1.0), QPointF(200, 200));
        // Drehgriff 30 Bildschirm-Pixel über der Mitte der oberen Kante: bei Massstab 0.5 sind
        // das 60 Overlay-Pixel.
        QCOMPARE(handlePosition(s, CanvasHandle::Rotate, 0.5), QPointF(200, 40));
        // Gedreht um 90°: der Drehgriff liegt rechts
        const Source r = box(100, 100, 200, 100, 900);
        const QPointF rot = handlePosition(r, CanvasHandle::Rotate, 1.0);
        QVERIFY(std::abs(rot.x() - (200 + 50 + 30)) < 1e-9);
        QVERIFY(std::abs(rot.y() - 150) < 1e-9);
        QVERIFY(isCornerHandle(CanvasHandle::TopRight));
        QVERIFY(isEdgeHandle(CanvasHandle::Left));
        QVERIFY(!isCornerHandle(CanvasHandle::Move));
    }

    void handleAtPriority()
    {
        const Source s = box(100, 100, 200, 100);
        const double scale = 0.5; // Fangradius 6 Bildschirm-Pixel = 12 Overlay-Pixel
        QCOMPARE(handleAt(s, QPointF(105, 95), scale), CanvasHandle::TopLeft);
        QCOMPARE(handleAt(s, QPointF(200, 110), scale), CanvasHandle::Top);
        QCOMPARE(handleAt(s, QPointF(200, 150), scale), CanvasHandle::Move);
        QCOMPARE(handleAt(s, QPointF(200, 45), scale), CanvasHandle::Rotate);
        QCOMPARE(handleAt(s, QPointF(200, 70), scale), CanvasHandle::None);
        QCOMPARE(handleAt(s, QPointF(500, 500), scale), CanvasHandle::None);
        // Kleine Quelle: Ecken vor Kanten
        const Source tiny = box(0, 0, 4, 4);
        QCOMPARE(handleAt(tiny, QPointF(0, 0), 1.0), CanvasHandle::TopLeft);
        // Gedreht: Ecke dort, wo sie nach dem Drehen liegt
        const Source r = box(100, 100, 200, 100, 900);
        QCOMPARE(handleAt(r, QPointF(250, 50), 1.0), CanvasHandle::TopLeft);
    }

    void press()
    {
        Overlay o;
        o.sources = {box(0, 0, 100, 100), box(50, 50, 100, 100)};
        const QUuid back = o.sources[0].id;
        const QUuid front = o.sources[1].id;
        // Ohne Auswahl: oberste Quelle, verschieben
        CanvasPress p = canvasPress(o, QUuid(), QPointF(75, 75), 1.0);
        QCOMPARE(p.sourceId, front);
        QCOMPARE(p.handle, CanvasHandle::Move);
        // Gewählte hintere Quelle: in ihr bleibt sie gewählt, auch unter der vorderen
        p = canvasPress(o, back, QPointF(75, 75), 1.0);
        QCOMPARE(p.sourceId, back);
        QCOMPARE(p.handle, CanvasHandle::Move);
        // Griff der gewählten Quelle vor der oberen Quelle
        p = canvasPress(o, back, QPointF(100, 100), 1.0);
        QCOMPARE(p.sourceId, back);
        QCOMPARE(p.handle, CanvasHandle::BottomRight);
        // Daneben: Auswahl aufheben
        p = canvasPress(o, front, QPointF(500, 500), 1.0);
        QVERIFY(p.sourceId.isNull());
        QCOMPARE(p.handle, CanvasHandle::None);
        // Verborgene gewählte Quelle hat keine Griffe; darunter wird die sichtbare gewählt
        o.sources[1].visible = false;
        p = canvasPress(o, front, QPointF(75, 75), 1.0);
        QCOMPARE(p.sourceId, back);
    }

    void move()
    {
        const Source s = box(10, 20, 100, 50);
        QCOMPARE(moveGeometry(s, QPointF(30, 30), QPointF(45.4, 19.6)), geo(25, 10, 100, 50));
        QCOMPARE(moveGeometry(s, QPointF(30, 30), QPointF(30.4, 30.4)), geo(10, 20, 100, 50));
        // Übernehmen in das Overlay
        Overlay o;
        o.sources = {s};
        QVERIFY(applyGeometry(&o, s.id, geo(1, 2, 3, 4, 5)));
        QCOMPARE(geometryOf(o.sources[0]), geo(1, 2, 3, 4, 5));
        QVERIFY(!applyGeometry(&o, QUuid::createUuid(), geo(1, 2, 3, 4, 5)));
    }

    void resizeCorner()
    {
        const Source s = box(100, 100, 200, 100); // Verhältnis 2:1
        // Rechts unten nach (400, 150): frei 300x50; mit Verhältnis: 300/50 = 6 > 2 → Höhe folgt
        QCOMPARE(resizeGeometry(s, CanvasHandle::BottomRight, QPointF(400, 150), true),
                 geo(100, 100, 300, 50));
        QCOMPARE(resizeGeometry(s, CanvasHandle::BottomRight, QPointF(400, 150), false),
                 geo(100, 100, 300, 150));
        // Verhältnis kleiner: Breite folgt der Höhe
        QCOMPARE(resizeGeometry(s, CanvasHandle::BottomRight, QPointF(250, 300), false),
                 geo(100, 100, 400, 200));
        // Links oben: rechts unten (300, 200) bleibt stehen
        QCOMPARE(resizeGeometry(s, CanvasHandle::TopLeft, QPointF(200, 150), false),
                 geo(200, 150, 100, 50));
        QCOMPARE(resizeGeometry(s, CanvasHandle::TopLeft, QPointF(0, 120), true),
                 geo(0, 120, 300, 80));
        // Rechts oben: links unten (100, 200) bleibt
        QCOMPARE(resizeGeometry(s, CanvasHandle::TopRight, QPointF(500, 0), true),
                 geo(100, 0, 400, 200));
        // Ganze Pixel
        const SourceGeometry g = resizeGeometry(s, CanvasHandle::BottomRight, QPointF(333.3, 177.7), true);
        QCOMPARE(g, geo(100, 100, 233, 78));
    }

    void resizeEdge()
    {
        const Source s = box(100, 100, 200, 100);
        // Kante: nur Breite bzw. Höhe, auch ohne Shift (Entscheid 7)
        QCOMPARE(resizeGeometry(s, CanvasHandle::Right, QPointF(350, 500), false), geo(100, 100, 250, 100));
        QCOMPARE(resizeGeometry(s, CanvasHandle::Left, QPointF(50, -40), false), geo(50, 100, 250, 100));
        QCOMPARE(resizeGeometry(s, CanvasHandle::Bottom, QPointF(0, 260), false), geo(100, 100, 200, 160));
        QCOMPARE(resizeGeometry(s, CanvasHandle::Top, QPointF(0, 180), true), geo(100, 180, 200, 20));
    }

    void resizeMinimum()
    {
        const Source s = box(100, 100, 200, 100);
        // Über den Gegengriff hinaus: Mindestgrösse 1, kein Spiegeln
        QCOMPARE(resizeGeometry(s, CanvasHandle::Right, QPointF(0, 150), false), geo(100, 100, 1, 100));
        QCOMPARE(resizeGeometry(s, CanvasHandle::Top, QPointF(0, 500), false), geo(100, 199, 200, 1));
        const SourceGeometry g = resizeGeometry(s, CanvasHandle::BottomRight, QPointF(-50, -50), true);
        QCOMPARE(g.width, 1);
        QCOMPARE(g.height, 1);
    }

    void resizeRotated()
    {
        // 90° gedreht: die lokale rechte Kante zeigt im Overlay nach unten.
        const Source s = box(100, 100, 200, 100, 900);
        const QPointF anchor = handlePosition(s, CanvasHandle::Left, 1.0); // bleibt stehen
        const SourceGeometry g = resizeGeometry(s, CanvasHandle::Right, QPointF(250, 300), false);
        QCOMPARE(g.width, 250); // lokal 50 länger
        QCOMPARE(g.height, 100);
        QCOMPARE(g.rotation, 900);
        Source after = s;
        after.x = g.x;
        after.y = g.y;
        after.width = g.width;
        after.height = g.height;
        const QPointF anchorAfter = handlePosition(after, CanvasHandle::Left, 1.0);
        QVERIFY2(std::abs(anchorAfter.x() - anchor.x()) <= 0.5 && std::abs(anchorAfter.y() - anchor.y()) <= 0.5,
                 qPrintable(QStringLiteral("%1,%2 statt %3,%4").arg(anchorAfter.x()).arg(anchorAfter.y())
                                .arg(anchor.x()).arg(anchor.y())));
        // Ecke gedreht, mit Verhältnis: Gegenecke bleibt
        const Source t = box(0, 0, 200, 100, 300);
        const QPointF opposite = handlePosition(t, CanvasHandle::TopLeft, 1.0);
        const SourceGeometry h = resizeGeometry(t, CanvasHandle::BottomRight,
                                                sourceToOverlay(t, QPointF(400, 200)), false);
        QCOMPARE(h.width, 400);
        QCOMPARE(h.height, 200);
        Source t2 = t;
        t2.x = h.x;
        t2.y = h.y;
        t2.width = h.width;
        t2.height = h.height;
        const QPointF oppositeAfter = handlePosition(t2, CanvasHandle::TopLeft, 1.0);
        QVERIFY(std::abs(oppositeAfter.x() - opposite.x()) <= 0.5);
        QVERIFY(std::abs(oppositeAfter.y() - opposite.y()) <= 0.5);
    }

    void rotate()
    {
        const Source s = box(0, 0, 100, 100); // Mitte (50, 50)
        QCOMPARE(rotateGeometry(s, QPointF(50, -100), false).rotation, 0);    // oben
        QCOMPARE(rotateGeometry(s, QPointF(200, 50), false).rotation, 900);   // rechts
        QCOMPARE(rotateGeometry(s, QPointF(50, 200), false).rotation, 1800);  // unten
        QCOMPARE(rotateGeometry(s, QPointF(-100, 50), false).rotation, 2700); // links
        // Zehntelgrad: atan(10/100) = 5.71° → 57
        QCOMPARE(rotateGeometry(s, QPointF(60, -50), false).rotation, 57);
        // Shift: auf 15° einrasten
        QCOMPARE(rotateGeometry(s, QPointF(60, -50), true).rotation, 0);
        QCOMPARE(rotateGeometry(s, QPointF(50 + 100 * std::sin(0.40), 50 - 100 * std::cos(0.40)), true).rotation,
                 300); // 22.9° → 30°
        QCOMPARE(rotateGeometry(s, QPointF(50 - 100 * std::sin(0.1), 50 - 100 * std::cos(0.1)), true).rotation,
                 0); // -5.7° → 0
        QCOMPARE(rotateGeometry(s, QPointF(50 - 100 * std::sin(0.2), 50 - 100 * std::cos(0.2)), true).rotation,
                 3450); // -11.5° → -15° = 345°
        // Lage und Grösse bleiben, Zeiger in der Mitte ändert nichts
        QCOMPARE(rotateGeometry(s, QPointF(200, 50), false), geo(0, 0, 100, 100, 900));
        QCOMPARE(rotateGeometry(box(0, 0, 100, 100, 450), QPointF(50, 50), false).rotation, 450);
    }

    void nudge()
    {
        const Source s = box(10, 20, 100, 50, 125);
        QCOMPARE(nudgeGeometry(s, -kNudgeStep, 0), geo(9, 20, 100, 50, 125));
        QCOMPARE(nudgeGeometry(s, 0, kNudgeStepLarge), geo(10, 30, 100, 50, 125));
        QCOMPARE(kNudgeStep, 1);
        QCOMPARE(kNudgeStepLarge, 10);
    }

    void cursors()
    {
        QCOMPARE(cursorFor(CanvasHandle::None, 0, false), CanvasCursor::Arrow);
        QCOMPARE(cursorFor(CanvasHandle::Move, 0, false), CanvasCursor::Move);
        QCOMPARE(cursorFor(CanvasHandle::Rotate, 0, false), CanvasCursor::OpenHand);
        QCOMPARE(cursorFor(CanvasHandle::Rotate, 0, true), CanvasCursor::ClosedHand);
        QCOMPARE(cursorFor(CanvasHandle::Left, 0, false), CanvasCursor::SizeHorizontal);
        QCOMPARE(cursorFor(CanvasHandle::Top, 0, false), CanvasCursor::SizeVertical);
        QCOMPARE(cursorFor(CanvasHandle::TopLeft, 0, false), CanvasCursor::SizeFDiagonal);
        QCOMPARE(cursorFor(CanvasHandle::BottomRight, 0, false), CanvasCursor::SizeFDiagonal);
        QCOMPARE(cursorFor(CanvasHandle::TopRight, 0, false), CanvasCursor::SizeBDiagonal);
        // Gedreht: 90° macht aus waagrecht senkrecht, 45° aus waagrecht diagonal
        QCOMPARE(cursorFor(CanvasHandle::Left, 900, false), CanvasCursor::SizeVertical);
        QCOMPARE(cursorFor(CanvasHandle::Right, 450, false), CanvasCursor::SizeFDiagonal);
        QCOMPARE(cursorFor(CanvasHandle::TopLeft, 450, false), CanvasCursor::SizeVertical);
        QCOMPARE(cursorFor(CanvasHandle::TopLeft, 3500, false), CanvasCursor::SizeFDiagonal);
    }
};

QTEST_APPLESS_MAIN(TestCanvasEdit)
#include "tst_canvasedit.moc"
