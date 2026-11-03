#pragma once

#include <QByteArray>
#include <QString>

// Übersetzungsdatei von Qt (.qm) ohne «ß» (P09, Punkt 44): Die Übersetzung von Qt
// (qtbase_de.qm) wird beim Bau kopiert, «ß» durch «ss» ersetzt und vom Fenster geladen
// (CLAUDE.md Regel 13). Nur Qt Core.
//
// Aufbau der Datei nach dem Lesecode von Qt 6.8.2 (src/corelib/kernel/qtranslator.cpp,
// https://github.com/qt/qtbase/blob/v6.8.2/src/corelib/kernel/qtranslator.cpp): 16 Byte
// Kennung, danach Blöcke (1 Byte Art, 4 Byte Länge big endian, Inhalt). Block «Messages» (0x69)
// enthält Einträge aus Feldern (1 Byte Art, je nach Art 4 Byte Länge und Inhalt), jeder Eintrag
// endet mit Tag_End (1). Block «Hashes» (0x42) enthält Paare (Hash, Stelle des Eintrags im Block
// Messages). Übersetzungen (Tag_Translation, 3) stehen in UTF-16 big endian. Ändert sich deren
// Länge, verschieben sich die Einträge; die Stellen in «Hashes» werden neu gesetzt. Die Hashes
// selbst hängen nur am Quelltext und bleiben gleich.

namespace dss {

// Ersetzt in allen Übersetzungen «ß» durch «ss» und «ẞ» (U+1E9E) durch «SS». *replaced (falls
// nicht nullptr) bekommt die Zahl ersetzter Zeichen. false mit Meldung bei unbekanntem Aufbau;
// dann ist das Ergebnis leer.
bool replaceSharpS(const QByteArray &qm, QByteArray *result, QString *error, int *replaced = nullptr);

} // namespace dss
