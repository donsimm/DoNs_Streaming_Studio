// Ausgabe eines Mischers über PipeWire (P12b b3), siehe pwoutput.h.
//
// Quellen (lokal gelesen 2026-11-12, libpipewire-0.3-dev 1.4.2 und wireplumber 0.5.8, kein
// Webzugang; https://docs.pipewire.org/group__pw__stream.html nicht abgerufen):
//   /usr/include/pipewire-0.3/pipewire/stream.h
//     :146-168  Abruf «process» für Wiedergabe: dequeue, füllen, queue im selben Abruf
//     :254-278  pw_buffer.size (Summe ergibt pw_time.queued, hier Rahmen), requested
//     :291-404  struct pw_time (now, rate, ticks, delay, queued, buffered, size) und Formel der
//               Verzögerung; pw_time.now und pw_stream_get_nsec() sind vergleichbar (:372-377, :594-596)
//     :456-497  Flags AUTOCONNECT, MAP_BUFFERS, RT_PROCESS, DONT_RECONNECT
//     :538-555  pw_stream_connect: Ziel über PW_KEY_TARGET_OBJECT (node.name oder object.serial)
//     :591-596  pw_stream_get_time_n und pw_stream_get_nsec sind RT-sicher
//   /usr/include/pipewire-0.3/pipewire/thread-loop.h:44-67 (Sperre, Rückrufe mit Sperre, Abruf mit
//     RT_PROCESS ohne Sperre)
//   /usr/include/pipewire-0.3/pipewire/loop.h:115-141 (Ereignis und Timer in der Schleife)
//   /usr/include/pipewire-0.3/pipewire/keys.h (node.latency, node.dont-reconnect, target.object)
//   /usr/include/spa-0.2/spa/node/io.h:139-141 (spa_io_clock.nsec: CLOCK_MONOTONIC)
//   /usr/share/wireplumber/scripts/linking/find-defined-target.lua:36-128 (target.object nach
//     node.name; node.dont-fallback: fehlt das Ziel, kein anderes Gerät, Fehler an den Strom;
//     node.dont-move: Metadaten «target.object» werden nicht beachtet)
//   /usr/share/wireplumber/scripts/linking/prepare-link.lua:27-120 (node.dont-reconnect: kein
//     Umhängen, wenn das Ziel verschwindet)
//   Prototyp prototypes/ton/pwsitzung.cpp und pwklick.cpp (b1, Z3)
#include "pwoutput.h"

#include "mixer.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <chrono>
#include <mutex>

