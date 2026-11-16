// P14 b1: Ablauf der Aktionen (actionengine.h): Zustandsmaschine, Verschachtelung, Probelauf,
// Warteschlangen und Grenzen nach Plan 3.3. Ohne Qt Gui, ohne Netz; Uhr und Dienste nach aussen
// sind ersetzt (Plan 3.1), Daten erfunden (Plan 2.10, Entscheid 2026-11-15).

#include "actionengine.h"
#include "actionjson.h"
#include "aktionsuhr.h"
#include "steptypes.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace dss::actions;

namespace {

using FakeClock = FakeActionClock;

// Ersatz für alle Dienste nach aussen. sync: Rückruf im Aufruf selbst, sonst in pending.
class FakeServices : public ChatSender, public TwitchApi, public ObsControl, public SoundPlayer, public OverlayLive
{
public:
    void sendChat(const ChatRequest &request, StepDone done) override
    {
        chats.append(request);
        answer(std::move(done), {});
    }
    void userInfo(const QString &login, StepDone done) override
    {
        userInfos.append(login);
        answer(std::move(done), userInfoResult);
    }
    void shoutout(const QString &login, StepDone done) override
    {
        shoutouts.append(login);
        answer(std::move(done), {});
    }
    void announce(const AnnounceRequest &request, StepDone done) override
    {
        announces.append(request);
        answer(std::move(done), {});
    }
    void setSourceVisible(const ObsVisibilityRequest &request, StepDone done) override
    {
        obs.append(request);
        answer(std::move(done), {});
    }
    void play(const SoundRequest &request, StepDone done) override
    {
        sounds.append(request);
        answer(std::move(done), {});
    }
    void stopRun(const QUuid &runId) override { stoppedRuns.append(runId); }
    void apply(const OverlayRequest &request, StepDone done) override
    {
        overlays.append(request);
        answer(std::move(done), {});
    }

    ActionServices services() { return {this, this, this, this, this}; }
    QStringList chatTexts() const
    {
        QStringList t;
        for (const ChatRequest &c : chats)
            t.append(c.text);
        return t;
    }

    bool sync = true;
    StepResult userInfoResult;
    QList<StepDone> pending;
    QList<ChatRequest> chats;
    QStringList userInfos;
    QStringList shoutouts;
    QList<AnnounceRequest> announces;
    QList<ObsVisibilityRequest> obs;
    QList<SoundRequest> sounds;
    QList<QUuid> stoppedRuns;
    QList<OverlayRequest> overlays;

private:
    void answer(StepDone done, const StepResult &result)
    {
        if (sync)
            done(result);
        else
            pending.append([done, result](const StepResult &) { done(result); });
    }
};

Step mk(const char *type, const QJsonObject &fields = {}, const QList<Step> &children = {}, bool enabled = true)
{
    Step s = newStep(QString::fromLatin1(type));
    for (auto it = fields.begin(); it != fields.end(); ++it)
        s.fields.insert(it.key(), it.value());
    if (!children.isEmpty())
        s.steps = children;
    s.enabled = enabled;
    return s;
}

Step chat(const QString &text)
{
    return mk(kStepChatSend, {{"text", text}});
}

Step mkIf(const QString &input, const QString &operation, const QString &value, const QList<Step> &then,
          const QList<Step> &otherwise = {}, bool autoType = true)
{
    Step s = mk(kStepIf, {{"input", input}, {"operation", operation}, {"value", value}, {"auto_type", autoType}});
    s.steps[0].steps = then;
    s.steps[1].steps = otherwise;
    return s;
}

Step runAction(const QUuid &target, bool immediately = true)
{
    return mk(kStepRunAction, {{"action_id", uuidText(target)}, {"immediately", immediately}});
}

Step delay(qint64 ms)
{
    return mk(kStepDelay, {{"value_ms", double(ms)}});
}

Action act(const QString &name, const QList<Step> &steps)
{
    Action a;
    a.id = QUuid::createUuid();
    a.name = name;
    a.steps = steps;
    return a;
}

struct Env
{
    FakeClock clock;
    VariableStore variables;
    FakeServices services;
    ActionEngine engine{&clock, &variables};
    ActionsConfig config;

    Env() { engine.setServices(services.services()); }
    QUuid add(const Action &a)
    {
        config.actions.append(a);
        engine.setConfig(config);
        return a.id;
    }
    QUuid addQueue(const QString &name, bool blocking)
    {
        Queue q;
        q.id = QUuid::createUuid();
        q.name = name;
        q.blocking = blocking;
        config.queues.append(q);
        engine.setConfig(config);
        return q.id;
    }
    QUuid run(const QUuid &action, const Arguments &arguments = {}, bool dryRun = false)
    {
        return engine.enqueue({action, QStringLiteral("test"), QUuid(), arguments, dryRun});
    }
    RunRecord rec(const QUuid &run) const { return engine.record(run).value_or(RunRecord()); }
    RunState state(const QUuid &run) const { return rec(run).state; }
};

QStringList messages(const RunRecord &r, StepLogEntry::Result result)
{
    QStringList m;
    for (const StepLogEntry &e : r.log) {
        if (e.result == result)
            m.append(e.message);
    }
    return m;
}

} // namespace

class TestActionRun : public QObject
{
    Q_OBJECT

private slots:
    // Schritte nacheinander, Argumente aus Auslöser, Kern und den festen (Plan 2.5)
    void sequenceAndArguments()
    {
        Env env;
        env.engine.setBaseArguments([] {
            return Arguments{{QStringLiteral("broadcastUser"), QStringLiteral("Testkanal")},
                             {QStringLiteral("isLive"), true}};
        });
        const QUuid a = env.add(act(QStringLiteral("Hallo"),
                                    {mk(kStepComment, {{"text", "nur Notiz"}}),
                                     mk(kStepSetArgument, {{"name", "zahl"}, {"value", "41"}, {"auto_type", true}}),
                                     chat(QStringLiteral("%user% bei %BroadcastUser% in %actionname%, %zahl% %unbekannt%")),
                                     chat(QStringLiteral("%triggerType% %isLive% %date% %unixtime%"))}));
        const QUuid run = env.run(a, {{QStringLiteral("user"), QStringLiteral("Testzuschauer")}});
        const RunRecord r = env.rec(run);
        QCOMPARE(r.state, RunState::Ok);
        QCOMPARE(env.services.chatTexts().value(0),
                 QStringLiteral("Testzuschauer bei Testkanal in Hallo, 41 %unbekannt%"));
        const QDateTime start = env.clock.now();
        QCOMPARE(env.services.chatTexts().value(1),
                 QStringLiteral("test True %1 %2")
                     .arg(start.toLocalTime().toString(QStringLiteral("dd.MM.yyyy")))
                     .arg(start.toSecsSinceEpoch()));
        QCOMPARE(r.stepCount, 4);
        QCOMPARE(r.stepIndex, 4);
        QCOMPARE(r.stepsExecuted, 4);
        QCOMPARE(r.log.size(), 4);
        QCOMPARE(r.log.at(2).path, QList<int>{3});
        QVERIFY(r.startedAt.isValid() && r.finishedAt.isValid());
    }

