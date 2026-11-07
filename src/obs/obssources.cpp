#include "obssources.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <memory>

namespace dss::obs {

namespace {

constexpr int kFileFormat = 1;
constexpr char kFormatKey[] = "format";
constexpr char kSourcesKey[] = "quellen";
constexpr char kOverlayKey[] = "overlay";
constexpr char kInputUuidKey[] = "input_uuid";
constexpr char kInputNameKey[] = "input_name";
// Mehrere Ereignisse hintereinander (Anlegen in OBS) lösen einen Abgleich aus.
constexpr int kResyncDelayMs = 200;

const QString kNotConnectedText = QStringLiteral("OBS ist nicht verbunden.");
const QString kAbortedText = QStringLiteral("Verbindung zu OBS abgebrochen. Ob die Quelle angelegt wurde, "
                                            "zeigt sich nach dem Wiederverbinden.");
const QString kTimeoutText = QStringLiteral("OBS hat nicht rechtzeitig geantwortet.");

ObsSources::Result failedRequest(RequestFailure failure)
{
    if (failure == RequestFailure::NotConnected)
        return {ObsSources::Failure::NotConnected, kNotConnectedText};
    if (failure == RequestFailure::Disconnected)
        return {ObsSources::Failure::NotConnected, kAbortedText};
    return {ObsSources::Failure::Timeout, kTimeoutText};
}

} // namespace

ObsSources::ObsSources(ObsConnection *connection, const QString &filePath, QObject *parent)
    : QObject(parent)
    , m_connection(connection)
    , m_filePath(filePath)
{
    m_resyncTimer = new QTimer(this);
    m_resyncTimer->setSingleShot(true);
    m_resyncTimer->setInterval(kResyncDelayMs);
    connect(m_resyncTimer, &QTimer::timeout, this, &ObsSources::startSync);
    load();
    m_connection->setListener(this);
    if (m_connection->isReady())
        onObsReady();
}

ObsSources::~ObsSources()
{
    m_connection->setListener(nullptr);
}

void ObsSources::load()
{
    QFile file(m_filePath);
    if (!file.exists())
        return;
    const auto invalid = [this](const QString &reason) {
        const QString renamed = m_filePath + QStringLiteral(".ungueltig-")
                                + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        qWarning("OBS: %s ungültig (%s), umbenannt in %s", qPrintable(m_filePath), qPrintable(reason),
                 qPrintable(renamed));
        QFile::rename(m_filePath, renamed);
        m_mappings.clear();
    };
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("OBS: %s nicht lesbar: %s", qPrintable(m_filePath), qPrintable(file.errorString()));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (!doc.isObject()) {
        invalid(parseError.errorString());
        return;
    }
    const QJsonObject root = doc.object();
    if (root.value(QLatin1String(kFormatKey)).toInt() != kFileFormat
        || !root.value(QLatin1String(kSourcesKey)).isArray()) {
        invalid(QStringLiteral("Format oder Liste fehlt"));
        return;
    }
    const QJsonArray sources = root.value(QLatin1String(kSourcesKey)).toArray();
    for (const QJsonValue &v : sources) {
        const QJsonObject o = v.toObject();
        const QUuid id = QUuid::fromString(o.value(QLatin1String(kOverlayKey)).toString());
        const QString uuid = o.value(QLatin1String(kInputUuidKey)).toString();
        if (id.isNull() || uuid.isEmpty()) {
            invalid(QStringLiteral("Eintrag ohne Overlay oder Kennung"));
            return;
        }
        m_mappings.insert(id, {uuid, o.value(QLatin1String(kInputNameKey)).toString()});
    }
}

void ObsSources::save() const
{
    // Nach Overlay sortiert, damit die Datei nicht von der Reihenfolge im Speicher abhängt.
    QList<QUuid> ids = m_mappings.keys();
    std::sort(ids.begin(), ids.end());
    QJsonArray sources;
    for (const QUuid &id : std::as_const(ids)) {
        const Mapping &m = m_mappings.value(id);
        sources.append(QJsonObject{{QLatin1String(kOverlayKey), id.toString(QUuid::WithoutBraces)},
                                   {QLatin1String(kInputUuidKey), m.inputUuid},
                                   {QLatin1String(kInputNameKey), m.inputName}});
    }
    const QJsonObject root{{QLatin1String(kFormatKey), kFileFormat}, {QLatin1String(kSourcesKey), sources}};
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson()) < 0 || !file.commit())
        qWarning("OBS: %s nicht geschrieben: %s", qPrintable(m_filePath), qPrintable(file.errorString()));
}

