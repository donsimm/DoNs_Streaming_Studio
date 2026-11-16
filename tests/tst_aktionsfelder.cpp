// P14 e2-8: Felder der Schritte für die rechte Spalte im Unterreiter «Aktionen» (actionfields.h):
// jedes Feld aus StepTypeInfo::fields genau einmal mit Art und Grenzen, Wertebezeichnungen zu jedem
// Wert eines Enum, Reihenfolge und Abschnitte nach Plan 4 und 6, Titel und Abschnitt zu einem Feld.
// P14 e2-9: dasselbe für die Filter der Auslöser (TriggerTypeInfo::filters, Plan 2.5).
// Ohne Qt Gui (CLAUDE.md Regel 9).

#include "actionfields.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QSet>
#include <QTest>

using namespace dss;
using namespace dss::actions;

namespace {

QStringList rowNames(const QList<FieldSection> &form)
{
    QStringList names;
    for (const FieldSection &s : form) {
        for (const FieldRow &r : s.rows)
            names.append(r.name);
    }
    return names;
}

QStringList sectionTitles(const QList<FieldSection> &form)
{
    QStringList titles;
    for (const FieldSection &s : form)
        titles.append(s.title);
    return titles;
}

} // namespace

class TestActionFields : public QObject
{
    Q_OBJECT

private slots:
    void everyFieldOnce_data();
    void everyFieldOnce();
    void order_data();
    void order();
    void limitsAndChoices();
    void titles();
    void sectionOfField();
    void elementContent();
    void unknownType();
    void everyFilterOnce_data();
    void everyFilterOnce();
    void triggerOrder_data();
    void triggerOrder();
    void triggerLimitsAndChoices();
    void triggerTitles();
    void triggerSectionOfField();
    void secondsForMilliseconds();
    void inactiveRows();
    void listenButton();
};

void TestActionFields::everyFieldOnce_data()
{
    QTest::addColumn<QString>("type");
    for (const StepTypeInfo &t : stepTypes())
        QTest::newRow(qPrintable(t.key)) << t.key;
}

void TestActionFields::everyFieldOnce()
{
    QFETCH(QString, type);
    const StepTypeInfo *info = stepType(type);
    QVERIFY(info);
    const QList<FieldSection> form = stepForm(type);

    const QStringList names = rowNames(form);
    QCOMPARE(QSet<QString>(names.cbegin(), names.cend()).size(), names.size()); // keine Doppelten
    QStringList expected;
    for (const FieldSpec &spec : info->fields)
        expected.append(spec.name);
    QStringList sortedNames = names;
    sortedNames.sort();
    expected.sort();
    QCOMPARE(sortedNames, expected);

    for (const FieldSection &s : form) {
        QVERIFY2(!s.title.isEmpty(), qPrintable(type));
        QVERIFY2(!s.rows.isEmpty(), qPrintable(s.title));
        QVERIFY(!s.title.contains(QChar(0x00DF))); // Schweizer Rechtschreibung, kein «ß»
        for (const FieldRow &r : s.rows) {
            QVERIFY2(!r.label.isEmpty(), qPrintable(r.name));
            QVERIFY(!r.label.contains(QChar(0x00DF)));
            const FieldSpec *spec = nullptr;
            for (const FieldSpec &f : info->fields) {
                if (f.name == r.name)
                    spec = &f;
            }
            QVERIFY(spec);
            QCOMPARE(int(r.kind), int(spec->kind));
            if (r.kind == FieldKind::Enum) {
                // jeder Wert genau einmal, in der Reihenfolge der Tabelle
                QStringList values;
                for (const auto &v : r.values) {
                    values.append(v.first);
                    QVERIFY2(!v.second.isEmpty(), qPrintable(v.first));
                    QVERIFY(!v.second.contains(QChar(0x00DF)));
                }
                QCOMPARE(values, spec->values);
            }
        }
    }
}

