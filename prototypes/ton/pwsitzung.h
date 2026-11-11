// Verbindung von pw-messen zu PipeWire: pw_thread_loop (eigener Thread), Kontext, Core und
// Registry. Die Registry führt Knoten (Ausgänge, Eingänge, eigene Ströme) und Verbindungen (Links)
// nach, die Metadaten «default» liefern Standardausgang und Standardeingang.
//
// Sperre: Alle Rückrufe laufen im Thread der Schleife mit gehaltener Sperre. Wer knoten(),
// verbindungen() oder einen pw_stream benutzt, hält die Sperre (sperren()/entsperren()).
//
// Quellen (lokal gelesen 2026-11-11, libpipewire-0.3-dev 1.4.2, kein Webzugang):
//   /usr/include/pipewire-0.3/pipewire/thread-loop.h:44-67 (Sperre, Rückrufe mit Sperre),
//   /usr/include/pipewire-0.3/pipewire/core.h:110-157, 460-518 (Registry, global/global_remove,
//   sync/done schliesst die erste Runde ab), /usr/include/pipewire-0.3/pipewire/keys.h
//   (media.class, node.name, object.serial, link.output.node, link.input.node),
//   /usr/include/pipewire-0.3/pipewire/extensions/metadata.h:43-52 (Ereignis «property»).
#pragma once

#include <QString>

#include <spa/utils/hook.h>

#include <cstdint>
#include <functional>
#include <map>

struct pw_context;
struct pw_core;
struct pw_metadata;
struct pw_registry;
struct pw_thread_loop;

struct PwKnoten
{
    QString klasse; // media.class
    QString name; // node.name
    QString beschreibung; // node.description
    QString serial; // object.serial
};

struct PwVerbindung
{
    uint32_t aus = UINT32_MAX; // link.output.node (fehlt die Angabe: UINT32_MAX)
    uint32_t ein = UINT32_MAX; // link.input.node
};

class PwSitzung
{
public:
    PwSitzung();
    ~PwSitzung();
    PwSitzung(const PwSitzung &) = delete;
    PwSitzung &operator=(const PwSitzung &) = delete;

    // Startet den Thread, verbindet mit PipeWire und wartet die erste Runde der Registry und der
    // Metadaten ab. Ohne gehaltene Sperre aufrufen.
    bool verbinden(QString *fehler);
    // pw_core_sync und warten, bis «done» kommt (höchstens zeitS). Ohne gehaltene Sperre.
    bool abgleich(int zeitS = 5);

    void sperren();
    void entsperren();
    pw_thread_loop *schleife() const { return m_loop; }
    pw_core *core() const { return m_core; }

    // Nur mit gehaltener Sperre
    const std::map<uint32_t, PwKnoten> &knoten() const { return m_knoten; }
    const std::map<uint32_t, PwVerbindung> &verbindungen() const { return m_verbindungen; }
    QString standardSenke() const { return m_standardSenke; }
    QString standardQuelle() const { return m_standardQuelle; }
    QString fehlerText() const { return m_fehler; }
    // Knoten mit genau diesem node.name, dessen media.class mit klasse beginnt; sonst UINT32_MAX
    uint32_t knotenNachName(const QString &name, const QString &klasse) const;

    // Wird im Thread der Schleife (mit Sperre) für jede neue Verbindung aufgerufen. Nur mit
    // gehaltener Sperre setzen oder löschen.
    std::function<void(const PwVerbindung &)> neueVerbindung;

private:
    static void onGlobal(void *data, uint32_t id, uint32_t permissions, const char *type, uint32_t version,
                         const struct spa_dict *props);
    static void onGlobalRemove(void *data, uint32_t id);
    static void onDone(void *data, uint32_t id, int seq);
    static void onError(void *data, uint32_t id, int seq, int res, const char *message);
    static int onProperty(void *data, uint32_t subject, const char *key, const char *type, const char *value);

    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_registry *m_registry = nullptr;
    pw_metadata *m_metadaten = nullptr;
    spa_hook m_coreHook{};
    spa_hook m_registryHook{};
    spa_hook m_metadatenHook{};
    int m_wartetSeq = -1;
    bool m_fertig = false;
    std::map<uint32_t, PwKnoten> m_knoten;
    std::map<uint32_t, PwVerbindung> m_verbindungen;
    QString m_standardSenke;
    QString m_standardQuelle;
    QString m_fehler;
};

// Ausgänge (media.class «Audio/Sink») und Eingänge («Audio/Source…») aus der Registry, eine Zeile
// je Knoten: «  <node.name> «<node.description>» [Standard], id …, object.serial …». Sperrt selbst.
QString geraeteListe(PwSitzung &s);
