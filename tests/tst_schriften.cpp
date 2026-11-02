// Prüft die Auswahl der Schriften (P08, Teil F) ohne Qt Gui: Gewicht von fontconfig nach
// OpenType, Lesen der Ausgabe von fc-list, Auflösung Schriftname und Schnitt → Datei, die
// mitgelieferte Be Vietnam Pro und das Laden mit fc-list und fc-query auf dem Studio-PC.
// Feste Daten: tests/daten/schriften/fc-list.txt ist die Ausgabe von fc-list auf dem Studio-PC
// (2026-11-02 23:48:09 +0100, Befehl siehe docs/berichte/P08-bericht.md).

#include "fontcatalog.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace dss;

namespace {

const QString kFcList = QStringLiteral(DSS_TEST_FONT_DATA "/fc-list.txt");
const QString kDejaVu = QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
const QString kDejaVuBold = QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QList<FontFace> fixedFaces()
{
    QList<FontFace> faces = parseFontconfigOutput(readFile(kFcList), FontOrigin::System);
    sortFontFaces(&faces);
    return faces;
}

} // namespace

class TestSchriften : public QObject
{
    Q_OBJECT

private slots:
    void weightMapping_data()
    {
        QTest::addColumn<double>("fc");
        QTest::addColumn<int>("ot");
        // Stützstellen aus fontconfig 2.15.0 src/fcweight.c und Werte vom Studio-PC (Lato)
        QTest::newRow("Thin 0") << 0.0 << 100;
        QTest::newRow("ExtraLight 40") << 40.0 << 200;
        QTest::newRow("Lato Hairline 45") << 45.0 << 250;
        QTest::newRow("Lato Thin 47.5") << 47.5 << 275;
        QTest::newRow("Light 50") << 50.0 << 300;
        QTest::newRow("Book 75") << 75.0 << 380;
        QTest::newRow("Regular 80") << 80.0 << 400;
        QTest::newRow("Medium 100") << 100.0 << 500;
        QTest::newRow("DemiBold 180") << 180.0 << 600;
        QTest::newRow("Bold 200") << 200.0 << 700;
        QTest::newRow("ExtraBold 205") << 205.0 << 800;
        QTest::newRow("Black 210") << 210.0 << 900;
        QTest::newRow("ExtraBlack 215 begrenzt") << 215.0 << 900;
        QTest::newRow("zwischen Medium und DemiBold") << 140.0 << 550;
        QTest::newRow("negativ") << -1.0 << -1;
        QTest::newRow("zu gross") << 216.0 << -1;
    }

    void weightMapping()
    {
        QFETCH(double, fc);
        QFETCH(int, ot);
        QCOMPARE(fontconfigWeightToOpenType(fc), ot);
    }

