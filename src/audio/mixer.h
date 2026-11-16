#pragma once

#include "audioring.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Mischer für einen Ausgang (P12b b2), nur Logik, ohne Gerät und ohne Qt (CLAUDE.md Regel 9).
// Je Quelle eine Stelle mit eigenem Ring (audioring.h), in den ein Decoder schreibt. Der Ausgang
// (b3: pw_stream) ruft render() im Echtzeit-Thread auf; render() sperrt nicht, legt keinen
// Speicher an und macht keinen Systemaufruf. Steuerung (start, report, stop, …) kommt aus einem
// anderen Thread, immer demselben, über Puffer ohne Sperren (dreifacher Puffer je Stelle).
//
// Lage und hörbarer Zeitpunkt (Plan P12, Abschnitt «b2», nachgeführt nach Z3 am 2026-11-12):
// - Der Mischer zählt die übergebenen Rahmen selbst (Lage in Proben, outputStats().frames).
// - Den hörbaren Zeitpunkt der ersten Probe eines Abrufs rechnet er aus den Werten, die der
//   Ausgang je Abruf übergibt (OutputTiming, Felder aus struct pw_time), siehe audibleNs(). Darin
//   steht die Zyklusgrösse `size` (Lesart «Rest + Quantum» aus der Neuauswertung von Z3); ob
//   `delay` an einem echten Ausgang einen Wert hat, bestätigt erst M4.
// - Springt dieser Zeitpunkt gegen die Fortschreibung des vorigen Abrufs um mehr als
//   kOutputJumpNs (Wechsel des Quantums, Änderung von `delay` oder `queued`), gilt das als
//   gemeldeter Sprung des Ausgangs: Alle Quellen springen sofort an die richtige Lage.
//
// Lage einer Quelle: Die Seite meldet, welche Medienzeit das Video zu welchem Zeitpunkt zeigte
// (PageClock). Daraus folgt für den hörbaren Zeitpunkt jedes Abrufs die Soll-Lage im Ton. Weicht
// die Lage ab:
// - ab kHardNs (20 ms, Plan) Sprung: Proben verwerfen (Ton zu spät) bzw. Stille einfügen (Ton zu
//   früh), danach kurz einblenden (kRampFrames);
// - ab kSoftStartNs weich: Abspielen um kSoftRatio schneller bzw. langsamer, bis die Abweichung
//   unter kSoftStopNs liegt (Interpolation mit 4 Punkten nur in dieser Zeit, sonst Proben genau).
//
// Wiederholen: Mit loop und der Länge, die die Seite meldet (setLength), springt die Quelle nach
// der Länge an den Anfang des nächsten Durchgangs im Ring, nach einer Pause (Startwert
// kLoopGapStartNs aus M2). Meldet die Seite danach die Wiederholung, wird die Pause aus der
// Änderung der Abweichung nachgeführt (nicht während weichem Nachführen); die Abweichung selbst
// wird nach den Regeln oben ausgeglichen. Meldet die Seite eine Wiederholung, bevor die Quelle sie
// erreicht hat, springt die Quelle sofort. Hinter der Länge liest die Quelle nichts, auch nicht für
// die Interpolation; ist die Tonspur kürzer, bleibt es bis zur Länge still.
//
// Freie Stimme (P14 t, Plan P14 Abschnitt 4.3, Töne des Chat-Bots): startFree() spielt einen
// Durchgang ab dem nächsten Abruf von vorne, Probe für Probe, ohne Uhr einer Seite, ohne Nachführen
// und ohne Versatz. Lautstärke bis kMaxFreeGain (200 %); jede freie Stimme und die Summe des Abrufs
// sind auf ±1,0 begrenzt. Das Ende des Durchgangs (nächste Marke im Ring) meldet sourceStats()
// mit ended; ist der Ring leer, weil der Decoder fertig ist, sieht der Aufrufer das am Decoder
// (gespielte Rahmen = geschriebene Rahmen, ActionSound).

namespace dss {

// Werte des Ausgangs je Abruf, aus struct pw_time (pipewire/stream.h, b3).
struct OutputTiming
{
    int64_t callNs = 0;     // Abrufzeit, steady_clock (CLOCK_MONOTONIC) in ns
    int64_t sinceNowNs = 0; // seit pw_time.now vergangen (pw_stream_get_nsec() − now), ns
    uint64_t size = 0;      // pw_time.size: Zyklusgrösse in Rahmen des Stroms
    int64_t delay = 0;      // pw_time.delay: Verzögerung bis zum Gerät, in rateNum/rateDenom
    uint32_t rateNum = 0;   // pw_time.rate; rateDenom 0 = ungültig, delay zählt dann nicht
    uint32_t rateDenom = 0;
    uint64_t queued = 0;    // pw_time.queued: wartende Rahmen im Strom
    uint64_t buffered = 0;  // pw_time.buffered: Rahmen im Konverter
};

// Meldung der Seite: Medienzeit des Videos zu einem Zeitpunkt in steady_clock des Kerns.
struct PageClock
{
    int64_t mediaNs = 0;
    int64_t steadyNs = 0;
    uint32_t loop = 0; // Durchgang des Videos seit dem Start (0 = erster)
};

class Mixer
{
public:
    static constexpr int kRate = 48000;
    static constexpr int kChannels = AudioRing::kChannels;
    // Ein- und Ausblenden bei Start, Stopp, Lautstärke und Sprung: 5 ms
    static constexpr int kRampFrames = 240;
    // Grenzen des Nachführens (Plan: unter 20 ms weich, darüber Sprung)
    static constexpr int64_t kHardNs = 20'000'000;
    static constexpr int64_t kSoftStartNs = 2'000'000;
    static constexpr int64_t kSoftStopNs = 500'000;
    static constexpr double kSoftRatio = 0.002;
    // Sprung des hörbaren Zeitpunkts gegen die Fortschreibung, ab dem er als gemeldeter Sprung des
    // Ausgangs gilt. Z3: Streuung des Rests in Ruhe p5–p95 0,067 ms.
    static constexpr int64_t kOutputJumpNs = 2'000'000;
    // Pause des Videos beim Wiederholen: Startwert aus M2 (Median 58,59 ms), nachgeführt
    static constexpr int64_t kLoopGapStartNs = 58'600'000;
    static constexpr int64_t kLoopGapMaxNs = 500'000'000;
    // Grösste Lautstärke einer freien Stimme (Plan P14, 4.3: 0 bis 200 %)
    static constexpr float kMaxFreeGain = 2.0f;

