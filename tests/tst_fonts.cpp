// Prüft ohne Fenster (Plattform «offscreen», gesetzt in tests/CMakeLists.txt), dass die
// Schriftdateien unter src/app/schriften/ geladen werden und «Open Sans» liefern (Auftrag P06).

#include "fonts.h"

#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QTest>

class TestFonts : public QObject
{
    Q_OBJECT

private slots:
    void loadsOpenSans()
    {
        const QStringList families = loadWindowFonts(QStringLiteral(DSS_FONT_DIR));
        QCOMPARE(families, QStringList{QStringLiteral("Open Sans")});
    }

    void regularAndBoldAvailable()
    {
        const QStringList styles = QFontDatabase::styles(QStringLiteral("Open Sans"));
        QVERIFY2(styles.contains(QStringLiteral("Regular")), qPrintable(styles.join(u',')));
        QVERIFY2(styles.contains(QStringLiteral("Bold")), qPrintable(styles.join(u',')));
    }

    void fontResolvesToOpenSans()
    {
        // QFontInfo nennt die Schrift, die Qt tatsächlich nimmt (kein Ersatz).
        QFont font(QStringLiteral("Open Sans"), 10);
        QCOMPARE(QFontInfo(font).family(), QStringLiteral("Open Sans"));
        font.setBold(true);
        QCOMPARE(QFontInfo(font).family(), QStringLiteral("Open Sans"));
        QVERIFY(QFontInfo(font).bold());
    }

    void missingDirectoryGivesNothing()
    {
        QTest::ignoreMessage(QtWarningMsg, "Keine Schriftdatei in /gibt/es/nicht");
        QVERIFY(loadWindowFonts(QStringLiteral("/gibt/es/nicht")).isEmpty());
    }
};

QTEST_MAIN(TestFonts)
#include "tst_fonts.moc"
