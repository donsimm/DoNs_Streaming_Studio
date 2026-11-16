// Quellen:
//   QDateTime ISO 8601 mit Millisekunden (Qt::ISODateWithMs): https://doc.qt.io/qt-6.8/qt.html#DateFormat-enum

#include "variables.h"

#include "fieldspec.h"
#include "placeholders.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <cmath>

namespace dss::actions {

namespace {

constexpr double kMaxExactInteger = 9007199254740992.0;

QJsonObject valueToJson(const VariableValue &v)
{
    return {{QStringLiteral("value"), v.value},
            {QStringLiteral("origin"), variableOriginKey(v.origin)},
            {QStringLiteral("changed_at"), v.changedAt.toUTC().toString(Qt::ISODateWithMs)}};
}

QJsonObject mapToJson(const VariableMap &map)
{
    QJsonObject o;
    for (auto it = map.cbegin(); it != map.cend(); ++it)
        o.insert(it.key(), valueToJson(it.value()));
    return o;
}

bool fail(QString *error, const QString &path, const QString &message)
{
    *error = QStringLiteral("Feld «%1»: %2").arg(path, message);
    return false;
}

bool onlyKeys(const QJsonObject &o, const QStringList &keys, const QString &path, QString *error)
{
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!keys.contains(it.key()))
            return fail(error, path + it.key(), QStringLiteral("unbekanntes Feld"));
    }
    for (const QString &key : keys) {
        if (!o.contains(key))
            return fail(error, path + key, QStringLiteral("fehlt"));
    }
    return true;
}

// counter: nur ganze Zahlen ab 0 (Zähler der Befehle)
bool readValue(const QJsonValue &json, const QString &path, bool counter, VariableValue *out, QString *error)
{
    if (!json.isObject())
        return fail(error, path, QStringLiteral("muss ein Objekt sein"));
    const QJsonObject o = json.toObject();
    const QString prefix = path + QLatin1Char('.');
    if (!onlyKeys(o, {QStringLiteral("value"), QStringLiteral("origin"), QStringLiteral("changed_at")}, prefix, error))
        return false;
    const QJsonValue value = o.value(QStringLiteral("value"));
    if (counter) {
        if (!value.isDouble() || std::trunc(value.toDouble()) != value.toDouble() || value.toDouble() < 0
            || value.toDouble() > kMaxExactInteger)
            return fail(error, prefix + QStringLiteral("value"), QStringLiteral("muss eine ganze Zahl ab 0 sein"));
    } else if (!value.isString() && !value.isDouble()) {
        return fail(error, prefix + QStringLiteral("value"), QStringLiteral("muss Text oder Zahl sein"));
    }
    const QString origin = o.value(QStringLiteral("origin")).toString();
    if (origin == QLatin1String("studio"))
        out->origin = VariableOrigin::Studio;
    else if (origin == QLatin1String("streamerbot"))
        out->origin = VariableOrigin::Streamerbot;
    else
        return fail(error, prefix + QStringLiteral("origin"), QStringLiteral("muss studio oder streamerbot sein"));
    const QDateTime at = QDateTime::fromString(o.value(QStringLiteral("changed_at")).toString(), Qt::ISODateWithMs);
    if (!at.isValid())
        return fail(error, prefix + QStringLiteral("changed_at"),
                    QStringLiteral("muss ein Zeitpunkt nach ISO 8601 sein (2026-11-15T12:00:00.000Z)"));
    out->value = value;
    out->changedAt = at.toUTC();
    return true;
}

bool readMap(const QJsonValue &json, const QString &path, bool counter, QMap<QString, VariableValue> *out,
             QString *error)
{
    if (!json.isObject())
        return fail(error, path, QStringLiteral("muss ein Objekt sein"));
    const QJsonObject o = json.toObject();
    out->clear();
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (it.key().trimmed().isEmpty())
            return fail(error, path, QStringLiteral("leerer Name"));
        VariableValue v;
        if (!readValue(it.value(), path + QLatin1Char('.') + it.key(), counter, &v, error))
            return false;
        out->insert(it.key(), v);
    }
    return true;
}

bool isNumberLike(const QJsonValue &v)
{
    return v.isDouble() || (v.isString() && autoTypedValue(v.toString()).isDouble());
}

} // namespace

QString variableOriginKey(VariableOrigin origin)
{
    return origin == VariableOrigin::Streamerbot ? QStringLiteral("streamerbot") : QStringLiteral("studio");
}

