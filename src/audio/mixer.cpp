// Mischer für einen Ausgang (P12b b2), siehe mixer.h.
//
// Quellen:
//   struct pw_time, Bedeutung von delay, queued, buffered, size und die Formel für die gesamte
//     Verzögerung: /usr/include/pipewire-0.3/pipewire/stream.h (libpipewire 1.4.2, Kommentar über
//     struct pw_time und Felder); Webseite https://docs.pipewire.org/group__pw__stream.html in b2
//     nicht abgerufen (kein Webzugriff), der Header ist die Quelle
//   Lesart «Rest + Quantum» und Klick 197: docs/archiv/messungen/P12a-bericht.md, «Neuauswertung
//     (12.11.)»; Pause beim Wiederholen: ebenda, M2
//   Dreifacher Puffer und Ordnung der Atome: https://en.cppreference.com/w/cpp/atomic/memory_order
//   Interpolation mit 4 Punkten (Catmull-Rom, kubisch nach Hermite): Olli Niemitalo, «Polynomial
//     Interpolators for High-Quality Resampling of Oversampled Audio» (2001), «4-point, 3rd-order
//     Hermite», https://yehar.com/blog/wp-content/uploads/2009/08/deip.pdf (in b2 nicht abgerufen;
//     tst_tonmischer prüft, dass Gleichwert und Gerade genau bleiben)

#include "mixer.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace dss {

namespace {

constexpr double kNsPerFrame = 1e9 / Mixer::kRate;
constexpr float kRampStep = 1.0f / Mixer::kRampFrames;
// Beim Ende des weichen Nachführens wird die Lage auf eine ganze Probe gesetzt, sobald sie höchstens
// so weit davon entfernt ist (Fehler unter 0,05 µs), damit danach wieder genaue Proben spielen.
constexpr double kSnap = 0.0025;

// Neuester Wert von einem Schreiber an einen Leser, ohne Sperren und ohne Warten: Der Schreiber
// tauscht seinen fertigen Puffer mit dem mittleren, der Leser holt den mittleren, wenn er neu ist.
template<class T>
class TripleBuffer
{
    static_assert(std::is_trivially_copyable_v<T>);

public:
    void write(const T &value)
    {
        m_buffers[m_back] = value;
        const uint8_t old = m_middle.exchange(uint8_t(m_back | kFresh), std::memory_order_acq_rel);
        m_back = old & kIndex;
    }
    const T &read()
    {
        if (m_middle.load(std::memory_order_relaxed) & kFresh) {
            const uint8_t old = m_middle.exchange(m_front, std::memory_order_acq_rel);
            m_front = old & kIndex;
        }
        return m_buffers[m_front];
    }

private:
    static constexpr uint8_t kIndex = 3;
    static constexpr uint8_t kFresh = 4;
    T m_buffers[3] = {};
    std::atomic<uint8_t> m_middle{1};
    uint8_t m_back = 0;  // nur Schreiber
    uint8_t m_front = 2; // nur Leser
};

float hermite(float x0, float x1, float x2, float x3, float t)
{
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}

// Einstellungen einer Stelle, von der Steuerung an den Abruf
struct Params
{
    bool enabled = false;
    bool playing = false;
    uint32_t startSerial = 0;
    uint32_t minPass = 0; // ältere Durchgänge verwerfen
    PageClock clock;
    int64_t lengthFrames = 0;
    bool loop = false;
    float gain = 1.0f;
    bool muted = false;
};

// Zustand einer Quelle im Abruf
struct Voice
{
    // Kopf des Rings: Durchgang und Lage des Rahmens an der Lesestelle
    bool headKnown = false;
    uint32_t headPass = 0;
    int64_t headPos = 0;

    bool active = false;
    bool stopping = false;
    bool ended = false;     // ohne loop über die Länge hinaus
    uint32_t startSerial = 0;
    uint32_t needPass = 0;  // kleinster Durchgang, der gelten darf
    bool passKnown = false; // pass gewählt
    uint32_t pass = 0;

    int64_t k = 0;      // Lage (ganze Rahmen im Durchgang; negativ: Pause)
    double frac = 0.0;  // Bruchteil der Lage, 0 ausser beim weichen Nachführen
    double step = 1.0;  // Rahmen der Quelle je Rahmen des Ausgangs
    int soft = 0;       // weiches Nachführen: +1 schneller, −1 langsamer, 0 aus
    bool softEnding = false;
    float window[4][Mixer::kChannels] = {}; // Rahmen k−1, k, k+1, k+2
    float gain = 0.0f;  // eingeblendete Lautstärke

