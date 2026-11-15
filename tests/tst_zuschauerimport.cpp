// Prüft den Import der Zuschauer-Daten aus Streamer.bot ohne Kern und ohne Qt Gui (P14 z1a, Plan
// 9.3 bis 9.5): Lesen des Exports, Zuordnung über die ID und über den Login, fehlende ID ins Archiv,
// Probelauf schreibt nichts, Sicherung vor dem Schreiben, zweiter Import mit im Studio geänderten
// Werten, Überschreiben, kein Name, keine ID und kein Wert im Bericht. Erfundene Exportdaten in
// tests/daten/streamerbot-export/ (LIESMICH.md dort).

#include "actionjson.h"
#include "viewerimport.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace dss::actions;

namespace {

const QDateTime kNow = QDateTime(QDate(2026, 11, 15), QTime(12, 0), QTimeZone::UTC);
const QDateTime kLater = QDateTime(QDate(2026, 11, 16), QTime(12, 0), QTimeZone::UTC);
const QUuid kCommand1 = QUuid::fromString(QLatin1String("c1000000-0000-4000-8000-000000000001"));
const QUuid kCommand2 = QUuid::fromString(QLatin1String("c1000000-0000-4000-8000-000000000002"));
const QUuid kGroup = QUuid::fromString(QLatin1String("b1000000-0000-4000-8000-000000000001"));
const QString kUser1 = QStringLiteral("100000001");
const QString kUser2 = QStringLiteral("100000002");
const QString kUser3 = QStringLiteral("100000003");
const QString kUser4 = QStringLiteral("100000004");

QDateTime utc(int day, int hour, int ms = 0)
{
    return QDateTime(QDate(2026, 11, day), QTime(hour, 0, 0, ms), QTimeZone::UTC);
}

StreamerbotExport readExport()
{
    StreamerbotExport source;
    QString error;
    if (!readStreamerbotExport(QStringLiteral(DSS_TEST_EXPORT_DIR), &source, &error))
        qFatal("Export nicht lesbar: %s", qPrintable(error));
    return source;
}

// Stand vor dem ersten Import: aktionen.json vorhanden, ein Zuschauer schon aus dem Chat bekannt
ImportState startState()
{
    ImportState state;
    state.actions = ActionsConfig{};
    noteViewer(&state.viewers, kUser1, QStringLiteral("testzuschauer1"), QStringLiteral("TestZuschauer1Chat"));
    return state;
}

VariableValue fromStreamerbot(double value, const QDateTime &at)
{
    return {QJsonValue(value), VariableOrigin::Streamerbot, at};
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

void writeBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

QMap<QString, QByteArray> snapshot(const QString &dir)
{
    QMap<QString, QByteArray> files;
    for (const QString &name : QDir(dir).entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot))
        files.insert(name, readBytes(QDir(dir).filePath(name)));
    return files;
}

} // namespace

class TestZuschauerImport : public QObject
{
    Q_OBJECT

private slots:
    void readsExport();
    void mapsToStudio();
    void archivesWhatHasNoPlace();
    void reportHasNoNamesOrValues();
    void dryRunWritesNothing();
    void invalidFileStops();
    void writesWithBackup();
    void secondImportKeepsStudioChanges();
};

