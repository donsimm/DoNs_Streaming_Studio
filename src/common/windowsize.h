#pragma once

#include <QSize>
#include <QString>

namespace dss {

// Liest eine Fenstergrösse «BREITExHOEHE» in Pixeln, zum Beispiel «1920x1080» (für
// dss-app --fenstergroesse). Beide Werte 1 bis 10000. false bei jeder anderen Angabe;
// size bleibt dann unverändert. Nur Qt Core (CLAUDE.md Regel 9).
bool parseWindowSize(const QString &text, QSize *size);

} // namespace dss
