#include "actionrun.h"

#include "fieldspec.h"
#include "steptypes.h"

#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace dss::actions {

namespace {

// Name des Arguments, wenn text genau ein Platzhalter %name% ist
std::optional<QString> soleArgument(const QString &text)
{
    const QStringList names = argumentNames(text);
    if (names.size() == 1 && text == QLatin1Char('%') + names.first() + QLatin1Char('%'))
        return names.first();
    return std::nullopt;
}

std::optional<double> number(const QString &text)
{
    const QJsonValue v = autoTypedValue(text);
    if (v.isDouble())
        return v.toDouble();
    return std::nullopt;
}

QString withoutAt(QString login)
{
    login = login.trimmed();
    if (login.startsWith(QLatin1Char('@')))
        login.remove(0, 1);
    return login;
}

} // namespace

ActionRun::ActionRun(ActionEngine *engine, const Action &action, const ActionEngine::StartRequest &request,
                     const RunRecord &record)
    : m_engine(engine)
    , m_action(action)
    , m_request(request)
    , m_record(record)
    , m_alive(std::make_shared<int>(0))
{
    int n = 0;
    forEachStep(m_action.steps, [&](const Step &s, const QList<int> &) { m_ordinal.insert(s.id, ++n); });
    m_record.stepCount = n;
}

ActionRun::~ActionRun()
{
    if (m_stepTimer)
        m_engine->m_clock->stopTimer(m_stepTimer);
    if (m_limitTimer)
        m_engine->m_clock->stopTimer(m_limitTimer);
}

void ActionRun::start()
{
    const QDateTime now = m_engine->m_clock->now();
    m_record.state = RunState::Running;
    m_record.startedAt = now;

    // Allgemeine Argumente (Plan 2.5): aus dem Kern, dann die des Auslösers, dann die festen
    m_args = m_engine->m_baseArguments ? m_engine->m_baseArguments() : Arguments();
    for (auto it = m_request.arguments.cbegin(); it != m_request.arguments.cend(); ++it)
        m_args.insert(it.key(), it.value());
    const QDateTime local = now.toLocalTime();
    m_args.insert(QStringLiteral("actionName"), m_action.name);
    m_args.insert(QStringLiteral("actionId"), uuidText(m_action.id));
    m_args.insert(QStringLiteral("runId"), uuidText(m_record.runId));
    m_args.insert(QStringLiteral("triggerType"), m_record.triggerType);
    m_args.insert(QStringLiteral("date"), local.toString(QStringLiteral("dd.MM.yyyy")));
    m_args.insert(QStringLiteral("time"), local.toString(QStringLiteral("HH:mm:ss")));
    m_args.insert(QStringLiteral("unixtime"), double(now.toSecsSinceEpoch()));

    // Probelauf: eigener flüchtiger Satz, Kopie beim Beginn (Plan 3.6)
    if (m_record.dryRun)
        m_dryVariables = std::make_unique<VariableStore>(*m_engine->m_variables);

    const std::weak_ptr<int> alive = m_alive;
    m_limitTimer = m_engine->m_clock->startTimer(kMaxRunDurationMs, [this, alive] {
        if (alive.expired())
            return;
        m_limitTimer = 0;
        finish(RunState::Limit, QStringLiteral("Lauf dauerte länger als 15 min"));
    });

    if (m_action.concurrent)
        note(QStringLiteral("gleichzeitig nicht unterstützt, nacheinander ausgeführt"));
    m_engine->notifyChanged(this);
    pushFrame(m_action.steps, m_action.randomAction, {}, 0);
    resume();
}

