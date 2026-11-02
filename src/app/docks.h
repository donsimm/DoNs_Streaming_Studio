#pragma once

class QDockWidget;
class QWidget;

// Die vier Docks unten, wie auf docs/bilder/oberflaeche.png. In P05 leer bzw. ausgegraut,
// ohne Verbindung zu Daten. Jedes Dock hat einen festen objectName, damit
// QMainWindow::saveState() die Anordnung speichern kann.
QDockWidget *createOverlaysDock(QWidget *parent);
QDockWidget *createSourcesDock(QWidget *parent);
QDockWidget *createPropertiesDock(QWidget *parent);
QDockWidget *createControlDock(QWidget *parent);
