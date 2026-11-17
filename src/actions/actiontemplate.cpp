#include "actiontemplate.h"

#include "actionjson.h"
#include "triggertypes.h"

#include <QFile>

#include <algorithm>

namespace dss::actions {

namespace {

struct LoadedTemplate
{
    ActionsConfig config;
    QString error;
};

LoadedTemplate loadTemplate()
{
    LoadedTemplate t;
    QFile file(QString::fromLatin1(kActionTemplateResource));
    if (!file.open(QIODevice::ReadOnly)) {
        t.error = QStringLiteral("Vorlage %1 fehlt").arg(QLatin1String(kActionTemplateResource));
        return t;
    }
    if (!parseActions(file.readAll(), &t.config, &t.error)) {
        t.config = {};
        t.error = QStringLiteral("Vorlage ungültig: %1").arg(t.error);
    }
    return t;
}

const LoadedTemplate &loaded()
{
    static const LoadedTemplate t = loadTemplate();
    return t;
}

void renewIds(QList<Step> &steps)
{
    for (Step &s : steps) {
        s.id = QUuid::createUuid();
        renewIds(s.steps);
    }
}

// Befehl, Zeitnachricht und Warteschlange der Vorlage, die in config fehlen, mitnehmen
void addMissingReferences(ActionsConfig *config, Action *action)
{
    const ActionsConfig &tmpl = actionTemplate();
    if (!action->queue.isNull() && !config->queue(action->queue)) {
        if (const Queue *queue = tmpl.queue(action->queue)) {
            const auto sameName = std::find_if(config->queues.cbegin(), config->queues.cend(), [&](const Queue &q) {
                return q.name.compare(queue->name, Qt::CaseInsensitive) == 0;
            });
            if (sameName != config->queues.cend())
                action->queue = sameName->id;
            else
                config->queues.append(*queue);
        }
    }
    for (const Trigger &t : std::as_const(action->triggers)) {
        const QUuid target = QUuid::fromString(
            t.fields.value(t.type == QLatin1String(kTriggerTimer) ? QStringLiteral("timer_id") : QStringLiteral("command_id"))
                .toString());
        if (target.isNull())
            continue;
        if (t.type == QLatin1String(kTriggerCommand) && !config->command(target)) {
            if (const Command *command = tmpl.command(target))
                config->commands.append(*command);
        } else if (t.type == QLatin1String(kTriggerTimer) && !config->timer(target)) {
            if (const Timer *timer = tmpl.timer(target))
                config->timers.append(*timer);
        }
    }
}

// Aktion aus der Vorlage mit neuen Kennungen für Auslöser und Schritte
Action freshCopy(const Action &tmpl)
{
    Action a = tmpl;
    for (Trigger &t : a.triggers)
        t.id = QUuid::createUuid();
    renewIds(a.steps);
    return a;
}

} // namespace

const ActionsConfig &actionTemplate(QString *error)
{
    if (error)
        *error = loaded().error;
    return loaded().config;
}

const Action *templateAction(const QUuid &templateId)
{
    if (templateId.isNull())
        return nullptr;
    for (const Action &a : actionTemplate().actions) {
        if (a.templateId == templateId)
            return &a;
    }
    return nullptr;
}

QList<QUuid> missingTemplates(const ActionsConfig &config)
{
    QList<QUuid> missing;
    for (const Action &t : actionTemplate().actions) {
        const bool present = std::any_of(config.actions.cbegin(), config.actions.cend(),
                                         [&](const Action &a) { return a.templateId == t.templateId; });
        if (!present)
            missing.append(t.templateId);
    }
    return missing;
}

bool resetActionToTemplate(ActionsConfig *config, const QUuid &actionId, QString *error)
{
    const auto it = std::find_if(config->actions.begin(), config->actions.end(),
                                 [&](const Action &a) { return a.id == actionId; });
    if (it == config->actions.end()) {
        *error = QStringLiteral("Eine Aktion mit der Kennung %1 gibt es nicht").arg(actionId.toString(QUuid::WithoutBraces));
        return false;
    }
    if (it->templateId.isNull()) {
        *error = QStringLiteral("«%1» ist keine mitgelieferte Aktion").arg(it->name);
        return false;
    }
    const Action *tmpl = templateAction(it->templateId);
    if (!tmpl) {
        *error = QStringLiteral("Zu «%1» gibt es keine Vorlage").arg(it->name);
        return false;
    }
    Action fresh = freshCopy(*tmpl);
    fresh.id = actionId;
    const qsizetype index = it - config->actions.begin();
    addMissingReferences(config, &fresh);
    config->actions[index] = fresh;
    return true;
}

bool restoreTemplateAction(ActionsConfig *config, const QUuid &templateId, QUuid *created, QString *error)
{
    const Action *tmpl = templateAction(templateId);
    if (!tmpl) {
        *error = QStringLiteral("Eine mitgelieferte Aktion mit der Kennung %1 gibt es nicht")
                     .arg(templateId.toString(QUuid::WithoutBraces));
        return false;
    }
    for (const Action &a : std::as_const(config->actions)) {
        if (a.templateId == templateId) {
            *error = QStringLiteral("Die mitgelieferte Aktion «%1» gibt es schon").arg(a.name);
            return false;
        }
    }
    Action fresh = freshCopy(*tmpl);
    if (config->action(fresh.id))
        fresh.id = QUuid::createUuid();
    addMissingReferences(config, &fresh);
    config->actions.append(fresh);
    *created = fresh.id;
    return true;
}

} // namespace dss::actions
