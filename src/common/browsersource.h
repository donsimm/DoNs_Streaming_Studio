#pragma once

#include <QString>

// Adresse der Browserquelle eines Overlays (P10, Teil A 4; docs/08-kern-schnittstelle.md,
// «Overlay-Server»): http://<Adresse im Heimnetz>:<Port>/o/<adresse>/<token>/
// Alles, was die Seite nachlädt, liegt unter diesem Pfad. Die Adresse mit Token kommt nie ins
// Protokoll (CLAUDE.md Regel 12); dafür gibt es browserSourceLogPath().

namespace dss {

// "/o/<adresse>/<token>/"
QString browserSourcePath(const QString &address, const QString &token);
// Ganze Adresse; host ist eine IPv4-Adresse wie 192.168.1.9.
QString browserSourceUrl(const QString &host, int port, const QString &address, const QString &token);
// Pfad für das Protokoll: "/o/<adresse>/…" (Token ausgelassen).
QString browserSourceLogPath(const QString &address);

} // namespace dss
