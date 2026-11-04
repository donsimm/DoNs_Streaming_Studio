#pragma once

#include <QString>
#include <QStringList>

// Format der Dateien im Datenordner prüfen und umwandeln (P10a, Teil B 4). Nur Qt Core
// (CLAUDE.md Regel 9). Aufruf über dss-core --format-pruefen bzw. --umwandeln aus
// scripts/installieren.sh, nachdem das Skript den ganzen Datenordner gesichert hat.
//
// Geprüft werden die Dateien mit format_version: overlays/*.json (kOverlayFormatVersion),
// liste.json (kListFormatVersion) und tokens.json (kTokenFormatVersion). Umwandeln heisst: jede
// Overlay-Datei in älterem Format streng lesen und im aktuellen Format schreiben, genau wie es
// das Programm beim nächsten Speichern tut (docs/06-overlay-format.md, «Versionen»).
// Seit P10b auch medien.json (kMediaLibraryFormatVersion): Fehlt sie und liegen Dateien in
// media/, legt Umwandeln sie mit Ersatznamen an (als letzte Datei).

namespace dss {

struct DataFormatStatus
{
    // Je Datei «<Pfad im Datenordner> (Format N)»
    QStringList older;      // älter, wird umgewandelt
    QStringList newer;      // neuer, als dieses Programm lesen kann: nicht umwandeln, Fehler
    QStringList unreadable; // nicht lesbar oder ungültig; das Programm überspringt sie beim Laden
                            // ohne sie zu ändern (docs/06-overlay-format.md), Umwandeln auch
    QString error;          // Datenordner nicht lesbar
    bool needsConversion() const { return !older.isEmpty(); }
};

DataFormatStatus checkDataFormat(const QString &dataDir);

// Wandelt alle Dateien in älterem Format um. Nimmt die Sperre des Datenordners (läuft ein Kern,
// geht es nicht). Liest zuerst alle Dateien streng und schreibt erst, wenn alle gelesen sind;
// jede Datei atomar (QSaveFile). Scheitert das Schreiben einer Datei, werden die schon
// geschriebenen mit ihrem alten Inhalt zurückgeschrieben. false: *error nennt den Grund, die
// Dateien sind dann unverändert. *converted: die umgewandelten Dateien wie in older.
bool convertDataFormat(const QString &dataDir, QStringList *converted, QString *error);

} // namespace dss
