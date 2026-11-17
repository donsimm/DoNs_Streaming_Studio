#include "event.h"

namespace dss::events {

QJsonObject eventToJson(const Event &event)
{
    return QJsonObject{
        {QStringLiteral("id"), event.id.toString(QUuid::WithoutBraces)},
        {QStringLiteral("type"), event.type},
        {QStringLiteral("origin"), event.origin},
        {QStringLiteral("time"), event.time.toUTC().toString(Qt::ISODateWithMs)},
        {QStringLiteral("origin_id"), event.originId},
        {QStringLiteral("data"), event.data},
    };
}

bool eventFromJson(const QJsonObject &object, Event *event, QString *error)
{
    for (const char *key : {"id", "type", "origin", "time", "origin_id"}) {
        if (!object.value(QLatin1String(key)).isString()) {
            *error = QStringLiteral("Ereignis ohne Text im Feld «%1»").arg(QLatin1String(key));
            return false;
        }
    }
    if (!object.value(QLatin1String("data")).isObject()) {
        *error = QStringLiteral("Ereignis ohne Objekt im Feld «data»");
        return false;
    }
    Event result;
    result.id = QUuid::fromString(object.value(QLatin1String("id")).toString());
    result.type = object.value(QLatin1String("type")).toString();
    result.origin = object.value(QLatin1String("origin")).toString();
    result.time = QDateTime::fromString(object.value(QLatin1String("time")).toString(), Qt::ISODateWithMs);
    result.originId = object.value(QLatin1String("origin_id")).toString();
    result.data = object.value(QLatin1String("data")).toObject();
    if (result.id.isNull() || result.type.isEmpty() || !result.time.isValid()) {
        *error = QStringLiteral("Ereignis mit ungültiger Kennung, leerem Typ oder ungültiger Zeit");
        return false;
    }
    *event = result;
    return true;
}

} // namespace dss::events
