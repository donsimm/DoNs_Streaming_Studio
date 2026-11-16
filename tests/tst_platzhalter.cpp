// Prüft die Platzhalter wie Streamer.bot ohne Qt Gui (P14 a, Plan 2.9): %argument%,
// ~dauerhafte globale Variable~, unbekannte bleiben wörtlich, keine zweite Runde, auto_type.
// Erfundene Werte.

#include "placeholders.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>

using namespace dss::actions;

namespace {

Arguments testArguments()
{
    return {{QStringLiteral("user"), QStringLiteral("Testzuschauer")},
            {QStringLiteral("userName"), QStringLiteral("testzuschauer")},
            {QStringLiteral("bits"), 100},
            {QStringLiteral("ratio"), 0.1},
            {QStringLiteral("isSubscribed"), true},
            {QStringLiteral("leer"), QString()},
            {QStringLiteral("nichts"), QJsonValue::Null},
            {QStringLiteral("input0"), QStringLiteral("%user% ~zaehler~")},
            {QStringLiteral("liste"), QJsonArray{1, QStringLiteral("a")}}};
}

std::optional<QJsonValue> testGlobals(const QString &name)
{
    if (name == QLatin1String("zaehler"))
        return QJsonValue(7);
    if (name == QLatin1String("gruss"))
        return QJsonValue(QStringLiteral("Hallo"));
    return std::nullopt;
}

} // namespace