namespace dss {

namespace {

constexpr uint32_t kStride = sizeof(float) * Mixer::kChannels;
constexpr int64_t kRetryNs = 5'000'000'000; // nach einem Fehler des Stroms
const QLatin1String kSinkClass("Audio/Sink");

int64_t steadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

QString text(const spa_dict *props, const char *key)
{
    const char *v = props ? spa_dict_lookup(props, key) : nullptr;
    return v ? QString::fromUtf8(v) : QString();
}

uint32_t nodeId(const spa_dict *props, const char *key)
{
    const char *v = props ? spa_dict_lookup(props, key) : nullptr;
    if (!v)
        return UINT32_MAX;
    bool ok = false;
    const uint32_t id = QByteArray(v).toUInt(&ok);
    return ok ? id : UINT32_MAX;
}

// Wert der Metadaten «default» ist JSON wie {"name":"alsa_output…"}; sonst der Text selbst
QString metadataName(const char *value)
{
    if (!value)
        return {};
    const QJsonDocument d = QJsonDocument::fromJson(QByteArray(value));
    if (d.isObject())
        return d.object().value(QStringLiteral("name")).toString();
    return QString::fromUtf8(value);
}

} // namespace

struct PwOutput::Rt
{
    std::atomic<pw_stream *> stream{nullptr};
    std::atomic<bool> allowed{false};    // Verbindung zum gewählten Knoten bestätigt
    std::atomic<bool> resetTicks{false}; // neuer Strom oder wieder STREAMING
    std::atomic<uint64_t> pulls{0}, emptyPulls{0}, silentPulls{0}, abortedCycles{0}, clockOutside{0};
    std::atomic<int64_t> lastSinceNowNs{0}, maxSinceNowNs{0}, lastDelay{0};
    std::atomic<uint64_t> lastSize{0}, lastQueued{0}, lastBuffered{0};
    std::atomic<uint32_t> rateNum{0}, rateDenom{0};
    std::atomic<int64_t> lastAbortTicks{0}, lastAbortExpected{0};
    // nur im Abruf
    bool haveTicks = false;
    uint64_t lastTicks = 0;
    uint64_t lastCycle = 0;
};

struct PwOutput::Hooks
{
    spa_hook core{};
    spa_hook registry{};
    spa_hook metadata{};
    spa_hook stream{};
};

// Rückrufe von PipeWire; Zugriff auf die privaten Teile von PwOutput
struct PwOutputCallbacks
{
    static void global(void *data, uint32_t id, uint32_t, const char *type, uint32_t, const spa_dict *props)
    {
        auto *o = static_cast<PwOutput *>(data);
        const QByteArray t(type);
        if (t == PW_TYPE_INTERFACE_Node) {
            PwOutput::Node n{text(props, PW_KEY_MEDIA_CLASS), text(props, PW_KEY_NODE_NAME),
                             text(props, PW_KEY_NODE_DESCRIPTION)};
            const bool sink = n.mediaClass == kSinkClass;
            const bool isTarget = sink && !o->m_target.isEmpty() && n.name == o->m_target;
            o->m_nodes[id] = n;
            if (sink) {
                if (o->m_connected)
                    o->log(QStringLiteral("Ausgang erschienen: «%1» (Knoten %2)").arg(n.name).arg(id));
                o->changed();
            }
            if (isTarget)
                o->scheduleReconcile();
        } else if (t == PW_TYPE_INTERFACE_Link) {
            o->m_links[id] = {nodeId(props, PW_KEY_LINK_OUTPUT_NODE), nodeId(props, PW_KEY_LINK_INPUT_NODE)};
            o->checkLinks();
        } else if (t == PW_TYPE_INTERFACE_Metadata && !o->m_metadata
                   && text(props, "metadata.name") == QLatin1String("default")) {
            static const pw_metadata_events events = [] {
                pw_metadata_events e{};
                e.version = PW_VERSION_METADATA_EVENTS;
                e.property = &PwOutputCallbacks::metadataProperty;
                return e;
            }();
            o->m_metadata = static_cast<pw_metadata *>(
                pw_registry_bind(o->m_registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
            if (o->m_metadata)
                pw_metadata_add_listener(o->m_metadata, &o->m_hooks->metadata, &events, o);
        }
    }

    static void globalRemove(void *data, uint32_t id)
    {
        auto *o = static_cast<PwOutput *>(data);
        if (auto it = o->m_nodes.find(id); it != o->m_nodes.end()) {
            const bool sink = it->second.mediaClass == kSinkClass;
            if (sink)
                o->log(QStringLiteral("Ausgang verschwunden: «%1» (Knoten %2)").arg(it->second.name).arg(id));
            o->m_nodes.erase(it);
            if (o->m_stream && id == o->m_streamTarget) {
                o->log(QStringLiteral("Ausgang «%1» ist verschwunden, Strom entfernt (kein Ausweichen)").arg(o->m_target));
                o->destroyStream();
            }
            if (sink) {
                o->changed();
                o->reconcile();
            }
        }
        if (o->m_links.erase(id))
            o->checkLinks();
    }

    static void coreDone(void *data, uint32_t id, int seq)
    {
        auto *o = static_cast<PwOutput *>(data);
        if (id == PW_ID_CORE && seq == o->m_syncSeq) {
            o->m_synced = true;
            pw_thread_loop_signal(o->m_loop, false);
        }
    }

    static void coreError(void *data, uint32_t id, int seq, int res, const char *message)
    {
        auto *o = static_cast<PwOutput *>(data);
        const QString m = QString::fromUtf8(message ? message : "");
        o->log(QStringLiteral("Fehler von PipeWire: Objekt %1, seq %2, %3 (%4)").arg(id).arg(seq).arg(res).arg(m));
        if (id == PW_ID_CORE && res == -EPIPE) {
            // Verbindung zu PipeWire weg (Neustart von PipeWire): kein neuer Versuch (offen für b4)
            o->m_connected = false;
            o->m_rt->allowed = false;
            o->setState(PwOutput::State::NoPipeWire, m);
        }
        pw_thread_loop_signal(o->m_loop, false);
    }

    static int metadataProperty(void *data, uint32_t subject, const char *key, const char *, const char *value)
    {
        auto *o = static_cast<PwOutput *>(data);
        if (subject != PW_ID_CORE || !key || QByteArray(key) != "default.audio.sink")
            return 0;
        const QString name = metadataName(value);
        if (name != o->m_defaultSink) {
            o->m_defaultSink = name;
            o->changed();
        }
        return 0;
    }

    static void streamState(void *data, pw_stream_state old, pw_stream_state state, const char *error)
    {
        auto *o = static_cast<PwOutput *>(data);
        o->m_streamState = state;
        if (state == PW_STREAM_STATE_ERROR)
            o->m_streamError = QString::fromUtf8(error ? error : "");
        if (state == PW_STREAM_STATE_STREAMING)
            o->m_rt->resetTicks = true;
        o->log(QStringLiteral("Strom %1 → %2%3")
                   .arg(QString::fromUtf8(pw_stream_state_as_string(old)),
                        QString::fromUtf8(pw_stream_state_as_string(state)),
                        error ? QStringLiteral(" (%1)").arg(QString::fromUtf8(error)) : QString()));
        // Nicht hier entfernen oder neu anlegen (Rückruf des Stroms selbst), sondern im nächsten
        // Durchlauf der Schleife
        o->scheduleReconcile();
    }

    // Echtzeit-Thread: keine Sperre, kein Speicher, kein Protokoll
    static void process(void *data)
    {
        auto *o = static_cast<PwOutput *>(data);
        PwOutput::Rt &rt = *o->m_rt;
        pw_stream *s = rt.stream.load(std::memory_order_acquire);
        if (!s)
            return;
        const int64_t t0 = steadyNs();
        pw_buffer *b = pw_stream_dequeue_buffer(s);
        if (!b) {
            rt.emptyPulls.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        spa_data &d = b->buffer->datas[0];
        auto *p = static_cast<float *>(d.data);
        uint32_t n = p ? d.maxsize / kStride : 0;
        if (b->requested)
            n = uint32_t(std::min<uint64_t>(b->requested, n));

        pw_time t{};
        const bool valid = pw_stream_get_time_n(s, &t, sizeof t) == 0;
        const int64_t pwNs = int64_t(pw_stream_get_nsec(s));
        const int64_t t1 = steadyNs();
        if (pwNs < t0 || pwNs > t1)
            rt.clockOutside.fetch_add(1, std::memory_order_relaxed);

        if (rt.resetTicks.exchange(false, std::memory_order_acq_rel))
            rt.haveTicks = false;
        OutputTiming timing;
        timing.callNs = t0;
        if (valid) {
            timing.sinceNowNs = pwNs - t.now;
            timing.size = t.size;
            timing.delay = t.delay;
            timing.rateNum = t.rate.num;
            timing.rateDenom = t.rate.denom;
            timing.queued = t.queued;
            timing.buffered = t.buffered;
            // Abgebrochener Zyklus: Δticks ungleich der Grösse des vorigen Zyklus (Z3c, Klick 197)
            if (rt.haveTicks && t.ticks - rt.lastTicks != rt.lastCycle) {
                rt.lastAbortTicks.store(int64_t(t.ticks - rt.lastTicks), std::memory_order_relaxed);
                rt.lastAbortExpected.store(int64_t(rt.lastCycle), std::memory_order_relaxed);
                rt.abortedCycles.fetch_add(1, std::memory_order_release);
            }
            rt.haveTicks = true;
            rt.lastTicks = t.ticks;
            rt.lastCycle = t.size ? t.size : n;
        }

        if (n > 0) {
            if (rt.allowed.load(std::memory_order_acquire)) {
                o->m_mixer->render(timing, p, n);
            } else {
                std::fill(p, p + size_t(n) * Mixer::kChannels, 0.0f);
                rt.silentPulls.fetch_add(1, std::memory_order_relaxed);
            }
        }
        d.chunk->offset = 0;
        d.chunk->stride = int32_t(kStride);
        d.chunk->size = n * kStride;
        b->size = n; // pw_time.queued zählt damit in Rahmen

        rt.lastSinceNowNs.store(timing.sinceNowNs, std::memory_order_relaxed);
        if (timing.sinceNowNs > rt.maxSinceNowNs.load(std::memory_order_relaxed))
            rt.maxSinceNowNs.store(timing.sinceNowNs, std::memory_order_relaxed);
        rt.lastSize.store(timing.size, std::memory_order_relaxed);
        rt.lastDelay.store(timing.delay, std::memory_order_relaxed);
        rt.lastQueued.store(timing.queued, std::memory_order_relaxed);
        rt.lastBuffered.store(timing.buffered, std::memory_order_relaxed);
        rt.rateNum.store(timing.rateNum, std::memory_order_relaxed);
        rt.rateDenom.store(timing.rateDenom, std::memory_order_relaxed);
        rt.pulls.fetch_add(1, std::memory_order_release);
        pw_stream_queue_buffer(s, b);
    }

    static void reconcileEvent(void *data, uint64_t)
    {
        static_cast<PwOutput *>(data)->reconcile();
    }

    // Jede Sekunde: Zähler des Abrufs ins Protokoll, nach einem Fehler neu versuchen
    static void timer(void *data, uint64_t)
    {
        auto *o = static_cast<PwOutput *>(data);
        PwOutput::Rt &rt = *o->m_rt;
        const uint64_t aborted = rt.abortedCycles.load(std::memory_order_acquire);
        if (aborted != o->m_loggedAborted) {
            o->log(QStringLiteral("%1 abgebrochene Zyklen (gesamt %2), zuletzt Δticks %3 statt %4; kein Fehler, "
                                  "ein kurzes Stück Ton kann fehlen")
                       .arg(aborted - o->m_loggedAborted).arg(aborted)
                       .arg(rt.lastAbortTicks.load()).arg(rt.lastAbortExpected.load()));
            o->m_loggedAborted = aborted;
        }
        const uint64_t empty = rt.emptyPulls.load(std::memory_order_relaxed);
        if (empty != o->m_loggedEmpty) {
            o->log(QStringLiteral("%1 Abrufe ohne Puffer (gesamt %2)").arg(empty - o->m_loggedEmpty).arg(empty));
            o->m_loggedEmpty = empty;
        }
        const uint64_t outside = rt.clockOutside.load(std::memory_order_relaxed);
        if (outside != o->m_loggedClockOutside) {
            o->log(QStringLiteral("Uhr: %1 Abrufe mit pw_stream_get_nsec() ausserhalb von steady_clock (gesamt %2)")
                       .arg(outside - o->m_loggedClockOutside).arg(outside));
            o->m_loggedClockOutside = outside;
        }
        if (rt.pulls.load(std::memory_order_acquire) > 0) {
            const int64_t delay = rt.lastDelay.load(std::memory_order_relaxed);
            const uint64_t size = rt.lastSize.load(std::memory_order_relaxed);
            if (delay != o->m_loggedDelay || size != o->m_loggedSize) {
                o->log(QStringLiteral("Abruf: delay %1 (vorher %2, rate %3/%4), size %5 (vorher %6), queued %7, buffered %8")
                           .arg(delay).arg(o->m_loggedDelay)
                           .arg(rt.rateNum.load()).arg(rt.rateDenom.load())
                           .arg(size).arg(o->m_loggedSize)
                           .arg(rt.lastQueued.load()).arg(rt.lastBuffered.load()));
                o->m_loggedDelay = delay;
                o->m_loggedSize = size;
            }
        }
        if (o->m_state == PwOutput::State::Error && !o->m_blocked && o->m_retryAtNs && steadyNs() >= o->m_retryAtNs)
            o->reconcile();
    }
};

PwOutput::PwOutput(Mixer *mixer, const QString &purpose, const QString &nodeName, const QString &description)
    : m_mixer(mixer), m_purpose(purpose), m_nodeName(nodeName), m_description(description),
      m_hooks(std::make_unique<Hooks>()), m_rt(std::make_unique<Rt>())
{
}

PwOutput::~PwOutput()
{
    if (!m_loop)
        return;
    lock();
    destroyStream();
    if (m_timer)
        pw_loop_destroy_source(pw_thread_loop_get_loop(m_loop), m_timer);
    if (m_reconcileEvent)
        pw_loop_destroy_source(pw_thread_loop_get_loop(m_loop), m_reconcileEvent);
    if (m_metadata) {
        spa_hook_remove(&m_hooks->metadata);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_metadata));
    }
    if (m_registry) {
        spa_hook_remove(&m_hooks->registry);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_registry));
    }
    if (m_core) {
        spa_hook_remove(&m_hooks->core);
        pw_core_disconnect(m_core);
    }
    unlock();
    pw_thread_loop_stop(m_loop);
    if (m_context)
        pw_context_destroy(m_context);
    pw_thread_loop_destroy(m_loop);
}

