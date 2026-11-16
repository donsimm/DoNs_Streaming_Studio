// P12b b2: Mischer (dss::Mixer) ohne Gerät, mit gedachter Uhr und gedachten Werten des Ausgangs
// (Plan P12, Abschnitt «b2», nachgeführt nach Z3 am 2026-11-12). Ohne Qt Gui.
//
// Gedachter Ausgang (Sim): Zyklen wie PipeWire, je Zyklus ein Abruf mit Abrufzeit (zufälliger
// Abstand 0,05 bis 2 ms nach pw_time.now, sinceNow gleich gross), size, delay, queued, buffered.
// Wahr hörbar ist eine Probe nach der Formel des Mischers (Lesart «Rest + Quantum» aus Z3) plus
// einem festen, nicht gemeldeten Weg (3 ms, «über das Mischpult»), den der Versatz ausgleicht.
// Gedachte Seite (Page): zeigt Medienzeit m zum Zeitpunkt t0 + m (mit Wiederholen, Pause, Gang
// oder Stocken je Fall) und meldet Lage, Start und Wiederholen wie in b4 vorgesehen.
// Prüfgrösse «Fehler»: wahre hörbare Zeit eines Klicks minus Zeitpunkt, zu dem die Seite seine
// Medienzeit zeigt. Klicks: 48 Rahmen mit 0,5 alle 0,5 s.
//
// - audibleFormula: Formel des hörbaren Zeitpunkts mit Zahlen.
// - quantumChanges: Wechsel von size 1024 → 2048 → 256 → 1024 bei delay 0 wie Z3c, mit den in Z3
//   gemessenen abgebrochenen Zyklen (Δticks 680 und 318): jeder Wechsel als Sprung des Ausgangs
//   erkannt, Fehler aller Klicks ≤ 1 ms, Änderung des Medians 5 vor gegen 5 nach ≤ 10 ms (E1).
// - delayNonZero: delay ungleich 0 (auch in 1/44100) mit queued und buffered, und ein Wechsel von
//   delay mitten im Lauf.
// - positionJump: Sprung der Lage um 2048 Proben wie in M1c (Q): der Ausgang nimmt einmal 2048
//   Rahmen mehr und hält sie (queued). Gemeldet: kein Fehler. Nicht gemeldet (wie Q): Ton danach
//   42,67 ms zu spät, der Mischer kann es nicht wissen.
// - mixVolumeMute: zwei Quellen mischen, Lautstärke, Stumm, Einblenden in 240 Rahmen, Stopp.
// - startStopRestart: Start, Stopp (still nach dem Ausblenden), Neubeginn mit neuem Durchgang
//   (prepare): nur der neue Durchgang spielt, wieder von vorne.
// - reuseSlot: Stelle freigeben und neu belegen, alte Proben spielen nicht.
// - loopWithGap: Wiederholen nach der Länge, die die Seite meldet, mit Pause (Startwert 58,6 ms
//   aus M2), Pause aus der Meldung nachgeführt; Ton kürzer und länger als das Video (nichts hinter
//   der Länge hörbar). Weicht die Pause vom Startwert ab, liegt das erste Wiederholen bis 20 ms
//   daneben und wird nachgeführt, ab Durchgang 10 genau.
// - softDrift: Gang der Seite ±100 ppm, nur weich nachgeführt, Fehler ≤ 2,5 ms.
// - pageJumps: Video stockt 10 ms (weich) bzw. 30 ms (Sprung).
// - exactSamples: ohne Nachführen spielen die Proben bitgenau.
// - softKeepsLevel: beim weichen Nachführen bleibt ein Gleichwert gleich (Interpolation).
// - decoderThreads: echter Decoder (tests/daten/ton/blitz-piep.mp4) und Abruf in einem eigenen
//   Thread, Steuerung aus dem Testthread; Pieps an der richtigen Stelle, kein leerer Ring.
// - freeVoice (P14 t): freie Stimme ohne Uhr und ohne Versatz, Einblenden, Proben genau, Ende des
//   Durchgangs (ended, keine Wiederholung), Stopp.
// - freeMixLimit (P14 t): 200 %, Begrenzung je Stimme und der Summe auf ±1,0, 8 Stimmen
//   gleichzeitig, eine 9. Stelle gibt es nicht.
// - freeDecoder (P14 t): echter Decoder mit tests/daten/ton/ton-piep.wav als freie Stimme, Piep
//   bei 0,1 s, gespielte = geschriebene Rahmen.
// In allen Fällen legt render() keinen Speicher an (operator new gezählt).

#include "audiodecoder.h"
#include "mixer.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <new>
#include <random>
#include <thread>
#include <vector>

using namespace dss;

namespace {

// Speicher anlegen im Abruf zählen (nur im Thread, der gerade render() aufruft)
thread_local bool t_countAllocations = false;
std::atomic<uint64_t> g_allocations{0};

} // namespace

