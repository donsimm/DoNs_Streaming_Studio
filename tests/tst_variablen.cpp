// Prüft Variablen und Zähler der Befehle ohne Qt Gui (P14 a, Plan 2.9): Datei variablen.json mit
// Herkunft und Zeitpunkt, flüchtig und dauerhaft, je Zuschauer über die Twitch-ID, Erhöhen und
// Verringern, Zähler der Befehle. Dazu die weiteren Dateien mit Zuschauer-Daten (Plan 2.10):
// zuschauer.json, zitate.json, streamerbot-archiv.json. Erfundene Namen, IDs und Werte.

#include "variables.h"
#include "viewerdata.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QTimeZone>

using namespace dss::actions;

namespace {

const QDateTime kT1 = QDateTime(QDate(2026, 11, 15), QTime(12, 0), QTimeZone::UTC);
const QDateTime kT2 = QDateTime(QDate(2026, 11, 15), QTime(12, 5, 30, 250), QTimeZone::UTC);
const QUuid kCommand = QUuid::fromString(QLatin1String("a8000000-0000-4000-8000-000000000002"));
const QString kViewer = QStringLiteral("100000001");

VariableKey globalKey(const QString &name, bool persisted = true)
{
    return {VariableScope::Global, QString(), name, persisted};
}

VariableKey userKey(const QString &user, const QString &name, bool persisted = true)
{
    return {VariableScope::User, user, name, persisted};
}

// Beispiel einer Datei, wie der Kern sie schreibt (erfundene Werte)
const char kExample[] = R"({
    "command_counters": {
        "a8000000-0000-4000-8000-000000000002": {
            "counter": { "changed_at": "2026-11-15T12:00:00.000Z", "origin": "studio", "value": 12 },
            "user_counter": {
                "100000001": { "changed_at": "2026-11-14T20:00:00.000Z", "origin": "streamerbot", "value": 3 }
            }
        },
        "a8000000-0000-4000-8000-000000000001": { "counter": null, "user_counter": {} }
    },
    "format_version": 1,
    "global": {
        "firstChatterPlace": { "changed_at": "2026-11-15T12:00:00.000Z", "origin": "studio", "value": 2 },
        "gruss": { "changed_at": "2026-11-14T20:00:00.000Z", "origin": "streamerbot", "value": "Hallo" }
    },
    "users": {
        "100000001": {
            "firsts": { "changed_at": "2026-11-14T20:00:00.000Z", "origin": "streamerbot", "value": 4 },
            "watchtime": { "changed_at": "2026-11-14T20:00:00.000Z", "origin": "streamerbot", "value": 123456 }
        },
        "100000002": {
            "hugsReceived": { "changed_at": "2026-11-15T12:05:30.250+01:00", "origin": "studio", "value": "7" }
        }
    }
})";

QByteArray replaced(const char *from, const char *to)
{
    QByteArray data(kExample);
    data.replace(from, to);
    return data;
}

} // namespace

class TestVariables : public QObject
{
    Q_OBJECT

private slots:
    void readFile()
    {
        VariablesData d;
        QString error;
        QVERIFY2(parseVariables(kExample, &d, &error), qPrintable(error));
        QCOMPARE(d.global.size(), 2);
        QCOMPARE(d.global.value(QStringLiteral("firstChatterPlace")).value, QJsonValue(2));
        QCOMPARE(d.global.value(QStringLiteral("gruss")).origin, VariableOrigin::Streamerbot);
        QCOMPARE(d.users.value(kViewer).value(QStringLiteral("watchtime")).value, QJsonValue(123456));
        // Zeitpunkt mit Versatz wird nach UTC umgerechnet
        QCOMPARE(d.users.value(QStringLiteral("100000002")).value(QStringLiteral("hugsReceived")).changedAt,
                 QDateTime(QDate(2026, 11, 15), QTime(11, 5, 30, 250), QTimeZone::UTC));
        const CommandCounter c = d.commandCounters.value(kCommand);
        QCOMPARE(c.counter->value, QJsonValue(12));
        QCOMPARE(c.userCounters.value(kViewer).value, QJsonValue(3));
        QVERIFY(!d.commandCounters.value(QUuid::fromString(QLatin1String("a8000000-0000-4000-8000-000000000001")))
                     .counter.has_value());
    }

