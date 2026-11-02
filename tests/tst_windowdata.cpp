// Prüft die Aufbereitung der Daten vom Kern für das Fenster ohne Qt Gui (P07, Teil F).

#include "windowdata.h"

#include <QTest>

using namespace dss;

class TestWindowData : public QObject
{
    Q_OBJECT

private slots:
    void gaugeTexts()
    {
        QCOMPARE(cpuText(std::nullopt), QStringLiteral("–"));
        QCOMPARE(cpuText(0.6), QStringLiteral("1 %"));
        QCOMPARE(cpuText(4.104), QStringLiteral("4 %"));
        QCOMPARE(cpuText(100.0), QStringLiteral("100 %"));
        // Wie auf dem Bild: «0.8 / 7.8 GiB»
        QCOMPARE(ramText(qint64(838861), qint64(8178892)), QStringLiteral("0.8 / 7.8 GiB"));
        QCOMPARE(ramText(qint64(3713872), qint64(32809248)), QStringLiteral("3.5 / 31.3 GiB"));
        QCOMPARE(ramText(std::nullopt, qint64(1)), QStringLiteral("–"));
        QCOMPARE(lanText(std::nullopt), QStringLiteral("–"));
        QCOMPARE(lanText(LanStats{QStringLiteral("enp3s0"), 1.056, 0.4}),
                 QStringLiteral("↓ 1 ↑ 0 kbit/s"));
        QCOMPARE(lanText(LanStats{QStringLiteral("enp3s0"), 12345.6, 789.4}),
                 QStringLiteral("↓ 12346 ↑ 789 kbit/s"));
        QVERIFY(lanToolTip(LanStats{QStringLiteral("enp3s0"), 1, 2}).contains(QStringLiteral("enp3s0")));
    }

    void history()
    {
        ValueHistory h(3);
        QCOMPARE(h.maximum(), 0.0);
        h.add(1.0);
        h.add(std::nullopt);
        h.add(5.0);
        h.add(2.0);
        QCOMPARE(h.values().size(), 3);
        QVERIFY(!h.values().at(0)); // ältester Wert (1.0) ist herausgefallen
        QCOMPARE(*h.values().at(2), 2.0);
        QCOMPARE(h.maximum(), 5.0);
        QCOMPARE(lanScaleMaximum(h), 5.0);
        h.clear();
        QCOMPARE(lanScaleMaximum(h), 1.0);
        QCOMPARE(ValueHistory().capacity(), 60);
    }

    void sourceOrderLikeObs()
    {
        Overlay o;
        for (int i = 0; i < 3; ++i) {
            Source s;
            s.id = QUuid::createUuid();
            s.name = QStringLiteral("Q%1").arg(i);
            s.visible = i != 1;
            o.sources.append(s);
        }
        const QList<SourceRow> rows = sourceRows(o);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).name, QStringLiteral("Q2")); // vorderste oben
        QCOMPARE(rows.at(2).name, QStringLiteral("Q0"));
        QCOMPARE(rows.at(1).visible, false);
        QCOMPARE(findSource(o, o.sources.at(1).id), &o.sources.at(1));
        QCOMPARE(findSource(o, QUuid::createUuid()), nullptr);
        QCOMPARE(visibilityToolTip(false), QStringLiteral("ausgeblendet"));
    }

    void misc()
    {
        QCOMPARE(rotationDegrees(125), 12.5);
        QCOMPARE(rotationDegrees(3599), 359.9);
        QCOMPARE(coreStatusText(true), QStringLiteral("Kern-Dienst: verbunden"));
        QCOMPARE(coreStatusText(false), QStringLiteral("Kern-Dienst: nicht verbunden"));
    }
};

QTEST_GUILESS_MAIN(TestWindowData)
#include "tst_windowdata.moc"