void ActionRun::finish(RunState state, const QString &message)
{
    if (isFinished(m_record.state))
        return;
    if (m_stepTimer)
        m_engine->m_clock->stopTimer(m_stepTimer);
    if (m_limitTimer)
        m_engine->m_clock->stopTimer(m_limitTimer);
    m_stepTimer = 0;
    m_limitTimer = 0;
    m_alive.reset();
    m_waiting = false;
    m_frames.clear();
    m_record.state = state;
    m_record.message = message;
    m_record.finishedAt = m_engine->m_clock->now();
    // Abbruch (alles ausser dem normalen Ende nach dem letzten Schritt, Ok oder Error): die Töne
    // des Laufs enden sofort, mit und ohne wait (Entscheid Nutzer 2026-11-16, Plan 4.3)
    if (m_playedSound && state != RunState::Ok && state != RunState::Error && m_engine->m_services.sound)
        m_engine->m_services.sound->stopRun(m_record.runId);
    m_engine->finishRun(this);
}

void ActionRun::resume()
{
    if (m_inLoop) {
        m_resumeAgain = true;
        return;
    }
    m_inLoop = true;
    do {
        m_resumeAgain = false;
        loop();
    } while (m_resumeAgain && !m_waiting && !isFinished(m_record.state));
    m_inLoop = false;
}

void ActionRun::loop()
{
    while (!m_waiting && !isFinished(m_record.state)) {
        if (m_frames.isEmpty()) {
            finish(m_hadError ? RunState::Error : RunState::Ok);
            return;
        }
        Frame &frame = m_frames.last();
        if (frame.pos >= frame.steps.size()) {
            m_frames.removeLast();
            continue;
        }
        const Step step = frame.steps.at(frame.pos);
        QList<int> path = frame.prefix;
        path.append(frame.numbers.at(frame.pos));
        const int depth = frame.depth;
        ++frame.pos; // vor execute: execute legt womöglich neue Ebenen an
        execute(step, path, depth);
    }
}

void ActionRun::pushFrame(const QList<Step> &steps, bool randomOne, const QList<int> &prefix, int depth)
{
    Frame frame;
    frame.prefix = prefix;
    frame.depth = depth;
    if (!randomOne) {
        frame.steps = steps;
        for (qsizetype i = 0; i < steps.size(); ++i)
            frame.numbers.append(int(i) + 1);
    } else {
        // «zufällig einen» (Plan 2.3, 2.4): unter den aktiven Schritten nach weight; ist kein
        // Gewicht grösser als 0, alle gleich wahrscheinlich
        QList<qsizetype> candidates;
        double total = 0;
        for (qsizetype i = 0; i < steps.size(); ++i) {
            if (!steps.at(i).enabled)
                continue;
            candidates.append(i);
            total += std::max(0.0, steps.at(i).weight);
        }
        if (!candidates.isEmpty()) {
            const double r = std::clamp(m_engine->random(), 0.0, std::nextafter(1.0, 0.0));
            qsizetype chosen = candidates.last();
            if (total <= 0) {
                chosen = candidates.at(std::min(candidates.size() - 1, qsizetype(r * double(candidates.size()))));
            } else {
                double x = r * total;
                for (qsizetype i : candidates) {
                    x -= std::max(0.0, steps.at(i).weight);
                    if (x < 0) {
                        chosen = i;
                        break;
                    }
                }
            }
            frame.steps.append(steps.at(chosen));
            frame.numbers.append(int(chosen) + 1);
        }
    }
    m_frames.append(frame);
}

