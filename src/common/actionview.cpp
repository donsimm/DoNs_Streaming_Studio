#include "actionview.h"

#include "actionjson.h"
#include "actionprotocol.h"
#include "eventtypes.h"
#include "fieldspec.h"
#include "protocol.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace dss {

using namespace actions;

namespace {

// Strich für «kein Wert» in Tabellen, wie im Entwurf (Seite 3)
const QString kNone = QStringLiteral("—");

bool matches(const QString &text, const QString &search)
{
    const QString s = search.trimmed();
    return s.isEmpty() || text.contains(s, Qt::CaseInsensitive);
}

bool nameLess(const QString &a, const QString &b)
{
    const int c = QString::compare(a, b, Qt::CaseInsensitive);
    return c != 0 ? c < 0 : a < b;
}

QString uniqueName(const QString &base, const QStringList &names, Qt::CaseSensitivity cs = Qt::CaseSensitive)
{
    QString name = base;
    for (int n = 2; names.contains(name, cs); ++n)
        name = QStringLiteral("%1 %2").arg(base, QString::number(n));
    return name;
}

// Gruppennamen sind ohne Rücksicht auf Gross- und Kleinschreibung eindeutig (Entscheid des Nutzers,
// 2026-11-16).
bool sameGroup(const QString &a, const QString &b)
{
    return !a.isEmpty() && QString::compare(a, b, Qt::CaseInsensitive) == 0;
}

// Schreibweise der Gruppe bei der ersten Aktion in der Datei (ohne die Aktion except); leer, wenn es
// die Gruppe nicht gibt.
QString groupSpelling(const actions::ActionsConfig &config, const QString &group, const QUuid &except = QUuid())
{
    for (const actions::Action &a : config.actions) {
        if ((except.isNull() || a.id != except) && sameGroup(a.group, group))
            return a.group;
    }
    return QString();
}

QString quoted(const QString &text)
{
    return QStringLiteral("«%1»").arg(text);
}

ActionsEdit edit(const char *type, const QJsonObject &fields)
{
    return {QString::fromLatin1(type), fields};
}

EditResult problem(const QString &message)
{
    EditResult r;
    r.problem = message;
    return r;
}

QString emptyNameProblem()
{
    return QStringLiteral("Der Name darf nicht leer sein.");
}

// --- Schritte im Baum einer Aktion ---

bool isBranch(const Step &step)
{
    return step.type == QLatin1String(kStepIfTrue) || step.type == QLatin1String(kStepIfFalse);
}

// Liste, die den Schritt id enthält, und seine Stelle darin
struct StepPlace
{
    QList<Step> *list = nullptr;
    qsizetype index = -1;
};

StepPlace findStep(QList<Step> *steps, const QUuid &id)
{
    if (id.isNull())
        return {};
    for (qsizetype i = 0; i < steps->size(); ++i) {
        if ((*steps)[i].id == id)
            return {steps, i};
        if (const StepPlace inner = findStep(&(*steps)[i].steps, id); inner.list)
            return inner;
    }
    return {};
}

const FieldSpec *fieldSpec(const QList<FieldSpec> &specs, const QString &name)
{
    for (const FieldSpec &s : specs) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

QString text(const QJsonObject &fields, const char *key)
{
    return fields.value(QLatin1String(key)).toString();
}

QUuid idField(const QJsonObject &fields, const char *key)
{
    QUuid id;
    return parseUuidText(fields.value(QLatin1String(key)).toString(), &id) ? id : QUuid();
}

QString accountText(bool useBot)
{
    return useBot ? QStringLiteral("Bot") : QStringLiteral("Kanal");
}

QString variableScopeText(const QJsonObject &f)
{
    QString scope = text(f, "scope") == QLatin1String("user")
                        ? (text(f, "user") == QLatin1String("target") ? QStringLiteral("je Zuschauer (Ziel)")
                                                                       : QStringLiteral("je Zuschauer"))
                        : QStringLiteral("für alle");
    return QStringLiteral("%1, %2").arg(scope, f.value(QStringLiteral("persisted")).toBool()
                                                    ? QStringLiteral("dauerhaft")
                                                    : QStringLiteral("flüchtig"));
}

QString ifOperationText(const QString &operation)
{
    static const QHash<QString, QString> texts{
        {QStringLiteral("equals"), QStringLiteral("ist gleich")},
        {QStringLiteral("not_equals"), QStringLiteral("ist nicht gleich")},
        {QStringLiteral("equals_ignore_case"), QStringLiteral("ist gleich (ohne Gross-/Kleinschreibung)")},
        {QStringLiteral("not_equals_ignore_case"), QStringLiteral("ist nicht gleich (ohne Gross-/Kleinschreibung)")},
        {QStringLiteral("contains"), QStringLiteral("enthält")},
        {QStringLiteral("less_than"), QStringLiteral("ist kleiner als")},
        {QStringLiteral("greater_than"), QStringLiteral("ist grösser als")},
        {QStringLiteral("is_empty"), QStringLiteral("ist leer")},
        {QStringLiteral("not_exists"), QStringLiteral("existiert nicht")},
        {QStringLiteral("regex"), QStringLiteral("passt zum Regex")},
    };
    return texts.value(operation, operation);
}

QString overlayStepSummary(const Step &step, const ActionViewNames &names)
{
    const QJsonObject &f = step.fields;
    const QString mode = text(f, "mode");
    const QString modeText = mode == QLatin1String("hide")     ? QStringLiteral("verstecken")
                             : mode == QLatin1String("toggle") ? QStringLiteral("umschalten")
                                                               : QStringLiteral("zeigen");
    QStringList elements;
    for (const QJsonValue &v : f.value(QStringLiteral("elements")).toArray()) {
        const QUuid source = idField(v.toObject(), "source");
        elements.append(source.isNull()                 ? QStringLiteral("kein Element gewählt")
                        : names.sources.contains(source) ? quoted(names.sources.value(source))
                                                         : QStringLiteral("Element fehlt"));
    }
    if (elements.isEmpty())
        elements.append(QStringLiteral("kein Element gewählt"));
    const QUuid overlay = idField(f, "overlay");
    const QString overlayText = overlay.isNull()                  ? QStringLiteral("kein Overlay gewählt")
                                : names.overlays.contains(overlay) ? QStringLiteral("Overlay %1").arg(quoted(names.overlays.value(overlay)))
                                                                   : QStringLiteral("Overlay fehlt");
    QString s = QStringLiteral("%1: %2 in %3").arg(modeText, elements.join(QStringLiteral(", ")), overlayText);
    const qint64 duration = qint64(f.value(QStringLiteral("duration_ms")).toDouble());
    if (mode != QLatin1String("hide") && duration > 0)
        s += QStringLiteral(" · ") + durationText(duration);
    return s;
}

QString countRangeText(const Trigger &trigger, const QString &unit)
{
    const qint64 min = qint64(trigger.fields.value(QStringLiteral("min")).toDouble());
    const qint64 max = qint64(trigger.fields.value(QStringLiteral("max")).toDouble());
    // Bei Cheer heisst «nur max» genau dieser Wert (Plan 2.5, triggerdispatcher.cpp)
    if (trigger.type == QLatin1String(kTriggerCheer) && min == 0 && max > 0)
        return QStringLiteral("genau %1 %2").arg(max).arg(unit);
    if (min > 0 && max > 0)
        return QStringLiteral("%1 bis %2 %3").arg(min).arg(max).arg(unit);
    if (min > 0)
        return QStringLiteral("ab %1 %2").arg(min).arg(unit);
    if (max > 0)
        return QStringLiteral("bis %1 %2").arg(max).arg(unit);
    return QString();
}

QString triggerDetail(const Trigger &trigger, const ActionsConfig &config)
{
    QStringList parts;
    const QJsonObject &f = trigger.fields;
    const QJsonArray tiers = f.value(QStringLiteral("tiers")).toArray();
    if (!tiers.isEmpty()) {
        QStringList t;
        for (const QJsonValue &v : tiers)
            t.append(QString::number(v.toString().toInt() / 1000));
        parts.append(QStringLiteral("Stufe %1").arg(t.join(QStringLiteral(", "))));
    }
    if (const int months = f.value(QStringLiteral("min_months")).toInt(); months > 0)
        parts.append(months == 1 ? QStringLiteral("ab 1 Monat") : QStringLiteral("ab %1 Monaten").arg(months));
    if (f.contains(QStringLiteral("min"))) {
        const QString unit = trigger.type == QLatin1String(kTriggerCheer)  ? QStringLiteral("Bits")
                             : trigger.type == QLatin1String(kTriggerRaid) ? QStringLiteral("Zuschauer")
                                                                           : QStringLiteral("Abos");
        if (const QString range = countRangeText(trigger, unit); !range.isEmpty())
            parts.append(range);
    }
    if (f.value(QStringLiteral("skip_at_start")).toBool())
        parts.append(QStringLiteral("nicht beim Start des Studios im laufenden Stream"));
    if (trigger.type == QLatin1String(kTriggerTimer)) {
        if (const Timer *timer = config.timer(idField(f, "timer_id")))
            parts.append(QStringLiteral("alle %1 min").arg(minutesText(timer->intervalS)));
    }
    if (trigger.type == QLatin1String(kTriggerTest)) {
        const qsizetype n = f.value(QStringLiteral("variables")).toObject().size();
        if (n > 0)
            parts.append(n == 1 ? QStringLiteral("1 Testwert") : QStringLiteral("%1 Testwerte").arg(n));
    }
    return parts.join(QStringLiteral(" · "));
}

QString triggerText(const Trigger &trigger, const ActionsConfig &config)
{
    const TriggerTypeInfo *info = triggerType(trigger.type);
    const QString label = info ? info->label : trigger.type;
    if (trigger.type.startsWith(QLatin1String("twitch.")))
        return QStringLiteral("Twitch · %1").arg(label);
    if (trigger.type == QLatin1String(kTriggerCommand)) {
        const QUuid id = idField(trigger.fields, "command_id");
        const Command *c = config.command(id);
        const QString what = id.isNull() ? QStringLiteral("kein Befehl gewählt")
                             : !c        ? QStringLiteral("Befehl fehlt")
                             : c->commands.isEmpty() ? c->name
                                                     : c->commands.join(QStringLiteral(", "));
        return QStringLiteral("%1 · %2").arg(label, what);
    }
    if (trigger.type == QLatin1String(kTriggerTimer)) {
        const QUuid id = idField(trigger.fields, "timer_id");
        const Timer *t = config.timer(id);
        const QString what = id.isNull() ? QStringLiteral("keine Zeitnachricht gewählt")
                             : !t        ? QStringLiteral("Zeitnachricht fehlt")
                                         : t->name;
        return QStringLiteral("%1 · %2").arg(label, what);
    }
    return label;
}

void appendStepRows(const QList<Step> &steps, QList<int> path, bool parentInactive,
                    const ActionsConfig &config, const ActionViewNames &names, const QUuid &actionId,
                    const QList<ReferenceWarning> &warnings, QList<StepRow> *rows)
{
    for (qsizetype i = 0; i < steps.size(); ++i) {
        const Step &s = steps.at(i);
        path.append(int(i) + 1);
        StepRow row;
        row.id = s.id;
        row.depth = int(path.size()) - 1;
        QStringList number;
        for (int n : path)
            number.append(QString::number(n));
        row.number = number.join(QLatin1Char('.'));
        const StepTypeInfo *info = stepType(s.type);
        row.type = info ? info->label : s.type;
        if (s.type == QLatin1String(kStepUnsupported))
            row.type = QStringLiteral("nicht unterstützt: %1").arg(qint64(s.fields.value(QStringLiteral("sb_type")).toDouble()));
        row.summary = stepSummary(s, config, names);
        row.enabled = s.enabled;
        row.inactive = parentInactive || !s.enabled;
        row.branch = isBranch(s);
        row.hasChildren = !s.steps.isEmpty();
        row.warning = warningText(warnings, actionId, s.id);
        rows->append(row);
        appendStepRows(s.steps, path, row.inactive, config, names, actionId, warnings, rows);
        path.removeLast();
    }
}

// Aktionen mit einem Auslöser triggerType, dessen Feld field auf id zeigt
QList<const Action *> linkedActions(const ActionsConfig &config, const char *triggerTypeKey, const char *field,
                                    const QUuid &id)
{
    QList<const Action *> result;
    for (const Action &a : config.actions) {
        for (const Trigger &t : a.triggers) {
            if (t.type == QLatin1String(triggerTypeKey) && idField(t.fields, field) == id) {
                result.append(&a);
                break;
            }
        }
    }
    return result;
}

QString linkedActionsText(const ActionsConfig &config, const char *triggerTypeKey, const char *field,
                          const QUuid &id)
{
    QStringList names;
    for (const Action *a : linkedActions(config, triggerTypeKey, field, id))
        names.append(a->name);
    return names.isEmpty() ? kNone : names.join(QStringLiteral(", "));
}

EditResult setLinkedAction(const ActionsConfig &config, const char *triggerTypeKey, const char *field,
                           const QUuid &id, const QUuid &actionId)
{
    const QList<const Action *> linked = linkedActions(config, triggerTypeKey, field, id);
    const Action *current = linked.isEmpty() ? nullptr : linked.first();
    if ((current ? current->id : QUuid()) == actionId)
        return {};
    const Action *target = actionId.isNull() ? nullptr : config.action(actionId);
    if (!actionId.isNull() && !target)
        return problem(QStringLiteral("Die Aktion fehlt."));

    EditResult result;
    if (current) {
        Action a = *current;
        a.triggers.removeIf([&](const Trigger &t) {
            return t.type == QLatin1String(triggerTypeKey) && idField(t.fields, field) == id;
        });
        const EditResult r = putAction(*current, a);
        if (!r.problem.isEmpty())
            return r;
        result.edits += r.edits;
    }
    if (target && !linked.contains(target)) {
        Action a = *target;
        Trigger t = newTrigger(QString::fromLatin1(triggerTypeKey));
        t.fields.insert(QLatin1String(field), uuidText(id));
        a.triggers.append(t);
        const EditResult r = putAction(*target, a);
        if (!r.problem.isEmpty())
            return r;
        result.edits += r.edits;
        result.created = t.id;
    }
    return result;
}

QString viewerLabel(const ViewersData &viewers, const QString &userId)
{
    const auto it = viewers.viewers.constFind(userId);
    if (it != viewers.viewers.constEnd()) {
        if (!it->displayName.isEmpty())
            return it->displayName;
        if (!it->login.isEmpty())
            return it->login;
    }
    return QStringLiteral("unbekannt (%1)").arg(userId);
}

QString originText(VariableOrigin origin)
{
    return origin == VariableOrigin::Streamerbot ? QStringLiteral("Streamer.bot") : QStringLiteral("Studio");
}

VariableRow persistedRow(VariableScope scope, const QString &userId, const QString &name, const VariableValue &v)
{
    VariableRow row;
    row.scope = scope;
    row.userId = userId;
    row.name = name;
    row.value = v.value;
    row.valueText = variableValueText(v.value);
    row.persisted = true;
    row.persistedText = QStringLiteral("dauerhaft");
    row.origin = originText(v.origin);
    return row;
}

VariableRow volatileRow(VariableScope scope, const QString &userId, const QString &name, const QJsonValue &v)
{
    VariableRow row;
    row.scope = scope;
    row.userId = userId;
    row.name = name;
    row.value = v;
    row.valueText = variableValueText(v);
    row.persisted = false;
    row.persistedText = QStringLiteral("flüchtig");
    row.origin = kNone;
    return row;
}

QJsonObject variableFields(const VariableRow &row)
{
    QJsonObject f{{QStringLiteral("scope"),
                   row.scope == VariableScope::User ? QStringLiteral("user") : QStringLiteral("global")},
                  {QStringLiteral("name"), row.name},
                  {QStringLiteral("persisted"), row.persisted}};
    if (row.scope == VariableScope::User)
        f.insert(QStringLiteral("user_id"), row.userId);
    return f;
}

// --- Rückfragen vor dem Löschen ---

// «1 Befehl», «2 Befehlen» (nach «von» bzw. «samt»)
QString countText(qsizetype n, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(QString::number(n), n == 1 ? one : many);
}

// «1 Befehl und 2 Zeitnachrichten», «1 Befehl, 1 Zeitnachricht und 1 Aktion»; leer ohne
QString joinedCounts(const QStringList &parts)
{
    if (parts.size() < 2)
        return parts.join(QString());
    return QStringLiteral("%1 und %2").arg(parts.mid(0, parts.size() - 1).join(QStringLiteral(", ")), parts.last());
}

bool runsAction(const QList<Step> &steps, const QSet<QUuid> &targets)
{
    for (const Step &s : steps) {
        if (s.type == QLatin1String(kStepRunAction) && targets.contains(idField(s.fields, "action_id")))
            return true;
        if (runsAction(s.steps, targets))
            return true;
    }
    return false;
}

// Verweise auf die Aktionen targets von ausserhalb: Befehle und Zeitnachrichten aus ihren Auslösern,
// andere Aktionen mit «Aktion ausführen» darauf. Leer ohne.
QString actionUsesText(const ActionsConfig &config, const QSet<QUuid> &targets)
{
    QSet<QUuid> commands;
    QSet<QUuid> timers;
    qsizetype actions = 0;
    for (const Action &a : config.actions) {
        if (!targets.contains(a.id)) {
            if (runsAction(a.steps, targets))
                ++actions;
            continue;
        }
        for (const Trigger &t : a.triggers) {
            if (t.type == QLatin1String(kTriggerCommand) && config.command(idField(t.fields, "command_id")))
                commands.insert(idField(t.fields, "command_id"));
            else if (t.type == QLatin1String(kTriggerTimer) && config.timer(idField(t.fields, "timer_id")))
                timers.insert(idField(t.fields, "timer_id"));
        }
    }
    QStringList parts;
    if (!commands.isEmpty())
        parts.append(countText(commands.size(), QStringLiteral("Befehl"), QStringLiteral("Befehlen")));
    if (!timers.isEmpty())
        parts.append(countText(timers.size(), QStringLiteral("Zeitnachricht"), QStringLiteral("Zeitnachrichten")));
    if (actions > 0)
        parts.append(countText(actions, QStringLiteral("Aktion"), QStringLiteral("Aktionen")));
    return joinedCounts(parts);
}

// Aktionen mit einem Auslöser triggerType auf id, als «1 Aktion»; leer ohne
QString triggerUsesText(const ActionsConfig &config, const char *triggerTypeKey, const char *field, const QUuid &id)
{
    const qsizetype n = linkedActions(config, triggerTypeKey, field, id).size();
    return n > 0 ? countText(n, QStringLiteral("Aktion"), QStringLiteral("Aktionen")) : QString();
}

// Schritte in steps samt allen darin, ohne die Zweige «Dann» und «Sonst» selbst
qsizetype stepCount(const QList<Step> &steps)
{
    qsizetype n = 0;
    for (const Step &s : steps)
        n += (isBranch(s) ? 0 : 1) + stepCount(s.steps);
    return n;
}

} // namespace

// --- Allgemein ---

QUuid keptSelection(const QList<QUuid> &before, const QList<QUuid> &after, const QUuid &selected)
{
    if (selected.isNull())
        return {};
    if (after.contains(selected))
        return selected;
    const qsizetype at = before.indexOf(selected);
    if (at < 0)
        return {};
    for (qsizetype i = at + 1; i < before.size(); ++i) {
        if (after.contains(before.at(i)))
            return before.at(i);
    }
    for (qsizetype i = at - 1; i >= 0; --i) {
        if (after.contains(before.at(i)))
            return before.at(i);
    }
    return {};
}

QString savedStatusText(const QString &name)
{
    return QStringLiteral("Gespeichert: %1").arg(name);
}

QString deletedStatusText(const QString &name)
{
    return QStringLiteral("Gelöscht: %1").arg(name);
}

QString copyName(const QString &name, const QStringList &names)
{
    const QString first = QStringLiteral("%1 (Kopie)").arg(name);
    QString candidate = first;
    for (int n = 2; names.contains(candidate); ++n)
        candidate = QStringLiteral("%1 (Kopie %2)").arg(name, QString::number(n));
    return candidate;
}

QString warningText(const QList<ReferenceWarning> &warnings, const QUuid &objectId, const QUuid &itemId)
{
    QStringList lines;
    for (const ReferenceWarning &w : warnings) {
        if (w.objectId == objectId && (itemId.isNull() || w.itemId == itemId))
            lines.append(w.message);
    }
    return lines.join(QLatin1Char('\n'));
}

QString durationText(qint64 ms)
{
    if (ms < 1000)
        return QStringLiteral("%1 ms").arg(ms);
    if (ms % 60000 == 0)
        return QStringLiteral("%1 min").arg(ms / 60000);
    if (ms % 1000 == 0)
        return QStringLiteral("%1 s").arg(ms / 1000);
    // Schweizer Schreibweise mit Komma, passt zur Eingabe «8,5 s»
    return QStringLiteral("%1 s").arg(
        QString::number(double(ms) / 1000.0, 'g', 6).replace(QLatin1Char('.'), QLatin1Char(',')));
}

// --- «Aktionen», links ---

QString actionTriggerSummary(const Action &action, const ActionsConfig &config)
{
    QStringList parts;
    for (const Trigger &t : action.triggers) {
        if (!t.enabled)
            continue;
        QString part;
        if (t.type == QLatin1String(kTriggerCommand)) {
            const Command *c = config.command(idField(t.fields, "command_id"));
            part = !c                     ? QStringLiteral("Befehl fehlt")
                   : c->commands.isEmpty() ? c->name
                                           : c->commands.first();
        } else if (t.type == QLatin1String(kTriggerTimer)) {
            const Timer *timer = config.timer(idField(t.fields, "timer_id"));
            // Abstand in Minuten wie in «Zeitnachrichten» (Nachtrag des Nutzers, 2026-11-16)
            part = timer ? QStringLiteral("alle %1 min").arg(minutesText(timer->intervalS))
                         : QStringLiteral("Zeitnachricht fehlt");
        } else {
            const TriggerTypeInfo *info = triggerType(t.type);
            part = info ? info->label : t.type;
        }
        if (!parts.contains(part))
            parts.append(part);
    }
    return parts.join(QStringLiteral(", "));
}

QList<ActionListRow> actionListRows(const ActionsConfig &config, const QString &search,
                                    const QList<ReferenceWarning> &warnings)
{
    const auto actionRow = [&](const Action &a, int depth) {
        ActionListRow row;
        row.group = a.group;
        row.actionId = a.id;
        row.text = a.name;
        row.detail = actionTriggerSummary(a, config);
        row.enabled = a.enabled;
        row.depth = depth;
        row.warning = warningText(warnings, a.id);
        return row;
    };
    const auto actionMatches = [&](const Action &a) {
        return matches(a.name, search) || matches(a.group, search)
               || matches(actionTriggerSummary(a, config), search);
    };

    QList<ActionListRow> rows;
    QSet<QString> groupsDone;
    for (const Action &a : config.actions) {
        if (a.group.isEmpty()) {
            if (actionMatches(a))
                rows.append(actionRow(a, 0));
            continue;
        }
        const QString key = a.group.toCaseFolded();
        if (groupsDone.contains(key))
            continue;
        groupsDone.insert(key);
        const bool groupMatches = matches(a.group, search);
        QList<ActionListRow> members;
        for (const Action &m : config.actions) {
            if (sameGroup(m.group, a.group) && (groupMatches || actionMatches(m))) {
                members.append(actionRow(m, 1));
                members.last().group = a.group;
            }
        }
        if (members.isEmpty())
            continue;
        ActionListRow group;
        group.isGroup = true;
        group.group = a.group;
        group.text = a.group;
        group.detail = QStringLiteral("(%1)").arg(members.size());
        rows.append(group);
        rows += members;
    }
    return rows;
}

EditResult newAction(const ActionsConfig &config, const QString &group)
{
    QStringList names;
    for (const Action &a : config.actions)
        names.append(a.name);
    Action a;
    a.id = QUuid::createUuid();
    a.name = uniqueName(QStringLiteral("Neue Aktion"), names);
    a.group = group.trimmed();
    const QString existing = groupSpelling(config, a.group);
    if (!existing.isEmpty())
        a.group = existing;
    EditResult r;
    r.edits.append(edit("action_put", {{QStringLiteral("action"), actionToJson(a)}}));
    r.created = a.id;
    return r;
}

EditResult newActionGroup(const ActionsConfig &config)
{
    // Gruppen gibt es nur über ihre Aktionen (Plan 2.3): Die neue Gruppe entsteht mit einer neuen Aktion.
    return newAction(config,
                     uniqueName(QStringLiteral("Neue Gruppe"), actionGroupChoices(config), Qt::CaseInsensitive));
}

EditResult copyAction(const ActionsConfig &config, const QUuid &actionId)
{
    if (!config.action(actionId))
        return {};
    EditResult r;
    r.edits.append(edit("action_copy", {{QStringLiteral("action_id"), uuidText(actionId)}}));
    return r;
}

EditResult removeAction(const ActionsConfig &config, const QUuid &actionId)
{
    if (!config.action(actionId))
        return {};
    EditResult r;
    r.edits.append(edit("action_remove", {{QStringLiteral("action_id"), uuidText(actionId)}}));
    return r;
}

bool actionResettable(const Action &action)
{
    return !action.templateId.isNull();
}

EditResult resetAction(const ActionsConfig &config, const QUuid &actionId)
{
    const Action *a = config.action(actionId);
    if (!a || !actionResettable(*a))
        return {};
    EditResult r;
    r.edits.append(edit("action_reset", {{QStringLiteral("action_id"), uuidText(actionId)}}));
    return r;
}

QString resetActionQuestion(const Action &action)
{
    return QStringLiteral("Aktion «%1» auf die mitgelieferte Fassung zurücksetzen? Eigene Änderungen "
                          "an Auslösern und Schritten gehen verloren.")
        .arg(action.name);
}

EditResult renameActionGroup(const ActionsConfig &config, const QString &group, const QString &name)
{
    const QString newName = name.trimmed();
    if (groupSpelling(config, group).isEmpty())
        return {};
    if (newName.isEmpty())
        return problem(emptyNameProblem());
    // Nur eine andere Schreibweise derselben Gruppe ist ein Umbenennen, sonst kein Zusammenführen.
    for (const Action &a : config.actions) {
        if (!sameGroup(a.group, group) && sameGroup(a.group, newName))
            return problem(QStringLiteral("Die Gruppe «%1» gibt es schon.").arg(a.group));
    }
    EditResult r;
    for (const Action &a : config.actions) {
        if (!sameGroup(a.group, group) || a.group == newName)
            continue;
        Action after = a;
        after.group = newName;
        const EditResult put = putAction(a, after);
        if (!put.problem.isEmpty())
            return put;
        r.edits += put.edits;
    }
    return r;
}

EditResult removeActionGroup(const ActionsConfig &config, const QString &group)
{
    EditResult r;
    for (const Action &a : config.actions) {
        if (sameGroup(a.group, group))
            r.edits.append(edit("action_remove", {{QStringLiteral("action_id"), uuidText(a.id)}}));
    }
    return r;
}

QString removeActionQuestion(const ActionsConfig &config, const QUuid &actionId)
{
    const Action *action = config.action(actionId);
    if (!action)
        return QString();
    const QString uses = actionUsesText(config, {actionId});
    if (uses.isEmpty())
        return QString();
    return QStringLiteral("Aktion «%1» löschen? Sie wird von %2 benutzt.").arg(action->name, uses);
}

QString removeActionGroupQuestion(const ActionsConfig &config, const QString &group)
{
    QSet<QUuid> members;
    for (const Action &a : config.actions) {
        if (sameGroup(a.group, group))
            members.insert(a.id);
    }
    if (members.isEmpty())
        return QString();
    QString s = QStringLiteral("Gruppe «%1» mit %2 löschen?")
                    .arg(groupSpelling(config, group), countText(members.size(), QStringLiteral("Aktion"), QStringLiteral("Aktionen")));
    const QString uses = actionUsesText(config, members);
    if (!uses.isEmpty())
        s += (members.size() == 1 ? QStringLiteral(" Sie wird von %1 benutzt.") : QStringLiteral(" Sie werden von %1 benutzt."))
                 .arg(uses);
    return s;
}

// --- «Aktionen», Bereich «Aktion» ---

QString actionTitle(const Action &action)
{
    return QStringLiteral("Aktion %1").arg(quoted(action.name));
}

namespace {

// Name des Arguments zu einem Feld user_* der Chat-Nachricht (eventtypes.h, Plan 1.3)
QString userArgument(const char *field)
{
    const events::EventType *type = events::findEventType(QString::fromLatin1(kTriggerChatMessage));
    Q_ASSERT(type);
    for (const events::EventField &f : type->fields) {
        if (qstrcmp(f.name, field) == 0)
            return QString::fromLatin1(f.argument);
    }
    Q_UNREACHABLE_RETURN(QString());
}

bool testState(const ActionsStatus &status)
{
    return status.state != QLatin1String("unavailable") && status.state != QLatin1String("file_error");
}

} // namespace

QList<TestUserChoice> testUserChoices(const TwitchStatusInfo &twitch)
{
    // Erfunden; 0 ist keine Twitch-ID, Variablen je Zuschauer treffen so keinen echten Zuschauer
    QList<TestUserChoice> choices{{QStringLiteral("Testzuschauer"), QStringLiteral("Testzuschauer"),
                                   QStringLiteral("testzuschauer"), QStringLiteral("0")}};
    const TwitchAccountInfo &channel = twitch.channel;
    if (twitch.enabled && !channel.login.isEmpty() && !channel.userId.isEmpty())
        choices.append({QStringLiteral("eigener Kanal"), channel.login, channel.login, channel.userId});
    return choices;
}

bool testAllowed(const Action &action, const ActionsStatus &status)
{
    return !action.id.isNull() && testState(status);
}

bool realTestAllowed(const Action &action, const ActionsStatus &status)
{
    return testAllowed(action, status) && status.state != QLatin1String("muted");
}

EditResult testAction(const Action &action, const TestUserChoice &user, bool real)
{
    QJsonObject arguments;
    for (const Trigger &trigger : action.triggers) {
        if (trigger.type == QLatin1String(kTriggerTest)) {
            arguments = trigger.fields.value(QStringLiteral("variables")).toObject();
            break;
        }
    }
    arguments.insert(userArgument("user_name"), user.user);
    arguments.insert(userArgument("user_login"), user.userName);
    arguments.insert(userArgument("user_id"), user.userId);
    EditResult r;
    r.edits.append(edit(msg::kActionTest, {{QStringLiteral("action_id"), uuidText(action.id)},
                                           {QStringLiteral("arguments"), arguments},
                                           {QStringLiteral("dry_run"), !real}}));
    return r;
}

QString testStartedStatusText(const QString &name, bool real)
{
    return real ? QStringLiteral("Gestartet: %1").arg(name) : QStringLiteral("Probelauf gestartet: %1").arg(name);
}

QList<QueueChoice> queueChoices(const ActionsConfig &config, const QUuid &current)
{
    QList<QueueChoice> choices{{QUuid(), QStringLiteral("Standard"), false}};
    if (!current.isNull() && !config.queue(current))
        choices.append({current, QStringLiteral("Warteschlange fehlt"), false});
    QList<Queue> queues = config.queues;
    std::stable_sort(queues.begin(), queues.end(),
                     [](const Queue &a, const Queue &b) { return nameLess(a.name, b.name); });
    for (const Queue &q : queues)
        choices.append({q.id, q.blocking ? QStringLiteral("%1 (nacheinander)").arg(q.name) : q.name, false});
    choices.append({QUuid(), QStringLiteral("Warteschlangen verwalten …"), true});
    return choices;
}

int queueChoiceIndex(const QList<QueueChoice> &choices, const QUuid &queue)
{
    for (qsizetype i = 0; i < choices.size(); ++i) {
        if (!choices.at(i).manage && choices.at(i).id == queue)
            return int(i);
    }
    return -1;
}

QStringList actionGroupChoices(const ActionsConfig &config)
{
    QStringList groups;
    for (const Action &a : config.actions) {
        if (!a.group.isEmpty() && !groups.contains(a.group, Qt::CaseInsensitive))
            groups.append(a.group);
    }
    std::sort(groups.begin(), groups.end(), nameLess);
    return groups;
}

QString templateHint(const Action &action, const Action *templateAction)
{
    if (action.templateId.isNull())
        return QString();
    if (templateAction) {
        Action t = *templateAction;
        t.id = action.id;
        t.templateId = action.templateId;
        if (!(t == action))
            return QStringLiteral("mitgeliefert, geändert");
    }
    return QStringLiteral("mitgeliefert");
}

EditResult putAction(const Action &before, const Action &after)
{
    Action a = after;
    a.name = a.name.trimmed();
    a.group = a.group.trimmed();
    if (a.name.isEmpty())
        return problem(emptyNameProblem());
    const QJsonObject json = actionToJson(a);
    Action check;
    QString error;
    if (!actionFromJson(json, &check, &error))
        return problem(error);
    if (a == before)
        return {};
    EditResult r;
    r.edits.append(edit("action_put", {{QStringLiteral("action"), json}}));
    return r;
}

EditResult putAction(const ActionsConfig &config, const Action &before, const Action &after)
{
    Action a = after;
    const QString existing = groupSpelling(config, a.group.trimmed(), a.id);
    if (!existing.isEmpty())
        a.group = existing;
    return putAction(before, a);
}

// --- «Aktionen», Bereich «Auslöser» ---

QList<TriggerRow> triggerRows(const Action &action, const ActionsConfig &config,
                              const QList<ReferenceWarning> &warnings)
{
    QList<TriggerRow> rows;
    for (const Trigger &t : action.triggers) {
        TriggerRow row;
        row.id = t.id;
        row.text = triggerText(t, config);
        row.detail = triggerDetail(t, config);
        row.enabled = t.enabled;
        row.warning = warningText(warnings, action.id, t.id);
        rows.append(row);
    }
    return rows;
}

QList<TypeChoice> triggerChoices()
{
    QList<TypeChoice> choices;
    for (const TriggerTypeInfo &t : triggerTypes()) {
        const bool twitch = t.key.startsWith(QLatin1String("twitch."));
        choices.append({t.key, t.label, twitch ? QStringLiteral("Twitch") : QStringLiteral("Chat-Bot")});
    }
    // Twitch zuerst, sonst in der Reihenfolge der Tabelle
    std::stable_sort(choices.begin(), choices.end(), [](const TypeChoice &a, const TypeChoice &b) {
        return a.section == QLatin1String("Twitch") && b.section != QLatin1String("Twitch");
    });
    return choices;
}

EditResult addTrigger(const Action &action, const QString &type)
{
    const Trigger t = newTrigger(type);
    if (t.id.isNull())
        return problem(QStringLiteral("Unbekannter Auslöser «%1».").arg(type));
    Action a = action;
    a.triggers.append(t);
    EditResult r = putAction(action, a);
    if (r.problem.isEmpty())
        r.created = t.id;
    return r;
}

EditResult removeTrigger(const Action &action, const QUuid &triggerId)
{
    Action a = action;
    if (a.triggers.removeIf([&](const Trigger &t) { return t.id == triggerId; }) == 0)
        return {};
    return putAction(action, a);
}

EditResult setTriggerEnabled(const Action &action, const QUuid &triggerId, bool enabled)
{
    Action a = action;
    for (Trigger &t : a.triggers) {
        if (t.id == triggerId)
            t.enabled = enabled;
    }
    return putAction(action, a);
}

EditResult setTriggerField(const Action &action, const QUuid &triggerId, const QString &field,
                           const QJsonValue &value)
{
    Action a = action;
    for (Trigger &t : a.triggers) {
        if (t.id != triggerId)
            continue;
        const TriggerTypeInfo *info = triggerType(t.type);
        const FieldSpec *spec = info ? fieldSpec(info->filters, field) : nullptr;
        if (!spec)
            return problem(QStringLiteral("Unbekanntes Feld «%1».").arg(field));
        if (const QString message = checkFieldValue(*spec, value); !message.isEmpty())
            return problem(message);
        t.fields.insert(field, value);
        return putAction(action, a);
    }
    return {};
}

// --- «Aktionen», Bereich «Schritte» ---

QString stepSummary(const Step &step, const ActionsConfig &config, const ActionViewNames &names)
{
    const QJsonObject &f = step.fields;
    const QString &type = step.type;
    if (type == QLatin1String(kStepComment))
        return text(f, "text");
    if (type == QLatin1String(kStepGroup)) {
        const QString name = text(f, "name");
        if (!f.value(QStringLiteral("random")).toBool())
            return name;
        return name.isEmpty() ? QStringLiteral("zufällig einer") : QStringLiteral("%1, zufällig einer").arg(name);
    }
    if (type == QLatin1String(kStepIf)) {
        const QString operation = text(f, "operation");
        QString s = QStringLiteral("Wenn %1 %2").arg(text(f, "input"), ifOperationText(operation));
        if (operation != QLatin1String("is_empty") && operation != QLatin1String("not_exists"))
            s += QLatin1Char(' ') + quoted(text(f, "value"));
        if (operation == QLatin1String("regex"))
            s += QStringLiteral(" (wird nicht ausgeführt)");
        return s;
    }
    if (type == QLatin1String(kStepBreak))
        return QStringLiteral("Lauf beenden");
    if (type == QLatin1String(kStepDelay)) {
        const qint64 value = qint64(f.value(QStringLiteral("value_ms")).toDouble());
        if (!f.value(QStringLiteral("random")).toBool())
            return durationText(value);
        const qint64 max = qint64(f.value(QStringLiteral("max_value_ms")).toDouble());
        return QStringLiteral("%1 bis %2, zufällig").arg(durationText(value), durationText(max));
    }
    if (type == QLatin1String(kStepRunAction)) {
        const QUuid id = idField(f, "action_id");
        const Action *target = config.action(id);
        const QString what = id.isNull() ? QStringLiteral("keine Aktion gewählt")
                             : target    ? QStringLiteral("Aktion %1").arg(quoted(target->name))
                                         : QStringLiteral("Aktion fehlt");
        return QStringLiteral("%1, %2").arg(what, f.value(QStringLiteral("immediately")).toBool()
                                                      ? QStringLiteral("sofort")
                                                      : QStringLiteral("eigener Lauf"));
    }
    if (type == QLatin1String(kStepSetArgument))
        return QStringLiteral("%%1% = %2").arg(text(f, "name"), quoted(text(f, "value")));
    if (type == QLatin1String(kStepGlobalGet))
        return QStringLiteral("Variable %1 (%2) → %%3%")
            .arg(quoted(text(f, "name")), variableScopeText(f), text(f, "destination"));
    if (type == QLatin1String(kStepGlobalSet)) {
        const QString operation = text(f, "operation");
        const QString value = text(f, "value");
        const QString name = QStringLiteral("Variable %1").arg(quoted(text(f, "name")));
        QString s;
        if (operation == QLatin1String("increment"))
            s = QStringLiteral("%1 + %2").arg(name, value.trimmed().isEmpty() ? QStringLiteral("1") : value);
        else if (operation == QLatin1String("decrement"))
            s = QStringLiteral("%1 − %2").arg(name, value.trimmed().isEmpty() ? QStringLiteral("1") : value);
        else if (operation == QLatin1String("delete"))
            s = QStringLiteral("%1 löschen").arg(name);
        else
            s = QStringLiteral("%1 = %2").arg(name, quoted(value));
        return QStringLiteral("%1 (%2)").arg(s, variableScopeText(f));
    }
    if (type == QLatin1String(kStepChatSend)) {
        QString s = QStringLiteral("%1: %2").arg(accountText(f.value(QStringLiteral("use_bot")).toBool()),
                                                quoted(text(f, "text")));
        if (f.value(QStringLiteral("reply")).toBool())
            s += QStringLiteral(" · als Antwort");
        return s;
    }
    if (type == QLatin1String(kStepUserInfo))
        return QStringLiteral("Benutzer: %1").arg(text(f, "login"));
    if (type == QLatin1String(kStepTwitchShoutout))
        return QStringLiteral("für %1").arg(text(f, "login"));
    if (type == QLatin1String(kStepTwitchAnnounce))
        return QStringLiteral("%1: %2").arg(accountText(f.value(QStringLiteral("use_bot")).toBool()),
                                           quoted(text(f, "text")));
    if (type == QLatin1String(kStepPlaySound)) {
        const QString file = text(f, "file");
        const QString name = file.isEmpty() ? QStringLiteral("keine Datei gewählt") : names.media.value(file, file);
        return QStringLiteral("%1 · %2 % · %3")
            .arg(name, QString::number(qint64(f.value(QStringLiteral("volume")).toDouble())),
                 f.value(QStringLiteral("wait")).toBool() ? QStringLiteral("warten") : QStringLiteral("nicht warten"));
    }
    if (type == QLatin1String(kStepOverlaySource))
        return overlayStepSummary(step, names);
    if (type == QLatin1String(kStepObsSourceVisible)) {
        const QString state = text(f, "state");
        return QStringLiteral("Quelle %1 in Szene %2: %3")
            .arg(quoted(text(f, "source_name")), quoted(text(f, "scene_name")),
                 state == QLatin1String("off")      ? QStringLiteral("aus")
                 : state == QLatin1String("toggle") ? QStringLiteral("umschalten")
                                                    : QStringLiteral("ein"));
    }
    if (type == QLatin1String(kStepScript))
        return text(f, "language");
    return QString(); // Dann, Sonst, nicht unterstützt (Typ steht in der Bezeichnung)
}

QList<StepRow> stepRows(const Action &action, const ActionsConfig &config, const ActionViewNames &names,
                        const QList<ReferenceWarning> &warnings)
{
    QList<StepRow> rows;
    appendStepRows(action.steps, {}, false, config, names, action.id, warnings, &rows);
    return rows;
}

QList<TypeChoice> stepChoices()
{
    // Art → Typen, in dieser Reihenfolge im Menü
    const QList<std::pair<QString, QStringList>> sections{
        {QStringLiteral("Chat und Twitch"),
         {QString::fromLatin1(kStepChatSend), QString::fromLatin1(kStepUserInfo),
          QString::fromLatin1(kStepTwitchShoutout), QString::fromLatin1(kStepTwitchAnnounce)}},
        {QStringLiteral("Overlay und Ton"),
         {QString::fromLatin1(kStepOverlaySource), QString::fromLatin1(kStepPlaySound)}},
        {QStringLiteral("Ablauf"),
         {QString::fromLatin1(kStepDelay), QString::fromLatin1(kStepIf), QString::fromLatin1(kStepGroup),
          QString::fromLatin1(kStepRunAction), QString::fromLatin1(kStepBreak), QString::fromLatin1(kStepComment)}},
        {QStringLiteral("Variablen und Argumente"),
         {QString::fromLatin1(kStepSetArgument), QString::fromLatin1(kStepGlobalGet),
          QString::fromLatin1(kStepGlobalSet)}},
    };
    QList<TypeChoice> choices;
    for (const auto &[section, keys] : sections) {
        for (const QString &key : keys) {
            const StepTypeInfo *info = stepType(key);
            if (info && info->creatable && info->part == StepPart::P14_1)
                choices.append({key, info->label, section});
        }
    }
    return choices;
}

EditResult addStep(const Action &action, const QUuid &selected, const QString &type)
{
    const StepTypeInfo *info = stepType(type);
    if (!info || !info->creatable)
        return problem(QStringLiteral("Unbekannter Schritt «%1».").arg(type));
    const Step step = newStep(type);
    Action a = action;
    const StepPlace place = findStep(&a.steps, selected);
    if (!place.list) {
        a.steps.append(step);
    } else {
        Step &at = (*place.list)[place.index];
        if (at.type == QLatin1String(kStepGroup) || isBranch(at))
            at.steps.append(step);
        else
            place.list->insert(place.index + 1, step);
    }
    EditResult r = putAction(action, a);
    if (r.problem.isEmpty())
        r.created = step.id;
    return r;
}

EditResult removeStep(const Action &action, const QUuid &stepId)
{
    Action a = action;
    const StepPlace place = findStep(&a.steps, stepId);
    if (!place.list || isBranch(place.list->at(place.index)))
        return {};
    place.list->removeAt(place.index);
    return putAction(action, a);
}

QString removeStepQuestion(const Action &action, const QUuid &stepId)
{
    Action a = action;
    const StepPlace place = findStep(&a.steps, stepId);
    if (!place.list)
        return QString();
    const Step &step = place.list->at(place.index);
    const qsizetype n = stepCount(step.steps);
    if (isBranch(step) || n == 0)
        return QString();
    const StepTypeInfo *info = stepType(step.type);
    return QStringLiteral("Schritt «%1» samt %2 darin löschen?")
        .arg(info ? info->label : step.type, countText(n, QStringLiteral("Schritt"), QStringLiteral("Schritten")));
}

EditResult moveStep(const Action &action, const QUuid &stepId, int direction)
{
    Action a = action;
    const StepPlace place = findStep(&a.steps, stepId);
    if (!place.list || isBranch(place.list->at(place.index)) || direction == 0)
        return {};
    const qsizetype to = place.index + (direction < 0 ? -1 : 1);
    if (to < 0 || to >= place.list->size())
        return {};
    place.list->swapItemsAt(place.index, to);
    return putAction(action, a);
}

EditResult setStepEnabled(const Action &action, const QUuid &stepId, bool enabled)
{
    Action a = action;
    const StepPlace place = findStep(&a.steps, stepId);
    if (!place.list || isBranch(place.list->at(place.index)))
        return {};
    (*place.list)[place.index].enabled = enabled;
    return putAction(action, a);
}

EditResult setStepField(const Action &action, const QUuid &stepId, const QString &field, const QJsonValue &value)
{
    Action a = action;
    const StepPlace place = findStep(&a.steps, stepId);
    if (!place.list)
        return {};
    Step &step = (*place.list)[place.index];
    const StepTypeInfo *info = stepType(step.type);
    const FieldSpec *spec = info ? fieldSpec(info->fields, field) : nullptr;
    if (!spec)
        return problem(QStringLiteral("Unbekanntes Feld «%1».").arg(field));
    if (const QString message = checkFieldValue(*spec, value); !message.isEmpty())
        return problem(message);
    step.fields.insert(field, value);
    return putAction(action, a);
}

StepButtons stepButtons(const Action &action, const QUuid &selected)
{
    Action a = action;
    const StepPlace place = findStep(&a.steps, selected);
    if (!place.list || isBranch(place.list->at(place.index)))
        return {};
    return {true, place.index > 0, place.index + 1 < place.list->size()};
}

// --- Ausführen: Aktion ---

QList<ActionChoice> actionChoices(const ActionsConfig &config)
{
    QList<ActionChoice> actions;
    for (const Action &a : config.actions)
        actions.append({a.id, a.name});
    std::stable_sort(actions.begin(), actions.end(),
                     [](const ActionChoice &a, const ActionChoice &b) { return nameLess(a.label, b.label); });
    actions.prepend({QUuid(), QStringLiteral("Keine")});
    return actions;
}

QUuid commandAction(const ActionsConfig &config, const QUuid &commandId)
{
    const QList<const Action *> linked = linkedActions(config, kTriggerCommand, "command_id", commandId);
    return linked.isEmpty() ? QUuid() : linked.first()->id;
}

QUuid timerAction(const ActionsConfig &config, const QUuid &timerId)
{
    const QList<const Action *> linked = linkedActions(config, kTriggerTimer, "timer_id", timerId);
    return linked.isEmpty() ? QUuid() : linked.first()->id;
}

EditResult setCommandAction(const ActionsConfig &config, const QUuid &commandId, const QUuid &actionId)
{
    if (!config.command(commandId))
        return {};
    return setLinkedAction(config, kTriggerCommand, "command_id", commandId, actionId);
}

EditResult setTimerAction(const ActionsConfig &config, const QUuid &timerId, const QUuid &actionId)
{
    if (!config.timer(timerId))
        return {};
    return setLinkedAction(config, kTriggerTimer, "timer_id", timerId, actionId);
}

// --- Unterreiter «Befehle» ---

QString permissionsText(const Permissions &permissions, const ActionsConfig &config)
{
    QStringList parts;
    for (const QString &role : permissions.roles) {
        parts.append(role == QLatin1String("subscriber") ? QStringLiteral("Abonnenten")
                     : role == QLatin1String("vip")      ? QStringLiteral("VIPs")
                     : role == QLatin1String("moderator") ? QStringLiteral("Moderatoren")
                                                          : role);
    }
    for (const QUuid &id : permissions.groups) {
        const UserGroup *g = config.group(id);
        parts.append(g ? g->name : QStringLiteral("Gruppe fehlt"));
    }
    if (const qsizetype n = permissions.users.size(); n > 0)
        parts.append(n == 1 ? QStringLiteral("1 Zuschauer") : QStringLiteral("%1 Zuschauer").arg(n));
    if (parts.isEmpty())
        return QStringLiteral("Alle");
    const QString list = parts.join(QStringLiteral(", "));
    return permissions.grant == QLatin1String("deny") ? QStringLiteral("alle ausser %1").arg(list) : list;
}

QList<CommandRow> commandRows(const ActionsConfig &config, const QString &search, const QString &group,
                              const VariablesData *variables, const QList<ReferenceWarning> &warnings)
{
    QList<CommandRow> rows;
    for (const Command &c : config.commands) {
        if (!group.isEmpty() && c.group != group)
            continue;
        CommandRow row;
        row.id = c.id;
        row.enabled = c.enabled;
        row.commands = c.commands.join(QStringLiteral(", "));
        row.group = c.group;
        row.permissions = permissionsText(c.permissions, config);
        row.globalCooldown = c.globalCooldownS > 0 ? QStringLiteral("%1 s").arg(c.globalCooldownS) : kNone;
        row.userCooldown = c.userCooldownS > 0 ? QStringLiteral("%1 s").arg(c.userCooldownS) : kNone;
        row.actions = linkedActionsText(config, kTriggerCommand, "command_id", c.id);
        row.used = kNone;
        if (variables) {
            const auto it = variables->commandCounters.constFind(c.id);
            if (it != variables->commandCounters.constEnd() && it->counter)
                row.used = variableValueText(it->counter->value);
        }
        row.warning = warningText(warnings, c.id);
        if (!matches(c.name, search) && !matches(row.commands, search) && !matches(c.group, search)
            && !matches(row.actions, search))
            continue;
        rows.append(row);
    }
    return rows;
}

QStringList commandGroupChoices(const ActionsConfig &config)
{
    QStringList groups;
    for (const Command &c : config.commands) {
        if (!c.group.isEmpty() && !groups.contains(c.group))
            groups.append(c.group);
    }
    std::sort(groups.begin(), groups.end(), nameLess);
    groups.prepend(QString());
    return groups;
}

QString commandGroupLabel(const QString &group)
{
    return group.isEmpty() ? QStringLiteral("Alle Gruppen") : group;
}

namespace {

// Gemeinsamer Satz der Zeile unter der Liste und des Hinweises in «Wer darf»
QString channelAlwaysAllowed()
{
    return QStringLiteral("Der Kanal darf immer");
}

} // namespace

QString commandListHint(const ActionsConfig &config)
{
    const qsizetype total = config.commands.size();
    const qsizetype off = std::count_if(config.commands.cbegin(), config.commands.cend(),
                                        [](const Command &c) { return !c.enabled; });
    QString s = total == 1 ? QStringLiteral("1 Befehl") : QStringLiteral("%1 Befehle").arg(total);
    if (off > 0)
        s += QStringLiteral(", %1 abgeschaltet").arg(off);
    return s + QStringLiteral(" · %1 und hat keine Abklingzeit.").arg(channelAlwaysAllowed());
}

CommandFormHints commandFormHints()
{
    CommandFormHints h;
    h.triggers = QStringLiteral("ein Auslöser pro Zeile");
    h.permissions = QStringLiteral("Mit «Alle» sind die anderen eingeschlossen. %1.").arg(channelAlwaysAllowed());
    h.cooldown = QStringLiteral("0 s = keine Abklingzeit. Abgewiesene Aufrufe stehen in «Läufe der Aktionen».");
    h.run = QStringLiteral("Die Aktion bekommt %rawInput%, %input0% …, %user%.");
    return h;
}

QString commandTitle(const Command &command)
{
    return QStringLiteral("Befehl %1").arg(quoted(command.commands.isEmpty() ? command.name : command.commands.first()));
}

QList<LocationChoice> commandLocations()
{
    return {{QStringLiteral("start"), QStringLiteral("am Anfang der Nachricht")},
            {QStringLiteral("exact"), QStringLiteral("genau die Nachricht")},
            {QStringLiteral("anywhere"), QStringLiteral("irgendwo in der Nachricht")}};
}

PermissionChoice permissionChoice(const Permissions &permissions)
{
    PermissionChoice c;
    c.subscriber = permissions.roles.contains(QStringLiteral("subscriber"));
    c.vip = permissions.roles.contains(QStringLiteral("vip"));
    c.moderator = permissions.roles.contains(QStringLiteral("moderator"));
    c.groups = permissions.groups;
    c.all = permissions.roles.isEmpty() && permissions.groups.isEmpty() && permissions.users.isEmpty();
    return c;
}

Permissions permissionsFromChoice(const Permissions &before, const PermissionChoice &choice)
{
    Permissions p = before;
    p.roles.clear();
    p.groups.clear();
    if (choice.all)
        return p;
    if (choice.subscriber)
        p.roles.append(QStringLiteral("subscriber"));
    if (choice.vip)
        p.roles.append(QStringLiteral("vip"));
    if (choice.moderator)
        p.roles.append(QStringLiteral("moderator"));
    for (const QUuid &g : choice.groups) {
        if (!p.groups.contains(g))
            p.groups.append(g);
    }
    return p;
}

QList<UserGroupChoice> userGroupChoices(const ActionsConfig &config, const Permissions &current)
{
    QList<UserGroup> groups = config.groups;
    std::stable_sort(groups.begin(), groups.end(),
                     [](const UserGroup &a, const UserGroup &b) { return nameLess(a.name, b.name); });
    QList<UserGroupChoice> choices;
    for (const UserGroup &g : groups)
        choices.append({g.id, g.name, false});
    for (const QUuid &id : current.groups) {
        if (!config.group(id))
            choices.append({id, QStringLiteral("Gruppe fehlt"), true});
    }
    return choices;
}

bool commandTestAllowed(const ActionsConfig &config, const QUuid &commandId, const ActionsStatus &status)
{
    return config.command(commandId) && testState(status);
}

EditResult testCommand(const ActionsConfig &config, const QUuid &commandId, const TestUserChoice &user, bool real)
{
    const Command *command = config.command(commandId);
    if (!command)
        return {};
    // Wie triggerdispatcher.cpp (handleChat): Argumente des Befehls ohne Zähler und ohne input0…
    QJsonObject arguments{{QStringLiteral("command"), command->commands.value(0)},
                          {QStringLiteral("commandId"), uuidText(command->id)},
                          {QStringLiteral("commandName"), command->name},
                          {QStringLiteral("rawInput"), QString()}};
    arguments.insert(userArgument("user_name"), user.user);
    arguments.insert(userArgument("user_login"), user.userName);
    arguments.insert(userArgument("user_id"), user.userId);
    EditResult r;
    for (const Action &action : config.actions) {
        if (!action.enabled)
            continue;
        const bool hit = std::any_of(action.triggers.cbegin(), action.triggers.cend(), [&](const Trigger &t) {
            return t.enabled && t.type == QLatin1String(kTriggerCommand)
                   && t.fields.value(QStringLiteral("command_id")).toString() == uuidText(commandId);
        });
        if (hit) {
            r.edits.append(edit(msg::kActionTest, {{QStringLiteral("action_id"), uuidText(action.id)},
                                                   {QStringLiteral("arguments"), arguments},
                                                   {QStringLiteral("dry_run"), !real}}));
        }
    }
    if (r.edits.isEmpty())
        return problem(QStringLiteral("Keine aktive Aktion führt diesen Befehl aus."));
    return r;
}

QString commandsText(const QStringList &commands)
{
    return commands.join(QLatin1Char('\n'));
}

QStringList commandsFromText(const QString &text)
{
    QStringList commands;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        const QString c = line.trimmed();
        if (!c.isEmpty())
            commands.append(c);
    }
    return commands;
}

