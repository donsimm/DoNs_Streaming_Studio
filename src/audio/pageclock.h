#pragma once

#include "mixer.h"

#include <cstdint>
#include <deque>
#include <utility>

// Meldungen der Seite auf die Uhr des Kerns abbilden und glätten (P12b b4), nur Logik, ohne Qt
// (CLAUDE.md Regel 9). Offener Punkt aus b3: Die Meldungen kommen über das Netz mit schwankender
// Laufzeit; ohne Glätten führte der Mischer laufend weich nach.
//
// 1. PageClockMapper: Die Seite meldet Zeiten in performance.now() (ms, eigene Zeitachse je Seite).
//    Der Abstand «Eintreffen (steady_clock) − now_ms» ist Laufzeit plus fester Abstand der Uhren;
//    der kleinste Abstand der letzten kWindowNs kommt dem festen Abstand am nächsten (Laufzeit
//    kaum über null). Wie im Prototyp (prototypes/ton/folgen.h, M2). Gang zwischen den Uhren: Das
//    Fenster folgt ihm; bei 40 ppm (M2: −38,8 ppm) liegt der Fehler unter 0,4 ms.
// 2. PageClockSmoother: Jede Meldung gibt eine Lage (Medienzeit zu einem Zeitpunkt). Weicht sie
//    um höchstens kSmoothMaxNs von der Fortschreibung der vorigen ab, zählt nur 1/kDivisor der
//    Abweichung (Streuung der Meldungen fällt weg, Gang wird mit kleinem Rückstand nachgeführt:
//    bei 40 ppm und 1 Meldung je Sekunde etwa 0,3 ms). Grössere Abweichungen (Video hängt,
//    Sprung) gelten sofort. Ein neuer Durchgang (loop) beginnt neu.

namespace dss {

class PageClockMapper
{
public:
    static constexpr int64_t kWindowNs = 10'000'000'000;

    // Meldung eingetroffen (steady_clock, ns) mit now_ms der Seite.
    void add(int64_t arrivalNs, double pageNowMs);
    bool valid() const { return !m_samples.empty(); }
    // Abstand steady_clock − Zeit der Seite (ns); 0 ohne Meldung.
    int64_t offsetNs() const;
    int64_t toSteadyNs(double pageMs) const;

private:
    std::deque<std::pair<int64_t, int64_t>> m_samples; // Eintreffen, Abstand
};

class PageClockSmoother
{
public:
    static constexpr int64_t kSmoothMaxNs = 4'000'000;
    static constexpr int kDivisor = 8;

    void reset() { m_have = false; }
    PageClock apply(const PageClock &raw);

private:
    bool m_have = false;
    PageClock m_last;
};

} // namespace dss
