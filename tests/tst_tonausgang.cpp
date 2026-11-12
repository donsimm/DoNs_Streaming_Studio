// P12b b3: Ausgabe über PipeWire (dss::PwOutput) mit virtuellen Senken im eigenen Prozess (Plan P12,
// Abschnitt «b3»). Ohne Qt Gui. Unhörbar: Der Mischer hat keine Quelle (nur Stille), und der Strom
// zielt nur auf eigene virtuelle Senken (support.null-audio-sink, Name «dss_test_tonausgang_<pid>…»),
// nie auf ein Gerät. Der Test ändert weder den Standardausgang noch clock.force-quantum; der
// Standardausgang wird am Anfang, nach jeder neuen Senke und am Ende verglichen.
//
// Die Senken legt eine eigene Verbindung (TestPw) über die Fabrik «adapter» an, ohne object.linger:
// Sie gehören dieser Verbindung und verschwinden mit pw_proxy_destroy bzw. mit der Verbindung.
// cleanupTestCase prüft mit einer neuen Verbindung, dass keine Knoten «dss_test_tonausgang_<pid>…»
// übrig sind.
//
// - steadyClockIsMonotonic: steady_clock liegt zwischen zwei Ablesungen von CLOCK_MONOTONIC.
// - listAndPlay: Liste enthält die Senke (Beschreibung, nicht Standard); Wahl → «playing», der
//   Strom hängt nur an der Senke; je Abruf rate 1/48000, size > 0, sinceNow 0 bis 50 ms;
//   pw_stream_get_nsec() immer zwischen zwei Ablesungen von steady_clock (dieselbe Uhr); der
//   Mischer bekommt die Abrufe.
// - missingTarget: gewählter Ausgang fehlt → «missing», kein Strom, keine Verbindung, kein Abruf.
// - removeAndReturn: Senke entfernt → «missing», Strom weg, keine Verbindung zu einem anderen
//   Knoten, keine Abrufe mehr; Senke mit gleichem Namen wieder da → wieder «playing» ohne Zutun.
// - foreignLink: Verbindung des Stroms zu einer zweiten Senke von aussen → Stille, Strom weg,
//   «error»; neue Wahl → wieder «playing».
// - noTarget: kein Ausgang gewählt → «no_target», kein Strom.
// In allen Fällen: Alle je gesehenen Verbindungen des Stroms gingen nur zur gewählten Senke.
//
// Quellen (lokal gelesen 2026-11-12, kein Webzugang):
//   /usr/include/pipewire-0.3/pipewire/core.h (pw_core_create_object, sync/done),
//   /usr/include/pipewire-0.3/pipewire/keys.h (factory.name, object.linger, link.output.node,
//   link.input.node), /usr/include/pipewire-0.3/pipewire/stream.h (pw_stream_get_nsec);
//   virtuelle Senke wie «pw-cli create-node adapter { factory.name=support.null-audio-sink … }»,
//   https://docs.pipewire.org/page_man_pw-cli_1.html (nicht abgerufen);
//   steady_clock = CLOCK_MONOTONIC unter Linux (libstdc++), https://en.cppreference.com/w/cpp/chrono/steady_clock
//   (nicht abgerufen, hier im Test geprüft)

#include "mixer.h"
#include "pwoutput.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

#include <pipewire/extensions/metadata.h>
#include <pipewire/pipewire.h>

#include <unistd.h>

#include <chrono>
#include <ctime>
#include <map>
#include <memory>
#include <vector>

using namespace dss;

namespace {

int64_t steadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int64_t monotonicNs()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

QString prefix()
{
    return QStringLiteral("dss_test_tonausgang_%1").arg(::getpid());
}

QString text(const spa_dict *props, const char *key)
{
    const char *v = props ? spa_dict_lookup(props, key) : nullptr;
    return v ? QString::fromUtf8(v) : QString();
}

uint32_t idOf(const spa_dict *props, const char *key)
{
    const char *v = props ? spa_dict_lookup(props, key) : nullptr;
    bool ok = false;
    const uint32_t id = v ? QByteArray(v).toUInt(&ok) : 0;
    return ok ? id : UINT32_MAX;
}

// Eigene Verbindung zu PipeWire: legt Senken und Verbindungen an und beobachtet Knoten,
// Verbindungen und den Standardausgang.
class TestPw
{
public:
    struct LinkSeen
    {
        uint32_t outputNode, inputNode;
        QString outputName, inputName;
    };

