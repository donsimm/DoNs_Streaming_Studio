#include "actionservice.h"

#include "actionedit.h"
#include "actionjson.h"
#include "actiontemplate.h"
#include "overlaylivestate.h"
#include "placeholders.h"
#include "protocol.h"
#include "savescheduler.h"
#include "triggerevent.h"
#include "triggertypes.h"
#include "viewerimport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocalSocket>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#if DSS_CORE_AUDIO
#include "actionsound.h"
#endif

namespace {

const QString kTwitchNotConnected = QStringLiteral("Twitch nicht verbunden");
constexpr int kProbeTimeoutMs = 1000;
// chat_send: höchstens 500 UTF-16-Einheiten wie die Grenze beim Schritt (Plan 4.1, ChatOutbox::Limits)
constexpr int kChatSendMaxLength = 500;

QJsonObject result(qint64 requestId, bool ok, const QString &code = {}, const QString &message = {},
                   const QUuid &id = {})
{
    return dss::changeResultMessage({requestId, ok, id, code, message});
}

QJsonObject rejected(qint64 requestId, const QString &message)
{
    return result(requestId, false, QString::fromLatin1(dss::err::kRejected), message);
}

bool writeFile(const QString &path, const QByteArray &bytes, QString *error)
{
    // Atomar: QSaveFile schreibt eine vorläufige Datei und benennt sie erst am Ende um
    // (https://doc.qt.io/qt-6.8/qsavefile.html)
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    return true;
}

// Liest eine Datei des Aktionssystems. Fehlt sie: true ohne Inhalt. Ungültig: false mit Meldung.
bool readFile(const QString &path, QByteArray *bytes, bool *exists, QString *error)
{
    QFile file(path);
    *exists = file.exists();
    if (!*exists)
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1 nicht lesbar: %2").arg(QFileInfo(path).fileName(), file.errorString());
        return false;
    }
    *bytes = file.readAll();
    return true;
}

QString pathText(const QList<int> &path)
{
    QStringList parts;
    for (int n : path)
        parts.append(QString::number(n));
    return parts.join(QLatin1Char('.'));
}

} // namespace

// Benutzerinfo, Shoutout und Ansage (c1b): an Options::twitchApi weiter; ohne (Twitch nicht
// eingebaut) «Twitch nicht verbunden». Eine gefundene Benutzerinfo führt zuschauer.json nach (Plan 4.8).
class ActionService::TwitchForward : public dss::actions::TwitchApi
{
public:
    explicit TwitchForward(ActionService *service)
        : m_service(service)
    {
    }

    void userInfo(const QString &login, dss::actions::StepDone done) override
    {
        if (!api()) {
            done(dss::actions::StepResult::failure(kTwitchNotConnected));
            return;
        }
        QPointer<ActionService> service(m_service);
        api()->userInfo(login, [service, done](const dss::actions::StepResult &r) {
            if (service && r.ok && r.arguments.value(QStringLiteral("targetUserFound")).toBool())
                service->noteViewer(r.arguments.value(QStringLiteral("targetUserId")).toString(),
                                    r.arguments.value(QStringLiteral("targetUserName")).toString(),
                                    r.arguments.value(QStringLiteral("targetUser")).toString());
            done(r);
        });
    }
    void shoutout(const QString &login, dss::actions::StepDone done) override
    {
        if (api())
            api()->shoutout(login, std::move(done));
        else
            done(dss::actions::StepResult::failure(kTwitchNotConnected));
    }
    void announce(const dss::actions::AnnounceRequest &request, dss::actions::StepDone done) override
    {
        if (api())
            api()->announce(request, std::move(done));
        else
            done(dss::actions::StepResult::failure(kTwitchNotConnected));
    }

private:
    dss::actions::TwitchApi *api() const { return m_service->m_options.twitchApi; }

    ActionService *m_service;
};

// Ton abspielen (c2, Plan 4.3): an dss::ActionSound (Zweck «aktionen»). Die Prüfungen (Ausgang,
// Lautstärke, Datei, höchstens 8 gleichzeitig) und ihre Meldungen macht ActionSound. Mit wait kommt
// done erst mit finished(), sonst sobald der Ton begonnen hat.
class ActionService::SoundForward : public dss::actions::SoundPlayer
{
public:
    explicit SoundForward(ActionService *service)
        : m_service(service)
    {
    }

    void play(const dss::actions::SoundRequest &request, dss::actions::StepDone done) override
    {
#if DSS_CORE_AUDIO
        dss::ActionSound *sound = m_service->m_options.actionSound ? m_service->m_options.actionSound() : nullptr;
        if (!sound) {
            done(dss::actions::StepResult::failure(QStringLiteral("nicht verfügbar")));
            return;
        }
        if (m_connected != sound) {
            m_connected = sound;
            QObject::connect(sound, &dss::ActionSound::finished, m_service,
                             [this](quint64 id, bool ok, const QString &message) {
                                 forgetSound(id);
                                 const dss::actions::StepDone waiting = m_waiting.take(id);
                                 if (waiting)
                                     waiting(ok ? dss::actions::StepResult{}
                                                : dss::actions::StepResult::failure(message));
                             });
        }
        // Nur Dateien unter media/ im Datenordner: kein absoluter Pfad, kein «..»
        if (!request.file.startsWith(QLatin1String("media/")) || QDir::isAbsolutePath(request.file)
            || request.file.split(QLatin1Char('/')).contains(QLatin1String(".."))) {
            done(dss::actions::StepResult::failure(
                QStringLiteral("Datei «%1» ungültig, erwartet eine Datei der Mediathek (media/…).").arg(request.file)));
            return;
        }
        QString error;
        const quint64 id = sound->play(request.file, request.volume, &error);
        if (id == 0) {
            done(dss::actions::StepResult::failure(error));
            return;
        }
        m_byRun[request.runId].insert(id);
        if (request.wait)
            m_waiting.insert(id, std::move(done));
        else
            done({});
#else
        Q_UNUSED(request);
        done(dss::actions::StepResult::failure(QStringLiteral("ohne Ton gebaut")));
#endif
    }

