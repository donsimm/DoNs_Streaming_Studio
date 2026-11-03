// P09b, Entscheid 5 (E-25): Befehle «Transformieren» wie OBS, ohne Qt Gui (CLAUDE.md Regel 9).
// Erwartete Werte von Hand nach OBS (Quellenangaben in src/common/transform.h).

#include "transform.h"

#include <QTest>

using namespace dss;

namespace {

Source box(int x, int y, int w, int h, int rotation = 0, bool flipH = false, bool flipV = false,
           SourceKind kind = SourceKind::Image)
{
    Source s;
    s.id = QUuid::createUuid();
    s.kind = kind;
    s.name = QStringLiteral("q");
    s.file = kind == SourceKind::Text ? QString() : QStringLiteral("media/") + QString(64, QLatin1Char('a')) + QStringLiteral(".png");
    s.x = x;
    s.y = y;
    s.width = w;
    s.height = h;
    s.rotation = rotation;
    s.flipHorizontal = flipH;
    s.flipVertical = flipV;
    return s;
}

SourceGeometry geo(int x, int y, int w, int h, int rotation = 0, bool flipH = false, bool flipV = false)
{
    return {x, y, w, h, rotation, flipH, flipV};
}

Source applied(Source s, const SourceGeometry &g)
{
    s.x = g.x;
    s.y = g.y;
    s.width = g.width;
    s.height = g.height;
    s.rotation = g.rotation;
    s.flipHorizontal = g.flipHorizontal;
    s.flipVertical = g.flipVertical;
    return s;
}

const QSize kCanvas(1920, 1080);
const std::optional<QSize> kNoSize;

} // namespace

Q_DECLARE_METATYPE(Source)
Q_DECLARE_METATYPE(SourceGeometry)

namespace QTest {
template <>
char *toString(const SourceGeometry &g)
{
    return qstrdup(QStringLiteral("(%1, %2, %3x%4, %5, h%6 v%7)")
                       .arg(g.x).arg(g.y).arg(g.width).arg(g.height).arg(g.rotation)
                       .arg(g.flipHorizontal).arg(g.flipVertical).toUtf8().constData());
}
} // namespace QTest

class TestTransform : public QObject
{
    Q_OBJECT

private slots:
    void boundingBoxRotated()
    {
        // 200x100 bei (100, 100), Mitte (200, 150)
        const BoundingBox flat = boundingBox(geo(100, 100, 200, 100));
        QCOMPARE(flat.left, 100.0);
        QCOMPARE(flat.top, 100.0);
        QCOMPARE(flat.width, 200.0);
        QCOMPARE(flat.height, 100.0);
        const BoundingBox quarter = boundingBox(geo(100, 100, 200, 100, 900));
        QCOMPARE(quarter.width, 100.0);
        QCOMPARE(quarter.height, 200.0);
        QCOMPARE(quarter.left, 150.0);
        QCOMPARE(quarter.top, 50.0);
        // 45°: (200 + 100) * cos 45°
        const BoundingBox diagonal = boundingBox(geo(100, 100, 200, 100, 450));
        QVERIFY(qAbs(diagonal.width - 300.0 / std::sqrt(2.0)) < 1e-9);
        QVERIFY(qAbs(diagonal.height - diagonal.width) < 1e-9);
        // Spiegeln ändert den Kasten nicht
        const BoundingBox flipped = boundingBox(geo(100, 100, 200, 100, 900, true, true));
        QCOMPARE(flipped.left, quarter.left);
        QCOMPARE(flipped.width, quarter.width);
    }

    void rotateKeepsBoxTopLeft_data()
    {
        QTest::addColumn<Source>("source");
        QTest::addColumn<int>("command");
        QTest::addColumn<SourceGeometry>("expected");
        using C = TransformCommand;
        // 200x100 bei (100, 100): Kasten links oben (100, 100). Nach 90°: umschliessend 100x200,
        // links oben bleibt (100, 100) → Mitte (150, 200) → x = 50, y = 150.
        QTest::newRow("90 im Uhrzeigersinn") << box(100, 100, 200, 100) << int(C::Rotate90Clockwise)
                                             << geo(50, 150, 200, 100, 900);
        QTest::newRow("90 gegen") << box(100, 100, 200, 100) << int(C::Rotate90CounterClockwise)
                                  << geo(50, 150, 200, 100, 2700);
        // 180°: Kasten gleich, Lage gleich
        QTest::newRow("180") << box(100, 100, 200, 100) << int(C::Rotate180)
                             << geo(100, 100, 200, 100, 1800);
        // Über 360° hinaus: 300° + 90° = 30°; gespiegelt bleibt gespiegelt
        QTest::newRow("über 360") << box(100, 100, 200, 100, 3000, true, false)
                                  << int(C::Rotate90Clockwise) << geo(100, 100, 200, 100, 300, true, false);
        // 0° gegen den Uhrzeigersinn: 270°
        QTest::newRow("unter 0") << box(0, 0, 100, 100, 0) << int(C::Rotate90CounterClockwise)
                                 << geo(0, 0, 100, 100, 2700);
    }

