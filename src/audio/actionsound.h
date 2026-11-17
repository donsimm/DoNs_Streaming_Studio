#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

class QTimer;

namespace dss {

class AudioOutput;
class Mixer;

// Töne des Chat-Bots (P14 t, Plan P14 Abschnitt 4.3): je Ton ein Decoder (AudioDecoder) und eine
// freie Stimme im Mischer des Zwecks «aktionen» (Mixer::startFree), höchstens kMaxSounds
// gleichzeitig. Nur Qt Core (CLAUDE.md Regel 9). Den Schritt «Ton abspielen» baut Abschnitt c2;
// er ruft play() und wartet bei «wait» auf finished().
//
// - play() prüft zuerst den Ausgang: ohne gewählten Ausgang, mit fehlendem Ausgang, ohne PipeWire
//   oder im Zustand «Fehler» wird nichts gespielt (Meldung). «Verbindung wird aufgebaut» gilt als
//   bereit (der Strom spielt, sobald er verbunden ist).
// - Ende: Der Decoder ist fertig und der Mischer hat alles gespielt, was er geschrieben hat (geprüft
//   alle kCheckIntervalMs), oder der Mischer meldet das Ende des Durchgangs. Datei ohne Ton oder
//   nicht lesbar: Ende mit Fehler. Danach werden Stelle und Decoder freigegeben.
// - stop() blendet sofort aus (5 ms im Mischer) und meldet das Ende mit ok = false.
// - Protokoll: «Ton Chat-Bot: Start …», «… Ende …» mit Name der Datei, nie mehr.
class ActionSound : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMaxSounds = 8; // Plan P14, 4.3
    static constexpr int kMaxVolumePercent = 200;
    static constexpr int kCheckIntervalMs = 20;

    // dataDir: Datenordner (Verweise media/… relativ dazu). mixer und output müssen länger leben.
    ActionSound(const QString &dataDir, Mixer *mixer, AudioOutput *output, QObject *parent = nullptr);
    ~ActionSound() override;

    // Spielt die Datei (Verweis media/…, Medienart «Ton») mit volume 0 bis 200 %. Gibt eine
    // Kennung grösser 0 zurück, bei Abweisung 0 mit Meldung in *error.
    quint64 play(const QString &file, int volumePercent, QString *error);
    // Beendet einen Ton vorzeitig; finished(id, false, «abgebrochen»). Unbekannte Kennung: nichts.
    void stop(quint64 id);
    // Alle Töne beenden (beim Beenden des Kerns, solange Mischer und Ausgang leben).
    void shutdown();
    int playingCount() const { return int(m_sounds.size()); }
    // Meldung, wenn der Ausgang keinen Ton zulässt; sonst leer.
    QString outputProblem() const;

signals:
    void finished(quint64 id, bool ok, const QString &message);

private:
    struct Sound;
    void check();
    void finish(quint64 id, bool ok, const QString &message);

    QString m_dataDir;
    Mixer *m_mixer;
    AudioOutput *m_output;
    QTimer *m_timer;
    QHash<quint64, std::shared_ptr<Sound>> m_sounds;
    quint64 m_nextId = 1;
    bool m_shutdown = false;
};

} // namespace dss