    void roundTrip()
    {
        VariablesData d;
        QString error;
        QVERIFY2(parseVariables(kExample, &d, &error), qPrintable(error));
        const QByteArray written = serializeVariables(d);
        VariablesData again;
        QVERIFY2(parseVariables(written, &again, &error), qPrintable(error));
        QCOMPARE(again, d);
        QCOMPARE(serializeVariables(again), written);
        // Zeitpunkte in UTC mit Millisekunden
        const QJsonObject o = QJsonDocument::fromJson(written).object();
        QCOMPARE(o.value(QStringLiteral("users")).toObject().value(QStringLiteral("100000002")).toObject()
                     .value(QStringLiteral("hugsReceived")).toObject().value(QStringLiteral("changed_at")).toString(),
                 QStringLiteral("2026-11-15T11:05:30.250Z"));
        // Leer
        QVERIFY(parseVariables(serializeVariables(VariablesData()), &again, &error));
        QCOMPARE(again, VariablesData());
    }

    void invalidFile_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("expected");
        QTest::newRow("kein JSON") << QByteArray("{ kaputt") << QString();
        QTest::newRow("neueres Format") << replaced("\"format_version\": 1", "\"format_version\": 2")
                                        << QStringLiteral("format_version 2 ist unbekannt");
        QTest::newRow("Feld unbekannt") << replaced("\"global\": {", "\"extra\": 1, \"global\": {")
                                        << QStringLiteral("Feld «extra»: unbekanntes Feld");
        QTest::newRow("Wert Wahrheitswert") << replaced("\"value\": 123456", "\"value\": true")
                                            << QStringLiteral("Feld «users.100000001.watchtime.value»: muss Text oder Zahl sein");
        QTest::newRow("Herkunft unbekannt") << replaced("\"origin\": \"studio\", \"value\": 2", "\"origin\": \"kick\", \"value\": 2")
                                            << QStringLiteral("Feld «global.firstChatterPlace.origin»: muss studio oder streamerbot sein");
        QTest::newRow("Zeitpunkt ungültig") << replaced("2026-11-15T12:00:00.000Z\", \"origin\": \"studio\", \"value\": 2",
                                                        "gestern\", \"origin\": \"studio\", \"value\": 2")
                                            << QStringLiteral("Feld «global.firstChatterPlace.changed_at»");
        QTest::newRow("Zähler Bruch") << replaced("\"value\": 12 }", "\"value\": 1.5 }")
                                      << QStringLiteral(".counter.value»: muss eine ganze Zahl ab 0 sein");
        QTest::newRow("Zähler Text") << replaced("\"value\": 3", "\"value\": \"3\"")
                                     << QStringLiteral(".user_counter.100000001.value»: muss eine ganze Zahl ab 0 sein");
        QTest::newRow("Befehl keine UUID") << replaced("\"a8000000-0000-4000-8000-000000000001\"", "\"Discord\"")
                                           << QStringLiteral("Feld «command_counters.Discord»: Schlüssel muss die UUID");
        QTest::newRow("Twitch-ID leer") << replaced("\"100000002\": {", "\"\": {")
                                        << QStringLiteral("Feld «users»: leere Twitch-ID");
        QTest::newRow("Name leer") << replaced("\"gruss\":", "\" \":") << QStringLiteral("Feld «global»: leerer Name");
        QTest::newRow("Wert ohne Herkunft") << replaced("\"origin\": \"streamerbot\", \"value\": \"Hallo\"", "\"value\": \"Hallo\"")
                                            << QStringLiteral("Feld «global.gruss.origin»: fehlt");
    }

    void invalidFile()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, expected);
        VariablesData d;
        d.global.insert(QStringLiteral("bleibt"), VariableValue{QJsonValue(1), VariableOrigin::Studio, kT1});
        const VariablesData before = d;
        QString error;
        QVERIFY(!parseVariables(data, &d, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY2(error.contains(expected), qPrintable(error));
        QCOMPARE(d, before);
    }

    void setAndGet()
    {
        VariableStore s;
        QVERIFY(!s.changed());
        QCOMPARE(s.set(globalKey(QStringLiteral("gruss")), QStringLiteral("Hallo"), kT1), QString());
        QVERIFY(s.changed());
        QCOMPARE(s.value(globalKey(QStringLiteral("gruss"))), std::optional<QJsonValue>(QStringLiteral("Hallo")));
        QCOMPARE(s.entry(globalKey(QStringLiteral("gruss")))->origin, VariableOrigin::Studio);
        QCOMPARE(s.entry(globalKey(QStringLiteral("gruss")))->changedAt, kT1);
        // flüchtig und dauerhaft sind getrennt
        QVERIFY(!s.value(globalKey(QStringLiteral("gruss"), false)));
        s.clearChanged();
        QCOMPARE(s.set(globalKey(QStringLiteral("gruss"), false), 5, kT1), QString());
        QVERIFY(!s.changed()); // flüchtig: nichts zu speichern
        QCOMPARE(s.value(globalKey(QStringLiteral("gruss"), false)), std::optional<QJsonValue>(5));
        QVERIFY(!s.entry(globalKey(QStringLiteral("gruss"), false)));
        QVERIFY(!s.data().global.value(QStringLiteral("gruss")).value.isDouble());
        // je Zuschauer über die Twitch-ID
        QCOMPARE(s.set(userKey(kViewer, QStringLiteral("firsts")), 1, kT1), QString());
        QCOMPARE(s.value(userKey(kViewer, QStringLiteral("firsts"))), std::optional<QJsonValue>(1));
        QVERIFY(!s.value(userKey(QStringLiteral("100000002"), QStringLiteral("firsts"))));
        QVERIFY(!s.value(globalKey(QStringLiteral("firsts"))));
        // Fehler
        QVERIFY(!s.set(globalKey(QString()), 1, kT1).isEmpty());
        QVERIFY(!s.set(userKey(QString(), QStringLiteral("x")), 1, kT1).isEmpty());
        QCOMPARE(s.set(globalKey(QStringLiteral("x")), true, kT1),
                 QStringLiteral("Variable «x»: Wert muss Text oder Zahl sein"));
        QVERIFY(!s.value(globalKey(QStringLiteral("x"))));
    }

    // Eine Änderung im Studio setzt die Herkunft auf studio (Plan 9.4)
    void originChanges()
    {
        VariablesData d;
        d.users[kViewer].insert(QStringLiteral("firsts"), VariableValue{QJsonValue(4), VariableOrigin::Streamerbot, kT1});
        VariableStore s;
        s.setData(d);
        QVERIFY(!s.changed());
        QCOMPARE(s.entry(userKey(kViewer, QStringLiteral("firsts")))->origin, VariableOrigin::Streamerbot);
        QJsonValue result;
        QCOMPARE(s.add(userKey(kViewer, QStringLiteral("firsts")), 1, kT2, &result), QString());
        QCOMPARE(result, QJsonValue(5));
        const VariableValue e = *s.entry(userKey(kViewer, QStringLiteral("firsts")));
        QCOMPARE(e.origin, VariableOrigin::Studio);
        QCOMPARE(e.changedAt, kT2);
        QVERIFY(s.changed());
    }

    void addAndRemove()
    {
        VariableStore s;
        QJsonValue result;
        // fehlt: gilt als 0
        QCOMPARE(s.add(globalKey(QStringLiteral("firstChatterPlace")), 1, kT1, &result), QString());
        QCOMPARE(result, QJsonValue(1));
        QCOMPARE(s.add(globalKey(QStringLiteral("firstChatterPlace")), -3, kT1, &result), QString());
        QCOMPARE(result, QJsonValue(-2));
        // Text, der eine Zahl ist
        s.set(globalKey(QStringLiteral("text")), QStringLiteral("7"), kT1);
        QCOMPARE(s.add(globalKey(QStringLiteral("text")), 0.5, kT1, &result), QString());
        QCOMPARE(result, QJsonValue(7.5));
        // anderer Text: Fehler, Wert bleibt
        s.set(globalKey(QStringLiteral("name")), QStringLiteral("Hallo"), kT1);
        QCOMPARE(s.add(globalKey(QStringLiteral("name")), 1, kT1), QStringLiteral("Variable «name» ist keine Zahl"));
        QCOMPARE(s.value(globalKey(QStringLiteral("name"))), std::optional<QJsonValue>(QStringLiteral("Hallo")));
        // flüchtig je Zuschauer
        QCOMPARE(s.add(userKey(kViewer, QStringLiteral("x"), false), 2, kT1, &result), QString());
        QCOMPARE(s.value(userKey(kViewer, QStringLiteral("x"), false)), std::optional<QJsonValue>(2));
        QVERIFY(!s.data().users.contains(kViewer));

        s.clearChanged();
        QVERIFY(s.remove(globalKey(QStringLiteral("name"))));
        QVERIFY(s.changed());
        QVERIFY(!s.remove(globalKey(QStringLiteral("name"))));
        QVERIFY(s.remove(userKey(kViewer, QStringLiteral("x"), false)));
        QVERIFY(!s.remove(userKey(kViewer, QStringLiteral("x"), false)));
        s.set(userKey(kViewer, QStringLiteral("y")), 1, kT1);
        QVERIFY(s.remove(userKey(kViewer, QStringLiteral("y"))));
        QVERIFY(!s.data().users.contains(kViewer)); // leerer Zuschauer fällt weg
    }

    void commandCounters()
    {
        VariableStore s;
        VariableStore::CommandCount c = s.countCommand(kCommand, kViewer, true, true, kT1);
        QCOMPARE(c.counter, 1);
        QCOMPARE(c.userCounter, 1);
        c = s.countCommand(kCommand, kViewer, true, true, kT2);
        QCOMPARE(c.counter, 2);
        QCOMPARE(c.userCounter, 2);
        c = s.countCommand(kCommand, QStringLiteral("100000002"), true, true, kT2);
        QCOMPARE(c.counter, 3);
        QCOMPARE(c.userCounter, 1);
        const CommandCounter stored = s.data().commandCounters.value(kCommand);
        QCOMPARE(stored.counter->value, QJsonValue(3));
        QCOMPARE(stored.counter->changedAt, kT2);
        QCOMPARE(stored.userCounters.value(kViewer).value, QJsonValue(2));
        // ohne Zuschauer nur der Zähler über alle
        c = s.countCommand(kCommand, QString(), true, true, kT2);
        QCOMPARE(c.counter, 4);
        QCOMPARE(c.userCounter, 0);

        // flüchtig: nicht in der Datei
        const QUuid other = QUuid::createUuid();
        s.clearChanged();
        c = s.countCommand(other, kViewer, false, false, kT1);
        QCOMPARE(c.counter, 1);
        QCOMPARE(c.userCounter, 1);
        c = s.countCommand(other, kViewer, false, false, kT1);
        QCOMPARE(c.counter, 2);
        QCOMPARE(c.userCounter, 2);
        QVERIFY(!s.data().commandCounters.contains(other));
        QVERIFY(!s.changed());

        // weiterzählen nach Laden (übernommene Zähler)
        VariableStore loaded;
        VariablesData d;
        QString error;
        QVERIFY2(parseVariables(kExample, &d, &error), qPrintable(error));
        loaded.setData(d);
        c = loaded.countCommand(kCommand, kViewer, true, true, kT2);
        QCOMPARE(c.counter, 13);
        QCOMPARE(c.userCounter, 4);
        QCOMPARE(loaded.data().commandCounters.value(kCommand).userCounters.value(kViewer).origin,
                 VariableOrigin::Studio);

        // Datei mit Zählern liest sich wieder
        VariablesData again;
        QVERIFY2(parseVariables(serializeVariables(loaded.data()), &again, &error), qPrintable(error));
        QCOMPARE(again, loaded.data());
    }

    // zuschauer.json: weitere Felder aus Streamer.bot bleiben unverändert (Plan 2.10)
    void viewers()
    {
        const QByteArray data = R"({"format_version": 1, "viewers": {
            "100000001": {"login": "testzuschauer", "display_name": "TestZuschauer",
                          "test_feld_aus_streamerbot": {"a": [1, 2]}, "lastActive": "2026-11-01"},
            "100000002": {"login": "zweiter", "display_name": ""}}})";
        ViewersData d;
        QString error;
        QVERIFY2(parseViewers(data, &d, &error), qPrintable(error));
        QCOMPARE(d.viewers.size(), 2);
        const Viewer v = d.viewers.value(kViewer);
        QCOMPARE(v.login, QStringLiteral("testzuschauer"));
        QCOMPARE(v.displayName, QStringLiteral("TestZuschauer"));
        QCOMPARE(v.extra.value(QStringLiteral("lastActive")).toString(), QStringLiteral("2026-11-01"));
        QCOMPARE(QJsonDocument::fromJson(serializeViewers(d)).object(), QJsonDocument::fromJson(data).object());

        // Nachführen: nur Login und Anzeigename, weitere Felder bleiben
        QVERIFY(!noteViewer(&d, kViewer, QStringLiteral("testzuschauer"), QStringLiteral("TestZuschauer")));
        QVERIFY(noteViewer(&d, kViewer, QStringLiteral("neuername"), QStringLiteral("NeuerName")));
        QCOMPARE(d.viewers.value(kViewer).login, QStringLiteral("neuername"));
        QCOMPARE(d.viewers.value(kViewer).extra, v.extra);
        QVERIFY(noteViewer(&d, QStringLiteral("100000003"), QStringLiteral("dritter"), QStringLiteral("Dritter")));
        QCOMPARE(d.viewers.size(), 3);
        QVERIFY(!noteViewer(&d, QString(), QStringLiteral("x"), QStringLiteral("X")));
        QVERIFY(!noteViewer(&d, QStringLiteral("100000004"), QString(), QStringLiteral("X")));
        QCOMPARE(d.viewers.size(), 3);

        for (const QByteArray &bad : {QByteArray(R"({"format_version": 1, "viewers": {"1": {"display_name": "x"}}})"),
                                      QByteArray(R"({"format_version": 1, "viewers": {"1": {"login": "", "display_name": "x"}}})"),
                                      QByteArray(R"({"format_version": 1, "viewers": {"1": {"login": "a", "display_name": 3}}})"),
                                      QByteArray(R"({"format_version": 1, "viewers": {"": {"login": "a", "display_name": ""}}})"),
                                      QByteArray(R"({"format_version": 1, "viewers": []})"),
                                      QByteArray(R"({"format_version": 1, "viewers": {}, "x": 1})"),
                                      QByteArray(R"({"format_version": 2, "viewers": {}})"),
                                      QByteArray(R"({"viewers": {}})")}) {
            ViewersData unchanged = d;
            QVERIFY2(!parseViewers(bad, &unchanged, &error), bad.constData());
            QCOMPARE(unchanged, d);
        }
    }

    void quotesAndArchive()
    {
        const QByteArray quotes = R"({"format_version": 1, "quotes": [
            {"id": 1, "quote": "Erfundenes Zitat", "user": "testzuschauer"}, {"id": 2}]})";
        QuotesData q;
        QString error;
        QVERIFY2(parseQuotes(quotes, &q, &error), qPrintable(error));
        QCOMPARE(q.quotes.size(), 2);
        QCOMPARE(q.quotes.at(0).value(QStringLiteral("quote")).toString(), QStringLiteral("Erfundenes Zitat"));
        QCOMPARE(QJsonDocument::fromJson(serializeQuotes(q)).object(), QJsonDocument::fromJson(quotes).object());
        QVERIFY(!parseQuotes(R"({"format_version": 1, "quotes": [1]})", &q, &error));
        QCOMPARE(error, QStringLiteral("Feld «quotes[0]»: muss ein Objekt sein"));
        QVERIFY(!parseQuotes(R"({"format_version": 1})", &q, &error));
        QCOMPARE(error, QStringLiteral("Feld «quotes»: fehlt"));

        const QByteArray archive = R"({"format_version": 1, "files": {
            "kick_data.db": {"users": [{"_id": "k1", "name": "erfunden"}]},
            "globals.db": {"settings": {"paket": {"verschachtelt": true}}, "leer": null}}})";
        ArchiveData a;
        QVERIFY2(parseArchive(archive, &a, &error), qPrintable(error));
        QCOMPARE(a.files.size(), 2);
        QCOMPARE(a.files.value(QStringLiteral("kick_data.db")).value(QStringLiteral("users")).toArray().size(), 1);
        QCOMPARE(QJsonDocument::fromJson(serializeArchive(a)).object(), QJsonDocument::fromJson(archive).object());
        QVERIFY(!parseArchive(R"({"format_version": 1, "files": {"x.db": []}})", &a, &error));
        QVERIFY2(error.startsWith(QStringLiteral("Feld «files.x.db»")), qPrintable(error));
    }

    void dataFileList()
    {
        QStringList names;
        for (const DataFileFormat &f : actionDataFiles()) {
            names.append(f.fileName);
            QCOMPARE(f.version, 1);
            QString error;
            QVERIFY(!f.check("{}", &error));
        }
        QCOMPARE(names, (QStringList{QStringLiteral("aktionen.json"), QStringLiteral("variablen.json"),
                                     QStringLiteral("zuschauer.json"), QStringLiteral("zitate.json"),
                                     QStringLiteral("streamerbot-archiv.json")}));
    }
};

QTEST_GUILESS_MAIN(TestVariables)
#include "tst_variablen.moc"
