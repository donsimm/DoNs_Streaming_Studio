#include "actionedit.h"

#include "actionjson.h"

#include <QSet>

#include <optional>

namespace dss::actions {

namespace {

const QStringList &editTypes()
{
    static const QStringList types = {
        QStringLiteral("actions_set_enabled"), QStringLiteral("action_put"),    QStringLiteral("action_remove"),
        QStringLiteral("action_copy"),         QStringLiteral("action_reset"),  QStringLiteral("command_put"),
        QStringLiteral("command_remove"),      QStringLiteral("timer_put"),     QStringLiteral("timer_remove"),
        QStringLiteral("queue_put"),           QStringLiteral("queue_remove"),  QStringLiteral("group_put"),
        QStringLiteral("group_remove"),
    };
    return types;
}

ConfigEditResult failure(const QString &message)
{
    return {false, message, {}, {}};
}

QUuid idField(const QJsonObject &fields, const QString &name)
{
    return QUuid::fromString(fields.value(name).toString());
}

// Ersetzt das Objekt mit derselben Kennung oder hängt es an. true = neu.
template <typename T>
bool putItem(QList<T> *list, const T &item)
{
    for (T &existing : *list) {
        if (existing.id == item.id) {
            existing = item;
            return false;
        }
    }
    list->append(item);
    return true;
}

// Entfernt das Objekt; leerer Text = nicht gefunden, sonst sein Name
template <typename T>
std::optional<QString> removeItem(QList<T> *list, const QUuid &id)
{
    for (qsizetype i = 0; i < list->size(); ++i) {
        if (list->at(i).id == id) {
            const QString name = list->at(i).name;
            list->removeAt(i);
            return name;
        }
    }
    return std::nullopt;
}

template <typename T>
ConfigEditResult put(ActionsConfig *config, QList<T> ActionsConfig::*member, const QJsonObject &fields,
                     const char *field, bool (*fromJson)(const QJsonObject &, T *, QString *))
{
    T item;
    QString error;
    if (!fromJson(fields.value(QLatin1String(field)).toObject(), &item, &error))
        return failure(QStringLiteral("%1: %2").arg(QLatin1String(field), error));
    const bool created = putItem(&(config->*member), item);
    return {true, {}, created ? item.id : QUuid(), item.name};
}

template <typename T>
ConfigEditResult remove(ActionsConfig *config, QList<T> ActionsConfig::*member, const QJsonObject &fields,
                        const char *field, const QString &what)
{
    const QUuid id = idField(fields, QLatin1String(field));
    const std::optional<QString> name = removeItem(&(config->*member), id);
    if (!name)
        return failure(QStringLiteral("%1 mit der Kennung %2 gibt es nicht").arg(what, id.toString(QUuid::WithoutBraces)));
    return {true, {}, {}, *name};
}

void renewStepIds(QList<Step> &steps)
{
    for (Step &s : steps) {
        s.id = QUuid::createUuid();
        renewStepIds(s.steps);
    }
}

} // namespace

bool isConfigEditType(const QString &type)
{
    return editTypes().contains(type);
}

Action copiedAction(const ActionsConfig &config, const Action &action)
{
    QSet<QString> names;
    for (const Action &a : config.actions)
        names.insert(a.name);
    Action copy = action;
    copy.id = QUuid::createUuid();
    copy.templateId = QUuid();
    for (Trigger &t : copy.triggers)
        t.id = QUuid::createUuid();
    renewStepIds(copy.steps);
    copy.name = QStringLiteral("%1 (Kopie)").arg(action.name);
    for (int n = 2; names.contains(copy.name); ++n)
        copy.name = QStringLiteral("%1 (Kopie %2)").arg(action.name).arg(n);
    return copy;
}

ConfigEditResult applyConfigEdit(ActionsConfig *config, const QString &type, const QJsonObject &fields)
{
    ActionsConfig next = *config;
    ConfigEditResult result;
    if (type == QLatin1String("actions_set_enabled")) {
        next.enabled = fields.value(QStringLiteral("enabled")).toBool();
        result = {true, {}, {}, next.enabled ? QStringLiteral("Chat-Bot an") : QStringLiteral("Chat-Bot aus")};
    } else if (type == QLatin1String("action_put")) {
        result = put(&next, &ActionsConfig::actions, fields, "action", &actionFromJson);
    } else if (type == QLatin1String("action_remove")) {
        result = remove(&next, &ActionsConfig::actions, fields, "action_id", QStringLiteral("Eine Aktion"));
    } else if (type == QLatin1String("action_copy")) {
        const QUuid id = idField(fields, QStringLiteral("action_id"));
        const Action *original = next.action(id);
        if (!original)
            return failure(QStringLiteral("Eine Aktion mit der Kennung %1 gibt es nicht").arg(id.toString(QUuid::WithoutBraces)));
        const Action copy = copiedAction(next, *original);
        const qsizetype at = original - next.actions.constData();
        next.actions.insert(at + 1, copy); // gleich nach dem Original (Reihenfolge der Läufe, Plan 3.2)
        result = {true, {}, copy.id, copy.name};
    } else if (type == QLatin1String("action_reset")) {
        const QUuid id = idField(fields, QStringLiteral("action_id"));
        const Action *action = next.action(id);
        if (!action)
            return failure(QStringLiteral("Eine Aktion mit der Kennung %1 gibt es nicht").arg(id.toString(QUuid::WithoutBraces)));
        if (action->templateId.isNull())
            return failure(QStringLiteral("«%1» ist keine mitgelieferte Aktion").arg(action->name));
        // Die Vorlagen kommen mit Abschnitt f (Plan 7)
        return failure(QStringLiteral("Zurücksetzen geht noch nicht: Vorlagen fehlen"));
    } else if (type == QLatin1String("command_put")) {
        result = put(&next, &ActionsConfig::commands, fields, "command", &commandFromJson);
    } else if (type == QLatin1String("command_remove")) {
        result = remove(&next, &ActionsConfig::commands, fields, "command_id", QStringLiteral("Einen Befehl"));
    } else if (type == QLatin1String("timer_put")) {
        result = put(&next, &ActionsConfig::timers, fields, "timer", &timerFromJson);
    } else if (type == QLatin1String("timer_remove")) {
        result = remove(&next, &ActionsConfig::timers, fields, "timer_id", QStringLiteral("Eine Zeitnachricht"));
    } else if (type == QLatin1String("queue_put")) {
        result = put(&next, &ActionsConfig::queues, fields, "queue", &queueFromJson);
    } else if (type == QLatin1String("queue_remove")) {
        result = remove(&next, &ActionsConfig::queues, fields, "queue_id", QStringLiteral("Eine Warteschlange"));
    } else if (type == QLatin1String("group_put")) {
        result = put(&next, &ActionsConfig::groups, fields, "group", &userGroupFromJson);
    } else if (type == QLatin1String("group_remove")) {
        result = remove(&next, &ActionsConfig::groups, fields, "group_id", QStringLiteral("Eine Benutzergruppe"));
    } else {
        return failure(QStringLiteral("«%1» ist keine Änderung an aktionen.json").arg(type));
    }
    if (!result.ok)
        return result;
    // Ganzer Stand wie beim Lesen der Datei (Kennungen über alle Teile eindeutig)
    ActionsConfig checked;
    QString error;
    if (!actionsFromJson(actionsToJson(next), &checked, &error))
        return failure(error);
    *config = checked;
    return result;
}

} // namespace dss::actions