ObsSources::SourceState ObsSources::stateOf(const QUuid &overlayId) const
{
    const auto it = m_mappings.constFind(overlayId);
    if (it == m_mappings.cend())
        return SourceState::None;
    // Noch kein Abgleich: letzter bekannter Stand aus der Datei.
    return !m_synced || m_known.contains(it->inputUuid) ? SourceState::Present : SourceState::Missing;
}

QList<ObsSources::Entry> ObsSources::entries() const
{
    QList<Entry> result;
    result.reserve(m_desired.size());
    for (const Desired &d : m_desired)
        result.append({d.overlayId, stateOf(d.overlayId), m_mappings.value(d.overlayId).inputName});
    return result;
}

QString ObsSources::inputNameOf(const QUuid &overlayId) const
{
    return m_mappings.value(overlayId).inputName;
}

const ObsSources::Desired *ObsSources::desiredOf(const QUuid &overlayId) const
{
    for (const Desired &d : m_desired) {
        if (d.overlayId == overlayId)
            return &d;
    }
    return nullptr;
}

void ObsSources::publish()
{
    const QList<Entry> now = entries();
    if (now == m_lastEntries)
        return;
    m_lastEntries = now;
    emit entriesChanged();
}

void ObsSources::setDesired(const QList<Desired> &desired)
{
    if (desired == m_desired)
        return;
    m_desired = desired;
    QSet<QUuid> ids;
    for (const Desired &d : desired)
        ids.insert(d.overlayId);
    bool removed = false;
    for (auto it = m_mappings.begin(); it != m_mappings.end();) {
        if (ids.contains(it.key())) {
            ++it;
        } else {
            qInfo("OBS: Overlay %s gibt es nicht mehr; Quelle «%s» bleibt in OBS",
                  qPrintable(it.key().toString(QUuid::WithoutBraces)), qPrintable(it->inputName));
            it = m_mappings.erase(it);
            removed = true;
        }
    }
    if (removed)
        save();
    if (m_synced && !m_syncing && m_connection->isReady())
        reconcile();
    publish();
}

void ObsSources::onObsReady()
{
    m_collectionChanging = false;
    refreshScenes();
    startSync();
}

void ObsSources::onObsLost()
{
    // Offene Anfragen dieses Abgleichs sind schon beantwortet; der Stand bleibt der letzte bekannte.
    ++m_syncGeneration;
    m_syncing = false;
    m_resyncTimer->stop();
}

void ObsSources::onObsEvent(const Event &event)
{
    const QString &type = event.type;
    const QString inputUuid = event.data.value(QStringLiteral("inputUuid")).toString();
    if (type == QLatin1String("InputRemoved")) {
        // Beim Wechsel der Szenensammlung verschwinden alle Quellen scheinbar; der Abgleich danach zählt.
        if (!m_collectionChanging && m_known.remove(inputUuid) > 0)
            publish();
    } else if (type == QLatin1String("InputNameChanged")) {
        const QString name = event.data.value(QStringLiteral("inputName")).toString();
        const auto known = m_known.find(inputUuid);
        if (known != m_known.end())
            known->name = name;
        for (auto it = m_mappings.begin(); it != m_mappings.end(); ++it) {
            if (it->inputUuid == inputUuid) {
                it->inputName = name;
                save();
                publish();
                break;
            }
        }
    } else if (type == QLatin1String("InputCreated")) {
        // Von Hand angelegt oder nach einem Abbruch beim Anlegen: vielleicht zu übernehmen (F10).
        if (event.data.value(QStringLiteral("inputKind")).toString() == QLatin1String(kBrowserKind))
            m_resyncTimer->start();
    } else if (type == QLatin1String("SceneListChanged") || type == QLatin1String("SceneNameChanged")) {
        refreshScenes();
    } else if (type == QLatin1String("CurrentSceneCollectionChanging")) {
        m_collectionChanging = true;
    } else if (type == QLatin1String("CurrentSceneCollectionChanged")) {
        m_collectionChanging = false;
        refreshScenes();
        startSync();
    }
}