void ActionRun::execute(const Step &step, const QList<int> &path, int depth)
{
    m_current = {step.id, step.type, path, depth};
    if (depth == 0)
        m_record.stepIndex = m_ordinal.value(step.id, m_record.stepIndex);
    if (!step.enabled) {
        log(StepLogEntry::Result::Skipped, QStringLiteral("abgeschaltet"));
        return;
    }
    if (++m_record.stepsExecuted > kMaxStepsPerRun) {
        finish(RunState::Limit, QStringLiteral("mehr als %1 Schritte").arg(kMaxStepsPerRun));
        return;
    }
    m_engine->notifyChanged(this);

    const QString &type = step.type;
    const ActionServices &services = m_engine->m_services;
    const bool dry = m_record.dryRun;
    const QString unavailable = QStringLiteral("nicht verfügbar");

    if (type == QLatin1String(kStepComment)) {
        log(StepLogEntry::Result::Ok, QString());
    } else if (type == QLatin1String(kStepGroup) || type == QLatin1String(kStepIfTrue)
               || type == QLatin1String(kStepIfFalse)) {
        log(StepLogEntry::Result::Ok, QString());
        pushFrame(step.steps,
                  type == QLatin1String(kStepGroup) && step.fields.value(QStringLiteral("random")).toBool(), path,
                  depth);
    } else if (type == QLatin1String(kStepIf)) {
        executeIf(step, path, depth);
    } else if (type == QLatin1String(kStepBreak)) {
        log(StepLogEntry::Result::Ok, QString());
        finish(RunState::Break);
    } else if (type == QLatin1String(kStepDelay)) {
        executeDelay(step);
    } else if (type == QLatin1String(kStepRunAction)) {
        executeRunAction(step, depth);
    } else if (type == QLatin1String(kStepSetArgument)) {
        const QString name = step.fields.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) {
            log(StepLogEntry::Result::Error, QStringLiteral("kein Name"));
            return;
        }
        const QString value = text(step, QStringLiteral("value"));
        m_args.insert(name, step.fields.value(QStringLiteral("auto_type")).toBool() ? autoTypedValue(value)
                                                                                    : QJsonValue(value));
        log(StepLogEntry::Result::Ok, QString());
    } else if (type == QLatin1String(kStepGlobalGet)) {
        executeGlobalGet(step);
    } else if (type == QLatin1String(kStepGlobalSet)) {
        executeGlobalSet(step);
    } else if (type == QLatin1String(kStepChatSend)) {
        ChatRequest request;
        request.text = text(step, QStringLiteral("text"));
        request.useBot = step.fields.value(QStringLiteral("use_bot")).toBool(true);
        request.fallback = step.fields.value(QStringLiteral("fallback")).toBool(true);
        if (request.text.trimmed().isEmpty()) {
            log(StepLogEntry::Result::Error, QStringLiteral("Text ist leer"));
            return;
        }
        if (step.fields.value(QStringLiteral("reply")).toBool())
            request.replyTo = placeholderText(m_args.value(QStringLiteral("msgId")));
        if (dry) {
            log(StepLogEntry::Result::DryRun, QStringLiteral("Probelauf: würde senden als %1: %2")
                                                  .arg(request.useBot ? QStringLiteral("Bot") : QStringLiteral("Kanal"),
                                                       request.text));
        } else if (!services.chat) {
            log(StepLogEntry::Result::Error, unavailable);
        } else {
            services.chat->sendChat(request, waitForService());
        }
    } else if (type == QLatin1String(kStepUserInfo)) {
        // liest nur: läuft auch im Probelauf (Begrüssung mit Profilbild, Plan 12 L3)
        const QString login = withoutAt(text(step, QStringLiteral("login")));
        if (login.isEmpty())
            log(StepLogEntry::Result::Error, QStringLiteral("kein Name"));
        else if (!services.twitch)
            log(StepLogEntry::Result::Error, unavailable);
        else
            services.twitch->userInfo(login, waitForService());
    } else if (type == QLatin1String(kStepTwitchShoutout)) {
        const QString login = withoutAt(text(step, QStringLiteral("login")));
        if (login.isEmpty())
            log(StepLogEntry::Result::Error, QStringLiteral("kein Name"));
        else if (dry)
            log(StepLogEntry::Result::DryRun, QStringLiteral("Probelauf: würde Shoutout geben für %1").arg(login));
        else if (!services.twitch)
            log(StepLogEntry::Result::Error, unavailable);
        else
            services.twitch->shoutout(login, waitForService());
    } else if (type == QLatin1String(kStepTwitchAnnounce)) {
        AnnounceRequest request;
        request.text = text(step, QStringLiteral("text"));
        request.color = step.fields.value(QStringLiteral("color")).toString();
        request.useBot = step.fields.value(QStringLiteral("use_bot")).toBool(true);
        if (request.text.trimmed().isEmpty())
            log(StepLogEntry::Result::Error, QStringLiteral("Text ist leer"));
        else if (dry)
            log(StepLogEntry::Result::DryRun, QStringLiteral("Probelauf: würde Ansage senden: %1").arg(request.text));
        else if (!services.twitch)
            log(StepLogEntry::Result::Error, unavailable);
        else
            services.twitch->announce(request, waitForService());
    } else if (type == QLatin1String(kStepPlaySound)) {
        SoundRequest request;
        request.file = step.fields.value(QStringLiteral("file")).toString();
        request.volume = step.fields.value(QStringLiteral("volume")).toInt(100);
        request.wait = step.fields.value(QStringLiteral("wait")).toBool(true);
        request.runId = m_record.runId;
        if (request.file.isEmpty()) {
            log(StepLogEntry::Result::Error, QStringLiteral("keine Datei gewählt"));
        } else if (!services.sound) {
            log(StepLogEntry::Result::Error, unavailable);
        } else {
            m_playedSound = true;
            services.sound->play(request, waitForService());
        }
    } else if (type == QLatin1String(kStepOverlaySource)) {
        executeOverlay(step);
    } else if (type == QLatin1String(kStepObsSourceVisible)) {
        ObsVisibilityRequest request;
        request.sceneName = text(step, QStringLiteral("scene_name"));
        request.sourceName = text(step, QStringLiteral("source_name"));
        request.state = step.fields.value(QStringLiteral("state")).toString();
        if (dry)
            log(StepLogEntry::Result::DryRun, QStringLiteral("Probelauf: würde OBS-Quelle %1 in %2 schalten: %3")
                                                  .arg(request.sourceName, request.sceneName, request.state));
        else if (!services.obs)
            log(StepLogEntry::Result::Error, unavailable);
        else
            services.obs->setSourceVisible(request, waitForService());
    } else if (type == QLatin1String(kStepScript)) {
        log(StepLogEntry::Result::Skipped, QStringLiteral("Skript-Schritt nicht verfügbar"));
    } else if (type == QLatin1String(kStepUnsupported)) {
        log(StepLogEntry::Result::Skipped,
            QStringLiteral("nicht unterstützt: %1").arg(step.fields.value(QStringLiteral("sb_type")).toInteger()));
    } else {
        log(StepLogEntry::Result::Error, QStringLiteral("unbekannter Typ"));
    }
}