EditResult newCommand(const ActionsConfig &config)
{
    QStringList names;
    for (const Command &c : config.commands)
        names.append(c.name);
    Command c;
    c.id = QUuid::createUuid();
    c.name = uniqueName(QStringLiteral("Neuer Befehl"), names);
    EditResult r;
    r.edits.append(edit("command_put", {{QStringLiteral("command"), commandToJson(c)}}));
    r.created = c.id;
    return r;
}

EditResult copyCommand(const ActionsConfig &config, const QUuid &commandId)
{
    const Command *source = config.command(commandId);
    if (!source)
        return {};
    QStringList names;
    for (const Command &c : config.commands)
        names.append(c.name);
    Command c = *source;
    c.id = QUuid::createUuid();
    c.name = copyName(source->name, names);
    EditResult r;
    r.edits.append(edit("command_put", {{QStringLiteral("command"), commandToJson(c)}}));
    r.created = c.id;
    return r;
}

EditResult removeCommand(const ActionsConfig &config, const QUuid &commandId)
{
    if (!config.command(commandId))
        return {};
    EditResult r;
    r.edits.append(edit("command_remove", {{QStringLiteral("command_id"), uuidText(commandId)}}));
    return r;
}

QString removeCommandQuestion(const ActionsConfig &config, const QUuid &commandId)
{
    const Command *command = config.command(commandId);
    const QString uses = command ? triggerUsesText(config, kTriggerCommand, "command_id", commandId) : QString();
    if (uses.isEmpty())
        return QString();
    return QStringLiteral("%1 löschen? Er wird von %2 benutzt.").arg(commandTitle(*command), uses);
}

