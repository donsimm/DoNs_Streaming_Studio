// Quellen:
//   QJsonDocument, QJsonObject: https://doc.qt.io/qt-6.8/qjsondocument.html,
//   https://doc.qt.io/qt-6.8/qjsonobject.html

#include "actionjson.h"

#include "fieldspec.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <cmath>

namespace dss::actions {

namespace {

const QString kZeroUuid = QStringLiteral("00000000-0000-0000-0000-000000000000");

// Liest die Felder eines Objekts und merkt sich, welche bekannt sind; der Rest kommt in extra.
// Der erste Fehler bleibt in *error stehen.
class ObjectReader
{
public:
    ObjectReader(const QJsonObject &object, const QString &path, QString *error)
        : m_object(object), m_path(path), m_error(error)
    {
    }

    bool fail(const QString &key, const QString &message)
    {
        *m_error = QStringLiteral("Feld «%1%2»: %3").arg(m_path, key, message);
        return false;
    }

    // Pflichtfeld vorhanden; merkt es als bekannt
    bool has(const QString &key)
    {
        m_known.insert(key);
        if (!m_object.contains(key))
            return fail(key, QStringLiteral("fehlt"));
        return true;
    }

    QJsonValue value(const QString &key) const { return m_object.value(key); }

    // Feld ohne Pflicht; merkt es als bekannt. undefined, wenn es fehlt
    QJsonValue optional(const QString &key)
    {
        m_known.insert(key);
        return value(key);
    }

    bool boolean(const QString &key, bool *out)
    {
        if (!has(key))
            return false;
        if (!value(key).isBool())
            return fail(key, QStringLiteral("muss true oder false sein"));
        *out = value(key).toBool();
        return true;
    }

    bool integer(const QString &key, int min, int max, int *out)
    {
        if (!has(key))
            return false;
        const QJsonValue v = value(key);
        if (!v.isDouble() || std::trunc(v.toDouble()) != v.toDouble() || v.toDouble() < min
            || v.toDouble() > max)
            return fail(key, max == std::numeric_limits<int>::max()
                                 ? QStringLiteral("muss eine ganze Zahl ab %1 sein").arg(min)
                                 : QStringLiteral("muss eine ganze Zahl von %1 bis %2 sein").arg(min).arg(max));
        *out = int(v.toDouble());
        return true;
    }

    bool text(const QString &key, QString *out)
    {
        if (!has(key))
            return false;
        if (!value(key).isString())
            return fail(key, QStringLiteral("muss Text sein"));
        *out = value(key).toString();
        return true;
    }

    // Text, der nicht leer und nicht nur Leerzeichen ist
    bool name(const QString &key, QString *out)
    {
        if (!text(key, out))
            return false;
        if (out->trimmed().isEmpty())
            return fail(key, QStringLiteral("darf nicht leer sein"));
        return true;
    }

    bool oneOf(const QString &key, const QStringList &values, QString *out)
    {
        if (!text(key, out))
            return false;
        if (!values.contains(*out))
            return fail(key, QStringLiteral("muss einer der Werte %1 sein").arg(values.join(QStringLiteral(", "))));
        return true;
    }

    bool uuid(const QString &key, QUuid *out)
    {
        if (!has(key))
            return false;
        if (!parseUuidText(value(key).toString(), out))
            return fail(key, QStringLiteral("muss eine UUID in der Form xxxxxxxx-xxxx-… sein"));
        return true;
    }

    // UUID oder null; mit zeroIsNull auch die UUID aus lauter Nullen (Warteschlange «Standard»)
    bool nullableUuid(const QString &key, QUuid *out, bool zeroIsNull = false)
    {
        if (!has(key))
            return false;
        const QJsonValue v = value(key);
        if (v.isNull() || (zeroIsNull && v.toString() == kZeroUuid)) {
            *out = QUuid();
            return true;
        }
        if (!parseUuidText(v.toString(), out))
            return fail(key, QStringLiteral("muss eine UUID in der Form xxxxxxxx-xxxx-… oder null sein"));
        return true;
    }

    bool array(const QString &key, QJsonArray *out)
    {
        if (!has(key))
            return false;
        if (!value(key).isArray())
            return fail(key, QStringLiteral("muss eine Liste sein"));
        *out = value(key).toArray();
        return true;
    }

