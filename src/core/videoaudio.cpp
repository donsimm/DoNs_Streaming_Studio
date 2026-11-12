// Ton der Videos im Kern (P12b b4, Commit 3), siehe videoaudio.h.
// Ablauf stop → restart → prepare → start: src/audio/mixer.h und src/media/audiodecoder.h (b2).
#include "videoaudio.h"

#include "audiodecoder.h"
#include "mixer.h"
#include "overlaystore.h"

#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace dss {

namespace {

qint64 steadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

const Source *findSource(const Overlay &o, const QUuid &id)
{
    const auto it = std::find_if(o.sources.cbegin(), o.sources.cend(), [&id](const Source &s) { return s.id == id; });
    return it == o.sources.cend() ? nullptr : &*it;
}

QString millis(qint64 ns)
{
    return QString::number(double(ns) / 1e6, 'f', 2);
}

} // namespace

struct VideoAudio::Voice
{
    QUuid overlayId;
    QUuid sourceId;
    QString file;
    bool loop = false;
    QString name;
    QString overlayName;
    int slot = -1;
    std::unique_ptr<AudioDecoder> decoder;
    uint32_t pass = 0;   // Durchgang mit Vorrat für den nächsten Start
    bool playing = false;
    QString owner;       // Seite, die spielt
    int run = -1;
    PageClockSmoother smoother;
    qint64 lengthNs = 0;
    qint64 lastReportNs = 0;
    qint64 stoppedNs = 0;
    qint64 lastStatusNs = 0;
    qint64 lastCorrectionLogNs = 0;
    Mixer::SourceStats atStart; // Zähler beim Start (Zähler des Mischers laufen weiter)
    uint32_t loggedHard = 0;
    uint32_t loggedSoft = 0;

    // Seit dem Start, ohne den Sprung des Starts selbst
    uint32_t hardSinceStart(const Mixer::SourceStats &s) const
    {
        const uint32_t n = s.hardCorrections - atStart.hardCorrections;
        return n > 0 ? n - 1 : 0;
    }
    uint32_t softSinceStart(const Mixer::SourceStats &s) const { return s.softCorrections - atStart.softCorrections; }
};

VideoAudio::VideoAudio(const OverlayStore *store, Mixer *mixer, QObject *parent)
    : QObject(parent), m_store(store), m_mixer(mixer), m_timer(new QTimer(this))
{
    m_timer->setInterval(kCheckIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &VideoAudio::check);
    m_timer->start();
}

VideoAudio::~VideoAudio()
{
    shutdown();
}

void VideoAudio::shutdown()
{
    if (m_shutdown)
        return;
    m_shutdown = true;
    m_timer->stop();
    const QStringList keys = m_voices.keys();
    for (const QString &k : keys)
        removeVoice(k, QStringLiteral("Kern beendet"));
}

QString VideoAudio::key(const QUuid &overlayId, const QString &second)
{
    return overlayId.toString(QUuid::WithoutBraces) + QLatin1Char('/') + second;
}

QString VideoAudio::label(const Voice &v) const
{
    return QStringLiteral("«%1» in «%2»").arg(v.name, v.overlayName);
}

VideoAudio::Page *VideoAudio::page(const QUuid &overlayId, const QString &id, qint64 nowNs)
{
    const QString k = key(overlayId, id);
    auto it = m_pages.find(k);
    if (it != m_pages.end())
        return &*it;
    if (m_pages.size() >= kMaxPages) {
        for (auto p = m_pages.begin(); p != m_pages.end();) {
            if (!p->connected && nowNs - p->lastNs > kPageIdleNs)
                p = m_pages.erase(p);
            else
                ++p;
        }
        if (m_pages.size() >= kMaxPages)
            return nullptr;
    }
    Page p;
    p.overlayId = overlayId;
    p.id = id;
    p.lastNs = nowNs;
    return &*m_pages.insert(k, p);
}