void ActionRun::executeIf(const Step &step, const QList<int> &path, int depth)
{
    const QString rawInput = step.fields.value(QStringLiteral("input")).toString();
    const QString input = placeholders(rawInput);
    const QString value = text(step, QStringLiteral("value"));
    const QString operation = step.fields.value(QStringLiteral("operation")).toString();
    const bool autoType = step.fields.value(QStringLiteral("auto_type")).toBool(true);

    // Gibt es das Argument? Bei genau einem Platzhalter %name% dessen Name, sonst der Text als Name
    const std::optional<QString> sole = soleArgument(rawInput);
    const bool exists = m_args.contains(sole ? *sole : rawInput.trimmed());

    bool result = false;
    QString error;
    const auto equal = [&] {
        if (autoType) {
            const std::optional<double> a = number(input);
            const std::optional<double> b = number(value);
            if (a && b)
                return *a == *b;
        }
        return input == value;
    };
    const auto compare = [&](bool less) {
        const std::optional<double> a = number(input);
        const std::optional<double> b = number(value);
        if (!a || !b) {
            error = QStringLiteral("Vergleich braucht zwei Zahlen: «%1» und «%2»").arg(input, value);
            return false;
        }
        return less ? *a < *b : *a > *b;
    };
    if (operation == QLatin1String("equals"))
        result = equal();
    else if (operation == QLatin1String("not_equals"))
        result = !equal();
    else if (operation == QLatin1String("equals_ignore_case"))
        result = input.compare(value, Qt::CaseInsensitive) == 0;
    else if (operation == QLatin1String("not_equals_ignore_case"))
        result = input.compare(value, Qt::CaseInsensitive) != 0;
    else if (operation == QLatin1String("contains"))
        result = input.contains(value);
    else if (operation == QLatin1String("less_than"))
        result = compare(true);
    else if (operation == QLatin1String("greater_than"))
        result = compare(false);
    else if (operation == QLatin1String("is_empty"))
        result = sole ? (!exists || input.isEmpty()) : input.isEmpty();
    else if (operation == QLatin1String("not_exists"))
        result = !exists;
    else if (operation == QLatin1String("regex"))
        error = QStringLiteral("Vergleich regex wird nicht ausgeführt");
    else
        error = QStringLiteral("unbekannter Vergleich");

    // Fehler im Vergleich: Eintrag, kein Zweig (Plan 3.1: der Lauf geht weiter)
    if (!error.isEmpty()) {
        log(StepLogEntry::Result::Error, error);
        return;
    }
    log(StepLogEntry::Result::Ok, result ? QStringLiteral("Dann") : QStringLiteral("Sonst"));
    const char *branchType = result ? kStepIfTrue : kStepIfFalse;
    for (qsizetype i = 0; i < step.steps.size(); ++i) {
        const Step &branch = step.steps.at(i);
        if (branch.type != QLatin1String(branchType))
            continue;
        if (branch.enabled) {
            QList<int> prefix = path;
            prefix.append(int(i) + 1);
            pushFrame(branch.steps, false, prefix, depth);
        }
        break;
    }
}

