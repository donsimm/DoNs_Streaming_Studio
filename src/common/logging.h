#pragma once

#include <QString>

namespace dss {

// Einheitliches Format des Protokolls (stderr) für alle Programme.
void setupLogging();

// Erste Zeile des Protokolls: Name, Version des Programms, Qt-Version zur Laufzeit.
QString startLine(const QString &programName);

} // namespace dss