bool VideoAudio::overlayConnected(const QUuid &overlayId) const
{
    return std::any_of(m_pages.cbegin(), m_pages.cend(),
                       [&overlayId](const Page &p) { return p.connected && p.overlayId == overlayId; });
}

VideoAudio::Voice *VideoAudio::ensureVoice(const Overlay &overlay, const Source &source)
{
    const QString k = key(overlay.id, source.id.toString(QUuid::WithoutBraces));
    const auto it = m_voices.constFind(k);
    if (it != m_voices.cend())
        return it->get();
    const int slot = m_mixer->addSource();
    if (slot < 0) {
        if (!m_noRoomLogged)
            qWarning("Ton: kein Platz im Mischer für «%s» in «%s», das Video bleibt ohne Ton",
                     qPrintable(source.name), qPrintable(overlay.name));
        m_noRoomLogged = true;
        return nullptr;
    }
    m_noRoomLogged = false;
    auto v = std::make_shared<Voice>();
    v->overlayId = overlay.id;
    v->sourceId = source.id;
    v->file = source.file;
    v->loop = source.loop;
    v->name = source.name;
    v->overlayName = overlay.name;
    v->slot = slot;
    v->decoder = std::make_unique<AudioDecoder>(m_store->dataDir() + QLatin1Char('/') + source.file,
                                                m_mixer->ring(slot), source.loop);
    v->pass = v->decoder->firstPass();
    m_mixer->prepare(slot, v->pass);
    m_mixer->setLength(slot, 0, source.loop);
    v->stoppedNs = steadyNs();
    m_voices.insert(k, v);
    return v.get();
}

void VideoAudio::setLength(Voice &v, qint64 durationNs)
{
    if (durationNs <= 0 || durationNs == v.lengthNs)
        return;
    v.lengthNs = durationNs;
    const int64_t frames = std::llround(double(durationNs) * Mixer::kRate / 1e9);
    m_mixer->setLength(v.slot, frames, v.loop);
    v.decoder->setLoopLength(frames);
}

void VideoAudio::startVoice(Voice &v, const Page &p, const ClockReport &report, const PageClock &raw)
{
    if (v.playing) {
        // Neuer Start, während der Ton spielt: von vorne mit neuem Durchgang
        m_mixer->stop(v.slot);
        v.pass = v.decoder->restart();
        m_mixer->prepare(v.slot, v.pass);
    }
    v.atStart = m_mixer->sourceStats(v.slot);
    v.loggedHard = 0;
    v.loggedSoft = 0;
    v.smoother.reset();
    m_mixer->start(v.slot, v.pass, v.smoother.apply(raw));
    v.playing = true;
    v.owner = p.id;
    v.run = report.run;
    v.lastReportNs = report.arrivalNs;
    v.lastStatusNs = report.arrivalNs;
    v.lastCorrectionLogNs = report.arrivalNs;
    if (report.event == ClockReport::Event::Start)
        qInfo("Ton: Start %s (Seite %s, Lauf %d)", qPrintable(label(v)), qPrintable(p.id), report.run);
    else
        qInfo("Ton: Einstieg %s bei %s s (Seite %s, Lauf %d, Durchgang %d)", qPrintable(label(v)),
              qPrintable(QString::number(double(raw.mediaNs) / 1e9, 'f', 3)), qPrintable(p.id), report.run,
              report.loop);
}

void VideoAudio::stopVoice(Voice &v, const QString &reason)
{
    if (!v.playing)
        return;
    m_mixer->stop(v.slot);
    const Mixer::SourceStats s = m_mixer->sourceStats(v.slot);
    qInfo("Ton: Stopp %s: %s (gespielt %s s, Wiederholungen %u, Sprünge %u, weich %u, Abweichung zuletzt %s ms)",
          qPrintable(label(v)), qPrintable(reason),
          qPrintable(QString::number(double(s.frames - v.atStart.frames) / Mixer::kRate, 'f', 2)), s.loops,
          v.hardSinceStart(s), v.softSinceStart(s), qPrintable(millis(s.lastDeviationNs)));
    // Vorrat für den nächsten Start (Plan P12, «Ergebnis b2»)
    v.pass = v.decoder->restart();
    m_mixer->prepare(v.slot, v.pass);
    v.playing = false;
    v.owner.clear();
    v.stoppedNs = steadyNs();
}