    // Pause über die Uhr, nie geschlafen; zufällige Pause im Bereich (Plan 4.2)
    void delayUsesClock()
    {
        Env env;
        env.engine.setRandomSource([] { return 0.5; });
        Step randomDelay = mk(kStepDelay, {{"value_ms", 1000}, {"max_value_ms", 2000}, {"random", true}});
        const QUuid a = env.add(act(QStringLiteral("Pause"), {chat(QStringLiteral("eins")), delay(5000),
                                                              chat(QStringLiteral("zwei")), randomDelay,
                                                              chat(QStringLiteral("drei"))}));
        const QUuid run = env.run(a);
        QCOMPARE(env.state(run), RunState::Running);
        QCOMPARE(env.rec(run).stepIndex, 2);
        QCOMPARE(env.services.chatTexts(), QStringList{QStringLiteral("eins")});
        env.clock.advance(4999);
        QCOMPARE(env.services.chats.size(), 1);
        env.clock.advance(1);
        QCOMPARE(env.services.chatTexts().size(), 2);
        // 1000 + floor(0.5 * 1001) = 1500
        env.clock.advance(1499);
        QCOMPARE(env.state(run), RunState::Running);
        env.clock.advance(1);
        QCOMPARE(env.state(run), RunState::Ok);
        QCOMPARE(env.services.chatTexts().last(), QStringLiteral("drei"));
        QVERIFY(messages(env.rec(run), StepLogEntry::Result::Ok).contains(QStringLiteral("1500 ms")));
        QCOMPARE(env.rec(run).finishedAt, env.rec(run).startedAt.addMSecs(6500));
    }

    // Vergleiche des Schritts «Wenn» (Plan 2.4, 4.7)
    void ifOperations_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("operation");
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("autoType");
        QTest::addColumn<QString>("expected"); // dann, sonst, fehler