    ~TestPw()
    {
        if (!m_loop)
            return;
        pw_thread_loop_lock(m_loop);
        for (auto &[id, p] : m_proxies)
            pw_proxy_destroy(p);
        m_proxies.clear();
        if (m_metadata) {
            spa_hook_remove(&m_metadataHook);
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_metadata));
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

    bool connect()
    {
        static const pw_core_events coreEvents = [] {
            pw_core_events e{};
            e.version = PW_VERSION_CORE_EVENTS;
            e.done = &TestPw::onDone;
            e.error = &TestPw::onError;
            return e;
        }();
        static const pw_registry_events registryEvents = [] {
            pw_registry_events e{};
            e.version = PW_VERSION_REGISTRY_EVENTS;
            e.global = &TestPw::onGlobal;
            e.global_remove = &TestPw::onGlobalRemove;
            return e;
        }();
        pw_init(nullptr, nullptr);
        m_loop = pw_thread_loop_new("tst-tonausgang", nullptr);
        m_context = m_loop ? pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0) : nullptr;
        if (!m_context || pw_thread_loop_start(m_loop) < 0)
            return false;
        pw_thread_loop_lock(m_loop);
        m_core = pw_context_connect(m_context, nullptr, 0);
        if (m_core) {
            pw_core_add_listener(m_core, &m_coreHook, &coreEvents, this);
            m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
            if (m_registry)
                pw_registry_add_listener(m_registry, &m_registryHook, &registryEvents, this);
        }
        pw_thread_loop_unlock(m_loop);
        return m_core && m_registry && sync() && sync();
    }

    bool sync()
    {
        pw_thread_loop_lock(m_loop);
        m_synced = false;
        m_seq = pw_core_sync(m_core, PW_ID_CORE, 0);
        const int64_t deadline = steadyNs() + 5'000'000'000;
        while (!m_synced && steadyNs() < deadline)
            pw_thread_loop_timed_wait(m_loop, 1);
        const bool ok = m_synced;
        pw_thread_loop_unlock(m_loop);
        return ok;
    }

    // Virtuelle Senke ohne object.linger; Rückgabe: Schlüssel für destroy()
    int createSink(const QString &name)
    {
        if (!name.startsWith(prefix())) // nur eigene Testknoten
            return -1;
        const QByteArray n = name.toUtf8();
        const QByteArray d = QStringLiteral("DSS Test Tonausgang %1 (stumm)").arg(name.section(QLatin1Char('_'), -1)).toUtf8();
        pw_properties *props = pw_properties_new("factory.name", "support.null-audio-sink",
                                                 PW_KEY_NODE_NAME, n.constData(),
                                                 PW_KEY_NODE_DESCRIPTION, d.constData(),
                                                 PW_KEY_MEDIA_CLASS, "Audio/Sink",
                                                 "audio.position", "FL,FR",
                                                 PW_KEY_OBJECT_LINGER, "false",
                                                 nullptr);
        return create("adapter", PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, props);
    }

    // Verbindung vom ersten Ausgangsport des einen zum ersten Eingangsport des anderen Knotens
    int createLink(uint32_t outputNode, uint32_t inputNode)
    {
        pw_thread_loop_lock(m_loop);
        uint32_t outPort = UINT32_MAX, inPort = UINT32_MAX;
        for (const auto &[id, p] : m_ports) {
            if (p.node == outputNode && p.direction == QLatin1String("out") && outPort == UINT32_MAX)
                outPort = id;
            if (p.node == inputNode && p.direction == QLatin1String("in") && inPort == UINT32_MAX)
                inPort = id;
        }
        pw_thread_loop_unlock(m_loop);
        if (outPort == UINT32_MAX || inPort == UINT32_MAX)
            return -1;
        pw_properties *props = pw_properties_new(PW_KEY_OBJECT_LINGER, "false", nullptr);
        pw_properties_setf(props, PW_KEY_LINK_OUTPUT_NODE, "%u", outputNode);
        pw_properties_setf(props, PW_KEY_LINK_OUTPUT_PORT, "%u", outPort);
        pw_properties_setf(props, PW_KEY_LINK_INPUT_NODE, "%u", inputNode);
        pw_properties_setf(props, PW_KEY_LINK_INPUT_PORT, "%u", inPort);
        return create("link-factory", PW_TYPE_INTERFACE_Link, PW_VERSION_LINK, props);
    }

