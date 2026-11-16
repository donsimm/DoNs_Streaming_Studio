// Prüft die Ansichtslogik «Chat-Bot» › «Live» ohne Fenster (P14 e2-5, Plan 3.5, Regel 9).

#include "chatbotlive.h"

#include <QTest>

using namespace dss;
using actions::RunRecord;
using actions::RunState;

namespace {

RunRecord record(RunState state, const QString &message = QString())
{
    RunRecord r;
    r.runId = QUuid::createUuid();
    r.actionId = QUuid::createUuid();
    r.actionName = QStringLiteral("Begrüssung");
    r.triggerType = QStringLiteral("command");
    r.queueName = QStringLiteral("Standard");
    r.state = state;
    r.message = message;
    r.queuedAt = QDateTime(QDate(2026, 11, 16), QTime(20, 5, 9));
    return r;
}

} // namespace

class TestChatBotLive : public QObject
{
    Q_OBJECT

private slots:
    void resultTexts_data()
    {
        QTest::addColumn<int>("state");
        QTest::addColumn<QString>("message");
        QTest::addColumn<QString>("result");
        QTest::addColumn<QString>("color");
        QTest::addColumn<bool>("cancellable");

        QTest::newRow("wartet") << int(RunState::Waiting) << QString()
                                << QStringLiteral("wartet: Warteschlange Standard") << QString() << true;
        QTest::newRow("läuft") << int(RunState::Running) << QString() << QStringLiteral("läuft") << QString()
                               << true;
        QTest::newRow("beendet") << int(RunState::Break) << QString() << QStringLiteral("beendet")
                                 << QStringLiteral("ok") << false;
        QTest::newRow("Fehler ohne Meldung") << int(RunState::Error) << QString() << QStringLiteral("Fehler")
                                             << QStringLiteral("error") << false;
        QTest::newRow("abgewiesen") << int(RunState::Rejected) << QStringLiteral("Chat-Bot aus")
                                    << QStringLiteral("abgewiesen: Chat-Bot aus") << QStringLiteral("warning")
                                    << false;
        QTest::newRow("stumm") << int(RunState::Muted) << QStringLiteral("stumm: Version im Einsatz läuft")
                               << QStringLiteral("stumm") << QStringLiteral("warning") << false;
        QTest::newRow("verworfen") << int(RunState::Dropped) << QStringLiteral("Warteschlange voll")
                                   << QStringLiteral("verworfen: Warteschlange voll") << QStringLiteral("error")
                                   << false;
        QTest::newRow("Grenze") << int(RunState::Limit) << QStringLiteral("mehr als 2000 Schritte")
                                << QStringLiteral("Grenze: mehr als 2000 Schritte") << QStringLiteral("error")
                                << false;
        QTest::newRow("abgebrochen mit Grund")
            << int(RunState::Cancelled) << QStringLiteral("Chat-Bot aus") << QStringLiteral("abgebrochen: Chat-Bot aus")
            << QStringLiteral("warning") << false;
        // Ohne Grund setzt der Ablauf «abgebrochen» als Meldung
        QTest::newRow("abgebrochen ohne Grund") << int(RunState::Cancelled) << QStringLiteral("abgebrochen")
                                                << QStringLiteral("abgebrochen") << QStringLiteral("warning")
                                                << false;
        QTest::newRow("abgebrochen leer") << int(RunState::Cancelled) << QString()
                                          << QStringLiteral("abgebrochen") << QStringLiteral("warning") << false;
    }

