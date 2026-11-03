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

SourceGeometry geo(int x, int y, int w, int h, int r = 0, bool flipH = false, bool flipV = false)
{
    return {x, y, w, h, r, flipH, flipV};
}

} // namespace

void applyGeometryTo(Source *s, const SourceGeometry &g)
{
    s->x = g.x;
    s->y = g.y;
    s->width = g.width;
    s->height = g.height;
    s->rotation = g.rotation;
    s->flipHorizontal = g.flipHorizontal;
    s->flipVertical = g.flipVertical;
}

namespace QTest {
template <>
char *toString(const dss::SourceGeometry &g)
{
    return qstrdup(qPrintable(QStringLiteral("x=%1 y=%2 w=%3 h=%4 r=%5 fh=%6 fv=%7")
                                  .arg(g.x).arg(g.y).arg(g.width).arg(g.height).arg(g.rotation)
                                  .arg(g.flipHorizontal).arg(g.flipVertical)));
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
        QVERIFY(applyGeometry(&o, s.id, geo(1, 2, 3, 4, 5, true, false)));
        QCOMPARE(geometryOf(o.sources[0]), geo(1, 2, 3, 4, 5, true, false));
        QCOMPARE(o.sources[0].flipHorizontal, true);
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

    void resizeOriginalAspect()
    {
        // P09b, Entscheid 3: Ecken halten das Verhältnis der Datei, nicht des Kastens (wie OBS).
        const Source s = box(100, 100, 200, 100); // Kasten 2:1
        const QSize square(256, 256);
        // Rechts unten nach (400, 150): 300x50, Verhältnis 6 > 1 → Höhe folgt der Breite
        QCOMPARE(resizeGeometry(s, CanvasHandle::BottomRight, QPointF(400, 150), false, square),
                 geo(100, 100, 300, 300));
        // Nach (250, 300): 150x200, Verhältnis 0.75 < 1 → Breite folgt der Höhe
        QCOMPARE(resizeGeometry(s, CanvasHandle::BottomRight, QPointF(250, 300), false, square),
                 geo(100, 100, 200, 200));
        // Links oben: rechts unten (300, 200) bleibt stehen; 16:9 aus einem Video.
        // 160x200, Verhältnis 0.8 < 1.78 → Breite 200 * 16/9 = 355.6 → 356
        QCOMPARE(resizeGeometry(s, CanvasHandle::TopLeft, QPointF(140, 0), false, QSize(1920, 1080)),
                 geo(-56, 0, 356, 200));
        // Mit Shift frei, auch mit Originalgrösse
        QCOMPARE(resizeGeometry(s, CanvasHandle::BottomRight, QPointF(400, 150), true, square),
                 geo(100, 100, 300, 50));
        // Kanten ändern weiter nur eine Seite
        QCOMPARE(resizeGeometry(s, CanvasHandle::Right, QPointF(350, 500), false, square),
                 geo(100, 100, 250, 100));
        // Gedreht und gespiegelt: gleiche Grösse wie ohne Drehung im Rahmen der Quelle
        Source turned = s;
        turned.rotation = 900;
        turned.flipHorizontal = true;
        const QPointF corner = sourceToOverlay(turned, QPointF(300, 50));
        const SourceGeometry g = resizeGeometry(turned, CanvasHandle::BottomRight, corner, false, square);
        QCOMPARE(g.width, 300);
        QCOMPARE(g.height, 300);
        QCOMPARE(g.flipHorizontal, true);
        QCOMPARE(g.rotation, 900);
    }

    void originalSizeOfSource()
    {
        const MediaSizes sizes{{QStringLiteral("media/a.png"), QSize(640, 480)}};
        Source image = box(0, 0, 10, 10);
        image.kind = SourceKind::Image;
        image.file = QStringLiteral("media/a.png");
        QCOMPARE(originalSize(image, sizes), std::optional<QSize>(QSize(640, 480)));
        image.file = QStringLiteral("media/b.png");
        QCOMPARE(originalSize(image, sizes), std::nullopt);
        Source text = box(0, 0, 10, 10);
        text.kind = SourceKind::Text;
        QCOMPARE(originalSize(text, sizes), std::nullopt);
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
        // Genau auf den Gegengriff: Mindestgrösse 1, nicht gespiegelt
        QCOMPARE(resizeGeometry(s, CanvasHandle::Right, QPointF(100, 150), false), geo(100, 100, 1, 100));
        QCOMPARE(resizeGeometry(s, CanvasHandle::Right, QPointF(100.4, 150), false), geo(100, 100, 1, 100));
        QCOMPARE(resizeGeometry(s, CanvasHandle::Top, QPointF(0, 200), false), geo(100, 199, 200, 1));
        const SourceGeometry g = resizeGeometry(s, CanvasHandle::BottomRight, QPointF(100, 100), true);
        QCOMPARE(g, geo(100, 100, 1, 1));
    }

    // P09b, Entscheid 4: über die Gegenseite hinaus spiegeln, wie OBS (StretchItem, ClampAspect).
    void resizeMirror()
    {
        const Source s = box(100, 100, 200, 100); // rechts unten (300, 200)
        // Rechte Kante 100 px links über die linke Kante hinaus: Kasten 100..0 links davon,
        // waagrecht gespiegelt; die linke Kante (x = 100) bleibt.
        QCOMPARE(resizeGeometry(s, CanvasHandle::Right, QPointF(0, 150), false),
                 geo(0, 100, 100, 100, 0, true, false));
        // Linke Kante nach rechts über die rechte hinaus
        QCOMPARE(resizeGeometry(s, CanvasHandle::Left, QPointF(450, 150), false),
                 geo(300, 100, 150, 100, 0, true, false));
        // Obere Kante unter die untere: senkrecht gespiegelt, untere Kante (y = 200) bleibt
        QCOMPARE(resizeGeometry(s, CanvasHandle::Top, QPointF(0, 260), false),
                 geo(100, 200, 200, 60, 0, false, true));
        // Ecke frei (Shift) über beide Seiten: beide gespiegelt
        QCOMPARE(resizeGeometry(s, CanvasHandle::BottomRight, QPointF(-50, 20), true),
                 geo(-50, 20, 150, 80, 0, true, true));
        // Ecke mit Verhältnis über beide Seiten: Verhältnis positiv wie ohne Spiegeln
        // (-150 / -80 = 1.875 < 2 → Breite folgt der Höhe: 160 x 80)
        QCOMPARE(resizeGeometry(s, CanvasHandle::BottomRight, QPointF(-50, 20), false),
                 geo(-60, 20, 160, 80, 0, true, true));
        // Ecke mit Verhältnis nur waagrecht über die Gegenseite: Verhältnis negativ, wie OBS
        // folgt dann die Breite der Höhe (-150 / 50 < 2 → Breite 2 * 50 = 100 nach links)
        QCOMPARE(resizeGeometry(s, CanvasHandle::BottomRight, QPointF(-50, 150), false),
                 geo(0, 100, 100, 50, 0, true, false));
        // Schon gespiegelte Quelle: nochmals über die Gegenseite schaltet zurück
        Source flipped = s;
        flipped.flipHorizontal = true;
        flipped.flipVertical = true;
        QCOMPARE(resizeGeometry(flipped, CanvasHandle::Right, QPointF(0, 150), false),
                 geo(0, 100, 100, 100, 0, false, true));
        // Innerhalb bleibt das Spiegeln, wie es war
        QCOMPARE(resizeGeometry(flipped, CanvasHandle::Right, QPointF(350, 150), false),
                 geo(100, 100, 250, 100, 0, true, true));
        // Gedreht (90°): die Gegenkante bleibt auch beim Spiegeln stehen
        const Source r = box(100, 100, 200, 100, 900);
        const QPointF anchor = handlePosition(r, CanvasHandle::Left, 1.0);
        const SourceGeometry m = resizeGeometry(r, CanvasHandle::Right,
                                                sourceToOverlay(r, QPointF(-80, 50)), false);
        QCOMPARE(m.width, 80);
        QCOMPARE(m.height, 100);
        QCOMPARE(m.flipHorizontal, true);
        Source after = r;
        applyGeometryTo(&after, m);
        // Die Gegenkante (lokal links beim Beginn) ist jetzt die rechte Kante des neuen Kastens.
        const QPointF anchorAfter = handlePosition(after, CanvasHandle::Right, 1.0);
        QVERIFY(std::abs(anchorAfter.x() - anchor.x()) <= 0.5);
        QVERIFY(std::abs(anchorAfter.y() - anchor.y()) <= 0.5);
        // Änderung an den Kern mit allen Werten
        const SourceChanges c = geometryChanges(m);
        QCOMPARE(c.flipHorizontal, std::optional<bool>(true));
        QCOMPARE(c.flipVertical, std::optional<bool>(false));
        QCOMPARE(c.width, std::optional<int>(80));
        QVERIFY(!c.name);
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
        // Shift: auf ganze Grad einrasten (P09b, vorher 15°)
        QCOMPARE(rotateGeometry(s, QPointF(60, -50), true).rotation, 60); // 5.71° → 6°
        QCOMPARE(rotateGeometry(s, QPointF(50 + 100 * std::sin(0.40), 50 - 100 * std::cos(0.40)), true).rotation,
                 230); // 22.92° → 23°
        QCOMPARE(rotateGeometry(s, QPointF(50 - 100 * std::sin(0.1), 50 - 100 * std::cos(0.1)), true).rotation,
                 3540); // -5.73° → -6° = 354°
        QCOMPARE(rotateGeometry(s, QPointF(50 - 100 * std::sin(0.2), 50 - 100 * std::cos(0.2)), true).rotation,
                 3490); // -11.46° → -11° = 349°
        QCOMPARE(rotateGeometry(s, QPointF(200, 50.5), true).rotation, 900); // 90.29° → 90°
        // Ohne Shift kein Einrasten: Zehntelgrad bleiben
        QCOMPARE(rotateGeometry(s, QPointF(50 + 100 * std::sin(0.40), 50 - 100 * std::cos(0.40)), false).rotation,
                 229); // 22.92° → 22.9°
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