    // Liste von Texten ohne leere Einträge; mit allowed nur diese Werte, ohne Doppelte
    bool stringList(const QString &key, QStringList *out, const QStringList &allowed = {})
    {
        QJsonArray a;
        if (!array(key, &a))
            return false;
        out->clear();
        for (const QJsonValue &v : a) {
            if (!v.isString() || v.toString().trimmed().isEmpty())
                return fail(key, QStringLiteral("muss eine Liste von Texten (nicht leer) sein"));
            if (!allowed.isEmpty() && !allowed.contains(v.toString()))
                return fail(key, QStringLiteral("erlaubt sind nur %1").arg(allowed.join(QStringLiteral(", "))));
            if (!allowed.isEmpty() && out->contains(v.toString()))
                return fail(key, QStringLiteral("«%1» kommt mehrfach vor").arg(v.toString()));
            out->append(v.toString());
        }
        return true;
    }

    QJsonObject extra() const
    {
        QJsonObject extra;
        for (auto it = m_object.begin(); it != m_object.end(); ++it) {
            if (!m_known.contains(it.key()))
                extra.insert(it.key(), it.value());
        }
        return extra;
    }

    const QString &path() const { return m_path; }

private:
    QJsonObject m_object;
    QString m_path;
    QString *m_error;
    QSet<QString> m_known;
};

// Kennungen, die in der ganzen Datei eindeutig sein müssen (je Art)
struct IdSets
{
    QSet<QUuid> steps;
    QSet<QUuid> triggers;
};

bool objectAt(const QJsonArray &array, qsizetype i, const QString &path, QJsonObject *out, QString *error)
{
    if (!array.at(i).isObject()) {
        *error = QStringLiteral("Feld «%1»: muss ein Objekt sein").arg(path);
        return false;
    }
    *out = array.at(i).toObject();
    return true;
}

QJsonObject withExtra(const QJsonObject &extra, std::initializer_list<std::pair<QString, QJsonValue>> fields)
{
    QJsonObject o = extra;
    for (const auto &[key, value] : fields)
        o.insert(key, value);
    return o;
}

QJsonValue nullableId(const QUuid &id)
{
    return id.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(uuidText(id));
}

// --- Schritte ---

bool readSteps(const QJsonArray &array, const QString &path, const QString &parentType, int depth,
               IdSets *ids, QList<Step> *out, QString *error);

bool readStep(const QJsonObject &o, const QString &path, const QString &parentType, int depth,
              IdSets *ids, Step *step, QString *error)
{
    ObjectReader r(o, path, error);
    if (!r.uuid(QStringLiteral("id"), &step->id) || !r.text(QStringLiteral("type"), &step->type)
        || !r.boolean(QStringLiteral("enabled"), &step->enabled))
        return false;
    if (ids->steps.contains(step->id))
        return r.fail(QStringLiteral("id"), QStringLiteral("Kennung des Schritts kommt mehrfach vor"));
    ids->steps.insert(step->id);
    const StepTypeInfo *info = stepType(step->type);
    if (!info)
        return r.fail(QStringLiteral("type"), QStringLiteral("unbekannter Typ «%1»").arg(step->type));

    const bool branch = step->type == QLatin1String(kStepIfTrue) || step->type == QLatin1String(kStepIfFalse);
    if (branch != (parentType == QLatin1String(kStepIf)))
        return r.fail(QStringLiteral("type"),
                      branch ? QStringLiteral("«%1» nur direkt in «if»").arg(step->type)
                             : QStringLiteral("in «if» stehen nur «if_true» und «if_false»"));
    if (step->type == QLatin1String(kStepUnsupported) && step->enabled)
        return r.fail(QStringLiteral("enabled"), QStringLiteral("ein nicht unterstützter Schritt ist immer aus"));

    // weight ist kein Pflichtfeld (Plan 2.2, Beispiel ohne weight)
    const QJsonValue weight = r.optional(QStringLiteral("weight"));
    if (!weight.isUndefined()) {
        if (!weight.isDouble() || weight.toDouble() < 0)
            return r.fail(QStringLiteral("weight"), QStringLiteral("muss eine Zahl ab 0 sein"));
        step->weight = weight.toDouble();
    }

    for (const FieldSpec &f : info->fields) {
        if (!r.has(f.name))
            return false;
        const QString problem = checkFieldValue(f, r.value(f.name));
        if (!problem.isEmpty())
            return r.fail(f.name, problem);
        step->fields.insert(f.name, r.value(f.name));
    }

    if (info->hasSteps) {
        if (depth >= kMaxStepDepth)
            return r.fail(QStringLiteral("steps"),
                          QStringLiteral("höchstens %1 Ebenen verschachtelt").arg(kMaxStepDepth));
        QJsonArray children;
        if (!r.array(QStringLiteral("steps"), &children)
            || !readSteps(children, path + QStringLiteral("steps"), step->type, depth + 1, ids,
                          &step->steps, error))
            return false;
        if (step->type == QLatin1String(kStepIf)
            && (step->steps.size() != 2 || step->steps.at(0).type != QLatin1String(kStepIfTrue)
                || step->steps.at(1).type != QLatin1String(kStepIfFalse)))
            return r.fail(QStringLiteral("steps"),
                          QStringLiteral("«if» hat genau die Schritte «if_true» und «if_false» in dieser Reihenfolge"));
    } else if (o.contains(QStringLiteral("steps"))) {
        return r.fail(QStringLiteral("steps"),
                      QStringLiteral("nur bei «group», «if», «if_true» und «if_false»"));
    }
    step->extra = r.extra();
    return true;
}

bool readSteps(const QJsonArray &array, const QString &path, const QString &parentType, int depth,
               IdSets *ids, QList<Step> *out, QString *error)
{
    out->clear();
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QString itemPath = QStringLiteral("%1[%2]").arg(path).arg(i);
        QJsonObject o;
        Step step;
        if (!objectAt(array, i, itemPath, &o, error)
            || !readStep(o, itemPath + QLatin1Char('.'), parentType, depth, ids, &step, error))
            return false;
        out->append(step);
    }
    return true;
}

QJsonArray stepsToJson(const QList<Step> &steps);

QJsonObject stepToJson(const Step &step)
{
    QJsonObject o = step.extra;
    for (auto it = step.fields.begin(); it != step.fields.end(); ++it)
        o.insert(it.key(), it.value());
    o.insert(QStringLiteral("id"), uuidText(step.id));
    o.insert(QStringLiteral("type"), step.type);
    o.insert(QStringLiteral("enabled"), step.enabled);
    o.insert(QStringLiteral("weight"), step.weight);
    const StepTypeInfo *info = stepType(step.type);
    if (info && info->hasSteps)
        o.insert(QStringLiteral("steps"), stepsToJson(step.steps));
    return o;
}

QJsonArray stepsToJson(const QList<Step> &steps)
{
    QJsonArray a;
    for (const Step &s : steps)
        a.append(stepToJson(s));
    return a;
}

// --- Auslöser ---

bool readTrigger(const QJsonObject &o, const QString &path, IdSets *ids, Trigger *trigger, QString *error)
{
    ObjectReader r(o, path, error);
    if (!r.uuid(QStringLiteral("id"), &trigger->id) || !r.text(QStringLiteral("type"), &trigger->type)
        || !r.boolean(QStringLiteral("enabled"), &trigger->enabled))
        return false;
    if (ids->triggers.contains(trigger->id))
        return r.fail(QStringLiteral("id"), QStringLiteral("Kennung des Auslösers kommt mehrfach vor"));
    ids->triggers.insert(trigger->id);
    const TriggerTypeInfo *info = triggerType(trigger->type);
    if (!info)
        return r.fail(QStringLiteral("type"), QStringLiteral("unbekannter Typ «%1»").arg(trigger->type));
    for (const FieldSpec &f : info->filters) {
        if (!r.has(f.name))
            return false;
        const QString problem = checkFieldValue(f, r.value(f.name));
        if (!problem.isEmpty())
            return r.fail(f.name, problem);
        trigger->fields.insert(f.name, r.value(f.name));
    }
    trigger->extra = r.extra();
    return true;
}

QJsonObject triggerToJson(const Trigger &trigger)
{
    QJsonObject o = trigger.extra;
    for (auto it = trigger.fields.begin(); it != trigger.fields.end(); ++it)
        o.insert(it.key(), it.value());
    o.insert(QStringLiteral("id"), uuidText(trigger.id));
    o.insert(QStringLiteral("type"), trigger.type);
    o.insert(QStringLiteral("enabled"), trigger.enabled);
    return o;
}

// --- Aktion ---

bool readAction(const QJsonObject &o, const QString &path, IdSets *ids, Action *action, QString *error)
{
    ObjectReader r(o, path, error);
    QJsonArray triggers;
    QJsonArray steps;
    if (!r.uuid(QStringLiteral("id"), &action->id) || !r.name(QStringLiteral("name"), &action->name)
        || !r.text(QStringLiteral("group"), &action->group)
        || !r.boolean(QStringLiteral("enabled"), &action->enabled)
        || !r.nullableUuid(QStringLiteral("queue"), &action->queue, true)
        || !r.boolean(QStringLiteral("exclude_from_history"), &action->excludeFromHistory)
        || !r.boolean(QStringLiteral("random_action"), &action->randomAction)
        || !r.boolean(QStringLiteral("concurrent"), &action->concurrent)
        || !r.boolean(QStringLiteral("always_run"), &action->alwaysRun)
        || !r.nullableUuid(QStringLiteral("template_id"), &action->templateId)
        || !r.array(QStringLiteral("triggers"), &triggers) || !r.array(QStringLiteral("steps"), &steps))
        return false;
    action->triggers.clear();
    for (qsizetype i = 0; i < triggers.size(); ++i) {
        const QString itemPath = QStringLiteral("%1triggers[%2]").arg(path).arg(i);
        QJsonObject t;
        Trigger trigger;
        if (!objectAt(triggers, i, itemPath, &t, error)
            || !readTrigger(t, itemPath + QLatin1Char('.'), ids, &trigger, error))
            return false;
        action->triggers.append(trigger);
    }
    if (!readSteps(steps, path + QStringLiteral("steps"), QString(), 0, ids, &action->steps, error))
        return false;
    action->extra = r.extra();
    return true;
}

QJsonObject actionToJsonImpl(const Action &a)
{
    QJsonArray triggers;
    for (const Trigger &t : a.triggers)
        triggers.append(triggerToJson(t));
    return withExtra(a.extra, {{QStringLiteral("id"), uuidText(a.id)},
                               {QStringLiteral("name"), a.name},
                               {QStringLiteral("group"), a.group},
                               {QStringLiteral("enabled"), a.enabled},
                               {QStringLiteral("queue"), nullableId(a.queue)},
                               {QStringLiteral("exclude_from_history"), a.excludeFromHistory},
                               {QStringLiteral("random_action"), a.randomAction},
                               {QStringLiteral("concurrent"), a.concurrent},
                               {QStringLiteral("always_run"), a.alwaysRun},
                               {QStringLiteral("template_id"), nullableId(a.templateId)},
                               {QStringLiteral("triggers"), triggers},
                               {QStringLiteral("steps"), stepsToJson(a.steps)}});
}

// --- Benutzer, Rechte, Gruppen ---

bool readUsers(ObjectReader &r, const QString &key, QList<UserRef> *out, QString *error)
{
    QJsonArray users;
    if (!r.array(key, &users))
        return false;
    out->clear();
    QSet<QString> seen;
    for (qsizetype i = 0; i < users.size(); ++i) {
        const QString itemPath = QStringLiteral("%1%2[%3]").arg(r.path(), key).arg(i);
        QJsonObject o;
        if (!objectAt(users, i, itemPath, &o, error))
            return false;
        ObjectReader u(o, itemPath + QLatin1Char('.'), error);
        UserRef ref;
        if (!u.name(QStringLiteral("user_id"), &ref.userId))
            return false;
        if (seen.contains(ref.userId))
            return u.fail(QStringLiteral("user_id"), QStringLiteral("Zuschauer kommt mehrfach vor"));
        seen.insert(ref.userId);
        ref.extra = u.extra();
        out->append(ref);
    }
    return true;
}

QJsonArray usersToJson(const QList<UserRef> &users)
{
    QJsonArray a;
    for (const UserRef &u : users)
        a.append(withExtra(u.extra, {{QStringLiteral("user_id"), u.userId}}));
    return a;
}

bool readPermissions(ObjectReader &parent, Permissions *p, QString *error)
{
    const QString key = QStringLiteral("permissions");
    if (!parent.has(key))
        return false;
    if (!parent.value(key).isObject())
        return parent.fail(key, QStringLiteral("muss ein Objekt sein"));
    ObjectReader r(parent.value(key).toObject(), parent.path() + key + QLatin1Char('.'), error);
    QJsonArray groups;
    if (!r.oneOf(QStringLiteral("grant"), {QStringLiteral("allow"), QStringLiteral("deny")}, &p->grant)
        || !r.stringList(QStringLiteral("roles"), &p->roles,
                         {QStringLiteral("subscriber"), QStringLiteral("vip"), QStringLiteral("moderator")})
        || !r.array(QStringLiteral("groups"), &groups))
        return false;
    p->groups.clear();
    for (qsizetype i = 0; i < groups.size(); ++i) {
        QUuid id;
        if (!parseUuidText(groups.at(i).toString(), &id))
            return r.fail(QStringLiteral("groups[%1]").arg(i), QStringLiteral("muss die UUID einer Benutzergruppe sein"));
        if (p->groups.contains(id))
            return r.fail(QStringLiteral("groups[%1]").arg(i), QStringLiteral("Gruppe kommt mehrfach vor"));
        p->groups.append(id);
    }
    if (!readUsers(r, QStringLiteral("users"), &p->users, error))
        return false;
    p->extra = r.extra();
    return true;
}

QJsonObject permissionsToJson(const Permissions &p)
{
    QJsonArray groups;
    for (const QUuid &id : p.groups)
        groups.append(uuidText(id));
    return withExtra(p.extra, {{QStringLiteral("grant"), p.grant},
                               {QStringLiteral("roles"), QJsonArray::fromStringList(p.roles)},
                               {QStringLiteral("groups"), groups},
                               {QStringLiteral("users"), usersToJson(p.users)}});
}

bool readCommand(const QJsonObject &o, const QString &path, Command *c, QString *error)
{
    ObjectReader r(o, path, error);
    if (!r.uuid(QStringLiteral("id"), &c->id) || !r.name(QStringLiteral("name"), &c->name)
        || !r.boolean(QStringLiteral("enabled"), &c->enabled) || !r.text(QStringLiteral("group"), &c->group)
        || !r.stringList(QStringLiteral("commands"), &c->commands)
        || !r.oneOf(QStringLiteral("mode"), {QStringLiteral("basic"), QStringLiteral("regex")}, &c->mode)
        || !r.oneOf(QStringLiteral("location"),
                    {QStringLiteral("start"), QStringLiteral("exact"), QStringLiteral("anywhere")}, &c->location)
        || !r.boolean(QStringLiteral("case_sensitive"), &c->caseSensitive)
        || !r.boolean(QStringLiteral("ignore_bot_account"), &c->ignoreBotAccount)
        || !r.boolean(QStringLiteral("ignore_internal"), &c->ignoreInternal)
        || !r.stringList(QStringLiteral("sources"), &c->sources, {QStringLiteral("twitch_message")})
        || !r.integer(QStringLiteral("global_cooldown_s"), 0, std::numeric_limits<int>::max(), &c->globalCooldownS)
        || !r.integer(QStringLiteral("user_cooldown_s"), 0, std::numeric_limits<int>::max(), &c->userCooldownS)
        || !r.boolean(QStringLiteral("persist_counter"), &c->persistCounter)
        || !r.boolean(QStringLiteral("persist_user_counter"), &c->persistUserCounter)
        || !readPermissions(r, &c->permissions, error))
        return false;
    c->extra = r.extra();
    return true;
}

bool readTimer(const QJsonObject &o, const QString &path, Timer *t, QString *error)
{
    ObjectReader r(o, path, error);
    constexpr int kMax = std::numeric_limits<int>::max();
    if (!r.uuid(QStringLiteral("id"), &t->id) || !r.name(QStringLiteral("name"), &t->name)
        || !r.boolean(QStringLiteral("enabled"), &t->enabled) || !r.boolean(QStringLiteral("repeat"), &t->repeat)
        || !r.integer(QStringLiteral("interval_s"), kMinTimerIntervalS, kMax, &t->intervalS)
        || !r.boolean(QStringLiteral("random"), &t->random)
        || !r.integer(QStringLiteral("random_min_s"), 0, kMax, &t->randomMinS)
        || !r.integer(QStringLiteral("random_max_s"), 0, kMax, &t->randomMaxS)
        || !r.integer(QStringLiteral("lines"), 0, kMax, &t->lines)
        || !r.boolean(QStringLiteral("only_live"), &t->onlyLive))
        return false;
    if (t->random && t->randomMinS < kMinTimerIntervalS)
        return r.fail(QStringLiteral("random_min_s"),
                      QStringLiteral("muss bei Zufall mindestens %1 sein").arg(kMinTimerIntervalS));
    if (t->random && t->randomMaxS < t->randomMinS)
        return r.fail(QStringLiteral("random_max_s"), QStringLiteral("muss bei Zufall mindestens random_min_s sein"));
    t->extra = r.extra();
    return true;
}

bool readQueue(const QJsonObject &o, const QString &path, Queue *q, QString *error)
{
    ObjectReader r(o, path, error);
    if (!r.uuid(QStringLiteral("id"), &q->id) || !r.name(QStringLiteral("name"), &q->name)
        || !r.boolean(QStringLiteral("blocking"), &q->blocking))
        return false;
    q->extra = r.extra();
    return true;
}

bool readUserGroup(const QJsonObject &o, const QString &path, UserGroup *g, QString *error)
{
    ObjectReader r(o, path, error);
    if (!r.uuid(QStringLiteral("id"), &g->id) || !r.name(QStringLiteral("name"), &g->name)
        || !readUsers(r, QStringLiteral("users"), &g->users, error))
        return false;
    g->extra = r.extra();
    return true;
}

// Liste mit eindeutigen Kennungen lesen
template <typename T, typename ReadFn>
bool readList(ObjectReader &r, const QString &key, QList<T> *out, ReadFn read, QString *error)
{
    QJsonArray array;
    if (!r.array(key, &array))
        return false;
    out->clear();
    QSet<QUuid> ids;
    for (qsizetype i = 0; i < array.size(); ++i) {
        const QString itemPath = QStringLiteral("%1[%2]").arg(key).arg(i);
        QJsonObject o;
        T item;
        if (!objectAt(array, i, itemPath, &o, error) || !read(o, itemPath + QLatin1Char('.'), &item, error))
            return false;
        if (ids.contains(item.id)) {
            *error = QStringLiteral("Feld «%1.id»: Kennung kommt mehrfach vor").arg(itemPath);
            return false;
        }
        ids.insert(item.id);
        out->append(item);
    }
    return true;
}

template <typename T, typename ToJson>
QJsonArray listToJson(const QList<T> &list, ToJson toJson)
{
    QJsonArray a;
    for (const T &item : list)
        a.append(toJson(item));
    return a;
}

bool parseObject(const QByteArray &data, QJsonObject *object, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (!doc.isObject()) {
        *error = doc.isNull() ? parseError.errorString() : QStringLiteral("muss ein JSON-Objekt sein");
        return false;
    }
    *object = doc.object();
    return true;
}

} // namespace