    void resultTexts()
    {
        QFETCH(int, state);
        QFETCH(QString, message);
        QFETCH(QString, result);
        QFETCH(QString, color);
        QFETCH(bool, cancellable);
        const RunRecord r = record(RunState(state), message);
        const RunRow row = runRow(r);
        QCOMPARE(row.result, result);
        QCOMPARE(row.state, color);
        QCOMPARE(row.cancellable, cancellable);
        QCOMPARE(row.isError, color == QLatin1String("error"));
        QCOMPARE(row.runId, r.runId);
        QCOMPARE(row.actionId, r.actionId);
        QCOMPARE(row.action, QStringLiteral("Begrüssung"));
        QCOMPARE(row.trigger, QStringLiteral("Befehl"));
        QCOMPARE(row.time, r.queuedAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")));
    }

    void runningSteps()
    {
        RunRecord r = record(RunState::Running);
        r.stepIndex = 2;
        r.stepCount = 5;
        QCOMPARE(runRow(r).result, QStringLiteral("läuft, Schritt 2 von 5"));
    }

    void waitingQueueName()
    {
        RunRecord r = record(RunState::Waiting);
        r.queueName = QStringLiteral("Sounds");
        QCOMPARE(runRow(r).result, QStringLiteral("wartet: Warteschlange Sounds"));
        r.queueName.clear();
        QCOMPARE(runRow(r).result, QStringLiteral("wartet: Warteschlange Standard"));
    }

    void okDuration()
    {
        RunRecord r = record(RunState::Ok);
        QCOMPARE(runRow(r).result, QStringLiteral("fertig"));
        r.startedAt = r.queuedAt;
        r.finishedAt = r.queuedAt.addMSecs(1500);
        QCOMPARE(runRow(r).result, QStringLiteral("fertig (1.5 s)"));
        QCOMPARE(runRow(r).state, QStringLiteral("ok"));
        r.finishedAt = r.queuedAt.addMSecs(250);
        QCOMPARE(runRow(r).result, QStringLiteral("fertig (250 ms)"));
    }

    void errorFromStepLog()
    {
        RunRecord r = record(RunState::Error);
        actions::StepLogEntry ok;
        ok.message = QStringLiteral("gesendet");
        actions::StepLogEntry first;
        first.result = actions::StepLogEntry::Result::Error;
        first.message = QStringLiteral("Text ist leer");
        actions::StepLogEntry second = first;
        second.message = QStringLiteral("kein Name");
        r.log = {ok, first, second};
        QCOMPARE(runRow(r).result, QStringLiteral("Fehler: Text ist leer"));
        r.message = QStringLiteral("Meldung zum Lauf");
        QCOMPARE(runRow(r).result, QStringLiteral("Fehler: Meldung zum Lauf"));
    }

    void dryRunPrefix()
    {
        RunRecord r = record(RunState::Break);
        r.dryRun = true;
        QCOMPARE(runRow(r).result, QStringLiteral("Probelauf · beendet"));
        r.state = RunState::Running;
        r.stepIndex = 1;
        r.stepCount = 3;
        QCOMPARE(runRow(r).result, QStringLiteral("Probelauf · läuft, Schritt 1 von 3"));
    }

    void triggerLabels()
    {
        QCOMPARE(runTriggerText(QStringLiteral("test")), QStringLiteral("Test"));
        QCOMPARE(runTriggerText(QStringLiteral("run_action")), QStringLiteral("Aktion ausführen"));
        QCOMPARE(runTriggerText(QStringLiteral("unbekannt")), QStringLiteral("unbekannt"));
    }

    void missingTime()
    {
        RunRecord r = record(RunState::Rejected);
        r.queuedAt = QDateTime();
        QCOMPARE(runRow(r).time, QStringLiteral("–"));
        r.finishedAt = QDateTime(QDate(2026, 11, 16), QTime(21, 0, 0));
        QCOMPARE(runRow(r).time, r.finishedAt.toLocalTime().toString(QStringLiteral("HH:mm:ss")));
    }

    void replaceInsteadOfAppend()
    {
        RunRows rows;
        RunRecord r = record(RunState::Waiting);
        QCOMPARE(rows.apply(r), (RunRows::Change{0, true, 0}));
        r.state = RunState::Running;
        r.stepIndex = 1;
        r.stepCount = 2;
        QCOMPARE(rows.apply(r), (RunRows::Change{0, false, 0}));
        const RunRecord other = record(RunState::Waiting);
        QCOMPARE(rows.apply(other), (RunRows::Change{1, true, 0}));
        r.state = RunState::Ok;
        QCOMPARE(rows.apply(r), (RunRows::Change{0, false, 0}));
        QCOMPARE(rows.rows().size(), 2);
        QCOMPARE(rows.rows().at(0).result, QStringLiteral("fertig"));
        QVERIFY(!rows.rows().at(0).cancellable);
        QVERIFY(rows.row(other.runId));
        QCOMPARE(rows.row(other.runId)->result, QStringLiteral("wartet: Warteschlange Standard"));
        QVERIFY(!rows.row(QUuid::createUuid()));
    }

    void limit()
    {
        RunRows rows;
        QList<RunRecord> records;
        for (int i = 0; i < RunRows::kMaxRows + 5; ++i)
            records.append(record(RunState::Ok));
        rows.reset(records);
        QCOMPARE(rows.rows().size(), RunRows::kMaxRows);
        QCOMPARE(rows.rows().first().runId, records.at(5).runId);
        QCOMPARE(rows.rows().last().runId, records.last().runId);

        const RunRecord next = record(RunState::Waiting);
        QCOMPARE(rows.apply(next), (RunRows::Change{RunRows::kMaxRows - 1, true, 1}));
        QCOMPARE(rows.rows().size(), RunRows::kMaxRows);
        QCOMPARE(rows.rows().first().runId, records.at(6).runId);
        QCOMPARE(rows.rows().last().runId, next.runId);
        // Ersetzen entfernt nichts
        RunRecord changed = next;
        changed.state = RunState::Running;
        QCOMPARE(rows.apply(changed), (RunRows::Change{RunRows::kMaxRows - 1, false, 0}));

        // reset ersetzt die ganze Liste
        rows.reset({record(RunState::Ok)});
        QCOMPARE(rows.rows().size(), 1);
    }

    void onlyErrors()
    {
        RunRows rows;
        const RunRecord ok = record(RunState::Ok);
        const RunRecord failed = record(RunState::Error);
        const RunRecord cancelled = record(RunState::Cancelled);
        RunRecord running = record(RunState::Running);
        rows.reset({ok, failed, cancelled, running});
        QCOMPARE(rows.visibleRows().size(), 4);

        rows.setOnlyErrors(true);
        QVERIFY(rows.onlyErrors());
        QCOMPARE(rows.visibleRows().size(), 1);
        QCOMPARE(rows.visibleRows().first().runId, failed.runId);

        // Nicht sichtbare Änderungen und neue Zeilen ohne Fehler: sichtbar nichts
        running.stepIndex = 1;
        running.stepCount = 2;
        QCOMPARE(rows.apply(running), (RunRows::Change{-1, false, 0}));
        QCOMPARE(rows.apply(record(RunState::Waiting)), (RunRows::Change{-1, false, 0}));
        // Neuer Fehler: am Ende angehängt
        const RunRecord limit = record(RunState::Limit, QStringLiteral("Lauf dauerte länger als 15 min"));
        QCOMPARE(rows.apply(limit), (RunRows::Change{1, true, 0}));
        // Bestehender Lauf wird zum Fehler: an seiner Stelle eingefügt
        running.state = RunState::Dropped;
        QCOMPARE(rows.apply(running), (RunRows::Change{1, true, 0}));
        QCOMPARE(rows.visibleRows().size(), 3);
        QCOMPARE(rows.visibleRows().at(0).runId, failed.runId);
        QCOMPARE(rows.visibleRows().at(1).runId, running.runId);
        QCOMPARE(rows.visibleRows().at(2).runId, limit.runId);

        rows.setOnlyErrors(false);
        QCOMPARE(rows.visibleRows().size(), 6);
    }

    void onlyErrorsRemovedFront()
    {
        RunRows rows;
        QList<RunRecord> records{record(RunState::Error)};
        for (int i = 1; i < RunRows::kMaxRows; ++i)
            records.append(record(RunState::Ok));
        rows.reset(records);
        rows.setOnlyErrors(true);
        // Vorne fällt eine sichtbare Fehlerzeile weg
        QCOMPARE(rows.apply(record(RunState::Ok)), (RunRows::Change{-1, false, 1}));
        QVERIFY(rows.visibleRows().isEmpty());
        // Vorne fällt eine nicht sichtbare Zeile weg
        QCOMPARE(rows.apply(record(RunState::Error)), (RunRows::Change{0, true, 0}));
    }
};

QTEST_GUILESS_MAIN(TestChatBotLive)
#include "tst_chatbotlive.moc"