void VideoAudio::removeVoice(const QString &k, const QString &reason)
{
    const auto it = m_voices.find(k);
    if (it == m_voices.end())
        return;
    const std::shared_ptr<Voice> v = *it;
    m_voices.erase(it);
    stopVoice(*v, reason);
    // Erst die Stelle freigeben, dann den Decoder beenden; beides vor einer neuen Belegung (mixer.h)
    m_mixer->removeSource(v->slot);
    v->decoder.reset();
}

void VideoAudio::prepareVoices(const QUuid &overlayId)
{
    const Overlay *o = m_store->overlay(overlayId);
    if (!o || !o->output)
        return;
    for (const Source &s : o->sources) {
        if (s.kind == SourceKind::Video && s.visible)
            ensureVoice(*o, s);
    }
}

void VideoAudio::clockReported(const QUuid &overlayId, const ClockReport &report)
{
    using Event = ClockReport::Event;
    if (m_shutdown)
        return;
    Page *p = page(overlayId, report.page, report.arrivalNs);
    if (!p || report.seq <= p->lastSeq)
        return; // zu viele Seiten bzw. ältere oder doppelte Meldung
    p->lastSeq = report.seq;
    p->lastNs = report.arrivalNs;
    p->mapper.add(report.arrivalNs, report.nowMs);
    if (report.run < p->run)
        return;
    if (report.run > p->run) {
        p->run = report.run;
        p->stopped = false;
        p->ended.clear();
    }
    if (p->closed)
        return;

    if (report.event == Event::Stop || report.event == Event::End) {
        const QString reason = report.event == Event::End ? QStringLiteral("Ende des Videos")
                                                          : QStringLiteral("Seite inaktiv");
        if (report.source.isNull())
            p->stopped = true;
        else
            p->ended.insert(report.source);
        for (const std::shared_ptr<Voice> &v : std::as_const(m_voices)) {
            if (v->playing && v->owner == p->id && v->overlayId == overlayId
                && (report.source.isNull() || v->sourceId == report.source))
                stopVoice(*v, reason);
        }
        return;
    }
    if (report.event == Event::Start) {
        p->stopped = false;
        p->ended.remove(report.source);
    } else if (p->stopped || p->ended.contains(report.source)) {
        return;
    }

    // Nur sichtbare Videos mit eingeschalteter Ausgabe, und nur die Datei, die die Quelle jetzt hat
    const Overlay *o = m_store->overlay(overlayId);
    const Source *s = o ? findSource(*o, report.source) : nullptr;
    if (!s || s->kind != SourceKind::Video || !o->output || !s->visible || s->file != report.file)
        return;
    Voice *v = ensureVoice(*o, *s);
    if (!v || (v->playing && v->owner != p->id))
        return; // kein Platz bzw. eine andere Seite führt
    const PageClock raw{report.mediaNs, p->mapper.toSteadyNs(report.displayMs), uint32_t(report.loop)};
    setLength(*v, report.durationNs);
    if (report.event == Event::Start || !v->playing || v->run != report.run) {
        startVoice(*v, *p, report, raw);
        return;
    }
    m_mixer->report(v->slot, v->smoother.apply(raw));
    v->lastReportNs = report.arrivalNs;
}

void VideoAudio::pageOpened(const QUuid &overlayId, const QString &id)
{
    if (m_shutdown)
        return;
    Page *p = page(overlayId, id, steadyNs());
    if (!p)
        return;
    p->connected = true;
    p->closed = false;
    prepareVoices(overlayId);
}