void ActionRun::executeRunAction(const Step &step, int depth)
{
    QUuid target;
    if (!parseUuidText(step.fields.value(QStringLiteral("action_id")).toString(), &target)) {
        log(StepLogEntry::Result::Error, QStringLiteral("keine Aktion gewählt"));
        return;
    }
    const Action *action = m_engine->m_config.action(target);
    if (!action) {
        log(StepLogEntry::Result::Error, QStringLiteral("Aktion fehlt"));
        return;
    }
    if (!action->enabled) {
        log(StepLogEntry::Result::Error, QStringLiteral("Aktion «%1» ist aus").arg(action->name));
        return;
    }
    if (step.fields.value(QStringLiteral("immediately")).toBool(true)) {
        // im selben Lauf, Argumente hin und zurück (Plan 4.10); Stand der Aktion von jetzt
        if (depth + 1 > kMaxImmediateDepth) {
            finish(RunState::Limit, QStringLiteral("Aktion ausführen: mehr als %1 Ebenen").arg(kMaxImmediateDepth));
            return;
        }
        log(StepLogEntry::Result::Ok, QStringLiteral("sofort: %1").arg(action->name));
        pushFrame(action->steps, action->randomAction, {}, depth + 1);
        return;
    }
    // eigener Lauf in der Warteschlange des Ziels mit einer Kopie der Argumente
    ActionEngine::StartRequest request;
    request.actionId = target;
    request.triggerType = QStringLiteral("run_action");
    request.arguments = m_args;
    request.dryRun = m_record.dryRun;
    log(StepLogEntry::Result::Ok, QStringLiteral("eigener Lauf: %1").arg(action->name));
    m_engine->enqueue(request);
}

std::optional<VariableKey> ActionRun::variableKey(const Step &step, QString *error) const
{
    VariableKey key;
    key.name = step.fields.value(QStringLiteral("name")).toString().trimmed();
    key.persisted = step.fields.value(QStringLiteral("persisted")).toBool(true);
    if (key.name.isEmpty()) {
        *error = QStringLiteral("kein Name");
        return std::nullopt;
    }
    if (step.fields.value(QStringLiteral("scope")).toString() == QLatin1String("user")) {
        key.scope = VariableScope::User;
        const QString argument = step.fields.value(QStringLiteral("user")).toString() == QLatin1String("target")
                                     ? QStringLiteral("targetUserId")
                                     : QStringLiteral("userId");
        key.userId = placeholderText(m_args.value(argument));
        if (key.userId.isEmpty()) {
            *error = QStringLiteral("Zuschauer fehlt (%%1%)").arg(argument);
            return std::nullopt;
        }
    }
    return key;
}