void TestZuschauerImport::readsExport()
{
    // quotes.dat hat wie das Original eine UTF-8-Kennung (BOM)
    QVERIFY(readBytes(QStringLiteral(DSS_TEST_EXPORT_DIR "/quotes.json")).startsWith("\xEF\xBB\xBF"));

    const StreamerbotExport source = readExport();
    QStringList names;
    for (const ExportCollection &c : source.collections)
        names.append(c.file + QLatin1Char('/') + c.collection);
    QCOMPARE(names, QStringList({
        QStringLiteral("commands.db/commandUsage"), QStringLiteral("commands.db/commandUsageUserCounts"),
        QStringLiteral("firstwords.db/last_update"), QStringLiteral("globals.db/globals"),
        QStringLiteral("globals.db/temp_user_globals"), QStringLiteral("globals.db/user_globals"),
        QStringLiteral("kick_data.db/channelRewards"), QStringLiteral("kick_data.db/settings"),
        QStringLiteral("neue_datei.db/neue_sammlung"), QStringLiteral("pronouns.db/pronouns"),
        QStringLiteral("pronouns.db/user_custom_pronouns"), QStringLiteral("twitch_data.db/moderators"),
        QStringLiteral("twitch_data.db/subscriptionHistory"), QStringLiteral("twitch_data.db/userColors"),
        QStringLiteral("youtube_data.db/superChat")}));
    QVERIFY(source.quotes.has_value());
    QVERIFY(source.users.has_value());
    QVERIFY(source.otherJson.isEmpty());
    // auth/ enthält kein JSON: wäre es gelesen worden, schlüge readStreamerbotExport fehl
    QStringList skipped = source.skipped;
    skipped.sort();
    QCOMPARE(skipped, QStringList({QStringLiteral("LIESMICH.md"), QStringLiteral("auth")}));

    StreamerbotExport none;
    QString error;
    QVERIFY(!readStreamerbotExport(QStringLiteral(DSS_TEST_EXPORT_DIR "/fehlt"), &none, &error));
    QVERIFY(!error.isEmpty());
}