EditResult putCommand(const Command &before, const Command &after)
{
    Command c = after;
    c.name = c.name.trimmed();
    c.group = c.group.trimmed();
    c.commands = commandsFromText(commandsText(c.commands));
    if (c.name.isEmpty())
        return problem(emptyNameProblem());
    if (c.globalCooldownS < 0 || c.userCooldownS < 0)
        return problem(QStringLiteral("Die Abklingzeit darf nicht negativ sein."));
    if (c.globalCooldownS > kMaxCooldownS || c.userCooldownS > kMaxCooldownS)
        return problem(QStringLiteral("Die Abklingzeit darf höchstens %1 s sein.").arg(kMaxCooldownS));
    const QJsonObject json = commandToJson(c);
    Command check;
    QString error;
    if (!commandFromJson(json, &check, &error))
        return problem(error);
    if (c == before)
        return {};
    EditResult r;
    r.edits.append(edit("command_put", {{QStringLiteral("command"), json}}));
    return r;
}

// --- Unterreiter «Zeitnachrichten» ---

QList<TimerRow> timerRows(const ActionsConfig &config, const QList<ReferenceWarning> &warnings)
{
    QList<TimerRow> rows;
    for (const Timer &t : config.timers) {
        TimerRow row;
        row.id = t.id;
        row.enabled = t.enabled;
        row.name = t.name;
        QString interval = t.random ? QStringLiteral("%1 bis %2 min, zufällig")
                                          .arg(minutesText(t.randomMinS), minutesText(t.randomMaxS))
                                    : QStringLiteral("%1 min").arg(minutesText(t.intervalS));
        row.interval = t.repeat ? interval : QStringLiteral("einmal nach %1").arg(interval);
        row.lines = t.lines > 0 ? QString::number(t.lines) : kNone;
        row.onlyLive = t.onlyLive ? QStringLiteral("ja") : QStringLiteral("nein");
        row.actions = linkedActionsText(config, kTriggerTimer, "timer_id", t.id);
        row.warning = warningText(warnings, t.id);
        rows.append(row);
    }
    return rows;
}