    void parseFixedData()
    {
        const QByteArray data = readFile(kFcList);
        QVERIFY(!data.isEmpty());
        int skipped = -1;
        const QList<FontFace> faces = parseFontconfigOutput(data, FontOrigin::System, &skipped);
        // 191 Zeilen: 113 ttf/otf, 78 Type 1 (t1, pfb) werden übergangen.
        QCOMPARE(data.count('\n'), 191);
        QCOMPARE(faces.size(), 113);
        QCOMPARE(skipped, 78);
        for (const FontFace &f : faces) {
            QVERIFY2(f.file.endsWith(QLatin1String(".ttf")) || f.file.endsWith(QLatin1String(".otf")),
                     qPrintable(f.file));
            QVERIFY(f.weight >= 100 && f.weight <= 900);
            QCOMPARE(f.origin, FontOrigin::System);
        }
        const FontFace *bold = fontFaceForFile(faces, kDejaVuBold);
        QVERIFY(bold);
        QCOMPARE(bold->family, QStringLiteral("DejaVu Sans"));
        QCOMPARE(bold->style, QStringLiteral("Bold"));
        QCOMPARE(bold->weight, 700);
        QCOMPARE(bold->italic, false);
        // Erster Familienname, wenn fontconfig mehrere nennt («Lato,Lato Black»).
        const FontFace *black = fontFaceForFile(faces, QStringLiteral("/usr/share/fonts/truetype/lato/Lato-BlackItalic.ttf"));
        QVERIFY(black);
        QCOMPARE(black->family, QStringLiteral("Lato"));
        QCOMPARE(black->style, QStringLiteral("Black Italic"));
        QCOMPARE(black->weight, 900);
        QCOMPARE(black->italic, true);
        // Schräg (Oblique, slant 110) gilt als kursiv.
        const FontFace *oblique = fontFaceForFile(faces, QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Oblique.ttf"));
        QVERIFY(oblique && oblique->italic);
    }

    void variableFontsAndBrokenLines()
    {
        // Auf dem Studio-PC ist keine variable Schrift installiert; die Zeilen sind nach der
        // Beschreibung von fontconfig gebildet (Grundform mit Gewicht als Bereich, benannte
        // Schnitte mit Index (Nummer + 1) << 16), nicht gemessen.
        const QByteArray data =
            "/x/Inter.ttf\t0\tInter\tRegular\t[0 210]\t0\tTrue\n"
            "/x/Inter.ttf\t65536\tInter\tThin\t0\t0\tFalse\n"
            "/x/Inter.ttf\t589824\tInter\tBold\t200\t0\tFalse\n"
            "/x/Sammlung.otf\t1\tSammlung\tRegular\t80\t0\tFalse\n"
            "/x/Kaputt.ttf\t0\tKaputt\n"
            "/x/Schrift.TTF\t0\tGross\tRegular\t80\t0\tFalse\n";
        int skipped = 0;
        const QList<FontFace> faces = parseFontconfigOutput(data, FontOrigin::System, &skipped);
        QCOMPARE(faces.size(), 3);
        QCOMPARE(skipped, 3);
        QCOMPARE(faces.at(0).file, QStringLiteral("/x/Inter.ttf"));
        QCOMPARE(faces.at(0).weight, 100);
        QCOMPARE(faces.at(1).weight, 700);
        QCOMPARE(faces.at(1).file, faces.at(0).file); // alle Gewichte in einer Datei
        QCOMPARE(faces.at(2).family, QStringLiteral("Gross"));
    }

    void resolveNameAndStyleToFile()
    {
        const QList<FontFace> faces = fixedFaces();
        const FontFace *f = resolveFontFace(faces, QStringLiteral("DejaVu Sans"), 700, false);
        QVERIFY(f);
        QCOMPARE(f->file, kDejaVuBold);
        f = resolveFontFace(faces, QStringLiteral("dejavu sans"), 400, false);
        QVERIFY(f);
        QCOMPARE(f->file, kDejaVu);
        // DejaVu Sans hat keinen kursiven Schnitt: das Gewicht entscheidet allein.
        f = resolveFontFace(faces, QStringLiteral("DejaVu Sans"), 600, true);
        QVERIFY(f);
        QCOMPARE(f->file, kDejaVuBold);
        // Lato: kursiv und gleicher Abstand zu 600 und 700 → das schwerere
        f = resolveFontFace(faces, QStringLiteral("Lato"), 650, true);
        QVERIFY(f);
        QCOMPARE(f->file, QStringLiteral("/usr/share/fonts/truetype/lato/Lato-BoldItalic.ttf"));
        f = resolveFontFace(faces, QStringLiteral("Lato"), 100, false);
        QVERIFY(f);
        QCOMPARE(f->style, QStringLiteral("Hairline"));
        QVERIFY(!resolveFontFace(faces, QStringLiteral("Gibt es nicht"), 400, false));
        // Familien: jede einmal, Lato hat 18 Schnitte
        const QStringList families = fontFamilies(faces);
        QCOMPARE(families.count(QStringLiteral("Lato")), 1);
        QCOMPARE(facesOfFamily(faces, QStringLiteral("Lato")).size(), 18);
        // Installierte Datei geht einer Kopie im Datenordner vor.
        QList<FontFace> withMedia = faces;
        FontFace copy = *fontFaceForFile(faces, kDejaVuBold);
        copy.file = QStringLiteral("media/") + QString(64, QLatin1Char('a')) + QStringLiteral(".ttf");
        copy.origin = FontOrigin::Media;
        withMedia.prepend(copy);
        f = resolveFontFace(withMedia, QStringLiteral("DejaVu Sans"), 700, false);
        QVERIFY(f);
        QCOMPARE(f->file, kDejaVuBold);
    }

    void builtinBeVietnamPro()
    {
        const QStringList files = fontFilesIn(builtinFontDir());
        QCOMPARE(files.size(), 18);
        QVERIFY(QFile::exists(builtinFontDir() + QStringLiteral("/OFL.txt")));
        QVERIFY(files.contains(defaultTextFontFile()));
        QString error;
        QList<FontFace> faces = queryFontFiles(files, FontOrigin::Builtin, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(faces.size(), 18);
        sortFontFaces(&faces);
        QCOMPARE(fontFamilies(faces), QStringList{QStringLiteral("Be Vietnam Pro")});
        for (int w = 100; w <= 900; w += 100) {
            for (bool italic : {false, true}) {
                const FontFace *f = resolveFontFace(faces, QStringLiteral("Be Vietnam Pro"), w, italic);
                QVERIFY(f);
                QCOMPARE(f->weight, w);
                QCOMPARE(f->italic, italic);
            }
        }
        const FontFace *regular = resolveFontFace(faces, QStringLiteral("Be Vietnam Pro"), 400, false);
        QCOMPARE(regular->file, defaultTextFontFile());
        QCOMPARE(regular->style, QStringLiteral("Regular"));
    }

    void loaderOnThisPc()
    {
        // Echte Abfrage auf dem Studio-PC: installierte, mitgelieferte und eine Kopie im Datenordner.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile font(kDejaVu);
        QVERIFY(font.open(QIODevice::ReadOnly));
        const QByteArray data = font.readAll();
        const QString reference = QStringLiteral("media/")
                                  + QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())
                                  + QStringLiteral(".ttf");
        QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("media"))));
        QFile copy(dir.filePath(reference));
        QVERIFY(copy.open(QIODevice::WriteOnly));
        copy.write(data);
        copy.close();

        FontCatalogLoader loader;
        QSignalSpy spy(&loader, &FontCatalogLoader::finished);
        QElapsedTimer t;
        t.start();
        loader.start({copy.fileName()}, {reference});
        QVERIFY(loader.isRunning());
        QVERIFY(spy.wait(10000));
        qInfo("fc-list und fc-query: %lld ms", static_cast<long long>(t.elapsed()));
        const QList<FontFace> faces = spy.at(0).at(0).value<QList<FontFace>>();
        QCOMPARE(spy.at(0).at(1).toString(), QString());
        // Mitgelieferte zuerst
        QCOMPARE(fontFamilies(faces).first(), QStringLiteral("Be Vietnam Pro"));
        QCOMPARE(faces.first().origin, FontOrigin::Builtin);
        const FontFace *system = fontFaceForFile(faces, kDejaVuBold);
        QVERIFY(system);
        QCOMPARE(system->origin, FontOrigin::System);
        const FontFace *media = fontFaceForFile(faces, reference);
        QVERIFY(media);
        QCOMPARE(media->origin, FontOrigin::Media);
        QCOMPARE(media->family, QStringLiteral("DejaVu Sans"));
        QCOMPARE(media->weight, 400);
        // Die Auflösung nimmt die installierte Datei, nicht die Kopie.
        QCOMPARE(resolveFontFace(faces, QStringLiteral("DejaVu Sans"), 400, false)->file, kDejaVu);
    }
};

QTEST_GUILESS_MAIN(TestSchriften)
#include "tst_schriften.moc"