void TestZuschauerImport::mapsToStudio()
{
    const ImportResult result = planViewerImport(readExport(), startState(), {false, kNow});
    const VariablesData &vars = result.state.variables;
    const ImportReport &r = result.report;

    // Globale Variablen: nur einfache Werte, Zeitpunkt aus lastWrite (7 Nachkommastellen → ms)
    QCOMPARE(vars.global.keys(),
             QStringList({QStringLiteral("testGross"), QStringLiteral("testText"), QStringLiteral("testZaehler")}));
    QCOMPARE(vars.global.value(QStringLiteral("testZaehler")), fromStreamerbot(42, utc(1, 18)));
    QCOMPARE(vars.global.value(QStringLiteral("testGross")).value.toDouble(), 5000000000.0);
    QCOMPARE(vars.global.value(QStringLiteral("testText")),
             (VariableValue{QJsonValue(QStringLiteral("Hallo Testwert")), VariableOrigin::Streamerbot, utc(1, 18, 123)}));
    QCOMPARE(r.globalVariables, (ImportCount{3, 0, 0, 0, 0}));

    // Je Zuschauer: über die ID, über den Login (aus twitch_data.db), der neuere doppelte gilt,
    // firstwords mit Vorsilbe der Datei
    QCOMPARE(vars.users.keys(), QStringList({kUser1, kUser2, kUser3}));
    QCOMPARE(vars.users[kUser1].value(QStringLiteral("watchtime")), fromStreamerbot(1200, utc(1, 18)));
    QCOMPARE(vars.users[kUser1].value(QStringLiteral("firsts")), fromStreamerbot(7, utc(3, 18)));
    QCOMPARE(vars.users[kUser2].value(QStringLiteral("watchtime")), fromStreamerbot(300, utc(1, 18)));
    QCOMPARE(vars.users[kUser2].value(QStringLiteral("firstwords.count")), fromStreamerbot(3, kNow));
    QCOMPARE(vars.users[kUser3].value(QStringLiteral("hugsReceived")).value, QJsonValue(QStringLiteral("2")));
    QCOMPARE(r.userVariables, (ImportCount{5, 0, 0, 0, 0}));
    QCOMPARE(r.userVariableNames, (QMap<QString, int>{{QStringLiteral("firsts"), 1},
                                                       {QStringLiteral("firstwords.count"), 1},
                                                       {QStringLiteral("hugsReceived"), 1},
                                                       {QStringLiteral("watchtime"), 2}}));
    QCOMPARE(r.withoutId, 1);  // Login ohne ID
    QCOMPARE(r.duplicates, 2); // user_globals und users.dat

    // Zähler der Befehle: über alle und je Twitch-Zuschauer, Zeitpunkt der letzten Benutzung
    QCOMPARE(vars.commandCounters.keys(), QList<QUuid>({kCommand1, kCommand2}));
    const CommandCounter &c1 = vars.commandCounters[kCommand1];
    QCOMPARE(*c1.counter, fromStreamerbot(3, utc(3, 18)));
    QCOMPARE(c1.userCounters.value(kUser1), fromStreamerbot(2, utc(2, 18)));
    QCOMPARE(c1.userCounters.value(kUser2), fromStreamerbot(1, utc(3, 18)));
    QCOMPARE(*vars.commandCounters[kCommand2].counter, fromStreamerbot(1, utc(4, 18)));
    QVERIFY(vars.commandCounters[kCommand2].userCounters.isEmpty()); // YouTube
    QCOMPARE(r.commandCounters, (ImportCount{4, 0, 0, 0, 0}));

    // Zuschauer: aus users.dat (Login, Anzeigename, weitere Felder) und twitch_data.db
    const QMap<QString, Viewer> &viewers = result.state.viewers.viewers;
    QCOMPARE(viewers.keys(), QStringList({kUser1, kUser2, kUser3, kUser4}));
    QCOMPARE(viewers[kUser1].displayName, QStringLiteral("TestZuschauer1Chat")); // aus dem Chat, bleibt
    QCOMPARE(viewers[kUser1].extra.value(QStringLiteral("role")).toInt(), 1);
    QCOMPARE(viewers[kUser1].extra.keys(),
             QStringList({QStringLiteral("exempt"), QStringLiteral("firstMessage"), QStringLiteral("lastActive"),
                          QStringLiteral("present"), QStringLiteral("previousActive"), QStringLiteral("role"),
                          QStringLiteral("subscribed"), QStringLiteral("type")}));
    QCOMPARE(viewers[kUser2].displayName, QStringLiteral("TestZuschauer2")); // der neuere Eintrag
    QCOMPARE(viewers[kUser2].extra.value(QStringLiteral("role")).toInt(), 2);
    QCOMPARE(viewers[kUser3], (Viewer{QStringLiteral("testzuschauer3"), QStringLiteral("TestZuschauer3"), {}}));
    QCOMPARE(viewers[kUser4], (Viewer{QStringLiteral("testzuschauer4"), QStringLiteral("TestZuschauer4"), {}}));
    QCOMPARE(r.viewers, (ImportCount{3, 1, 0, 0, 0}));
    QCOMPARE(r.userTypes, (QMap<QString, int>{{QStringLiteral("kick"), 1}, {QStringLiteral("twitch"), 3}}));

    // Zitate unverändert
    QCOMPARE(result.state.quotes.quotes.size(), 2);
    QCOMPARE(result.state.quotes.quotes[0].value(QStringLiteral("quote")).toString(),
             QStringLiteral("Ein erfundenes Testzitat"));
    QCOMPARE(result.state.quotes.quotes[1].keys().size(), 7);
    QCOMPARE(r.quotes, (ImportCount{2, 0, 0, 0, 0}));

    // Benutzergruppen nach aktionen.json, Mitglieder als Twitch-ID
    QCOMPARE(result.state.actions->groups.size(), 1);
    const UserGroup &group = result.state.actions->groups[0];
    QCOMPARE(group.id, kGroup);
    QCOMPARE(group.name, QStringLiteral("Testgruppe"));
    QCOMPARE(group.users, (QList<UserRef>{{kUser1, {}}, {kUser2, {}}}));
    QCOMPARE(group.extra, (QJsonObject{{QStringLiteral("bots"), false}}));
    QCOMPARE(r.groups, (ImportCount{1, 0, 0, 0, 0}));
    QVERIFY(!r.groupsSkipped);

    // Ohne aktionen.json keine Gruppen
    ImportState noActions = startState();
    noActions.actions.reset();
    const ImportResult skipped = planViewerImport(readExport(), noActions, {false, kNow});
    QVERIFY(skipped.report.groupsSkipped);
    QVERIFY(!skipped.state.actions);
}

