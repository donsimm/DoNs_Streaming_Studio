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
        // P10-vor: laufende Übernahmen
        QCOMPARE(importStatusText({}), QString());
        QCOMPARE(importStatusText({QStringLiteral("a.png")}), QStringLiteral("Übernehme «a.png» …"));
        QCOMPARE(importStatusText({QStringLiteral("a.png"), QStringLiteral("b.mp4")}),
                 QStringLiteral("Übernehme 2 Dateien («a.png», «b.mp4») …"));
    }

    void settingsForm()
    {
        const CoreSettings current{8081, QStringLiteral("192.168.1.9")};
        QVERIFY(!settingsUpdateFromForm(current, 8081, QStringLiteral(" 192.168.1.9 ")));
        std::optional<SettingsUpdate> u = settingsUpdateFromForm(current, 8090, QStringLiteral("192.168.1.9"));
        QVERIFY(u);
        QCOMPARE(u->port, std::optional<int>(8090));
        QVERIFY(!u->homeAddress);
        u = settingsUpdateFromForm(current, 8081, QString());
        QVERIFY(u && !u->port);
        QCOMPARE(u->homeAddress, std::optional<QString>(QString()));
        u = settingsUpdateFromForm(current, 80, QStringLiteral("abc"));
        QVERIFY(u && u->port == 80 && u->homeAddress == QStringLiteral("abc")); // prüft der Kern
        QVERIFY(settingsDialogHint().contains(QStringLiteral("Nur Adressen dieses PCs")));
        QVERIFY(!settingsDialogProblem(false, std::nullopt).isEmpty());
        QVERIFY(settingsDialogProblem(true, std::nullopt).isEmpty());
        SettingsInfo info{current, 1024, 32767, QStringLiteral("/x/kern.ini"), QStringLiteral("kaputt")};
        QCOMPARE(settingsDialogProblem(true, info), QStringLiteral("kaputt"));
    }

    // P10, Teil C
    void browserSourceTexts()
    {
        const QUuid a = QUuid::createUuid();
        const QUuid b = QUuid::createUuid();
        BrowserSourceField f = browserSourceField(QUuid(), std::nullopt);
        QVERIFY(f.text.isEmpty() && !f.canCopy);
        QCOMPARE(f.placeholder, QStringLiteral("Kein Overlay gewählt"));
        f = browserSourceField(a, BrowserSourceInfo{b, QStringLiteral("http://x/"), QString()});
        QVERIFY(f.text.isEmpty() && !f.canCopy); // Antwort zu einem anderen Overlay
        f = browserSourceField(a, BrowserSourceInfo{a, QStringLiteral("http://192.168.1.9:8081/o/a/t/"), QString()});
        QCOMPARE(f.text, QStringLiteral("http://192.168.1.9:8081/o/a/t/"));
        QVERIFY(f.canCopy);
        f = browserSourceField(a, BrowserSourceInfo{a, QString(), QStringLiteral("Keine Adresse")});
        QVERIFY(f.text.isEmpty() && !f.canCopy);
        QCOMPARE(f.placeholder, QStringLiteral("Keine Adresse"));

        QCOMPARE(outputStateText(std::nullopt, 3), QStringLiteral("Ausgabe: –"));
        QCOMPARE(outputStateText(true, std::nullopt), QStringLiteral("Ausgabe: ein"));
        QCOMPARE(outputStateText(true, 0), QStringLiteral("Ausgabe: ein, keine Browserquelle verbunden"));
        QCOMPARE(outputStateText(true, 1), QStringLiteral("Ausgabe: ein, 1 Browserquelle verbunden"));
        QCOMPARE(outputStateText(false, 2), QStringLiteral("Ausgabe: aus (Browserquelle leer), 2 Browserquellen verbunden"));
        QVERIFY(outputToolTip(true).startsWith(QStringLiteral("Ausgabe ein")));
        QVERIFY(outputToolTip(false).startsWith(QStringLiteral("Ausgabe aus")));

        ServerStatusInfo s;
        s.enabled = true;
        QCOMPARE(serverMessages(true, s).text, QString());
        QCOMPARE(serverMessages(true, std::nullopt).shortText, QString());
        s.problems = {QStringLiteral("Port 8081 ist belegt, vermutlich von DoNs Streaming Studio im Einsatz (Prozess 12). "
                                     "Die Browserquelle ist nicht erreichbar. Anderen Port wählen.")};
        s.invalidUse = QStringLiteral("Eine Browserquelle (192.168.1.116) benutzt eine ungültige Adresse (/o/a/…).");
        QCOMPARE(serverMessages(false, s).text, QString()); // ohne Verbindung nichts
        const ServerMessages m = serverMessages(true, s);
        QCOMPARE(m.text, s.problems.first() + QLatin1Char('\n') + s.invalidUse);
        QCOMPARE(m.shortText, QStringLiteral("Browserquelle: Port 8081 ist belegt, vermutlich von DoNs Streaming Studio im "
                                             "Einsatz (Pro… (+1)"));
        s.problems = {QStringLiteral("Die Adresse im Heimnetz 192.168.1.9 ist auf diesem PC gerade nicht vorhanden "
                                     "(Netzwerkkabel?). Die Browserquelle ist nur auf diesem PC erreichbar (127.0.0.1) "
                                     "und wird es dort, sobald die Adresse da ist.")};
        s.invalidUse.clear();
        // Punkte in der Adresse beenden den Satz nicht; höchstens 90 Zeichen
        QCOMPARE(serverMessages(true, s).shortText,
                 QStringLiteral("Browserquelle: Die Adresse im Heimnetz 192.168.1.9 ist auf diesem PC gerade nicht vorhand…"));
        QCOMPARE(serverMessages(true, s).shortText.size(), 90);
        s = ServerStatusInfo();
        QVERIFY(serverMessages(true, s).text.contains(QStringLiteral("ohne Overlay-Server")));
    }
};

QTEST_GUILESS_MAIN(TestWindowData)
#include "tst_windowdata.moc"
