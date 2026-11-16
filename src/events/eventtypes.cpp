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

QStringList eventArgumentNames(const QString &type)
{
    QStringList names;
    const EventType *entry = findEventType(type);
    if (!entry)
        return names;
    for (const EventField &field : entry->fields)
        names.append(QString::fromLatin1(field.argument));
    for (const char *name : entry->derived)
        names.append(QString::fromLatin1(name));
    return names;
}

bool keepsHistory(const QString &type)
{
    const EventType *entry = findEventType(type);
    return !entry || QLatin1String(entry->origin) != QLatin1String("actions");
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
