// P14 e2-8: Felder der Schritte für die rechte Spalte im Unterreiter «Aktionen» (actionfields.h):
// jedes Feld aus StepTypeInfo::fields genau einmal mit Art und Grenzen, Wertebezeichnungen zu jedem
// Wert eines Enum, Reihenfolge und Abschnitte nach Plan 4 und 6, Titel und Abschnitt zu einem Feld.
// Ohne Qt Gui (CLAUDE.md Regel 9).

#include "actionfields.h"
#include "steptypes.h"

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
    QCOMPARE(delay[0].rows[0].max, double(kMaxDelayMs));
    QCOMPARE(delay[0].rows[2].max, double(kMaxDelayMs));
    QCOMPARE(delay[0].rows[0].suffix, QStringLiteral("ms"));

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
}

QTEST_GUILESS_MAIN(TestActionFields)
#include "tst_aktionsfelder.moc"