QJsonObject actionToJson(const Action &action)
{
    return actionToJsonImpl(action);
}

bool actionFromJson(const QJsonObject &object, Action *action, QString *error)
{
    IdSets ids;
    Action result;
    if (!readAction(object, QString(), &ids, &result, error))
        return false;
    *action = result;
    return true;
}

QJsonObject commandToJson(const Command &c)
{
    return withExtra(c.extra, {{QStringLiteral("id"), uuidText(c.id)},
                               {QStringLiteral("name"), c.name},
                               {QStringLiteral("enabled"), c.enabled},
                               {QStringLiteral("group"), c.group},
                               {QStringLiteral("commands"), QJsonArray::fromStringList(c.commands)},
                               {QStringLiteral("mode"), c.mode},
                               {QStringLiteral("location"), c.location},
                               {QStringLiteral("case_sensitive"), c.caseSensitive},
                               {QStringLiteral("ignore_bot_account"), c.ignoreBotAccount},
                               {QStringLiteral("ignore_internal"), c.ignoreInternal},
                               {QStringLiteral("sources"), QJsonArray::fromStringList(c.sources)},
                               {QStringLiteral("global_cooldown_s"), c.globalCooldownS},
                               {QStringLiteral("user_cooldown_s"), c.userCooldownS},
                               {QStringLiteral("persist_counter"), c.persistCounter},
                               {QStringLiteral("persist_user_counter"), c.persistUserCounter},
                               {QStringLiteral("permissions"), permissionsToJson(c.permissions)}});
}