    // Abbruch eines Laufs (Entscheid Nutzer 2026-11-16): seine noch spielenden Töne enden sofort.
    // Der Lauf ist schon beendet; sein wartendes done fällt weg, statt die Meldung «abgebrochen»
    // von ActionSound::stop an einen beendeten Lauf zu geben.
    void stopRun(const QUuid &runId) override
    {
#if DSS_CORE_AUDIO
        const QSet<quint64> ids = m_byRun.take(runId);
        for (quint64 id : ids) {
            m_waiting.remove(id);
            if (m_connected)
                m_connected->stop(id);
        }
#else
        Q_UNUSED(runId);
#endif
    }

private:
#if DSS_CORE_AUDIO
    void forgetSound(quint64 id)
    {
        for (auto it = m_byRun.begin(); it != m_byRun.end(); ++it) {
            if (it->remove(id)) {
                if (it->isEmpty())
                    m_byRun.erase(it);
                return;
            }
        }
    }
#endif

    ActionService *m_service;
#if DSS_CORE_AUDIO
    QPointer<dss::ActionSound> m_connected;
    QHash<quint64, dss::actions::StepDone> m_waiting; // Töne mit wait: Kennung → done
    QHash<QUuid, QSet<quint64>> m_byRun;              // spielende Töne je Lauf (für stopRun)
#endif
};

// Overlay-Element zeigen/verstecken (c2, Plan 4.4): derselbe Weg wie overlay_live_set
// (OverlayLiveState::set), nur im Speicher. Unbekanntes Overlay oder Element, zu lange Dauer:
// Fehler im Lauf. Hinweise des Laufzustands (Bild nicht geladen, Text gekürzt) stehen im Lauf.
class ActionService::OverlayForward : public dss::actions::OverlayLive
{
public:
    explicit OverlayForward(ActionService *service)
        : m_service(service)
    {
    }

    void apply(const dss::actions::OverlayRequest &request, dss::actions::StepDone done) override
    {
        dss::OverlayLiveState *live = m_service->m_options.overlayLive;
        if (!live) {
            done(dss::actions::StepResult::failure(QStringLiteral("nicht verfügbar")));
            return;
        }
        if (request.durationMs < 0 || request.durationMs > dss::kLiveMaxDurationMs) {
            done(dss::actions::StepResult::failure(QStringLiteral("Dauer %1 ms ungültig, erlaubt sind 0 (ohne Ende) bis %2 ms.")
                                                       .arg(request.durationMs)
                                                       .arg(dss::kLiveMaxDurationMs)));
            return;
        }
        dss::LiveSetRequest r;
        r.overlayId = request.overlay;
        if (request.mode == QLatin1String("hide"))
            r.mode = dss::LiveMode::Hide;
        else if (request.mode == QLatin1String("toggle"))
            r.mode = dss::LiveMode::Toggle;
        else
            r.mode = dss::LiveMode::Show;
        for (const dss::actions::OverlayRequest::Element &e : request.elements)
            r.elements.append({e.source, e.text, e.image.value_or(QString())});
        r.durationMs = int(request.durationMs);
        // Text und Bild aus dem Schritt hat der Lauf schon eingesetzt (ActionRun::executeOverlay)
        r.resolved = true;
        // Argumente des Laufs für die Platzhalter im eigenen Text der Elemente
        for (auto it = request.arguments.cbegin(); it != request.arguments.cend(); ++it)
            r.arguments.insert(it.key(), dss::actions::placeholderText(it.value()));
        // Plan 4.4: in P14-1 nur ohne Übergang; «fade» kommt mit P14-2
        QString note;
        if (request.transitionIn == QLatin1String("fade") || request.transitionOut == QLatin1String("fade"))
            note = QStringLiteral("Übergang «weich» kommt später, ohne Übergang gezeigt.");
        live->set(r, [done, note](const dss::OverlayLiveState::Result &result) {
            if (!result.ok) {
                done(dss::actions::StepResult::failure(result.message));
                return;
            }
            QStringList messages{note, result.warning};
            messages.removeAll(QString());
            done({true, messages.join(QLatin1Char(' ')), {}});
        });
    }

private:
    ActionService *m_service;
};