    uint32_t loops = 0;
    int64_t gapNs = Mixer::kLoopGapStartNs;
    bool learnGap = false; // nach eigenem Wiederholen: Pause aus der nächsten Meldung nachführen
    int64_t lastDevNs = 0;   // Abweichung im letzten Abruf
    int64_t devAtWrapNs = 0; // Abweichung vor dem eigenen Wiederholen (läuft weich weiter)
    int64_t limit = 0;       // Länge des Videos in Rahmen (0 = unbekannt): dahinter nichts lesen
};

} // namespace

struct Mixer::Slot
{
    explicit Slot(size_t ringFrames) : ring(ringFrames) {}

    AudioRing ring;
    TripleBuffer<Params> params;
    Params control; // nur Steuerung: zuletzt geschriebene Einstellungen
    Voice voice;    // nur Abruf

    std::atomic<bool> playing{false};
    std::atomic<uint32_t> pass{0};
    std::atomic<uint32_t> loops{0};
    std::atomic<uint64_t> frames{0};
    std::atomic<uint64_t> emptyFrames{0};
    std::atomic<uint32_t> hardCorrections{0};
    std::atomic<uint32_t> softCorrections{0};
    std::atomic<int64_t> lastDeviationNs{0};
    std::atomic<int64_t> loopGapNs{0};

    void publish() { params.write(control); }

    // --- nur Abruf ---
    void syncHead()
    {
        AudioRing::Mark mark;
        while (ring.frontMark(&mark) && mark.frame == ring.readPosition()) {
            voice.headKnown = true;
            voice.headPass = mark.pass;
            voice.headPos = 0;
            ring.popMark();
        }
    }
    // Höchstens n lesbare Rahmen bis zur nächsten Marke
    size_t untilMark(size_t n) const
    {
        AudioRing::Mark mark;
        if (ring.frontMark(&mark))
            n = std::min<size_t>(n, size_t(mark.frame - ring.readPosition()));
        return n;
    }
    void skip(size_t n)
    {
        ring.consume(n);
        voice.headPos += int64_t(n);
    }
    // Rest des laufenden Durchgangs am Kopf verwerfen (bis zur nächsten Marke)
    void skipToMark(size_t readable) { skip(untilMark(readable)); }

    // Ruhende Stelle: Durchgänge vor minPass und Proben ohne Marke verwerfen.
    void discardStale(uint32_t minPass)
    {
        for (;;) {
            syncHead();
            const size_t n = ring.readable();
            if (n == 0 || (voice.headKnown && voice.headPass >= minPass))
                return;
            skipToMark(n);
        }
    }

