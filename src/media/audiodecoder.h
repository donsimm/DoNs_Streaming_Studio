#pragma once

#include "audioring.h"

#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

// Ton einer Videodatei für den Mischer (P12b b2). Dekodiert mit libavcodec in einem eigenen Thread
// und wandelt mit libswresample in float, 48 kHz, Stereo (Mono und 5.1 mit der Standardmatrix von
// libswresample). Ohne Qt Gui (CLAUDE.md Regel 9).
// P14 t: Tondateien ohne Bildspur (mp3, wav, ogg, m4a) gehen ohne Änderung denselben Weg
// (av_find_best_stream sucht nur den Tonstrom); geprüft in tst_tondecoder («audioOnly»).
//
// Der Thread schreibt in den Ring der Stelle des Mischers (audioring.h) und füllt ihn als Vorrat,
// so weit Platz ist; ist der Ring voll, wartet er. Jeder Durchgang beginnt mit einer Marke und
// enthält den Ton ab Medienzeit 0: Die Lage kommt aus dem Zeitstempel des ersten Blocks (P12a,
// tests/daten/ton/LIESMICH.md). Beginnt der Ton später, steht davor Stille; beginnt er vorher
// (Opus in WebM bei −7 ms), werden die Proben vor 0 verworfen.
//
// Ende eines Durchgangs: Ende der Tonspur oder die Länge aus setLoopLength (Länge des Videos, die
// die Seite meldet). Mit loop beginnt dann sofort der nächste Durchgang, sonst wartet der Thread
// auf restart(). Jeder Durchgang öffnet die Datei neu, damit jeder Durchgang genau gleich dekodiert
// (kein Suchen in der Datei).

namespace dss {

class AudioDecoder
{
public:
    static constexpr int kRate = 48000;
    static constexpr int kChannels = AudioRing::kChannels;

    enum class State {
        Opening, // Thread startet
        Running, // Durchgang läuft (oder wartet auf Platz im Ring)
        Ended,   // Durchgang fertig ohne loop, wartet auf restart()
        NoAudio, // Datei ohne Ton: schreibt nichts
        Failed,  // Datei nicht lesbar oder Decoder fehlt: schreibt nichts
    };

    struct Info
    {
        int sourceRate = 0;     // Abtastrate der Datei
        int sourceChannels = 0; // Kanäle der Datei
        QString codec;          // Name des Decoders
    };

    struct Statistics
    {
        uint64_t frames = 0; // geschriebene Rahmen, alle Durchgänge
        uint32_t passes = 0; // begonnene Durchgänge
    };

    // ring gehört der Stelle des Mischers und lebt länger als der Decoder. Auf einem Ring schreibt
    // immer nur ein Decoder; ein neuer erst, wenn der alte zerstört ist.
    AudioDecoder(const QString &path, AudioRing *ring, bool loop);
    ~AudioDecoder(); // hält den Thread an und wartet auf ihn
    AudioDecoder(const AudioDecoder &) = delete;
    AudioDecoder &operator=(const AudioDecoder &) = delete;

    // Nummer des ersten Durchgangs (beim Bau vergeben).
    uint32_t firstPass() const { return m_firstPass; }
    // Neuer Durchgang von vorne; bricht den laufenden ab. Gibt die Nummer des neuen Durchgangs.
    uint32_t restart();
    // Länge eines Durchgangs in Rahmen (Länge des Videos); 0 = ganze Tonspur.
    void setLoopLength(int64_t frames) { m_loopLength.store(frames, std::memory_order_relaxed); }

    State state() const;
    QString error() const;
    Info info() const;
    Statistics statistics() const;

private:
    enum class Wake { Go, Restart, Stop };

    void run();
    // Durchgang `pass` ab der Medienzeit 0; Go = Ende des Durchgangs erreicht.
    Wake decodePass(uint32_t pass, int64_t *written);
    // Wartet auf Platz für eine Marke (needMark) bzw. mindestens einen Rahmen.
    Wake waitForRoom(bool needMark);
    // Schreibt n Rahmen (nullptr = Stille), wartet bei vollem Ring.
    Wake writeFrames(const float *frames, int64_t n);
    void setState(State state, const QString &error = QString());

    const QString m_path;
    AudioRing *const m_ring;
    const bool m_loop;
    const uint32_t m_firstPass;
    std::atomic<int64_t> m_loopLength{0};
    std::atomic<bool> m_stopFlag{false};
    std::atomic<bool> m_restartFlag{false};
    std::atomic<uint64_t> m_frames{0};
    std::atomic<uint32_t> m_passes{0};

    mutable std::mutex m_mutex; // schützt alles bis m_info und die Vergabe der Durchgänge
    std::condition_variable m_wake;
    uint32_t m_requestedPass = 0; // Nummer aus restart()
    State m_state = State::Opening;
    QString m_error;
    Info m_info;

    std::vector<float> m_buffer; // nur Thread des Decoders
    std::thread m_thread;
};

} // namespace dss