bool commandFromJson(const QJsonObject &object, Command *command, QString *error)
{
    Command result;
    if (!readCommand(object, QString(), &result, error))
        return false;
    *command = result;
    return true;
}

QJsonObject timerToJson(const Timer &t)
{
    return withExtra(t.extra, {{QStringLiteral("id"), uuidText(t.id)},
                               {QStringLiteral("name"), t.name},
                               {QStringLiteral("enabled"), t.enabled},
                               {QStringLiteral("repeat"), t.repeat},
                               {QStringLiteral("interval_s"), t.intervalS},
                               {QStringLiteral("random"), t.random},
                               {QStringLiteral("random_min_s"), t.randomMinS},
                               {QStringLiteral("random_max_s"), t.randomMaxS},
                               {QStringLiteral("lines"), t.lines},
                               {QStringLiteral("only_live"), t.onlyLive}});
}

bool timerFromJson(const QJsonObject &object, Timer *timer, QString *error)
{
    Timer result;
    if (!readTimer(object, QString(), &result, error))
        return false;
    *timer = result;
    return true;
}

QJsonObject queueToJson(const Queue &q)
{
    return withExtra(q.extra, {{QStringLiteral("id"), uuidText(q.id)},
                               {QStringLiteral("name"), q.name},
                               {QStringLiteral("blocking"), q.blocking}});
}