    QString lastError()
    {
        pw_thread_loop_lock(m_loop);
        const QString r = m_error;
        pw_thread_loop_unlock(m_loop);
        return r;
    }

    void destroy(int key)
    {
        pw_thread_loop_lock(m_loop);
        if (auto it = m_proxies.find(key); it != m_proxies.end()) {
            pw_proxy_destroy(it->second);
            m_proxies.erase(it);
        }
        pw_thread_loop_unlock(m_loop);
    }

    uint32_t nodeByName(const QString &name)
    {
        pw_thread_loop_lock(m_loop);
        uint32_t r = UINT32_MAX;
        for (const auto &[id, n] : m_nodes)
            if (n.name == name)
                r = id;
        pw_thread_loop_unlock(m_loop);
        return r;
    }

    QStringList nodesWithPrefix(const QString &p)
    {
        QStringList r;
        pw_thread_loop_lock(m_loop);
        for (const auto &[id, n] : m_nodes)
            if (n.name.startsWith(p))
                r << n.name;
        pw_thread_loop_unlock(m_loop);
        return r;
    }

    // Aktuelle Verbindungen, deren Ausgang der Knoten mit diesem Namen ist: Name des Eingangs
    QStringList linksFrom(const QString &outputName)
    {
        QStringList r;
        pw_thread_loop_lock(m_loop);
        for (const auto &[id, l] : m_links) {
            const auto o = m_nodes.find(l.first);
            const auto i = m_nodes.find(l.second);
            if (o != m_nodes.end() && o->second.name == outputName)
                r << (i != m_nodes.end() ? i->second.name : QStringLiteral("?%1").arg(l.second));
        }
        pw_thread_loop_unlock(m_loop);
        return r;
    }

    std::vector<LinkSeen> seen()
    {
        pw_thread_loop_lock(m_loop);
        const auto r = m_seen;
        pw_thread_loop_unlock(m_loop);
        return r;
    }

    QString defaultSink()
    {
        pw_thread_loop_lock(m_loop);
        const QString r = m_defaultSink;
        pw_thread_loop_unlock(m_loop);
        return r;
    }

private:
    struct Node
    {
        QString name, mediaClass;
    };
    struct Port
    {
        uint32_t node;
        QString direction; // port.direction: "in" oder "out"
    };

    int create(const char *factory, const char *type, uint32_t version, pw_properties *props)
    {
        pw_thread_loop_lock(m_loop);
        auto *p = static_cast<pw_proxy *>(pw_core_create_object(m_core, factory, type, version, &props->dict, 0));
        int key = -1;
        if (p) {
            key = ++m_nextKey;
            m_proxies[key] = p;
        }
        pw_thread_loop_unlock(m_loop);
        pw_properties_free(props);
        return key;
    }

    static void onDone(void *data, uint32_t id, int seq)
    {
        auto *t = static_cast<TestPw *>(data);
        if (id == PW_ID_CORE && seq == t->m_seq) {
            t->m_synced = true;
            pw_thread_loop_signal(t->m_loop, false);
        }
    }