bool PwOutput::start(QString *error)
{
    // pw_init einmal je Prozess; pw_deinit nie (andere Teile des Prozesses könnten PipeWire nutzen)
    static std::once_flag initOnce;
    std::call_once(initOnce, [] { pw_init(nullptr, nullptr); });

    auto fail = [&](const QString &why) {
        *error = why;
        log(why);
        return false;
    };
    m_loop = pw_thread_loop_new(QStringLiteral("dss-pw-%1").arg(m_purpose).toUtf8().constData(), nullptr);
    if (!m_loop)
        return fail(QStringLiteral("pw_thread_loop_new fehlgeschlagen"));
    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context)
        return fail(QStringLiteral("pw_context_new fehlgeschlagen"));
    if (pw_thread_loop_start(m_loop) < 0)
        return fail(QStringLiteral("pw_thread_loop_start fehlgeschlagen"));

    static const pw_core_events coreEvents = [] {
        pw_core_events e{};
        e.version = PW_VERSION_CORE_EVENTS;
        e.done = &PwOutputCallbacks::coreDone;
        e.error = &PwOutputCallbacks::coreError;
        return e;
    }();
    static const pw_registry_events registryEvents = [] {
        pw_registry_events e{};
        e.version = PW_VERSION_REGISTRY_EVENTS;
        e.global = &PwOutputCallbacks::global;
        e.global_remove = &PwOutputCallbacks::globalRemove;
        return e;
    }();

    lock();
    pw_loop *loop = pw_thread_loop_get_loop(m_loop);
    m_reconcileEvent = pw_loop_add_event(loop, &PwOutputCallbacks::reconcileEvent, this);
    m_timer = pw_loop_add_timer(loop, &PwOutputCallbacks::timer, this);
    timespec second{1, 0};
    if (m_timer)
        pw_loop_update_timer(loop, m_timer, &second, &second, false);
    m_core = pw_context_connect(m_context, nullptr, 0);
    if (m_core) {
        pw_core_add_listener(m_core, &m_hooks->core, &coreEvents, this);
        m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
        if (m_registry)
            pw_registry_add_listener(m_registry, &m_hooks->registry, &registryEvents, this);
    }
    const bool ok = m_core && m_registry;
    unlock();
    if (!ok)
        return fail(QStringLiteral("keine Verbindung zu PipeWire (pw_context_connect)"));

    // Erste Runde: alle Globals; zweite Runde: Eigenschaften der Metadaten «default» (gebunden
    // während der ersten Runde)
    for (int round = 0; round < 2; ++round) {
        lock();
        m_synced = false;
        m_syncSeq = pw_core_sync(m_core, PW_ID_CORE, 0);
        const int64_t deadline = steadyNs() + 5'000'000'000;
        // Rückgabewert von timed_wait ist im Header nicht beschrieben, deshalb eigene Frist
        while (!m_synced && steadyNs() < deadline)
            pw_thread_loop_timed_wait(m_loop, 1);
        const bool synced = m_synced;
        unlock();
        if (!synced)
            return fail(QStringLiteral("PipeWire antwortet nicht (pw_core_sync)"));
    }
    lock();
    m_connected = true;
    log(QStringLiteral("verbunden, libpipewire %1, %2 Ausgänge")
            .arg(QString::fromUtf8(pw_get_library_version()))
            .arg(std::count_if(m_nodes.begin(), m_nodes.end(),
                               [](const auto &n) { return n.second.mediaClass == kSinkClass; })));
    reconcile();
    unlock();
    return true;
}