void TestActionFields::order_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<QStringList>("sections");
    QTest::addColumn<QStringList>("fields");
    QTest::newRow("overlay_source")
        << QStringLiteral("overlay_source")
        << QStringList{"Overlay", "Elemente", "Dauer", "Einblenden/Ausblenden"}
        << QStringList{"overlay", "mode", "elements", "duration_ms", "wait", "transition_in", "transition_out",
                       "transition_ms"};
    QTest::newRow("play_sound") << QStringLiteral("play_sound") << QStringList{"Ton"}
                                << QStringList{"file", "volume", "wait"};
    QTest::newRow("chat_send") << QStringLiteral("chat_send") << QStringList{"Nachricht", "Konto"}
                               << QStringList{"text", "reply", "use_bot", "fallback"};
    QTest::newRow("delay") << QStringLiteral("delay") << QStringList{"Pause"}
                           << QStringList{"value_ms", "random", "max_value_ms"};
    QTest::newRow("global_get") << QStringLiteral("global_get") << QStringList{"Variable", "Ergebnis"}
                                << QStringList{"name", "scope", "user", "persisted", "destination", "default"};
    QTest::newRow("global_set") << QStringLiteral("global_set") << QStringList{"Variable", "Änderung"}
                                << QStringList{"name", "scope", "user", "persisted", "operation", "value"};
    QTest::newRow("if") << QStringLiteral("if") << QStringList{"Vergleich"}
                        << QStringList{"input", "operation", "value", "auto_type"};
    QTest::newRow("break") << QStringLiteral("break") << QStringList{} << QStringList{};
    QTest::newRow("if_true") << QStringLiteral("if_true") << QStringList{} << QStringList{};
    QTest::newRow("if_false") << QStringLiteral("if_false") << QStringList{} << QStringList{};
}

void TestActionFields::order()
{
    QFETCH(QString, type);
    QFETCH(QStringList, sections);
    QFETCH(QStringList, fields);
    const QList<FieldSection> form = stepForm(type);
    QCOMPARE(sectionTitles(form), sections);
    QCOMPARE(rowNames(form), fields);
}

void TestActionFields::limitsAndChoices()
{
    // Beschriftungen aus Plan 6 (Overlay, Ton) und Grenzen aus steptypes.cpp
    const QList<FieldSection> sound = stepForm(QStringLiteral("play_sound"));
    const FieldRow &file = sound[0].rows[0];
    QCOMPARE(file.label, QStringLiteral("Datei"));
    QCOMPARE(file.choices, QStringLiteral("media"));
    const FieldRow &volume = sound[0].rows[1];
    QCOMPARE(volume.label, QStringLiteral("Lautstärke"));
    QCOMPARE(volume.min, 0.0);
    QCOMPARE(volume.max, 200.0);
    QCOMPARE(volume.suffix, QStringLiteral("%"));

    const QList<FieldSection> overlay = stepForm(QStringLiteral("overlay_source"));
    QCOMPARE(overlay[0].rows[0].label, QStringLiteral("Overlay"));
    QCOMPARE(overlay[0].rows[0].choices, QStringLiteral("overlay"));
    QCOMPARE(int(overlay[1].rows[0].kind), int(FieldKind::ElementList));
    QCOMPARE(overlay[1].rows[0].choices, QStringLiteral("source"));
    QCOMPARE(overlay[2].rows[1].label, QStringLiteral("Warten, bis ausgeblendet"));

    const QList<FieldSection> delay = stepForm(QStringLiteral("delay"));
    QCOMPARE(delay[0].rows[0].max, double(kMaxDelayMs) / 1000);
    QCOMPARE(delay[0].rows[2].max, double(kMaxDelayMs) / 1000);
    QCOMPARE(delay[0].rows[0].suffix, QStringLiteral("s"));

    const QList<FieldSection> run = stepForm(QStringLiteral("run_action"));
    QCOMPARE(run[0].rows[0].choices, QStringLiteral("action"));
    QCOMPARE(int(run[0].rows[0].kind), int(FieldKind::NullableUuid));

    // Bezeichnungen der Änderung wie Plan 4.6
    const QList<FieldSection> set = stepForm(QStringLiteral("global_set"));
    QStringList labels;
    for (const auto &v : set[1].rows[0].values)
        labels.append(v.second);
    QCOMPARE(labels, (QStringList{"Setzen", "Erhöhen", "Verringern", "Löschen"}));
}

void TestActionFields::titles()
{
    for (const StepTypeInfo &t : stepTypes()) {
        Step step;
        step.type = t.key;
        QCOMPARE(stepTitle(step), QStringLiteral("Schritt «%1»").arg(t.label));
    }
    Step step;
    step.type = QStringLiteral("play_sound");
    QCOMPARE(stepTitle(step), QStringLiteral("Schritt «Ton abspielen»"));
    step.type = QStringLiteral("gibt_es_nicht");
    QCOMPARE(stepTitle(step), QStringLiteral("Schritt «gibt_es_nicht»"));
}

