// Quellen:
//   Platzhalter von Streamer.bot: https://docs.streamer.bot/guide/variables (gelesen 2026-11-09
//   für den Plan P14, Abschnitt 2.9)
//   QString::number mit QLocale::FloatingPointShortest: https://doc.qt.io/qt-6.8/qstring.html#number-6

#include "placeholders.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

#include <cmath>

namespace dss::actions {

namespace {

bool isNameChar(QChar c)
{
    return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
           || (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('_');
}

bool isName(QStringView text)
{
    if (text.isEmpty())
        return false;
    for (QChar c : text) {
        if (!isNameChar(c))
            return false;
    }
    return true;
}

// Geht durch text und ruft found(Begrenzer, Name) für jeden Platzhalter auf. found gibt den
// eingesetzten Text zurück oder nullopt (bleibt wörtlich). Ergebnis: der Text mit Ersetzungen.
template <typename F>
QString scan(const QString &text, F &&found)
{
    QString out;
    out.reserve(text.size());
    qsizetype i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('%') || c == QLatin1Char('~')) {
            const qsizetype end = text.indexOf(c, i + 1);
            if (end > i + 1) {
                const QStringView name = QStringView(text).mid(i + 1, end - i - 1);
                if (isName(name)) {
                    const std::optional<QString> value = found(c, name.toString());
                    out += value ? *value : QStringView(text).mid(i, end - i + 1).toString();
                    i = end + 1;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

QStringList names(const QString &text, QChar delimiter)
{
    QStringList result;
    scan(text, [&](QChar c, const QString &name) -> std::optional<QString> {
        if (c == delimiter && !result.contains(name))
            result.append(name);
        return std::nullopt;
    });
    return result;
}

} // namespace

QString placeholderText(const QJsonValue &value)
{
    switch (value.type()) {
    case QJsonValue::String:
        return value.toString();
    case QJsonValue::Double: {
        const double d = value.toDouble();
        if (std::trunc(d) == d && std::abs(d) < 9007199254740992.0)
            return QString::number(qint64(d));
        return QString::number(d, 'g', QLocale::FloatingPointShortest);
    }
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("True") : QStringLiteral("False");
    case QJsonValue::Array:
        return QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
    case QJsonValue::Object:
        return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
    case QJsonValue::Null:
    case QJsonValue::Undefined:
        break;
    }
    return QString();
}

QString replacePlaceholders(const QString &text, const Arguments &arguments, const GlobalLookup &globals)
{
    return scan(text, [&](QChar c, const QString &name) -> std::optional<QString> {
        if (c == QLatin1Char('%')) {
            const auto it = arguments.constFind(name);
            if (it == arguments.constEnd())
                return std::nullopt;
            return placeholderText(*it);
        }
        if (!globals)
            return std::nullopt;
        const std::optional<QJsonValue> value = globals(name);
        if (!value)
            return std::nullopt;
        return placeholderText(*value);
    });
}

QStringList argumentNames(const QString &text)
{
    return names(text, QLatin1Char('%'));
}

QStringList globalNames(const QString &text)
{
    return names(text, QLatin1Char('~'));
}

QJsonValue autoTypedValue(const QString &text)
{
    // Nur Ziffern, «-» am Anfang und ein Punkt; kein «+», «inf», «nan», «1e5» oder Leerzeichen
    bool digit = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isDigit() && c.unicode() < 128)
            digit = true;
        else if (!(c == QLatin1Char('.') || (i == 0 && c == QLatin1Char('-'))))
            return text;
    }
    if (!digit || text.count(QLatin1Char('.')) > 1)
        return text;
    bool ok = false;
    const double d = QLocale::c().toDouble(text, &ok);
    return ok && std::isfinite(d) ? QJsonValue(d) : QJsonValue(text);
}

} // namespace dss::actions