void PwOutput::setTarget(const QString &nodeName)
{
    if (!m_loop) {
        m_target = nodeName;
        return;
    }
    lock();
    if (nodeName != m_target || m_blocked) {
        log(QStringLiteral("Ausgang gewählt: «%1»").arg(nodeName));
        m_target = nodeName;
        m_blocked = false;
        m_retryAtNs = 0;
        destroyStream();
        reconcile();
    }
    unlock();
}

QString PwOutput::target() const
{
    lock();
    const QString t = m_target;
    unlock();
    return t;
}

QList<AudioDevice> PwOutput::devices() const
{
    QList<AudioDevice> r;
    lock();
    for (const auto &[id, n] : m_nodes) {
        if (n.mediaClass != kSinkClass || n.name.isEmpty())
            continue;
        r.append({n.name, n.description.isEmpty() ? n.name : n.description, n.name == m_defaultSink});
    }
    unlock();
    std::sort(r.begin(), r.end(), [](const AudioDevice &a, const AudioDevice &b) { return a.name < b.name; });
    return r;
}

PwOutput::State PwOutput::state() const
{
    lock();
    const State s = m_state;
    unlock();
    return s;
}

QString PwOutput::stateDetail() const
{
    lock();
    const QString s = m_stateDetail;
    unlock();
    return s;
}