    void rotateKeepsBoxTopLeft()
    {
        QFETCH(Source, source);
        QFETCH(int, command);
        QFETCH(SourceGeometry, expected);
        const SourceGeometry g = transformGeometry(source, TransformCommand(command), kCanvas, kNoSize);
        if (source.rotation % 900 != 0) {
            // schräg: Lage nach der Rechnung, linke obere Ecke des Kastens bleibt (±0.5 Pixel)
            QCOMPARE(g.rotation, expected.rotation);
            QCOMPARE(g.flipHorizontal, expected.flipHorizontal);
            const BoundingBox before = boundingBox(geometryOf(source));
            const BoundingBox after = boundingBox(g);
            QVERIFY(qAbs(before.left - after.left) <= 0.5);
            QVERIFY(qAbs(before.top - after.top) <= 0.5);
        } else {
            QCOMPARE(g, expected);
        }
    }

    void rotateFourTimes()
    {
        // Gerader Unterschied von Breite und Höhe: viermal 90° gibt wieder den Anfang.
        Source s = box(300, 200, 640, 360, 0, false, true);
        const SourceGeometry start = geometryOf(s);
        for (int i = 0; i < 4; ++i)
            s = applied(s, transformGeometry(s, TransformCommand::Rotate90Clockwise, kCanvas, kNoSize));
        QCOMPARE(geometryOf(s), start);
    }

    void flip()
    {
        // Gedreht und schon gespiegelt: nur das Spiegeln wechselt, die Lage bleibt (der
        // umschliessende Kasten ändert sich nicht).
        const Source s = box(100, 100, 200, 100, 450, true, false);
        QCOMPARE(transformGeometry(s, TransformCommand::FlipHorizontal, kCanvas, kNoSize),
                 geo(100, 100, 200, 100, 450, false, false));
        QCOMPARE(transformGeometry(s, TransformCommand::FlipVertical, kCanvas, kNoSize),
                 geo(100, 100, 200, 100, 450, true, true));
    }

    void reset()
    {
        // Lage 0/0, Originalgrösse, Drehung 0, nicht gespiegelt
        const Source s = box(100, 100, 200, 100, 450, true, true);
        QCOMPARE(transformGeometry(s, TransformCommand::Reset, kCanvas, QSize(256, 256)),
                 geo(0, 0, 256, 256));
        // Text (keine Originalgrösse): Grösse bleibt
        const Source text = box(100, 100, 300, 80, 900, true, false, SourceKind::Text);
        QCOMPARE(transformGeometry(text, TransformCommand::Reset, kCanvas, kNoSize), geo(0, 0, 300, 80));
    }

    void fit_data()
    {
        QTest::addColumn<Source>("source");
        QTest::addColumn<QSize>("canvas");
        QTest::addColumn<QSize>("original");
        QTest::addColumn<SourceGeometry>("expected");
        // 4:3 in 16:9: Höhe voll, Breite 1440, mittig (1920 - 1440) / 2 = 240
        QTest::newRow("4:3") << box(5, 5, 10, 10, 450, true, true) << kCanvas << QSize(640, 480)
                             << geo(240, 0, 1440, 1080);
        // breiter als die Leinwand: Breite voll, Höhe 1920 / 4 = 480, y = 300
        QTest::newRow("4:1") << box(5, 5, 10, 10) << kCanvas << QSize(1024, 256)
                             << geo(0, 300, 1920, 480);
        // Grösse der Datei zählt, nicht des Kastens (wie OBS: Massstab erst auf 1)
        QTest::newRow("Kasten verzerrt") << box(0, 0, 1000, 100) << kCanvas << QSize(1920, 1080)
                                         << geo(0, 0, 1920, 1080);
        // ungerader Rest: ganzzahlig halbiert (add_alignment in OBS)
        QTest::newRow("ungerade") << box(0, 0, 10, 10) << QSize(1921, 1080) << QSize(100, 100)
                                  << geo(420, 0, 1080, 1080);
        // Hochformat (EXIF-Ausrichtung schon in der Originalgrösse)
        QTest::newRow("hoch") << box(0, 0, 10, 10) << kCanvas << QSize(1080, 1920)
                              << geo(656, 0, 608, 1080);
    }