void TestActionFields::sectionOfField()
{
    const QList<FieldSection> overlay = stepForm(QStringLiteral("overlay_source"));
    QCOMPARE(fieldSection(overlay, QStringLiteral("overlay")), QStringLiteral("Overlay"));
    QCOMPARE(fieldSection(overlay, QStringLiteral("elements")), QStringLiteral("Elemente"));
    QCOMPARE(fieldSection(overlay, QStringLiteral("duration_ms")), QStringLiteral("Dauer"));
    QCOMPARE(fieldSection(overlay, QStringLiteral("wait")), QStringLiteral("Dauer"));
    QCOMPARE(fieldSection(overlay, QStringLiteral("transition_ms")), QStringLiteral("Einblenden/Ausblenden"));
    QCOMPARE(fieldSection(overlay, QStringLiteral("gibt_es_nicht")), QString());

    const QList<FieldSection> chat = stepForm(QStringLiteral("chat_send"));
    QCOMPARE(fieldSection(chat, QStringLiteral("text")), QStringLiteral("Nachricht"));
    QCOMPARE(fieldSection(chat, QStringLiteral("fallback")), QStringLiteral("Konto"));
    QCOMPARE(fieldSection({}, QStringLiteral("text")), QString());

    // jedes Feld jedes Typs hat einen Abschnitt
    for (const StepTypeInfo &t : stepTypes()) {
        const QList<FieldSection> form = stepForm(t.key);
        for (const FieldSpec &spec : t.fields)
            QVERIFY2(!fieldSection(form, spec.name).isEmpty(), qPrintable(t.key + QLatin1Char('.') + spec.name));
    }
}

void TestActionFields::elementContent()
{
    const FieldSection s = elementForm();
    QCOMPARE(s.title, QStringLiteral("Inhalt der Quellen"));
    QStringList names;
    for (const FieldRow &r : s.rows)
        names.append(r.name);
    QCOMPARE(names, (QStringList{"source", "text", "image"}));
    QCOMPARE(s.rows[0].choices, QStringLiteral("source"));
    QCOMPARE(int(s.rows[0].kind), int(FieldKind::NullableUuid));
    QCOMPARE(int(s.rows[1].kind), int(FieldKind::NullableString));
    QCOMPARE(int(s.rows[2].kind), int(FieldKind::NullableString));
    QCOMPARE(s.rows[2].choices, QStringLiteral("media"));
}

void TestActionFields::unknownType()
{
    QVERIFY(stepForm(QStringLiteral("gibt_es_nicht")).isEmpty());
    QVERIFY(triggerForm(QStringLiteral("gibt_es_nicht")).isEmpty());
}

// P14 e2-9: Felder der Auslöser (Plan 2.5)

void TestActionFields::everyFilterOnce_data()
{
    QTest::addColumn<QString>("type");
    for (const TriggerTypeInfo &t : triggerTypes())
        QTest::newRow(qPrintable(t.key)) << t.key;
}

void TestActionFields::everyFilterOnce()
{
    QFETCH(QString, type);
    const TriggerTypeInfo *info = triggerType(type);
    QVERIFY(info);
    const QList<FieldSection> form = triggerForm(type);

    const QStringList names = rowNames(form);
    QCOMPARE(QSet<QString>(names.cbegin(), names.cend()).size(), names.size()); // keine Doppelten
    QStringList expected;
    for (const FieldSpec &spec : info->filters)
        expected.append(spec.name);
    QStringList sortedNames = names;
    sortedNames.sort();
    expected.sort();
    QCOMPARE(sortedNames, expected);

    for (const FieldSection &s : form) {
        QVERIFY2(!s.title.isEmpty(), qPrintable(type));
        QVERIFY2(!s.rows.isEmpty(), qPrintable(s.title));
        QVERIFY(!s.title.contains(QChar(0x00DF)));
        for (const FieldRow &r : s.rows) {
            QVERIFY2(!r.label.isEmpty(), qPrintable(r.name));
            QVERIFY(!r.label.contains(QChar(0x00DF)));
            const FieldSpec *spec = nullptr;
            for (const FieldSpec &f : info->filters) {
                if (f.name == r.name)
                    spec = &f;
            }
            QVERIFY(spec);
            QCOMPARE(int(r.kind), int(spec->kind));
            if (r.kind == FieldKind::StringList && !spec->values.isEmpty()) {
                QStringList values;
                for (const auto &v : r.values) {
                    values.append(v.first);
                    QVERIFY2(!v.second.isEmpty(), qPrintable(v.first));
                }
                QCOMPARE(values, spec->values);
            }
        }
    }
}

