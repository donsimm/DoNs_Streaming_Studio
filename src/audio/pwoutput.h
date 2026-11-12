#pragma once

#include <QList>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>

// Ausgabe eines Mischers über PipeWire (P12b b3), dünne Schicht über pw_thread_loop und pw_stream
// (Plan P12, Abschnitt «b3»). Ein Ausgang je Zweck (zuerst «Ton der Videos»), ein eigener Strom.
//
// - Liste der Ausgänge aus der Registry (media.class «Audio/Sink»), Standardausgang aus den
//   Metadaten «default» (nur zur Anzeige, nie geändert).
// - Gewählter Ausgang über target.object (node.name). Der Strom wird nur angelegt, wenn dieser
//   Knoten in der Registry steht. Verschwindet er, wird der Strom sofort entfernt (Zustand
//   «fehlt»); kommt er wieder, legt die Schicht den Strom neu an. Kein Ausweichen auf ein anderes
//   Gerät: node.dont-reconnect, node.dont-fallback und node.dont-move gesetzt, dazu die eigene
//   Prüfung der Verbindungen: Hängt der Strom an einem anderen Knoten als dem gewählten, spielt er
//   nur Stille, wird entfernt und bleibt im Zustand «Fehler», bis ein Ausgang neu gewählt wird.
//   Bis die Verbindung zum gewählten Knoten bestätigt ist, spielt er Stille.
// - Je Abruf (Echtzeit-Thread von PipeWire, PW_STREAM_FLAG_RT_PROCESS) übergibt die Schicht dem
//   Mischer Abrufzeit (steady_clock), sinceNowNs (pw_stream_get_nsec() − pw_time.now), size,
//   delay, rate, queued und buffered (Mixer::render, OutputTiming). Die Lage in Proben zählt der
//   Mischer selbst.
// - Ein abgebrochener Zyklus (Δticks ungleich der vorigen Zyklusgrösse, wie beim Wechsel des
//   Quantums in Z3c) wird gezählt und ins Protokoll geschrieben; er ist kein Fehler. Wechsel von
//   delay und size ebenso (delay war an der virtuellen Senke in Z3 immer 0, Wert an einem echten
//   Ausgang erst mit M4).
// - Uhr: Je Abruf wird geprüft, ob pw_stream_get_nsec() zwischen zwei Ablesungen von steady_clock
//   liegt (dieselbe Uhr CLOCK_MONOTONIC); Abrufe ausserhalb werden gezählt (Stats::clockOutside).
//
// Threads: Alle Rückrufe von PipeWire (ausser dem Abruf) laufen im Thread der Schleife mit
// gehaltener Sperre; die öffentlichen Funktionen sperren selbst und dürfen aus jedem Thread ausser
// dem der Schleife aufgerufen werden. onChange und onLog laufen im Thread der Schleife (mit
// Sperre): Sie dürfen keine Funktion dieser Klasse aufrufen, nur weiterreichen (z. B. Qt::QueuedConnection).
//
// Fällt die Verbindung zu PipeWire weg (Neustart von PipeWire), bleibt der Zustand «kein PipeWire»
// bis zum Neustart des Programms (offen für b4, nicht geprüft).

struct pw_context;
struct pw_core;
struct pw_metadata;
struct pw_registry;
struct pw_stream;
struct pw_thread_loop;
struct spa_source;

namespace dss {

class Mixer;

struct AudioDevice
{
    QString name;        // node.name (target.object, kern.ini)
    QString description; // node.description, sonst node.name
    bool isDefault = false;

    bool operator==(const AudioDevice &) const = default;
};

class PwOutput
{
public:
    enum class State {
        NoPipeWire, // keine Verbindung zu PipeWire
        NoTarget,   // kein Ausgang gewählt
        Missing,    // gewählter Ausgang fehlt (kein Ton, kein Ausweichen)
        Connecting, // Strom angelegt, Verbindung zum Ausgang noch nicht bestätigt (Stille)
        Playing,    // Strom läuft und hängt nur am gewählten Ausgang
        Error,      // Fehler des Stroms oder fremde Verbindung (Stille, Strom entfernt)
    };
    static const char *stateName(State state); // "no_pipewire", "no_target", … (Protokoll, Schnittstelle)

    // Gewünschte Verzögerung node.latency: 40 ms (Frage 2 des Nutzers, 2026-11-11)
    static constexpr int kLatencyFrames = 1920;