QString timerTitle(const Timer &timer)
{
    return QStringLiteral("Zeitnachricht %1").arg(quoted(timer.name));
}

QString minutesText(int seconds)
{
    QString text = QString::number(double(seconds) / 60.0, 'f', 1);
    if (text.endsWith(QLatin1String(".0")))
        text.chop(2);
    return text.replace(QLatin1Char('.'), QLatin1Char(','));
}

std::optional<int> secondsFromMinutes(const QString &text)
{
    bool ok = false;
    const double minutes = QString(text.trimmed()).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (!ok || !std::isfinite(minutes) || minutes < 0)
        return std::nullopt;
    const double seconds = std::round(minutes * 60.0);
    if (seconds > double(std::numeric_limits<int>::max()))
        return std::nullopt;
    return int(seconds);
}

void setTimerRandom(Timer &timer, bool random)
{
    timer.random = random;
    if (random && (timer.randomMinS < kMinTimerIntervalS || timer.randomMaxS < timer.randomMinS)) {
        timer.randomMinS = timer.intervalS;
        timer.randomMaxS = timer.intervalS;
    }
}

EditResult newTimer(const ActionsConfig &config)
{
    QStringList names;
    for (const Timer &t : config.timers)
        names.append(t.name);
    Timer t;
    t.id = QUuid::createUuid();
    t.name = uniqueName(QStringLiteral("Neue Zeitnachricht"), names);
    EditResult r;
    r.edits.append(edit("timer_put", {{QStringLiteral("timer"), timerToJson(t)}}));
    r.created = t.id;
    return r;
}

