// Auswertung der Messungen: Kennzahlen (Quantile), Ausgleichsgerade, Auswertung der Meldungen
// der Messseite (M2, M3).
#pragma once

#include <QString>
#include <cstdint>
#include <vector>

struct Kennzahlen
{
    size_t n = 0;
    double min = 0, p5 = 0, median = 0, p95 = 0, max = 0;
};

Kennzahlen kennzahlen(std::vector<double> werte);
// «n=… min … p5 … Median … p95 … max … (Spanne …)» mit Einheit
QString kennzahlenText(const Kennzahlen &k, const char *einheit);

// Eine Meldung «bild» der Messseite (alle Zeiten der Seite in ms, wie performance.now())
struct BildMeldung
{
    int64_t ankunftNs = 0; // steady_clock beim Eintreffen
    double mediaTime = 0; // s
    double presentationTime = 0;
    double expectedDisplayTime = 0;
    double now = 0; // performance.now() beim Senden
    double dauer = 0; // s, Länge des Videos
    int64_t presentedFrames = 0;
    int schleife = 0; // Wiederholung seit dem letzten Start
    int lauf = 0; // Start (in OBS: jedes «aktiv»)
};

// Schwankung (Abweichung von der Geraden, je Wiederholung eigene Lage), Gang (gemeinsame Steigung
// gegen mediaTime, ppm und ms/min), Sprung beim Wiederholen, Lücken in presentedFrames.
QString seitenAuswertung(const std::vector<BildMeldung> &meldungen);
