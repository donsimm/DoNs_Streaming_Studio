// M1: Ausgabe im Umfeld des Kerns. Der Prototyp spielt Klicks über QAudioSink (Zugmodus, eigener
// Thread) auf einen Ausgang und nimmt sie im selben Prozess mit QAudioSource wieder auf. Ausgang und
// Eingang sind die beiden Seiten von pw-loopback (virtuelle Senke dss_p12a_ausgabe → virtuelle
// Quelle dss_p12a_aufnahme), weil Qt 6.8 keine Monitor-Quellen auflistet (gemessen 2026-11-09 mit
// --liste). Gemessen werden Startverzögerung, Streuung der Verzögerung, Aussetzer und Sprünge der
// Lage (Proben) zwischen geschriebenem und aufgenommenem Klick.
#pragma once

#include <QAudioDevice>
#include <QString>

struct KlickOptionen
{
    QString ausgang; // Teil der Kennung oder Beschreibung, leer = Standardausgang
    QString eingang; // dito für die Aufnahme
    int pufferMs = 20;
    int dauerS = 120;
    int abstandMs = 500;
    QString aus; // Präfix der Ausgabedateien (<aus>.csv, <aus>-zusammenfassung.txt)
};

// Ausgänge und Eingänge mit Kennung, Beschreibung, Standard und bevorzugtem Format
QString geraeteListe();
// Gerät nach Teil der Kennung oder Beschreibung; leer = Standard. isNull(), wenn nicht gefunden.
QAudioDevice ausgangSuchen(const QString &teil);
QAudioDevice eingangSuchen(const QString &teil);

int klickMessung(const KlickOptionen &o);

// Kurzprobe: nur Nullen mit Lautstärke 0 auf den Ausgang (unhörbar), Zustand und Takt melden
int stilleProbe(const QString &ausgang, int pufferMs, int dauerS);