QByteArray serializeVariables(const VariablesData &data)
{
    QJsonObject users;
    for (auto it = data.users.cbegin(); it != data.users.cend(); ++it)
        users.insert(it.key(), mapToJson(it.value()));
    QJsonObject counters;
    for (auto it = data.commandCounters.cbegin(); it != data.commandCounters.cend(); ++it) {
        counters.insert(uuidText(it.key()),
                        QJsonObject{{QStringLiteral("counter"), it->counter ? QJsonValue(valueToJson(*it->counter))
                                                                            : QJsonValue(QJsonValue::Null)},
                                    {QStringLiteral("user_counter"), mapToJson(it->userCounters)}});
    }
    const QJsonObject root{{QStringLiteral("format_version"), kVariablesFormatVersion},
                           {QStringLiteral("global"), mapToJson(data.global)},
                           {QStringLiteral("users"), users},
                           {QStringLiteral("command_counters"), counters}};
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool parseVariables(const QByteArray &bytes, VariablesData *data, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (!doc.isObject()) {
        *error = doc.isNull() ? parseError.errorString() : QStringLiteral("muss ein JSON-Objekt sein");
        return false;
    }
    const QJsonObject root = doc.object();
    const QJsonValue version = root.value(QStringLiteral("format_version"));
    if (!version.isDouble() || std::trunc(version.toDouble()) != version.toDouble())
        return fail(error, QStringLiteral("format_version"), QStringLiteral("fehlt oder ist keine ganze Zahl"));
    if (version.toInt() != kVariablesFormatVersion) {
        *error = QStringLiteral("format_version %1 ist unbekannt, erwartet %2.")
                     .arg(version.toDouble()).arg(kVariablesFormatVersion);
        return false;
    }
    if (!onlyKeys(root, {QStringLiteral("format_version"), QStringLiteral("global"), QStringLiteral("users"),
                         QStringLiteral("command_counters")},
                  QString(), error))
        return false;

    VariablesData result;
    if (!readMap(root.value(QStringLiteral("global")), QStringLiteral("global"), false, &result.global, error))
        return false;
    const QJsonValue users = root.value(QStringLiteral("users"));
    if (!users.isObject())
        return fail(error, QStringLiteral("users"), QStringLiteral("muss ein Objekt sein"));
    const QJsonObject usersObject = users.toObject();
    for (auto it = usersObject.begin(); it != usersObject.end(); ++it) {
        if (it.key().trimmed().isEmpty())
            return fail(error, QStringLiteral("users"), QStringLiteral("leere Twitch-ID"));
        VariableMap map;
        if (!readMap(it.value(), QStringLiteral("users.") + it.key(), false, &map, error))
            return false;
        result.users.insert(it.key(), map);
    }
    const QJsonValue counters = root.value(QStringLiteral("command_counters"));
    if (!counters.isObject())
        return fail(error, QStringLiteral("command_counters"), QStringLiteral("muss ein Objekt sein"));
    const QJsonObject countersObject = counters.toObject();
    for (auto it = countersObject.begin(); it != countersObject.end(); ++it) {
        const QString path = QStringLiteral("command_counters.") + it.key();
        QUuid id;
        if (!parseUuidText(it.key(), &id))
            return fail(error, path, QStringLiteral("Schlüssel muss die UUID eines Befehls sein"));
        if (!it.value().isObject())
            return fail(error, path, QStringLiteral("muss ein Objekt sein"));
        const QJsonObject o = it.value().toObject();
        if (!onlyKeys(o, {QStringLiteral("counter"), QStringLiteral("user_counter")}, path + QLatin1Char('.'), error))
            return false;
        CommandCounter counter;
        if (!o.value(QStringLiteral("counter")).isNull()) {
            VariableValue v;
            if (!readValue(o.value(QStringLiteral("counter")), path + QStringLiteral(".counter"), true, &v, error))
                return false;
            counter.counter = v;
        }
        if (!readMap(o.value(QStringLiteral("user_counter")), path + QStringLiteral(".user_counter"), true,
                     &counter.userCounters, error))
            return false;
        result.commandCounters.insert(id, counter);
    }
    *data = result;
    return true;
}

QJsonValue *VariableStore::volatileSlot(const VariableKey &key, bool create)
{
    QMap<QString, QJsonValue> *map = &m_volatileGlobal;
    if (key.scope == VariableScope::User) {
        if (!create && !m_volatileUsers.contains(key.userId))
            return nullptr;
        map = &m_volatileUsers[key.userId];
    }
    if (!create && !map->contains(key.name))
        return nullptr;
    return &(*map)[key.name];
}

VariableValue *VariableStore::persistedSlot(const VariableKey &key, bool create)
{
    VariableMap *map = &m_data.global;
    if (key.scope == VariableScope::User) {
        if (!create && !m_data.users.contains(key.userId))
            return nullptr;
        map = &m_data.users[key.userId];
    }
    if (!create && !map->contains(key.name))
        return nullptr;
    return &(*map)[key.name];
}

std::optional<QJsonValue> VariableStore::value(const VariableKey &key) const
{
    if (key.persisted) {
        const std::optional<VariableValue> e = entry(key);
        return e ? std::optional<QJsonValue>(e->value) : std::nullopt;
    }
    const QMap<QString, QJsonValue> &map =
        key.scope == VariableScope::User ? m_volatileUsers.value(key.userId) : m_volatileGlobal;
    const auto it = map.constFind(key.name);
    return it == map.constEnd() ? std::nullopt : std::optional<QJsonValue>(*it);
}

std::optional<VariableValue> VariableStore::entry(const VariableKey &key) const
{
    if (!key.persisted)
        return std::nullopt;
    const VariableMap &map = key.scope == VariableScope::User ? m_data.users.value(key.userId) : m_data.global;
    const auto it = map.constFind(key.name);
    return it == map.constEnd() ? std::nullopt : std::optional<VariableValue>(*it);
}

QString VariableStore::set(const VariableKey &key, const QJsonValue &value, const QDateTime &now)
{
    if (key.name.trimmed().isEmpty())
        return QStringLiteral("Name der Variable fehlt");
    if (key.scope == VariableScope::User && key.userId.trimmed().isEmpty())
        return QStringLiteral("Variable «%1»: Zuschauer fehlt").arg(key.name);
    if (!value.isString() && !value.isDouble())
        return QStringLiteral("Variable «%1»: Wert muss Text oder Zahl sein").arg(key.name);
    if (!key.persisted) {
        *volatileSlot(key, true) = value;
        return QString();
    }
    *persistedSlot(key, true) = VariableValue{value, VariableOrigin::Studio, now.toUTC()};
    m_changed = true;
    return QString();
}

QString VariableStore::add(const VariableKey &key, double delta, const QDateTime &now, QJsonValue *result)
{
    const QJsonValue current = value(key).value_or(QJsonValue(0));
    if (!isNumberLike(current))
        return QStringLiteral("Variable «%1» ist keine Zahl").arg(key.name);
    const double base = current.isDouble() ? current.toDouble() : autoTypedValue(current.toString()).toDouble();
    const QJsonValue next(base + delta);
    const QString error = set(key, next, now);
    if (error.isEmpty() && result)
        *result = next;
    return error;
}

bool VariableStore::remove(const VariableKey &key)
{
    if (!key.persisted) {
        if (key.scope == VariableScope::Global)
            return m_volatileGlobal.remove(key.name) > 0;
        auto it = m_volatileUsers.find(key.userId);
        if (it == m_volatileUsers.end() || it->remove(key.name) == 0)
            return false;
        if (it->isEmpty())
            m_volatileUsers.erase(it);
        return true;
    }
    bool removed = false;
    if (key.scope == VariableScope::Global) {
        removed = m_data.global.remove(key.name) > 0;
    } else {
        auto it = m_data.users.find(key.userId);
        removed = it != m_data.users.end() && it->remove(key.name) > 0;
        if (removed && it->isEmpty())
            m_data.users.erase(it);
    }
    m_changed |= removed;
    return removed;
}

VariableStore::CommandCount VariableStore::countCommand(const QUuid &commandId, const QString &userId,
                                                        bool persistCounter, bool persistUserCounter,
                                                        const QDateTime &now)
{
    CommandCount count;
    const auto bump = [&](std::optional<VariableValue> *slot) {
        const qint64 next = (*slot ? qint64((*slot)->value.toDouble()) : 0) + 1;
        *slot = VariableValue{double(next), VariableOrigin::Studio, now.toUTC()};
        m_changed = true;
        return next;
    };
    if (persistCounter) {
        count.counter = bump(&m_data.commandCounters[commandId].counter);
    } else {
        count.counter = ++m_volatileCounters[commandId];
    }
    if (userId.isEmpty())
        return count;
    if (persistUserCounter) {
        CommandCounter &c = m_data.commandCounters[commandId];
        std::optional<VariableValue> slot;
        if (const auto it = c.userCounters.constFind(userId); it != c.userCounters.constEnd())
            slot = *it;
        count.userCounter = bump(&slot);
        c.userCounters.insert(userId, *slot);
    } else {
        count.userCounter = ++m_volatileUserCounters[commandId][userId];
    }
    return count;
}

} // namespace dss::actions
