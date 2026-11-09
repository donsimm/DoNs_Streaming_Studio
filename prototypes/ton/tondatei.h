// Ton einer Videodatei ganz in den Speicher dekodieren: float, 48 kHz, Stereo, verschachtelt.
// Probe 0 gehört zur Medienzeit 0 (nach den Zeitstempeln der Datei, nicht nach der ersten
// ausgegebenen Probe; siehe tests/daten/ton/LIESMICH.md zum Vorlauf von Opus).
#pragma once

#include <QString>
#include <vector>

struct TonDatei
{
    std::vector<float> proben; // L R L R …
    int quellRate = 0;
    int quellKanaele = 0;
    QString codec;
    double erstePts = 0.0; // Sekunden, Zeitstempel des ersten dekodierten Blocks
    qint64 frames() const { return qint64(proben.size()) / 2; }
};

bool tonLaden(const QString &pfad, TonDatei *aus, QString *fehler);

// Einsätze von Pieps (Betrag > Schwelle nach mindestens `ruheProben` Stille), in Proben
std::vector<qint64> einsaetzeFinden(const float *proben, qint64 frames, int kanaele, float schwelle,
                                    qint64 ruheProben);
