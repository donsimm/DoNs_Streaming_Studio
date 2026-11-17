#include "cameracallqueue.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

namespace dss::camera {

namespace {

// Gleiche Felder, in verschachtelten Objekten ebenso; die Werte selbst zählen nicht.
bool sameFields(const QJsonObject &a, const QJsonObject &b)
{
    if (a.keys() != b.keys())
        return false;
    for (auto it = a.begin(); it != a.end(); ++it) {
        const QJsonValue other = b.value(it.key());
        if (it->isObject() != other.isObject())
            return false;
        if (it->isObject() && !sameFields(it->toObject(), other.toObject()))
            return false;
    }
    return true;
}

// Relativer Wert: "toggle" bei on (docs/09-kamera-dienst.md, «Lampen»).
bool hasToggle(const QJsonValue &value)
{
    if (value.isString())
        return value.toString() == QLatin1String("toggle");
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (hasToggle(*it))
                return true;
        }
    }
    if (value.isArray()) {
        for (const QJsonValue &item : value.toArray()) {
            if (hasToggle(item))
                return true;
        }
    }
    return false;
}

bool canMerge(const CallQueue::Call &waiting, const QByteArray &method, const QString &path, const QJsonValue &body)
{
    return waiting.method == "PUT" && method == "PUT" && waiting.path == path && waiting.body.isObject()
           && body.isObject() && sameFields(waiting.body.toObject(), body.toObject()) && !hasToggle(waiting.body)
           && !hasToggle(body);
}

} // namespace

CallTarget callTarget(const QByteArray &method, const QString &path)
{
    if (method == "GET")
        return {};
    const QStringList parts = path.split(QLatin1Char('/'));
    const QString &root = parts.first();
    if (root == QLatin1String("lights")) {
        if (parts.size() == 1)
            return {CallTarget::Area::Light, {}}; // POST lights: neue Lampe
        if (parts.at(1) == QLatin1String("check"))
            return {};
        if (parts.at(1) == QLatin1String("all"))
            return {CallTarget::Area::Light, {}};
        return {CallTarget::Area::Light, parts.at(1)};
    }
    if (root == QLatin1String("light-presets"))
        return {CallTarget::Area::Light, {}};
    if (root == QLatin1String("cameras") && parts.size() >= 2) {
        if (parts.size() >= 3 && (parts.at(2) == QLatin1String("move") || parts.at(2) == QLatin1String("stop")))
            return {};
        return {CallTarget::Area::Camera, parts.at(1)};
    }
    if (root == QLatin1String("camera-presets"))
        return {CallTarget::Area::Camera, {}};
    return {};
}

bool targetsConflict(const CallTarget &a, const CallTarget &b)
{
    if (a.area == CallTarget::Area::None || a.area != b.area)
        return false;
    return a.key.isEmpty() || b.key.isEmpty() || a.key == b.key;
}

quint64 CallQueue::add(const QByteArray &method, const QString &path, const QJsonValue &body)
{
    const CallTarget target = callTarget(method, path);
    if (target.area != CallTarget::Area::None) {
        // Nur die letzte wartende Anfrage an dasselbe Ziel kommt in Frage: Steht eine andere
        // dazwischen, bliebe deren Wirkung sonst nicht in der Reihenfolge.
        for (qsizetype i = m_waiting.size() - 1; i >= 0; --i) {
            Entry &entry = m_waiting[i];
            if (!targetsConflict(entry.target, target))
                continue;
            if (canMerge(entry.call, method, path, body)) {
                entry.call.body = body;
                return entry.call.id;
            }
            break;
        }
    }
    const quint64 id = m_nextId++;
    m_waiting.append({{id, method, path, body}, target});
    return id;
}

QList<CallQueue::Call> CallQueue::takeReady()
{
    QList<Call> ready;
    QList<CallTarget> blocked; // Ziele früherer, noch wartender Anfragen
    const auto conflicts = [](const CallTarget &target, const auto &others) {
        for (const auto &other : others) {
            if (targetsConflict(target, other))
                return true;
        }
        return false;
    };
    QList<CallTarget> running;
    for (const Entry &entry : std::as_const(m_running))
        running.append(entry.target);
    for (auto it = m_waiting.begin(); it != m_waiting.end();) {
        if (conflicts(it->target, running) || conflicts(it->target, blocked)) {
            blocked.append(it->target);
            ++it;
            continue;
        }
        ready.append(it->call);
        running.append(it->target);
        m_running.append(*it);
        it = m_waiting.erase(it);
    }
    return ready;
}

void CallQueue::finish(quint64 id)
{
    m_running.removeIf([id](const Entry &entry) { return entry.call.id == id; });
}

} // namespace dss::camera
