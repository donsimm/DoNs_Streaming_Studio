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
};

QTEST_GUILESS_MAIN(TestWindowSize)
#include "tst_windowsize.moc"
