// Quellen (lokal gelesen 2026-11-11, libpipewire-0.3-dev 1.4.2, kein Webzugang):
//   /usr/include/pipewire-0.3/pipewire/stream.h
//     :146-168  Abruf «process» für Wiedergabe: dequeue, füllen, queue im selben Abruf
//     :254-278  pw_buffer: size (Summe ergibt pw_time.queued, hier Frames), requested (vorgeschlagene
//               Frames), time (Aufnahme: Zykluszeit in ns, seit 1.0.5)
//     :291-404  pw_time und Formel für die Verzögerung der ersten Probe eines neu eingereihten Puffers:
//               buffered/Rate + queued/app.rate + (delay − elapsed)·rate.num/rate.denom
//     :456-497  Flags AUTOCONNECT, MAP_BUFFERS, RT_PROCESS (Abruf im Echtzeit-Thread), DONT_RECONNECT
//     :591-596  pw_stream_get_time_n und pw_stream_get_nsec sind RT-sicher
//   /usr/include/pipewire-0.3/pipewire/keys.h:165-166 (node.latency als Bruch), :181-184
//     (node.dont-reconnect: «initially linked to target.object or the default node» – deshalb prüft
//     pw-messen die Verbindung selbst, bevor es klickt), :376-377 (target.object: Name oder Serial)
//   /usr/include/spa-0.2/spa/param/audio/raw-utils.h:50 (spa_format_audio_raw_build)
#include "pwklick.h"

#include "auswertung.h"
#include "pwsitzung.h"
#include "zeit.h"

#include <QFile>
#include <QThread>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <map>
#include <vector>

namespace {

constexpr int kKlickProben = 48; // 1 ms, wie klickmessung.cpp
constexpr float kKlickPegel = 0.5f;
constexpr uint32_t kStride = sizeof(float) * kKanaele;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
const QLatin1String kPraefix("dss_p12a");

// Liste für den Echtzeit-Abruf: Platz vorher angelegt, ein Schreiber (Thread von PipeWire), gelesen
// erst nach dem Ende der Ströme. Ist sie voll, wird nur gezählt.
template<class T>
class RtListe
{
public:
    void anlegen(size_t n) { m_v.resize(n); }
    void add(const T &x)
    {
        const size_t i = m_n.load(std::memory_order_relaxed);
        if (i < m_v.size()) {
            m_v[i] = x;
            m_n.store(i + 1, std::memory_order_release);
        } else {
            m_verworfen.fetch_add(1, std::memory_order_relaxed);
        }
    }
    size_t size() const { return m_n.load(std::memory_order_acquire); }
    const T &operator[](size_t i) const { return m_v[i]; }
    uint64_t verworfen() const { return m_verworfen.load(); }

private:
    std::vector<T> m_v;
    std::atomic<size_t> m_n{0};
    std::atomic<uint64_t> m_verworfen{0};
};

// Kopie von pw_time (stream.h:371-404)
struct Zeitangabe
{
    bool gueltig = false; // pw_stream_get_time_n == 0
    int64_t now = 0;
    uint32_t rateNum = 0, rateDenom = 0;
    uint64_t ticks = 0;
    int64_t delay = 0;
    uint64_t queued = 0, buffered = 0;
    uint32_t queuedBuffers = 0, availBuffers = 0;
    uint64_t size = 0;

    double tickNs() const { return rateDenom ? 1e9 * double(rateNum) / double(rateDenom) : kNaN; }
    double delayMs() const { return double(delay) * tickNs() / 1e6; }
};

struct AufrufAus
{
    int64_t steadyNs = 0;
    int64_t pwNs = 0; // pw_stream_get_nsec im selben Abruf
    Zeitangabe t;
    int64_t lage = 0; // erste Probe dieses Abrufs
    uint32_t frames = 0, requested = 0, platz = 0;
    bool leer = false; // kein Puffer (dequeue == NULL)
    bool kurz = false; // weniger geschrieben als verlangt
    bool klicken = false;
};

struct AufrufEin
{
    int64_t steadyNs = 0;
    int64_t pwNs = 0;
    Zeitangabe t;
    int64_t lage = 0;
    uint32_t frames = 0;
    uint64_t pufferZeit = 0; // pw_buffer.time
    bool leer = false;
};

struct Einsatz
{
    int64_t lage = 0; // Probe in der Aufnahme
    int64_t ns = 0; // steady_clock: Abruf − (Frames − Index)/Rate, wie klickmessung.cpp
    uint64_t pufferZeit = 0; // pw_buffer.time des Aufnahmepuffers (Uhr von PipeWire)
    int64_t pwMinusSteady = 0; // im selben Abruf, zum Umrechnen von pufferZeit
    uint32_t index = 0, frames = 0;
};

struct Zustand
{
    int64_t ns;
    const char *strom;
    pw_stream_state alt, neu;
    QString fehler;
};

Zeitangabe zeitLesen(pw_stream *s)
{
    pw_time t{};
    Zeitangabe z;
    z.gueltig = pw_stream_get_time_n(s, &t, sizeof t) == 0;
    z.now = t.now;
    z.rateNum = t.rate.num;
    z.rateDenom = t.rate.denom;
    z.ticks = t.ticks;
    z.delay = t.delay;
    z.queued = t.queued;
    z.buffered = t.buffered;
    z.queuedBuffers = t.queued_buffers;
    z.availBuffers = t.avail_buffers;
    z.size = t.size;
    return z;
}

struct Messung
{
    pw_stream *aus = nullptr;
    pw_stream *ein = nullptr;
    spa_hook ausHook{};
    spa_hook einHook{};
    int64_t abstand = 0; // Proben zwischen zwei Klicks
    std::atomic<bool> freigabe{false}; // Verbindung geprüft: nur zur Senke dss_p12a…
    std::atomic<bool> klicksAus{false};
    std::atomic<bool> gefahr{false};
    // nur im Echtzeit-Thread
    int64_t lageAus = 0;
    int64_t lageEin = 0;
    int64_t ruhe = kRate;
    RtListe<AufrufAus> aufrufeAus;
    RtListe<AufrufEin> aufrufeEin;
    RtListe<Einsatz> einsaetze;
    // nur im Thread der Schleife bzw. mit Sperre
    pw_stream_state zustandAus = PW_STREAM_STATE_UNCONNECTED;
    pw_stream_state zustandEin = PW_STREAM_STATE_UNCONNECTED;
    std::vector<Zustand> zustaende;
};

void ausProcess(void *data)
{
    auto *m = static_cast<Messung *>(data);
    AufrufAus a;
    a.steadyNs = steadyNs();
    pw_buffer *b = pw_stream_dequeue_buffer(m->aus);
    if (!b) {
        a.leer = true;
        a.lage = m->lageAus;
        m->aufrufeAus.add(a);
        return;
    }
    spa_data &d = b->buffer->datas[0];
    auto *p = static_cast<float *>(d.data);
    a.platz = p ? d.maxsize / kStride : 0;
    a.requested = uint32_t(b->requested);
    uint32_t n = a.platz;
    if (b->requested)
        n = uint32_t(std::min<uint64_t>(b->requested, a.platz));
    a.t = zeitLesen(m->aus);
    a.pwNs = int64_t(pw_stream_get_nsec(m->aus));
    a.klicken = m->freigabe.load(std::memory_order_acquire) && !m->klicksAus.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < n; ++i) {
        const float v = a.klicken && (m->lageAus + i) % m->abstand < kKlickProben ? kKlickPegel : 0.0f;
        p[2 * i] = v;
        p[2 * i + 1] = v;
    }
    d.chunk->offset = 0;
    d.chunk->stride = int32_t(kStride);
    d.chunk->size = n * kStride;
    b->size = n; // pw_time.queued zählt damit in Frames
    a.lage = m->lageAus;
    a.frames = n;
    a.kurz = b->requested && n < b->requested;
    m->lageAus += n;
    m->aufrufeAus.add(a);
    pw_stream_queue_buffer(m->aus, b);
}