EditResult copyTimer(const ActionsConfig &config, const QUuid &timerId)
{
    const Timer *source = config.timer(timerId);
    if (!source)
        return {};
    QStringList names;
    for (const Timer &t : config.timers)
        names.append(t.name);
    Timer t = *source;
    t.id = QUuid::createUuid();
    t.name = copyName(source->name, names);
    EditResult r;
    r.edits.append(edit("timer_put", {{QStringLiteral("timer"), timerToJson(t)}}));
    r.created = t.id;
    return r;
}

EditResult removeTimer(const ActionsConfig &config, const QUuid &timerId)
{
    if (!config.timer(timerId))
        return {};
    EditResult r;
    r.edits.append(edit("timer_remove", {{QStringLiteral("timer_id"), uuidText(timerId)}}));
    return r;
}

QString removeTimerQuestion(const ActionsConfig &config, const QUuid &timerId)
{
    const Timer *timer = config.timer(timerId);
    const QString uses = timer ? triggerUsesText(config, kTriggerTimer, "timer_id", timerId) : QString();
    if (uses.isEmpty())
        return QString();
    return QStringLiteral("%1 löschen? Sie wird von %2 benutzt.").arg(timerTitle(*timer), uses);
}

EditResult putTimer(const Timer &before, const Timer &after)
{
    Timer t = after;
    t.name = t.name.trimmed();
    if (t.name.isEmpty())
        return problem(emptyNameProblem());
    // Abstände in Minuten wie im Feld (Entscheid des Nutzers, 2026-11-16)
    if (t.intervalS < kMinTimerIntervalS)
        return problem(QStringLiteral("Der Abstand muss mindestens %1 min sein.").arg(minutesText(kMinTimerIntervalS)));
    if (t.intervalS > kMaxTimerIntervalS)
        return problem(QStringLiteral("Der Abstand darf höchstens %1 min sein.").arg(minutesText(kMaxTimerIntervalS)));
    if (t.random && t.randomMinS < kMinTimerIntervalS)
        return problem(
            QStringLiteral("Der kürzeste Abstand muss mindestens %1 min sein.").arg(minutesText(kMinTimerIntervalS)));
    if (t.random && t.randomMinS > kMaxTimerIntervalS)
        return problem(
            QStringLiteral("Der kürzeste Abstand darf höchstens %1 min sein.").arg(minutesText(kMaxTimerIntervalS)));
    if (t.random && t.randomMaxS > kMaxTimerIntervalS)
        return problem(
            QStringLiteral("Der längste Abstand darf höchstens %1 min sein.").arg(minutesText(kMaxTimerIntervalS)));
    if (t.random && t.randomMaxS < t.randomMinS)
        return problem(QStringLiteral("Der längste Abstand darf nicht kürzer sein als der kürzeste."));
    if (t.lines < 0 || t.lines > kMaxTimerLines)
        return problem(QStringLiteral("Die Zahl der Chat-Zeilen muss zwischen 0 und %1 liegen.").arg(kMaxTimerLines));
    const QJsonObject json = timerToJson(t);
    Timer check;
    QString error;
    if (!timerFromJson(json, &check, &error))
        return problem(error);
    if (t == before)
        return {};
    EditResult r;
    r.edits.append(edit("timer_put", {{QStringLiteral("timer"), json}}));
    return r;
}

