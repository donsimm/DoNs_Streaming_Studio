#pragma once

#include <QString>

namespace dss {

// Version des Programms, gesetzt in der obersten CMakeLists.txt (PROJECT_VERSION).
QString version();

// Zeile für --version, zum Beispiel «dss-core 0.1.0».
QString versionLine(const QString &programName);

} // namespace dss
