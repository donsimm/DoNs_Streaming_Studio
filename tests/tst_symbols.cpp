// Prüft, dass Symbole einfarbig gezeichnet werden, egal welche Farben die Datei hat (Auftrag
// P06, Punkt 28). Läuft in der X11-Sitzung (CLAUDE.md Regel 6); ohne X11 meldet der Test
// «übersprungen» (Exit-Code 77). Die mehrfarbige Test-SVG entsteht nur hier, in einem
// temporären Ordner, nicht unter src/app/symbole/.

#include "symbols.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPalette>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

namespace {

// Vier Farben, halbdurchsichtige Fläche, Kreis mit weichen Kanten (Kantenglättung).
const char kColorfulSvg[] =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\" viewBox=\"0 0 64 64\">\n"
    "  <rect x=\"0\" y=\"0\" width=\"32\" height=\"32\" fill=\"#ff0000\"/>\n"
    "  <rect x=\"32\" y=\"0\" width=\"32\" height=\"32\" fill=\"#00c000\"/>\n"
    "  <rect x=\"0\" y=\"32\" width=\"32\" height=\"32\" fill=\"#0000ff\" fill-opacity=\"0.5\"/>\n"
    "  <circle cx=\"48\" cy=\"48\" r=\"13\" fill=\"#e5af24\"/>\n"
    "</svg>\n";

// Alle Pixel mit Deckkraft >= 128 haben die Farbe color (±1 wegen Rundung beim Umrechnen aus
// vormultiplizierten Werten), die Deckkraft ist wie im ungefärbten Bild.
void verifySingleColor(const QImage &tinted, const QImage &original, const QColor &color)
{
    QCOMPARE(tinted.size(), original.size());
    const QImage t = tinted.convertToFormat(QImage::Format_ARGB32);
    const QImage o = original.convertToFormat(QImage::Format_ARGB32);
    int opaque = 0;
    for (int y = 0; y < t.height(); ++y) {
        for (int x = 0; x < t.width(); ++x) {
            const QRgb tp = t.pixel(x, y);
            const QRgb op = o.pixel(x, y);
            QVERIFY2(qAbs(qAlpha(tp) - qAlpha(op)) <= 1,
                     qPrintable(QStringLiteral("Deckkraft bei %1,%2: %3 statt %4")
                                    .arg(x).arg(y).arg(qAlpha(tp)).arg(qAlpha(op))));
            if (qAlpha(tp) < 128)
                continue;
            ++opaque;
            QVERIFY2(qAbs(qRed(tp) - color.red()) <= 1 && qAbs(qGreen(tp) - color.green()) <= 1
                         && qAbs(qBlue(tp) - color.blue()) <= 1,
                     qPrintable(QStringLiteral("Farbe bei %1,%2: %3 statt %4")
                                    .arg(x).arg(y)
                                    .arg(QColor(tp).name(), color.name())));
        }
    }
    QVERIFY(opaque > 0);
}

QImage plainRender(const QString &path, const QSize &size)
{
    return QIcon(path).pixmap(size).toImage();
}

} // namespace

class TestSymbols : public QObject
{
    Q_OBJECT

private slots:
    void colorfulSvgBecomesOneColor_data()
    {
        QTest::addColumn<QColor>("color");
        QTest::addColumn<int>("edge");
        QTest::newRow("Textfarbe, 28 px") << QColor(0xff, 0xff, 0xff) << 28;
        QTest::newRow("ausgegraut, 14 px") << QColor(0x96, 0x96, 0x96) << 14;
        QTest::newRow("hell (E-16), 64 px") << QColor(0x1d, 0x1f, 0x26) << 64;
    }

    void colorfulSvgBecomesOneColor()
    {
        QFETCH(QColor, color);
        QFETCH(int, edge);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("bunt.svg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(kColorfulSvg);
        file.close();

        const QSize size(edge, edge);
        const QImage original = plainRender(path, size);
        QVERIFY2(!original.isNull(), "SVG nicht gelesen (qt6-svg-plugins?)");
        // Die Vorlage ist wirklich mehrfarbig und hat halbdurchsichtige Pixel.
        QSet<QRgb> colors;
        bool halfTransparent = false;
        const QImage o = original.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < o.height(); ++y) {
            for (int x = 0; x < o.width(); ++x) {
                const QRgb p = o.pixel(x, y);
                if (qAlpha(p) == 255)
                    colors.insert(p & 0xffffff);
                if (qAlpha(p) > 0 && qAlpha(p) < 255)
                    halfTransparent = true;
            }
        }
        QVERIFY2(colors.size() >= 3, qPrintable(QString::number(colors.size())));
        QVERIFY(halfTransparent);

        verifySingleColor(tintedSymbol(path, size, color), original, color);
    }

    void missingFileGivesEmptyImage()
    {
        QVERIFY(tintedSymbol(QStringLiteral("/gibt/es/nicht.svg"), QSize(16, 16), Qt::white)
                    .isNull());
    }

    void connectionSymbolsUsePaletteColors_data()
    {
        QTest::addColumn<QString>("file");
        for (const char *f : {"twitch.svg", "youtube.svg", "obs.svg", "ndi.svg", "hinzufuegen.svg",
                              "kopieren.svg", "einfuegen.svg", "zuruecksetzen.svg"})
            QTest::newRow(f) << QString::fromLatin1(f);
    }

