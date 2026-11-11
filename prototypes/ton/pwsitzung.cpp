#include "pwsitzung.h"

#include "zeit.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>

namespace {

QString text(const spa_dict *props, const char *key)
{
    const char *v = props ? spa_dict_lookup(props, key) : nullptr;
    return v ? QString::fromUtf8(v) : QString();
}

uint32_t knotenId(const spa_dict *props, const char *key)
{
    const char *v = props ? spa_dict_lookup(props, key) : nullptr;
    if (!v)
        return UINT32_MAX;
    bool ok = false;
    const uint32_t id = QByteArray(v).toUInt(&ok);
    return ok ? id : UINT32_MAX;
}

// Wert der Metadaten «default» ist JSON wie {"name":"alsa_output…"}; sonst der Text selbst
QString metadatenName(const char *value)
{
    if (!value)
        return {};
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray(value));
    if (d.isObject())
        return d.object().value(QStringLiteral("name")).toString();
    return QString::fromUtf8(value);
}

} // namespace

PwSitzung::PwSitzung() = default;

PwSitzung::~PwSitzung()
{
    if (!m_loop)
        return;
    pw_thread_loop_lock(m_loop);
    if (m_metadaten) {
        spa_hook_remove(&m_metadatenHook);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_metadaten));
    }
    if (m_registry) {
        spa_hook_remove(&m_registryHook);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_registry));
    }
    if (m_core) {
        spa_hook_remove(&m_coreHook);
        pw_core_disconnect(m_core);
    }
    pw_thread_loop_unlock(m_loop);
    pw_thread_loop_stop(m_loop);
    if (m_context)
        pw_context_destroy(m_context);
    pw_thread_loop_destroy(m_loop);
}

bool PwSitzung::verbinden(QString *fehler)
{
    m_loop = pw_thread_loop_new("dss-pw-messen", nullptr);
    if (!m_loop) {
        *fehler = QStringLiteral("pw_thread_loop_new fehlgeschlagen");
        return false;
    }
    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context) {
        *fehler = QStringLiteral("pw_context_new fehlgeschlagen");
        return false;
    }
    if (pw_thread_loop_start(m_loop) < 0) {
        *fehler = QStringLiteral("pw_thread_loop_start fehlgeschlagen");
        return false;
    }

    static const pw_core_events coreEvents = [] {
        pw_core_events e{};
        e.version = PW_VERSION_CORE_EVENTS;
        e.done = &PwSitzung::onDone;
        e.error = &PwSitzung::onError;
        return e;
    }();
    static const pw_registry_events registryEvents = [] {
        pw_registry_events e{};
        e.version = PW_VERSION_REGISTRY_EVENTS;
        e.global = &PwSitzung::onGlobal;
        e.global_remove = &PwSitzung::onGlobalRemove;
        return e;
    }();

    pw_thread_loop_lock(m_loop);
    m_core = pw_context_connect(m_context, nullptr, 0);
    if (m_core) {
        pw_core_add_listener(m_core, &m_coreHook, &coreEvents, this);
        m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
        if (m_registry)
            pw_registry_add_listener(m_registry, &m_registryHook, &registryEvents, this);
    }
    pw_thread_loop_unlock(m_loop);
    if (!m_core || !m_registry) {
        *fehler = QStringLiteral("keine Verbindung zu PipeWire (pw_context_connect)");
        return false;
    }
    // Erste Runde: alle Globals; zweite Runde: Eigenschaften der Metadaten «default» (gebunden
    // während der ersten Runde)
    if (!abgleich() || !abgleich()) {
        *fehler = QStringLiteral("PipeWire antwortet nicht (pw_core_sync)");
        return false;
    }
    return true;
}

bool PwSitzung::abgleich(int zeitS)
{
    pw_thread_loop_lock(m_loop);
    m_fertig = false;
    m_wartetSeq = pw_core_sync(m_core, PW_ID_CORE, 0);
    const int64_t frist = steadyNs() + int64_t(zeitS) * 1'000'000'000;
    // Rückgabewert von timed_wait ist im Header nicht beschrieben, deshalb eigene Frist
    while (!m_fertig && steadyNs() < frist)
        pw_thread_loop_timed_wait(m_loop, 1);
    const bool ok = m_fertig;
    pw_thread_loop_unlock(m_loop);
    return ok;
}

void PwSitzung::sperren()
{
    pw_thread_loop_lock(m_loop);
}

void PwSitzung::entsperren()
{
    pw_thread_loop_unlock(m_loop);
}

uint32_t PwSitzung::knotenNachName(const QString &name, const QString &klasse) const
{
    for (const auto &[id, k] : m_knoten)
        if (k.name == name && k.klasse.startsWith(klasse))
            return id;
    return UINT32_MAX;
}