void TestActionFields::triggerOrder_data()
{
    QTest::addColumn<QString>("type");
    QTest::addColumn<QStringList>("sections");
    QTest::addColumn<QStringList>("fields");
    QTest::newRow("command") << QStringLiteral("command") << QStringList{"Befehl"} << QStringList{"command_id"};
    QTest::newRow("timer") << QStringLiteral("timer") << QStringList{"Zeitnachricht"} << QStringList{"timer_id"};
    QTest::newRow("subscribe") << QStringLiteral("twitch.subscribe") << QStringList{"Stufe"}
                               << QStringList{"tiers"};
    QTest::newRow("resubscribe") << QStringLiteral("twitch.resubscribe") << QStringList{"Stufe", "Monate"}
                                 << QStringList{"tiers", "min_months"};
    QTest::newRow("gift_sub") << QStringLiteral("twitch.gift_sub") << QStringList{"Stufe"} << QStringList{"tiers"};
    QTest::newRow("gift_bomb") << QStringLiteral("twitch.gift_bomb") << QStringList{"Stufe", "Anzahl"}
                               << QStringList{"tiers", "min", "max"};
    QTest::newRow("cheer") << QStringLiteral("twitch.cheer") << QStringList{"Bits"} << QStringList{"min", "max"};
    QTest::newRow("raid") << QStringLiteral("twitch.raid") << QStringList{"Zuschauer"}
                          << QStringList{"min", "max"};
    QTest::newRow("stream_online") << QStringLiteral("twitch.stream_online") << QStringList{"Start"}
                                   << QStringList{"skip_at_start"};
    QTest::newRow("test") << QStringLiteral("test") << QStringList{"Testwerte"} << QStringList{"variables"};
    QTest::newRow("chat_message") << QStringLiteral("twitch.chat_message") << QStringList{} << QStringList{};
    QTest::newRow("first_message") << QStringLiteral("twitch.first_message") << QStringList{} << QStringList{};
    QTest::newRow("follow") << QStringLiteral("twitch.follow") << QStringList{} << QStringList{};
    QTest::newRow("stream_offline") << QStringLiteral("twitch.stream_offline") << QStringList{} << QStringList{};
}

void TestActionFields::triggerOrder()
{
    QFETCH(QString, type);
    QFETCH(QStringList, sections);
    QFETCH(QStringList, fields);
    const QList<FieldSection> form = triggerForm(type);
    QCOMPARE(sectionTitles(form), sections);
    QCOMPARE(rowNames(form), fields);
}

