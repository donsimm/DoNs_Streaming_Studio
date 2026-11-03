#pragma once

#include <QString>

namespace dss {

// Version des Programms, gesetzt in der obersten CMakeLists.txt (PROJECT_VERSION).
QString version();

// Commit, aus dem gebaut wurde (Schalter DSS_COMMIT, gesetzt von scripts/installieren.sh);
// leer, wenn nicht gesetzt.
QString commit();

// Version des Baus und Commit für Protokoll und --version: «Entwicklung» bzw.
// «Einsatz, Commit 0036f8f».
QString buildDetails();

// Zeile für --version, zum Beispiel «dss-core 0.1.0 (Entwicklung)» oder
// «dss-core 0.1.0 (Einsatz, Commit 0036f8f)».
QString versionLine(const QString &programName);

} // namespace dss
