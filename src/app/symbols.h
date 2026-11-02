#pragma once

#include <QColor>
#include <QIcon>
#include <QImage>
#include <QSize>
#include <QString>

// Lädt ein Symbol zur Laufzeit, damit der Nutzer die Dateien ohne neuen Bau ersetzen kann.
// Gesucht wird zuerst im Ordner des Nutzers (~/.config/dons-streaming-studio/symbole/, P07),
// dann in src/app/symbole/ (symbolPath). Seit P06 wird jedes Symbol einfarbig gezeichnet: Die Datei dient
// nur als Maske, gefüllt wird mit der Textfarbe der Palette (ausgegraut mit der Farbe für
// ausgegrauten Text), egal welche Farben die Datei selbst hat. So folgen die Symbole dem
// Design (Punkt 28). Fehlt die Datei, steht eine Warnung im Protokoll und das Ergebnis ist
// leer (der Knopf zeigt dann seinen Text).
QIcon loadSymbol(const QString &fileName, const QSize &size);

// Zeichnet die Datei path in der Grösse size (Bildpunkte) und füllt jedes Pixel mit color;
// die Deckkraft jedes Pixels bleibt wie in der Datei. Leeres Bild, wenn die Datei nicht
// gelesen werden kann. Braucht eine QGuiApplication.
QImage tintedSymbol(const QString &path, const QSize &size, const QColor &color);

// Ordner der mitgelieferten Symbole (src/app/symbole/).
QString symbolDir();

// Pfad der Symboldatei fileName: aus dem Ordner des Nutzers (dss::userSymbolDir()), wenn dort
// eine Datei mit diesem Namen liegt, sonst aus symbolDir(). Auch für die Bilder im Stylesheet.
QString symbolPath(const QString &fileName);

// true, wenn der Nutzer eine eigene Datei fileName in seinem Symbolordner hat.
bool hasUserSymbol(const QString &fileName);