// --- Dialog «Warteschlangen» ---

namespace {

const QString kDefaultQueueName = QStringLiteral("Standard");

EditResult putQueue(const Queue &queue)
{
    EditResult r;
    r.edits.append(edit("queue_put", {{QStringLiteral("queue"), queueToJson(queue)}}));
    return r;
}

} // namespace

QList<QueueRow> queueRows(const ActionsConfig &config)
{
    QList<QueueRow> rows{{QUuid(), kDefaultQueueName, false, true, false, false}};
    QList<Queue> queues = config.queues;
    std::stable_sort(queues.begin(), queues.end(),
                     [](const Queue &a, const Queue &b) { return nameLess(a.name, b.name); });
    for (const Queue &q : queues)
        rows.append({q.id, q.name, q.blocking, false, true, true});
    return rows;
}

EditResult newQueue(const ActionsConfig &config)
{
    QStringList names{kDefaultQueueName};
    for (const Queue &q : config.queues)
        names.append(q.name);
    Queue q;
    q.id = QUuid::createUuid();
    for (int n = 1;; ++n) {
        q.name = QStringLiteral("Warteschlange %1").arg(n);
        if (!names.contains(q.name, Qt::CaseInsensitive))
            break;
    }
    EditResult r = putQueue(q);
    r.created = q.id;
    return r;
}

