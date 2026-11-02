#pragma once

#include <QString>
#include <QStringList>

// Schrift des Fensters (Auftrag P06): Open Sans als Datei unter src/app/schriften/, nicht aus
// einer Installation. Herkunft und Lizenz: src/app/schriften/OFL.txt und
// docs/berichte/P06-bericht.md.

// Ordner der Schriftdateien (src/app/schriften/).
QString fontDir();

// Lädt alle .ttf- und .otf-Dateien aus dir mit QFontDatabase::addApplicationFont und gibt
// die Familiennamen zurück (jeden einmal). Braucht eine QGuiApplication. Was nicht geladen
// werden kann, steht als Warnung im Protokoll.
// https://doc.qt.io/qt-6.8/qfontdatabase.html#addApplicationFont
QStringList loadWindowFonts(const QString &dir);