    void connectionSymbolsUsePaletteColors()
    {
        // Die Dateien unter src/app/symbole/: normal in Textfarbe, ausgegraut in der Farbe für
        // ausgegrauten Text, jeweils aus der Palette.
        QFETCH(QString, file);
        QPalette palette;
        palette.setColor(QPalette::Normal, QPalette::ButtonText, QColor(0x12, 0x34, 0x56));
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xab, 0xcd, 0xef));
        QApplication::setPalette(palette);
        const QSize size(28, 28);
        const QIcon icon = loadSymbol(file, size);
        QVERIFY(!icon.isNull());
        const QImage original = plainRender(symbolDir() + QLatin1Char('/') + file, size);
        verifySingleColor(icon.pixmap(size, QIcon::Normal).toImage(), original,
                          QColor(0x12, 0x34, 0x56));
        verifySingleColor(icon.pixmap(size, QIcon::Disabled).toImage(), original,
                          QColor(0xab, 0xcd, 0xef));
    }

    void userSymbolOverridesBuiltin()
    {
        // P07, Teil A: Eine Datei im Symbolordner des Nutzers ersetzt die mitgelieferte mit
        // demselben Namen und wird gleich eingefärbt. Eigener Ordner statt ~/.config.
        QTemporaryDir config;
        QVERIFY(config.isValid());
        const QByteArray previous = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", config.path().toLocal8Bit());
        const QString userDir = config.path() + QStringLiteral("/dons-streaming-studio/symbole");
        QVERIFY(QDir().mkpath(userDir));
        QFile file(userDir + QStringLiteral("/ndi.svg"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(kColorfulSvg);
        file.close();

        QVERIFY(hasUserSymbol(QStringLiteral("ndi.svg")));
        QVERIFY(!hasUserSymbol(QStringLiteral("twitch.svg")));
        QCOMPARE(symbolPath(QStringLiteral("ndi.svg")), file.fileName());
        QCOMPARE(symbolPath(QStringLiteral("twitch.svg")),
                 symbolDir() + QStringLiteral("/twitch.svg"));

        QPalette palette;
        palette.setColor(QPalette::Normal, QPalette::ButtonText, QColor(0x12, 0x34, 0x56));
        QApplication::setPalette(palette);
        const QSize size(28, 28);
        const QImage own = plainRender(file.fileName(), size);
        const QImage builtin = plainRender(symbolDir() + QStringLiteral("/ndi.svg"), size);
        QVERIFY(own != builtin);
        verifySingleColor(loadSymbol(QStringLiteral("ndi.svg"), size).pixmap(size).toImage(), own,
                          QColor(0x12, 0x34, 0x56));
        qputenv("XDG_CONFIG_HOME", previous);
    }

    // P10a: Programm-Symbol aus assets/branding/symbol, alle Grössen von 16 bis 512, in Farbe
    void appIconSizes()
    {
        const QIcon icon = appIcon(QStringLiteral(DSS_TEST_APP_ICON_DIR));
        QVERIFY(!icon.isNull());
        QCOMPARE(icon.availableSizes().size(), ::appIconSizes().size());
        for (const int size : ::appIconSizes()) {
            QVERIFY2(icon.availableSizes().contains(QSize(size, size)), qPrintable(QString::number(size)));
            const QImage image = icon.pixmap(QSize(size, size)).toImage();
            QCOMPARE(image.size(), QSize(size, size));
            // Ecken fast durchsichtig (kleine Grössen: Lanczos verwischt den Rahmen etwas)
            QVERIFY2(qAlpha(image.pixel(0, 0)) < 16, qPrintable(QString::number(size)));
            QVERIFY2(qAlpha(image.pixel(size - 1, size - 1)) < 16, qPrintable(QString::number(size)));
        }
        // Quadrat um den Fernseher: oberste und unterste Zeile ganz durchsichtig, links in der
        // Mitte der Rahmen deckend
        const QImage large = icon.pixmap(QSize(512, 512)).toImage();
        for (int x = 0; x < 512; ++x) {
            QCOMPARE(qAlpha(large.pixel(x, 0)), 0);
            QCOMPARE(qAlpha(large.pixel(x, 511)), 0);
        }
        QCOMPARE(qAlpha(large.pixel(12, 256)), 255);
        // nicht einfarbig: Gehäuse rot-braun, Knöpfe heller
        QVERIFY(qRed(large.pixel(12, 256)) > qBlue(large.pixel(12, 256)) + 40);
        // Ordner ohne Dateien: leeres Symbol
        QTest::ignoreMessage(QtWarningMsg, "Programm-Symbol fehlt: /gibt/es/nicht");
        QVERIFY(appIcon(QStringLiteral("/gibt/es/nicht")).isNull());
    }
};

int main(int argc, char *argv[])
{
    if (qgetenv("XDG_SESSION_TYPE") != "x11" || qEnvironmentVariableIsEmpty("DISPLAY")) {
        qInfo("übersprungen: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)");
        return 77;
    }
    QApplication app(argc, argv);
    TestSymbols test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_symbols.moc"