    static void onGlobal(void *data, uint32_t id, uint32_t, const char *type, uint32_t, const spa_dict *props)
    {
        auto *t = static_cast<TestPw *>(data);
        const QByteArray ty(type);
        if (ty == PW_TYPE_INTERFACE_Node) {
            t->m_nodes[id] = {text(props, PW_KEY_NODE_NAME), text(props, PW_KEY_MEDIA_CLASS)};
        } else if (ty == PW_TYPE_INTERFACE_Port) {
            t->m_ports[id] = {idOf(props, PW_KEY_NODE_ID), text(props, PW_KEY_PORT_DIRECTION)};
        } else if (ty == PW_TYPE_INTERFACE_Link) {
            const uint32_t o = idOf(props, PW_KEY_LINK_OUTPUT_NODE);
            const uint32_t i = idOf(props, PW_KEY_LINK_INPUT_NODE);
            t->m_links[id] = {o, i};
            auto name = [&](uint32_t n) { return t->m_nodes.count(n) ? t->m_nodes.at(n).name : QString(); };
            t->m_seen.push_back({o, i, name(o), name(i)});
        } else if (ty == PW_TYPE_INTERFACE_Metadata && !t->m_metadata
                   && text(props, "metadata.name") == QLatin1String("default")) {
            static const pw_metadata_events events = [] {
                pw_metadata_events e{};
                e.version = PW_VERSION_METADATA_EVENTS;
                e.property = &TestPw::onProperty;
                return e;
            }();
            t->m_metadata = static_cast<pw_metadata *>(
                pw_registry_bind(t->m_registry, id, PW_TYPE_INTERFACE_Metadata, PW_VERSION_METADATA, 0));
            if (t->m_metadata)
                pw_metadata_add_listener(t->m_metadata, &t->m_metadataHook, &events, t);
        }
    }

    static void onGlobalRemove(void *data, uint32_t id)
    {
        auto *t = static_cast<TestPw *>(data);
        t->m_nodes.erase(id);
        t->m_ports.erase(id);
        t->m_links.erase(id);
    }

    static void onError(void *data, uint32_t id, int seq, int res, const char *message)
    {
        auto *t = static_cast<TestPw *>(data);
        t->m_error = QStringLiteral("Objekt %1, seq %2, %3: %4").arg(id).arg(seq).arg(res).arg(QString::fromUtf8(message ? message : ""));
    }

    static int onProperty(void *data, uint32_t subject, const char *key, const char *, const char *value)
    {
        auto *t = static_cast<TestPw *>(data);
        if (subject == PW_ID_CORE && key && QByteArray(key) == "default.audio.sink") {
            const QJsonDocument d = QJsonDocument::fromJson(QByteArray(value ? value : ""));
            t->m_defaultSink = d.isObject() ? d.object().value(QStringLiteral("name")).toString()
                                            : QString::fromUtf8(value ? value : "");
        }
        return 0;
    }

    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_registry *m_registry = nullptr;
    pw_metadata *m_metadata = nullptr;
    spa_hook m_coreHook{}, m_registryHook{}, m_metadataHook{};
    int m_seq = -1;
    bool m_synced = false;
    int m_nextKey = 0;
    std::map<int, pw_proxy *> m_proxies;
    std::map<uint32_t, Node> m_nodes;
    std::map<uint32_t, Port> m_ports;
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> m_links;
    QString m_error;
    std::vector<LinkSeen> m_seen;
    QString m_defaultSink;
};

const char *stateName(const PwOutput &o)
{
    return PwOutput::stateName(o.state());
}

} // namespace

class TestTonausgang : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void steadyClockIsMonotonic();
    void listAndPlay();
    void missingTarget();
    void removeAndReturn();
    void foreignLink();
    void noTarget();
    void cleanupTestCase();

private:
    QString sinkName(const char *what) const { return prefix() + QLatin1Char('_') + QLatin1String(what); }
    QString streamName() const { return prefix() + QStringLiteral("_strom"); }
    QSet<QString> linkTargets(const QString &output)
    {
        const QStringList l = m_pw->linksFrom(output);
        return QSet<QString>(l.begin(), l.end());
    }
    // Alle je gesehenen Verbindungen des Stroms gingen zu einer erlaubten Senke
    void verifyOnlyLinkedTo(const QStringList &allowed);
    void verifyDefaultUnchanged();

    std::unique_ptr<TestPw> m_pw;
    QString m_defaultBefore;
    size_t m_seenStart = 0; // gesehene Verbindungen vor dem laufenden Testfall
};

void TestTonausgang::init()
{
    m_seenStart = m_pw->seen().size();
}