void ActionRun::executeGlobalGet(const Step &step)
{
    QString error;
    const std::optional<VariableKey> key = variableKey(step, &error);
    if (!key) {
        log(StepLogEntry::Result::Error, error);
        return;
    }
    QString destination = step.fields.value(QStringLiteral("destination")).toString().trimmed();
    if (destination.isEmpty())
        destination = key->name;
    if (const std::optional<QJsonValue> value = variables().value(*key)) {
        m_args.insert(destination, *value);
        log(StepLogEntry::Result::Ok, QString());
        return;
    }
    // Ohne Standardwert bleibt das Argument ungesetzt (Wenn «existiert nicht» trifft dann zu)
    const QString fallback = text(step, QStringLiteral("default"));
    if (fallback.isEmpty()) {
        log(StepLogEntry::Result::Ok, QStringLiteral("nicht vorhanden"));
        return;
    }
    m_args.insert(destination, autoTypedValue(fallback));
    log(StepLogEntry::Result::Ok, QStringLiteral("nicht vorhanden, Standardwert"));
}

void ActionRun::executeGlobalSet(const Step &step)
{
    QString error;
    const std::optional<VariableKey> key = variableKey(step, &error);
    if (!key) {
        log(StepLogEntry::Result::Error, error);
        return;
    }
    const QString operation = step.fields.value(QStringLiteral("operation")).toString();
    const QDateTime now = m_engine->m_clock->now();
    const QString value = text(step, QStringLiteral("value"));
    if (operation == QLatin1String("set")) {
        error = variables().set(*key, autoTypedValue(value), now);
    } else if (operation == QLatin1String("increment") || operation == QLatin1String("decrement")) {
        // leerer Wert = um 1
        const std::optional<double> delta = value.trimmed().isEmpty() ? std::optional<double>(1) : number(value);
        if (!delta)
            error = QStringLiteral("«%1» ist keine Zahl").arg(value);
        else
            error = variables().add(*key, operation == QLatin1String("increment") ? *delta : -*delta, now);
    } else if (operation == QLatin1String("delete")) {
        variables().remove(*key);
    } else {
        error = QStringLiteral("unbekannte Art");
    }
    log(error.isEmpty() ? StepLogEntry::Result::Ok : StepLogEntry::Result::Error, error);
}

void ActionRun::executeDelay(const Step &step)
{
    qint64 ms = step.fields.value(QStringLiteral("value_ms")).toInteger();
    if (step.fields.value(QStringLiteral("random")).toBool()) {
        const qint64 max = step.fields.value(QStringLiteral("max_value_ms")).toInteger();
        if (max > ms) {
            const double r = std::clamp(m_engine->random(), 0.0, std::nextafter(1.0, 0.0));
            ms += qint64(std::floor(r * double(max - ms + 1)));
        }
    }
    ms = std::clamp<qint64>(ms, 0, kMaxDelayMs);
    waitForTimer(ms, QStringLiteral("%1 ms").arg(ms));
}

