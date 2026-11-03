// P09b, Entscheid 7: EXIF-Ausrichtung lesen und rechnen, ohne Qt Gui (CLAUDE.md Regel 9).
// Bilder: tests/daten/exif/erzeugen.py (Ausrichtung 1 bis 8 als JPEG mit APP1 und PNG mit eXIf).

#include "imageorientation.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace dss;

namespace {

QString exifFile(const QString &name)
{
    return QStringLiteral(DSS_TEST_EXIF_DIR "/") + name;
}

// TIFF-Daten mit einem Eintrag (Tag, Typ, Anzahl, Wert), wie erzeugen.py.
QByteArray tiffWith(bool little, quint16 tag, quint16 type, quint32 count, quint16 value)
{
    QByteArray d;
    const auto u16 = [&d, little](quint16 v) {
        if (little)
            d.append(char(v & 0xFF)).append(char(v >> 8));
        else
            d.append(char(v >> 8)).append(char(v & 0xFF));
    };
    const auto u32 = [&u16, little](quint32 v) {
        u16(quint16(little ? v & 0xFFFF : v >> 16));
        u16(quint16(little ? v >> 16 : v & 0xFFFF));
    };
    d.append(little ? "II" : "MM");
    u16(42);
    u32(8);
    u16(1);
    u16(tag);
    u16(type);
    u32(count);
    u16(value);
    u16(0);
    u32(0);
    return d;
}

} // namespace