PwOutput::Stats PwOutput::stats() const
{
    Stats s;
    s.pulls = m_rt->pulls.load(std::memory_order_acquire);
    s.emptyPulls = m_rt->emptyPulls.load();
    s.silentPulls = m_rt->silentPulls.load();
    s.abortedCycles = m_rt->abortedCycles.load();
    s.clockOutside = m_rt->clockOutside.load();
    s.lastSinceNowNs = m_rt->lastSinceNowNs.load();
    s.maxSinceNowNs = m_rt->maxSinceNowNs.load();
    s.lastSize = m_rt->lastSize.load();
    s.lastDelay = m_rt->lastDelay.load();
    s.lastQueued = m_rt->lastQueued.load();
    s.lastBuffered = m_rt->lastBuffered.load();
    s.rateNum = m_rt->rateNum.load();
    s.rateDenom = m_rt->rateDenom.load();
    lock();
    s.streamsCreated = m_streamsCreated;
    unlock();
    return s;
}

uint32_t PwOutput::streamNodeId() const
{
    lock();
    const uint32_t id = m_stream ? pw_stream_get_node_id(m_stream) : UINT32_MAX;
    unlock();
    return id;
}

void PwOutput::lock() const
{
    if (m_loop)
        pw_thread_loop_lock(m_loop);
}