ActionService::ActionService(dss::events::EventBus *bus, const Options &options, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_options(options)
    , m_engine(std::make_unique<dss::actions::ActionEngine>(&m_clock, &m_variables))
    , m_dispatcher(std::make_unique<dss::actions::TriggerDispatcher>(&m_clock, &m_variables))
    , m_chat(std::make_unique<dss::actions::ChatOutbox>(&m_clock, options.chatTransport))
    , m_twitch(std::make_unique<TwitchForward>(this))
    , m_sound(std::make_unique<SoundForward>(this))
    , m_overlay(std::make_unique<OverlayForward>(this))
    , m_saver(new dss::SaveScheduler(this))
{
    dss::actions::ActionServices services;
    services.chat = m_chat.get();
    services.twitch = m_twitch.get();
    services.sound = m_sound.get();
    services.overlay = m_overlay.get();
    services.camera = options.camera;
    m_engine->setServices(services);
    m_chat->setResultHandler([this](const dss::actions::ChatOutcome &outcome) { publishChat(outcome); });
    m_engine->setBaseArguments([this] {
        return dss::actions::Arguments{{QStringLiteral("isLive"), m_dispatcher->isLive()}};
    });
    connect(m_engine.get(), &dss::actions::ActionEngine::runStarted, this, &ActionService::onRunStarted);
    connect(m_engine.get(), &dss::actions::ActionEngine::runChanged, this, &ActionService::onRunChanged);
    connect(m_engine.get(), &dss::actions::ActionEngine::runFinished, this, &ActionService::onRunFinished);
    m_dispatcher->setTimerHandler([this](const dss::actions::TriggerResult &r) {
        dss::actions::submit(*m_engine, r);
    });
    connect(m_saver, &dss::SaveScheduler::saveRequested, this, [this] {
        QString error;
        if (!savePending(&error))
            qCritical("Aktionen: Speichern fehlgeschlagen: %s", qPrintable(error));
    });
    // Twitch-Ereignisse vom Fluss (P13)
    m_subscription = m_bus->subscribe(this, {QStringLiteral("twitch.")},
                                      [this](const dss::events::Event &event) { onEvent(event); });
    // c5: «~name~» im eigenen Text eines Elements (und in overlay_live_set) mit den dauerhaften
    // globalen Variablen, wie im Lauf (ActionRun::placeholders, Plan 2.9)
    if (m_options.overlayLive) {
        m_options.overlayLive->setSubstitute([this](const QString &text, const QHash<QString, QString> &arguments) {
            return dss::substitutePlaceholders(text, arguments, [this](const QString &name) {
                return m_variables.value({dss::actions::VariableScope::Global, QString(), name, true});
            });
        });
    }

    if (!m_options.inUseSocket.isEmpty()) {
        m_probeTimer = new QTimer(this);
        m_probeTimer->setInterval(m_options.probeIntervalMs);
        connect(m_probeTimer, &QTimer::timeout, this, &ActionService::probeInUse);
        m_probeTimer->start();
        QTimer::singleShot(0, this, &ActionService::probeInUse);
    }
}

ActionService::~ActionService()
{
    QString error;
    if (!savePending(&error))
        qCritical("Aktionen: beim Beenden nicht gespeichert: %s", qPrintable(error));
    if (m_options.overlayLive)
        m_options.overlayLive->setSubstitute({});
    // Läufe vor Uhr und Variablen beenden (Reihenfolge der Mitglieder)
    m_dispatcher.reset();
    m_engine.reset();
}

QString ActionService::filePath(const char *name) const
{
    return QDir(m_options.dataDir).filePath(QString::fromLatin1(name));
}

void ActionService::load()
{
    QString error;
    m_variablesError.clear();
    m_viewersError.clear();
    m_quotesError.clear();
    QByteArray bytes;
    bool exists = false;

    // aktionen.json; fehlt sie, legt der Kern die mitgelieferten Aktionen an (Plan 2.11 und 7,
    // Hauptschalter aus), nie über eine vorhandene Datei
    dss::actions::ActionsConfig config;
    bool created = false;
    m_configError.clear();
    if (!readFile(filePath(dss::actions::kActionsFileName), &bytes, &exists, &error)
        || (exists && !dss::actions::parseActions(bytes, &config, &error))) {
        m_configError = QStringLiteral("%1: %2").arg(QLatin1String(dss::actions::kActionsFileName), error);
        config = {};
        qCritical("Aktionen: %s; die Datei bleibt unverändert, Chat-Bot aus", qPrintable(m_configError));
    } else if (!exists) {
        QString templateError;
        config = dss::actions::actionTemplate(&templateError);
        if (!templateError.isEmpty()) {
            qCritical("Aktionen: %s", qPrintable(templateError));
        } else if (!writeConfig(config, &error)) {
            qCritical("Aktionen: mitgelieferte Aktionen nicht gespeichert: %s", qPrintable(error));
        } else {
            qInfo("Aktionen: aktionen.json fehlte, mitgelieferte Aktionen angelegt");
            created = true;
        }
    }
    m_config = config;
    if (created && m_options.templateCreated)
        m_options.templateCreated(); // f2: Overlay «Chat-Bot», wenn es fehlt

    dss::actions::VariablesData variables;
    bytes.clear();
    if (!readFile(filePath(dss::actions::kVariablesFileName), &bytes, &exists, &error)
        || (exists && !dss::actions::parseVariables(bytes, &variables, &error))) {
        m_variablesError = QStringLiteral("%1: %2").arg(QLatin1String(dss::actions::kVariablesFileName), error);
        variables = {};
        qCritical("Aktionen: %s; die Datei bleibt unverändert", qPrintable(m_variablesError));
    }
    m_variables.setData(variables);
    m_variables.clearChanged();

    m_viewers = {};
    bytes.clear();
    if (!readFile(filePath(dss::actions::kViewersFileName), &bytes, &exists, &error)
        || (exists && !dss::actions::parseViewers(bytes, &m_viewers, &error))) {
        m_viewersError = QStringLiteral("%1: %2").arg(QLatin1String(dss::actions::kViewersFileName), error);
        m_viewers = {};
        qCritical("Aktionen: %s; die Datei bleibt unverändert", qPrintable(m_viewersError));
    }
    m_viewersChanged = false;

    m_quotes = {};
    bytes.clear();
    if (!readFile(filePath(dss::actions::kQuotesFileName), &bytes, &exists, &error)
        || (exists && !dss::actions::parseQuotes(bytes, &m_quotes, &error))) {
        m_quotesError = QStringLiteral("%1: %2").arg(QLatin1String(dss::actions::kQuotesFileName), error);
        m_quotes = {};
        qCritical("Aktionen: %s; die Datei bleibt unverändert", qPrintable(m_quotesError));
    }

    applyConfig();
    qInfo("Aktionen: %lld Aktionen, %lld Befehle, %lld Zeitnachrichten geladen, Chat-Bot %s",
          static_cast<long long>(m_config.actions.size()), static_cast<long long>(m_config.commands.size()),
          static_cast<long long>(m_config.timers.size()), m_config.enabled ? "an" : "aus");
    emit broadcastRequested(configMessage());
    broadcastStatusIfChanged();
}

void ActionService::applyConfig()
{
    m_engine->setConfig(m_config);
    m_dispatcher->setConfig(m_config);
}

bool ActionService::savePending(QString *error)
{
    bool ok = true;
    if (m_variables.changed() && m_variablesError.isEmpty()) {
        if (writeFile(filePath(dss::actions::kVariablesFileName), dss::actions::serializeVariables(m_variables.data()),
                      error))
            m_variables.clearChanged();
        else
            ok = false;
    }
    if (m_viewersChanged && m_viewersError.isEmpty()) {
        if (writeFile(filePath(dss::actions::kViewersFileName), dss::actions::serializeViewers(m_viewers), error))
            m_viewersChanged = false;
        else
            ok = false;
    }
    return ok;
}

bool ActionService::writeConfig(const dss::actions::ActionsConfig &config, QString *error)
{
    return writeFile(filePath(dss::actions::kActionsFileName), dss::actions::serializeActions(config), error);
}

QJsonObject ActionService::configMessage() const
{
    dss::ActionsConfigInfo info;
    if (m_configError.isEmpty())
        info.config = m_config;
    info.fileError = m_configError;
    return dss::actionsConfigMessage(info);
}

dss::ActionsStatus ActionService::status() const
{
    dss::ActionsStatus s;
    if (!m_configError.isEmpty()) {
        s.state = QStringLiteral("file_error");
        s.message = QStringLiteral("aktionen.json ungültig, Chat-Bot aus: ") + m_configError;
    } else if (m_muted) {
        s.state = QStringLiteral("muted");
        s.message = QStringLiteral("stumm: Version im Einsatz läuft");
    } else if (!m_config.enabled) {
        s.state = QStringLiteral("off");
        s.message = QStringLiteral("Chat-Bot aus");
    } else {
        s.state = QStringLiteral("on");
        s.message = QStringLiteral("Chat-Bot aktiv");
    }
    // Hauptschalter auch bei «stumm» melden, die Kopfzeile zeigt dort den echten Stand
    s.enabled = m_configError.isEmpty() && m_config.enabled;
    const QList<dss::actions::RunRecord> runs = m_engine->history();
    const auto queueStatus = [&](const QUuid &id, const QString &name, bool blocking) {
        dss::ActionsQueueStatus q{id, name, blocking, m_engine->isQueuePaused(id), 0, 0};
        for (const dss::actions::RunRecord &r : runs) {
            if (r.queue != id)
                continue;
            if (r.state == dss::actions::RunState::Waiting)
                ++q.waiting;
            else if (r.state == dss::actions::RunState::Running)
                ++q.running;
        }
        return q;
    };
    s.queues.append(queueStatus(QUuid(), QStringLiteral("Standard"), false));
    for (const dss::actions::Queue &q : m_config.queues)
        s.queues.append(queueStatus(q.id, q.name, q.blocking));
    s.running = m_engine->runningCount();
    // Konten «kann senden» (Plan 5)
    for (const dss::actions::ChatAccount account : {dss::actions::ChatAccount::Bot, dss::actions::ChatAccount::Broadcaster}) {
        const QString reason = m_chat->unavailableReason(account);
        s.accounts.append({dss::actions::chatAccountKey(account), reason.isEmpty(), reason});
    }
    return s;
}

void ActionService::broadcastStatusIfChanged()
{
    const QJsonObject message = dss::actionsStatusMessage(status());
    if (message == m_lastStatus)
        return;
    m_lastStatus = message;
    emit broadcastRequested(message);
}

QList<QJsonObject> ActionService::helloMessages() const
{
    return {configMessage(), dss::actionsStatusMessage(status())};
}

void ActionService::noteVariablesChanged()
{
    if (m_variables.changed() || m_viewersChanged)
        m_saver->changed();
}

// --- Anfragen ---

void ActionService::handleRequest(const QJsonObject &message, const Reply &reply)
{
    dss::ActionsRequest request;
    QString error;
    if (!dss::parseActionsRequest(message, &request, &error)) {
        reply(result(std::max<qint64>(dss::messageRequestId(message), 0), false,
                     QString::fromLatin1(dss::err::kInvalidMessage), error));
        return;
    }
    const QString &type = request.type;
    const qint64 id = request.requestId;
    if (dss::actions::isConfigEditType(type)) {
        handleEdit(request, reply);
    } else if (type == QLatin1String(dss::msg::kVariableSet) || type == QLatin1String(dss::msg::kVariableRemove)) {
        handleVariableEdit(request, reply);
    } else if (type == QLatin1String(dss::msg::kQuotePut) || type == QLatin1String(dss::msg::kQuoteRemove)) {
        handleQuoteEdit(request, reply);
    } else if (type == QLatin1String(dss::msg::kQueueControl)) {
        handleQueueControl(request, reply);
    } else if (type == QLatin1String(dss::msg::kActionTest)) {
        handleTest(request, reply);
    } else if (type == QLatin1String(dss::msg::kActionRunCancel)) {
        const QUuid run = *dss::uuidField(request.fields, QStringLiteral("run_id"), false, &error);
        if (!m_engine->cancel(run, QStringLiteral("abgebrochen")))
            reply(result(id, false, QString::fromLatin1(dss::err::kUnknownRun), QStringLiteral("Der Lauf läuft nicht (mehr)")));
        else
            reply(result(id, true));
    } else if (type == QLatin1String(dss::msg::kChatSend)) {
        handleChatSend(request, reply);
    } else if (type == QLatin1String(dss::msg::kActionRunDetails)) {
        const QUuid run = *dss::uuidField(request.fields, QStringLiteral("run_id"), false, &error);
        const std::optional<dss::actions::RunRecord> record = m_engine->record(run);
        if (!record)
            reply(result(id, false, QString::fromLatin1(dss::err::kUnknownRun), QStringLiteral("Lauf unbekannt")));
        else
            reply(dss::actionRunMessage(id, *record));
    } else if (type == QLatin1String(dss::msg::kGetActionRuns)) {
        reply(dss::actionRunsMessage(id, m_engine->history()));
    } else if (type == QLatin1String(dss::msg::kGetVariables)) {
        dss::VariablesInfo info{m_variables.data(), m_variables.volatileGlobal(), m_variables.volatileUsers(),
                                m_variablesError};
        reply(dss::variablesMessage(id, info));
    } else if (type == QLatin1String(dss::msg::kGetViewers)) {
        dss::actions::ViewersData found;
        for (const QJsonValue &v : request.fields.value(QStringLiteral("user_ids")).toArray()) {
            const auto it = m_viewers.viewers.constFind(v.toString());
            if (it != m_viewers.viewers.cend())
                found.viewers.insert(it.key(), it.value());
        }
        reply(dss::viewersMessage(id, found));
    } else if (type == QLatin1String(dss::msg::kGetQuotes)) {
        reply(dss::quotesMessage(id, m_quotes));
    } else if (type == QLatin1String(dss::msg::kViewerImport)) {
        handleViewerImport(request, reply);
    } else if (type == QLatin1String(dss::msg::kTestEvent)) {
        handleTestEvent(request, reply);
    }
}

void ActionService::handleEdit(const dss::ActionsRequest &request, const Reply &reply)
{
    if (!m_configError.isEmpty()) {
        reply(result(request.requestId, false, QString::fromLatin1(dss::err::kActionsFileError),
                     QStringLiteral("Nicht gespeichert, aktionen.json ist ungültig: ") + m_configError));
        return;
    }
    dss::actions::ActionsConfig next = m_config;
    const dss::actions::ConfigEditResult r = dss::actions::applyConfigEdit(&next, request.type, request.fields);
    if (!r.ok) {
        reply(rejected(request.requestId, r.message));
        return;
    }
    QString error;
    if (!writeConfig(next, &error)) {
        qCritical("Aktionen: aktionen.json nicht gespeichert: %s", qPrintable(error));
        reply(result(request.requestId, false, QString::fromLatin1(dss::err::kSaveFailed),
                     QStringLiteral("Nicht gespeichert: ") + error));
        return;
    }
    const dss::actions::ActionsConfig previous = m_config;
    m_config = next;
    // Vor applyConfig: dessen pump() soll keinen wartenden Lauf einer abgeschalteten Aktion starten
    cancelRunsAfterEdit(previous);
    applyConfig();
    qInfo("Aktionen gespeichert: %s «%s»", qPrintable(request.type), qPrintable(r.name));
    emit broadcastRequested(configMessage());
    reply(result(request.requestId, true, {}, {}, r.created));
    broadcastStatusIfChanged();
}

// Läufe abgeschalteter oder gelöschter Aktionen enden sofort, wartende werden verworfen; der
// Hauptschalter aus beendet alle Läufe (Entscheid Nutzer 2026-11-16, P14 e2-1)
void ActionService::cancelRunsAfterEdit(const dss::actions::ActionsConfig &previous)
{
    int count = 0;
    if (previous.enabled && !m_config.enabled) {
        count = m_engine->cancelAll(QStringLiteral("abgebrochen: Chat-Bot aus"));
    } else {
        QHash<QUuid, QString> messages;
        for (const dss::actions::Action &old : previous.actions) {
            const dss::actions::Action *now = m_config.action(old.id);
            if (!now)
                messages.insert(old.id, QStringLiteral("abgebrochen: Aktion gelöscht"));
            else if (old.enabled && !now->enabled)
                messages.insert(old.id, QStringLiteral("abgebrochen: Aktion abgeschaltet"));
        }
        if (!messages.isEmpty())
            count = m_engine->cancelActions(messages);
    }
    if (count > 0)
        qInfo("Aktionen: %d Läufe abgebrochen", count);
}

void ActionService::handleVariableEdit(const dss::ActionsRequest &request, const Reply &reply)
{
    const QJsonObject &f = request.fields;
    dss::actions::VariableKey key;
    const QString scope = f.value(QStringLiteral("scope")).toString();
    if (scope == QLatin1String("user")) {
        key.scope = dss::actions::VariableScope::User;
        key.userId = f.value(QStringLiteral("user_id")).toString();
    } else if (scope != QLatin1String("global")) {
        reply(rejected(request.requestId, QStringLiteral("scope muss global oder user sein")));
        return;
    }
    key.name = f.value(QStringLiteral("name")).toString();
    key.persisted = f.value(QStringLiteral("persisted")).toBool();
    if (key.persisted && !m_variablesError.isEmpty()) {
        reply(result(request.requestId, false, QString::fromLatin1(dss::err::kActionsFileError),
                     QStringLiteral("Nicht gespeichert, variablen.json ist ungültig: ") + m_variablesError));
        return;
    }
    if (request.type == QLatin1String(dss::msg::kVariableSet)) {
        const QString error = m_variables.set(key, f.value(QStringLiteral("value")), m_clock.now());
        if (!error.isEmpty()) {
            reply(rejected(request.requestId, error));
            return;
        }
    } else if (!m_variables.remove(key)) {
        reply(rejected(request.requestId, QStringLiteral("Variable «%1» gibt es nicht").arg(key.name)));
        return;
    }
    noteVariablesChanged();
    reply(result(request.requestId, true));
}

void ActionService::handleQuoteEdit(const dss::ActionsRequest &request, const Reply &reply)
{
    if (!m_quotesError.isEmpty()) {
        reply(result(request.requestId, false, QString::fromLatin1(dss::err::kActionsFileError),
                     QStringLiteral("Nicht gespeichert, zitate.json ist ungültig: ") + m_quotesError));
        return;
    }
    const qint64 index = qint64(request.fields.value(QStringLiteral("index")).toDouble());
    dss::actions::QuotesData next = m_quotes;
    if (request.type == QLatin1String(dss::msg::kQuotePut)) {
        // index -1 (oder Anzahl) = neues Zitat am Ende, sonst ersetzen
        if (index < -1 || index > next.quotes.size()) {
            reply(rejected(request.requestId, QStringLiteral("Kein Zitat mit der Nummer %1").arg(index)));
            return;
        }
        const QJsonObject quote = request.fields.value(QStringLiteral("quote")).toObject();
        if (index == -1 || index == next.quotes.size())
            next.quotes.append(quote);
        else
            next.quotes[index] = quote;
    } else {
        if (index < 0 || index >= next.quotes.size()) {
            reply(rejected(request.requestId, QStringLiteral("Kein Zitat mit der Nummer %1").arg(index)));
            return;
        }
        next.quotes.removeAt(index);
    }
    QString error;
    // Prüfen wie beim Lesen, dann atomar schreiben
    dss::actions::QuotesData checked;
    const QByteArray bytes = dss::actions::serializeQuotes(next);
    if (!dss::actions::parseQuotes(bytes, &checked, &error)) {
        reply(rejected(request.requestId, error));
        return;
    }
    if (!writeFile(filePath(dss::actions::kQuotesFileName), bytes, &error)) {
        reply(result(request.requestId, false, QString::fromLatin1(dss::err::kSaveFailed),
                     QStringLiteral("Nicht gespeichert: ") + error));
        return;
    }
    m_quotes = checked;
    reply(result(request.requestId, true));
}

// Zuschauer-Import aus Streamer.bot (P14 z1b, Plan 9.4): nimmt den Stand aus dem Speicher (sonst
// überschriebe der SaveScheduler die importierten Dateien) und gibt den neuen dorthin zurück.
// Protokoll und Antwort nennen nur Anzahlen und Namen von Variablen (Plan 2.10).
void ActionService::handleViewerImport(const dss::ActionsRequest &request, const Reply &reply)
{
    const qint64 id = request.requestId;
    const bool dryRun = request.fields.value(QStringLiteral("dry_run")).toBool();
    const QString path = request.fields.value(QStringLiteral("path")).toString();
    for (const QString &fileError : {m_configError, m_variablesError, m_viewersError, m_quotesError}) {
        if (!fileError.isEmpty()) {
            reply(result(id, false, QString::fromLatin1(dss::err::kActionsFileError),
                         QStringLiteral("Kein Import, Datei ungültig: ") + fileError));
            return;
        }
    }
    if (!QDir::isAbsolutePath(path)) {
        reply(rejected(id, QStringLiteral("Der Ordner des Exports muss ein absoluter Pfad sein")));
        return;
    }
    QString error;
    dss::actions::StreamerbotExport source;
    if (!dss::actions::readStreamerbotExport(path, &source, &error)) {
        reply(rejected(id, QStringLiteral("Export nicht lesbar: ") + error));
        return;
    }
    // Ungespeicherte Variablen und Zuschauer zuerst schreiben: Datei = Speicher, die Sicherung
    // enthält damit den Stand vor dem Import
    if (!dryRun && !savePending(&error)) {
        reply(result(id, false, QString::fromLatin1(dss::err::kSaveFailed), QStringLiteral("Nicht gespeichert: ") + error));
        return;
    }
    // Archiv aus der Datei (nur der Import schreibt es), alles andere aus dem Speicher
    dss::actions::ImportState before;
    if (!dss::actions::loadImportState(m_options.dataDir, &before, &error)) {
        reply(result(id, false, QString::fromLatin1(dss::err::kActionsFileError),
                     QStringLiteral("Kein Import, Datei ungültig: ") + error));
        return;
    }
    before.variables = m_variables.data();
    before.viewers = m_viewers;
    before.quotes = m_quotes;
    if (before.actions)
        before.actions = m_config;

    const QDateTime now = m_clock.now().toUTC();
    const dss::actions::ImportResult planned = dss::actions::planViewerImport(
        source, before, {request.fields.value(QStringLiteral("overwrite")).toBool(), now});
    dss::ViewerImportResult answer;
    answer.dryRun = dryRun;
    answer.report = dss::actions::importReportText(planned.report, !dryRun);
    if (dryRun) {
        qInfo("Zuschauer-Import: Probelauf, nichts geschrieben");
        reply(dss::viewerImportResultMessage(id, answer));
        return;
    }

    // Zwei Importe in derselben Sekunde: Nummer anhängen (writeViewerImport verlangt einen neuen Ordner)
    const QDir data(m_options.dataDir);
    const QString baseName = QStringLiteral("import/") + dss::actions::importBackupDirName(now);
    QString backupName = baseName;
    for (int n = 2; QFileInfo::exists(data.filePath(backupName)); ++n)
        backupName = QStringLiteral("%1-%2").arg(baseName).arg(n);
    if (!dss::actions::writeViewerImport(m_options.dataDir, data.filePath(backupName), before,
                                         planned.state, &answer.written, &error)) {
        qCritical("Zuschauer-Import: nicht geschrieben: %s", qPrintable(error));
        reply(result(id, false, QString::fromLatin1(dss::err::kSaveFailed), QStringLiteral("Nicht geschrieben: ") + error));
        return;
    }
    if (!answer.written.isEmpty())
        answer.backup = backupName;
    m_variables.setData(planned.state.variables);
    m_variables.clearChanged();
    m_viewers = planned.state.viewers;
    m_viewersChanged = false;
    m_quotes = planned.state.quotes;
    const bool configChanged = planned.state.actions && *planned.state.actions != m_config;
    if (configChanged) {
        m_config = *planned.state.actions;
        applyConfig();
    }
    qInfo("Zuschauer-Import: endgültig, %lld Dateien geschrieben (%s), Sicherung %s",
          static_cast<long long>(answer.written.size()), qPrintable(answer.written.join(QStringLiteral(", "))),
          answer.backup.isEmpty() ? "keine" : qPrintable(answer.backup));
    if (configChanged)
        emit broadcastRequested(configMessage());
    reply(dss::viewerImportResultMessage(id, answer));
    broadcastStatusIfChanged();
}

void ActionService::handleQueueControl(const dss::ActionsRequest &request, const Reply &reply)
{
    QString error;
    const QUuid queue = *dss::uuidField(request.fields, QStringLiteral("queue_id"), true, &error);
    if (!queue.isNull() && !m_config.queue(queue)) {
        reply(rejected(request.requestId, QStringLiteral("Warteschlange unbekannt")));
        return;
    }
    const QString control = request.fields.value(QStringLiteral("control")).toString();
    if (control == QLatin1String("pause")) {
        m_engine->pauseQueue(queue);
    } else if (control == QLatin1String("resume")) {
        m_engine->resumeQueue(queue);
    } else if (control == QLatin1String("clear")) {
        m_engine->clearQueue(queue);
    } else {
        reply(rejected(request.requestId, QStringLiteral("control muss pause, resume oder clear sein")));
        return;
    }
    reply(result(request.requestId, true));
    broadcastStatusIfChanged();
}

void ActionService::handleTest(const dss::ActionsRequest &request, const Reply &reply)
{
    QString error;
    const QUuid actionId = *dss::uuidField(request.fields, QStringLiteral("action_id"), false, &error);
    const bool dryRun = request.fields.value(QStringLiteral("dry_run")).toBool();
    if (!m_configError.isEmpty()) {
        reply(result(request.requestId, false, QString::fromLatin1(dss::err::kActionsFileError),
                     QStringLiteral("aktionen.json ist ungültig: ") + m_configError));
        return;
    }
    if (!m_config.action(actionId)) {
        reply(result(request.requestId, false, QString::fromLatin1(dss::err::kUnknownAction),
                     QStringLiteral("Aktion unbekannt")));
        return;
    }
    // Stumm (Plan 3.4): nach aussen handelt nur die Version im Einsatz; Probeläufe gehen weiter
    if (m_muted && !dryRun) {
        reply(rejected(request.requestId, QStringLiteral("stumm: Version im Einsatz läuft, nur Probelauf möglich")));
        return;
    }
    dss::actions::Arguments arguments;
    const QJsonObject args = request.fields.value(QStringLiteral("arguments")).toObject();
    for (auto it = args.begin(); it != args.end(); ++it)
        arguments.insert(it.key(), it.value());
    const QUuid run = m_engine->enqueue(
        {actionId, QString::fromLatin1(dss::actions::kTriggerTest), QUuid(), arguments, dryRun});
    reply(result(request.requestId, true, {}, {}, run));
}

// Chat senden aus dem Fenster (Version 18, Plan 5, e2-2): Handarbeit des Nutzers, darum auch bei
// «Chat-Bot aus»; stumm abgewiesen (Plan 3.4). Kein Ersatzkonto, keine Kürzung: zu lange Texte werden
// abgewiesen. Die Antwort sagt nur «angenommen»; das Ergebnis kommt als twitch.chat_sent oder
// twitch.chat_dropped über publishChat. Der Text kommt nicht ins Journal (Plan 3.5).
void ActionService::handleChatSend(const dss::ActionsRequest &request, const Reply &reply)
{
    const QString text = request.fields.value(QStringLiteral("text")).toString();
    const QString accountKey = request.fields.value(QStringLiteral("account")).toString();
    const QString replyTo = request.fields.value(QStringLiteral("reply_to")).toString();
    std::optional<dss::actions::ChatAccount> account;
    for (const dss::actions::ChatAccount a : {dss::actions::ChatAccount::Bot, dss::actions::ChatAccount::Broadcaster}) {
        if (accountKey == dss::actions::chatAccountKey(a))
            account = a;
    }
    const QString trimmed = text.trimmed();
    QString problem;
    if (!account)
        problem = QStringLiteral("account muss bot oder broadcaster sein");
    else if (trimmed.isEmpty())
        problem = QStringLiteral("Die Nachricht ist leer");
    else if (text.size() > kChatSendMaxLength)
        problem = QStringLiteral("Die Nachricht ist länger als %1 Zeichen").arg(kChatSendMaxLength);
    else if (trimmed.startsWith(QLatin1Char('/')) || trimmed.startsWith(QLatin1Char('.')))
        problem = QStringLiteral("Twitch-Befehle sind nicht möglich");
    else if (m_muted)
        problem = QStringLiteral("stumm: Version im Einsatz läuft");
    else
        problem = m_chat->unavailableReason(*account);
    if (!problem.isEmpty()) {
        reply(rejected(request.requestId, problem));
        return;
    }
    dss::actions::ChatRequest chat;
    chat.text = text;
    chat.useBot = *account == dss::actions::ChatAccount::Bot;
    chat.fallback = false;
    chat.replyTo = replyTo;
    // Erst antworten: das Ergebnis darf noch im Aufruf von sendChat kommen
    reply(result(request.requestId, true));
    m_chat->sendChat(chat, {});
}

void ActionService::handleTestEvent(const dss::ActionsRequest &request, const Reply &reply)
{
    if (!m_options.testEvents) {
        reply(result(request.requestId, false, QString::fromLatin1(dss::err::kUnknownType),
                     QStringLiteral("test_event gibt es nur mit dss-core --testereignisse")));
        return;
    }
    dss::events::Event event;
    QString error;
    if (!dss::events::eventFromJson(request.fields.value(QStringLiteral("event")).toObject(), &event, &error)) {
        reply(rejected(request.requestId, error));
        return;
    }
    if (!m_bus->publish(event)) {
        reply(rejected(request.requestId, dss::events::EventBus::validate(event)));
        return;
    }
    reply(result(request.requestId, true));
}

// --- Ereignisse und Läufe ---

void ActionService::onEvent(const dss::events::Event &event)
{
    // Zuschauer nachführen (Plan 2.10): nur Login und Anzeigename
    if (event.type == QLatin1String(dss::actions::kTriggerChatMessage))
        noteViewer(event.data.value(QStringLiteral("user_id")).toString(),
                   event.data.value(QStringLiteral("user_login")).toString(),
                   event.data.value(QStringLiteral("user_name")).toString(), false);
    m_dispatcher->setMuted(m_muted);
    const dss::actions::TriggerEvent trigger{event.type, event.time, event.originId, event.data};
    // Eigener Kanal und «vom Studio gesendet» (Plan 4.1, c1a)
    dss::actions::TriggerContext context;
    if (m_options.broadcasterId)
        context.broadcasterId = m_options.broadcasterId();
    context.internal = m_chat->isInternal(event.data.value(QStringLiteral("message_id")).toString(),
                                          event.data.value(QStringLiteral("user_id")).toString(),
                                          event.data.value(QStringLiteral("text")).toString());
    const dss::actions::TriggerResult r = m_dispatcher->handleEvent(trigger, context);
    dss::actions::submit(*m_engine, r);
    noteVariablesChanged();
}

void ActionService::noteViewer(const QString &userId, const QString &login, const QString &displayName, bool save)
{
    if (!m_viewersError.isEmpty() || !dss::actions::noteViewer(&m_viewers, userId, login, displayName))
        return;
    m_viewersChanged = true;
    if (save)
        noteVariablesChanged();
}

void ActionService::publishRun(const char *type, const dss::actions::RunRecord &record)
{
    dss::events::Event event;
    event.type = QString::fromLatin1(type);
    event.data = dss::runEventData(record);
    m_bus->publish(event);
}

// Ergebnis von «Chat senden» auf dem Fluss (Plan 4.1). Nur an die Fenster, nicht ins Journal.
void ActionService::publishChat(const dss::actions::ChatOutcome &outcome)
{
    dss::events::Event event;
    event.type = outcome.sent ? QStringLiteral("twitch.chat_sent") : QStringLiteral("twitch.chat_dropped");
    event.data = {{QStringLiteral("account"), dss::actions::chatAccountKey(outcome.account)},
                  {QStringLiteral("user_id"), outcome.senderId},
                  {QStringLiteral("text"), outcome.text},
                  {QStringLiteral("reply_to"), outcome.replyTo.isEmpty() ? QJsonValue() : QJsonValue(outcome.replyTo)},
                  {QStringLiteral("truncated"), outcome.truncated}};
    if (outcome.sent) {
        event.data.insert(QStringLiteral("message_id"), outcome.messageId);
    } else {
        event.data.insert(QStringLiteral("code"), outcome.code);
        event.data.insert(QStringLiteral("message"), outcome.message);
        qInfo("Chat nicht gesendet (%s): %s", qPrintable(event.data.value(QStringLiteral("account")).toString()),
              qPrintable(outcome.code));
    }
    if (!m_bus->publish(event))
        qWarning("Aktionen: %s abgewiesen: %s", qPrintable(event.type), qPrintable(dss::events::EventBus::validate(event)));
}

void ActionService::onRunStarted(const dss::actions::RunRecord &record)
{
    publishRun("actions.run_started", record);
    broadcastStatusIfChanged();
}

void ActionService::onRunChanged(const dss::actions::RunRecord &record)
{
    publishRun("actions.run_changed", record);
    broadcastStatusIfChanged();
}

void ActionService::onRunFinished(const dss::actions::RunRecord &record)
{
    publishRun("actions.run_finished", record);
    // Journal (Plan 3.5): Name der Aktion, Ergebnis, Fehler; keine Chat-Texte, keine Zuschauernamen.
    // Die Meldungen der Schritte im Lauf (mit eingesetzten Texten) bleiben im Speicher.
    const QString state = dss::actions::runStateKey(record.state);
    qInfo("Lauf «%s»%s: %s%s%s", qPrintable(record.actionName), record.dryRun ? " (Probelauf)" : "",
          qPrintable(state), record.message.isEmpty() ? "" : ", ", qPrintable(record.message));
    for (const dss::actions::StepLogEntry &e : record.log) {
        if (e.result != dss::actions::StepLogEntry::Result::Error)
            continue;
        qInfo("  Schritt %s (%s): Fehler", qPrintable(pathText(e.path)), qPrintable(e.type));
    }
    noteVariablesChanged();
    broadcastStatusIfChanged();
}

// --- Stumm-Regel (Plan 3.4) ---

void ActionService::probeInUse()
{
    if (m_probe)
        return;
    auto *probe = new QLocalSocket(this);
    m_probe = probe;
    auto buffer = std::make_shared<QByteArray>();
    const auto finish = [this, probe](bool running) {
        if (m_probe != probe)
            return;
        m_probe = nullptr;
        probe->abort();
        probe->deleteLater();
        setMuted(running);
    };
    connect(probe, &QLocalSocket::errorOccurred, this, [finish] { finish(false); });
    connect(probe, &QLocalSocket::disconnected, this, [finish] { finish(false); });
    connect(probe, &QLocalSocket::readyRead, this, [probe, buffer, finish] {
        buffer->append(probe->readAll());
        const qsizetype end = buffer->indexOf('\n');
        if (end < 0)
            return;
        QJsonObject message;
        QString error;
        dss::Hello hello;
        finish(dss::decodeMessage(buffer->left(end), &message, &error)
               && dss::messageType(message) == QLatin1String(dss::msg::kHello)
               && dss::parseHello(message, &hello, &error) && hello.program == QLatin1String("dss-core"));
    });
    QTimer::singleShot(kProbeTimeoutMs, probe, [finish] { finish(false); });
    probe->connectToServer(m_options.inUseSocket);
}

void ActionService::setMuted(bool muted)
{
    if (muted == m_muted)
        return;
    m_muted = muted;
    m_dispatcher->setMuted(muted);
    qInfo(muted ? "Aktionen: stumm, die Version im Einsatz läuft" : "Aktionen: nicht mehr stumm");
    broadcastStatusIfChanged();
}