void TestActionFields::triggerLimitsAndChoices()
{
    const QList<FieldSection> command = triggerForm(QStringLiteral("command"));
    QCOMPARE(command[0].rows[0].choices, QStringLiteral("command"));
    QCOMPARE(int(command[0].rows[0].kind), int(FieldKind::NullableUuid));
    QCOMPARE(triggerForm(QStringLiteral("timer"))[0].rows[0].choices, QStringLiteral("timer"));

    // Stufen wie triggerDetail in actionview.cpp
    const QList<FieldSection> sub = triggerForm(QStringLiteral("twitch.subscribe"));
    QStringList labels;
    for (const auto &v : sub[0].rows[0].values)
        labels.append(v.second);
    QCOMPARE(labels, (QStringList{"Stufe 1", "Stufe 2", "Stufe 3"}));

    const QList<FieldSection> resub = triggerForm(QStringLiteral("twitch.resubscribe"));
    QCOMPARE(int(resub[1].rows[0].kind), int(FieldKind::Int));
    QCOMPARE(resub[1].rows[0].min, 0.0);
    QCOMPARE(resub[1].rows[0].suffix, QStringLiteral("Monaten"));

    const QList<FieldSection> cheer = triggerForm(QStringLiteral("twitch.cheer"));
    QCOMPARE(cheer[0].rows[0].label, QStringLiteral("von"));
    QCOMPARE(cheer[0].rows[1].label, QStringLiteral("bis"));
    QCOMPARE(cheer[0].rows[1].suffix, QStringLiteral("Bits"));
    QCOMPARE(cheer[0].rows[0].min, 0.0);
    // Hinweis unter «Bits» nur bei Cheer (Entscheid Nutzer 2026-11-16)
    QCOMPARE(cheer[0].title, QStringLiteral("Bits"));
    QCOMPARE(cheer[0].hint, QStringLiteral("Nur «bis»: genau dieser Wert"));
    for (const TriggerTypeInfo &t : triggerTypes())
        for (const FieldSection &s : triggerForm(t.key))
            if (t.key != QLatin1String("twitch.cheer"))
                QVERIFY2(s.hint.isEmpty(), qPrintable(t.key));
    for (const StepTypeInfo &t : stepTypes())
        for (const FieldSection &s : stepForm(t.key))
            QVERIFY2(s.hint.isEmpty(), qPrintable(t.key));
    QCOMPARE(triggerForm(QStringLiteral("twitch.raid"))[0].rows[0].suffix, QStringLiteral("Zuschauer"));
    QCOMPARE(triggerForm(QStringLiteral("twitch.gift_bomb"))[1].rows[1].suffix, QStringLiteral("Abos"));

    const QList<FieldSection> online = triggerForm(QStringLiteral("twitch.stream_online"));
    QCOMPARE(int(online[0].rows[0].kind), int(FieldKind::Bool));
    QCOMPARE(online[0].rows[0].label, QStringLiteral("nicht beim Start des Studios im laufenden Stream"));

    // Testwerte als eigene Art
    QCOMPARE(int(triggerForm(QStringLiteral("test"))[0].rows[0].kind), int(FieldKind::StringMap));
}

void TestActionFields::triggerTitles()
{
    for (const TriggerTypeInfo &t : triggerTypes()) {
        Trigger trigger;
        trigger.type = t.key;
        QCOMPARE(triggerTitle(trigger), QStringLiteral("Auslöser «%1»").arg(t.label));
    }
    Trigger trigger;
    trigger.type = QStringLiteral("twitch.cheer");
    QCOMPARE(triggerTitle(trigger), QStringLiteral("Auslöser «Cheer»"));
    trigger.type = QStringLiteral("gibt_es_nicht");
    QCOMPARE(triggerTitle(trigger), QStringLiteral("Auslöser «gibt_es_nicht»"));
}

void TestActionFields::triggerSectionOfField()
{
    const QList<FieldSection> bomb = triggerForm(QStringLiteral("twitch.gift_bomb"));
    QCOMPARE(fieldSection(bomb, QStringLiteral("tiers")), QStringLiteral("Stufe"));
    QCOMPARE(fieldSection(bomb, QStringLiteral("max")), QStringLiteral("Anzahl"));
    QCOMPARE(fieldSection(bomb, QStringLiteral("gibt_es_nicht")), QString());

    for (const TriggerTypeInfo &t : triggerTypes()) {
        const QList<FieldSection> form = triggerForm(t.key);
        for (const FieldSpec &spec : t.filters)
            QVERIFY2(!fieldSection(form, spec.name).isEmpty(), qPrintable(t.key + QLatin1Char('.') + spec.name));
    }
}