void PwOutput::unlock() const
{
    if (m_loop)
        pw_thread_loop_unlock(m_loop);
}

uint32_t PwOutput::targetNodeId() const
{
    if (m_target.isEmpty())
        return UINT32_MAX;
    for (const auto &[id, n] : m_nodes)
        if (n.mediaClass == kSinkClass && n.name == m_target)
            return id;
    return UINT32_MAX;
}

void PwOutput::scheduleReconcile()
{
    if (m_reconcileEvent)
        pw_loop_signal_event(pw_thread_loop_get_loop(m_loop), m_reconcileEvent);
}

void PwOutput::reconcile()
{
    if (!m_connected) {
        destroyStream();
        setState(State::NoPipeWire, m_stateDetail);
        return;
    }
    if (m_target.isEmpty()) {
        destroyStream();
        setState(State::NoTarget);
        return;
    }
    if (m_blocked) {
        destroyStream();
        return; // bleibt Error bis setTarget
    }
    if (m_stream && m_streamState == PW_STREAM_STATE_ERROR) {
        const QString why = m_streamError.isEmpty() ? QStringLiteral("Fehler des Stroms") : m_streamError;
        destroyStream();
        m_retryAtNs = steadyNs() + kRetryNs;
        setState(State::Error, why);
        return;
    }
    const uint32_t id = targetNodeId();
    if (id == UINT32_MAX) {
        destroyStream();
        m_retryAtNs = 0;
        setState(State::Missing);
        return;
    }
    if (m_stream && m_streamTarget != id)
        destroyStream();
    if (!m_stream) {
        if (m_state == State::Error && m_retryAtNs && steadyNs() < m_retryAtNs)
            return;
        m_retryAtNs = 0;
        createStream(id);
    }
    checkLinks();
}

