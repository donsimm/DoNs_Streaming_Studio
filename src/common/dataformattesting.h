#pragma once

// Testmöglichkeit für convertDataFormat (P10-vor, Punkt 70). Nur für tst_datenformat: Kein
// Programm bindet diese Datei ein, die Einstellung bleibt dort 0 und wirkt nicht.

namespace dss::testing {

// n ≥ 1: convertDataFormat scheitert beim Schreiben der n-ten umzuwandelnden Datei (in der
// Reihenfolge von DataFormatStatus::older), als ob QSaveFile sie nicht schreiben könnte; die
// Dateien davor sind dann schon geschrieben und werden zurückgeschrieben. 0 = aus.
void setConvertFailWriteAt(int n);

} // namespace dss::testing