void VideoAudio::pageClosed(const QUuid &overlayId, const QString &id)
{
    if (m_shutdown)
        return;
    const auto it = m_pages.find(key(overlayId, id));
    if (it == m_pages.end())
        return;
    it->connected = false;
    it->closed = true;
    for (const std::shared_ptr<Voice> &v : std::as_const(m_voices)) {
        if (v->playing && v->owner == id && v->overlayId == overlayId)
            stopVoice(*v, QStringLiteral("Seite getrennt"));
    }
}

void VideoAudio::overlaysChanged()
{
    if (m_shutdown)
        return;
    const QStringList keys = m_voices.keys();
    for (const QString &k : keys) {
        Voice &v = *m_voices.value(k);
        const Overlay *o = m_store->overlay(v.overlayId);
        const Source *s = o ? findSource(*o, v.sourceId) : nullptr;
        if (!o) {
            removeVoice(k, QStringLiteral("Overlay gelöscht"));
            continue;
        }
        if (!s || s->kind != SourceKind::Video) {
            removeVoice(k, QStringLiteral("Quelle gelöscht"));
            continue;
        }
        if (s->file != v.file || s->loop != v.loop) {
            removeVoice(k, QStringLiteral("Quelle geändert"));
            continue;
        }
        v.name = s->name;
        v.overlayName = o->name;
        if (!o->output)
            stopVoice(v, QStringLiteral("Ausgabe aus"));
        else if (!s->visible)
            stopVoice(v, QStringLiteral("Quelle verborgen"));
    }
    QSet<QUuid> connected;
    for (const Page &p : std::as_const(m_pages)) {
        if (p.connected)
            connected.insert(p.overlayId);
    }
    for (const QUuid &id : std::as_const(connected))
        prepareVoices(id);
}

void VideoAudio::check()
{
    const qint64 now = steadyNs();
    const QStringList keys = m_voices.keys();
    for (const QString &k : keys) {
        Voice &v = *m_voices.value(k);
        if (!v.playing) {
            // Stimme ohne verbundene Seite: Decoder und Stelle freigeben
            if (!overlayConnected(v.overlayId) && now - v.stoppedNs > kReleaseNs)
                removeVoice(k, QString());
            continue;
        }
        if (now - v.lastReportNs > kTimeoutNs) {
            stopVoice(v, QStringLiteral("3 s ohne Meldung"));
            continue;
        }
        const Mixer::SourceStats s = m_mixer->sourceStats(v.slot);
        const uint32_t hard = v.hardSinceStart(s);
        const uint32_t soft = v.softSinceStart(s);
        if ((hard != v.loggedHard || soft != v.loggedSoft) && now - v.lastCorrectionLogNs >= kCorrectionLogNs) {
            qInfo("Ton: Nachführen %s: Sprünge %u, weich %u, Abweichung %s ms", qPrintable(label(v)), hard, soft,
                  qPrintable(millis(s.lastDeviationNs)));
            v.loggedHard = hard;
            v.loggedSoft = soft;
            v.lastCorrectionLogNs = now;
        }
        if (now - v.lastStatusNs >= kStatusIntervalNs) {
            qInfo("Ton: Lage %s: Abweichung %s ms, Wiederholungen %u, Sprünge %u, weich %u, Pause beim Wiederholen %s ms",
                  qPrintable(label(v)), qPrintable(millis(s.lastDeviationNs)), s.loops, hard, soft,
                  qPrintable(millis(s.loopGapNs)));
            v.lastStatusNs = now;
        }
    }
    for (auto it = m_pages.begin(); it != m_pages.end();) {
        if (!it->connected && now - it->lastNs > kPageIdleNs)
            it = m_pages.erase(it);
        else
            ++it;
    }
}

} // namespace dss