class TestImageOrientation : public QObject
{
    Q_OBJECT

private slots:
    void readFiles_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("orientation");
        for (int n = 1; n <= 8; ++n) {
            QTest::addRow("JPEG %d", n) << exifFile(QStringLiteral("orientierung-%1.jpg").arg(n)) << n;
            QTest::addRow("PNG %d", n) << exifFile(QStringLiteral("orientierung-%1.png").arg(n)) << n;
        }
        QTest::newRow("JPEG ohne EXIF") << exifFile(QStringLiteral("ohne-exif.jpg")) << 1;
        // Daten wie im Handtest (prototypes/media): PNG ohne eXIf
        QTest::newRow("bild1.png") << QStringLiteral(DSS_TEST_MEDIA_DIR "/bild1.png") << 1;
        QTest::newRow("Video") << QStringLiteral(DSS_TEST_MEDIA_DIR "/video.webm") << 1;
        QTest::newRow("fehlt") << exifFile(QStringLiteral("fehlt.jpg")) << 1;
    }

    void readFiles()
    {
        QFETCH(QString, file);
        QFETCH(int, orientation);
        QCOMPARE(imageFileOrientation(file), orientation);
        // P10: Lage des Werts in der Datei (der Overlay-Server setzt ihn in den gelieferten Bytes
        // auf 1); dort steht der Wert in der Byte-Reihenfolge der TIFF-Daten.
        const OrientationLocation l = imageOrientationLocation(file);
        QCOMPARE(l.orientation, orientation);
        if (orientation == 1 && l.valueOffset < 0)
            return;
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray all = f.readAll();
        QVERIFY(l.valueOffset > 0 && l.valueOffset + 2 <= all.size());
        const auto b0 = quint8(all.at(l.valueOffset));
        const auto b1 = quint8(all.at(l.valueOffset + 1));
        QCOMPARE(l.littleEndian ? (b0 | b1 << 8) : (b0 << 8 | b1), orientation);
        if (file.endsWith(QLatin1String(".png"))) {
            QCOMPARE(all.mid(l.pngChunk + 4, 4), QByteArray("eXIf"));
            QVERIFY(l.pngChunkLength > 0);
        } else {
            QCOMPARE(l.pngChunk, qint64(-1));
        }
    }

    void pngAfterIdatIgnored()
    {
        // eXIf nach IDAT gilt nicht (PNG Third Edition, Tabelle 7): Chunk nach hinten schieben.
        QFile in(exifFile(QStringLiteral("orientierung-6.png")));
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray data = in.readAll();
        const qsizetype exifAt = data.indexOf("eXIf") - 4;
        const qsizetype idatAt = data.indexOf("IDAT") - 4;
        const qsizetype iendAt = data.indexOf("IEND") - 4;
        QVERIFY(exifAt > 0 && idatAt > exifAt && iendAt > idatAt);
        const QByteArray exif = data.mid(exifAt, idatAt - exifAt);
        const QByteArray moved = data.left(exifAt) + data.mid(idatAt, iendAt - idatAt) + exif
                                 + data.mid(iendAt);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("spaet.png"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(moved);
        out.close();
        QCOMPARE(imageFileOrientation(path), 1);
    }

    void brokenFiles_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::newRow("leer") << QByteArray();
        QTest::newRow("nur SOI") << QByteArray("\xFF\xD8", 2);
        QTest::newRow("APP1 zu kurz") << QByteArray("\xFF\xD8\xFF\xE1\x00\x40" "Exif\0\0MM", 14);
        QTest::newRow("Länge 0") << QByteArray("\xFF\xD8\xFF\xE1\x00\x00", 6);
        QTest::newRow("PNG abgeschnitten") << QByteArray("\x89PNG\r\n\x1A\n\x00\x00", 10);
        QTest::newRow("PNG eXIf zu gross")
            << QByteArray("\x89PNG\r\n\x1A\n\x7F\xFF\xFF\xFF" "eXIf", 16);
    }

    void brokenFiles()
    {
        QFETCH(QByteArray, data);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("kaputt"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(data);
        out.close();
        QCOMPARE(imageFileOrientation(path), 1);
    }

    void tiff_data()
    {
        QTest::addColumn<QByteArray>("tiff");
        QTest::addColumn<int>("orientation");
        QTest::newRow("II 6") << tiffWith(true, 0x0112, 3, 1, 6) << 6;
        QTest::newRow("MM 8") << tiffWith(false, 0x0112, 3, 1, 8) << 8;
        QTest::newRow("Wert 0") << tiffWith(false, 0x0112, 3, 1, 0) << 1;
        QTest::newRow("Wert 9") << tiffWith(false, 0x0112, 3, 1, 9) << 1;
        QTest::newRow("Typ LONG") << tiffWith(false, 0x0112, 4, 1, 6) << 1;
        QTest::newRow("Anzahl 2") << tiffWith(false, 0x0112, 3, 2, 6) << 1;
        QTest::newRow("anderes Tag") << tiffWith(false, 0x0110, 3, 1, 6) << 1;
        QTest::newRow("abgeschnitten") << tiffWith(false, 0x0112, 3, 1, 6).left(15) << 1;
        QTest::newRow("kein TIFF") << QByteArray("XX\x00\x2A\x00\x00\x00\x08", 8) << 1;
        QByteArray badMagic = tiffWith(false, 0x0112, 3, 1, 6);
        badMagic[3] = 43;
        QTest::newRow("nicht 42") << badMagic << 1;
        QByteArray farIfd = tiffWith(false, 0x0112, 3, 1, 6);
        farIfd[7] = char(200);
        QTest::newRow("IFD ausserhalb") << farIfd << 1;
    }

    void tiff()
    {
        QFETCH(QByteArray, tiff);
        QFETCH(int, orientation);
        QCOMPARE(tiffOrientation(tiff), orientation);
    }

    void size()
    {
        for (int n = 1; n <= 4; ++n)
            QCOMPARE(orientedSize(QSize(64, 32), n), QSize(64, 32));
        for (int n = 5; n <= 8; ++n)
            QCOMPARE(orientedSize(QSize(32, 64), n), QSize(64, 32));
        QCOMPARE(orientedSize(QSize(64, 32), 0), QSize(64, 32));
    }

    void points_data()
    {
        // Gespeicherter Punkt links oben (0, 0) und rechts oben (w, 0) eines Bildes w x h = 4 x 2:
        // wohin sie nach der Ausrichtung kommen (EXIF 2.32, Abschnitt 4.6.4, Orientation: wo
        // die 0. Zeile und die 0. Spalte im angezeigten Bild liegen).
        QTest::addColumn<int>("orientation");
        QTest::addColumn<QPointF>("topLeft");
        QTest::addColumn<QPointF>("topRight");
        QTest::newRow("1") << 1 << QPointF(0, 0) << QPointF(4, 0);
        QTest::newRow("2") << 2 << QPointF(4, 0) << QPointF(0, 0);
        QTest::newRow("3") << 3 << QPointF(4, 2) << QPointF(0, 2);
        QTest::newRow("4") << 4 << QPointF(0, 2) << QPointF(4, 2);
        QTest::newRow("5") << 5 << QPointF(0, 0) << QPointF(0, 4); // 0. Zeile links
        QTest::newRow("6") << 6 << QPointF(2, 0) << QPointF(2, 4); // 0. Zeile rechts
        QTest::newRow("7") << 7 << QPointF(2, 4) << QPointF(2, 0);
        QTest::newRow("8") << 8 << QPointF(0, 4) << QPointF(0, 0);
    }

    void points()
    {
        QFETCH(int, orientation);
        QFETCH(QPointF, topLeft);
        QFETCH(QPointF, topRight);
        const QSize stored(4, 2);
        QCOMPARE(orientedPoint(QPointF(0, 0), stored, orientation), topLeft);
        QCOMPARE(orientedPoint(QPointF(4, 0), stored, orientation), topRight);
    }

    void stepsMatchPoints()
    {
        // Die Schritte für die Oberfläche (waagrecht spiegeln, dann im Uhrzeigersinn drehen)
        // ergeben dieselbe Abbildung wie orientedPoint, für jeden Punkt.
        const QSize stored(5, 3);
        for (int n = 1; n <= 8; ++n) {
            const OrientationSteps steps = orientationSteps(n);
            for (const QPointF p : {QPointF(0, 0), QPointF(5, 0), QPointF(1, 2), QPointF(4.5, 0.5)}) {
                double w = stored.width();
                double h = stored.height();
                QPointF q = steps.mirror ? QPointF(w - p.x(), p.y()) : p;
                for (int turn = 0; turn < steps.rotation / 90; ++turn) {
                    q = QPointF(h - q.y(), q.x()); // 90° im Uhrzeigersinn in w x h
                    std::swap(w, h);
                }
                QCOMPARE(q, orientedPoint(p, stored, n));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestImageOrientation)
#include "tst_imageorientation.moc"
