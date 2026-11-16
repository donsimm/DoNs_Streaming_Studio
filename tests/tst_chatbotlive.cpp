// Prüft die Ansichtslogik «Chat-Bot» › «Live» ohne Fenster (P14 e2-5, Plan 3.5, Regel 9).

#include "chatbotlive.h"

#include <QTest>
#include <QTimeZone>

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

    // --- Bereich «Chat» (e2-6) ---

    void chatMessageRow()
    {
        const events::Event e = chatEvent("m1", {{"is_bot_account", true}, {"is_broadcaster", true}, {"is_moderator", true}});
        const std::optional<ChatRow> row = chatRow(e);
        QVERIFY(row);
        QCOMPARE(row->time, rowTimeText(kTime));
        QCOMPARE(row->badges, (QStringList{"Bot", "Kanal", "Mod"}));
        QCOMPARE(row->name, QStringLiteral("Zuschauer42"));
        QCOMPARE(row->color, QStringLiteral("#1E90FF"));
        QCOMPARE(row->text, QStringLiteral("Hallo zusammen"));
        QCOMPARE(row->messageId, QStringLiteral("m1"));
        QVERIFY(!row->isEvent);
        QVERIFY(chatRow(chatEvent("m2"))->badges.isEmpty());
        QCOMPARE(chatRow(chatEvent("m3", {{"is_moderator", true}}))->badges, QStringList{"Mod"});
    }

    void sentAndDroppedRows()
    {
        TwitchStatusInfo twitch;
        twitch.bot.login = QStringLiteral("testbot");
        twitch.channel.login = QStringLiteral("testkanal");
        const events::Event bot = event("twitch.chat_sent", {{"message_id", "s1"}, {"account", "bot"}, {"text", "Hallo"}});
        QCOMPARE(*chatRow(bot, twitch),
                 (ChatRow{rowTimeText(kTime), {"Bot"}, "testbot", QString(), "Hallo", false, "s1"}));
        const events::Event channel = event("twitch.chat_sent", {{"message_id", "s2"}, {"account", "broadcaster"}, {"text", "Hi"}});
        QCOMPARE(chatRow(channel, twitch)->badges, QStringList{"Kanal"});
        QCOMPARE(chatRow(channel, twitch)->name, QStringLiteral("testkanal"));
        QCOMPARE(chatRow(channel)->name, QStringLiteral("—"));
        const std::optional<ChatRow> dropped =
            chatRow(event("twitch.chat_dropped", {{"account", "bot"}, {"text", "Hallo"}, {"code", "expired"},
                                                  {"message", "zu alt"}}));
        QVERIFY(dropped->isEvent);
        QCOMPARE(dropped->text, QStringLiteral("Nicht gesendet: zu alt"));
    }

    void greyRows()
    {
        const std::optional<ChatRow> follow = chatRow(event("twitch.follow", {{"user_name", "Zuschauer42"}}));
        QCOMPARE(*follow, (ChatRow{rowTimeText(kTime), {}, QString(), QString(), "Follow: Zuschauer42", true, QString()}));
        QCOMPARE(chatRow(event("twitch.raid", {{"from_user_name", "Zuschauer7"}, {"viewers", 23}}))->text,
                 QStringLiteral("Raid: Zuschauer7, 23 Zuschauer"));
        QCOMPARE(chatRow(event("twitch.stream_online", {}))->text, QStringLiteral("Stream online"));

        RunRecord run = record(RunState::Running);
        QCOMPARE(chatRow(event("actions.run_started", runEventData(run)))->text,
                 QStringLiteral("Aktion «Begrüssung» ausgelöst"));
        run.dryRun = true;
        const std::optional<ChatRow> dry = chatRow(event("actions.run_started", runEventData(run)));
        QVERIFY(dry->isEvent);
        QCOMPARE(dry->text, QStringLiteral("Probelauf · Aktion «Begrüssung» ausgelöst"));

        // Ohne Zeile im Chat
        QVERIFY(!chatRow(event("actions.run_changed", runEventData(run))));
        QVERIFY(!chatRow(event("actions.run_finished", runEventData(run))));
        QVERIFY(!chatRow(event("twitch.gift_sub", {{"recipient_user_name", "Zuschauer7"}})));
        QVERIFY(!chatRow(event("obs.scene_changed", {{"scene_name", "Live"}})));
    }

    void chatLimit()
    {
        QList<events::Event> history;
        for (int i = 0; i < 250; ++i)
            history.append(chatEvent(QString::number(i)));
        ChatRows rows;
        rows.reset(history);
        QCOMPARE(rows.rows().size(), ChatRows::kMaxRows);
        QCOMPARE(rows.rows().first().messageId, QStringLiteral("50"));
        QCOMPARE(rows.add(chatEvent("neu")), (RowsAppend{true, 1}));
        QCOMPARE(rows.rows().first().messageId, QStringLiteral("51"));
        QCOMPARE(rows.rows().last().messageId, QStringLiteral("neu"));
        QCOMPARE(rows.add(event("actions.run_changed", runEventData(record(RunState::Ok)))), RowsAppend{});
        rows.reset({});
        QCOMPARE(rows.add(chatEvent("a")), (RowsAppend{true, 0}));
    }

    void noDuplicates()
    {
        // Eigene Nachricht: erst chat_sent, dann das Echo über EventSub (falls es kommt)
        const events::Event sent = event("twitch.chat_sent", {{"message_id", "s1"}, {"account", "bot"}, {"text", "Hallo"}});
        ChatRows rows;
        QCOMPARE(rows.add(sent), (RowsAppend{true, 0}));
        QCOMPARE(rows.add(chatEvent("s1", {{"is_bot_account", true}})), RowsAppend{});
        // Umgekehrte Reihenfolge
        QCOMPARE(rows.add(chatEvent("s2")), (RowsAppend{true, 0}));
        QCOMPARE(rows.add(event("twitch.chat_sent", {{"message_id", "s2"}, {"account", "bot"}, {"text", "x"}})),
                 RowsAppend{});
        // Graue Zeilen haben keine Kennung und kommen jedes Mal
        QCOMPARE(rows.add(event("twitch.follow", {{"user_name", "A"}})), (RowsAppend{true, 0}));
        QCOMPARE(rows.add(event("twitch.follow", {{"user_name", "A"}})), (RowsAppend{true, 0}));
        QCOMPARE(rows.rows().size(), 4);
    }

    // --- Senden (e2-6, Plan 4.1) ---

    void sendCounter()
    {
        QCOMPARE(chatSendView(QString(), "bot", std::nullopt).counter, QStringLiteral("0 / 500"));
        const ChatSendView empty = chatSendView(QStringLiteral("   "), "bot", status());
        QVERIFY(!empty.sendEnabled);
        QVERIFY(empty.problem.isEmpty());
        // Emoji: zwei UTF-16-Einheiten je Zeichen (wie der Kern zählt)
        const QString emoji = QString::fromUtf8("\xF0\x9F\x98\x80");
        QCOMPARE(emoji.size(), 2);
        QCOMPARE(chatSendView(QStringLiteral("Hallo ") + emoji, "bot", status()).counter, QStringLiteral("8 / 500"));
        const QString full = QString(498, QLatin1Char('a')) + emoji;
        ChatSendView v = chatSendView(full, "broadcaster", status());
        QCOMPARE(v.counter, QStringLiteral("500 / 500"));
        QVERIFY(!v.tooLong);
        QVERIFY(v.sendEnabled);
        v = chatSendView(full + QLatin1Char('a'), "broadcaster", status());
        QCOMPARE(v.counter, QStringLiteral("501 / 500"));
        QVERIFY(v.tooLong);
        QVERIFY(!v.sendEnabled);
        QCOMPARE(v.problem, QStringLiteral("Die Nachricht ist länger als 500 Zeichen"));
    }

    void sendRejections_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("account");
        QTest::addColumn<QString>("state");
        QTest::addColumn<QString>("problem");
        const QString ok = QStringLiteral("Hallo");
        QTest::newRow("leer") << QString() << "bot" << "on" << "Die Nachricht ist leer";
        QTest::newRow("nur Leerzeichen") << "  " << "bot" << "on" << "Die Nachricht ist leer";
        QTest::newRow("zu lang") << QString(501, QLatin1Char('a')) << "bot" << "on"
                                 << "Die Nachricht ist länger als 500 Zeichen";
        QTest::newRow("Schrägstrich") << "/ban x" << "bot" << "on" << "Twitch-Befehle sind nicht möglich";
        QTest::newRow("Punkt") << ".me x" << "bot" << "on" << "Twitch-Befehle sind nicht möglich";
        QTest::newRow("Schrägstrich nach Leerzeichen") << "  /ban x" << "bot" << "on"
                                                       << "Twitch-Befehle sind nicht möglich";
        QTest::newRow("Punkt nach Leerzeichen") << " .me x" << "broadcaster" << "on"
                                                << "Twitch-Befehle sind nicht möglich";
        QTest::newRow("Konto") << ok << "kanal" << "on" << "account muss bot oder broadcaster sein";
        QTest::newRow("stumm") << ok << "broadcaster" << "muted" << "stumm: Version im Einsatz läuft";
        QTest::newRow("nicht verfügbar") << ok << "bot" << "unavailable" << "Aktionen sind abgeschaltet";
        QTest::newRow("Bot kann nicht senden") << ok << "bot" << "on" << "Bot nicht angemeldet";
        QTest::newRow("aus: Kanal sendet") << ok << "broadcaster" << "off" << "";
        QTest::newRow("Dateifehler: Kanal sendet") << ok << "broadcaster" << "file_error" << "";
        QTest::newRow("Punkt in der Mitte") << "Hallo." << "broadcaster" << "on" << "";
    }

    void sendRejections()
    {
        QFETCH(QString, text);
        QFETCH(QString, account);
        QFETCH(QString, state);
        QFETCH(QString, problem);
        const std::optional<ActionsStatus> s = status(state, state == QLatin1String("unavailable") ? "Aktionen sind abgeschaltet" : "");
        QCOMPARE(chatSendProblem(text, account, s), problem);
        const EditResult r = chatSendEdit(text, account, s);
        QCOMPARE(r.problem, problem);
        QCOMPARE(r.edits.isEmpty(), !problem.isEmpty());
        if (!r.edits.isEmpty()) {
            QCOMPARE(r.edits.first().type, QStringLiteral("chat_send"));
            QCOMPARE(r.edits.first().fields.value("text").toString(), text);
            QCOMPARE(r.edits.first().fields.value("account").toString(), account);
            QVERIFY(r.edits.first().fields.value("reply_to").isNull());
        }
        const ChatSendView v = chatSendView(text, account, s);
        QCOMPARE(v.sendEnabled, problem.isEmpty());
        // Leerer Text: ohne Meldung
        QCOMPARE(v.problem, text.trimmed().isEmpty() ? QString() : problem);
    }

    void sendAccounts()
    {
        // Ohne actions_status: beide wählbar, der Kern entscheidet
        ChatSendView v = chatSendView("Hallo", "bot", std::nullopt);
        QCOMPARE(v.accounts, (QList<ChatSendAccount>{{"bot", "als Bot", true, QString()},
                                                     {"broadcaster", "als Kanal", true, QString()}}));
        QVERIFY(v.sendEnabled);
        QCOMPARE(chatSendEdit("Hallo", "bot", std::nullopt).edits.size(), 1);
        // Bot kann nicht senden: «als Bot» ausgegraut mit Grund
        v = chatSendView("Hallo", "broadcaster", status());
        QCOMPARE(v.accounts.at(0), (ChatSendAccount{"bot", "als Bot", false, "Bot nicht angemeldet"}));
        QVERIFY(v.accounts.at(1).enabled);
        QVERIFY(v.sendEnabled);
        v = chatSendView("Hallo", "bot", status());
        QVERIFY(!v.sendEnabled);
        QCOMPARE(v.problem, QStringLiteral("Bot nicht angemeldet"));
        // Stumm: nicht senden, auch als Kanal
        v = chatSendView("Hallo", "broadcaster", status("muted"));
        QVERIFY(!v.sendEnabled);
        QCOMPARE(v.problem, QStringLiteral("stumm: Version im Einsatz läuft"));
    }

private:
    static inline const QDateTime kTime{QDate(2026, 11, 16), QTime(20, 5, 9), QTimeZone::UTC};

    static events::Event event(const char *type, const QJsonObject &data)
    {
        events::Event e;
        e.type = QString::fromLatin1(type);
        e.time = kTime;
        e.data = data;
        return e;
    }

    static events::Event chatEvent(const QString &messageId, const QJsonObject &extra = {})
    {
        QJsonObject data{{"message_id", messageId}, {"user_name", "Zuschauer42"}, {"user_login", "zuschauer42"},
                         {"text", "Hallo zusammen"}, {"color", "#1E90FF"}};
        for (auto it = extra.begin(); it != extra.end(); ++it)
            data.insert(it.key(), it.value());
        return event("twitch.chat_message", data);
    }

    // Bot nicht angemeldet, Kanal kann senden
    static ActionsStatus status(const QString &state = QStringLiteral("on"), const QString &message = QString())
    {
        ActionsStatus s;
        s.state = state;
        s.message = message;
        s.accounts = {{"bot", false, "Bot nicht angemeldet"}, {"broadcaster", true, QString()}};
        return s;
    }
};

QTEST_GUILESS_MAIN(TestChatBotLive)
#include "tst_chatbotlive.moc"