void PwSitzung::onGlobal(void *data, uint32_t id, uint32_t, const char *type, uint32_t, const spa_dict *props)
{
    auto *s = static_cast<PwSitzung *>(data);
    const QByteArray t(type);
    if (t == PW_TYPE_INTERFACE_Node) {
        s->m_knoten[id] = {text(props, PW_KEY_MEDIA_CLASS), text(props, PW_KEY_NODE_NAME),
                           text(props, PW_KEY_NODE_DESCRIPTION), text(props, PW_KEY_OBJECT_SERIAL)};
    } else if (t == PW_TYPE_INTERFACE_Link) {
        const PwVerbindung v{knotenId(props, PW_KEY_LINK_OUTPUT_NODE), knotenId(props, PW_KEY_LINK_INPUT_NODE)};
        s->m_verbindungen[id] = v;
        if (s->neueVerbindung)
            s->neueVerbindung(v);
    } else if (t == PW_TYPE_INTERFACE_Metadata && !s->m_metadaten
               && text(props, "metadata.name") == QLatin1String("default")) {
        static const pw_metadata_events metadatenEvents = [] {
            pw_metadata_events e{};
            e.version = PW_VERSION_METADATA_EVENTS;
            e.property = &PwSitzung::onProperty;
            return e;
        }();
        s->m_metadaten = static_cast<pw_metadata *>(
            pw_registry_bind(s->m_registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
        if (s->m_metadaten)
            pw_metadata_add_listener(s->m_metadaten, &s->m_metadatenHook, &metadatenEvents, s);
    }
}

void PwSitzung::onGlobalRemove(void *data, uint32_t id)
{
    auto *s = static_cast<PwSitzung *>(data);
    s->m_knoten.erase(id);
    s->m_verbindungen.erase(id);
}

void PwSitzung::onDone(void *data, uint32_t id, int seq)
{
    auto *s = static_cast<PwSitzung *>(data);
    if (id == PW_ID_CORE && seq == s->m_wartetSeq) {
        s->m_fertig = true;
        pw_thread_loop_signal(s->m_loop, false);
    }
}

void PwSitzung::onError(void *data, uint32_t id, int seq, int res, const char *message)
{
    auto *s = static_cast<PwSitzung *>(data);
    s->m_fehler = QStringLiteral("Fehler von PipeWire: Objekt %1, seq %2, %3 (%4)")
                      .arg(id).arg(seq).arg(res).arg(QString::fromUtf8(message ? message : ""));
    pw_thread_loop_signal(s->m_loop, false);
}

int PwSitzung::onProperty(void *data, uint32_t subject, const char *key, const char *, const char *value)
{
    auto *s = static_cast<PwSitzung *>(data);
    if (subject != PW_ID_CORE || !key)
        return 0;
    const QByteArray k(key);
    if (k == "default.audio.sink")
        s->m_standardSenke = metadatenName(value);
    else if (k == "default.audio.source")
        s->m_standardQuelle = metadatenName(value);
    return 0;
}

QString geraeteListe(PwSitzung &s)
{
    QString t;
    s.sperren();
    auto liste = [&](const QString &ueberschrift, bool ausgaenge, const QString &standard) {
        t += ueberschrift;
        for (const auto &[id, k] : s.knoten()) {
            const bool passt = ausgaenge ? k.klasse == QLatin1String("Audio/Sink")
                                         : k.klasse.startsWith(QLatin1String("Audio/Source"));
            if (!passt)
                continue;
            t += QStringLiteral("  %1 «%2»%3, id %4, object.serial %5%6\n")
                     .arg(k.name, k.beschreibung, k.name == standard ? QStringLiteral(" [Standard]") : QString())
                     .arg(id)
                     .arg(k.serial)
                     .arg(ausgaenge ? QString() : QStringLiteral(", media.class ") + k.klasse);
        }
    };
    liste(QStringLiteral("Ausgänge (Registry, media.class Audio/Sink):\n"), true, s.standardSenke());
    liste(QStringLiteral("Eingänge (Registry, media.class Audio/Source…):\n"), false, s.standardQuelle());
    t += QStringLiteral("Standard (Metadaten «default»): default.audio.sink %1, default.audio.source %2\n")
             .arg(s.standardSenke().isEmpty() ? QStringLiteral("?") : s.standardSenke(),
                  s.standardQuelle().isEmpty() ? QStringLiteral("?") : s.standardQuelle());
    if (!s.fehlerText().isEmpty())
        t += s.fehlerText() + QLatin1Char('\n');
    s.entsperren();
    return t;
}
