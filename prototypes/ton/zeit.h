// Gemeinsame Zeitbasis des Prototyps: std::chrono::steady_clock (wie der Kern) in Nanosekunden,
// dazu die Wanduhr in ms für den Abgleich mit dem Protokoll von nachtlauf.sh (date +%s%3N).
#pragma once

#include <chrono>
#include <cstdint>

inline int64_t steadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

inline int64_t epochMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Format aller Ausgaben und Aufnahmen: float, 48 kHz, Stereo (verschachtelt L R)
constexpr int kRate = 48000;
constexpr int kKanaele = 2;