void TestTonausgang::initTestCase()
{
    m_pw = std::make_unique<TestPw>();
    if (!m_pw->connect())
        QFAIL("keine Verbindung zu PipeWire");
    m_defaultBefore = m_pw->defaultSink();
    qInfo().noquote() << "Standardausgang vorher:" << (m_defaultBefore.isEmpty() ? QStringLiteral("?") : m_defaultBefore);
    QVERIFY(m_pw->nodesWithPrefix(prefix()).isEmpty());
}

void TestTonausgang::verifyOnlyLinkedTo(const QStringList &allowed)
{
    int n = 0;
    const auto seen = m_pw->seen();
    for (size_t i = m_seenStart; i < seen.size(); ++i) {
        const auto &l = seen[i];
        if (l.outputName != streamName())
            continue;
        ++n;
        QVERIFY2(allowed.contains(l.inputName),
                 qPrintable(QStringLiteral("Strom war verbunden mit «%1» (Knoten %2)").arg(l.inputName).arg(l.inputNode)));
    }
    qInfo() << "Verbindungen des Stroms in diesem Testfall:" << n;
    QVERIFY(n > 0);
}

void TestTonausgang::verifyDefaultUnchanged()
{
    QVERIFY(m_pw->sync());
    QCOMPARE(m_pw->defaultSink(), m_defaultBefore);
    QVERIFY(!m_pw->defaultSink().startsWith(prefix()));
}

void TestTonausgang::steadyClockIsMonotonic()
{
    for (int i = 0; i < 1000; ++i) {
        const int64_t a = monotonicNs();
        const int64_t s = steadyNs();
        const int64_t b = monotonicNs();
        QVERIFY2(a <= s && s <= b, qPrintable(QStringLiteral("%1 %2 %3").arg(a).arg(s).arg(b)));
    }
}

void TestTonausgang::listAndPlay()
{
    const QString sink = sinkName("a");
    const int key = m_pw->createSink(sink);
    QVERIFY(key > 0);
    QTRY_VERIFY(m_pw->nodeByName(sink) != UINT32_MAX);
    verifyDefaultUnchanged();

    Mixer mixer;
    PwOutput out(&mixer, QStringLiteral("test"), streamName(), QStringLiteral("DSS Test Tonausgang Strom"));
    QString error;
    QVERIFY2(out.start(&error), qPrintable(error));
    QCOMPARE(out.state(), PwOutput::State::NoTarget);
    QTRY_VERIFY([&] {
        for (const AudioDevice &d : out.devices())
            if (d.name == sink)
                return true;
        return false;
    }());
    for (const AudioDevice &d : out.devices()) {
        if (d.name == sink) {
            QVERIFY(d.description.startsWith(QLatin1String("DSS Test Tonausgang")));
            QVERIFY(!d.isDefault);
        }
    }
    QCOMPARE(out.streamNodeId(), UINT32_MAX);

    out.setTarget(sink);
    QTRY_VERIFY2_WITH_TIMEOUT(out.state() == PwOutput::State::Playing, stateName(out), 10000);
    QVERIFY(out.streamNodeId() != UINT32_MAX);
    const uint64_t before = out.stats().pulls;
    QTRY_VERIFY(out.stats().pulls >= before + 50);
    const PwOutput::Stats s = out.stats();
    qInfo() << "Abrufe" << s.pulls << "ohne Puffer" << s.emptyPulls << "Stille" << s.silentPulls << "abgebrochen"
            << s.abortedCycles << "size" << s.lastSize << "delay" << s.lastDelay << "queued" << s.lastQueued
            << "buffered" << s.lastBuffered << "rate" << s.rateNum << "/" << s.rateDenom << "sinceNow max ns"
            << s.maxSinceNowNs << "Uhr ausserhalb" << s.clockOutside;
    QCOMPARE(s.clockOutside, uint64_t(0)); // pw_stream_get_nsec() und steady_clock: dieselbe Uhr
    QCOMPARE(s.rateNum, 1u);
    QCOMPARE(s.rateDenom, 48000u);
    QVERIFY(s.lastSize > 0);
    QVERIFY(s.maxSinceNowNs >= 0);
    QVERIFY(s.maxSinceNowNs < 50'000'000);
    QCOMPARE(s.streamsCreated, uint64_t(1));
    const Mixer::OutputStats ms = mixer.outputStats();
    QVERIFY(ms.pulls > 0);
    QVERIFY(ms.pulls <= s.pulls);
    QVERIFY(ms.frames > 0);
    QCOMPARE(linkTargets(streamName()), QSet<QString>{sink});
    verifyOnlyLinkedTo({sink});

    out.setTarget({});
    QTRY_VERIFY(m_pw->nodeByName(streamName()) == UINT32_MAX);
    m_pw->destroy(key);
    QTRY_VERIFY(m_pw->nodeByName(sink) == UINT32_MAX);
}