void TestZuschauerImport::archivesWhatHasNoPlace()
{
    const ImportResult result = planViewerImport(readExport(), startState(), {false, kNow});
    const ImportReport &r = result.report;
    QCOMPARE(r.archived, (QMap<QString, int>{{QStringLiteral("commands.db/commandUsage"), 4},
                                             {QStringLiteral("globals.db/globals"), 3},
                                             {QStringLiteral("globals.db/user_globals"), 3},
                                             {QStringLiteral("kick_data.db/settings"), 1},
                                             {QStringLiteral("neue_datei.db/neue_sammlung"), 1},
                                             {QStringLiteral("pronouns.db/pronouns"), 1},
                                             {QStringLiteral("twitch_data.db/subscriptionHistory"), 1},
                                             {QStringLiteral("twitch_data.db/userColors"), 1},
                                             {QStringLiteral("users.dat/groups"), 1},
                                             {QStringLiteral("users.dat/users"), 2}}));
    QCOMPARE(r.emptyCollections.size(), 6);
    QVERIFY(r.emptyCollections.contains(QStringLiteral("youtube_data.db/superChat")));

    const QMap<QString, QJsonObject> &files = result.state.archive.files;
    QCOMPARE(files.keys(), QStringList({QStringLiteral("commands.db"), QStringLiteral("globals.db"),
                                        QStringLiteral("kick_data.db"), QStringLiteral("neue_datei.db"),
                                        QStringLiteral("pronouns.db"), QStringLiteral("quotes.dat"),
                                        QStringLiteral("twitch_data.db"), QStringLiteral("users.dat")}));
    // Nicht einfache globale Werte unverändert (wahr/falsch, JSON-Text, ohne Wert)
    QStringList globals;
    for (const QJsonValue &doc : files[QStringLiteral("globals.db")].value(QStringLiteral("globals")).toArray())
        globals.append(doc.toObject().value(QStringLiteral("_id")).toString());
    QCOMPARE(globals, QStringList({QStringLiteral("testSchalter"), QStringLiteral("testPaket"),
                                   QStringLiteral("testOhneWert")}));
    // Unbekannte Datei unverändert, mit unbekanntem Feld
    QCOMPARE(files[QStringLiteral("neue_datei.db")].value(QStringLiteral("neue_sammlung")).toArray()[0].toObject()
                 .value(QStringLiteral("unbekanntesFeld")).toString(),
             QStringLiteral("Testinhalt"));
    QCOMPARE(files[QStringLiteral("quotes.dat")],
             (QJsonObject{{QStringLiteral("t"), QStringLiteral("Testart")}, {QStringLiteral("version"), 1}}));
    QCOMPARE(files[QStringLiteral("users.dat")].value(QStringLiteral("version")).toInt(), 1);

    // Das Archiv besteht die strenge Prüfung von streamerbot-archiv.json
    ArchiveData parsed;
    QString error;
    QVERIFY2(parseArchive(serializeArchive(result.state.archive), &parsed, &error), qPrintable(error));
    QCOMPARE(parsed, result.state.archive);
}

void TestZuschauerImport::reportHasNoNamesOrValues()
{
    const ImportResult result = planViewerImport(readExport(), startState(), {false, kNow});
    for (const bool final : {false, true}) {
        const QString text = importReportText(result.report, final);
        QVERIFY(text.contains(final ? QStringLiteral("endgültig") : QStringLiteral("Probelauf")));
        QVERIFY(text.contains(QStringLiteral("watchtime (2 Zuschauer)")));
        QVERIFY(text.contains(QStringLiteral("testZaehler")));
        QVERIFY(text.contains(QStringLiteral("Ohne Twitch-ID (ins Archiv): 1")));
        for (const QString &secret :
             {QStringLiteral("zuschauer1"), QStringLiteral("zuschauer2"), QStringLiteral("zuschauer3"),
              QStringLiteral("zuschauer4"), QStringLiteral("zuschauer9"), QStringLiteral("10000000"),
              QStringLiteral("Hallo Testwert"), QStringLiteral("Testzitat"), QStringLiteral("Testgruppe"),
              QStringLiteral("testfarbe"), QStringLiteral("#123456"), QStringLiteral("1200"),
              QStringLiteral("Testeinstellungswert"), QStringLiteral("testpronomen")})
            QVERIFY2(!text.contains(secret, Qt::CaseInsensitive), qPrintable(secret));
    }
}