class TestPlaceholders : public QObject
{
    Q_OBJECT

private slots:
    void replace_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("ohne") << QStringLiteral("Hallo zusammen") << QStringLiteral("Hallo zusammen");
        QTest::newRow("leer") << QString() << QString();
        QTest::newRow("Argument") << QStringLiteral("Willkommen, %user%!") << QStringLiteral("Willkommen, Testzuschauer!");
        QTest::newRow("zwei") << QStringLiteral("%user% (%userName%)") << QStringLiteral("Testzuschauer (testzuschauer)");
        QTest::newRow("nebeneinander") << QStringLiteral("%user%%userName%") << QStringLiteral("Testzuschauertestzuschauer");
        QTest::newRow("Zahl") << QStringLiteral("%bits% Bits") << QStringLiteral("100 Bits");
        QTest::newRow("Bruch") << QStringLiteral("%ratio%") << QStringLiteral("0.1");
        QTest::newRow("Wahrheitswert") << QStringLiteral("%isSubscribed%") << QStringLiteral("True");
        QTest::newRow("leerer Wert") << QStringLiteral("[%leer%]") << QStringLiteral("[]");
        QTest::newRow("null") << QStringLiteral("[%nichts%]") << QStringLiteral("[]");
        QTest::newRow("Liste") << QStringLiteral("%liste%") << QStringLiteral("[1,\"a\"]");
        QTest::newRow("unbekannt bleibt") << QStringLiteral("Hallo %unbekannt%!") << QStringLiteral("Hallo %unbekannt%!");
        QTest::newRow("unbekannt dann bekannt") << QStringLiteral("%a%user%") << QStringLiteral("%a%user%");
        QTest::newRow("Prozent im Text") << QStringLiteral("100% sicher, %user%") << QStringLiteral("100% sicher, Testzuschauer");
        QTest::newRow("Prozent am Ende") << QStringLiteral("%user% 50%") << QStringLiteral("Testzuschauer 50%");
        QTest::newRow("doppeltes Prozent") << QStringLiteral("%%user%") << QStringLiteral("%Testzuschauer");
        QTest::newRow("allein") << QStringLiteral("%") << QStringLiteral("%");
        QTest::newRow("leerer Name") << QStringLiteral("%%") << QStringLiteral("%%");
        QTest::newRow("Leerzeichen im Namen") << QStringLiteral("%us er%") << QStringLiteral("%us er%");
        QTest::newRow("Format nicht in P14") << QStringLiteral("%user:upper%") << QStringLiteral("%user:upper%");
        QTest::newRow("Gross/klein egal") << QStringLiteral("%User% %USER%") << QStringLiteral("Testzuschauer Testzuschauer");
        QTest::newRow("Umlaut kein Name") << QStringLiteral("%grüsse%") << QStringLiteral("%grüsse%");
        QTest::newRow("keine zweite Runde") << QStringLiteral("Eingabe: %input0%") << QStringLiteral("Eingabe: %user% ~zaehler~");
        QTest::newRow("global") << QStringLiteral("~gruss~ %user%, Zähler ~zaehler~") << QStringLiteral("Hallo Testzuschauer, Zähler 7");
        QTest::newRow("global unbekannt") << QStringLiteral("~fehlt~") << QStringLiteral("~fehlt~");
        QTest::newRow("Tilde im Text") << QStringLiteral("~ungefähr~ 5") << QStringLiteral("~ungefähr~ 5");
        QTest::newRow("gemischt") << QStringLiteral("%user%~gruss~") << QStringLiteral("TestzuschauerHallo");
        QTest::newRow("Math nicht in P14") << QStringLiteral("$math(1+2)$") << QStringLiteral("$math(1+2)$");
    }

    void replace()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(replacePlaceholders(text, testArguments(), testGlobals), expected);
    }

    void withoutGlobals()
    {
        QCOMPARE(replacePlaceholders(QStringLiteral("~gruss~ %user%"), testArguments()),
                 QStringLiteral("~gruss~ Testzuschauer"));
    }

    void numbers()
    {
        QCOMPARE(placeholderText(QJsonValue(-3)), QStringLiteral("-3"));
        QCOMPARE(placeholderText(QJsonValue(1.5)), QStringLiteral("1.5"));
        QCOMPARE(placeholderText(QJsonValue(1e20)), QStringLiteral("1e+20"));
        QCOMPARE(placeholderText(QJsonValue(9007199254740991.0)), QStringLiteral("9007199254740991"));
        QCOMPARE(placeholderText(QJsonValue(false)), QStringLiteral("False"));
        QCOMPARE(placeholderText(QJsonValue(QJsonObject{{QStringLiteral("a"), 1}})), QStringLiteral("{\"a\":1}"));
    }

    void names()
    {
        const QString text = QStringLiteral("%user% hat ~firsts~ %user% 100% %bits% ~x~");
        QCOMPARE(argumentNames(text), (QStringList{QStringLiteral("user"), QStringLiteral("bits")}));
        QCOMPARE(globalNames(text), (QStringList{QStringLiteral("firsts"), QStringLiteral("x")}));
    }

    void autoType_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QJsonValue>("expected");
        QTest::newRow("ganz") << QStringLiteral("12") << QJsonValue(12);
        QTest::newRow("negativ") << QStringLiteral("-3") << QJsonValue(-3);
        QTest::newRow("Bruch") << QStringLiteral("1.5") << QJsonValue(1.5);
        QTest::newRow("Text") << QStringLiteral("Hallo") << QJsonValue(QStringLiteral("Hallo"));
        QTest::newRow("leer") << QString() << QJsonValue(QString());
        QTest::newRow("Leerzeichen") << QStringLiteral(" 12") << QJsonValue(QStringLiteral(" 12"));
        QTest::newRow("Plus") << QStringLiteral("+5") << QJsonValue(QStringLiteral("+5"));
        QTest::newRow("Exponent") << QStringLiteral("1e5") << QJsonValue(QStringLiteral("1e5"));
        QTest::newRow("inf") << QStringLiteral("inf") << QJsonValue(QStringLiteral("inf"));
        QTest::newRow("zwei Punkte") << QStringLiteral("1.2.3") << QJsonValue(QStringLiteral("1.2.3"));
        QTest::newRow("Komma") << QStringLiteral("1,5") << QJsonValue(QStringLiteral("1,5"));
        QTest::newRow("Wahrheitswert bleibt Text") << QStringLiteral("true") << QJsonValue(QStringLiteral("true"));
        QTest::newRow("nur Minus") << QStringLiteral("-") << QJsonValue(QStringLiteral("-"));
        QTest::newRow("arabische Ziffer") << QStringLiteral("٣") << QJsonValue(QStringLiteral("٣"));
    }

    void autoType()
    {
        QFETCH(QString, text);
        QFETCH(QJsonValue, expected);
        QCOMPARE(autoTypedValue(text), expected);
    }
};

QTEST_GUILESS_MAIN(TestPlaceholders)
#include "tst_platzhalter.moc"