void TestTonausgang::missingTarget()
{
    Mixer mixer;
    PwOutput out(&mixer, QStringLiteral("test"), streamName(), QStringLiteral("DSS Test Tonausgang Strom"));
    QString error;
    QVERIFY2(out.start(&error), qPrintable(error));
    out.setTarget(sinkName("fehlt"));
    QCOMPARE(out.state(), PwOutput::State::Missing);
    QCOMPARE(out.streamNodeId(), UINT32_MAX);
    QTest::qWait(1000);
    QCOMPARE(out.state(), PwOutput::State::Missing);
    QVERIFY(m_pw->sync());
    QCOMPARE(m_pw->nodeByName(streamName()), UINT32_MAX);
    QVERIFY(m_pw->linksFrom(streamName()).isEmpty());
    QCOMPARE(out.stats().pulls, uint64_t(0));
    QCOMPARE(out.stats().streamsCreated, uint64_t(0));
    QCOMPARE(mixer.outputStats().pulls, uint64_t(0));
}

void TestTonausgang::removeAndReturn()
{
    const QString sink = sinkName("b");
    int key = m_pw->createSink(sink);
    QVERIFY(key > 0);
    QTRY_VERIFY(m_pw->nodeByName(sink) != UINT32_MAX);
    verifyDefaultUnchanged();

    Mixer mixer;
    PwOutput out(&mixer, QStringLiteral("test"), streamName(), QStringLiteral("DSS Test Tonausgang Strom"));
    QString error;
    QVERIFY2(out.start(&error), qPrintable(error));
    out.setTarget(sink);
    QTRY_VERIFY2_WITH_TIMEOUT(out.state() == PwOutput::State::Playing, stateName(out), 10000);
    QTRY_VERIFY(out.stats().pulls > 20);

    // Senke weg: kein Strom, keine Verbindung, keine Abrufe
    m_pw->destroy(key);
    QTRY_VERIFY2(out.state() == PwOutput::State::Missing, stateName(out));
    QCOMPARE(out.streamNodeId(), UINT32_MAX);
    QTRY_VERIFY(m_pw->nodeByName(streamName()) == UINT32_MAX);
    const uint64_t pulls = out.stats().pulls;
    const uint64_t mixed = mixer.outputStats().pulls;
    QTest::qWait(1500);
    QCOMPARE(out.state(), PwOutput::State::Missing);
    QVERIFY(m_pw->sync());
    QCOMPARE(m_pw->nodeByName(streamName()), UINT32_MAX);
    QVERIFY(m_pw->linksFrom(streamName()).isEmpty());
    QCOMPARE(out.stats().pulls, pulls);
    QCOMPARE(mixer.outputStats().pulls, mixed);
    verifyOnlyLinkedTo({sink});
    verifyDefaultUnchanged();

    // Wiederkehr mit demselben Namen: ohne Zutun wieder «playing»
    key = m_pw->createSink(sink);
    QVERIFY(key > 0);
    QTRY_VERIFY2_WITH_TIMEOUT(out.state() == PwOutput::State::Playing, stateName(out), 10000);
    QTRY_VERIFY(out.stats().pulls > pulls + 20);
    QCOMPARE(out.stats().streamsCreated, uint64_t(2));
    QCOMPARE(out.stats().clockOutside, uint64_t(0));
    QCOMPARE(linkTargets(streamName()), QSet<QString>{sink});
    verifyOnlyLinkedTo({sink});
    verifyDefaultUnchanged();

    out.setTarget({});
    QTRY_VERIFY(m_pw->nodeByName(streamName()) == UINT32_MAX);
    m_pw->destroy(key);
    QTRY_VERIFY(m_pw->nodeByName(sink) == UINT32_MAX);
}