bool queueFromJson(const QJsonObject &object, Queue *queue, QString *error)
{
    Queue result;
    if (!readQueue(object, QString(), &result, error))
        return false;
    *queue = result;
    return true;
}

QJsonObject userGroupToJson(const UserGroup &g)
{
    return withExtra(g.extra, {{QStringLiteral("id"), uuidText(g.id)},
                               {QStringLiteral("name"), g.name},
                               {QStringLiteral("users"), usersToJson(g.users)}});
}

bool userGroupFromJson(const QJsonObject &object, UserGroup *group, QString *error)
{
    UserGroup result;
    if (!readUserGroup(object, QString(), &result, error))
        return false;
    *group = result;
    return true;
}

QJsonObject actionsToJson(const ActionsConfig &config)
{
    return withExtra(config.extra,
                     {{QStringLiteral("format_version"), kActionsFormatVersion},
                      {QStringLiteral("enabled"), config.enabled},
                      {QStringLiteral("queues"), listToJson(config.queues, queueToJson)},
                      {QStringLiteral("groups"), listToJson(config.groups, userGroupToJson)},
                      {QStringLiteral("actions"), listToJson(config.actions, actionToJsonImpl)},
                      {QStringLiteral("commands"), listToJson(config.commands, commandToJson)},
                      {QStringLiteral("timers"), listToJson(config.timers, timerToJson)}});
}