void TestZuschauerImport::dryRunWritesNothing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    VariablesData vars;
    vars.global.insert(QStringLiteral("nurImStudio"), {QJsonValue(1), VariableOrigin::Studio, kNow});
    writeBytes(dir.filePath(QString::fromLatin1(kVariablesFileName)), serializeVariables(vars));
    writeBytes(dir.filePath(QString::fromLatin1(kActionsFileName)), serializeActions(ActionsConfig{}));
    const QMap<QString, QByteArray> before = snapshot(dir.path());

    ImportState state;
    QString error;
    QVERIFY2(loadImportState(dir.path(), &state, &error), qPrintable(error));
    QCOMPARE(state.variables, vars);
    QVERIFY(state.actions.has_value());
    const ImportResult result = planViewerImport(readExport(), state, {false, kNow});
    QVERIFY(result.state != state);
    QVERIFY(!importReportText(result.report, false).isEmpty());
    QCOMPARE(snapshot(dir.path()), before);
}

void TestZuschauerImport::invalidFileStops()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    writeBytes(dir.filePath(QString::fromLatin1(kViewersFileName)), "{\"format_version\": 1}");
    ImportState state;
    QString error;
    QVERIFY(!loadImportState(dir.path(), &state, &error));
    QVERIFY(error.startsWith(QString::fromLatin1(kViewersFileName)));
}

void TestZuschauerImport::writesWithBackup()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString data = dir.filePath(QStringLiteral("daten"));
    QVERIFY(QDir().mkpath(data));
    VariablesData vars;
    vars.global.insert(QStringLiteral("nurImStudio"), {QJsonValue(1), VariableOrigin::Studio, kNow});
    const QByteArray oldVariables = serializeVariables(vars);
    const QByteArray oldActions = serializeActions(ActionsConfig{});
    writeBytes(QDir(data).filePath(QString::fromLatin1(kVariablesFileName)), oldVariables);
    writeBytes(QDir(data).filePath(QString::fromLatin1(kActionsFileName)), oldActions);

    ImportState before;
    QString error;
    QVERIFY2(loadImportState(data, &before, &error), qPrintable(error));
    const ImportResult result = planViewerImport(readExport(), before, {false, kNow});

    QCOMPARE(importBackupDirName(kNow), QStringLiteral("sicherung-20261115-120000"));
    const QString backup = dir.filePath(QStringLiteral("import/") + importBackupDirName(kNow));
    QStringList written;
    QVERIFY2(writeViewerImport(data, backup, before, result.state, &written, &error), qPrintable(error));
    QCOMPARE(written, QStringList({QString::fromLatin1(kVariablesFileName), QString::fromLatin1(kViewersFileName),
                                   QString::fromLatin1(kQuotesFileName), QString::fromLatin1(kArchiveFileName),
                                   QString::fromLatin1(kActionsFileName)}));
    // Gesichert sind die bisherigen Fassungen der betroffenen Dateien, die es schon gab
    QCOMPARE(snapshot(backup), (QMap<QString, QByteArray>{{QString::fromLatin1(kActionsFileName), oldActions},
                                                          {QString::fromLatin1(kVariablesFileName), oldVariables}}));

    ImportState after;
    QVERIFY2(loadImportState(data, &after, &error), qPrintable(error));
    QCOMPARE(after, result.state);
    QCOMPARE(after.variables.global.value(QStringLiteral("nurImStudio")).value.toInt(), 1);

    // Nichts geändert: nichts geschrieben, keine Sicherung
    const QString backup2 = dir.filePath(QStringLiteral("import/sicherung-2"));
    QVERIFY(writeViewerImport(data, backup2, after, after, &written, &error));
    QVERIFY(written.isEmpty());
    QVERIFY(!QFileInfo::exists(backup2));

    // Sicherung besteht schon: Abbruch vor dem Schreiben
    const QMap<QString, QByteArray> files = snapshot(data);
    ImportState changed = after;
    changed.variables.global.insert(QStringLiteral("neu"), {QJsonValue(2), VariableOrigin::Studio, kNow});
    QVERIFY(!writeViewerImport(data, backup, after, changed, &written, &error));
    QVERIFY(written.isEmpty());
    QCOMPARE(snapshot(data), files);
}