void TestTonausgang::foreignLink()
{
    const QString sink = sinkName("c");
    const QString other = sinkName("d");
    const int key = m_pw->createSink(sink);
    const int keyOther = m_pw->createSink(other);
    QVERIFY(key > 0 && keyOther > 0);
    QTRY_VERIFY(m_pw->nodeByName(sink) != UINT32_MAX && m_pw->nodeByName(other) != UINT32_MAX);
    verifyDefaultUnchanged();

    Mixer mixer;
    PwOutput out(&mixer, QStringLiteral("test"), streamName(), QStringLiteral("DSS Test Tonausgang Strom"));
    QString error;
    QVERIFY2(out.start(&error), qPrintable(error));
    out.setTarget(sink);
    QTRY_VERIFY2_WITH_TIMEOUT(out.state() == PwOutput::State::Playing, stateName(out), 10000);
    QTRY_VERIFY(out.stats().pulls > 20);

    // Verbindung zur zweiten Senke von aussen
    const uint32_t streamNode = out.streamNodeId();
    const int link = m_pw->createLink(streamNode, m_pw->nodeByName(other));
    QVERIFY(link > 0);
    QTRY_VERIFY2(out.state() == PwOutput::State::Error,
                 qPrintable(QStringLiteral("%1, Fehler: %2").arg(QLatin1String(stateName(out)), m_pw->lastError())));
    qInfo().noquote() << "Grund:" << out.stateDetail();
    QVERIFY(out.stateDetail().contains(other));
    QCOMPARE(out.streamNodeId(), UINT32_MAX);
    QTRY_VERIFY(m_pw->nodeByName(streamName()) == UINT32_MAX);
    QTest::qWait(1500);
    QCOMPARE(out.state(), PwOutput::State::Error); // kein neuer Strom von selbst
    QCOMPARE(out.stats().streamsCreated, uint64_t(1));
    m_pw->destroy(link);

    // Neue Wahl: wieder «playing», nur an der gewählten Senke
    out.setTarget(sink);
    QTRY_VERIFY2_WITH_TIMEOUT(out.state() == PwOutput::State::Playing, stateName(out), 10000);
    QCOMPARE(linkTargets(streamName()), QSet<QString>{sink});
    verifyOnlyLinkedTo({sink, other});

    out.setTarget({});
    QTRY_VERIFY(m_pw->nodeByName(streamName()) == UINT32_MAX);
    m_pw->destroy(key);
    m_pw->destroy(keyOther);
    QTRY_VERIFY(m_pw->nodesWithPrefix(prefix()).isEmpty());
}

void TestTonausgang::noTarget()
{
    Mixer mixer;
    PwOutput out(&mixer, QStringLiteral("test"), streamName(), QStringLiteral("DSS Test Tonausgang Strom"));
    QString error;
    QVERIFY2(out.start(&error), qPrintable(error));
    out.setTarget({});
    QCOMPARE(out.state(), PwOutput::State::NoTarget);
    QCOMPARE(out.streamNodeId(), UINT32_MAX);
    QTest::qWait(300);
    QCOMPARE(out.stats().pulls, uint64_t(0));
    QCOMPARE(out.stats().streamsCreated, uint64_t(0));
}

void TestTonausgang::cleanupTestCase()
{
    if (!m_pw)
        return;
    verifyDefaultUnchanged();
    m_pw.reset(); // Verbindung zu: alle Senken und Verbindungen dieses Tests gehen mit
    // Neue Verbindung: keine Knoten dieses Tests übrig, Standardausgang wie vorher
    TestPw check;
    QVERIFY(check.connect());
    QVERIFY(check.sync());
    QVERIFY2(check.nodesWithPrefix(prefix()).isEmpty(), qPrintable(check.nodesWithPrefix(prefix()).join(u' ')));
    QCOMPARE(check.defaultSink(), m_defaultBefore);
    qInfo().noquote() << "Ende: keine Knoten" << prefix() << "übrig, Standardausgang unverändert";
}

QTEST_GUILESS_MAIN(TestTonausgang)
#include "tst_tonausgang.moc"