    struct SourceStats
    {
        bool playing = false;
        bool ended = false;          // freie Stimme: Durchgang zu Ende (nächste Marke erreicht)
        uint32_t pass = 0;           // Durchgang im Ring, der gerade spielt
        uint32_t loops = 0;          // Wiederholungen seit dem Start
        uint64_t frames = 0;         // gespielte Rahmen mit Daten aus dem Ring
        uint64_t emptyFrames = 0;    // Rahmen, für die der Ring leer war (Decoder zu langsam oder Ende)
        uint32_t hardCorrections = 0; // Sprünge (auch beim Start und bei Sprüngen des Ausgangs)
        uint32_t softCorrections = 0;
        int64_t lastDeviationNs = 0; // Lage minus Soll-Lage im letzten Abruf (positiv: Ton zu früh)
        int64_t loopGapNs = 0;       // Pause beim Wiederholen, wie nachgeführt
    };

    struct OutputStats
    {
        uint64_t frames = 0;      // übergebene Rahmen (Lage in Proben)
        uint64_t pulls = 0;       // Abrufe
        int64_t audibleNs = 0;    // hörbarer Zeitpunkt der ersten Probe des letzten Abrufs
        uint32_t outputJumps = 0; // gemeldete Sprünge des Ausgangs
    };

    explicit Mixer(int maxSources = 8, size_t ringFrames = size_t(1) << 15);
    ~Mixer();
    Mixer(const Mixer &) = delete;
    Mixer &operator=(const Mixer &) = delete;

    // --- Steuerung (ein Thread) ---
    // Freie Stelle für eine Quelle, -1 wenn alle belegt.
    int addSource();
    // Gibt die Stelle frei; die Quelle verstummt sofort (Ausblenden). Den Decoder der Stelle
    // zerstört der Aufrufer, bevor er die Stelle wieder belegt.
    void removeSource(int id);
    // Ring der Stelle, für den Decoder (AudioDecoder).
    AudioRing *ring(int id);
    // Start von vorne mit Durchgang `pass` (AudioDecoder::firstPass oder restart) und der ersten
    // Meldung der Seite. Ältere Durchgänge im Ring werden verworfen.
    void start(int id, uint32_t pass, const PageClock &clock);
    // Nach restart() des Decoders ohne Start: Durchgänge vor `pass` verwerfen, damit der Vorrat für
    // den nächsten Start im Ring Platz hat.
    void prepare(int id, uint32_t pass);
    // Neue Meldung der Seite (Lage etwa jede Sekunde, Wiederholung mit loop + 1).
    void report(int id, const PageClock &clock);
    // P14 t: freie Stimme mit Durchgang `pass` (AudioDecoder::firstPass), Start beim nächsten Abruf.
    void startFree(int id, uint32_t pass);
    void stop(int id);
    // gain 0 bis 1 (linear); freie Stimme 0 bis kMaxFreeGain
    void setVolume(int id, float gain, bool muted);
    // Länge des Videos in Rahmen, die die Seite meldet (0 = unbekannt), und ob es sich wiederholt.
    void setLength(int id, int64_t frames, bool loop);
    // Versatz: so viel ist der Ton hinter dem Ausgang noch unterwegs (Weg über das Mischpult).
    void setExtraDelayNs(int64_t ns) { m_extraDelayNs.store(ns, std::memory_order_relaxed); }

    // --- Abruf des Ausgangs (Echtzeit) ---
    // Schreibt `frames` Rahmen float Stereo verschränkt nach out.
    void render(const OutputTiming &timing, float *out, uint32_t frames);

    // --- Zähler (jeder Thread) ---
    SourceStats sourceStats(int id) const;
    OutputStats outputStats() const;

    // Hörbarer Zeitpunkt der ersten Probe des Abrufs in steady_clock (ns), ohne Versatz:
    // Abrufzeit − sinceNow + (buffered + queued + size) / 48 kHz + delay · rate.
    // Nach pipewire/stream.h (struct pw_time, Formel «total delay»), ergänzt um `size` nach Z3.
    static int64_t audibleNs(const OutputTiming &timing);

private:
    struct Slot;

    std::vector<std::unique_ptr<Slot>> m_slots;
    std::vector<bool> m_used; // nur Steuerung
    std::atomic<int64_t> m_extraDelayNs{0};

    // nur Abruf
    bool m_havePrediction = false;
    int64_t m_predictedNs = 0;

    std::atomic<uint64_t> m_frames{0};
    std::atomic<uint64_t> m_pulls{0};
    std::atomic<int64_t> m_audibleNs{0};
    std::atomic<uint32_t> m_outputJumps{0};
};

} // namespace dss
