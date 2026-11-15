#include "eventtypes.h"

namespace dss::events {

const EventType *findEventType(const QString &type)
{
    for (const EventType &entry : kEventTypes) {
        if (type == QLatin1String(entry.type))
            return &entry;
    }
    return nullptr;
}

QStringList eventTypeNames()
{
    QStringList names;
    for (const EventType &entry : kEventTypes)
        names.append(QString::fromLatin1(entry.type));
    return names;
}

bool matchesFilter(const QString &type, const QString &filter)
{
    if (filter.endsWith(QLatin1Char('.')))
        return type.startsWith(filter);
    return type == filter;
}

bool matchesFilters(const QString &type, const QStringList &filters)
{
    for (const QString &filter : filters) {
        if (matchesFilter(type, filter))
            return true;
    }
    return false;
}

bool isValidFilter(const QString &filter)
{
    if (!filter.endsWith(QLatin1Char('.')))
        return findEventType(filter) != nullptr;
    if (filter.size() < 2)
        return false;
    for (const EventType &entry : kEventTypes) {
        if (matchesFilter(QString::fromLatin1(entry.type), filter))
            return true;
    }
    return false;
}

} // namespace dss::events
