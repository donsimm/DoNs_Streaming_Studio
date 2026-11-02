#pragma once

class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QWidget;

// Die vier Docks unten, wie auf docs/bilder/oberflaeche.png. Jedes Dock hat einen festen
// objectName, damit QMainWindow::saveState() die Anordnung speichern kann. Seit P07 zeigen
// «Overlays», «Quellen» und «Eigenschaften» Daten vom Kern (nur Anzeige); die Knöpfe und
// Felder bleiben ausgegraut (Bearbeiten kommt mit P08).

struct OverlaysDock
{
    QDockWidget *dock = nullptr;
    QListWidget *list = nullptr;
};

struct SourcesDock
{
    QDockWidget *dock = nullptr;
    QListWidget *list = nullptr;
};

struct PropertiesDock
{
    QDockWidget *dock = nullptr;
    QLineEdit *name = nullptr;
    QSpinBox *x = nullptr;
    QSpinBox *y = nullptr;
    QSpinBox *width = nullptr;
    QSpinBox *height = nullptr;
    QDoubleSpinBox *rotation = nullptr;
    QLabel *hint = nullptr;
};

OverlaysDock createOverlaysDock(QWidget *parent);
SourcesDock createSourcesDock(QWidget *parent);
PropertiesDock createPropertiesDock(QWidget *parent);
QDockWidget *createControlDock(QWidget *parent);
