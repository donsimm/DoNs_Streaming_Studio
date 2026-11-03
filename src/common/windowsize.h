#pragma once

#include <QSize>
#include <QString>

namespace dss {

// Liest eine Fenstergrösse «BREITExHOEHE» in Pixeln, zum Beispiel «1920x1080» (für
// dss-app --fenstergroesse). Beide Werte 1 bis 10000. false bei jeder anderen Angabe;
// size bleibt dann unverändert. Nur Qt Core (CLAUDE.md Regel 9).
bool parseWindowSize(const QString &text, QSize *size);

// Leistungsanzeige im Kopfbereich (CPU, RAM, LAN), Entscheid 2 in P09b: zusammen fest 600 Pixel
// (200 je Anzeige), rechts im Kopfbereich; wäre das mehr als ein Drittel der Fensterbreite, ein
// Drittel (abgerundet). Ein Fenster ohne Breite (0 oder weniger) gibt 0.
inline constexpr int kHeaderGaugesWidth = 600;
inline constexpr int kHeaderGaugeCount = 3;
int headerGaugesWidth(int windowWidth);
// Breite der Anzeige index (0 bis 2) bei der Gesamtbreite total; ein Rest der Teilung geht an die
// vorderen Anzeigen, die Summe ist immer total.
int headerGaugeWidth(int total, int index);

} // namespace dss