EditResult renameQueue(const ActionsConfig &config, const QUuid &queueId, const QString &name)
{
    const Queue *queue = queueId.isNull() ? nullptr : config.queue(queueId);
    if (!queue)
        return {};
    const QString newName = name.trimmed();
    if (newName.isEmpty())
        return problem(emptyNameProblem());
    if (sameGroup(newName, kDefaultQueueName))
        return problem(QStringLiteral("Die Warteschlange «%1» gibt es schon.").arg(kDefaultQueueName));
    for (const Queue &q : config.queues) {
        if (q.id != queueId && sameGroup(q.name, newName))
            return problem(QStringLiteral("Die Warteschlange «%1» gibt es schon.").arg(q.name));
    }
    if (queue->name == newName)
        return {};
    Queue after = *queue;
    after.name = newName;
    return putQueue(after);
}

EditResult setQueueBlocking(const ActionsConfig &config, const QUuid &queueId, bool blocking)
{
    const Queue *queue = queueId.isNull() ? nullptr : config.queue(queueId);
    if (!queue || queue->blocking == blocking)
        return {};
    Queue after = *queue;
    after.blocking = blocking;
    return putQueue(after);
}

EditResult removeQueue(const ActionsConfig &config, const QUuid &queueId)
{
    if (queueId.isNull() || !config.queue(queueId))
        return {};
    // Entscheid des Nutzers (16.11.): Aktionen der Warteschlange im selben Stapel auf «Standard».
    EditResult r;
    for (const Action &a : config.actions) {
        if (a.queue != queueId)
            continue;
        Action after = a;
        after.queue = QUuid();
        const EditResult put = putAction(a, after);
        if (!put.problem.isEmpty())
            return put;
        r.edits += put.edits;
    }
    r.edits.append(edit("queue_remove", {{QStringLiteral("queue_id"), uuidText(queueId)}}));
    return r;
}