void TestZuschauerImport::secondImportKeepsStudioChanges()
{
    const StreamerbotExport source = readExport();
    ImportState state = planViewerImport(source, startState(), {false, kNow}).state;

    // Im Studio seither: watchtime von Zuschauer 1 geändert, eine Variable nur im Studio; ein Wert aus
    // einem früheren Import weicht ab (Herkunft streamerbot)
    VariableStore store;
    store.setData(state.variables);
    QVERIFY(store.set({VariableScope::User, kUser1, QStringLiteral("watchtime"), true}, QJsonValue(1500), kLater)
                .isEmpty());
    QVERIFY(store.set({VariableScope::Global, QString(), QStringLiteral("nurImStudio"), true}, QJsonValue(1), kLater)
                .isEmpty());
    state.variables = store.data();
    state.variables.users[kUser2][QStringLiteral("watchtime")].value = QJsonValue(299);

    const ImportResult second = planViewerImport(source, state, {false, kLater});
    const VariablesData &vars = second.state.variables;
    QCOMPARE(vars.users[kUser1].value(QStringLiteral("watchtime")),
             (VariableValue{QJsonValue(1500), VariableOrigin::Studio, kLater}));
    QCOMPARE(vars.users[kUser2].value(QStringLiteral("watchtime")).value.toInt(), 300);
    QCOMPARE(vars.global.value(QStringLiteral("nurImStudio")).value.toInt(), 1);
    QCOMPARE(second.report.userVariables, (ImportCount{0, 1, 3, 1, 0}));
    QCOMPARE(second.report.globalVariables, (ImportCount{0, 0, 3, 0, 0}));
    QCOMPARE(second.report.commandCounters, (ImportCount{0, 0, 4, 0, 0}));
    QCOMPARE(second.report.viewers, (ImportCount{0, 0, 4, 0, 0}));
    QCOMPARE(second.report.quotes, (ImportCount{0, 0, 2, 0, 0}));
    QCOMPARE(second.report.groups, (ImportCount{0, 0, 1, 0, 0}));
    QCOMPARE(second.report.keptVariableNames, (QMap<QString, int>{{QStringLiteral("watchtime"), 1}}));
    QVERIFY(importReportText(second.report, false).contains(QStringLiteral("--ueberschreiben")));

    // --ueberschreiben: auch der im Studio geänderte Wert kommt aus Streamer.bot
    const ImportResult forced = planViewerImport(source, state, {true, kLater});
    QCOMPARE(forced.state.variables.users[kUser1].value(QStringLiteral("watchtime")),
             fromStreamerbot(1200, utc(1, 18)));
    QCOMPARE(forced.state.variables.global.value(QStringLiteral("nurImStudio")).value.toInt(), 1);
    QCOMPARE(forced.report.userVariables, (ImportCount{0, 1, 3, 0, 1}));
    QCOMPARE(forced.report.overwrittenVariableNames, (QMap<QString, int>{{QStringLiteral("watchtime"), 1}}));
}

QTEST_GUILESS_MAIN(TestZuschauerImport)
#include "tst_zuschauerimport.moc"
