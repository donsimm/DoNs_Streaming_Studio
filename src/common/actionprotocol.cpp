#include "actionprotocol.h"

#include "actionjson.h"
#include "protocol.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace dss {

namespace {

enum class Kind { Bool, Object, Uuid, NullableUuid, String, NullableString, Int, Value, Array };

struct FieldRule
{
    const char *name;
    Kind kind;
    bool optional = false;
};

struct RequestRule
{
    const char *type;
    QList<FieldRule> fields;
};

const QList<RequestRule> &requestRules()
{
    static const QList<RequestRule> rules = {
        {msg::kActionsSetEnabled, {{"enabled", Kind::Bool}}},
        {msg::kActionPut, {{"action", Kind::Object}}},
        {msg::kActionRemove, {{"action_id", Kind::Uuid}}},
        {msg::kActionCopy, {{"action_id", Kind::Uuid}}},
        {msg::kActionReset, {{"action_id", Kind::Uuid}}},
        {msg::kCommandPut, {{"command", Kind::Object}}},
        {msg::kCommandRemove, {{"command_id", Kind::Uuid}}},
        {msg::kTimerPut, {{"timer", Kind::Object}}},
        {msg::kTimerRemove, {{"timer_id", Kind::Uuid}}},
        {msg::kQueuePut, {{"queue", Kind::Object}}},
        {msg::kQueueRemove, {{"queue_id", Kind::Uuid}}},
        {msg::kQueueControl, {{"queue_id", Kind::NullableUuid}, {"control", Kind::String}}},
        {msg::kGroupPut, {{"group", Kind::Object}}},
        {msg::kGroupRemove, {{"group_id", Kind::Uuid}}},
        {msg::kVariableSet,
         {{"scope", Kind::String}, {"user_id", Kind::String, true}, {"name", Kind::String},
          {"persisted", Kind::Bool}, {"value", Kind::Value}}},
        {msg::kVariableRemove,
         {{"scope", Kind::String}, {"user_id", Kind::String, true}, {"name", Kind::String},
          {"persisted", Kind::Bool}}},
        {msg::kQuotePut, {{"index", Kind::Int}, {"quote", Kind::Object}}},
        {msg::kQuoteRemove, {{"index", Kind::Int}}},
        {msg::kActionTest, {{"action_id", Kind::Uuid}, {"arguments", Kind::Object}, {"dry_run", Kind::Bool}}},
        {msg::kActionRunCancel, {{"run_id", Kind::Uuid}}},
        {msg::kChatSend, {{"text", Kind::String}, {"account", Kind::String}, {"reply_to", Kind::NullableString}}},
        {msg::kActionRunDetails, {{"run_id", Kind::Uuid}}},
        {msg::kGetActionRuns, {}},
        {msg::kGetVariables, {}},
        {msg::kGetViewers, {{"user_ids", Kind::Array}}},
        {msg::kGetQuotes, {}},
        {msg::kViewerImport, {{"path", Kind::String}, {"dry_run", Kind::Bool}, {"overwrite", Kind::Bool}}},
        {msg::kTestEvent, {{"event", Kind::Object}}},
    };
    return rules;
}

const RequestRule *requestRule(const QString &type)
{
    for (const RequestRule &rule : requestRules()) {
        if (type == QLatin1String(rule.type))
            return &rule;
    }
    return nullptr;
}

QJsonObject typed(const char *type)
{
    return QJsonObject{{QStringLiteral("type"), QString::fromLatin1(type)}};
}

QJsonValue uuidValue(const QUuid &id)
{
    return id.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(id.toString(QUuid::WithoutBraces));
}

QJsonValue timeValue(const QDateTime &time)
{
    return time.isValid() ? QJsonValue(time.toUTC().toString(Qt::ISODateWithMs)) : QJsonValue(QJsonValue::Null);
}

QDateTime timeFrom(const QJsonValue &value)
{
    return value.isString() ? QDateTime::fromString(value.toString(), Qt::ISODateWithMs) : QDateTime();
}

bool requireObject(const QJsonObject &message, const QString &name, QJsonObject *object, QString *error)
{
    const QJsonValue v = message.value(name);
    if (!v.isObject()) {
        *error = QStringLiteral("Feld %1 fehlt oder ist kein Objekt").arg(name);
        return false;
    }
    *object = v.toObject();
    return true;
}

bool requireArray(const QJsonObject &message, const QString &name, QJsonArray *array, QString *error)
{
    const QJsonValue v = message.value(name);
    if (!v.isArray()) {
        *error = QStringLiteral("Feld %1 fehlt oder ist keine Liste").arg(name);
        return false;
    }
    *array = v.toArray();
    return true;
}

std::optional<actions::RunState> runStateFrom(const QString &key)
{
    using actions::RunState;
    for (RunState s : {RunState::Waiting, RunState::Running, RunState::Ok, RunState::Break, RunState::Error,
                       RunState::Rejected, RunState::Muted, RunState::Dropped, RunState::Limit,
                       RunState::Cancelled}) {
        if (actions::runStateKey(s) == key)
            return s;
    }
    return std::nullopt;
}

std::optional<actions::StepLogEntry::Result> stepResultFrom(const QString &key)
{
    using R = actions::StepLogEntry::Result;
    for (R r : {R::Ok, R::Error, R::Skipped, R::DryRun, R::Note}) {
        if (actions::stepResultKey(r) == key)
            return r;
    }
    return std::nullopt;
}

// Datei-Format als Objekt (gleicher Inhalt wie die Datei)
QJsonObject fileObject(const QByteArray &bytes)
{
    return QJsonDocument::fromJson(bytes).object();
}

QByteArray objectBytes(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool checkType(const QJsonObject &message, const char *type, QString *error)
{
    if (messageType(message) == QLatin1String(type))
        return true;
    *error = QStringLiteral("Nachricht «%1» erwartet, erhalten «%2»").arg(QLatin1String(type), messageType(message));
    return false;
}

} // namespace

QStringList actionsRequestTypes()
{
    QStringList types;
    for (const RequestRule &rule : requestRules())
        types.append(QString::fromLatin1(rule.type));
    return types;
}

bool isActionsRequestType(const QString &type)
{
    return requestRule(type) != nullptr;
}

QJsonObject actionsRequestMessage(const ActionsRequest &request)
{
    QJsonObject o = request.fields;
    o.insert(QStringLiteral("type"), request.type);
    o.insert(QStringLiteral("request_id"), double(request.requestId));
    return o;
}

QJsonObject actionsEditMessage(const ActionsEdit &edit, qint64 requestId)
{
    return actionsRequestMessage({requestId, edit.type, edit.fields});
}

bool parseActionsRequest(const QJsonObject &message, ActionsRequest *request, QString *error)
{
    ActionsRequest r;
    r.type = messageType(message);
    const RequestRule *rule = requestRule(r.type);
    if (!rule) {
        *error = QStringLiteral("«%1» ist keine Anfrage des Aktionssystems").arg(r.type);
        return false;
    }
    r.requestId = messageRequestId(message);
    if (r.requestId < 0) {
        *error = QStringLiteral("Feld request_id fehlt oder ist keine ganze Zahl ab 0");
        return false;
    }
    for (const FieldRule &f : rule->fields) {
        const QString name = QString::fromLatin1(f.name);
        const QJsonValue v = message.value(name);
        if (v.isUndefined() && f.optional)
            continue;
        bool ok = false;
        switch (f.kind) {
        case Kind::Bool: ok = v.isBool(); break;
        case Kind::Object: ok = v.isObject(); break;
        case Kind::Array: ok = v.isArray(); break;
        case Kind::String: ok = v.isString(); break;
        case Kind::NullableString: ok = v.isString() || v.isNull(); break;
        case Kind::Value: ok = v.isString() || v.isDouble(); break;
        case Kind::Int: ok = v.isDouble() && v.toDouble() == double(qint64(v.toDouble())); break;
        case Kind::Uuid:
        case Kind::NullableUuid: {
            QString ignored;
            ok = uuidField(message, name, f.kind == Kind::NullableUuid, &ignored).has_value();
            break;
        }
        }
        if (!ok) {
            *error = QStringLiteral("Feld %1 fehlt oder hat die falsche Art").arg(name);
            return false;
        }
        r.fields.insert(name, v);
    }
    *request = r;
    return true;
}

std::optional<QUuid> uuidField(const QJsonObject &fields, const QString &name, bool allowNull, QString *error)
{
    const QJsonValue v = fields.value(name);
    if (v.isNull() && allowNull)
        return QUuid();
    const QUuid id = v.isString() ? QUuid::fromString(v.toString()) : QUuid();
    if (id.isNull()) {
        *error = QStringLiteral("Feld %1 ist keine Kennung").arg(name);
        return std::nullopt;
    }
    return id;
}

// --- actions_config ---

QJsonObject actionsConfigMessage(const ActionsConfigInfo &info)
{
    QJsonObject o = typed(msg::kActionsConfig);
    o.insert(QStringLiteral("config"),
             info.config ? QJsonValue(actions::actionsToJson(*info.config)) : QJsonValue(QJsonValue::Null));
    o.insert(QStringLiteral("file_error"),
             info.fileError.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(info.fileError));
    return o;
}

bool parseActionsConfig(const QJsonObject &message, ActionsConfigInfo *info, QString *error)
{
    if (!checkType(message, msg::kActionsConfig, error))
        return false;
    ActionsConfigInfo i;
    const QJsonValue config = message.value(QStringLiteral("config"));
    if (config.isObject()) {
        actions::ActionsConfig c;
        if (!actions::actionsFromJson(config.toObject(), &c, error))
            return false;
        i.config = c;
    } else if (!config.isNull()) {
        *error = QStringLiteral("Feld config ist weder Objekt noch null");
        return false;
    }
    i.fileError = message.value(QStringLiteral("file_error")).toString();
    *info = i;
    return true;
}

// --- actions_status ---

QJsonObject actionsStatusMessage(const ActionsStatus &status)
{
    QJsonObject o = typed(msg::kActionsStatus);
    o.insert(QStringLiteral("state"), status.state);
    o.insert(QStringLiteral("message"), status.message);
    o.insert(QStringLiteral("running"), status.running);
    QJsonArray queues;
    for (const ActionsQueueStatus &q : status.queues) {
        queues.append(QJsonObject{{QStringLiteral("id"), uuidValue(q.id)},
                                  {QStringLiteral("name"), q.name},
                                  {QStringLiteral("blocking"), q.blocking},
                                  {QStringLiteral("paused"), q.paused},
                                  {QStringLiteral("waiting"), q.waiting},
                                  {QStringLiteral("running"), q.running}});
    }
    o.insert(QStringLiteral("queues"), queues);
    QJsonArray accounts;
    for (const ActionsAccountStatus &a : status.accounts) {
        accounts.append(QJsonObject{{QStringLiteral("account"), a.account},
                                    {QStringLiteral("can_send"), a.canSend},
                                    {QStringLiteral("message"), a.message}});
    }
    o.insert(QStringLiteral("accounts"), accounts);
    return o;
}

bool parseActionsStatus(const QJsonObject &message, ActionsStatus *status, QString *error)
{
    if (!checkType(message, msg::kActionsStatus, error))
        return false;
    ActionsStatus s;
    s.state = message.value(QStringLiteral("state")).toString();
    if (s.state.isEmpty()) {
        *error = QStringLiteral("Feld state fehlt");
        return false;
    }
    s.message = message.value(QStringLiteral("message")).toString();
    s.running = message.value(QStringLiteral("running")).toInt();
    QJsonArray queues;
    QJsonArray accounts;
    if (!requireArray(message, QStringLiteral("queues"), &queues, error)
        || !requireArray(message, QStringLiteral("accounts"), &accounts, error))
        return false;
    for (const QJsonValue &v : std::as_const(queues)) {
        const QJsonObject q = v.toObject();
        s.queues.append({QUuid::fromString(q.value(QStringLiteral("id")).toString()),
                         q.value(QStringLiteral("name")).toString(), q.value(QStringLiteral("blocking")).toBool(),
                         q.value(QStringLiteral("paused")).toBool(), q.value(QStringLiteral("waiting")).toInt(),
                         q.value(QStringLiteral("running")).toInt()});
    }
    for (const QJsonValue &v : std::as_const(accounts)) {
        const QJsonObject a = v.toObject();
        s.accounts.append({a.value(QStringLiteral("account")).toString(),
                           a.value(QStringLiteral("can_send")).toBool(),
                           a.value(QStringLiteral("message")).toString()});
    }
    *status = s;
    return true;
}

// --- Läufe ---

QJsonObject runSummaryToJson(const actions::RunRecord &r)
{
    return QJsonObject{{QStringLiteral("run_id"), uuidValue(r.runId)},
                       {QStringLiteral("action_id"), uuidValue(r.actionId)},
                       {QStringLiteral("action_name"), r.actionName},
                       {QStringLiteral("trigger_type"), r.triggerType},
                       {QStringLiteral("trigger_id"), uuidValue(r.triggerId)},
                       {QStringLiteral("queue_id"), uuidValue(r.queue)},
                       {QStringLiteral("queue_name"), r.queueName},
                       {QStringLiteral("dry_run"), r.dryRun},
                       {QStringLiteral("state"), actions::runStateKey(r.state)},
                       {QStringLiteral("message"), r.message},
                       {QStringLiteral("queued_at"), timeValue(r.queuedAt)},
                       {QStringLiteral("started_at"), timeValue(r.startedAt)},
                       {QStringLiteral("finished_at"), timeValue(r.finishedAt)},
                       {QStringLiteral("step_index"), r.stepIndex},
                       {QStringLiteral("step_count"), r.stepCount},
                       {QStringLiteral("steps_executed"), r.stepsExecuted}};
}

QJsonObject runToJson(const actions::RunRecord &r)
{
    QJsonObject o = runSummaryToJson(r);
    QJsonArray log;
    for (const actions::StepLogEntry &e : r.log) {
        QJsonArray path;
        for (int n : e.path)
            path.append(n);
        log.append(QJsonObject{{QStringLiteral("time"), timeValue(e.time)},
                               {QStringLiteral("step_id"), uuidValue(e.stepId)},
                               {QStringLiteral("type"), e.type},
                               {QStringLiteral("path"), path},
                               {QStringLiteral("depth"), e.depth},
                               {QStringLiteral("result"), actions::stepResultKey(e.result)},
                               {QStringLiteral("message"), e.message}});
    }
    o.insert(QStringLiteral("log"), log);
    return o;
}

bool runFromJson(const QJsonObject &o, actions::RunRecord *record, QString *error)
{
    actions::RunRecord r;
    r.runId = QUuid::fromString(o.value(QStringLiteral("run_id")).toString());
    const std::optional<actions::RunState> state = runStateFrom(o.value(QStringLiteral("state")).toString());
    if (r.runId.isNull() || !state) {
        *error = QStringLiteral("Lauf ohne run_id oder mit unbekanntem state");
        return false;
    }
    r.state = *state;
    r.actionId = QUuid::fromString(o.value(QStringLiteral("action_id")).toString());
    r.actionName = o.value(QStringLiteral("action_name")).toString();
    r.triggerType = o.value(QStringLiteral("trigger_type")).toString();
    r.triggerId = QUuid::fromString(o.value(QStringLiteral("trigger_id")).toString());
    r.queue = QUuid::fromString(o.value(QStringLiteral("queue_id")).toString());
    r.queueName = o.value(QStringLiteral("queue_name")).toString();
    r.dryRun = o.value(QStringLiteral("dry_run")).toBool();
    r.message = o.value(QStringLiteral("message")).toString();
    r.queuedAt = timeFrom(o.value(QStringLiteral("queued_at")));
    r.startedAt = timeFrom(o.value(QStringLiteral("started_at")));
    r.finishedAt = timeFrom(o.value(QStringLiteral("finished_at")));
    r.stepIndex = o.value(QStringLiteral("step_index")).toInt();
    r.stepCount = o.value(QStringLiteral("step_count")).toInt();
    r.stepsExecuted = o.value(QStringLiteral("steps_executed")).toInt();
    const QJsonArray log = o.value(QStringLiteral("log")).toArray();
    for (const QJsonValue &v : log) {
        const QJsonObject e = v.toObject();
        actions::StepLogEntry entry;
        entry.time = timeFrom(e.value(QStringLiteral("time")));
        entry.stepId = QUuid::fromString(e.value(QStringLiteral("step_id")).toString());
        entry.type = e.value(QStringLiteral("type")).toString();
        const QJsonArray path = e.value(QStringLiteral("path")).toArray();
        for (const QJsonValue &n : path)
            entry.path.append(n.toInt());
        entry.depth = e.value(QStringLiteral("depth")).toInt();
        const auto result = stepResultFrom(e.value(QStringLiteral("result")).toString());
        if (!result) {
            *error = QStringLiteral("Schritt im Lauf mit unbekanntem result");
            return false;
        }
        entry.result = *result;
        entry.message = e.value(QStringLiteral("message")).toString();
        r.log.append(entry);
    }
    *record = r;
    return true;
}

QJsonObject actionRunsMessage(qint64 requestId, const QList<actions::RunRecord> &runs)
{
    QJsonObject o = typed(msg::kActionRuns);
    o.insert(QStringLiteral("request_id"), double(requestId));
    QJsonArray list;
    for (const actions::RunRecord &r : runs)
        list.append(runSummaryToJson(r));
    o.insert(QStringLiteral("runs"), list);
    return o;
}

bool parseActionRuns(const QJsonObject &message, QList<actions::RunRecord> *runs, QString *error)
{
    QJsonArray list;
    if (!checkType(message, msg::kActionRuns, error) || !requireArray(message, QStringLiteral("runs"), &list, error))
        return false;
    QList<actions::RunRecord> result;
    for (const QJsonValue &v : std::as_const(list)) {
        actions::RunRecord r;
        if (!runFromJson(v.toObject(), &r, error))
            return false;
        result.append(r);
    }
    *runs = result;
    return true;
}

QJsonObject actionRunMessage(qint64 requestId, const actions::RunRecord &record)
{
    QJsonObject o = typed(msg::kActionRun);
    o.insert(QStringLiteral("request_id"), double(requestId));
    o.insert(QStringLiteral("run"), runToJson(record));
    return o;
}

bool parseActionRun(const QJsonObject &message, actions::RunRecord *record, QString *error)
{
    QJsonObject run;
    return checkType(message, msg::kActionRun, error) && requireObject(message, QStringLiteral("run"), &run, error)
           && runFromJson(run, record, error);
}

QJsonObject runEventData(const actions::RunRecord &r)
{
    return QJsonObject{{QStringLiteral("run_id"), uuidValue(r.runId)},
                       {QStringLiteral("action_id"), uuidValue(r.actionId)},
                       {QStringLiteral("action_name"), r.actionName},
                       {QStringLiteral("trigger_type"), r.triggerType},
                       {QStringLiteral("queue_name"), r.queueName},
                       {QStringLiteral("dry_run"), r.dryRun},
                       {QStringLiteral("state"), actions::runStateKey(r.state)},
                       {QStringLiteral("message"), r.message},
                       {QStringLiteral("step_index"), r.stepIndex},
                       {QStringLiteral("step_count"), r.stepCount}};
}

// --- variables, viewers, quotes ---

QJsonObject variablesMessage(qint64 requestId, const VariablesInfo &info)
{
    QJsonObject o = typed(msg::kVariables);
    o.insert(QStringLiteral("request_id"), double(requestId));
    o.insert(QStringLiteral("persisted"), fileObject(actions::serializeVariables(info.persisted)));
    QJsonObject global;
    for (auto it = info.volatileGlobal.cbegin(); it != info.volatileGlobal.cend(); ++it)
        global.insert(it.key(), it.value());
    QJsonObject users;
    for (auto it = info.volatileUsers.cbegin(); it != info.volatileUsers.cend(); ++it) {
        QJsonObject values;
        for (auto v = it->cbegin(); v != it->cend(); ++v)
            values.insert(v.key(), v.value());
        users.insert(it.key(), values);
    }
    o.insert(QStringLiteral("volatile_global"), global);
    o.insert(QStringLiteral("volatile_users"), users);
    o.insert(QStringLiteral("file_error"),
             info.fileError.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(info.fileError));
    return o;
}

bool parseVariablesMessage(const QJsonObject &message, VariablesInfo *info, QString *error)
{
    QJsonObject persisted;
    QJsonObject global;
    QJsonObject users;
    if (!checkType(message, msg::kVariables, error)
        || !requireObject(message, QStringLiteral("persisted"), &persisted, error)
        || !requireObject(message, QStringLiteral("volatile_global"), &global, error)
        || !requireObject(message, QStringLiteral("volatile_users"), &users, error))
        return false;
    VariablesInfo i;
    if (!actions::parseVariables(objectBytes(persisted), &i.persisted, error))
        return false;
    for (auto it = global.begin(); it != global.end(); ++it)
        i.volatileGlobal.insert(it.key(), it.value());
    for (auto it = users.begin(); it != users.end(); ++it) {
        const QJsonObject values = it.value().toObject();
        for (auto v = values.begin(); v != values.end(); ++v)
            i.volatileUsers[it.key()].insert(v.key(), v.value());
    }
    i.fileError = message.value(QStringLiteral("file_error")).toString();
    *info = i;
    return true;
}

QJsonObject viewersMessage(qint64 requestId, const actions::ViewersData &viewers)
{
    QJsonObject o = typed(msg::kViewers);
    o.insert(QStringLiteral("request_id"), double(requestId));
    o.insert(QStringLiteral("viewers"),
             fileObject(actions::serializeViewers(viewers)).value(QStringLiteral("viewers")));
    return o;
}

bool parseViewersMessage(const QJsonObject &message, actions::ViewersData *viewers, QString *error)
{
    QJsonObject list;
    if (!checkType(message, msg::kViewers, error) || !requireObject(message, QStringLiteral("viewers"), &list, error))
        return false;
    return actions::parseViewers(objectBytes(QJsonObject{{QStringLiteral("format_version"), actions::kViewersFormatVersion},
                                                         {QStringLiteral("viewers"), list}}),
                                 viewers, error);
}

QJsonObject quotesMessage(qint64 requestId, const actions::QuotesData &quotes)
{
    QJsonObject o = typed(msg::kQuotes);
    o.insert(QStringLiteral("request_id"), double(requestId));
    o.insert(QStringLiteral("quotes"), fileObject(actions::serializeQuotes(quotes)).value(QStringLiteral("quotes")));
    return o;
}

bool parseQuotesMessage(const QJsonObject &message, actions::QuotesData *quotes, QString *error)
{
    QJsonArray list;
    if (!checkType(message, msg::kQuotes, error) || !requireArray(message, QStringLiteral("quotes"), &list, error))
        return false;
    return actions::parseQuotes(objectBytes(QJsonObject{{QStringLiteral("format_version"), actions::kQuotesFormatVersion},
                                                        {QStringLiteral("quotes"), list}}),
                                quotes, error);
}

QJsonObject viewerImportResultMessage(qint64 requestId, const ViewerImportResult &result)
{
    QJsonObject o = typed(msg::kViewerImportResult);
    o.insert(QStringLiteral("request_id"), double(requestId));
    o.insert(QStringLiteral("dry_run"), result.dryRun);
    o.insert(QStringLiteral("report"), result.report);
    o.insert(QStringLiteral("written"), QJsonArray::fromStringList(result.written));
    o.insert(QStringLiteral("backup"), result.backup.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(result.backup));
    return o;
}

bool parseViewerImportResult(const QJsonObject &message, ViewerImportResult *result, QString *error)
{
    QJsonArray written;
    if (!checkType(message, msg::kViewerImportResult, error)
        || !requireArray(message, QStringLiteral("written"), &written, error))
        return false;
    const QJsonValue dryRun = message.value(QStringLiteral("dry_run"));
    const QJsonValue report = message.value(QStringLiteral("report"));
    const QJsonValue backup = message.value(QStringLiteral("backup"));
    if (!dryRun.isBool() || !report.isString() || !(backup.isString() || backup.isNull())) {
        *error = QStringLiteral("Feld dry_run, report oder backup fehlt oder hat die falsche Art");
        return false;
    }
    ViewerImportResult r;
    r.dryRun = dryRun.toBool();
    r.report = report.toString();
    r.backup = backup.toString();
    for (const QJsonValue &v : written) {
        if (!v.isString()) {
            *error = QStringLiteral("Feld written enthält einen Eintrag, der kein Text ist");
            return false;
        }
        r.written.append(v.toString());
    }
    *result = r;
    return true;
}

} // namespace dss
