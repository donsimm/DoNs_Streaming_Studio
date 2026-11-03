#pragma once

#include <QString>

// Angaben zu einer Bild- oder Videodatei über libavformat/libavcodec (P09). Nur Qt Core.

namespace dss {

// Grösse in Pixeln (P09, Entscheid 6): Bild = Pixel der Datei, Video = Grösse des ersten
// dekodierten Bildes. false mit Meldung, wenn die Datei nicht lesbar ist oder kein Bild enthält.
// Dauert einige Millisekunden (öffnen, ein Bild dekodieren); blockiert den Aufrufer.
bool readMediaSize(const QString &path, int *width, int *height, QString *error);

} // namespace dss
