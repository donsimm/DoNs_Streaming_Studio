#pragma once

class QCoreApplication;
class QTranslator;

// Deutsche Texte von Qt selbst (Kontextmenü der Textfelder, Knöpfe der Dialoge) aus der beim Bau
// erzeugten Kopie von qtbase_de.qm ohne «ß» (P09, Punkt 44). nullptr, wenn die Datei fehlt
// (dann bleiben diese Texte englisch, mit Warnung im Protokoll).
QTranslator *installQtTranslation(QCoreApplication *app);
