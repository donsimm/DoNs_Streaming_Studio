#pragma once

#include <QList>

class OverlayTree;
class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QWidget;
class SourceList;

// Die vier Docks unten, wie auf docs/bilder/oberflaeche.png. Jedes Dock hat einen festen
// objectName, damit QMainWindow::saveState() die Anordnung speichern kann. Seit P07 zeigen
// «Overlays», «Quellen» und «Eigenschaften» Daten vom Kern, seit P08 lassen sie sich bearbeiten
// (Schalter DSS_APP_EDITING). Hier wird nur aufgebaut; was die Knöpfe tun, verbindet MainWindow.
// Alle Knöpfe und Felder beginnen ausgegraut.

struct OverlaysDock
{
    QDockWidget *dock = nullptr;
    OverlayTree *tree = nullptr;
    QToolButton *add = nullptr;
    QToolButton *remove = nullptr;
    QToolButton *copy = nullptr;
};

struct SourcesDock
{
    QDockWidget *dock = nullptr;
    SourceList *list = nullptr;
    QToolButton *add = nullptr; // Menü: Bild, Video, Text
    QAction *addImage = nullptr;
    QAction *addVideo = nullptr;
    QAction *addText = nullptr;
    QToolButton *remove = nullptr;
    QToolButton *forward = nullptr;
    QToolButton *backward = nullptr;
};

struct PropertiesDock
{
    QDockWidget *dock = nullptr;
    QScrollArea *scroll = nullptr;
    // Für alle Arten
    QLineEdit *name = nullptr;
    QSpinBox *x = nullptr;
    QSpinBox *y = nullptr;
    QSpinBox *width = nullptr;
    QSpinBox *height = nullptr;
    QDoubleSpinBox *rotation = nullptr;
    QLabel *hint = nullptr;
    // Seite je Art: kEmptyPage, kImagePage, kVideoPage, kTextPage
    QStackedWidget *kindPages = nullptr;
    // Bild
    QPushButton *replaceImage = nullptr;
    // Video
    QCheckBox *loop = nullptr;
    // Text
    QPlainTextEdit *content = nullptr;
    QComboBox *family = nullptr;
    QComboBox *weight = nullptr;
    QCheckBox *italic = nullptr;
    QSpinBox *fontSize = nullptr;
    QToolButton *color = nullptr;
    QComboBox *align = nullptr; // Daten: dss::TextAlign als int
    // Ausrichten auf der Leinwand: bleibt in P08 ausgegraut.
    QList<QToolButton *> alignButtons;

    static constexpr int kEmptyPage = 0;
    static constexpr int kImagePage = 1;
    static constexpr int kVideoPage = 2;
    static constexpr int kTextPage = 3;
};

OverlaysDock createOverlaysDock(QWidget *parent);
SourcesDock createSourcesDock(QWidget *parent);
PropertiesDock createPropertiesDock(QWidget *parent);
QDockWidget *createControlDock(QWidget *parent);
