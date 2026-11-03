#pragma once

#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Abspielen eines Videos für den Zeichner im Editor (P09, Teil B). Dekodiert in einem eigenen
// Thread, nie im Thread der Oberfläche, wie M1 aus P02: libvpx-vp9 für VP9 mit Alpha, sonst der
// Standard-Decoder (avsupport.h). Ohne Ton (P12). Nur Qt Core (CLAUDE.md Regel 9): Das Bild ist
// ein Puffer BGRA mit vormultipliziertem Alpha; das Fenster legt ein QImage
// Format_ARGB32_Premultiplied darüber, ohne zu kopieren.
// Der Thread dekodiert einige Bilder voraus (kQueueSize), jedes mit seinem Zeitpunkt (Beginn +
// Zeitstempel). Gezeigt wird, was frameAt(Zeitpunkt) liefert: das letzte Bild, dessen Zeitpunkt
// erreicht ist. Fragt die Leinwand in festem Takt mit dem geplanten Zeitpunkt jedes Takts, kommt
// bei gleicher Bildrate in jedem Takt genau ein neues Bild, auch wenn der Takt etwas schwankt.
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
        uint64_t dropped = 0;  // nie abgeholt, weil mehr als 100 ms überfällig (niemand fragt)
        double firstAlphaMean = -1.0; // Mittel des Alphakanals im ersten Bild vor dem
                                      // Vormultiplizieren, wie P02; -1 ohne Alpha
    };

    using Clock = std::chrono::steady_clock;
    static constexpr size_t kQueueSize = 4; // vorausdekodierte Bilder
    static constexpr std::chrono::milliseconds kDropAfter{100};
    // Nach dem Abholen füllt der Decoder erst so viel später nach, damit Dekodieren und Zeichnen
    // nicht gleichzeitig laufen. Gleichzeitig kostete es auf dem Studio-PC rund 30 % eines Kerns
    // mehr Rechenzeit bei gleicher Arbeit (Messung im Bericht P09).
    static constexpr std::chrono::milliseconds kRefillDelay{15};

    VideoPlayer(const QString &path, bool loop, bool holdAtEnd);
    ~VideoPlayer(); // hält den Thread an und wartet auf ihn
    VideoPlayer(const VideoPlayer &) = delete;
    VideoPlayer &operator=(const VideoPlayer &) = delete;

    // Bild zum Zeitpunkt t: das letzte, dessen Zeitpunkt erreicht ist; ältere werden verworfen.
    // nullptr vor dem ersten Bild, nach dem Ende ohne holdAtEnd und bei Fehler. Ein früherer
    // Zeitpunkt als beim letzten Aufruf geht nicht zurück.
    std::shared_ptr<const VideoFrame> frameAt(Clock::time_point t);
    std::shared_ptr<const VideoFrame> currentFrame() { return frameAt(Clock::now()); }
    // Zählt bei jedem neu gezeigten Bild (und beim Leerwerden) hoch.
    uint64_t frameSerial() const { return m_serial.load(); }
    // Zustand dessen, was gezeigt wird (Ende erst, wenn frameAt es erreicht hat).
    State state() const;
    QString error() const;
    QString decoderName() const;
    Statistics statistics() const;

private:
    struct Queued
    {
        std::shared_ptr<const VideoFrame> frame; // nullptr = ab hier leer (Ende ohne holdAtEnd)
        Clock::time_point due;
    };

    void run();
    // Legt ein Bild in die Warteschlange; wartet, solange sie voll ist oder (mit Vorrat) bis
    // kRefillDelay nach dem letzten Abholen. Ist das vorderste Bild
    // mehr als kDropAfter überfällig, wird es verworfen, damit das Video in Echtzeit weiterläuft,
    // auch wenn niemand abholt (Leinwand verdeckt). false = anhalten.
    bool push(std::shared_ptr<const VideoFrame> frame, Clock::time_point due);

    const QString m_path;
    const bool m_loop;
    const bool m_holdAtEnd;

    mutable std::mutex m_mutex; // schützt alles bis m_stats
    std::condition_variable m_wake; // Platz in der Warteschlange oder anhalten
    bool m_stop = false;
    std::deque<Queued> m_queue;
    std::shared_ptr<const VideoFrame> m_frame; // gezeigtes Bild
    Clock::time_point m_refillAt; // frühestens dann nachfüllen (kRefillDelay)
    bool m_shown = false;      // mindestens ein Bild gezeigt
    bool m_decoderDone = false; // Thread am Ende (oder Fehler)
    bool m_failed = false;
    QString m_error;
    QString m_decoderName;
    Statistics m_stats;
    std::atomic<uint64_t> m_serial{0};
    std::thread m_thread;
};

} // namespace dss