    void fit()
    {
        QFETCH(Source, source);
        QFETCH(QSize, canvas);
        QFETCH(QSize, original);
        QFETCH(SourceGeometry, expected);
        QCOMPARE(transformGeometry(source, TransformCommand::FitToCanvas, canvas, original), expected);
    }

    void fitText()
    {
        // Ohne Originalgrösse: Verhältnis des Kastens (300x100 = 3:1) → 1920x640, y = 220
        const Source text = box(10, 10, 300, 100, 900, false, true, SourceKind::Text);
        QCOMPARE(transformGeometry(text, TransformCommand::FitToCanvas, kCanvas, kNoSize),
                 geo(0, 220, 1920, 640));
    }

    void stretch()
    {
        const Source s = box(100, 100, 200, 100, 1234, true, true);
        QCOMPARE(transformGeometry(s, TransformCommand::StretchToCanvas, kCanvas, QSize(10, 10)),
                 geo(0, 0, 1920, 1080));
        QCOMPARE(transformGeometry(s, TransformCommand::StretchToCanvas, QSize(1280, 720), kNoSize),
                 geo(0, 0, 1280, 720));
    }

    void center_data()
    {
        QTest::addColumn<Source>("source");
        QTest::addColumn<int>("command");
        QTest::addColumn<SourceGeometry>("expected");
        using C = TransformCommand;
        // 200x100: Mitte auf (960, 540) → (860, 490); Drehung und Spiegeln bleiben
        QTest::newRow("Mitte") << box(10, 20, 200, 100) << int(C::CenterOnCanvas) << geo(860, 490, 200, 100);
        QTest::newRow("gedreht gespiegelt") << box(10, 20, 200, 100, 900, true, false)
                                            << int(C::CenterOnCanvas) << geo(860, 490, 200, 100, 900, true, false);
        QTest::newRow("schräg") << box(-500, 3000, 200, 100, 333) << int(C::CenterOnCanvas)
                                << geo(860, 490, 200, 100, 333);
        // senkrecht: nur y; waagrecht: nur x
        QTest::newRow("senkrecht") << box(10, 20, 200, 100, 450) << int(C::CenterVertically)
                                   << geo(10, 490, 200, 100, 450);
        QTest::newRow("waagrecht") << box(10, 20, 200, 100, 450) << int(C::CenterHorizontally)
                                   << geo(860, 20, 200, 100, 450);
        // grösser als die Leinwand und ungerade: ganzzahlig halbiert
        QTest::newRow("gross") << box(0, 0, 2001, 1081) << int(C::CenterOnCanvas) << geo(-40, 0, 2001, 1081);
    }

    void center()
    {
        QFETCH(Source, source);
        QFETCH(int, command);
        QFETCH(SourceGeometry, expected);
        QCOMPARE(transformGeometry(source, TransformCommand(command), kCanvas, kNoSize), expected);
    }

    void copiedTransformIsComplete()
    {
        // Kopieren nimmt alle Werte der Transformation; Einfügen schickt sie alle (auch in ein
        // anderes Overlay, die Werte sind Pixel und bleiben gleich).
        const Source s = box(12, 34, 56, 78, 901, true, false);
        const SourceGeometry copied = geometryOf(s);
        const SourceChanges c = geometryChanges(copied);
        QCOMPARE(c.x, std::optional<int>(12));
        QCOMPARE(c.y, std::optional<int>(34));
        QCOMPARE(c.width, std::optional<int>(56));
        QCOMPARE(c.height, std::optional<int>(78));
        QCOMPARE(c.rotation, std::optional<int>(901));
        QCOMPARE(c.flipHorizontal, std::optional<bool>(true));
        QCOMPARE(c.flipVertical, std::optional<bool>(false));
        Source other = box(0, 0, 1, 1, 0, false, true);
        const QString error = applySourceChanges(&other, c);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(geometryOf(other), copied);
    }
};

QTEST_GUILESS_MAIN(TestTransform)
#include "tst_transform.moc"
