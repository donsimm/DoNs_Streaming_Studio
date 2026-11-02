#pragma once

#include <QSet>
#include <QString>

// Adresse eines Overlays für die Browserquelle (Entscheide 3 und 4 in P04).

namespace dss {

// Bildet die Adresse aus dem Namen: Kleinbuchstaben, ä/ö/ü → ae/oe/ue, andere Akzente
// ohne Akzent, alle übrigen Zeichen ausser a-z und 0-9 → «-», mehrere «-» → eines,
// «-» am Anfang und Ende fällt weg. Leeres Ergebnis → "overlay".
// Beispiel: «Überblick Start» → "ueberblick-start"
QString addressFromName(const QString &name);

// Gibt base zurück, wenn frei, sonst base-2, base-3 usw.
QString uniqueAddress(const QString &base, const QSet<QString> &taken);

} // namespace dss