// P14 e2-9b (Entscheid Nutzer, 2026-11-16): jede Zeit in ms wird in Sekunden angezeigt und
// eingegeben, gespeichert bleibt ms
void TestActionFields::secondsForMilliseconds()
{
    QList<QPair<QList<FieldSection>, QList<FieldSpec>>> forms;
    for (const StepTypeInfo &t : stepTypes())
        forms.append({stepForm(t.key), t.fields});
    for (const TriggerTypeInfo &t : triggerTypes())
        forms.append({triggerForm(t.key), t.filters});

    int msRows = 0;
    for (const auto &form : forms) {
        for (const FieldSection &s : form.first) {
            for (const FieldRow &r : s.rows) {
                QVERIFY2(r.suffix != QLatin1String("ms"), qPrintable(r.name));
                if (!r.name.endsWith(QLatin1String("_ms"))) {
                    QCOMPARE(r.scale, 1.0);
                    QCOMPARE(toDisplay(r, 42), 42.0);
                    QCOMPARE(fromDisplay(r, 42), 42.0);
                    continue;
                }
                ++msRows;
                QCOMPARE(r.suffix, QStringLiteral("s"));
                QCOMPARE(r.scale, 1000.0);
                QCOMPARE(r.decimals, 3);
                QCOMPARE(int(r.kind), int(FieldKind::Int));
                const FieldSpec *spec = nullptr;
                for (const FieldSpec &f : form.second) {
                    if (f.name == r.name)
                        spec = &f;
                }
                QVERIFY(spec);
                QCOMPARE(r.min, spec->min / 1000);
                QCOMPARE(r.max, spec->max / 1000);
                QCOMPARE(fromDisplay(r, r.min), spec->min);
                if (spec->max <= double(kMaxDelayMs))
                    QCOMPARE(fromDisplay(r, r.max), spec->max);

                QCOMPARE(toDisplay(r, 8500), 8.5);
                QCOMPARE(fromDisplay(r, 8.5), 8500.0);
                for (const double ms : {0.0, 1.0, 999.0, 1250.0, 8500.0, 123457.0, double(kMaxDelayMs)})
                    QCOMPARE(fromDisplay(r, toDisplay(r, ms)), ms);
                QCOMPARE(fromDisplay(r, 0.001), 1.0);
                QCOMPARE(fromDisplay(r, 0.1 + 0.2), 300.0); // ganze ms trotz Rundungsfehler
            }
        }
    }
    QCOMPARE(msRows, 4); // Pause «Dauer», «bis»; Overlay «Dauer», «Dauer weich»
}

// «weich» wirkt erst ab P14-2: nur der Abschnitt «Einblenden/Ausblenden» ist ausgegraut
void TestActionFields::inactiveRows()
{
    QStringList inactive;
    for (const StepTypeInfo &t : stepTypes()) {
        for (const FieldSection &s : stepForm(t.key)) {
            for (const FieldRow &r : s.rows) {
                if (!r.inactiveReason.isEmpty()) {
                    inactive.append(t.key + QLatin1Char('.') + r.name);
                    QVERIFY(!r.inactiveReason.contains(QChar(0x00DF)));
                }
            }
        }
    }
    for (const TriggerTypeInfo &t : triggerTypes()) {
        for (const FieldSection &s : triggerForm(t.key)) {
            for (const FieldRow &r : s.rows)
                QVERIFY2(r.inactiveReason.isEmpty(), qPrintable(r.name));
        }
    }
    for (const FieldRow &r : elementForm().rows)
        QVERIFY(r.inactiveReason.isEmpty());
    QCOMPARE(inactive, (QStringList{"overlay_source.transition_in", "overlay_source.transition_out",
                                    "overlay_source.transition_ms"}));
    const QList<FieldSection> overlay = stepForm(QStringLiteral("overlay_source"));
    QCOMPARE(fieldSection(overlay, QStringLiteral("transition_ms")), QStringLiteral("Einblenden/Ausblenden"));
}

// «Anhören» ist ein Knopf im Abschnitt «Ton», ausgegraut bis e3; sonst keine Knöpfe
void TestActionFields::listenButton()
{
    for (const StepTypeInfo &t : stepTypes()) {
        const QList<FieldSection> form = stepForm(t.key);
        for (int i = 0; i < form.size(); ++i) {
            if (t.key == QLatin1String("play_sound") && i == 0) {
                QCOMPARE(form[i].title, QStringLiteral("Ton"));
                QCOMPARE(form[i].buttons.size(), 1);
                const FieldButton &listen = form[i].buttons[0];
                QCOMPARE(listen.name, QStringLiteral("listen"));
                QCOMPARE(listen.label, QStringLiteral("Anhören"));
                QVERIFY(!listen.inactiveReason.isEmpty());
                QVERIFY(!listen.inactiveReason.contains(QChar(0x00DF)));
            } else {
                QVERIFY2(form[i].buttons.isEmpty(), qPrintable(t.key));
            }
        }
    }
    for (const TriggerTypeInfo &t : triggerTypes()) {
        for (const FieldSection &s : triggerForm(t.key))
            QVERIFY(s.buttons.isEmpty());
    }
}

QTEST_GUILESS_MAIN(TestActionFields)
#include "tst_aktionsfelder.moc"
