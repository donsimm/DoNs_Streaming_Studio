// Töne des Chat-Bots (P14 t), siehe actionsound.h.
// Ablauf Decoder → Ring → freie Stimme: src/media/audiodecoder.h, src/audio/mixer.h.
#include "actionsound.h"

#include "audiodecoder.h"
#include "audiooutput.h"
#include "mixer.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>

#include <algorithm>

namespace dss {

namespace {

// Spielt der Ausgang so lange nichts (Mischer wird nicht abgerufen), endet der Ton mit Fehler,
// damit ein wartender Lauf nicht hängt.
constexpr qint64 kStallMs = 5000;

} // namespace

struct ActionSound::Sound
{
    QString file;
    int slot = -1;
    std::unique_ptr<AudioDecoder> decoder;
    uint64_t playedAtStart = 0; // Zähler der Stelle beim Start (läuft über Töne hinweg weiter)
    uint64_t lastPlayed = 0;
    QElapsedTimer sinceProgress;
};

ActionSound::ActionSound(const QString &dataDir, Mixer *mixer, AudioOutput *output, QObject *parent)
    : QObject(parent), m_dataDir(dataDir), m_mixer(mixer), m_output(output), m_timer(new QTimer(this))
{
    m_timer->setInterval(kCheckIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &ActionSound::check);
}

ActionSound::~ActionSound()
{
    shutdown();
}

QString ActionSound::outputProblem() const
{
    switch (m_output->state()) {
    case AudioOutput::State::NoTarget:
        return QStringLiteral("Kein Ausgang für «Ton Chat-Bot» gewählt (Dock «Steuerung»).");
    case AudioOutput::State::Missing:
        return QStringLiteral("Der Ausgang für «Ton Chat-Bot» fehlt.");
    case AudioOutput::State::NoPipeWire:
        return QStringLiteral("PipeWire nicht erreichbar, kein Ton.");
    case AudioOutput::State::Error:
        return QStringLiteral("Der Ausgang für «Ton Chat-Bot» hat einen Fehler.");
    case AudioOutput::State::Connecting:
    case AudioOutput::State::Playing:
        break;
    }
    return {};
}

quint64 ActionSound::play(const QString &file, int volumePercent, QString *error)
{
    if (m_shutdown) {
        *error = QStringLiteral("Kern wird beendet.");
        return 0;
    }
    if (const QString problem = outputProblem(); !problem.isEmpty()) {
        *error = problem;
        return 0;
    }
    if (volumePercent < 0 || volumePercent > kMaxVolumePercent) {
        *error = QStringLiteral("Lautstärke %1 % ungültig, erlaubt sind 0 bis %2 %.").arg(volumePercent).arg(kMaxVolumePercent);
        return 0;
    }
    const QString path = QDir(m_dataDir).filePath(file);
    if (!QFileInfo(path).isFile()) {
        *error = QStringLiteral("Datei %1 fehlt im Datenordner.").arg(file);
        return 0;
    }
    const int slot = m_mixer->addSource();
    if (slot < 0) {
        *error = QStringLiteral("Schon %1 Töne gleichzeitig, mehr gehen nicht.").arg(kMaxSounds);
        return 0;
    }
    auto s = std::make_shared<Sound>();
    s->file = file;
    s->slot = slot;
    s->decoder = std::make_unique<AudioDecoder>(path, m_mixer->ring(slot), false);
    s->playedAtStart = m_mixer->sourceStats(slot).frames;
    s->lastPlayed = s->playedAtStart;
    s->sinceProgress.start();
    m_mixer->setVolume(slot, float(volumePercent) / 100.0f, false);
    // Erst den Durchgang wählen, dann starten: Der Abruf liest ab der Marke des Decoders.
    m_mixer->startFree(slot, s->decoder->firstPass());
    const quint64 id = m_nextId++;
    m_sounds.insert(id, s);
    if (!m_timer->isActive())
        m_timer->start();
    qInfo("Ton Chat-Bot: Start %s (%d %%, %d gleichzeitig)", qPrintable(file), volumePercent, int(m_sounds.size()));
    return id;
}

void ActionSound::stop(quint64 id)
{
    if (m_sounds.contains(id))
        finish(id, false, QStringLiteral("abgebrochen"));
}

void ActionSound::shutdown()
{
    if (m_shutdown)
        return;
    m_shutdown = true;
    m_timer->stop();
    const QList<quint64> ids = m_sounds.keys();
    for (quint64 id : ids)
        finish(id, false, QStringLiteral("Kern beendet"));
}

void ActionSound::finish(quint64 id, bool ok, const QString &message)
{
    const std::shared_ptr<Sound> s = m_sounds.take(id);
    if (!s)
        return;
    // Erst die Stelle freigeben (blendet aus), dann den Decoder beenden; beides vor einer neuen
    // Belegung (mixer.h).
    m_mixer->removeSource(s->slot);
    s->decoder.reset();
    if (m_sounds.isEmpty())
        m_timer->stop();
    qInfo("Ton Chat-Bot: Ende %s: %s", qPrintable(s->file), ok ? "gespielt" : qPrintable(message));
    emit finished(id, ok, message);
}

void ActionSound::check()
{
    const QList<quint64> ids = m_sounds.keys();
    for (quint64 id : ids) {
        const std::shared_ptr<Sound> s = m_sounds.value(id);
        if (!s)
            continue;
        const AudioDecoder::State state = s->decoder->state();
        if (state == AudioDecoder::State::NoAudio || state == AudioDecoder::State::Failed) {
            finish(id, false, QStringLiteral("%1: %2").arg(s->file, s->decoder->error()));
            continue;
        }
        const Mixer::SourceStats stats = m_mixer->sourceStats(s->slot);
        // Zuerst den Decoder fragen, dann seine Zähler: Ended kommt nach dem letzten Schreiben.
        const bool decoded = state == AudioDecoder::State::Ended;
        const uint64_t written = s->decoder->statistics().frames;
        const uint64_t played = stats.frames - s->playedAtStart;
        if (stats.ended || (decoded && played >= written)) {
            finish(id, true, QString());
            continue;
        }
        if (stats.frames != s->lastPlayed) {
            s->lastPlayed = stats.frames;
            s->sinceProgress.restart();
        } else if (s->sinceProgress.elapsed() > kStallMs) {
            const QString problem = outputProblem();
            finish(id, false, problem.isEmpty() ? QStringLiteral("Der Ausgang spielt nicht.") : problem);
        }
    }
}

} // namespace dss
