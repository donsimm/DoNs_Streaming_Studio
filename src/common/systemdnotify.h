#pragma once

#include <QByteArray>

// Meldung an systemd für Type=notify (P07, Teil C): Der Kern meldet «READY=1», sobald er
// Verbindungen annimmt; erst dann kehrt «systemctl --user start» zurück. Eigene Umsetzung des
// Protokolls ohne libsystemd: ein Datagramm an den AF_UNIX-Socket aus $NOTIFY_SOCKET
// («@» am Anfang = abstrakter Namensraum). Laut systemd ist das Protokoll stabil.
// https://www.freedesktop.org/software/systemd/man/latest/sd_notify.html (Abschnitt Notes)

namespace dss {

// false, wenn NOTIFY_SOCKET nicht gesetzt ist (Start ohne systemd) oder das Senden scheitert.
bool notifySystemd(const QByteArray &state);

} // namespace dss