        QTest::newRow("gleich Zahl mit auto_type") << "%role%" << "equals" << "4.0" << true << "dann";
        QTest::newRow("gleich Zahl ohne auto_type") << "%role%" << "equals" << "4.0" << false << "sonst";
        QTest::newRow("gleich Text") << "%user%" << "equals" << "Testzuschauer" << true << "dann";
        QTest::newRow("ungleich") << "%user%" << "not_equals" << "testzuschauer" << true << "dann";
        QTest::newRow("gleich ohne Gross/klein") << "%user%" << "equals_ignore_case" << "TESTZUSCHAUER" << true << "dann";
        QTest::newRow("ungleich ohne Gross/klein") << "%user%" << "not_equals_ignore_case" << "TESTZUSCHAUER" << true << "sonst";
        QTest::newRow("enthält") << "%message%" << "contains" << "lo W" << true << "dann";
        QTest::newRow("enthält nicht") << "%message%" << "contains" << "lo w" << true << "sonst";
        QTest::newRow("kleiner") << "%role%" << "less_than" << "10" << true << "dann";
        QTest::newRow("grösser") << "%role%" << "greater_than" << "10" << true << "sonst";
        QTest::newRow("kleiner mit Text") << "%user%" << "less_than" << "10" << true << "fehler";
        QTest::newRow("leer: fehlt") << "%gibtsnicht%" << "is_empty" << "" << true << "dann";
        QTest::newRow("leer: leerer Text") << "%leer%" << "is_empty" << "" << true << "dann";
        QTest::newRow("leer: nein") << "%user%" << "is_empty" << "" << true << "sonst";
        QTest::newRow("existiert nicht") << "%gibtsnicht%" << "not_exists" << "" << true << "dann";
        QTest::newRow("existiert") << "%user%" << "not_exists" << "" << true << "sonst";
        QTest::newRow("existiert, Name ohne %") << "user" << "not_exists" << "" << true << "sonst";
        QTest::newRow("regex nicht ausgeführt") << "%user%" << "regex" << ".*" << true << "fehler";
    }
    void ifOperations()
    {
        QFETCH(QString, input);
        QFETCH(QString, operation);
        QFETCH(QString, value);
        QFETCH(bool, autoType);
        QFETCH(QString, expected);
        Env env;
        const QUuid a = env.add(act(QStringLiteral("Wenn"), {mkIf(input, operation, value, {chat(QStringLiteral("dann"))},
                                                                  {chat(QStringLiteral("sonst"))}, autoType),
                                                             chat(QStringLiteral("danach"))}));
        const QUuid run = env.run(a, {{QStringLiteral("role"), 4},
                                      {QStringLiteral("user"), QStringLiteral("Testzuschauer")},
                                      {QStringLiteral("message"), QStringLiteral("Hallo Welt")},
                                      {QStringLiteral("leer"), QString()}});
        const QStringList texts = env.services.chatTexts();
        if (expected == QLatin1String("fehler")) {
            // Fehler im Vergleich: Eintrag, kein Zweig, der Lauf geht weiter
            QCOMPARE(texts, QStringList{QStringLiteral("danach")});
            QCOMPARE(env.state(run), RunState::Error);
            QCOMPARE(messages(env.rec(run), StepLogEntry::Result::Error).size(), 1);
        } else {
            QCOMPARE(texts, (QStringList{expected, QStringLiteral("danach")}));
            QCOMPARE(env.state(run), RunState::Ok);
        }
    }

    // Verschachtelung, Pfade und «Schritt x von y» in der Reihenfolge der Anzeige
    void nestingAndPaths()
    {
        Env env;
        Step inner = mk(kStepGroup, {{"name", "innen"}}, {chat(QStringLiteral("tief"))});
        const QUuid a = env.add(act(QStringLiteral("Tief"), {mkIf(QStringLiteral("1"), QStringLiteral("equals"),
                                                                  QStringLiteral("1"), {inner}),
                                                             chat(QStringLiteral("ende"))}));
        const QUuid run = env.run(a);
        const RunRecord r = env.rec(run);
        QCOMPARE(r.state, RunState::Ok);
        QCOMPARE(env.services.chatTexts(), (QStringList{QStringLiteral("tief"), QStringLiteral("ende")}));
        // if(1) → if_true(1.1) → group(1.1.1) → chat(1.1.1.1); if_false(1.2); chat(2)
        QCOMPARE(r.stepCount, 6);
        QCOMPARE(r.stepIndex, 6);
        QList<QList<int>> paths;
        for (const StepLogEntry &e : r.log)
            paths.append(e.path);
        QCOMPARE(paths, (QList<QList<int>>{{1}, {1, 1, 1}, {1, 1, 1, 1}, {2}}));
    }

    // Abbruch beendet den ganzen Lauf ohne Fehler, auch aus «Aktion ausführen» sofort (Plan 2.4)
    void breakEndsRun()
    {
        Env env;
        const QUuid inner = env.add(act(QStringLiteral("Innen"), {chat(QStringLiteral("innen")), mk(kStepBreak),
                                                                  chat(QStringLiteral("nie"))}));
        const QUuid outer = env.add(act(QStringLiteral("Aussen"), {mk(kStepGroup, {}, {runAction(inner)}),
                                                                   chat(QStringLiteral("auch nie"))}));
        const QUuid run = env.run(outer);
        QCOMPARE(env.state(run), RunState::Break);
        QCOMPARE(env.services.chatTexts(), QStringList{QStringLiteral("innen")});
        QVERIFY(env.rec(run).message.isEmpty());
    }

    // Abgeschaltete Schritte und Gruppen samt Inhalt werden übersprungen (Plan 3.1)
    void disabledSkipped()
    {
        Env env;
        const QUuid a = env.add(act(QStringLiteral("Aus"),
                                    {mk(kStepChatSend, {{"text", "aus"}}, {}, false),
                                     mk(kStepGroup, {}, {chat(QStringLiteral("in Gruppe"))}, false),
                                     mkIf(QStringLiteral("1"), QStringLiteral("equals"), QStringLiteral("1"),
                                          {chat(QStringLiteral("dann"))}),
                                     chat(QStringLiteral("an"))}));
        Action &action = env.config.actions.last();
        action.steps[2].steps[0].enabled = false; // Zweig «Dann» aus
        env.engine.setConfig(env.config);
        const QUuid run = env.run(a);
        QCOMPARE(env.services.chatTexts(), QStringList{QStringLiteral("an")});
        const RunRecord r = env.rec(run);
        QCOMPARE(r.state, RunState::Ok);
        QCOMPARE(messages(r, StepLogEntry::Result::Skipped).size(), 2);
        QCOMPARE(r.stepsExecuted, 2);
    }

    // Fehler eines Schritts: Eintrag, der Lauf geht weiter; Ergebnis «error» (Plan 3.1)
    void stepErrorContinues()
    {
        Env env;
        env.engine.setServices({});
        Step script = mk(kStepScript, {{"language", "csharp"}, {"code", "x"}});
        script.enabled = true;
        const QUuid a = env.add(act(QStringLiteral("Fehler"),
                                    {chat(QStringLiteral("ohne Dienst")), mk(kStepPlaySound), script,
                                     mk(kStepSetArgument, {{"name", "weiter"}, {"value", "ja"}}),
                                     mk(kStepUserInfo, {{"login", "@%weiter%"}})}));
        const QUuid run = env.run(a);
        const RunRecord r = env.rec(run);
        QCOMPARE(r.state, RunState::Error);
        QCOMPARE(messages(r, StepLogEntry::Result::Error),
                 (QStringList{QStringLiteral("nicht verfügbar"), QStringLiteral("keine Datei gewählt"),
                              QStringLiteral("nicht verfügbar")}));
        QCOMPARE(messages(r, StepLogEntry::Result::Skipped), QStringList{QStringLiteral("Skript-Schritt nicht verfügbar")});
        QCOMPARE(r.stepsExecuted, 5);
    }

    // «zufällig einen» nach Gewicht, nur aktive Schritte (Plan 2.3, 2.4)
    void randomChoice_data()
    {
        QTest::addColumn<double>("random");
        QTest::addColumn<QString>("expected");
        // Gewichte 1, (aus), 3: 0..0.25 → a, sonst c
        QTest::newRow("0.0") << 0.0 << "a";
        QTest::newRow("0.24") << 0.24 << "a";
        QTest::newRow("0.26") << 0.26 << "c";
        QTest::newRow("0.999") << 0.999 << "c";
        QTest::newRow("1.0 begrenzt") << 1.0 << "c";
    }
    void randomChoice()
    {
        QFETCH(double, random);
        QFETCH(QString, expected);
        Env env;
        env.engine.setRandomSource([random] { return random; });
        Step a = chat(QStringLiteral("a"));
        a.weight = 1;
        Step b = chat(QStringLiteral("b"));
        b.weight = 100;
        b.enabled = false;
        Step c = chat(QStringLiteral("c"));
        c.weight = 3;
        Action action = act(QStringLiteral("Zufall"), {a, b, c});
        action.randomAction = true;
        env.run(env.add(action));
        QCOMPARE(env.services.chatTexts(), QStringList{expected});

        // Gruppe mit random, alle Gewichte 0: gleich wahrscheinlich
        Env env2;
        env2.engine.setRandomSource([random] { return random; });
        Step group = mk(kStepGroup, {{"random", true}},
                        {chat(QStringLiteral("x")), chat(QStringLiteral("y")), chat(QStringLiteral("z"))});
        env2.run(env2.add(act(QStringLiteral("Gruppe"), {group})));
        const QString picked = random < 1.0 / 3 ? QStringLiteral("x") : random < 2.0 / 3 ? QStringLiteral("y") : QStringLiteral("z");
        QCOMPARE(env2.services.chatTexts(), QStringList{picked});
    }

    // «Aktion ausführen» sofort: im selben Lauf, Argumente hin und zurück (Plan 4.10)
    void runActionImmediate()
    {
        Env env;
        const QUuid target = env.add(act(QStringLiteral("Ziel"), {chat(QStringLiteral("Ziel sieht %hin%")),
                                                                  mk(kStepSetArgument, {{"name", "zurueck"}, {"value", "ok"}})}));
        Action off = act(QStringLiteral("Aus"), {chat(QStringLiteral("nie"))});
        off.enabled = false;
        const QUuid disabled = env.add(off);
        const QUuid caller = env.add(act(QStringLiteral("Rufer"),
                                         {mk(kStepSetArgument, {{"name", "hin"}, {"value", "hallo"}}), runAction(target),
                                          runAction(disabled), runAction(QUuid::createUuid()),
                                          mk(kStepRunAction), chat(QStringLiteral("zurück: %zurueck%"))}));
        const QUuid run = env.run(caller);
        QCOMPARE(env.services.chatTexts(),
                 (QStringList{QStringLiteral("Ziel sieht hallo"), QStringLiteral("zurück: ok")}));
        const RunRecord r = env.rec(run);
        QCOMPARE(r.state, RunState::Error);
        QCOMPARE(messages(r, StepLogEntry::Result::Error),
                 (QStringList{QStringLiteral("Aktion «Aus» ist aus"), QStringLiteral("Aktion fehlt"),
                              QStringLiteral("keine Aktion gewählt")}));
        // Schritte der Zielaktion tragen Tiefe 1, «Schritt x von y» bleibt beim Rufer
        QCOMPARE(r.log.at(2).depth, 1);
        QCOMPARE(r.stepCount, 6);
        QCOMPARE(env.engine.history().size(), 1);
    }

    // Tiefe von run_action sofort höchstens 8 (Plan 3.3)
    void immediateDepthLimit()
    {
        Env env;
        Action self = act(QStringLiteral("Schleife"), {});
        self.steps = {chat(QStringLiteral("%actionName%")), runAction(self.id)};
        const QUuid run = env.run(env.add(self));
        const RunRecord r = env.rec(run);
        QCOMPARE(r.state, RunState::Limit);
        QCOMPARE(r.message, QStringLiteral("Aktion ausführen: mehr als 8 Ebenen"));
        QCOMPARE(env.services.chats.size(), kMaxImmediateDepth + 1);
    }

    // «Aktion ausführen» eigenständig: eigener Lauf in der Warteschlange des Ziels mit einer
    // Kopie der Argumente
    void runActionQueued()
    {
        Env env;
        const QUuid queue = env.addQueue(QStringLiteral("Meldungen"), true);
        Action target = act(QStringLiteral("Ziel"), {chat(QStringLiteral("Ziel: %hin%")),
                                                      mk(kStepSetArgument, {{"name", "hin"}, {"value", "geändert"}})});
        target.queue = queue;
        const QUuid targetId = env.add(target);
        const QUuid caller = env.add(act(QStringLiteral("Rufer"), {mk(kStepSetArgument, {{"name", "hin"}, {"value", "a"}}),
                                                                   runAction(targetId, false), delay(10),
                                                                   chat(QStringLiteral("Rufer: %hin%"))}));
        QList<RunRecord> started;
        connect(&env.engine, &ActionEngine::runStarted, this, [&](const RunRecord &r) { started.append(r); });
        env.run(caller);
        QCOMPARE(started.size(), 2);
        QCOMPARE(started.at(1).triggerType, QStringLiteral("run_action"));
        QCOMPARE(started.at(1).queue, queue);
        QCOMPARE(started.at(1).queueName, QStringLiteral("Meldungen"));
        QCOMPARE(env.state(started.at(1).runId), RunState::Ok);
        env.clock.advance(10);
        QCOMPARE(env.services.chatTexts(), (QStringList{QStringLiteral("Ziel: a"), QStringLiteral("Rufer: a")}));
    }

    // Höchstens 2000 ausgeführte Schritte je Lauf (Plan 3.3)
    void stepLimit()
    {
        Env env;
        QList<Step> steps;
        for (int i = 0; i < kMaxStepsPerRun; ++i)
            steps.append(mk(kStepComment));
        const QUuid ok = env.run(env.add(act(QStringLiteral("2000"), steps)));
        QCOMPARE(env.state(ok), RunState::Ok);
        steps.append(chat(QStringLiteral("zu viel")));
        const QUuid tooMany = env.run(env.add(act(QStringLiteral("2001"), steps)));
        QCOMPARE(env.state(tooMany), RunState::Limit);
        QCOMPARE(env.rec(tooMany).message, QStringLiteral("mehr als 2000 Schritte"));
        QVERIFY(env.services.chats.isEmpty());
    }

    // Dauer eines Laufs höchstens 15 min; späte Rückrufe und Zeitgeber tun danach nichts (Plan 3.3)
    void durationLimit()
    {
        Env env;
        env.services.sync = false;
        const QUuid a = env.add(act(QStringLiteral("Lang"), {delay(kMaxDelayMs), chat(QStringLiteral("nie"))}));
        const QUuid b = env.add(act(QStringLiteral("Hängt"), {chat(QStringLiteral("wartet")), chat(QStringLiteral("nie"))}));
        const QUuid runA = env.run(a);
        const QUuid runB = env.run(b);
        env.clock.advance(kMaxRunDurationMs - 1);
        QCOMPARE(env.state(runA), RunState::Running);
        env.clock.advance(1);
        QCOMPARE(env.state(runA), RunState::Limit);
        QCOMPARE(env.state(runB), RunState::Limit);
        QCOMPARE(env.rec(runA).message, QStringLiteral("Lauf dauerte länger als 15 min"));
        QCOMPARE(env.clock.pendingTimers(), 0);
        QCOMPARE(env.services.pending.size(), 1);
        env.services.pending.takeFirst()({});
        env.clock.advance(kMaxDelayMs);
        QCOMPARE(env.services.chats.size(), 1);
        QCOMPARE(env.engine.runningCount(), 0);
    }

    // Blockierende Warteschlange: nacheinander in der Reihenfolge des Eintreffens; «Standard»
    // nicht blockierend (Plan 2.8)
    void blockingQueue()
    {
        Env env;
        const QUuid queue = env.addQueue(QStringLiteral("Alerts"), true);
        Action blocking = act(QStringLiteral("Alert"), {chat(QStringLiteral("Alert %n%")), delay(1000)});
        blocking.queue = queue;
        const QUuid alert = env.add(blocking);
        const QUuid free = env.add(act(QStringLiteral("Frei"), {chat(QStringLiteral("Frei %n%")), delay(1000)}));

        const QUuid a1 = env.run(alert, {{QStringLiteral("n"), 1}});
        const QUuid a2 = env.run(alert, {{QStringLiteral("n"), 2}});
        const QUuid a3 = env.run(alert, {{QStringLiteral("n"), 3}});
        env.run(free, {{QStringLiteral("n"), 1}});
        env.run(free, {{QStringLiteral("n"), 2}});
        QCOMPARE(env.state(a1), RunState::Running);
        QCOMPARE(env.state(a2), RunState::Waiting);
        QCOMPARE(env.engine.waitingRuns(queue), (QList<QUuid>{a2, a3}));
        QCOMPARE(env.engine.runningCount(), 3);
        env.clock.advance(1000);
        QCOMPARE(env.state(a1), RunState::Ok);
        QCOMPARE(env.state(a2), RunState::Running);
        env.clock.advance(2000);
        QCOMPARE(env.state(a3), RunState::Ok);
        QCOMPARE(env.services.chatTexts(),
                 (QStringList{QStringLiteral("Alert 1"), QStringLiteral("Frei 1"), QStringLiteral("Frei 2"),
                              QStringLiteral("Alert 2"), QStringLiteral("Alert 3")}));
        // Wartezeit steht im Eintrag: Beginn von Alert 3 nach 2 s
        QCOMPARE(env.rec(a3).startedAt, env.rec(a3).queuedAt.addMSecs(2000));
    }

    // Höchstens 50 wartende je Warteschlange, ältere zuerst verworfen (Plan 3.3)
    void queueOverflow()
    {
        Env env;
        const QUuid queue = env.addQueue(QStringLiteral("Alerts"), true);
        Action alert = act(QStringLiteral("Alert"), {chat(QStringLiteral("%n%"))});
        alert.queue = queue;
        const QUuid a = env.add(alert);
        env.engine.pauseQueue(queue);
        QList<QUuid> runs;
        for (int i = 0; i < kMaxWaitingPerQueue + 2; ++i)
            runs.append(env.run(a, {{QStringLiteral("n"), i}}));
        QCOMPARE(env.state(runs.at(0)), RunState::Dropped);
        QCOMPARE(env.state(runs.at(1)), RunState::Dropped);
        QCOMPARE(env.rec(runs.at(0)).message, QStringLiteral("Warteschlange «Alerts» voll (50 wartende)"));
        QCOMPARE(env.engine.waitingRuns(queue).size(), kMaxWaitingPerQueue);
        QCOMPARE(env.state(runs.at(2)), RunState::Waiting);
        env.engine.resumeQueue(queue);
        QCOMPARE(env.services.chats.size(), kMaxWaitingPerQueue);
        QCOMPARE(env.services.chatTexts().first(), QStringLiteral("2"));
        QCOMPARE(env.services.chatTexts().last(), QStringLiteral("51"));
    }

    // Angehaltene Warteschlange: nur always_run läuft; Leeren verwirft die wartenden (Plan 2.8)
    void pauseAlwaysRunAndClear()
    {
        Env env;
        const QUuid queue = env.addQueue(QStringLiteral("Meldungen"), false);
        Action normal = act(QStringLiteral("Normal"), {chat(QStringLiteral("normal"))});
        normal.queue = queue;
        Action always = act(QStringLiteral("Immer"), {chat(QStringLiteral("immer"))});
        always.queue = queue;
        always.alwaysRun = true;
        const QUuid n = env.add(normal);
        const QUuid al = env.add(always);
        env.engine.pauseQueue(queue);
        QVERIFY(env.engine.isQueuePaused(queue));
        const QUuid r1 = env.run(n);
        const QUuid r2 = env.run(al);
        const QUuid r3 = env.run(n);
        QCOMPARE(env.state(r1), RunState::Waiting);
        QCOMPARE(env.state(r2), RunState::Ok);
        QCOMPARE(env.engine.clearQueue(queue), 2);
        QCOMPARE(env.state(r3), RunState::Cancelled);
        QCOMPARE(env.rec(r3).message, QStringLiteral("Warteschlange geleert"));
        env.engine.resumeQueue(queue);
        QCOMPARE(env.services.chatTexts(), QStringList{QStringLiteral("immer")});
        // «Standard» lässt sich auch anhalten
        env.engine.pauseQueue(QUuid());
        const QUuid r4 = env.run(env.add(act(QStringLiteral("Std"), {chat(QStringLiteral("std"))})));
        QCOMPARE(env.state(r4), RunState::Waiting);
        env.engine.resumeQueue(QUuid());
        QCOMPARE(env.state(r4), RunState::Ok);
    }

    // Höchstens 32 gleichzeitige Läufe; weitere warten (Plan 3.3)
    void concurrentLimit()
    {
        Env env;
        const QUuid a = env.add(act(QStringLiteral("Lang"), {delay(1000), chat(QStringLiteral("%n%"))}));
        QList<QUuid> runs;
        for (int i = 0; i < kMaxRunningRuns + 1; ++i)
            runs.append(env.run(a, {{QStringLiteral("n"), i}}));
        QCOMPARE(env.engine.runningCount(), kMaxRunningRuns);
        QCOMPARE(env.state(runs.last()), RunState::Waiting);
        env.clock.advance(1000);
        QCOMPARE(env.state(runs.first()), RunState::Ok);
        QCOMPARE(env.state(runs.last()), RunState::Running);
        env.clock.advance(1000);
        QCOMPARE(env.state(runs.last()), RunState::Ok);
        QCOMPARE(env.services.chats.size(), kMaxRunningRuns + 1);
    }

    // Abbrechen: wartend und laufend; später Rückruf wird ignoriert (Plan 3.5 «Lauf abbrechen»)
    void cancel()
    {
        Env env;
        env.services.sync = false;
        const QUuid queue = env.addQueue(QStringLiteral("Alerts"), true);
        Action alert = act(QStringLiteral("Alert"), {chat(QStringLiteral("eins")), chat(QStringLiteral("zwei"))});
        alert.queue = queue;
        const QUuid a = env.add(alert);
        const QUuid r1 = env.run(a);
        const QUuid r2 = env.run(a);
        QVERIFY(env.engine.cancel(r2));
        QCOMPARE(env.state(r2), RunState::Cancelled);
        QVERIFY(!env.rec(r2).startedAt.isValid());
        QVERIFY(env.engine.cancel(r1, QStringLiteral("vom Nutzer")));
        QCOMPARE(env.rec(r1).message, QStringLiteral("vom Nutzer"));
        QVERIFY(!env.engine.cancel(r1));
        env.services.pending.takeFirst()({});
        QCOMPARE(env.services.chats.size(), 1);
        QCOMPARE(env.rec(r1).log.size(), 0);
        QCOMPARE(env.clock.pendingTimers(), 0);
    }

    // c4 (Entscheid Nutzer 2026-11-16): ein Abbruch beendet die Töne des Laufs (stopRun mit seiner
    // Kennung), mit und ohne wait; das normale Ende (Ok, Error) nicht. Abbruch sind «Abbruch»,
    // «Lauf abbrechen», Zeit- und Schrittgrenze.
    void abortStopsSounds()
    {
        Env env;
        const Step withWait = mk(kStepPlaySound, {{"file", "media/ton.mp3"}, {"wait", true}});
        const Step noWait = mk(kStepPlaySound, {{"file", "media/ton.mp3"}, {"wait", false}});

        // normales Ende, auch mit Fehler in einem Schritt: kein stopRun
        QUuid r = env.run(env.add(act(QStringLiteral("Normal"), {noWait})));
        QCOMPARE(env.state(r), RunState::Ok);
        QCOMPARE(env.services.sounds.last().runId, r);
        r = env.run(env.add(act(QStringLiteral("Fehler"), {noWait, chat(QString())})));
        QCOMPARE(env.state(r), RunState::Error);
        QVERIFY(env.services.stoppedRuns.isEmpty());

        // Schritt «Abbruch» nach einem Ton ohne wait
        r = env.run(env.add(act(QStringLiteral("Abbruch"), {noWait, mk(kStepBreak)})));
        QCOMPARE(env.state(r), RunState::Break);
        QCOMPARE(env.services.stoppedRuns, QList<QUuid>{r});

        // Abbruch ohne Ton: kein stopRun
        env.services.stoppedRuns.clear();
        r = env.run(env.add(act(QStringLiteral("Ohne Ton"), {chat(QStringLiteral("x")), mk(kStepBreak)})));
        QCOMPARE(env.state(r), RunState::Break);
        QVERIFY(env.services.stoppedRuns.isEmpty());

        // «Lauf abbrechen» während eines Tons mit wait; zwei Läufe, nur der abgebrochene
        env.services.sync = false;
        const QUuid a = env.add(act(QStringLiteral("Warten"), {withWait, chat(QStringLiteral("danach"))}));
        const QUuid r1 = env.run(a);
        const QUuid r2 = env.run(a);
        QCOMPARE(env.services.pending.size(), 2);
        QVERIFY(env.engine.cancel(r1));
        QCOMPARE(env.services.stoppedRuns, QList<QUuid>{r1});
        env.services.pending.takeFirst()({}); // spätes Ende des Tons: kein Lauf mehr
        env.services.sync = true;
        env.services.pending.takeFirst()({});
        QCOMPARE(env.state(r2), RunState::Ok);
        QCOMPARE(env.services.chatTexts().count(QStringLiteral("danach")), 1); // nur r2 geht weiter
        QCOMPARE(env.services.stoppedRuns, QList<QUuid>{r1});

        // Zeitgrenze (15 min) nach einem Ton ohne wait
        env.services.stoppedRuns.clear();
        r = env.run(env.add(act(QStringLiteral("Lang"), {noWait, delay(kMaxDelayMs), delay(kMaxDelayMs)})));
        env.clock.advance(kMaxRunDurationMs);
        QCOMPARE(env.state(r), RunState::Limit);
        QCOMPARE(env.services.stoppedRuns, QList<QUuid>{r});
    }

    // e2-1 (Entscheid Nutzer 2026-11-16): Aktion abgeschaltet oder gelöscht bricht ihre laufenden
    // Läufe ab und verwirft die wartenden, auch Probeläufe, wie «Lauf abbrechen»; andere Aktionen
    // bleiben. Hauptschalter aus bricht alle ab.
    void cancelAction()
    {
        Env env;
        env.services.sync = false;
        const QUuid queue = env.addQueue(QStringLiteral("Alerts"), true);
        Action alert = act(QStringLiteral("Alert"), {mk(kStepPlaySound, {{"file", "media/ton.mp3"}, {"wait", true}}),
                                                     chat(QStringLiteral("danach"))});
        alert.queue = queue;
        const QUuid a = env.add(alert);
        const QUuid b = env.add(act(QStringLiteral("Andere"), {chat(QStringLiteral("andere"))}));
        const QUuid running = env.run(a);
        const QUuid waiting = env.run(a);
        const QUuid probe = env.run(a, {}, true);
        const QUuid other = env.run(b);
        QCOMPARE(env.state(running), RunState::Running);
        QCOMPARE(env.state(waiting), RunState::Waiting);
        QCOMPARE(env.state(probe), RunState::Waiting);
        QCOMPARE(env.services.pending.size(), 2); // Ton von «Alert», Chat von «Andere»

        const QString message = QStringLiteral("abgebrochen: Aktion abgeschaltet");
        QCOMPARE(env.engine.cancelAction(a, message), 3);
        for (const QUuid &r : {running, waiting, probe}) {
            QCOMPARE(env.state(r), RunState::Cancelled);
            QCOMPARE(env.rec(r).message, message);
        }
        // wartende nie gestartet, Ton nur des laufenden gestoppt
        QVERIFY(!env.rec(waiting).startedAt.isValid());
        QVERIFY(!env.rec(probe).startedAt.isValid());
        QCOMPARE(env.services.stoppedRuns, QList<QUuid>{running});
        QCOMPARE(env.state(other), RunState::Running);
        QCOMPARE(env.engine.cancelAction(a, message), 0);

        // spätes Ende des Tons: kein Lauf mehr; «Andere» läuft weiter
        env.services.sync = true;
        env.services.pending.takeFirst()({});
        env.services.pending.takeFirst()({});
        QCOMPARE(env.state(other), RunState::Ok);
        QCOMPARE(env.services.chatTexts().count(QStringLiteral("danach")), 0);

        // Mehrere Aktionen in einem Durchgang, je eigene Meldung
        env.services.sync = false;
        const QUuid r1 = env.run(a);
        const QUuid r2 = env.run(b);
        QCOMPARE(env.engine.cancelActions({{a, QStringLiteral("eins")}, {b, QStringLiteral("zwei")}}), 2);
        QCOMPARE(env.rec(r1).message, QStringLiteral("eins"));
        QCOMPARE(env.rec(r2).message, QStringLiteral("zwei"));
        env.services.pending.clear();

        // Hauptschalter aus: alle Läufe, laufend und wartend
        const QUuid h1 = env.run(a);
        const QUuid h2 = env.run(a);
        const QUuid h3 = env.run(a, {}, true);
        const QUuid h4 = env.run(b);
        QCOMPARE(env.state(h2), RunState::Waiting);
        QCOMPARE(env.state(h4), RunState::Running);
        env.services.stoppedRuns.clear();
        QCOMPARE(env.engine.cancelAll(QStringLiteral("abgebrochen: Chat-Bot aus")), 4);
        for (const QUuid &r : {h1, h2, h3, h4}) {
            QCOMPARE(env.state(r), RunState::Cancelled);
            QCOMPARE(env.rec(r).message, QStringLiteral("abgebrochen: Chat-Bot aus"));
        }
        QVERIFY(!env.rec(h2).startedAt.isValid());
        QCOMPARE(env.services.stoppedRuns, QList<QUuid>{h1});
        QCOMPARE(env.engine.runningCount(), 0);
        QCOMPARE(env.clock.pendingTimers(), 0);
    }

    // Probelauf: nichts nach Twitch oder OBS; Benutzerinfo, Overlay, Ton echt; Variablen getrennt
    // (Plan 3.6)
    void dryRun()
    {
        Env env;
        env.variables.set({VariableScope::Global, QString(), QStringLiteral("zaehler"), true}, 5, env.clock.now());
        env.variables.clearChanged();
        env.services.userInfoResult.arguments = {{QStringLiteral("targetUser"), QStringLiteral("Ziel")}};
        Step overlay = mk(kStepOverlaySource, {{"overlay", uuidText(QUuid::createUuid())}, {"duration_ms", 0}});
        const QUuid a = env.add(act(QStringLiteral("Probe"),
                                    {mk(kStepUserInfo, {{"login", "ziel"}}), chat(QStringLiteral("Hallo %targetUser%")),
                                     mk(kStepTwitchShoutout, {{"login", "%targetUser%"}}),
                                     mk(kStepTwitchAnnounce, {{"text", "Ansage"}}),
                                     mk(kStepObsSourceVisible, {{"scene_name", "test 2"}, {"source_name", "Quelle"}}),
                                     mk(kStepPlaySound, {{"file", "media/ton.mp3"}}), overlay,
                                     mk(kStepGlobalSet, {{"name", "zaehler"}, {"operation", "increment"}, {"value", "1"}}),
                                     mk(kStepGlobalGet, {{"name", "zaehler"}, {"destination", "z"}}),
                                     mk(kStepSetArgument, {{"name", "kontrolle"}, {"value", "~zaehler~/%z%"}})}));
        int finished = 0;
        connect(&env.engine, &ActionEngine::runFinished, this, [&] { ++finished; });
        const QUuid run = env.run(a, {}, true);
        const RunRecord r = env.rec(run);
        QCOMPARE(r.state, RunState::Ok);
        QVERIFY(r.dryRun);
        QVERIFY(env.services.chats.isEmpty());
        QVERIFY(env.services.shoutouts.isEmpty());
        QVERIFY(env.services.announces.isEmpty());
        QVERIFY(env.services.obs.isEmpty());
        QCOMPARE(env.services.userInfos, QStringList{QStringLiteral("ziel")});
        QCOMPARE(env.services.sounds.size(), 1);
        QCOMPARE(env.services.overlays.size(), 1);
        QCOMPARE(messages(r, StepLogEntry::Result::DryRun),
                 (QStringList{QStringLiteral("Probelauf: würde senden als Bot: Hallo Ziel"),
                              QStringLiteral("Probelauf: würde Shoutout geben für Ziel"),
                              QStringLiteral("Probelauf: würde Ansage senden: Ansage"),
                              QStringLiteral("Probelauf: würde OBS-Quelle Quelle in test 2 schalten: on")}));
        // echte Variable unverändert, im Probelauf erhöht
        QCOMPARE(env.variables.value({VariableScope::Global, QString(), QStringLiteral("zaehler"), true}).value_or(QJsonValue()),
                 QJsonValue(5));
        QVERIFY(!env.variables.changed());
        QCOMPARE(finished, 1);
    }

    // Dienste: Rückruf im Aufruf oder später, doppelter Rückruf ignoriert, Argumente aus dem Ergebnis
    void services_data()
    {
        QTest::addColumn<bool>("sync");
        QTest::newRow("im Aufruf") << true;
        QTest::newRow("später") << false;
    }
    void services()
    {
        QFETCH(bool, sync);
        Env env;
        env.services.sync = sync;
        env.services.userInfoResult = {true, QString(), {{QStringLiteral("targetUserId"), QStringLiteral("100000009")},
                                                         {QStringLiteral("targetUserFound"), true}}};
        const QUuid a = env.add(act(QStringLiteral("Info"),
                                    {mk(kStepUserInfo, {{"login", "  @Ziel "}}), chat(QStringLiteral("%targetUserId%")),
                                     mk(kStepChatSend, {{"text", "Antwort"}, {"reply", true}, {"use_bot", false}})}));
        const QUuid run = env.run(a, {{QStringLiteral("msgId"), QStringLiteral("m-1")}});
        if (!sync) {
            QCOMPARE(env.state(run), RunState::Running);
            const StepDone first = env.services.pending.takeFirst();
            first({});
            first({}); // doppelt: ignoriert
            env.services.pending.takeFirst()({});
            env.services.pending.takeFirst()({});
        }
        QCOMPARE(env.state(run), RunState::Ok);
        QCOMPARE(env.services.userInfos, QStringList{QStringLiteral("Ziel")});
        QCOMPARE(env.services.chatTexts().value(0), QStringLiteral("100000009"));
        QCOMPARE(env.services.chats.value(1).replyTo, QStringLiteral("m-1"));
        QVERIFY(!env.services.chats.value(1).useBot);
        QCOMPARE(env.rec(run).log.size(), 3);
    }

    // Ein Dienst meldet einen Fehler: Eintrag, Lauf geht weiter
    void serviceFailure()
    {
        Env env;
        env.services.userInfoResult = StepResult::failure(QStringLiteral("Twitch antwortet nicht"));
        const QUuid run = env.run(env.add(act(QStringLiteral("F"), {mk(kStepUserInfo, {{"login", "x"}}),
                                                                    chat(QStringLiteral("weiter"))})));
        QCOMPARE(env.state(run), RunState::Error);
        QCOMPARE(messages(env.rec(run), StepLogEntry::Result::Error), QStringList{QStringLiteral("Twitch antwortet nicht")});
        QCOMPARE(env.services.chats.size(), 1);
    }

    // Overlay: Platzhalter in Text und Bild eingesetzt, «warten» über die Uhr (Plan 4.4)
    void overlayWait()
    {
        Env env;
        const QUuid overlayId = QUuid::createUuid();
        const QUuid source = QUuid::createUuid();
        QJsonArray elements{QJsonObject{{"source", uuidText(source)}, {"text", "Hallo %user%"}, {"image", QJsonValue()}},
                            QJsonObject{{"source", uuidText(QUuid::createUuid())}, {"text", QJsonValue()},
                                        {"image", "%bild%"}}};
        Step show = mk(kStepOverlaySource, {{"overlay", uuidText(overlayId)}, {"elements", elements},
                                            {"duration_ms", 8000}, {"wait", true}});
        Step hide = mk(kStepOverlaySource, {{"overlay", uuidText(overlayId)}, {"mode", "hide"}, {"duration_ms", 8000}});
        const QUuid a = env.add(act(QStringLiteral("Overlay"), {show, chat(QStringLiteral("nach 8 s")), hide,
                                                                chat(QStringLiteral("sofort"))}));
        const QUuid run = env.run(a, {{QStringLiteral("user"), QStringLiteral("Testzuschauer")},
                                      {QStringLiteral("bild"), QStringLiteral("https://example.invalid/b.png")}});
        QCOMPARE(env.services.overlays.size(), 1);
        const OverlayRequest &request = env.services.overlays.first();
        QCOMPARE(request.overlay, overlayId);
        QCOMPARE(request.mode, QStringLiteral("show"));
        QCOMPARE(request.elements.size(), 2);
        QCOMPARE(request.elements.at(0).source, source);
        QCOMPARE(request.elements.at(0).text.value_or(QString()), QStringLiteral("Hallo Testzuschauer"));
        QVERIFY(!request.elements.at(0).image);
        QVERIFY(!request.elements.at(1).text);
        QCOMPARE(request.elements.at(1).image.value_or(QString()), QStringLiteral("https://example.invalid/b.png"));
        QCOMPARE(request.arguments.value(QStringLiteral("user")).toString(), QStringLiteral("Testzuschauer"));
        QVERIFY(env.services.chats.isEmpty());
        env.clock.advance(7999);
        QVERIFY(env.services.chats.isEmpty());
        env.clock.advance(1);
        QCOMPARE(env.services.chatTexts(), (QStringList{QStringLiteral("nach 8 s"), QStringLiteral("sofort")}));
        QCOMPARE(env.state(run), RunState::Ok);
        // ohne Overlay: Fehler
        const QUuid bad = env.run(env.add(act(QStringLiteral("Ohne"), {mk(kStepOverlaySource)})));
        QCOMPARE(messages(env.rec(bad), StepLogEntry::Result::Error), QStringList{QStringLiteral("kein Overlay gewählt")});
    }

    // Variablen je Zuschauer über die Twitch-ID, Standardwert, ~global~ (Plan 2.9, 4.6)
    void variables()
    {
        Env env;
        const QUuid a = env.add(act(
            QStringLiteral("Zähler"),
            {mk(kStepGlobalSet, {{"name", "firsts"}, {"scope", "user"}, {"operation", "increment"}, {"value", ""}}),
             mk(kStepGlobalSet, {{"name", "firsts"}, {"scope", "user"}, {"operation", "increment"}, {"value", "2"}}),
             mk(kStepGlobalGet, {{"name", "firsts"}, {"scope", "user"}}),
             mk(kStepGlobalGet, {{"name", "fehlt"}, {"destination", "f"}, {"default", "%user%"}}),
             mk(kStepGlobalGet, {{"name", "fehlt"}, {"destination", "g"}}),
             mk(kStepGlobalSet, {{"name", "platz"}, {"operation", "set"}, {"value", "7"}}),
             mk(kStepGlobalSet, {{"name", "fluechtig"}, {"persisted", false}, {"value", "x"}}),
             mk(kStepGlobalSet, {{"name", "platz"}, {"operation", "decrement"}, {"value", "abc"}}),
             mk(kStepGlobalSet, {{"name", "ziel"}, {"scope", "user"}, {"user", "target"}, {"value", "1"}}),
             mk(kStepGlobalSet, {{"name", "fluechtig"}, {"persisted", false}, {"operation", "delete"}}),
             chat(QStringLiteral("%firsts% %f% ~platz~ %g%"))}));
        const QUuid run = env.run(a, {{QStringLiteral("userId"), QStringLiteral("100000001")},
                                      {QStringLiteral("user"), QStringLiteral("Testzuschauer")}});
        QCOMPARE(env.services.chatTexts(), QStringList{QStringLiteral("3 Testzuschauer 7 %g%")});
        const VariableKey firsts{VariableScope::User, QStringLiteral("100000001"), QStringLiteral("firsts"), true};
        QCOMPARE(env.variables.value(firsts).value_or(QJsonValue()), QJsonValue(3));
        QCOMPARE(env.variables.entry(firsts)->changedAt, env.clock.now());
        QCOMPARE(messages(env.rec(run), StepLogEntry::Result::Error),
                 (QStringList{QStringLiteral("«abc» ist keine Zahl"), QStringLiteral("Zuschauer fehlt (%targetUserId%)")}));
        QVERIFY(!env.variables.value({VariableScope::Global, QString(), QStringLiteral("fluechtig"), false}));
    }

    // Ein Lauf arbeitet mit dem Stand bei seinem Beginn (Plan 3.1)
    void snapshotAtStart()
    {
        Env env;
        const QUuid a = env.add(act(QStringLiteral("Alt"), {delay(100), chat(QStringLiteral("alt"))}));
        const QUuid run = env.run(a);
        env.config.actions.first().steps[1].fields.insert(QStringLiteral("text"), QStringLiteral("neu"));
        env.engine.setConfig(env.config);
        env.clock.advance(100);
        QCOMPARE(env.state(run), RunState::Ok);
        env.run(a);
        env.clock.advance(100);
        QCOMPARE(env.services.chatTexts(), (QStringList{QStringLiteral("alt"), QStringLiteral("neu")}));
    }

    // Verlauf: Signale, Hinweise, exclude_from_history, Einträge ohne Lauf, höchstens 200 (Plan 3.5)
    void history()
    {
        Env env;
        Action hidden = act(QStringLiteral("Versteckt"), {chat(QStringLiteral("x"))});
        hidden.excludeFromHistory = true;
        const QUuid h = env.add(hidden);
        Action concurrent = act(QStringLiteral("Gleichzeitig"), {delay(5)});
        concurrent.concurrent = true;
        concurrent.queue = QUuid::createUuid(); // gelöschte Warteschlange
        const QUuid c = env.add(concurrent);

        int started = 0;
        int changed = 0;
        int finished = 0;
        connect(&env.engine, &ActionEngine::runStarted, this, [&] { ++started; });
        connect(&env.engine, &ActionEngine::runChanged, this, [&] { ++changed; });
        connect(&env.engine, &ActionEngine::runFinished, this, [&] { ++finished; });

        const QUuid hiddenRun = env.run(h);
        QCOMPARE(started + changed + finished, 0);
        QVERIFY(!env.engine.record(hiddenRun));
        QCOMPARE(env.services.chats.size(), 1);

        const QUuid run = env.run(c);
        QCOMPARE(started, 1);
        QVERIFY(changed >= 2); // Beginn und Schritt
        QCOMPARE(env.engine.history().size(), 1);
        QCOMPARE(env.engine.history().first().state, RunState::Running);
        QCOMPARE(messages(env.rec(run), StepLogEntry::Result::Note),
                 (QStringList{QStringLiteral("Warteschlange fehlt, läuft in «Standard»"),
                              QStringLiteral("gleichzeitig nicht unterstützt, nacheinander ausgeführt")}));
        QVERIFY(env.rec(run).queue.isNull());
        env.clock.advance(5);
        QCOMPARE(finished, 1);

        const QUuid rejected = env.engine.addRecord(c, QStringLiteral("command"), RunState::Rejected,
                                                    QStringLiteral("Abklingzeit"));
        QCOMPARE(env.state(rejected), RunState::Rejected);
        QCOMPARE(env.rec(rejected).actionName, QStringLiteral("Gleichzeitig"));
        QCOMPARE(finished, 2);
        QVERIFY(env.engine.enqueue({QUuid::createUuid(), QString(), QUuid(), {}, false}).isNull());

        for (int i = 0; i < kRunHistorySize; ++i)
            env.engine.addRecord(c, QStringLiteral("command"), RunState::Muted, QString());
        QCOMPARE(env.engine.history().size(), kRunHistorySize);
        QVERIFY(!env.engine.record(run));
        QCOMPARE(runStateKey(RunState::Muted), QStringLiteral("muted"));
        QCOMPARE(stepResultKey(StepLogEntry::Result::DryRun), QStringLiteral("dry_run"));
    }

    // Uhr des Systems: Zeitgeber feuert einmal, angehaltener nie
    void systemClock()
    {
        SystemActionClock clock;
        int fired = 0;
        int stopped = 0;
        const QDateTime before = clock.now();
        clock.startTimer(20, [&] { ++fired; });
        const ActionClock::TimerId id = clock.startTimer(10, [&] { ++stopped; });
        clock.stopTimer(id);
        clock.stopTimer(12345);
        QCOMPARE(fired, 0);
        QTRY_COMPARE(fired, 1);
        QTest::qWait(30);
        QCOMPARE(fired, 1);
        QCOMPARE(stopped, 0);
        QVERIFY(clock.now() >= before.addMSecs(20));
        QCOMPARE(clock.now().timeSpec(), Qt::UTC);
    }

    // Engine mit der Uhr des Systems, wie im Kern: Pause in der Ereignisschleife
    void engineWithSystemClock()
    {
        SystemActionClock clock;
        VariableStore variables;
        FakeServices services;
        ActionEngine engine(&clock, &variables);
        engine.setServices(services.services());
        ActionsConfig config;
        config.actions.append(act(QStringLiteral("Echt"), {delay(30), chat(QStringLiteral("fertig"))}));
        engine.setConfig(config);
        const QUuid run = engine.enqueue({config.actions.first().id, QStringLiteral("test"), QUuid(), {}, false});
        QCOMPARE(engine.record(run)->state, RunState::Running);
        QTRY_COMPARE(engine.record(run)->state, RunState::Ok);
        QCOMPARE(services.chatTexts(), QStringList{QStringLiteral("fertig")});
    }

    // Beispiel aus dem Plan (tests/daten/aktionen/plan-beispiel.json): Begrüssung
    void planExample()
    {
        QFile file(QStringLiteral(DSS_TEST_AKTIONEN_DIR "/plan-beispiel.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        Env env;
        QString error;
        QVERIFY2(parseActions(file.readAll(), &env.config, &error), qPrintable(error));
        env.engine.setConfig(env.config);
        env.services.userInfoResult.arguments = {
            {QStringLiteral("targetUserProfileImageUrl"), QStringLiteral("https://example.invalid/profil.png")}};
        const QUuid run = env.run(env.config.actions.first().id,
                                  {{QStringLiteral("user"), QStringLiteral("Testzuschauer")},
                                   {QStringLiteral("userName"), QStringLiteral("testzuschauer")}});
        QCOMPARE(env.services.userInfos, QStringList{QStringLiteral("testzuschauer")});
        QCOMPARE(env.services.chatTexts(), QStringList{QStringLiteral("Willkommen, Testzuschauer!")});
        QCOMPARE(env.services.overlays.first().elements.at(0).image.value_or(QString()),
                 QStringLiteral("https://example.invalid/profil.png"));
        QCOMPARE(env.state(run), RunState::Running);
        env.clock.advance(8000);
        QCOMPARE(env.state(run), RunState::Ok);
        QVERIFY(env.rec(run).queue.isNull()); // UUID aus lauter Nullen = «Standard»
    }
};

QTEST_GUILESS_MAIN(TestActionRun)
#include "tst_aktionsablauf.moc"