void PwOutput::createStream(uint32_t targetId)
{
    static const pw_stream_events streamEvents = [] {
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.state_changed = &PwOutputCallbacks::streamState;
        e.process = &PwOutputCallbacks::process;
        return e;
    }();
    const QByteArray target = m_target.toUtf8();
    pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_APP_NAME, "DoNs Streaming Studio",
        PW_KEY_NODE_NAME, m_nodeName.toUtf8().constData(),
        PW_KEY_NODE_DESCRIPTION, m_description.toUtf8().constData(),
        PW_KEY_TARGET_OBJECT, target.constData(),
        PW_KEY_NODE_DONT_RECONNECT, "true",
        "node.dont-fallback", "true",
        "node.dont-move", "true",
        nullptr);
    pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%d/%d", kLatencyFrames, Mixer::kRate);
    pw_stream *s = pw_stream_new(m_core, m_nodeName.toUtf8().constData(), props);
    if (!s) {
        m_retryAtNs = steadyNs() + kRetryNs;
        setState(State::Error, QStringLiteral("pw_stream_new fehlgeschlagen"));
        return;
    }
    pw_stream_add_listener(s, &m_hooks->stream, &streamEvents, this);

    uint8_t buffer[1024];
    spa_pod_builder b{};
    spa_pod_builder_init(&b, buffer, sizeof buffer);
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = Mixer::kRate;
    info.channels = Mixer::kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};

    m_stream = s;
    m_streamTarget = targetId;
    m_streamState = PW_STREAM_STATE_UNCONNECTED;
    m_streamError.clear();
    m_rt->allowed = false;
    m_rt->resetTicks = true;
    m_rt->stream.store(s, std::memory_order_release);
    const auto flags = pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
                                       | PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_DONT_RECONNECT);
    if (pw_stream_connect(s, PW_DIRECTION_OUTPUT, PW_ID_ANY, flags, params, 1) < 0) {
        destroyStream();
        m_retryAtNs = steadyNs() + kRetryNs;
        setState(State::Error, QStringLiteral("pw_stream_connect fehlgeschlagen"));
        return;
    }
    ++m_streamsCreated;
    log(QStringLiteral("Strom angelegt für Ausgang «%1» (Knoten %2), node.latency %3/%4")
            .arg(m_target).arg(targetId).arg(kLatencyFrames).arg(Mixer::kRate));
    setState(State::Connecting);
}

void PwOutput::destroyStream()
{
    if (!m_stream)
        return;
    m_rt->allowed = false;
    pw_stream_disconnect(m_stream);
    spa_hook_remove(&m_hooks->stream);
    pw_stream_destroy(m_stream);
    m_rt->stream.store(nullptr, std::memory_order_release);
    m_stream = nullptr;
    m_streamTarget = UINT32_MAX;
    m_streamState = PW_STREAM_STATE_UNCONNECTED;
}

void PwOutput::checkLinks()
{
    if (!m_stream)
        return;
    const uint32_t me = pw_stream_get_node_id(m_stream);
    if (me == SPA_ID_INVALID) {
        m_rt->allowed = false;
        return;
    }
    bool toTarget = false;
    for (const auto &[id, l] : m_links) {
        if (l.outputNode != me)
            continue;
        if (l.inputNode != m_streamTarget) {
            // Fremde Verbindung: sofort Stille, Strom weg, kein neuer Strom bis setTarget
            m_rt->allowed = false;
            const auto other = m_nodes.find(l.inputNode);
            const QString name = other != m_nodes.end() ? other->second.name : QString();
            log(QStringLiteral("Strom hängt an einem anderen Knoten (%1 «%2») statt an «%3»: Stille, Strom entfernt")
                    .arg(l.inputNode).arg(name, m_target));
            destroyStream();
            m_blocked = true;
            setState(State::Error, QStringLiteral("Verbindung zu einem anderen Gerät: %1").arg(name));
            return;
        }
        toTarget = true;
    }
    const bool playing = toTarget && m_streamState == PW_STREAM_STATE_STREAMING;
    m_rt->allowed.store(playing, std::memory_order_release);
    setState(playing ? State::Playing : State::Connecting);
}

void PwOutput::setState(State state, const QString &detail)
{
    if (state == m_state && detail == m_stateDetail)
        return;
    m_state = state;
    m_stateDetail = detail;
    log(QStringLiteral("Zustand %1%2")
            .arg(QString::fromLatin1(stateName(state)), detail.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(detail)));
    changed();
}

void PwOutput::log(const QString &line)
{
    const QString l = QStringLiteral("Ton (%1): %2").arg(m_purpose, line);
    if (onLog)
        onLog(l);
    else
        qInfo().noquote() << l;
}

void PwOutput::changed()
{
    if (onChange)
        onChange();
}

} // namespace dss
