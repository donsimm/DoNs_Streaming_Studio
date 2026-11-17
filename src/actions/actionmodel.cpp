#include "actionmodel.h"

#include "steptypes.h"
#include "triggertypes.h"

namespace dss::actions {

namespace {

template <typename T>
const T *findById(const QList<T> &list, const QUuid &id)
{
    if (id.isNull())
        return nullptr;
    for (const T &item : list) {
        if (item.id == id)
            return &item;
    }
    return nullptr;
}

} // namespace

bool Step::operator==(const Step &other) const
{
    return id == other.id && type == other.type && enabled == other.enabled && weight == other.weight
           && fields == other.fields && steps == other.steps && extra == other.extra;
}

const Action *ActionsConfig::action(const QUuid &id) const
{
    return findById(actions, id);
}

const Command *ActionsConfig::command(const QUuid &id) const
{
    return findById(commands, id);
}

const Timer *ActionsConfig::timer(const QUuid &id) const
{
    return findById(timers, id);
}

const Queue *ActionsConfig::queue(const QUuid &id) const
{
    return findById(queues, id);
}

const UserGroup *ActionsConfig::group(const QUuid &id) const
{
    return findById(groups, id);
}

Step newStep(const QString &type)
{
    Step step;
    const StepTypeInfo *info = stepType(type);
    if (!info)
        return step;
    step.id = QUuid::createUuid();
    step.type = type;
    for (const FieldSpec &f : info->fields)
        step.fields.insert(f.name, f.defaultValue);
    if (type == QLatin1String(kStepIf)) {
        step.steps.append(newStep(QString::fromLatin1(kStepIfTrue)));
        step.steps.append(newStep(QString::fromLatin1(kStepIfFalse)));
    }
    // unsupported ist immer aus (Plan 2.4)
    step.enabled = info->part != StepPart::Never;
    return step;
}

Trigger newTrigger(const QString &type)
{
    Trigger trigger;
    const TriggerTypeInfo *info = triggerType(type);
    if (!info)
        return trigger;
    trigger.id = QUuid::createUuid();
    trigger.type = type;
    for (const FieldSpec &f : info->filters)
        trigger.fields.insert(f.name, f.defaultValue);
    return trigger;
}

} // namespace dss::actions
