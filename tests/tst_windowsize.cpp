// Prüft das Lesen der Fenstergrösse für dss-app --fenstergroesse ohne Fenster (P06).

#include "windowsize.h"

#include <QTest>

using namespace dss;

class TestWindowSize : public QObject
{
    Q_OBJECT

private slots:
    void valid_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QSize>("expected");
        QTest::newRow("1920x1080") << QStringLiteral("1920x1080") << QSize(1920, 1080);
        QTest::newRow("1280x800") << QStringLiteral("1280x800") << QSize(1280, 800);
        QTest::newRow("kleinste") << QStringLiteral("1x1") << QSize(1, 1);
        QTest::newRow("grösste") << QStringLiteral("10000x10000") << QSize(10000, 10000);
        QTest::newRow("führende Null") << QStringLiteral("0800x0600") << QSize(800, 600);
    }

    void valid()
    {
        QFETCH(QString, text);
        QFETCH(QSize, expected);
        QSize size;
        QVERIFY(parseWindowSize(text, &size));
        QCOMPARE(size, expected);
    }

    void invalid_data()
    {
        QTest::addColumn<QString>("text");
        for (const char *t : {"", "1920", "1920x", "x1080", "1920X1080", "1920 x 1080",
                              " 1920x1080", "1920x1080 ", "0x1080", "1920x0", "10001x100",
                              "-1920x1080", "19.5x1080", "1920x1080x3", "abcxdef", "123456x1"})
            QTest::newRow(t) << QString::fromUtf8(t);
    }

    void invalid()
    {
        QFETCH(QString, text);
        QSize size(7, 7);
        QVERIFY(!parseWindowSize(text, &size));
        QCOMPARE(size, QSize(7, 7));
    }

    // Leistungsanzeige im Kopfbereich (P09b, Entscheid 2): 600 px, höchstens ein Drittel.
    void headerGauges()
    {
        QCOMPARE(headerGaugesWidth(1920), 600);
        QCOMPARE(headerGaugesWidth(1800), 600); // genau ein Drittel
        QCOMPARE(headerGaugesWidth(1799), 599);
        QCOMPARE(headerGaugesWidth(1280), 426);
        QCOMPARE(headerGaugesWidth(3840), 600);
        QCOMPARE(headerGaugesWidth(2), 0);
        QCOMPARE(headerGaugesWidth(0), 0);
        QCOMPARE(headerGaugesWidth(-5), 0);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(headerGaugeWidth(600, i), 200);
        QCOMPARE(headerGaugeWidth(426, 0), 142);
        QCOMPARE(headerGaugeWidth(427, 0), 143);
        QCOMPARE(headerGaugeWidth(427, 1), 142);
        QCOMPARE(headerGaugeWidth(428, 1), 143);
        QCOMPARE(headerGaugeWidth(428, 2), 142);
        QCOMPARE(headerGaugeWidth(600, 3), 0);
        for (int total : {0, 1, 2, 425, 599, 600}) {
            int sum = 0;
            for (int i = 0; i < kHeaderGaugeCount; ++i)
                sum += headerGaugeWidth(total, i);
            QCOMPARE(sum, total);
        }
    }

    // P10a Teil A 1: Verbindungsknöpfe mittig zwischen Reitern und Leistungsanzeige.
    void headerCentered()
    {
        QCOMPARE(headerCenteredX(200, 1300, 200), 650);  // je 450 links und rechts
        QCOMPARE(headerCenteredX(200, 1301, 200), 650);  // Rest nach rechts
        QCOMPARE(headerCenteredX(200, 400, 200), 200);   // genau Platz
        QCOMPARE(headerCenteredX(200, 300, 200), 200);   // zu wenig Platz: ab den Reitern
        QCOMPARE(headerCenteredX(200, 100, 200), 200);
        QCOMPARE(headerCenteredX(0, 10, 0), 5);
        QCOMPARE(headerCenteredX(10, 20, -4), 15);       // negative Breite wie 0
    }
};

QTEST_GUILESS_MAIN(TestWindowSize)
#include "tst_windowsize.moc"
