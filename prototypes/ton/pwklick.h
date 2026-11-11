// Z3 (P12b, b1): Klickmessung wie M1, aber Ausgabe über pw_stream statt QAudioSink. Die Aufnahme
// läuft ebenfalls über pw_stream (Qt Multimedia ist in pw-messen ausgeschlossen), derselbe Messweg:
// virtuelle Senke dss_p12a_ausgabe → pw-loopback → virtuelle Quelle dss_p12a_aufnahme.
//
// Je Klick: Schreibzeit der ersten Probe (steady_clock im Abruf von PipeWire), gemeldete Zeitangaben
// des Stroms (pw_time: delay, queued, buffered, rate, ticks, size), daraus die erwartete hörbare Zeit,
// Einsatz in der Aufnahme und «Rest» = Einsatz − erwartete hörbare Zeit.
#pragma once

#include <QString>

#include <atomic>

struct PwKlickOptionen
{
    QString ausgang; // node.name der Senke, muss mit dss_p12a beginnen
    QString eingang; // node.name der Quelle, muss mit dss_p12a beginnen
    int latenzMs = 40; // node.latency beider Ströme
    int dauerS = 120; // Dauer ab Freigabe der Klicks
    int abstandMs = 500;
    QString aus; // Präfix: <aus>.csv, <aus>-aufrufe.csv, <aus>-zusammenfassung.txt
    QString marken; // Datei mit Zeilen «<epoch_ms> <Text>» (z. B. Wechsel des Quantums), leer = keine
    const std::atomic<bool> *ende = nullptr; // SIGTERM/SIGINT: Messung früher beenden und auswerten
};

// Ausgänge und Eingänge über die Registry (für --liste)
int pwListe();

int pwKlickMessung(const PwKlickOptionen &o);