QString removeQueueQuestion(const ActionsConfig &config, const QUuid &queueId)
{
    const Queue *queue = queueId.isNull() ? nullptr : config.queue(queueId);
    if (!queue)
        return QString();
    qsizetype n = 0;
    for (const Action &a : config.actions) {
        if (a.queue == queueId)
            ++n;
    }
    if (n == 0)
        return QString();
    return QStringLiteral("Warteschlange «%1» löschen? Sie wird von %2 benutzt. %3 danach in «%4».")
        .arg(queue->name, countText(n, QStringLiteral("Aktion"), QStringLiteral("Aktionen")),
             n == 1 ? QStringLiteral("Diese läuft") : QStringLiteral("Diese laufen"), kDefaultQueueName);
}

// --- Unterreiter «Variablen» ---

QString variableValueText(const QJsonValue &value)
{
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (std::isfinite(d) && d == std::floor(d) && std::abs(d) < 1e15)
            return QString::number(qint64(d));
        return QString::number(d, 'g', 15);
    }
    return value.toString();
}

QList<VariableRow> globalVariableRows(const VariablesData &data, const VolatileValues &volatileValues,
                                      const QString &search)
{
    QList<VariableRow> rows;
    for (auto it = data.global.cbegin(); it != data.global.cend(); ++it)
        rows.append(persistedRow(VariableScope::Global, QString(), it.key(), it.value()));
    for (auto it = volatileValues.cbegin(); it != volatileValues.cend(); ++it)
        rows.append(volatileRow(VariableScope::Global, QString(), it.key(), it.value()));
    rows.removeIf([&](const VariableRow &r) { return !matches(r.name, search) && !matches(r.valueText, search); });
    std::stable_sort(rows.begin(), rows.end(),
                     [](const VariableRow &a, const VariableRow &b) { return nameLess(a.name, b.name); });
    return rows;
}

QList<VariableRow> userVariableRows(const VariablesData &data, const VolatileUserValues &volatileValues,
                                    const ViewersData &viewers, const QString &search, const QString &nameFilter)
{
    QList<VariableRow> rows;
    for (auto u = data.users.cbegin(); u != data.users.cend(); ++u) {
        for (auto it = u->cbegin(); it != u->cend(); ++it)
            rows.append(persistedRow(VariableScope::User, u.key(), it.key(), it.value()));
    }
    for (auto u = volatileValues.cbegin(); u != volatileValues.cend(); ++u) {
        for (auto it = u->cbegin(); it != u->cend(); ++it)
            rows.append(volatileRow(VariableScope::User, u.key(), it.key(), it.value()));
    }
    for (VariableRow &r : rows)
        r.viewer = viewerLabel(viewers, r.userId);
    rows.removeIf([&](const VariableRow &r) {
        if (!nameFilter.isEmpty() && r.name != nameFilter)
            return true;
        const QString login = viewers.viewers.value(r.userId).login;
        return !matches(r.name, search) && !matches(r.valueText, search) && !matches(r.viewer, search)
               && !(!login.isEmpty() && matches(login, search));
    });
    std::stable_sort(rows.begin(), rows.end(), [](const VariableRow &a, const VariableRow &b) {
        if (a.viewer != b.viewer)
            return nameLess(a.viewer, b.viewer);
        if (a.userId != b.userId)
            return a.userId < b.userId;
        return nameLess(a.name, b.name);
    });
    return rows;
}

QStringList userVariableNames(const VariablesData &data, const VolatileUserValues &volatileValues)
{
    QSet<QString> names;
    for (const VariableMap &m : data.users) {
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            names.insert(it.key());
    }
    for (const VolatileValues &m : volatileValues) {
        for (auto it = m.cbegin(); it != m.cend(); ++it)
            names.insert(it.key());
    }
    QStringList list(names.cbegin(), names.cend());
    std::sort(list.begin(), list.end(), nameLess);
    list.prepend(QString());
    return list;
}

QString variableNameLabel(const QString &name)
{
    return name.isEmpty() ? QStringLiteral("Alle Variablen") : name;
}

EditResult setVariableValue(const VariableRow &row, const QString &text)
{
    QJsonValue value;
    if (row.value.isDouble()) {
        bool ok = false;
        const double d = text.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(d))
            return problem(QStringLiteral("«%1» ist keine Zahl.").arg(text));
        value = d;
    } else {
        value = text;
    }
    if (value == row.value)
        return {};
    QJsonObject fields = variableFields(row);
    fields.insert(QStringLiteral("value"), value);
    EditResult r;
    r.edits.append(edit("variable_set", fields));
    return r;
}

EditResult removeVariable(const VariablesData &data, const VolatileValues &volatileValues,
                          const VolatileUserValues &volatileUserValues, const VariableRow &row)
{
    const bool user = row.scope == VariableScope::User;
    const bool exists = row.persisted ? (user ? data.users.value(row.userId).contains(row.name)
                                              : data.global.contains(row.name))
                                      : (user ? volatileUserValues.value(row.userId).contains(row.name)
                                              : volatileValues.contains(row.name));
    if (!exists)
        return {};
    EditResult r;
    r.edits.append(edit("variable_remove", variableFields(row)));
    return r;
}

QString removeVariableQuestion(const VariableRow &row)
{
    if (row.scope == VariableScope::User)
        return QStringLiteral("Variable «%1» von «%2» löschen?").arg(row.name, row.viewer);
    return QStringLiteral("Variable «%1» löschen?").arg(row.name);
}

} // namespace dss