void *operator new(std::size_t n)
{
    if (t_countAllocations)
        g_allocations.fetch_add(1);
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n)
{
    return operator new(n);
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete[](void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void *p, std::size_t) noexcept
{
    std::free(p);
}

namespace {

constexpr int64_t kRate = Mixer::kRate;
constexpr double kNsPerFrame = 1e9 / kRate;
constexpr int64_t kMs = 1'000'000;
constexpr int64_t kS = 1'000'000'000;
constexpr int64_t kClickSpacing = kRate / 2; // alle 0,5 s
constexpr int64_t kClickFrames = 48;
constexpr int64_t kHiddenNs = 3 * kMs; // wahrer Weg hinter dem Ausgang, nicht gemeldet

int64_t framesNs(double frames)
{
    return std::llround(frames * kNsPerFrame);
}

using Signal = float (*)(int64_t pos, int channel);

float clicks(int64_t pos, int)
{
    return pos % kClickSpacing < kClickFrames ? 0.5f : 0.0f;
}
float dcA(int64_t, int channel)
{
    return channel == 0 ? 0.25f : -0.25f;
}
float dcB(int64_t, int)
{
    return 0.5f;
}
float dcLow(int64_t, int)
{
    return -0.25f;
}
float dc03(int64_t, int)
{
    return 0.3f;
}
float dc075(int64_t, int)
{
    return 0.75f;
}
float tone(int64_t pos, int channel)
{
    const double v = 0.4 * std::sin(double(pos) * 0.0123) + 0.2 * std::sin(double(pos) * 0.00071);
    return float(channel == 0 ? v : -0.5 * v);
}

// Schreibt wie der Decoder Durchgänge mit Marke in den Ring einer Stelle.
struct FakeSource
{
    FakeSource(AudioRing *r, Signal s, int64_t length = 0, bool l = false)
        : ring(r), signal(s), passLength(length), loop(l), pass(r->allocatePass()), buffer(4096 * 2)
    {}

    AudioRing *ring;
    Signal signal;
    int64_t passLength; // 0 = endlos
    bool loop;
    uint32_t pass;
    int64_t pos = 0;
    bool marked = false;
    bool done = false;
    std::vector<float> buffer;

    uint32_t restart()
    {
        pass = ring->allocatePass();
        pos = 0;
        marked = false;
        done = false;
        return pass;
    }

    void fill()
    {
        while (!done) {
            if (!marked) {
                if (!ring->markWritable())
                    return;
                ring->markPass(pass);
                marked = true;
            }
            if (passLength > 0 && pos >= passLength) {
                if (!loop) {
                    done = true;
                    return;
                }
                restart();
                continue;
            }
            int64_t n = int64_t(std::min<size_t>(ring->writable(), 4096));
            if (passLength > 0)
                n = std::min(n, passLength - pos);
            if (n == 0)
                return;
            for (int64_t i = 0; i < n; ++i) {
                buffer[size_t(2 * i)] = signal(pos + i, 0);
                buffer[size_t(2 * i + 1)] = signal(pos + i, 1);
            }
            ring->write(buffer.data(), size_t(n));
            pos += n;
        }
    }
};

// Gedachter Ausgang
struct Sim
{
    explicit Sim(Mixer &m) : mixer(m) {}

    Mixer &mixer;
    std::vector<FakeSource *> sources;
    // gemeldete Werte
    uint32_t size = 1024;
    int64_t delay = 0;
    uint32_t rateNum = 1;
    uint32_t rateDenom = 48000;
    uint64_t queued = 0;
    uint64_t buffered = 0;
    // wahr, aber nicht gemeldet
    int64_t hiddenNs = kHiddenNs;
    uint64_t unreportedQueued = 0;

    int64_t nowNs = 10 * kS; // pw_time.now des nächsten Zyklus
    std::mt19937 rng{20261112};
    std::vector<float> buffer = std::vector<float>(1 << 15);
    uint64_t frame = 0;
    int64_t quiet = 1 << 30;
    std::vector<int64_t> onsets; // wahre hörbare Zeit jedes Einsatzes (linker Kanal)
    bool keepOutput = false;
    std::vector<float> output;
    uint64_t allocations = 0;
    int64_t firstAudibleNs = -1;
    OutputTiming last;

    // Ein Zyklus: Abruf von `frames` Rahmen (0 = size), die Zeit läuft `advance` Rahmen weiter
    // (0 = size; kleiner bei einem abgebrochenen Zyklus).
    void cycle(uint32_t frames = 0, uint32_t advance = 0)
    {
        for (FakeSource *s : sources)
            s->fill();
        if (frames == 0)
            frames = size;
        if (advance == 0)
            advance = size;
        std::uniform_int_distribution<int64_t> lagDist(50'000, 2'000'000);
        const int64_t lag = lagDist(rng);
        OutputTiming t;
        t.callNs = nowNs + lag;
        t.sinceNowNs = lag;
        t.size = size;
        t.delay = delay;
        t.rateNum = rateNum;
        t.rateDenom = rateDenom;
        t.queued = queued;
        t.buffered = buffered;
        if (buffer.size() < size_t(frames) * 2)
            buffer.resize(size_t(frames) * 2);
        const uint64_t before = g_allocations.load();
        t_countAllocations = true;
        mixer.render(t, buffer.data(), frames);
        t_countAllocations = false;
        allocations += g_allocations.load() - before;
        const int64_t trueA0 = Mixer::audibleNs(t) + hiddenNs + framesNs(double(unreportedQueued));
        if (firstAudibleNs < 0)
            firstAudibleNs = trueA0;
        for (uint32_t i = 0; i < frames; ++i) {
            const float l = buffer[2 * i];
            if (keepOutput) {
                output.push_back(l);
                output.push_back(buffer[2 * i + 1]);
            }
            if (std::fabs(l) > 0.1f) {
                if (quiet >= 1000)
                    onsets.push_back(trueA0 + framesNs(i));
                quiet = 0;
            } else {
                ++quiet;
            }
        }
        frame += frames;
        last = t;
        nowNs += framesNs(advance);
    }
};

// Gedachte Seite
struct Page
{
    int64_t t0 = 0;
    int64_t lengthNs = 0; // 0: ohne Wiederholen
    int64_t gapNs = 0;    // Pause beim Wiederholen
    double ppm = 0.0;     // Gang der Medienzeit gegen die Uhr des Kerns
    int64_t stallAtNs = -1; // Medienzeit, bei der das Video einmal stockt
    int64_t stallNs = 0;

    // Zeitpunkt, an dem die Seite Medienzeit m im Durchgang loop zeigt
    int64_t timeOf(uint32_t loop, int64_t m) const
    {
        int64_t t = t0 + int64_t(loop) * (lengthNs + gapNs) + std::llround(double(m) / (1.0 + ppm * 1e-6));
        if (stallAtNs >= 0 && m >= stallAtNs)
            t += stallNs;
        return t;
    }
    // Meldung zur Uhrzeit t (nicht in der Pause und nicht beim Stocken)
    PageClock clockAt(int64_t t) const
    {
        int64_t e = t - t0;
        uint32_t loop = 0;
        if (lengthNs > 0) {
            loop = uint32_t(e / (lengthNs + gapNs));
            e -= int64_t(loop) * (lengthNs + gapNs);
        }
        if (stallAtNs >= 0 && e >= stallAtNs + stallNs)
            e -= stallNs;
        return PageClock{std::llround(double(e) * (1.0 + ppm * 1e-6)), t, loop};
    }
};

struct Event
{
    int64_t deliverNs = 0;
    PageClock clock;
    bool start = false;
};

// Läuft bis untilNs; liefert fällige Meldungen vor jedem Zyklus. before() darf selbst Zyklen machen.
void run(Sim &sim, int id, uint32_t pass, std::vector<Event> events, int64_t untilNs,
         const std::function<void(Sim &)> &before = {})
{
    std::sort(events.begin(), events.end(), [](const Event &a, const Event &b) { return a.deliverNs < b.deliverNs; });
    size_t next = 0;
    while (sim.nowNs < untilNs) {
        while (next < events.size() && events[next].deliverNs <= sim.nowNs) {
            if (events[next].start)
                sim.mixer.start(id, pass, events[next].clock);
            else
                sim.mixer.report(id, events[next].clock);
            ++next;
        }
        if (before)
            before(sim);
        sim.cycle();
    }
}

// Start bei t0 und Lage jede Sekunde (ohne Wiederholen), je `latency` nach dem gemeldeten Zeitpunkt
std::vector<Event> simpleEvents(const Page &page, int64_t untilNs, int64_t latency = 20 * kMs)
{
    std::vector<Event> events;
    events.push_back(Event{page.t0 + latency, PageClock{0, page.t0, 0}, true});
    for (int64_t t = page.t0 + kS; t < untilNs; t += kS) {
        const PageClock c = page.clockAt(t);
        if (page.stallAtNs >= 0 && c.mediaNs >= page.stallAtNs && t - page.t0 < page.stallAtNs + page.stallNs)
            continue; // stockt gerade, keine Meldung
        events.push_back(Event{t + latency, c, false});
    }
    return events;
}

struct Matched
{
    std::vector<int64_t> expected; // Zeitpunkt der Seite
    std::vector<double> errorMs;   // je erwarteter Klick; NaN = fehlt
    int missing = 0;
    int extra = 0;
    std::vector<int64_t> extraAt; // Zeitpunkte der zusätzlichen Einsätze
    double maxAbsMs() const
    {
        double m = 0;
        for (double e : errorMs)
            if (!std::isnan(e))
                m = std::max(m, std::fabs(e));
        return m;
    }
};

// Ordnet Einsätze den erwarteten Klicks zu (± 100 ms); nur im Fenster [from, to].
Matched match(std::vector<int64_t> expected, const std::vector<int64_t> &onsets, int64_t from, int64_t to)
{
    Matched r;
    std::sort(expected.begin(), expected.end());
    std::vector<bool> used(onsets.size(), false);
    for (int64_t e : expected) {
        if (e < from || e > to)
            continue;
        r.expected.push_back(e);
        auto it = std::lower_bound(onsets.begin(), onsets.end(), e - 100 * kMs);
        double best = std::nan("");
        size_t bestIndex = 0;
        for (; it != onsets.end() && *it <= e + 100 * kMs; ++it) {
            const double err = double(*it - e) / 1e6;
            if (std::isnan(best) || std::fabs(err) < std::fabs(best)) {
                best = err;
                bestIndex = size_t(it - onsets.begin());
            }
        }
        if (std::isnan(best))
            ++r.missing;
        else
            used[bestIndex] = true;
        r.errorMs.push_back(best);
    }
    for (size_t i = 0; i < onsets.size(); ++i)
        if (!used[i] && onsets[i] >= from && onsets[i] <= to) {
            ++r.extra;
            r.extraAt.push_back(onsets[i]);
        }
    return r;
}

// Klicks der Seite ohne Wiederholen bis untilNs
std::vector<int64_t> clickTimes(const Page &page, int64_t untilNs)
{
    std::vector<int64_t> r;
    for (int64_t m = 0; page.timeOf(0, m) < untilNs; m += framesNs(double(kClickSpacing)))
        r.push_back(page.timeOf(0, m));
    return r;
}

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? std::nan("") : v[v.size() / 2];
}

} // namespace

class TstTonMischer : public QObject
{
    Q_OBJECT

private slots:
    void audibleFormula();
    void quantumChanges();
    void delayNonZero_data();
    void delayNonZero();
    void positionJump_data();
    void positionJump();
    void mixVolumeMute();
    void startStopRestart();
    void reuseSlot();
    void loopWithGap_data();
    void loopWithGap();
    void softDrift_data();
    void softDrift();
    void pageJumps_data();
    void pageJumps();
    void exactSamples();
    void softKeepsLevel();
    void decoderThreads();
    void freeVoice();
    void freeMixLimit();
    void freeDecoder();
};

void TstTonMischer::audibleFormula()
{
    OutputTiming t;
    t.callNs = 5 * kS;
    t.sinceNowNs = 1 * kMs;
    t.size = 1024;
    t.queued = 256;
    t.buffered = 32;
    t.delay = 480;
    t.rateNum = 1;
    t.rateDenom = 48000;
    // 5 s − 1 ms + (32 + 256 + 1024) / 48 kHz + 480 / 48 kHz
    QCOMPARE(Mixer::audibleNs(t), 5 * kS - kMs + framesNs(1312) + 10 * kMs);
    t.rateDenom = 0; // ungültig: delay zählt nicht
    QCOMPARE(Mixer::audibleNs(t), 5 * kS - kMs + framesNs(1312));
    t.rateDenom = 44100;
    t.delay = 441; // 10 ms in 1/44100
    QCOMPARE(Mixer::audibleNs(t), 5 * kS - kMs + framesNs(1312) + 10 * kMs);
}

void TstTonMischer::quantumChanges()
{
    Mixer mixer(2);
    const int id = mixer.addSource();
    FakeSource src(mixer.ring(id), clicks);
    Sim sim(mixer);
    sim.sources = {&src};
    mixer.setExtraDelayNs(kHiddenNs);
    Page page;
    page.t0 = sim.nowNs + 50 * kMs;
    const int64_t start = sim.nowNs;
    const int64_t until = start + 180 * kS;

    // Wechsel wie Z3c (bei 60, 100, 140 s), je 0,25 s neben den Klicks; der letzte Zyklus vor dem
    // Wechsel auf 2048 und auf 256 bricht ab wie in Z3 gemessen (Δticks 680 bzw. 318).
    struct Change
    {
        int64_t at;
        uint32_t size;
        uint32_t abortedTicks; // 0: kein Abbruch
    };
    const std::vector<Change> changes = {{start + 60'250 * kMs, 2048, 680},
                                         {start + 100'250 * kMs, 256, 318},
                                         {start + 140'250 * kMs, 1024, 0}};
    size_t nextChange = 0;
    run(sim, id, src.pass, simpleEvents(page, until), until, [&](Sim &s) {
        if (nextChange < changes.size() && s.nowNs >= changes[nextChange].at) {
            const Change &c = changes[nextChange++];
            if (c.abortedTicks)
                s.cycle(s.size, c.abortedTicks);
            s.size = c.size;
        }
    });

    const Mixer::OutputStats out = mixer.outputStats();
    const Mixer::SourceStats st = mixer.sourceStats(id);
    QCOMPARE(out.outputJumps, 3u);
    QCOMPARE(st.hardCorrections, 1u + 3u); // Start und die drei Wechsel
    QCOMPARE(st.softCorrections, 0u);
    QCOMPARE(st.emptyFrames, uint64_t(0));
    QCOMPARE(sim.allocations, uint64_t(0));

    const Matched m = match(clickTimes(page, until), sim.onsets, sim.firstAudibleNs + 500 * kMs, until - 500 * kMs);
    qInfo("Klicks %d, fehlend %d, zusätzlich %d, grösster Fehler %.3f ms", int(m.expected.size()), m.missing,
          m.extra, m.maxAbsMs());
    QCOMPARE(m.missing, 0);
    QCOMPARE(m.extra, 0);
    QVERIFY2(m.maxAbsMs() <= 1.0, qPrintable(QString::number(m.maxAbsMs())));
    // E1: Median von 5 Klicks vor gegen 5 nach jedem Wechsel
    for (const Change &c : changes) {
        std::vector<double> before, after;
        for (size_t i = 0; i < m.expected.size(); ++i) {
            if (m.expected[i] < c.at && m.expected[i] >= c.at - 2'600 * kMs)
                before.push_back(m.errorMs[i]);
            if (m.expected[i] > c.at && m.expected[i] <= c.at + 2'600 * kMs)
                after.push_back(m.errorMs[i]);
        }
        QCOMPARE(int(before.size()), 5);
        QCOMPARE(int(after.size()), 5);
        const double change = median(after) - median(before);
        qInfo("  Wechsel auf %u: Median vorher %.3f ms, nachher %.3f ms, Änderung %.3f ms", c.size,
              median(before), median(after), change);
        QVERIFY2(std::fabs(change) <= 10.0, qPrintable(QString::number(change)));
    }
}

void TstTonMischer::delayNonZero_data()
{
    QTest::addColumn<int>("delay");
    QTest::addColumn<int>("rateDenom");
    QTest::addColumn<int>("queued");
    QTest::addColumn<int>("buffered");
    QTest::addColumn<int>("delayLater"); // -1: bleibt
    QTest::newRow("delay 1536 bei 48 kHz") << 1536 << 48000 << 512 << 64 << -1;
    QTest::newRow("delay 1323 bei 44,1 kHz") << 1323 << 44100 << 0 << 37 << -1;
    QTest::newRow("delay wechselt 1536 auf 4096") << 1536 << 48000 << 0 << 0 << 4096;
}

void TstTonMischer::delayNonZero()
{
    QFETCH(int, delay);
    QFETCH(int, rateDenom);
    QFETCH(int, queued);
    QFETCH(int, buffered);
    QFETCH(int, delayLater);

    Mixer mixer(2);
    const int id = mixer.addSource();
    FakeSource src(mixer.ring(id), clicks);
    Sim sim(mixer);
    sim.sources = {&src};
    sim.delay = delay;
    sim.rateDenom = uint32_t(rateDenom);
    sim.queued = uint64_t(queued);
    sim.buffered = uint64_t(buffered);
    mixer.setExtraDelayNs(kHiddenNs);
    Page page;
    page.t0 = sim.nowNs + 100 * kMs;
    const int64_t until = sim.nowNs + 40 * kS;
    const int64_t changeAt = sim.nowNs + 20'250 * kMs;
    run(sim, id, src.pass, simpleEvents(page, until), until, [&](Sim &s) {
        if (delayLater >= 0 && s.nowNs >= changeAt)
            s.delay = delayLater;
    });

    const Matched m = match(clickTimes(page, until), sim.onsets, sim.firstAudibleNs + 500 * kMs, until - 500 * kMs);
    qInfo("Klicks %d, grösster Fehler %.3f ms", int(m.expected.size()), m.maxAbsMs());
    QCOMPARE(m.missing, 0);
    QCOMPARE(m.extra, 0);
    QVERIFY2(m.maxAbsMs() <= 1.0, qPrintable(QString::number(m.maxAbsMs())));
    QCOMPARE(mixer.outputStats().outputJumps, delayLater >= 0 ? 1u : 0u);
    QCOMPARE(mixer.sourceStats(id).softCorrections, 0u);
    QCOMPARE(mixer.sourceStats(id).emptyFrames, uint64_t(0));
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::positionJump_data()
{
    QTest::addColumn<bool>("reported");
    QTest::newRow("gemeldet (queued)") << true;
    QTest::newRow("nicht gemeldet, wie Q in M1c") << false;
}

void TstTonMischer::positionJump()
{
    QFETCH(bool, reported);

    Mixer mixer(2);
    const int id = mixer.addSource();
    FakeSource src(mixer.ring(id), clicks);
    Sim sim(mixer);
    sim.sources = {&src};
    mixer.setExtraDelayNs(kHiddenNs);
    Page page;
    page.t0 = sim.nowNs + 100 * kMs;
    const int64_t until = sim.nowNs + 40 * kS;
    const int64_t jumpAt = sim.nowNs + 20'250 * kMs;
    bool jumped = false;
    run(sim, id, src.pass, simpleEvents(page, until), until, [&](Sim &s) {
        if (!jumped && s.nowNs >= jumpAt) {
            jumped = true;
            s.cycle(s.size + 2048, s.size); // der Ausgang nimmt 2048 Rahmen mehr und hält sie
            if (reported)
                s.queued = 2048;
            else
                s.unreportedQueued = 2048;
        }
    });

    const Matched m = match(clickTimes(page, until), sim.onsets, sim.firstAudibleNs + 500 * kMs, until - 500 * kMs);
    QCOMPARE(m.missing, 0);
    QCOMPARE(m.extra, 0);
    const double jumpMs = 2048.0 * 1000.0 / kRate; // 42,667 ms
    double worstBefore = 0, worstAfter = 0;
    for (size_t i = 0; i < m.expected.size(); ++i) {
        if (m.expected[i] < jumpAt - 100 * kMs)
            worstBefore = std::max(worstBefore, std::fabs(m.errorMs[i]));
        else if (m.expected[i] > jumpAt + 1'000 * kMs)
            worstAfter = std::max(worstAfter, std::fabs(m.errorMs[i] - (reported ? 0.0 : jumpMs)));
    }
    qInfo("grösster Fehler vorher %.3f ms, nachher (gegen %s) %.3f ms", worstBefore,
          reported ? "0" : "+42,667 ms", worstAfter);
    QVERIFY2(worstBefore <= 1.0, qPrintable(QString::number(worstBefore)));
    QVERIFY2(worstAfter <= 1.0, qPrintable(QString::number(worstAfter)));
    // Gemeldet ist der Zeitpunkt stetig, kein Sprung; nicht gemeldet sieht der Mischer einen
    // Sprung von −42,67 ms und folgt ihm (falsch, aber ohne Meldung nicht erkennbar).
    QCOMPARE(mixer.outputStats().outputJumps, reported ? 0u : 1u);
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::mixVolumeMute()
{
    Mixer mixer(3);
    const int a = mixer.addSource();
    const int b = mixer.addSource();
    QVERIFY(a >= 0 && b >= 0 && a != b);
    FakeSource srcA(mixer.ring(a), dcA);
    FakeSource srcB(mixer.ring(b), dcB);
    Sim sim(mixer);
    sim.sources = {&srcA, &srcB};
    sim.keepOutput = true;
    const PageClock clock{0, sim.nowNs, 0};
    mixer.start(a, srcA.pass, clock);
    mixer.start(b, srcB.pass, clock);
    auto frameAt = [&](uint64_t n, int ch) { return sim.output[size_t(2 * n + uint64_t(ch))]; };

    sim.cycle();
    // Einblenden: steigt über 240 Rahmen, danach genau die Summe
    QVERIFY(std::fabs(frameAt(0, 0) - 0.75f / 240) < 1e-6f);
    for (uint64_t n = 1; n < 240; ++n)
        QVERIFY(frameAt(n, 0) > frameAt(n - 1, 0));
    for (uint64_t n = 241; n < 1024; ++n) {
        QCOMPARE(frameAt(n, 0), 0.75f);
        QCOMPARE(frameAt(n, 1), 0.25f);
    }
    QVERIFY(mixer.sourceStats(a).playing);

    mixer.setVolume(a, 0.5f, false);
    const uint64_t n1 = sim.frame;
    sim.cycle();
    QVERIFY(frameAt(n1 + 100, 0) < 0.75f && frameAt(n1 + 100, 0) > 0.625f); // im Übergang
    QCOMPARE(frameAt(n1 + 300, 0), 0.625f);
    QCOMPARE(frameAt(n1 + 300, 1), 0.375f);

    mixer.setVolume(b, 1.0f, true); // stumm
    const uint64_t n2 = sim.frame;
    sim.cycle();
    QCOMPARE(frameAt(n2 + 300, 0), 0.125f);
    QCOMPARE(frameAt(n2 + 300, 1), -0.125f);
    QVERIFY(mixer.sourceStats(b).playing); // stumm spielt weiter

    mixer.stop(a);
    const uint64_t n3 = sim.frame;
    sim.cycle();
    for (uint64_t n = n3 + 300; n < sim.frame; ++n) {
        QCOMPARE(frameAt(n, 0), 0.0f);
        QCOMPARE(frameAt(n, 1), 0.0f);
    }
    QVERIFY(!mixer.sourceStats(a).playing);

    mixer.setVolume(b, 1.0f, false);
    const uint64_t n4 = sim.frame;
    sim.cycle();
    QCOMPARE(frameAt(n4 + 300, 0), 0.5f);
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::startStopRestart()
{
    Mixer mixer(2);
    const int id = mixer.addSource();
    FakeSource src(mixer.ring(id), clicks);
    Sim sim(mixer);
    sim.sources = {&src};
    mixer.setExtraDelayNs(kHiddenNs);
    Page page;
    page.t0 = sim.nowNs + 100 * kMs;
    const int64_t stopAt = page.t0 + 5'250 * kMs;
    run(sim, id, src.pass, simpleEvents(page, stopAt), stopAt);
    mixer.stop(id);
    sim.cycle();
    QVERIFY(!mixer.sourceStats(id).playing);
    const size_t onsetsAtStop = sim.onsets.size();

    // Neubeginn des Decoders (neuer Durchgang), Vorrat ohne Start
    const uint32_t pass = src.restart();
    mixer.prepare(id, pass);
    for (int i = 0; i < 100; ++i)
        sim.cycle();
    QCOMPARE(sim.onsets.size(), onsetsAtStop); // still
    QVERIFY(mixer.ring(id)->readable() > 0);    // Vorrat des neuen Durchgangs liegt bereit

    Page page2;
    page2.t0 = sim.nowNs + 100 * kMs;
    const int64_t until = page2.t0 + 5 * kS;
    run(sim, id, pass, simpleEvents(page2, until), until);
    QCOMPARE(mixer.sourceStats(id).pass, pass);

    const Matched m1 = match(clickTimes(page, stopAt), sim.onsets, sim.firstAudibleNs + 500 * kMs, stopAt - 100 * kMs);
    const Matched m2 = match(clickTimes(page2, until), sim.onsets, page2.t0 + 100 * kMs, until - 500 * kMs);
    QCOMPARE(m1.missing, 0);
    QCOMPARE(m2.missing, 0);
    QCOMPARE(m2.extra, 0); // keine Klicks aus dem alten Durchgang
    QVERIFY2(m1.maxAbsMs() <= 1.0, qPrintable(QString::number(m1.maxAbsMs())));
    QVERIFY2(m2.maxAbsMs() <= 1.0, qPrintable(QString::number(m2.maxAbsMs())));
    // Zwischen Stopp und neuem Start kein Einsatz
    const Matched between = match({}, sim.onsets, stopAt + 10 * kMs, page2.t0);
    QCOMPARE(between.extra, 0);
    QCOMPARE(mixer.sourceStats(id).emptyFrames, uint64_t(0));
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::reuseSlot()
{
    Mixer mixer(1, 4096);
    int id = mixer.addSource();
    QCOMPARE(id, 0);
    QCOMPARE(mixer.addSource(), -1); // nur eine Stelle
    Sim sim(mixer);
    sim.keepOutput = true;
    {
        FakeSource old(mixer.ring(id), dcB);
        sim.sources = {&old};
        mixer.start(id, old.pass, PageClock{0, sim.nowNs, 0});
        for (int i = 0; i < 5; ++i)
            sim.cycle();
        QCOMPARE(sim.output.back(), 0.5f);
        sim.sources.clear(); // «Decoder» zerstört, Ring bleibt voll mit alten Proben
    }
    mixer.removeSource(id);
    sim.cycle();
    QCOMPARE(sim.output.back(), 0.0f);
    id = mixer.addSource();
    QCOMPARE(id, 0);
    FakeSource fresh(mixer.ring(id), dcLow);
    sim.sources = {&fresh};
    const uint64_t from = sim.frame;
    mixer.start(id, fresh.pass, PageClock{0, sim.nowNs, 0});
    for (int i = 0; i < 10; ++i)
        sim.cycle();
    for (uint64_t n = from; n < sim.frame; ++n)
        QVERIFY2(sim.output[size_t(2 * n)] <= 0.0f, "alte Probe gespielt");
    QCOMPARE(sim.output.back(), -0.25f);
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::loopWithGap_data()
{
    QTest::addColumn<int>("audioMs"); // Länge der Tonspur
    QTest::addColumn<int>("gapUs");   // wahre Pause der Seite
    QTest::newRow("Pause 75 ms") << 1000 << 75000;
    QTest::newRow("Pause 58,6 ms wie M2") << 1000 << 58600;
    QTest::newRow("Pause 0 ms") << 1000 << 0;
    QTest::newRow("Ton kürzer (0,9 s)") << 900 << 75000;
    QTest::newRow("Ton länger (1,2 s)") << 1200 << 75000;
}

void TstTonMischer::loopWithGap()
{
    QFETCH(int, audioMs);
    QFETCH(int, gapUs);

    constexpr int loops = 12;
    constexpr int64_t latency = 30 * kMs; // Meldungen kommen so viel später an
    Mixer mixer(2);
    const int id = mixer.addSource();
    FakeSource src(mixer.ring(id), clicks, int64_t(audioMs) * kRate / 1000, true);
    Sim sim(mixer);
    sim.sources = {&src};
    mixer.setExtraDelayNs(kHiddenNs);
    mixer.setLength(id, kRate, true); // Video 1 s, gemeldet von der Seite
    Page page;
    page.t0 = sim.nowNs + 100 * kMs;
    page.lengthNs = kS;
    page.gapNs = int64_t(gapUs) * 1000;

    std::vector<Event> events;
    std::vector<int64_t> expected;
    for (uint32_t k = 0; k < loops; ++k) {
        const int64_t tk = page.timeOf(k, 0);
        events.push_back(Event{tk + latency, PageClock{0, tk, k}, k == 0});
        events.push_back(Event{page.timeOf(k, 500 * kMs) + latency, PageClock{500 * kMs, page.timeOf(k, 500 * kMs), k}, false});
        for (int64_t m = 0; m < std::min<int64_t>(int64_t(audioMs) * kMs, kS); m += 500 * kMs)
            expected.push_back(page.timeOf(k, m));
    }
    // Ende 0,3 s vor dem Ende des letzten Durchgangs, damit die Quelle nicht schon wiederholt
    const int64_t until = page.timeOf(loops - 1, 700 * kMs);
    run(sim, id, src.pass, events, until);

    const Mixer::SourceStats st = mixer.sourceStats(id);
    qInfo("Wiederholungen %u, Pause nachgeführt %.3f ms, Sprünge %u, weich %u", st.loops, st.loopGapNs / 1e6,
          st.hardCorrections, st.softCorrections);
    QCOMPARE(st.loops, uint32_t(loops - 1));
    QVERIFY2(std::llabs(st.loopGapNs - page.gapNs) <= 500'000, qPrintable(QString::number(st.loopGapNs)));
    QCOMPARE(st.emptyFrames, uint64_t(0));

    // Erster Durchgang genau. Beim ersten Wiederholen liegt der Ton um die Differenz zum Startwert
    // der Pause daneben (hier höchstens 16,4 ms, unter 20 ms weich: 0,2 % holen 2 ms je s auf), ab
    // Durchgang 10 wieder genau. Grenzen der Fenster je bei 0,75 s eines Durchgangs, weit weg von
    // den Klicks bei 0 und 0,5 s.
    const int64_t settled = page.timeOf(9, 750 * kMs);
    const Matched first = match(expected, sim.onsets, sim.firstAudibleNs + 400 * kMs, page.timeOf(0, 750 * kMs));
    const Matched middle = match(expected, sim.onsets, page.timeOf(0, 750 * kMs), settled);
    const Matched late = match(expected, sim.onsets, settled, until);
    qInfo("Fehler: Durchgang 0 %.3f ms, 1 bis 9 %.3f ms, ab 10 %.3f ms (%d Klicks)", first.maxAbsMs(),
          middle.maxAbsMs(), late.maxAbsMs(), int(late.expected.size()));
    for (const Matched *m : {&first, &middle, &late})
        for (int64_t t : m->extraAt)
            qInfo("  zusätzlicher Einsatz bei %.3f ms nach t0", double(t - page.t0) / 1e6);
    QCOMPARE(first.missing + late.missing, 0);
    QCOMPARE(first.extra + middle.extra + late.extra, 0);
    QVERIFY(middle.missing <= 1); // Klick bei 0 im ersten Wiederholen, wenn die Pause zu lang geschätzt war
    QVERIFY2(first.maxAbsMs() <= 1.0, qPrintable(QString::number(first.maxAbsMs())));
    QVERIFY2(middle.maxAbsMs() <= 20.0, qPrintable(QString::number(middle.maxAbsMs())));
    QVERIFY2(late.maxAbsMs() <= 1.0, qPrintable(QString::number(late.maxAbsMs())));
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::softDrift_data()
{
    QTest::addColumn<double>("ppm");
    QTest::newRow("+100 ppm") << 100.0;
    QTest::newRow("-100 ppm") << -100.0;
}

void TstTonMischer::softDrift()
{
    QFETCH(double, ppm);

    Mixer mixer(2);
    const int id = mixer.addSource();
    FakeSource src(mixer.ring(id), clicks);
    Sim sim(mixer);
    sim.sources = {&src};
    mixer.setExtraDelayNs(kHiddenNs);
    Page page;
    page.t0 = sim.nowNs + 100 * kMs;
    page.ppm = ppm;
    const int64_t until = sim.nowNs + 120 * kS;
    run(sim, id, src.pass, simpleEvents(page, until), until);

    const Mixer::SourceStats st = mixer.sourceStats(id);
    const Matched m = match(clickTimes(page, until), sim.onsets, sim.firstAudibleNs + 500 * kMs, until - 500 * kMs);
    qInfo("weich %u, Sprünge %u, grösster Fehler %.3f ms", st.softCorrections, st.hardCorrections, m.maxAbsMs());
    QCOMPARE(m.missing, 0);
    QCOMPARE(m.extra, 0);
    QVERIFY2(m.maxAbsMs() <= 2.5, qPrintable(QString::number(m.maxAbsMs())));
    QCOMPARE(st.hardCorrections, 1u);
    QVERIFY(st.softCorrections >= 1);
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::pageJumps_data()
{
    QTest::addColumn<int>("stallMs");
    QTest::addColumn<uint>("hard");     // Sprünge insgesamt (mit Start)
    QTest::addColumn<int>("settleMs");  // danach höchstens 1 ms Fehler
    QTest::newRow("10 ms, weich") << 10 << 1u << 8000;
    QTest::newRow("30 ms, Sprung") << 30 << 2u << 1100;
}

void TstTonMischer::pageJumps()
{
    QFETCH(int, stallMs);
    QFETCH(uint, hard);
    QFETCH(int, settleMs);

    Mixer mixer(2);
    const int id = mixer.addSource();
    FakeSource src(mixer.ring(id), clicks);
    Sim sim(mixer);
    sim.sources = {&src};
    mixer.setExtraDelayNs(kHiddenNs);
    Page page;
    page.t0 = sim.nowNs + 100 * kMs;
    page.stallAtNs = 10'250 * kMs;
    page.stallNs = int64_t(stallMs) * kMs;
    const int64_t until = sim.nowNs + 30 * kS;
    run(sim, id, src.pass, simpleEvents(page, until), until);

    const Mixer::SourceStats st = mixer.sourceStats(id);
    const int64_t stallTime = page.t0 + page.stallAtNs;
    const Matched before = match(clickTimes(page, until), sim.onsets, sim.firstAudibleNs + 500 * kMs, stallTime);
    const Matched after = match(clickTimes(page, until), sim.onsets, stallTime + int64_t(settleMs) * kMs, until - 500 * kMs);
    const Matched between = match(clickTimes(page, until), sim.onsets, stallTime, stallTime + int64_t(settleMs) * kMs);
    qInfo("Sprünge %u, weich %u; Fehler vorher %.3f, dazwischen %.3f, danach %.3f ms", st.hardCorrections,
          st.softCorrections, before.maxAbsMs(), between.maxAbsMs(), after.maxAbsMs());
    QCOMPARE(st.hardCorrections, hard);
    QVERIFY(before.maxAbsMs() <= 1.0);
    QVERIFY(after.maxAbsMs() <= 1.0);
    QVERIFY(between.maxAbsMs() <= double(stallMs) + 1.0);
    QCOMPARE(before.missing + after.missing + between.missing, 0);
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::exactSamples()
{
    Mixer mixer(2);
    const int id = mixer.addSource();
    FakeSource src(mixer.ring(id), tone);
    Sim sim(mixer);
    sim.sources = {&src};
    sim.keepOutput = true;
    mixer.setExtraDelayNs(kHiddenNs);
    Page page;
    page.t0 = sim.nowNs - 200 * kMs; // Video läuft schon 0,2 s (innerhalb des Vorrats im Ring)
    mixer.start(id, src.pass, PageClock{0, page.t0, 0});
    const int64_t until = sim.nowNs + 5 * kS;
    run(sim, id, src.pass, {}, until, [&](Sim &s) {
        const int64_t t = s.nowNs;
        if ((t / kS) != ((t - framesNs(s.size)) / kS))
            s.mixer.report(id, page.clockAt(t));
    });
    QCOMPARE(mixer.sourceStats(id).hardCorrections, 1u);
    QCOMPARE(mixer.sourceStats(id).softCorrections, 0u);
    QCOMPARE(mixer.sourceStats(id).emptyFrames, uint64_t(0));

    // Lage der ersten Probe suchen (0,2 s plus Verzögerung des Ausgangs, gesucht bis 0,35 s), dann
    // alles bitgenau
    const int64_t n0 = 300; // nach dem Einblenden
    int64_t k0 = -1;
    for (int64_t k = kRate / 5; k < kRate / 5 + 3 * kRate / 20 && k0 < 0; ++k) {
        bool same = true;
        for (int64_t i = 0; i < 64 && same; ++i)
            same = sim.output[size_t(2 * (n0 + i))] == tone(k + n0 + i, 0);
        if (same)
            k0 = k;
    }
    QVERIFY(k0 >= 0);
    int64_t differences = 0;
    for (uint64_t n = uint64_t(n0); n < sim.frame; ++n) {
        if (sim.output[size_t(2 * n)] != tone(k0 + int64_t(n), 0) || sim.output[size_t(2 * n + 1)] != tone(k0 + int64_t(n), 1))
            ++differences;
    }
    QCOMPARE(differences, int64_t(0));
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::softKeepsLevel()
{
    Mixer mixer(2);
    const int id = mixer.addSource();
    FakeSource src(mixer.ring(id), dc03);
    Sim sim(mixer);
    sim.sources = {&src};
    sim.keepOutput = true;
    Page page;
    page.t0 = sim.nowNs;
    page.ppm = 1000.0; // 1 ms je s: weiches Nachführen läuft fast immer
    const int64_t until = sim.nowNs + 20 * kS;
    run(sim, id, src.pass, simpleEvents(page, until, 0), until);
    QVERIFY(mixer.sourceStats(id).softCorrections >= 1);
    QCOMPARE(mixer.sourceStats(id).hardCorrections, 1u);
    float worst = 0.0f;
    for (uint64_t n = 300; n < sim.frame; ++n)
        worst = std::max(worst, std::fabs(sim.output[size_t(2 * n)] - 0.3f));
    QVERIFY2(worst <= 1e-6f, qPrintable(QString::number(worst)));
}

void TstTonMischer::decoderThreads()
{
    Mixer mixer(2);
    const int id = mixer.addSource();
    AudioDecoder decoder(QStringLiteral(DSS_TEST_TON_DIR "/blitz-piep.mp4"), mixer.ring(id), false);
    QTRY_VERIFY_WITH_TIMEOUT(mixer.ring(id)->readable() >= 8192, 5000);

    Sim sim(mixer);
    mixer.setExtraDelayNs(kHiddenNs);
    Page page;
    page.t0 = sim.nowNs + 200 * kMs; // erstes hörbares Stück vor 0: Stille, dann Piep bei 0
    const int64_t until = page.t0 + 6 * kS;
    mixer.start(id, decoder.firstPass(), PageClock{0, page.t0, 0});
    std::atomic<int64_t> simNow{sim.nowNs};
    std::atomic<bool> done{false};
    // Abruf wie der Echtzeit-Thread von PipeWire, viermal schneller als die Zeit (21,3 ms Ton je
    // 5 ms); die Uhr ist gedacht, die Threads sind echt.
    std::thread rt([&] {
        while (sim.nowNs < until) {
            sim.cycle();
            simNow.store(sim.nowNs);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        done = true;
    });
    // Steuerung im Testthread: Lage alle 0,5 s gedachter Zeit
    int64_t nextReport = page.t0 + 500 * kMs;
    while (!done) {
        if (simNow.load() >= nextReport) {
            mixer.report(id, page.clockAt(nextReport));
            nextReport += 500 * kMs;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    rt.join();

    // Pieps bei 0, 2, 4 s
    std::vector<int64_t> expected = {page.timeOf(0, 0), page.timeOf(0, 2 * kS), page.timeOf(0, 4 * kS)};
    const Matched m = match(expected, sim.onsets, page.t0 - 100 * kMs, until - 100 * kMs);
    qInfo("Pieps %d, fehlend %d, zusätzlich %d, grösster Fehler %.3f ms", int(m.expected.size()), m.missing, m.extra,
          m.maxAbsMs());
    QCOMPARE(m.missing, 0);
    QCOMPARE(m.extra, 0);
    QVERIFY2(m.maxAbsMs() <= 1.0, qPrintable(QString::number(m.maxAbsMs())));
    QCOMPARE(mixer.sourceStats(id).emptyFrames, uint64_t(0));
    QCOMPARE(mixer.sourceStats(id).hardCorrections, 1u);
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::freeVoice()
{
    Mixer mixer(2);
    const int id = mixer.addSource();
    // 0,1 s Ton; danach schreibt die Quelle einen neuen Durchgang (Ende des ersten)
    FakeSource src(mixer.ring(id), dcB, 4800, true);
    Sim sim(mixer);
    sim.sources = {&src};
    sim.keepOutput = true;
    mixer.setExtraDelayNs(50 * kMs); // wirkt bei freien Stimmen nicht
    mixer.setVolume(id, 1.0f, false);
    mixer.startFree(id, src.pass);
    auto frameAt = [&](uint64_t n, int ch) { return sim.output[size_t(2 * n + uint64_t(ch))]; };
    for (int i = 0; i < 10; ++i)
        sim.cycle();
    // Einsatz sofort im ersten Abruf, eingeblendet über 240 Rahmen
    QVERIFY(std::fabs(frameAt(0, 0) - 0.5f / 240) < 1e-6f);
    for (uint64_t n = 1; n < 240; ++n)
        QVERIFY(frameAt(n, 0) > frameAt(n - 1, 0));
    for (uint64_t n = 241; n < 4800; ++n) {
        QCOMPARE(frameAt(n, 0), 0.5f);
        QCOMPARE(frameAt(n, 1), 0.5f);
    }
    // nach dem Durchgang still, keine Wiederholung
    for (uint64_t n = 4800; n < sim.frame; ++n)
        QCOMPARE(frameAt(n, 0), 0.0f);
    Mixer::SourceStats stats = mixer.sourceStats(id);
    QVERIFY(stats.ended);
    QCOMPARE(stats.frames, uint64_t(4800));
    QCOMPARE(stats.hardCorrections, 0u);
    QCOMPARE(stats.softCorrections, 0u);

    // Neuer Start auf derselben Stelle: ended zurück, Stopp blendet aus
    const uint32_t pass = src.restart();
    src.passLength = 0;
    mixer.startFree(id, pass);
    sim.cycle();
    sim.cycle();
    stats = mixer.sourceStats(id);
    QVERIFY(!stats.ended);
    QVERIFY(stats.playing);
    QCOMPARE(sim.output[sim.output.size() - 2], 0.5f);
    mixer.stop(id);
    const uint64_t n1 = sim.frame;
    sim.cycle();
    for (uint64_t n = n1 + 300; n < sim.frame; ++n)
        QCOMPARE(frameAt(n, 0), 0.0f);
    QVERIFY(!mixer.sourceStats(id).playing);
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::freeMixLimit()
{
    Mixer mixer(8);
    std::vector<int> ids;
    for (int i = 0; i < 8; ++i)
        ids.push_back(mixer.addSource());
    QVERIFY(std::find(ids.cbegin(), ids.cend(), -1) == ids.cend());
    QCOMPARE(mixer.addSource(), -1); // keine 9. Stelle
    std::vector<std::unique_ptr<FakeSource>> sources;
    const Signal shapes[8] = {dc03, dc075, dcLow, dcB, dcB, dcB, dcB, dcB};
    Sim sim(mixer);
    for (int i = 0; i < 8; ++i) {
        sources.push_back(std::make_unique<FakeSource>(mixer.ring(ids[size_t(i)]), shapes[i]));
        sim.sources.push_back(sources.back().get());
    }
    sim.keepOutput = true;
    auto last = [&](int ch) { return sim.output[sim.output.size() - 2 + size_t(ch)]; };
    auto startVoice = [&](int i, float gain) {
        mixer.setVolume(ids[size_t(i)], gain, false);
        mixer.startFree(ids[size_t(i)], sources[size_t(i)]->pass);
    };

    // 200 %: 0,3 → 0,6
    startVoice(0, 2.0f);
    sim.cycle();
    QCOMPARE(last(0), 0.6f);
    // Begrenzung je Stimme: 0,75 · 200 % → 1,0 (nicht 1,5), dazu −0,25 → 0,75
    mixer.stop(ids[0]);
    startVoice(1, 2.0f);
    startVoice(2, 1.0f);
    sim.cycle();
    sim.cycle();
    QCOMPARE(last(0), 0.75f);
    QCOMPARE(last(1), 0.75f);
    // 8 Stimmen gleichzeitig: Summe begrenzt auf 1,0, alle spielen
    startVoice(0, 2.0f);
    for (int i = 3; i < 8; ++i)
        startVoice(i, 2.0f);
    sim.cycle();
    sim.cycle();
    for (int i = 0; i < 8; ++i)
        QVERIFY(mixer.sourceStats(ids[size_t(i)]).playing);
    float peak = 0.0f;
    for (float v : sim.output)
        peak = std::max(peak, std::fabs(v));
    QCOMPARE(peak, 1.0f);
    QCOMPARE(last(0), 1.0f);
    QCOMPARE(last(1), 1.0f);
    QCOMPARE(sim.allocations, uint64_t(0));
}

void TstTonMischer::freeDecoder()
{
    Mixer mixer(2);
    const int id = mixer.addSource();
    AudioDecoder decoder(QStringLiteral(DSS_TEST_TON_DIR "/ton-piep.wav"), mixer.ring(id), false);
    mixer.setVolume(id, 1.0f, false);
    mixer.startFree(id, decoder.firstPass());
    Sim sim(mixer);
    sim.keepOutput = true;
    QTRY_VERIFY_WITH_TIMEOUT(mixer.ring(id)->readable() >= 8192, 5000);
    // Abruf langsamer als der Decoder schreibt; 1 s Ton und etwas Stille danach
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000
           && !(decoder.state() == AudioDecoder::State::Ended
                && mixer.sourceStats(id).frames >= decoder.statistics().frames)) {
        sim.cycle();
        QTest::qWait(1);
    }
    QCOMPARE(decoder.state(), AudioDecoder::State::Ended);
    QCOMPARE(mixer.sourceStats(id).frames, decoder.statistics().frames);
    QCOMPARE(decoder.statistics().frames, uint64_t(48000));
    // Piep von 0,1 bis 0,3 s (Amplitude 0,5): erster deutlicher Wert bei Rahmen 4800 ±6
    int64_t first = -1;
    int64_t lastLoud = -1;
    for (size_t n = 0; n < sim.output.size() / 2; ++n) {
        if (std::fabs(sim.output[2 * n]) > 0.05f) {
            if (first < 0)
                first = int64_t(n);
            lastLoud = int64_t(n);
        }
    }
    QVERIFY2(std::llabs(first - 4800) <= 6, qPrintable(QString::number(first)));
    QVERIFY2(std::llabs(lastLoud - 14400) <= 6, qPrintable(QString::number(lastLoud)));
    QCOMPARE(sim.allocations, uint64_t(0));
}

QTEST_GUILESS_MAIN(TstTonMischer)
#include "tst_tonmischer.moc"