    struct Stats
    {
        uint64_t pulls = 0;         // Abrufe mit Puffer
        uint64_t emptyPulls = 0;    // Abrufe ohne Puffer (pw_stream_dequeue_buffer == NULL)
        uint64_t silentPulls = 0;   // Abrufe mit Stille, weil die Verbindung nicht bestätigt war
        uint64_t abortedCycles = 0; // Δticks ungleich voriger Zyklusgrösse
        uint64_t clockOutside = 0;  // pw_stream_get_nsec() nicht zwischen zwei Ablesungen von steady_clock
        uint64_t streamsCreated = 0;
        int64_t lastSinceNowNs = 0;
        int64_t maxSinceNowNs = 0;
        uint64_t lastSize = 0;
        int64_t lastDelay = 0;
        uint64_t lastQueued = 0;
        uint64_t lastBuffered = 0;
        uint32_t rateNum = 0;
        uint32_t rateDenom = 0;
    };

    // purpose: Zweck in Protokoll und Namen des Stroms (z. B. "ton-video"); nodeName: node.name des
    // Stroms; mixer: liefert die Proben (muss länger leben als dieses Objekt).
    PwOutput(Mixer *mixer, const QString &purpose, const QString &nodeName, const QString &description);
    ~PwOutput();
    PwOutput(const PwOutput &) = delete;
    PwOutput &operator=(const PwOutput &) = delete;

    // Startet den Thread, verbindet mit PipeWire und wartet die erste Runde der Registry ab
    // (höchstens 5 s). false: kein PipeWire (Zustand NoPipeWire), Grund in error.
    bool start(QString *error);

    // Gewählter Ausgang (node.name); leer = kein Ausgang.
    void setTarget(const QString &nodeName);
    QString target() const;

    QList<AudioDevice> devices() const;
    State state() const;
    // Kurzer Grund zum Zustand (Fehlertext von PipeWire, fremder Knoten); sonst leer.
    QString stateDetail() const;
    Stats stats() const;
    // node.id des Stroms, SPA_ID_INVALID (UINT32_MAX) ohne Strom
    uint32_t streamNodeId() const;

    // Liste der Ausgänge oder Zustand haben sich geändert (Thread der Schleife).
    std::function<void()> onChange;
    // Zeile fürs Protokoll (Thread der Schleife).
    std::function<void(const QString &)> onLog;

private:
    struct Node
    {
        QString mediaClass;
        QString name;
        QString description;
    };
    struct Link
    {
        uint32_t outputNode = UINT32_MAX;
        uint32_t inputNode = UINT32_MAX;
    };
    struct Rt; // Werte des Echtzeit-Abrufs
    struct Hooks;
    friend struct PwOutputCallbacks; // Rückrufe von PipeWire (pwoutput.cpp)

    // Nur im Thread der Schleife bzw. mit Sperre
    void reconcile();
    void createStream(uint32_t targetId);
    void destroyStream();
    void checkLinks();
    void setState(State state, const QString &detail = {});
    void log(const QString &line);
    void changed();
    uint32_t targetNodeId() const;
    void scheduleReconcile();
    void lock() const;
    void unlock() const;

    Mixer *m_mixer;
    QString m_purpose;
    QString m_nodeName;
    QString m_description;

    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_registry *m_registry = nullptr;
    pw_metadata *m_metadata = nullptr;
    pw_stream *m_stream = nullptr;
    spa_source *m_reconcileEvent = nullptr;
    spa_source *m_timer = nullptr;
    std::unique_ptr<Hooks> m_hooks;
    std::unique_ptr<Rt> m_rt;

    // mit Sperre
    int m_syncSeq = -1;
    bool m_synced = false;
    bool m_connected = false;
    std::map<uint32_t, Node> m_nodes;
    std::map<uint32_t, Link> m_links;
    QString m_defaultSink;
    QString m_target;
    uint32_t m_streamTarget = UINT32_MAX; // Knoten, für den der Strom angelegt ist
    int m_streamState = 0;                // enum pw_stream_state
    QString m_streamError;
    bool m_blocked = false;               // fremde Verbindung: kein neuer Strom bis setTarget
    int64_t m_retryAtNs = 0;              // nach einem Fehler des Stroms frühestens dann neu anlegen
    State m_state = State::NoPipeWire;
    QString m_stateDetail;
    uint64_t m_streamsCreated = 0;
    // Protokoll: zuletzt gemeldete Werte (Timer)
    uint64_t m_loggedAborted = 0;
    uint64_t m_loggedEmpty = 0;
    uint64_t m_loggedClockOutside = 0;
    int64_t m_loggedDelay = 0;
    uint64_t m_loggedSize = 0;
};

} // namespace dss