void einProcess(void *data)
{
    auto *m = static_cast<Messung *>(data);
    AufrufEin e;
    e.steadyNs = steadyNs();
    pw_buffer *b = pw_stream_dequeue_buffer(m->ein);
    if (!b) {
        e.leer = true;
        e.lage = m->lageEin;
        m->aufrufeEin.add(e);
        return;
    }
    const spa_data &d = b->buffer->datas[0];
    const uint32_t off = std::min(d.chunk->offset, d.maxsize);
    const uint32_t groesse = std::min(d.chunk->size, d.maxsize - off);
    const float *p = d.data ? reinterpret_cast<const float *>(static_cast<const uint8_t *>(d.data) + off) : nullptr;
    const uint32_t n = p ? groesse / kStride : 0;
    e.t = zeitLesen(m->ein);
    e.pwNs = int64_t(pw_stream_get_nsec(m->ein));
    e.pufferZeit = b->time;
    // Einsätze (Betrag > 0,1 nach 250 ms Ruhe), wie Aufnahme::lesen in klickmessung.cpp
    for (uint32_t i = 0; i < n; ++i) {
        const float v = std::max(std::fabs(p[2 * i]), std::fabs(p[2 * i + 1]));
        if (v > 0.1f) {
            if (m->ruhe >= kRate / 4) {
                Einsatz x;
                x.lage = m->lageEin + i;
                x.ns = e.steadyNs - int64_t(double(n - i) * 1e9 / kRate);
                x.pufferZeit = b->time;
                x.pwMinusSteady = e.pwNs - e.steadyNs;
                x.index = i;
                x.frames = n;
                m->einsaetze.add(x);
            }
            m->ruhe = 0;
        } else {
            ++m->ruhe;
        }
    }
    e.lage = m->lageEin;
    e.frames = n;
    m->lageEin += n;
    m->aufrufeEin.add(e);
    pw_stream_queue_buffer(m->ein, b);
}

void zustandMerken(Messung *m, const char *strom, pw_stream_state alt, pw_stream_state neu, const char *fehler)
{
    m->zustaende.push_back({steadyNs(), strom, alt, neu, QString::fromUtf8(fehler ? fehler : "")});
}

void ausZustand(void *data, pw_stream_state alt, pw_stream_state neu, const char *fehler)
{
    auto *m = static_cast<Messung *>(data);
    m->zustandAus = neu;
    zustandMerken(m, "ausgabe", alt, neu, fehler);
}

void einZustand(void *data, pw_stream_state alt, pw_stream_state neu, const char *fehler)
{
    auto *m = static_cast<Messung *>(data);
    m->zustandEin = neu;
    zustandMerken(m, "aufnahme", alt, neu, fehler);
}

// Mit Sperre aufrufen
pw_stream *stromAnlegen(Messung &m, pw_core *core, bool ausgabe, const QString &ziel, int latenzFrames)
{
    static const pw_stream_events ausEvents = [] {
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.state_changed = &ausZustand;
        e.process = &ausProcess;
        return e;
    }();
    static const pw_stream_events einEvents = [] {
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.state_changed = &einZustand;
        e.process = &einProcess;
        return e;
    }();
    pw_properties *props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Audio",
        PW_KEY_MEDIA_CATEGORY, ausgabe ? "Playback" : "Capture",
        PW_KEY_NODE_NAME, ausgabe ? "dss_p12a_pw_klicks" : "dss_p12a_pw_aufnahme",
        PW_KEY_NODE_DESCRIPTION, ausgabe ? "DSS P12b pw-messen Klicks (stumm)" : "DSS P12b pw-messen Aufnahme",
        PW_KEY_TARGET_OBJECT, ziel.toUtf8().constData(),
        PW_KEY_NODE_DONT_RECONNECT, "true",
        nullptr);
    pw_properties_setf(props, PW_KEY_NODE_LATENCY, "%d/%d", latenzFrames, kRate);
    pw_stream *s = pw_stream_new(core, ausgabe ? "dss-p12b-klicks" : "dss-p12b-aufnahme", props);
    if (!s)
        return nullptr;
    pw_stream_add_listener(s, ausgabe ? &m.ausHook : &m.einHook, ausgabe ? &ausEvents : &einEvents, &m);

    uint8_t puffer[1024];
    spa_pod_builder b{};
    spa_pod_builder_init(&b, puffer, sizeof puffer);
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = kRate;
    info.channels = kKanaele;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    const spa_pod *params[1] = {spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat, &info)};
    const auto flags = pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS
                                       | PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_DONT_RECONNECT);
    if (pw_stream_connect(s, ausgabe ? PW_DIRECTION_OUTPUT : PW_DIRECTION_INPUT, PW_ID_ANY, flags, params, 1) < 0) {
        spa_hook_remove(ausgabe ? &m.ausHook : &m.einHook);
        pw_stream_destroy(s);
        return nullptr;
    }
    return s;
}

