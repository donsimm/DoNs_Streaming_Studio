#include "fieldspec.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QUuid>

#include <cmath>

namespace dss::actions {

namespace {

// Grösste ganze Zahl, die ein double in JSON genau trägt (2^53)
constexpr double kMaxExactInteger = 9007199254740992.0;

QString rangeText(const FieldSpec &spec)
{
    const bool hasMin = spec.min > std::numeric_limits<double>::lowest();
    const bool hasMax = spec.max < std::numeric_limits<double>::max();
    if (hasMin && hasMax)
        return QStringLiteral(" von %1 bis %2").arg(spec.min, 0, 'g', 16).arg(spec.max, 0, 'g', 16);
    if (hasMin)
        return QStringLiteral(" ab %1").arg(spec.min, 0, 'g', 16);
    if (hasMax)
        return QStringLiteral(" bis %1").arg(spec.max, 0, 'g', 16);
    return QString();
}

QString checkElement(const QJsonValue &value)
{
    if (!value.isObject())
        return QStringLiteral("jedes Element muss ein Objekt sein");
    const QJsonObject o = value.toObject();
    QUuid id;
    if (!parseUuidText(o.value(QStringLiteral("source")).toString(), &id))
        return QStringLiteral("«source» muss die UUID einer Quelle sein");
    for (const QString &key : {QStringLiteral("text"), QStringLiteral("image")}) {
        const QJsonValue v = o.value(key);
        if (!v.isString() && !v.isNull())
            return QStringLiteral("«%1» muss Text oder null sein").arg(key);
    }
    return QString();
}

} // namespace

QString uuidText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

bool parseUuidText(const QString &text, QUuid *id)
{
    const QUuid parsed = QUuid::fromString(text);
    if (parsed.isNull() || uuidText(parsed) != text)
        return false;
    *id = parsed;
    return true;
}

QString checkFieldValue(const FieldSpec &spec, const QJsonValue &value)
{
    switch (spec.kind) {
    case FieldKind::Bool:
        return value.isBool() ? QString() : QStringLiteral("muss true oder false sein");
    case FieldKind::Int:
    case FieldKind::Number: {
        const bool integer = spec.kind == FieldKind::Int;
        if (!value.isDouble()
            || (integer && (std::trunc(value.toDouble()) != value.toDouble()
                            || std::abs(value.toDouble()) > kMaxExactInteger)))
            return integer ? QStringLiteral("muss eine ganze Zahl%1 sein").arg(rangeText(spec))
                           : QStringLiteral("muss eine Zahl%1 sein").arg(rangeText(spec));
        if (value.toDouble() < spec.min || value.toDouble() > spec.max)
            return QStringLiteral("muss%1 sein").arg(rangeText(spec));
        return QString();
    }
    case FieldKind::String:
        return value.isString() ? QString() : QStringLiteral("muss Text sein");
    case FieldKind::NullableString:
        return value.isString() || value.isNull() ? QString() : QStringLiteral("muss Text oder null sein");
    case FieldKind::NullableUuid: {
        QUuid id;
        return value.isNull() || (value.isString() && parseUuidText(value.toString(), &id))
                   ? QString()
                   : QStringLiteral("muss eine UUID in der Form xxxxxxxx-xxxx-… oder null sein");
    }
    case FieldKind::Enum:
        return value.isString() && spec.values.contains(value.toString())
                   ? QString()
                   : QStringLiteral("muss einer der Werte %1 sein").arg(spec.values.join(QStringLiteral(", ")));
    case FieldKind::StringList: {
        if (!value.isArray())
            return QStringLiteral("muss eine Liste von Texten sein");
        QSet<QString> seen;
        for (const QJsonValue &v : value.toArray()) {
            if (!v.isString())
                return QStringLiteral("muss eine Liste von Texten sein");
            if (!spec.values.isEmpty() && !spec.values.contains(v.toString()))
                return QStringLiteral("erlaubt sind nur %1").arg(spec.values.join(QStringLiteral(", ")));
            if (!spec.values.isEmpty() && seen.contains(v.toString()))
                return QStringLiteral("«%1» kommt mehrfach vor").arg(v.toString());
            seen.insert(v.toString());
        }
        return QString();
    }
    case FieldKind::StringMap: {
        if (!value.isObject())
            return QStringLiteral("muss ein Objekt Name → Text sein");
        const QJsonObject o = value.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!it.value().isString())
                return QStringLiteral("«%1» muss Text sein").arg(it.key());
        }
        return QString();
    }
    case FieldKind::Object:
        return value.isObject() ? QString() : QStringLiteral("muss ein Objekt sein");
    case FieldKind::ElementList: {
        if (!value.isArray())
            return QStringLiteral("muss eine Liste von Elementen sein");
        for (const QJsonValue &v : value.toArray()) {
            const QString problem = checkElement(v);
            if (!problem.isEmpty())
                return problem;
        }
        return QString();
    }
    }
    return QStringLiteral("unbekannte Art des Felds");
}

} // namespace dss::actions