void ObsSources::refreshScenes()
{
    const QPointer<ObsSources> self(this);
    m_connection->request(QStringLiteral("GetSceneList"), {}, [self](const Response &r, RequestFailure failure) {
        if (!self || failure != RequestFailure::None || !r.ok)
            return;
        const QList<Scene> scenes = parseScenes(r.data);
        if (scenes != self->m_scenes) {
            self->m_scenes = scenes;
            emit self->scenesChanged();
        }
    });
}

void ObsSources::startSync()
{
    m_resyncTimer->stop();
    m_syncing = true;
    const quint64 generation = ++m_syncGeneration;
    const QPointer<ObsSources> self(this);
    const QJsonObject filter{{QStringLiteral("inputKind"), QString::fromLatin1(kBrowserKind)}};
    m_connection->request(QStringLiteral("GetInputList"), filter, [self, generation](const Response &r, RequestFailure failure) {
        if (!self || generation != self->m_syncGeneration)
            return;
        if (failure != RequestFailure::None || !r.ok) {
            qWarning("OBS: Liste der Browserquellen nicht lesbar");
            self->m_syncing = false;
            return;
        }
        const QList<Input> inputs = parseInputs(r.data);
        if (inputs.isEmpty()) {
            self->finishSync({});
            return;
        }
        auto known = std::make_shared<QHash<QString, KnownInput>>();
        auto remaining = std::make_shared<qsizetype>(inputs.size());
        for (const Input &input : inputs) {
            const QJsonObject fields{{QStringLiteral("inputUuid"), input.uuid}};
            self->m_connection->request(QStringLiteral("GetInputSettings"), fields,
                                        [self, generation, input, known, remaining](const Response &settings, RequestFailure f) {
                if (!self || generation != self->m_syncGeneration)
                    return;
                // Antwortet OBS zu dieser Quelle nicht, bleibt sie ohne Adresse; sie ist trotzdem da.
                KnownInput k;
                k.name = input.name;
                if (f == RequestFailure::None && settings.ok) {
                    const QJsonObject o = settings.data.value(QStringLiteral("inputSettings")).toObject();
                    k.url = o.value(QStringLiteral("url")).toString();
                    k.width = o.value(QStringLiteral("width")).toInt();
                    k.height = o.value(QStringLiteral("height")).toInt();
                }
                known->insert(input.uuid, k);
                if (--*remaining == 0)
                    self->finishSync(*known);
            });
        }
    });
}

void ObsSources::finishSync(const QHash<QString, KnownInput> &known)
{
    m_known = known;
    m_synced = true;
    m_syncing = false;
    reconcile();
    publish();
}

void ObsSources::reconcile()
{
    bool changed = false;
    QSet<QString> used;
    for (auto it = m_mappings.cbegin(); it != m_mappings.cend(); ++it) {
        if (m_known.contains(it->inputUuid))
            used.insert(it->inputUuid);
    }
    for (const Desired &d : std::as_const(m_desired)) {
        auto mapping = m_mappings.find(d.overlayId);
        const bool present = mapping != m_mappings.end() && m_known.contains(mapping->inputUuid);
        if (present) {
            // Umbenannt, während das Studio nicht verbunden war
            const QString &name = m_known[mapping->inputUuid].name;
            if (mapping->inputName != name) {
                mapping->inputName = name;
                changed = true;
            }
            continue;
        }
        if (d.url.isEmpty())
            continue;
        // F10: Die ganze Adresse muss stimmen, samt Port und Token. Auch für eine fehlende Quelle
        // unter neuer Kennung und nach einem Abbruch beim Anlegen (kein Duplikat).
        for (auto known = m_known.cbegin(); known != m_known.cend(); ++known) {
            if (known->url == d.url && !used.contains(known.key())) {
                m_mappings.insert(d.overlayId, {known.key(), known->name});
                used.insert(known.key());
                changed = true;
                qInfo("OBS: Quelle «%s» übernommen für Overlay %s", qPrintable(known->name),
                      qPrintable(d.overlayId.toString(QUuid::WithoutBraces)));
                break;
            }
        }
    }
    if (changed)
        save();
}