// Mit Sperre. 1 = beide Ströme laufen und hängen nur an ihren Zielen; 0 = noch nicht so weit;
// −1 = Verbindung zu einem anderen Knoten; −2 = Strom im Fehlerzustand.
int verbindungPruefen(const Messung &m, const PwSitzung &s, uint32_t zielAus, uint32_t zielEin, QString *was)
{
    if (m.zustandAus == PW_STREAM_STATE_ERROR || m.zustandEin == PW_STREAM_STATE_ERROR) {
        *was = QStringLiteral("Strom im Fehlerzustand");
        return -2;
    }
    const uint32_t idAus = pw_stream_get_node_id(m.aus);
    const uint32_t idEin = pw_stream_get_node_id(m.ein);
    bool ausOk = false, einOk = false;
    for (const auto &[id, v] : s.verbindungen()) {
        if (idAus != SPA_ID_INVALID && v.aus == idAus) {
            if (v.ein != zielAus) {
                *was = QStringLiteral("Ausgabe verbunden mit Knoten %1 «%2»")
                           .arg(v.ein).arg(s.knoten().count(v.ein) ? s.knoten().at(v.ein).name : QString());
                return -1;
            }
            ausOk = true;
        }
        if (idEin != SPA_ID_INVALID && v.ein == idEin) {
            if (v.aus != zielEin) {
                *was = QStringLiteral("Aufnahme verbunden mit Knoten %1 «%2»")
                           .arg(v.aus).arg(s.knoten().count(v.aus) ? s.knoten().at(v.aus).name : QString());
                return -1;
            }
            einOk = true;
        }
    }
    if (m.gefahr) {
        *was = QStringLiteral("Verbindung zu einem fremden Knoten gemeldet (Registry)");
        return -1;
    }
    const bool laufen = m.zustandAus == PW_STREAM_STATE_STREAMING && m.zustandEin == PW_STREAM_STATE_STREAMING;
    *was = QStringLiteral("Ausgabe %1, Aufnahme %2, Verbindung Ausgabe %3, Aufnahme %4")
               .arg(QString::fromUtf8(pw_stream_state_as_string(m.zustandAus)),
                    QString::fromUtf8(pw_stream_state_as_string(m.zustandEin)),
                    ausOk ? QStringLiteral("ja") : QStringLiteral("nein"),
                    einOk ? QStringLiteral("ja") : QStringLiteral("nein"));
    return laufen && ausOk && einOk ? 1 : 0;
}

double median(std::vector<double> v)
{
    return v.empty() ? kNaN : kennzahlen(std::move(v)).median;
}

QString zahl(double v, int stellen = 3)
{
    return std::isnan(v) ? QString() : QString::number(v, 'f', stellen);
}

void zeile(QFile &f, const QString &s)
{
    f.write(s.toUtf8());
    f.write("\n");
}

// CSV mit benannten Spalten; nicht genannte Spalten einer Zeile bleiben leer
class Csv
{
public:
    explicit Csv(const char *kopf) : m_spalten(QString::fromLatin1(kopf).split(QLatin1Char(','))) {}
    QString kopf() const { return m_spalten.join(QLatin1Char(',')); }
    QString zeile(std::initializer_list<std::pair<const char *, QString>> werte) const
    {
        QStringList r;
        r.resize(m_spalten.size());
        for (const auto &[name, wert] : werte) {
            const qsizetype i = m_spalten.indexOf(QLatin1String(name));
            if (i < 0)
                qFatal("Spalte %s fehlt", name);
            r[i] = wert;
        }
        return r.join(QLatin1Char(','));
    }

private:
    QStringList m_spalten;
};

QString n(int64_t v)
{
    return QString::number(v);
}

QString n(uint64_t v)
{
    return QString::number(v);
}

QString n(uint32_t v)
{
    return QString::number(v);
}

} // namespace

int pwListe()
{
    PwSitzung s;
    QString fehler;
    if (!s.verbinden(&fehler)) {
        std::fprintf(stderr, "%s\n", qPrintable(fehler));
        return 3;
    }
    std::printf("QCoreApplication (ohne QGuiApplication), Qt %s, libpipewire %s (Header %s)\n%s", qVersion(),
                pw_get_library_version(), pw_get_headers_version(), qPrintable(geraeteListe(s)));
    return 0;
}

