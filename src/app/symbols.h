#pragma once

#include <QIcon>
#include <QString>

// Lädt ein Symbol aus src/app/symbole/ zur Laufzeit, damit der Nutzer die Dateien ohne
// neuen Bau ersetzen kann. Fehlt die Datei, steht eine Warnung im Protokoll und das
// Ergebnis ist leer (der Knopf zeigt dann seinen Text).
QIcon loadSymbol(const QString &fileName);