void ObsSources::createSource(const QUuid &overlayId, const QString &sceneUuid, const QString &name, const Done &done)
{
    if (!m_connection->isReady()) {
        done({Failure::NotConnected, kNotConnectedText});
        return;
    }
    const Desired *desired = desiredOf(overlayId);
    if (!desired || desired->url.isEmpty()) {
        done({Failure::Rejected, QStringLiteral("Für dieses Overlay gibt es keine Adresse.")});
        return;
    }
    if (m_synced && stateOf(overlayId) == SourceState::Present) {
        done({Failure::Rejected, QStringLiteral("Dieses Overlay hat schon eine Quelle in OBS.")});
        return;
    }
    const QString inputName = name.trimmed();
    if (inputName.isEmpty()) {
        done({Failure::Rejected, QStringLiteral("Der Name fehlt.")});
        return;
    }
    const Desired wanted = *desired;
    // Sichtbar wie beim Hinzufügen in OBS (F5). Die Grösse setzt der Kern immer, der Standard von OBS
    // ist 800x600.
    const QJsonObject fields{
        {QStringLiteral("sceneUuid"), sceneUuid},
        {QStringLiteral("inputName"), inputName},
        {QStringLiteral("inputKind"), QString::fromLatin1(kBrowserKind)},
        {QStringLiteral("inputSettings"), QJsonObject{{QStringLiteral("url"), wanted.url},
                                                      {QStringLiteral("width"), wanted.width},
                                                      {QStringLiteral("height"), wanted.height}}},
        {QStringLiteral("sceneItemEnabled"), true},
    };
    const QPointer<ObsSources> self(this);
    m_connection->request(QStringLiteral("CreateInput"), fields,
                          [self, wanted, inputName, done](const Response &r, RequestFailure failure) {
        if (!self)
            return;
        if (failure != RequestFailure::None) {
            done(failedRequest(failure));
            return;
        }
        if (!r.ok) {
            if (r.code == kStatusResourceAlreadyExists) {
                done({Failure::NameTaken, QStringLiteral("Der Name «%1» ist in OBS schon vergeben.").arg(inputName)});
            } else if (r.code == kStatusResourceNotFound) {
                self->refreshScenes();
                done({Failure::SceneMissing, QStringLiteral("Die Szene gibt es in OBS nicht mehr.")});
            } else {
                done({Failure::Rejected, QStringLiteral("OBS hat abgelehnt: %1").arg(r.comment)});
            }
            return;
        }
        const QString uuid = r.data.value(QStringLiteral("inputUuid")).toString();
        if (uuid.isEmpty()) {
            done({Failure::Rejected, QStringLiteral("OBS hat keine Kennung für die Quelle geliefert.")});
            return;
        }
        self->m_known.insert(uuid, {inputName, wanted.url, wanted.width, wanted.height});
        self->m_mappings.insert(wanted.overlayId, {uuid, inputName});
        self->save();
        qInfo("OBS: Quelle «%s» angelegt für Overlay %s", qPrintable(inputName),
              qPrintable(wanted.overlayId.toString(QUuid::WithoutBraces)));
        self->publish();
        done({});
    });
}

void ObsSources::addToScene(const QUuid &overlayId, const QString &sceneUuid, const Done &done)
{
    if (!m_connection->isReady()) {
        done({Failure::NotConnected, kNotConnectedText});
        return;
    }
    const auto mapping = m_mappings.constFind(overlayId);
    if (mapping == m_mappings.cend() || stateOf(overlayId) != SourceState::Present) {
        done({Failure::SourceMissing, QStringLiteral("Die Quelle gibt es in OBS nicht mehr.")});
        return;
    }
    const QJsonObject fields{{QStringLiteral("sceneUuid"), sceneUuid},
                             {QStringLiteral("sourceUuid"), mapping->inputUuid},
                             {QStringLiteral("sceneItemEnabled"), true}};
    const QPointer<ObsSources> self(this);
    m_connection->request(QStringLiteral("CreateSceneItem"), fields, [self, done](const Response &r, RequestFailure failure) {
        if (!self)
            return;
        if (failure != RequestFailure::None) {
            done(failedRequest(failure));
        } else if (r.ok) {
            done({});
        } else if (r.code == kStatusResourceNotFound) {
            // Die Antwort unterscheidet Szene und Quelle nicht (AcquireScene, AcquireSource).
            self->refreshScenes();
            self->startSync();
            done({Failure::SourceMissing, QStringLiteral("Die Quelle oder die Szene gibt es in OBS nicht mehr.")});
        } else {
            done({Failure::Rejected, QStringLiteral("OBS hat abgelehnt: %1").arg(r.comment)});
        }
    });
}

} // namespace dss::obs