int pwKlickMessung(const PwKlickOptionen &o)
{
    // Sicherung (unhörbar): Klicks nur zwischen den virtuellen Knoten des Messtests
    if (!o.ausgang.startsWith(kPraefix) || !o.eingang.startsWith(kPraefix)) {
        std::fprintf(stderr, "Klicks nur zwischen Knoten «dss_p12a…» (Ausgang «%s», Eingang «%s»)\n",
                     qPrintable(o.ausgang), qPrintable(o.eingang));
        return 64;
    }
    auto ende = [&] { return o.ende && o.ende->load(); };
    PwSitzung s;
    QString fehler;
    if (!s.verbinden(&fehler)) {
        std::fprintf(stderr, "%s\n", qPrintable(fehler));
        return 3;
    }
    s.sperren();
    const uint32_t zielAus = s.knotenNachName(o.ausgang, QStringLiteral("Audio/Sink"));
    const uint32_t zielEin = s.knotenNachName(o.eingang, QStringLiteral("Audio/Source"));
    s.entsperren();
    if (zielAus == UINT32_MAX || zielEin == UINT32_MAX) {
        std::fprintf(stderr, "Knoten nicht gefunden (Ausgang «%s»: %s, Eingang «%s»: %s)\n%s", qPrintable(o.ausgang),
                     zielAus == UINT32_MAX ? "fehlt" : "ok", qPrintable(o.eingang),
                     zielEin == UINT32_MAX ? "fehlt" : "ok", qPrintable(geraeteListe(s)));
        return 2;
    }

    Messung m;
    m.abstand = int64_t(o.abstandMs) * kRate / 1000;
    // Platz für den kleinsten Zyklus von 32 Proben (min-quantum laut pipewire.conf)
    const size_t zyklen = size_t(o.dauerS + 30) * size_t(kRate / 32) + 1000;
    m.aufrufeAus.anlegen(zyklen);
    m.aufrufeEin.anlegen(zyklen);
    m.einsaetze.anlegen(size_t(o.dauerS + 30) * 1000 / size_t(std::max(1, o.abstandMs)) * 4 + 100);
    const int latenzFrames = o.latenzMs * kRate / 1000;

    const int64_t startNs = steadyNs();
    const int64_t startEpoch = epochMs();
    s.sperren();
    // Fremde Verbindung: Klicks sofort aus, noch bevor die nächste Prüfung läuft
    s.neueVerbindung = [&](const PwVerbindung &v) {
        const uint32_t idAus = m.aus ? pw_stream_get_node_id(m.aus) : SPA_ID_INVALID;
        const uint32_t idEin = m.ein ? pw_stream_get_node_id(m.ein) : SPA_ID_INVALID;
        if ((idAus != SPA_ID_INVALID && v.aus == idAus && v.ein != zielAus)
            || (idEin != SPA_ID_INVALID && v.ein == idEin && v.aus != zielEin)) {
            m.freigabe = false;
            m.gefahr = true;
        }
    };
    // Aufnahme zuerst, damit der erste Klick nicht fehlt
    m.ein = stromAnlegen(m, s.core(), false, o.eingang, latenzFrames);
    m.aus = m.ein ? stromAnlegen(m, s.core(), true, o.ausgang, latenzFrames) : nullptr;
    s.entsperren();

    auto stroemeBeenden = [&] {
        s.sperren();
        s.neueVerbindung = nullptr;
        if (m.aus) {
            pw_stream_disconnect(m.aus);
            spa_hook_remove(&m.ausHook);
            pw_stream_destroy(m.aus);
            m.aus = nullptr;
        }
        if (m.ein) {
            pw_stream_disconnect(m.ein);
            spa_hook_remove(&m.einHook);
            pw_stream_destroy(m.ein);
            m.ein = nullptr;
        }
        s.entsperren();
    };
    if (!m.ein || !m.aus) {
        std::fprintf(stderr, "pw_stream_new/pw_stream_connect fehlgeschlagen\n");
        stroemeBeenden();
        return 3;
    }

    // Warten, bis beide Ströme laufen und nur an ihren Zielen hängen (höchstens 10 s)
    int pruef = 0;
    QString was;
    auto pruefen = [&] {
        s.sperren();
        pruef = verbindungPruefen(m, s, zielAus, zielEin, &was);
        s.entsperren();
    };
    while (pruef == 0 && steadyNs() - startNs < 10'000'000'000 && !ende()) {
        QThread::msleep(50);
        pruefen();
    }
    if (pruef == 1) { // Nachprüfung: alle Verbindungen der Ströme stehen
        QThread::msleep(300);
        pruefen();
    }
    int rc = 0;
    int64_t freigabeNs = 0;
    if (pruef == 1) {
        m.freigabe = true;
        freigabeNs = steadyNs();
        std::printf("Messung läuft: %d s, node.latency %d/%d (%d ms), Freigabe nach %.1f ms\n", o.dauerS,
                    latenzFrames, kRate, o.latenzMs, (freigabeNs - startNs) / 1e6);
        std::fflush(stdout);
        while (steadyNs() - freigabeNs < int64_t(o.dauerS) * 1'000'000'000 && !ende()) {
            QThread::msleep(50);
            pruefen();
            if (pruef < 0) {
                m.klicksAus = true;
                rc = pruef == -1 ? 65 : 3;
                std::fprintf(stderr, "ABBRUCH bei %.1f s: %s\n", (steadyNs() - freigabeNs) / 1e9, qPrintable(was));
                break;
            }
        }
    } else {
        rc = pruef == -1 ? 65 : 3;
        std::fprintf(stderr, "ABBRUCH: Ströme nicht freigegeben (%s)\n", qPrintable(was));
    }
    m.klicksAus = true;
    QThread::msleep(700); // letzte Klicks aufnehmen
    const int64_t stoppNs = steadyNs();
    stroemeBeenden();

    // ---------- Auswertung (Ströme beendet, Listen nur noch hier gelesen)
    const size_t nAus = m.aufrufeAus.size();
    const size_t nEin = m.aufrufeEin.size();
    const size_t nEinsatz = m.einsaetze.size();

    // Abrufe der Ausgabe: Unterläufe, Zeitachse (Δticks gegen Grösse des vorigen Zyklus)
    std::vector<size_t> leerBis(nAus), kurzBis(nAus), zeitachseBis(nAus);
    std::map<int64_t, size_t> dTicksAus, dTicksEin;
    std::vector<QString> zeitachseAus, zeitachseEin;
    size_t leer = 0, kurz = 0, zeitachse = 0;
    std::vector<double> pwMinusSteadyUs, abrufAbstand, abrufLaenge, delayAusMs;
    std::vector<QString> delayWechsel;
    size_t nDelayWechsel = 0;
    const AufrufAus *vorher = nullptr;
    for (size_t i = 0; i < nAus; ++i) {
        const AufrufAus &a = m.aufrufeAus[i];
        if (a.leer)
            ++leer;
        if (a.kurz)
            ++kurz;
        if (!a.leer && a.t.gueltig) {
            pwMinusSteadyUs.push_back((a.pwNs - a.steadyNs) / 1e3);
            delayAusMs.push_back(a.t.delayMs());
            abrufLaenge.push_back(double(a.frames) * 1e3 / kRate);
            if (vorher) {
                abrufAbstand.push_back((a.steadyNs - vorher->steadyNs) / 1e6);
                const int64_t dt = int64_t(a.t.ticks - vorher->t.ticks);
                ++dTicksAus[dt];
                const int64_t erwartet = int64_t(vorher->t.size ? vorher->t.size : vorher->frames);
                if (dt != erwartet) {
                    ++zeitachse;
                    if (zeitachseAus.size() < 20)
                        zeitachseAus.push_back(QStringLiteral("  bei %1 s: Δticks %2, vorige Zyklusgrösse %3")
                                                   .arg((a.steadyNs - startNs) / 1e9, 0, 'f', 3).arg(dt).arg(erwartet));
                }
                if (a.t.delay != vorher->t.delay || a.t.size != vorher->t.size || a.t.rateDenom != vorher->t.rateDenom) {
                    ++nDelayWechsel;
                    if (delayWechsel.size() < 30)
                        delayWechsel.push_back(QStringLiteral("  bei %1 s: delay %2 → %3 (%4 → %5 ms), size %6 → %7")
                                                   .arg((a.steadyNs - startNs) / 1e9, 0, 'f', 3)
                                                   .arg(vorher->t.delay).arg(a.t.delay)
                                                   .arg(vorher->t.delayMs(), 0, 'f', 2).arg(a.t.delayMs(), 0, 'f', 2)
                                                   .arg(vorher->t.size).arg(a.t.size));
                }
            }
            vorher = &a;
        }
        leerBis[i] = leer;
        kurzBis[i] = kurz;
        zeitachseBis[i] = zeitachse;
    }
    size_t leerEin = 0, zeitachseEinN = 0;
    std::vector<double> delayEinMs;
    const AufrufEin *vorherEin = nullptr;
    for (size_t i = 0; i < nEin; ++i) {
        const AufrufEin &e = m.aufrufeEin[i];
        if (e.leer) {
            ++leerEin;
            continue;
        }
        if (!e.t.gueltig)
            continue;
        delayEinMs.push_back(e.t.delayMs());
        if (vorherEin) {
            const int64_t dt = int64_t(e.t.ticks - vorherEin->t.ticks);
            ++dTicksEin[dt];
            const int64_t erwartet = int64_t(vorherEin->t.size ? vorherEin->t.size : vorherEin->frames);
            if (dt != erwartet) {
                ++zeitachseEinN;
                if (zeitachseEin.size() < 20)
                    zeitachseEin.push_back(QStringLiteral("  bei %1 s: Δticks %2, vorige Zyklusgrösse %3")
                                               .arg((e.steadyNs - startNs) / 1e9, 0, 'f', 3).arg(dt).arg(erwartet));
            }
        }
        vorherEin = &e;
    }

    // Geschriebene Klicks: Abruf, der die erste Probe des Klicks lieferte
    struct Klick
    {
        int64_t k = 0, lage = 0;
        size_t aufruf = 0;
        uint32_t index = 0;
        int64_t schreibNs = 0;
        double erwartetNs = kNaN;
        bool gefunden = false;
        Einsatz e;
    };
    std::vector<Klick> klicks;
    for (size_t i = 0; i < nAus; ++i) {
        const AufrufAus &a = m.aufrufeAus[i];
        if (a.leer || !a.klicken)
            continue;
        for (int64_t k = (a.lage + m.abstand - 1) / m.abstand; k * m.abstand < a.lage + a.frames; ++k) {
            Klick c;
            c.k = k;
            c.lage = k * m.abstand;
            c.aufruf = i;
            c.index = uint32_t(c.lage - a.lage);
            c.schreibNs = a.steadyNs;
            // stream.h:339-347, umgerechnet auf steady_clock: Abrufzeit + Wartendes in Frames
            // + delay − bereits vergangene Zeit seit pw_time.now (beides in der Uhr von PipeWire)
            if (a.t.gueltig && a.t.rateDenom)
                c.erwartetNs = double(a.steadyNs) + double(a.t.buffered + a.t.queued + c.index) * 1e9 / kRate
                               + double(a.t.delay) * a.t.tickNs() - double(a.pwNs - a.t.now);
            klicks.push_back(c);
        }
    }

    // Einsätze nach der Zeit zuordnen (wie Z0, neu_auswerten.py): Bezug = Median von (Einsatz −
    // Schreibzeit des letzten davor geschriebenen Klicks) über die ersten 10 Einsätze; jeder Einsatz
    // gehört zum Klick mit der nächsten Zeit «Schreibzeit + Bezug», Fenster ± halber Klickabstand.
    const double abstandNs = double(m.abstand) * 1e9 / kRate;
    std::vector<Einsatz> fremd;
    double bezugNs = kNaN;
    if (!klicks.empty()) {
        std::vector<double> kandidaten;
        for (size_t j = 0; j < nEinsatz && kandidaten.size() < 10; ++j) {
            const Einsatz &e = m.einsaetze[j];
            auto it = std::upper_bound(klicks.begin(), klicks.end(), e.ns,
                                       [](int64_t ns, const Klick &c) { return ns < c.schreibNs; });
            if (it != klicks.begin())
                kandidaten.push_back(double(e.ns - std::prev(it)->schreibNs));
        }
        bezugNs = median(kandidaten);
    }
    std::vector<double> abweichungMs;
    for (size_t j = 0; j < nEinsatz; ++j) {
        const Einsatz &e = m.einsaetze[j];
        if (klicks.empty() || std::isnan(bezugNs)) {
            fremd.push_back(e);
            continue;
        }
        const double t = double(e.ns) - bezugNs;
        auto it = std::lower_bound(klicks.begin(), klicks.end(), t,
                                   [](const Klick &c, double x) { return double(c.schreibNs) < x; });
        Klick *best = nullptr;
        for (auto c : {it, it == klicks.begin() ? klicks.end() : std::prev(it)})
            if (c != klicks.end() && (!best || std::fabs(double(c->schreibNs) - t) < std::fabs(double(best->schreibNs) - t)))
                best = &*c;
        const double dev = best ? t - double(best->schreibNs) : kNaN;
        if (!best || std::fabs(dev) >= abstandNs / 2 || best->gefunden) {
            fremd.push_back(e);
            continue;
        }
        best->gefunden = true;
        best->e = e;
        abweichungMs.push_back(dev / 1e6);
    }

    // CSV je Klick
    QFile csv(o.aus + QStringLiteral(".csv"));
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "kann %s nicht schreiben\n", qPrintable(csv.fileName()));
        return 4;
    }
    const Csv spalten("art,k,lage,geschrieben_ms,index,delay,rate,queued,buffered,queued_buffers,avail_buffers,"
                      "size,requested,vergangen_us,erwartet_ms,aufnahme_lage,aufnahme_ms,aufnahme_pufferzeit_ms,"
                      "aufnahme_index,aufnahme_frames,roh_ms,rest_ms,erwartet_verz_ms,versatz_proben,leer_bis_hier,"
                      "kurz_bis_hier,zeitachse_bis_hier,epoch_ms,text");
    zeile(csv, spalten.kopf());
    std::vector<double> roh, rest, erwVerz, queuedV, bufferedV;
    std::vector<std::pair<int64_t, int64_t>> spruenge; // Klick, Änderung der Lage-Differenz in Proben
    int64_t versatzVorher = std::numeric_limits<int64_t>::min();
    size_t fehlend = 0, ohneZeit = 0;
    auto epochVon = [&](int64_t ns) { return double(startEpoch) + (ns - startNs) / 1e6; };
    for (size_t i = 0; i < klicks.size(); ++i) {
        const Klick &c = klicks[i];
        const AufrufAus &a = m.aufrufeAus[c.aufruf];
        const double geschrieben = (c.schreibNs - startNs) / 1e6;
        const double erwartet = (c.erwartetNs - double(startNs)) / 1e6;
        if (std::isnan(c.erwartetNs))
            ++ohneZeit;
        else
            erwVerz.push_back((c.erwartetNs - double(c.schreibNs)) / 1e6);
        queuedV.push_back(double(a.t.queued));
        bufferedV.push_back(double(a.t.buffered));
        const QString erwVerzText =
            zahl(std::isnan(c.erwartetNs) ? kNaN : (c.erwartetNs - double(c.schreibNs)) / 1e6);
        QString aufLage, aufMsText, zyklusText, aufIndex, aufFrames, rohText, restText, versatzText;
        if (c.gefunden) {
            const double aufMs = (c.e.ns - startNs) / 1e6;
            const double r = aufMs - geschrieben;
            const double re = std::isnan(c.erwartetNs) ? kNaN : aufMs - erwartet;
            roh.push_back(r);
            if (!std::isnan(re))
                rest.push_back(re);
            const int64_t versatz = c.e.lage - c.lage;
            if (versatzVorher != std::numeric_limits<int64_t>::min() && std::llabs(versatz - versatzVorher) > 2)
                spruenge.push_back({int64_t(i), versatz - versatzVorher});
            versatzVorher = versatz;
            aufLage = n(c.e.lage);
            aufMsText = zahl(aufMs);
            // pw_buffer.time (Uhr von PipeWire) über den Abstand der Uhren im selben Abruf umgerechnet
            if (c.e.pufferZeit)
                zyklusText = zahl((double(c.e.pufferZeit) - double(c.e.pwMinusSteady) - double(startNs)) / 1e6);
            aufIndex = n(c.e.index);
            aufFrames = n(c.e.frames);
            rohText = zahl(r);
            restText = zahl(re);
            versatzText = n(versatz);
        } else {
            ++fehlend;
        }
        zeile(csv, spalten.zeile({{"art", QStringLiteral("klick")},
                            {"k", n(uint64_t(i))},
                            {"lage", n(c.lage)},
                            {"geschrieben_ms", zahl(geschrieben)},
                            {"index", n(c.index)},
                            {"delay", n(a.t.delay)},
                            {"rate", n(a.t.rateNum) + QLatin1Char('/') + n(a.t.rateDenom)},
                            {"queued", n(a.t.queued)},
                            {"buffered", n(a.t.buffered)},
                            {"queued_buffers", n(a.t.queuedBuffers)},
                            {"avail_buffers", n(a.t.availBuffers)},
                            {"size", n(a.t.size)},
                            {"requested", n(a.requested)},
                            {"vergangen_us", zahl((a.pwNs - a.t.now) / 1e3, 1)},
                            {"erwartet_ms", zahl(erwartet)},
                            {"aufnahme_lage", aufLage},
                            {"aufnahme_ms", aufMsText},
                            {"aufnahme_pufferzeit_ms", zyklusText},
                            {"aufnahme_index", aufIndex},
                            {"aufnahme_frames", aufFrames},
                            {"roh_ms", rohText},
                            {"rest_ms", restText},
                            {"erwartet_verz_ms", erwVerzText},
                            {"versatz_proben", versatzText},
                            {"leer_bis_hier", n(uint64_t(leerBis[c.aufruf]))},
                            {"kurz_bis_hier", n(uint64_t(kurzBis[c.aufruf]))},
                            {"zeitachse_bis_hier", n(uint64_t(zeitachseBis[c.aufruf]))},
                            {"epoch_ms", zahl(epochVon(c.schreibNs), 0)}}));
    }
    for (const Einsatz &e : fremd)
        zeile(csv, spalten.zeile({{"art", QStringLiteral("fremd")},
                            {"aufnahme_lage", n(e.lage)},
                            {"aufnahme_ms", zahl((e.ns - startNs) / 1e6)},
                            {"aufnahme_index", n(e.index)},
                            {"aufnahme_frames", n(e.frames)},
                            {"epoch_ms", zahl(epochVon(e.ns), 0)}}));
    for (const Zustand &z : m.zustaende)
        zeile(csv, spalten.zeile({{"art", QStringLiteral("zustand")},
                            {"geschrieben_ms", zahl((z.ns - startNs) / 1e6)},
                            {"epoch_ms", zahl(epochVon(z.ns), 0)},
                            {"text", QStringLiteral("\"%1 %2 → %3 %4\"")
                                         .arg(QString::fromUtf8(z.strom),
                                              QString::fromUtf8(pw_stream_state_as_string(z.alt)),
                                              QString::fromUtf8(pw_stream_state_as_string(z.neu)),
                                              QString(z.fehler).replace(QLatin1Char('"'), QLatin1Char('\'')))}}));
    csv.close();

    // CSV je Abruf (beide Ströme), für die Auswertung der Wechsel des Quantums
    QFile abrufe(o.aus + QStringLiteral("-aufrufe.csv"));
    if (abrufe.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const Csv a("strom,steady_ms,pw_minus_steady_us,vergangen_us,gueltig,ticks,delay,rate,queued,buffered,"
                    "queued_buffers,avail_buffers,size,lage,frames,requested,platz,leer,kurz,klicken,pufferzeit_ms");
        zeile(abrufe, a.kopf());
        auto ja = [](bool b) { return b ? QStringLiteral("1") : QStringLiteral("0"); };
        for (size_t i = 0; i < nAus; ++i) {
            const AufrufAus &x = m.aufrufeAus[i];
            if (x.leer) {
                zeile(abrufe, a.zeile({{"strom", QStringLiteral("aus")},
                                       {"steady_ms", zahl((x.steadyNs - startNs) / 1e6)},
                                       {"lage", n(x.lage)},
                                       {"leer", ja(true)}}));
                continue;
            }
            zeile(abrufe, a.zeile({{"strom", QStringLiteral("aus")},
                                   {"steady_ms", zahl((x.steadyNs - startNs) / 1e6)},
                                   {"pw_minus_steady_us", zahl((x.pwNs - x.steadyNs) / 1e3, 1)},
                                   {"vergangen_us", zahl((x.pwNs - x.t.now) / 1e3, 1)},
                                   {"gueltig", ja(x.t.gueltig)},
                                   {"ticks", n(x.t.ticks)},
                                   {"delay", n(x.t.delay)},
                                   {"rate", n(x.t.rateNum) + QLatin1Char('/') + n(x.t.rateDenom)},
                                   {"queued", n(x.t.queued)},
                                   {"buffered", n(x.t.buffered)},
                                   {"queued_buffers", n(x.t.queuedBuffers)},
                                   {"avail_buffers", n(x.t.availBuffers)},
                                   {"size", n(x.t.size)},
                                   {"lage", n(x.lage)},
                                   {"frames", n(x.frames)},
                                   {"requested", n(x.requested)},
                                   {"platz", n(x.platz)},
                                   {"leer", ja(false)},
                                   {"kurz", ja(x.kurz)},
                                   {"klicken", ja(x.klicken)}}));
        }
        for (size_t i = 0; i < nEin; ++i) {
            const AufrufEin &x = m.aufrufeEin[i];
            if (x.leer) {
                zeile(abrufe, a.zeile({{"strom", QStringLiteral("ein")},
                                       {"steady_ms", zahl((x.steadyNs - startNs) / 1e6)},
                                       {"lage", n(x.lage)},
                                       {"leer", ja(true)}}));
                continue;
            }
            zeile(abrufe, a.zeile({{"strom", QStringLiteral("ein")},
                                   {"steady_ms", zahl((x.steadyNs - startNs) / 1e6)},
                                   {"pw_minus_steady_us", zahl((x.pwNs - x.steadyNs) / 1e3, 1)},
                                   {"vergangen_us", zahl((x.pwNs - x.t.now) / 1e3, 1)},
                                   {"gueltig", ja(x.t.gueltig)},
                                   {"ticks", n(x.t.ticks)},
                                   {"delay", n(x.t.delay)},
                                   {"rate", n(x.t.rateNum) + QLatin1Char('/') + n(x.t.rateDenom)},
                                   {"queued", n(x.t.queued)},
                                   {"buffered", n(x.t.buffered)},
                                   {"queued_buffers", n(x.t.queuedBuffers)},
                                   {"avail_buffers", n(x.t.availBuffers)},
                                   {"size", n(x.t.size)},
                                   {"lage", n(x.lage)},
                                   {"frames", n(x.frames)},
                                   {"leer", ja(false)},
                                   {"pufferzeit_ms", x.pufferZeit ? zahl((double(x.pufferZeit) - double(x.pwNs - x.steadyNs)
                                                                          - double(startNs)) / 1e6)
                                                                  : QString()}}));
        }
        abrufe.close();
    }

    // Marken (z. B. Wechsel des Quantums): Median von 5 Klicks vor gegen 5 Klicks nach der Marke
    struct Marke
    {
        double epoch;
        QString text;
    };
    std::vector<Marke> marken;
    if (!o.marken.isEmpty()) {
        QFile f(o.marken);
        if (f.open(QIODevice::ReadOnly)) {
            for (const QByteArray &z : f.readAll().split('\n')) {
                const QString t = QString::fromUtf8(z).trimmed();
                const qsizetype leerz = t.indexOf(QLatin1Char(' '));
                bool ok = false;
                const double ep = t.left(leerz).toDouble(&ok);
                if (ok)
                    marken.push_back({ep, leerz > 0 ? t.mid(leerz + 1) : QString()});
            }
        }
    }

    QString t;
    t += QStringLiteral("Z3 Klickmessung pw_stream (pw-messen), Beginn (Wanduhr) %1 ms, Dauer %2 s\n")
             .arg(startEpoch).arg(o.dauerS);
    t += QStringLiteral("libpipewire %1 (Header %2), Qt %3, QCoreApplication ohne Qt Gui und Qt Multimedia\n")
             .arg(QString::fromUtf8(pw_get_library_version()), QString::fromUtf8(pw_get_headers_version()),
                  QString::fromUtf8(qVersion()));
    t += QStringLiteral("Ausgang: %1 (id %2), Eingang: %3 (id %4), node.latency %5/%6 = %7 ms\n")
             .arg(o.ausgang).arg(zielAus).arg(o.eingang).arg(zielEin).arg(latenzFrames).arg(kRate).arg(o.latenzMs);
    t += QStringLiteral("Freigabe der Klicks nach %1 ms, Ende der Klicks bei %2 s, Exit-Code %3\n")
             .arg(freigabeNs ? zahl((freigabeNs - startNs) / 1e6, 1) : QStringLiteral("–"))
             .arg((stoppNs - startNs) / 1e9 - 0.7, 0, 'f', 1).arg(rc);
    for (const Zustand &z : m.zustaende)
        t += QStringLiteral("  Zustand %1 bei %2 ms: %3 → %4%5\n")
                 .arg(QString::fromUtf8(z.strom)).arg((z.ns - startNs) / 1e6, 0, 'f', 1)
                 .arg(QString::fromUtf8(pw_stream_state_as_string(z.alt)), QString::fromUtf8(pw_stream_state_as_string(z.neu)),
                      z.fehler.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(z.fehler));
    t += QStringLiteral("Klicks geschrieben %1, gefunden %2, fehlend %3, fremde Einsätze %4, ohne Zeitangabe %5\n")
             .arg(klicks.size()).arg(klicks.size() - fehlend).arg(fehlend).arg(fremd.size()).arg(ohneZeit);
    t += QStringLiteral("Zuordnung nach der Zeit: Bezug %1 ms, Abstand zum Bezug %2\n")
             .arg(zahl(bezugNs / 1e6)).arg(kennzahlenText(kennzahlen(abweichungMs), "ms"));
    if (!klicks.empty() && klicks.front().gefunden && freigabeNs)
        t += QStringLiteral("Startverzögerung (Freigabe bis Klick 0 in der Aufnahme): %1 ms\n")
                 .arg((klicks.front().e.ns - freigabeNs) / 1e6, 0, 'f', 3);
    const Kennzahlen kRest = kennzahlen(rest);
    t += QStringLiteral("Verzögerung roh (Schreiben → Aufnahme): %1\n").arg(kennzahlenText(kennzahlen(roh), "ms"));
    t += QStringLiteral("Erwartete Verzögerung aus pw_time (erwartet − Schreiben): %1\n")
             .arg(kennzahlenText(kennzahlen(erwVerz), "ms"));
    t += QStringLiteral("Rest (Aufnahme − erwartet): %1\n").arg(kennzahlenText(kRest, "ms"));
    t += QStringLiteral("  p5–p95 des Rests: %1 ms (E4 in Ruhe ≤ 15 ms)\n").arg(zahl(kRest.p95 - kRest.p5));
    t += QStringLiteral("Gemeldete Verzögerung pw_time.delay (Ausgabe): %1\n").arg(kennzahlenText(kennzahlen(delayAusMs), "ms"));
    t += QStringLiteral("  queued (Frames) je Klick: %1\n").arg(kennzahlenText(kennzahlen(queuedV), "Frames"));
    t += QStringLiteral("  buffered (Frames) je Klick: %1\n").arg(kennzahlenText(kennzahlen(bufferedV), "Frames"));
    t += QStringLiteral("  Änderungen von delay, size oder rate: %1%2\n")
             .arg(nDelayWechsel).arg(nDelayWechsel > delayWechsel.size() ? QStringLiteral(" (die ersten 30)") : QString());
    for (const QString &z : delayWechsel)
        t += z + QLatin1Char('\n');
    t += QStringLiteral("Gemeldete Verzögerung pw_time.delay (Aufnahme): %1\n").arg(kennzahlenText(kennzahlen(delayEinMs), "ms"));
    t += QStringLiteral("Uhr: pw_stream_get_nsec − steady_clock je Abruf: %1\n")
             .arg(kennzahlenText(kennzahlen(pwMinusSteadyUs), "µs"));
    t += QStringLiteral("Abrufe Ausgabe: %1, Abstand %2\n").arg(nAus).arg(kennzahlenText(kennzahlen(abrufAbstand), "ms"));
    t += QStringLiteral("  Länge: %1\n").arg(kennzahlenText(kennzahlen(abrufLaenge), "ms"));
    QString verteilung;
    for (const auto &[dt, n] : dTicksAus)
        verteilung += QStringLiteral(" %1×%2").arg(n).arg(dt);
    t += QStringLiteral("  Δticks (Anzahl×Wert):%1\n").arg(verteilung);
    verteilung.clear();
    for (const auto &[dt, n] : dTicksEin)
        verteilung += QStringLiteral(" %1×%2").arg(n).arg(dt);
    t += QStringLiteral("Abrufe Aufnahme: %1, Δticks (Anzahl×Wert):%2\n").arg(nEin).arg(verteilung);
    t += QStringLiteral("Unterläufe Ausgabe: leere Abrufe %1, zu kurze Abrufe %2 (E2: beide 0)\n").arg(leer).arg(kurz);
    t += QStringLiteral("Sprünge der Zeitachse (Δticks ≠ vorige Zyklusgrösse, Hinweis auf Aussetzer, nicht geprüft): "
                        "Ausgabe %1, Aufnahme %2; leere Abrufe Aufnahme %3\n")
             .arg(zeitachse).arg(zeitachseEinN).arg(leerEin);
    for (const QString &z : zeitachseAus)
        t += QStringLiteral("  Ausgabe") + z.mid(1) + QLatin1Char('\n');
    for (const QString &z : zeitachseEin)
        t += QStringLiteral("  Aufnahme") + z.mid(1) + QLatin1Char('\n');
    t += QStringLiteral("Listen voll (verworfen): Abrufe Ausgabe %1, Aufnahme %2, Einsätze %3\n")
             .arg(m.aufrufeAus.verworfen()).arg(m.aufrufeEin.verworfen()).arg(m.einsaetze.verworfen());
    t += QStringLiteral("Sprünge der Lage (Aufnahme − Ausgabe, > 2 Proben): %1\n").arg(spruenge.size());
    for (size_t i = 0; i < spruenge.size() && i < 30; ++i)
        t += QStringLiteral("  bei Klick %1 (%2 s): %3 Proben = %4 ms\n")
                 .arg(spruenge[i].first)
                 .arg((klicks[size_t(spruenge[i].first)].schreibNs - startNs) / 1e9, 0, 'f', 1)
                 .arg(spruenge[i].second).arg(double(spruenge[i].second) * 1e3 / kRate, 0, 'f', 2);
    bool e1 = true;
    for (const Marke &mk : marken) {
        std::vector<const Klick *> vor, nach;
        for (const Klick &c : klicks) {
            if (!c.gefunden || std::isnan(c.erwartetNs))
                continue;
            if (epochVon(c.schreibNs) < mk.epoch)
                vor.push_back(&c);
            else if (nach.size() < 5)
                nach.push_back(&c);
        }
        if (vor.size() > 5)
            vor.erase(vor.begin(), vor.end() - 5);
        auto med = [](const std::vector<const Klick *> &v, auto f) {
            std::vector<double> x;
            for (const Klick *c : v)
                x.push_back(f(*c));
            return median(x);
        };
        auto fRest = [&](const Klick &c) { return (double(c.e.ns) - c.erwartetNs) / 1e6; };
        auto fRoh = [&](const Klick &c) { return double(c.e.ns - c.schreibNs) / 1e6; };
        auto fErw = [&](const Klick &c) { return (c.erwartetNs - double(c.schreibNs)) / 1e6; };
        auto fLage = [&](const Klick &c) { return double(c.e.lage - c.lage); };
        const double rv = med(vor, fRest), rn = med(nach, fRest);
        const bool ok = vor.size() == 5 && nach.size() == 5 && std::fabs(rn - rv) <= 10.0;
        e1 = e1 && ok;
        t += QStringLiteral("Marke «%1» bei %2 s (je %3 Klicks vor / %4 nach):\n")
                 .arg(mk.text).arg((mk.epoch - double(startEpoch)) / 1e3, 0, 'f', 3).arg(vor.size()).arg(nach.size());
        t += QStringLiteral("  Rest Median %1 → %2 ms, Änderung %3 ms (E1 ≤ 10 ms: %4)\n")
                 .arg(zahl(rv), zahl(rn), zahl(rn - rv), ok ? QStringLiteral("ja") : QStringLiteral("nein"));
        t += QStringLiteral("  roh Median %1 → %2 ms, Änderung %3 ms (roher Sprung)\n")
                 .arg(zahl(med(vor, fRoh)), zahl(med(nach, fRoh)), zahl(med(nach, fRoh) - med(vor, fRoh)));
        t += QStringLiteral("  erwartete Verzögerung Median %1 → %2 ms, Änderung %3 ms\n")
                 .arg(zahl(med(vor, fErw)), zahl(med(nach, fErw)), zahl(med(nach, fErw) - med(vor, fErw)));
        t += QStringLiteral("  Lage-Differenz Median %1 → %2 Proben\n")
                 .arg(zahl(med(vor, fLage), 0), zahl(med(nach, fLage), 0));
    }
    if (!marken.empty())
        t += QStringLiteral("E1 (alle %1 Marken, Änderung des Rests ≤ 10 ms): %2\n")
                 .arg(marken.size()).arg(e1 ? QStringLiteral("erfüllt") : QStringLiteral("NICHT erfüllt"));

    QFile z(o.aus + QStringLiteral("-zusammenfassung.txt"));
    if (z.open(QIODevice::WriteOnly | QIODevice::Truncate))
        z.write(t.toUtf8());
    std::printf("%s", qPrintable(t));
    return rc;
}