QByteArray serializeActions(const ActionsConfig &config)
{
    return QJsonDocument(actionsToJson(config)).toJson(QJsonDocument::Indented);
}

bool actionsFromJson(const QJsonObject &object, ActionsConfig *config, QString *error)
{
    ObjectReader r(object, QString(), error);
    int version = 0;
    if (!r.has(QStringLiteral("format_version")))
        return false;
    const QJsonValue v = r.value(QStringLiteral("format_version"));
    if (!v.isDouble() || std::trunc(v.toDouble()) != v.toDouble())
        return r.fail(QStringLiteral("format_version"), QStringLiteral("muss eine ganze Zahl sein"));
    version = int(v.toDouble());
    if (version != kActionsFormatVersion) {
        *error = QStringLiteral("format_version %1 ist unbekannt, erwartet %2.").arg(version).arg(kActionsFormatVersion);
        return false;
    }

    ActionsConfig result;
    IdSets ids;
    const auto readActionItem = [&ids](const QJsonObject &o, const QString &path, Action *a, QString *e) {
        return readAction(o, path, &ids, a, e);
    };
    if (!r.boolean(QStringLiteral("enabled"), &result.enabled)
        || !readList(r, QStringLiteral("queues"), &result.queues, readQueue, error)
        || !readList(r, QStringLiteral("groups"), &result.groups, readUserGroup, error)
        || !readList(r, QStringLiteral("actions"), &result.actions, readActionItem, error)
        || !readList(r, QStringLiteral("commands"), &result.commands, readCommand, error)
        || !readList(r, QStringLiteral("timers"), &result.timers, readTimer, error))
        return false;
    result.extra = r.extra();
    *config = result;
    return true;
}

bool parseActions(const QByteArray &data, ActionsConfig *config, QString *error)
{
    QJsonObject object;
    return parseObject(data, &object, error) && actionsFromJson(object, config, error);
}

} // namespace dss::actions