    // Rahmen an Lage p im spielenden Durchgang nach dst; Stille, wenn er nicht (mehr) da ist.
    void fetch(int64_t p, float *dst)
    {
        dst[0] = dst[1] = 0.0f;
        // Hinter der Länge des Videos nichts, auch nicht für die Interpolation am Ende
        if (p < 0 || voice.ended || (voice.limit > 0 && p >= voice.limit))
            return;
        for (;;) {
            syncHead();
            const size_t n = ring.readable();
            if (n == 0) {
                emptyFrames.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (!voice.headKnown) {
                skipToMark(n);
                continue;
            }
            if (!voice.passKnown) {
                if (voice.headPass < voice.needPass) {
                    skipToMark(n);
                    continue;
                }
                voice.pass = voice.headPass;
                voice.passKnown = true;
                pass.store(voice.pass, std::memory_order_relaxed);
            }
            if (voice.headPass < voice.pass) {
                skipToMark(n);
                continue;
            }
            if (voice.headPass > voice.pass || voice.headPos > p)
                return; // nächster Durchgang (Pause bis zum Wiederholen) bzw. schon vorbei
            if (voice.headPos < p) {
                skip(untilMark(std::min<size_t>(n, size_t(p - voice.headPos))));
                continue;
            }
            const float *f = ring.frame(0);
            dst[0] = f[0];
            dst[1] = f[1];
            skip(1);
            frames.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    void seek(int64_t target)
    {
        Voice &v = voice;
        v.k = target;
        v.frac = 0.0;
        v.step = 1.0;
        v.soft = 0;
        v.softEnding = false;
        for (int j = 0; j < 4; ++j)
            fetch(target - 1 + j, v.window[j]);
    }

    void shift()
    {
        Voice &v = voice;
        for (int j = 0; j < 3; ++j) {
            v.window[j][0] = v.window[j + 1][0];
            v.window[j][1] = v.window[j + 1][1];
        }
        ++v.k;
        fetch(v.k + 2, v.window[3]);
    }

    // Nach der Länge an den Anfang des nächsten Durchgangs, nach der Pause.
    void wrap()
    {
        Voice &v = voice;
        ++v.loops;
        loops.store(v.loops, std::memory_order_relaxed);
        v.needPass = v.pass + 1;
        v.passKnown = false;
        // Pause nur nachführen, wenn nicht gerade weich nachgeführt wird; sonst zählte dessen
        // Fortschritt bis zur nächsten Meldung als Pause.
        v.learnGap = v.soft == 0;
        v.devAtWrapNs = v.lastDevNs;
        // Weiches Nachführen läuft über das Wiederholen weiter (sonst bliebe ein Rest unter
        // kSoftStartNs stehen).
        const double frac = v.frac, step = v.step;
        const int soft = v.soft;
        const bool softEnding = v.softEnding;
        seek(-std::llround(double(v.gapNs) / kNsPerFrame));
        v.frac = frac;
        v.step = step;
        v.soft = soft;
        v.softEnding = softEnding;
    }

    void advance(const Params &p)
    {
        Voice &v = voice;
        v.frac += v.step;
        while (v.frac >= 1.0) {
            v.frac -= 1.0;
            shift();
        }
        if (v.softEnding && (v.frac < kSnap || v.frac > 1.0 - kSnap)) {
            if (v.frac > 0.5)
                shift();
            v.frac = 0.0;
            v.step = 1.0;
            v.soft = 0;
            v.softEnding = false;
        }
        if (p.lengthFrames > 0 && v.k >= p.lengthFrames) {
            if (p.loop)
                wrap();
            else
                v.ended = true;
        }
    }

    void sample(float *l, float *r) const
    {
        const Voice &v = voice;
        if (v.frac == 0.0) {
            *l = v.window[1][0];
            *r = v.window[1][1];
            return;
        }
        const float t = float(v.frac);
        *l = hermite(v.window[0][0], v.window[1][0], v.window[2][0], v.window[3][0], t);
        *r = hermite(v.window[0][1], v.window[1][1], v.window[2][1], v.window[3][1], t);
    }

    // Soll-Lage in Rahmen für den hörbaren Zeitpunkt `audible` (mit Versatz), in den Durchgängen
    // der Quelle gezählt.
    double desiredFrame(const Params &p, int64_t audible) const
    {
        const Voice &v = voice;
        double mediaNs = double(p.clock.mediaNs) + double(audible - p.clock.steadyNs);
        if (p.lengthFrames > 0)
            mediaNs -= (double(v.loops) - double(p.clock.loop)) * (double(p.lengthFrames) * kNsPerFrame + double(v.gapNs));
        return mediaNs / kNsPerFrame;
    }

    void render(int64_t audible, bool outputJump, float *out, uint32_t count)
    {
        const Params &p = params.read();
        Voice &v = voice;
        v.limit = p.lengthFrames;
        bool hard = false;
        if (p.enabled && p.playing && p.startSerial != v.startSerial) {
            v.startSerial = p.startSerial;
            v.active = true;
            v.stopping = false;
            v.ended = false;
            v.needPass = p.minPass;
            v.passKnown = false;
            v.loops = 0;
            v.gapNs = kLoopGapStartNs;
            v.learnGap = false;
            v.lastDevNs = 0;
            v.devAtWrapNs = 0;
            v.gain = 0.0f;
            loops.store(0, std::memory_order_relaxed);
            hard = true;
        }
        if (v.active && (!p.enabled || !p.playing))
            v.stopping = true;
        playing.store(v.active && !v.stopping, std::memory_order_relaxed);
        if (!v.active) {
            discardStale(p.minPass);
            return;
        }

        if (!v.stopping) {
            // Die Seite hat schon wiederholt, die Quelle noch nicht: sofort nachziehen.
            if (p.loop && p.clock.loop > v.loops) {
                v.loops = p.clock.loop;
                loops.store(v.loops, std::memory_order_relaxed);
                v.needPass = v.passKnown ? v.pass + 1 : v.needPass;
                v.passKnown = false;
                v.learnGap = false;
                hard = true;
            }
            const double desired = desiredFrame(p, audible);
            const double dev = double(v.k) + v.frac - desired;
            const double devNs = dev * kNsPerFrame;
            v.lastDevNs = std::llround(devNs);
            lastDeviationNs.store(v.lastDevNs, std::memory_order_relaxed);
            if (v.learnGap && p.clock.loop == v.loops) {
                // Erste Meldung nach dem eigenen Wiederholen: Was sich an der Abweichung über das
                // Wiederholen geändert hat, kommt von der Pause; positiv heisst, das Video stand länger.
                v.gapNs = std::clamp<int64_t>(v.gapNs + v.lastDevNs - v.devAtWrapNs, 0, kLoopGapMaxNs);
                v.learnGap = false;
            }
            loopGapNs.store(v.gapNs, std::memory_order_relaxed);
            if (hard || outputJump || std::fabs(devNs) >= double(kHardNs)) {
                v.ended = false; // vor seek(), sonst holt fetch() nichts
                seek(std::llround(desired));
                v.gain = 0.0f;
                hardCorrections.fetch_add(1, std::memory_order_relaxed);
            } else if (v.soft == 0 && std::fabs(devNs) >= double(kSoftStartNs)) {
                v.soft = dev > 0 ? -1 : 1;
                v.step = 1.0 + v.soft * kSoftRatio;
                v.softEnding = false;
                softCorrections.fetch_add(1, std::memory_order_relaxed);
            } else if (v.soft != 0 && (std::fabs(devNs) < double(kSoftStopNs) || (dev > 0) == (v.soft > 0))) {
                v.softEnding = true; // nahe genug oder schon darüber hinaus
            }
        }

        const float target = (v.stopping || p.muted) ? 0.0f : std::clamp(p.gain, 0.0f, 1.0f);
        for (uint32_t i = 0; i < count; ++i) {
            if (v.gain < target)
                v.gain = std::min(target, v.gain + kRampStep);
            else if (v.gain > target)
                v.gain = std::max(target, v.gain - kRampStep);
            if (v.stopping && v.gain <= 0.0f) {
                v.active = false;
                v.stopping = false;
                playing.store(false, std::memory_order_relaxed);
                discardStale(p.minPass);
                return;
            }
            float l = 0.0f, r = 0.0f;
            if (v.k >= 0 && !v.ended)
                sample(&l, &r);
            out[2 * i] += l * v.gain;
            out[2 * i + 1] += r * v.gain;
            advance(p);
        }
    }
};

Mixer::Mixer(int maxSources, size_t ringFrames)
{
    m_slots.reserve(size_t(maxSources));
    for (int i = 0; i < maxSources; ++i)
        m_slots.push_back(std::make_unique<Slot>(ringFrames));
    m_used.assign(size_t(maxSources), false);
}

Mixer::~Mixer() = default;

int Mixer::addSource()
{
    for (size_t i = 0; i < m_used.size(); ++i) {
        if (m_used[i])
            continue;
        m_used[i] = true;
        Slot &s = *m_slots[i];
        const uint32_t serial = s.control.startSerial; // zählt weiter, damit der Abruf den Start sieht
        s.control = Params();
        s.control.enabled = true;
        s.control.minPass = s.ring.nextPass(); // Reste eines früheren Decoders gelten nicht
        s.control.startSerial = serial;
        s.publish();
        return int(i);
    }
    return -1;
}

void Mixer::removeSource(int id)
{
    if (id < 0 || size_t(id) >= m_used.size() || !m_used[size_t(id)])
        return;
    m_used[size_t(id)] = false;
    Slot &s = *m_slots[size_t(id)];
    s.control.enabled = false;
    s.control.playing = false;
    s.control.minPass = s.ring.nextPass();
    s.publish();
}

AudioRing *Mixer::ring(int id)
{
    if (id < 0 || size_t(id) >= m_slots.size())
        return nullptr;
    return &m_slots[size_t(id)]->ring;
}

void Mixer::start(int id, uint32_t pass, const PageClock &clock)
{
    if (id < 0 || size_t(id) >= m_used.size() || !m_used[size_t(id)])
        return;
    Slot &s = *m_slots[size_t(id)];
    s.control.playing = true;
    ++s.control.startSerial;
    s.control.minPass = pass;
    s.control.clock = clock;
    s.publish();
}

void Mixer::prepare(int id, uint32_t pass)
{
    if (id < 0 || size_t(id) >= m_used.size() || !m_used[size_t(id)])
        return;
    Slot &s = *m_slots[size_t(id)];
    s.control.minPass = pass;
    s.publish();
}

void Mixer::report(int id, const PageClock &clock)
{
    if (id < 0 || size_t(id) >= m_used.size() || !m_used[size_t(id)])
        return;
    Slot &s = *m_slots[size_t(id)];
    s.control.clock = clock;
    s.publish();
}

void Mixer::stop(int id)
{
    if (id < 0 || size_t(id) >= m_used.size() || !m_used[size_t(id)])
        return;
    Slot &s = *m_slots[size_t(id)];
    s.control.playing = false;
    s.publish();
}

void Mixer::setVolume(int id, float gain, bool muted)
{
    if (id < 0 || size_t(id) >= m_used.size() || !m_used[size_t(id)])
        return;
    Slot &s = *m_slots[size_t(id)];
    s.control.gain = gain;
    s.control.muted = muted;
    s.publish();
}

void Mixer::setLength(int id, int64_t frames, bool loop)
{
    if (id < 0 || size_t(id) >= m_used.size() || !m_used[size_t(id)])
        return;
    Slot &s = *m_slots[size_t(id)];
    s.control.lengthFrames = std::max<int64_t>(0, frames);
    s.control.loop = loop;
    s.publish();
}

int64_t Mixer::audibleNs(const OutputTiming &t)
{
    double ns = double(t.callNs - t.sinceNowNs);
    ns += double(t.buffered + t.queued + t.size) * kNsPerFrame;
    if (t.rateDenom > 0)
        ns += double(t.delay) * 1e9 * double(t.rateNum) / double(t.rateDenom);
    return std::llround(ns);
}

void Mixer::render(const OutputTiming &timing, float *out, uint32_t frames)
{
    std::fill(out, out + size_t(frames) * kChannels, 0.0f);
    const int64_t audible = audibleNs(timing);
    bool jump = false;
    if (m_havePrediction && std::llabs(audible - m_predictedNs) > kOutputJumpNs) {
        jump = true;
        m_outputJumps.fetch_add(1, std::memory_order_relaxed);
    }
    m_havePrediction = true;
    m_predictedNs = audible + std::llround(double(frames) * kNsPerFrame);
    const int64_t withDelay = audible + m_extraDelayNs.load(std::memory_order_relaxed);
    for (const std::unique_ptr<Slot> &slot : m_slots)
        slot->render(withDelay, jump, out, frames);
    m_frames.fetch_add(frames, std::memory_order_relaxed);
    m_pulls.fetch_add(1, std::memory_order_relaxed);
    m_audibleNs.store(audible, std::memory_order_relaxed);
}

Mixer::SourceStats Mixer::sourceStats(int id) const
{
    SourceStats r;
    if (id < 0 || size_t(id) >= m_slots.size())
        return r;
    const Slot &s = *m_slots[size_t(id)];
    r.playing = s.playing.load(std::memory_order_relaxed);
    r.pass = s.pass.load(std::memory_order_relaxed);
    r.loops = s.loops.load(std::memory_order_relaxed);
    r.frames = s.frames.load(std::memory_order_relaxed);
    r.emptyFrames = s.emptyFrames.load(std::memory_order_relaxed);
    r.hardCorrections = s.hardCorrections.load(std::memory_order_relaxed);
    r.softCorrections = s.softCorrections.load(std::memory_order_relaxed);
    r.lastDeviationNs = s.lastDeviationNs.load(std::memory_order_relaxed);
    r.loopGapNs = s.loopGapNs.load(std::memory_order_relaxed);
    return r;
}

Mixer::OutputStats Mixer::outputStats() const
{
    OutputStats r;
    r.frames = m_frames.load(std::memory_order_relaxed);
    r.pulls = m_pulls.load(std::memory_order_relaxed);
    r.audibleNs = m_audibleNs.load(std::memory_order_relaxed);
    r.outputJumps = m_outputJumps.load(std::memory_order_relaxed);
    return r;
}

} // namespace dss
