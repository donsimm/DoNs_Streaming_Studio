#pragma once

#include <QByteArray>

#include <chrono>

// Meldung an systemd für Type=notify (P07, Teil C): Der Kern meldet «READY=1», sobald er
// Verbindungen annimmt; erst dann kehrt «systemctl --user start» zurück. Eigene Umsetzung des
// Protokolls ohne libsystemd: ein Datagramm an den AF_UNIX-Socket aus $NOTIFY_SOCKET
// («@» am Anfang = abstrakter Namensraum). Laut systemd ist das Protokoll stabil.
// https://www.freedesktop.org/software/systemd/man/latest/sd_notify.html (Abschnitt Notes)

namespace dss {

// false, wenn NOTIFY_SOCKET nicht gesetzt ist (Start ohne systemd) oder das Senden scheitert.
bool notifySystemd(const QByteArray &state);

// Watchdog (P10a, Punkt 41) wie sd_watchdog_enabled(3): Zeit aus WATCHDOG_USEC (Mikrosekunden),
// nur wenn WATCHDOG_PID fehlt oder die eigene PID ist; sonst 0 (kein Watchdog). Der Dienst
// schickt dann etwa alle halbe Zeit «WATCHDOG=1».
// https://www.freedesktop.org/software/systemd/man/latest/sd_watchdog_enabled.html
std::chrono::microseconds watchdogTimeout(const QByteArray &usec, const QByteArray &pid,
                                          qint64 ownPid);
// Dasselbe mit den Umgebungsvariablen dieses Prozesses.
std::chrono::microseconds watchdogTimeout();

} // namespace dss
