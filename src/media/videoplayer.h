#pragma once

#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Abspielen eines Videos für den Zeichner im Editor (P09, Teil B). Dekodiert in einem eigenen
// Thread, nie im Thread der Oberfläche, wie M1 aus P02: libvpx-vp9 für VP9 mit Alpha, sonst der
// Standard-Decoder (avsupport.h). Ohne Ton (P12). Nur Qt Core (CLAUDE.md Regel 9): Das Bild ist
// ein Puffer BGRA mit vormultipliziertem Alpha; das Fenster legt ein QImage
// Format_ARGB32_Premultiplied darüber, ohne zu kopieren.
//
// Ende (Entscheid 9): loop → wieder von vorne; sonst holdAtEnd → das letzte Bild bleibt;
// sonst wird die Quelle leer (currentFrame() gibt nullptr). Mit loop hat holdAtEnd keine Wirkung.

namespace dss {

struct VideoFrame
{
    int width = 0;
    int height = 0;
    int stride = 0;            // Bytes pro Zeile
    std::vector<uint8_t> data; // BGRA, vormultipliziert (Byte-Folge wie QImage ARGB32 auf x86)
    double pts = 0.0;          // Sekunden ab Beginn der Datei
};

class VideoPlayer
{
public:
    enum class State { Starting, Playing, Holding, Ended, Failed };

    struct Statistics
    {
        uint64_t decoded = 0;  // dekodierte Bilder
        uint64_t late = 0;     // Bilder, die mehr als 50 ms nach ihrer Zeit fertig waren
        uint64_t loops = 0;    // Anfänge nach dem Ende (loop)
        double firstAlphaMean = -1.0; // Mittel des Alphakanals im ersten Bild vor dem
                                      // Vormultiplizieren, wie P02; -1 ohne Alpha
    };

    VideoPlayer(const QString &path, bool loop, bool holdAtEnd);
    ~VideoPlayer(); // hält den Thread an und wartet auf ihn
    VideoPlayer(const VideoPlayer &) = delete;
    VideoPlayer &operator=(const VideoPlayer &) = delete;

    // Aktuelles Bild, nullptr vor dem ersten Bild, nach dem Ende ohne holdAtEnd und bei Fehler.
    std::shared_ptr<const VideoFrame> currentFrame() const;
    // Zählt bei jedem neuen Bild (und beim Leerwerden) hoch.
    uint64_t frameSerial() const { return m_serial.load(); }
    State state() const { return m_state.load(); }
    QString error() const;
    QString decoderName() const;
    Statistics statistics() const;

private:
    void run();
    void publish(std::shared_ptr<const VideoFrame> frame);
    // Wartet bis zum Zeitpunkt oder bis zum Anhalten; false = anhalten.
    template <typename TimePoint>
    bool waitUntil(const TimePoint &due);

    const QString m_path;
    const bool m_loop;
    const bool m_holdAtEnd;

    mutable std::mutex m_mutex; // schützt m_frame, m_error, m_decoderName, m_stats
    std::condition_variable m_wake;
    bool m_stop = false; // unter m_mutex
    std::shared_ptr<const VideoFrame> m_frame;
    QString m_error;
    QString m_decoderName;
    Statistics m_stats;
    std::atomic<uint64_t> m_serial{0};
    std::atomic<State> m_state{State::Starting};
    std::thread m_thread;
};

} // namespace dss
