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
        const auto form = [](int port, const QString &address) {
            SettingsForm f;
            f.port = port;
            f.address = address;
            return f;
        };
        QVERIFY(!settingsUpdateFromForm(current, form(8081, QStringLiteral(" 192.168.1.9 ")), false));
        std::optional<SettingsUpdate> u = settingsUpdateFromForm(current, form(8090, QStringLiteral("192.168.1.9")), false);
        QVERIFY(u);
        QCOMPARE(u->port, std::optional<int>(8090));
        QVERIFY(!u->homeAddress);
        u = settingsUpdateFromForm(current, form(8081, QString()), false);
        QVERIFY(u && !u->port);
        QCOMPARE(u->homeAddress, std::optional<QString>(QString()));
        u = settingsUpdateFromForm(current, form(80, QStringLiteral("abc")), false);
        QVERIFY(u && u->port == 80 && u->homeAddress == QStringLiteral("abc")); // prüft der Kern
        QVERIFY(settingsDialogHint().contains(QStringLiteral("Nur Adressen dieses PCs")));
        // P10b, Punkt 85: leere Adresse ohne Neustart
        QVERIFY(settingsDialogHint().contains(QStringLiteral("innerhalb von 2 s")));
        QVERIFY(!settingsDialogProblem(false, std::nullopt).isEmpty());
        QVERIFY(settingsDialogProblem(true, std::nullopt).isEmpty());
        SettingsInfo info{current, 1024, 32767, QStringLiteral("/x/kern.ini"), QStringLiteral("kaputt")};
        QCOMPARE(settingsDialogProblem(true, info), QStringLiteral("kaputt"));
    }

    // P10c: Bereich «OBS» im Dialog
    void settingsFormObs()
    {
        CoreSettings current{8081, QStringLiteral("192.168.1.9")};
        SettingsForm f;
        f.port = 8081;
        f.address = QStringLiteral("192.168.1.9");
        QVERIFY(!settingsUpdateFromForm(current, f, false));
        // Einzeln, nur Geändertes; Adresse ohne Leerzeichen am Rand
        f.obsEnabled = true;
        f.obsHost = QStringLiteral(" 192.168.1.116 ");
        f.obsPort = 4456;
        std::optional<SettingsUpdate> u = settingsUpdateFromForm(current, f, false);
        QVERIFY(u && !u->port && !u->homeAddress && !u->obsPassword);
        QCOMPARE(u->obsEnabled, std::optional<bool>(true));
        QCOMPARE(u->obsHost, std::optional<QString>(QStringLiteral("192.168.1.116")));
        QCOMPARE(u->obsPort, std::optional<int>(4456));
        // Leer = behalten; «Passwort löschen» schickt leer; ein neues Passwort geht vor
        current.obsEnabled = true;
        current.obsHost = QStringLiteral("192.168.1.116");
        current.obsPort = 4456;
        QVERIFY(!settingsUpdateFromForm(current, f, false));
        f.deleteObsPassword = true;
        u = settingsUpdateFromForm(current, f, false);
        QVERIFY(u && u->obsPassword && u->obsPassword->isEmpty());
        f.obsPassword = QStringLiteral("testwert");
        u = settingsUpdateFromForm(current, f, false);
        QVERIFY(u);
        QCOMPARE(u->obsPassword, std::optional<QString>(QStringLiteral("testwert")));
        // Rot und unverändert: «OK» versucht es wieder, aber nicht, wenn OBS aus ist
        f.deleteObsPassword = false;
        f.obsPassword.clear();
        u = settingsUpdateFromForm(current, f, true);
        QVERIFY(u);
        QCOMPARE(u->obsEnabled, std::optional<bool>(true));
        QVERIFY(!u->obsHost && !u->obsPort && !u->obsPassword);
        current.obsEnabled = false;
        f.obsEnabled = false;
        QVERIFY(!settingsUpdateFromForm(current, f, true));

        QVERIFY(obsPasswordPlaceholder(true, false).contains(QStringLiteral("behalten")));
        QVERIFY(obsPasswordPlaceholder(false, false).contains(QStringLiteral("kein Passwort")));
        QVERIFY(obsPasswordPlaceholder(true, true).contains(QStringLiteral("gelöscht")));
    }

    void obsToolTipTexts()
    {
        ObsStatusInfo status;
        // Ohne Stand und aus: wie die anderen Knöpfe
        QCOMPARE(obsToolTip(ConnectionState::Off, std::nullopt), QStringLiteral("OBS: nicht eingerichtet oder abgeschaltet"));
        status.message = QStringLiteral("Nicht eingerichtet oder abgeschaltet");
        QCOMPARE(obsToolTip(ConnectionState::Off, status), QStringLiteral("OBS: nicht eingerichtet oder abgeschaltet"));
        // Problem: die Meldung des Kerns
        status.message = QStringLiteral("OBS hat das Passwort abgelehnt. Im Dialog «Einstellungen» neu eingeben.");
        QCOMPARE(obsToolTip(ConnectionState::Error, status), status.message);
        status.message.clear();
        QCOMPARE(obsToolTip(ConnectionState::Connecting, status), QStringLiteral("OBS: Verbindungsversuch läuft"));
        // Verbunden: mit Einzelheiten, soweit bekannt
        QCOMPARE(obsToolTip(ConnectionState::Connected, status), QStringLiteral("OBS: verbunden"));
        status.obsVersion = QStringLiteral("33.0.1");
        QCOMPARE(obsToolTip(ConnectionState::Connected, status), QStringLiteral("OBS: verbunden (OBS 33.0.1)"));
        status.currentScene = {QStringLiteral("a"), QStringLiteral("Live")};
        QCOMPARE(obsToolTip(ConnectionState::Connected, status), QStringLiteral("OBS: verbunden (OBS 33.0.1, Szene «Live»)"));
    }

    // P10c, E-29: Zeile «OBS» im Dock «Steuerung»
    void obsSourceRowTexts()
    {
        const QUuid id = QUuid::createUuid();
        const ObsSourceInfo none{id, ObsSourceState::None, QString()};
        const ObsSourceInfo present{id, ObsSourceState::Present, QStringLiteral("Quelle")};
        const ObsSourceInfo missing{id, ObsSourceState::Missing, QStringLiteral("Quelle")};
        const QString create = QStringLiteral("In OBS anlegen …");
        const QString add = QStringLiteral("In weitere Szene einfügen …");

        ObsSourceRow row = obsSourceRow(false, ConnectionState::Connected, none);
        QCOMPARE(row.text, QStringLiteral("–"));
        QVERIFY(!row.canAct && !row.problem);
        QCOMPARE(row.buttonText, create);

        row = obsSourceRow(true, ConnectionState::Connected, none);
        QCOMPARE(row.text, QStringLiteral("Noch keine Quelle in OBS"));
        QVERIFY(row.canAct && !row.problem && !row.addsToScene);
        QCOMPARE(row.buttonText, create);

        // F6: Quelle da, weitere Szene
        row = obsSourceRow(true, ConnectionState::Connected, present);
        QCOMPARE(row.text, QStringLiteral("Quelle «Quelle» in OBS"));
        QVERIFY(row.canAct && !row.problem && row.addsToScene);
        QCOMPARE(row.buttonText, add);

        // F7: fehlt, wieder anlegen
        row = obsSourceRow(true, ConnectionState::Connected, missing);
        QCOMPARE(row.text, QStringLiteral("Quelle in OBS fehlt"));
        QVERIFY(row.canAct && row.problem && !row.addsToScene);
        QCOMPARE(row.buttonText, create);

        // Getrennt: letzter bekannter Stand, Knopf gesperrt
        row = obsSourceRow(true, ConnectionState::Error, present);
        QVERIFY(row.text.contains(QStringLiteral("gerade nicht verbunden")));
        QVERIFY(!row.canAct && row.problem);
        QCOMPARE(row.buttonText, add);
        row = obsSourceRow(true, ConnectionState::Error, none);
        QCOMPARE(row.text, QStringLiteral("OBS nicht verbunden"));
        QVERIFY(!row.canAct && row.problem);
        row = obsSourceRow(true, ConnectionState::Connecting, none);
        QVERIFY(!row.canAct && !row.problem);
        row = obsSourceRow(true, ConnectionState::Off, none);
        QCOMPARE(row.text, QStringLiteral("OBS nicht eingerichtet oder abgeschaltet"));
        QVERIFY(!row.canAct && !row.problem);
        // Tooltip nennt den Grund für den gesperrten Knopf
        QVERIFY(row.buttonToolTip.contains(QStringLiteral("verbundenem OBS")));
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
