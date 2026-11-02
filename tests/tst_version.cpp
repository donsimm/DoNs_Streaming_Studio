// Prüft die Versionsangabe aus dss_common gegen PROJECT_VERSION aus CMake.

#include "version.h"

#include <QRegularExpression>
#include <QTest>

class TestVersion : public QObject
{
    Q_OBJECT

private slots:
    void versionMatchesCMake()
    {
        QCOMPARE(dss::version(), QStringLiteral(DSS_EXPECTED_VERSION));
    }

    void versionHasThreeNumbers()
    {
        static const QRegularExpression pattern(QStringLiteral("^\\d+\\.\\d+\\.\\d+$"));
        QVERIFY2(pattern.match(dss::version()).hasMatch(), qPrintable(dss::version()));
    }

    void versionLine()
    {
        QCOMPARE(dss::versionLine(QStringLiteral("dss-core")),
                 QStringLiteral("dss-core ") + QStringLiteral(DSS_EXPECTED_VERSION));
    }
};

// Ohne Qt Gui: QTEST_GUILESS_MAIN erzeugt nur eine QCoreApplication.
QTEST_GUILESS_MAIN(TestVersion)
#include "tst_version.moc"