void ActionRun::executeOverlay(const Step &step)
{
    OverlayRequest request;
    if (!parseUuidText(step.fields.value(QStringLiteral("overlay")).toString(), &request.overlay)) {
        log(StepLogEntry::Result::Error, QStringLiteral("kein Overlay gewählt"));
        return;
    }
    request.mode = step.fields.value(QStringLiteral("mode")).toString();
    const QJsonArray elements = step.fields.value(QStringLiteral("elements")).toArray();
    for (const QJsonValue &v : elements) {
        const QJsonObject o = v.toObject();
        OverlayRequest::Element element;
        parseUuidText(o.value(QStringLiteral("source")).toString(), &element.source);
        if (o.value(QStringLiteral("text")).isString())
            element.text = placeholders(o.value(QStringLiteral("text")).toString());
        if (o.value(QStringLiteral("image")).isString())
            element.image = placeholders(o.value(QStringLiteral("image")).toString());
        request.elements.append(element);
    }
    request.durationMs = step.fields.value(QStringLiteral("duration_ms")).toInteger();
    request.transitionIn = step.fields.value(QStringLiteral("transition_in")).toString();
    request.transitionOut = step.fields.value(QStringLiteral("transition_out")).toString();
    request.transitionMs = step.fields.value(QStringLiteral("transition_ms")).toInteger();
    request.arguments = m_args;
    if (!m_engine->m_services.overlay) {
        log(StepLogEntry::Result::Error, QStringLiteral("nicht verfügbar"));
        return;
    }
    const bool wait = step.fields.value(QStringLiteral("wait")).toBool(true)
                      && request.mode != QLatin1String("hide") && request.durationMs > 0;
    const qint64 ms = qMin(request.durationMs, kMaxRunDurationMs);
    m_engine->m_services.overlay->apply(
        request, waitForService(wait ? std::function<void(const StepResult &)>([this, ms](const StepResult &) {
                                     waitForTimer(ms, QString());
                                 })
                                     : std::function<void(const StepResult &)>()));
}

StepDone ActionRun::waitForService(std::function<void(const StepResult &)> then)
{
    m_waiting = true;
    const std::weak_ptr<int> alive = m_alive;
    const Current current = m_current;
    auto called = std::make_shared<bool>(false);
    return [this, alive, current, called, then = std::move(then)](const StepResult &result) {
        if (alive.expired() || *called)
            return;
        *called = true;
        m_waiting = false;
        m_current = current;
        for (auto it = result.arguments.cbegin(); it != result.arguments.cend(); ++it)
            m_args.insert(it.key(), it.value());
        log(result.ok ? StepLogEntry::Result::Ok : StepLogEntry::Result::Error, result.message);
        if (result.ok && then)
            then(result);
        resume();
    };
}

void ActionRun::waitForTimer(qint64 ms, const QString &message)
{
    m_waiting = true;
    const std::weak_ptr<int> alive = m_alive;
    const Current current = m_current;
    m_stepTimer = m_engine->m_clock->startTimer(ms, [this, alive, current, message] {
        if (alive.expired())
            return;
        m_stepTimer = 0;
        m_waiting = false;
        m_current = current;
        if (!message.isEmpty())
            log(StepLogEntry::Result::Ok, message);
        resume();
    });
}

void ActionRun::log(StepLogEntry::Result result, const QString &message)
{
    StepLogEntry entry;
    entry.time = m_engine->m_clock->now();
    entry.stepId = m_current.id;
    entry.type = m_current.type;
    entry.path = m_current.path;
    entry.depth = m_current.depth;
    entry.result = result;
    entry.message = message;
    m_record.log.append(entry);
    if (result == StepLogEntry::Result::Error)
        m_hadError = true;
}

void ActionRun::note(const QString &message)
{
    StepLogEntry entry;
    entry.time = m_engine->m_clock->now();
    entry.result = StepLogEntry::Result::Note;
    entry.message = message;
    m_record.log.append(entry);
}

QString ActionRun::text(const Step &step, const QString &field) const
{
    return placeholders(step.fields.value(field).toString());
}

QString ActionRun::placeholders(const QString &text) const
{
    return replacePlaceholders(text, m_args, [this](const QString &name) {
        return variables().value({VariableScope::Global, QString(), name, true});
    });
}

VariableStore &ActionRun::variables()
{
    return m_dryVariables ? *m_dryVariables : *m_engine->m_variables;
}

const VariableStore &ActionRun::variables() const
{
    return m_dryVariables ? *m_dryVariables : *m_engine->m_variables;
}

} // namespace dss::actions
